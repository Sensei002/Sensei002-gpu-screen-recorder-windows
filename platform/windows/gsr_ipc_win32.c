/* gsr_ipc_win32.c — Windows named-pipe transport for the engine IPC API
 * (Phase 11). Implements the gsr_ipc interface from upstream/include/cli/
 * ipc.h over \\\\.\\pipe\\gsr-<name> byte-stream pipes, replacing the
 * upstream Unix domain socket (upstream/src/cli/ipc.c, not built here).
 *
 * The semantics are byte-identical to upstream:
 *   - newline-terminated JSON requests/replies (the platform codec
 *     gsr_platform_ipc_build_reply matches upstream ipc_client_send_reply)
 *   - request dispatch to the gsr_ipc_handlers
 *   - deferred requests (stop / save-replay / stop-replay-recording) are
 *     replied to only when gsr_ipc_complete_request fires (from the
 *     recorder callbacks)
 *   - identical error messages ("unknown request name '%s'", the
 *     already-pending messages, "GPU Screen Recorder exited before the
 *     request finished", ...)
 *
 * Transport model: one IPC thread (like upstream) that waits on the
 * shutdown event, the listen-pipe connect OVERLAPPED event, and each
 * client's read OVERLAPPED event. All writes happen on the IPC thread, so
 * there are no cross-thread pipe writes (the recorder thread only flips
 * deferred state + signals via gsr_ipc_complete_request).
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3q.
 */
#include "../../upstream/include/cli/ipc.h"
#include "../../upstream/include/recorder/replay_save.h" /* GSR_SAVE_REPLAY_SECONDS_FULL */
#include "../../upstream/include/recorder/error.h" /* GSR_ERROR_* */
#include "../../upstream/include/json.h"
#include "../../upstream/include/log.h"
#include "../../platform/include/ipc.h" /* protocol codec (client side) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <errno.h>
#include <pthread.h>

#define GSR_IPC_WIN32_PIPE_PREFIX "\\\\.\\pipe\\"
#define GSR_IPC_WIN32_MAX_PIPE_NAME 240 /* minus prefix + NUL < 256 */

/* Local re-creation of upstream's parsed-request view (ipc.c keeps it
   internal). */
typedef struct {
    int64_t id;
    char name[GSR_IPC_MAX_REQUEST_SIZE];
    sj_Value data;
    bool has_data;
    const char *request_end;
} gsr_ipc_request;

/* ------------------------------------------------------------------ */
/* request parsing (mirrors upstream src/cli/ipc.c byte for byte)      */
/* ------------------------------------------------------------------ */

static bool ipc_request_parse(char *data, size_t size, gsr_ipc_request *request, char *error_message, size_t error_message_size) {
    memset(request, 0, sizeof(*request));
    request->request_end = data + size;

    sj_Reader reader = sj_reader(data, size);
    const sj_Value root = sj_read(&reader);
    if(root.type != SJ_OBJECT) {
        snprintf(error_message, error_message_size, "expected the request to be a json object");
        return false;
    }

    sj_Value id_value = {0};
    sj_Value name_value = {0};
    bool has_id = false;
    bool has_name = false;

    sj_Value key;
    sj_Value value;
    while(sj_iter_object(&reader, root, &key, &value)) {
        if(gsr_json_string_equals(&key, "id")) {
            id_value = value;
            has_id = true;
        } else if(gsr_json_string_equals(&key, "name")) {
            name_value = value;
            has_name = true;
        } else if(gsr_json_string_equals(&key, "data")) {
            request->data = value;
            request->has_data = true;
        }
    }

    if(reader.error) {
        snprintf(error_message, error_message_size, "failed to parse the request: %s", reader.error);
        return false;
    }

    if(!has_id) {
        snprintf(error_message, error_message_size, "the request is missing the 'id' field");
        return false;
    }

    if(!gsr_json_number_to_int64(&id_value, &request->id)) {
        snprintf(error_message, error_message_size, "expected 'id' to be an integer");
        return false;
    }

    if(!has_name) {
        snprintf(error_message, error_message_size, "the request is missing the 'name' field");
        return false;
    }

    if(name_value.type != SJ_STRING) {
        snprintf(error_message, error_message_size, "expected 'name' to be a string");
        return false;
    }

    snprintf(request->name, sizeof(request->name), "%.*s", (int)(name_value.end - name_value.start), name_value.start);
    return true;
}

static bool json_value_to_save_replay_seconds(const sj_Value *value, int *seconds, char *error_message, size_t error_message_size) {
    int64_t data_seconds = 0;
    if(!gsr_json_number_to_int64(value, &data_seconds) || data_seconds <= 0 || data_seconds > INT_MAX) {
        snprintf(error_message, error_message_size, "expected the number of seconds to save to be larger than 0");
        return false;
    }

    *seconds = (int)data_seconds;
    return true;
}

