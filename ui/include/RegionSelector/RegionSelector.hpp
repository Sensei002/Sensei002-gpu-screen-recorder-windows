#pragma once

#include "../WindowUtils.hpp"
#include <mglpp/system/vec.hpp>
#include <mglpp/graphics/Color.hpp>

typedef struct _XDisplay Display;
struct wl_display;

namespace gsr {
    struct Region {
        mgl::vec2i pos;
        mgl::vec2i size;
    };

    struct RegionWindow {
        Window window = None;
        mgl::vec2i pos;
        mgl::vec2i size;
    };

    class RegionSelector {
    public:
        enum class SelectionType {
            NONE,
            REGION,
            WINDOW
        };

        RegionSelector() = default;
        RegionSelector(const RegionSelector&) = delete;
        RegionSelector& operator=(const RegionSelector&) = delete;
        virtual ~RegionSelector() = default;

        virtual bool start(SelectionType selection_type, mgl::Color border_color) = 0;
        virtual void stop() = 0;
        virtual void cancel() = 0;
        virtual bool is_started() const = 0;

        virtual bool failed() const = 0;
        virtual void handle_event(void *native_event) = 0;
        virtual bool take_selection() = 0;
        virtual bool take_canceled() = 0;
        virtual Region get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const = 0;
        // Returns None if none is selected
        virtual Window get_window_selection() const = 0;

        virtual SelectionType get_selection_type() const = 0;
    };
}
