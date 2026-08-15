#include "../../../include/encoder/video/nvenc.h"
#include "../../../include/log.h"
#include "../../../include/egl.h"
#include "../../../include/cuda.h"
#include "../../../include/utils.h"

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_cuda.h>

#include <stdlib.h>

typedef struct {
    gsr_video_encoder_nvenc_params params;

    unsigned int target_textures[2];
    vec2i target_texture_size[2];

    AVBufferRef *device_ctx;

    gsr_cuda cuda;
    CUgraphicsResource cuda_graphics_resources[2];
    CUarray mapped_arrays[2];
    CUstream cuda_stream;
} gsr_video_encoder_nvenc;

static bool gsr_video_encoder_nvenc_setup_context(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context) {
    self->device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA);
    if(!self->device_ctx) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_context failed: failed to create hardware device context");
        return false;
    }

    AVHWDeviceContext *hw_device_context = (AVHWDeviceContext*)self->device_ctx->data;
    AVCUDADeviceContext *cuda_device_context = (AVCUDADeviceContext*)hw_device_context->hwctx;
    cuda_device_context->cuda_ctx = self->cuda.cu_ctx;
    if(av_hwdevice_ctx_init(self->device_ctx) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_context failed: failed to create hardware device context");
        av_buffer_unref(&self->device_ctx);
        return false;
    }

    AVBufferRef *frame_context = av_hwframe_ctx_alloc(self->device_ctx);
    if(!frame_context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_context failed: failed to create hwframe context");
        av_buffer_unref(&self->device_ctx);
        return false;
    }

    AVHWFramesContext *hw_frame_context = (AVHWFramesContext*)frame_context->data;
    hw_frame_context->width = video_codec_context->width;
    hw_frame_context->height = video_codec_context->height;
    hw_frame_context->sw_format = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    hw_frame_context->format = video_codec_context->pix_fmt;
    hw_frame_context->device_ctx = (AVHWDeviceContext*)self->device_ctx->data;

    if (av_hwframe_ctx_init(frame_context) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_context failed: failed to initialize hardware frame context (note: ffmpeg version needs to be > 4.0)");
        av_buffer_unref(&self->device_ctx);
        //av_buffer_unref(&frame_context);
        return false;
    }

    self->cuda_stream = cuda_device_context->stream;
    video_codec_context->hw_frames_ctx = av_buffer_ref(frame_context);
    av_buffer_unref(&frame_context);
    return true;
}

static bool cuda_register_opengl_texture(gsr_cuda *cuda, CUgraphicsResource *cuda_graphics_resource, CUarray *mapped_array, unsigned int texture_id) {
    CUresult res;
    res = cuda->cuGraphicsGLRegisterImage(cuda_graphics_resource, texture_id, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_NONE);
    if (res != CUDA_SUCCESS) {
        const char *err_str = "unknown";
        cuda->cuGetErrorString(res, &err_str);
        gsr_log(GSR_LOG_LEVEL_ERROR, "cuda_register_opengl_texture: cuGraphicsGLRegisterImage failed, error: %s, texture id: %u", err_str, texture_id);
        return false;
    }

    res = cuda->cuGraphicsResourceSetMapFlags(*cuda_graphics_resource, CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE);
    res = cuda->cuGraphicsMapResources(1, cuda_graphics_resource, 0);

    res = cuda->cuGraphicsSubResourceGetMappedArray(mapped_array, *cuda_graphics_resource, 0, 0);
    return true;
}

static bool gsr_video_encoder_nvenc_setup_textures(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context, AVFrame *frame) {
    const int res = av_hwframe_get_buffer(video_codec_context->hw_frames_ctx, frame, 0);
    if(res < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_textures: av_hwframe_get_buffer failed: %d", res);
        return false;
    }

    const unsigned int internal_formats_nv12[2] = { GL_R8, GL_RG8 };
    const unsigned int internal_formats_p010[2] = { GL_R16, GL_RG16 };
    const unsigned int formats[2] = { GL_RED, GL_RG };
    const int div[2] = {1, 2}; // divide UV texture size by 2 because chroma is half size

    for(int i = 0; i < 2; ++i) {
        self->target_texture_size[i] = (vec2i){ video_codec_context->width / div[i], video_codec_context->height / div[i] };
        self->target_textures[i] = gl_create_texture(self->params.egl, self->target_texture_size[i].x, self->target_texture_size[i].y, self->params.color_depth == GSR_COLOR_DEPTH_8_BITS ? internal_formats_nv12[i] : internal_formats_p010[i], formats[i], GL_NEAREST);
        if(self->target_textures[i] == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_setup_textures: failed to create opengl texture");
            return false;
        }

        if(!cuda_register_opengl_texture(&self->cuda, &self->cuda_graphics_resources[i], &self->mapped_arrays[i], self->target_textures[i])) {
            return false;
        }
    }

    return true;
}

