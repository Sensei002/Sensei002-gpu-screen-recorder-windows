/* gsr_commands_win32.c — Windows implementation of the engine CLI command
 * handlers (upstream/include/cli/commands.h, Phase 11). Upstream's
 * commands.c is Linux-coupled (PipeWire, v4l2, DRM, X11, fork/execvp) and
 * is not built on Windows; these replace it over the platform APIs:
 *
 *   - --info: same section/key line format the UI parses (GsrInfo.cpp),
 *     with display_server|x11 (the Windows UI already branches on
 *     #ifdef _WIN32 for the X11-style paths) and a Windows-only
 *     `hags|yes|no` line under system_info (Phase 11 HAGS hardening).
 *   - --list-audio-devices: WASAPI endpoints in the upstream
 *     `name|description` format (+ the default_output/default_input
 *     aliases).
 *   - --list-application-audio / --list-v4l2-devices: no output (WASAPI
 *     has no per-app capture — Phase 8 — and Windows has no v4l2).
 *   - --list-capture-options / --list-monitors: window/focused/region +
 *     the monitor lines from the DXGI enumeration (display.h).
 *   - -sc scripts run via CreateProcess (cmd /c for .bat/.cmd, PowerShell
 *     for .ps1, direct exe otherwise).
 */
#include "../../upstream/include/cli/commands.h"
#include "../../upstream/include/recorder/windowing.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/codec_select.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/log.h"
#include "../../upstream/include/json.h"
#include "../../platform/include/display.h"
#include "../../platform/include/audio.h"
#include "../../platform/include/hags.h"

#include <libavcodec/avcodec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int version_command(void *userdata) {
    (void)userdata;
    puts(GSR_VERSION);
    fflush(stdout);
    return 0;
}

static void list_gpu_info(gsr_egl *egl) {
    switch(egl->gpu_info.vendor) {
        case GSR_GPU_VENDOR_AMD:      printf("vendor|amd\n"); break;
        case GSR_GPU_VENDOR_INTEL:    printf("vendor|intel\n"); break;
        case GSR_GPU_VENDOR_NVIDIA:   printf("vendor|nvidia\n"); break;
        case GSR_GPU_VENDOR_BROADCOM: printf("vendor|broadcom\n"); break;
        case GSR_GPU_VENDOR_APPLE:    printf("vendor|apple\n"); break;
        case GSR_GPU_VENDOR_UNKNOWN:  printf("vendor|unknown\n"); break;
    }
    printf("card_path|%s\n", egl->card_path);
}

/* No wayland gating on Windows: WGC captures the composited desktop, so
   HDR-capable codecs are listed whenever the probe reports them. */
static void list_supported_video_codecs(gsr_egl *egl) {
    gsr_supported_video_codecs supported_video_codecs;
    get_supported_video_codecs(egl, GSR_VIDEO_CODEC_H264, false, false, &supported_video_codecs);

    gsr_supported_video_codecs supported_video_codecs_vulkan;
    get_supported_video_codecs(egl, GSR_VIDEO_CODEC_H264_VULKAN, false, false, &supported_video_codecs_vulkan);

    set_supported_video_codecs_ffmpeg(&supported_video_codecs, &supported_video_codecs_vulkan, egl->gpu_info.vendor);

    if(supported_video_codecs.h264.supported)
        puts("h264");
    if(avcodec_find_encoder_by_name("libx264"))
        puts("h264_software");
    if(supported_video_codecs.hevc.supported)
        puts("hevc");
    if(supported_video_codecs.hevc_hdr.supported)
        puts("hevc_hdr");
    if(supported_video_codecs.hevc_10bit.supported)
        puts("hevc_10bit");
    if(supported_video_codecs.av1.supported)
        puts("av1");
    if(supported_video_codecs.av1_hdr.supported)
        puts("av1_hdr");
    if(supported_video_codecs.av1_10bit.supported)
        puts("av1_10bit");
    if(supported_video_codecs.vp8.supported)
        puts("vp8");
    if(supported_video_codecs.vp9.supported)
        puts("vp9");
    if(supported_video_codecs_vulkan.h264.supported)
        puts("h264_vulkan");
    if(supported_video_codecs_vulkan.hevc.supported)
        puts("hevc_vulkan");
    if(supported_video_codecs_vulkan.hevc_hdr.supported)
        puts("hevc_hdr_vulkan");
    if(supported_video_codecs_vulkan.hevc_10bit.supported)
        puts("hevc_10bit_vulkan");
    if(supported_video_codecs_vulkan.av1.supported)
        puts("av1_vulkan");
    if(supported_video_codecs_vulkan.av1_hdr.supported)
        puts("av1_hdr_vulkan");
    if(supported_video_codecs_vulkan.av1_10bit.supported)
        puts("av1_10bit_vulkan");
}

static int list_monitors(const gsr_window *window) {
    (void)window;
    gsr_platform_monitor *monitors = NULL;
    int num_monitors = 0;
    if(!gsr_platform_display_list_monitors(&monitors, &num_monitors))
        return 0;

    int printed = 0;
    for(int i = 0; i < num_monitors; ++i) {
        char line[256];
        if(gsr_platform_display_format_monitor_line(&monitors[i], line, sizeof(line)) > 0) {
            puts(line);
            ++printed;
        }
    }
    free(monitors);
    return printed;
}

static void list_supported_capture_options(const gsr_window *window, bool do_list_monitors) {
    puts("window");
    puts("focused");

    int num_monitors = 0;
    if(do_list_monitors)
        num_monitors = list_monitors(window);

    if(num_monitors > 0)
        puts("region");
    /* No v4l2 devices and no desktop portal on Windows. */
}

