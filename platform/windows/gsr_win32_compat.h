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
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

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
