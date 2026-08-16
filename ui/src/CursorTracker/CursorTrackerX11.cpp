#include "../../include/CursorTracker/CursorTrackerX11.hpp"
#include "../../include/WindowUtils.hpp"

namespace gsr {
    CursorTrackerX11::CursorTrackerX11(Display *dpy) : dpy(dpy) {
        
    }

    std::optional<CursorInfo> CursorTrackerX11::get_latest_cursor_info() {
        Window window = None;
        const auto cursor_pos = get_cursor_position(dpy, &window);
        const auto monitors = get_monitors(dpy);
        std::string monitor_name;

        for(const auto &monitor : monitors) {
            if(cursor_pos.x >= monitor.position.x && cursor_pos.x <= monitor.position.x + monitor.size.x
                && cursor_pos.y >= monitor.position.y && cursor_pos.y <= monitor.position.y + monitor.size.y)
            {
                monitor_name = monitor.name;
                break;
            }
        }

        if(monitor_name.empty())
            return std::nullopt;

        return CursorInfo{ cursor_pos, std::move(monitor_name) };
    }
}