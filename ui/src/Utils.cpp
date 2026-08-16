#include "../include/Utils.hpp"
#include "../include/Process.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <optional>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern "C" {
#include <mgl/system/clock.h>
}

namespace gsr {
    static std::optional<std::string> get_xdg_autostart_content() {
        const char *args[] = {
            "/bin/sh", "-c",
            "cat \"${XDG_CONFIG_HOME:-$HOME/.config}/autostart/gpu-screen-recorder-ui.desktop\"",
            nullptr
        };
        std::string output;
        if(exec_program_on_host_get_stdout(args, output, false) != 0)
            return std::nullopt;
        return output;
    }

    // Returns the exit status or -1 on timeout
    static int run_command_timeout(const char **args, double sleep_time_sec, double timeout_sec) {
        mgl_clock clock;
        mgl_clock_init(&clock);

        do {
            int read_fd = 0;
            const pid_t process_id = exec_program(args, &read_fd, false);
            if(process_id <= 0)
                continue;

            const double time_elapsed_sleep_start = mgl_clock_get_elapsed_time_seconds(&clock);
            pid_t waitpid_result = 0;
            do {
                int status = 0;
                waitpid_result = waitpid(process_id, &status, WNOHANG);
                if(waitpid_result > 0)
                    break;

                usleep(30 * 1000); // 30ms
            } while(mgl_clock_get_elapsed_time_seconds(&clock) - time_elapsed_sleep_start < sleep_time_sec);

            int status = 0;
            if(waitpid_result > 0) {
                int exit_status = -0;
                if(WIFEXITED(status))
                    exit_status = -1;

                if(exit_status == 0)
                    exit_status = WEXITSTATUS(status);

                close(read_fd);
                return exit_status;
            } else {
                kill(process_id, SIGKILL);
                waitpid(process_id, &status, 0);
                close(read_fd);
            }
        } while(mgl_clock_get_elapsed_time_seconds(&clock) < timeout_sec);

        return -1;
    }

    void string_split_char(std::string_view str, char delimiter, StringSplitCallback callback_func) {
        size_t index = 0;
        while(index < str.size()) {
            size_t new_index = str.find(delimiter, index);
            if(new_index == std::string_view::npos)
                new_index = str.size();

            if(!callback_func(str.substr(index, new_index - index)))
                break;

            index = new_index + 1;
        }
    }

