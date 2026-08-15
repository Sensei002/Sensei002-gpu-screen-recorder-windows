#include "../../include/cli/commands.h"
#include "../../include/cli/ipc.h"
#include "../../include/recorder/recorder.h"
#include "../../include/recorder/screenshot.h"
#include "../../include/recorder/capture_source.h"
#include "../../include/recorder/capture_setup.h"
#include "../../include/recorder/windowing.h"
#include "../../include/recorder/audio_input.h"
#include "../../include/recorder/replay_save.h"
#include "../../include/recorder/error.h"
#include "../../include/args_parser.h"
#include "../../include/sound.h"
#include "../../include/shader.h"
#include "../../include/utils.h"
#include "../../include/log.h"
#ifdef GSR_APP_AUDIO
#include "../../include/pipewire_audio.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <malloc.h>

static atomic_int running = 1;
static gsr_recorder *recorder = NULL;
/* Signals that are received before the recorder has been created are applied when it has been created */
static volatile sig_atomic_t pending_toggle_pause = 0;
static volatile sig_atomic_t pending_toggle_replay_recording = 0;
static volatile sig_atomic_t pending_save_replay_seconds = 0;

static void stop_handler(int signal_value) {
    (void)signal_value;
    atomic_store(&running, 0);
    if(recorder)
        gsr_recorder_stop(recorder);
}

static void toggle_pause_handler(int signal_value) {
    (void)signal_value;
    if(recorder)
        gsr_recorder_toggle_pause(recorder);
    else
        pending_toggle_pause = 1;
}

static void toggle_replay_recording_handler(int signal_value) {
    (void)signal_value;
    if(recorder)
        gsr_recorder_toggle_replay_recording(recorder);
    else
        pending_toggle_replay_recording = 1;
}

static void save_replay_seconds_handler(gsr_recorder *rec, int seconds) {
    if(rec)
        gsr_recorder_save_replay(rec, seconds, GSR_RESTART_REPLAY_USE_OPTION);
    else
        pending_save_replay_seconds = seconds;
}

static void apply_pending_signals(gsr_recorder *rec) {
    if(pending_toggle_pause) {
        pending_toggle_pause = 0;
        gsr_recorder_toggle_pause(rec);
    }

    if(pending_toggle_replay_recording) {
        pending_toggle_replay_recording = 0;
        gsr_recorder_toggle_replay_recording(rec);
    }

    if(pending_save_replay_seconds != 0) {
        const int seconds = pending_save_replay_seconds;
        pending_save_replay_seconds = 0;
        gsr_recorder_save_replay(rec, seconds, GSR_RESTART_REPLAY_USE_OPTION);
    }
}

static void save_replay_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, GSR_SAVE_REPLAY_SECONDS_FULL);
}

static void save_replay_10_seconds_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 10);
}

static void save_replay_30_seconds_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 30);
}

static void save_replay_1_minute_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 60);
}

static void save_replay_5_minutes_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 60*5);
}

static void save_replay_10_minutes_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 60*10);
}

static void save_replay_30_minutes_handler(int signal_value) {
    (void)signal_value;
    save_replay_seconds_handler(recorder, 60*30);
}

static bool ipc_stop_handler(char *error_message, size_t error_message_size, void *userdata) {
    (void)error_message;
    (void)error_message_size;
    (void)userdata;
    atomic_store(&running, 0);
    gsr_recorder_stop(recorder);
    return true;
}

static bool ipc_toggle_pause_handler(char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(settings->is_replaying) {
        snprintf(error_message, error_message_size, "pausing is not supported when recording a replay");
        return false;
    }

    gsr_recorder_toggle_pause(recorder);
    return true;
}

static bool ipc_set_paused_handler(bool paused, char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(settings->is_replaying) {
        snprintf(error_message, error_message_size, "pausing is not supported when recording a replay");
        return false;
    }

    gsr_recorder_set_paused(recorder, paused);
    return true;
}

