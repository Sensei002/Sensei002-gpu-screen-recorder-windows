#include "../../include/cli/ipc.h"
#include "../../include/recorder/error.h"
#include "../../include/recorder/replay_save.h"
#include "../../include/json.h"
#include "../../include/log.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#ifdef __linux__
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#endif

#define GSR_IPC_MAX_REQUEST_NAME_SIZE 64
#define GSR_IPC_MAX_EVENTS (2 + GSR_IPC_MAX_CLIENTS*2)
#define GSR_IPC_SHUTDOWN_SEND_TIMEOUT_MILLISECONDS 1000
#define GSR_IPC_SOCKET_MODE 0600

#define GSR_IPC_WAKEUP_QUIT (1 << 0)
#define GSR_IPC_WAKEUP_COMPLETED_REQUEST (1 << 1)

typedef struct {
    int64_t id;
    char name[GSR_IPC_MAX_REQUEST_NAME_SIZE];
    sj_Value data;
    bool has_data;
    const char *request_end;
} gsr_ipc_request;

typedef struct {
    int fd;
    bool readable;
    bool writable;
} gsr_ipc_event;

static bool string_is_only_whitespace(const char *str, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n')
            return false;
    }
    return true;
}

static bool fd_set_cloexec(int fd) {
    const int flags = fcntl(fd, F_GETFD);
    return flags != -1 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}

static bool fd_set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

#ifdef __linux__
static bool ipc_poller_init(gsr_ipc *self) {
    self->poll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(self->poll_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to create an epoll instance, error: %s", strerror(errno));
        return false;
    }
    return true;
}

static bool ipc_poller_add(gsr_ipc *self, int fd) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = fd;
    return epoll_ctl(self->poll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
}

static bool ipc_poller_set_write_notify(gsr_ipc *self, int fd, bool enable) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN | EPOLLET | (enable ? EPOLLOUT : 0);
    event.data.fd = fd;
    return epoll_ctl(self->poll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
}

/* Returns the number of events, or -1 on failure. Waits until at least one event is available */
static int ipc_poller_wait(gsr_ipc *self, gsr_ipc_event *events, int events_capacity) {
    struct epoll_event platform_events[GSR_IPC_MAX_EVENTS];
    if(events_capacity > GSR_IPC_MAX_EVENTS)
        events_capacity = GSR_IPC_MAX_EVENTS;

    const int num_events = epoll_wait(self->poll_fd, platform_events, events_capacity, -1);
    if(num_events == -1)
        return errno == EINTR ? 0 : -1;

    for(int i = 0; i < num_events; ++i) {
        events[i].fd = platform_events[i].data.fd;
        events[i].readable = platform_events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR);
        events[i].writable = platform_events[i].events & EPOLLOUT;
    }
    return num_events;
}
#else
static bool ipc_poller_init(gsr_ipc *self) {
    self->poll_fd = kqueue();
    if(self->poll_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to create a kqueue instance, error: %s", strerror(errno));
        return false;
    }
    fd_set_cloexec(self->poll_fd);
    return true;
}

static bool ipc_poller_add(gsr_ipc *self, int fd) {
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
    return kevent(self->poll_fd, &change, 1, NULL, 0, NULL) != -1;
}

static bool ipc_poller_set_write_notify(gsr_ipc *self, int fd, bool enable) {
    struct kevent change;
    EV_SET(&change, fd, EVFILT_WRITE, enable ? (EV_ADD | EV_CLEAR) : EV_DELETE, 0, 0, NULL);
    return kevent(self->poll_fd, &change, 1, NULL, 0, NULL) != -1;
}

/* Returns the number of events, or -1 on failure. Waits until at least one event is available */
static int ipc_poller_wait(gsr_ipc *self, gsr_ipc_event *events, int events_capacity) {
    struct kevent platform_events[GSR_IPC_MAX_EVENTS];
    if(events_capacity > GSR_IPC_MAX_EVENTS)
        events_capacity = GSR_IPC_MAX_EVENTS;

    const int num_events = kevent(self->poll_fd, NULL, 0, platform_events, events_capacity, NULL);
    if(num_events == -1)
        return errno == EINTR ? 0 : -1;

    for(int i = 0; i < num_events; ++i) {
        events[i].fd = platform_events[i].ident;
        events[i].readable = platform_events[i].filter == EVFILT_READ;
        events[i].writable = platform_events[i].filter == EVFILT_WRITE;
    }
    return num_events;
}
#endif

