#ifndef MGL_IMAGE_H
#define MGL_IMAGE_H

#include <stddef.h>

typedef struct mgl_image mgl_image;

typedef enum {
    MGL_IMAGE_FORMAT_ALPHA,
    MGL_IMAGE_FORMAT_GRAY,
    MGL_IMAGE_FORMAT_GRAY_ALPHA,
    MGL_IMAGE_FORMAT_RGB,
    MGL_IMAGE_FORMAT_RGBA
} mgl_image_format;

struct mgl_image {
    unsigned char *data;
    int width;
    int height;
    mgl_image_format format;
};

int mgl_image_load_from_file(mgl_image *self, const char *filepath);
int mgl_image_load_from_memory(mgl_image *self, const unsigned char *data, size_t size);
void mgl_image_unload(mgl_image *self);

size_t mgl_image_get_size(const mgl_image *self);
int mgl_image_get_num_channels(const mgl_image *self);

#endif /* MGL_IMAGE_H */
