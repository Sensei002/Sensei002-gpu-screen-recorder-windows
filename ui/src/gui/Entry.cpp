#include "../../include/gui/Entry.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <string>

namespace gsr {
    static const float padding_top_scale    = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale   = 0.007f;
    static const float padding_right_scale  = 0.007f;
    static const float border_scale         = 0.0015f;

    Entry::Entry(const char *font_desc, const char *text, float max_width) :
        text_edit(font_desc, std::max(0.0f, max_width - (padding_left_scale * get_theme().window_height) - (padding_right_scale  * get_theme().window_height))),
        max_width(max_width)
    {
        const int padding_top    = padding_top_scale    * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left   = padding_left_scale   * get_theme().window_height;
        const int padding_right  = padding_right_scale  * get_theme().window_height;

        text_edit.set_single_paragraph_mode(true);
        text_edit.set_color(get_color_theme().text_color);
        text_edit.set_margins(padding_left, padding_top, padding_right, padding_bottom);
        set_text(text);
        text_edit.sync();
    }

    bool Entry::on_event(mgl::Event &event, mgl::Window &/*window*/, mgl::vec2f /*offset*/) {
        if(!visible)
            return true;

        if(text_edit.handle_event(event)) {
            if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left)
                return true;
            else if(event.type == mgl::Event::KeyPressed || event.type == mgl::Event::TextEntered) {
                if(on_changed)
                    on_changed(text_edit.get_text());
            }
            return false;
        }

        return true;
    }

    void Entry::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;

        const mgl::vec2f size = get_size();

        background.set_size(size);
        background.set_position(draw_pos.floor());
        background.set_color(text_edit.is_focused() ? mgl::Color(0, 0, 0, 255) : mgl::Color(0, 0, 0, 120));
        window.draw(background);

        if(text_edit.is_focused()) {
            const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
            draw_rectangle_outline(window, draw_pos.floor(), size.floor(), get_color_theme().tint_color, border_size);
        }

        text_edit.set_position(draw_pos.floor());
        window.draw(text_edit);
    }

    mgl::vec2f Entry::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2i text_size = text_edit.get_size(true);
        return text_size.to_vec2f();
    }

    void Entry::set_text(std::string_view str) {
        text_edit.set_text(std::string(str).c_str());
        if(on_changed)
            on_changed(text_edit.get_text());
    }

    std::string_view Entry::get_text() const {
        return text_edit.get_text();
    }

    void Entry::set_masked(bool masked) {
        text_edit.set_masked(masked);
    }

    bool Entry::is_masked() const {
        return text_edit.is_masked();
    }

    void Entry::set_number_mode(bool enabled, int min_val, int max_val) {
        text_edit.set_number_mode(enabled, min_val, max_val);
    }
}
