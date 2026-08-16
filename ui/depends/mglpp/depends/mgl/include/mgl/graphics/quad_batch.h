#ifndef MGL_QUAD_BATCH_H
#define MGL_QUAD_BATCH_H

#include <stdint.h>

typedef struct {
    float x, y, u, v;
} mgl_quad_vertex;

typedef struct {
    mgl_quad_vertex   *verts;
    uint32_t          *indices;
    uint32_t           vert_count, vert_cap;
    uint32_t           idx_count, idx_cap;
} mgl_quad_batch;

void mgl_batch_init(mgl_quad_batch *batch);
void mgl_batch_deinit(mgl_quad_batch *batch);

void mgl_batch_clear(mgl_quad_batch *batch);
void mgl_batch_push_quad(mgl_quad_batch *batch,
                         float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1);
void mgl_batch_draw(const mgl_quad_batch *batch);

#endif /* MGL_QUAD_BATCH_H */
