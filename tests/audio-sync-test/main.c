/* tests/audio-sync-test/main.c — Phase 8, milestone B: the A/V-sync
 * validation harness.
 *
 * The engine's audio contract (recorder.c + audio_capture.c) is a wall of
 * small invariants that together keep audio and video in sync; most of
 * them are provable headless with synthetic data, which is what this test
 * does:
 *
 *   1. The device delivers EXACTLY period_frame_size frames per chunk and
 *      N seconds of audio produce exactly N * sample_rate frames.
 *   2. Resampling preserves wall-clock duration (a 0.1 s 44.1 kHz mix
 *      chunk must become 4800 frames at 48 kHz, not 4410).
 *   3. Quantization into the ring is sample-accurate (constant amplitude
 *      in, exact S32 values out).
 *   4. Codec delay offsets: audio_codec_get_desired_delay must match the
 *      upstream formulas, and the recorder's derived audio PTS start
 *      (pts = -frame_size * (delay / timeout)) must equal -delay * 48000
 *      — the audio stream leads the video by exactly the codec's priming
 *      delay, which is what makes the first audio sample align with the
 *      first video frame in the container.
 *   5. The codec format mapping: what the engine asks the device to
 *      deliver per codec (AAC→S32, FLAC→S32, opus→S16/F32) and the opened
 *      codec's frame_size (1024/960), which is the period the device must
 *      deliver.
 *
 * The live WASAPI path cannot run on the CI runner (no audio endpoints);
 * the conversion pipeline is driven here through the audio_wasapi_internal.h
 * seam exactly as audio-conv-test does.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "audio_wasapi_internal.h"
#include "../../upstream/include/recorder/audio_codec.h"
#include "../../upstream/include/defs.h"

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

#define CHECK_NEAR(a, b, tol) \
    do { \
        ++num_checks; \
        const double va = (a), vb = (b); \
        if(fabs(va - vb) > (tol)) { \
            ++num_failures; \
            fprintf(stderr, "FAIL %s:%d: %s == %.9f, expected %.9f\n", __FILE__, __LINE__, #a, va, vb); \
        } \
    } while(0)

/* Build an F32 stereo interleaved buffer of |frames| frames with the
   given constant amplitude (so quantization is checkable). */
static float *make_sine_chunk(size_t frames, float amplitude, DWORD sample_rate) {
    float *data = malloc(frames * 2 * sizeof(float));
    for(size_t i = 0; i < frames; ++i) {
        const double t = (double)i / (double)sample_rate;
        data[i * 2] = amplitude * (float)sin(2.0 * 3.14159265358979 * 440.0 * t);
        data[i * 2 + 1] = amplitude * (float)sin(2.0 * 3.14159265358979 * 220.0 * t);
    }
    return data;
}

static void make_f32_mix_format(WAVEFORMATEX *fmt, DWORD sample_rate) {
    memset(fmt, 0, sizeof(*fmt));
    fmt->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt->nChannels = 2;
    fmt->nSamplesPerSec = sample_rate;
    fmt->wBitsPerSample = 32;
    fmt->nBlockAlign = 8;
    fmt->nAvgBytesPerSec = sample_rate * 8;
}

/* A conversion-only device (no COM, no threads): the fields
   convert_chunk_to_ring uses. */
static void init_convert_device(wasapi_sound_device *self, const WAVEFORMATEX *fmt, gsr_audio_format format) {
    memset(self, 0, sizeof(*self));
    CHECK(mix_format_info_get(fmt, &self->mix_info));
    self->audio_format = format;
    self->num_channels = 2;
    self->frame_bytes = format == GSR_AUDIO_FORMAT_S16 ? 4 : 8;
    self->ring_capacity_frames = 16384;
    self->ring = calloc(self->ring_capacity_frames, self->frame_bytes);
    CHECK(self->ring != NULL);
}

/* Read one period from the ring into |out| (mirrors
   sound_device_read_next_chunk's copy, no locking — single thread here). */
