#pragma once

#include "GlobalHotkeys.hpp"
#include <unordered_map>
#include <X11/Xlib.h>

namespace gsr {
    class GlobalHotkeysX11 : public GlobalHotkeys {
    public:
        GlobalHotkeysX11();
        GlobalHotkeysX11(const GlobalHotkeysX11&) = delete;
        GlobalHotkeysX11& operator=(const GlobalHotkeysX11&) = delete;
        ~GlobalHotkeysX11() override;

        // Hotkey key is a KeySym (XK_z for example) and modifiers is a bitmask of X11 modifier masks (for example ShiftMask | Mod1Mask)
        bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) override;
        void unbind_key_press(const std::string &id) override;
        void unbind_all_keys() override;
        void poll_events() override;
        bool on_event(mgl::Event &event) override;
    private:
        // Returns true if a key bind has been registered for the hotkey
        bool call_hotkey_callback(Hotkey hotkey) const;
    private:
        struct HotkeyData {
            Hotkey hotkey;
            GlobalHotkeyCallback callback;
        };

        Display *dpy = nullptr;
        XEvent xev;
        std::unordered_map<std::string, HotkeyData> bound_keys_by_id;
    };
}