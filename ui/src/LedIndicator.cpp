#include "../include/LedIndicator.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// TODO: Support hotplug for led indicator check (led_brightness_files)

namespace gsr {
    static bool string_starts_with(const char *str, const char *sub) {
        const int str_len = strlen(str);
        const int sub_len = strlen(sub);
        return str_len >= sub_len && memcmp(str, sub, sub_len) == 0;
    }

    static bool string_ends_with(const char *str, const char *sub) {
        const int str_len = strlen(str);
        const int sub_len = strlen(sub);
        return str_len >= sub_len && memcmp(str + str_len - sub_len, sub, sub_len) == 0;
    }

    static std::vector<int> open_device_leds_brightness_files(const char *led_name_path) {
        std::vector<int> files;

        DIR *dir = opendir("/sys/class/leds");
        if(!dir)
            return files;

        char brightness_filepath[1024];
        struct dirent *entry;
        while((entry = readdir(dir)) != NULL) {
            if(entry->d_name[0] == '.')
                continue;

            if(!string_starts_with(entry->d_name, "input") || !string_ends_with(entry->d_name, led_name_path))
                continue;

            snprintf(brightness_filepath, sizeof(brightness_filepath), "/sys/class/leds/%s/brightness", entry->d_name);
            const int led_brightness_file_fd = open(brightness_filepath, O_RDONLY | O_NONBLOCK);
            if(led_brightness_file_fd > 0)
                files.push_back(led_brightness_file_fd);
        }

        closedir(dir);
        return files;
    }

    LedIndicator::LedIndicator() {
        led_brightness_files = open_device_leds_brightness_files("scrolllock");
        run_gsr_global_hotkeys_set_leds(false);
    }

    LedIndicator::~LedIndicator() {
        for(int led_brightness_file_fd : led_brightness_files) {
            close(led_brightness_file_fd);
        }

        run_gsr_global_hotkeys_set_leds(false);
        if(gsr_global_hotkeys_pid > 0) {
            int status;
            waitpid(gsr_global_hotkeys_pid, &status, 0);
        }
    }

    void LedIndicator::set_led(bool enabled) {
        led_enabled = enabled;
        perform_blink = false;
    }

    void LedIndicator::blink() {
        perform_blink = true;
        blink_timer.restart();
    }

    bool LedIndicator::run_gsr_global_hotkeys_set_leds(bool enabled) {
        if(gsr_global_hotkeys_pid > 0) {
            int status;
            if(waitpid(gsr_global_hotkeys_pid, &status, WNOHANG) == 0) {
                // Still running
                return false;
            }
            gsr_global_hotkeys_pid = -1;
        }

        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        const char *user_homepath = getenv("HOME");
        if(!user_homepath)
            user_homepath = "/tmp";

        gsr_global_hotkeys_pid = vfork();
        if(gsr_global_hotkeys_pid == -1) {
            fprintf(stderr, "Error: LedIndicator::run_gsr_global_hotkeys_set_leds: failed to fork\n");
            return false;
        } else if(gsr_global_hotkeys_pid == 0) { // Child
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if(inside_flatpak) {
                const char *args[] = { "flatpak-spawn", "--host", "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/kms-server-proxy", "launch-gsr-global-hotkeys", user_homepath, "--set-led", "Scroll Lock", enabled ? "on" : "off", nullptr };
                execvp(args[0], (char* const*)args);
            } else {
                const char *args[] = { "gsr-global-hotkeys", "--set-led", "Scroll Lock", enabled ? "on" : "off", nullptr };
                execvp(args[0], (char* const*)args);
            }

            perror("gsr-global-hotkeys");
            _exit(127);
            return false;
        } else { // Parent
            return true;
        }
    }

    void LedIndicator::update_led(bool new_state) {
        if(new_state == led_indicator_on)
            return;

        if(run_gsr_global_hotkeys_set_leds(new_state))
            led_indicator_on = new_state;
    }

    void LedIndicator::update() {
        update_led_with_active_status();
        check_led_status_outdated();
    }

    void LedIndicator::update_led_with_active_status() {
        if(perform_blink) {
            const double blink_elapsed_sec = blink_timer.get_elapsed_time_seconds();
            if(blink_elapsed_sec < 0.2) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.4) {
                update_led(true);
            } else if(blink_elapsed_sec < 0.6) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.8) {
                update_led(true);
            } else {
                perform_blink = false;
            }
        } else {
            update_led(led_enabled);
        }
    }

    void LedIndicator::check_led_status_outdated() {
        // The display server will unset our scroll lock led when pressing capslock/numlock as it updates
        // all leds at the same time (not just the button pressed) (or at least xorg server does that).
        // When that is done we want to set the scroll lock led on again if it should be on.
        // TODO: Improve this. Dont do this with a timer.. but inotify doesn't work sysfs. netlink should work (man 7 netlink).
        if(read_led_brightness_timer.get_elapsed_time_seconds() > 0.2) {
            read_led_brightness_timer.restart();

            bool any_keyboard_with_led_enabled = false;
            bool any_keyboard_with_led_disabled = false;
            char buffer[32];
            for(int led_brightness_file_fd : led_brightness_files) {
                const ssize_t bytes_read = read(led_brightness_file_fd, buffer, sizeof(buffer));
                if(bytes_read > 0) {
                    if(buffer[0] == '0')
                        any_keyboard_with_led_disabled = true;
                    else
                        any_keyboard_with_led_enabled = true;
                    lseek(led_brightness_file_fd, 0, SEEK_SET);
                }
            }

            if(led_enabled && any_keyboard_with_led_disabled)
                run_gsr_global_hotkeys_set_leds(true);
            else if(!led_enabled && any_keyboard_with_led_enabled)
                run_gsr_global_hotkeys_set_leds(false);
        }
    }
}