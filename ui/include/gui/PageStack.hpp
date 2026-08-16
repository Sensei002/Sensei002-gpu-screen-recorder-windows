#pragma once

#include "Widget.hpp"
#include "Page.hpp"
#include "../SafeVector.hpp"
#include <memory>

namespace gsr {
    class PageStack : public Widget {
    public:
        PageStack();
        PageStack(const PageStack&) = delete;
        PageStack& operator=(const PageStack&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void push(std::unique_ptr<Page> page);
        // This can be used even if the stack is empty
        void pop();
        // This can be used even if the stack is empty
        Page* top();
        bool empty() const;
    private:
        SafeVector<std::unique_ptr<Page>> widget_stack;
    };
}