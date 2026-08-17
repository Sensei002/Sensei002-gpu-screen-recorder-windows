/* tests/engine-ipc-test/main.c — Phase 11 engine IPC test.
 *
 * Part 1 (always runs, headless): the named-pipe transport from
 * platform/windows/gsr_ipc_win32.c exercised in-process — a server with
 * test handlers, clients connecting through the same client API gsr-cli
 * uses, and assertions on:
 *   - the ok reply JSON, the error reply JSON (unknown request name, bad
 *     set-paused data), the "expected a json object" parse error;
 *   - the deferred-request flow: a save-replay request gets no immediate
 *     reply, gsr_ipc_complete_request (from another thread) delivers the
 *     reply with the saved filepath;
 *   - the already-pending guard ("a replay is already being saved");
 *   - the client disconnect path (a second client mid-deferred).
 *
 * Part 2 (SKIPs like recorder-self-test when ANGLE is unavailable): spawns
 * the real gpu-screen-recorder.exe with -ipc and drives it through gsr-cli:
 * status -> running, toggle-pause -> ok, stop -> the saved filepath, and
 * the engine exits 0 with the recording on disk.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3q.
 */
#include "../../upstream/include/cli/ipc.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/replay_save.h" /* GSR_SAVE_REPLAY_SECONDS_FULL */
#include "../../upstream/include/egl.h"
#include "../../upstream/include/window/window.h" /* full gsr_window struct */
#include "../../upstream/include/log.h"
#include "../../platform/include/ipc.h" /* codec + client API */
#include "../../platform/include/display.h" /* primary monitor for -w */
#include "gsr_ipc_client_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/* part 1: in-process transport                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    bool stop_called;
    bool toggle_pause_called;
    bool set_paused_called;
    bool set_paused_value;
    bool save_replay_called;
    int save_replay_seconds;
    bool has_restart_replay;
    bool restart_replay;
} handler_state;

static bool test_stop_handler(char *error_message, size_t error_message_size, void *userdata) {
    (void)error_message;
    (void)error_message_size;
    ((handler_state*)userdata)->stop_called = true;
    return true;
}

static bool test_toggle_pause_handler(char *error_message, size_t error_message_size, void *userdata) {
    (void)error_message;
    (void)error_message_size;
    ((handler_state*)userdata)->toggle_pause_called = true;
    return true;
}

static bool test_set_paused_handler(bool paused, char *error_message, size_t error_message_size, void *userdata) {
    (void)error_message;
    (void)error_message_size;
    handler_state *state = userdata;
    state->set_paused_called = true;
    state->set_paused_value = paused;
    return true;
}

static bool test_save_replay_handler(int seconds, bool has_restart_replay, bool restart_replay, char *error_message, size_t error_message_size, void *userdata) {
    (void)error_message;
    (void)error_message_size;
    handler_state *state = userdata;
    state->save_replay_called = true;
    state->save_replay_seconds = seconds;
    state->has_restart_replay = has_restart_replay;
    state->restart_replay = restart_replay;
    return true;
}

/* Deferred-completion thread: sleeps briefly then completes the request,
   like the recorder's save callback would. */
typedef struct {
    gsr_ipc *ipc;
    gsr_ipc_deferred_request_type type;
    bool success;
    const char *filepath;
} complete_thread_params;

static void* complete_request_thread(void *userdata) {
    complete_thread_params *params = userdata;
    Sleep(50);
    gsr_ipc_complete_request(params->ipc, params->type, params->success, params->filepath);
    return NULL;
}

