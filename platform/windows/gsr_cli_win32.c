/* gsr_cli_win32.c — gsr-cli.exe, the Windows port of upstream
 * tools/gsr-cli/main.c (Phase 11). The CLI contract is unchanged
 * (gsr-cli -ipc <pipe> <command> [args]); the transport is the named-pipe
 * client from gsr_ipc_win32.c instead of a Unix domain socket. Output and
 * exit codes match upstream byte for byte.
 */
#include "../../upstream/include/json.h"
#include "../../upstream/include/log.h"
#include "../../platform/include/ipc.h" /* gsr_platform_ipc_build_request */
#include "gsr_ipc_client_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <windows.h>

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
    printf("    The ipc pipe name that GPU Screen Recorder was started with. Required.\n");
    printf("\n");
    printf("COMMANDS:\n");
    printf("  status\n");
    printf("    Check if a GPU Screen Recorder instance is listening on the pipe. Prints \"running\" or\n");
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
    printf("  gsr-cli -ipc gsr-myrec status\n");
    printf("  gsr-cli -ipc gsr-myrec save-replay 30\n");
    printf("  gsr-cli -ipc gsr-myrec save-replay restart-replay=true\n");
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
    sj_Value result_value = {0};
    bool has_result = false;
    sj_Value data_value = {0};
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
    const HANDLE pipe = gsr_platform_ipc_client_connect(socket_filepath, GSR_CLI_REPLY_TIMEOUT_SECONDS);
    if(pipe == INVALID_HANDLE_VALUE) {
        printf("not running\n");
        fflush(stdout);
        return 1;
    }

    gsr_platform_ipc_client_disconnect(pipe);
    printf("running\n");
    fflush(stdout);
    return 0;
}

static int send_request(const char *socket_filepath, const char *request, int reply_timeout_seconds) {
    const HANDLE pipe = gsr_platform_ipc_client_connect(socket_filepath, reply_timeout_seconds);
    if(pipe == INVALID_HANDLE_VALUE) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to connect to \"%s\". Is GPU Screen Recorder running with the -ipc option?", socket_filepath);
        return 1;
    }

    int exit_code = 1;
    char reply[GSR_CLI_MAX_REPLY_SIZE];
    size_t reply_size = 0;
    if(gsr_platform_ipc_client_send_all(pipe, request, strlen(request)) && gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size))
        exit_code = ipc_handle_reply(reply, reply_size, GSR_CLI_REQUEST_ID);

    gsr_platform_ipc_client_disconnect(pipe);
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
