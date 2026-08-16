/* platform/windows/gsr_nvenc_win32.c — Phase 7, milestone B: the upstream
 * NVIDIA encoder + capability query on Windows, replacing upstream's
 * GL+CUDA path (upstream/src/encoder/video/nvenc.c and
 * upstream/src/codec_query/nvenc.c, neither built on Windows).
 *
 * Upstream's nvenc encoder registers the GL target textures with CUDA and
 * copies each frame's planes into CUDA device memory backing the NVENC
 * hw frames. Windows has no CUDA-GL interop in this port, so the same
 * contract is met with d3d11va:
 *
 *   - the color conversion renders into 2 GL textures (Y/UV), exactly like
 *     the software encoder;
 *   - copy_textures_to_frame reads them with glReadPixels into a persistent
 *     system-memory NV12/P010 frame, then av_hwframe_transfer_data uploads
 *     it into a D3D11 texture (hw frame) on the SAME device ANGLE uses
 *     (gsr_platform_egl_get_d3d11_device, Phase 5b) — the d3d11va
 *     equivalent of upstream's cuMemcpy2DAsync;
 *   - the hw frames context (AV_PIX_FMT_D3D11 / NV12 or P010) is attached
 *     to the codec context and h264_nvenc/hevc_nvenc/av1_nvenc encode from
 *     it directly (zero CPU copy after the GL readback, which the software
 *     encoder also pays).
 *
 * Capability probing mirrors upstream's approach but probes honestly:
 * gsr_get_supported_video_codecs_nvenc creates a hardware D3D11 device,
 * verifies it is NVIDIA, maps the adapter description to a generation
 * (pure table, see gsr_nvenc_internal.h), pre-filters codecs the
 * generation cannot have (e.g. AV1 before Ampere), and then ACTUALLY opens
 * each encoder with a d3d11va hw frames context. The probe is
 * authoritative: on the CI runner (Basic Display Adapter, no NVIDIA) it
 * returns false, which drives the existing -fallback-cpu-encoding path.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3j.
 */
#include "gsr_nvenc_internal.h"
#include "../../upstream/include/encoder/video/nvenc.h"
#include "../../upstream/include/codec_query/nvenc.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/log.h"
#include "../../upstream/include/utils.h" /* gl_create_texture */
#include "../include/egl_win32.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

#define NVENC_LINESIZE_ALIGNMENT 32

/* =========================================================================
 * Pure generation table (headless-tested via gsr_nvenc_internal.h)
 * ========================================================================= */

/* Case-insensitive ASCII substring helper (strcasestr is not reliably
   declared by MinGW-w64). */
static bool contains_ci(const char *haystack, const char *needle) {
    if(!haystack || !needle || !*needle)
        return false;
    const size_t nlen = strlen(needle);
    for(const char *p = haystack; *p; ++p) {
        size_t i;
        for(i = 0; i < nlen; ++i) {
            if(!p[i])
                return false;
            char a = p[i];
            char b = needle[i];
            if(a >= 'A' && a <= 'Z') a += (char)('a' - 'A');
            if(b >= 'A' && b <= 'Z') b += (char)('a' - 'A');
            if(a != b)
                break;
        }
        if(i == nlen)
            return true;
    }
    return false;
}

bool gsr_nvenc_description_is_nvidia(const char *description) {
    return contains_ci(description, "nvidia");
}

