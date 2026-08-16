/* platform/windows/audio_wasapi.c — the upstream sound_device API over
 * WASAPI (Phase 8). Replaces upstream/src/sound.c (PulseAudio/PipeWire),
 * which is not built on Windows.
 *
 * The upstream contract (upstream/include/sound.h, consumed by
 * recorder/audio_capture.c) is:
 *
 *   - sound_device_get_by_name() opens an endpoint and returns it in
 *     SoundDevice. The device must deliver exactly the requested
 *     sample rate (GSR_AUDIO_SAMPLE_RATE = 48000), channel count (2,
 *     stereo) and gsr_audio_format (S16/S32/F32 — the codec decides:
 *     AAC and flac map to S32, opus to F32/S16). Each call to
 *     sound_device_read_next_chunk() must return period_frame_size
 *     frames (the codec frame_size, typically 1024), because the
 *     engine's swr_convert consumes exactly that many and its A/V sync
 *     bookkeeping counts whole chunks.
 *   - sound_device_read_next_chunk() blocks up to timeout_sec for a
 *     full chunk and returns the frame count (or -1 on timeout). The
 *     timeout is load-bearing: when the recording stops, the engine's
 *     audio thread is blocked here and must wake up to see its
 *     running=false flag. (-1 = "no audio", the engine fills silence.)
 *   - sound_device_flush() discards captured audio (called right
 *     before recording starts so stale pre-recording audio is dropped).
 *   - "" device_name means "no device" (silent track): return success
 *     with a NULL handle. default_output/default_input map to the
 *     Windows default render/capture endpoints.
 *   - get_pulseaudio_inputs() fills the gsr_audio_devices list that the
 *     -a argument parser validates against (name = endpoint ID,
 *     description = friendly name). Keeps the upstream symbol name.
 *
 * Implementation notes:
 *   - Shared-mode WASAPI is opened with the endpoint's *mix format* and
 *     the data is converted in software to F32 stereo 48 kHz by a
 *     dedicated capture thread, then quantized to the requested format
 *     into a ring buffer. On modern Windows the mix format *is*
 *     F32/48 kHz/stereo, so the common path is a straight pass-through.
 *   - The conversion handles the rare cases (16/24/32-bit PCM, mono or
 *     multichannel mix formats, non-48 kHz mix rates with a linear
 *     resampler) so the backend works on any endpoint that WASAPI can
 *     open. The linear resampler is a deliberate quality trade-off for
 *     a path that essentially never triggers on Windows 10+.
 *   - Input (microphone) endpoints are captured directly; render
 *     endpoints use loopback mode (AUDCLNT_STREAMFLAGS_LOOPBACK), i.e.
 *     "what you hear", matching upstream's default_output semantics.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3i.
 */
#include "../../upstream/include/sound.h"
#include "../../upstream/include/recorder/audio_codec.h" /* GSR_AUDIO_SAMPLE_RATE */
#include "../../upstream/include/log.h"
#include "../../upstream/include/utils.h"

#include "audio_wasapi_internal.h" /* conversion pipeline (test seam) */

#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

/* KSDATAFORMAT_SUBTYPE_* are in ksmedia.h, which MinGW-w64 ships but which
   drags in kernel headers; define the two we need directly. */
static const GUID GSR_KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID GSR_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

/* mingw-w64 *declares* the MMDevice/audio-client IIDs and CLSID in its
   headers but no import library provides their definitions, so taking
   their address fails to link (same class of issue as the DXGI IIDs in
   Phase 4). Define them here with external linkage to satisfy the
   headers' extern declarations; only this TU references them, so there
   is no collision. Values are the canonical SDK GUIDs. */
const IID IID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4c32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID IID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48a0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xE4, 0x17}};
const CLSID CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};

#define GSR_RING_FRAMES_MIN 4096           /* never less than ~85 ms @48 kHz */
#define GSR_RING_PERIODS 32                /* ring capacity in periods       */

/* mingw-w64's mmdeviceapi.h defines DEVICE_STATE_ACTIVE/DISABLED/... but
   not the DEVICE_STATE_ALL mask. */
#ifndef DEVICE_STATE_ALL
#define DEVICE_STATE_ALL 0x0000000F
#endif

