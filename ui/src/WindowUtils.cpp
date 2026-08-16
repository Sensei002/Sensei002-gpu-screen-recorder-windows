#include "../include/WindowUtils.hpp"
#include "../include/Utils.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shapeconst.h>
#include <X11/extensions/Xrandr.h>

#include <wayland-client.h>
#include "xdg-output-unstable-v1-client-protocol.h"

#include <mglpp/system/Rect.hpp>
#include <mglpp/system/Utf8.hpp>

extern "C" {
#include <mgl/window/window.h>
}

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#define MAX_PROPERTY_VALUE_LEN 4096

namespace gsr {
    struct WaylandOutput {
        uint32_t wl_name;
        struct wl_output *output;
        struct zxdg_output_v1 *xdg_output;
        mgl::vec2i pos;
        mgl::vec2i size;
        mgl::vec2i logical_pos;
        mgl::vec2i logical_size;
        int32_t transform;
        std::string name;
    };

    struct Wayland {
        std::vector<WaylandOutput> outputs;
        struct zxdg_output_manager_v1 *xdg_output_manager = nullptr;
    };

    static WaylandOutput* get_wayland_monitor_by_output(Wayland &wayland, struct wl_output *output) {
        for(WaylandOutput &monitor : wayland.outputs) {
            if(monitor.output == output)
                return &monitor;
        }
        return nullptr;
    }

    static void output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
        (void)wl_output;
        (void)phys_width;
        (void)phys_height;
        (void)subpixel;
        (void)make;
        (void)model;
        Wayland *wayland = (Wayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*wayland, wl_output);
        if(!monitor)
            return;