gsr_nvenc_generation gsr_nvenc_generation_from_adapter_description(const char *description) {
    if(!description)
        return GSR_NVENC_GEN_UNKNOWN;
    /* Precedence matters — the professional naming is ambiguous with the
       consumer one: "Quadro RTX 4000" is Turing but "GeForce RTX 4080" is
       Ada ("rtx 40" is a substring of both), and "Quadro RTX A6000" is
       Ampere. Check the pro patterns (RTX A-series, quadro rtx, "ada"
       naming) BEFORE the consumer "rtx N0" series. */
    if(contains_ci(description, "rtx a") || contains_ci(description, "a100") || contains_ci(description, "a2000") || contains_ci(description, "a4000") || contains_ci(description, "a5000") || contains_ci(description, "a6000"))
        return GSR_NVENC_GEN_AMPERE;
    if(contains_ci(description, "quadro rtx"))
        return GSR_NVENC_GEN_TURING;
    /* No "ada" rule: the substring appears inside "adapter" and other
       words, and the Ada pro naming ("RTX 4000 Ada Generation") already
       matches "rtx 40". Unmatched pro cards fall through to UNKNOWN,
       which probes every codec — the honest outcome. */
    if(contains_ci(description, "rtx 50"))
        return GSR_NVENC_GEN_BLACKWELL;
    if(contains_ci(description, "rtx 40"))
        return GSR_NVENC_GEN_ADA;
    if(contains_ci(description, "rtx 30"))
        return GSR_NVENC_GEN_AMPERE;
    if(contains_ci(description, "rtx 20") || contains_ci(description, "gtx 16") || contains_ci(description, "v100") || contains_ci(description, "titan v"))
        return GSR_NVENC_GEN_TURING;
    if(contains_ci(description, "gtx 10") || contains_ci(description, "p100") || contains_ci(description, "p2000") || contains_ci(description, "p4000") || contains_ci(description, "p5000") || contains_ci(description, "p6000"))
        return GSR_NVENC_GEN_PASCAL;
    if(contains_ci(description, "gtx 9") || contains_ci(description, "m2000") || contains_ci(description, "m4000") || contains_ci(description, "m5000"))
        return GSR_NVENC_GEN_MAXWELL;
    return GSR_NVENC_GEN_UNKNOWN;
}

static const gsr_nvenc_generation_caps nvenc_generation_caps_table[GSR_NVENC_GEN_COUNT] = {
    [GSR_NVENC_GEN_UNKNOWN]  = {.h264 = true, .hevc = true, .hevc_10bit = true, .av1 = true, .av1_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_MAXWELL]  = {.h264 = true, .hevc = true, .h264_max = {4096, 2304}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_PASCAL]   = {.h264 = true, .hevc = true, .hevc_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_TURING]   = {.h264 = true, .hevc = true, .hevc_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_AMPERE]   = {.h264 = true, .hevc = true, .hevc_10bit = true, .av1 = true, .av1_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_ADA]      = {.h264 = true, .hevc = true, .hevc_10bit = true, .av1 = true, .av1_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
    [GSR_NVENC_GEN_BLACKWELL] = {.h264 = true, .hevc = true, .hevc_10bit = true, .av1 = true, .av1_10bit = true, .h264_max = {4096, 4096}, .hevc_av1_max = {8192, 8192}},
};

const gsr_nvenc_generation_caps *gsr_nvenc_get_generation_caps(gsr_nvenc_generation gen) {
    if(gen <= GSR_NVENC_GEN_UNKNOWN || gen >= GSR_NVENC_GEN_COUNT)
        return &nvenc_generation_caps_table[GSR_NVENC_GEN_UNKNOWN];
    return &nvenc_generation_caps_table[gen];
}

/* =========================================================================
 * Live probe
 * ========================================================================= */

/* Create a hardware D3D11 device (BGRA, like the Phase 5b shared device). */
static ID3D11Device *nvenc_create_hardware_device(void) {
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    ID3D11Device *device = NULL;
    const HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
        &device, NULL, NULL);
    return SUCCEEDED(hr) ? device : NULL;
}

static void nvenc_adapter_description(ID3D11Device *device, char *out, size_t out_size) {
    out[0] = '\0';
    IDXGIDevice *dxgi_device = NULL;
    if(FAILED(device->lpVtbl->QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi_device)) || !dxgi_device)
        return;
    IDXGIAdapter *adapter = NULL;
    if(SUCCEEDED(dxgi_device->lpVtbl->GetAdapter(dxgi_device, &adapter)) && adapter) {
        DXGI_ADAPTER_DESC desc;
        memset(&desc, 0, sizeof(desc));
        if(SUCCEEDED(adapter->lpVtbl->GetDesc(adapter, &desc))) {
            size_t written = 0;
            for(size_t i = 0; i < 128 && desc.Description[i] && written < out_size - 1; ++i) {
                const wchar_t wc = desc.Description[i];
                if(wc < 0x80)
                    out[written++] = (char)wc;
            }
        }
        adapter->lpVtbl->Release(adapter);
    }
    dxgi_device->lpVtbl->Release(dxgi_device);
}

bool gsr_nvenc_get_adapter_description(char *out, size_t out_size) {
    ID3D11Device *device = nvenc_create_hardware_device();
    if(!device)
        return false;
    nvenc_adapter_description(device, out, out_size);
    device->lpVtbl->Release(device);
    return out[0] != '\0';
}