typedef enum {
    GSR_ENDPOINT_RENDER,   /* loopback capture ("what you hear")            */
    GSR_ENDPOINT_CAPTURE   /* normal capture (microphone)                   */
} gsr_endpoint_kind;

/* ---- small helpers ------------------------------------------------------ */

/* Initialize COM for this thread. Returns true when the thread was left in
   an initialized state; every true must be balanced with com_uninit().
   (S_OK and S_FALSE both increment the per-thread refcount; only
   RPC_E_CHANGED_MODE — thread already in the other apartment — fails.) */
static bool com_init(void) {
    return SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
}

static void com_uninit(void) {
    CoUninitialize();
}

static const char *endpoint_kind_name(gsr_endpoint_kind kind) {
    return kind == GSR_ENDPOINT_RENDER ? "render" : "capture";
}

/* ---- endpoint lookup ---------------------------------------------------- */

/* Resolve a -a device name to an IMMDevice. |name| is one of:
     "default_output" / "default_input" / an endpoint ID from the listing.
   Returns S_OK with *out_device set (caller releases). */
static HRESULT resolve_endpoint(const char *name, gsr_endpoint_kind *out_kind, IMMDevice **out_device) {
    IMMDeviceEnumerator *enumerator = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                  &IID_IMMDeviceEnumerator, (void**)&enumerator);
    if(FAILED(hr) || !enumerator)
        return hr;

    IMMDevice *device = NULL;
    if(strcmp(name, "default_output") == 0) {
        hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
        *out_kind = GSR_ENDPOINT_RENDER;
    } else if(strcmp(name, "default_input") == 0) {
        hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eCapture, eConsole, &device);
        *out_kind = GSR_ENDPOINT_CAPTURE;
    } else {
        /* Named device: match the endpoint ID (the listing's name field).
           Search render first (loopback), then capture. */
        for(int flow = 0; flow < 2 && !device; ++flow) {
            const EDataFlow data_flow = flow == 0 ? eRender : eCapture;
            IMMDeviceCollection *collection = NULL;
            hr = enumerator->lpVtbl->EnumAudioEndpoints(enumerator, data_flow, DEVICE_STATE_ACTIVE, &collection);
            if(FAILED(hr) || !collection)
                continue;

            UINT count = 0;
            collection->lpVtbl->GetCount(collection, &count);
            for(UINT i = 0; i < count && !device; ++i) {
                IMMDevice *candidate = NULL;
                collection->lpVtbl->Item(collection, i, &candidate);
                if(!candidate)
                    continue;
                LPWSTR id = NULL;
                if(SUCCEEDED(candidate->lpVtbl->GetId(candidate, &id)) && id) {
                    /* Device IDs are ASCII GUID strings; a narrow copy of
                       the wide string is sufficient for the comparison. */
                    char id_utf8[256] = {0};
                    size_t n = wcslen(id);
                    if(n < sizeof(id_utf8)) {
                        for(size_t j = 0; j < n; ++j)
                            id_utf8[j] = (char)id[j];
                        if(strcmp(id_utf8, name) == 0) {
                            device = candidate;
                            *out_kind = data_flow == eRender ? GSR_ENDPOINT_RENDER : GSR_ENDPOINT_CAPTURE;
                        }
                    }
                    CoTaskMemFree(id);
                }
                if(!device)
                    candidate->lpVtbl->Release(candidate);
            }
            if(collection)
                collection->lpVtbl->Release(collection);
        }
        hr = device ? S_OK : E_NOTFOUND;
    }

    enumerator->lpVtbl->Release(enumerator);
    *out_device = device;
    return hr;
}

/* ---- mix-format -> requested format conversion (capture thread) --------- */

