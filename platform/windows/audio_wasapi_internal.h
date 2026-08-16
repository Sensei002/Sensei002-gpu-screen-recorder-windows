/* platform/windows/audio_wasapi_internal.h — internal test seam for the
 * WASAPI audio backend (Phase 8).
 *
 * The mix-format -> requested-format conversion pipeline in audio_wasapi.c
 * is pure math (no COM, no endpoints): decode the mix format samples, mix
 * down to stereo, resample to 48 kHz, quantize to the requested codec
 * format. The CI runner has no audio endpoints at all (headless GitHub
 * runner — get_pulseaudio_inputs reports 0 devices), so the live WASAPI
 * capture path cannot be exercised there. These declarations let
 * tests/audio-conv-test drive the conversion with synthetic data, the same
 * pure-logic pattern the DXGI backend uses for its rotation math.
 *
 * The rest of audio_wasapi.c (the WASAPI objects, ring buffer, threads,
 * device listing) stays internal; only the conversion functions below are
 * exported, and only for the test binary.
 */
#ifndef GSR_AUDIO_WASAPI_INTERNAL_H
#define GSR_AUDIO_WASAPI_INTERNAL_H

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../../upstream/include/sound.h"

typedef struct {
    bool is_float;
    int sample_bytes;      /* container size per sample (2, 3 or 4)         */
    int bits;              /* wBitsPerSample (valid data bits)              */
    int num_channels;
    DWORD sample_rate;
} mix_format_info;

typedef struct {
    /* capture parameters (copied from the sound_device_get_by_name args) */
    unsigned int num_channels;        /* always 2 (stereo)                  */
    unsigned int period_frame_size;   /* frames per read_next_chunk chunk   */
    gsr_audio_format audio_format;    /* chunk format (S16/S32/F32)         */
    size_t frame_bytes;               /* bytes per interleaved frame        */

    /* WASAPI objects (created on the calling thread, used by the capture
       thread; COM is initialized per-thread) */
    IAudioClient *audio_client;
    IAudioCaptureClient *capture_client;

    /* capture thread */
    HANDLE thread;
    volatile LONG stop_requested;
    bool thread_created;

    /* ring buffer (frames stored in the requested audio_format) */
    uint8_t *ring;
    size_t ring_capacity_frames;
    size_t ring_head_frames;
    size_t ring_count_frames;
    SRWLOCK ring_lock;
    CONDITION_VARIABLE ring_cond;

    /* reusable read buffer (the engine keeps the pointer we hand back and
       never frees it, like upstream's ringbuffer read pointer) */
    uint8_t *read_buffer;

    /* mix-format conversion state (parsed at open; used by the capture
       thread) */
    mix_format_info mix_info;
    WAVEFORMATEX *mix_format;         /* freed in close                     */
    double resample_pos;              /* fractional input-frame position    */
} wasapi_sound_device;

/* Parse a WAVEFORMATEX (or WAVEFORMATEXTENSIBLE) into the fields the
   conversion needs. Returns false for unsupported formats (A-law etc.). */
bool mix_format_info_get(const WAVEFORMATEX *format, mix_format_info *info);

/* Decode one sample from the mix format to float in [-1, 1]. */
float decode_sample(const mix_format_info *info, const uint8_t *data, size_t sample_index);

/* Mix n_channels interleaved float samples down to stereo. */
void downmix_to_stereo(const float *in, int num_channels, float *out_l, float *out_r, size_t num_frames);

/* Encode F32 stereo frames into the requested format (interleaved bytes).
   Returns a malloc'd buffer of num_frames * frame_bytes. */
uint8_t *encode_stereo(const float *l, const float *r, size_t num_frames, gsr_audio_format format, size_t frame_bytes);

/* Convert one mix-format WASAPI chunk into the ring buffer (requested
   format). Returns the number of frames pushed. Only the conversion fields
   of |self| are used (mix_info, resample_pos, audio_format, frame_bytes,
   ring*), so a test can drive it without any COM. */
size_t convert_chunk_to_ring(wasapi_sound_device *self, const BYTE *data, UINT32 num_frames);

#endif /* GSR_AUDIO_WASAPI_INTERNAL_H */