static bool ipc_request_get_save_replay_options(const gsr_ipc_request *request, int *seconds, bool *has_restart_replay, bool *restart_replay, char *error_message, size_t error_message_size) {
    *seconds = GSR_SAVE_REPLAY_SECONDS_FULL;
    *has_restart_replay = false;
    *restart_replay = false;
    if(!request->has_data || request->data.type == SJ_NULL)
        return true;

    if(request->data.type != SJ_OBJECT) {
        snprintf(error_message, error_message_size, "expected 'data' to be an object with the optional fields 'seconds' and 'restart-replay'");
        return false;
    }

    sj_Reader reader = sj_reader(request->data.start, request->request_end - request->data.start);
    const sj_Value data = sj_read(&reader);

    sj_Value key;
    sj_Value value;
    while(sj_iter_object(&reader, data, &key, &value)) {
        if(gsr_json_string_equals(&key, "seconds")) {
            if(value.type == SJ_NULL)
                continue;

            if(!json_value_to_save_replay_seconds(&value, seconds, error_message, error_message_size))
                return false;
        } else if(gsr_json_string_equals(&key, "restart-replay")) {
            if(value.type != SJ_BOOL) {
                snprintf(error_message, error_message_size, "expected 'restart-replay' to be true or false");
                return false;
            }

            *has_restart_replay = true;
            *restart_replay = gsr_json_string_equals(&value, "true");
        }
    }

    if(reader.error) {
        snprintf(error_message, error_message_size, "failed to parse 'data': %s", reader.error);
        return false;
    }

    return true;
}

static bool ipc_request_get_set_paused_state(const gsr_ipc_request *request, bool *paused, char *error_message, size_t error_message_size) {
    if(request->has_data && request->data.type == SJ_BOOL) {
        *paused = gsr_json_string_equals(&request->data, "true");
        return true;
    }

    snprintf(error_message, error_message_size, "expected 'data' to be true to pause or false to unpause");
    return false;
}

/* ------------------------------------------------------------------ */
/* deferred requests (mirrors upstream)                                */
/* ------------------------------------------------------------------ */

static bool ipc_request_name_to_deferred_request_type(const char *name, gsr_ipc_deferred_request_type *type) {
    if(strcmp(name, "stop") == 0) {
        *type = GSR_IPC_DEFERRED_REQUEST_STOP;
        return true;
    }

    if(strcmp(name, "save-replay") == 0) {
        *type = GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY;
        return true;
    }

    if(strcmp(name, "stop-replay-recording") == 0) {
        *type = GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING;
        return true;
    }

    return false;
}

static const char* deferred_request_already_pending_error(gsr_ipc_deferred_request_type type) {
    switch(type) {
        case GSR_IPC_DEFERRED_REQUEST_STOP:                  return "GPU Screen Recorder is already stopping";
        case GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY:           return "a replay is already being saved";
        case GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING: return "the recording is already being stopped";
        case GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT:            break;
    }
    return "the request is already being handled";
}

static const char* deferred_request_failed_error(gsr_ipc_deferred_request_type type) {
    switch(type) {
        case GSR_IPC_DEFERRED_REQUEST_STOP:                  return "failed to save the recording";
        case GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY:           return "failed to save the replay";
        case GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING: return "failed to save the recording";
        case GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT:            break;
    }
    return "the request failed";
}

static bool ipc_set_deferred_request_pending(gsr_ipc *self, gsr_ipc_deferred_request_type type, HANDLE client_pipe, int64_t request_id) {
    pthread_mutex_lock(&self->deferred_requests_mutex);
    gsr_ipc_deferred_request *deferred_request = &self->deferred_requests[type];
    const bool was_empty = deferred_request->state == GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
    if(was_empty) {
        deferred_request->state = GSR_IPC_DEFERRED_REQUEST_STATE_PENDING;
        deferred_request->client_fd = (intptr_t)client_pipe;
        deferred_request->request_id = request_id;
        deferred_request->success = false;
        deferred_request->has_filepath = false;
    }
    pthread_mutex_unlock(&self->deferred_requests_mutex);
    return was_empty;
}

static void ipc_clear_deferred_request(gsr_ipc *self, gsr_ipc_deferred_request_type type) {
    pthread_mutex_lock(&self->deferred_requests_mutex);
    self->deferred_requests[type].state = GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
    pthread_mutex_unlock(&self->deferred_requests_mutex);
}

/* ------------------------------------------------------------------ */
/* client handling                                                     */
/* ------------------------------------------------------------------ */

static bool string_is_only_whitespace(const char *str, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(str[i] != ' ' && str[i] != '\t' && str[i] != '\r')
            return false;
    }
    return true;
}

static bool ipc_write_all(HANDLE pipe, const char *data, size_t size) {
    size_t offset = 0;
    while(offset < size) {
        DWORD bytes_written = 0;
        if(!WriteFile(pipe, data + offset, (DWORD)(size - offset), &bytes_written, NULL) || bytes_written == 0)
            return false;
        offset += bytes_written;
    }
    return true;
}

