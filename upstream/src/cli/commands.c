#include "../../include/cli/commands.h"
#include "../../include/recorder/windowing.h"
#include "../../include/recorder/codec_select.h"
#include "../../include/recorder/error.h"
#include "../../include/capture/v4l2.h"
#include "../../include/window/window.h"
#include "../../include/sound.h"
#include "../../include/utils.h"
#include "../../include/log.h"
#ifdef GSR_APP_AUDIO
#include "../../include/pipewire_audio.h"
#endif
#ifdef GSR_PORTAL
#include "../../include/dbus.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef GSR_VERSION
#define GSR_VERSION "unknown"
#endif

static void list_system_info(bool wayland) {
    printf("display_server|%s\n", wayland ? "wayland" : "x11");
    bool supports_app_audio = false;
#ifdef GSR_APP_AUDIO
    supports_app_audio = pulseaudio_server_is_pipewire();
    if(supports_app_audio) {
        gsr_pipewire_audio audio;
        if(gsr_pipewire_audio_init(&audio))
            gsr_pipewire_audio_deinit(&audio);
        else
            supports_app_audio = false;
    }
#endif
    printf("supports_app_audio|%s\n", supports_app_audio ? "yes" : "no");
}

static void list_gpu_info(gsr_egl *egl) {
    switch(egl->gpu_info.vendor) {
        case GSR_GPU_VENDOR_AMD:
            printf("vendor|amd\n");
            break;
        case GSR_GPU_VENDOR_INTEL:
            printf("vendor|intel\n");
            break;
        case GSR_GPU_VENDOR_NVIDIA:
            printf("vendor|nvidia\n");
            break;
        case GSR_GPU_VENDOR_BROADCOM:
            printf("vendor|broadcom\n");
            break;
        case GSR_GPU_VENDOR_APPLE:
            printf("vendor|apple\n");
            break;
        case GSR_GPU_VENDOR_UNKNOWN:
            /* Windows port addition (§3f): ANGLE on a software adapter. */
            printf("vendor|unknown\n");
            break;
    }
    printf("card_path|%s\n", egl->card_path);
}

static void list_supported_video_codecs(gsr_egl *egl, bool wayland) {
    // Dont clean it up on purpose to increase shutdown speed
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
    if(supported_video_codecs.hevc_hdr.supported && wayland)
        puts("hevc_hdr");
    if(supported_video_codecs.hevc_10bit.supported)
        puts("hevc_10bit");
    if(supported_video_codecs.av1.supported)
        puts("av1");
    if(supported_video_codecs.av1_hdr.supported && wayland)
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
    if(supported_video_codecs_vulkan.hevc_hdr.supported && wayland)
        puts("hevc_hdr_vulkan");
    if(supported_video_codecs_vulkan.hevc_10bit.supported)
        puts("hevc_10bit_vulkan");
    if(supported_video_codecs_vulkan.av1.supported)
        puts("av1_vulkan");
    if(supported_video_codecs_vulkan.av1_hdr.supported && wayland)
        puts("av1_hdr_vulkan");
    if(supported_video_codecs_vulkan.av1_10bit.supported)
        puts("av1_10bit_vulkan");
}

void run_recording_saved_script_async(const char *script_file, const char *video_file, const char *type) {
    char script_file_full[PATH_MAX];
    script_file_full[0] = '\0';
    if(!realpath(script_file, script_file_full)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "script file not found: %s", script_file);
        return;
    }

    const char *args[7];
    const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;

    if(inside_flatpak) {
        args[0] = "flatpak-spawn";
        args[1] = "--host";
        args[2] = "--";
        args[3] = script_file_full;
        args[4] = video_file;
        args[5] = type;
        args[6] = NULL;
    } else {
        args[0] = script_file_full;
        args[1] = video_file;
        args[2] = type;
        args[3] = NULL;
    }

    pid_t pid = fork();
    if(pid == -1) {
        perror(script_file_full);
        return;
    } else if(pid == 0) { // child
        setsid();
        signal(SIGHUP, SIG_IGN);

        pid_t second_child = fork();
        if(second_child == 0) { // child
            execvp(args[0], (char* const*)args);
            perror(script_file_full);
            _exit(127);
        } else if(second_child != -1) { // parent
            _exit(0);
        }
    } else { // parent
        waitpid(pid, NULL, 0);
    }
}

typedef struct {
    const gsr_window *window;
    int num_monitors;
} capture_options_callback;

static void output_monitor_info(const gsr_monitor *monitor, void *userdata) {
    capture_options_callback *options = (capture_options_callback*)userdata;
    if(gsr_window_get_display_server(options->window) == GSR_DISPLAY_SERVER_WAYLAND) {
        vec2i monitor_size = monitor->size;
        gsr_monitor_rotation monitor_rotation = GSR_MONITOR_ROT_0;
        vec2i monitor_position = {0, 0};
        drm_monitor_get_display_server_data(options->window, monitor, &monitor_rotation, &monitor_position);
        if(monitor_rotation == GSR_MONITOR_ROT_90 || monitor_rotation == GSR_MONITOR_ROT_270)
            {
            const int tmp = monitor_size.x;
            monitor_size.x = monitor_size.y;
            monitor_size.y = tmp;
        }
        printf("%.*s|%dx%d\n", monitor->name_len, monitor->name, monitor_size.x, monitor_size.y);
    } else {
        printf("%.*s|%dx%d\n", monitor->name_len, monitor->name, monitor->size.x, monitor->size.y);
    }
    ++options->num_monitors;
}

