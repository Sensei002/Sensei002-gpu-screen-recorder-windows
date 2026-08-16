/* tests/audio-conv-test/main.c — Phase 8, headless tests of the WASAPI
 * audio conversion pipeline (platform/windows/audio_wasapi.c).
 *
 * The CI runner has no audio endpoints at all (headless GitHub runner:
 * get_pulseaudio_inputs reports 0 devices), so the live WASAPI capture
 * path cannot run there. What CAN run is the pure conversion math — the
 * part most likely to be wrong — driven with synthetic mix-format data:
 *   - mix_format_info_get() parsing of WAVEFORMATEX/WAVEFORMATEXTENSIBLE
 *   - decode_sample() for 16/24/32-bit PCM and F32
 *   - downmix_to_stereo() for mono and surround
 *   - encode_stereo() quantization to S16/S32/F32
 *   - convert_chunk_to_ring() end-to-end (decode -> downmix -> resample ->
 *     quantize -> ring), including the 44.1 kHz -> 48 kHz linear resample
 *     and ring overflow/drop-oldest behavior.
 *
 * This is the same pure-logic pattern as the DXGI backend's rotation
 * tests (dxgi-self-test): the live path is exercised by recorder-self-test
 * on machines that have audio; the math is proven everywhere.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "audio_wasapi_internal.h"

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

/* Little-endian float/int helpers for byte-level checks. */
static float float_from_bytes(const uint8_t *b) {
    float v;
    memcpy(&v, b, 4);
    return v;
}

static int16_t s16_from_bytes(const uint8_t *b) {
    int16_t v;
    memcpy(&v, b, 2);
    return v;
}

static int32_t s32_from_bytes(const uint8_t *b) {
    int32_t v;
    memcpy(&v, b, 4);
    return v;
}

