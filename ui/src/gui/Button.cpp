#include "../../include/gui/Button.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;

    // These are relative to the button size
    static const float padding_top_icon_scale = 0.25f;
    static const float padding_bottom_icon_scale = 0.25f;
    //static const float padding_left_icon_scale = 0.25f;
    static const float padding_right_icon_scale = 0.15f;

    Button::Button(const char *font_desc, const char *text, mgl::vec2f size, mgl::Color bg_color) :
        size(size), bg_color(bg_color), bg_hover_color(bg_color), text(text, font_desc)
    {

    }

    bool Button::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f item_size = get_size().floor();
        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mgl::FloatRect(position + offset, item_size).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
            if(clicked_inside) {
                if(on_click)
                    on_click();
                return false;
            }
        }
        return true;
    }

    void Button::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f item_size = get_size().floor();
        const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f()) && !has_parent_with_selected_child_widget();

        mgl::Rectangle background(item_size);
        background.set_position(draw_pos.floor());
        background.set_color(mouse_inside ? bg_hover_color : bg_color);
        window.draw(background);

        if(sprite.get_texture() && sprite.get_texture()->is_valid()) {
            scale_sprite_to_button_size();
            const int padding_left = padding_left_scale * get_theme().window_height;
            if(text.get_string().empty()) // Center
                sprite.set_position((background.get_position() + background.get_size() * 0.5f - sprite.get_size() * 0.5f).floor());
            else // Left
                sprite.set_position((draw_pos + mgl::vec2f(padding_left, background.get_size().y * 0.5f - sprite.get_size().y * 0.5f)).floor());
            window.draw(sprite);

            const int padding_icon_right = padding_right_icon_scale * get_button_height();
            text.set_position((sprite.get_position() + mgl::vec2f(sprite.get_size().x + padding_icon_right, sprite.get_size().y * 0.5f - text.get_bounds().size.y * 0.52f)).floor());
            window.draw(text);
        } else {
            text.set_position((draw_pos + item_size * 0.5f - text.get_bounds().size * 0.5f).floor());
            window.draw(text);
        }

        if(mouse_inside) {
            const mgl::Color outline_color = (bg_color == get_color_theme().tint_color) ? mgl::Color(255, 255, 255) : get_color_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, item_size, outline_color, std::max(1.0f, border_scale * get_theme().window_height));
        }
    }

    mgl::vec2f Button::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        const mgl::vec2f text_bounds = text.get_bounds().size;
        mgl::vec2f widget_size = size;

        if(widget_size.y < 0.0001f)
            widget_size.y = get_button_height();

        if(widget_size.x < 0.0001f) {
            widget_size.x = padding_left + text_bounds.x + padding_right;
            if(sprite.get_texture() && sprite.get_texture()->is_valid()) {
                scale_sprite_to_button_size();
                const int padding_icon_right = text_bounds.x > 0.001f ? padding_right_icon_scale * widget_size.y : 0.0f;
                widget_size.x += sprite.get_size().x + padding_icon_right;
            }
        }

        return widget_size;
    }

    void Button::set_border_scale(float scale) {
        border_scale = scale;
    }

    void Button::set_icon_padding_scale(float scale) {
        icon_padding_scale = scale;
    }

    void Button::set_bg_hover_color(mgl::Color color) {
        bg_hover_color = color;
    }

    void Button::set_icon(mgl::Texture *texture) {
        sprite.set_texture(texture);
    }

    std::string_view Button::get_text() const {
        return text.get_string();
    }

    void Button::set_text(std::string_view str) {
        text.set_string(str);
    }

    void Button::scale_sprite_to_button_size() {
        if(!sprite.get_texture() || !sprite.get_texture()->is_valid())
            return;

        const float widget_height = get_button_height();

        const int padding_icon_top = padding_top_icon_scale * icon_padding_scale * widget_height;
        const int padding_icon_bottom = padding_bottom_icon_scale * icon_padding_scale * widget_height;

        const float desired_height = widget_height - (padding_icon_top + padding_icon_bottom);
        sprite.set_height((int)desired_height);
    }

    float Button::get_button_height() {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;

        float widget_height = size.y;
        if(widget_height < 0.0001f)
            widget_height = padding_top + text.get_bounds().size.y + padding_bottom;

        return widget_height;
    }
}