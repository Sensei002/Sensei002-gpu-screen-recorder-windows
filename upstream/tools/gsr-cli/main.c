#include "../../include/json.h"
#include "../../include/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#define GSR_CLI_REQUEST_ID 1
#define GSR_CLI_MAX_REQUEST_SIZE 256
#define GSR_CLI_MAX_REPLY_SIZE 8192
#define GSR_CLI_REPLY_TIMEOUT_SECONDS 10
#define GSR_CLI_NO_REPLY_TIMEOUT 0

static void usage(void) {
    printf("usage: gsr-cli -ipc <socket_path> <command> [command_arguments...]\n");
    printf("\n");
    printf("Sends a command to a GPU Screen Recorder instance that was started with the -ipc option.\n");
    printf("\n");
    printf("OPTIONS:\n");
    printf("  -ipc <socket_path>\n");
    printf("    The unix domain socket that GPU Screen Recorder was started with. Required.\n");
    printf("\n");
    printf("COMMANDS:\n");
    printf("  status\n");
    printf("    Check if a GPU Screen Recorder instance is listening on the socket. Prints \"running\" or\n");
    printf("    \"not running\" and exits with 0 when it's running.\n");
    printf("  stop\n");
    printf("    Stop and save the recording (stop without save in replay mode). Waits until the recording\n");
    printf("    has been saved and prints the path of the saved file.\n");
    printf("  toggle-pause\n");
    printf("    Pause/unpause the recording (not for streaming/replay).\n");
    printf("  set-paused true|false\n");
    printf("    Pause/unpause the recording (not for streaming/replay). Unlike toggle-pause this doesn't\n");
    printf("    fail when the recording is already paused/unpaused.\n");
    printf("  toggle-replay-recording\n");
    printf("    Start/stop a regular recording during replay/streaming.\n");
    printf("  start-replay-recording\n");
    printf("    Start a regular recording during replay/streaming. Does nothing when a recording is already running.\n");
    printf("  stop-replay-recording\n");
    printf("    Stop the regular recording that runs during replay/streaming. Waits until the recording\n");
    printf("    has been saved and prints the path of the saved file.\n");
    printf("  save-replay [seconds] [restart-replay=true|false]\n");
    printf("    Save the replay. The number of seconds has to be larger than 0. The whole replay buffer is\n");
    printf("    saved when no number of seconds is given. Waits until the replay has been saved and prints\n");
    printf("    the path of the saved file. restart-replay overrides the -restart-replay-on-save option\n");
    printf("    of GPU Screen Recorder for this save.\n");
    printf("\n");
    printf("EXAMPLES:\n");
    printf("  gsr-cli -ipc \"$XDG_RUNTIME_DIR/gsr.sock\" status\n");
    printf("  gsr-cli -ipc \"$XDG_RUNTIME_DIR/gsr.sock\" save-replay 30\n");
    printf("  gsr-cli -ipc \"$XDG_RUNTIME_DIR/gsr.sock\" save-replay restart-replay=true\n");
    fflush(stdout);
}

static bool string_to_int64(const char *str, int64_t *result) {
    char *number_end = NULL;
    errno = 0;
    const long long parsed_value = strtoll(str, &number_end, 10);
    if(errno != 0 || number_end == str || *number_end != '\0')
        return false;

    *result = parsed_value;
    return true;
}