static void test_transport(void) {
    printf("-- ipc transport (named pipes)\n");

    char pipe_name[128];
    snprintf(pipe_name, sizeof(pipe_name), "gsr-ipc-test-%lu", (unsigned long)GetCurrentProcessId());

    gsr_ipc ipc;
    memset(&ipc, 0, sizeof(ipc));
    CHECK(gsr_ipc_init(&ipc, pipe_name) == GSR_ERROR_OK);

    handler_state state;
    memset(&state, 0, sizeof(state));

    gsr_ipc_handlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.stop = test_stop_handler;
    handlers.toggle_pause = test_toggle_pause_handler;
    handlers.set_paused = test_set_paused_handler;
    handlers.save_replay = test_save_replay_handler;
    handlers.userdata = &state;

    CHECK(gsr_ipc_start(&ipc, &handlers) == GSR_ERROR_OK);

    char request[256];
    char reply[1024];
    size_t reply_size = 0;

    /* 1. toggle-pause: immediate ok reply. */
    int n = gsr_platform_ipc_build_request(request, sizeof(request), 1, "toggle-pause", NULL);
    CHECK(n > 0);
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 5);
        CHECK(pipe != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe, request, (size_t)n));
        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":1,\"result\":\"ok\"}"));
        CHECK(strncmp(reply, "{\"id\":1,\"result\":\"ok\"}", reply_size) == 0);
        CHECK(state.toggle_pause_called);
        gsr_platform_ipc_client_disconnect(pipe);
    }

    /* 2. set-paused true: ok reply, handler saw the value. */
    n = gsr_platform_ipc_build_request(request, sizeof(request), 2, "set-paused", "true");
    CHECK(n > 0);
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 5);
        CHECK(pipe != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe, request, (size_t)n));
        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":2,\"result\":\"ok\"}"));
        CHECK(state.set_paused_called && state.set_paused_value);
        gsr_platform_ipc_client_disconnect(pipe);
    }

    /* 3. unknown request: error reply with the exact upstream message. */
    n = gsr_platform_ipc_build_request(request, sizeof(request), 3, "bogus", NULL);
    CHECK(n > 0);
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 5);
        if(pipe == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 3 failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe, request, (size_t)n));
        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":3,\"result\":\"error\",\"data\":\"unknown request name 'bogus'\"}"));
        CHECK(strncmp(reply, "{\"id\":3,\"result\":\"error\",\"data\":\"unknown request name 'bogus'\"}", reply_size) == 0);
        gsr_platform_ipc_client_disconnect(pipe);
    }
    printf("   [transport] after request 3\n");

    /* 4. malformed request (not json): parse error reply. */
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 5);
        if(pipe == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 4 failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe != INVALID_HANDLE_VALUE);
        const char *garbage = "this is not json\n";
        CHECK(gsr_platform_ipc_client_send_all(pipe, garbage, strlen(garbage)));
        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(strstr(reply, "\"result\":\"error\"") != NULL);
        CHECK(strstr(reply, "expected the request to be a json object") != NULL);
        gsr_platform_ipc_client_disconnect(pipe);
    }
    printf("   [transport] after request 4\n");

    /* 5. deferred: save-replay gets no immediate reply; the completion
       thread (another thread, like the recorder save callback) delivers it
       with the saved filepath. */
    n = gsr_platform_ipc_build_request(request, sizeof(request), 5, "save-replay", "{\"seconds\":30}");
    CHECK(n > 0);
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 15);
        if(pipe == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 5 failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe, request, (size_t)n));

        complete_thread_params params;
        params.ipc = &ipc;
        params.type = GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY;
        params.success = true;
        params.filepath = "C:\\\\Users\\\\test\\\\Replay_2026-08-05_14-04-22.mp4";
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, complete_request_thread, &params) == 0);

        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":5,\"result\":\"ok\",\"data\":\"C:\\\\Users\\\\test\\\\Replay_2026-08-05_14-04-22.mp4\"}"));
        CHECK(strncmp(reply, "{\"id\":5,\"result\":\"ok\",\"data\":\"C:\\\\Users\\\\test\\\\Replay_2026-08-05_14-04-22.mp4\"}", reply_size) == 0);
        CHECK(state.save_replay_called && state.save_replay_seconds == 30);
        pthread_join(thread, NULL);
        gsr_platform_ipc_client_disconnect(pipe);
    }
    printf("   [transport] after request 5\n");

    /* 6. already-pending: a second save-replay while the first is pending
       gets the exact upstream error (no immediate completion this time). */
    n = gsr_platform_ipc_build_request(request, sizeof(request), 6, "save-replay", NULL);
    CHECK(n > 0);
    {
        const HANDLE pipe1 = gsr_platform_ipc_client_connect(pipe_name, 15);
        if(pipe1 == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 6a failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe1 != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe1, request, (size_t)n));

        /* Give the server a moment to register the pending request. */
        Sleep(100);

        const HANDLE pipe2 = gsr_platform_ipc_client_connect(pipe_name, 5);
        if(pipe2 == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 6b failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe2 != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe2, request, (size_t)n));
        CHECK(gsr_platform_ipc_client_receive_reply(pipe2, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":6,\"result\":\"error\",\"data\":\"a replay is already being saved\"}"));
        gsr_platform_ipc_client_disconnect(pipe2);

        /* Complete pipe1's pending request. */
        gsr_ipc_complete_request(&ipc, GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY, true, "C:\\\\Users\\\\test\\\\Replay_2.mp4");
        CHECK(gsr_platform_ipc_client_receive_reply(pipe1, reply, sizeof(reply), &reply_size));
        CHECK(strstr(reply, "\"result\":\"ok\"") != NULL);
        gsr_platform_ipc_client_disconnect(pipe1);
    }
    printf("   [transport] after request 6\n");

    /* 7. stop: deferred, completed with success. */
    n = gsr_platform_ipc_build_request(request, sizeof(request), 7, "stop", NULL);
    CHECK(n > 0);
    {
        const HANDLE pipe = gsr_platform_ipc_client_connect(pipe_name, 15);
        if(pipe == INVALID_HANDLE_VALUE)
            fprintf(stderr, "   [transport] connect 7 failed, GetLastError: %lu\n", (unsigned long)GetLastError());
        CHECK(pipe != INVALID_HANDLE_VALUE);
        CHECK(gsr_platform_ipc_client_send_all(pipe, request, (size_t)n));

        complete_thread_params params;
        params.ipc = &ipc;
        params.type = GSR_IPC_DEFERRED_REQUEST_STOP;
        params.success = true;
        params.filepath = NULL;
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, complete_request_thread, &params) == 0);

        CHECK(gsr_platform_ipc_client_receive_reply(pipe, reply, sizeof(reply), &reply_size));
        CHECK(reply_size == (size_t)strlen("{\"id\":7,\"result\":\"ok\"}"));
        CHECK(state.stop_called);
        pthread_join(thread, NULL);
        gsr_platform_ipc_client_disconnect(pipe);
    }
    printf("   [transport] after request 7\n");

    /* 8. a second init on the same pipe name fails (already in use). */
    {
        gsr_ipc ipc2;
        memset(&ipc2, 0, sizeof(ipc2));
        CHECK(gsr_ipc_init(&ipc2, pipe_name) != GSR_ERROR_OK);
    }
    printf("   [transport] after request 8\n");

    gsr_ipc_stop(&ipc);
    gsr_ipc_deinit(&ipc);
    printf("   [transport] transport test complete\n");
}