bool mix_format_info_get(const WAVEFORMATEX *format, mix_format_info *info) {
    memset(info, 0, sizeof(*info));
    info->num_channels = format->nChannels;
    info->sample_rate = format->nSamplesPerSec;
    if(format->nBlockAlign == 0 || format->nChannels == 0)
        return false;
    info->sample_bytes = format->nBlockAlign / format->nChannels;
    info->bits = format->wBitsPerSample;

    if(format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        info->is_float = true;
        return true;
    } else if(format->wFormatTag == WAVE_FORMAT_PCM) {
        info->is_float = false;
        return true;
    } else if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE*)format;
        if(IsEqualGUID(&ext->SubFormat, &GSR_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            info->is_float = true;
            return true;
        } else if(IsEqualGUID(&ext->SubFormat, &GSR_KSDATAFORMAT_SUBTYPE_PCM)) {
            info->is_float = false;
            return true;
        }
    }
    return false; /* A-law/µ-law and other exotic formats: unsupported */
}

/* Decode one sample from the mix format to float. */
float decode_sample(const mix_format_info *info, const uint8_t *data, size_t sample_index) {
    const uint8_t *p = data + (size_t)info->sample_bytes * sample_index;
    if(info->is_float) {
        if(info->sample_bytes == 4) {
            float v;
            memcpy(&v, p, 4);
            return v;
        }
        return 0.0f;
    }
    if(info->sample_bytes == 2) {
        int16_t v;
        memcpy(&v, p, 2);
        return (float)v / 32768.0f;
    } else if(info->sample_bytes == 3) {
        /* 24-bit, sign-extended */
        int32_t v = (int32_t)(p[0] | (p[1] << 8) | ((int32_t)p[2] << 16));
        if(v & 0x800000)
            v |= ~0xFFFFFF;
        return (float)v / 8388608.0f;
    } else if(info->sample_bytes == 4) {
        if(info->bits == 24) {
            int32_t v;
            memcpy(&v, p, 4);
            v >>= 8; /* 24 bits in a 32-bit container */
            return (float)v / 8388608.0f;
        }
        int32_t v;
        memcpy(&v, p, 4);
        return (float)v / 2147483648.0f;
    }
    return 0.0f;
}

/* Mix n_channels interleaved float samples down to stereo (into out_l/out_r
   arrays of num_frames each). Simple, documented approximation for
   surround formats; the front L/R pair is the primary source. */
void downmix_to_stereo(const float *in, int num_channels, float *out_l, float *out_r, size_t num_frames) {
    for(size_t i = 0; i < num_frames; ++i) {
        const float *s = in + (size_t)num_channels * i;
        float l, r;
        switch(num_channels) {
            case 1:  l = r = s[0]; break;
            case 2:  l = s[0]; r = s[1]; break;
            case 4:  l = (s[0] + s[2]) * 0.5f; r = (s[1] + s[3]) * 0.5f; break;
            case 6:  l = s[0] + s[2] * 0.5f + s[4] * 0.5f; r = s[1] + s[2] * 0.5f + s[5] * 0.5f; break;
            case 8:  l = s[0] + s[2] * 0.5f + s[4] * 0.5f + s[6] * 0.5f; r = s[1] + s[2] * 0.5f + s[5] * 0.5f + s[7] * 0.5f; break;
            default: l = s[0]; r = num_channels > 1 ? s[1] : s[0]; break;
        }
        out_l[i] = l;
        out_r[i] = r;
    }
}

/* Encode F32 stereo frames into the requested format (interleaved bytes).
   Returns a malloc'd buffer of num_frames * frame_bytes. */
uint8_t *encode_stereo(const float *l, const float *r, size_t num_frames, gsr_audio_format format, size_t frame_bytes) {
    uint8_t *out = malloc(num_frames * frame_bytes);
    if(!out)
        return NULL;
    /* The two channels are interleaved at the start of each frame; the
       engine always requests stereo, so the right channel follows the left
       at sample size 2 or 4 bytes. */
    if(format == GSR_AUDIO_FORMAT_F32) {
        for(size_t i = 0; i < num_frames; ++i) {
            memcpy(out + i * frame_bytes, &l[i], 4);
            memcpy(out + i * frame_bytes + 4, &r[i], 4);
        }
        return out;
    }
    const bool s16 = format == GSR_AUDIO_FORMAT_S16;
    for(size_t i = 0; i < num_frames; ++i) {
        float vl = l[i] < -1.0f ? -1.0f : (l[i] > 1.0f ? 1.0f : l[i]);
        float vr = r[i] < -1.0f ? -1.0f : (r[i] > 1.0f ? 1.0f : r[i]);
        if(s16) {
            const int16_t sl = (int16_t)(vl * 32767.0f);
            const int16_t sr = (int16_t)(vr * 32767.0f);
            memcpy(out + i * frame_bytes, &sl, 2);
            memcpy(out + i * frame_bytes + 2, &sr, 2);
        } else {
            const int32_t sl = (int32_t)(vl * 2147483647.0f);
            const int32_t sr = (int32_t)(vr * 2147483647.0f);
            memcpy(out + i * frame_bytes, &sl, 4);
            memcpy(out + i * frame_bytes + 4, &sr, 4);
        }
    }
    return out;
}

