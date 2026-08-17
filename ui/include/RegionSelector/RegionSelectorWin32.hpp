#pragma once

#include <windows.h>

#include "RegionSelector.hpp"
#include <vector>
#include <optional>

namespace gsr {
    // Windows equivalent of RegionSelectorX11: a fullscreen layered overlay
    // window that lets the user drag a selection rectangle (REGION) or click
    // a window (WINDOW). The border is drawn with GDI on the overlay's DC;
    // handle_event(nullptr) pumps the overlay's own message queue from the
    // UI's main loop.
    class RegionSelectorWin32 : public RegionSelector {
    public:
        RegionSelectorWin32();
        ~RegionSelectorWin32() override;

        bool start(SelectionType selection_type, mgl::Color border_color) override;
        void stop() override;
        void cancel() override;
        bool is_started() const override;

        bool failed() const override;
        void handle_event(void *native_event) override;
        bool take_selection() override;
        bool take_canceled() override;
        Region get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const override;
        // Returns None if none is selected
        Window get_window_selection() const override;

        SelectionType get_selection_type() const override;
    private:
        void pump_messages();
        void on_left_button_down();
        void on_mouse_move();
        void on_left_button_up();
        void on_key_escape();
        void invalidate();
    private:
        HWND hwnd = nullptr;
        bool started = false;
        bool failed_ = false;

        Region region;
        bool selecting = false;
        bool selected = false;
        bool canceled = false;

        /* Virtual-screen coordinates of the overlay window. */
        int virtual_x = 0;
        int virtual_y = 0;

        uint32_t border_color = 0; /* 0x00RRGGBB */
        SelectionType selection_type = SelectionType::NONE;

        /* Tracked selection state. */
        bool mouse_down = false;
        int drag_start_x = 0;
        int drag_start_y = 0;
        int drag_cur_x = 0;
        int drag_cur_y = 0;

        friend LRESULT CALLBACK region_selector_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    };
}
