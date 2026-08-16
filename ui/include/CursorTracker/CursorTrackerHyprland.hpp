#pragma once

#include "CursorTracker.hpp"

namespace gsr {
    class CursorTrackerHyprland : public CursorTracker {
    public:
        static bool is_supported();

        CursorTrackerHyprland();
        CursorTrackerHyprland(const CursorTrackerHyprland&) = delete;
        CursorTrackerHyprland& operator=(const CursorTrackerHyprland&) = delete;
        ~CursorTrackerHyprland() = default;

        void update() override {}
        std::optional<CursorInfo> get_latest_cursor_info() override;
    };
}