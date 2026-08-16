/* tests/nvenc-self-test/main.c — Phase 7, milestone B self-test: the
 * NVENC capability logic + the -encoder gpu fallback path.
 *
 * The runner has no NVIDIA GPU (Basic Display Adapter), so the real
 * h264_nvenc encode cannot run in CI — the same situation as WASAPI in
 * Phase 8. What is proven here:
 *
 *   1. The pure GPU-generation table (adapter description -> generation ->
 *      supported codecs), driven with real NVIDIA adapter strings.
 *   2. The live probe is HONEST: gsr_get_supported_video_codecs_nvenc must
 *      report nothing on a machine whose DXGI adapter is not NVIDIA, and
 *      must report H.264 (at least) when the adapter IS NVIDIA.
 *   3. The end-to-end fallback semantics: -encoder gpu with
 *      -fallback-cpu-encoding yes on a machine without NVENC records a
 *      real file via the software encoder (force_cpu_encoding -> libx264),
 *      proving the selection path — including the new Windows NVENC probe —
 *      degrades exactly as upstream designed.
 *
 * Part 3 needs ANGLE (libEGL.dll, only in the MSYS2 ctest step) and a
 * capture backend; it SKIPs (exit 0) like recorder-self-test where either
 * is missing. Parts 1-2 always run.
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
#include "gsr_nvenc_internal.h"
#include "../../upstream/include/codec_query/codec_query.h"
#include "../../upstream/include/codec_query/nvenc.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/window/window.h"
#include "../../upstream/include/recorder/recorder.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/capture_source.h"
#include "../../upstream/include/recorder/settings.h"

#define OUTPUT_FILENAME "nvenc-self-test-output.mkv"
#define RECORD_SECONDS 3
#define FPS 10

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) \
    do { \
        ++num_checks; \
        if(!(cond)) { \
            ++num_failures; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static void check_generation(const char *description, gsr_nvenc_generation expected) {
    const gsr_nvenc_generation got = gsr_nvenc_generation_from_adapter_description(description);
    if(got != expected) {
        ++num_failures;
        fprintf(stderr, "FAIL: '%s' -> generation %d, expected %d\n", description, (int)got, (int)expected);
    }
    ++num_checks;
}

static void check_generation_caps(const char *description, bool expect_hevc, bool expect_hevc_10bit, bool expect_av1) {
    const gsr_nvenc_generation gen = gsr_nvenc_generation_from_adapter_description(description);
    const gsr_nvenc_generation_caps *caps = gsr_nvenc_get_generation_caps(gen);
    ++num_checks;
    if(caps->hevc != expect_hevc || caps->hevc_10bit != expect_hevc_10bit || caps->av1 != expect_av1) {
        ++num_failures;
        fprintf(stderr, "FAIL: '%s' caps: hevc=%d hevc10=%d av1=%d, expected %d/%d/%d\n",
            description, caps->hevc ? 1 : 0, caps->hevc_10bit ? 1 : 0, caps->av1 ? 1 : 0,
            expect_hevc ? 1 : 0, expect_hevc_10bit ? 1 : 0, expect_av1 ? 1 : 0);
    }
}

/* ---- part 3: the -encoder gpu fallback recording ----------------------- */

static void *stop_recorder_thread(void *userdata) {
    gsr_recorder *recorder = (gsr_recorder*)userdata;
    Sleep(RECORD_SECONDS * 1000);
    gsr_recorder_stop(recorder);
    return NULL;
}