static bool ipc_client_send_reply(gsr_ipc *self, gsr_ipc_client *client, int64_t id, bool success, const char *error_message, const char *data) {
    (void)self;
    char reply[GSR_IPC_MAX_REPLY_SIZE];
    int reply_size = 0;

    if(success && data) {
        char escaped_data[GSR_IPC_MAX_ESCAPED_DATA_SIZE];
        gsr_json_escape_string(escaped_data, sizeof(escaped_data), data);
        reply_size = snprintf(reply, sizeof(reply), "{\"id\":%" PRIi64 ",\"result\":\"ok\",\"data\":\"%s\"}\n", id, escaped_data);
    } else if(success) {
        reply_size = snprintf(reply, sizeof(reply), "{\"id\":%" PRIi64 ",\"result\":\"ok\"}\n", id);
    } else {
        char escaped_error_message[GSR_IPC_MAX_ESCAPED_ERROR_MESSAGE_SIZE];
        gsr_json_escape_string(escaped_error_message, sizeof(escaped_error_message), error_message ? error_message : "");
        reply_size = snprintf(reply, sizeof(reply), "{\"id\":%" PRIi64 ",\"result\":\"error\",\"data\":\"%s\"}\n", id, escaped_error_message);
    }

    if(reply_size < 0 || reply_size >= (int)sizeof(reply)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to create a reply to request %" PRIi64, id);
        return false;
    }

    return ipc_write_all(client->pipe, reply, (size_t)reply_size);
}

static bool ipc_handle_request(gsr_ipc *self, const gsr_ipc_request *request, char *error_message, size_t error_message_size) {
    if(strcmp(request->name, "stop") == 0)
        return self->handlers.stop(error_message, error_message_size, self->handlers.userdata);

    if(strcmp(request->name, "toggle-pause") == 0)
        return self->handlers.toggle_pause(error_message, error_message_size, self->handlers.userdata);

    if(strcmp(request->name, "set-paused") == 0) {
        bool paused = false;
        if(!ipc_request_get_set_paused_state(request, &paused, error_message, error_message_size))
            return false;

        return self->handlers.set_paused(paused, error_message, error_message_size, self->handlers.userdata);
    }

    if(strcmp(request->name, "toggle-replay-recording") == 0)
        return self->handlers.toggle_replay_recording(error_message, error_message_size, self->handlers.userdata);

    if(strcmp(request->name, "start-replay-recording") == 0)
        return self->handlers.start_replay_recording(error_message, error_message_size, self->handlers.userdata);

    if(strcmp(request->name, "stop-replay-recording") == 0)
        return self->handlers.stop_replay_recording(error_message, error_message_size, self->handlers.userdata);

    if(strcmp(request->name, "save-replay") == 0) {
        int seconds = GSR_SAVE_REPLAY_SECONDS_FULL;
        bool has_restart_replay = false;
        bool restart_replay = false;
        if(!ipc_request_get_save_replay_options(request, &seconds, &has_restart_replay, &restart_replay, error_message, error_message_size))
            return false;

        return self->handlers.save_replay(seconds, has_restart_replay, restart_replay, error_message, error_message_size, self->handlers.userdata);
    }

    snprintf(error_message, error_message_size, "unknown request name '%s'", request->name);
    return false;
}

static bool ipc_client_on_request(gsr_ipc *self, gsr_ipc_client *client) {
    if(client->request_too_large)
        return ipc_client_send_reply(self, client, 0, false, "the request is too large", NULL);

    if(string_is_only_whitespace(client->request, client->request_size))
        return ipc_client_send_reply(self, client, 0, false, "the request is empty", NULL);

    char error_message[GSR_IPC_MAX_ERROR_MESSAGE_SIZE];
    error_message[0] = '\0';

    gsr_ipc_request request;
    if(!ipc_request_parse(client->request, client->request_size, &request, error_message, sizeof(error_message)))
        return ipc_client_send_reply(self, client, request.id, false, error_message, NULL);

    /* The pending deferred request has to be registered before the handler
       starts the operation, otherwise the operation could finish before the
       reply to it gets registered. */
    gsr_ipc_deferred_request_type deferred_request_type;
    const bool reply_is_deferred = ipc_request_name_to_deferred_request_type(request.name, &deferred_request_type);
    if(reply_is_deferred && !ipc_set_deferred_request_pending(self, deferred_request_type, client->pipe, request.id))
        return ipc_client_send_reply(self, client, request.id, false, deferred_request_already_pending_error(deferred_request_type), NULL);

    if(!ipc_handle_request(self, &request, error_message, sizeof(error_message))) {
        if(reply_is_deferred)
            ipc_clear_deferred_request(self, deferred_request_type);
        return ipc_client_send_reply(self, client, request.id, false, error_message, NULL);
    }

    if(reply_is_deferred)
        return true;

    return ipc_client_send_reply(self, client, request.id, true, NULL, NULL);
}

