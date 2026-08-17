#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define write _write
#else
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifdef _WIN32
static const char *pipe_full_name(void) {
    static char full_name[64];
    snprintf(full_name, sizeof(full_name), "\\\\.\\pipe\\gsr-ui");
    return full_name;
}

/* Assumes |str| size is less than 256 */
static bool file_write_all(HANDLE file, const char *str) {
    char command[256];
    const int command_size = snprintf(command, sizeof(command), "%s\n", str);
    if(command_size >= (int)sizeof(command)) {
        fprintf(stderr, "Error: command too long: %s\n", str);
        return false;
    }

    DWORD offset = 0;
    while(offset < (DWORD)command_size) {
        DWORD bytes_written = 0;
        if(!WriteFile(file, command + offset, (DWORD)(command_size - offset), &bytes_written, NULL))
            return false;
        if(bytes_written > 0)
            offset += bytes_written;
    }
    return true;
}
#else
static bool build_abstract_address(const char *name, struct sockaddr_un *addr, socklen_t *addrlen_out) {
    char dir[PATH_MAX];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if(runtime_dir)
        snprintf(dir, sizeof(dir), "%s", runtime_dir);
    else
        snprintf(dir, sizeof(dir), "/run/user/%d", geteuid());

    if(access(dir, F_OK) != 0)
        snprintf(dir, sizeof(dir), "/tmp");

    /* Stay human-readable so the name shows up sensibly in
        /proc/net/unix and `ss -xa`. Abstract names print with the
        leading NUL rendered as '@'. */
    char path[PATH_MAX];
    const int path_len = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if(path_len <= 0)
        return false;
    /* Need room for the leading NUL byte plus path_len bytes of name. */
    if((size_t)path_len + 1 > sizeof(addr->sun_path))
        return false;

    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    memcpy(addr->sun_path + 1, path, (size_t)path_len);
    *addrlen_out = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + (size_t)path_len);
    return true;
}

/* Assumes |str| size is less than 256 */
static void file_write_all(int file_fd, const char *str) {
    char command[256];
    const ssize_t command_size = snprintf(command, sizeof(command), "%s\n", str);
    if(command_size >= (ssize_t)sizeof(command)) {
        fprintf(stderr, "Error: command too long: %s\n", str);
        return;
    }

    ssize_t offset = 0;
    while(offset < (ssize_t)command_size) {
        const ssize_t bytes_written = write(file_fd, command + offset, command_size - offset);
        if(bytes_written > 0)
            offset += bytes_written;
    }
}
#endif

static void usage(void) {
    printf("usage: gsr-ui-cli <command>\n");
    printf("Run commands on the running gsr-ui instance.\n");
    printf("\n");
    printf("COMMANDS:\n");
    printf("  toggle-show\n");
    printf("      Show/hide the UI.\n");
    printf("  toggle-record\n");
    printf("      Start/stop recording.\n");
    printf("  toggle-pause\n");
    printf("      Pause/unpause recording. Only applies to regular recording.\n");
    printf("  toggle-record-region\n");
    printf("      Start/stop recording a region.\n");
    printf("  toggle-record-window\n");
    printf("      Start/stop recording a window (or desktop portal on Wayland).\n");
    printf("  toggle-stream\n");
    printf("      Start/stop streaming.\n");
    printf("  toggle-replay\n");
    printf("      Start/stop replay.\n");
    printf("  replay-save\n");
    printf("      Save replay.\n");
    printf("  replay-save-1-min\n");
    printf("      Save 1 minute replay.\n");
    printf("  replay-save-10-min\n");
    printf("      Save 10 minute replay.\n");
    printf("  take-screenshot\n");
    printf("      Take a screenshot.\n");
    printf("  take-screenshot-region\n");
    printf("      Take a screenshot of a region.\n");
    printf("  take-screenshot-window\n");
    printf("      Take a screenshot of a window (or desktop portal on Wayland).\n");
    printf("\n");
    printf("EXAMPLES:\n");
    printf("  gsr-ui-cli toggle-show\n");
    printf("  gsr-ui-cli toggle-record\n");
    exit(1);
}

static bool is_valid_command(const char *command) {
    const char *commands[] = {
        "toggle-show",
        "toggle-record",
        "toggle-pause",
        "toggle-record-region",
        "toggle-record-window",
        "toggle-stream",
        "toggle-replay",
        "replay-save",
        "replay-save-1-min",
        "replay-save-10-min",
        "take-screenshot",
        "take-screenshot-region",
        "take-screenshot-window",
        NULL
    };

    for(int i = 0; commands[i]; ++i) {
        if(strcmp(command, commands[i]) == 0)
            return true;
    }

    return false;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        printf("Error: expected 1 argument, %d provided\n", argc - 1);
        usage();
    }

    const char *command = argv[1];
    if(strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0)
        usage();

    if(!is_valid_command(command)) {
        fprintf(stderr, "Error: invalid command: \"%s\"\n", command);
        usage();
    }

#ifdef _WIN32
    const char *full_name = pipe_full_name();
    HANDLE file = NULL;
    int retries = 0;
    for(;;) {
        file = CreateFileA(full_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if(file != INVALID_HANDLE_VALUE)
            break;
        const DWORD err = GetLastError();
        if(err == ERROR_PIPE_BUSY) {
            /* Bounded retry: fail after ~5s of busy instances. */
            if(++retries > 50) {
                fprintf(stderr, "Error: gsr-ui pipe is busy, giving up. Is gsr-ui running?\n");
                exit(2);
            }
            if(!WaitNamedPipeA(full_name, 100))
                continue;
        } else {
            fprintf(stderr, "Error: failed to connect, error: %lu. Maybe gsr-ui is not running?\n", (unsigned long)err);
            exit(2);
        }
    }

    if(!file_write_all(file, command))
        fprintf(stderr, "Error: failed to write command\n");
    CloseHandle(file);
    return 0;
#else
    struct sockaddr_un addr;
    socklen_t addrlen = 0;
    if(!build_abstract_address("gsr-ui", &addr, &addrlen)) {
        fprintf(stderr, "Error: Rpc::create: name too long\n");
        return false;
    }

    const int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(socket_fd <= 0) {
        fprintf(stderr, "Error: failed to create socket\n");
        exit(2);
    }

    for(;;) {
        if(connect(socket_fd, (struct sockaddr*)&addr, addrlen) == -1) {
            const int err = errno;
            if(err == EWOULDBLOCK) {
                usleep(10 * 1000);
            } else {
                fprintf(stderr, "Error: failed to connect, error: %s. Maybe gsr-ui is not running?\n", strerror(err));
                exit(2);
            }
        } else {
            break;
        }
    }

    file_write_all(socket_fd, command);
    close(socket_fd);
    return 0;
#endif
}
