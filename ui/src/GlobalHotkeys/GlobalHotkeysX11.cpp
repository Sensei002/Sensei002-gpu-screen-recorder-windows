#include "../../include/GlobalHotkeys/GlobalHotkeysX11.hpp"
#include <X11/keysym.h>
#include <mglpp/window/Event.hpp>
#include <assert.h>

namespace gsr {
    static bool x_failed = false;
    static int xerror_grab_error(Display*, XErrorEvent*) {
        x_failed = true;
        return 0;
    }

    static unsigned int x11_get_numlock_mask(Display *dpy) {
        unsigned int numlockmask = 0;
        KeyCode numlock_keycode = XKeysymToKeycode(dpy, XK_Num_Lock);
        XModifierKeymap *modmap = XGetModifierMapping(dpy);
        if(modmap) {
            for(int i = 0; i < 8; ++i) {
                for(int j = 0; j < modmap->max_keypermod; ++j) {
                    if(modmap->modifiermap[i * modmap->max_keypermod + j] == numlock_keycode)
                        numlockmask = (1 << i);
                }
            }
            XFreeModifiermap(modmap);
        }
        return numlockmask;
    }

    static KeySym mgl_key_to_key_sym(mgl::Keyboard::Key key) {
        switch(key) {
            case mgl::Keyboard::Z:   return XK_z;
            case mgl::Keyboard::F7:  return XK_F7;
            case mgl::Keyboard::F8:  return XK_F8;
            case mgl::Keyboard::F9:  return XK_F9;
            case mgl::Keyboard::F10: return XK_F10;
            default:                 return None;
        }
    }

    static uint32_t mgl_key_modifiers_to_x11_modifier_mask(const mgl::Event::KeyEvent &key_event) {
        uint32_t mask = 0;
        if(key_event.key_states.shift)
            mask |= ShiftMask;
        if(key_event.key_states.control)
            mask |= ControlMask;
        if(key_event.key_states.alt)
            mask |= Mod1Mask;
        if(key_event.key_states.system)
            mask |= Mod4Mask;
        return mask;
    }

    static uint32_t modifiers_to_x11_modifiers(uint32_t modifiers) {
        uint32_t result = 0;
        if(modifiers & HOTKEY_MOD_LSHIFT)
            result |= ShiftMask;
        if(modifiers & HOTKEY_MOD_RSHIFT)
            result |= ShiftMask;
        if(modifiers & HOTKEY_MOD_LCTRL)
            result |= ControlMask;
        if(modifiers & HOTKEY_MOD_RCTRL)
            result |= ControlMask;
        if(modifiers & HOTKEY_MOD_LALT)
            result |= Mod1Mask;
        if(modifiers & HOTKEY_MOD_RALT)
            result |= Mod5Mask;
        if(modifiers & HOTKEY_MOD_LSUPER)
            result |= Mod4Mask;
        if(modifiers & HOTKEY_MOD_RSUPER)
            result |= Mod4Mask;
        return result;
    }

    GlobalHotkeysX11::GlobalHotkeysX11() {
        dpy = XOpenDisplay(NULL);
        if(!dpy)
            fprintf(stderr, "GlobalHotkeysX11 error: failed to connect to X11 server, global hotkeys wont be available\n");
    }

    GlobalHotkeysX11::~GlobalHotkeysX11() {
        if(dpy) {
            XCloseDisplay(dpy);
            dpy = nullptr;
        }
    }

