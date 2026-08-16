#pragma once

#include "Widget.hpp"
#include "../SafeVector.hpp"
#include <memory>

namespace gsr {
    class List : public Widget {
    public:
        enum class Orientation {
            VERTICAL,
            HORIZONTAL
        };

        enum class Alignment {
            START,
            CENTER,
            END
        };

        List(Orientation orientation, Alignment content_alignment = Alignment::START);
        List(const List&) = delete;
        List& operator=(const List&) = delete;
        virtual ~List() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_widget(std::unique_ptr<Widget> widget);
        void remove_widget(Widget *widget);
        void replace_widget(Widget *widget, std::unique_ptr<Widget> new_widget);
        void clear();
        // Return true from |callback| to continue
        void for_each_child_widget(std::function<bool(std::unique_ptr<Widget> &widget)> callback);
        // Returns nullptr if index is invalid
        Widget* get_child_widget_by_index(size_t index) const;
        size_t get_num_children() const;

        void set_spacing(float spacing);

        mgl::vec2f get_size() override;
    protected:
        SafeVector<std::unique_ptr<Widget>> widgets;
        Orientation orientation;
        Alignment content_alignment;
        float spacing_scale = 0.009f;
    };
}