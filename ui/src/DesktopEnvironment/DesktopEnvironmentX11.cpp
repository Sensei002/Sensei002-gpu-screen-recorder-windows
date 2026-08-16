#include "../../include/DesktopEnvironment/DesktopEnvironmentX11.hpp"
#include "../../include/WindowUtils.hpp"

namespace gsr {
    DesktopEnvironmentX11::~DesktopEnvironmentX11() {
        
    }

    bool DesktopEnvironmentX11::start() {
        return true;
    }

    void DesktopEnvironmentX11::update() {
        
    }

    std::string DesktopEnvironmentX11::get_focused_window_title() {
        std::string focused_window_title;
        if(!dpy) {
            fprintf(stderr, "Error: DesktopEnvironmentX11: get_focused_window_title: display object is NULL, returning empty string\n");
            return focused_window_title;
        }

        focused_window_title = get_window_name_at_cursor_position(dpy, "gsr-ui");
        if(focused_window_title.empty())
            focused_window_title = get_focused_window_name(dpy, WindowCaptureType::FOCUSED, false);

        return focused_window_title;
    }

    // std::string DesktopEnvironmentX11::get_focused_monitor_name() {
    //     return "";
    // }
}
