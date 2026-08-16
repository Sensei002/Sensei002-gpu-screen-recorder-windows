#ifndef MGL_PRIMITIVE_TYPE_H
#define MGL_PRIMITIVE_TYPE_H

typedef enum {
    MGL_PRIMITIVE_POINTS,
    MGL_PRIMITIVE_LINES,
    MGL_PRIMITIVE_LINE_STRIP,
    MGL_PRIMITIVE_TRIANGLES,
    MGL_PRIMITIVE_TRIANGLE_STRIP,
    MGL_PRIMITIVE_TRIANGLE_FAN,
    MGL_PRIMITIVE_QUADS,
    MGL_PRIMITIVE_QUAD_STRIP,
    MGL_PRIMITIVE_POLYGON,
} mgl_primitive_type;

unsigned int mgl_primitive_type_to_gl_mode(mgl_primitive_type primitive_type);

#endif /* MGL_PRIMITIVE_TYPE_H */
