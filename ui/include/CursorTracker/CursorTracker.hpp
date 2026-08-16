#pragma once

#include <optional>
#include <string>
#include <mglpp/system/vec.hpp>

namespace gsr {
    struct CursorInfo {
        mgl::vec2i position;
        std::string monitor_name;
    };

    class CursorTracker {
    public:
        CursorTracker() = default;
        CursorTracker(const CursorTracker&) = delete;
        CursorTracker& operator=(const CursorTracker&) = delete;
        virtual ~CursorTracker() = default;

        virtual void update() = 0;
        virtual std::optional<CursorInfo> get_latest_cursor_info() = 0;
    };
}