static bool ipc_toggle_replay_recording_handler(char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(!settings->replay_recording_directory) {
        snprintf(error_message, error_message_size, "option -ro is required to start a recording");
        return false;
    }

    gsr_recorder_toggle_replay_recording(recorder);
    return true;
}

static bool ipc_start_replay_recording_handler(char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(!settings->replay_recording_directory) {
        snprintf(error_message, error_message_size, "option -ro is required to start a recording");
        return false;
    }

    gsr_recorder_start_replay_recording(recorder);
    return true;
}

static bool ipc_stop_replay_recording_handler(char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(!settings->replay_recording_directory) {
        snprintf(error_message, error_message_size, "option -ro is required to start a recording");
        return false;
    }

    if(!gsr_recorder_is_replay_recording(recorder)) {
        snprintf(error_message, error_message_size, "no recording is running");
        return false;
    }

    gsr_recorder_stop_replay_recording(recorder);
    return true;
}

static bool ipc_save_replay_handler(int seconds, bool has_restart_replay, bool restart_replay, char *error_message, size_t error_message_size, void *userdata) {
    const gsr_recorder_settings *settings = userdata;
    if(!settings->is_replaying) {
        snprintf(error_message, error_message_size, "option -r is required to save a replay");
        return false;
    }

    int restart_replay_request = GSR_RESTART_REPLAY_USE_OPTION;
    if(has_restart_replay)
        restart_replay_request = restart_replay ? GSR_RESTART_REPLAY_ENABLE : GSR_RESTART_REPLAY_DISABLE;

    gsr_recorder_save_replay(recorder, seconds, restart_replay_request);
    return true;
}

static void install_signal_handlers(void) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    signal(SIGUSR1, save_replay_handler);
    signal(SIGUSR2, toggle_pause_handler);
    signal(SIGRTMIN, toggle_replay_recording_handler);
    signal(SIGRTMIN+1, save_replay_10_seconds_handler);
    signal(SIGRTMIN+2, save_replay_30_seconds_handler);
    signal(SIGRTMIN+3, save_replay_1_minute_handler);
    signal(SIGRTMIN+4, save_replay_5_minutes_handler);
    signal(SIGRTMIN+5, save_replay_10_minutes_handler);
    signal(SIGRTMIN+6, save_replay_30_minutes_handler);
}

static void set_display_server_environment_variables(void) {
    /* Some users dont have properly setup environments (no display manager that does systemctl --user import-environment DISPLAY WAYLAND_DISPLAY) */
    const char *display = getenv("DISPLAY");
    if(!display) {
        display = ":0";
        setenv("DISPLAY", display, true);
    }

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if(!wayland_display) {
        wayland_display = "wayland-0";
        setenv("WAYLAND_DISPLAY", wayland_display, true);
    }
}

static void set_environment_variables(void) {
    set_display_server_environment_variables();

    /* Linux nvidia driver 580.105.08 added the environment variable CUDA_DISABLE_PERF_BOOST to disable the p2 power level issue,
       where running cuda (which includes nvenc) causes the gpu to be forcefully set to p2 power level which on many nvidia gpus
       decreases gpu performance in games. On my GTX 1080 it decreased game performance by 10% for absolutely no reason. */
    setenv("CUDA_DISABLE_PERF_BOOST", "1", true);
    /* Stop nvidia driver from buffering frames */
    setenv("__GL_MaxFramesAllowed", "1", true);
    /* If this is set to 1 then cuGraphicsGLRegisterImage will fail for egl context with error: invalid OpenGL or DirectX context,
       so we overwrite it */
    setenv("__GL_THREADED_OPTIMIZATIONS", "0", true);
    /* Some people set this to nvidia (for nvdec) or vdpau (for nvidia vdpau), which breaks gpu screen recorder since
       nvidia doesn't support vaapi and nvidia-vaapi-driver doesn't support encoding yet.
       Let vaapi find the right vaapi driver instead of forcing a specific one. */
    unsetenv("LIBVA_DRIVER_NAME");
    /* Some people set this to force all applications to vsync on nvidia, but this makes eglSwapBuffers never return. */
    unsetenv("__GL_SYNC_TO_VBLANK");
    /* Same as above, but for amd/intel */
    unsetenv("vblank_mode");
}