/* Returns the socket, or -1 on failure. Only logs an error when the failure isn't a missing GPU Screen Recorder instance */
static int ipc_connect(const char *socket_filepath, int reply_timeout_seconds) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_filepath) >= (int)sizeof(addr.sun_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "the ipc socket path is too long, it can be at most %d characters: \"%s\"", (int)sizeof(addr.sun_path) - 1, socket_filepath);
        return -1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create a socket, error: %s", strerror(errno));
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = reply_timeout_seconds;
    timeout.tv_usec = 0;
    if(reply_timeout_seconds != GSR_CLI_NO_REPLY_TIMEOUT)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    timeout.tv_sec = GSR_CLI_REPLY_TIMEOUT_SECONDS;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if(connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

static bool ipc_send_all(int fd, const char *data, size_t size) {
    size_t offset = 0;
    while(offset < size) {
        const ssize_t bytes_written = send(fd, data + offset, size - offset, MSG_NOSIGNAL);
        if(bytes_written > 0) {
            offset += bytes_written;
            continue;
        }

        if(bytes_written == -1 && errno == EINTR)
            continue;

        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to send the request, error: %s", strerror(errno));
        return false;
    }
    return true;
}

/* Reads until a newline. |reply_size| is set to the size of the reply, excluding the newline */
static bool ipc_receive_reply(int fd, char *reply, size_t reply_capacity, size_t *reply_size) {
    size_t offset = 0;
    for(;;) {
        if(offset == reply_capacity) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "the reply is too large");
            return false;
        }

        const ssize_t bytes_read = recv(fd, reply + offset, reply_capacity - offset, 0);
        if(bytes_read == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "GPU Screen Recorder closed the connection before replying");
            return false;
        }

        if(bytes_read == -1) {
            if(errno == EINTR)
                continue;

            if(errno == EAGAIN || errno == EWOULDBLOCK)
                gsr_log(GSR_LOG_LEVEL_ERROR, "timed out after %d seconds waiting for a reply", GSR_CLI_REPLY_TIMEOUT_SECONDS);
            else
                gsr_log(GSR_LOG_LEVEL_ERROR, "failed to receive the reply, error: %s", strerror(errno));
            return false;
        }

        const char *newline = memchr(reply + offset, '\n', bytes_read);
        offset += bytes_read;
        if(newline) {
            *reply_size = newline - reply;
            return true;
        }
    }
}

static void print_json_string(const char *str, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        char c = str[i];
        if(c == '\\' && i + 1 < size) {
            ++i;
            switch(str[i]) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default:  c = str[i]; break;
            }
        }
        putchar(c);
    }
    putchar('\n');
}

/* Returns the exit code that gsr-cli should exit with */
static int ipc_handle_reply(char *reply, size_t reply_size, int64_t request_id) {
    sj_Reader reader = sj_reader(reply, reply_size);
    const sj_Value root = sj_read(&reader);
    if(root.type != SJ_OBJECT) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "expected the reply to be a json object, got: %.*s", (int)reply_size, reply);
        return 1;
    }

    int64_t id = 0;
    bool has_id = false;
    sj_Value result_value;
    bool has_result = false;
    sj_Value data_value;
    bool has_data = false;

    sj_Value key;
    sj_Value value;
    while(sj_iter_object(&reader, root, &key, &value)) {
        if(gsr_json_string_equals(&key, "id")) {
            has_id = gsr_json_number_to_int64(&value, &id);
        } else if(gsr_json_string_equals(&key, "result")) {
            result_value = value;
            has_result = value.type == SJ_STRING;
        } else if(gsr_json_string_equals(&key, "data")) {
            data_value = value;
            has_data = true;
        }
    }

    if(reader.error) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to parse the reply: %s", reader.error);
        return 1;
    }

    if(!has_id || id != request_id) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "received a reply to another request: %.*s", (int)reply_size, reply);
        return 1;
    }

    if(!has_result) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "the reply is missing the 'result' field: %.*s", (int)reply_size, reply);
        return 1;
    }

    if(gsr_json_string_equals(&result_value, "ok")) {
        if(has_data && data_value.type == SJ_STRING)
            print_json_string(data_value.start, data_value.end - data_value.start);
        return 0;
    }

    if(has_data && data_value.type == SJ_STRING)
        gsr_log(GSR_LOG_LEVEL_ERROR, "%.*s", (int)(data_value.end - data_value.start), data_value.start);
    else
        gsr_log(GSR_LOG_LEVEL_ERROR, "the request failed: %.*s", (int)reply_size, reply);

    return 1;
}

static int status_command(const char *socket_filepath) {
    const int fd = ipc_connect(socket_filepath, GSR_CLI_REPLY_TIMEOUT_SECONDS);
    if(fd == -1) {
        printf("not running\n");
        fflush(stdout);
        return 1;
    }

    close(fd);
    printf("running\n");
    fflush(stdout);
    return 0;
}

static int send_request(const char *socket_filepath, const char *request, int reply_timeout_seconds) {
    const int fd = ipc_connect(socket_filepath, reply_timeout_seconds);
    if(fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to connect to \"%s\". Is GPU Screen Recorder running with the -ipc option?", socket_filepath);
        return 1;
    }

    int exit_code = 1;
    char reply[GSR_CLI_MAX_REPLY_SIZE];
    size_t reply_size = 0;
    if(ipc_send_all(fd, request, strlen(request)) && ipc_receive_reply(fd, reply, sizeof(reply), &reply_size))
        exit_code = ipc_handle_reply(reply, reply_size, GSR_CLI_REQUEST_ID);

    close(fd);
    return exit_code;
}

