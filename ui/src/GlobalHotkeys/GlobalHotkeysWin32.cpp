#include "../../include/GlobalHotkeys/GlobalHotkeysWin32.hpp"

#include <windows.h>
#include <stdio.h>

namespace gsr {
    /* X11 keysym -> Windows VK translation. Only the keys the UI actually
       binds are needed (alphanumerics, F-keys, Escape); anything unknown is
       left unmapped so bind_key_press fails cleanly. */
    static bool keysym_to_vk(uint32_t keysym, UINT *vk_out) {
        /* Letters: XK_a..XK_z == 0x61..0x7a */
        if(keysym >= 0x61 && keysym <= 0x7a) {
            *vk_out = (UINT)(keysym - 0x61 + 'A');
            return true;
        }
        /* Digits: XK_0..XK_9 == 0x30..0x39 */
        if(keysym >= 0x30 && keysym <= 0x39) {
            *vk_out = (UINT)keysym;
            return true;
        }
        /* XK_F1..XK_F24 == 0xffbe..0xffd5 */
        if(keysym >= 0xffbe && keysym <= 0xffd5) {
            *vk_out = VK_F1 + (UINT)(keysym - 0xffbe);
            return true;
        }

        switch(keysym) {
            case 0xff1b /* XK_Escape */:      *vk_out = VK_ESCAPE; return true;
            case 0xff08 /* XK_BackSpace */:   *vk_out = VK_BACK; return true;
            case 0xff09 /* XK_Tab */:         *vk_out = VK_TAB; return true;
            case 0xff0d /* XK_Return */:      *vk_out = VK_RETURN; return true;
            case 0xff51 /* XK_Left */:        *vk_out = VK_LEFT; return true;
            case 0xff52 /* XK_Up */:          *vk_out = VK_UP; return true;
            case 0xff53 /* XK_Right */:       *vk_out = VK_RIGHT; return true;
            case 0xff54 /* XK_Down */:        *vk_out = VK_DOWN; return true;
            case 0xff55 /* XK_Page_Up */:     *vk_out = VK_PRIOR; return true;
            case 0xff56 /* XK_Page_Down */:   *vk_out = VK_NEXT; return true;
            case 0xff57 /* XK_End */:         *vk_out = VK_END; return true;
            case 0xff50 /* XK_Home */:        *vk_out = VK_HOME; return true;
            case 0xff63 /* XK_Insert */:      *vk_out = VK_INSERT; return true;
            case 0xffff /* XK_Delete */:      *vk_out = VK_DELETE; return true;
            case 0x20   /* XK_space */:       *vk_out = VK_SPACE; return true;
            case 0xff13 /* XK_Pause */:       *vk_out = VK_PAUSE; return true;
            case 0xff61 /* XK_Print */:       *vk_out = VK_SNAPSHOT; return true;
            case 0xffaa /* XK_KP_Multiply */: *vk_out = VK_MULTIPLY; return true;
            case 0xffab /* XK_KP_Add */:      *vk_out = VK_ADD; return true;
            case 0xffad /* XK_KP_Subtract */: *vk_out = VK_SUBTRACT; return true;
            case 0xffae /* XK_KP_Decimal */:  *vk_out = VK_DECIMAL; return true;
            case 0xffaf /* XK_KP_Divide */:   *vk_out = VK_DIVIDE; return true;
            case 0x3b   /* XK_semicolon */:   *vk_out = VK_OEM_1; return true;
            case 0x3d   /* XK_equal */:       *vk_out = VK_OEM_PLUS; return true;
            case 0x2c   /* XK_comma */:       *vk_out = VK_OEM_COMMA; return true;
            case 0x2d   /* XK_minus */:       *vk_out = VK_OEM_MINUS; return true;
            case 0x2e   /* XK_period */:      *vk_out = VK_OEM_PERIOD; return true;
            case 0x2f   /* XK_slash */:       *vk_out = VK_OEM_2; return true;
            case 0x60   /* XK_grave */:       *vk_out = VK_OEM_3; return true;
            case 0x5b   /* XK_bracketleft */: *vk_out = VK_OEM_4; return true;
            case 0x5c   /* XK_backslash */:   *vk_out = VK_OEM_5; return true;
            case 0x5d   /* XK_bracketright */:*vk_out = VK_OEM_6; return true;
            case 0x27   /* XK_apostrophe */:  *vk_out = VK_OEM_7; return true;
            default: return false;
        }
    }

