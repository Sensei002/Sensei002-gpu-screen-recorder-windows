#pragma once

#include "CursorTracker.hpp"

typedef struct _XDisplay Display;

namespace gsr {
    class CursorTrackerX11 : public CursorTracker {
    public:
        CursorTrackerX11(Display *dpy);
        CursorTrackerX11(const CursorTrackerX11&) = delete;
        CursorTrackerX11& operator=(const CursorTrackerX11&) = delete;
        ~CursorTrackerX11() = default;

        void update() override {}
        std::optional<CursorInfo> get_latest_cursor_info() override;
    private:
        Display *dpy = nullptr;
    };
}