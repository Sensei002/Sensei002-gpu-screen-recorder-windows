/* gsr_platform_win32.c — small platform helpers: time (platform/include/gsr_time.h),
 * thread naming (thread.h) and capture backend identity/selection (capture.h).
 *
 * Phase 3 deliverable.
 */
#include "../../platform/include/gsr_time.h"
#include "../../platform/include/thread.h"
#include "../../platform/include/capture.h"
#include "../../platform/include/filesystem.h"
#include "../../platform/include/audio.h"

#include "../../upstream/include/utils.h" /* clock_get_monotonic_seconds */

#include <windows.h>

#include <string.h>

/* ---- time ---------------------------------------------------------------- */

int64_t gsr_platform_time_monotonic_ns(void) {
    static LARGE_INTEGER qpc_frequency = {0};
    if(qpc_frequency.QuadPart == 0)
        QueryPerformanceFrequency(&qpc_frequency);

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    /* Scale without overflow: seconds * 1e9 + remainder * 1e9 / frequency. */
    const int64_t seconds = counter.QuadPart / qpc_frequency.QuadPart;
    const int64_t remainder = counter.QuadPart % qpc_frequency.QuadPart;
    return seconds * 1000000000LL + (remainder * 1000000000LL) / qpc_frequency.QuadPart;
}

double gsr_platform_time_monotonic_seconds(void) {
    return clock_get_monotonic_seconds();
}

int64_t gsr_platform_time_wall_clock_ms(void) {
    return gsr_platform_time_monotonic_ns() / 1000000LL;
}

/* ---- thread naming ------------------------------------------------------- */

void gsr_platform_thread_set_current_name(const char *name) {
    /* SetThreadDescription is available on Windows 10 1607+; on older
       systems this is a harmless no-op. */
    typedef HRESULT (WINAPI *SetThreadDescription_t)(HANDLE, PCWSTR);
    static SetThreadDescription_t set_thread_description = NULL;
    if(!set_thread_description) {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if(!kernel32)
            return;
        set_thread_description = (SetThreadDescription_t)(void*)GetProcAddress(kernel32, "SetThreadDescription");
        if(!set_thread_description)
            return;
    }

    wchar_t wname[64];
    if(gsr_platform_utf8_to_wide(name, wname, sizeof(wname) / sizeof(wname[0])))
        set_thread_description(GetCurrentThread(), wname);
}

/* ---- capture backend identity / selection --------------------------------- */

const char *gsr_platform_capture_backend_name(gsr_capture_backend_type backend) {
    switch(backend) {
        case GSR_CAPTURE_BACKEND_WGC:               return "Windows Graphics Capture";
        case GSR_CAPTURE_BACKEND_DXGI_DUPLICATION:  return "Desktop Duplication";
        default:                                    return "unknown";
    }
}

gsr_capture_backend_type gsr_platform_capture_select_backend(bool wgc_supported, bool dxgi_supported) {
    if(wgc_supported)
        return GSR_CAPTURE_BACKEND_WGC;
    if(dxgi_supported)
        return GSR_CAPTURE_BACKEND_DXGI_DUPLICATION;
    /* Neither backend exists; the caller must check availability and fail
       with a clear error instead of recording nothing. */
    return GSR_CAPTURE_BACKEND_WGC;
}

bool gsr_platform_capture_backend_available(gsr_capture_backend_type backend) {
    /* Runtime probes implemented in Phase 5 (WGC) / Phase 6 (DXGI). */
    (void)backend;
    return false;
}

/* ---- audio device line (pure formatter; enumeration is Phase 8) ---------- */

int gsr_platform_audio_format_device_line(const gsr_platform_audio_device *device, char *buf, size_t size) {
    if(!device || !buf)
        return -1;

    const int written = snprintf(buf, size, "%s (%s)", device->name, device->description);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}
