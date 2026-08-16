#pragma once

#include <mglpp/system/vec.hpp>
#include <memory>
#include <string>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget {
        friend class StaticPage;
        friend class ScrollablePage;
        friend class List;
        friend class Page;
        friend class Subsection;
    public:
        enum class Alignment {
            START,
            CENTER,
            END
        };

        Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        virtual ~Widget();

        // Return true to allow other widgets to also process the event
        virtual bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void draw(mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void set_position(mgl::vec2f position);

        virtual mgl::vec2f get_position() const;
        virtual mgl::vec2f get_size() = 0;
        // This can be different from get_size, for example with ScrollablePage this excludes the margins
        virtual mgl::vec2f get_inner_size() { return get_size(); }

        void set_horizontal_alignment(Alignment alignment);
        void set_vertical_alignment(Alignment alignment);

        Alignment get_horizontal_alignment() const;
        Alignment get_vertical_alignment() const;

        void set_visible(bool visible);
        bool is_visible() const;

        Widget* get_parent_widget();

        void set_tooltip_text(std::string text);
        const std::string& get_tooltip_text() const;
        void handle_tooltip_event(mgl::Event &event, mgl::vec2f position, mgl::vec2f size);

        void *userdata = nullptr;
    protected:
        void set_widget_as_selected_in_parent();
        void remove_widget_as_selected_in_parent();
        bool has_parent_with_selected_child_widget() const;
    protected:
        mgl::vec2f position;
        Widget *parent_widget = nullptr;
        Widget *selected_child_widget = nullptr;

        Alignment horizontal_aligment = Alignment::START;
        Alignment vertical_aligment = Alignment::START;

        bool visible = true;
        std::string tooltip_text;
    };

    void add_widget_to_remove(std::unique_ptr<Widget> widget);
    void remove_widgets_to_be_removed();

    void set_current_tooltip(Widget *widget);
    void remove_as_current_tooltip(Widget *widget);
    void draw_tooltip(mgl::Window &window);
}