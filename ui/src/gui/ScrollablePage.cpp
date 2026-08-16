#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const int scroll_speed = 80;
    static const double scroll_update_speed = 10.0;
    static const float scrollbar_width_scale = 0.004f;
    static const float scrollbar_spacing_scale = 0.004f;

    ScrollablePage::ScrollablePage(mgl::vec2f size) : size(size) {}

    ScrollablePage::~ScrollablePage() {
        widgets.for_each([this](std::unique_ptr<Widget> &widget) {
            if(widget->parent_widget == this)
                widget->parent_widget = nullptr;
            return true;
        }, true);
    }

    bool ScrollablePage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        offset = position + offset;

        const mgl::vec2f content_size = get_inner_size();
        const mgl::vec2i scissor_pos(offset.x, offset.y);
        const mgl::vec2i scissor_size(content_size.x, content_size.y);

        offset.y += scroll_y;
        Widget *selected_widget = selected_child_widget;

        if(event.type == mgl::Event::MouseButtonPressed && scrollbar_rect.contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
            set_widget_as_selected_in_parent();
            moving_scrollbar_with_cursor = true;
            scrollbar_move_cursor_start_pos = mgl::vec2f(event.mouse_button.x, event.mouse_button.y);
            scrollbar_move_cursor_scroll_y_start = scroll_y;
            return false;
        }

        if(event.type == mgl::Event::MouseButtonReleased && moving_scrollbar_with_cursor) {
            moving_scrollbar_with_cursor = false;
            remove_widget_as_selected_in_parent();
            return false;
        }

        // Pass release to children even if outside area, because we want to be able to release mouse when moved outside,
        // for example in Entry when selecting text
        if(event.type == mgl::Event::MouseButtonPressed/* || event.type == mgl::Event::MouseButtonReleased*/) {
            if(!mgl::IntRect(scissor_pos, scissor_size).contains({event.mouse_button.x, event.mouse_button.y}))
                return true;
        } else if(event.type == mgl::Event::MouseMoved) {
            if(!mgl::IntRect(scissor_pos, scissor_size).contains({event.mouse_move.x, event.mouse_move.y}))
                return true;
        }

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, offset))
                return false;
        }

        // Process widgets by visibility (backwards)
        const bool continue_events = widgets.for_each_reverse([selected_widget, &window, &event, offset](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, offset))
                    return false;
            }
            return true;
        });

        if(!continue_events)
            return false;

        if(event.type == mgl::Event::MouseWheelScrolled) {
            const double scroll = event.mouse_wheel_scroll.delta * scroll_speed;
            scroll_target_y += scroll;
            return false;
        }

        return true;
    }

    void ScrollablePage::draw(mgl::Window &window, mgl::vec2f offset) {
        scrollbar_rect = mgl::FloatRect();

        if(!visible || widgets.empty()) {
            reset_scroll();
            return;
        }

        const double scrollbar_width = get_scrollbar_width();
        const mgl::vec2f scrollbar_pos = position + offset + mgl::vec2f(size.x - scrollbar_width, 0.0f);

        offset = position + offset;

        const mgl::Scissor prev_scissor = window.get_scissor();

        const mgl::vec2f content_size = get_inner_size();
        const mgl_scissor new_scissor = {
            mgl_vec2i{(int)offset.x, (int)offset.y},
            mgl_vec2i{(int)content_size.x, (int)content_size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        Widget *selected_widget = selected_child_widget;
        offset.y += scroll_y;

        double child_top = 999999.0;
        double child_bottom = 0.0;
        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget) {
                // TODO: Do not draw if scrolled out of view
                widget->draw(window, offset);

                // TODO: Create a widget function to get the render area instead, which each widget should set (position + offset as start, and position + offset + size as end), this has to be done in the widgets to ensure that recursive rendering has correct position.
                // TODO: Do these calls before drawing, so that scroll limits can be set before drawing to prevent scrolling from overflowing for 1 frame.
                // To make that possible move inner_size calculation in FileChooserBody to the get_inner_size function.
                // That calculation can be done without looping folders.
                const mgl::vec2f widget_pos = widget->get_position() + offset;
                const mgl::vec2f widget_inner_size = widget->get_inner_size();

                child_top = std::min(child_top, (double)widget_pos.y);
                child_bottom = std::max(child_bottom, (double)widget_pos.y + (double)widget_inner_size.y);
            }
        }

        if(selected_widget) {
            selected_widget->draw(window, offset);

            const mgl::vec2f widget_pos = selected_widget->get_position() + offset;
            const mgl::vec2f widget_inner_size = selected_widget->get_inner_size();

            child_top = std::min(child_top, (double)widget_pos.y);
            child_bottom = std::max(child_bottom, (double)widget_pos.y + (double)widget_inner_size.y);
        }

        const double child_height = child_bottom - child_top;

        //fprintf(stderr, "scrollbar height: %f\n", scrollbar_height);

        // Debug output
        // mgl::Rectangle bottom(mgl::vec2f(size.x, 5.0f));
        // bottom.set_color(mgl::Color(255, 0, 0, 255));
        // bottom.set_position(mgl::vec2f(offset.x, child_bottom));
        // window.draw(bottom);

        // mgl::Rectangle bottom_d(mgl::vec2f(size.x, 5.0f));
        // bottom_d.set_color(mgl::Color(0, 255, 0, 255));
        // bottom_d.set_position(mgl::vec2f(offset.x, page_scroll_start.y + size.y - 5));
        // window.draw(bottom_d);

        apply_animation();
        limit_scroll(child_height);

        window.set_scissor(prev_scissor);

        double scrollbar_height = 1.0;
        if(child_height > 0.001)
            scrollbar_height = size.y / child_height;
        if(scrollbar_height > 1.0)
            scrollbar_height = 1.0;

        const double scrollbar_height_absolute = std::max(10.0, size.y * scrollbar_height);
        const double scrollbar_empty_space = size.y - scrollbar_height_absolute;

        if(scrollbar_height < 0.999) {
            double scroll_amount = -scroll_y / (child_height - size.y);
            if(scroll_amount < 0.0)
                scroll_amount = 0.0;
            else if(scroll_amount > 1.0)
                scroll_amount = 1.0;

            mgl::Rectangle scrollbar(
                (scrollbar_pos + mgl::vec2f(0.0f, scroll_amount * scrollbar_empty_space)).floor(),
                mgl::vec2f(scrollbar_width, scrollbar_height_absolute).floor());
            scrollbar.set_color(mgl::Color(200, 200, 200));
            window.draw(scrollbar);

            scrollbar_rect.position = scrollbar.get_position();
            scrollbar_rect.size = scrollbar.get_size();
        }

        limit_scroll_cursor(window, child_height, scrollbar_empty_space);
    }

    void ScrollablePage::apply_animation() {
        const double scroll_diff = scroll_target_y - scroll_y;
        if(std::abs(scroll_diff) < 0.1) {
            scroll_y = scroll_target_y;
        } else {
            const double frame_scroll_speed = std::min(1.0, get_frame_delta_seconds() * scroll_update_speed);
            scroll_y += (scroll_diff * frame_scroll_speed);
        }
    }

    void ScrollablePage::limit_scroll(double child_height) {
        const double scroll_bottom_limit = child_height - size.y;
        if(scroll_y > 0.001 || child_height < size.y) {
            scroll_y = 0;
            scroll_target_y = 0;
        } else if(scroll_y < -scroll_bottom_limit) {
            scroll_y = -scroll_bottom_limit;
            scroll_target_y = -scroll_bottom_limit;
        }
    }

    void ScrollablePage::limit_scroll_cursor(mgl::Window &window, double child_height, double scrollbar_empty_space) {
        if(moving_scrollbar_with_cursor) {
            const double scroll_bottom_limit = child_height - size.y;
            const mgl::vec2f scrollbar_move_diff = window.get_mouse_position().to_vec2f() - scrollbar_move_cursor_start_pos;
            const double scroll_amount = scrollbar_move_diff.y / scrollbar_empty_space;
            scroll_y = scrollbar_move_cursor_scroll_y_start - scroll_amount * (child_height - size.y);
            if(scroll_y > 0.0)
                scroll_y = 0.0;
            else if(scroll_y < -scroll_bottom_limit)
                scroll_y = -scroll_bottom_limit;
            scroll_target_y = scroll_y;
        }
    }

    mgl::vec2f ScrollablePage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    mgl::vec2f ScrollablePage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const float scrollbar_spacing = std::max(2.0f, scrollbar_spacing_scale * get_theme().window_height);
        return size - mgl::vec2f(get_scrollbar_width() + scrollbar_spacing, 0.0f);
    }

    void ScrollablePage::set_size(mgl::vec2f size) {
        this->size = size;
    }

    void ScrollablePage::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }

    void ScrollablePage::reset_scroll() {
        scroll_y = 0;
        scroll_target_y = 0;
    }

    float ScrollablePage::get_scrollbar_width() const {
        return std::max(5.0f, scrollbar_width_scale * get_theme().window_height);
    }
}