    bool GlobalHotkeysX11::bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) {
        if(!dpy)
            return false;

        auto it = bound_keys_by_id.find(id);
        if(it != bound_keys_by_id.end())
            return false;

        x_failed = false;
        XErrorHandler prev_xerror = XSetErrorHandler(xerror_grab_error);

        const uint32_t modifiers_x11 = modifiers_to_x11_modifiers(hotkey.modifiers);
        unsigned int numlock_mask = x11_get_numlock_mask(dpy);
        unsigned int modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };
        for(int i = 0; i < 4; ++i) {
            XGrabKey(dpy, XKeysymToKeycode(dpy, hotkey.key), modifiers_x11 | modifiers[i], DefaultRootWindow(dpy), False, GrabModeAsync, GrabModeAsync);
        }
        XSync(dpy, False);
        
        if(x_failed) {
            for(int i = 0; i < 4; ++i) {
                XUngrabKey(dpy, XKeysymToKeycode(dpy, hotkey.key), modifiers_x11 | modifiers[i], DefaultRootWindow(dpy));
            }
            XSync(dpy, False);
            XSetErrorHandler(prev_xerror);
            return false;
        } else {
            XSetErrorHandler(prev_xerror);
            bound_keys_by_id[id] = { hotkey, std::move(callback) };
            return true;
        }
    }

    void GlobalHotkeysX11::unbind_key_press(const std::string &id) {
        if(!dpy)
            return;

        auto it = bound_keys_by_id.find(id);
        if(it == bound_keys_by_id.end())
            return;

        x_failed = false;
        XErrorHandler prev_xerror = XSetErrorHandler(xerror_grab_error);

        const uint32_t modifiers_x11 = modifiers_to_x11_modifiers(it->second.hotkey.modifiers);
        unsigned int numlock_mask = x11_get_numlock_mask(dpy);
        unsigned int modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };
        for(int i = 0; i < 4; ++i) {
            XUngrabKey(dpy, XKeysymToKeycode(dpy, it->second.hotkey.key), modifiers_x11 | modifiers[i], DefaultRootWindow(dpy));
        }
        XSync(dpy, False);

        XSetErrorHandler(prev_xerror);
        bound_keys_by_id.erase(id);
    }

    void GlobalHotkeysX11::unbind_all_keys() {
        if(!dpy)
            return;

        x_failed = false;
        XErrorHandler prev_xerror = XSetErrorHandler(xerror_grab_error);

        unsigned int numlock_mask = x11_get_numlock_mask(dpy);
        unsigned int modifiers[] = { 0, LockMask, numlock_mask, numlock_mask|LockMask };
        for(auto it = bound_keys_by_id.begin(); it != bound_keys_by_id.end();) {
            const uint32_t modifiers_x11 = modifiers_to_x11_modifiers(it->second.hotkey.modifiers);
            for(int i = 0; i < 4; ++i) {
                XUngrabKey(dpy, XKeysymToKeycode(dpy, it->second.hotkey.key), modifiers_x11 | modifiers[i], DefaultRootWindow(dpy));
            }
        }
        bound_keys_by_id.clear();
        XSync(dpy, False);

        XSetErrorHandler(prev_xerror);
    }

    void GlobalHotkeysX11::poll_events() {
        if(!dpy)
            return;

        while(XPending(dpy)) {
            XNextEvent(dpy, &xev);
            if(xev.type == KeyPress) {
                const KeySym key_sym = XLookupKeysym(&xev.xkey, 0);
                call_hotkey_callback({ (uint32_t)key_sym, xev.xkey.state });
            }
        }
    }

    bool GlobalHotkeysX11::on_event(mgl::Event &event) {
        if(event.type != mgl::Event::KeyPressed)
            return true;

        // Note: not all keys are mapped in mgl_key_to_key_sym. If more hotkeys are added or changed then add the key mapping there
        const KeySym key_sym = mgl_key_to_key_sym(event.key.code);
        const uint32_t modifiers = mgl_key_modifiers_to_x11_modifier_mask(event.key);
        return !call_hotkey_callback(Hotkey{(uint32_t)key_sym, modifiers});
    }

    static unsigned int key_state_without_locks(unsigned int key_state) {
        return key_state & ~(Mod2Mask|LockMask);
    }

    bool GlobalHotkeysX11::call_hotkey_callback(Hotkey hotkey) const {
        const uint32_t modifiers_x11 = modifiers_to_x11_modifiers(hotkey.modifiers);
        for(const auto &[key, val] : bound_keys_by_id) {
            if(val.hotkey.key == hotkey.key && key_state_without_locks(modifiers_to_x11_modifiers(val.hotkey.modifiers)) == key_state_without_locks(modifiers_x11)) {
                val.callback(key);
                return true;
            }
        }
        return false;
    }
}