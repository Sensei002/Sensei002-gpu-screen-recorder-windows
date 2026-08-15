/* gsr_ipc_protocol.c — platform/include/ipc.h protocol codec.
 *
 * Phase 3 deliverable. The wire format is byte-identical to upstream
 * (src/cli/ipc.c, tools/gsr-cli/main.c): newline-terminated JSON
 * requests/replies plus the deferred-request state machine. The Phase 11
 * named-pipe transport uses this codec for every message.
 */
#include "../../platform/include/ipc.h"

#include "../../upstream/include/json.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* ---- request codec ------------------------------------------------------- */

int gsr_platform_ipc_build_request(char *buf, size_t size, int64_t id, const char *name, const char *data_json) {
    if(!buf || !name)
        return -1;

    const int written = data_json
        ? snprintf(buf, size, "{\"id\":%" PRId64 ",\"name\":\"%s\",\"data\":%s}\n", id, name, data_json)
        : snprintf(buf, size, "{\"id\":%" PRId64 ",\"name\":\"%s\"}\n", id, name);

    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

bool gsr_platform_ipc_parse_request(const char *data, size_t size, int64_t *id, char *name, size_t name_size, bool *has_data, char *error_message, size_t error_message_size) {
    if(has_data)
        *has_data = false;

    /* sj takes a mutable char*; the reader never writes back (upstream
       passes its read buffer the same way). */
    sj_Reader reader = sj_reader((char*)data, size);
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
            if(has_data)
                *has_data = true;
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

    if(!gsr_json_number_to_int64(&id_value, id)) {
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

    snprintf(name, name_size, "%.*s", (int)(name_value.end - name_value.start), name_value.start);
    return true;
}

/* ---- reply codec --------------------------------------------------------- */

int gsr_platform_ipc_build_reply(char *buf, size_t size, int64_t id, bool success, const char *message_or_data) {
    if(!buf)
        return -1;

    int written = -1;
    if(success && message_or_data) {
        char escaped[GSR_PLATFORM_IPC_MAX_REPLY_SIZE];
        gsr_json_escape_string(escaped, sizeof(escaped), message_or_data);
        written = snprintf(buf, size, "{\"id\":%" PRId64 ",\"result\":\"ok\",\"data\":\"%s\"}\n", id, escaped);
    } else if(success) {
        written = snprintf(buf, size, "{\"id\":%" PRId64 ",\"result\":\"ok\"}\n", id);
    } else {
        char escaped[GSR_PLATFORM_IPC_MAX_REPLY_SIZE];
        gsr_json_escape_string(escaped, sizeof(escaped), message_or_data ? message_or_data : "");
        written = snprintf(buf, size, "{\"id\":%" PRId64 ",\"result\":\"error\",\"data\":\"%s\"}\n", id, escaped);
    }

    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

/* ---- deferred requests ---------------------------------------------------- */

gsr_platform_ipc_deferred_type gsr_platform_ipc_deferred_type_from_request_name(const char *name) {
    if(strcmp(name, "stop") == 0)
        return GSR_PLATFORM_IPC_DEFERRED_STOP;
    if(strcmp(name, "save-replay") == 0)
        return GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY;
    if(strcmp(name, "stop-replay-recording") == 0)
        return GSR_PLATFORM_IPC_DEFERRED_STOP_REPLAY_RECORDING;
    return GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT;
}

bool gsr_platform_ipc_deferred_set_pending(gsr_platform_ipc_deferred_request *requests, gsr_platform_ipc_deferred_type type, int64_t request_id) {
    if(!requests || type >= GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT)
        return false;

    gsr_platform_ipc_deferred_request *request = &requests[type];
    if(request->state == GSR_PLATFORM_IPC_DEFERRED_STATE_PENDING)
        return false;

    *request = (gsr_platform_ipc_deferred_request){0};
    request->state = GSR_PLATFORM_IPC_DEFERRED_STATE_PENDING;
    request->request_id = request_id;
    return true;
}

void gsr_platform_ipc_deferred_set_completed(gsr_platform_ipc_deferred_request *requests, gsr_platform_ipc_deferred_type type, bool success, const char *filepath) {
    if(!requests || type >= GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT)
        return;

    gsr_platform_ipc_deferred_request *request = &requests[type];
    if(request->state != GSR_PLATFORM_IPC_DEFERRED_STATE_PENDING)
        return;

    request->state = GSR_PLATFORM_IPC_DEFERRED_STATE_COMPLETED;
    request->success = success;
    request->has_filepath = filepath != NULL;
    if(filepath)
        snprintf(request->filepath, sizeof(request->filepath), "%s", filepath);
}
