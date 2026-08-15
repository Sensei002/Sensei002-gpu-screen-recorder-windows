/*
 * Phase 1 CI toolchain smoke test.
 * Prints compiler + OS information so CI logs contain the diagnostics
 * required by the project brief (§77). Replaced by the real application
 * build in Phase 2.
 */
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#endif

int main(void) {
    printf("ci-smoke: ok\n");

#ifdef _MSC_VER
    printf("compiler: MSVC %d (%s)\n", _MSC_VER, _MSC_FULL_VER ? "full" : "");
#elif defined(__GNUC__)
    printf("compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__clang__)
    printf("compiler: clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#endif

#ifdef _WIN64
    printf("arch: x64\n");
#elif defined(_WIN32)
    printf("arch: x86\n");
#endif

#if defined(_WIN32)
    /* RtlGetVersion works without manifest issues; GetVersionEx is deprecated. */
    typedef LONG(WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    OSVERSIONINFOW ovi;
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        RtlGetVersion_t fn = (RtlGetVersion_t)(void *)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn && fn(&ovi) == 0)
            /* DWORD is unsigned long on Windows (LLP64), so %lu is correct. */
            printf("windows: %lu.%lu.%lu (build %lu)\n", (unsigned long)ovi.dwMajorVersion, (unsigned long)ovi.dwMinorVersion,
                   (unsigned long)ovi.dwBuildNumber, (unsigned long)ovi.dwBuildNumber);
    }
#endif

    return 0;
}
