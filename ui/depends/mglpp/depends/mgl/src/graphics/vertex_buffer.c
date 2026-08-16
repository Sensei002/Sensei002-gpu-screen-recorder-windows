#include "../../include/mgl/graphics/vertex_buffer.h"
#include "../../include/mgl/graphics/texture.h"
#include "../../include/mgl/mgl.h"

/* TODO: Cache vertex buffer */
/*
    TODO: Add option to only use vertices and color or vertices and texture coordinates.
    In such cases we should call glEnableClientState/glDisableClientState(GL_COLOR_ARRAY/GL_TEXTURE_COORD_ARRAY) and others.
*/

static unsigned int mgl_vertex_buffer_usage_to_gl_usage(mgl_vertex_buffer_usage usage) {
    switch(usage) {
        case MGL_USAGE_STREAM:  return GL_STREAM_DRAW;
        case MGL_USAGE_DYNAMIC: return GL_DYNAMIC_DRAW;
        case MGL_USAGE_STATIC:  return GL_STATIC_DRAW;
    }
    return 0;
}

void mgl_vertex_buffer_init(mgl_vertex_buffer *self) {
    self->id = 0;
    self->vertex_count = 0;
    self->primitive_type = -1;
    self->usage = -1;
    self->position = (mgl_vec2f){ 0.0f, 0.0f };
}

void mgl_vertex_buffer_deinit(mgl_vertex_buffer *self) {
    mgl_context *context = mgl_get_context();
    if(self->id) {
        context->gl.glDeleteBuffers(1, &self->id);
        self->id = 0;
    }
    self->vertex_count = 0;
    self->primitive_type = 0;
    self->usage = 0;
}

static void mgl_vertex_buffer_set_gl_buffer_pointers(mgl_context *context) {
    context->gl.glVertexPointer(2, GL_FLOAT, sizeof(mgl_vertex), (void*)offsetof(mgl_vertex, position));
    context->gl.glTexCoordPointer(2, GL_FLOAT, sizeof(mgl_vertex), (void*)offsetof(mgl_vertex, texcoords));
    context->gl.glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(mgl_vertex), (void*)offsetof(mgl_vertex, color));
}

/* TODO: Check for glBufferData error */
static int mgl_vertex_buffer_resize(mgl_vertex_buffer *self, const mgl_vertex *vertices, size_t vertex_count, mgl_primitive_type primitive_type, mgl_vertex_buffer_usage usage) {
    mgl_context *context = mgl_get_context();

    if(self->id == 0) {
        context->gl.glGenBuffers(1, &self->id);
        if(self->id == 0)
            return -1;
    }

    self->primitive_type = primitive_type;
    self->usage = usage;

    context->gl.glBindBuffer(GL_ARRAY_BUFFER, self->id);
    /* TODO: Optimize by calling with NULL data first? or do that in |mgl_vertex_buffer_update| */
    context->gl.glBufferData(GL_ARRAY_BUFFER, sizeof(mgl_vertex) * vertex_count, vertices, mgl_vertex_buffer_usage_to_gl_usage(self->usage));
    context->gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    self->vertex_count = vertex_count;
    return 0;
}

void mgl_vertex_buffer_set_position(mgl_vertex_buffer *self, mgl_vec2f position) {
    self->position = position;
}

/* TODO: Check for glBufferSubData error */
int mgl_vertex_buffer_update(mgl_vertex_buffer *self, const mgl_vertex *vertices, size_t vertex_count, mgl_primitive_type primitive_type, mgl_vertex_buffer_usage usage) {
    mgl_context *context = mgl_get_context();
    
    if(vertex_count == 0) {
        if(self->id) {
            context->gl.glDeleteBuffers(1, &self->id);
            self->id = 0;
        }
        self->vertex_count = vertex_count;
        self->primitive_type = primitive_type;
        self->usage = usage;
        return 0;
    }
    
    if(vertex_count != self->vertex_count || primitive_type != self->primitive_type || usage != self->usage)
        return mgl_vertex_buffer_resize(self, vertices, vertex_count, primitive_type, usage);

    context->gl.glBindBuffer(GL_ARRAY_BUFFER, self->id);
    context->gl.glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(mgl_vertex) * vertex_count, vertices);
    context->gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    return 0;
}

/* TODO: Optimize bind texture */
void mgl_vertex_buffer_draw(mgl_context *context, mgl_vertex_buffer *vertex_buffer, const mgl_texture *texture) {
    if(vertex_buffer->vertex_count == 0 || vertex_buffer->id == 0)
        return;

    /* TODO: Optimize this */
    context->gl.glPushMatrix();
    context->gl.glTranslatef(vertex_buffer->position.x, vertex_buffer->position.y, 0.0f);

    mgl_texture_use(texture);
    context->gl.glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer->id);
    mgl_vertex_buffer_set_gl_buffer_pointers(context);
    context->gl.glDrawArrays(mgl_primitive_type_to_gl_mode(vertex_buffer->primitive_type), 0, vertex_buffer->vertex_count);
    context->gl.glBindBuffer(GL_ARRAY_BUFFER, 0);
    mgl_texture_use(NULL);

    context->gl.glPopMatrix();
}