/* Processes |bytes_read| newly received bytes. Returns false when the
   client should be disconnected. */
static bool ipc_client_on_bytes(gsr_ipc *self, gsr_ipc_client *client, const char *buffer, size_t bytes_read) {
    for(size_t i = 0; i < bytes_read; ++i) {
        const char c = buffer[i];
        if(c != '\n') {
            if(client->request_size < GSR_IPC_MAX_REQUEST_SIZE)
                client->request[client->request_size++] = c;
            else
                client->request_too_large = true;
            continue;
        }

        const bool keep_client = ipc_client_on_request(self, client);
        client->request_size = 0;
        client->request_too_large = false;
        if(!keep_client)
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* server loop                                                         */
/* ------------------------------------------------------------------ */

static int ipc_find_client_index_by_pipe(const gsr_ipc *self, HANDLE pipe) {
    for(int i = 0; i < self->num_clients; ++i) {
        if(self->clients[i].pipe == pipe)
            return i;
    }
    return -1;
}

static void ipc_remove_client(gsr_ipc *self, int index) {
    gsr_ipc_client *client = &self->clients[index];
    const HANDLE client_pipe = client->pipe;

    pthread_mutex_lock(&self->deferred_requests_mutex);
    for(int i = 0; i < GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT; ++i) {
        if(self->deferred_requests[i].state != GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY && (HANDLE)(intptr_t)self->deferred_requests[i].client_fd == client_pipe)
            self->deferred_requests[i].state = GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
    }
    pthread_mutex_unlock(&self->deferred_requests_mutex);

    /* Close the handle BEFORE freeing the OVERLAPPED: closing the pipe
       cancels the pending overlapped read, and the cancellation writes the
       final status into the OVERLAPPED (see §3o in the porting notes). */
    OVERLAPPED *read_overlapped = client->read_overlapped;
    CloseHandle(client_pipe);
    if(read_overlapped) {
        if(read_overlapped->hEvent)
            CloseHandle(read_overlapped->hEvent);
        free(read_overlapped);
    }
    memset(client, 0, sizeof(*client));

    for(int i = index; i < self->num_clients - 1; ++i)
        self->clients[i] = self->clients[i + 1];
    self->clients[self->num_clients - 1].pipe = NULL;
    --self->num_clients;
}

/* Arms an overlapped ReadFile on |client| (into its scratch buffer).
   Returns 0 when a read is now pending (client->read_overlapped set) or
   when data was already processed synchronously. Returns 1 when the client
   disconnected before any data (not an error — the caller removes the
   client). Returns -1 on a real failure. */
static int ipc_client_start_read(gsr_ipc *self, gsr_ipc_client *client) {
    OVERLAPPED *ov = (OVERLAPPED*)calloc(1, sizeof(OVERLAPPED));
    if(!ov)
        return -1;
    ov->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if(!ov->hEvent) {
        free(ov);
        return -1;
    }

    DWORD bytes_read = 0;
    const BOOL result = ReadFile(client->pipe, client->read_buffer, sizeof(client->read_buffer), &bytes_read, ov);

    if(result) {
        /* Completed synchronously — the bytes are already in the buffer and
           must be processed now (they were NOT signalled through the event).
           0 bytes = client disconnected. */
        if(bytes_read > 0) {
            const bool keep_client = ipc_client_on_bytes(self, client, client->read_buffer, bytes_read);
            CloseHandle(ov->hEvent);
            free(ov);
            if(!keep_client)
                return 1;
            /* Re-arm for the next chunk. */
            return ipc_client_start_read(self, client);
        }
        CloseHandle(ov->hEvent);
        free(ov);
        return 1;
    }

    const DWORD error = GetLastError();
    if(error == ERROR_IO_PENDING) {
        client->read_overlapped = ov;
        return 0;
    }

    /* ERROR_BROKEN_PIPE / ERROR_PIPE_NOT_CONNECTED: client gone. */
    CloseHandle(ov->hEvent);
    free(ov);
    return 1;
}

static void ipc_add_client(gsr_ipc *self, HANDLE client_pipe) {
    if(self->num_clients == GSR_IPC_MAX_CLIENTS) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_ipc: too many ipc clients are connected, rejecting the new connection");
        CloseHandle(client_pipe);
        return;
    }

    gsr_ipc_client *client = &self->clients[self->num_clients];
    memset(client, 0, sizeof(*client));
    client->pipe = client_pipe;

    const int read_result = ipc_client_start_read(self, client);
    if(read_result < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to setup the ipc client pipe");
        CloseHandle(client_pipe);
        return;
    }
    if(read_result > 0) {
        /* Client connected and immediately disconnected. */
        CloseHandle(client_pipe);
        return;
    }

    ++self->num_clients;
}

/* Creates a new listen pipe instance and arms ConnectNamedPipe on it.
   Returns the new listen handle (or NULL on failure), storing the listen
   OVERLAPPED in self->listen_overlapped. The caller must free a PREVIOUS
   listen OVERLAPPED before calling this (the consumed one is freed in the
   ipc thread after a connect completes), and CloseHandle the pipe BEFORE
   freeing the OVERLAPPED (pending ConnectNamedPipe cancellation writes
   into it — the Rpc.cpp lesson). */
static HANDLE ipc_listen_pipe_create(gsr_ipc *self) {
    char full_name[GSR_IPC_WIN32_MAX_PIPE_NAME + sizeof(GSR_IPC_WIN32_PIPE_PREFIX)];
    snprintf(full_name, sizeof(full_name), "%s%s", GSR_IPC_WIN32_PIPE_PREFIX, self->socket_filepath);

    HANDLE pipe = CreateNamedPipeA(full_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        GSR_IPC_MAX_CLIENTS,
        GSR_IPC_CLIENT_SEND_BUFFER_SIZE,
        GSR_IPC_MAX_REQUEST_SIZE,
        0, NULL);
    if(pipe == INVALID_HANDLE_VALUE) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the ipc pipe, error: %lu", (unsigned long)GetLastError());
        return NULL;
    }

    OVERLAPPED *ov = (OVERLAPPED*)calloc(1, sizeof(OVERLAPPED));
    if(!ov) {
        CloseHandle(pipe);
        return NULL;
    }
    ov->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if(!ov->hEvent) {
        free(ov);
        CloseHandle(pipe);
        return NULL;
    }

    self->listen_overlapped = ov;
    return pipe;
}

