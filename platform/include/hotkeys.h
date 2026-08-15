/* platform/include/hotkeys.h — global hotkey interfaces for the Windows port.
 *
 * Phase 3 deliverable (headers only). Implemented in Phase 11
 * (platform/windows/hotkeys.c, RegisterHotKey / RegisterRawInputDevices).
 *
 * Upstream registers global hotkeys through GlobalHotkeys (X11/joystick)
 * in the UI. The port replaces that with RegisterHotKey; the semantics the
 * UI needs are: register/unregister a system-wide key combo and receive a
 * callback. The hotkey *identifiers* stay UI-owned (upstream's
 * GlobalHotkeys IDs) so the UI's settings schema is unchanged.
 */
#ifndef GSR_PLATFORM_HOTKEYS_H
#define GSR_PLATFORM_HOTKEYS_H

#include <stdbool.h>
#include <stdint.h>

/* Modifier flags (bitmask, Win32 MOD_* equivalents kept abstract so the
 * header stays platform-neutral). */
typedef enum {
    GSR_PLATFORM_HOTKEY_MOD_ALT = 1 << 0,
    GSR_PLATFORM_HOTKEY_MOD_CONTROL = 1 << 1,
    GSR_PLATFORM_HOTKEY_MOD_SHIFT = 1 << 2,
    GSR_PLATFORM_HOTKEY_MOD_WIN = 1 << 3
} gsr_platform_hotkey_modifier;

/* Registers a system-wide hotkey. |id| is the UI's hotkey id (must be in
 * 0x0000-0xBFFF, the app-reserved range). |vk| is a Windows virtual-key
 * code. Returns false when registration fails (e.g. the combo is taken or
 * the same |id| is already registered). */
bool gsr_platform_hotkey_register(int id, uint32_t modifiers, uint32_t vk);

/* Unregisters a previously registered hotkey. Safe to call for an id that
 * was never registered. */
void gsr_platform_hotkey_unregister(int id);

#endif /* GSR_PLATFORM_HOTKEYS_H */