/* Only used when the ipc thread exits, to not lose replies that haven't been fully sent yet */
static bool ipc_send_all_blocking(int fd, const char *data, size_t size) {
    size_t offset = 0;
    while(offset < size) {
        const ssize_t bytes_written = send(fd, data + offset, size - offset, MSG_NOSIGNAL);
        if(bytes_written > 0) {
            offset += bytes_written;
            continue;
        }

        if(bytes_written == -1 && errno == EINTR)
            continue;

        if(bytes_written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd poll_fd;
            poll_fd.fd = fd;
            poll_fd.events = POLLOUT;
            poll_fd.revents = 0;

            const int poll_result = poll(&poll_fd, 1, GSR_IPC_SHUTDOWN_SEND_TIMEOUT_MILLISECONDS);
            if(poll_result == -1 && errno == EINTR)
                continue;

            if(poll_result <= 0)
                return false;

            continue;
        }

        return false;
    }
    return true;
}

static bool ipc_client_send_data(gsr_ipc *self, gsr_ipc_client *client, const char *data, size_t size) {
    size_t offset = 0;
    if(client->send_buffer_size == 0) {
        while(offset < size) {
            const ssize_t bytes_written = send(client->fd, data + offset, size - offset, MSG_NOSIGNAL);
            if(bytes_written > 0) {
                offset += bytes_written;
                continue;
            }

            if(bytes_written == -1 && errno == EINTR)
                continue;

            if(bytes_written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;

            return false;
        }
    }

    const size_t bytes_remaining = size - offset;
    if(bytes_remaining == 0)
        return true;

    if(client->send_buffer_size + bytes_remaining > sizeof(client->send_buffer)) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_ipc: an ipc client isn't reading replies fast enough, disconnecting it");
        return false;
    }

    const bool send_buffer_was_empty = client->send_buffer_size == 0;
    memcpy(client->send_buffer + client->send_buffer_size, data + offset, bytes_remaining);
    client->send_buffer_size += bytes_remaining;
    return !send_buffer_was_empty || ipc_poller_set_write_notify(self, client->fd, true);
}

