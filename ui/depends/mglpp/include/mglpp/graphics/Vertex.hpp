#ifndef MGLPP_VERTEX_HPP
#define MGLPP_VERTEX_HPP

#include "Color.hpp"
#include "../system/vec.hpp"

namespace mgl {
    struct Vertex {
        Vertex() = default;
        Vertex(vec2f position, Color color) : position(position), color(color) {}
        Vertex(vec2f position, vec2f texcoords, Color color) : position(position), texcoords(texcoords), color(color) {}

        vec2f position;
        vec2f texcoords;
        Color color;
    };
}

#endif /* MGLPP_VERTEX_HPP */
