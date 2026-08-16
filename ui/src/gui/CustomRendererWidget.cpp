#include "../../include/gui/CustomRendererWidget.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/window/Window.hpp>

namespace gsr {
    CustomRendererWidget::CustomRendererWidget(mgl::vec2f size) : size(size) {}
    
    bool CustomRendererWidget::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible || !event_handler)
            return true;
        return event_handler(event, window, position + offset, size);
    }

    void CustomRendererWidget::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;

        const mgl::Scissor parent_scissor = window.get_scissor();
        const mgl::Scissor scissor = scissor_get_sub_area(parent_scissor, {draw_pos.to_vec2i(), size.to_vec2i()});
        window.set_scissor(scissor);

        if(draw_handler)
            draw_handler(window, draw_pos, size);

        window.set_scissor(parent_scissor);
    }

    mgl::vec2f CustomRendererWidget::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    void CustomRendererWidget::set_size(mgl::vec2f size) {
        this->size = size;
    }
}