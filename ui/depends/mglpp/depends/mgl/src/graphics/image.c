#include "../../include/mgl/graphics/image.h"
#include <assert.h>

#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_IMPLEMENTATION
#include "../../external/stb_image.h"

static mgl_image_format stbi_format_to_mgl_image_format(int stbi_format) {
    switch(stbi_format) {
        case STBI_grey:
            return MGL_IMAGE_FORMAT_GRAY;
        case STBI_grey_alpha:
            return MGL_IMAGE_FORMAT_GRAY_ALPHA;
        case STBI_rgb:
            return MGL_IMAGE_FORMAT_RGB;
        case STBI_rgb_alpha:
            return MGL_IMAGE_FORMAT_RGBA;
    }
    assert(0);
    return MGL_IMAGE_FORMAT_RGBA;
}

static size_t mgl_image_format_num_channels(mgl_image_format image_format) {
    switch(image_format) {
        case MGL_IMAGE_FORMAT_ALPHA:        return 1;
        case MGL_IMAGE_FORMAT_GRAY:         return 1;
        case MGL_IMAGE_FORMAT_GRAY_ALPHA:   return 2;
        case MGL_IMAGE_FORMAT_RGB:          return 3;
        case MGL_IMAGE_FORMAT_RGBA:         return 4;
    }
    assert(0);
    return 4;
}

/* TODO: Ensure texture is power of 2 if the hardware doesn't support non power of two textures */
/* TODO: Verify if source format should always be 4 components (RGBA) because apparently if its another format then opengl will internally convert it to RGBA */
int mgl_image_load_from_file(mgl_image *self, const char *filepath) {
    self->data = NULL;
    self->width = 0;
    self->height = 0;
    
    int format;
    self->data = stbi_load(filepath, &self->width, &self->height, &format, 0);
    if(!self->data) {
        fprintf(stderr, "mgl error: failed to load image %s, error: %s\n", filepath, stbi_failure_reason());
        mgl_image_unload(self);
        return -1;
    }
    self->format = stbi_format_to_mgl_image_format(format);

    return 0;
}

/* TODO: Ensure texture is power of 2 if the hardware doesn't support non power of two textures */
/* TODO: Verify if source format should always be 4 components (RGBA) because apparently if its another format then opengl will internally convert it to RGBA */
int mgl_image_load_from_memory(mgl_image *self, const unsigned char *data, size_t size) {
    self->data = NULL;
    self->width = 0;
    self->height = 0;
    
    int format;
    self->data = stbi_load_from_memory(data, size, &self->width, &self->height, &format, 0);
    if(!self->data) {
        fprintf(stderr, "mgl error: failed to load image from memory, error: %s\n", stbi_failure_reason());
        mgl_image_unload(self);
        return -1;
    }
    self->format = stbi_format_to_mgl_image_format(format);

    return 0;
}

void mgl_image_unload(mgl_image *self) {
    if(self->data) {
        stbi_image_free(self->data);
        self->data = NULL;
    }
    self->width = 0;
    self->height = 0;
    self->format = 0;
}

size_t mgl_image_get_size(const mgl_image *self) {
    return (size_t)self->width * (size_t)self->height * mgl_image_format_num_channels(self->format);
}

int mgl_image_get_num_channels(const mgl_image *self) {
    return mgl_image_format_num_channels(self->format);
}