/* ------------------------------------------------------------------ */
/* part 2: real engine + gsr-cli                                      */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* Runs gsr-cli.exe with |args| (e.g. "-ipc gsr-x status") and returns its
   stdout. The binary is next to the test exe.

   Bounded by design: the read loop must not block forever when gsr-cli
   hangs (e.g. a no-timeout save-replay/stop waiting on an engine that
   stalled before starting its ipc thread). It drains whatever gsr-cli
   writes, but also watches for the process to exit and gives up after a
   deadline, so a hung child fails the check instead of eating the whole
   120s ctest timeout. */
static bool exec_gsr_cli(const char *args, char *out, size_t out_size) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char *slash = strrchr(exe_path, '\\');
    if(slash)
        *slash = '\0';
    char cli_path[MAX_PATH];
    snprintf(cli_path, sizeof(cli_path), "%s\\gsr-cli.exe", exe_path);

    char cmd_line[MAX_PATH + 256];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", cli_path, args);

    SECURITY_ATTRIBUTES sa = { 0 };
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    if(!CreatePipe(&read_pipe, &write_pipe, &sa, 0))
        return false;

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    PROCESS_INFORMATION pi = { 0 };
    if(!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return false;
    }
    CloseHandle(write_pipe);

    size_t offset = 0;
    char buf[512];
    bool process_exited = false;
    const DWORD deadline_ms = 25000;
    const DWORD start_time = GetTickCount();
    for(;;) {
        DWORD bytes_read = 0;
        if(PeekNamedPipe(read_pipe, NULL, 0, NULL, &bytes_read, NULL) && bytes_read > 0) {
            if(!ReadFile(read_pipe, buf, sizeof(buf) - 1, &bytes_read, NULL) || bytes_read == 0)
                break;
            buf[bytes_read] = '\0';
            const size_t to_copy = bytes_read < out_size - 1 - offset ? bytes_read : out_size - 1 - offset;
            memcpy(out + offset, buf, to_copy);
            offset += to_copy;
            continue;
        }

        if(WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            process_exited = true;
            break;
        }
        if(GetTickCount() - start_time >= deadline_ms) {
            fprintf(stderr, "exec_gsr_cli: %s timed out after %lu ms (engine/ipc hang?)\n",
                args, (unsigned long)deadline_ms);
            break;
        }
    }
    /* Only drain after a clean exit (EOF is then guaranteed); on the
       timeout path the child is still alive and a blocking read would hang
       forever. */
    if(process_exited) {
        for(;;) {
            DWORD bytes_read = 0;
            if(!ReadFile(read_pipe, buf, sizeof(buf) - 1, &bytes_read, NULL) || bytes_read == 0)
                break;
            buf[bytes_read] = '\0';
            const size_t to_copy = bytes_read < out_size - 1 - offset ? bytes_read : out_size - 1 - offset;
            memcpy(out + offset, buf, to_copy);
            offset += to_copy;
        }
    }
    out[offset] = '\0';
    CloseHandle(read_pipe);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit_code == 0;
}
#endif

