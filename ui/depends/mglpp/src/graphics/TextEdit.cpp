#include "../../include/mglpp/graphics/TextEdit.hpp"
#include "../../include/mglpp/window/Event.hpp"

#include <string.h>

extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    TextEdit::TextEdit(const char *font_str, int wrap_width) {
        mgl_text_edit_init(&text_edit, font_str, wrap_width);
        initialized = true;
    }

    TextEdit::TextEdit(TextEdit &&other) : text_edit(other.text_edit), draw_color(other.draw_color), initialized(other.initialized) {
        memset(&other.text_edit, 0, sizeof(other.text_edit));
        other.initialized = false;
    }

    TextEdit& TextEdit::operator=(TextEdit &&other) {
        if(initialized)
            mgl_text_edit_deinit(&text_edit);
        text_edit = other.text_edit;
        draw_color = other.draw_color;
        initialized = other.initialized;
        memset(&other.text_edit, 0, sizeof(other.text_edit));
        other.initialized = false;
        return *this;
    }

    TextEdit::~TextEdit() {
        if(initialized)
            mgl_text_edit_deinit(&text_edit);
        initialized = false;
    }

    void TextEdit::set_position(vec2f position) {
        mgl_text_edit_set_position(&text_edit, {position.x, position.y});
    }

    vec2f TextEdit::get_position() const {
        const mgl_vec2f pos = mgl_text_edit_get_position(const_cast<mgl_text_edit*>(&text_edit));
        return {pos.x, pos.y};
    }

    void TextEdit::set_color(Color color) {
        draw_color = color;
    }

    void TextEdit::set_single_paragraph_mode(bool setting) {
        mgl_text_edit_set_single_paragraph_mode(&text_edit, setting);
    }

    void TextEdit::set_number_mode(bool enabled, int min_val, int max_val) {
        mgl_text_edit_set_number_mode(&text_edit, enabled, min_val, max_val);
    }

    void TextEdit::clamp_number() {
        mgl_text_edit_clamp_number(&text_edit);
    }

    void TextEdit::set_mask_char(uint32_t mask_char) {
        mgl_text_edit_set_mask_char(&text_edit, mask_char);
    }

    void TextEdit::set_masked(bool masked) {
        mgl_text_edit_set_masked(&text_edit, masked);
    }

    bool TextEdit::is_masked() const {
        return mgl_text_edit_is_masked(&text_edit);
    }

    void TextEdit::set_text(const char *text) {
        mgl_text_edit_set_text(&text_edit, text);
    }

    void TextEdit::insert(std::string_view str) {
        mgl_text_edit_insert(&text_edit, str.data(), (int)str.size());
    }

    void TextEdit::delete_text(int direction, int count) {
        mgl_text_edit_delete(&text_edit, direction, count);
    }

    std::string_view TextEdit::get_text() const {
        const char *str = mgl_text_edit_text(&text_edit);
        int len = mgl_text_edit_len(&text_edit);
        return {str, (size_t)len};
    }

    int TextEdit::get_length() const {
        return mgl_text_edit_len(&text_edit);
    }

    mgl_range TextEdit::get_selection() const {
        return mgl_text_edit_get_selection(&text_edit);
    }

    void TextEdit::move_left(bool extend_selection) {
        mgl_text_edit_move_left(&text_edit, extend_selection);
    }

    void TextEdit::move_right(bool extend_selection) {
        mgl_text_edit_move_right(&text_edit, extend_selection);
    }

    void TextEdit::move_word_left(bool extend_selection) {
        mgl_text_edit_move_word_left(&text_edit, extend_selection);
    }

    void TextEdit::move_word_right(bool extend_selection) {
        mgl_text_edit_move_word_right(&text_edit, extend_selection);
    }

    void TextEdit::delete_word(int direction) {
        mgl_text_edit_delete_word(&text_edit, direction);
    }

    void TextEdit::move_vertical(int direction, bool extend_selection) {
        mgl_text_edit_move_vertical(&text_edit, direction, extend_selection);
    }

    void TextEdit::move_home(bool extend_selection) {
        mgl_text_edit_move_home(&text_edit, extend_selection);
    }

    void TextEdit::move_end(bool extend_selection) {
        mgl_text_edit_move_end(&text_edit, extend_selection);
    }

    void TextEdit::set_caret_from_mouse(int mouse_x, int mouse_y, bool extend_selection) {
        mgl_text_edit_set_caret_from_mouse(&text_edit, mouse_x, mouse_y, extend_selection);
    }

    bool TextEdit::is_focused() const {
        return mgl_text_edit_is_focused(&text_edit);
    }

    void TextEdit::set_focused(bool focused) {
        mgl_text_edit_set_focused(&text_edit, focused);
    }

    bool TextEdit::handle_event(const Event &event) {
        return mgl_text_edit_handle_event(&text_edit, reinterpret_cast<const mgl_event*>(&event));
    }

    void TextEdit::sync() {
        mgl_text_edit_sync(&text_edit);
    }

    vec2i TextEdit::get_size(bool use_wrap_width) {
        const mgl_vec2i size = mgl_text_edit_get_size(&text_edit, use_wrap_width);
        return {size.x, size.y};
    }

    void TextEdit::set_margins(int left, int top, int right, int bottom) {
        mgl_text_edit_set_margins(&text_edit, left, top, right, bottom);
    }

    void TextEdit::draw(Window&) {
        mgl_text_edit_draw(&text_edit, {draw_color.r, draw_color.g, draw_color.b, draw_color.a});
    }
}