static void gsr_video_encoder_nvenc_stop(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context);

static bool gsr_video_encoder_nvenc_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame) {
    gsr_video_encoder_nvenc *self = encoder->priv;

    if(!gsr_cuda_load(&self->cuda)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_nvenc_start: failed to load cuda");
        gsr_video_encoder_nvenc_stop(self, video_codec_context);
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

    if(!gsr_video_encoder_nvenc_setup_context(self, video_codec_context)) {
        gsr_video_encoder_nvenc_stop(self, video_codec_context);
        return false;
    }

    if(!gsr_video_encoder_nvenc_setup_textures(self, video_codec_context, frame)) {
        gsr_video_encoder_nvenc_stop(self, video_codec_context);
        return false;
    }

    return true;
}

void gsr_video_encoder_nvenc_stop(gsr_video_encoder_nvenc *self, AVCodecContext *video_codec_context) {
    self->params.egl->glDeleteTextures(2, self->target_textures);
    self->target_textures[0] = 0;
    self->target_textures[1] = 0;

    if(video_codec_context && video_codec_context->hw_frames_ctx)
        av_buffer_unref(&video_codec_context->hw_frames_ctx);
    if(self->device_ctx)
        av_buffer_unref(&self->device_ctx);

    if(self->cuda.cu_ctx) {
        for(int i = 0; i < 2; ++i) {
            if(self->cuda_graphics_resources[i]) {
                self->cuda.cuGraphicsUnmapResources(1, &self->cuda_graphics_resources[i], 0);
                self->cuda.cuGraphicsUnregisterResource(self->cuda_graphics_resources[i]);
                self->cuda_graphics_resources[i] = 0;
            }
        }
    }

    gsr_cuda_unload(&self->cuda);
}

static void gsr_video_encoder_nvenc_copy_textures_to_frame(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion) {
    gsr_video_encoder_nvenc *self = encoder->priv;
    const int div[2] = {1, 2}; // divide UV texture size by 2 because chroma is half size
    for(int i = 0; i < 2; ++i) {
        CUDA_MEMCPY2D memcpy_struct;
        memcpy_struct.srcXInBytes = 0;
        memcpy_struct.srcY = 0;
        memcpy_struct.srcMemoryType = CU_MEMORYTYPE_ARRAY;

        memcpy_struct.dstXInBytes = 0;
        memcpy_struct.dstY = 0;
        memcpy_struct.dstMemoryType = CU_MEMORYTYPE_DEVICE;

        memcpy_struct.srcArray = self->mapped_arrays[i];
        memcpy_struct.srcPitch = frame->width / div[i];
        memcpy_struct.dstDevice = (CUdeviceptr)frame->data[i];
        memcpy_struct.dstPitch = frame->linesize[i];
        memcpy_struct.WidthInBytes = frame->width * (self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? 2 : 1);
        memcpy_struct.Height = frame->height / div[i];
        // TODO: Remove this copy if possible
        self->cuda.cuMemcpy2DAsync_v2(&memcpy_struct, self->cuda_stream);
    }

    // TODO: needed?
    self->cuda.cuStreamSynchronize(self->cuda_stream);
}

static void gsr_video_encoder_nvenc_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color) {
    gsr_video_encoder_nvenc *self = encoder->priv;
    textures[0] = self->target_textures[0];
    textures[1] = self->target_textures[1];
    texture_sizes[0] = self->target_texture_size[0];
    texture_sizes[1] = self->target_texture_size[1];
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

    gsr_video_encoder_nvenc *encoder_cuda = calloc(1, sizeof(gsr_video_encoder_nvenc));
    if(!encoder_cuda) {
        free(encoder);
        return NULL;
    }

    encoder_cuda->params = *params;

    *encoder = (gsr_video_encoder) {
        .start = gsr_video_encoder_nvenc_start,
        .copy_textures_to_frame = gsr_video_encoder_nvenc_copy_textures_to_frame,
        .get_textures = gsr_video_encoder_nvenc_get_textures,
        .destroy = gsr_video_encoder_nvenc_destroy,
        .priv = encoder_cuda
    };

    return encoder;
}
