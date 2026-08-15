#include "../include/image_writer.h"
#include "../include/log.h"
#include "../include/egl.h"
#include "../include/utils.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>

#define TJPF_RGBA 7
#define TJSAMP_444 0
#define TJSAMP_420 2

typedef void* tjhandle;
typedef tjhandle (*FUNC_tjInitCompress)(void);
typedef int (*FUNC_tjCompress2)(tjhandle handle, const unsigned char *srcBuf,
                                int width, int pitch, int height, int pixelFormat,
                                unsigned char **jpegBuf, unsigned long *jpegSize,
                                int jpegSubsamp, int jpegQual, int flags);
typedef int (*FUNC_tjDestroy)(tjhandle handle);
typedef void (*FUNC_tjFree)(unsigned char *buffer);

static bool write_buffer_to_file(const char *filepath, const unsigned char *data, unsigned long size) {
    FILE *file = fopen(filepath, "wb");
    if(!file)
        return false;

    const bool success = fwrite(data, size, 1, file) == 1;
    fclose(file);
    return success;
}

static void gsr_image_writer_init_libturbojpeg(gsr_image_writer *self) {
    self->turbojpeg_lib = dlopen("libturbojpeg.so.0", RTLD_LAZY);
    if(!self->turbojpeg_lib)
        return;

    const FUNC_tjInitCompress tjInitCompress_func = (FUNC_tjInitCompress)dlsym(self->turbojpeg_lib, "tjInitCompress");
    self->tjCompress2_func = dlsym(self->turbojpeg_lib, "tjCompress2");
    self->tjDestroy_func = dlsym(self->turbojpeg_lib, "tjDestroy");
    self->tjFree_func = dlsym(self->turbojpeg_lib, "tjFree");
    if(!tjInitCompress_func || !self->tjCompress2_func || !self->tjDestroy_func || !self->tjFree_func) {
        dlclose(self->turbojpeg_lib);
        self->turbojpeg_lib = NULL;
        return;
    }

    self->turbojpeg_compressor = tjInitCompress_func();
    if(!self->turbojpeg_compressor) {
        dlclose(self->turbojpeg_lib);
        self->turbojpeg_lib = NULL;
    }
}

static bool write_jpeg_with_libturbojpeg(gsr_image_writer *self, const char *filepath, const void *data, int quality) {
    if(!self->turbojpeg_compressor)
        return false;

    const FUNC_tjCompress2 tjCompress2_func = (FUNC_tjCompress2)self->tjCompress2_func;
    const FUNC_tjFree tjFree_func = (FUNC_tjFree)self->tjFree_func;
    const int chroma_subsampling = quality >= JPEG_YUV444_QUALITY_THRESHOLD ? TJSAMP_444 : TJSAMP_420;

    bool success = false;
    unsigned char *jpeg_data = NULL;
    unsigned long jpeg_size = 0;
    if(tjCompress2_func(self->turbojpeg_compressor, data, self->width, self->width * 4, self->height, TJPF_RGBA, &jpeg_data, &jpeg_size, chroma_subsampling, quality, 0) == 0)
        success = write_buffer_to_file(filepath, jpeg_data, jpeg_size);

    if(jpeg_data)
        tjFree_func(jpeg_data);
    return success;
}

/* TODO: Support hdr/10-bit */
bool gsr_image_writer_init_opengl(gsr_image_writer *self, gsr_egl *egl, int width, int height) {
    memset(self, 0, sizeof(*self));
    self->egl = egl;
    self->width = width;
    self->height = height;
    self->texture = gl_create_texture(self->egl, self->width, self->height, GL_RGBA8, GL_RGBA, GL_NEAREST); /* TODO: use GL_RGB16 instead of GL_RGB8 for hdr/10-bit */
    if(self->texture == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_image_writer_init: failed to create texture");
        return false;
    }

    gsr_image_writer_init_libturbojpeg(self);
    return true;
}

void gsr_image_writer_deinit(gsr_image_writer *self) {
    if(self->turbojpeg_compressor) {
        const FUNC_tjDestroy tjDestroy_func = (FUNC_tjDestroy)self->tjDestroy_func;
        tjDestroy_func(self->turbojpeg_compressor);
        self->turbojpeg_compressor = NULL;
    }

    if(self->turbojpeg_lib) {
        dlclose(self->turbojpeg_lib);
        self->turbojpeg_lib = NULL;
    }

    if(self->texture) {
        self->egl->glDeleteTextures(1, &self->texture);
        self->texture = 0;
    }
}

static bool gsr_image_writer_write_memory_to_file(gsr_image_writer *self, const char *filepath, gsr_image_format image_format, int quality, const void *data) {
    if(quality < 1)
        quality = 1;
    else if(quality > 100)
        quality = 100;

    bool success = false;
    switch(image_format) {
        case GSR_IMAGE_FORMAT_JPEG:
            success = write_jpeg_with_libturbojpeg(self, filepath, data, quality);
            if(!success)
                success = stbi_write_jpg(filepath, self->width, self->height, 4, data, quality);
            break;
        case GSR_IMAGE_FORMAT_PNG:
            success = stbi_write_png(filepath, self->width, self->height, 4, data, 0);
            break;
    }

    if(!success)
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_image_writer_write_to_file: failed to write image data to output file %s", filepath);

    return success;
}

static bool gsr_image_writer_write_opengl_texture_to_file(gsr_image_writer *self, const char *filepath, gsr_image_format image_format, int quality) {
    uint8_t *frame_data = malloc(self->width * self->height * 4);
    if(!frame_data) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_image_writer_write_to_file: failed to allocate memory for image frame");
        return false;
    }

    unsigned int fbo = 0;
    self->egl->glGenFramebuffers(1, &fbo);
    self->egl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    self->egl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, self->texture, 0);

    self->egl->glReadPixels(0, 0, self->width, self->height, GL_RGBA, GL_UNSIGNED_BYTE, frame_data);

    self->egl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    self->egl->glDeleteFramebuffers(1, &fbo);

    self->egl->glFlush();
    self->egl->glFinish();
    
    const bool success = gsr_image_writer_write_memory_to_file(self, filepath, image_format, quality, frame_data);
    free(frame_data);
    return success;
}

bool gsr_image_writer_write_to_file(gsr_image_writer *self, const char *filepath, gsr_image_format image_format, int quality) {
    return gsr_image_writer_write_opengl_texture_to_file(self, filepath, image_format, quality);
}
