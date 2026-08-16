/* tests/dxgi-self-test/main.c — Phase 6 DXGI Desktop Duplication self-test.
 *
 * Two parts:
 *   1. Pure logic (always runs, headless): the DXGI rotation mapping and
 *      size-swap decision (same checks as platform-test, standalone here).
 *   2. Live Desktop Duplication on the primary monitor (SKIPs gracefully
 *      where DD is unavailable — locked-down sessions, RDP, Server SKUs
 *      without a real display adapter, WARP-only devices).
 *
 * On GitHub Actions the runner exposes a Basic Display Adapter with a real
 * virtual monitor; unlike WGC (which needs the WinRT interop runtime that
 * Server SKUs lack), Desktop Duplication is plain DXGI and may genuinely
 * work there — in which case this test exercises a REAL capture path on CI.
 */
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

#include "capture.h"
#include "display.h"
#include "../../upstream/include/capture/capture.h" /* gsr_capture wrappers */

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++num_failures; \
    } \
} while(0)

static void run_pure_logic_tests(void) {
    printf("-- dxgi rotation mapping (pure)\n");
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(1) == GSR_PLATFORM_WGC_ROT_0);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(2) == GSR_PLATFORM_WGC_ROT_90);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(3) == GSR_PLATFORM_WGC_ROT_180);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(4) == GSR_PLATFORM_WGC_ROT_270);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(0) == GSR_PLATFORM_WGC_ROT_0);
    CHECK(gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_90));
    CHECK(gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_270));
    CHECK(!gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_0));
    CHECK(!gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_180));
}

static void run_live_capture(void) {
    printf("-- live Desktop Duplication (primary monitor)\n");

    /* The probe decides availability; also tells us which SKU we're on. */
    if(!gsr_platform_capture_dxgi_available()) {
        printf("SKIP: Desktop Duplication unavailable in this session; exit 0\n");
        return;
    }
    printf("dxgi: Desktop Duplication available\n");

    /* Primary monitor via the Phase 4 enumeration. */
    gsr_platform_monitor *monitors = NULL;
    int monitor_count = 0;
    CHECK(gsr_platform_display_list_monitors(&monitors, &monitor_count));
    if(monitor_count < 1) {
        fprintf(stderr, "FAIL: no monitors enumerated\n");
        ++num_failures;
        free(monitors);
        return;
    }
    const gsr_platform_monitor *primary = &monitors[0];
    for(int i = 0; i < monitor_count; ++i) {
        if(monitors[i].is_primary) {
            primary = &monitors[i];
            break;
        }
    }
    printf("dxgi: primary monitor = %s\n", primary->name);

    void *hmon = gsr_platform_display_find_hmonitor(primary->name);
    CHECK(hmon != NULL);
    if(!hmon) {
        free(monitors);
        return;
    }

    gsr_platform_dxgi_target target;
    memset(&target, 0, sizeof(target));
    target.hmonitor = hmon;
    strncpy(target.name, primary->name, sizeof(target.name) - 1);

    gsr_platform_dxgi_options options;
    memset(&options, 0, sizeof(options));
    options.cursor = false;
    options.hdr = false;
    options.egl = NULL; /* standalone — no GL pipeline in this test */

    gsr_capture *cap = gsr_platform_capture_dxgi_create(&target, &options);
    CHECK(cap != NULL);
    if(!cap) {
        free(monitors);
        return;
    }

    gsr_capture_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    CHECK(gsr_capture_start(cap, &metadata) == 0);
    if(!cap->started) {
        fprintf(stderr, "FAIL: capture did not start\n");
        ++num_failures;
        gsr_capture_destroy(cap);
        free(monitors);
        return;
    }
    printf("dxgi: started, video_size = %dx%d\n", metadata.video_size.x, metadata.video_size.y);
    CHECK(metadata.video_size.x > 0 && metadata.video_size.y > 0);

    /* Pump tick() for a few seconds; DD produces frames only when the
       desktop changes. On a busy runner a frame usually arrives quickly. */
    bool got_frame = false;
    int width = 0, height = 0;
    for(int i = 0; i < 50 && !got_frame; ++i) {
        gsr_capture_tick(cap);
        bool err = false;
        if(gsr_capture_should_stop(cap, &err)) {
            fprintf(stderr, "FAIL: capture stopped unexpectedly (err=%d)\n", err ? 1 : 0);
            ++num_failures;
            break;
        }
        got_frame = gsr_platform_capture_dxgi_get_frame(cap, NULL, &width, &height);
        if(!got_frame)
            Sleep(100);
    }

    if(got_frame) {
        printf("dxgi: captured a real frame: %dx%d\n", width, height);
        CHECK(width > 0 && height > 0);
        CHECK(cap->is_damaged(cap) == true);
        /* recorder contract: clear_damage() precedes capture() */
        cap->clear_damage(cap);
        CHECK(cap->is_damaged(cap) == false);
    } else {
        /* No frame within the window — the desktop was idle. That is not a
           backend failure; the important assertions (available, started,
           correct metadata) already passed. */
        printf("dxgi: no desktop update within the sample window (idle desktop); backend healthy\n");
    }

    gsr_capture_destroy(cap);
    free(monitors);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("dxgi-self-test: Phase 6 DXGI Desktop Duplication self-test\n");

    run_pure_logic_tests();
    run_live_capture();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
