#include "../../include/mglpp/graphics/Rectangle.hpp"

extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    Rectangle::Rectangle() : Rectangle(vec2f(0.0f, 0.0f), vec2f(0.0f, 0.0f)) {}

    Rectangle::Rectangle(vec2f size) : Rectangle(vec2f(0.0f, 0.0f), size) {}

    Rectangle::Rectangle(vec2f position, vec2f size) {
        rectangle.color = { 255, 255, 255, 255 };
        rectangle.position = { position.x, position.y };
        rectangle.size = { size.x, size.y };
    }

    void Rectangle::set_position(vec2f position) {
        rectangle.position = { position.x, position.y };
    }

    void Rectangle::set_color(Color color) {
        rectangle.color = { color.r, color.g, color.b, color.a };
    }

    vec2f Rectangle::get_position() const {
        return { rectangle.position.x, rectangle.position.y };
    }

    void Rectangle::set_size(vec2f size) {
        rectangle.size = { size.x, size.y };
    }

    vec2f Rectangle::get_size() const {
        return { rectangle.size.x, rectangle.size.y };
    }

    void Rectangle::draw(Window&) {
        mgl_rectangle_draw(mgl_get_context(), &rectangle);
    }
}