static void install_cuda_no_stable_perf_limit(void) {
    if(access("/proc/driver/nvidia/version", F_OK) != 0)
        return;

    const char *home = getenv("HOME");
    if(!home) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "install_cuda_no_stable_perf_limit: $HOME not set");
        return;
    }

    char nv_profiles_path[4096];
    snprintf(nv_profiles_path, sizeof(nv_profiles_path), "%s/.nv/nvidia-application-profiles-rc.d", home);

    if(create_directory_recursive(nv_profiles_path) != 0) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "install_cuda_no_stable_perf_limit: failed to create directory: %s", nv_profiles_path);
        return;
    }

    snprintf(nv_profiles_path, sizeof(nv_profiles_path), "%s/.nv/nvidia-application-profiles-rc.d/10-gsr-cuda-no-stable-perf-limit", home);

    FILE *f = fopen(nv_profiles_path, "wb");
    if(!f) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "install_cuda_no_stable_perf_limit: failed to create file: %s", nv_profiles_path);
        return;
    }

    const char *profile_data =
        "{\n"
        "    \"profiles\": [\n"
        "        {\n"
        "            \"name\": \"CudaNoStablePerfLimit\",\n"
        "            \"settings\": [\"0x166c5e\", 0]\n"
        "        }\n"
        "    ],\n"
        "    \"rules\": [\n"
        "        { \"pattern\": \"gpu-screen-recorder\", \"profile\": \"CudaNoStablePerfLimit\" }\n"
        "    ]\n"
        "}\n";

    fwrite(profile_data, 1, strlen(profile_data), f);
    fclose(f);
}

static int validate_args_with_capture_sources(args_parser *arg_parser, const gsr_capture_sources *capture_sources) {
    const Arg *output_resolution_arg = args_parser_get_arg(arg_parser, "-s");
    assert(output_resolution_arg);

    const Arg *region_arg = args_parser_get_arg(arg_parser, "-region");
    assert(region_arg);

    if(gsr_capture_sources_has_type(capture_sources, GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW) && output_resolution_arg->num_values == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "option -s is required when using '-w focused' option");
        args_parser_print_usage();
        return GSR_ERROR_GENERIC;
    }

    const bool is_capturing_region = gsr_capture_sources_has_type(capture_sources, GSR_CAPTURE_SOURCE_TYPE_REGION);
    if(region_arg->num_values == 0) {
        if(is_capturing_region && !gsr_capture_sources_has_region_set(capture_sources)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "option -region is required when '-w region' is used");
            args_parser_print_usage();
            return GSR_ERROR_GENERIC;
        }
    } else {
        if(is_capturing_region) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "option -region is deprecated, use -w with region directly instead, for example: -w %s", region_arg->values[0]);
        } else {
            gsr_log(GSR_LOG_LEVEL_ERROR, "option -region can only be used when option '-w region' is used");
            args_parser_print_usage();
            return GSR_ERROR_GENERIC;
        }
    }

    if(!arg_parser->settings.restore_portal_session && gsr_capture_sources_has_type(capture_sources, GSR_CAPTURE_SOURCE_TYPE_PORTAL))
        gsr_log(GSR_LOG_LEVEL_INFO, "option '-w portal' was used without '-restore-portal-session yes'. The previous screencast session will be ignored");

    return GSR_ERROR_OK;
}

static void screenshot_saved_callback(const char *filepath, void *userdata) {
    const char *recording_saved_script = userdata;
    if(recording_saved_script)
        run_recording_saved_script_async(recording_saved_script, filepath, "screenshot");
}