static bool ipc_client_flush_send_buffer(gsr_ipc *self, gsr_ipc_client *client) {
    if(client->send_buffer_size == 0)
        return true;

    size_t offset = 0;
    while(offset < client->send_buffer_size) {
        const ssize_t bytes_written = send(client->fd, client->send_buffer + offset, client->send_buffer_size - offset, MSG_NOSIGNAL);
        if(bytes_written > 0) {
            offset += bytes_written;
            continue;
        }

        if(bytes_written == -1 && errno == EINTR)
            continue;

        if(bytes_written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;

        return false;
    }

    memmove(client->send_buffer, client->send_buffer + offset, client->send_buffer_size - offset);
    client->send_buffer_size -= offset;
    return client->send_buffer_size != 0 || ipc_poller_set_write_notify(self, client->fd, false);
}

static bool ipc_client_send_reply(gsr_ipc *self, gsr_ipc_client *client, int64_t id, bool success, const char *error_message, const char *data) {
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

    return ipc_client_send_data(self, client, reply, reply_size);
}

static bool ipc_request_parse(char *data, size_t size, gsr_ipc_request *request, char *error_message, size_t error_message_size) {
    memset(request, 0, sizeof(*request));
    request->request_end = data + size;

    sj_Reader reader = sj_reader(data, size);
    const sj_Value root = sj_read(&reader);
    if(root.type != SJ_OBJECT) {
        snprintf(error_message, error_message_size, "expected the request to be a json object");
        return false;
    }

    sj_Value id_value;
    sj_Value name_value;
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

    *seconds = data_seconds;
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

static bool ipc_set_deferred_request_pending(gsr_ipc *self, gsr_ipc_deferred_request_type type, int client_fd, int64_t request_id) {
    pthread_mutex_lock(&self->deferred_requests_mutex);
    gsr_ipc_deferred_request *deferred_request = &self->deferred_requests[type];
    const bool was_empty = deferred_request->state == GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
    if(was_empty) {
        deferred_request->state = GSR_IPC_DEFERRED_REQUEST_STATE_PENDING;
        deferred_request->client_fd = client_fd;
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

    /* The pending deferred request has to be registered before the handler starts the operation,
       otherwise the operation could finish before the reply to it gets registered */
    gsr_ipc_deferred_request_type deferred_request_type;
    const bool reply_is_deferred = ipc_request_name_to_deferred_request_type(request.name, &deferred_request_type);
    if(reply_is_deferred && !ipc_set_deferred_request_pending(self, deferred_request_type, client->fd, request.id))
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

static bool ipc_client_on_byte(gsr_ipc *self, gsr_ipc_client *client, char c) {
    if(c != '\n') {
        if(client->request_size < GSR_IPC_MAX_REQUEST_SIZE)
            client->request[client->request_size++] = c;
        else
            client->request_too_large = true;
        return true;
    }

    const bool keep_client = ipc_client_on_request(self, client);
    client->request_size = 0;
    client->request_too_large = false;
    return keep_client;
}

static bool ipc_client_receive(gsr_ipc *self, gsr_ipc_client *client) {
    for(;;) {
        char buffer[1024];
        const ssize_t bytes_read = recv(client->fd, buffer, sizeof(buffer), 0);
        if(bytes_read == 0)
            return false;

        if(bytes_read == -1) {
            if(errno == EINTR)
                continue;

            return errno == EAGAIN || errno == EWOULDBLOCK;
        }

        for(ssize_t i = 0; i < bytes_read; ++i) {
            if(!ipc_client_on_byte(self, client, buffer[i]))
                return false;
        }
    }
}

static void ipc_add_client(gsr_ipc *self, int client_fd) {
    if(self->num_clients == GSR_IPC_MAX_CLIENTS) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_ipc: too many ipc clients are connected, rejecting the new connection");
        close(client_fd);
        return;
    }

    if(!fd_set_cloexec(client_fd) || !fd_set_nonblocking(client_fd) || !ipc_poller_add(self, client_fd)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to setup the ipc client socket, error: %s", strerror(errno));
        close(client_fd);
        return;
    }

    gsr_ipc_client *client = &self->clients[self->num_clients];
    client->fd = client_fd;
    client->request_size = 0;
    client->request_too_large = false;
    client->send_buffer_size = 0;
    ++self->num_clients;
}

static void ipc_accept_clients(gsr_ipc *self) {
    for(;;) {
        const int client_fd = accept(self->socket_fd, NULL, NULL);
        if(client_fd == -1) {
            if(errno == EINTR)
                continue;
            return;
        }

        ipc_add_client(self, client_fd);
    }
}

static void ipc_remove_client(gsr_ipc *self, int index) {
    const int client_fd = self->clients[index].fd;

    pthread_mutex_lock(&self->deferred_requests_mutex);
    for(int i = 0; i < GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT; ++i) {
        if(self->deferred_requests[i].state != GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY && self->deferred_requests[i].client_fd == client_fd)
            self->deferred_requests[i].state = GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY;
    }
    pthread_mutex_unlock(&self->deferred_requests_mutex);

    close(client_fd);
    for(int i = index; i < self->num_clients - 1; ++i) {
        self->clients[i] = self->clients[i + 1];
    }
    --self->num_clients;
}

static int ipc_find_client_index_by_fd(const gsr_ipc *self, int fd) {
    for(int i = 0; i < self->num_clients; ++i) {
        if(self->clients[i].fd == fd)
            return i;
    }
    return -1;
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

        const int client_index = ipc_find_client_index_by_fd(self, deferred_request.client_fd);
        if(client_index == -1)
            continue;

        const char *error_message = deferred_request_failed_error(i);
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

        const int client_index = ipc_find_client_index_by_fd(self, deferred_request.client_fd);
        if(client_index == -1)
            continue;

        if(!ipc_client_send_reply(self, &self->clients[client_index], deferred_request.request_id, false, "GPU Screen Recorder exited before the request finished", NULL))
            ipc_remove_client(self, client_index);
    }
}

static void ipc_flush_clients_blocking(gsr_ipc *self) {
    for(int i = 0; i < self->num_clients; ++i) {
        gsr_ipc_client *client = &self->clients[i];
        if(client->send_buffer_size > 0)
            ipc_send_all_blocking(client->fd, client->send_buffer, client->send_buffer_size);
        client->send_buffer_size = 0;
    }
}

static int ipc_drain_wakeup_pipe(gsr_ipc *self) {
    int wakeup_flags = 0;
    for(;;) {
        char buffer[64];
        const ssize_t bytes_read = read(self->wakeup_pipe[0], buffer, sizeof(buffer));
        if(bytes_read == -1 && errno == EINTR)
            continue;

        if(bytes_read <= 0)
            break;

        for(ssize_t i = 0; i < bytes_read; ++i) {
            if(buffer[i] == 'q')
                wakeup_flags |= GSR_IPC_WAKEUP_QUIT;
            else if(buffer[i] == 'c')
                wakeup_flags |= GSR_IPC_WAKEUP_COMPLETED_REQUEST;
        }
    }
    return wakeup_flags;
}

static void ipc_wakeup_thread(gsr_ipc *self, char wakeup_value) {
    ssize_t bytes_written = 0;
    do {
        bytes_written = write(self->wakeup_pipe[1], &wakeup_value, 1);
    } while(bytes_written == -1 && errno == EINTR);

    if(bytes_written == -1)
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to wake up the ipc thread, error: %s", strerror(errno));
}

static void* ipc_thread(void *userdata) {
    gsr_ipc *self = userdata;
    gsr_ipc_event events[GSR_IPC_MAX_EVENTS];
    bool running = true;

    while(running) {
        const int num_events = ipc_poller_wait(self, events, GSR_IPC_MAX_EVENTS);
        if(num_events == -1) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc: failed to wait for ipc events, error: %s", strerror(errno));
            break;
        }

        for(int i = 0; i < num_events; ++i) {
            if(events[i].fd == self->wakeup_pipe[0]) {
                const int wakeup_flags = ipc_drain_wakeup_pipe(self);
                if(wakeup_flags & GSR_IPC_WAKEUP_COMPLETED_REQUEST)
                    ipc_send_completed_request_replies(self);
                if(wakeup_flags & GSR_IPC_WAKEUP_QUIT)
                    running = false;
                continue;
            }

            if(events[i].fd == self->socket_fd) {
                ipc_accept_clients(self);
                continue;
            }

            const int client_index = ipc_find_client_index_by_fd(self, events[i].fd);
            if(client_index == -1)
                continue;

            gsr_ipc_client *client = &self->clients[client_index];
            bool keep_client = true;
            if(events[i].readable)
                keep_client = ipc_client_receive(self, client);
            if(keep_client && events[i].writable)
                keep_client = ipc_client_flush_send_buffer(self, client);

            if(!keep_client)
                ipc_remove_client(self, client_index);
        }
    }

    ipc_send_completed_request_replies(self);
    ipc_fail_pending_requests(self);
    ipc_flush_clients_blocking(self);
    return NULL;
}

static bool ipc_socket_filepath_in_use(const char *socket_filepath) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_filepath);

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd == -1)
        return true;

    const bool in_use = connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) == 0;
    close(fd);
    return in_use;
}

