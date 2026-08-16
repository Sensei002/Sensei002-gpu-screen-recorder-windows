#ifndef MGLPP_VERTEX_BUFFER_HPP
#define MGLPP_VERTEX_BUFFER_HPP

#include "PrimitiveType.hpp"
#include "Drawable.hpp"

extern "C" {
#include <mgl/graphics/vertex_buffer.h>
}

namespace mgl {
    class Texture;
    struct Vertex;

    class VertexBuffer : public Drawable {
    public:
        enum Usage {
            Stream,
            Dynamic,
            Static
        };

        VertexBuffer();
        VertexBuffer(const VertexBuffer&) = delete;
        VertexBuffer& operator=(const VertexBuffer&) = delete;
        ~VertexBuffer();

        void set_position(vec2f position) override;
        void set_color(Color color) override;
        vec2f get_position() const override;

        void set_texture(Texture *texture);
        const Texture* get_texture() const;

        size_t size() const;

        bool update(const Vertex *vertices, size_t vertex_count, PrimitiveType primitive_type, Usage usage);
    protected:
        void draw(Window &window) override;
    private:
        mgl_vertex_buffer vertex_buffer;
        Texture *texture;
    };
}

#endif /* MGLPP_VERTEX_BUFFER_HPP */
