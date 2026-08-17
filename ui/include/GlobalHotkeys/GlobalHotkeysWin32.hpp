#pragma once

#include "GlobalHotkeys.hpp"
#include <unordered_map>
#include <stdint.h>

namespace gsr {
    // Windows global hotkeys via RegisterHotKey + a message-only window.
    // Hotkey.key is an X11 keysym (matching upstream's config->Hotkey
    // conversion) and modifiers is a HotkeyModifier bitmask; both are
    // translated to VK codes + MOD_* flags for RegisterHotKey.
    class GlobalHotkeysWin32 : public GlobalHotkeys {
    public:
        GlobalHotkeysWin32();
        GlobalHotkeysWin32(const GlobalHotkeysWin32&) = delete;
        GlobalHotkeysWin32& operator=(const GlobalHotkeysWin32&) = delete;
        ~GlobalHotkeysWin32() override;

        bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) override;
        void unbind_key_press(const std::string &id) override;
        void unbind_all_keys() override;
        void poll_events() override;
    private:
        void *hwnd = nullptr; /* HWND of the message-only window */
        std::unordered_map<std::string, Hotkey> bound_hotkeys_by_id;
        std::unordered_map<std::string, GlobalHotkeyCallback> hotkey_callbacks_by_id;
        std::unordered_map<std::string, uint32_t> hotkey_id_by_id;
        uint32_t next_id = 1;
    };
}
