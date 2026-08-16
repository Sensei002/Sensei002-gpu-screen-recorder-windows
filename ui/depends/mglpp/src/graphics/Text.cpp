#include "../../include/mglpp/graphics/Text.hpp"

#include <string.h>
#include <string>

extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    Text::Text() {
        mgl_text_init(&text, "", 0, "Sans 12");
        initialized = true;
    }

    Text::Text(std::string_view str, const char *font_desc) : Text(std::move(str), vec2f(0.0f, 0.0f), font_desc) {}

    Text::Text(std::string_view str, vec2f position, const char *font_desc) {
        mgl_text_init(&text, str.data(), str.size(), font_desc);
        mgl_text_set_position(&text, { position.x, position.y });
        initialized = true;
    }

    Text::Text(const Text &other) {
        *this = other;
        initialized = true;
    }

    Text& Text::operator=(const Text &other) {
        if(initialized)
            mgl_text_deinit(&text);
        mgl_text_copy(&other.text, &text);
        return *this;
    }

    Text::Text(Text &&other) {
        if(initialized)
            mgl_text_deinit(&text);
        text = other.text;
        memset(&other.text, 0, sizeof(other.text));
    }

    Text::~Text() {
        mgl_text_deinit(&text);
        initialized = false;
    }

    void Text::set_position(vec2f position) {
        mgl_text_set_position(&text, {position.x, position.y});
    }

    vec2f Text::get_position() const {
        return { text.position.x, text.position.y };
    }

    int Text::get_font_size() const {
        return mgl_text_get_font_size(&text);
    }

    void Text::set_color(Color color) {
        mgl_text_set_color(&text, {color.r, color.g, color.b, color.a});
    }

    void Text::set_wrap_width(int width) {
        mgl_text_set_wrap_width(&text, width);
    }

    void Text::set_max_rows(unsigned int max_rows) {
        mgl_text_set_max_rows(&text, max_rows);
    }

    FloatRect Text::get_bounds() {
        mgl_vec2i size = mgl_text_get_size(&text);
        FloatRect rect(get_position(), { (float)size.x, (float)size.y });
        return rect;
    }

    void Text::set_string(std::string_view str) {
        mgl_text_set_string(&text, str.data(), str.size());
    }

    std::string_view Text::get_string() const {
        int len = 0;
        const char *str = mgl_text_get_string(&text, &len);
        return {str, (size_t)len};
    }

    void Text::append_string(std::string_view str) {
        std::string text{get_string()};
        text += str;
        set_string(text);
    }

    char Text::operator[](size_t index) const {
        return get_string()[index];
    }

    vec2f Text::find_character_pos(size_t index) const {
        const mgl_vec2f pos = mgl_text_find_character_pos(&text, index);
        return vec2f(pos.x, pos.y);
    }

    mgl_index_codepoint_pair Text::find_closest_caret_index_by_position(mgl::vec2f position) const {
        return mgl_text_find_closest_caret_index_by_position(&text, {position.x, position.y});
    }

    void Text::draw(Window&) {
        mgl_text_draw(&text);
    }

    // static
    int Text::get_font_size_from_font_description(const char *font_desc) {
        return mgl_text_get_font_size_from_font_description(font_desc);
    }

    // static
    std::string Text::get_default_font_name() {
        char default_font_name[128];
        mgl_text_get_default_font_name(default_font_name, sizeof(default_font_name));
        return default_font_name;
    }
}
