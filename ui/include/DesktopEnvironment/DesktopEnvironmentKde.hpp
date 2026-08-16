#pragma once

#include "DesktopEnvironment.hpp"
#include <sys/types.h>

namespace gsr {
    class DesktopEnvironmentKde : public DesktopEnvironment {
    public:
        DesktopEnvironmentKde() = default;
        DesktopEnvironmentKde(const DesktopEnvironmentKde&) = delete;
        DesktopEnvironmentKde& operator=(const DesktopEnvironmentKde&) = delete;
        ~DesktopEnvironmentKde();

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;
        //std::string get_focused_monitor_name() override;
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
    };
}