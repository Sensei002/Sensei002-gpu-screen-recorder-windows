#include "../../include/CursorTracker/CursorTrackerWin32.hpp"

#include <windows.h>
#include <stdio.h>

namespace gsr {
    CursorTrackerWin32::CursorTrackerWin32() {

    }

    std::optional<CursorInfo> CursorTrackerWin32::get_latest_cursor_info() {
        POINT cursor_pos;
        if(!GetCursorPos(&cursor_pos))
            return std::nullopt;

        HMONITOR monitor = MonitorFromPoint(cursor_pos, MONITOR_DEFAULTTONULL);
        if(!monitor)
            return std::nullopt;

        MONITORINFOEXA monitor_info;
        memset(&monitor_info, 0, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        if(!GetMonitorInfoA(monitor, &monitor_info))
            return std::nullopt;

        return CursorInfo{
            mgl::vec2i{ cursor_pos.x, cursor_pos.y },
            std::string(monitor_info.szDevice)
        };
    }
}