static int run_fallback_recording(void) {
    /* Capture availability. */
    const bool wgc = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_WGC);
    const bool dxgi = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_DXGI_DUPLICATION);
    if(!wgc && !dxgi) {
        printf("nvenc: SKIP fallback recording (no capture backend); exit 0\n");
        return 0;
    }

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

    gsr_egl egl;
    memset(&egl, 0, sizeof(egl));
    gsr_window window;
    memset(&window, 0, sizeof(window));
    if(!gsr_egl_load_win32(&egl, &window, false)) {
        printf("nvenc: SKIP fallback recording (no ANGLE); exit 0\n");
        free(monitors);
        return 0;
    }

    gsr_windowing windowing;
    memset(&windowing, 0, sizeof(windowing));
    windowing.window = &window;
    windowing.egl = egl;
    windowing.egl_loaded = true;

    gsr_capture_deps capture_deps;
    gsr_capture_deps_init(&capture_deps);
    gsr_capture_deps_init_cursor(&capture_deps, &windowing.egl, false);

    gsr_recorder_settings settings;
    memset(&settings, 0, sizeof(settings));
    /* The point of this test: request the GPU encoder with NO nvenc-capable
       GPU; the fallback must silently pick libx264 and record. */
    settings.video_encoder = GSR_VIDEO_ENCODER_HW_GPU;
    settings.pixel_format = GSR_PIXEL_FORMAT_YUV420;
    settings.framerate_mode = GSR_FRAMERATE_MODE_CONSTANT;
    settings.color_range = GSR_COLOR_RANGE_LIMITED;
    settings.tune = GSR_TUNE_QUALITY;
    settings.video_codec = (gsr_video_codec)GSR_VIDEO_CODEC_AUTO; /* exercises the auto-select fallback */
    settings.audio_codec = GSR_AUDIO_CODEC_AAC;
    settings.bitrate_mode = GSR_BITRATE_MODE_QP;
    settings.video_quality = GSR_VIDEO_QUALITY_HIGH;
    settings.replay_storage = GSR_REPLAY_STORAGE_RAM;
    settings.capture_source = primary->name;
    settings.container_format = "matroska";
    settings.filename = OUTPUT_FILENAME;
    settings.verbose = true;
    settings.fallback_cpu_encoding = true; /* the path under test */
    settings.record_cursor = false;
    settings.is_replaying = false;
    settings.is_livestream = false;
    settings.is_output_piped = false;
    settings.fps = FPS;
    settings.replay_buffer_size_secs = 0;
    settings.keyint = 60;

    gsr_capture_sources capture_sources;
    if(gsr_capture_sources_parse(&capture_sources, primary->name, (vec2i){0, 0}, (vec2i){0, 0}) != GSR_ERROR_OK || capture_sources.num_items == 0) {
        fprintf(stderr, "FAIL: could not parse capture source\n");
        free(monitors);
        return 1;
    }

    gsr_audio_input_tracks audio_input_tracks;
    memset(&audio_input_tracks, 0, sizeof(audio_input_tracks));

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

    int error = GSR_ERROR_GENERIC;
    gsr_recorder *recorder = gsr_recorder_create(&params, &callbacks, &error);
    if(!recorder) {
        fprintf(stderr, "FAIL: gsr_recorder_create failed (error %d) — the -encoder gpu fallback did not engage\n", error);
        gsr_capture_sources_deinit(&capture_sources);
        gsr_capture_deps_deinit(&capture_deps);
        free(monitors);
        return 1;
    }

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
    free(monitors);

    if(run_result != GSR_ERROR_OK) {
        fprintf(stderr, "FAIL: gsr_recorder_run returned %d\n", run_result);
        return 1;
    }

    /* Validate the file: it must exist and be a readable matroska. */
    FILE *f = fopen(OUTPUT_FILENAME, "rb");
    if(!f) {
        fprintf(stderr, "FAIL: no output file '%s'\n", OUTPUT_FILENAME);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    printf("nvenc: fallback recording produced '%s' (%ld bytes)\n", OUTPUT_FILENAME, size);
    if(size < 1024) {
        fprintf(stderr, "FAIL: output file suspiciously small (%ld bytes)\n", size);
        return 1;
    }
    AVFormatContext *format = NULL;
    if(avformat_open_input(&format, OUTPUT_FILENAME, NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: could not open '%s' with libavformat\n", OUTPUT_FILENAME);
        return 1;
    }
    if(avformat_find_stream_info(format, NULL) < 0) {
        fprintf(stderr, "FAIL: no stream info in '%s'\n", OUTPUT_FILENAME);
        avformat_close_input(&format);
        return 1;
    }
    int video_streams = 0;
    for(unsigned int i = 0; i < format->nb_streams; ++i) {
        if(format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            ++video_streams;
    }
    /* Copy the fields we need BEFORE closing the input: codecpar is owned
       by the format context and freed by avformat_close_input. */
    const AVCodecParameters *codecpar = video_streams > 0 ? format->streams[0]->codecpar : NULL;
    printf("nvenc: fallback file validated: %d video stream(s), codec %s, %dx%d\n",
        video_streams,
        codecpar ? avcodec_get_name(codecpar->codec_id) : "(none)",
        codecpar ? codecpar->width : 0, codecpar ? codecpar->height : 0);
    const enum AVCodecID codec_id = codecpar ? codecpar->codec_id : AV_CODEC_ID_NONE;
    avformat_close_input(&format);
    if(video_streams == 0) {
        fprintf(stderr, "FAIL: no video stream in the fallback recording\n");
        return 1;
    }
    if(codec_id != AV_CODEC_ID_H264) {
        fprintf(stderr, "FAIL: fallback recording is %s, expected h264 (libx264)\n", avcodec_get_name(codec_id));
        return 1;
    }
    printf("nvenc: -encoder gpu fallback recorded h264 via the software encoder OK\n");
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("nvenc-self-test: NVENC capability logic (headless)\n");

    /* 1. Generation table. */
    printf("-- generation table\n");
    check_generation("NVIDIA GeForce GTX 960", GSR_NVENC_GEN_MAXWELL);
    check_generation("NVIDIA GeForce GTX 1060 6GB", GSR_NVENC_GEN_PASCAL);
    check_generation("NVIDIA GeForce GTX 1080 Ti", GSR_NVENC_GEN_PASCAL);
    check_generation("NVIDIA GeForce GTX 1660 SUPER", GSR_NVENC_GEN_TURING);
    check_generation("NVIDIA GeForce RTX 2070", GSR_NVENC_GEN_TURING);
    check_generation("NVIDIA Quadro RTX 4000", GSR_NVENC_GEN_TURING);
    check_generation("NVIDIA Quadro RTX 6000", GSR_NVENC_GEN_TURING);
    check_generation("NVIDIA GeForce RTX 3060 Ti", GSR_NVENC_GEN_AMPERE);
    check_generation("NVIDIA Quadro RTX A6000", GSR_NVENC_GEN_AMPERE);
    check_generation("NVIDIA GeForce RTX 4080", GSR_NVENC_GEN_ADA);
    check_generation("NVIDIA GeForce RTX 5090", GSR_NVENC_GEN_BLACKWELL);
    check_generation("Microsoft Basic Display Adapter", GSR_NVENC_GEN_UNKNOWN);
    check_generation("NVIDIA GeForce RTX 4090 Laptop GPU", GSR_NVENC_GEN_ADA);
    CHECK(!gsr_nvenc_description_is_nvidia("Microsoft Basic Display Adapter"));
    CHECK(gsr_nvenc_description_is_nvidia("NVIDIA GeForce RTX 3060"));

    check_generation_caps("NVIDIA GeForce GTX 960", true, false, false);   /* Maxwell: hevc 8-bit only */
    check_generation_caps("NVIDIA GeForce GTX 1060 6GB", true, true, false); /* Pascal: + hevc 10-bit */
    check_generation_caps("NVIDIA GeForce RTX 2070", true, true, false);    /* Turing: no av1 */
    check_generation_caps("NVIDIA GeForce RTX 3060 Ti", true, true, true);  /* Ampere: + av1 */
    check_generation_caps("NVIDIA GeForce RTX 4080", true, true, true);
    check_generation_caps("NVIDIA GeForce RTX 5090", true, true, true);
    /* UNKNOWN generation probes everything. */
    const gsr_nvenc_generation_caps *unknown_caps = gsr_nvenc_get_generation_caps(GSR_NVENC_GEN_UNKNOWN);
    CHECK(unknown_caps->h264 && unknown_caps->hevc && unknown_caps->hevc_10bit && unknown_caps->av1 && unknown_caps->av1_10bit);

    /* 2. The live probe must be honest about the actual machine. */
    printf("-- live probe\n");
    gsr_supported_video_codecs codecs;
    const bool query_result = gsr_get_supported_video_codecs_nvenc(&codecs, true);
    char adapter[256] = {0};
    const bool have_adapter = gsr_nvenc_get_adapter_description(adapter, sizeof(adapter));
    printf("nvenc: adapter '%s', query returned %d (h264=%d hevc=%d av1=%d)\n",
        have_adapter ? adapter : "(none)", query_result ? 1 : 0,
        codecs.h264.supported ? 1 : 0, codecs.hevc.supported ? 1 : 0, codecs.av1.supported ? 1 : 0);
    if(have_adapter && gsr_nvenc_description_is_nvidia(adapter)) {
        /* On a real NVIDIA machine the probe MUST report h264 supported. */
        CHECK(query_result && codecs.h264.supported);
    } else {
        /* On any other machine it must report nothing (the honest fallback
           that drives -fallback-cpu-encoding). */
        CHECK(!query_result || !codecs.h264.supported);
        CHECK(!codecs.hevc.supported && !codecs.hevc_10bit.supported && !codecs.av1.supported && !codecs.av1_10bit.supported);
    }

    /* 3. The -encoder gpu fallback recording. */
    printf("-- fallback recording\n");
    const int fallback_result = run_fallback_recording();
    if(fallback_result != 0)
        return fallback_result;

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    if(num_failures > 0) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
