#include "../../include/gui/Tooltip.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    static const float padding_top_scale = 0.008f;
    static const float padding_bottom_scale = 0.008f;
    static const float padding_left_scale = 0.008f;
    static const float padding_right_scale = 0.008f;
    static const float accent_scale = 0.0025f;

    Tooltip::Tooltip(const char *font_desc) : label("", font_desc) {}

    bool Tooltip::on_event(mgl::Event&, mgl::Window&, mgl::vec2f) {
        return true;
    }

    void Tooltip::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = (window.get_mouse_position().to_vec2f() + offset).floor();

        const int padding_top = get_theme().window_height * padding_top_scale;
        const int padding_left = get_theme().window_height * padding_left_scale;
        const int accent_height = get_theme().window_height * accent_scale;
        const int icon_height = label.get_font_size()*2.0f;

        mgl::Rectangle background(get_size());
        background.set_position(draw_pos - mgl::vec2f(0.0f, background.get_size().y));
        background.set_color(mgl::Color(0, 0, 0));
        window.draw(background);

        mgl::Rectangle accent(background.get_position(), mgl::vec2f(background.get_size().x, accent_height).floor());
        accent.set_color(get_color_theme().tint_color);
        window.draw(accent);

        mgl::Sprite icon_sprite(&get_theme().info_texture, background.get_position() + mgl::vec2f(padding_left, accent_height + padding_top).floor());
        icon_sprite.set_height(icon_height);
        window.draw(icon_sprite);

        label.set_position(background.get_position() + mgl::vec2f(padding_left, accent_height + padding_top + icon_sprite.get_size().y).floor());
        window.draw(label);
    }

    mgl::vec2f Tooltip::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int padding_top = get_theme().window_height * padding_top_scale;
        const int padding_bottom = get_theme().window_height * padding_bottom_scale;
        const int padding_left = get_theme().window_height * padding_left_scale;
        const int padding_right = get_theme().window_height * padding_right_scale;
        const int accent_height = get_theme().window_height * accent_scale;
        const mgl::vec2f text_size = label.get_bounds().size.floor();
        const int icon_height = label.get_font_size()*2.0f;

        return mgl::vec2f(padding_left + text_size.x + padding_right, accent_height + padding_top + icon_height + text_size.y + padding_bottom).floor();
    }

    void Tooltip::set_text(std::string_view text) {
        label.set_string(text);
    }
}