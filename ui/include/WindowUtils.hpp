#pragma once

#include <mglpp/system/vec.hpp>
#include <string>
#include <vector>
#include <optional>

#ifdef _WIN32
/* X11 types the UI uses as opaque handles. On Windows, Display is never
   dereferenced (x11_dpy is NULL) and Window is an HWND-sized handle. */
typedef struct _XDisplay Display;
typedef uint64_t Window;
typedef uint64_t Drawable;
typedef unsigned long Atom;
#else
#include <X11/Xlib.h>
#endif

struct wl_display;

namespace gsr {
    enum class WindowCaptureType {
        FOCUSED,
        CURSOR
    };

    struct Monitor {
        mgl::vec2i position; // Logical position on Wayland
        mgl::vec2i size;     // Logical size on Wayland
        std::string name;
    };

    struct DrawableGeometry {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    std::optional<std::string> get_window_title(Display *dpy, Window window);
    Window get_focused_window(Display *dpy, WindowCaptureType cap_type, bool fallback_cursor_focused = true);
    std::string window_title_utf8_sanitize(const char *str, int size);
    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type, bool fallback_cursor_focused = true);
    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, std::string_view ignore_window_title);
    std::string get_window_name_at_cursor_position(Display *dpy, std::string_view ignore_window_title);
    void set_window_size_not_resizable(Display *dpy, Window window, int width, int height);
    Window window_get_target_window_child(Display *display, Window window);
    unsigned char* window_get_property(Display *dpy, Window window, Atom property_type, const char *property_name, unsigned int *property_size);
    mgl::vec2i get_cursor_position(Display *dpy, Window *window);
    mgl::vec2i create_window_get_center_position(Display *display);
    std::string get_window_manager_name(Display *display);
    bool is_compositor_running(Display *dpy, int screen);
    std::vector<Monitor> get_monitors(Display *dpy);
    std::vector<Monitor> get_monitors_wayland(struct wl_display *dpy);
    void xi_grab_all_mouse_devices(Display *dpy);
    void xi_ungrab_all_mouse_devices(Display *dpy);
    void xi_warp_all_mouse_devices(Display *dpy, mgl::vec2i position);
    void window_set_fullscreen(Display *dpy, Window window, bool fullscreen);
    bool window_is_fullscreen(Display *display, Window window);
    bool get_drawable_geometry(Display *display, Drawable drawable, DrawableGeometry *geometry);
    std::optional<Monitor> get_monitor_by_window_center(Display *display, Window window);
    bool set_window_wm_state(Display *dpy, Window window, unsigned long atom);
    void make_window_click_through(Display *display, Window window);
    bool make_window_sticky(Display *dpy, Window window);
    bool hide_window_from_taskbar(Display *dpy, Window window);
}
