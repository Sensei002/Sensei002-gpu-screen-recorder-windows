/* tests/recorder-self-test/main.c — Phase 7, milestone A self-test:
 * the upstream recorder end-to-end on Windows.
 *
 * Drives the REAL pipeline the engine will use: live capture of the
 * primary monitor (WGC, or DXGI Desktop Duplication as the fallback) ->
 * the ANGLE GL color-conversion -> libx264 (software encoder; the runner
 * has no NVIDIA GPU) -> Matroska mux -> a real .mkv file on disk.
 *
 * The recorder itself (upstream/src/recorder/recorder.c) runs unchanged;
 * this test only wires the pieces the recorder consumes (gsr_egl via the
 * win32 ANGLE loader, gsr_capture_deps/gsr_video_sources via the Phase 7
 * capture_setup_win32 seam, an audio track list).
 *
 * Phase 8: when the machine has a default audio output that WASAPI can
 * capture (loopback), a `-a default_output` track is added so the
 * recording carries a real AAC audio stream that is validated alongside
 * the video. Machines with no usable audio endpoint (e.g. some headless
 * runners) record video-only — the audio half of the pipeline is probed
 * first so it degrades to a clean skip, never a failure.
 *
 * SKIP semantics: like dxgi-self-test, the capture half needs a real
 * session — on CI the runner's Basic Display Adapter supports Desktop
 * Duplication, so this usually exercises a REAL end-to-end recording.
 * When neither WGC nor DXGI is available the test reports a clean SKIP
 * (exit 0) instead of failing.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>

#include "capture.h"
#include "display.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/window/window.h" /* full gsr_window struct */
#include "../../upstream/include/recorder/recorder.h"
#include "../../upstream/include/recorder/replay_save.h" /* GSR_SAVE_REPLAY_SECONDS_FULL */
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/capture_source.h"
#include "../../upstream/include/recorder/settings.h"
#include "../../upstream/include/recorder/audio_input.h"

#define OUTPUT_FILENAME "recorder-self-test-output.mkv"
#define RECORD_SECONDS 4
#define FPS 10

/* The recorder must run on the thread that made the GL context current
   (upstream: the CLI thread that loaded egl). gsr_recorder_stop is safe
   to call from another thread (atomic store), so a timer thread stops the
   recording while the recorder runs on the main thread. */
static void *stop_recorder_thread(void *userdata) {
    gsr_recorder *recorder = (gsr_recorder*)userdata;
    Sleep(RECORD_SECONDS * 1000);
    gsr_recorder_stop(recorder);
    return NULL;
}

static void on_recording_stopped(const char *filepath, void *userdata) {
    (void)userdata;
    if(filepath)
        printf("recorder: recording stopped, file = %s\n", filepath);
    else
        printf("recorder: recording stopped (no file)\n");
}

/* ---- Phase 9: replay buffer save pass ------------------------------------
 * A second recording that runs with a replay buffer (-r) and disk storage,
 * then saves it mid-recording: a 2-second save and two FULL saves with
 * -restart-replay-on-save enabled. The recorder's replay_save machinery
 * (clone -> thread -> Replay_*.mkv) is upstream code; this pass is the
 * end-to-end Windows verification of it. The saved files are validated
 * like the main recording: container opens, h264 video stream, sane
 * duration. The third (post-restart) save must be SHORTER than the second
 * — the restart cleared the buffer, so it only holds what was recorded
 * after the restart — which proves -restart-replay-on-save actually works.
 */
#define REPLAY_OUTPUT_DIR "replay-self-test-output"
#define REPLAY_RECORD_SECONDS 8
#define REPLAY_FPS 10

typedef struct {
    char filepath[PATH_MAX];
} saved_replay_file;

static saved_replay_file saved_replays[4];
static int num_saved_replays = 0;

static void on_replay_saved(const char *filepath, void *userdata) {
    (void)userdata;
    if(!filepath) {
        printf("recorder: replay saved (no file — save failed or empty)\n");
        return;
    }
    if(num_saved_replays < 4) {
        snprintf(saved_replays[num_saved_replays].filepath, sizeof(saved_replays[0].filepath), "%s", filepath);
        ++num_saved_replays;
        printf("recorder: replay saved: %s\n", filepath);
    }
}

