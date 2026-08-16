#include "../../include/gui/CheckBox.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float spacing_scale = 0.005f;
    static const float check_animation_speed = 10.0f;

    static mgl::Color color_multiply_ignore_alpha(mgl::Color color, float multiply) {
        return mgl::Color(color.r * multiply, color.g * multiply, color.b * multiply, color.a);
    }

    static float linear_interpolation(float source, float destination, float interpolation) {
        return source + (destination - source) * interpolation;
    }

    static mgl::Color interpolate_color_ignore_alpha(mgl::Color source, mgl::Color destination, float interpolation) {
        mgl::Color color;
        color.r = linear_interpolation(source.r, destination.r, interpolation);
        color.g = linear_interpolation(source.g, destination.g, interpolation);
        color.b = linear_interpolation(source.b, destination.b, interpolation);
        color.a = source.a;
        return color;
    }

    CheckBox::CheckBox(const char *font_desc, const char *text) :
        text(text, font_desc),
        background_sprite(&get_theme().checkbox_background_texture),
        circle_sprite(&get_theme().checkbox_circle_texture)
    {
        background_sprite.set_color(get_color_theme().tint_color);
        circle_sprite.set_color(get_color_theme().tint_color);
    }

    bool CheckBox::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        handle_tooltip_event(event, position + offset, get_size());

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mgl::FloatRect(position + offset, get_size()).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
            if(clicked_inside) {
                checked = !checked;
                if(on_changed)
                    on_changed(checked);
                return false;
            }
        }
        return true;
    }

    void CheckBox::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f checkbox_size = get_checkbox_size();

        apply_animation();

        const mgl::Color background_color_unchecked = color_multiply_ignore_alpha(mgl::Color(25, 30, 34), 0.6f);
        const mgl::Color background_color_checked = color_multiply_ignore_alpha(get_color_theme().tint_color, 0.6f);
        background_sprite.set_color(interpolate_color_ignore_alpha(background_color_unchecked, background_color_checked, toggle_animation_value));
        background_sprite.set_position(draw_pos.floor());
        window.draw(background_sprite);

        circle_sprite.set_height((int)background_sprite.get_size().y);
        const float circle_animation_x = linear_interpolation(0.0f, background_sprite.get_size().x - circle_sprite.get_size().x, toggle_animation_value);
        circle_sprite.set_position((draw_pos + mgl::vec2f(circle_animation_x, 0.0f)).floor());
        window.draw(circle_sprite);

        const mgl::vec2f text_bounds = text.get_bounds().size;
        text.set_position((draw_pos + mgl::vec2f(checkbox_size.x + spacing_scale * get_theme().window_height, checkbox_size.y * 0.5f - text_bounds.y * 0.5f)).floor());
        window.draw(text);
    }

    void CheckBox::apply_animation() {
        if(checked)
            toggle_animation_value += (get_frame_delta_seconds() * check_animation_speed);
        else
            toggle_animation_value -= (get_frame_delta_seconds() * check_animation_speed);

        if(toggle_animation_value < 0.0f)
            toggle_animation_value = 0.0f;
        else if(toggle_animation_value > 1.0f)
            toggle_animation_value = 1.0f;
    }

    mgl::vec2f CheckBox::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        mgl::vec2f size = text.get_bounds().size;
        const mgl::vec2f checkbox_size = get_checkbox_size();
        size.x += checkbox_size.x + spacing_scale * get_theme().window_height;
        size.y = std::max(size.y, checkbox_size.y);
        return size;
    }

    mgl::vec2f CheckBox::get_checkbox_size() {
        background_sprite.set_height((int)text.get_bounds().size.y);
        return background_sprite.get_size().floor();
    }

    void CheckBox::set_checked(bool checked, bool animated) {
        this->checked = checked;
        if(!animated)
            toggle_animation_value = checked ? 1.0f : 0.0f;
        if(on_changed)
            on_changed(checked);
    }

    bool CheckBox::is_checked() const {
        return checked;
    }
}