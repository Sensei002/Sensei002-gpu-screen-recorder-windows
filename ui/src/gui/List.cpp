#include "../../include/gui/List.hpp"
#include "../../include/Theme.hpp"

static float floor(float f) {
    return (int)f;
}

namespace gsr {
    // TODO: Add homogeneous option, using a specified max size of this list.

    List::List(Orientation orientation, Alignment content_alignment) : orientation(orientation), content_alignment(content_alignment) {}
    
    bool List::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        // We want to store the selected child widget since it can change in the event loop below
        Widget *selected_widget = selected_child_widget;
        if(selected_widget) {
            if(!selected_widget->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        // Process widgets by visibility (backwards)
        return widgets.for_each_reverse([selected_widget, &event, &window](std::unique_ptr<Widget> &widget) {
            // Ignore offset because widgets are positioned with offset in ::draw, this solution is simpler
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                    return false;
            }
            return true;
        });
    }

    List::~List() {
        widgets.for_each([this](std::unique_ptr<Widget> &widget) {
            if(widget->parent_widget == this)
                widget->parent_widget = nullptr;
            return true;
        }, true);
    }

    void List::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        mgl::vec2f draw_pos = position + offset;
        offset = {0.0f, 0.0f};
        Widget *selected_widget = selected_child_widget;

        // TODO: Handle start/end alignment
        const mgl::vec2f size = get_size();
        const mgl::vec2f parent_inner_size = parent_widget ? parent_widget->get_inner_size() : mgl::vec2f(0.0f, 0.0f);

        const float spacing = floor(spacing_scale * get_theme().window_height);
        bool first_visible_widget = true;
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(size_t i = 0; i < widgets.size(); ++i) {
                    auto &widget = widgets[i];
                    if(!widget->visible)
                        continue;

                    if(!first_visible_widget)
                        draw_pos.y += spacing;
                    first_visible_widget = false;

                    const auto widget_size = widget->get_size();
                    // TODO: Do this parent widget alignment for horizontal alignment and for other types of widget alignment
                    // and other widgets.
                    // Also take this widget alignment into consideration in get_size.
                    if(widget->get_horizontal_alignment() == Widget::Alignment::CENTER && parent_inner_size.x > 0.001f)
                        offset.x = floor(parent_inner_size.x * 0.5f - widget_size.x * 0.5f);
                    else if(content_alignment == Alignment::CENTER)
                        offset.x = floor(size.x * 0.5f - widget_size.x * 0.5f);
                    else
                        offset.x = 0.0f;

                    widget->set_position(draw_pos + offset);
                    if(widget.get() != selected_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.y += widget_size.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(size_t i = 0; i < widgets.size(); ++i) {
                    auto &widget = widgets[i];
                    if(!widget->visible)
                        continue;

                    if(!first_visible_widget)
                        draw_pos.x += spacing;
                    first_visible_widget = false;

                    const auto widget_size = widget->get_size();
                    if(content_alignment == Alignment::CENTER)
                        offset.y = floor(size.y * 0.5f - widget_size.y * 0.5f);
                    else
                        offset.y = 0.0f;

                    widget->set_position(draw_pos + offset);
                    if(widget.get() != selected_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.x += widget_size.x;
                }
                break;
            }
        }

        if(selected_widget)
            selected_widget->draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    void List::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }

    void List::remove_widget(Widget *widget) {
        widgets.remove(widget);
    }

    void List::replace_widget(Widget *widget, std::unique_ptr<Widget> new_widget) {
        widgets.replace_item(widget, std::move(new_widget));
    }

    void List::clear() {
        widgets.clear();
    }

    void List::for_each_child_widget(std::function<bool(std::unique_ptr<Widget> &widget)> callback) {
        widgets.for_each(callback);
    }

    Widget* List::get_child_widget_by_index(size_t index) const {
        if(index < widgets.size())
            return widgets[index].get();
        else
            return nullptr;
    }

    size_t List::get_num_children() const {
        return widgets.size();
    }

    void List::set_spacing(float spacing) {
        spacing_scale = spacing;
    }

    // TODO: Cache result
    mgl::vec2f List::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        mgl::vec2f size;
        const float spacing = floor(spacing_scale * get_theme().window_height);
        bool first_visible_widget = true;
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(size_t i = 0; i < widgets.size(); ++i) {
                    auto &widget = widgets[i];
                    if(!widget->visible)
                        continue;

                    if(!first_visible_widget)
                        size.y += spacing;
                    first_visible_widget = false;

                    const auto widget_size = widget->get_size();
                    size.x = std::max(size.x, widget_size.x);
                    size.y += widget_size.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(size_t i = 0; i < widgets.size(); ++i) {
                    auto &widget = widgets[i];
                    if(!widget->visible)
                        continue;

                    if(!first_visible_widget)
                        size.x += spacing;
                    first_visible_widget = false;

                    const auto widget_size = widget->get_size();
                    size.x += widget_size.x;
                    size.y = std::max(size.y, widget_size.y);
                }
                break;
            }
        }
        return size;
    }
}