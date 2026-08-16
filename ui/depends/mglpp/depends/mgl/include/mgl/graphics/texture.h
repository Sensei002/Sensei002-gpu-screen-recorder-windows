#ifndef MGL_TEXTURE_H
#define MGL_TEXTURE_H

#include "image.h"
#include <stdbool.h>

typedef struct mgl_texture mgl_texture;

typedef enum {
    MGL_TEXTURE_FORMAT_ALPHA,
    MGL_TEXTURE_FORMAT_GRAY,
    MGL_TEXTURE_FORMAT_GRAY_ALPHA,
    MGL_TEXTURE_FORMAT_RGB,
    MGL_TEXTURE_FORMAT_RGBA
} mgl_texture_format;

typedef enum {
    MGL_TEXTURE_SCALE_NEAREST,
    MGL_TEXTURE_SCALE_LINEAR,
    MGL_TEXTURE_SCALE_LINEAR_MIPMAP /* generate mipmaps (available since opengl 3.0) */
} mgl_texture_scale_type;

struct mgl_texture {
    unsigned int id;
    int width;
    int height;
    mgl_texture_format format;
    int max_width;
    int max_height;
    bool pixel_coordinates;
    mgl_texture_scale_type scale_type;
    bool owned;
};

typedef struct {
    bool compressed; /* false by default */
    bool pixel_coordinates; /* false by default, texture coordinates are normalized */
    mgl_texture_scale_type scale_type; /* MGL_TEXTURE_SCALE_LINEAR by default */
} mgl_texture_load_options;

typedef struct {
    bool pixel_coordinates; /* false by default, texture coordinates are normalized */
    mgl_texture_scale_type scale_type; /* MGL_TEXTURE_SCALE_LINEAR by default */
} mgl_texture_reference_options;

int mgl_texture_init(mgl_texture *self);
/* |reference_options| can be null, in which case the default options are used */
int mgl_texture_init_reference_existing_gl_texture(mgl_texture *self, unsigned int texture_id, mgl_texture_format format, const mgl_texture_reference_options *reference_options);

/* |load_options| can be null, in which case the default options are used */
int mgl_texture_load_from_file(mgl_texture *self, const char *filepath, const mgl_texture_load_options *load_options);
/* |load_options| can be null, in which case the default options are used */
int mgl_texture_load_from_image(mgl_texture *self, const mgl_image *image, const mgl_texture_load_options *load_options);
/* |load_options| can be null, in which case the default options are used */
int mgl_texture_load_from_memory(mgl_texture *self, const unsigned char *data, int width, int height, mgl_image_format format, const mgl_texture_load_options *load_options);

int mgl_texture_update(mgl_texture *self, const unsigned char *data, int offset_x, int offset_y, int width, int height, mgl_image_format format);
/* |load_options| can be null, in which case the default options are used */
int mgl_texture_resize(mgl_texture *self, int new_width, int new_height, const mgl_texture_load_options *load_options);
/* If |texture| is NULL then no texture is used */
void mgl_texture_use(const mgl_texture *texture);
const mgl_texture* mgl_texture_current_texture(void);
void mgl_texture_unload(mgl_texture *self);

#endif /* MGL_TEXTURE_H */
