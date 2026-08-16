#include "../../include/GlobalHotkeys/GlobalHotkeysLinux.hpp"
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <linux/input-event-codes.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    static const char* grab_type_to_arg(GlobalHotkeysLinux::GrabType grab_type) {
        switch(grab_type) {
            case GlobalHotkeysLinux::GrabType::ALL:     return "--all";
            case GlobalHotkeysLinux::GrabType::VIRTUAL: return "--virtual";
            case GlobalHotkeysLinux::GrabType::NO_GRAB: return "--no-grab";
        }
        return "--all";
    }

    static inline uint8_t x11_keycode_to_linux_keycode(uint8_t code) {
        return code - 8;
    }

    static std::vector<uint8_t> modifiers_to_linux_keys(uint32_t modifiers) {
        std::vector<uint8_t> result;
        if(modifiers & HOTKEY_MOD_LSHIFT)
            result.push_back(KEY_LEFTSHIFT);
        if(modifiers & HOTKEY_MOD_RSHIFT)
            result.push_back(KEY_RIGHTSHIFT);
        if(modifiers & HOTKEY_MOD_LCTRL)
            result.push_back(KEY_LEFTCTRL);
        if(modifiers & HOTKEY_MOD_RCTRL)
            result.push_back(KEY_RIGHTCTRL);
        if(modifiers & HOTKEY_MOD_LALT)
            result.push_back(KEY_LEFTALT);
        if(modifiers & HOTKEY_MOD_RALT)
            result.push_back(KEY_RIGHTALT);
        if(modifiers & HOTKEY_MOD_LSUPER)
            result.push_back(KEY_LEFTMETA);
        if(modifiers & HOTKEY_MOD_RSUPER)
            result.push_back(KEY_RIGHTMETA);
        return result;
    }

    static std::string linux_keys_to_command_string(const uint8_t *keys, size_t size) {
        std::string result;
        for(size_t i = 0; i < size; ++i) {
            if(!result.empty())
                result += "+";
            result += std::to_string(keys[i]);
        }
        return result;
    }

    static bool x11_key_is_alpha_numerical(KeySym keysym) {
        return (keysym >= XK_A && keysym <= XK_Z) || (keysym >= XK_a && keysym <= XK_z) || (keysym >= XK_0 && keysym <= XK_9);
    }

    GlobalHotkeysLinux::GlobalHotkeysLinux(Display *x11_dpy, GrabType grab_type) : grab_type(grab_type), x11_dpy(x11_dpy) {
        for(int i = 0; i < 2; ++i) {
            read_pipes[i] = -1;
            write_pipes[i] = -1;
        }
    }

    GlobalHotkeysLinux::~GlobalHotkeysLinux() {
        if(write_pipes[PIPE_WRITE] > 0) {
            char command[32];
            const int command_size = snprintf(command, sizeof(command), "exit\n");
            if(write(write_pipes[PIPE_WRITE], command, command_size) != command_size) {
                fprintf(stderr, "Error: GlobalHotkeysLinux::~GlobalHotkeysLinux: failed to write command to gsr-global-hotkeys, error: %s\n", strerror(errno));
                close_fds();
            }
        } else {
            close_fds();
        }

        if(process_id > 0) {
            int status;
            waitpid(process_id, &status, 0);
        }

        close_fds();
    }

    void GlobalHotkeysLinux::close_fds() {
        for(int i = 0; i < 2; ++i) {
            if(read_pipes[i] > 0) {
                close(read_pipes[i]);
                read_pipes[i] = -1;
            }

            if(write_pipes[i] > 0) {
                close(write_pipes[i]);
                write_pipes[i] = -1;
            }
        }

        if(read_file) {
            fclose(read_file);
            read_file = nullptr;
        }
    }

    bool GlobalHotkeysLinux::start() {
        const char *grab_type_arg = grab_type_to_arg(grab_type);
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        const char *user_homepath = getenv("HOME");
        if(!user_homepath)
            user_homepath = "/tmp";

        if(process_id > 0)
            return false;

        if(pipe(read_pipes) == -1)
            return false;

        if(pipe(write_pipes) == -1) {
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
                read_pipes[i] = -1;
            }
            return false;
        }

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
                close(write_pipes[i]);
                read_pipes[i] = -1;
                write_pipes[i] = -1;
            }
            return false;
        } else if(pid == 0) { /* child */
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            dup2(read_pipes[PIPE_WRITE], STDOUT_FILENO);
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
            }

            dup2(write_pipes[PIPE_READ], STDIN_FILENO);
            for(int i = 0; i < 2; ++i) {
                close(write_pipes[i]);
            }

            if(inside_flatpak) {
                const char *args[] = { "flatpak-spawn", "--host", "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/kms-server-proxy", "launch-gsr-global-hotkeys", user_homepath, grab_type_arg, nullptr };
                execvp(args[0], (char* const*)args);
            } else {
                const char *args[] = { "gsr-global-hotkeys", grab_type_arg, nullptr };
                execvp(args[0], (char* const*)args);
            }

            perror("gsr-global-hotkeys");
            _exit(127);
        } else { /* parent */
            process_id = pid;

            close(read_pipes[PIPE_WRITE]);
            read_pipes[PIPE_WRITE] = -1;

            close(write_pipes[PIPE_READ]);
            write_pipes[PIPE_READ] = -1;

            fcntl(read_pipes[PIPE_READ], F_SETFL, fcntl(read_pipes[PIPE_READ], F_GETFL) | O_NONBLOCK);
            read_file = fdopen(read_pipes[PIPE_READ], "r");
            if(read_file)
                read_pipes[PIPE_READ] = -1;
            else
                fprintf(stderr, "fdopen failed for read, error: %s\n", strerror(errno));
        }

        return true;
    }

    bool GlobalHotkeysLinux::bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) {
        if(process_id <= 0)
            return false;

        if(bound_actions_by_id.find(id) != bound_actions_by_id.end())
            return false;

        if(id.find(' ') != std::string::npos || id.find('\n') != std::string::npos) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: id \"%s\" contains either space or newline\n", id.c_str());
            return false;
        }

        if(hotkey.key == 0 || hotkey.key == XK_VoidSymbol) {
            //fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: hotkey requires a key\n");
            return false;
        }

        if(hotkey.modifiers == 0 && x11_key_is_alpha_numerical(hotkey.key)) {
            //fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: hotkey requires a modifier\n");
            return false;
        }

        if(!x11_dpy) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: no X11 display available — cannot translate keysym to keycode\n");
            return false;
        }
        const uint8_t keycode = x11_keycode_to_linux_keycode(XKeysymToKeycode(x11_dpy, hotkey.key));
        const std::vector<uint8_t> modifiers = modifiers_to_linux_keys(hotkey.modifiers);
        const std::string modifiers_command = linux_keys_to_command_string(modifiers.data(), modifiers.size());

        char command[256];
        int command_size = 0;
        if(modifiers_command.empty())
            command_size = snprintf(command, sizeof(command), "bind %s %d\n", id.c_str(), (int)keycode);
        else
            command_size = snprintf(command, sizeof(command), "bind %s %d+%s\n", id.c_str(), (int)keycode, modifiers_command.c_str());

        if(write(write_pipes[PIPE_WRITE], command, command_size) != command_size) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: failed to write command to gsr-global-hotkeys, error: %s\n", strerror(errno));
            return false;
        }

        bound_actions_by_id[id] = std::move(callback);
        return true;
    }

    void GlobalHotkeysLinux::unbind_all_keys() {
        if(process_id <= 0)
            return;

        if(bound_actions_by_id.empty())
            return;

        char command[32];
        const int command_size = snprintf(command, sizeof(command), "unbind_all\n");
        if(write(write_pipes[PIPE_WRITE], command, command_size) != command_size) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::unbind_all_keys: failed to write command to gsr-global-hotkeys, error: %s\n", strerror(errno));
        }
        bound_actions_by_id.clear();
    }

    void GlobalHotkeysLinux::poll_events() {
        if(process_id <= 0) {
            //fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, process has not been started yet. Use GlobalHotkeysLinux::start to start the process first\n");
            return;
        }

        if(!read_file) {
            //fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, read file hasn't opened\n");
            return;
        }

        std::string action;
        char buffer[256];
        while(true) {
            char *line = fgets(buffer, sizeof(buffer), read_file);
            if(!line)
                break;

            int line_len = strlen(line);
            if(line_len == 0)
                continue;

            if(line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
                --line_len;
            }

            action = line;
            auto it = bound_actions_by_id.find(action);
            if(it != bound_actions_by_id.end())
                it->second(action);
            else if(on_gsr_ui_virtual_keyboard_grabbed && action == "gsr-ui-virtual-keyboard-grabbed")
                on_gsr_ui_virtual_keyboard_grabbed();
        }
    }
}