/* Convert one WASAPI chunk into the ring buffer (requested format).
   Returns the number of frames pushed. Called with the ring lock held. */
size_t convert_chunk_to_ring(wasapi_sound_device *self, const BYTE *data, UINT32 num_frames) {
    const mix_format_info *info = &self->mix_info;
    const DWORD mix_rate = info->sample_rate;
    const double ratio = (double)GSR_AUDIO_SAMPLE_RATE / (double)mix_rate;

    /* Decode + downmix to F32 stereo. */
    float *stereo = malloc((num_frames + 1) * 2 * sizeof(float));
    if(!stereo)
        return 0;

    if(info->num_channels != 2) {
        float *mono_mix = malloc((size_t)num_frames * info->num_channels * sizeof(float));
        if(!mono_mix) {
            free(stereo);
            return 0;
        }
        for(size_t i = 0; i < (size_t)num_frames; ++i)
            for(int c = 0; c < info->num_channels; ++c)
                mono_mix[i * info->num_channels + c] = decode_sample(info, data, i * info->num_channels + c);
        downmix_to_stereo(mono_mix, info->num_channels, stereo, stereo + num_frames, num_frames);
        free(mono_mix);
    } else {
        for(size_t i = 0; i < (size_t)num_frames; ++i) {
            stereo[i] = decode_sample(info, data, i * 2);
            stereo[num_frames + i] = decode_sample(info, data, i * 2 + 1);
        }
    }

    /* Resample to 48 kHz when the mix rate differs (linear interpolation). */
    size_t out_frames = num_frames;
    float *resampled = NULL;
    if(mix_rate != GSR_AUDIO_SAMPLE_RATE) {
        out_frames = (size_t)((double)num_frames * ratio + 0.5);
        resampled = malloc((out_frames + 1) * 2 * sizeof(float));
        if(!resampled) {
            free(stereo);
            return 0;
        }
        double pos = self->resample_pos;
        for(size_t i = 0; i < out_frames; ++i) {
            size_t idx = (size_t)pos;
            if(idx >= num_frames)
                idx = num_frames - 1;
            const double frac = pos - (double)idx;
            const size_t idx2 = idx + 1 < num_frames ? idx + 1 : idx;
            resampled[i]              = (float)(stereo[idx]              * (1.0 - frac) + stereo[idx2]              * frac);
            resampled[out_frames + i] = (float)(stereo[num_frames + idx] * (1.0 - frac) + stereo[num_frames + idx2] * frac);
            pos += 1.0 / ratio;
        }
        /* Keep the fractional position in range to bound drift. */
        self->resample_pos = pos - (double)(size_t)(pos / (double)num_frames) * (double)num_frames;
        free(stereo);
        stereo = resampled;
    }

    /* Encode to the requested format and push into the ring (drop-oldest
       on overflow; the ring is sized well beyond what the consumer can
       fall behind by). */
    const size_t frame_bytes = self->frame_bytes;
    uint8_t *encoded = encode_stereo(stereo, stereo + out_frames, out_frames, self->audio_format, frame_bytes);
    free(stereo);
    if(!encoded)
        return 0;

    size_t pushed = 0;
    for(size_t i = 0; i < out_frames; ++i) {
        if(self->ring_count_frames >= self->ring_capacity_frames) {
            /* overflow: drop the oldest frame */
            self->ring_head_frames = (self->ring_head_frames + 1) % self->ring_capacity_frames;
            --self->ring_count_frames;
        }
        const size_t tail = (self->ring_head_frames + self->ring_count_frames) % self->ring_capacity_frames;
        memcpy(self->ring + tail * frame_bytes, encoded + i * frame_bytes, frame_bytes);
        ++self->ring_count_frames;
        ++pushed;
    }

    free(encoded);
    return pushed;
}

