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
 * capture_setup_win32 seam, an empty audio track list).
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

#include "capture.h"
#include "display.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/window/window.h" /* full gsr_window struct */
#include "../../upstream/include/recorder/recorder.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/capture_source.h"
#include "../../upstream/include/recorder/settings.h"
#include "../../upstream/include/recorder/audio_input.h"

#define OUTPUT_FILENAME "recorder-self-test-output.mkv"
#define RECORD_SECONDS 4
#define FPS 10

typedef struct {
    gsr_recorder *recorder;
    int result;
} run_userdata;

static void *run_recorder_thread(void *userdata) {
    run_userdata *data = (run_userdata*)userdata;
    data->result = gsr_recorder_run(data->recorder);
    return NULL;
}

static void on_recording_stopped(const char *filepath, void *userdata) {
    (void)userdata;
    if(filepath)
        printf("recorder: recording stopped, file = %s\n", filepath);
    else
        printf("recorder: recording stopped (no file)\n");
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

    /* 7. No audio tracks (empty list is valid; the recorder handles it). */
    gsr_audio_input_tracks audio_input_tracks;
    memset(&audio_input_tracks, 0, sizeof(audio_input_tracks));

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

    run_userdata userdata;
    userdata.recorder = recorder;
    userdata.result = GSR_ERROR_GENERIC;
    pthread_t thread;
    if(pthread_create(&thread, NULL, run_recorder_thread, &userdata) != 0) {
        fprintf(stderr, "FAIL: could not create recorder thread\n");
        gsr_recorder_destroy(recorder, false);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }

    Sleep(RECORD_SECONDS * 1000);
    gsr_recorder_stop(recorder);
    pthread_join(thread, NULL);

    gsr_recorder_destroy(recorder, false);
    gsr_capture_sources_deinit(&capture_sources);
    gsr_capture_deps_deinit(&capture_deps);
    free(monitors);

    if(userdata.result != GSR_ERROR_OK) {
        fprintf(stderr, "FAIL: gsr_recorder_run returned %d\n", userdata.result);
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
    AVFormatContext *format = NULL;
    if(avformat_open_input(&format, OUTPUT_FILENAME, NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: could not open '%s' with libavformat\n", OUTPUT_FILENAME);
        return 1;
    }
    if(avformat_find_stream_info(format, NULL) < 0) {
        fprintf(stderr, "FAIL: could not read stream info from '%s'\n", OUTPUT_FILENAME);
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
        fprintf(stderr, "FAIL: no video stream in '%s'\n", OUTPUT_FILENAME);
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
    printf("\nPASS\n");
    return 0;
}
