#include "../../include/gui/Widget.hpp"
#include "../../include/gui/Tooltip.hpp"
#include "../../include/Theme.hpp"
#include <vector>

#include <mglpp/window/Event.hpp>

namespace gsr {
    static std::vector<std::unique_ptr<Widget>> widgets_to_remove;
    static Widget *current_tooltip_widget = nullptr;
    static std::unique_ptr<Tooltip> tooltip;

    static void set_current_tooltip_text(Widget *widget);

    Widget::Widget() {
        
    }

    Widget::~Widget() {
        remove_widget_as_selected_in_parent();
        remove_as_current_tooltip(this);
    }

    void Widget::set_position(mgl::vec2f position) {
        this->position = position;
    }

    mgl::vec2f Widget::get_position() const {
        return position;
    }

    void Widget::set_widget_as_selected_in_parent() {
        if(parent_widget) {
            parent_widget->selected_child_widget = this;
            parent_widget->set_widget_as_selected_in_parent();
        }
    }

    void Widget::remove_widget_as_selected_in_parent() {
        if(parent_widget && parent_widget->selected_child_widget == this) {
            parent_widget->selected_child_widget = nullptr;
            parent_widget->remove_widget_as_selected_in_parent();
        }
    }

    bool Widget::has_parent_with_selected_child_widget() const {
        // TODO: Optimize since this is called in draw function in widgets
        if(parent_widget) {
            if(parent_widget->selected_child_widget)
                return true;
            return parent_widget->has_parent_with_selected_child_widget();
        }
        return false;
    }

    void Widget::set_horizontal_alignment(Alignment alignment) {
        horizontal_aligment = alignment;
    }

    void Widget::set_vertical_alignment(Alignment alignment) {
        vertical_aligment = alignment;
    }

    Widget::Alignment Widget::get_horizontal_alignment() const {
        return horizontal_aligment;
    }

    Widget::Alignment Widget::get_vertical_alignment() const {
        return vertical_aligment;
    }

    void Widget::set_visible(bool visible) {
        this->visible = visible;
    }

    bool Widget::is_visible() const {
        return visible;
    }

    Widget* Widget::get_parent_widget() {
        return parent_widget;
    }

    void Widget::set_tooltip_text(std::string text) {
        tooltip_text = std::move(text);
        if(current_tooltip_widget == this)
            set_current_tooltip_text(current_tooltip_widget);
    }

    const std::string& Widget::get_tooltip_text() const {
        return tooltip_text;
    }

    void Widget::handle_tooltip_event(mgl::Event &event, mgl::vec2f position, mgl::vec2f size) {
        if(event.type == mgl::Event::MouseMoved) {
            if(mgl::FloatRect(position, size).contains(mgl::vec2f(event.mouse_move.x, event.mouse_move.y))) {
                set_current_tooltip(this);
            } else {
                remove_as_current_tooltip(this);
            }
        }
    }

    void add_widget_to_remove(std::unique_ptr<Widget> widget) {
        widgets_to_remove.push_back(std::move(widget));
    }

    void remove_widgets_to_be_removed() {
        for(size_t i = 0; i < widgets_to_remove.size(); ++i) {
            widgets_to_remove[i].reset();
        }
        widgets_to_remove.clear();
    }

    void set_current_tooltip(Widget *widget) {
        if(current_tooltip_widget == widget)
            return;

        set_current_tooltip_text(widget);
    }

    void remove_as_current_tooltip(Widget *widget) {
        if(current_tooltip_widget == widget)
            set_current_tooltip_text(nullptr);
    }

    void set_current_tooltip_text(Widget *widget) {
        if(widget && !widget->get_tooltip_text().empty()) {
            current_tooltip_widget = widget;
            if(!tooltip)
                tooltip = std::make_unique<Tooltip>(get_theme().body_font_desc.c_str());
            tooltip->set_text(current_tooltip_widget->get_tooltip_text());
        } else {
            current_tooltip_widget = nullptr;
            tooltip.reset();
        }
    }

    void draw_tooltip(mgl::Window &window) {
        if(!tooltip)
            return;

        if(!current_tooltip_widget->is_visible()) {
            set_current_tooltip(nullptr);
            return;
        }

        tooltip->draw(window, mgl::vec2f(0.0f, 0.0f));
    }
}