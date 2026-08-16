#include "../../include/gui/Page.hpp"

namespace gsr {
    Page::~Page() {
        widgets.for_each([this](std::unique_ptr<Widget> &widget) {
            if(widget->parent_widget == this)
                widget->parent_widget = nullptr;
            return true;
        }, true);
    }

    void Page::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }
}