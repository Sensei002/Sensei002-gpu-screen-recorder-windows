#include "../../include/gui/Label.hpp"
#include <mglpp/window/Window.hpp>

namespace gsr {
    Label::Label(const char *font_desc, const char *text, mgl::Color color) : text(text, font_desc) {
        this->text.set_color(color);
    }

    bool Label::on_event(mgl::Event&, mgl::Window&, mgl::vec2f) {
        return true;
    }

    void Label::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        text.set_position((position + offset).floor());
        window.draw(text);
    }

    void Label::set_text(std::string_view str) {
        text.set_string(str);
    }

    std::string_view Label::get_text() const {
        return text.get_string();
    }

    mgl::vec2f Label::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return text.get_bounds().size;
    }

    // Set to 0 to disable
    void Label::set_wrap_width(int width) {
        text.set_wrap_width(width);
    }

    // Set to 0 to disable
    void Label::set_max_rows(int max_rows) {
        text.set_max_rows(max_rows);
    }

    int Label::get_font_size() const {
        return text.get_font_size();
    }
}