static bool ipc_listen_arm_connect(gsr_ipc *self) {
    OVERLAPPED *ov = self->listen_overlapped;
    ResetEvent(ov->hEvent);
    self->listen_connected_sync = false;
    const BOOL result = ConnectNamedPipe(self->listen_pipe, ov);
    if(result) {
        /* A client connected between CreateNamedPipe and this call; the
           OVERLAPPED never went pending so its event won't fire. Signal the
           event and remember that GetOverlappedResult must be skipped. */
        self->listen_connected_sync = true;
        SetEvent(ov->hEvent);
        return true;
    }

    const DWORD error = GetLastError();
    if(error == ERROR_IO_PENDING)
        return true;
    if(error == ERROR_PIPE_CONNECTED) {
        self->listen_connected_sync = true;
        SetEvent(ov->hEvent);
        return true;
    }
    return false;
}

static void ipc_send_completed_request_replies(gsr_ipc *self) {
    for(int i = 0; i < GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT; ++i) {
        pthread_mutex_lock(&self->deferred_requests_mutex);
        const gsr_ipc_deferred_request deferred_request = self->deferred_requests[i];
        if(deferred_request.state == GSR_IPC_DEFERRED_REQUEST_STATE_COMPLETED)
            self->deferred_requests[i].state = GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
        pthread_mutex_unlock(&self->deferred_requests_mutex);

        if(deferred_request.state != GSR_IPC_DEFERRED_REQUEST_STATE_COMPLETED)
            continue;

        const int client_index = ipc_find_client_index_by_pipe(self, (HANDLE)(intptr_t)deferred_request.client_fd);
        if(client_index == -1)
            continue;

        const char *error_message = deferred_request_failed_error((gsr_ipc_deferred_request_type)i);
        const char *filepath = deferred_request.has_filepath ? deferred_request.filepath : NULL;
        if(!ipc_client_send_reply(self, &self->clients[client_index], deferred_request.request_id, deferred_request.success, error_message, filepath))
            ipc_remove_client(self, client_index);
    }
}

static void ipc_fail_pending_requests(gsr_ipc *self) {
    for(int i = 0; i < GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT; ++i) {
        pthread_mutex_lock(&self->deferred_requests_mutex);
        const gsr_ipc_deferred_request deferred_request = self->deferred_requests[i];
        self->deferred_requests[i].state = GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
        pthread_mutex_unlock(&self->deferred_requests_mutex);

        if(deferred_request.state != GSR_IPC_DEFERRED_REQUEST_STATE_PENDING)
            continue;

        const int client_index = ipc_find_client_index_by_pipe(self, (HANDLE)(intptr_t)deferred_request.client_fd);
        if(client_index == -1)
            continue;

        if(!ipc_client_send_reply(self, &self->clients[client_index], deferred_request.request_id, false, "GPU Screen Recorder exited before the request finished", NULL))
            ipc_remove_client(self, client_index);
    }
}

