#pragma once

#include "Widget.hpp"
#include "../SafeVector.hpp"
#include <memory>

namespace gsr {
    class Page : public Widget {
    public:
        Page() = default;
        Page(const Page&) = delete;
        Page& operator=(const Page&) = delete;
        virtual ~Page() override;

        virtual void on_navigate_to_page() {}
        virtual void on_navigate_away_from_page() {}

        virtual void add_widget(std::unique_ptr<Widget> widget);
    protected:
        SafeVector<std::unique_ptr<Widget>> widgets;
    };
}