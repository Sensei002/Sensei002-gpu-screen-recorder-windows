#include "../../include/gui/RadioButton.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float spacing_scale = 0.007f;
    static const float border_scale = 0.0015f;

    RadioButton::RadioButton(const char *font_desc, Orientation orientation) : font_desc(font_desc), orientation(orientation) {
        
    }

    bool RadioButton::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            mgl::vec2f draw_pos = position + offset;
            for(size_t i = 0; i < items.size(); ++i) {
                auto &item = items[i];
                const mgl::vec2f item_size = item.text.get_bounds().size + mgl::vec2f(padding_left + padding_right, padding_top + padding_bottom);

                const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y));
                if(mouse_inside) {
                    if(selected_item != i && on_selection_changed) {
                        if(!on_selection_changed(item.text.get_string(), item.id))
                            return false;
                    }

                    selected_item = i;
                    return false;
                }

                switch(orientation) {
                    case Orientation::VERTICAL:
                        draw_pos.y += item_size.y + spacing_scale * get_theme().window_height;
                        break;
                    case Orientation::HORIZONTAL:
                        draw_pos.x += item_size.x + spacing_scale * get_theme().window_height;
                        break;
                }
            }
        }
        return true;
    }

    void RadioButton::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        update_if_dirty();

        if(items.empty())
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        const bool can_select_item = !has_parent_with_selected_child_widget();

        mgl::vec2f draw_pos = position + offset;
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            const mgl::vec2f item_size = item.text.get_bounds().size + mgl::vec2f(padding_left + padding_right, padding_top + padding_bottom);
            mgl::Rectangle background(item_size.floor());
            background.set_position(draw_pos.floor());
            background.set_color(i == selected_item ? get_color_theme().tint_color : mgl::Color(0, 0, 0, 120));
            window.draw(background);

            const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f());
            if(can_select_item && mouse_inside) {
                const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
                const mgl::Color border_color = i == selected_item ? mgl::Color(255, 255, 255) : get_color_theme().tint_color;
                draw_rectangle_outline(window, draw_pos.floor(), item_size.floor(), border_color, border_size);
            }

            item.text.set_position((draw_pos + item_size * 0.5f - item.text.get_bounds().size * 0.5f).floor());
            window.draw(item.text);

            switch(orientation) {
                case Orientation::VERTICAL:
                    draw_pos.y += item_size.y + spacing_scale * get_theme().window_height;
                    break;
                case Orientation::HORIZONTAL:
                    draw_pos.x += item_size.x + spacing_scale * get_theme().window_height;
                    break;
            }
        }
    }

    void RadioButton::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        size = { 0.0f, 0.0f };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            switch(orientation) {
                case Orientation::VERTICAL:
                    size.x = std::max(size.x, bounds.x + padding_left + padding_right);
                    size.y += bounds.y + padding_top + padding_bottom;
                    break;
                case Orientation::HORIZONTAL:
                    size.x += bounds.x + padding_left + padding_right;
                    size.y = item.text.get_font_size()*2.0f + (float)padding_top + (float)padding_bottom;
                    break;
            }
        }

        if(items.size() > 1) {
            switch(orientation) {
                case Orientation::VERTICAL:
                    size.y += (items.size() - 1) * spacing_scale * get_theme().window_height;
                    break;
                case Orientation::HORIZONTAL:
                    size.x += (items.size() - 1) * spacing_scale * get_theme().window_height;
                    break;
            }
        }

        dirty = false;
    }

    mgl::vec2f RadioButton::get_size() {
        if(!visible || items.empty())
            return {0.0f, 0.0f};

        update_if_dirty();
        return size;
    }

    void RadioButton::add_item(const std::string &text, const std::string &id) {
        items.push_back({mgl::Text(text, font_desc.c_str()), id});
        dirty = true;
    }

    void RadioButton::set_selected_item(std::string_view id, bool trigger_event, bool trigger_event_even_if_selection_not_changed) {
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            if(item.id == id) {
                if(trigger_event && (trigger_event_even_if_selection_not_changed || selected_item != i) && on_selection_changed) {
                    if(!on_selection_changed(item.text.get_string(), item.id))
                        break;
                }

                selected_item = i;
                break;
            }
        }
    }

    std::string_view RadioButton::get_selected_id() const {
        if(items.empty()) {
            return "";
        } else {
            return items[selected_item].id;
        }
    }

    std::string_view RadioButton::get_selected_text() const {
        if(items.empty()) {
            return "";
        } else {
            return items[selected_item].text.get_string();
        }
    }
}