/* WindowUtilsWin32.cpp — Windows implementation of the gsr::WindowUtils API.
 *
 * The upstream WindowUtils.cpp is X11/Wayland-only; this file provides the
 * same function surface (see WindowUtils.hpp) on Windows so Overlay.cpp's
 * platform-neutral code paths (monitor lookup, capture target names,
 * fullscreen checks) work without X11. The Display* parameters are ignored
 * (x11_dpy is NULL on Windows); Window is an HWND-sized opaque handle.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "../include/WindowUtils.hpp"
#include "../include/Utils.hpp"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace gsr {
    /* ---- window titles --------------------------------------------------- */

    static std::string utf16_to_utf8(const wchar_t *str, int length) {
        std::string result;
        if(length <= 0)
            return result;
        const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, str, length, NULL, 0, NULL, NULL);
        if(utf8_size > 0) {
            result.resize(utf8_size);
            WideCharToMultiByte(CP_UTF8, 0, str, length, &result[0], utf8_size, NULL, NULL);
        }
        return result;
    }

    std::string window_title_utf8_sanitize(const char *str, int size) {
        /* Strip control characters that would break the recording folder name. */
        std::string result;
        result.reserve(size);
        for(int i = 0; i < size; ++i) {
            const unsigned char c = (unsigned char)str[i];
            if(c < 0x20 && c != '\t')
                result += ' ';
            else if(c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                result += '_';
            else
                result += (char)c;
        }
        return result;
    }

    std::optional<std::string> get_window_title(Display *dpy, Window window) {
        (void)dpy;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd || !IsWindow(hwnd))
            return std::nullopt;

        wchar_t buffer[1024];
        const int length = GetWindowTextW(hwnd, buffer, sizeof(buffer) / sizeof(wchar_t));
        if(length <= 0)
            return std::nullopt;

        return utf16_to_utf8(buffer, length);
    }

    /* ---- focused window / capture target name --------------------------- */

    Window get_focused_window(Display *dpy, WindowCaptureType cap_type, bool fallback_cursor_focused) {
        (void)dpy;
        HWND hwnd = GetForegroundWindow();
        if(!hwnd && fallback_cursor_focused) {
            POINT cursor_pos;
            if(GetCursorPos(&cursor_pos))
                hwnd = WindowFromPoint(cursor_pos);
        }
        if(!hwnd)
            return 0;
        if(!IsWindow(hwnd))
            return 0;
        (void)cap_type;
        return (Window)(uintptr_t)hwnd;
    }

    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type, bool fallback_cursor_focused) {
        const Window window = get_focused_window(dpy, window_capture_type, fallback_cursor_focused);
        const std::optional<std::string> title = get_window_title(dpy, window);
        if(!title)
            return "";
        return window_title_utf8_sanitize(title->c_str(), title->size());
    }

    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, std::string_view ignore_window_title) {
        (void)dpy;
        HWND hwnd = WindowFromPoint(POINT{ position.x, position.y });
        if(!hwnd)
            return "";

        const std::optional<std::string> title = get_window_title(nullptr, (Window)(uintptr_t)hwnd);
        if(!title)
            return "";
        if(!ignore_window_title.empty() && *title == ignore_window_title)
            return "";
        return window_title_utf8_sanitize(title->c_str(), title->size());
    }

    std::string get_window_name_at_cursor_position(Display *dpy, std::string_view ignore_window_title) {
        POINT cursor_pos;
        if(!GetCursorPos(&cursor_pos))
            return "";
        return get_window_name_at_position(dpy, mgl::vec2i{ cursor_pos.x, cursor_pos.y }, ignore_window_title);
    }

    void set_window_size_not_resizable(Display *dpy, Window window, int width, int height) {
        (void)dpy;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return;
        SetWindowPos(hwnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    Window window_get_target_window_child(Display *display, Window window) {
        (void)display;
        return window;
    }

    unsigned char* window_get_property(Display *dpy, Window window, Atom property_type, const char *property_name, unsigned int *property_size) {
        (void)dpy;
        (void)window;
        (void)property_type;
        (void)property_name;
        if(property_size)
            *property_size = 0;
        return nullptr;
    }

    mgl::vec2i get_cursor_position(Display *dpy, Window *window) {
        (void)dpy;
        POINT cursor_pos;
        mgl::vec2i result{ 0, 0 };
        if(GetCursorPos(&cursor_pos))
            result = mgl::vec2i{ cursor_pos.x, cursor_pos.y };
        if(window)
            *window = (Window)(uintptr_t)WindowFromPoint(cursor_pos);
        return result;
    }

    mgl::vec2i create_window_get_center_position(Display *display) {
        (void)display;
        /* Center of the primary monitor's work area. */
        RECT work_area;
        if(SystemParametersInfoA(SPI_GETWORKAREA, 0, &work_area, 0))
            return mgl::vec2i{ work_area.left + (work_area.right - work_area.left) / 2, work_area.top + (work_area.bottom - work_area.top) / 2 };
        return mgl::vec2i{ 0, 0 };
    }

    std::string get_window_manager_name(Display *display) {
        (void)display;
        return "Windows";
    }

    bool is_compositor_running(Display *dpy, int screen) {
        (void)dpy;
        (void)screen;
        return false;
    }

    /* ---- monitors -------------------------------------------------------- */

    struct MonitorEnumData {
        std::vector<Monitor> monitors;
    };

    static BOOL CALLBACK enum_monitors_proc(HMONITOR hmonitor, HDC hdc, LPRECT rect, LPARAM lparam) {
        (void)hdc;
        (void)rect;
        MonitorEnumData *data = (MonitorEnumData*)lparam;

        MONITORINFOEXA monitor_info;
        memset(&monitor_info, 0, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        if(!GetMonitorInfoA(hmonitor, &monitor_info))
            return TRUE;

        Monitor monitor;
        monitor.position.x = monitor_info.rcMonitor.left;
        monitor.position.y = monitor_info.rcMonitor.top;
        monitor.size.x = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
        monitor.size.y = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
        monitor.name = monitor_info.szDevice;
        data->monitors.push_back(std::move(monitor));
        return TRUE;
    }

    std::vector<Monitor> get_monitors(Display *dpy) {
        (void)dpy;
        MonitorEnumData data;
        EnumDisplayMonitors(NULL, NULL, enum_monitors_proc, (LPARAM)&data);
        return std::move(data.monitors);
    }

    std::vector<Monitor> get_monitors_wayland(struct wl_display *dpy) {
        (void)dpy;
        return get_monitors(nullptr);
    }

    void xi_grab_all_mouse_devices(Display *dpy) {
        (void)dpy;
    }

    void xi_ungrab_all_mouse_devices(Display *dpy) {
        (void)dpy;
    }

    void xi_warp_all_mouse_devices(Display *dpy, mgl::vec2i position) {
        (void)dpy;
        SetCursorPos(position.x, position.y);
    }

    void window_set_fullscreen(Display *dpy, Window window, bool fullscreen) {
        (void)dpy;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return;
        /* Best-effort: a plain maximize loses the borderless look, so go
           through mgl where possible. This helper is used by the overlay's
           fullscreen path; SetWindowPos to the monitor rect is the fallback. */
        if(fullscreen) {
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi;
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            if(monitor && GetMonitorInfoA(monitor, &mi))
                SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            ShowWindow(hwnd, SW_RESTORE);
        }
    }

    bool window_is_fullscreen(Display *display, Window window) {
        (void)display;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return false;

        RECT window_rect;
        if(!GetWindowRect(hwnd, &window_rect))
            return false;

        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if(!monitor || !GetMonitorInfoA(monitor, &mi))
            return false;

        return window_rect.left == mi.rcMonitor.left && window_rect.top == mi.rcMonitor.top
            && window_rect.right == mi.rcMonitor.right && window_rect.bottom == mi.rcMonitor.bottom;
    }

    bool get_drawable_geometry(Display *display, Drawable drawable, DrawableGeometry *geometry) {
        (void)display;
        HWND hwnd = (HWND)(uintptr_t)drawable;
        if(!geometry || !hwnd)
            return false;

        RECT rect;
        if(!GetWindowRect(hwnd, &rect))
            return false;

        geometry->x = rect.left;
        geometry->y = rect.top;
        geometry->width = rect.right - rect.left;
        geometry->height = rect.bottom - rect.top;
        return true;
    }

    std::optional<Monitor> get_monitor_by_window_center(Display *display, Window window) {
        (void)display;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return std::nullopt;

        RECT rect;
        if(!GetWindowRect(hwnd, &rect))
            return std::nullopt;

        const POINT center = { (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
        HMONITOR hmonitor = MonitorFromPoint(center, MONITOR_DEFAULTTONULL);
        if(!hmonitor)
            return std::nullopt;

        MONITORINFOEXA monitor_info;
        memset(&monitor_info, 0, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        if(!GetMonitorInfoA(hmonitor, &monitor_info))
            return std::nullopt;

        Monitor monitor;
        monitor.position.x = monitor_info.rcMonitor.left;
        monitor.position.y = monitor_info.rcMonitor.top;
        monitor.size.x = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
        monitor.size.y = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
        monitor.name = monitor_info.szDevice;
        return monitor;
    }

    bool set_window_wm_state(Display *dpy, Window window, Atom atom) {
        (void)dpy;
        (void)window;
        (void)atom;
        return true;
    }

    void make_window_click_through(Display *display, Window window) {
        // No-op on Windows. Click-through is an X11 concept: the X11 path pairs it
        // with an X input grab so the overlay still receives every event. On Windows
        // there is no input grab, and the naive port that set
        // WS_EX_LAYERED | WS_EX_TRANSPARENT here did two harmful things:
        //   1. WS_EX_LAYERED breaks WGL content compositing on NVIDIA + Win11 — the
        //      GL back buffer never appears and the overlay renders as a bare
        //      dim/black layer (mgl's win32 backend deliberately avoids WS_EX_LAYERED
        //      and uses PFD_SUPPORT_COMPOSITION + DwmEnableBlurBehindWindow instead).
        //   2. WS_EX_TRANSPARENT makes the interactive overlay click-through (no input
        //      grab redirects events back to it), so the user cannot click the
        //      Record / Instant Replay / Settings buttons at all.
        (void)display;
        (void)window;
        // The overlay stays interactive and composited; hide_window_from_taskbar
        // (WS_EX_TOOLWINDOW) and make_window_sticky (HWND_TOPMOST) are enough.
    }

    bool make_window_sticky(Display *dpy, Window window) {
        (void)dpy;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return false;
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return true;
    }

    bool hide_window_from_taskbar(Display *dpy, Window window) {
        (void)dpy;
        HWND hwnd = (HWND)(uintptr_t)window;
        if(!hwnd)
            return false;
        const LONG_PTR ex_style = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, ex_style | WS_EX_TOOLWINDOW);
        return true;
    }
}
