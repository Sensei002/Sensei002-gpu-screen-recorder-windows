#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Text.hpp>
#include <vector>
#include <functional>

namespace gsr {
    class RadioButton : public Widget {
    public:
        enum class Orientation {
            VERTICAL,
            HORIZONTAL
        };

        RadioButton(const char *font_desc, Orientation orientation);
        RadioButton(const RadioButton&) = delete;
        RadioButton& operator=(const RadioButton&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_item(const std::string &text, const std::string &id);
        void set_selected_item(std::string_view id, bool trigger_event = true, bool trigger_event_even_if_selection_not_changed = true);
        std::string_view get_selected_id() const;
        std::string_view get_selected_text() const;

        mgl::vec2f get_size() override;

        // Return false to revert the change
        std::function<bool(std::string_view text, std::string_view id)> on_selection_changed;
    private:
        void update_if_dirty();
    private:
        struct Item {
            mgl::Text text;
            std::string id;
        };

        std::string font_desc;
        Orientation orientation;
        std::vector<Item> items;
        size_t selected_item = 0;
        bool dirty = true;
        mgl::vec2f size;
    };
}