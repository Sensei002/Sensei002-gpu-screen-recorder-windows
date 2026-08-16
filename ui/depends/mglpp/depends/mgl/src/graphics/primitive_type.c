#include "../../include/mgl/graphics/primitive_type.h"
#include "../../include/mgl/gl_macro.h"

unsigned int mgl_primitive_type_to_gl_mode(mgl_primitive_type primitive_type) {
    switch(primitive_type) {
        case MGL_PRIMITIVE_POINTS:          return GL_POINTS;
        case MGL_PRIMITIVE_LINES:           return GL_LINES;
        case MGL_PRIMITIVE_LINE_STRIP:      return GL_LINE_STRIP;
        case MGL_PRIMITIVE_TRIANGLES:       return GL_TRIANGLES;
        case MGL_PRIMITIVE_TRIANGLE_STRIP:  return GL_TRIANGLE_STRIP;
        case MGL_PRIMITIVE_TRIANGLE_FAN:    return GL_TRIANGLE_FAN;
        case MGL_PRIMITIVE_QUADS:           return GL_QUADS;
        case MGL_PRIMITIVE_QUAD_STRIP:      return GL_QUAD_STRIP;
        case MGL_PRIMITIVE_POLYGON:         return GL_POLYGON;
    }
    return 0;
}
