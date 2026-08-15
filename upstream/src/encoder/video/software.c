#include "../../../include/encoder/video/software.h"
#include "../../../include/log.h"
#include "../../../include/egl.h"
#include "../../../include/utils.h"

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>

#include <stdlib.h>

#define LINESIZE_ALIGNMENT 4

typedef struct {
    gsr_video_encoder_software_params params;

    unsigned int target_textures[2];
    vec2i texture_sizes[2];
} gsr_video_encoder_software;

static bool gsr_video_encoder_software_setup_textures(gsr_video_encoder_software *self, AVCodecContext *video_codec_context, AVFrame *frame) {
    int res = av_frame_get_buffer(frame, LINESIZE_ALIGNMENT);
    if(res < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_software_setup_textures: av_frame_get_buffer failed: %d", res);
        return false;
    }

    res = av_frame_make_writable(frame);
    if(res < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_software_setup_textures: av_frame_make_writable failed: %d", res);
        return false;
    }

    const unsigned int internal_formats_nv12[2] = { GL_R8, GL_RG8 };
    const unsigned int internal_formats_p010[2] = { GL_R16, GL_RG16 };
    const unsigned int formats[2] = { GL_RED, GL_RG };
    const int div[2] = {1, 2}; // divide UV texture size by 2 because chroma is half size

    for(int i = 0; i < 2; ++i) {
        self->texture_sizes[i] = (vec2i){ video_codec_context->width / div[i], video_codec_context->height / div[i] };
        self->target_textures[i] = gl_create_texture(self->params.egl, self->texture_sizes[i].x, self->texture_sizes[i].y, self->params.color_depth == GSR_COLOR_DEPTH_8_BITS ? internal_formats_nv12[i] : internal_formats_p010[i], formats[i], GL_NEAREST);
        if(self->target_textures[i] == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_setup_cuda_textures: failed to create opengl texture");
            return false;
        }
    }

    return true;
}

static void gsr_video_encoder_software_stop(gsr_video_encoder_software *self, AVCodecContext *video_codec_context);

static bool gsr_video_encoder_software_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame) {
    gsr_video_encoder_software *self = encoder->priv;

    video_codec_context->width = FFALIGN(video_codec_context->width, LINESIZE_ALIGNMENT);
    video_codec_context->height = FFALIGN(video_codec_context->height, 2);

    frame->width = video_codec_context->width;
    frame->height = video_codec_context->height;

    if(!gsr_video_encoder_software_setup_textures(self, video_codec_context, frame)) {
        gsr_video_encoder_software_stop(self, video_codec_context);
        return false;
    }

    return true;
}

void gsr_video_encoder_software_stop(gsr_video_encoder_software *self, AVCodecContext *video_codec_context) {
    (void)video_codec_context;
    self->params.egl->glDeleteTextures(2, self->target_textures);
    self->target_textures[0] = 0;
    self->target_textures[1] = 0;
}

static void gsr_video_encoder_software_copy_textures_to_frame(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion) {
    (void)encoder;
    //gsr_video_encoder_software *self = encoder->priv;
    // TODO: hdr support
    const unsigned int formats[2] = { GL_RED, GL_RG };
    const int div[2] = {1, 2}; // divide UV texture size by 2 because chroma is half size
    for(int i = 0; i < 2; ++i) {
        // TODO: Use glPixelStore?
        gsr_color_conversion_read_destination_texture(color_conversion, i, 0, 0, frame->width / div[i], frame->height / div[i], formats[i], GL_UNSIGNED_BYTE, frame->data[i]);
    }
    // cap_kms->kms.base.egl->eglSwapBuffers(cap_kms->kms.base.egl->egl_display, cap_kms->kms.base.egl->egl_surface);

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();
}

static void gsr_video_encoder_software_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color) {
    gsr_video_encoder_software *self = encoder->priv;
    textures[0] = self->target_textures[0];
    textures[1] = self->target_textures[1];
    texture_sizes[0] = self->texture_sizes[0];
    texture_sizes[1] = self->texture_sizes[1];
    *num_textures = 2;
    *destination_color = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? GSR_DESTINATION_COLOR_P010 : GSR_DESTINATION_COLOR_NV12;
}

static void gsr_video_encoder_software_destroy(gsr_video_encoder *encoder, AVCodecContext *video_codec_context) {
    gsr_video_encoder_software_stop(encoder->priv, video_codec_context);
    free(encoder->priv);
    free(encoder);
}

gsr_video_encoder* gsr_video_encoder_software_create(const gsr_video_encoder_software_params *params) {
    gsr_video_encoder *encoder = calloc(1, sizeof(gsr_video_encoder));
    if(!encoder)
        return NULL;

    gsr_video_encoder_software *encoder_software = calloc(1, sizeof(gsr_video_encoder_software));
    if(!encoder_software) {
        free(encoder);
        return NULL;
    }

    encoder_software->params = *params;

    *encoder = (gsr_video_encoder) {
        .start = gsr_video_encoder_software_start,
        .copy_textures_to_frame = gsr_video_encoder_software_copy_textures_to_frame,
        .get_textures = gsr_video_encoder_software_get_textures,
        .destroy = gsr_video_encoder_software_destroy,
        .priv = encoder_software
    };

    return encoder;
}
