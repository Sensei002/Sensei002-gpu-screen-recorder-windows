/* gsr_win32_compat.h — portability shim for the Windows (MinGW-w64) build.
 *
 * This header is force-included into every translation unit of the engine via
 * CMake's `-include` flag (see the root CMakeLists.txt). It supplies the small
 * set of POSIX/GNU symbols the upstream engine expects that the MinGW-w64
 * runtime does not provide. Everything here is guarded so it only kicks in
 * when the system headers do not already provide it.
 *
 * Windows port modification — see docs/upstream-porting-notes.md.
 */
#ifndef GSR_WIN32_COMPAT_H
#define GSR_WIN32_COMPAT_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
/* Target the Windows 10 API level (additive — only widens what the system
   headers declare). Needed by the Phase 4 DXGI display enumeration:
   dxgi1_6.h requires _WIN32_WINNT >= 0x0A00 and shellscalingapi.h's
   GetDpiForMonitor requires NTDDI >= Win8.1. */
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* These system headers must be included BEFORE the feature checks below:
   the shim is force-included into every translation unit ahead of the
   sources' own includes, so if a symbol (CLOCK_MONOTONIC, ssize_t, S_IS*)
   is available natively we must see it here or we would define a duplicate
   that clashes when the system header is included later. */
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/* strings.h provides strcasecmp/strncasecmp on MinGW-w64. */
#if defined(__has_include)
#if __has_include(<strings.h>)
#include <strings.h>
#endif
#endif

/* ---- Path limits ------------------------------------------------------ */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/* ---- ssize_t ---------------------------------------------------------- */
#if !defined(_SSIZE_T_DEFINED) && !defined(ssize_t)
typedef intptr_t ssize_t;
#endif

/* ---- X11 types used by upstream headers --------------------------------
 * A few upstream headers compiled on Windows for their portable parts
 * (cursor.h, recorder/capture_setup.h) reference X11 types in function
 * signatures. Display/Window/XID/Bool are provided by egl.h's _WIN32
 * branch; XEvent is not, so it is declared here — as an OPAQUE UNION,
 * exactly matching the real X11 definition and upstream window.h's own
 * `typedef union _XEvent XEvent;`. With identical declarations, window.h's
 * later (re)definition is a legal C11 typedef redeclaration; a `struct`
 * tag instead of `union` is a hard conflict. The port never dereferences
 * it (see also the stubs/ include dir). */
#ifndef GSR_STUB_XEVENT_DEFINED
#define GSR_STUB_XEVENT_DEFINED
typedef union _XEvent XEvent;
#endif

/* ---- S_IS* / permission macros (MinGW-w64 defines S_IF* but not these) - */
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#endif
#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif
#ifndef S_IRWXU
#define S_IRWXU 0700
#endif

/* ---- clock_gettime -----------------------------------------------------
 * Modern MinGW-w64 (>= 6.0, shipped by MSYS2) declares clock_gettime(),
 * struct timespec and the CLOCK_* constants in <time.h>. The fallback below
 * only compiles when they are missing. */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#define CLOCK_REALTIME 2

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};
#endif

int clock_gettime(int clock_id, struct timespec *tp);
#endif

/* ---- Platform replay-buffer crash cleanup --------------------------------
 * The disk replay buffer (upstream/src/replay_buffer/replay_buffer_disk.c)
 * names each session's working directory gsr-replay-<timestamp>.gsr inside
 * the replay directory and removes it on a clean exit; a crashed session
 * leaves it behind. This helper (implemented in gsr_filesystem_win32.c)
 * sweeps stale ones when the next session starts. Declared here because
 * this header is force-included into every translation unit, so the
 * upstream file can call it without a platform include (see
 * docs/upstream-porting-notes.md §3l). extern "C": the header is also
 * force-included into the C++ WGC backend TU. */
#ifdef __cplusplus
extern "C" {
#endif
int gsr_platform_replay_cleanup_stale_directories(const char *replay_directory, const char *current_session_dirname);
#ifdef __cplusplus
}
#endif

/* ---- POSIX process shims (UI only) --------------------------------------
 * The UI (ui/src/Overlay.cpp, ui/src/Process.cpp) uses a small POSIX process
 * surface that MinGW-w64 does not provide: usleep, kill, waitpid, WIFEXITED/
 * WEXITSTATUS, WNOHANG and the signal constants. None of the engine files
 * compiled on Windows use these, so defining them here (the header is
 * force-included into every TU) is safe. The UI's Process.cpp maps child
 * process HANDLEs to pid_t, so kill/waitpid operate on HANDLEs. */
#ifndef GSR_POSIX_PROCESS_SHIMS
#define GSR_POSIX_PROCESS_SHIMS

#ifndef SIGINT
#define SIGINT 2
#endif
#ifndef SIGTERM
#define SIGTERM 15
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif
#ifndef SIGUSR2
#define SIGUSR2 12
#endif
#ifndef WNOHANG
#define WNOHANG 1
#endif
#ifndef WIFEXITED
#define WIFEXITED(status) 1
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) 0
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) ((int)(status))
#endif

static inline void usleep(unsigned long usec) {
    Sleep((DWORD)((usec + 999) / 1000));
}

/* pid_t is a child process HANDLE (from ui Process.cpp's CreateProcess). */
static inline int kill(int pid, int sig) {
    (void)sig;
    if(pid <= 0)
        return -1;
    HANDLE process = (HANDLE)(intptr_t)pid;
    if(!TerminateProcess(process, 1))
        return -1;
    return 0;
}

static inline int waitpid(int pid, int *status, int options) {
    (void)options;
    if(pid <= 0)
        return -1;
    HANDLE process = (HANDLE)(intptr_t)pid;
    DWORD exit_code = 0;
    if(WaitForSingleObject(process, options & WNOHANG ? 0 : INFINITE) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process, &exit_code);
        if(status)
            *status = (int)exit_code;
        CloseHandle(process);
        return pid;
    }
    return 0; /* still running (WNOHANG) */
}
#endif /* GSR_POSIX_PROCESS_SHIMS */

/* ---- dlopen/dlsym/dlclose/dlerror --------------------------------------
 * MinGW-w64 may provide <dlfcn.h> + libdl.a; regardless of that, this port
 * ships its own implementation (gsr_win32_compat.c) built on LoadLibrary/
 * GetProcAddress and never links libdl.a, so the symbols are always present.
 * The declarations below match the POSIX signatures, so they do not conflict
 * with dlfcn.h when it exists. */
#ifndef RTLD_LAZY
#define RTLD_LAZY  1
#endif
#ifndef RTLD_NOW
#define RTLD_NOW   2
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0x100
#endif

#ifdef __cplusplus
extern "C" {
#endif
void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
char *dlerror(void);
#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* GSR_WIN32_COMPAT_H */
