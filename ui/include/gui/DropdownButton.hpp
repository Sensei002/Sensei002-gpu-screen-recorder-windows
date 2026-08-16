#pragma once

#include "Widget.hpp"
#include <string>
#include <functional>
#include <vector>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>

namespace gsr {
    class DropdownButton : public Widget {
    public:
        DropdownButton(const char *title_font_desc, const char *description_font_desc, const char *title, const char *description, mgl::Texture *icon_texture, mgl::vec2f size);
        DropdownButton(const DropdownButton&) = delete;
        DropdownButton& operator=(const DropdownButton&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_item(const std::string &text, const std::string &id, const std::string &description = "");
        void set_item_label(std::string_view id, const std::string &new_label);
        void set_item_icon(std::string_view id, mgl::Texture *texture);
        void set_item_description(std::string_view id, const std::string &new_description);
        void set_item_enabled(std::string_view id, bool enabled);

        void set_description(std::string description_text);
        void set_activated(bool activated);

        mgl::vec2f get_size() override;

        std::function<void(const std::string &id)> on_click;
    private:
        void update_if_dirty();
    private:
        struct Item {
            mgl::Text text;
            mgl::Text description_text;
            mgl::Texture *icon_texture = nullptr;
            std::string id;
            bool enabled = true;
        };

        std::vector<Item> items;
        std::string title_font_desc;
        std::string description_font_desc;
        mgl::vec2f size;
        bool mouse_inside = false;
        bool show_dropdown = false;
        bool dirty = true;
        mgl::vec2f max_size;
        int mouse_inside_item = -1;

        mgl::Text title;
        mgl::Text description;
        mgl::Sprite icon_sprite;

        bool activated = false;
    };
}