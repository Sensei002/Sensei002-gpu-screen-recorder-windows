#ifndef MGL_VERTEX_BUFFER_H
#define MGL_VERTEX_BUFFER_H

#include "vertex.h"
#include <stddef.h>

typedef struct mgl_context mgl_context;
typedef struct mgl_texture mgl_texture;

typedef enum {
    MGL_USAGE_STREAM,
    MGL_USAGE_DYNAMIC,
    MGL_USAGE_STATIC
} mgl_vertex_buffer_usage;

typedef struct {
    unsigned int id;
    size_t vertex_count;
    mgl_primitive_type primitive_type;
    mgl_vertex_buffer_usage usage;
    mgl_vec2f position;
} mgl_vertex_buffer;

void mgl_vertex_buffer_init(mgl_vertex_buffer *self);
void mgl_vertex_buffer_deinit(mgl_vertex_buffer *self);

void mgl_vertex_buffer_set_position(mgl_vertex_buffer *self, mgl_vec2f position);
int mgl_vertex_buffer_update(mgl_vertex_buffer *self, const mgl_vertex *vertices, size_t vertex_count, mgl_primitive_type primitive_type, mgl_vertex_buffer_usage usage);
/* |texture| can be NULL to not use any texture */
void mgl_vertex_buffer_draw(mgl_context *context, mgl_vertex_buffer *vertex_buffer, const mgl_texture *texture);

#endif /* MGL_VERTEX_BUFFER_H */
