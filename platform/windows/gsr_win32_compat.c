/* gsr_win32_compat.c — implementations for the Windows portability shims
 * declared in gsr_win32_compat.h. See that header for details.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "gsr_win32_compat.h"

#if defined(_WIN32)

/* ---- clock_gettime fallback (only when MinGW-w64 lacks it) ------------- */
#if !defined(CLOCK_MONOTONIC)
static double qpc_frequency(void) {
    static double freq = 0.0;
    if(freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart;
    }
    return freq;
}

int clock_gettime(int clock_id, struct timespec *tp) {
    (void)clock_id; /* only CLOCK_MONOTONIC / CLOCK_REALTIME are requested */
    LARGE_INTEGER counter;
    if(!QueryPerformanceCounter(&counter))
        return -1;
    const double seconds = (double)counter.QuadPart / qpc_frequency();
    tp->tv_sec = (time_t)seconds;
    tp->tv_nsec = (long)((seconds - (double)tp->tv_sec) * 1000000000.0);
    return 0;
}
#endif

/* ---- dl* built on the Windows loader ----------------------------------- */
void *dlopen(const char *filename, int flags) {
    (void)flags;
    if(!filename)
        return (void*)GetModuleHandleA(NULL);
    /* LoadLibraryA would fail for a module already loaded by the process;
       prefer GetModuleHandleA first so "open the already-loaded library"
       works the way dlopen("libx.so") does on Linux. */
    HMODULE mod = GetModuleHandleA(filename);
    if(!mod)
        mod = LoadLibraryA(filename);
    return (void*)mod;
}

void *dlsym(void *handle, const char *name) {
    return (void*)(intptr_t)GetProcAddress((HMODULE)handle, name);
}

int dlclose(void *handle) {
    /* Never unload libraries that were already loaded before dlopen
       (we cannot tell them apart from freshly loaded ones here); FreeLibrary
       on a NULL/static handle is harmless. */
    if(handle)
        FreeLibrary((HMODULE)handle);
    return 0;
}

char *dlerror(void) {
    static char error_buffer[512];
    const DWORD error_code = GetLastError();
    if(error_code == 0) {
        error_buffer[0] = '\0';
        return NULL;
    }
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error_code, 0, error_buffer, sizeof(error_buffer), NULL);
    return error_buffer;
}

/* ---- dirname/basename (libgen.h) ---------------------------------------
 * POSIX semantics, but also treating '\\' as a separator so Windows paths
 * work. dirname() may modify |path| in place, exactly like POSIX. */
static bool is_separator(char c) {
    return c == '/' || c == '\\';
}

char *dirname(char *path) {
    if(!path || path[0] == '\0')
        return (char*)".";

    size_t len = strlen(path);
    while(len > 1 && is_separator(path[len - 1]))
        --len;

    size_t i = len;
    while(i > 0 && !is_separator(path[i - 1]))
        --i;

    if(i == 0)
        return (char*)".";

    /* Path is a single root separator (e.g. "/" or "C:\\") */
    if(i == 1 && is_separator(path[0]))
        return (char*)"/";

    size_t j = i;
    while(j > 1 && is_separator(path[j - 1]))
        --j;

    path[j] = '\0';
    return path;
}

char *basename(char *path) {
    if(!path || path[0] == '\0')
        return (char*)".";

    size_t len = strlen(path);
    while(len > 1 && is_separator(path[len - 1]))
        --len;

    size_t i = len;
    while(i > 0 && !is_separator(path[i - 1]))
        --i;

    return path + i;
}

#endif /* _WIN32 */