/* ---- WASAPI capture thread ---------------------------------------------- */

static DWORD WINAPI wasapi_capture_thread(LPVOID userdata) {
    wasapi_sound_device *self = (wasapi_sound_device*)userdata;
    const bool com_ok = com_init();

    while(!self->stop_requested) {
        UINT32 frames = 0;
        BYTE *data = NULL;
        DWORD flags = 0;
        HRESULT hr = self->capture_client->lpVtbl->GetBuffer(self->capture_client, &data, &frames, &flags, NULL, NULL);
        if(hr == AUDCLNT_S_BUFFER_EMPTY) {
            Sleep(5);
            continue;
        }
        if(FAILED(hr)) {
            /* e.g. AUDCLNT_E_DEVICE_INVALIDATED (device unplugged). Sleep
               and retry; the engine produces silence meanwhile. */
            Sleep(10);
            continue;
        }
        if(frames > 0 && data) {
            /* SILENT buffers contain garbage data; replace with zeroes so
               the silence is actually silent. */
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            uint8_t *clean_data = NULL;
            if(silent) {
                const size_t chunk_bytes = (size_t)frames * self->mix_format->nBlockAlign;
                clean_data = calloc(1, chunk_bytes);
                data = clean_data;
            }
            AcquireSRWLockExclusive(&self->ring_lock);
            convert_chunk_to_ring(self, data, frames);
            ReleaseSRWLockExclusive(&self->ring_lock);
            WakeAllConditionVariable(&self->ring_cond);
            free(clean_data);
        }
        self->capture_client->lpVtbl->ReleaseBuffer(self->capture_client, frames);
    }

    if(com_ok)
        com_uninit();
    return 0;
}

/* ---- sound_device API --------------------------------------------------- */