/* Build a d3d11va hw frames context (AV_PIX_FMT_D3D11) on |device|. The
   device reference is taken by FFmpeg (which releases it on free). */
static AVBufferRef *nvenc_create_hw_frames(ID3D11Device *device, int width, int height, enum AVPixelFormat sw_format) {
    AVBufferRef *device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if(!device_ctx)
        return NULL;
    AVHWDeviceContext *hw_device_ctx = (AVHWDeviceContext*)device_ctx->data;
    AVD3D11VADeviceContext *d3d11_ctx = (AVD3D11VADeviceContext*)hw_device_ctx->hwctx;
    device->lpVtbl->AddRef(device); /* FFmpeg releases this on free */
    d3d11_ctx->device = device;
    if(av_hwdevice_ctx_init(device_ctx) < 0) {
        av_buffer_unref(&device_ctx);
        return NULL;
    }

    AVBufferRef *frames_ref = av_hwframe_ctx_alloc(device_ctx);
    av_buffer_unref(&device_ctx);
    if(!frames_ref)
        return NULL;
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)frames_ref->data;
    frames_ctx->format = AV_PIX_FMT_D3D11;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width = width;
    frames_ctx->height = height;
    if(av_hwframe_ctx_init(frames_ref) < 0) {
        av_buffer_unref(&frames_ref);
        return NULL;
    }
    return frames_ref;
}

/* Open an nvenc encoder with a d3d11va hw frames context. Returns true when
   the encoder genuinely opens (i.e. the GPU supports the codec/profile). */
static bool nvenc_probe_encoder(const char *encoder_name, bool ten_bit, ID3D11Device *device) {
    const AVCodec *codec = avcodec_find_encoder_by_name(encoder_name);
    if(!codec)
        return false;

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if(!codec_ctx)
        return false;
    codec_ctx->width = 128;
    codec_ctx->height = 128;
    codec_ctx->time_base = (AVRational){1, 60};
    codec_ctx->pix_fmt = AV_PIX_FMT_D3D11;

    const enum AVPixelFormat sw_format = ten_bit ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    AVDictionary *options = NULL;
    if(ten_bit)
        av_dict_set_int(&options, "highbitdepth", 1, 0);

    AVBufferRef *frames_ref = nvenc_create_hw_frames(device, codec_ctx->width, codec_ctx->height, sw_format);
    bool opened = false;
    if(frames_ref) {
        codec_ctx->hw_frames_ctx = av_buffer_ref(frames_ref);
        opened = avcodec_open2(codec_ctx, codec, &options) == 0;
        avcodec_free_context(&codec_ctx);
        av_buffer_unref(&frames_ref);
    } else {
        avcodec_free_context(&codec_ctx);
    }
    av_dict_free(&options);
    return opened;
}

bool gsr_get_supported_video_codecs_nvenc(gsr_supported_video_codecs *video_codecs, bool cleanup) {
    (void)cleanup; /* nothing cached */
    memset(video_codecs, 0, sizeof(*video_codecs));

    ID3D11Device *device = nvenc_create_hardware_device();
    if(!device) {
        gsr_log(GSR_LOG_LEVEL_INFO, "nvenc: no hardware D3D11 device; NVENC unavailable");
        return false;
    }

    char description[256] = {0};
    nvenc_adapter_description(device, description, sizeof(description));
    if(!gsr_nvenc_description_is_nvidia(description)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "nvenc: adapter '%s' is not NVIDIA; NVENC unavailable", description[0] ? description : "(unknown)");
        device->lpVtbl->Release(device);
        return false;
    }

    const gsr_nvenc_generation gen = gsr_nvenc_generation_from_adapter_description(description);
    const gsr_nvenc_generation_caps *caps = gsr_nvenc_get_generation_caps(gen);
    gsr_log(GSR_LOG_LEVEL_INFO, "nvenc: NVIDIA adapter '%s' (generation %d)", description, (int)gen);

    /* The table pre-filters codecs the generation cannot have; each codec
       still must pass the real avcodec_open2 probe. */
    if(caps->h264 && nvenc_probe_encoder("h264_nvenc", false, device)) {
        video_codecs->h264.supported = true;
        video_codecs->h264.max_resolution = caps->h264_max;
    }
    if(caps->hevc && nvenc_probe_encoder("hevc_nvenc", false, device)) {
        video_codecs->hevc.supported = true;
        video_codecs->hevc.max_resolution = caps->hevc_av1_max;
    }
    if(caps->hevc_10bit && nvenc_probe_encoder("hevc_nvenc", true, device)) {
        video_codecs->hevc_10bit.supported = true;
        video_codecs->hevc_10bit.max_resolution = caps->hevc_av1_max;
        video_codecs->hevc_hdr = video_codecs->hevc_10bit; /* HDR rides on Main10 */
    }
    if(caps->av1 && nvenc_probe_encoder("av1_nvenc", false, device)) {
        video_codecs->av1.supported = true;
        video_codecs->av1.max_resolution = caps->hevc_av1_max;
    }
    if(caps->av1_10bit && nvenc_probe_encoder("av1_nvenc", true, device)) {
        video_codecs->av1_10bit.supported = true;
        video_codecs->av1_10bit.max_resolution = caps->hevc_av1_max;
        video_codecs->av1_hdr = video_codecs->av1_10bit;
    }
    /* vp8/vp9: NVENC does not do VP8/VP9 (and the pinned FFmpeg build has
       no nvenc vp8/vp9 encoders); upstream never offered them via NVENC. */

    device->lpVtbl->Release(device);
    return true;
}

