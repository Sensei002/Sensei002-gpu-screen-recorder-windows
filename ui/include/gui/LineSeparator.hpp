#pragma once

#include "Widget.hpp"

namespace gsr {
    class LineSeparator : public Widget {
    public:
        enum class Orientation {
            HORIZONTAL
        };

        LineSeparator(Orientation orientation, float width);
        LineSeparator(const LineSeparator&) = delete;
        LineSeparator& operator=(const LineSeparator&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
    private:
        Orientation orientation;
        float width;
    };
}