#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class Label : public Widget {
    public:
        Label(const char *font_desc, const char *text, mgl::Color color);
        Label(const Label&) = delete;
        Label& operator=(const Label&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_text(std::string_view str);
        std::string_view get_text() const;

        // Set to 0 to disable
        void set_wrap_width(int width);

        // Set to 0 to disable
        void set_max_rows(int max_rows);

        int get_font_size() const;
    private:
        mgl::Text text;
    };
}