static void ipc_close(gsr_ipc *self) {
    while(self->num_clients > 0)
        ipc_remove_client(self, self->num_clients - 1);

    /* Close the listen pipe BEFORE freeing its OVERLAPPED (pending
       ConnectNamedPipe cancellation writes into it). */
    if(self->listen_pipe) {
        CloseHandle(self->listen_pipe);
        self->listen_pipe = NULL;
    }
    if(self->listen_overlapped) {
        if(self->listen_overlapped->hEvent)
            CloseHandle(self->listen_overlapped->hEvent);
        free(self->listen_overlapped);
        self->listen_overlapped = NULL;
    }

    if(self->shutdown_event) {
        CloseHandle(self->shutdown_event);
        self->shutdown_event = NULL;
    }
    if(self->wakeup_event) {
        CloseHandle(self->wakeup_event);
        self->wakeup_event = NULL;
    }

    if(self->deferred_requests_mutex_created) {
        pthread_mutex_destroy(&self->deferred_requests_mutex);
        self->deferred_requests_mutex_created = false;
    }

    self->socket_bound = false;
}

static void* ipc_thread(void *userdata) {
    gsr_ipc *self = userdata;
    bool quit = false;

    while(!quit) {
        HANDLE wait_handles[2 + GSR_IPC_MAX_CLIENTS];
        int num_wait_handles = 0;
        wait_handles[num_wait_handles++] = self->shutdown_event;
        wait_handles[num_wait_handles++] = self->wakeup_event;
        if(self->listen_pipe && self->listen_overlapped)
            wait_handles[num_wait_handles++] = self->listen_overlapped->hEvent;
        for(int i = 0; i < self->num_clients; ++i) {
            if(self->clients[i].read_overlapped)
                wait_handles[num_wait_handles++] = self->clients[i].read_overlapped->hEvent;
        }

        /* A modest timeout keeps the array in sync when clients are added
           or removed (handle array indices shift); events are still
           latency-free because waiting on signalled handles returns
           immediately. */
        const DWORD wait_result = WaitForMultipleObjects((DWORD)num_wait_handles, wait_handles, FALSE, 100);
        if(wait_result == WAIT_FAILED) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to wait for ipc events, error: %lu", (unsigned long)GetLastError());
            break;
        }

        if(WaitForSingleObject(self->shutdown_event, 0) == WAIT_OBJECT_0) {
            ipc_send_completed_request_replies(self);
            quit = true;
            continue;
        }

        /* A deferred request completed (gsr_ipc_complete_request from the
           recorder thread): send its reply now.

           NOTE: do NOT gate this on the wakeup event — the event is
           auto-reset, and when it fires, WaitForMultipleObjects above
           consumes it (bWaitAll=FALSE returns the lowest signaled index and
           resets that auto-reset object), so a subsequent
           WaitForSingleObject(wakeup, 0) would see it as unsignaled and the
           completed request would never be drained — the client waits
           forever for its reply. Draining unconditionally every iteration is
           cheap (a mutex + a scan of the few deferred slots) and the 100ms
           wait timeout bounds latency when the event is missed. */
        ipc_send_completed_request_replies(self);

        /* Listen connect completed? */
        if(self->listen_pipe && self->listen_overlapped &&
           WaitForSingleObject(self->listen_overlapped->hEvent, 0) == WAIT_OBJECT_0) {
            const bool connected = self->listen_connected_sync;
            if(connected || GetOverlappedResult(self->listen_pipe, self->listen_overlapped, &(DWORD){0}, FALSE)) {
                /* Hand the accepted connection to a client slot... */
                HANDLE accepted = self->listen_pipe;
                self->listen_pipe = NULL;
                ipc_add_client(self, accepted);

                /* The old listen OVERLAPPED is consumed (its event fired and
                   GetOverlappedResult succeeded, or it connected
                   synchronously); free it now, then create a fresh listen
                   instance and re-arm. */
                if(self->listen_overlapped) {
                    if(self->listen_overlapped->hEvent)
                        CloseHandle(self->listen_overlapped->hEvent);
                    free(self->listen_overlapped);
                    self->listen_overlapped = NULL;
                }

                self->listen_pipe = ipc_listen_pipe_create(self);
                if(self->listen_pipe) {
                    if(!ipc_listen_arm_connect(self)) {
                        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to re-arm the listen pipe, error: %lu", (unsigned long)GetLastError());
                        ipc_close(self);
                        break;
                    }
                } else {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to create the next listen pipe, error: %lu", (unsigned long)GetLastError());
                    ipc_close(self);
                    break;
                }
            }
        }

        /* Client reads completed? */
        for(int i = 0; i < self->num_clients; ++i) {
            gsr_ipc_client *client = &self->clients[i];
            if(!client->read_overlapped)
                continue;

            if(WaitForSingleObject(client->read_overlapped->hEvent, 0) != WAIT_OBJECT_0)
                continue;

            DWORD bytes_read = 0;
            const BOOL ok = GetOverlappedResult(client->pipe, client->read_overlapped, &bytes_read, FALSE);
            OVERLAPPED *completed_ov = client->read_overlapped;
            CloseHandle(completed_ov->hEvent);
            free(completed_ov);
            client->read_overlapped = NULL;

            if(!ok || bytes_read == 0) {
                ipc_remove_client(self, i);
                --i;
                continue;
            }

            if(!ipc_client_on_bytes(self, client, client->read_buffer, bytes_read)) {
                ipc_remove_client(self, i);
                --i;
                continue;
            }

            /* Re-arm the read for the next chunk. */
            const int read_result = ipc_client_start_read(self, client);
            if(read_result != 0) {
                ipc_remove_client(self, i);
                --i;
            }
        }
    }

    ipc_send_completed_request_replies(self);
    ipc_fail_pending_requests(self);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