int main(int argc, char **argv) {
    if(argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }

    if(argc < 4 || strcmp(argv[1], "-ipc") != 0) {
        usage();
        return 1;
    }

    if(argc > 6) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "too many arguments");
        usage();
        return 1;
    }

    const char *socket_filepath = argv[2];
    const char *command = argv[3];
    const char *command_argument = argc >= 5 ? argv[4] : NULL;
    const char *command_argument2 = argc >= 6 ? argv[5] : NULL;
    char request[GSR_CLI_MAX_REQUEST_SIZE];

    if(strcmp(command, "save-replay") == 0) {
        bool has_seconds = false;
        int64_t seconds = 0;
        const char *restart_replay = NULL;

        const char *command_arguments[2] = { command_argument, command_argument2 };
        for(int i = 0; i < 2; ++i) {
            const char *argument = command_arguments[i];
            if(!argument)
                continue;

            if(strncmp(argument, "restart-replay=", 15) == 0) {
                restart_replay = argument + 15;
                if(strcmp(restart_replay, "true") != 0 && strcmp(restart_replay, "false") != 0) {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "expected restart-replay to be either true or false, got: '%s'", restart_replay);
                    return 1;
                }
            } else if(!has_seconds && string_to_int64(argument, &seconds) && seconds > 0 && seconds <= INT_MAX) {
                has_seconds = true;
            } else {
                gsr_log(GSR_LOG_LEVEL_ERROR, "expected the argument to be the number of seconds to save (an integer larger than 0) or restart-replay=true|false, got: '%s'", argument);
                return 1;
            }
        }

        if(restart_replay && has_seconds)
            snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"save-replay\",\"data\":{\"seconds\":%" PRIi64 ",\"restart-replay\":%s}}\n", GSR_CLI_REQUEST_ID, seconds, restart_replay);
        else if(restart_replay)
            snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"save-replay\",\"data\":{\"restart-replay\":%s}}\n", GSR_CLI_REQUEST_ID, restart_replay);
        else if(has_seconds)
            snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"save-replay\",\"data\":{\"seconds\":%" PRIi64 "}}\n", GSR_CLI_REQUEST_ID, seconds);
        else
            snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"save-replay\"}\n", GSR_CLI_REQUEST_ID);

        return send_request(socket_filepath, request, GSR_CLI_NO_REPLY_TIMEOUT);
    }

    if(command_argument2) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "the '%s' command doesn't take more than one argument", command);
        usage();
        return 1;
    }

    if(strcmp(command, "set-paused") == 0) {
        if(!command_argument || (strcmp(command_argument, "true") != 0 && strcmp(command_argument, "false") != 0)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "the 'set-paused' command expects either true or false as the argument");
            usage();
            return 1;
        }
        snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"set-paused\",\"data\":%s}\n", GSR_CLI_REQUEST_ID, command_argument);
        return send_request(socket_filepath, request, GSR_CLI_REPLY_TIMEOUT_SECONDS);
    }

    if(command_argument) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "the '%s' command doesn't take an argument", command);
        usage();
        return 1;
    }

    if(strcmp(command, "status") == 0)
        return status_command(socket_filepath);

    if(strcmp(command, "toggle-pause") == 0 || strcmp(command, "toggle-replay-recording") == 0 || strcmp(command, "start-replay-recording") == 0) {
        snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"%s\"}\n", GSR_CLI_REQUEST_ID, command);
        return send_request(socket_filepath, request, GSR_CLI_REPLY_TIMEOUT_SECONDS);
    }

    if(strcmp(command, "stop") == 0 || strcmp(command, "stop-replay-recording") == 0) {
        snprintf(request, sizeof(request), "{\"id\":%d,\"name\":\"%s\"}\n", GSR_CLI_REQUEST_ID, command);
        return send_request(socket_filepath, request, GSR_CLI_NO_REPLY_TIMEOUT);
    }

    gsr_log(GSR_LOG_LEVEL_ERROR, "invalid command '%s'", command);
    usage();
    return 1;
}
