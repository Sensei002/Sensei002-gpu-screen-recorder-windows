#pragma once

#include "CursorTracker.hpp"

struct wl_display;

namespace gsr {
    class CursorTrackerDrm : public CursorTracker {
    public:
        CursorTrackerDrm(const char *card_path, struct wl_display *wayland_dpy);
        CursorTrackerDrm(const CursorTrackerDrm&) = delete;
        CursorTrackerDrm& operator=(const CursorTrackerDrm&) = delete;
        ~CursorTrackerDrm();

        void update() override;
        std::optional<CursorInfo> get_latest_cursor_info() override;
    private:
        int drm_fd = -1;
        mgl::vec2i latest_cursor_position; // Position of the cursor within the monitor
        int latest_crtc_id = -1;
        struct wl_display *wayland_dpy = nullptr;
    };
}