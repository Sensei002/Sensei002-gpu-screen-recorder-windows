#pragma once

#include "RegionSelector.hpp"
#include <vector>
#include <optional>

namespace gsr {
    class RegionSelectorX11 : public RegionSelector {
    public:
        explicit RegionSelectorX11(Display *dpy);
        ~RegionSelectorX11() override;

        bool start(SelectionType selection_type, mgl::Color border_color) override;
        void stop() override;
        void cancel() override;
        bool is_started() const override;

        bool failed() const override;
        void handle_event(void *native_event) override;
        bool take_selection() override;
        bool take_canceled() override;
        Region get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const override;
        Window get_window_selection() const override;

        SelectionType get_selection_type() const override;
    private:
        void on_button_press(const void *de);
        void on_button_release(const void *de);
        void on_mouse_motion(const void *de);
    private:
        Display *dpy = nullptr; // not owned
        bool started = false;
        unsigned long region_window = 0;
        unsigned long cursor_window = 0;
        unsigned long region_window_colormap = 0;
        int xi_opcode = 0;
        GC region_gc = nullptr;
        GC cursor_gc = nullptr;

        Region region;
        bool selecting_region = false;
        bool selected = false;
        bool canceled = false;
        bool is_wayland = false;
        std::vector<Monitor> monitors;
        std::vector<RegionWindow> windows; // First window is the window that is on top
        std::optional<RegionWindow> focused_window;
        mgl::vec2i cursor_pos;

        SelectionType selection_type = SelectionType::NONE;
    };
}
