#include "../../include/gui/ComboBox.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <algorithm>
#include <cmath>
#include <assert.h>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float border_scale = 0.0015f;
    static const size_t max_visible_items = 10;
    static const int scroll_speed = 80;
    static const double scroll_update_speed = 10.0;
    static const float scrollbar_width_scale = 0.004f;

    ComboBox::ComboBox(const char *font_desc) : font_desc(font_desc), dropdown_arrow(&get_theme().combobox_arrow_texture) {
        assert(font_desc);
        font_size = mgl::Text::get_font_size_from_font_description(font_desc);
    }

    bool ComboBox::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(items.empty())
            return true;

        if(show_dropdown && event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left &&
            scrollbar_rect.contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
            moving_scrollbar_with_cursor = true;
            scrollbar_move_cursor_start_pos = mgl::vec2f(event.mouse_button.x, event.mouse_button.y);
            scrollbar_move_cursor_scroll_y_start = scroll_y;
            return false;
        }

        if(event.type == mgl::Event::MouseButtonReleased && moving_scrollbar_with_cursor) {
            moving_scrollbar_with_cursor = false;
            return false;
        }

        if(show_dropdown && event.type == mgl::Event::MouseWheelScrolled &&
            dropdown_list_rect.contains(window.get_mouse_position().to_vec2f())) {
            scroll_target_y += event.mouse_wheel_scroll.delta * scroll_speed;
            return false;
        }

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f mouse_pos = { (float)event.mouse_button.x, (float)event.mouse_button.y };
            mgl::vec2f item_size = get_size();

            if(show_dropdown && dropdown_list_rect.contains(mouse_pos)) {
                for(size_t i = 0; i < items.size(); ++i) {
                    Item &item = items[i];
                    item_size.y = get_item_height(item);
                    if(mgl::FloatRect(item.position, item_size).contains(mouse_pos) && item.enabled) {
                        const size_t prev_selected_item = selected_item;
                        selected_item = i;
                        show_dropdown = false;
                        dirty = true;
                        remove_widget_as_selected_in_parent();

                        if(selected_item != prev_selected_item && on_selection_changed)
                            on_selection_changed(item.text.get_string(), item.id);

                        return false;
                    }
                }
            }

            const mgl::vec2f draw_pos = position + offset;
            item_size = get_size();
            if(mgl::FloatRect(draw_pos, item_size).contains(mouse_pos)) {
                show_dropdown = !show_dropdown;
                if(show_dropdown) {
                    set_widget_as_selected_in_parent();
                    scroll_y = 0.0;
                    scroll_target_y = 0;
                } else {
                    remove_widget_as_selected_in_parent();
                }
            } else {
                show_dropdown = false;
                remove_widget_as_selected_in_parent();
            }
        }

        return true;
    }

    void ComboBox::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        //const mgl::Scissor scissor = window.get_scissor();
        update_if_dirty();
        const mgl::vec2f draw_pos = (position + offset).floor();
        //max_size.x = std::min((scissor.position.x + scissor.size.x) - draw_pos.x, max_size.x);

        if(show_dropdown)
            draw_selected(window, draw_pos);
        else
            draw_unselected(window, draw_pos);
    }

    void ComboBox::add_item(std::string_view text, const std::string &id, bool allow_duplicate) {
        if(!allow_duplicate) {
            for(const auto &item : items) {
                if(item.id == id)
                    return;
            }
        }

        items.push_back({mgl::Text(text, font_desc.c_str()), id, {0.0f, 0.0f}});
        items.back().text.set_wrap_width(items.back().text.get_font_size() * 35); // TODO: Make a proper solution
        items.back().text.set_max_rows(2);
        dirty = true;
    }

    void ComboBox::clear_items() {
        items.clear();
        selected_item = 0;
        show_dropdown = false;
        scroll_y = 0.0;
        scroll_target_y = 0;
        moving_scrollbar_with_cursor = false;
        dirty = true;
    }

    void ComboBox::set_selected_item(std::string_view id, bool trigger_event, bool trigger_event_even_if_selection_not_changed) {
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            if(item.id == id && item.enabled) {
                const size_t prev_selected_item = selected_item;
                selected_item = i;
                dirty = true;

                if(trigger_event && (trigger_event_even_if_selection_not_changed || selected_item != prev_selected_item) && on_selection_changed)
                    on_selection_changed(item.text.get_string(), item.id);

                break;
            }
        }
    }

    void ComboBox::set_item_enabled(std::string_view id, bool enabled) {
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            if(item.id == id) {
                item.enabled = enabled;
                item.text.set_color(item.enabled ? mgl::Color(255, 255, 255, 255) : mgl::Color(255, 255, 255, 80));
                if(selected_item == i) {
                    selected_item = 0;
                    show_dropdown = false;
                    dirty = true;
                }
                return;
            }
        }
    }

    std::string_view ComboBox::get_selected_id() const {
        if(items.empty()) {
            return "";
        } else {
            return items[selected_item].id;
        }
    }

    void ComboBox::draw_selected(mgl::Window &window, mgl::vec2f draw_pos) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;

        const mgl::Scissor scissor = window.get_scissor();
        const float scissor_top = scissor.position.y;
        const float scissor_bottom = scissor.position.y + scissor.size.y;

        const mgl::vec2f item_size = get_size();

        float items_total_height = 0.0f;
        float wanted_height = 0.0f;
        for(size_t i = 0; i < items.size(); ++i) {
            const float item_height = get_item_height(items[i]);
            items_total_height += item_height;
            if(i < max_visible_items)
                wanted_height += item_height;
        }

        const float space_below = scissor_bottom - (draw_pos.y + item_size.y);
        const float space_above = draw_pos.y - scissor_top;

        bool flip_up = false;
        float list_height = wanted_height;
        if(draw_pos.y + item_size.y + wanted_height <= scissor_bottom) {
            list_height = wanted_height;
        } else if(space_above > space_below) {
            flip_up = true;
            list_height = std::min(wanted_height, space_above);
        } else {
            list_height = std::min(wanted_height, space_below);
        }
        list_height = std::max(0.0f, list_height);

        const float list_window_top = flip_up ? (draw_pos.y - list_height) : (draw_pos.y + item_size.y);
        const float max_scroll = std::max(0.0f, items_total_height - list_height);
        const bool has_scrollbar = max_scroll > 0.001f;

        const float scrollbar_width = has_scrollbar ? get_scrollbar_width() : 0.0f;

        float scrollbar_height_absolute = list_height;
        if(has_scrollbar && items_total_height > 0.001f)
            scrollbar_height_absolute = std::max(10.0f, list_height * (list_height / items_total_height));
        const float scrollbar_empty_space = list_height - scrollbar_height_absolute;

        dropdown_list_rect = mgl::FloatRect(mgl::vec2f(draw_pos.x, list_window_top), mgl::vec2f(max_size.x, list_height));

        limit_scroll_cursor(window, max_scroll, scrollbar_empty_space);
        apply_scroll_animation();
        limit_scroll(max_scroll);

        const float region_top = std::min(draw_pos.y, list_window_top);
        const float region_bottom = std::max(draw_pos.y + item_size.y, list_window_top + list_height);
        mgl::Rectangle background(mgl::vec2f(draw_pos.x, region_top).floor(), mgl::vec2f(max_size.x, region_bottom - region_top).floor());
        background.set_color(mgl::Color(0, 0, 0));
        window.draw(background);

        if(selected_item < items.size()) {
            draw_item_outline(window, draw_pos, item_size);

            Item &selected_item_widget = items[selected_item];
            selected_item_widget.text.set_position(draw_pos + mgl::vec2f(padding_left, padding_top).floor());
            window.draw(selected_item_widget.text);
        }

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor(scissor_get_sub_area(prev_scissor, mgl::Scissor{
            mgl::vec2i((int)draw_pos.x, (int)list_window_top),
            mgl::vec2i((int)max_size.x, (int)list_height)
        }));

        bool cursor_inside = false;
        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();
        mgl::vec2f items_draw_pos = mgl::vec2f(draw_pos.x, list_window_top + (float)scroll_y);

        for(size_t i = 0; i < items.size(); ++i) {
            Item &item = items[i];
            const mgl::vec2f cur_item_size(max_size.x, get_item_height(item));

            item.position = items_draw_pos;

            if(!cursor_inside && item.enabled && dropdown_list_rect.contains(mouse_pos)) {
                const mgl::vec2f hover_size(cur_item_size.x - scrollbar_width, cur_item_size.y);
                cursor_inside = mgl::FloatRect(items_draw_pos, hover_size).contains(mouse_pos);
                if(cursor_inside) {
                    mgl::Rectangle item_background(items_draw_pos.floor(), hover_size.floor());
                    item_background.set_color(get_color_theme().tint_color);
                    window.draw(item_background);
                }
            }

            item.text.set_position((items_draw_pos + mgl::vec2f(padding_left, padding_top)).floor());
            window.draw(item.text);

            items_draw_pos.y += cur_item_size.y;
        }

        window.set_scissor(prev_scissor);

        scrollbar_rect = mgl::FloatRect();
        if(has_scrollbar) {
            float scroll_amount = max_scroll > 0.001f ? (float)(-scroll_y / max_scroll) : 0.0f;
            scroll_amount = std::min(1.0f, std::max(0.0f, scroll_amount));

            mgl::Rectangle scrollbar(
                mgl::vec2f(draw_pos.x + max_size.x - scrollbar_width, list_window_top + scroll_amount * scrollbar_empty_space).floor(),
                mgl::vec2f(scrollbar_width, scrollbar_height_absolute).floor());
            scrollbar.set_color(mgl::Color(200, 200, 200));
            window.draw(scrollbar);

            scrollbar_rect.position = scrollbar.get_position();
            scrollbar_rect.size = scrollbar.get_size();
        }
    }

    void ComboBox::draw_unselected(mgl::Window &window, mgl::vec2f draw_pos) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        mgl::vec2f item_size = get_size();
        mgl::Rectangle background(draw_pos.floor(), item_size.floor());
        background.set_color(mgl::Color(0, 0, 0, 120));
        window.draw(background);

        dropdown_arrow.set_height(get_dropdown_arrow_height());
        dropdown_arrow.set_position(draw_pos + mgl::vec2f(item_size.x - dropdown_arrow.get_size().x - padding_right, item_size.y * 0.5f - dropdown_arrow.get_size().y * 0.5f).floor());
        dropdown_arrow.set_color(mgl::Color(255, 255, 255, 30));
        window.draw(dropdown_arrow);

        if(selected_item < items.size()) {
            const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();
            const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(mouse_pos) && !has_parent_with_selected_child_widget();
            if(mouse_inside)
                draw_item_outline(window, draw_pos, item_size);

            Item &selected_item_widget = items[selected_item];
            selected_item_widget.text.set_position(draw_pos + mgl::vec2f(padding_left, padding_top).floor());
            window.draw(selected_item_widget.text);
        }
    }

    void ComboBox::draw_item_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
        const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
        const mgl::Color border_color = get_color_theme().tint_color;
        draw_rectangle_outline(window, pos.floor(), size.floor(), border_color, border_size);
    }

    void ComboBox::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        Item *selected_item_ptr = (selected_item < items.size()) ? &items[selected_item] : nullptr;
        max_size = { 0.0f, padding_top + padding_bottom + (selected_item_ptr ? selected_item_ptr->text.get_bounds().size.y : font_size) };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            max_size.x = std::max(max_size.x, bounds.x + padding_left + padding_right);
            max_size.y += padding_top + bounds.y + padding_bottom;
        }

        if(max_size.x <= 0.001f)
            max_size.x = 50.0f;

        max_size.x += padding_left + get_dropdown_arrow_height();
        dirty = false;
    }

    mgl::vec2f ComboBox::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        update_if_dirty();

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        Item *selected_item_ptr = (selected_item < items.size()) ? &items[selected_item] : nullptr;
        return { max_size.x, padding_top + padding_bottom + (selected_item_ptr ? selected_item_ptr->text.get_bounds().size.y : font_size) };
    }

    float ComboBox::get_dropdown_arrow_height() const {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return (font_size * 2.0f + padding_top + padding_bottom) * 0.4f;
    }

    float ComboBox::get_item_height(Item &item) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return padding_top + item.text.get_bounds().size.y + padding_bottom;
    }

    void ComboBox::apply_scroll_animation() {
        const double scroll_diff = scroll_target_y - scroll_y;
        if(std::abs(scroll_diff) < 0.1) {
            scroll_y = scroll_target_y;
        } else {
            const double frame_scroll_speed = std::min(1.0, get_frame_delta_seconds() * scroll_update_speed);
            scroll_y += (scroll_diff * frame_scroll_speed);
        }
    }

    void ComboBox::limit_scroll(float max_scroll) {
        if(scroll_y > 0.0)
            scroll_y = 0.0;
        else if(scroll_y < -max_scroll)
            scroll_y = -max_scroll;

        if(scroll_target_y > 0)
            scroll_target_y = 0;
        else if(scroll_target_y < -(int)max_scroll)
            scroll_target_y = -(int)max_scroll;
    }

    void ComboBox::limit_scroll_cursor(mgl::Window &window, float max_scroll, float scrollbar_empty_space) {
        if(!moving_scrollbar_with_cursor)
            return;

        const mgl::vec2f scrollbar_move_diff = window.get_mouse_position().to_vec2f() - scrollbar_move_cursor_start_pos;
        const double scroll_amount = scrollbar_empty_space > 0.001f ? scrollbar_move_diff.y / scrollbar_empty_space : 0.0;
        scroll_y = scrollbar_move_cursor_scroll_y_start - scroll_amount * max_scroll;
        if(scroll_y > 0.0)
            scroll_y = 0.0;
        else if(scroll_y < -max_scroll)
            scroll_y = -max_scroll;
        scroll_target_y = scroll_y;
    }

    float ComboBox::get_scrollbar_width() const {
        return std::max(5.0f, scrollbar_width_scale * get_theme().window_height);
    }
}