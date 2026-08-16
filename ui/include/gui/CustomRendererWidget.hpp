#pragma once

#include "Widget.hpp"

#include <functional>

namespace gsr {
    class CustomRendererWidget : public Widget {
    public:
        CustomRendererWidget(mgl::vec2f size);
        CustomRendererWidget(const CustomRendererWidget&) = delete;
        CustomRendererWidget& operator=(const CustomRendererWidget&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_size(mgl::vec2f size);

        std::function<void(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size)> draw_handler;
        // Return true to allow other widgets to handle events
        std::function<bool(mgl::Event &event, mgl::Window &window, mgl::vec2f pos, mgl::vec2f size)> event_handler;
    private:
        mgl::vec2f size;
    };
}