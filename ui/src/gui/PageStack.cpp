#include "../../include/gui/PageStack.hpp"

namespace gsr {
    PageStack::PageStack() {}
    
    bool PageStack::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        offset = position + offset;
        std::unique_ptr<Page> *top_page = widget_stack.back();
        if(top_page) {
            if(!(*top_page)->on_event(event, window, offset))
                return false;
        }

        return true;
    }

    void PageStack::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        offset = position + offset;
        std::unique_ptr<Page> *top_page = widget_stack.back();
        if(top_page)
            (*top_page)->draw(window, offset);
    }

    mgl::vec2f PageStack::get_size() {
        return {0.0f, 0.0f};
    }

    void PageStack::push(std::unique_ptr<Page> widget) {
        widget->on_navigate_to_page();
        widget_stack.push_back(std::move(widget));
    }

    void PageStack::pop() {
        std::unique_ptr<Page> *top_page = widget_stack.back();
        if(top_page) {
            (*top_page)->on_navigate_away_from_page();
            widget_stack.pop_back();
        }
    }

    Page* PageStack::top() {
        std::unique_ptr<Page> *top_page = widget_stack.back();
        if(top_page)
            return top_page->get();
        else
            return nullptr;
    }

    bool PageStack::empty() const {
        return widget_stack.empty();
    }
}