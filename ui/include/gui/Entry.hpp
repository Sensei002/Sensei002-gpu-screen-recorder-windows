#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/TextEdit.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    class Entry;

    class Entry : public Widget {
    public:
        Entry(const char *font_desc, const char *text, float max_width);
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_text(std::string_view str);
        std::string_view get_text() const;

        void set_masked(bool masked);
        bool is_masked() const;

        void set_number_mode(bool enabled, int min_val, int max_val);

        std::function<void(std::string_view text)> on_changed;
    private:
        mgl::Rectangle background;
        mgl::TextEdit text_edit;
        float max_width = 0.0f;
    };
}
