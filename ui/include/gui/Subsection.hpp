#pragma once

#include "Widget.hpp"
#include "Label.hpp"
#include <memory>

namespace gsr {
    class Subsection : public Widget {
    public:
        // If size width or height is 0 then the size in that direction is the size of the widgets
        Subsection(const char *title, std::unique_ptr<Widget> inner_widget, mgl::vec2f size);
        Subsection(const Subsection&) = delete;
        Subsection& operator=(const Subsection&) = delete;
        virtual ~Subsection() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;

        Widget* get_inner_widget();
        void set_bg_color(mgl::Color color);
    private:
        Label label;
        std::unique_ptr<Widget> inner_widget;
        mgl::vec2f size;
        mgl::Color bg_color{25, 30, 34};
    };
}