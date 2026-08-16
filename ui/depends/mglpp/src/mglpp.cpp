#include "../include/mglpp/mglpp.hpp"
extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    Init::Init(WindowSystem window_system) {
        if(mgl_init((mgl_window_system)window_system) != 0)
            throw InitException();
    }

    Init::Init(struct wl_display *dpy) {
        if(mgl_init_with_wayland_display(dpy) != 0)
            throw InitException();
    }

    Init::~Init() {
        mgl_deinit();
    }

    bool Init::is_connected_to_display_server() {
        return mgl_is_connected_to_display_server();
    }

    void Init::ping_display_server() {
        return mgl_ping_display_server();
    }
}