int sound_device_get_by_name(SoundDevice *device, const char *node_name, const char *device_name, const char *description, unsigned int num_channels, unsigned int period_frame_size, gsr_audio_format audio_format) {
    (void)node_name;
    (void)description;
    memset(device, 0, sizeof(*device));

    if(!device_name || device_name[0] == '\0') {
        /* "" = no device (upstream: used for application audio). The engine
           treats a NULL handle as a silent track. */
        device->handle = NULL;
        device->frames = 0;
        return 0;
    }

    if(period_frame_size == 0 || num_channels == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: invalid device parameters (channels=%u, period=%u)", num_channels, period_frame_size);
        return -1;
    }

    const bool com_ok = com_init();

    gsr_endpoint_kind kind = GSR_ENDPOINT_RENDER;
    IMMDevice *endpoint = NULL;
    HRESULT hr = resolve_endpoint(device_name, &kind, &endpoint);
    if(FAILED(hr) || !endpoint) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: could not find audio device \"%s\"", device_name);
        if(com_ok)
            com_uninit();
        return -1;
    }

    wasapi_sound_device *self = calloc(1, sizeof(wasapi_sound_device));
    if(!self) {
        endpoint->lpVtbl->Release(endpoint);
        if(com_ok)
            com_uninit();
        return -1;
    }
    self->num_channels = num_channels;
    self->period_frame_size = period_frame_size;
    self->audio_format = audio_format;
    switch(audio_format) {
        case GSR_AUDIO_FORMAT_S16: self->frame_bytes = 2 * num_channels; break;
        case GSR_AUDIO_FORMAT_S32:
        case GSR_AUDIO_FORMAT_F32: self->frame_bytes = 4 * num_channels; break;
    }

    /* Ring buffer: at least GSR_RING_FRAMES_MIN frames, and at least
       GSR_RING_PERIODS periods. */
    size_t ring_frames = (size_t)period_frame_size * GSR_RING_PERIODS;
    if(ring_frames < GSR_RING_FRAMES_MIN)
        ring_frames = GSR_RING_FRAMES_MIN;
    self->ring_capacity_frames = ring_frames;
    self->ring = calloc(ring_frames, self->frame_bytes);
    self->read_buffer = malloc((size_t)period_frame_size * self->frame_bytes);
    if(!self->ring || !self->read_buffer) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: out of memory opening \"%s\"", device_name);
        free(self->ring);
        free(self->read_buffer);
        free(self);
        endpoint->lpVtbl->Release(endpoint);
        if(com_ok)
            com_uninit();
        return -1;
    }
    InitializeSRWLock(&self->ring_lock);
    InitializeConditionVariable(&self->ring_cond);

    /* Open the audio client in shared mode with the endpoint's mix format.
       (The engine converts to the requested format in software — see the
       file header.) */
    hr = endpoint->lpVtbl->Activate(endpoint, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&self->audio_client);
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: Activate(IAudioClient) failed for \"%s\"", device_name);
        goto fail;
    }

    hr = self->audio_client->lpVtbl->GetMixFormat(self->audio_client, &self->mix_format);
    if(FAILED(hr) || !self->mix_format) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: GetMixFormat failed for \"%s\"", device_name);
        goto fail;
    }

    if(!mix_format_info_get(self->mix_format, &self->mix_info)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: unsupported mix format for \"%s\" (tag %u)", device_name, (unsigned)self->mix_format->wFormatTag);
        goto fail;
    }

    const DWORD stream_flags = kind == GSR_ENDPOINT_RENDER ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = self->audio_client->lpVtbl->Initialize(self->audio_client, AUDCLNT_SHAREMODE_SHARED, stream_flags, 0, 0, self->mix_format, NULL);
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: Initialize failed for \"%s\" (mix %u Hz, %u ch): 0x%08lx", device_name, (unsigned)self->mix_format->nSamplesPerSec, (unsigned)self->mix_format->nChannels, (unsigned long)hr);
        goto fail;
    }

    hr = self->audio_client->lpVtbl->GetService(self->audio_client, &IID_IAudioCaptureClient, (void**)&self->capture_client);
    if(FAILED(hr) || !self->capture_client) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: GetService(IAudioCaptureClient) failed for \"%s\"", device_name);
        goto fail;
    }

    hr = self->audio_client->lpVtbl->Start(self->audio_client);
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: Start failed for \"%s\"", device_name);
        goto fail;
    }

    self->thread = CreateThread(NULL, 0, wasapi_capture_thread, self, 0, NULL);
    if(!self->thread) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "wasapi: could not create capture thread for \"%s\"", device_name);
        goto fail;
    }
    self->thread_created = true;

    if(com_ok)
        com_uninit();

    device->handle = self;
    device->frames = period_frame_size;
    gsr_log(GSR_LOG_LEVEL_INFO, "wasapi: opened \"%s\" (%s, %u Hz, %u ch, mix %u Hz %u ch, format %d, period %u)",
        device_name, endpoint_kind_name(kind), (unsigned)GSR_AUDIO_SAMPLE_RATE, num_channels,
        (unsigned)self->mix_info.sample_rate, (unsigned)self->mix_info.num_channels, (int)audio_format, period_frame_size);
    endpoint->lpVtbl->Release(endpoint);
    return 0;

fail:
    if(self->thread_created) {
        InterlockedExchange(&self->stop_requested, 1);
        WaitForSingleObject(self->thread, INFINITE);
        CloseHandle(self->thread);
        self->thread_created = false;
    }
    if(self->audio_client) {
        self->audio_client->lpVtbl->Stop(self->audio_client);
        if(self->capture_client)
            self->capture_client->lpVtbl->Release(self->capture_client);
        self->audio_client->lpVtbl->Release(self->audio_client);
    }
    if(self->mix_format)
        CoTaskMemFree(self->mix_format);
    free(self->ring);
    free(self->read_buffer);
    free(self);
    endpoint->lpVtbl->Release(endpoint);
    if(com_ok)
        com_uninit();
    return -1;
}

