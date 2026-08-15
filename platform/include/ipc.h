/* platform/include/ipc.h — IPC interfaces for the Windows port.
 *
 * Phase 3 deliverable. Two layers:
 *
 *  1. The protocol codec (gsr_platform_ipc_*_build/parse_*) — pure logic,
 *     implemented now (platform/windows/gsr_ipc_protocol.c). The wire
 *     format is byte-identical to upstream (src/cli/ipc.c, tools/gsr-cli):
 *     newline-terminated JSON requests/replies
 *       {"id":1,"name":"save-replay","data":{"seconds":30}}\n
 *       {"id":1,"result":"ok","data":"/path/Replay_....mp4"}\n
 *       {"id":1,"result":"error","data":"message"}\n
 *     plus the deferred-request state machine (stop / save-replay /
 *     stop-replay-recording are replied to only when the save completes).
 *
 *  2. The transport — named pipes (\\\\.\\pipe\\gsr-<pid>) replacing the
 *     upstream Unix socket. Implemented in Phase 11
 *     (platform/windows/ipc.c). The server uses this codec for every
 *     request/reply; gsr-cli.exe uses the client side.
 */
#ifndef GSR_PLATFORM_IPC_H
#define GSR_PLATFORM_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- protocol limits (mirror upstream include/cli/ipc.h) ---------------- */
#define GSR_PLATFORM_IPC_MAX_REQUEST_NAME_SIZE 64
#define GSR_PLATFORM_IPC_MAX_REQUEST_SIZE 4096
#define GSR_PLATFORM_IPC_MAX_ERROR_MESSAGE_SIZE 256
#define GSR_PLATFORM_IPC_MAX_REPLY_SIZE (GSR_PLATFORM_IPC_MAX_ERROR_MESSAGE_SIZE*6 + 260 + 128)

/* ---- request codec ------------------------------------------------------ */

/* Builds a client request: {"id":<id>,"name":"<name>"} + newline, or with
 * |data_json| (raw JSON, no trailing newline) {"id":<id>,"name":"<name>",
 * "data":<data_json>} + newline. Returns the number of bytes written
 * (excluding NUL) or -1 when the buffer is too small. */
int gsr_platform_ipc_build_request(char *buf, size_t size, int64_t id, const char *name, const char *data_json);

/* Parses a server-side request. Validates the shape exactly like upstream
 * ipc_request_parse (root must be an object; 'id' a required integer;
 * 'name' a required string; 'data' optional). On success fills |id|,
 * |name| and |has_data| and returns true. On failure writes a
 * human-readable message into |error_message| and returns false. */
bool gsr_platform_ipc_parse_request(const char *data, size_t size, int64_t *id, char *name, size_t name_size, bool *has_data, char *error_message, size_t error_message_size);

/* ---- reply codec (byte-identical to upstream ipc_client_send_reply) ---- */

/* success + data: {"id":<id>,"result":"ok","data":"<escaped>"}\n
 * success:        {"id":<id>,"result":"ok"}\n
 * failure:        {"id":<id>,"result":"error","data":"<escaped message>"}\n
 * |message_or_data| is the filepath on success-with-data and the error
 * message on failure; may be NULL for a bare success. Returns bytes
 * written (excluding NUL) or -1. */
int gsr_platform_ipc_build_reply(char *buf, size_t size, int64_t id, bool success, const char *message_or_data);

/* ---- deferred requests (mirror upstream gsr_ipc_deferred_request) ------- */

typedef enum {
    GSR_PLATFORM_IPC_DEFERRED_STOP,              /* "stop"                      */
    GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY,       /* "save-replay"               */
    GSR_PLATFORM_IPC_DEFERRED_STOP_REPLAY_RECORDING, /* "stop-replay-recording" */
    GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT
} gsr_platform_ipc_deferred_type;

/* Maps a request name to its deferred type. Returns
 * GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT for names that are answered
 * immediately. */
gsr_platform_ipc_deferred_type gsr_platform_ipc_deferred_type_from_request_name(const char *name);

typedef enum {
    GSR_PLATFORM_IPC_DEFERRED_STATE_EMPTY,
    GSR_PLATFORM_IPC_DEFERRED_STATE_PENDING,
    GSR_PLATFORM_IPC_DEFERRED_STATE_COMPLETED
} gsr_platform_ipc_deferred_state;

typedef struct {
    gsr_platform_ipc_deferred_state state;
    int64_t request_id;
    bool success;
    bool has_filepath;
    char filepath[260];
} gsr_platform_ipc_deferred_request;

/* Marks the deferred request |type| as pending for |request_id|. Returns
 * false when one is already pending (the caller replies with the matching
 * upstream error: "GPU Screen Recorder is already stopping" / "a replay is
 * already being saved" / "the recording is already being stopped"). */
bool gsr_platform_ipc_deferred_set_pending(gsr_platform_ipc_deferred_request *requests, gsr_platform_ipc_deferred_type type, int64_t request_id);

/* Completes the pending deferred request of |type| (idempotent). */
void gsr_platform_ipc_deferred_set_completed(gsr_platform_ipc_deferred_request *requests, gsr_platform_ipc_deferred_type type, bool success, const char *filepath);

/* ---- transport (Phase 11, named pipes) ---------------------------------- */

/* Server: owns the named pipe \\\\.\\pipe\\gsr-<pid>. The pipe name and the
 * accept/read/write loop are Phase 11; the codec above is what every
 * message passes through. */
typedef struct gsr_platform_ipc_server gsr_platform_ipc_server;

/* Client: connects to the pipe, sends one request, waits for the reply.
 * Returns 0 on success (reply parsed by the caller with the codec above),
 * 1 when the engine is not running, -1 on protocol errors. */
typedef struct gsr_platform_ipc_client gsr_platform_ipc_client;

#endif /* GSR_PLATFORM_IPC_H */
