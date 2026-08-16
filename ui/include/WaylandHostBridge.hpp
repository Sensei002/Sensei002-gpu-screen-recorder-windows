#pragma once

struct wl_display;

namespace gsr {
    struct wl_display *wayland_connect_to_host();
}
