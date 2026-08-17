/* ui-module-test: headless smoke test for the Phase 10 (remaining) Win32 UI
 * platform modules — the native equivalents of the X11/Wayland modules the
 * upstream UI uses. Runs entirely headless on CI (no display server needed;
 * the runner's virtual display provides the monitor/cursor surfaces).
 *
 * Covers:
 *   - WindowUtilsWin32: monitor enumeration, cursor position, focused window,
 *     title sanitization
 *   - CursorTrackerWin32: cursor info via GetCursorPos + MonitorFromPoint
 *   - DesktopEnvironmentWin32: focused-window title via GetForegroundWindow
 *   - GlobalHotkeysWin32: RegisterHotKey bind/unbind round-trip
 *   - ClipboardWin32 + AudioPlayer + RegionSelectorWin32: construct/destruct
 *     (no live clipboard push or audio playback on CI)
 */
#include <stdio.h>
#include <string.h>

#include "WindowUtils.hpp"
#include "CursorTracker/CursorTrackerWin32.hpp"
#include "DesktopEnvironment/DesktopEnvironmentWin32.hpp"
#include "GlobalHotkeys/GlobalHotkeysWin32.hpp"
#include "Clipboard/ClipboardWin32.hpp"
#include "AudioPlayer.hpp"
#include "RegionSelector/RegionSelectorWin32.hpp"

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

static void test_window_utils(void) {
    /* Monitor enumeration — the CI runner has a virtual display. */
    const std::vector<gsr::Monitor> monitors = gsr::get_monitors(nullptr);
    CHECK(!monitors.empty());
    if(!monitors.empty()) {
        CHECK(monitors.front().size.x > 0);
        CHECK(monitors.front().size.y > 0);
    }

    /* Cursor position + the window it is over. */
    gsr::Window cursor_window = 0;
    const mgl::vec2i cursor_position = gsr::get_cursor_position(nullptr, &cursor_window);
    CHECK(cursor_position.x >= 0 || cursor_position.x == 0); /* always valid */
    (void)cursor_window;

    /* Focused window (may legitimately be 0 on a locked session). */
    const gsr::Window focused = gsr::get_focused_window(nullptr, gsr::WindowCaptureType::FOCUSED, false);
    (void)focused;

    /* Title sanitization strips path-invalid characters. */
    const std::string sanitized = gsr::window_title_utf8_sanitize("a/b:c*d", 6);
    CHECK(sanitized == "a_b_c_");
}

static void test_cursor_tracker(void) {
    gsr::CursorTrackerWin32 tracker;
    tracker.update();
    const std::optional<gsr::CursorInfo> info = tracker.get_latest_cursor_info();
    CHECK(info.has_value());
    if(info.has_value()) {
        CHECK(!info->monitor_name.empty());
    }
}

static void test_desktop_environment(void) {
    gsr::DesktopEnvironmentWin32 desktop_environment;
    CHECK(desktop_environment.start());
    desktop_environment.update();
    /* Title may be empty when no window is focused; must not crash. */
    const std::string title = desktop_environment.get_focused_window_title();
    (void)title;
    const std::string process_name = desktop_environment.get_focused_window_process_name();
    (void)process_name;
}

static void test_global_hotkeys(void) {
    gsr::GlobalHotkeysWin32 hotkeys;
    gsr::Hotkey hotkey;
    /* X11 keysym for F12 (0xFFC9); modifiers Ctrl+Alt+Shift. */
    hotkey.key = 0xFFC9;
    hotkey.modifiers = gsr::HOTKEY_MOD_LCTRL | gsr::HOTKEY_MOD_LALT | gsr::HOTKEY_MOD_LSHIFT;
    bool callback_called = false;
    const bool bound = hotkeys.bind_key_press(hotkey, "test_hotkey", [&callback_called](const std::string &id) {
        (void)id;
        callback_called = true;
    });
    /* RegisterHotKey works in headless sessions; fail loudly if it doesn't. */
    CHECK(bound);

    hotkeys.poll_events();
    (void)callback_called;

    hotkeys.unbind_key_press("test_hotkey");
    hotkeys.unbind_all_keys();
}

static void test_misc_modules(void) {
    gsr::ClipboardWin32 clipboard;
    (void)clipboard;

    gsr::AudioPlayer audio_player;
    (void)audio_player;

    gsr::RegionSelectorWin32 region_selector;
    (void)region_selector;
}

int main(void) {
    printf("ui-module-test: win32 ui platform module smoke tests\n");

    test_window_utils();
    test_cursor_tracker();
    test_desktop_environment();
    test_global_hotkeys();
    test_misc_modules();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
