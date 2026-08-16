#pragma once

#include "CursorTracker.hpp"

namespace gsr {
    class CursorTrackerSway : public CursorTracker {
    public:
        static bool is_supported();

        CursorTrackerSway();
        CursorTrackerSway(const CursorTrackerSway&) = delete;
        CursorTrackerSway& operator=(const CursorTrackerSway&) = delete;
        ~CursorTrackerSway() = default;

        void update() override {}
        std::optional<CursorInfo> get_latest_cursor_info() override;
    };
}