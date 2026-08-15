#include "../../../include/encoder/video/vaapi.h"
#include "../../../include/log.h"
#include "../../../include/utils.h"
#include "../../../include/egl.h"

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/intreadwrite.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    gsr_video_encoder_vaapi_params params;

    unsigned int target_textures[2];
    vec2i texture_sizes[2];

    AVBufferRef *device_ctx;
    VADisplay va_dpy;
    VADRMPRIMESurfaceDescriptor prime;
} gsr_video_encoder_vaapi;

static bool gsr_video_encoder_vaapi_setup_context(gsr_video_encoder_vaapi *self, AVCodecContext *video_codec_context) {
    char render_path[128];
    if(!gsr_card_path_get_render_path(self->params.egl->card_path, render_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_context: failed to get /dev/dri/renderDXXX file from %s", self->params.egl->card_path);
        return false;
    }

    if(av_hwdevice_ctx_create(&self->device_ctx, AV_HWDEVICE_TYPE_VAAPI, render_path, NULL, 0) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_context: failed to create hardware device context");
        return false;
    }

    AVBufferRef *frame_context = av_hwframe_ctx_alloc(self->device_ctx);
    if(!frame_context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_context: failed to create hwframe context");
        av_buffer_unref(&self->device_ctx);
        return false;
    }

    AVHWFramesContext *hw_frame_context = (AVHWFramesContext*)frame_context->data;
    hw_frame_context->width = video_codec_context->width;
    hw_frame_context->height = video_codec_context->height;
    hw_frame_context->sw_format = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    hw_frame_context->format = video_codec_context->pix_fmt;
    hw_frame_context->device_ctx = (AVHWDeviceContext*)self->device_ctx->data;

    //hw_frame_context->initial_pool_size = 20;

    AVVAAPIDeviceContext *vactx = ((AVHWDeviceContext*)self->device_ctx->data)->hwctx;
    self->va_dpy = vactx->display;

    if (av_hwframe_ctx_init(frame_context) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_context: failed to initialize hardware frame context (note: ffmpeg version needs to be > 4.0)");
        av_buffer_unref(&self->device_ctx);
        //av_buffer_unref(&frame_context);
        return false;
    }

    video_codec_context->hw_frames_ctx = av_buffer_ref(frame_context);
    av_buffer_unref(&frame_context);
    return true;
}

static uint32_t fourcc(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (d << 24) | (c << 16) | (b << 8) | a;
}

static bool gsr_video_encoder_vaapi_setup_textures(gsr_video_encoder_vaapi *self, AVCodecContext *video_codec_context, AVFrame *frame) {
    const int res = av_hwframe_get_buffer(video_codec_context->hw_frames_ctx, frame, 0);
    if(res < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_textures: av_hwframe_get_buffer failed: %d", res);
        return false;
    }

    VASurfaceID target_surface_id = (uintptr_t)frame->data[3];

    VAStatus va_status = vaExportSurfaceHandle(self->va_dpy, target_surface_id, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &self->prime);
    if(va_status != VA_STATUS_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_textures: vaExportSurfaceHandle failed, error: %d", va_status);
        return false;
    }
    vaSyncSurface(self->va_dpy, target_surface_id);

    const uint32_t formats_nv12[2] = { fourcc('R', '8', ' ', ' '), fourcc('G', 'R', '8', '8') };
    const uint32_t formats_p010[2] = { fourcc('R', '1', '6', ' '), fourcc('G', 'R', '3', '2') };

    if(self->prime.fourcc == VA_FOURCC_NV12 || self->prime.fourcc == VA_FOURCC_P010) {
        const uint32_t *formats = self->prime.fourcc == VA_FOURCC_NV12 ? formats_nv12 : formats_p010;
        const int div[2] = {1, 2}; // divide UV texture size by 2 because chroma is half size

        self->params.egl->glGenTextures(2, self->target_textures);
        for(int i = 0; i < 2; ++i) {
            const int layer = i;

            int fds[4];
            uint32_t offsets[4];
            uint32_t pitches[4];
            uint64_t modifiers[4];
            for(uint32_t j = 0; j < self->prime.layers[layer].num_planes; ++j) {
                // TODO: Close these? in _stop, using self->prime
                fds[j] = self->prime.objects[self->prime.layers[layer].object_index[j]].fd;
                offsets[j] = self->prime.layers[layer].offset[j];
                pitches[j] = self->prime.layers[layer].pitch[j];
                modifiers[j] = self->prime.objects[self->prime.layers[layer].object_index[j]].drm_format_modifier;
            }

            intptr_t img_attr[44];
            setup_dma_buf_attrs(img_attr, formats[i], self->prime.width / div[i], self->prime.height / div[i],
                fds, offsets, pitches, modifiers, self->prime.layers[layer].num_planes, true);
            self->texture_sizes[i] = (vec2i){ self->prime.width / div[i], self->prime.height / div[i] };

            while(self->params.egl->eglGetError() != EGL_SUCCESS){}
            EGLImage image = self->params.egl->eglCreateImage(self->params.egl->egl_display, 0, EGL_LINUX_DMA_BUF_EXT, NULL, img_attr);
            if(!image) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_textures: failed to create egl image from drm fd for output drm fd, error: %d", self->params.egl->eglGetError());
                return false;
            }

            self->params.egl->glBindTexture(GL_TEXTURE_2D, self->target_textures[i]);
            self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            while(self->params.egl->glGetError()) {}
            while(self->params.egl->eglGetError() != EGL_SUCCESS){}
            self->params.egl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
            if(self->params.egl->glGetError() != 0 || self->params.egl->eglGetError() != EGL_SUCCESS) {
                // TODO: Get the error properly
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_textures: failed to bind egl image to gl texture, error: %d", self->params.egl->eglGetError());
                self->params.egl->eglDestroyImage(self->params.egl->egl_display, image);
                self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);
                return false;
            }

            self->params.egl->eglDestroyImage(self->params.egl->egl_display, image);
            self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);
        }

        return true;
    } else {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vaapi_setup_textures: unexpected fourcc %u for output drm fd, expected nv12 or p010", self->prime.fourcc);
        return false;
    }
}

