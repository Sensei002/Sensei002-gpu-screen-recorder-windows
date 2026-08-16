#pragma once

#include "GlobalHotkeys.hpp"
#include <unordered_map>
#include <sys/types.h>

typedef struct _XDisplay Display;

namespace gsr {
    class GlobalHotkeysLinux : public GlobalHotkeys {
    public:
        enum class GrabType {
            ALL,
            VIRTUAL,
            NO_GRAB
        };

        // |x11_dpy| is used to translate Hotkey::key (an X11 KeySym) into a keycode.
        // May be nullptr — bind_key_press will then fail rather than crash.
        GlobalHotkeysLinux(Display *x11_dpy, GrabType grab_type);
        GlobalHotkeysLinux(const GlobalHotkeysLinux&) = delete;
        GlobalHotkeysLinux& operator=(const GlobalHotkeysLinux&) = delete;
        ~GlobalHotkeysLinux() override;

        bool start();
        bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) override;
        void unbind_all_keys() override;
        void poll_events() override;

        std::function<void()> on_gsr_ui_virtual_keyboard_grabbed;
    private:
        void close_fds();
    private:
        pid_t process_id = 0;
        int read_pipes[2];
        int write_pipes[2];
        FILE *read_file = nullptr;
        std::unordered_map<std::string, GlobalHotkeyCallback> bound_actions_by_id;
        GrabType grab_type;
        Display *x11_dpy = nullptr;
    };
}