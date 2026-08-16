#include "../../include/gui/GsrPage.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    static const float button_spacing_scale = 0.015f;

    GsrPage::GsrPage(const char *top_text, const char *bottom_text) :
        top_text(top_text, get_theme().title_font_desc.c_str()),
        bottom_text(bottom_text, get_theme().title_font_desc.c_str())
    {
        const float margin = 0.02f;
        set_margins(margin, margin, margin, margin);
    }

    bool GsrPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            if(!button_item.button->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const mgl::vec2f content_page_position = get_content_position() + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, content_page_position))
                return false;
        }

        // Process widgets by visibility (backwards)
        return widgets.for_each_reverse([selected_widget, &window, &event, content_page_position](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, content_page_position))
                    return false;
            }
            return true;
        });
    }

    void GsrPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_color_theme().page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(content_page_size.x, get_border_size()).floor());
        border.set_position(content_page_position);
        border.set_color(get_color_theme().tint_color);
        window.draw(border);

        draw_page_label(window, content_page_position);
        draw_buttons(window, content_page_position, content_page_size);

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        draw_children(window, content_page_position + mgl::vec2f(margin_left, get_border_size() + margin_top).floor());
    }

    void GsrPage::draw_page_label(mgl::Window &window, mgl::vec2f body_pos) {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();

        mgl::Rectangle background(mgl::vec2f(window_size.x / 10, window_size.x / 10).floor());
        background.set_position(body_pos.floor() - mgl::vec2f(background.get_size().x + get_horizontal_spacing(), 0.0f).floor());
        background.set_color(mgl::Color(0, 0, 0, 255));
        window.draw(background);

        const int text_margin = background.get_size().y * 0.085;

        top_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - top_text.get_bounds().size.x * 0.5f, text_margin)).floor());
        window.draw(top_text);

        mgl::Sprite icon(&get_theme().settings_texture);
        icon.set_height((int)(background.get_size().y * 0.5f));
        icon.set_position((background.get_position() + background.get_size() * 0.5f - icon.get_size() * 0.5f).floor());
        window.draw(icon);

        bottom_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - bottom_text.get_bounds().size.x * 0.5f, background.get_size().y - bottom_text.get_bounds().size.y - text_margin)).floor());
        window.draw(bottom_text);
    }

    void GsrPage::draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size) {
        float offset_y = 0.0f;
        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            button_item.button->set_position(body_pos + mgl::vec2f(body_size.x + get_horizontal_spacing(), offset_y).floor());
            button_item.button->draw(window, mgl::vec2f(0.0f, 0.0f));
            offset_y +=  button_item.button->get_size().y + (button_spacing_scale * get_theme().window_height);
        }
    }

    void GsrPage::draw_children(mgl::Window &window, mgl::vec2f position) {
        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor({position.to_vec2i(), get_inner_size().to_vec2i()});

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, position);
        }

        if(selected_widget)
            selected_widget->draw(window, position);

        window.set_scissor(prev_scissor);
    }

    mgl::vec2f GsrPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = (window_size * mgl::vec2f(0.3333f, 0.7f)).floor();
        return content_page_size;
    }

    mgl::vec2f GsrPage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return get_size() - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void GsrPage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
    }

    void GsrPage::add_button(const std::string &text, const std::string &id, mgl::Color color) {
        auto button = std::make_unique<Button>(get_theme().title_font_desc.c_str(), text.c_str(),
            mgl::vec2f(get_theme().window_width / 10, get_theme().window_height / 15).floor(), color);
        button->set_border_scale(0.003f);
        button->on_click = [this, id]() {
            if(on_click)
                on_click(id);
        };
        buttons.push_back({ std::move(button), id });
    }

    float GsrPage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }

    float GsrPage::get_horizontal_spacing() const {
        return get_theme().window_width / 50;
    }

    mgl::vec2f GsrPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
    }
}