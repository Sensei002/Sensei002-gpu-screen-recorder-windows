#include "../../include/DesktopEnvironment/DesktopEnvironmentKde.hpp"
#include "../../include/Process.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

namespace gsr {
    static constexpr std::string_view prefix_title = "Active window title set to: ";
    static constexpr std::string_view prefix_fullscreen = "Active window fullscreen state set to: ";
    static constexpr std::string_view prefix_monitor = "Active window monitor name set to: ";

    DesktopEnvironmentKde::~DesktopEnvironmentKde() {
        shutdown();
    }

    bool DesktopEnvironmentKde::start() {
        if(process_id > 0) {
            fprintf(stderr, "Error: DesktopEnvironmentKde: already running\n");
            return false;
        }

        const char *args[] = { "gsr-kwin-helper", NULL };
        process_id = exec_program(args, &read_fd, false);
        if(process_id == -1) {
            fprintf(stderr, "Error: DesktopEnvironmentKde: failed to execute gsr-kwin-helper\n");
            return false;
        }

        fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

        stdout_file = fdopen(read_fd, "r");
        if (!stdout_file) {
            perror("Error: DesktopEnvironmentKde: fdopen");
            shutdown();
            return false;
        }
        read_fd = -1;

        fprintf(stderr, "Info: DesktopEnvironmentKde: started kwin helper process\n");
        return true;
    }

    void DesktopEnvironmentKde::shutdown() {
        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            if(waitpid(process_id, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            process_id = -1;
        }

        if(stdout_file) {
            fclose(stdout_file);
            stdout_file = nullptr;
        }

        if(read_fd > 0) {
            close(read_fd);
            read_fd = -1;
        }
    }

    void DesktopEnvironmentKde::update() {
        while (fgets(line_buffer, sizeof(line_buffer), stdout_file) != nullptr) {
            line = line_buffer;

            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }

            size_t pos = std::string::npos;
            if ((pos = line.find(prefix_title)) != std::string::npos) {
                window_title = line.substr(pos + prefix_title.length());
            } else if ((pos = line.find(prefix_fullscreen)) != std::string::npos) {
                //fullscreen = line.substr(pos + prefix_fullscreen.length()) == "1";
            } else if ((pos = line.find(prefix_monitor)) != std::string::npos) {
                monitor_name = line.substr(pos + prefix_monitor.length());
            }
        }
    }

    std::string DesktopEnvironmentKde::get_focused_window_title() {
        return window_title;
    }

    // std::string DesktopEnvironmentKde::get_focused_monitor_name() {
    //     return monitor_name;
    // }
}