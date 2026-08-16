#include "../../include/mgl/graphics/texture.h"
#include "../../include/mgl/graphics/image.h"
#include "../../include/mgl/mgl.h"
#include <stdio.h>

/* TODO: Check for glTexImage2D/glTexSubImage2D failure */

static const mgl_texture *current_texture = NULL;

static int mgl_texture_format_to_opengl_format(mgl_texture_format format) {
    switch(format) {
        case MGL_TEXTURE_FORMAT_ALPHA:      return GL_ALPHA8;
        case MGL_TEXTURE_FORMAT_GRAY:       return GL_LUMINANCE8;
        case MGL_TEXTURE_FORMAT_GRAY_ALPHA: return GL_LUMINANCE8_ALPHA8;
        case MGL_TEXTURE_FORMAT_RGB:        return GL_RGB8;
        case MGL_TEXTURE_FORMAT_RGBA:       return GL_RGBA8;
    }
    return 0;
}

static int mgl_texture_format_to_compressed_opengl_format(mgl_texture_format format) {
    switch(format) {
        case MGL_TEXTURE_FORMAT_ALPHA:      return GL_ALPHA8;
        case MGL_TEXTURE_FORMAT_GRAY:       return GL_LUMINANCE8;
        case MGL_TEXTURE_FORMAT_GRAY_ALPHA: return GL_LUMINANCE8_ALPHA8;
        case MGL_TEXTURE_FORMAT_RGB:        return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        case MGL_TEXTURE_FORMAT_RGBA:       return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    }
    return 0;
}

static int mgl_texture_format_to_source_opengl_format(mgl_texture_format format) {
    switch(format) {
        case MGL_TEXTURE_FORMAT_ALPHA:      return GL_ALPHA;
        case MGL_TEXTURE_FORMAT_GRAY:       return GL_LUMINANCE;
        case MGL_TEXTURE_FORMAT_GRAY_ALPHA: return GL_LUMINANCE_ALPHA;
        case MGL_TEXTURE_FORMAT_RGB:        return GL_RGB;
        case MGL_TEXTURE_FORMAT_RGBA:       return GL_RGBA;
    }
    return 0;
}

static mgl_texture_format mgl_image_format_to_mgl_texture_format(mgl_image_format image_format) {
    switch(image_format) {
        case MGL_IMAGE_FORMAT_ALPHA:        return MGL_TEXTURE_FORMAT_ALPHA;
        case MGL_IMAGE_FORMAT_GRAY:         return MGL_TEXTURE_FORMAT_GRAY;
        case MGL_IMAGE_FORMAT_GRAY_ALPHA:   return MGL_TEXTURE_FORMAT_GRAY_ALPHA;
        case MGL_IMAGE_FORMAT_RGB:          return MGL_TEXTURE_FORMAT_RGB;
        case MGL_IMAGE_FORMAT_RGBA:         return MGL_TEXTURE_FORMAT_RGBA;
    }
    return 0;
}

static void gl_get_texture_size(unsigned int texture_id, int *width, int *height) {
    *width = 0;
    *height = 0;

    mgl_context *context = mgl_get_context();
    context->gl.glBindTexture(GL_TEXTURE_2D, texture_id);
    context->gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, width);
    context->gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, height);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
}

int mgl_texture_init(mgl_texture *self) {
    self->id = 0;
    self->width = 0;
    self->height = 0;
    self->format = 0;
    self->max_width = 0;
    self->max_height = 0;
    self->pixel_coordinates = false;
    self->scale_type = MGL_TEXTURE_SCALE_LINEAR;
    self->owned = true;

    mgl_context *context = mgl_get_context();
    context->gl.glGenTextures(1, &self->id);
    if(self->id == 0) {
        fprintf(stderr, "mgl error: failed to init texture (glGenTextures failed)\n");
        return -1;
    }

    int max_texture_size = 0;
    context->gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    self->max_width = max_texture_size;
    self->max_height = max_texture_size;

    return 0;
}

int mgl_texture_init_reference_existing_gl_texture(mgl_texture *self, unsigned int texture_id, mgl_texture_format format, const mgl_texture_reference_options *reference_options) {
    self->id = texture_id;
    self->width = 0;
    self->height = 0;
    self->format = format;
    self->max_width = 0;
    self->max_height = 0;
    self->pixel_coordinates = reference_options && reference_options->pixel_coordinates;
    self->scale_type = reference_options ? reference_options->scale_type : MGL_TEXTURE_SCALE_LINEAR;
    self->owned = false;

    gl_get_texture_size(self->id, &self->width, &self->height);

    int max_texture_size = 0;
    mgl_context *context = mgl_get_context();
    context->gl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    self->max_width = max_texture_size;
    self->max_height = max_texture_size;

    return 0;
}

int mgl_texture_load_from_file(mgl_texture *self, const char *filepath, const mgl_texture_load_options *load_options) {
    mgl_image image;
    if(mgl_image_load_from_file(&image, filepath) != 0)
        return -1;

    int result = mgl_texture_load_from_image(self, &image, load_options);
    mgl_image_unload(&image);
    return result;
}

