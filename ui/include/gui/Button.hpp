#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>

namespace gsr {
    class Button : public Widget {
    public:
        // If width is 0 then the width of the text is used instead (with padding).
        // If height is 0 then the height of the text is used instead (with padding).
        Button(const char *font_desc, const char *text, mgl::vec2f size, mgl::Color bg_color);
        Button(const Button&) = delete;
        Button& operator=(const Button&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_border_scale(float scale);
        void set_icon_padding_scale(float scale);
        void set_bg_hover_color(mgl::Color color);
        void set_icon(mgl::Texture *texture);

        std::string_view get_text() const;
        void set_text(std::string_view str);

        std::function<void()> on_click;
    private:
        void scale_sprite_to_button_size();
        float get_button_height();
    private:
        mgl::vec2f size;
        mgl::Color bg_color;
        mgl::Color bg_hover_color;
        mgl::Text text;
        mgl::Sprite sprite;
        float border_scale = 0.0015f;
        float icon_padding_scale = 1.0f;
    };
}