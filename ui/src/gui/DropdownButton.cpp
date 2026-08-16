#include "../../include/gui/DropdownButton.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top_scale = 0.00694f;
    static const float padding_bottom_scale = 0.00694f;
    static const float padding_left_scale = 0.009259f;
    static const float padding_right_scale = 0.009259f;
    static const float icon_spacing_scale = 0.008f;
    static const float border_scale = 0.003f;

    DropdownButton::DropdownButton(const char *title_font_desc, const char *description_font_desc, const char *title, const char *description, mgl::Texture *icon_texture, mgl::vec2f size) :
        title_font_desc(title_font_desc),
        description_font_desc(description_font_desc),
        size(size),
        title(title, title_font_desc),
        description(description, description_font_desc)
    {
        if(icon_texture && icon_texture->is_valid()) {
            icon_sprite.set_texture(icon_texture);
            icon_sprite.set_height((int)(size.y * 0.45f));
        }
        this->description.set_color(mgl::Color(150, 150, 150));
    }

    bool DropdownButton::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(event.type == mgl::Event::MouseMoved) {
            const mgl::vec2f draw_pos = position + offset;
            const mgl::vec2f collision_margin(1.0f, 1.0f); // Makes sure that multiple buttons that are next to each other wont activate at the same time when the cursor is right between them
            mouse_inside = mgl::FloatRect(draw_pos + collision_margin, size - collision_margin).contains({ (float)event.mouse_move.x, (float)event.mouse_move.y });
        } else if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mouse_inside;

            if(show_dropdown && clicked_inside && mouse_inside_item == -1) {
                show_dropdown = false;
                remove_widget_as_selected_in_parent();
                return false;
            }

            show_dropdown = clicked_inside;

            if(show_dropdown)
                set_widget_as_selected_in_parent();
            else
                remove_widget_as_selected_in_parent();

            if(mouse_inside_item >= 0 && mouse_inside_item < (int)items.size()) {
                if(on_click)
                    on_click(items[mouse_inside_item].id);
                return false;
            }
        }
        return true;
    }

    void DropdownButton::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        update_if_dirty();

        const mgl::vec2f draw_pos = position + offset;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;
        const int icon_spacing = icon_spacing_scale * get_theme().window_height;
        const int border_size = border_scale * get_theme().window_height;

        if(show_dropdown) {
            // Background
            {
                mgl::Rectangle rect(size);
                rect.set_position(draw_pos);
                rect.set_color(mgl::Color(0, 0, 0, 255));
                window.draw(rect);
            }

            const mgl::Color border_color = get_color_theme().tint_color;

            // Green line at top
            {
                mgl::Rectangle rect({ size.x, (float)border_size });
                rect.set_position(draw_pos);
                rect.set_color(border_color);
                window.draw(rect);
            }
        } else if(mouse_inside) {
            // Background
            {
                mgl::Rectangle rect(size);
                rect.set_position(draw_pos);
                rect.set_color(mgl::Color(0, 0, 0, 255));
                window.draw(rect);
            }

            const mgl::Color border_color = get_color_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, size, border_color, border_size);
        } else {
            // Background
            mgl::Rectangle rect(size);
            rect.set_position(draw_pos);
            rect.set_color(mgl::Color(0, 0, 0, 180));
            window.draw(rect);
        }

        if(activated) {
            description.set_color(get_color_theme().tint_color);
            icon_sprite.set_color(get_color_theme().tint_color);
        } else {
            description.set_color(mgl::Color(150, 150, 150));
            icon_sprite.set_color(mgl::Color(255, 255, 255));
        }

        const int text_margin = size.y * 0.085;

        const auto title_bounds = title.get_bounds();
        title.set_position((draw_pos + mgl::vec2f(size.x * 0.5f - title_bounds.size.x * 0.5f, text_margin)).floor());
        window.draw(title);

        const auto description_bounds = description.get_bounds();
        description.set_position((draw_pos + mgl::vec2f(size.x * 0.5f - description_bounds.size.x * 0.5f, size.y - description_bounds.size.y - text_margin)).floor());
        window.draw(description);

        if(icon_sprite.get_texture()->is_valid()) {
            icon_sprite.set_position((draw_pos + size * 0.5f - icon_sprite.get_size() * 0.5f).floor());
            window.draw(icon_sprite);
        }

        mouse_inside_item = -1;
        if(show_dropdown) {
            const mgl::vec2i mouse_pos = window.get_mouse_position();

            mgl::Rectangle dropdown_bg(max_size);
            dropdown_bg.set_position(draw_pos + mgl::vec2f(0.0f, size.y));
            dropdown_bg.set_color(mgl::Color(0, 0, 0));
            window.draw(dropdown_bg);

            mgl::vec2f item_position = dropdown_bg.get_position();
            for(size_t i = 0; i < items.size(); ++i) {
                auto &item = items[i];
                const auto text_bounds = item.text.get_bounds();
                const float item_height = padding_top + text_bounds.size.y + padding_bottom;
                const mgl::vec2f item_size(max_size.x, item_height);

                if(i > 0) {
                    const float separator_height = std::max(1.0f, item_size.y * 0.05f);
                    mgl::Rectangle separator((item_position - mgl::vec2f(0.0f, separator_height * 0.5f)).floor(), mgl::vec2f(item_size.x, separator_height).floor());
                    separator.set_color(get_color_theme().page_bg_color);
                    window.draw(separator);
                }

                if(mouse_inside_item == -1 && item.enabled) {
                    const bool inside = mgl::FloatRect(item_position, item_size).contains({ (float)mouse_pos.x, (float)mouse_pos.y });
                    if(inside) {
                        draw_rectangle_outline(window, item_position, item_size, get_color_theme().tint_color, border_size);
                        mouse_inside_item = i;
                    }
                }

                float icon_offset = 0.0f;
                if(item.icon_texture) {
                    mgl::Sprite icon(item.icon_texture);
                    icon.set_height((int)(item_size.y * 0.4f));
                    icon.set_position((item_position + mgl::vec2f(padding_left, item_size.y * 0.5f - icon.get_size().y * 0.5f)).floor());
                    icon.set_color(item.enabled ? mgl::Color(255, 255, 255, 255) : mgl::Color(255, 255, 255, 80));
                    window.draw(icon);
                    icon_offset = icon.get_size().x + icon_spacing;
                }

                item.text.set_position((item_position + mgl::vec2f(padding_left + icon_offset, item_size.y * 0.5f - text_bounds.size.y * 0.5f)).floor());
                item.text.set_color(item.enabled ? mgl::Color(255, 255, 255, 255) : mgl::Color(255, 255, 255, 80));
                window.draw(item.text);

                const auto description_bounds = item.description_text.get_bounds();
                item.description_text.set_position((item_position + mgl::vec2f(item_size.x - description_bounds.size.x - padding_right, item_size.y * 0.5f - description_bounds.size.y * 0.5f)).floor());
                item.description_text.set_color(item.enabled ? mgl::Color(255, 255, 255, 120) : mgl::Color(255, 255, 255, 40));
                window.draw(item.description_text);

                item_position.y += item_size.y;
            }
        }
    }

    void DropdownButton::add_item(const std::string &text, const std::string &id, const std::string &description) {
        for(auto &item : items) {
            if(item.id == id)
                return;
        }
        items.push_back({mgl::Text(text, title_font_desc.c_str()), mgl::Text(description, description_font_desc.c_str()), nullptr, id});
        dirty = true;
    }

    void DropdownButton::set_item_label(std::string_view id, const std::string &new_label) {
        for(auto &item : items) {
            if(item.id == id) {
                item.text.set_string(new_label);
                return;
            }
        }
    }

    void DropdownButton::set_item_icon(std::string_view id, mgl::Texture *texture) {
        for(auto &item : items) {
            if(item.id == id) {
                item.icon_texture = texture;
                return;
            }
        }
    }

    void DropdownButton::set_item_description(std::string_view id, const std::string &new_description) {
        for(auto &item : items) {
            if(item.id == id) {
                item.description_text.set_string(new_description);
                return;
            }
        }
    }

    void DropdownButton::set_item_enabled(std::string_view id, bool enabled) {
        for(auto &item : items) {
            if(item.id == id) {
                item.enabled = enabled;
                return;
            }
        }
    }

    void DropdownButton::set_description(std::string description_text) {
        description.set_string(std::move(description_text));
    }

    void DropdownButton::set_activated(bool activated) {
        if(this->activated == activated)
            return;

        this->activated = activated;
    }

    void DropdownButton::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;

        max_size = { size.x, 0.0f };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            max_size.y += bounds.y + padding_top + padding_bottom;
        }
        dirty = false;
    }

    mgl::vec2f DropdownButton::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        update_if_dirty();
        return size;
    }
}
