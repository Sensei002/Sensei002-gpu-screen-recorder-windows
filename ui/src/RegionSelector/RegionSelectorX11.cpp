#include "../../include/RegionSelector/RegionSelectorX11.hpp"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>
#include <mglpp/system/Rect.hpp>

namespace gsr {
    static const int cursor_window_size = 32;
    static const int cursor_thickness = 5;
    static const int region_border_size = 2;

    static bool xinput_is_supported(Display *dpy, int *xi_opcode) {
        *xi_opcode = 0;
        int query_event = 0;
        int query_error = 0;
        if(!XQueryExtension(dpy, "XInputExtension", xi_opcode, &query_event, &query_error)) {
            fprintf(stderr, "error: RegionSelector: X Input extension not available\n");
            return false;
        }

        int major = 2;
        int minor = 1;
        int retval = XIQueryVersion(dpy, &major, &minor);
        if(retval != Success) {
            fprintf(stderr, "error: RegionSelector: XInput 2.1 is not supported\n");
            return false;
        }

        return true;
    }

    static int max_int(int a, int b) {
        return a >= b ? a : b;
    }

    static void set_region_rectangle(Display *dpy, Window window, int x, int y, int width, int height, int border_size) {
        if(width < 0) {
            x += width;
            width = abs(width);
        }

        if(height < 0) {
            y += height;
            height = abs(height);
        }

        XRectangle rectangles[] = {
            {
                (short)max_int(0, x),                              (short)max_int(0, y),
                (unsigned short)max_int(0, border_size),           (unsigned short)max_int(0, height)
            }, // Left
            {
                (short)max_int(0, x + width - border_size),        (short)max_int(0, y),
                (unsigned short)max_int(0, border_size),           (unsigned short)max_int(0, height)
            }, // Right
            {
                (short)max_int(0, x + border_size),                (short)max_int(0, y),
                (unsigned short)max_int(0, width - border_size*2), (unsigned short)max_int(0, border_size)
            }, // Top
            {
                (short)max_int(0, x + border_size),                (short)max_int(0, y + height - border_size),
                (unsigned short)max_int(0, width - border_size*2), (unsigned short)max_int(0, border_size)
            }, // Bottom
        };

        if(width == 0 && height == 0)
            XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectangles, 0, ShapeSet, Unsorted);
        else
            XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectangles, 4, ShapeSet, Unsorted);
        XFlush(dpy);
    }

    static void set_window_shape_cross(Display *dpy, Window window, int window_width, int window_height, int thickness) {
        XRectangle rectangles[] = {
            {
                (short)(window_width / 2 - thickness / 2), (short)0,
                (unsigned short)thickness,                 (unsigned short)window_height
            }, // Vertical
            {
                (short)(0),                                (short)(window_height / 2 - thickness / 2),
                (unsigned short)window_width,              (unsigned short)thickness
            },     // Horizontal
        };
        XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectangles, 2, ShapeSet, Unsorted);
        XFlush(dpy);
    }

    static void draw_rectangle(Display *dpy, Window window, GC gc, int x, int y, int width, int height) {
        if(width < 0) {
            x += width;
            width = abs(width);
        }

        if(height < 0) {
            y += height;
            height = abs(height);
        }

        if(width != 0 && height != 0)
            XDrawRectangle(dpy, window, gc, x, y, width, height);
    }

    static Window create_cursor_window(Display *dpy, int width, int height, XVisualInfo *vinfo, unsigned long background_pixel, Colormap colormap) {
        XSetWindowAttributes window_attr;
        window_attr.background_pixel = background_pixel;
        window_attr.border_pixel = 0;
        window_attr.override_redirect = true;
        window_attr.event_mask = StructureNotifyMask | PointerMotionMask;
        window_attr.colormap = colormap;
        const Window window = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, width, height, 0, vinfo->depth, InputOutput, vinfo->visual, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask | CWColormap, &window_attr);
        if(window) {
            set_window_size_not_resizable(dpy, window, width, height);
            set_window_shape_cross(dpy, window, width, height, cursor_thickness);
            make_window_click_through(dpy, window);
        }
        return window;
    }

    static void draw_rectangle_or_region(Display *dpy, Window window, GC region_gc, int region_border_size, bool is_wayland, mgl::vec2i pos, mgl::vec2i size) {
        if(is_wayland)
            draw_rectangle(dpy, window, region_gc, pos.x, pos.y, size.x, size.y);
        else
            set_region_rectangle(dpy, window, pos.x, pos.y, size.x, size.y, region_border_size);
    }

    static void draw_rectangle_around_selected_monitor(Display *dpy, Window window, GC region_gc, int region_border_size, bool is_wayland, const std::vector<Monitor> &monitors, mgl::vec2i cursor_pos) {
        const Monitor *focused_monitor = nullptr;
        for(const Monitor &monitor : monitors) {
            if(cursor_pos.x >= monitor.position.x && cursor_pos.x <= monitor.position.x + monitor.size.x
                && cursor_pos.y >= monitor.position.y && cursor_pos.y <= monitor.position.y + monitor.size.y)
            {
                focused_monitor = &monitor;
                break;
            }
        }

        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if(focused_monitor) {
            x = focused_monitor->position.x;
            y = focused_monitor->position.y;
            width = focused_monitor->size.x;
            height = focused_monitor->size.y;
        }

        draw_rectangle_or_region(dpy, window, region_gc, region_border_size, is_wayland, mgl::vec2i(x, y), mgl::vec2i(width, height));
    }

    static void update_cursor_window(Display *dpy, Window window, Window cursor_window, bool is_wayland, int cursor_x, int cursor_y, int cursor_window_size, int thickness, GC cursor_gc) {
        if(is_wayland) {
            const int x = cursor_x - cursor_window_size / 2;
            const int y = cursor_y - cursor_window_size / 2;
            XFillRectangle(dpy, window, cursor_gc, x + cursor_window_size / 2 - thickness / 2 , y,                                          thickness,          cursor_window_size);
            XFillRectangle(dpy, window, cursor_gc, x,                                           y + cursor_window_size / 2 - thickness / 2, cursor_window_size, thickness);
        } else if(cursor_window) {
            XMoveWindow(dpy, cursor_window, cursor_x - cursor_window_size / 2, cursor_y - cursor_window_size / 2);
        }
        XFlush(dpy);
    }

    static bool is_xwayland(Display *dpy) {
        int opcode, event, error;
        return XQueryExtension(dpy, "XWAYLAND", &opcode, &event, &error);
    }

    static unsigned long mgl_color_to_x11_color(mgl::Color color) {
        if(color.a == 0)
            return 0;
        return ((uint32_t)color.a << 24) | (((uint32_t)color.r * color.a / 0xFF) << 16) | (((uint32_t)color.g * color.a / 0xFF) << 8) | ((uint32_t)color.b * color.a / 0xFF);
    }

    static const Monitor* get_monitor_by_region_center(const std::vector<Monitor> &monitors, Region region) {
        const mgl::vec2i center = {region.pos.x + region.size.x / 2, region.pos.y + region.size.y / 2};
        for(const Monitor &monitor : monitors) {
            if(center.x >= monitor.position.x && center.x <= monitor.position.x + monitor.size.x
                && center.y >= monitor.position.y && center.y <= monitor.position.y + monitor.size.y)
            {
                return &monitor;
            }
        }
        return nullptr;
    }

    // Name is the x11 name. TODO: verify if this works on all wayland compositors
    static const Monitor* get_wayland_monitor_by_name(const std::vector<Monitor> &monitors, const std::string &name) {
        for(const Monitor &monitor : monitors) {
            if(monitor.name == name)
                return &monitor;
        }
        return nullptr;
    }

    static mgl::vec2d to_vec2d(mgl::vec2i v) {
        return { (double)v.x, (double)v.y };
    }

    static Region x11_region_to_wayland_region(Display *dpy, struct wl_display *wayland_dpy, Region x11_region) {
        const std::vector<Monitor> x11_monitors = get_monitors(dpy);
        const Monitor *x11_selected_monitor = get_monitor_by_region_center(x11_monitors, x11_region);
        if(!x11_selected_monitor) {
            fprintf(stderr, "Warning: RegionSelector: failed to get x11 monitor\n");
            return x11_region;
        }

        const std::vector<Monitor> wayland_monitors = get_monitors_wayland(wayland_dpy);
        const Monitor *wayland_monitor = get_wayland_monitor_by_name(wayland_monitors, x11_selected_monitor->name);
        if(!wayland_monitor) {
            fprintf(stderr, "Warning: RegionSelector: failed to get wayland monitor\n");
            return x11_region;
        }

        const mgl::vec2d region_relative_pos = {
            (double)(x11_region.pos.x - x11_selected_monitor->position.x) / (double)x11_selected_monitor->size.x,
            (double)(x11_region.pos.y - x11_selected_monitor->position.y) / (double)x11_selected_monitor->size.y,
        };

        const mgl::vec2d region_relative_size = {
            (double)x11_region.size.x / (double)x11_selected_monitor->size.x,
            (double)x11_region.size.y / (double)x11_selected_monitor->size.y,
        };

        return Region {
            wayland_monitor->position + (region_relative_pos * to_vec2d(wayland_monitor->size)).to_vec2i(),
            (region_relative_size * to_vec2d(wayland_monitor->size)).to_vec2i(),
        };
    }

    static std::vector<RegionWindow> query_windows(Display *dpy) {
        std::vector<RegionWindow> windows;

        Window root_return = None;
        Window parent_return = None;
        Window *children_return = nullptr;
        unsigned int num_children_return = 0;
        if(!XQueryTree(dpy, DefaultRootWindow(dpy), &root_return, &parent_return, &children_return, &num_children_return) || !children_return)
            return windows;

        for(int i = (int)num_children_return - 1; i >= 0; --i) {
            const Window child_window = children_return[i];
            XWindowAttributes win_attr;
            if(XGetWindowAttributes(dpy, child_window, &win_attr) && !win_attr.override_redirect && win_attr.c_class == InputOutput && win_attr.map_state == IsViewable) {
                windows.push_back(
                    RegionWindow{
                        child_window,
                        mgl::vec2i(win_attr.x, win_attr.y),
                        mgl::vec2i(win_attr.width, win_attr.height)
                    }
                );
            }
        }

        XFree(children_return);
        return windows;
    }

    static std::optional<RegionWindow> get_window_by_position(const std::vector<RegionWindow> &windows, mgl::vec2i pos) {
        for(const RegionWindow &window : windows) {
            if(mgl::IntRect(window.pos, window.size).contains(pos))
                return window;
        }
        return std::nullopt;
    }

    RegionSelectorX11::RegionSelectorX11(Display *dpy) : dpy(dpy) {

    }

    RegionSelectorX11::~RegionSelectorX11() {
        stop();
    }

    bool RegionSelectorX11::start(SelectionType selection_type, mgl::Color border_color) {
        if(started)
            return false;

        if(!dpy) {
            fprintf(stderr, "Error: RegionSelectorX11::start: no X11 display\n");
            return false;
        }

        const unsigned long border_color_x11 = mgl_color_to_x11_color(border_color);

        xi_opcode = 0;
        if(!xinput_is_supported(dpy, &xi_opcode)) {
            fprintf(stderr, "Error: RegionSelectorX11::start: xinput not supported on your system\n");
            return false;
        }

        is_wayland = is_xwayland(dpy);
        monitors = get_monitors(dpy);

        Window x11_cursor_window = None;
        cursor_pos = get_cursor_position(dpy, &x11_cursor_window);
        region.pos = {0, 0};
        region.size = {0, 0};

        XVisualInfo vinfo;
        memset(&vinfo, 0, sizeof(vinfo));
        XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vinfo);
        region_window_colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);

        XSetWindowAttributes window_attr;
        window_attr.background_pixel = is_wayland ? 0 : border_color_x11;
        window_attr.border_pixel = 0;
        window_attr.override_redirect = true;
        window_attr.event_mask = StructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask;
        window_attr.colormap = region_window_colormap;

        Screen *screen = XDefaultScreenOfDisplay(dpy);
        region_window = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, XWidthOfScreen(screen), XHeightOfScreen(screen), 0,
            vinfo.depth, InputOutput, vinfo.visual, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask | CWColormap, &window_attr);
        if(!region_window) {
            fprintf(stderr, "Error: RegionSelectorX11::start: failed to create region window\n");
            stop();
            return false;
        }
        set_window_size_not_resizable(dpy, region_window, XWidthOfScreen(screen), XHeightOfScreen(screen));

        unsigned char data = 2; // Prefer being composed to allow transparency. Do this to prevent the compositor from getting turned on/off when taking a screenshot
        XChangeProperty(dpy, region_window, XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        if(!is_wayland) {
            cursor_window = create_cursor_window(dpy, cursor_window_size, cursor_window_size, &vinfo, border_color_x11, region_window_colormap);
            if(!cursor_window)
                fprintf(stderr, "Warning: RegionSelectorX11::start: failed to create cursor window\n");
            set_region_rectangle(dpy, region_window, 0, 0, 0, 0, 0);
        }

        XGCValues region_gc_values;
        memset(&region_gc_values, 0, sizeof(region_gc_values));
        region_gc_values.foreground = border_color_x11;
        region_gc_values.line_width = region_border_size;
        region_gc_values.line_style = LineSolid;
        region_gc = XCreateGC(dpy, region_window, GCForeground | GCLineWidth | GCLineStyle, &region_gc_values);

        XGCValues cursor_gc_values;
        memset(&cursor_gc_values, 0, sizeof(cursor_gc_values));
        cursor_gc_values.foreground = border_color_x11;
        cursor_gc_values.line_width = cursor_thickness;
        cursor_gc_values.line_style = LineSolid;
        cursor_gc = XCreateGC(dpy, region_window, GCForeground | GCLineWidth | GCLineStyle, &cursor_gc_values);

        if(!region_gc || !cursor_gc) {
            fprintf(stderr, "Error: RegionSelectorX11::start: failed to create gc\n");
            stop();
            return false;
        }

        XMapWindow(dpy, region_window);
        make_window_sticky(dpy, region_window);
        hide_window_from_taskbar(dpy, region_window);
        XFixesHideCursor(dpy, region_window);
        XGrabPointer(dpy, DefaultRootWindow(dpy), True, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync, CurrentTime);
        xi_grab_all_mouse_devices(dpy);
        XFlush(dpy);

        window_set_fullscreen(dpy, region_window, true);

        if(!is_wayland || x11_cursor_window)
            update_cursor_window(dpy, region_window, cursor_window, is_wayland, cursor_pos.x, cursor_pos.y, cursor_window_size, cursor_thickness, cursor_gc);

        if(cursor_window) {
            XMapWindow(dpy, cursor_window);
            make_window_sticky(dpy, cursor_window);
            hide_window_from_taskbar(dpy, cursor_window);
        }

        windows = query_windows(dpy);

        if(selection_type == SelectionType::WINDOW) {
            focused_window = get_window_by_position(windows, cursor_pos);
            if(focused_window)
                draw_rectangle_or_region(dpy, region_window, region_gc, region_border_size, is_wayland, focused_window->pos, focused_window->size);
        } else if(selection_type == SelectionType::REGION) {
            draw_rectangle_around_selected_monitor(dpy, region_window, region_gc, region_border_size, is_wayland, monitors, cursor_pos);
        }

        XFlush(dpy);
        selected = false;
        canceled = false;
        this->selection_type = selection_type;
        started = true;
        return true;
    }

    void RegionSelectorX11::stop() {
        if(!started)
            return;

        XWarpPointer(dpy, DefaultRootWindow(dpy), DefaultRootWindow(dpy), 0, 0, 0, 0, cursor_pos.x, cursor_pos.y);
        xi_warp_all_mouse_devices(dpy, cursor_pos);
        XFixesShowCursor(dpy, region_window);

        XUngrabPointer(dpy, CurrentTime);
        XUngrabKeyboard(dpy, CurrentTime);
        xi_ungrab_all_mouse_devices(dpy);
        XFlush(dpy);

        if(region_gc) {
            XFreeGC(dpy, region_gc);
            region_gc = nullptr;
        }

        if(cursor_gc) {
            XFreeGC(dpy, cursor_gc);
            cursor_gc = nullptr;
        }

        if(region_window_colormap) {
            XFreeColormap(dpy, region_window_colormap);
            region_window_colormap = 0;
        }

        if(cursor_window) {
            XDestroyWindow(dpy, cursor_window);
            cursor_window = 0;
        }

        if(region_window) {
            XDestroyWindow(dpy, region_window);
            region_window = 0;
        }

        XFlush(dpy);
        XSync(dpy, False);

        started = false;
        selecting_region = false;
        monitors.clear();
        windows.clear();
    }

    void RegionSelectorX11::cancel() {
        canceled = true;
        selected = false;
        stop();
    }

    bool RegionSelectorX11::is_started() const {
        return started;
    }

    bool RegionSelectorX11::failed() const {
        return !dpy;
    }

    void RegionSelectorX11::handle_event(void *native_event) {
        if(!started || selected || !native_event)
            return;

        XEvent *xev = (XEvent*)native_event;

        if(xev->type == KeyRelease && XKeycodeToKeysym(dpy, xev->xkey.keycode, 0) == XK_Escape) {
            cancel();
            return;
        }

        XGenericEventCookie *cookie = &xev->xcookie;
        if(cookie->type != GenericEvent || cookie->extension != xi_opcode)
            return;

        const bool fetched = XGetEventData(dpy, cookie);
        if(!fetched)
            return;

        const XIDeviceEvent *de = (XIDeviceEvent*)cookie->data;
        switch(cookie->evtype) {
            case XI_ButtonPress:   on_button_press(de);   break;
            case XI_ButtonRelease: on_button_release(de); break;
            case XI_Motion:        on_mouse_motion(de);   break;
        }
        XFreeEventData(dpy, cookie);

        if(selected)
            stop();
    }

    bool RegionSelectorX11::take_selection() {
        const bool result = selected;
        selected = false;
        return result;
    }

    bool RegionSelectorX11::take_canceled() {
        const bool result = canceled;
        canceled = false;
        return result;
    }

    Region RegionSelectorX11::get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const {
        assert(selection_type == SelectionType::REGION);
        Region returned_region = region;
        if(is_wayland && x11_dpy && wayland_dpy)
            returned_region = x11_region_to_wayland_region(x11_dpy, wayland_dpy, returned_region);
        return returned_region;
    }

    Window RegionSelectorX11::get_window_selection() const {
        assert(selection_type == SelectionType::WINDOW);
        if(focused_window)
            return focused_window->window;
        else
            return None;
    }

    RegionSelectorX11::SelectionType RegionSelectorX11::get_selection_type() const {
        return selection_type;
    }

    void RegionSelectorX11::on_button_press(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        if(device_event->detail != Button1)
            return;

        if(selection_type == SelectionType::REGION) {
            region.pos = { (int)device_event->root_x, (int)device_event->root_y };
            selecting_region = true;
        }
    }

    void RegionSelectorX11::on_button_release(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        if(device_event->detail != Button1)
            return;

        if(selection_type == SelectionType::WINDOW) {
            focused_window = get_window_by_position(windows, mgl::vec2i(device_event->root_x, device_event->root_y));
            if(focused_window) {
                const Window real_window = window_get_target_window_child(dpy, focused_window->window);
                XWindowAttributes win_attr;
                if(XGetWindowAttributes(dpy, real_window, &win_attr)) {
                    focused_window = RegionWindow{
                        real_window,
                        mgl::vec2i(win_attr.x, win_attr.y),
                        mgl::vec2i(win_attr.width, win_attr.height)
                    };
                }
            }
        } else if(selection_type == SelectionType::REGION) {
            if(!selecting_region)
                return;
        }

        if(is_wayland) {
            XClearWindow(dpy, region_window);
            XFlush(dpy);
        } else {
            set_region_rectangle(dpy, region_window, 0, 0, 0, 0, 0);
        }
        selecting_region = false;

        if(selection_type == SelectionType::WINDOW) {
            cursor_pos = { (int)device_event->root_x, (int)device_event->root_y };
        } else if(selection_type == SelectionType::REGION) {
            cursor_pos = region.pos + region.size;
        }

        if(region.size.x < 0) {
            region.pos.x += region.size.x;
            region.size.x = abs(region.size.x);
        }

        if(region.size.y < 0) {
            region.pos.y += region.size.y;
            region.size.y = abs(region.size.y);
        }

        if(region.size.x > 0)
            region.size.x += 1;

        if(region.size.y > 0)
            region.size.y += 1;

        selected = true;
    }

    void RegionSelectorX11::on_mouse_motion(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        XClearWindow(dpy, region_window);

        if(selecting_region) {
            region.size.x = device_event->root_x - region.pos.x;
            region.size.y = device_event->root_y - region.pos.y;
            cursor_pos = region.pos + region.size;

            draw_rectangle_or_region(dpy, region_window, region_gc, region_border_size, is_wayland, region.pos, region.size);
        } else if(selection_type == SelectionType::WINDOW) {
            cursor_pos = { (int)device_event->root_x, (int)device_event->root_y };

            focused_window = get_window_by_position(windows, cursor_pos);
            if(focused_window)
                draw_rectangle_or_region(dpy, region_window, region_gc, region_border_size, is_wayland, focused_window->pos, focused_window->size);
            else
                draw_rectangle_or_region(dpy, region_window, region_gc, region_border_size, is_wayland, mgl::vec2i(0, 0), mgl::vec2i(0, 0));
        } else if(selection_type == SelectionType::REGION) {
            cursor_pos = { (int)device_event->root_x, (int)device_event->root_y };
            draw_rectangle_around_selected_monitor(dpy, region_window, region_gc, region_border_size, is_wayland, monitors, cursor_pos);
        }

        update_cursor_window(dpy, region_window, cursor_window, is_wayland, cursor_pos.x, cursor_pos.y, cursor_window_size, cursor_thickness, cursor_gc);
        XFlush(dpy);
    }
}