static void test_engine_binary(void) {
    printf("-- engine binary (gpu-screen-recorder.exe + gsr-cli.exe)\n");

#ifdef _WIN32
    /* The engine needs ANGLE + a capture backend, same as
       recorder-self-test; SKIP cleanly where ANGLE isn't available. */
    gsr_egl egl;
    memset(&egl, 0, sizeof(egl));
    gsr_window window;
    memset(&window, 0, sizeof(window));
    if(!gsr_egl_load_win32(&egl, &window, false)) {
        printf("SKIP: ANGLE initialization failed (see gsr error logs above); exit 0\n");
        return;
    }
    gsr_egl_unload_win32(&egl);

    char pipe_name[128];
    snprintf(pipe_name, sizeof(pipe_name), "gsr-engine-test-%lu", (unsigned long)GetCurrentProcessId());

    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char *slash = strrchr(exe_path, '\\');
    if(slash)
        *slash = '\0';
    char engine_path[MAX_PATH];
    snprintf(engine_path, sizeof(engine_path), "%s\\gpu-screen-recorder.exe", exe_path);

    char output_path[MAX_PATH];
    snprintf(output_path, sizeof(output_path), "%s\\engine-ipc-test-output.mkv", exe_path);

    /* -w takes a real monitor name on Windows (\\.\DISPLAY1); "screen" is
       the primary alias. Resolve the primary so the engine's capture
       setup finds it. */
    char capture_arg[128];
    snprintf(capture_arg, sizeof(capture_arg), "screen");
    gsr_platform_monitor *monitors = NULL;
    int monitor_count = 0;
    if(gsr_platform_display_list_monitors(&monitors, &monitor_count) && monitor_count > 0) {
        const gsr_platform_monitor *primary = &monitors[0];
        for(int i = 0; i < monitor_count; ++i) {
            if(monitors[i].is_primary) {
                primary = &monitors[i];
                break;
            }
        }
        snprintf(capture_arg, sizeof(capture_arg), "%s", primary->name);
    }
    free(monitors);

    char cmd_line[MAX_PATH * 2];
    /* -fallback-cpu-encoding yes: CI has no NVIDIA GPU (Basic Display
       Adapter/WARP), so the default GPU encoder would fail; the fallback
       to libx264 makes the recording work exactly like recorder-self-test
       (which sets HW_CPU + fallback). */
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" -w %s -o \"%s\" -f 30 -fallback-cpu-encoding yes -ipc %s",
        engine_path, capture_arg, output_path, pipe_name);

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    if(!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "FAIL: failed to spawn gpu-screen-recorder.exe\n");
        ++num_failures;
        return;
    }
    CloseHandle(pi.hThread);

    /* Wait for the engine to create the pipe (it initializes it before
       starting to record). */
    bool connected = false;
    for(int i = 0; i < 200 && !connected; ++i) {
        const HANDLE probe = gsr_platform_ipc_client_connect(pipe_name, 1);
        if(probe != INVALID_HANDLE_VALUE) {
            gsr_platform_ipc_client_disconnect(probe);
            connected = true;
            break;
        }
        Sleep(50);
    }
    CHECK(connected);

    char out[1024];
    char args[512];

    /* status -> running */
    printf("   [engine] gsr-cli status...\n");
    snprintf(args, sizeof(args), "-ipc %s status", pipe_name);
    CHECK(exec_gsr_cli(args, out, sizeof(out)));
    CHECK(strstr(out, "running") != NULL);

    /* toggle-pause -> ok */
    printf("   [engine] gsr-cli toggle-pause...\n");
    snprintf(args, sizeof(args), "-ipc %s toggle-pause", pipe_name);
    CHECK(exec_gsr_cli(args, out, sizeof(out)));

    /* save-replay without -r -> error "option -r is required to save a
       replay" (gsr-cli exits 1 on an error reply, so exec_gsr_cli returns
       false but still captured the message on stderr). */
    printf("   [engine] gsr-cli save-replay (expect error)...\n");
    snprintf(args, sizeof(args), "-ipc %s save-replay", pipe_name);
    CHECK(!exec_gsr_cli(args, out, sizeof(out)));
    CHECK(strstr(out, "option -r is required to save a replay") != NULL);

    /* stop -> the saved filepath, engine exits 0 */
    printf("   [engine] gsr-cli stop...\n");
    snprintf(args, sizeof(args), "-ipc %s stop", pipe_name);
    CHECK(exec_gsr_cli(args, out, sizeof(out)));
    CHECK(strlen(out) > 0);
    CHECK(strstr(out, ".mkv") != NULL || strstr(out, ".mp4") != NULL);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, 60000);
    CHECK(wait_result == WAIT_OBJECT_0);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CHECK(exit_code == 0);
    CloseHandle(pi.hProcess);

    /* The recording was written. */
    const DWORD file_attrs = GetFileAttributesA(output_path);
    CHECK(file_attrs != INVALID_FILE_ATTRIBUTES);
    if(file_attrs != INVALID_FILE_ATTRIBUTES)
        DeleteFileA(output_path);
#else
    printf("SKIP: engine binary test is Windows-only; exit 0\n");
#endif
}

int main(void) {
    /* Unbuffered: ctest's timeout kill discards buffered stdout, which made
       a hang look like "no output at all". With unbuffered output the log
       shows exactly how far the test got before it stalled. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("engine-ipc-test: Phase 11 engine IPC + engine binary test\n");
    test_transport();
    test_engine_binary();

    printf("\n%zu checks, %d failures\n", num_checks, num_failures);
    if(num_failures > 0) {
        fprintf(stderr, "engine-ipc-test: %d failures\n", num_failures);
        return 1;
    }
    return 0;
}
