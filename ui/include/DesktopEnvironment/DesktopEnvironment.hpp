#pragma once

#include <string>

namespace gsr {
    class DesktopEnvironment {
    public:
        DesktopEnvironment() = default;
        DesktopEnvironment(const DesktopEnvironment&) = delete;
        DesktopEnvironment& operator=(const DesktopEnvironment&) = delete;
        virtual ~DesktopEnvironment() = default;

        virtual bool start() = 0;
        virtual void update() = 0;
        // Return an empty string if none
        virtual std::string get_focused_window_title() = 0;
        // Return an empty string if unknown
        //virtual std::string get_focused_monitor_name() = 0;
    };
}