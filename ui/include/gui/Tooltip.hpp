#pragma once

#include "Widget.hpp"
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class Tooltip : public Widget {
    public:
        Tooltip(const char *font_desc);
        Tooltip(const Tooltip&) = delete;
        Tooltip& operator=(const Tooltip&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_text(std::string_view text);
    private:
        mgl::Text label;
    };
}