typedef struct {
    const char *recording_saved_script;
    gsr_ipc *ipc;
} recorder_callbacks_context;

static void replay_saved_callback(const char *filepath, void *userdata) {
    recorder_callbacks_context *context = userdata;
    if(!filepath) {
        printf("gsr error: Failed to save replay\n");
        fflush(stdout);
        gsr_ipc_complete_request(context->ipc, GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY, false, NULL);
        return;
    }

    puts(filepath);
    fflush(stdout);
    if(context->recording_saved_script)
        run_recording_saved_script_async(context->recording_saved_script, filepath, "replay");

    gsr_ipc_complete_request(context->ipc, GSR_IPC_DEFERRED_REQUEST_SAVE_REPLAY, true, filepath);
}

static void recording_started_callback(const char *filepath, void *userdata) {
    (void)userdata;
    if(!filepath) {
        printf("gsr error: Failed to start recording\n");
        fflush(stdout);
    }
}

static void recording_stopped_callback(const char *filepath, void *userdata) {
    recorder_callbacks_context *context = userdata;
    if(!filepath) {
        printf("gsr error: Failed to save recording\n");
        fflush(stdout);
        gsr_ipc_complete_request(context->ipc, GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING, false, NULL);
        return;
    }

    puts(filepath);
    fflush(stdout);
    if(context->recording_saved_script)
        run_recording_saved_script_async(context->recording_saved_script, filepath, "regular");

    gsr_ipc_complete_request(context->ipc, GSR_IPC_DEFERRED_REQUEST_STOP_REPLAY_RECORDING, true, filepath);
}

#ifdef GSR_APP_AUDIO
static gsr_pipewire_audio pipewire_audio;

static bool app_audio_name_callback(const char *app_name, void *userdata) {
    gsr_app_audio_names *app_audio_names = userdata;
    gsr_app_audio_names_add(app_audio_names, app_name);
    return true;
}

static int setup_app_audio(gsr_app_audio_names *app_audio_names) {
    if(!pulseaudio_server_is_pipewire()) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "your sound server is not PipeWire. Application audio is only available when running PipeWire audio server");
        return GSR_ERROR_UNSUPPORTED;
    }

    if(!gsr_pipewire_audio_init(&pipewire_audio)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to setup PipeWire audio for application audio capture");
        return GSR_ERROR_UNSUPPORTED;
    }

    gsr_pipewire_audio_for_each_app(&pipewire_audio, app_audio_name_callback, app_audio_names);
    return GSR_ERROR_OK;
}
#endif

static int parse_audio_inputs(args_parser *arg_parser, gsr_audio_input_tracks *audio_input_tracks) {
    const Arg *audio_input_arg = args_parser_get_arg(arg_parser, "-a");
    assert(audio_input_arg);

    gsr_audio_devices audio_devices;
    memset(&audio_devices, 0, sizeof(audio_devices));
    if(audio_input_arg->num_values > 0)
        get_pulseaudio_inputs(&audio_devices);

    const int parse_result = gsr_audio_input_tracks_parse(audio_input_tracks, audio_input_arg->values, audio_input_arg->num_values, &audio_devices);
    gsr_audio_devices_deinit(&audio_devices);
    return parse_result;
}

static int take_screenshot(args_parser *arg_parser, gsr_windowing *windowing, gsr_capture_deps *capture_deps, gsr_capture_sources *capture_sources, gsr_image_format image_format) {
    const Arg *plugin_arg = args_parser_get_arg(arg_parser, "-p");
    assert(plugin_arg);

    arg_parser->settings.fps = 60; /* We want to capture an image as soon as possible */

    gsr_screenshot_params screenshot_params;
    memset(&screenshot_params, 0, sizeof(screenshot_params));
    screenshot_params.settings = &arg_parser->settings;
    screenshot_params.egl = &windowing->egl;
    screenshot_params.window = windowing->window;
    screenshot_params.capture_deps = capture_deps;
    screenshot_params.capture_sources = capture_sources;
    screenshot_params.image_format = image_format;
    screenshot_params.plugin_filepaths = plugin_arg->values;
    screenshot_params.num_plugin_filepaths = plugin_arg->num_values;
    screenshot_params.running = &running;
    screenshot_params.screenshot_saved = screenshot_saved_callback;
    screenshot_params.userdata = (void*)arg_parser->settings.recording_saved_script;

    return gsr_screenshot_take(&screenshot_params);
}