static void gsr_video_encoder_vaapi_stop(gsr_video_encoder_vaapi *self, AVCodecContext *video_codec_context);

static bool supports_hevc_without_padding(const char *card_path) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 28, 100) && VA_CHECK_VERSION(1, 21, 0)
    VAStatus va_status;
    VAConfigID va_config = 0;
    unsigned int num_surface_attr = 0;
    VASurfaceAttrib *surface_attr_list = NULL;
    bool supports_surface_attrib_alignment_size = false;
    int va_major = 0;
    int va_minor = 0;
    bool initialized = false;

    char render_path[128];
    if(!gsr_card_path_get_render_path(card_path, render_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to get /dev/dri/renderDXXX file from %s", card_path);
        return false;
    }

    const int drm_fd = open(render_path, O_RDWR);
    if(drm_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to open device %s", render_path);
        return false;
    }

    const VADisplay va_dpy = vaGetDisplayDRM(drm_fd);
    if(!va_dpy) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to get vaapi display for device %s", render_path);
        goto done;
    }

    vaSetInfoCallback(va_dpy, NULL, NULL);

    if(vaInitialize(va_dpy, &va_major, &va_minor) != VA_STATUS_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: vaInitialize failed");
        goto done;
    }
    initialized = true;

    va_status = vaCreateConfig(va_dpy, VAProfileHEVCMain, VAEntrypointEncSlice, NULL, 0, &va_config);
    if(va_status != VA_STATUS_SUCCESS) {
        va_status = vaCreateConfig(va_dpy, VAProfileHEVCMain, VAEntrypointEncSliceLP, NULL, 0, &va_config);
        if(va_status != VA_STATUS_SUCCESS) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to create hevc vaapi config, error: %s (%d)", vaErrorStr(va_status), va_status);
            return false;
        }
    }

    va_status = vaQuerySurfaceAttributes(va_dpy, va_config, 0, &num_surface_attr);
    if(va_status != VA_STATUS_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to query vaapi surface attributes size, error: %s (%d)", vaErrorStr(va_status), va_status);
        goto done;
    }

    surface_attr_list = malloc(num_surface_attr * sizeof(VASurfaceAttrib));
    if(!surface_attr_list) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to allocate memory for %u vaapi surface attributes, error: %s (%d)", num_surface_attr, vaErrorStr(va_status), va_status);
        goto done;
    }

    va_status = vaQuerySurfaceAttributes(va_dpy, va_config, surface_attr_list, &num_surface_attr);
    if(va_status != VA_STATUS_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "supports_hevc_without_padding: failed to query vaapi surface attributes data, error: %s (%d)", vaErrorStr(va_status), va_status);
        goto done;
    }

    for(unsigned int i = 0; i < num_surface_attr; ++i) {
        if(surface_attr_list[i].type == VASurfaceAttribAlignmentSize) {
            supports_surface_attrib_alignment_size = true;
            break;
        }
    }

    done:
    free(surface_attr_list);
    if(va_config > 0)
        vaDestroyConfig(va_dpy, va_config);
    if(initialized)
        vaTerminate(va_dpy);
    if(drm_fd > 0)
        close(drm_fd);
    return supports_surface_attrib_alignment_size;
#else
    return false;
#endif
}

