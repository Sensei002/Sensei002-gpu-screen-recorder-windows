#pragma once

#include "DesktopEnvironment.hpp"
#include <X11/Xlib.h>

namespace gsr {
    class DesktopEnvironmentX11 : public DesktopEnvironment {
    public:
        DesktopEnvironmentX11(Display *dpy) : dpy(dpy) {}
        DesktopEnvironmentX11(const DesktopEnvironmentX11&) = delete;
        DesktopEnvironmentX11& operator=(const DesktopEnvironmentX11&) = delete;
        ~DesktopEnvironmentX11();

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;
        //std::string get_focused_monitor_name() override;
    private:
        Display *dpy = NULL;
    };
}