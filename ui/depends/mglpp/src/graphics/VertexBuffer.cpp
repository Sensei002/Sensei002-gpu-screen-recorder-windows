#include "../../include/mglpp/graphics/VertexBuffer.hpp"
#include "../../include/mglpp/graphics/Texture.hpp"

extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    VertexBuffer::VertexBuffer() : texture(nullptr) {
        mgl_vertex_buffer_init(&vertex_buffer);
    }

    VertexBuffer::~VertexBuffer() {
        mgl_vertex_buffer_deinit(&vertex_buffer);
    }

    void VertexBuffer::set_position(vec2f position) {
        mgl_vertex_buffer_set_position(&vertex_buffer, {position.x, position.y});
    }

    void VertexBuffer::set_color(Color color) {
        // No-op, because the vertex buffer has colors for each vertex
        (void)color;
    }

    vec2f VertexBuffer::get_position() const {
        return { vertex_buffer.position.x, vertex_buffer.position.y };
    }

    void VertexBuffer::set_texture(Texture *texture) {
        this->texture = texture;
    }

    const Texture* VertexBuffer::get_texture() const {
        return texture;
    }

    size_t VertexBuffer::size() const {
        return vertex_buffer.vertex_count;
    }

    bool VertexBuffer::update(const Vertex *vertices, size_t vertex_count, PrimitiveType primitive_type, Usage usage) {
        return mgl_vertex_buffer_update(&vertex_buffer, (const mgl_vertex*)vertices, vertex_count, (mgl_primitive_type)primitive_type, (mgl_vertex_buffer_usage)usage) == 0;
    }

    void VertexBuffer::draw(Window&) {
        mgl_vertex_buffer_draw(mgl_get_context(), &vertex_buffer, texture ? texture->internal_texture() : nullptr);
    }
}