static void camera_query_callback(const char *path, const gsr_capture_v4l2_supported_setup *setup, void *userdata) {
    (void)userdata;
    printf("%s|%ux%u@%uhz|%s\n", path, setup->resolution.width, setup->resolution.height, gsr_capture_v4l2_framerate_to_number(setup->framerate), gsr_capture_v4l2_pixfmt_to_string(setup->pixfmt));
}

static int list_monitors(const gsr_window *window, const char *card_path) {
    capture_options_callback options;
    options.window = window;
    options.num_monitors = 0;

    const bool is_x11 = gsr_window_get_display_server(window) == GSR_DISPLAY_SERVER_X11;
    const gsr_connection_type connection_type = is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_DRM;
    for_each_active_monitor_output(window, card_path, connection_type, output_monitor_info, &options);

    return options.num_monitors;
}

static void list_supported_capture_options(const gsr_window *window, const char *card_path, bool do_list_monitors) {
    const bool wayland = gsr_window_get_display_server(window) == GSR_DISPLAY_SERVER_WAYLAND;
    if(!wayland) {
        puts("window");
        puts("focused");
    }

    int num_monitors = 0;
    if(do_list_monitors)
        num_monitors = list_monitors(window, card_path);

    if(num_monitors > 0)
        puts("region");

    gsr_capture_v4l2_list_devices(camera_query_callback, NULL);

#ifdef GSR_PORTAL
    // Desktop portal capture on x11 doesn't seem to be hardware accelerated
    if(!wayland)
        return;

    gsr_dbus dbus;
    if(!gsr_dbus_init(&dbus, NULL))
        return;

    char *session_handle = NULL;
    if(gsr_dbus_screencast_create_session(&dbus, &session_handle) == 0)
        puts("portal");

    gsr_dbus_deinit(&dbus);
#endif
}

int version_command(void *userdata) {
    (void)userdata;
    puts(GSR_VERSION);
    fflush(stdout);
    return 0;
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

    const bool wayland = gsr_windowing_is_wayland(&windowing);

    av_log_set_level(AV_LOG_FATAL);

    puts("section=system_info");
    list_system_info(wayland);
    if(windowing.egl.gpu_info.is_steam_deck)
        puts("is_steam_deck|yes");
    else
        puts("is_steam_deck|no");
    printf("gsr_version|%s\n", GSR_VERSION);
    puts("section=gpu_info");
    list_gpu_info(&windowing.egl);
    puts("section=video_codecs");
    list_supported_video_codecs(&windowing.egl, wayland);
    puts("section=image_formats");
    puts("jpeg");
    puts("png");
    puts("section=capture_options");
    list_supported_capture_options(windowing.window, windowing.egl.card_path, windowing.card_path_found);

    fflush(stdout);
    gsr_windowing_deinit(&windowing);
    return 0;
}

int list_audio_devices_command(void *userdata) {
    (void)userdata;
    gsr_audio_devices audio_devices;
    get_pulseaudio_inputs(&audio_devices);

    if(audio_devices.default_output[0] != '\0')
        puts("default_output|Default output");

    if(audio_devices.default_input[0] != '\0')
        puts("default_input|Default input");

    for(size_t i = 0; i < audio_devices.num_items; ++i) {
        printf("%s|%s\n", audio_devices.items[i].name, audio_devices.items[i].description);
    }

    gsr_audio_devices_deinit(&audio_devices);
    fflush(stdout);
    return 0;
}

static bool app_audio_query_callback(const char *app_name, void *userdata) {
    (void)userdata;
    puts(app_name);
    return true;
}

int list_application_audio_command(void *userdata) {
    (void)userdata;
#ifdef GSR_APP_AUDIO
    if(pulseaudio_server_is_pipewire()) {
        gsr_pipewire_audio audio;
        if(gsr_pipewire_audio_init(&audio)) {
            gsr_pipewire_audio_for_each_app(&audio, app_audio_query_callback, NULL);
            gsr_pipewire_audio_deinit(&audio);
        }
    }
#endif

    fflush(stdout);
    return 0;
}

int list_v4l2_devices(void *userdata) {
    (void)userdata;
    gsr_capture_v4l2_list_devices(camera_query_callback, NULL);

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

    if(card_path)
        list_supported_capture_options(windowing.window, card_path, true);
    else
        list_supported_capture_options(windowing.window, windowing.egl.card_path, windowing.card_path_found);

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
        list_monitors(windowing.window, windowing.egl.card_path);

    fflush(stdout);
    gsr_windowing_deinit(&windowing);
    return 0;
}
