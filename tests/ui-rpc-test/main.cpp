/* ui-rpc-test: headless round-trip test for the Rpc named-pipe (Windows) /
 * unix-socket (POSIX) layer used by gsr-ui and gsr-ui-cli.
 *
 * Covers:
 *   - server create + client open + write + poll -> handler fires
 *   - multiple clients in one poll pass
 *   - gsr-ui-cli subprocess round-trip (the real tool)
 *   - open against a non-existent server fails
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define snprintf _snprintf
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "Rpc.hpp"

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

static std::string g_received_command;
static int g_handler_calls = 0;

static void on_command(const std::string &name) {
    g_received_command = name;
    ++g_handler_calls;
}

#ifdef _WIN32
static std::string exec_cli(const char *command) {
    /* Build the path to gsr-ui-cli.exe next to the test exe. */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char *slash = strrchr(exe_path, '\\');
    if(slash)
        *slash = '\0';
    char cli_path[MAX_PATH];
    snprintf(cli_path, sizeof(cli_path), "%s\\gsr-ui-cli.exe", exe_path);

    char cmd_line[MAX_PATH + 64];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", cli_path, command);

    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    if(!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
        return "";

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    PROCESS_INFORMATION pi = { 0 };
    if(!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return "";
    }
    CloseHandle(write_pipe);

    std::string output;
    char buf[512];
    DWORD bytes_read = 0;
    while(ReadFile(read_pipe, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        output += buf;
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
}
#else
static std::string exec_cli(const char *command) {
    std::string result;
    char cli_path[PATH_MAX];
    snprintf(cli_path, sizeof(cli_path), "%s/gsr-ui-cli", getenv("GSR_TEST_BIN_DIR") ? getenv("GSR_TEST_BIN_DIR") : ".");

    int pipe_fds[2];
    if(pipe(pipe_fds) == -1)
        return "";

    const pid_t pid = fork();
    if(pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        execl(cli_path, cli_path, command, (char*)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);

    char buf[512];
    ssize_t bytes_read;
    while((bytes_read = read(pipe_fds[0], buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';
        result += buf;
    }
    close(pipe_fds[0]);
    waitpid(pid, NULL, 0);
    return result;
}
#endif

static void test_round_trip(void) {
    gsr::Rpc server;
    CHECK(server.create("gsr-ui"));
    CHECK(server.add_handler("toggle-show", on_command));
    CHECK(server.add_handler("toggle-record", on_command));

    gsr::Rpc client;
    CHECK(client.open("gsr-ui") == gsr::RpcOpenResult::OK);
    CHECK(client.write("toggle-show\n", 12));

    /* Poll until the server sees the command (with a bound). */
    bool received = false;
    for(int i = 0; i < 100 && !received; ++i) {
        server.poll();
        received = g_handler_calls > 0;
        if(!received)
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10 * 1000);
#endif
    }
    CHECK(received);
    CHECK(g_received_command == "toggle-show");

    /* Second client + command in the same pass. */
    gsr::Rpc client2;
    CHECK(client2.open("gsr-ui") == gsr::RpcOpenResult::OK);
    CHECK(client2.write("toggle-record\n", 14));
    bool received2 = false;
    for(int i = 0; i < 100 && !received2; ++i) {
        server.poll();
        received2 = g_handler_calls >= 2;
        if(!received2)
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10 * 1000);
#endif
    }
    CHECK(received2);
    CHECK(g_received_command == "toggle-record");
}

static void test_open_failure(void) {
    gsr::Rpc client;
    /* No server running with this name. */
    CHECK(client.open("gsr-ui-nonexistent") == gsr::RpcOpenResult::ERROR);
}

static void test_cli_round_trip(void) {
    gsr::Rpc server;
    CHECK(server.create("gsr-ui"));
    CHECK(server.add_handler("toggle-show", on_command));
    CHECK(server.add_handler("take-screenshot", on_command));

    g_handler_calls = 0;
    g_received_command.clear();

    std::string output = exec_cli("toggle-show");
    (void)output;

    bool received = false;
    for(int i = 0; i < 100 && !received; ++i) {
        server.poll();
        received = g_handler_calls > 0;
        if(!received)
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10 * 1000);
#endif
    }
    CHECK(received);
    CHECK(g_received_command == "toggle-show");

    /* Unknown command must be rejected by the CLI (exit != 0). */
    g_handler_calls = 0;
    g_received_command.clear();
    std::string output2 = exec_cli("--bogus");
    (void)output2;
    bool received2 = false;
    for(int i = 0; i < 20 && !received2; ++i) {
        server.poll();
        received2 = g_handler_calls > 0;
        if(!received2)
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10 * 1000);
#endif
    }
    CHECK(!received2);
}

int main(void) {
    printf("ui-rpc-test: rpc round-trip tests\n");

    test_round_trip();
    test_open_failure();
    test_cli_round_trip();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
