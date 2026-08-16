#include "../../include/gui/Utils.hpp"
#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    static double frame_delta_seconds = 1.0;

    mgl::vec2i min_vec2i(mgl::vec2i a, mgl::vec2i b) {
        return { std::min(a.x, b.x), std::min(a.y, b.y) };
    }

    mgl::vec2i max_vec2i(mgl::vec2i a, mgl::vec2i b) {
        return { std::max(a.x, b.x), std::max(a.y, b.y) };
    }

    mgl::vec2i clamp_vec2i(mgl::vec2i value, mgl::vec2i min, mgl::vec2i max) {
        return min_vec2i(max, max_vec2i(value, min));
    }

    // TODO: Use vertices to make it one draw call
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size) {
        pos = pos.floor();
        size = size.floor();
        border_size = (int)border_size;
        // Green line at top
        {
            mgl::Rectangle rect({ size.x, border_size });
            rect.set_position(pos);
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at bottom
        {
            mgl::Rectangle rect({ size.x, border_size });
            rect.set_position(pos + mgl::vec2f(0.0f, size.y - border_size));
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at left
        {
            mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
            rect.set_position(pos + mgl::vec2f(0, border_size));
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at right
        {
            mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
            rect.set_position(pos + mgl::vec2f(size.x - border_size, border_size));
            rect.set_color(color);
            window.draw(rect);
        }
    }

    double get_frame_delta_seconds() {
        return frame_delta_seconds;
    }

    void set_frame_delta_seconds(double frame_delta) {
        frame_delta_seconds = frame_delta;
    }

    mgl::vec2f scale_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to) {
        if(std::abs(from.x) <= 0.0001f || std::abs(from.y) <= 0.0001f)
            return {0.0f, 0.0f};

        const double height_to_width_ratio = (double)from.y / (double)from.x;
        from.x = to.x;
        from.y = from.x * height_to_width_ratio;
        
        if(from.y > to.y) {
            const double width_height_ratio = (double)from.x / (double)from.y;
            from.y = to.y;
            from.x = from.y * width_height_ratio;
        }

        return from;
    }

    mgl::vec2f clamp_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to) {
        if(from.x > to.x || from.y > to.y)
            return scale_keep_aspect_ratio(from, to);
        else
            return from;
    }

    mgl::Scissor scissor_get_sub_area(mgl::Scissor parent, mgl::Scissor child) {
        const mgl::vec2i pos = clamp_vec2i(child.position, parent.position, parent.position + parent.size);
        return mgl::Scissor{
            pos,
            max_vec2i(mgl::vec2i(0, 0), min_vec2i(child.position + child.size - pos, parent.position + parent.size - pos))
        };
    }
}