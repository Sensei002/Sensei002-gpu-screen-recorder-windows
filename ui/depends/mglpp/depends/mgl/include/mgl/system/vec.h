#ifndef MGL_VEC_H
#define MGL_VEC_H

typedef struct {
    float x, y;
} mgl_vec2f;

typedef struct {
    float x, y, z;
} mgl_vec3f;

typedef struct {
    float x, y, z, w;
} mgl_vec4f;

typedef struct {
    int x, y;
} mgl_vec2i;

typedef struct {
    int x, y, z;
} mgl_vec3i;

typedef struct {
    int x, y, z, w;
} mgl_vec4i;

#endif /* MGL_VEC_H */