void sound_device_close(SoundDevice *device) {
    wasapi_sound_device *self = (wasapi_sound_device*)device->handle;
    if(!self)
        return;

    InterlockedExchange(&self->stop_requested, 1);
    WakeAllConditionVariable(&self->ring_cond);
    if(self->thread_created) {
        WaitForSingleObject(self->thread, INFINITE);
        CloseHandle(self->thread);
        self->thread_created = false;
    }
    if(self->audio_client) {
        self->audio_client->lpVtbl->Stop(self->audio_client);
        if(self->capture_client)
            self->capture_client->lpVtbl->Release(self->capture_client);
        self->audio_client->lpVtbl->Release(self->audio_client);
    }
    if(self->mix_format)
        CoTaskMemFree(self->mix_format);
    free(self->ring);
    free(self->read_buffer);
    free(self);

    memset(device, 0, sizeof(*device));
}

void sound_device_flush(SoundDevice *device) {
    wasapi_sound_device *self = (wasapi_sound_device*)device->handle;
    if(!self)
        return;
    AcquireSRWLockExclusive(&self->ring_lock);
    self->ring_head_frames = 0;
    self->ring_count_frames = 0;
    ReleaseSRWLockExclusive(&self->ring_lock);
}

int sound_device_read_next_chunk(SoundDevice *device, void **buffer, double timeout_sec, double *latency_seconds) {
    wasapi_sound_device *self = (wasapi_sound_device*)device->handle;
    if(!self)
        return -1;

    if(latency_seconds)
        *latency_seconds = 0.0;

    /* Wait up to timeout_sec for a full period. The timeout is what lets
       the engine's audio thread exit when the recording stops. */
    const DWORD timeout_ms = (DWORD)(timeout_sec * 1000.0) + 1;
    AcquireSRWLockShared(&self->ring_lock);
    while(self->ring_count_frames < self->period_frame_size && !self->stop_requested) {
        if(!SleepConditionVariableSRW(&self->ring_cond, &self->ring_lock, timeout_ms, 0)) {
            ReleaseSRWLockShared(&self->ring_lock);
            return -1; /* timed out: no audio (engine fills silence) */
        }
    }
    if(self->stop_requested) {
        ReleaseSRWLockShared(&self->ring_lock);
        return -1;
    }

    /* Copy one period into the reusable read buffer. */
    const size_t frame_bytes = self->frame_bytes;
    size_t remaining = self->period_frame_size;
    size_t offset = 0;
    while(remaining > 0) {
        const size_t contiguous = self->ring_capacity_frames - self->ring_head_frames;
        const size_t take = remaining < contiguous ? remaining : contiguous;
        memcpy(self->read_buffer + offset * frame_bytes, self->ring + self->ring_head_frames * frame_bytes, take * frame_bytes);
        self->ring_head_frames = (self->ring_head_frames + take) % self->ring_capacity_frames;
        self->ring_count_frames -= take;
        remaining -= take;
        offset += take;
    }
    ReleaseSRWLockShared(&self->ring_lock);

    *buffer = self->read_buffer;
    return (int)self->period_frame_size;
}

/* ---- device listing ----------------------------------------------------- */

static bool audio_devices_add(gsr_audio_devices *audio_devices, const char *name, const char *description) {
    if(!gsr_array_ensure_capacity((void**)&audio_devices->items, audio_devices->num_items, &audio_devices->capacity_items, sizeof(gsr_audio_device)))
        return false;
    gsr_audio_device *item = &audio_devices->items[audio_devices->num_items];
    snprintf(item->name, sizeof(item->name), "%s", name);
    snprintf(item->description, sizeof(item->description), "%s", description);
    ++audio_devices->num_items;
    return true;
}

static void audio_devices_add_endpoint(gsr_audio_devices *audio_devices, IMMDevice *device) {
    LPWSTR id = NULL;
    if(FAILED(device->lpVtbl->GetId(device, &id)) || !id)
        return;

    char name[256] = {0};
    size_t n = wcslen(id);
    if(n < sizeof(name)) {
        for(size_t i = 0; i < n; ++i)
            name[i] = (char)id[i];
    }
    CoTaskMemFree(id);
    if(name[0] == '\0')
        return;

    char description[256] = {0};
    IPropertyStore *store = NULL;
    if(SUCCEEDED(device->lpVtbl->OpenPropertyStore(device, STGM_READ, &store)) && store) {
        PROPVARIANT variant;
        PropVariantInit(&variant);
        if(SUCCEEDED(store->lpVtbl->GetValue(store, &PKEY_Device_FriendlyName, &variant)) && variant.vt == VT_LPWSTR && variant.pwszVal) {
            size_t written = 0;
            for(const wchar_t *wc = variant.pwszVal; *wc && written < sizeof(description) - 1; ++wc) {
                if(*wc < 0x80)
                    description[written++] = (char)*wc;
            }
        }
        PropVariantClear(&variant);
        store->lpVtbl->Release(store);
    }
    if(description[0] == '\0')
        snprintf(description, sizeof(description), "%s", name);

    audio_devices_add(audio_devices, name, description);
}

