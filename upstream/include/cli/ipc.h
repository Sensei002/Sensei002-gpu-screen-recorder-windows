#ifndef GSR_CLI_IPC_H
#define GSR_CLI_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>

#define GSR_IPC_MAX_CLIENTS 8
#define GSR_IPC_MAX_REQUEST_SIZE 4096
#define GSR_IPC_MAX_ERROR_MESSAGE_SIZE 256
#define GSR_IPC_MAX_ESCAPED_ERROR_MESSAGE_SIZE (GSR_IPC_MAX_ERROR_MESSAGE_SIZE*6)
#define GSR_IPC_MAX_ESCAPED_DATA_SIZE PATH_MAX
#define GSR_IPC_MAX_REPLY_SIZE (GSR_IPC_MAX_ESCAPED_ERROR_MESSAGE_SIZE + GSR_IPC_MAX_ESCAPED_DATA_SIZE + 128)
#define GSR_IPC_CLIENT_SEND_BUFFER_SIZE (GSR_IPC_MAX_REPLY_SIZE*2)

/*
    These are called from the ipc thread while the recording is running.
    Return false and write a message to |error_message| to reply to the request with an error.
    The reply to the requests that match a |gsr_ipc_deferred_request_type| is not sent when the handler
    succeeds. It's sent when the matching gsr_ipc_complete_request is called.
*/
typedef struct {
    bool (*stop)(char *error_message, size_t error_message_size, void *userdata);
    bool (*toggle_pause)(char *error_message, size_t error_message_size, void *userdata);
    bool (*set_paused)(bool paused, char *error_message, size_t error_message_size, void *userdata);
    bool (*toggle_replay_recording)(char *error_message, size_t error_message_size, void *userdata);
    bool (*start_replay_recording)(char *error_message, size_t error_message_size, void *userdata);
    bool (*stop_replay_recording)(char *error_message, size_t error_message_size, void *userdata);
    /*
        |seconds| is GSR_SAVE_REPLAY_SECONDS_FULL when the whole replay buffer should be saved.
        |restart_replay| overrides the -restart-replay-on-save option for this save when |has_restart_replay| is set.
    */
    bool (*save_replay)(int seconds, bool has_restart_replay, bool restart_replay, char *error_message, size_t error_message_size, void *userdata);
    void *userdata;
} gsr_ipc_handlers;

/* Requests that are replied to when the file they save has been saved (see gsr_ipc_complete_request) */
typedef enum {
    GSR_IPC_DEFERRED_REQUEST_STOP,
    GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY,
    GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING,
    GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT
} gsr_ipc_deferred_request_type;

typedef enum {
    GSR_IPC_DEFERRED_REQUEST_STATE_EMPTY,
    GSR_IPC_DEFERRED_REQUEST_STATE_PENDING,
    GSR_IPC_DEFERRED_REQUEST_STATE_COMPLETED
} gsr_ipc_deferred_request_state;

typedef struct {
    gsr_ipc_deferred_request_state state;
    int client_fd;
    int64_t request_id;
    bool success;
    bool has_filepath;
    char filepath[PATH_MAX];
} gsr_ipc_deferred_request;

typedef struct {
    int fd;
    char request[GSR_IPC_MAX_REQUEST_SIZE];
    size_t request_size;
    bool request_too_large;
    char send_buffer[GSR_IPC_CLIENT_SEND_BUFFER_SIZE];
    size_t send_buffer_size;
} gsr_ipc_client;

/* Receives newline terminated json requests on a unix domain socket and replies to every request */
typedef struct {
    bool initialized;
    int socket_fd;
    int poll_fd;
    bool socket_bound;
    char socket_filepath[PATH_MAX];
    int wakeup_pipe[2];
    gsr_ipc_client clients[GSR_IPC_MAX_CLIENTS];
    int num_clients;
    gsr_ipc_handlers handlers;
    gsr_ipc_deferred_request deferred_requests[GSR_IPC_DEFERRED_REQUEST_TYPE_COUNT];
    pthread_mutex_t deferred_requests_mutex;
    bool deferred_requests_mutex_created;
    pthread_t thread;
    bool thread_running;
} gsr_ipc;

/* Returns a |gsr_error| value. Creates the socket, requests are not handled until gsr_ipc_start is called */
int gsr_ipc_init(gsr_ipc *self, const char *socket_filepath);
void gsr_ipc_deinit(gsr_ipc *self);

/* Returns a |gsr_error| value. Starts handling requests. Does nothing if |self| hasn't been initialized */
int gsr_ipc_start(gsr_ipc *self, const gsr_ipc_handlers *handlers);
/* Stops handling requests. Does nothing if the ipc hasn't been started */
void gsr_ipc_stop(gsr_ipc *self);

/*
    Sends the reply to the pending request of |type|, or does nothing when there is no pending request of that type.
    |filepath| is the path of the saved file and it can be NULL when nothing was saved.
    This is safe to call from any thread. Does nothing if |self| hasn't been initialized.
*/
void gsr_ipc_complete_request(gsr_ipc *self, gsr_ipc_deferred_request_type type, bool success, const char *filepath);

#endif /* GSR_CLI_IPC_H */
