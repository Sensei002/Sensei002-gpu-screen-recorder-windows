#ifndef MGL_RECTANGLE_H
#define MGL_RECTANGLE_H

#include "../system/vec.h"
#include "color.h"

typedef struct mgl_context mgl_context;

typedef struct {
    mgl_vec2f position;
    mgl_vec2f size;
    mgl_color color;
} mgl_rectangle;

void mgl_rectangle_draw(mgl_context *context, const mgl_rectangle *rect);

#endif /* MGL_RECTANGLE_H */