static bool gsr_video_encoder_vaapi_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame) {
    gsr_video_encoder_vaapi *self = encoder->priv;

    if(self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_AMD && video_codec_context->codec_id == AV_CODEC_ID_HEVC) {
        if(supports_hevc_without_padding(self->params.egl->card_path)) {
            video_codec_context->width = FFALIGN(video_codec_context->width, 2);
            video_codec_context->height = FFALIGN(video_codec_context->height, 2);
        } else {
            video_codec_context->width = FFALIGN(video_codec_context->width, 64);
            video_codec_context->height = FFALIGN(video_codec_context->height, 16);
        }
    } else if(self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_AMD && video_codec_context->codec_id == AV_CODEC_ID_AV1) {
        // TODO: Dont do this for VCN 5 and forward which should fix this hardware bug
        video_codec_context->width = FFALIGN(video_codec_context->width, 64);
        // AMD driver has special case handling for 1080 height to set it to 1082 instead of 1088 (1080 aligned to 16).
        // TODO: Set height to 1082 in this case, but it wont work because it will be aligned to 1088.
        if(video_codec_context->height == 1080) {
            video_codec_context->height = 1080;
        } else {
            video_codec_context->height = FFALIGN(video_codec_context->height, 16);
        }
    } else {
        video_codec_context->width = FFALIGN(video_codec_context->width, 2);
        video_codec_context->height = FFALIGN(video_codec_context->height, 2);
    }

    if(FFALIGN(video_codec_context->width, 2) != FFALIGN(frame->width, 2) || FFALIGN(video_codec_context->height, 2) != FFALIGN(frame->height, 2)) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_video_encoder_vaapi_start: black bars have been added to the video because of a bug in AMD drivers/hardware. Record with h264/hevc codec instead (-k h264) to get around this issue");
    }

    if(video_codec_context->width < 128)
        video_codec_context->width = 128;

    if(video_codec_context->height < 128)
        video_codec_context->height = 128;

    frame->width = video_codec_context->width;
    frame->height = video_codec_context->height;

    if(!gsr_video_encoder_vaapi_setup_context(self, video_codec_context)) {
        gsr_video_encoder_vaapi_stop(self, video_codec_context);
        return false;
    }

    if(!gsr_video_encoder_vaapi_setup_textures(self, video_codec_context, frame)) {
        gsr_video_encoder_vaapi_stop(self, video_codec_context);
        return false;
    }

    return true;
}

void gsr_video_encoder_vaapi_stop(gsr_video_encoder_vaapi *self, AVCodecContext *video_codec_context) {
    self->params.egl->glDeleteTextures(2, self->target_textures);
    self->target_textures[0] = 0;
    self->target_textures[1] = 0;

    if(video_codec_context && video_codec_context->hw_frames_ctx)
        av_buffer_unref(&video_codec_context->hw_frames_ctx);
    if(self->device_ctx)
        av_buffer_unref(&self->device_ctx);

    for(uint32_t i = 0; i < self->prime.num_objects; ++i) {
        if(self->prime.objects[i].fd > 0) {
            close(self->prime.objects[i].fd);
            self->prime.objects[i].fd = 0;
        }
    }
}

static void gsr_video_encoder_vaapi_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color) {
    gsr_video_encoder_vaapi *self = encoder->priv;
    textures[0] = self->target_textures[0];
    textures[1] = self->target_textures[1];
    texture_sizes[0] = self->texture_sizes[0];
    texture_sizes[1] = self->texture_sizes[1];
    *num_textures = 2;
    *destination_color = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? GSR_DESTINATION_COLOR_P010 : GSR_DESTINATION_COLOR_NV12;
}

static void gsr_video_encoder_vaapi_destroy(gsr_video_encoder *encoder, AVCodecContext *video_codec_context) {
    gsr_video_encoder_vaapi_stop(encoder->priv, video_codec_context);
    free(encoder->priv);
    free(encoder);
}

gsr_video_encoder* gsr_video_encoder_vaapi_create(const gsr_video_encoder_vaapi_params *params) {
    gsr_video_encoder *encoder = calloc(1, sizeof(gsr_video_encoder));
    if(!encoder)
        return NULL;

    gsr_video_encoder_vaapi *encoder_vaapi = calloc(1, sizeof(gsr_video_encoder_vaapi));
    if(!encoder_vaapi) {
        free(encoder);
        return NULL;
    }

    encoder_vaapi->params = *params;

    *encoder = (gsr_video_encoder) {
        .start = gsr_video_encoder_vaapi_start,
        .get_textures = gsr_video_encoder_vaapi_get_textures,
        .destroy = gsr_video_encoder_vaapi_destroy,
        .priv = encoder_vaapi
    };

    return encoder;
}
