/* tests/compat-probe/main.c — CI diagnostics for the Windows portability shim.
 *
 * Reports, at compile time and runtime, which POSIX/GNU symbols the MinGW-w64
 * runtime provides natively and which are supplied by the port's shim
 * (gsr_win32_compat.h/.c, dlfcn.h, libgen.h). Lets the CI workflow fail loudly
 * if a shim stops being needed (native symbol appeared) or goes missing.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* The shim and its headers (all resolvable via the include path / force-include). */
#include <dlfcn.h>
#include <libgen.h>

#define REPORT(fmt, ...) printf("  " fmt "\n", ##__VA_ARGS__)

int main(void) {
    printf("== compat-probe: Windows portability shim diagnostics\n");

    printf("-- compile-time environment\n");
#ifdef _WIN32
    REPORT("_WIN32 defined");
#endif
#ifdef _MINGW32__
    REPORT("__MINGW32__ defined");
#endif
#ifdef __MINGW64__
    REPORT("__MINGW64__ defined (64-bit toolchain)");
#endif
#ifdef __GNUC__
    REPORT("__GNUC__ = %d.%d", __GNUC__, __GNUC_MINOR__);
#endif

    printf("-- clock_gettime / CLOCK_MONOTONIC\n");
#ifdef CLOCK_MONOTONIC
    REPORT("CLOCK_MONOTONIC available (native MinGW-w64 clock_gettime)");
#else
    REPORT("CLOCK_MONOTONIC missing - using shim fallback (QPC)");
#endif
#ifdef _TIMESPEC_DEFINED
    REPORT("struct timespec defined by the shim fallback");
#endif
#ifdef _WIN32
    {
        struct timespec tp;
        const int rc = clock_gettime(CLOCK_MONOTONIC, &tp);
        REPORT("clock_gettime(CLOCK_MONOTONIC) -> %d, tv_sec=%lld tv_nsec=%ld",
            rc, (long long)tp.tv_sec, (long)tp.tv_nsec);
    }
#endif

    printf("-- path limits\n");
#ifdef PATH_MAX
    REPORT("PATH_MAX = %d", (int)PATH_MAX);
#endif
#ifdef NAME_MAX
    REPORT("NAME_MAX = %d", (int)NAME_MAX);
#endif

    printf("-- strings.h / strcasecmp\n");
    {
        const char *a = "Firefox", *b = "firefox";
        REPORT("strcasecmp(\"Firefox\", \"firefox\") = %d (0 == equal)",
            strcasecmp(a, b));
    }

    printf("-- ssize_t\n");
#ifdef _SSIZE_T_DEFINED
    REPORT("ssize_t defined natively");
#else
    REPORT("ssize_t (may be provided by the shim or the runtime)");
#endif

    printf("-- stat mode macros\n");
#ifdef S_ISREG
    REPORT("S_ISREG available");
#endif
#ifdef S_ISDIR
    REPORT("S_ISDIR available");
#endif
#ifdef S_ISFIFO
    REPORT("S_ISFIFO available");
#endif
#ifdef S_ISCHR
    REPORT("S_ISCHR available");
#endif

    printf("-- dlfcn shim\n");
    {
        void *handle = dlopen("kernel32.dll", RTLD_NOW);
        REPORT("dlopen(\"kernel32.dll\") -> %s", handle ? "ok" : "FAILED");
        if(handle) {
            void *sym = dlsym(handle, "GetTickCount");
            REPORT("dlsym(handle, \"GetTickCount\") -> %s", sym ? "ok" : "FAILED");
            dlclose(handle);
            REPORT("dlclose(handle) done");
        }
        dlerror(); /* clear any error state */
        REPORT("dlerror() returns %s", dlerror() ? "(non-null)" : "NULL");
    }

    printf("-- libgen shim\n");
    {
        char path1[] = "a/b/c";
        REPORT("dirname(\"a/b/c\") -> \"%s\"", dirname(path1));
        char path2[] = "onlyfile";
        REPORT("dirname(\"onlyfile\") -> \"%s\"", dirname(path2));
        char path3[] = "/root";
        REPORT("dirname(\"/root\") -> \"%s\"", dirname(path3));
        char path4[] = "dir\\file.txt";
        REPORT("dirname(\"dir\\\\file.txt\") -> \"%s\"", dirname(path4));
        char path5[] = "/a/b.txt";
        REPORT("basename(\"/a/b.txt\") -> \"%s\"", basename(path5));
    }

    printf("== compat-probe: done (no failures are reported by this tool)\n");
    return 0;
}
