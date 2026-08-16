#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>

#include <functional>

namespace gsr {
    class CheckBox : public Widget {
    public:
        CheckBox(const char *font_desc, const char *text);
        CheckBox(const CheckBox&) = delete;
        CheckBox& operator=(const CheckBox&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_checked(bool checked, bool animated = false);
        bool is_checked() const;

        std::function<void(bool checked)> on_changed;
    private:
        void apply_animation();
        mgl::vec2f get_checkbox_size();
    private:
        mgl::Text text;
        mgl::Sprite background_sprite;
        mgl::Sprite circle_sprite;
        bool checked = false;
        float toggle_animation_value = 0.0f;
    };
}