/* =========================================================================
 * The d3d11va encoder (gsr_video_encoder vtable)
 * ========================================================================= */

typedef struct {
    gsr_video_encoder_nvenc_params params;

    unsigned int target_textures[2];
    vec2i texture_sizes[2];

    AVBufferRef *hw_frames_ref;   /* d3d11va hw frames context (D3D11/NV12) */
    AVFrame *sw_frame;            /* persistent system-memory NV12/P010 frame */
} gsr_video_encoder_nvenc;

static bool nvenc_setup_textures(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context, AVFrame *frame) {
    const unsigned int internal_formats_nv12[2] = { GL_R8, GL_RG8 };
    const unsigned int internal_formats_p010[2] = { GL_R16, GL_RG16 };
    const unsigned int formats[2] = { GL_RED, GL_RG };
    const int div[2] = {1, 2}; /* UV texture is half size (chroma subsampling) */

    for(int i = 0; i < 2; ++i) {
        self->texture_sizes[i] = (vec2i){ video_codec_context->width / div[i], video_codec_context->height / div[i] };
        self->target_textures[i] = gl_create_texture(self->params.egl, self->texture_sizes[i].x, self->texture_sizes[i].y,
            self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? internal_formats_p010[i] : internal_formats_nv12[i],
            formats[i], GL_NEAREST);
        if(self->target_textures[i] == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc: failed to create opengl texture");
            return false;
        }
    }

    /* The persistent system-memory frame that glReadPixels fills each
       frame, then uploads into the D3D11 hw frame. */
    self->sw_frame = av_frame_alloc();
    if(!self->sw_frame)
        return false;
    self->sw_frame->format = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    self->sw_frame->width = video_codec_context->width;
    self->sw_frame->height = video_codec_context->height;
    if(av_frame_get_buffer(self->sw_frame, NVENC_LINESIZE_ALIGNMENT) < 0) {
        av_frame_free(&self->sw_frame);
        self->sw_frame = NULL;
        return false;
    }

    /* The recorder's |frame| becomes the reusable D3D11 hw frame. */
    if(av_hwframe_get_buffer(self->hw_frames_ref, frame, 0) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc: av_hwframe_get_buffer failed");
        return false;
    }

    return true;
}

static void gsr_video_encoder_nvenc_stop(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context);

