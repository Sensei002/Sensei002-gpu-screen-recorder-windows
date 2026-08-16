#ifndef MGLPP_TEXT_EDIT_HPP
#define MGLPP_TEXT_EDIT_HPP

#include <string_view>
#include "Drawable.hpp"
#include "../system/vec.hpp"
#include <stddef.h>

extern "C" {
#include <mgl/graphics/text_edit.h>
}

namespace mgl {
    class Event;

    class TextEdit : public Drawable {
    public:
        TextEdit(const char *font_str, int wrap_width = 0);
        TextEdit(const TextEdit &other) = delete;
        TextEdit& operator=(const TextEdit &other) = delete;
        TextEdit(TextEdit &&other);
        TextEdit& operator=(TextEdit &&other);
        ~TextEdit();

        void set_position(vec2f position) override;
        vec2f get_position() const override;
        void set_color(Color color) override;

        void set_single_paragraph_mode(bool setting);
        void set_number_mode(bool enabled, int min_val, int max_val);
        void clamp_number();
        void set_mask_char(uint32_t mask_char);
        void set_masked(bool masked);
        bool is_masked() const;

        void set_text(const char *text);
        void insert(std::string_view str);
        void delete_text(int direction, int count);

        std::string_view get_text() const;
        int get_length() const;

        mgl_range get_selection() const;

        void move_left(bool extend_selection);
        void move_right(bool extend_selection);
        void move_word_left(bool extend_selection);
        void move_word_right(bool extend_selection);
        void delete_word(int direction);
        void move_vertical(int direction, bool extend_selection);
        void move_home(bool extend_selection);
        void move_end(bool extend_selection);

        void set_caret_from_mouse(int mouse_x, int mouse_y, bool extend_selection);

        bool is_focused() const;
        void set_focused(bool focused);

        bool handle_event(const Event &event);
        void sync();

        vec2i get_size(bool use_wrap_width);
        void set_margins(int left, int top, int right, int bottom);

    protected:
        void draw(Window &window) override;

    private:
        mgl_text_edit text_edit;
        Color draw_color;
        bool initialized = false;
    };
}

#endif /* MGLPP_TEXT_EDIT_HPP */