    static UINT modifiers_to_mod(uint32_t modifiers) {
        UINT result = 0;
        if(modifiers & (HOTKEY_MOD_LSHIFT | HOTKEY_MOD_RSHIFT))
            result |= MOD_SHIFT;
        if(modifiers & (HOTKEY_MOD_LCTRL | HOTKEY_MOD_RCTRL))
            result |= MOD_CONTROL;
        if(modifiers & (HOTKEY_MOD_LALT | HOTKEY_MOD_RALT))
            result |= MOD_ALT;
        if(modifiers & (HOTKEY_MOD_LSUPER | HOTKEY_MOD_RSUPER))
            result |= MOD_WIN;
        return result;
    }

    static LRESULT CALLBACK rpc_hotkey_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        (void)hwnd;
        if(message == WM_HOTKEY) {
            GlobalHotkeysWin32 *self = (GlobalHotkeysWin32*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
            if(self)
                self->poll_events();
            (void)wparam;
            (void)lparam;
        }
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }

    GlobalHotkeysWin32::GlobalHotkeysWin32() {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = rpc_hotkey_wnd_proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "gsr_ui_global_hotkeys";
        RegisterClassA(&wc);

        hwnd = CreateWindowExA(0, wc.lpszClassName, "gsr-ui-global-hotkeys", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        if(!hwnd)
            fprintf(stderr, "GlobalHotkeysWin32 error: failed to create message-only window, global hotkeys wont be available\n");
        else
            SetWindowLongPtrA((HWND)hwnd, GWLP_USERDATA, (LONG_PTR)this);
    }

    GlobalHotkeysWin32::~GlobalHotkeysWin32() {
        unbind_all_keys();
        if(hwnd) {
            DestroyWindow((HWND)hwnd);
            hwnd = nullptr;
        }
    }

    bool GlobalHotkeysWin32::bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) {
        if(!hwnd)
            return false;

        if(bound_hotkeys_by_id.find(id) != bound_hotkeys_by_id.end())
            return false;

        if(hotkey.key == 0)
            return false;

        UINT vk = 0;
        if(!keysym_to_vk(hotkey.key, &vk))
            return false;

        const UINT mod = modifiers_to_mod(hotkey.modifiers);
        const UINT hotkey_id = next_id++;
        if(!RegisterHotKey((HWND)hwnd, hotkey_id, mod, vk)) {
            /* The most common cause is another process already owning the
               same combination (the NVIDIA App / GeForce Experience overlay
               grabs Alt+Z by default). Surface the failure instead of
               silently dropping the hotkey. */
            const DWORD err = GetLastError();
            fprintf(stderr,
                "GlobalHotkeysWin32 error: RegisterHotKey failed for '%s' (vk=0x%02x mod=0x%x), error %lu - another application is likely using this hotkey combination\n",
                id.c_str(), vk, mod, err);
            return false;
        }

        bound_hotkeys_by_id[id] = hotkey;
        hotkey_callbacks_by_id[id] = std::move(callback);
        /* Stash the id<->hotkey_id association for unbind. */
        hotkey_id_by_id[id] = hotkey_id;
        return true;
    }

    void GlobalHotkeysWin32::unbind_key_press(const std::string &id) {
        auto it = hotkey_id_by_id.find(id);
        if(it == hotkey_id_by_id.end())
            return;

        if(hwnd)
            UnregisterHotKey((HWND)hwnd, it->second);
        hotkey_id_by_id.erase(id);
        hotkey_callbacks_by_id.erase(id);
        bound_hotkeys_by_id.erase(id);
    }

    void GlobalHotkeysWin32::unbind_all_keys() {
        for(const auto &[id, hotkey_id] : hotkey_id_by_id) {
            if(hwnd)
                UnregisterHotKey((HWND)hwnd, hotkey_id);
        }
        hotkey_id_by_id.clear();
        hotkey_callbacks_by_id.clear();
        bound_hotkeys_by_id.clear();
    }

    void GlobalHotkeysWin32::poll_events() {
        if(!hwnd)
            return;

        MSG msg;
        while(PeekMessageA(&msg, (HWND)hwnd, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            /* Find the callback for this hotkey id. */
            const UINT hotkey_id = (UINT)msg.wParam;
            for(const auto &[id, id_val] : hotkey_id_by_id) {
                if(id_val == hotkey_id) {
                    auto it = hotkey_callbacks_by_id.find(id);
                    if(it != hotkey_callbacks_by_id.end())
                        it->second(id);
                    break;
                }
            }
        }
    }
}