static bool gsr_video_encoder_nvenc_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame) {
    gsr_video_encoder_nvenc *self = encoder->priv;

    ID3D11Device *device = (ID3D11Device*)gsr_platform_egl_get_d3d11_device(self->params.egl);
    if(!device) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc: no D3D11 device (did the egl loader run?)");
        return false;
    }

    video_codec_context->width = FFALIGN(video_codec_context->width, 2);
    video_codec_context->height = FFALIGN(video_codec_context->height, 2);
    if(video_codec_context->width < 128)
        video_codec_context->width = 128;
    if(video_codec_context->height < 128)
        video_codec_context->height = 128;
    frame->width = video_codec_context->width;
    frame->height = video_codec_context->height;

    const enum AVPixelFormat sw_format = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    self->hw_frames_ref = nvenc_create_hw_frames(device, video_codec_context->width, video_codec_context->height, sw_format);
    if(!self->hw_frames_ref) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc: failed to create d3d11va hw frames context");
        gsr_video_encoder_nvenc_stop(self, video_codec_context);
        return false;
    }
    video_codec_context->hw_frames_ctx = av_buffer_ref(self->hw_frames_ref);

    if(!nvenc_setup_textures(self, video_codec_context, frame)) {
        gsr_video_encoder_nvenc_stop(self, video_codec_context);
        return false;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "nvenc: encoder started (%dx%d, %s)", video_codec_context->width, video_codec_context->height,
        sw_format == AV_PIX_FMT_P010LE ? "p010" : "nv12");
    return true;
}

static void gsr_video_encoder_nvenc_stop(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context) {
    if(self->params.egl) {
        self->params.egl->glDeleteTextures(2, self->target_textures);
        self->target_textures[0] = 0;
        self->target_textures[1] = 0;
    }

    if(video_codec_context && video_codec_context->hw_frames_ctx)
        av_buffer_unref(&video_codec_context->hw_frames_ctx);
    if(self->hw_frames_ref)
        av_buffer_unref(&self->hw_frames_ref);
    self->hw_frames_ref = NULL;
    if(self->sw_frame) {
        av_frame_free(&self->sw_frame);
        self->sw_frame = NULL;
    }
}

static void gsr_video_encoder_nvenc_copy_textures_to_frame(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion) {
    gsr_video_encoder_nvenc *self = encoder->priv;
    const unsigned int formats[2] = { GL_RED, GL_RG };
    const int div[2] = {1, 2};
    const unsigned int type = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

    /* 1. Read the color-conversion destination textures into system memory
          (the same readback the software encoder does). */
    for(int i = 0; i < 2; ++i) {
        gsr_color_conversion_read_destination_texture(color_conversion, i, 0, 0,
            self->sw_frame->width / div[i], self->sw_frame->height / div[i],
            formats[i], type, self->sw_frame->data[i]);
    }

    /* 2. Upload into the D3D11 hw frame (d3d11va equivalent of upstream's
          cuMemcpy2DAsync). The hw frame must be writable — after
          avcodec_send_frame the encoder holds a reference until it has
          copied the texture, so the check is honest. */
    if(av_frame_make_writable(frame) < 0) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "nvenc: hw frame not writable, dropping frame");
        return;
    }
    if(av_hwframe_transfer_data(frame, self->sw_frame, AV_HWFRAME_TRANSFER_DIRECTION_TO) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "nvenc: av_hwframe_transfer_data failed, dropping frame");
    }
}

static void gsr_video_encoder_nvenc_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color) {
    gsr_video_encoder_nvenc *self = encoder->priv;
    textures[0] = self->target_textures[0];
    textures[1] = self->target_textures[1];
    texture_sizes[0] = self->texture_sizes[0];
    texture_sizes[1] = self->texture_sizes[1];
    *num_textures = 2;
    *destination_color = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? GSR_DESTINATION_COLOR_P010 : GSR_DESTINATION_COLOR_NV12;
}

static void gsr_video_encoder_nvenc_destroy(gsr_video_encoder *encoder, AVCodecContext *video_codec_context) {
    gsr_video_encoder_nvenc_stop(encoder->priv, video_codec_context);
    free(encoder->priv);
    free(encoder);
}

gsr_video_encoder* gsr_video_encoder_nvenc_create(const gsr_video_encoder_nvenc_params *params) {
    gsr_video_encoder *encoder = calloc(1, sizeof(gsr_video_encoder));
    if(!encoder)
        return NULL;

    gsr_video_encoder_nvenc *encoder_nvenc = calloc(1, sizeof(gsr_video_encoder_nvenc));
    if(!encoder_nvenc) {
        free(encoder);
        return NULL;
    }
    encoder_nvenc->params = *params;

    *encoder = (gsr_video_encoder) {
        .start = gsr_video_encoder_nvenc_start,
        .copy_textures_to_frame = gsr_video_encoder_nvenc_copy_textures_to_frame,
        .get_textures = gsr_video_encoder_nvenc_get_textures,
        .destroy = gsr_video_encoder_nvenc_destroy,
        .priv = encoder_nvenc
    };

    return encoder;
}