        monitor->pos.x = x;
        monitor->pos.y = y;
        monitor->transform = transform;
    }

    static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
        (void)wl_output;
        (void)flags;
        (void)refresh;
        Wayland *wayland = (Wayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*wayland, wl_output);
        if(!monitor)
            return;

        monitor->size.x = width;
        monitor->size.y = height;
    }

    static void output_handle_done(void *data, struct wl_output *wl_output) {
        (void)data;
        (void)wl_output;
    }

    static void output_handle_scale(void* data, struct wl_output *wl_output, int32_t factor) {
        (void)data;
        (void)wl_output;
        (void)factor;
    }

    static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
        (void)wl_output;
        Wayland *wayland = (Wayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*wayland, wl_output);
        if(!monitor)
            return;

        monitor->name = name;
    }

    static void output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
        (void)data;
        (void)wl_output;
        (void)description;
    }

    static const struct wl_output_listener output_listener = {
        output_handle_geometry,
        output_handle_mode,
        output_handle_done,
        output_handle_scale,
        output_handle_name,
        output_handle_description,
    };

    static void registry_add_object(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
        (void)version;
        Wayland *wayland = (Wayland*)data;
        if(strcmp(interface, wl_output_interface.name) == 0) {
            if(version < 4) {
                fprintf(stderr, "Warning: wl output interface version is < 4, expected >= 4\n");
                return;
            }

            struct wl_output *output = (struct wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 4);
            wayland->outputs.push_back(
                WaylandOutput{
                    name,                 // wl_name
                    output,               // output
                    nullptr,              // xdg_output
                    mgl::vec2i{0, 0},     // pos (native)
                    mgl::vec2i{0, 0},     // size (native)
                    mgl::vec2i{0, 0},     // logical_pos
                    mgl::vec2i{0, 0},     // logical_size
                    0,                    // transform
                    ""                    // name
                });
            wl_output_add_listener(output, &output_listener, wayland);
        } else if(strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
            if(version < 1) {
                fprintf(stderr, "Warning: xdg output interface version is < 1, expected >= 1\n");
                return;
            }

            if(wayland->xdg_output_manager) {
                zxdg_output_manager_v1_destroy(wayland->xdg_output_manager);
                wayland->xdg_output_manager = NULL;
            }
            wayland->xdg_output_manager = (struct zxdg_output_manager_v1*)wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 1);
        }
    }

    static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name) {
        (void)data;
        (void)registry;
        (void)name;
        // TODO: Remove output
    }

    static struct wl_registry_listener registry_listener = {
        registry_add_object,
        registry_remove_object,
    };

    static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *zxdg_output_v1, int32_t x, int32_t y) {
        (void)zxdg_output_v1;
        WaylandOutput *monitor = (WaylandOutput*)data;
        monitor->logical_pos.x = x;
        monitor->logical_pos.y = y;
    }

    static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height) {
        (void)xdg_output;
        WaylandOutput *monitor = (WaylandOutput*)data;
        monitor->logical_size.x = width;
        monitor->logical_size.y = height;
    }

    static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {
        (void)data;
        (void)xdg_output;
    }

    static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
        (void)data;
        (void)xdg_output;
        (void)name;
    }

    static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description) {
        (void)data;
        (void)xdg_output;
        (void)description;
    }

    static const struct zxdg_output_v1_listener xdg_output_listener = {
        xdg_output_logical_position,
        xdg_output_handle_logical_size,
        xdg_output_handle_done,
        xdg_output_handle_name,
        xdg_output_handle_description,
    };

    static const int transform_90 = 1;
    static const int transform_270 = 3;

    static void transform_monitors(Wayland &wayland) {
        for(WaylandOutput &output : wayland.outputs) {
            if(output.transform == transform_90 || output.transform == transform_270)
                std::swap(output.size.x, output.size.y);

            const int native_w = output.size.x;
            const int native_h = output.size.y;
            const int logical_w = output.logical_size.x > 0 ? output.logical_size.x : native_w;
            const int logical_h = output.logical_size.y > 0 ? output.logical_size.y : native_h;
            output.pos.x = (logical_w > 0)
                ? (int)((int64_t)output.logical_pos.x * native_w / logical_w)
                : output.logical_pos.x;
            output.pos.y = (logical_h > 0)
                ? (int)((int64_t)output.logical_pos.y * native_h / logical_h)
                : output.logical_pos.y;
        }
    }

    static void set_monitor_outputs_from_xdg_output(Wayland &wayland, struct wl_display *dpy) {
        if(!wayland.xdg_output_manager) {
            fprintf(stderr, "Warning: WindowUtils::set_monitor_outputs_from_xdg_output: zxdg_output_manager not found. Registered monitor positions might be incorrect\n");
            return;
        }

        for(WaylandOutput &monitor : wayland.outputs) {
            monitor.xdg_output = zxdg_output_manager_v1_get_xdg_output(wayland.xdg_output_manager, monitor.output);
            zxdg_output_v1_add_listener(monitor.xdg_output, &xdg_output_listener, &monitor);
        }

        // Fetch xdg_output
        wl_display_roundtrip(dpy);
    }

    unsigned char* window_get_property(Display *dpy, Window window, Atom property_type, const char *property_name, unsigned int *property_size) {
        Atom ret_property_type = None;
        int ret_format = 0;
        unsigned long num_items = 0;
        unsigned long num_remaining_bytes = 0;
        unsigned char *data = nullptr;
        const Atom atom = XInternAtom(dpy, property_name, False);
        if(XGetWindowProperty(dpy, window, atom, 0, MAX_PROPERTY_VALUE_LEN / 4, False, property_type, &ret_property_type, &ret_format, &num_items, &num_remaining_bytes, &data) != Success || !data) {
            return nullptr;
        }

        if(ret_property_type != property_type) {
            XFree(data);
            return nullptr;
        }

        *property_size = (ret_format / (32 / sizeof(long))) * num_items;
        return data;
    }

    static bool window_has_atom(Display *dpy, Window window, Atom atom) {
        Atom type;
        unsigned long len, bytes_left;
        int format;
        unsigned char *properties = NULL;
        if(XGetWindowProperty(dpy, window, atom, 0, 1024, False, AnyPropertyType, &type, &format, &len, &bytes_left, &properties) < Success)
            return false;

        if(properties)
            XFree(properties);

        return type != None;
    }

    static bool window_is_user_program(Display *dpy, Window window) {
        const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
        const Atom wm_state_atom = XInternAtom(dpy, "WM_STATE", False);
        return window_has_atom(dpy, window, net_wm_state_atom) || window_has_atom(dpy, window, wm_state_atom);
    }

    static Window get_window_graphics_parent(Display *dpy, Window window) {
        if(window == DefaultRootWindow(dpy) || window == None)
            return window;

        XWindowAttributes attr;
        memset(&attr, 0, sizeof(attr));
        XGetWindowAttributes(dpy, window, &attr);
        if(attr.override_redirect || attr.c_class != InputOutput || attr.map_state != IsViewable || !window_is_user_program(dpy, window)) {
            Window root;
            Window parent;
            Window *children = nullptr;
            unsigned int num_children = 0;
            if(!XQueryTree(dpy, window, &root, &parent, &children, &num_children))
                return None;

            if(children)
                XFree(children);

            if(parent)
                return get_window_graphics_parent(dpy, parent);
        }
        return window;
    }

    Window window_get_target_window_child(Display *display, Window window) {
        if(window == None)
            return None;

        if(window_is_user_program(display, window))
            return window;

        Window root;
        Window parent;
        Window *children = nullptr;
        unsigned int num_children = 0;
        if(!XQueryTree(display, window, &root, &parent, &children, &num_children) || !children)
            return None;

        Window found_window = None;
        for(int i = (int)num_children - 1; i >= 0; --i) {
            if(children[i] && window_is_user_program(display, children[i])) {
                found_window = children[i];
                goto finished;
            }
        }

        for(int i = (int)num_children - 1; i >= 0; --i) {
            if(children[i]) {
                Window win = window_get_target_window_child(display, children[i]);
                if(win) {
                    found_window = win;
                    goto finished;
                }
            }
        }

        finished:
        XFree(children);
        return found_window;
    }

    mgl::vec2i get_cursor_position(Display *dpy, Window *window) {
        Window root_window = None;
        *window = None;
        int dummy_i;
        unsigned int dummy_u;
        mgl::vec2i root_pos;
        XQueryPointer(dpy, DefaultRootWindow(dpy), &root_window, window, &root_pos.x, &root_pos.y, &dummy_i, &dummy_i, &dummy_u);

        const Window direct_window = *window;
        *window = window_get_target_window_child(dpy, *window);
        // HACK: Count some other x11 windows as having an x11 window focused. Some games seem to create an Input window and that gets focused.
        // direct_window can be None on Wayland sessions where the cursor isn't over any X11 window — querying its attributes would emit BadWindow.
        if(!*window && direct_window) {
            XWindowAttributes attr;
            memset(&attr, 0, sizeof(attr));
            XGetWindowAttributes(dpy, direct_window, &attr);
            if(attr.c_class == InputOnly && !get_window_title(dpy, direct_window))
                *window = direct_window;
        }
        return root_pos;
    }

    Window get_focused_window(Display *dpy, WindowCaptureType cap_type, bool fallback_cursor_focused) {
        //const Atom net_active_window_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        Window focused_window = None;

        if(cap_type == WindowCaptureType::FOCUSED) {
            // Atom type = None;
            // int format = 0;
            // unsigned long num_items = 0;
            // unsigned long bytes_left = 0;
            // unsigned char *data = NULL;
            // XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_active_window_atom, 0, 1, False, XA_WINDOW, &type, &format, &num_items, &bytes_left, &data);

            // if(type == XA_WINDOW && num_items == 1 && data)
            //     focused_window = *(Window*)data;

            // if(data)
            //     XFree(data);

            // if(focused_window)
            //     return focused_window;

            int revert_to = 0;
            XGetInputFocus(dpy, &focused_window, &revert_to);
            // XGetInputFocus returns PointerRoot (= 1) when no client holds focus
            // (typical on Wayland sessions where a native Wayland surface is focused).
            // It's not a real Window — treat it as "no focus" and fall through to the
            // cursor-position fallback below.
            if(focused_window == PointerRoot)
                focused_window = None;
            focused_window = get_window_graphics_parent(dpy, focused_window);

            if(focused_window && focused_window != DefaultRootWindow(dpy))
                return focused_window;

            if(!fallback_cursor_focused)
                return None;
        }

        get_cursor_position(dpy, &focused_window);
        if(focused_window && focused_window != DefaultRootWindow(dpy))
            return focused_window;

        return None;
    }

    std::string window_title_utf8_sanitize(const char *str, int size) {
        // Some games such as the finals has zero-width space characters
        const uint32_t zero_width_space_codepoint = 0x200b;
        const uint32_t zero_width_no_break_space_codepoint = 0xfeff;

        std::string result;
        for(int i = 0; i < size;) {
            // Some games such as the finals has utf8-bom between each character, wtf?
            if(i + 3 <= size && memcmp(str + i, "\xEF\xBB\xBF", 3) == 0) {
                i += 3;
                continue;
            }

            uint32_t codepoint = 0;
            size_t codepoint_length = 1;
            if(mgl::utf8_decode((uint8_t*)str + i, size - i, &codepoint, &codepoint_length) && codepoint != zero_width_space_codepoint && codepoint != zero_width_no_break_space_codepoint)
                result.append((const char*)str + i, codepoint_length);
            i += codepoint_length;
        }
        return result;
    }

    std::optional<std::string> get_window_title(Display *dpy, Window window) {
        std::optional<std::string> result;
        const Atom net_wm_name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
        const Atom wm_name_atom = XInternAtom(dpy, "WM_NAME", False);
        const Atom utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);

        Atom type = None;
        int format = 0;
        unsigned long num_items = 0;
        unsigned long bytes_left = 0;
        unsigned char *data = NULL;
        XGetWindowProperty(dpy, window, net_wm_name_atom, 0, 1024, False, utf8_string_atom, &type, &format, &num_items, &bytes_left, &data);

        if(type == utf8_string_atom && format == 8 && data) {
            result = window_title_utf8_sanitize((const char*)data, num_items);
            goto done;
        }

        if(data)
            XFree(data);

        type = None;
        format = 0;
        num_items = 0;
        bytes_left = 0;
        data = NULL;
        XGetWindowProperty(dpy, window, wm_name_atom, 0, 1024, False, 0, &type, &format, &num_items, &bytes_left, &data);

        if((type == XA_STRING || type == utf8_string_atom) && data) {
            result = window_title_utf8_sanitize((const char*)data, num_items);
            goto done;
        }

        done:
        if(data)
            XFree(data);
        return result;
    }

    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type, bool fallback_cursor_focused) {
        std::string result;
        const Window focused_window = get_focused_window(dpy, window_capture_type, fallback_cursor_focused);
        if(focused_window == None)
            return result;

        // Window title is not always ideal (for example for a browser), but for games its pretty much required
        const std::optional<std::string> window_title = get_window_title(dpy, focused_window);
        if(window_title) {
            result = strip(window_title.value());
            return result;
        }

        XClassHint class_hint = {nullptr, nullptr};
        XGetClassHint(dpy, focused_window, &class_hint);
        if(class_hint.res_class)
            result = strip(class_hint.res_class);

        if(class_hint.res_name)
            XFree(class_hint.res_name);

        if(class_hint.res_class)
            XFree(class_hint.res_class);

        return result;
    }

    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, std::string_view ignore_window_title) {
        std::string result;

        Window root;
        Window parent;
        Window *children = nullptr;
        unsigned int num_children = 0;
        if(!XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &num_children) || !children)
            return result;

        for(int i = (int)num_children - 1; i >= 0; --i) {
            if(get_window_title(dpy, children[i]) == ignore_window_title)
                continue;

            XWindowAttributes attr;
            memset(&attr, 0, sizeof(attr));
            XGetWindowAttributes(dpy, children[i], &attr);
            if(attr.override_redirect || attr.c_class != InputOutput || attr.map_state != IsViewable)
                continue;

            if(position.x >= attr.x && position.x <= attr.x + attr.width && position.y >= attr.y && position.y <= attr.y + attr.height) {
                const Window real_window = window_get_target_window_child(dpy, children[i]);
                const std::optional<std::string> window_title = get_window_title(dpy, real_window);
                if(!real_window || window_title == ignore_window_title)
                    continue;

                if(window_title)
                    result = strip(window_title.value());

                break;
            }
        }

        XFree(children);
        return result;
    }

    std::string get_window_name_at_cursor_position(Display *dpy, std::string_view ignore_window_title) {
        Window cursor_window;
        const mgl::vec2i cursor_position = get_cursor_position(dpy, &cursor_window);
        return get_window_name_at_position(dpy, cursor_position, ignore_window_title);
    }

    void set_window_size_not_resizable(Display *dpy, Window window, int width, int height) {
        XSizeHints *size_hints = XAllocSizeHints();
        if(size_hints) {
            size_hints->width = width;
            size_hints->height = height;
            size_hints->min_width = width;
            size_hints->min_height = height;
            size_hints->max_width = width;
            size_hints->max_height = height;
            size_hints->flags = PSize | PMinSize | PMaxSize;
            XSetWMNormalHints(dpy, window, size_hints);
            XFree(size_hints);
        }
    }

    typedef struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } MotifHints;

    #define MWM_HINTS_DECORATIONS   2

    #define MWM_DECOR_NONE          0
    #define MWM_DECOR_ALL           1

    static void window_set_decorations_visible(Display *display, Window window, bool visible) {
        const Atom motif_wm_hints_atom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
        MotifHints motif_hints;
        memset(&motif_hints, 0, sizeof(motif_hints));
        motif_hints.flags = MWM_HINTS_DECORATIONS;
        motif_hints.decorations = visible ? MWM_DECOR_ALL : MWM_DECOR_NONE;
        XChangeProperty(display, window, motif_wm_hints_atom, motif_wm_hints_atom, 32, PropModeReplace, (unsigned char*)&motif_hints, sizeof(motif_hints) / sizeof(long));
    }

    static bool create_window_get_center_position_kde(Display *display, mgl::vec2i &position) {
        const int size = 1;
        XSetWindowAttributes window_attr;
        window_attr.event_mask = StructureNotifyMask;
        window_attr.background_pixel = 0;
        const Window window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, size, size, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &window_attr);
        if(!window)
            return false;

        const Atom net_wm_window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        const Atom net_wm_window_type_notification_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
        const Atom net_wm_window_type_utility = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        const Atom net_wm_window_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);

        const Atom window_type_atoms[2] = {
            net_wm_window_type_notification_atom,
            net_wm_window_type_utility
        };
        XChangeProperty(display, window, net_wm_window_type_atom, XA_ATOM, 32, PropModeReplace, (const unsigned char*)window_type_atoms, 2L);

        const double alpha = 0.0;
        const unsigned long opacity = (unsigned long)(0xFFFFFFFFul * alpha);
        XChangeProperty(display, window, net_wm_window_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity, 1L);

        window_set_decorations_visible(display, window, false);
        set_window_size_not_resizable(display, window, size, size);

        XMapWindow(display, window);
        XFlush(display);

        bool got_data = false;
        const int x_fd = XConnectionNumber(display);
        XEvent xev;
        while(true) {
            struct pollfd poll_fd;
            poll_fd.fd = x_fd;
            poll_fd.events = POLLIN;
            poll_fd.revents = 0;
            const int fds_ready = poll(&poll_fd, 1, 200);
            if(fds_ready == 0) {
                fprintf(stderr, "Error: timed out waiting for ConfigureNotify after XCreateWindow\n");
                break;
            } else if(fds_ready == -1 || !(poll_fd.revents & POLLIN)) {
                continue;
            }

            while(XPending(display)) {
                XNextEvent(display, &xev);
                if(xev.type == ConfigureNotify && xev.xconfigure.window == window) {
                    got_data = xev.xconfigure.x > 0 && xev.xconfigure.y > 0;
                    position.x = xev.xconfigure.x + xev.xconfigure.width / 2;
                    position.y = xev.xconfigure.y + xev.xconfigure.height / 2;
                    goto done;
                }
            }
        }

        done:
        XDestroyWindow(display, window);
        XFlush(display);

        return got_data;
    }

    static bool create_window_get_center_position_gnome(Display *display, mgl::vec2i &position) {
        const int size = 32;
        XSetWindowAttributes window_attr;
        window_attr.event_mask = StructureNotifyMask | ExposureMask;
        window_attr.background_pixel = 0;
        const Window window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, size, size, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &window_attr);
        if(!window)
            return false;

        const Atom net_wm_window_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
        const double alpha = 0.0;
        const unsigned long opacity = (unsigned long)(0xFFFFFFFFul * alpha);
        XChangeProperty(display, window, net_wm_window_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity, 1L);

        window_set_decorations_visible(display, window, false);
        set_window_size_not_resizable(display, window, size, size);

        XMapWindow(display, window);
        XFlush(display);

        bool got_data = false;
        const int x_fd = XConnectionNumber(display);
        XEvent xev;
        while(true) {
            struct pollfd poll_fd;
            poll_fd.fd = x_fd;
            poll_fd.events = POLLIN;
            poll_fd.revents = 0;
            const int fds_ready = poll(&poll_fd, 1, 200);
            if(fds_ready == 0) {
                fprintf(stderr, "Error: timed out waiting for MapNotify/ConfigureNotify after XCreateWindow\n");
                break;
            } else if(fds_ready == -1 || !(poll_fd.revents & POLLIN)) {
                continue;
            }

            while(XPending(display)) {
                XNextEvent(display, &xev);
                if(xev.type == MapNotify && xev.xmap.window == window) {
                    int x = 0;
                    int y = 0;
                    Window w = None;
                    XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &x, &y, &w);

                    got_data = x > 0 && y > 0;
                    position.x = x + size / 2;
                    position.y = y + size / 2;
                    if(got_data)
                        goto done;
                } else if(xev.type == ConfigureNotify && xev.xconfigure.window == window) {
                    got_data = xev.xconfigure.x > 0 && xev.xconfigure.y > 0;
                    position.x = xev.xconfigure.x + xev.xconfigure.width / 2;
                    position.y = xev.xconfigure.y + xev.xconfigure.height / 2;
                    if(got_data)
                        goto done;
                }
            }
        }

        done:
        XDestroyWindow(display, window);
        XFlush(display);

        return got_data;
    }

    mgl::vec2i create_window_get_center_position(Display *display) {
        mgl::vec2i pos;
        if(!create_window_get_center_position_kde(display, pos)) {
            pos.x = 0;
            pos.y = 0;
            create_window_get_center_position_gnome(display, pos);
        }
        return pos;
    }

    std::string get_window_manager_name(Display *display) {
        std::string wm_name;
        unsigned int property_size = 0;
        Window window = None;

        unsigned char *net_supporting_wm_check = window_get_property(display, DefaultRootWindow(display), XA_WINDOW, "_NET_SUPPORTING_WM_CHECK", &property_size);
        if(net_supporting_wm_check) {
            if(property_size == 8)
                window = *(Window*)net_supporting_wm_check;
            XFree(net_supporting_wm_check);
        }

        if(!window) {
            unsigned char *win_supporting_wm_check = window_get_property(display, DefaultRootWindow(display), XA_WINDOW, "_WIN_SUPPORTING_WM_CHECK", &property_size);
            if(win_supporting_wm_check) {
                if(property_size == 8)
                    window = *(Window*)win_supporting_wm_check;
                XFree(win_supporting_wm_check);
            }
        }

        if(!window)
            return wm_name;

        const std::optional<std::string> window_title = get_window_title(display, window);
        if(window_title)
            wm_name = strip(window_title.value());

        return wm_name;
    }

    bool is_compositor_running(Display *dpy, int screen) {
        char prop_name[20];
        snprintf(prop_name, sizeof(prop_name), "_NET_WM_CM_S%d", screen);
        const Atom prop_atom = XInternAtom(dpy, prop_name, False);
        return XGetSelectionOwner(dpy, prop_atom) != None;
    }

    std::vector<Monitor> get_monitors(Display *dpy) {
        std::vector<Monitor> monitors;
        int nmonitors = 0;
        XRRMonitorInfo *monitor_info = XRRGetMonitors(dpy, DefaultRootWindow(dpy), True, &nmonitors);
        if(monitor_info) {
            for(int i = 0; i < nmonitors; ++i) {
                char *monitor_name = XGetAtomName(dpy, monitor_info[i].name);
                if(!monitor_name)
                    continue;

                monitors.push_back({mgl::vec2i(monitor_info[i].x, monitor_info[i].y), mgl::vec2i(monitor_info[i].width, monitor_info[i].height), std::string(monitor_name)});
                XFree(monitor_name);
            }
            XRRFreeMonitors(monitor_info);
        }
        return monitors;
    }

    std::vector<Monitor> get_monitors_wayland(struct wl_display *dpy) {
        Wayland wayland;

        struct wl_registry *registry = wl_display_get_registry(dpy);
        wl_registry_add_listener(registry, &registry_listener, &wayland);

        // Fetch globals
        wl_display_roundtrip(dpy);

        // Fetch wl_output
        wl_display_roundtrip(dpy);

        set_monitor_outputs_from_xdg_output(wayland, dpy);
        transform_monitors(wayland);

        std::vector<Monitor> monitors;
        for(WaylandOutput &output : wayland.outputs) {
            monitors.push_back(Monitor{output.pos, output.size, std::move(output.name)});

            if(output.output) {
                wl_output_destroy(output.output);
                output.output = nullptr;
            }

            if(output.xdg_output) {
                zxdg_output_v1_destroy(output.xdg_output);
                output.xdg_output = nullptr;
            }
        }
        wayland.outputs.clear();

        if(wayland.xdg_output_manager) {
            zxdg_output_manager_v1_destroy(wayland.xdg_output_manager);
            wayland.xdg_output_manager = nullptr;
        }

        wl_registry_destroy(registry);

        return monitors;
    }

    static bool device_is_mouse(const XIDeviceInfo *dev) {
        if(dev->use == XIMasterPointer || dev->use == XISlavePointer)
            return true;
        if(dev->use != XIFloatingSlave)
            return false;
        for(int i = 0; i < dev->num_classes; ++i) {
            if(dev->classes[i]->type == XIButtonClass || dev->classes[i]->type == XIValuatorClass)
                return true;
        }
        return false;
    }

    static void xi_grab_all_mouse_devices(Display *dpy, bool grab) {
        if(!dpy)
            return;

        int num_devices = 0;
        XIDeviceInfo *info = XIQueryDevice(dpy, XIAllDevices, &num_devices);
        if(!info)
            return;

        unsigned char mask[XIMaskLen(XI_LASTEVENT)];
        memset(mask, 0, sizeof(mask));
        XISetMask(mask, XI_Motion);
        //XISetMask(mask, XI_RawMotion);
        XISetMask(mask, XI_ButtonPress);
        XISetMask(mask, XI_ButtonRelease);
        XISetMask(mask, XI_KeyPress);
        XISetMask(mask, XI_KeyRelease);

        for (int i = 0; i < num_devices; ++i) {
            const XIDeviceInfo *dev = &info[i];
            if(!device_is_mouse(dev))
                continue;

            XIEventMask xi_masks;
            xi_masks.deviceid = dev->deviceid;
            xi_masks.mask_len = sizeof(mask);
            xi_masks.mask = mask;
            if(grab)
                XIGrabDevice(dpy, dev->deviceid, DefaultRootWindow(dpy), CurrentTime, None, XIGrabModeAsync, XIGrabModeAsync, XIOwnerEvents, &xi_masks);
            else
                XIUngrabDevice(dpy, dev->deviceid, CurrentTime);
        }

        XFlush(dpy);
        XIFreeDeviceInfo(info);
    }

    void xi_grab_all_mouse_devices(Display *dpy) {
        xi_grab_all_mouse_devices(dpy, true);
    }

    void xi_ungrab_all_mouse_devices(Display *dpy) {
        xi_grab_all_mouse_devices(dpy, false);
    }

    void xi_warp_all_mouse_devices(Display *dpy, mgl::vec2i position) {
        if(!dpy)
            return;

        int num_devices = 0;
        XIDeviceInfo *info = XIQueryDevice(dpy, XIAllDevices, &num_devices);
        if(!info)
            return;

        for (int i = 0; i < num_devices; ++i) {
            const XIDeviceInfo *dev = &info[i];
            if(!device_is_mouse(dev))
                continue;

            XIWarpPointer(dpy, dev->deviceid, DefaultRootWindow(dpy), DefaultRootWindow(dpy), 0, 0, 0, 0, position.x, position.y);
        }

        XFlush(dpy);
        XIFreeDeviceInfo(info);
    }

    void window_set_fullscreen(Display *dpy, Window window, bool fullscreen) {
        const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
        const Atom net_wm_state_fullscreen_atom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

        XEvent xev;
        xev.type = ClientMessage;
        xev.xclient.window = window;
        xev.xclient.message_type = net_wm_state_atom;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = fullscreen ? 1 : 0;
        xev.xclient.data.l[1] = net_wm_state_fullscreen_atom;
        xev.xclient.data.l[2] = 0;
        xev.xclient.data.l[3] = 1;
        xev.xclient.data.l[4] = 0;

        if(!XSendEvent(dpy, DefaultRootWindow(dpy), 0, SubstructureRedirectMask | SubstructureNotifyMask, &xev)) {
            fprintf(stderr, "mgl warning: failed to change window fullscreen state\n");
            return;
        }

        XFlush(dpy);
    }

    bool window_is_fullscreen(Display *display, Window window) {
        const Atom wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
        const Atom wm_state_fullscreen_atom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);

        Atom type = None;
        int format = 0;
        unsigned long num_items = 0;
        unsigned long bytes_after = 0;
        unsigned char *properties = nullptr;
        if(XGetWindowProperty(display, window, wm_state_atom, 0, 1024, False, XA_ATOM, &type, &format, &num_items, &bytes_after, &properties) < Success) {
            fprintf(stderr, "Failed to get window wm state property\n");
            return false;
        }

        if(!properties)
            return false;

        bool is_fullscreen = false;
        Atom *atoms = (Atom*)properties;
        for(unsigned long i = 0; i < num_items; ++i) {
            if(atoms[i] == wm_state_fullscreen_atom) {
                is_fullscreen = true;
                break;
            }
        }

        XFree(properties);
        return is_fullscreen;
    }

    bool get_drawable_geometry(Display *display, Drawable drawable, DrawableGeometry *geometry) {
        geometry->x = 0;
        geometry->y = 0;
        geometry->width = 0;
        geometry->height = 0;

        Window root_window;
        unsigned int w, h;
        unsigned int dummy_border, dummy_depth;
        Status s = XGetGeometry(display, drawable, &root_window, &geometry->x, &geometry->y, &w, &h, &dummy_border, &dummy_depth);

        geometry->width = w;
        geometry->height = h;
        return s == True;
    }

    std::optional<Monitor> get_monitor_by_window_center(Display *display, Window window) {
        DrawableGeometry geometry;
        if(!get_drawable_geometry(display, window, &geometry))
            return std::nullopt;

        const mgl::vec2i window_center = mgl::vec2i(geometry.x, geometry.y) + mgl::vec2i(geometry.width, geometry.height) / 2;
        auto monitors = get_monitors(display);
        for(auto &monitor : monitors) {
            if(mgl::IntRect(monitor.position, monitor.size).contains(window_center))
                return std::move(monitor);
        }
        return std::nullopt;
    }

    #define _NET_WM_STATE_REMOVE  0
    #define _NET_WM_STATE_ADD     1
    #define _NET_WM_STATE_TOGGLE  2

    bool set_window_wm_state(Display *dpy, Window window, Atom atom) {
        const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);

        XClientMessageEvent xclient;
        memset(&xclient, 0, sizeof(xclient));

        xclient.type = ClientMessage;
        xclient.window = window;
        xclient.message_type = net_wm_state_atom;
        xclient.format = 32;
        xclient.data.l[0] = _NET_WM_STATE_ADD;
        xclient.data.l[1] = atom;
        xclient.data.l[2] = 0;
        xclient.data.l[3] = 0;
        xclient.data.l[4] = 0;

        XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent*)&xclient);
        XFlush(dpy);
        return true;
    }

    void make_window_click_through(Display *display, Window window) {
        XRectangle rect;
        memset(&rect, 0, sizeof(rect));
        XserverRegion region = XFixesCreateRegion(display, &rect, 1);
        XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    }

    bool make_window_sticky(Display *dpy, Window window) {
        return set_window_wm_state(dpy, window, XInternAtom(dpy, "_NET_WM_STATE_STICKY", False));
    }

    bool hide_window_from_taskbar(Display *dpy, Window window) {
        return set_window_wm_state(dpy, window, XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False));
    }
}
