#include "../../include/mgl/graphics/rectangle.h"
#include "../../include/mgl/mgl.h"

void mgl_rectangle_draw(mgl_context *context, const mgl_rectangle *rect) {
    context->gl.glColor4ub(rect->color.r, rect->color.g, rect->color.b, rect->color.a);
    context->gl.glBegin(GL_QUADS);
        context->gl.glVertex3f(rect->position.x,                rect->position.y, 0.0f);
        context->gl.glVertex3f(rect->position.x + rect->size.x, rect->position.y, 0.0f);
        context->gl.glVertex3f(rect->position.x + rect->size.x, rect->position.y + rect->size.y, 0.0f);
        context->gl.glVertex3f(rect->position.x,                rect->position.y + rect->size.y, 0.0f);
    context->gl.glEnd();
}