int mgl_texture_load_from_image(mgl_texture *self, const mgl_image *image, const mgl_texture_load_options *load_options) {
    return mgl_texture_load_from_memory(self, image->data, image->width, image->height, image->format, load_options);
}

int mgl_texture_load_from_memory(mgl_texture *self, const unsigned char *data, int width, int height, mgl_image_format format, const mgl_texture_load_options *load_options) {
    if(width < 0 || height < 0)
        return -1;

    if(width > self->max_width || height > self->max_height)
        return -1;

    mgl_context *context = mgl_get_context();

    self->width = width;
    self->height = height;
    self->format = mgl_image_format_to_mgl_texture_format(format);
    self->pixel_coordinates = load_options && load_options->pixel_coordinates;
    self->scale_type = load_options ? load_options->scale_type : MGL_TEXTURE_SCALE_LINEAR; /* TODO: Check if glGenerateMipmap is actually available */

    int opengl_texture_format = mgl_texture_format_to_opengl_format(self->format);
    if(load_options && load_options->compressed)
        opengl_texture_format = mgl_texture_format_to_compressed_opengl_format(self->format);

    context->gl.glBindTexture(GL_TEXTURE_2D, self->id);
    context->gl.glTexImage2D(GL_TEXTURE_2D, 0, opengl_texture_format, self->width, self->height, 0, mgl_texture_format_to_source_opengl_format(self->format), GL_UNSIGNED_BYTE, data);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    switch(self->scale_type) {
        default:
        case MGL_TEXTURE_SCALE_LINEAR: {
            context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            break;
        }
        case MGL_TEXTURE_SCALE_NEAREST: {
            context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            break;
        }
        case MGL_TEXTURE_SCALE_LINEAR_MIPMAP: {
            context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if(context->gl.glGenerateMipmap) {
                context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                context->gl.glGenerateMipmap(GL_TEXTURE_2D);
            } else {
                context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
            break;
        }
    }
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);

    return 0;
}

int mgl_texture_update(mgl_texture *self, const unsigned char *data, int offset_x, int offset_y, int width, int height, mgl_image_format format) {
    if(offset_x + width > self->width || offset_y + height > self->height)
        return -1;

    mgl_context *context = mgl_get_context();
    context->gl.glBindTexture(GL_TEXTURE_2D, self->id);
    const mgl_texture_format texture_format = mgl_image_format_to_mgl_texture_format(format);
    context->gl.glTexSubImage2D(GL_TEXTURE_2D, 0, offset_x, offset_y, width, height, mgl_texture_format_to_source_opengl_format(texture_format), GL_UNSIGNED_BYTE, data);
    if(self->scale_type == MGL_TEXTURE_SCALE_LINEAR_MIPMAP && context->gl.glGenerateMipmap)
        context->gl.glGenerateMipmap(GL_TEXTURE_2D);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

int mgl_texture_resize(mgl_texture *self, int new_width, int new_height, const mgl_texture_load_options *load_options) {
    if(new_width == self->width && new_height == self->height)
        return 0;

    if(new_width < 0 || new_height < 0)
        return -1;

    if(new_width > self->max_width || new_height > self->max_height)
        return -1;

    self->width = new_width;
    self->height = new_height;

    const int opengl_texture_format = load_options && load_options->compressed ? mgl_texture_format_to_compressed_opengl_format(self->format) : mgl_texture_format_to_opengl_format(self->format);

    mgl_context *context = mgl_get_context();
    context->gl.glBindTexture(GL_TEXTURE_2D, self->id);
    context->gl.glTexImage2D(GL_TEXTURE_2D, 0, opengl_texture_format, self->width, self->height, 0, mgl_texture_format_to_source_opengl_format(self->format), GL_UNSIGNED_BYTE, NULL);
    if(self->scale_type == MGL_TEXTURE_SCALE_LINEAR_MIPMAP && context->gl.glGenerateMipmap)
        context->gl.glGenerateMipmap(GL_TEXTURE_2D);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

/* TODO: Optimize. Do not set matrix if the current coordinate type was the same as the previous one */
void mgl_texture_use(const mgl_texture *texture) {
    mgl_context *context = mgl_get_context();

    current_texture = texture;
    if(texture) {
        float matrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };

        if(texture->pixel_coordinates) {
            matrix[0] = 1.0f / (float)texture->width;
            matrix[5] = 1.0f / (float)texture->height;
        }

        context->gl.glMatrixMode(GL_TEXTURE);
        context->gl.glLoadMatrixf(matrix);
        context->gl.glMatrixMode(GL_MODELVIEW);

        context->gl.glBindTexture(GL_TEXTURE_2D, texture->id);
    } else {
        context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    }
}

const mgl_texture* mgl_texture_current_texture(void) {
    return current_texture;
}

void mgl_texture_unload(mgl_texture *self) {
    mgl_context *context = mgl_get_context();
    if(!self->owned)
        return;

    if(self->id) {
        context->gl.glDeleteTextures(1, &self->id);
        self->id = 0;
    }
}
