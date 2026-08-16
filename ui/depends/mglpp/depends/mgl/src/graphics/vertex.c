#include "../../include/mgl/graphics/vertex.h"
#include "../../include/mgl/graphics/texture.h"
#include "../../include/mgl/mgl.h"

void mgl_vertices_draw(mgl_context *context, const mgl_vertex *vertices, size_t vertex_count, mgl_primitive_type primitive_type, mgl_vec2f position) {
    if(vertex_count == 0)
        return;

    context->gl.glPushMatrix();

    if(!mgl_texture_current_texture()) {
        context->gl.glMatrixMode(GL_TEXTURE);
        context->gl.glLoadIdentity();
    }

    context->gl.glMatrixMode(GL_MODELVIEW);
    context->gl.glTranslatef(position.x, position.y, 0.0f);

    context->gl.glVertexPointer(2, GL_FLOAT, sizeof(mgl_vertex), (void*)&vertices[0].position);
    context->gl.glTexCoordPointer(2, GL_FLOAT, sizeof(mgl_vertex), (void*)&vertices[0].texcoords);
    context->gl.glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(mgl_vertex), (void*)&vertices[0].color);
    context->gl.glDrawArrays(mgl_primitive_type_to_gl_mode(primitive_type), 0, vertex_count);

    context->gl.glPopMatrix();
}
