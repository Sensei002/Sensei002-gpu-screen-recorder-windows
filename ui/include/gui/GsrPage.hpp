#pragma once

#include "Page.hpp"
#include "Button.hpp"

#include <functional>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class GsrPage : public Page {
    public:
        GsrPage(const char *top_text, const char *bottom_text);
        GsrPage(const GsrPage&) = delete;
        GsrPage& operator=(const GsrPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;

        void set_margins(float top, float bottom, float left, float right);
        void add_button(const std::string &text, const std::string &id, mgl::Color color);

        std::function<void(const std::string &id)> on_click;
    private:
        void draw_page_label(mgl::Window &window, mgl::vec2f body_pos);
        void draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size);
        void draw_children(mgl::Window &window, mgl::vec2f position);

        float get_border_size() const;
        float get_horizontal_spacing() const;
        mgl::vec2f get_content_position();
        mgl::vec2f get_content_position_with_margin();
    private:
        struct ButtonItem {
            std::unique_ptr<Button> button;
            std::string id;
        };

        float margin_top_scale = 0.0f;
        float margin_bottom_scale = 0.0f;
        float margin_left_scale = 0.0f;
        float margin_right_scale = 0.0f;
        mgl::Text top_text;
        mgl::Text bottom_text;
        std::vector<ButtonItem> buttons;
    };
}