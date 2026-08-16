#pragma once

#include <stdint.h>
#include <functional>
#include <string>

namespace mgl {
    class Event;
}

namespace gsr {
    enum HotkeyModifier : uint32_t {
        HOTKEY_MOD_LSHIFT = 1 << 0,
        HOTKEY_MOD_RSHIFT = 1 << 1,
        HOTKEY_MOD_LCTRL  = 1 << 2,
        HOTKEY_MOD_RCTRL  = 1 << 3,
        HOTKEY_MOD_LALT   = 1 << 4,
        HOTKEY_MOD_RALT   = 1 << 5,
        HOTKEY_MOD_LSUPER = 1 << 6,
        HOTKEY_MOD_RSUPER = 1 << 7
    };

    struct Hotkey {
        uint32_t key = 0;       // X11 keysym
        uint32_t modifiers = 0; // HotkeyModifier
    };

    using GlobalHotkeyCallback = std::function<void(const std::string &id)>;

    class GlobalHotkeys {
    public:
        GlobalHotkeys() = default;
        GlobalHotkeys(const GlobalHotkeys&) = delete;
        GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;
        virtual ~GlobalHotkeys() = default;

        virtual bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) { (void)hotkey; (void)id; (void)callback; return false; }
        virtual void unbind_key_press(const std::string &id) { (void)id; }
        virtual void unbind_all_keys() {}
        virtual bool bind_action(const std::string &id, GlobalHotkeyCallback callback) { (void)id; (void)callback; return false; };
        virtual void poll_events() = 0;
        // Returns true if the event wasn't consumed (if the event didn't match a key that has been bound)
        virtual bool on_event(mgl::Event &event) { (void)event; return true; }
    };
}