static void audio_devices_set_default(gsr_audio_devices *audio_devices, IMMDeviceEnumerator *enumerator, EDataFlow data_flow, char *out, size_t out_size) {
    (void)audio_devices;
    IMMDevice *device = NULL;
    if(FAILED(enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, data_flow, eConsole, &device)) || !device)
        return;
    LPWSTR id = NULL;
    if(SUCCEEDED(device->lpVtbl->GetId(device, &id)) && id) {
        size_t n = wcslen(id);
        if(n < out_size) {
            for(size_t i = 0; i < n; ++i)
                out[i] = (char)id[i];
        }
        CoTaskMemFree(id);
    }
    device->lpVtbl->Release(device);
}

void get_pulseaudio_inputs(gsr_audio_devices *audio_devices) {
    memset(audio_devices, 0, sizeof(*audio_devices));
    const bool com_ok = com_init();

    IMMDeviceEnumerator *enumerator = NULL;
    if(FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                               &IID_IMMDeviceEnumerator, (void**)&enumerator)) || !enumerator) {
        if(com_ok)
            com_uninit();
        return;
    }

    audio_devices_set_default(audio_devices, enumerator, eRender, audio_devices->default_output, sizeof(audio_devices->default_output));
    audio_devices_set_default(audio_devices, enumerator, eCapture, audio_devices->default_input, sizeof(audio_devices->default_input));

    /* Diagnostic: how many endpoints exist at all (including disabled and
       unplugged)? The runner reports 0 ACTIVE endpoints, which tells us
       nothing about whether a disabled endpoint exists — this count does.
       (The listing above only includes ACTIVE endpoints, since those are
       the only ones that can be captured.) */
    for(int flow = 0; flow < 2; ++flow) {
        IMMDeviceCollection *all = NULL;
        if(SUCCEEDED(enumerator->lpVtbl->EnumAudioEndpoints(enumerator, flow == 0 ? eRender : eCapture, DEVICE_STATE_ALL, &all)) && all) {
            UINT count = 0;
            all->lpVtbl->GetCount(all, &count);
            gsr_log(GSR_LOG_LEVEL_INFO, "wasapi: %u %s endpoint(s) total (incl. disabled/unplugged)", (unsigned)count, flow == 0 ? "render" : "capture");
            all->lpVtbl->Release(all);
        }
    }

    for(int flow = 0; flow < 2; ++flow) {
        const EDataFlow data_flow = flow == 0 ? eRender : eCapture;
        IMMDeviceCollection *collection = NULL;
        if(FAILED(enumerator->lpVtbl->EnumAudioEndpoints(enumerator, data_flow, DEVICE_STATE_ACTIVE, &collection)) || !collection)
            continue;
        UINT count = 0;
        collection->lpVtbl->GetCount(collection, &count);
        for(UINT i = 0; i < count; ++i) {
            IMMDevice *device = NULL;
            if(FAILED(collection->lpVtbl->Item(collection, i, &device)) || !device)
                continue;
            audio_devices_add_endpoint(audio_devices, device);
            device->lpVtbl->Release(device);
        }
        collection->lpVtbl->Release(collection);
    }

    enumerator->lpVtbl->Release(enumerator);
    if(com_ok)
        com_uninit();
}

void gsr_audio_devices_deinit(gsr_audio_devices *self) {
    free(self->items);
    memset(self, 0, sizeof(*self));
}

bool pulseaudio_server_is_pipewire(void) {
    return false; /* Windows has no PulseAudio/PipeWire */
}
