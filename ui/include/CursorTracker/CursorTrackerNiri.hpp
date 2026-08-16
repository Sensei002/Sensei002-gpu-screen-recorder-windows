#pragma once

#include "CursorTracker.hpp"

namespace gsr {
    class CursorTrackerNiri : public CursorTracker {
    public:
        static bool is_supported();

        CursorTrackerNiri();
        CursorTrackerNiri(const CursorTrackerNiri&) = delete;
        CursorTrackerNiri& operator=(const CursorTrackerNiri&) = delete;
        ~CursorTrackerNiri() = default;

        void update() override {}
        std::optional<CursorInfo> get_latest_cursor_info() override;
    };
}