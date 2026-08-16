#ifndef MGLPP_MGLPP_HPP
#define MGLPP_MGLPP_HPP

#include <exception>

struct wl_display;

namespace mgl {
    enum class WindowSystem {
        NATIVE,  // Use X11 on X11 and Wayland on Wayland
        X11,     // Use X11 on X11 and XWayland on Wayland
        WAYLAND, // Use Wayland. If user runs on X11 then it fails to connect
    };

    class InitException : public std::exception {
    public:
        const char* what() const noexcept override {
            return "Failed to initialize mgl";
        }
    };

    class Init {
    public:
        // Throws InitException on failure.
        Init(WindowSystem window_system = WindowSystem::X11);
        Init(struct wl_display *dpy);
        ~Init();

        bool is_connected_to_display_server();
        void ping_display_server();
    };
}

#endif /* MGLPP_MGLPP_HPP */