/* Removes the socket that a GPU Screen Recorder instance that was killed left behind, so the same ipc socket path can be used again */
static bool ipc_remove_unused_socket(const char *socket_filepath) {
    struct stat file_stat;
    if(lstat(socket_filepath, &file_stat) == -1)
        return true;

    if(!S_ISSOCK(file_stat.st_mode)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: can't use \"%s\" as the ipc socket path because a file that isn't a socket already exists there", socket_filepath);
        return false;
    }

    if(ipc_socket_filepath_in_use(socket_filepath)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: another program is already listening on \"%s\"", socket_filepath);
        return false;
    }

    unlink(socket_filepath);
    return true;
}

static bool ipc_bind(gsr_ipc *self, const struct sockaddr_un *addr) {
    if(!ipc_remove_unused_socket(self->socket_filepath))
        return false;

    const mode_t prev_mask = umask(0777 & ~GSR_IPC_SOCKET_MODE);
    const int bind_result = bind(self->socket_fd, (const struct sockaddr*)addr, sizeof(*addr));
    const int bind_error = errno;
    umask(prev_mask);

    if(bind_result == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to bind the ipc socket to \"%s\", error: %s", self->socket_filepath, strerror(bind_error));
        return false;
    }

    self->socket_bound = true;
    return true;
}

