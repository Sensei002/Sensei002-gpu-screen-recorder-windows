#include "../../include/mgl/graphics/quad_batch.h"
#include "../../include/mgl/mgl.h"

#include <stdlib.h>

void mgl_batch_init(mgl_quad_batch *batch) {
    batch->vert_cap = 4096;
    batch->idx_cap  = 4096;
    batch->verts    = (mgl_quad_vertex *)malloc(batch->vert_cap * sizeof(mgl_quad_vertex));
    batch->indices  = (uint32_t *)malloc(batch->idx_cap * sizeof(uint32_t));
    batch->vert_count = 0;
    batch->idx_count  = 0;
}

void mgl_batch_deinit(mgl_quad_batch *batch) {
    free(batch->verts);
    free(batch->indices);
}

void mgl_batch_clear(mgl_quad_batch *batch) {
    batch->vert_count = 0;
    batch->idx_count  = 0;
}

void mgl_batch_push_quad(mgl_quad_batch *batch,
                         float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1) {
    if (batch->vert_count + 4 > batch->vert_cap) {
        batch->vert_cap *= 2;
        batch->verts = realloc(batch->verts, batch->vert_cap * sizeof(mgl_quad_vertex));
    }
    if (batch->idx_count + 6 > batch->idx_cap) {
        batch->idx_cap *= 2;
        batch->indices = realloc(batch->indices, batch->idx_cap * sizeof(uint32_t));
    }

    const uint32_t base = (uint32_t)batch->vert_count;
    mgl_quad_vertex *v = batch->verts + batch->vert_count;
    v[0] = (mgl_quad_vertex){ x,     y,     u0, v0 };
    v[1] = (mgl_quad_vertex){ x + w, y,     u1, v0 };
    v[2] = (mgl_quad_vertex){ x + w, y + h, u1, v1 };
    v[3] = (mgl_quad_vertex){ x,     y + h, u0, v1 };
    batch->vert_count += 4;

    uint32_t *idx = batch->indices + batch->idx_count;
    idx[0] = base;     idx[1] = base + 1; idx[2] = base + 2;
    idx[3] = base;     idx[4] = base + 2; idx[5] = base + 3;
    batch->idx_count += 6;
}

void mgl_batch_draw(const mgl_quad_batch *batch) {
    mgl_context *context = mgl_get_context();

    if (!batch->vert_count)
        return;

    //glEnableClientState(GL_VERTEX_ARRAY);
    //glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    context->gl.glDisableClientState(GL_COLOR_ARRAY);
    context->gl.glVertexPointer(2, GL_FLOAT, sizeof(mgl_quad_vertex), &batch->verts[0].x);
    context->gl.glTexCoordPointer(2, GL_FLOAT, sizeof(mgl_quad_vertex), &batch->verts[0].u);
    context->gl.glDrawElements(GL_TRIANGLES, (int)batch->idx_count, GL_UNSIGNED_INT, batch->indices);
    context->gl.glEnableClientState(GL_COLOR_ARRAY);
    //glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    //glDisableClientState(GL_VERTEX_ARRAY);
}
