#pragma once

#include "DesktopEnvironment.hpp"
#include <X11/Xlib.h>

namespace gsr {
    class DesktopEnvironmentGnome : public DesktopEnvironment {
    public:
        DesktopEnvironmentGnome(Display *dpy) : x11_dpy(dpy) {}
        DesktopEnvironmentGnome(const DesktopEnvironmentGnome&) = delete;
        DesktopEnvironmentGnome& operator=(const DesktopEnvironmentGnome&) = delete;
        ~DesktopEnvironmentGnome();

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;
    private:
        void shutdown();
    private:
        pid_t process_id = -1;
        FILE *stdout_file = nullptr;
        int read_fd = -1;
        char line_buffer[1024];
        std::string line;

        std::string window_title;
        std::string monitor_name;

        Display *x11_dpy = NULL;
    };
}