static bool ipc_pipe_in_use(const char *pipe_name) {
    HANDLE probe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return true;
    }
    return false;
}

int gsr_ipc_init(gsr_ipc *self, const char *socket_filepath) {
    memset(self, 0, sizeof(*self));
    self->listen_pipe = NULL;
    self->listen_overlapped = NULL;
    self->shutdown_event = NULL;

    /* Normalize: callers may pass "\\.\pipe\gsr-x" or just "gsr-x". */
    const char *pipe_name = socket_filepath;
    if(strncmp(socket_filepath, GSR_IPC_WIN32_PIPE_PREFIX, strlen(GSR_IPC_WIN32_PIPE_PREFIX)) == 0)
        pipe_name += strlen(GSR_IPC_WIN32_PIPE_PREFIX);

    if(strlen(pipe_name) == 0 || strlen(pipe_name) > GSR_IPC_WIN32_MAX_PIPE_NAME) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: the ipc pipe name is invalid (it can be at most %d characters): \"%s\"", GSR_IPC_WIN32_MAX_PIPE_NAME, socket_filepath);
        return GSR_ERROR_GENERIC;
    }

    snprintf(self->socket_filepath, sizeof(self->socket_filepath), "%s", pipe_name);

    char full_name[GSR_IPC_WIN32_MAX_PIPE_NAME + sizeof(GSR_IPC_WIN32_PIPE_PREFIX)];
    snprintf(full_name, sizeof(full_name), "%s%s", GSR_IPC_WIN32_PIPE_PREFIX, pipe_name);

    if(ipc_pipe_in_use(full_name)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: another program is already listening on \"%s\"", full_name);
        return GSR_ERROR_GENERIC;
    }

    if(pthread_mutex_init(&self->deferred_requests_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the deferred requests mutex");
        return GSR_ERROR_GENERIC;
    }
    self->deferred_requests_mutex_created = true;

    self->shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if(!self->shutdown_event) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the ipc shutdown event, error: %lu", (unsigned long)GetLastError());
        goto err;
    }

    /* Auto-reset: gsr_ipc_complete_request sets it (from the recorder
       thread) to wake the ipc thread and send the deferred reply. */
    self->wakeup_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if(!self->wakeup_event) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the ipc wakeup event, error: %lu", (unsigned long)GetLastError());
        goto err;
    }

    self->listen_pipe = ipc_listen_pipe_create(self);
    if(!self->listen_pipe)
        goto err;

    if(!ipc_listen_arm_connect(self)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to listen on the ipc pipe, error: %lu", (unsigned long)GetLastError());
        goto err;
    }

    self->socket_bound = true;
    self->initialized = true;
    return GSR_ERROR_OK;

    err:
    ipc_close(self);
    return GSR_ERROR_GENERIC;
}

void gsr_ipc_deinit(gsr_ipc *self) {
    if(!self->initialized)
        return;

    gsr_ipc_stop(self);
    ipc_close(self);
    self->initialized = false;
}

int gsr_ipc_start(gsr_ipc *self, const gsr_ipc_handlers *handlers) {
    if(!self->initialized)
        return GSR_ERROR_OK;

    self->handlers = *handlers;

    const int thread_create_result = pthread_create(&self->thread, NULL, ipc_thread, self);
    if(thread_create_result != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_start: failed to create the ipc thread, error: %d", thread_create_result);
        return GSR_ERROR_GENERIC;
    }

    self->thread_running = true;
    return GSR_ERROR_OK;
}

void gsr_ipc_stop(gsr_ipc *self) {
    if(!self->thread_running)
        return;

    SetEvent(self->shutdown_event);
    pthread_join(self->thread, NULL);
    self->thread_running = false;
}

void gsr_ipc_complete_request(gsr_ipc *self, gsr_ipc_deferred_request_type type, bool success, const char *filepath) {
    if(!self->initialized)
        return;

    pthread_mutex_lock(&self->deferred_requests_mutex);
    gsr_ipc_deferred_request *deferred_request = &self->deferred_requests[type];
    const bool was_pending = deferred_request->state == GSR_IPC_DEFERRED_REQUEST_STATE_PENDING;
    if(was_pending) {
        deferred_request->state = GSR_IPC_DEFERRED_REQUEST_STATE_COMPLETED;
        deferred_request->success = success;
        deferred_request->has_filepath = filepath != NULL;
        if(filepath)
            snprintf(deferred_request->filepath, sizeof(deferred_request->filepath), "%s", filepath);
    }
    pthread_mutex_unlock(&self->deferred_requests_mutex);

    if(was_pending)
        SetEvent(self->wakeup_event);
}

