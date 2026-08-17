#pragma once

#include "CursorTracker.hpp"

namespace gsr {
    // Tracks the cursor via GetCursorPos and reports the monitor name under
    // the cursor via MonitorFromPoint + GetMonitorInfoW.
    class CursorTrackerWin32 : public CursorTracker {
    public:
        CursorTrackerWin32();
        CursorTrackerWin32(const CursorTrackerWin32&) = delete;
        CursorTrackerWin32& operator=(const CursorTrackerWin32&) = delete;
        ~CursorTrackerWin32() = default;

        void update() override {}
        std::optional<CursorInfo> get_latest_cursor_info() override;
    };
}