    bool starts_with(std::string_view str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data(), substr, len) == 0;
    }

    bool starts_with(std::string_view str, std::string_view substr) {
        size_t len = substr.size();
        return str.size() >= len && memcmp(str.data(), substr.data(), len) == 0;
    }

    bool ends_with(std::string_view str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data() + str.size() - len, substr, len) == 0;
    }

    std::string strip(const std::string &str) {
        int start_index = 0;
        int str_len = str.size();

        for(int i = 0; i < str_len; ++i) {
            if(str[i] != ' ') {
                start_index += i;
                str_len -= i;
                break;
            }
        }

        for(int i = str_len - 1; i >= 0; --i) {
            if(str[i] != ' ') {
                str_len = i + 1;
                break;
            }
        }

        return str.substr(start_index, str_len);
    }

    std::string get_home_dir() {
        const char *home_dir = getenv("HOME");
        if(!home_dir) {
            passwd *pw = getpwuid(getuid());
            home_dir = pw->pw_dir;
        }

        if(!home_dir) {
            fprintf(stderr, "Error: Failed to get home directory of user, using /tmp directory\n");
            home_dir = "/tmp";
        }

        return home_dir;
    }

    std::string get_config_dir() {
        std::string config_dir;
        const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
        if(xdg_config_home) {
            config_dir = xdg_config_home;
        } else {
            config_dir = get_home_dir() + "/.config";
        }
        config_dir += "/gpu-screen-recorder";
        return config_dir;
    }

    // Whoever designed xdg-user-dirs is retarded. Why are some XDG variables environment variables
    // while others are in this pseudo shell config file ~/.config/user-dirs.dirs
    std::map<std::string, std::string> get_xdg_variables() {
        std::string user_dirs_filepath;
        const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
        if(xdg_config_home) {
            user_dirs_filepath = xdg_config_home;
        } else {
            user_dirs_filepath = get_home_dir() + "/.config";
        }

        user_dirs_filepath += "/user-dirs.dirs";

        std::map<std::string, std::string> result;
        FILE *f = fopen(user_dirs_filepath.c_str(), "rb");
        if(!f)
            return result;

        char line[PATH_MAX];
        while(fgets(line, sizeof(line), f)) {
            int len = strlen(line);
            if(len < 2)
                continue;

            if(line[0] == '#')
                continue;

            if(line[len - 1] == '\n') {
                line[len - 1] = '\0';
                len--;
            }

            if(line[len - 1] != '"')
                continue;

            line[len - 1] = '\0';
            len--;

            const char *sep = strchr(line, '=');
            if(!sep)
                continue;

            if(sep[1] != '\"')
                continue;

            std::string value(sep + 2);
            if(strncmp(value.c_str(), "$HOME/", 6) == 0)
                value = get_home_dir() + value.substr(5);

            std::string key(line, sep - line);
            result[std::move(key)] = std::move(value);
        }

        fclose(f);
        return result;
    }

    std::string get_videos_dir() {
        auto xdg_vars = get_xdg_variables();
        std::string xdg_videos_dir = xdg_vars["XDG_VIDEOS_DIR"];
        if(xdg_videos_dir.empty())
            xdg_videos_dir = get_home_dir() + "/Videos";
        return xdg_videos_dir;
    }

     std::string get_pictures_dir() {
        auto xdg_vars = get_xdg_variables();
        std::string xdg_videos_dir = xdg_vars["XDG_PICTURES_DIR"];
        if(xdg_videos_dir.empty())
            xdg_videos_dir = get_home_dir() + "/Pictures";
        return xdg_videos_dir;
    }

    int create_directory_recursive(char *path) {
        int path_len = strlen(path);
        char *p = path;
        char *end = path + path_len;
        for(;;) {
            char *slash_p = strchr(p, '/');

            // Skips first '/', we don't want to try and create the root directory
            if(slash_p == path) {
                ++p;
                continue;
            }

            if(!slash_p)
                slash_p = end;

            char prev_char = *slash_p;
            *slash_p = '\0';
            int err = mkdir(path, S_IRWXU);
            *slash_p = prev_char;

            if(err == -1 && errno != EEXIST)
                return err;

            if(slash_p == end)
                break;
            else
                p = slash_p + 1;
        }
        return 0;
    }

    bool file_get_content(const char *filepath, std::string &file_content) {
        file_content.clear();
        bool success = false;

        FILE *file = fopen(filepath, "rb");
        if(!file)
            return success;

        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        if(file_size != -1) {
            file_content.resize(file_size);
            fseek(file, 0, SEEK_SET);
            if((long)fread(&file_content[0], 1, file_size, file) == file_size)
                success = true;
        }

        fclose(file);
        return success;
    }

    bool file_overwrite(const char *filepath, const std::string &data) {
        bool success = false;

        FILE *file = fopen(filepath, "wb");
        if(!file)
            return success;

        if(fwrite(data.data(), 1, data.size(), file) == data.size())
            success = true;

        fclose(file);
        return success;
    }

    std::string get_parent_directory(std::string_view directory) {
        std::string result;

        while(directory.size() > 1 && directory.back() == '/') {
            directory.remove_suffix(1);
        }

        const size_t prev_slash_index = directory.rfind('/');
        if(prev_slash_index == 0) {
            result = "/";
        } else if(prev_slash_index == std::string_view::npos) {
            result = ".";
        } else {
            result = directory.substr(0, prev_slash_index);
        }
        return result;
    }

    bool is_xdg_autostart_enabled() {
        const std::optional<std::string> output = get_xdg_autostart_content();
        return output.has_value() && output.value().find("Hidden=true") == std::string::npos;
    }

    int set_xdg_autostart(bool enable) {
        const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
        if(!xdg_current_desktop || strlen(xdg_current_desktop) == 0) {
            std::string output;
            const char *check_dex_args[] = { "/bin/sh", "-c", "command -v dex", nullptr };
            if(exec_program_on_host_get_stdout(check_dex_args, output, true) != 0)
                return 67;
        }

        const bool is_flatpak = getenv("FLATPAK_ID") != nullptr;
        const char *exec_line = is_flatpak
            ? "Exec=flatpak run com.dec05eba.gpu_screen_recorder gsr-ui"
            : "Exec=gsr-ui launch-daemon";

        std::string content =
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=GPU Screen Recorder\n"
            "GenericName=Screen recorder\n"
            "Comment=A ShadowPlay-like screen recorder for Linux\n"
            "Icon=gpu-screen-recorder\n" +
            std::string(exec_line) + "\n" +
            "Terminal=false\n" +
            "Hidden=" + (enable ? "false" : "true") + "\n";

        std::string shell_cmd =
            "p=\"${XDG_CONFIG_HOME:-$HOME/.config}/autostart/gpu-screen-recorder-ui.desktop\" && "
            "mkdir -p \"$(dirname \"$p\")\" && "
            "printf '" + content + "' > \"$p\"";

        const char *args[] = { "/bin/sh", "-c", shell_cmd.c_str(), nullptr };
        std::string dummy;
        return exec_program_on_host_get_stdout(args, dummy, true);
    }

    void replace_xdg_autostart_with_current_gsr_type() {
        const std::optional<std::string> output = get_xdg_autostart_content();
        if(!output.has_value())
            return;

        const bool is_flatpak = getenv("FLATPAK_ID") != nullptr;
        const bool is_exec_flatpak = output.value().find("flatpak run") != std::string::npos;
        if(is_flatpak != is_exec_flatpak) {
            const bool is_autostart_enabled = output.value().find("Hidden=true") == std::string::npos;
            set_xdg_autostart(is_autostart_enabled);
        }
    }

    bool wait_until_systemd_user_service_available() {
        const char *args[] = { "systemctl", "--user", "-q", "is-enabled", "gpu-screen-recorder-ui.service", nullptr };
        const char *flatpak_args[] = { "flatpak-spawn", "--host", "--", "systemctl", "--user", "-q", "is-enabled", "gpu-screen-recorder-ui.service", nullptr };
        const bool is_flatpak = getenv("FLATPAK_ID") != nullptr;
        return run_command_timeout(is_flatpak ? flatpak_args : args, 1.0, 5.0) >= 0;
    }

    bool is_systemd_service_enabled(const char *service_name) {
        const char *args[] = { "systemctl", "--user", "is-enabled", service_name, nullptr };
        std::string output;
        return exec_program_on_host_get_stdout(args, output, false) == 0;
    }

    bool disable_systemd_service(const char *service_name) {
        const char *args[] = { "systemctl", "--user", "disable", service_name, nullptr };
        std::string output;
        return exec_program_on_host_get_stdout(args, output, false) == 0;
    }

    bool is_wayland_layer_shell_overlay_session() {
        const char *wayland_display = getenv("WAYLAND_DISPLAY");
        if(!wayland_display || !wayland_display[0])
            return false;

        const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
        if(!xdg_current_desktop || !xdg_current_desktop[0])
            return false;

        return strstr(xdg_current_desktop, "Hyprland") ||
               strstr(xdg_current_desktop, "niri") ||
               strstr(xdg_current_desktop, "river");
    }
}