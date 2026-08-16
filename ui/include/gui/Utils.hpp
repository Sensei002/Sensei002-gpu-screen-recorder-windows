#pragma once

#include <mglpp/system/vec.hpp>
#include <mglpp/graphics/Color.hpp>
#include <mglpp/window/Window.hpp>

namespace mgl {
    class Window;
}

namespace gsr {
    mgl::vec2i min_vec2i(mgl::vec2i a, mgl::vec2i b);
    mgl::vec2i max_vec2i(mgl::vec2i a, mgl::vec2i b);
    mgl::vec2i clamp_vec2i(mgl::vec2i value, mgl::vec2i min, mgl::vec2i max);

    // Inner border
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size);
    double get_frame_delta_seconds();
    void set_frame_delta_seconds(double frame_delta);
    mgl::vec2f scale_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to);
    mgl::vec2f clamp_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to);
    mgl::Scissor scissor_get_sub_area(mgl::Scissor parent, mgl::Scissor child);
}