static int record(args_parser *arg_parser, gsr_windowing *windowing, gsr_capture_deps *capture_deps, gsr_capture_sources *capture_sources, gsr_audio_input_tracks *audio_input_tracks, gsr_ipc *ipc) {
    const Arg *plugin_arg = args_parser_get_arg(arg_parser, "-p");
    assert(plugin_arg);

    gsr_recorder_params recorder_params;
    memset(&recorder_params, 0, sizeof(recorder_params));
    recorder_params.settings = &arg_parser->settings;
    recorder_params.windowing = windowing;
    recorder_params.capture_deps = capture_deps;
    recorder_params.capture_sources = capture_sources;
    recorder_params.audio_input_tracks = audio_input_tracks;
    recorder_params.plugin_filepaths = plugin_arg->values;
    recorder_params.num_plugin_filepaths = plugin_arg->num_values;
#ifdef GSR_APP_AUDIO
    recorder_params.pipewire_audio = &pipewire_audio;
#endif

    recorder_callbacks_context callbacks_context;
    callbacks_context.recording_saved_script = arg_parser->settings.recording_saved_script;
    callbacks_context.ipc = ipc;

    gsr_recorder_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.replay_saved = replay_saved_callback;
    callbacks.recording_started = recording_started_callback;
    callbacks.recording_stopped = recording_stopped_callback;
    callbacks.userdata = &callbacks_context;

    int error = GSR_ERROR_OK;
    recorder = gsr_recorder_create(&recorder_params, &callbacks, &error);
    if(!recorder)
        return error;

    apply_pending_signals(recorder);
    if(!atomic_load(&running))
        gsr_recorder_stop(recorder);

    gsr_ipc_handlers ipc_handlers;
    memset(&ipc_handlers, 0, sizeof(ipc_handlers));
    ipc_handlers.stop = ipc_stop_handler;
    ipc_handlers.toggle_pause = ipc_toggle_pause_handler;
    ipc_handlers.set_paused = ipc_set_paused_handler;
    ipc_handlers.toggle_replay_recording = ipc_toggle_replay_recording_handler;
    ipc_handlers.start_replay_recording = ipc_start_replay_recording_handler;
    ipc_handlers.stop_replay_recording = ipc_stop_replay_recording_handler;
    ipc_handlers.save_replay = ipc_save_replay_handler;
    ipc_handlers.userdata = &arg_parser->settings;

    int run_result = gsr_ipc_start(ipc, &ipc_handlers);
    if(run_result == GSR_ERROR_OK)
        run_result = gsr_recorder_run(recorder);

    gsr_ipc_complete_request(ipc, GSR_IPC_DEFERRED_REQUEST_STOP, true, arg_parser->settings.is_replaying ? NULL : arg_parser->settings.filename);
    gsr_ipc_stop(ipc);
    gsr_recorder_destroy(recorder, true);
    recorder = NULL;
    return run_result;
}