static void ipc_close(gsr_ipc *self) {
    for(int i = 0; i < self->num_clients; ++i) {
        close(self->clients[i].fd);
    }
    self->num_clients = 0;

    for(int i = 0; i < 2; ++i) {
        if(self->wakeup_pipe[i] != -1) {
            close(self->wakeup_pipe[i]);
            self->wakeup_pipe[i] = -1;
        }
    }

    if(self->poll_fd != -1) {
        close(self->poll_fd);
        self->poll_fd = -1;
    }

    if(self->socket_fd != -1) {
        close(self->socket_fd);
        self->socket_fd = -1;
    }

    if(self->socket_bound) {
        unlink(self->socket_filepath);
        self->socket_bound = false;
    }

    if(self->deferred_requests_mutex_created) {
        pthread_mutex_destroy(&self->deferred_requests_mutex);
        self->deferred_requests_mutex_created = false;
    }
}

int gsr_ipc_init(gsr_ipc *self, const char *socket_filepath) {
    memset(self, 0, sizeof(*self));
    self->socket_fd = -1;
    self->poll_fd = -1;
    self->wakeup_pipe[0] = -1;
    self->wakeup_pipe[1] = -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_filepath) >= (int)sizeof(addr.sun_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: the ipc socket path is too long, it can be at most %d characters: \"%s\"", (int)sizeof(addr.sun_path) - 1, socket_filepath);
        goto err;
    }

    snprintf(self->socket_filepath, sizeof(self->socket_filepath), "%s", socket_filepath);

    if(pthread_mutex_init(&self->deferred_requests_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the deferred requests mutex");
        goto err;
    }
    self->deferred_requests_mutex_created = true;

    if(pipe(self->wakeup_pipe) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the ipc wakeup pipe, error: %s", strerror(errno));
        self->wakeup_pipe[0] = -1;
        self->wakeup_pipe[1] = -1;
        goto err;
    }

    if(!fd_set_cloexec(self->wakeup_pipe[0]) || !fd_set_cloexec(self->wakeup_pipe[1]) || !fd_set_nonblocking(self->wakeup_pipe[0])) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to setup the ipc wakeup pipe, error: %s", strerror(errno));
        goto err;
    }

    self->socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if(self->socket_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to create the ipc socket, error: %s", strerror(errno));
        goto err;
    }

    if(!ipc_poller_init(self))
        goto err;

    if(!ipc_poller_add(self, self->wakeup_pipe[0]) || !ipc_poller_add(self, self->socket_fd)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to register the ipc sockets for events, error: %s", strerror(errno));
        goto err;
    }

    if(!ipc_bind(self, &addr))
        goto err;

    if(listen(self->socket_fd, GSR_IPC_MAX_CLIENTS) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_init: failed to listen on the ipc socket, error: %s", strerror(errno));
        goto err;
    }

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

    /* Block all signals in the ipc thread to keep the signal handlers running on the main thread */
    sigset_t all_signals;
    sigset_t prev_signals;
    sigfillset(&all_signals);
    pthread_sigmask(SIG_SETMASK, &all_signals, &prev_signals);
    const int thread_create_result = pthread_create(&self->thread, NULL, ipc_thread, self);
    pthread_sigmask(SIG_SETMASK, &prev_signals, NULL);

    if(thread_create_result != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_ipc_start: failed to create the ipc thread, error: %s", strerror(thread_create_result));
        return GSR_ERROR_GENERIC;
    }

    self->thread_running = true;
    return GSR_ERROR_OK;
}

void gsr_ipc_stop(gsr_ipc *self) {
    if(!self->thread_running)
        return;

    ipc_wakeup_thread(self, 'q');
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
        ipc_wakeup_thread(self, 'c');
}
