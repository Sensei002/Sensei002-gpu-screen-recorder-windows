#include "../../include/gui/StaticPage.hpp"

#include <mglpp/window/Window.hpp>

namespace gsr {
    StaticPage::StaticPage(mgl::vec2f size) : size(size) {}
    
    bool StaticPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;
        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, offset))
                return false;
        }

        // Process widgets by visibility (backwards)
        return widgets.for_each_reverse([selected_widget, &window, &event, offset](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, offset))
                    return false;
            }
            return true;
        });
    }

    void StaticPage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;
        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor({draw_pos.to_vec2i(), size.to_vec2i()});

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, position);
        }

        if(selected_widget)
            selected_widget->draw(window, offset);

        window.set_scissor(prev_scissor);
    }

    mgl::vec2f StaticPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }
}