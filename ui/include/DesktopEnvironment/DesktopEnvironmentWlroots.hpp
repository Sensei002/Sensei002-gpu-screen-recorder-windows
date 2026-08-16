#pragma once

#include "DesktopEnvironment.hpp"
#include <memory>

struct wl_display;

namespace gsr {
    class DesktopEnvironmentWlroots : public DesktopEnvironment {
    public:
        static bool is_supported(struct wl_display *dpy);

        explicit DesktopEnvironmentWlroots(struct wl_display *dpy);
        DesktopEnvironmentWlroots(const DesktopEnvironmentWlroots&) = delete;
        DesktopEnvironmentWlroots& operator=(const DesktopEnvironmentWlroots&) = delete;
        ~DesktopEnvironmentWlroots() override;

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;

        struct Impl;
    private:
        std::unique_ptr<Impl> impl;
    };
}