int info_command(void *userdata) {
    (void)userdata;
    gsr_windowing windowing;
    const gsr_windowing_params windowing_params = { .monitor_capture = true };
    if(gsr_windowing_init(&windowing, &windowing_params) != GSR_ERROR_OK)
        return 1;

    if(gsr_windowing_load_egl(&windowing, &windowing_params) != GSR_ERROR_OK) {
        gsr_windowing_deinit(&windowing);
        return 22;
    }

    av_log_set_level(AV_LOG_FATAL);

    puts("section=system_info");
    puts("display_server|x11");
    puts("supports_app_audio|no");
    puts("is_steam_deck|no");
    printf("gsr_version|%s\n", GSR_VERSION);
    /* Windows-only line; the UI parser ignores unknown keys. */
    printf("hags|%s\n", gsr_platform_hags_enabled() ? "yes" : "no");
    puts("section=gpu_info");
    list_gpu_info(&windowing.egl);
    puts("section=video_codecs");
    list_supported_video_codecs(&windowing.egl);
    puts("section=image_formats");
    puts("jpeg");
    puts("png");
    puts("section=capture_options");
    list_supported_capture_options(windowing.window, windowing.card_path_found);

    fflush(stdout);
    gsr_windowing_deinit(&windowing);
    return 0;
}

int list_audio_devices_command(void *userdata) {
    (void)userdata;
    gsr_platform_audio_device *devices = NULL;
    int device_count = 0;
    if(!gsr_platform_audio_list_devices(&devices, &device_count))
        return 1;

    for(int i = 0; i < device_count; ++i) {
        if(devices[i].direction == GSR_PLATFORM_AUDIO_DIRECTION_OUTPUT && devices[i].is_default)
            puts("default_output|Default output");
        else if(devices[i].direction == GSR_PLATFORM_AUDIO_DIRECTION_INPUT && devices[i].is_default)
            puts("default_input|Default input");
    }

    for(int i = 0; i < device_count; ++i)
        printf("%s|%s\n", devices[i].name, devices[i].description);

    free(devices);
    fflush(stdout);
    return 0;
}

int list_application_audio_command(void *userdata) {
    (void)userdata;
    /* WASAPI has no per-session capture (Phase 8): nothing is listable as
       a capturable app stream. The -a app: input is rejected by the
       engine, matching the honest-unsupported contract. */
    fflush(stdout);
    return 0;
}

int list_v4l2_devices(void *userdata) {
    (void)userdata;
    /* No v4l2 on Windows. */
    fflush(stdout);
    return 0;
}

int list_capture_options_command(const char *card_path, void *userdata) {
    (void)userdata;
    gsr_windowing windowing;
    const gsr_windowing_params windowing_params = { .monitor_capture = true };
    if(gsr_windowing_init(&windowing, &windowing_params) != GSR_ERROR_OK)
        return 1;

    if(!card_path && gsr_windowing_load_egl(&windowing, &windowing_params) != GSR_ERROR_OK) {
        gsr_windowing_deinit(&windowing);
        return 22;
    }

    /* |card_path| is the D3D adapter name on Windows; monitor enumeration
       is independent of it. */
    list_supported_capture_options(windowing.window, true);

    fflush(stdout);
    gsr_windowing_deinit(&windowing);
    return 0;
}

int list_monitors_command(void *userdata) {
    (void)userdata;
    gsr_windowing windowing;
    const gsr_windowing_params windowing_params = { .monitor_capture = true };
    if(gsr_windowing_init(&windowing, &windowing_params) != GSR_ERROR_OK)
        return 1;

    if(gsr_windowing_load_egl(&windowing, &windowing_params) != GSR_ERROR_OK) {
        gsr_windowing_deinit(&windowing);
        return 22;
    }

    if(windowing.card_path_found)
        list_monitors(windowing.window);

    fflush(stdout);
    gsr_windowing_deinit(&windowing);
    return 0;
}

/* ---- -sc script execution (Phase 12) ------------------------------------ */

/* Runs the recording-saved script detached, passing the saved file path
   and the recording type, like upstream's fork+execvp. .bat/.cmd run via
   cmd /c, .ps1 via powershell -File, everything else directly. */
void run_recording_saved_script_async(const char *script_file, const char *video_file, const char *type) {
    if(!script_file || script_file[0] == '\0')
        return;

    const size_t script_len = strlen(script_file);
    const bool is_bat = (script_len >= 4 && _stricmp(script_file + script_len - 4, ".bat") == 0)
        || (script_len >= 4 && _stricmp(script_file + script_len - 4, ".cmd") == 0);
    const bool is_ps1 = script_len >= 4 && _stricmp(script_file + script_len - 4, ".ps1") == 0;

    char command_line[4096];
    if(is_bat) {
        snprintf(command_line, sizeof(command_line), "cmd /c call \"%s\" \"%s\" %s", script_file, video_file, type);
    } else if(is_ps1) {
        snprintf(command_line, sizeof(command_line), "powershell -NoProfile -ExecutionPolicy Bypass -File \"%s\" \"%s\" %s", script_file, video_file, type);
    } else {
        snprintf(command_line, sizeof(command_line), "\"%s\" \"%s\" %s", script_file, video_file, type);
    }

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    char *mutable_command_line = _strdup(command_line);
    if(!mutable_command_line)
        return;

    if(!CreateProcessA(NULL, mutable_command_line, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "script file failed to start: %s", script_file);
        free(mutable_command_line);
        return;
    }

    CloseHandle(pi.hThread);
    /* Detached: close our handle so the child isn't waited on. */
    CloseHandle(pi.hProcess);
    free(mutable_command_line);
}