typedef struct {
    gsr_recorder *recorder;
} replay_driver;

/* Schedule: save 2s at t=2s, FULL at t=5s (restarts the buffer), FULL again
   at t=6.8s (must be short — it only holds what was recorded after the
   restart, one keyframe interval), stop at t=7.5s. The recorder polls the
   atomic save requests every frame, so each save starts within ~100ms. */
static void *replay_driver_thread(void *userdata) {
    replay_driver *driver = (replay_driver*)userdata;
    gsr_recorder *recorder = driver->recorder;
    Sleep(2000);
    gsr_recorder_save_replay(recorder, 2, GSR_RESTART_REPLAY_ENABLE);
    Sleep(3000);
    gsr_recorder_save_replay(recorder, GSR_SAVE_REPLAY_SECONDS_FULL, GSR_RESTART_REPLAY_ENABLE);
    Sleep(1800);
    gsr_recorder_save_replay(recorder, GSR_SAVE_REPLAY_SECONDS_FULL, GSR_RESTART_REPLAY_ENABLE);
    Sleep(700);
    gsr_recorder_stop(recorder);
    return NULL;
}

static int validate_replay_file(const char *filepath, double min_duration, double max_duration, const char *label) {
    AVFormatContext *format = NULL;
    const int open_result = avformat_open_input(&format, filepath, NULL, NULL);
    if(open_result != 0) {
        char errbuf[128] = {0};
        av_strerror(open_result, errbuf, sizeof(errbuf));
        fprintf(stderr, "FAIL: %s: could not open '%s': %s\n", label, filepath, errbuf);
        return 1;
    }
    if(avformat_find_stream_info(format, NULL) < 0) {
        fprintf(stderr, "FAIL: %s: could not read stream info from '%s'\n", label, filepath);
        avformat_close_input(&format);
        return 1;
    }

    int video_stream_index = -1;
    for(unsigned int i = 0; i < format->nb_streams; ++i) {
        if(format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = (int)i;
            break;
        }
    }
    if(video_stream_index < 0) {
        fprintf(stderr, "FAIL: %s: no video stream in '%s'\n", label, filepath);
        avformat_close_input(&format);
        return 1;
    }

    const AVCodecParameters *codecpar = format->streams[video_stream_index]->codecpar;
    const double duration = format->duration > 0 ? (double)format->duration / AV_TIME_BASE : 0.0;
    printf("recorder: %s: '%s' = %s %dx%d, duration %.2fs\n", label, filepath,
        avcodec_get_name(codecpar->codec_id), codecpar->width, codecpar->height, duration);

    int failures = 0;
    if(codecpar->codec_id != AV_CODEC_ID_H264) {
        fprintf(stderr, "FAIL: %s: expected h264, got %s\n", label, avcodec_get_name(codecpar->codec_id));
        ++failures;
    }
    if(duration < min_duration || duration > max_duration) {
        fprintf(stderr, "FAIL: %s: duration %.2fs outside [%.2f, %.2f]\n", label, duration, min_duration, max_duration);
        ++failures;
    }

    int video_packets = 0;
    AVPacket *packet = av_packet_alloc();
    while(av_read_frame(format, packet) == 0 && video_packets < 5) {
        if(packet->stream_index == video_stream_index)
            ++video_packets;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if(video_packets == 0) {
        fprintf(stderr, "FAIL: %s: no video packets in '%s'\n", label, filepath);
        ++failures;
    }

    avformat_close_input(&format);
    return failures;
}

static int run_replay_pass(const gsr_egl *egl, const gsr_window *window) {
    printf("\nrecorder: ===== Phase 9: replay buffer pass (disk, 2s + FULL saves, restart-on-save) =====\n");

    gsr_platform_monitor *monitors = NULL;
    int monitor_count = 0;
    if(!gsr_platform_display_list_monitors(&monitors, &monitor_count) || monitor_count == 0) {
        fprintf(stderr, "FAIL: replay pass: no monitors enumerated\n");
        return 1;
    }
    const gsr_platform_monitor *primary = &monitors[0];
    for(int i = 0; i < monitor_count; ++i) {
        if(monitors[i].is_primary) {
            primary = &monitors[i];
            break;
        }
    }
    printf("recorder: replay pass: primary monitor = %s (%dx%d)\n", primary->name, primary->width, primary->height);

    /* Replay mode: -o is a DIRECTORY (-c is required), -r sets the buffer. */
    gsr_recorder_settings settings;
    memset(&settings, 0, sizeof(settings));
    settings.video_encoder = GSR_VIDEO_ENCODER_HW_CPU;
    settings.pixel_format = GSR_PIXEL_FORMAT_YUV420;
    settings.framerate_mode = GSR_FRAMERATE_MODE_CONSTANT;
    settings.color_range = GSR_COLOR_RANGE_LIMITED;
    settings.tune = GSR_TUNE_QUALITY;
    settings.video_codec = GSR_VIDEO_CODEC_H264;
    settings.audio_codec = GSR_AUDIO_CODEC_AAC;
    settings.bitrate_mode = GSR_BITRATE_MODE_QP;
    settings.video_quality = GSR_VIDEO_QUALITY_HIGH;
    settings.replay_storage = GSR_REPLAY_STORAGE_DISK;
    settings.capture_source = primary->name;
    settings.container_format = "matroska";
    settings.filename = REPLAY_OUTPUT_DIR;
    settings.verbose = true;
    settings.fallback_cpu_encoding = true;
    settings.record_cursor = false;
    settings.is_replaying = true;
    settings.is_livestream = false;
    settings.is_output_piped = false;
    settings.fps = REPLAY_FPS;
    settings.replay_buffer_size_secs = 8;
    /* The keyint setting is in SECONDS (x264 keyint = keyint * fps), so 1.0
       gives a keyframe every 1s — every save finds one, including the save
       after the restart-clear. */
    settings.keyint = 1.0;
    settings.restart_replay_on_save = true;
    settings.date_folders = false;

    gsr_windowing windowing;
    memset(&windowing, 0, sizeof(windowing));
    windowing.window = (gsr_window*)window;
    windowing.egl = *egl;
    windowing.egl_loaded = true;

    gsr_capture_deps capture_deps;
    gsr_capture_deps_init(&capture_deps);
    gsr_capture_deps_init_cursor(&capture_deps, &windowing.egl, false);

    gsr_capture_sources capture_sources;
    const int parse_result = gsr_capture_sources_parse(&capture_sources, primary->name, (vec2i){0, 0}, (vec2i){0, 0});
    if(parse_result != GSR_ERROR_OK || capture_sources.num_items == 0) {
        fprintf(stderr, "FAIL: replay pass: could not parse capture source '%s'\n", primary->name);
        free(monitors);
        return 1;
    }

    gsr_audio_input_tracks audio_input_tracks;
    memset(&audio_input_tracks, 0, sizeof(audio_input_tracks)); /* video-only */

    gsr_recorder_params params;
    memset(&params, 0, sizeof(params));
    params.settings = &settings;
    params.windowing = &windowing;
    params.capture_deps = &capture_deps;
    params.capture_sources = &capture_sources;
    params.audio_input_tracks = &audio_input_tracks;
    params.plugin_filepaths = NULL;
    params.num_plugin_filepaths = 0;

    gsr_recorder_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.replay_saved = on_replay_saved;

    int error = GSR_ERROR_GENERIC;
    gsr_recorder *recorder = gsr_recorder_create(&params, &callbacks, &error);
    if(!recorder) {
        fprintf(stderr, "FAIL: replay pass: gsr_recorder_create failed (error %d)\n", error);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }
    printf("recorder: replay pass: recording %d seconds with a %d-second replay buffer...\n",
        REPLAY_RECORD_SECONDS, (int)settings.replay_buffer_size_secs);

    num_saved_replays = 0;
    replay_driver driver;
    driver.recorder = recorder;
    pthread_t driver_thread;
    if(pthread_create(&driver_thread, NULL, replay_driver_thread, &driver) != 0) {
        fprintf(stderr, "FAIL: replay pass: could not create driver thread\n");
        gsr_recorder_destroy(recorder, false);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }

    const int run_result = gsr_recorder_run(recorder);
    pthread_join(driver_thread, NULL);

    gsr_recorder_destroy(recorder, false);
    gsr_capture_sources_deinit(&capture_sources);
    gsr_capture_deps_deinit(&capture_deps);
    gsr_audio_input_tracks_deinit(&audio_input_tracks);
    free(monitors);

    if(run_result != GSR_ERROR_OK) {
        fprintf(stderr, "FAIL: replay pass: gsr_recorder_run returned %d\n", run_result);
        return 1;
    }
    if(num_saved_replays != 3) {
        fprintf(stderr, "FAIL: replay pass: expected 3 saved replays, got %d\n", num_saved_replays);
        return 1;
    }

    int failures = 0;
    /* The saved durations are pts-derived, and the recorder's pts runs a
       bit short during its startup burst on the slow CI runner, so the
       bounds are deliberately loose: what matters is that each save is a
       real, valid file and that the post-restart save is clearly shorter
       than the FULL save (the restart actually cleared the buffer). */
    failures += validate_replay_file(saved_replays[0].filepath, 0.3, 4.0, "replay 2s save");
    failures += validate_replay_file(saved_replays[1].filepath, 2.0, 8.0, "replay FULL save");
    /* -restart-replay-on-save cleared the buffer after the FULL save, so
       this save only contains what was recorded after the restart (about
       one keyframe interval, 1s) — it must be shorter than the FULL save. */
    failures += validate_replay_file(saved_replays[2].filepath, 0.4, 3.0, "replay post-restart save");
    if(failures == 0) {
        /* Parse the durations out again just for the comparison log. */
        AVFormatContext *fmt = NULL;
        if(avformat_open_input(&fmt, saved_replays[1].filepath, NULL, NULL) == 0) {
            avformat_find_stream_info(fmt, NULL);
            const double full_duration = fmt->duration > 0 ? (double)fmt->duration / AV_TIME_BASE : 0.0;
            avformat_close_input(&fmt);
            fmt = NULL;
            if(avformat_open_input(&fmt, saved_replays[2].filepath, NULL, NULL) == 0) {
                avformat_find_stream_info(fmt, NULL);
                const double post_restart_duration = fmt->duration > 0 ? (double)fmt->duration / AV_TIME_BASE : 0.0;
                avformat_close_input(&fmt);
                printf("recorder: restart-replay-on-save: FULL=%.2fs vs post-restart=%.2fs\n",
                    full_duration, post_restart_duration);
                if(post_restart_duration >= full_duration) {
                    fprintf(stderr, "FAIL: replay pass: buffer was not restarted by the FULL save\n");
                    ++failures;
                }
            }
        }
    }

    /* Clean up the saved files and the output directory (the recorder
       already removed the disk buffer's own session directory). */
    for(int i = 0; i < num_saved_replays; ++i)
        remove(saved_replays[i].filepath);
    RemoveDirectoryA(REPLAY_OUTPUT_DIR);

    if(failures > 0)
        return 1;
    printf("recorder: replay pass OK\n");
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("recorder-self-test: Phase 7 milestone A (end-to-end recording)\n");

    /* 1. Capture availability: the whole test needs at least one backend. */
    const bool wgc_supported = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_WGC);
    const bool dxgi_supported = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_DXGI_DUPLICATION);
    if(!wgc_supported && !dxgi_supported) {
        printf("SKIP: no capture backend available in this session (WGC=%d, DXGI=%d); exit 0\n",
            wgc_supported ? 1 : 0, dxgi_supported ? 1 : 0);
        return 0;
    }

    /* 2. Primary monitor name (the -w value). */
    gsr_platform_monitor *monitors = NULL;
    int monitor_count = 0;
    if(!gsr_platform_display_list_monitors(&monitors, &monitor_count) || monitor_count == 0) {
        fprintf(stderr, "FAIL: no monitors enumerated\n");
        return 1;
    }
    const gsr_platform_monitor *primary = &monitors[0];
    for(int i = 0; i < monitor_count; ++i) {
        if(monitors[i].is_primary) {
            primary = &monitors[i];
            break;
        }
    }
    printf("recorder: primary monitor = %s (%dx%d)\n", primary->name, primary->width, primary->height);

    /* 3. The GL pipeline (ANGLE on D3D11, Phase 5b). */
    gsr_egl egl;
    memset(&egl, 0, sizeof(egl));
    gsr_window window;
    memset(&window, 0, sizeof(window)); /* the win32 loader only stores it */
    if(!gsr_egl_load_win32(&egl, &window, false)) {
        /* Same contract as render-self-test: the ANGLE DLLs only exist in
           the MSYS2 build environment, so the bare-Windows `test` job sees
           a clean SKIP instead of a failure. */
        printf("SKIP: ANGLE initialization failed (see gsr error logs above); exit 0\n");
        free(monitors);
        return 0;
    }
    printf("recorder: egl loaded (vendor %d)\n", (int)egl.gpu_info.vendor);

    /* 4. The windowing/capture-deps the recorder consumes. */
    gsr_windowing windowing;
    memset(&windowing, 0, sizeof(windowing));
    windowing.window = &window;
    windowing.egl = egl;
    windowing.egl_loaded = true;

    gsr_capture_deps capture_deps;
    gsr_capture_deps_init(&capture_deps);
    gsr_capture_deps_init_cursor(&capture_deps, &windowing.egl, false);

    /* 5. Settings: monitor capture, libx264 (software), Matroska, CFR. */
    gsr_recorder_settings settings;
    memset(&settings, 0, sizeof(settings));
    settings.video_encoder = GSR_VIDEO_ENCODER_HW_CPU;
    settings.pixel_format = GSR_PIXEL_FORMAT_YUV420; /* not used by h264 */
    settings.framerate_mode = GSR_FRAMERATE_MODE_CONSTANT;
    settings.color_range = GSR_COLOR_RANGE_LIMITED;
    settings.tune = GSR_TUNE_QUALITY;
    settings.video_codec = GSR_VIDEO_CODEC_H264;
    settings.audio_codec = GSR_AUDIO_CODEC_AAC;
    settings.bitrate_mode = GSR_BITRATE_MODE_QP;
    settings.video_quality = GSR_VIDEO_QUALITY_HIGH;
    settings.replay_storage = GSR_REPLAY_STORAGE_RAM;
    settings.capture_source = primary->name;
    settings.container_format = "matroska";
    settings.filename = OUTPUT_FILENAME;
    settings.verbose = true;
    settings.fallback_cpu_encoding = true;
    settings.record_cursor = false;
    settings.is_replaying = false;
    settings.is_livestream = false;
    settings.is_output_piped = false;
    settings.fps = FPS;
    settings.replay_buffer_size_secs = 0;
    settings.keyint = 60;

    /* 6. Capture sources: -w <primary monitor>. */
    gsr_capture_sources capture_sources;
    const int parse_result = gsr_capture_sources_parse(&capture_sources, primary->name, (vec2i){0, 0}, (vec2i){0, 0});
    if(parse_result != GSR_ERROR_OK || capture_sources.num_items == 0) {
        fprintf(stderr, "FAIL: could not parse capture source '%s'\n", primary->name);
        free(monitors);
        return 1;
    }

    /* 7. Audio: enumerate WASAPI endpoints and, when a default output is
       actually capturable, add a `-a default_output` track (Phase 8). The
       probe opens the device and reads one chunk so a headless machine
       with no usable audio falls back to video-only instead of failing
       the recording. */
    gsr_audio_devices audio_devices;
    memset(&audio_devices, 0, sizeof(audio_devices));
    get_pulseaudio_inputs(&audio_devices);
    printf("recorder: %zu audio device(s); default output: '%s'; default input: '%s'\n",
        audio_devices.num_items,
        audio_devices.default_output[0] ? audio_devices.default_output : "(none)",
        audio_devices.default_input[0] ? audio_devices.default_input : "(none)");
    for(size_t i = 0; i < audio_devices.num_items; ++i) {
        printf("recorder:   audio device %zu: %s (%s)\n", i, audio_devices.items[i].name, audio_devices.items[i].description);
    }

    gsr_audio_input_tracks audio_input_tracks;
    memset(&audio_input_tracks, 0, sizeof(audio_input_tracks));
    bool have_audio = false;
    if(audio_devices.default_output[0] != '\0') {
        SoundDevice probe;
        memset(&probe, 0, sizeof(probe));
        if(sound_device_get_by_name(&probe, "probe", "default_output", "probe", 2, 1024, GSR_AUDIO_FORMAT_F32) == 0 && probe.handle) {
            sound_device_flush(&probe);
            void *chunk = NULL;
            double latency = 0.0;
            const int got = sound_device_read_next_chunk(&probe, &chunk, 1.0, &latency);
            sound_device_close(&probe);
            if(got > 0) {
                const char *audio_args[] = { "default_output" };
                if(gsr_audio_input_tracks_parse(&audio_input_tracks, audio_args, 1, &audio_devices) == GSR_ERROR_OK) {
                    have_audio = true;
                    printf("recorder: audio track added (default_output), probe read %d frame(s)\n", got);
                }
            }
        }
        if(!have_audio)
            printf("recorder: default output exists but is not capturable; recording video-only\n");
    } else {
        printf("recorder: no default audio output; recording video-only\n");
    }
    gsr_audio_devices_deinit(&audio_devices);

    /* 8. Create + run the recorder. */
    gsr_recorder_params params;
    memset(&params, 0, sizeof(params));
    params.settings = &settings;
    params.windowing = &windowing;
    params.capture_deps = &capture_deps;
    params.capture_sources = &capture_sources;
    params.audio_input_tracks = &audio_input_tracks;
    params.plugin_filepaths = NULL;
    params.num_plugin_filepaths = 0;

    gsr_recorder_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.recording_stopped = on_recording_stopped;

    int error = GSR_ERROR_GENERIC;
    gsr_recorder *recorder = gsr_recorder_create(&params, &callbacks, &error);
    if(!recorder) {
        fprintf(stderr, "FAIL: gsr_recorder_create failed (error %d)\n", error);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }
    printf("recorder: created, recording %d seconds of %s at %dfps...\n",
        RECORD_SECONDS, primary->name, (int)settings.fps);

    pthread_t stop_thread;
    if(pthread_create(&stop_thread, NULL, stop_recorder_thread, recorder) != 0) {
        fprintf(stderr, "FAIL: could not create stop thread\n");
        gsr_recorder_destroy(recorder, false);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }

    const int run_result = gsr_recorder_run(recorder);
    pthread_join(stop_thread, NULL);

    gsr_recorder_destroy(recorder, false);
    gsr_capture_sources_deinit(&capture_sources);
    gsr_capture_deps_deinit(&capture_deps);
    gsr_audio_input_tracks_deinit(&audio_input_tracks);
    free(monitors);

    if(run_result != GSR_ERROR_OK) {
        fprintf(stderr, "FAIL: gsr_recorder_run returned %d\n", run_result);
        return 1;
    }

    /* 9. The output file must exist and be non-trivial. */
    FILE *f = fopen(OUTPUT_FILENAME, "rb");
    if(!f) {
        fprintf(stderr, "FAIL: output file '%s' was not created\n", OUTPUT_FILENAME);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    printf("recorder: output '%s' is %ld bytes\n", OUTPUT_FILENAME, size);
    if(size < 1024) {
        fprintf(stderr, "FAIL: output file is suspiciously small (%ld bytes)\n", size);
        return 1;
    }

    /* 10. Validate the container with libavformat (the ffmpeg build has no
       ffprobe binary, so the test is its own validator). */
    {
        /* Matroska/EBML magic is 0x1A 0x45 0xDF 0xA3. */
        unsigned char magic[8] = {0};
        f = fopen(OUTPUT_FILENAME, "rb");
        if(f) {
            const size_t got = fread(magic, 1, sizeof(magic), f);
            fclose(f);
            printf("recorder: file magic (%d bytes): %02x %02x %02x %02x %02x %02x %02x %02x\n",
                (int)got, magic[0], magic[1], magic[2], magic[3], magic[4], magic[5], magic[6], magic[7]);
        }
    }
    AVFormatContext *format = NULL;
    const int open_result = avformat_open_input(&format, OUTPUT_FILENAME, NULL, NULL);
    if(open_result != 0) {
        char errbuf[128] = {0};
        av_strerror(open_result, errbuf, sizeof(errbuf));
        fprintf(stderr, "FAIL: could not open '%s' with libavformat: %s\n", OUTPUT_FILENAME, errbuf);
        return 1;
    }
    if(avformat_find_stream_info(format, NULL) < 0) {
        fprintf(stderr, "FAIL: could not read stream info from '%s'\n", OUTPUT_FILENAME);
        avformat_close_input(&format);
        return 1;
    }

    int video_stream_index = -1;
    int audio_stream_index = -1;
    for(unsigned int i = 0; i < format->nb_streams; ++i) {
        if(format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0)
            video_stream_index = (int)i;
        else if(format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index < 0)
            audio_stream_index = (int)i;
    }
    if(video_stream_index < 0) {
        fprintf(stderr, "FAIL: no video stream in '%s'\n", OUTPUT_FILENAME);
        avformat_close_input(&format);
        return 1;
    }
    if(have_audio && audio_stream_index < 0) {
        fprintf(stderr, "FAIL: audio track was recorded but no audio stream in '%s'\n", OUTPUT_FILENAME);
        avformat_close_input(&format);
        return 1;
    }
    const AVCodecParameters *codecpar = format->streams[video_stream_index]->codecpar;
    const AVRational time_base = format->streams[video_stream_index]->time_base;
    printf("recorder: validated '%s': %s %dx%d, %d streams, duration %.2fs\n",
        OUTPUT_FILENAME,
        avcodec_get_name(codecpar->codec_id),
        codecpar->width, codecpar->height,
        (int)format->nb_streams,
        format->duration > 0 ? (double)format->duration / AV_TIME_BASE : 0.0);
    if(audio_stream_index >= 0) {
        const AVCodecParameters *audio_codecpar = format->streams[audio_stream_index]->codecpar;
        printf("recorder: audio stream: %s, %d Hz, %d channel(s)\n",
            avcodec_get_name(audio_codecpar->codec_id),
            audio_codecpar->sample_rate,
            audio_codecpar->ch_layout.nb_channels);
    }

    /* Read a few packets to prove the stream decodes structurally. */
    int video_packets = 0;
    AVPacket *packet = av_packet_alloc();
    while(av_read_frame(format, packet) == 0 && video_packets < 5) {
        if(packet->stream_index == video_stream_index)
            ++video_packets;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    if(video_packets == 0) {
        fprintf(stderr, "FAIL: no video packets in '%s'\n", OUTPUT_FILENAME);
        avformat_close_input(&format);
        return 1;
    }
    printf("recorder: read %d video packet(s) from '%s' (first pts %ld, time_base %d/%d)\n",
        video_packets, OUTPUT_FILENAME,
        (long)format->streams[video_stream_index]->start_time == AV_NOPTS_VALUE ? -1L : (long)format->streams[video_stream_index]->start_time,
        time_base.num, time_base.den);
    avformat_close_input(&format);

    printf("recorder: end-to-end recording OK\n");

    /* 11. Phase 9: the replay-buffer save pass (reuses the loaded egl). */
    if(run_replay_pass(&egl, &window) != 0) {
        fprintf(stderr, "FAIL: replay buffer pass failed\n");
        return 1;
    }

    printf("\nPASS\n");
    return 0;
}