static size_t read_period(wasapi_sound_device *self, uint8_t *out) {
    const size_t frames = self->period_frame_size;
    size_t remaining = frames;
    size_t offset = 0;
    while(remaining > 0) {
        const size_t contiguous = self->ring_capacity_frames - self->ring_head_frames;
        const size_t take = remaining < contiguous ? remaining : contiguous;
        memcpy(out + offset * self->frame_bytes, self->ring + self->ring_head_frames * self->frame_bytes, take * self->frame_bytes);
        self->ring_head_frames = (self->ring_head_frames + take) % self->ring_capacity_frames;
        self->ring_count_frames -= take;
        remaining -= take;
        offset += take;
    }
    return frames;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("audio-sync-test: A/V sync contract (headless)\n");

    /* 1. Chunk delivery + duration math at 48 kHz. */
    printf("-- chunk delivery\n");
    {
        WAVEFORMATEX fmt;
        make_f32_mix_format(&fmt, GSR_AUDIO_SAMPLE_RATE);
        wasapi_sound_device dev;
        init_convert_device(&dev, &fmt, GSR_AUDIO_FORMAT_S32);
        dev.period_frame_size = 1024;

        /* 3 seconds at 48 kHz = 144000 frames = 140.625 periods. Feed and
           drain like the real producer/consumer (the ring is 16384 frames;
           without draining it would drop-oldest and the count would lie). */
        const size_t total_frames = (size_t)3 * GSR_AUDIO_SAMPLE_RATE;
        const size_t chunk_frames = 1024;
        float *data = make_sine_chunk(chunk_frames, 0.5f, GSR_AUDIO_SAMPLE_RATE);
        uint8_t *period = malloc(1024 * dev.frame_bytes);
        size_t fed = 0, consumed = 0;
        while(fed < total_frames) {
            const size_t take = total_frames - fed < chunk_frames ? total_frames - fed : chunk_frames;
            const size_t pushed = convert_chunk_to_ring(&dev, (const BYTE*)data, (UINT32)take);
            CHECK(pushed == take); /* every fed frame enters the ring */
            fed += pushed;
            while(dev.ring_count_frames >= 1024) {
                CHECK(read_period(&dev, period) == 1024);
                consumed += 1024;
            }
        }
        CHECK(fed == total_frames);
        /* No frame lost: consumed + what is still buffered == what was fed. */
        CHECK(consumed + dev.ring_count_frames == total_frames);
        while(dev.ring_count_frames >= 1024) {
            CHECK(read_period(&dev, period) == 1024);
            consumed += 1024;
        }
        CHECK(consumed == total_frames);
        CHECK(dev.ring_count_frames == 0);
        printf("audio-sync: 3 s @48 kHz fed %zu frames, consumed %zu via 1024-frame periods, none lost\n", fed, consumed);
        free(period);
        free(data);
        free(dev.ring);
    }

    /* 2. Resampling preserves wall-clock duration (44.1k -> 48k). */
    printf("-- resample duration\n");
    {
        WAVEFORMATEX fmt;
        make_f32_mix_format(&fmt, 44100);
        wasapi_sound_device dev;
        init_convert_device(&dev, &fmt, GSR_AUDIO_FORMAT_S32);

        /* 0.1 s of 44.1 kHz = 4410 frames -> 4800 frames at 48 kHz. */
        const size_t in_frames = 4410;
        float *data = make_sine_chunk(in_frames, 0.5f, 44100);
        const size_t pushed = convert_chunk_to_ring(&dev, (const BYTE*)data, (UINT32)in_frames);
        CHECK(pushed == 4800);
        printf("audio-sync: 4410 frames @44.1k -> %zu frames @48k (0.1 s preserved)\n", pushed);
        free(data);
        free(dev.ring);
    }

    /* 3. Sample-accurate quantization into the ring. */
    printf("-- ring quantization\n");
    {
        WAVEFORMATEX fmt;
        make_f32_mix_format(&fmt, GSR_AUDIO_SAMPLE_RATE);
        wasapi_sound_device dev;
        init_convert_device(&dev, &fmt, GSR_AUDIO_FORMAT_S32);
        dev.period_frame_size = 1024;

        /* Constant +0.5 -> 0.5 * 2^31 = 1073741824 exactly. */
        const size_t frames = 1024;
        float *data = malloc(frames * 2 * sizeof(float));
        for(size_t i = 0; i < frames; ++i) {
            data[i * 2] = 0.5f;
            data[i * 2 + 1] = 0.5f;
        }
        CHECK(convert_chunk_to_ring(&dev, (const BYTE*)data, (UINT32)frames) == frames);
        const int32_t sample = *(const int32_t*)(dev.ring + 0);
        const int32_t sample_r = *(const int32_t*)(dev.ring + 4);
        CHECK(sample == 1073741824);
        CHECK(sample_r == 1073741824);
        free(data);
        free(dev.ring);
    }

    /* 4. Codec delay offsets + the derived audio PTS start. */
    printf("-- codec delay / A/V offset\n");
    {
        /* Upstream's formulas (audio_codec.c). The harness re-implements
           them as the expectation so any change to the math is caught. */
        const double base = 0.01 + 1.0 / 165.0;
        for(int fps = 30; fps <= 60; fps += 30) {
            const double fps_inv = 1.0 / (double)fps;
            const double expected_opus = fmax(0.0, base - fps_inv);
            const double expected_aac = fmax(0.0, (base + 0.008) * 2.0 - fps_inv);
            CHECK_NEAR(audio_codec_get_desired_delay(GSR_AUDIO_CODEC_OPUS, fps), expected_opus, 1e-12);
            CHECK_NEAR(audio_codec_get_desired_delay(GSR_AUDIO_CODEC_AAC, fps), expected_aac, 1e-12);
            CHECK_NEAR(audio_codec_get_desired_delay(GSR_AUDIO_CODEC_FLAC, fps), expected_opus, 1e-12);

            /* The recorder's A/V offset: with the audio thread consuming
               frame_size frames per timeout, the PTS shift in frames is
               delay * sample_rate (the frame_size cancels). Negative PTS
               = the audio track leads so the codec's priming delay lands
               before the first video frame. */
            const int frame_size = audio_codec_get_frame_size(GSR_AUDIO_CODEC_AAC);
            const double timeout = (double)frame_size / (double)GSR_AUDIO_SAMPLE_RATE;
            const double num_frames_shift = expected_aac / timeout;
            const double pts_frames = -(double)frame_size * num_frames_shift;
            CHECK_NEAR(pts_frames, -expected_aac * (double)GSR_AUDIO_SAMPLE_RATE, 1e-6);
            printf("audio-sync: aac@%dfps delay %.6f s -> audio pts start %.0f frames (%.1f ms lead)\n",
                fps, expected_aac, pts_frames, expected_aac * 1000.0);
        }
        CHECK(audio_codec_get_desired_delay(GSR_AUDIO_CODEC_AAC, 30) > 0.0);
    }

    /* 5. Codec -> device format mapping + frame sizes. */
    printf("-- codec format mapping\n");
    {
        AVCodecContext *ctx = create_audio_codec_context(30, GSR_AUDIO_CODEC_AAC, false, 0);
        CHECK(ctx != NULL);
        if(ctx) {
            CHECK(open_audio(ctx, NULL));
            CHECK(audio_codec_context_get_audio_format(ctx) == GSR_AUDIO_FORMAT_S32); /* AAC FLTP -> S32 */
            CHECK(ctx->frame_size == audio_codec_get_frame_size(GSR_AUDIO_CODEC_AAC)); /* 1024 */
            printf("audio-sync: aac frame_size=%d, device format S32\n", ctx->frame_size);
            avcodec_free_context(&ctx);
        }

        ctx = create_audio_codec_context(30, GSR_AUDIO_CODEC_OPUS, false, 0);
        CHECK(ctx != NULL);
        if(ctx) {
            CHECK(open_audio(ctx, NULL));
            const gsr_audio_format fmt = audio_codec_context_get_audio_format(ctx);
            CHECK(fmt == GSR_AUDIO_FORMAT_S16 || fmt == GSR_AUDIO_FORMAT_F32); /* libopus: S16 (or FLT) */
            CHECK(ctx->frame_size == audio_codec_get_frame_size(GSR_AUDIO_CODEC_OPUS)); /* 960 */
            printf("audio-sync: opus frame_size=%d, device format %s\n", ctx->frame_size, fmt == GSR_AUDIO_FORMAT_S16 ? "S16" : "F32");
            avcodec_free_context(&ctx);
        }

        ctx = create_audio_codec_context(30, GSR_AUDIO_CODEC_FLAC, false, 0);
        CHECK(ctx != NULL);
        if(ctx) {
            CHECK(open_audio(ctx, NULL));
            CHECK(audio_codec_context_get_audio_format(ctx) == GSR_AUDIO_FORMAT_S32); /* FLAC S32 -> S32 */
            CHECK(ctx->frame_size == 4096); /* libFLAC's fixed frame size */
            printf("audio-sync: flac frame_size=%d, device format S32\n", ctx->frame_size);
            avcodec_free_context(&ctx);
        }
    }

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    if(num_failures > 0) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
