#ifndef MGL_VERTEX_H
#define MGL_VERTEX_H

#include "primitive_type.h"
#include "color.h"
#include "../system/vec.h"
#include <stddef.h>

typedef struct mgl_context mgl_context;
typedef struct mgl_vertex mgl_vertex;

struct mgl_vertex {
    mgl_vec2f position;
    mgl_vec2f texcoords;
    mgl_color color;
};

/*
    Note: this sends the vertices to the gpu everytime this is called. This should only be used for small lists of vertices
    or if every vertex needs to change every frame. Use mgl_vertex_buffer instead if possible.
*/
void mgl_vertices_draw(mgl_context *context, const mgl_vertex *vertices, size_t vertex_count, mgl_primitive_type primitive_type, mgl_vec2f position);

#endif /* MGL_VERTEX_H */