static int run(args_parser *arg_parser) {
    int exit_code = 0;

    gsr_capture_sources capture_sources;
    gsr_audio_input_tracks audio_input_tracks;
    gsr_app_audio_names app_audio_names;
    gsr_windowing windowing;
    gsr_capture_deps capture_deps;
    gsr_ipc ipc;
    memset(&audio_input_tracks, 0, sizeof(audio_input_tracks));
    memset(&app_audio_names, 0, sizeof(app_audio_names));
    memset(&windowing, 0, sizeof(windowing));
    memset(&ipc, 0, sizeof(ipc));
    gsr_capture_deps_init(&capture_deps);

    const Arg *ipc_arg = args_parser_get_arg(arg_parser, "-ipc");
    assert(ipc_arg);

    const int parse_capture_sources_result = gsr_capture_sources_parse(&capture_sources, arg_parser->settings.capture_source, arg_parser->settings.region_position, arg_parser->settings.region_size);
    if(parse_capture_sources_result != GSR_ERROR_OK) {
        exit_code = gsr_error_to_exit_code(parse_capture_sources_result);
        goto done;
    }

    if(capture_sources.num_items == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "option -w can't be empty. You need to capture video from at least one source");
        args_parser_print_usage();
        exit_code = 1;
        goto done;
    }

    const int validate_args_result = validate_args_with_capture_sources(arg_parser, &capture_sources);
    if(validate_args_result != GSR_ERROR_OK) {
        exit_code = gsr_error_to_exit_code(validate_args_result);
        goto done;
    }

    if(ipc_arg->num_values > 0 && gsr_ipc_init(&ipc, ipc_arg->values[0]) != GSR_ERROR_OK) {
        exit_code = 1;
        goto done;
    }

    const int parse_audio_inputs_result = parse_audio_inputs(arg_parser, &audio_input_tracks);
    if(parse_audio_inputs_result != GSR_ERROR_OK) {
        exit_code = gsr_error_to_exit_code(parse_audio_inputs_result);
        goto done;
    }

    const bool uses_app_audio = gsr_audio_input_tracks_has_app_audio(&audio_input_tracks);
#ifdef GSR_APP_AUDIO
    if(uses_app_audio) {
        const int app_audio_result = setup_app_audio(&app_audio_names);
        if(app_audio_result != GSR_ERROR_OK) {
            exit_code = gsr_error_to_exit_code(app_audio_result);
            goto done;
        }
    }
#else
    if(uses_app_audio) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "application audio can't be recorded because GPU Screen Recorder is built without application audio support (-Dapp_audio option)");
        exit_code = 2;
        goto done;
    }
#endif

    const int validate_app_audio_result = gsr_audio_input_tracks_validate_app_audio(&audio_input_tracks, &app_audio_names);
    if(validate_app_audio_result != GSR_ERROR_OK) {
        exit_code = gsr_error_to_exit_code(validate_app_audio_result);
        goto done;
    }

    gsr_windowing_params windowing_params;
    windowing_params.monitor_capture = gsr_capture_sources_has_monitor_or_region(&capture_sources);
    windowing_params.gl_debug = arg_parser->settings.gl_debug;
    windowing_params.listen_to_x11_events = true;
    if(gsr_windowing_init(&windowing, &windowing_params) != GSR_ERROR_OK) {
        exit_code = 1;
        goto done;
    }

    if(gsr_capture_sources_has_type(&capture_sources, GSR_CAPTURE_SOURCE_TYPE_PORTAL)) {
        if(gsr_windowing_is_using_prime_run()) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "use of prime-run with -w portal option is currently not supported. Disabling prime-run");
            gsr_windowing_disable_prime_run();
        }

        if(video_codec_is_hdr(arg_parser->settings.video_codec)) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "portal capture option doesn't support hdr yet (PipeWire doesn't support hdr), the video will be tonemapped from hdr to sdr");
            arg_parser->settings.video_codec = hdr_video_codec_to_sdr_video_codec(arg_parser->settings.video_codec);
        }
    }

    if(gsr_windowing_load_egl(&windowing, &windowing_params) != GSR_ERROR_OK) {
        exit_code = 1;
        goto done;
    }

    gsr_shader_enable_debug_output(arg_parser->settings.gl_debug);
#ifndef NDEBUG
    gsr_shader_enable_debug_output(true);
