#include "../../include/gui/LineSeparator.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    static const float height_scale = 0.001f;

    static mgl::Color color_add_ignore_alpha(mgl::Color color, mgl::Color add) {
        return {
            (uint8_t)std::min((int)color.r + (int)add.r, 255),
            (uint8_t)std::min((int)color.g + (int)add.g, 255),
            (uint8_t)std::min((int)color.b + (int)add.b, 255),
            color.a
        };
    }

    LineSeparator::LineSeparator(Orientation orientation, float width) : orientation(orientation), width(width) {
        
    }

    bool LineSeparator::on_event(mgl::Event&, mgl::Window&, mgl::vec2f) {
        return true;
    }

    void LineSeparator::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = (position + offset).floor();
        const mgl::vec2f size = mgl::vec2f(width, std::max(1.0f, height_scale * get_theme().window_height)).floor();
        mgl::Rectangle rectangle(draw_pos, size);
        rectangle.set_color(color_add_ignore_alpha(mgl::Color(25, 30, 34), mgl::Color(30, 30, 30)));
        window.draw(rectangle);
    }

    mgl::vec2f LineSeparator::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f size = mgl::vec2f(width, std::max(1.0f, height_scale * get_theme().window_height)).floor();
        return size;
    }
}