#ifndef MGLPP_TEXT_HPP
#define MGLPP_TEXT_HPP

#include <string_view>
#include "Drawable.hpp"
#include "../system/FloatRect.hpp"
#include <stddef.h>
#include <string>

extern "C" {
#include <mgl/graphics/text.h>
}

namespace mgl {
    class Text : public Drawable {
    public:
        Text();
        Text(std::string_view str, const char *font_desc);
        Text(std::string_view str, vec2f position, const char *font_desc);
        Text(const Text &other);
        Text& operator=(const Text &other);
        Text(Text &&other);
        ~Text();

        void set_position(vec2f position) override;
        vec2f get_position() const override;

        int get_font_size() const;

        void set_color(Color color) override;
        // If |width| is 0 then the text has no wrap width
        void set_wrap_width(int width);
        // If |max_rows| is 0 then the text can display an unlimited amount of rows
        void set_max_rows(unsigned int max_rows);

        FloatRect get_bounds();
        void set_string(std::string_view str);
        std::string_view get_string() const;
        void append_string(std::string_view str);

        char operator[](size_t index) const;

        // Returns the visual position of a character from its index.
        // If the index is out of range, then the position of the end of the string is returned.
        // The index is the codepoint index, not the byte index.
        vec2f find_character_pos(size_t index) const;

        // Finds the ideal position for inserting a caret.
        // The |position| is in the same coordinate space as the text, so top left of the text is the texts position set with
        // |mgl_text_set_position|.
        mgl_index_codepoint_pair find_closest_caret_index_by_position(mgl::vec2f position) const;

        static int get_font_size_from_font_description(const char *font_desc);
        static std::string get_default_font_name();
    protected:
        void draw(Window &window) override;
    private:
        mgl_text text;
        bool initialized = false;
    };
}

#endif /* MGLPP_TEXT_HPP */