static void test_mix_format_info_get(void) {
    printf("-- mix_format_info_get\n");

    /* Plain F32/48k/stereo WAVEFORMATEX (the common Windows 10+ mix). */
    WAVEFORMATEX f32;
    memset(&f32, 0, sizeof(f32));
    f32.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    f32.nChannels = 2;
    f32.nSamplesPerSec = 48000;
    f32.wBitsPerSample = 32;
    f32.nBlockAlign = 8;
    mix_format_info info;
    CHECK(mix_format_info_get(&f32, &info));
    CHECK(info.is_float);
    CHECK(info.sample_bytes == 4);
    CHECK(info.bits == 32);
    CHECK(info.num_channels == 2);
    CHECK(info.sample_rate == 48000);

    /* WAVEFORMATEXTENSIBLE: 16-bit PCM, mono, 44.1 kHz. */
    WAVEFORMATEXTENSIBLE ext;
    memset(&ext, 0, sizeof(ext));
    ext.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext.Format.nChannels = 1;
    ext.Format.nSamplesPerSec = 44100;
    ext.Format.wBitsPerSample = 16;
    ext.Format.nBlockAlign = 2;
    ext.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    /* KSDATAFORMAT_SUBTYPE_PCM */
    ext.SubFormat = (GUID){0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    CHECK(mix_format_info_get(&ext.Format, &info));
    CHECK(!info.is_float);
    CHECK(info.sample_bytes == 2);
    CHECK(info.bits == 16);
    CHECK(info.num_channels == 1);
    CHECK(info.sample_rate == 44100);

    /* A-law (tag 6): unsupported. */
    WAVEFORMATEX alaw;
    memset(&alaw, 0, sizeof(alaw));
    alaw.wFormatTag = 6; /* WAVE_FORMAT_ALAW */
    alaw.nChannels = 2;
    alaw.nSamplesPerSec = 8000;
    alaw.nBlockAlign = 2;
    CHECK(!mix_format_info_get(&alaw, &info));
}

static void test_decode_sample(void) {
    printf("-- decode_sample\n");

    mix_format_info info;
    memset(&info, 0, sizeof(info));

    /* S16: 16384 / 32768 = 0.5 */
    info.is_float = false; info.sample_bytes = 2; info.bits = 16;
    const uint8_t s16_pos[] = {0x00, 0x40};
    const uint8_t s16_neg[] = {0x00, 0xC0}; /* -16384 */
    CHECK(decode_sample(&info, s16_pos, 0) > 0.499f && decode_sample(&info, s16_pos, 0) < 0.501f);
    CHECK(decode_sample(&info, s16_neg, 0) > -0.501f && decode_sample(&info, s16_neg, 0) < -0.499f);

    /* S24 (3-byte container): 0x400000 / 0x800000 = 0.5 */
    info.is_float = false; info.sample_bytes = 3; info.bits = 24;
    const uint8_t s24_pos[] = {0x00, 0x00, 0x40};
    const uint8_t s24_neg[] = {0x00, 0x00, 0xC0};
    CHECK(decode_sample(&info, s24_pos, 0) > 0.499f && decode_sample(&info, s24_pos, 0) < 0.501f);
    CHECK(decode_sample(&info, s24_neg, 0) > -0.501f && decode_sample(&info, s24_neg, 0) < -0.499f);

    /* S24 in a 32-bit container: 0x7FFFFF00 >> 8 = 0x7FFFFF / 0x800000 */
    info.is_float = false; info.sample_bytes = 4; info.bits = 24;
    const uint8_t s24in32[] = {0x00, 0xFF, 0xFF, 0x7F};
    const float v24 = decode_sample(&info, s24in32, 0);
    CHECK(v24 > 0.999f && v24 <= 1.0001f);

    /* S32: 0x40000000 / 0x80000000 = 0.5 */
    info.is_float = false; info.sample_bytes = 4; info.bits = 32;
    const uint8_t s32_half[] = {0x00, 0x00, 0x00, 0x40};
    CHECK(decode_sample(&info, s32_half, 0) > 0.499f && decode_sample(&info, s32_half, 0) < 0.501f);

    /* F32: 1.0 */
    info.is_float = true; info.sample_bytes = 4; info.bits = 32;
    const uint8_t f32_one[] = {0x00, 0x00, 0x80, 0x3F};
    CHECK(decode_sample(&info, f32_one, 0) > 0.999f && decode_sample(&info, f32_one, 0) <= 1.0001f);
}

static void test_downmix_to_stereo(void) {
    printf("-- downmix_to_stereo\n");

    /* Mono: both channels equal the input. */
    const float mono_in[] = {0.25f, -0.5f};
    float mono_l[2], mono_r[2];
    downmix_to_stereo(mono_in, 1, mono_l, mono_r, 2);
    CHECK(mono_l[0] == 0.25f && mono_r[0] == 0.25f);
    CHECK(mono_l[1] == -0.5f && mono_r[1] == -0.5f);

    /* 5.1: FL=1, FC=0.5, BL=0.5 -> L = 1 + 0.25 + 0.25 = 1.5 */
    const float s51_in[] = {1.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f};
    float l, r;
    downmix_to_stereo(s51_in, 6, &l, &r, 1);
    CHECK(l > 1.499f && l < 1.501f);
    CHECK(r > -0.001f && r < 0.001f);
}

static void test_encode_stereo(void) {
    printf("-- encode_stereo\n");

    const float l[2] = {0.5f, -0.25f};
    const float r[2] = {-0.5f, 0.25f};

    /* F32: raw float bytes. */
    uint8_t *out = encode_stereo(l, r, 2, GSR_AUDIO_FORMAT_F32, 8);
    CHECK(out != NULL);
    CHECK(float_from_bytes(out) == 0.5f);
    CHECK(float_from_bytes(out + 4) == -0.5f);
    CHECK(float_from_bytes(out + 8) == -0.25f);
    free(out);

    /* S16: 0.5 * 32767 = 16383 (0x3FFF); -0.5 -> -16383 (0xC001). */
    out = encode_stereo(l, r, 2, GSR_AUDIO_FORMAT_S16, 4);
    CHECK(out != NULL);
    CHECK(s16_from_bytes(out) == 16383);
    CHECK(s16_from_bytes(out + 2) == -16383);
    CHECK(s16_from_bytes(out + 4) == -8191); /* -0.25 * 32767 = -8191.75 -> -8191 */
    free(out);

    /* S32: 0.5 * 2147483647 = 1073741823 (0x3FFFFFFF). */
    out = encode_stereo(l, r, 1, GSR_AUDIO_FORMAT_S32, 8);
    CHECK(out != NULL);
    CHECK(s32_from_bytes(out) == 1073741823);
    free(out);
}

/* Build a fake device (no COM) around a given mix format and feed one
   chunk through the whole conversion pipeline. */
static void convert_test_run(wasapi_sound_device *dev, const mix_format_info *mix, const float *stereo_input, size_t num_frames) {
    memset(dev, 0, sizeof(*dev));
    dev->mix_info = *mix;
    dev->audio_format = GSR_AUDIO_FORMAT_S16;
    dev->frame_bytes = 4; /* S16 stereo */
    dev->ring_capacity_frames = 4096;
    dev->ring = malloc(dev->ring_capacity_frames * dev->frame_bytes);
    memset(dev->ring, 0xAB, dev->ring_capacity_frames * dev->frame_bytes);

    /* The pipeline expects mix-format bytes; for F32 stereo that is just
       the input floats. */
    uint8_t *chunk = malloc(num_frames * 8);
    for(size_t i = 0; i < num_frames; ++i) {
        memcpy(chunk + i * 8, &stereo_input[i], 4);
        memcpy(chunk + i * 8 + 4, &stereo_input[num_frames + i], 4);
    }
    convert_chunk_to_ring(dev, chunk, (UINT32)num_frames);
    free(chunk);
}

static void test_convert_passthrough(void) {
    printf("-- convert_chunk_to_ring (F32 48k stereo -> S16, passthrough)\n");

    mix_format_info mix;
    memset(&mix, 0, sizeof(mix));
    mix.is_float = true; mix.sample_bytes = 4; mix.bits = 32; mix.num_channels = 2; mix.sample_rate = 48000;

    const size_t n = 1024;
    float *input = malloc(n * 2 * sizeof(float));
    for(size_t i = 0; i < n; ++i) {
        input[i] = 0.25f;
        input[n + i] = -0.25f;
    }

    wasapi_sound_device dev;
    convert_test_run(&dev, &mix, input, n);
    free(input);

    CHECK(dev.ring_count_frames == n);
    CHECK(dev.ring_head_frames == 0);
    /* First frame: 0.25 -> 8191 (0x1FFF), -0.25 -> -8191 (0xE001). */
    CHECK(s16_from_bytes(dev.ring + 0) == 8191);
    CHECK(s16_from_bytes(dev.ring + 2) == -8191);
    /* Last frame: same. */
    CHECK(s16_from_bytes(dev.ring + (n - 1) * 4) == 8191);
    CHECK(s16_from_bytes(dev.ring + (n - 1) * 4 + 2) == -8191);
    free(dev.ring);
}

static void test_convert_resample(void) {
    printf("-- convert_chunk_to_ring (44.1k -> 48k linear resample)\n");

    mix_format_info mix;
    memset(&mix, 0, sizeof(mix));
    mix.is_float = true; mix.sample_bytes = 4; mix.bits = 32; mix.num_channels = 2; mix.sample_rate = 44100;

    /* 441 input frames at 44.1 kHz == 480 frames at 48 kHz (10 ms). */
    const size_t n = 441;
    float *input = malloc(n * 2 * sizeof(float));
    for(size_t i = 0; i < n; ++i) {
        input[i] = 0.5f;
        input[n + i] = -0.5f;
    }

    wasapi_sound_device dev;
    convert_test_run(&dev, &mix, input, n);
    free(input);

    CHECK(dev.ring_count_frames == 480);
    /* Constant 0.5 stays 0.5 through linear interpolation: 16383 or 16384. */
    const int16_t v = s16_from_bytes(dev.ring + 0);
    CHECK(v == 16383 || v == 16384);
    const int16_t vr = s16_from_bytes(dev.ring + 2);
    CHECK(vr == -16383 || vr == -16384);
    /* The resampler fractional position advanced (0.25 for 44.1 -> 48). */
    CHECK(dev.resample_pos > 0.001);
    free(dev.ring);
}

static void test_ring_overflow(void) {
    printf("-- ring overflow (drop oldest)\n");

    mix_format_info mix;
    memset(&mix, 0, sizeof(mix));
    mix.is_float = true; mix.sample_bytes = 4; mix.bits = 32; mix.num_channels = 2; mix.sample_rate = 48000;

    const size_t n = 32;
    float *input = malloc(n * 2 * sizeof(float));
    for(size_t i = 0; i < n; ++i) {
        input[i] = 0.125f;  /* 0.125 * 32767 = 4095 */
        input[n + i] = -0.125f;
    }

    wasapi_sound_device dev;
    memset(&dev, 0, sizeof(dev));
    dev.mix_info = mix;
    dev.audio_format = GSR_AUDIO_FORMAT_S16;
    dev.frame_bytes = 4;
    dev.ring_capacity_frames = 16; /* smaller than the chunk: overflow */
    dev.ring = malloc(dev.ring_capacity_frames * dev.frame_bytes);
    memset(dev.ring, 0, dev.ring_capacity_frames * dev.frame_bytes);

    uint8_t *chunk = malloc(n * 8);
    for(size_t i = 0; i < n; ++i) {
        memcpy(chunk + i * 8, &input[i], 4);
        memcpy(chunk + i * 8 + 4, &input[n + i], 4);
    }
    convert_chunk_to_ring(&dev, chunk, (UINT32)n);
    free(chunk);
    free(input);

    /* Capacity was 16: the oldest 16 frames were dropped, the newest 16
       remain. Ring head wrapped back to 0, count = 16. */
    CHECK(dev.ring_count_frames == 16);
    CHECK(dev.ring_head_frames == 0);
    /* The retained frames are the LAST 16 input frames (values 0.125). */
    CHECK(s16_from_bytes(dev.ring + 0) == 4095);
    CHECK(s16_from_bytes(dev.ring + (15) * 4 + 2) == -4095);
    free(dev.ring);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("audio-conv-test: WASAPI conversion pipeline (headless)\\n");

    test_mix_format_info_get();
    test_decode_sample();
    test_downmix_to_stereo();
    test_encode_stereo();
    test_convert_passthrough();
    test_convert_resample();
    test_ring_overflow();

    printf("\\n%d checks, %d failures\\n", num_checks, num_failures);
    if(num_failures > 0) {
        fprintf(stderr, "FAIL\\n");
        return 1;
    }
    printf("PASS\\n");
    return 0;
}