#endif

    if(!args_parser_validate_with_gl_info(arg_parser, &windowing.egl)) {
        exit_code = 1;
        goto done;
    }

    if(!windowing.card_path_found) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "no /dev/dri/cardX device found. Make sure that you have at least one monitor connected or record a single window instead on X11 or record with the -w portal option");
        exit_code = 2;
        goto done;
    }

    gsr_capture_deps_init_cursor(&capture_deps, &windowing.egl, arg_parser->settings.record_cursor);

    gsr_image_format image_format;
    if(get_image_format_from_filename(arg_parser->settings.filename, &image_format)) {
        if(audio_input_tracks.num_items > 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "can't record audio (-a) when taking a screenshot");
            exit_code = 1;
            goto done;
        }

        if(ipc_arg->num_values > 0)
            gsr_log(GSR_LOG_LEVEL_WARNING, "option -ipc has no effect when taking a screenshot");

        exit_code = gsr_error_to_exit_code(take_screenshot(arg_parser, &windowing, &capture_deps, &capture_sources, image_format));
    } else {
        exit_code = gsr_error_to_exit_code(record(arg_parser, &windowing, &capture_deps, &capture_sources, &audio_input_tracks, &ipc));
    }

    done:
    gsr_ipc_deinit(&ipc);
    gsr_capture_deps_deinit(&capture_deps);
    gsr_windowing_deinit(&windowing);
#ifdef GSR_APP_AUDIO
    gsr_pipewire_audio_deinit(&pipewire_audio);
#endif
    gsr_app_audio_names_deinit(&app_audio_names);
    gsr_audio_input_tracks_deinit(&audio_input_tracks);
    gsr_capture_sources_deinit(&capture_sources);
    return exit_code;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); /* Sigh... stupid C */
#ifdef __GLIBC__
    mallopt(M_MMAP_THRESHOLD, 65536);
#endif

    install_signal_handlers();
    set_environment_variables();
    install_cuda_no_stable_perf_limit();

    if(geteuid() == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "don't run gpu-screen-recorder as the root user");
        _exit(1);
    }

    args_handlers arg_handlers;
    arg_handlers.version = version_command;
    arg_handlers.info = info_command;
    arg_handlers.list_audio_devices = list_audio_devices_command;
    arg_handlers.list_application_audio = list_application_audio_command;
    arg_handlers.list_v4l2_devices = list_v4l2_devices;
    arg_handlers.list_capture_options = list_capture_options_command;
    arg_handlers.list_monitors = list_monitors_command;

    args_parser arg_parser;
    int exit_code = 0;
    int command_exit_code = 0;
    switch(args_parser_parse(&arg_parser, argc, argv, &arg_handlers, NULL, &command_exit_code)) {
        case ARGS_PARSE_RESULT_ERROR:
            exit_code = 1;
            break;
        case ARGS_PARSE_RESULT_COMMAND_HANDLED:
            exit_code = command_exit_code;
            break;
        case ARGS_PARSE_RESULT_OK: {
            if(!arg_parser.settings.low_power) {
                /* Forces low latency encoding mode. Use this environment variable until vaapi supports setting this as a parameter.
                   The downside of this is that it always uses maximum power, which is not ideal for replay mode that runs on system startup.
                   This option was added in mesa 24.1.4, released in july 17, 2024.
                   Seems like the performance issue is not in encoding, but rendering the frame.
                   Some frames end up taking 10 times longer. Seems to be an issue with amd gpu power management when letting the application sleep on the cpu side? */
                setenv("AMD_DEBUG", "lowlatencyenc", true);
            }

            exit_code = run(&arg_parser);
            break;
        }
    }

    args_parser_deinit(&arg_parser);

    /* We do an _exit here because cuda uses at_exit to do _something_ that causes the program to freeze,
       but only on some nvidia driver versions on some gpus (RTX?), and _exit exits the program without calling
       the at_exit registered functions.
       Cuda (nvenc) is loaded in a separate process, but this still happens. */
    _exit(exit_code);
}