/* ------------------------------------------------------------------ */
/* client side (used by gsr-cli.exe, platform/windows/gsr_cli_win32.c) */
/* ------------------------------------------------------------------ */

HANDLE gsr_platform_ipc_client_connect(const char *socket_filepath, int reply_timeout_seconds) {
    const char *pipe_name = socket_filepath;
    if(strncmp(socket_filepath, GSR_IPC_WIN32_PIPE_PREFIX, strlen(GSR_IPC_WIN32_PIPE_PREFIX)) == 0)
        pipe_name += strlen(GSR_IPC_WIN32_PIPE_PREFIX);

    if(strlen(pipe_name) == 0 || strlen(pipe_name) > GSR_IPC_WIN32_MAX_PIPE_NAME) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "the ipc pipe name is invalid (it can be at most %d characters): \"%s\"", GSR_IPC_WIN32_MAX_PIPE_NAME, socket_filepath);
        return INVALID_HANDLE_VALUE;
    }

    char full_name[GSR_IPC_WIN32_MAX_PIPE_NAME + sizeof(GSR_IPC_WIN32_PIPE_PREFIX)];
    snprintf(full_name, sizeof(full_name), "%s%s", GSR_IPC_WIN32_PIPE_PREFIX, pipe_name);

    /* Bounded retry: the server accepts a client, then frees the listen
       pipe and creates the next instance. A CreateFileA landing in that
       window fails with ERROR_PIPE_BUSY (instance exists but is being
       connected) or ERROR_FILE_NOT_FOUND (instance freed, next one not yet
       created). Both are transient — retry briefly instead of failing,
       like the UI's Rpc.cpp does. Give up after ~5s rather than spinning
       forever. */
    HANDLE pipe = INVALID_HANDLE_VALUE;
    int retries = 0;
    while(true) {
        pipe = CreateFileA(full_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if(pipe != INVALID_HANDLE_VALUE)
            break;

        const DWORD err = GetLastError();
        if(err == ERROR_PIPE_BUSY) {
            if(++retries > 50)
                return INVALID_HANDLE_VALUE;
            if(!WaitNamedPipeA(full_name, 100))
                continue;
        } else if(err == ERROR_FILE_NOT_FOUND) {
            if(++retries > 50)
                return INVALID_HANDLE_VALUE;
            /* No instance exists yet (server between pipe instances); wait
               for one to appear. WaitNamedPipeA returns immediately with
               FILE_NOT_FOUND here, so sleep briefly before retrying. */
            Sleep(10);
        } else {
            return INVALID_HANDLE_VALUE;
        }
    }

    /* Read timeout: 0 = wait forever (stop / save-replay), otherwise the
       caller's reply timeout in seconds. */
    DWORD timeout = 0;
    if(reply_timeout_seconds > 0)
        timeout = (DWORD)reply_timeout_seconds * 1000;
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, NULL, timeout ? &timeout : NULL);

    return pipe;
}

void gsr_platform_ipc_client_disconnect(HANDLE pipe) {
    if(pipe && pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
}

bool gsr_platform_ipc_client_send_all(HANDLE pipe, const char *data, size_t size) {
    size_t offset = 0;
    while(offset < size) {
        DWORD bytes_written = 0;
        if(!WriteFile(pipe, data + offset, (DWORD)(size - offset), &bytes_written, NULL) || bytes_written == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to send the request, error: %lu", (unsigned long)GetLastError());
            return false;
        }
        offset += bytes_written;
    }
    return true;
}

/* Reads until a newline. |reply_size| is set to the size of the reply,
   excluding the newline. */
bool gsr_platform_ipc_client_receive_reply(HANDLE pipe, char *reply, size_t reply_capacity, size_t *reply_size) {
    size_t offset = 0;
    for(;;) {
        if(offset == reply_capacity) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "the reply is too large");
            return false;
        }

        DWORD bytes_read = 0;
        if(!ReadFile(pipe, reply + offset, (DWORD)(reply_capacity - offset), &bytes_read, NULL) || bytes_read == 0) {
            const DWORD error = GetLastError();
            if(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "GPU Screen Recorder closed the connection before replying");
            } else {
                gsr_log(GSR_LOG_LEVEL_ERROR, "failed to receive the reply, error: %lu", (unsigned long)error);
            }
            return false;
        }

        const char *newline = memchr(reply + offset, '\n', bytes_read);
        offset += bytes_read;
        if(newline) {
            *reply_size = (size_t)(newline - reply);
            return true;
        }
    }
}
