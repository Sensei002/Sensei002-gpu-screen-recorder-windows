#pragma once

#include "RegionSelector.hpp"
#include <memory>

namespace gsr {
    class RegionSelectorWayland : public RegionSelector {
    public:
        // Returns true if the given wayland display advertises wlr-layer-shell-v1,
        // which is required by this implementation. `dpy` is left in the same state
        // it was given (a single registry roundtrip is performed on it).
        static bool is_supported(struct wl_display *dpy);

        explicit RegionSelectorWayland(struct wl_display *dpy);
        ~RegionSelectorWayland() override;

        bool start(SelectionType selection_type, mgl::Color border_color) override;
        void stop() override;
        void cancel() override;
        bool is_started() const override;

        bool failed() const override;
        void handle_event(void *native_event) override;
        bool take_selection() override;
        bool take_canceled() override;
        Region get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const override;
        // Always returns None on Wayland; window enumeration is not part of core Wayland.
        Window get_window_selection() const override;

        SelectionType get_selection_type() const override;

        struct Impl;
    private:
        std::unique_ptr<Impl> impl;
    };
}
