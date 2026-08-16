#pragma once

#include "Page.hpp"

namespace gsr {
    class StaticPage : public Page {
    public:
        StaticPage(mgl::vec2f size);
        StaticPage(const StaticPage&) = delete;
        StaticPage& operator=(const StaticPage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
    private:
        mgl::vec2f size;
    };
}