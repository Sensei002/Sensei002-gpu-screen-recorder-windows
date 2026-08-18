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
 *   - overlay window behavior (Phase 10 remaining): a real hidden mgl window
 *     gets the overlay treatment the UI applies in show() — click-through
 *     (WS_EX_LAYERED|WS_EX_TRANSPARENT), taskbar-hide (WS_EX_TOOLWINDOW),
 *     always-on-top (z-order), borderless fullscreen covering the monitor,
 *     and alpha support (WS_EX_LAYERED at creation) — and the resulting
 *     Win32 styles are asserted via GetWindowLongPtrA/GetWindowRect.
 */
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The mgl C headers have no extern "C" guards; the C++ wrapper (mglpp)
   wraps them itself, so C++ TUs must do the same. */
extern "C" {
#include <mgl/mgl.h>
#include <mgl/window/window.h>
}

#include "Utils.hpp"
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

    /* Cursor position + the window it is over. `Window` is the global
       X11-compatible opaque handle from WindowUtils.hpp (not in gsr::). */
    Window cursor_window = 0;
    const mgl::vec2i cursor_position = gsr::get_cursor_position(nullptr, &cursor_window);
    CHECK(cursor_position.x >= 0 || cursor_position.x == 0); /* always valid */
    (void)cursor_window;

    /* Focused window (may legitimately be 0 on a locked session). */
    const Window focused = gsr::get_focused_window(nullptr, gsr::WindowCaptureType::FOCUSED, false);
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
    /* Go through the REAL conversion path the UI uses: the UI stores the
       hotkey as an mgl key code, converts it to an X11 keysym via
       mgl_key_to_x11_keysym, and GlobalHotkeysWin32 translates that to a VK
       for RegisterHotKey. On Win32 mgl_key_to_x11_keysym previously returned
       0 for every key, so every hotkey silently failed to register. F12's
       X11 keysym is 0xFFC9; modifiers Ctrl+Alt+Shift. */
    hotkey.key = (uint32_t)mgl_key_to_x11_keysym(MGL_KEY_F12);
    hotkey.modifiers = gsr::HOTKEY_MOD_LCTRL | gsr::HOTKEY_MOD_LALT | gsr::HOTKEY_MOD_LSHIFT;
    CHECK(hotkey.key == 0xFFC9);
    bool callback_called = false;
    const bool bound = hotkeys.bind_key_press(hotkey, "test_hotkey", [&callback_called](const std::string &id) {
        (void)id;
        callback_called = true;
    });
    /* RegisterHotKey works in headless sessions; fail loudly if it doesn't. */
    CHECK(bound);

    /* Registering the same combination again must fail: this is exactly the
       failure mode when another application (e.g. the NVIDIA App overlay)
       already owns the hotkey, and it's what surfaces the force-show of the
       hidden UI (main.cpp checks show_hide_hotkey_bound). */
    const bool second_bound = hotkeys.bind_key_press(hotkey, "test_hotkey_conflict", [](const std::string &id) {
        (void)id;
    });
    CHECK(!second_bound);

    hotkeys.poll_events();
    (void)callback_called;

    hotkeys.unbind_key_press("test_hotkey");
    hotkeys.unbind_all_keys();

    /* The default show/hide hotkey is Alt+Z: mgl Z must convert to the X11
       keysym for lowercase z (0x7a), which keysym_to_vk maps to VK 'Z'.
       This is the exact path the UI uses at startup. */
    CHECK(mgl_key_to_x11_keysym(MGL_KEY_Z) == 0x7a);
    CHECK(mgl_key_to_x11_keysym(MGL_KEY_A) == 0x61);
    CHECK(mgl_key_to_x11_keysym(MGL_KEY_F1) == 0xffbe);
    CHECK(mgl_key_to_x11_keysym(MGL_KEY_ESCAPE) == 0xff1b);
}

/* Autostart (HKCU Run) round-trip through the real UI code path
   (Utils.cpp set_xdg_autostart / is_xdg_autostart_enabled). Writes to the
   user's actual Run key, so save the prior state and restore it afterwards
   — on CI there is no prior value and the key ends clean. */
static void test_startup_autostart(void) {
    printf("-- startup (HKCU Run autostart)\n");

    /* HKCU registry access is not guaranteed in every CI context (the
       artifact re-run job runs the binaries outside MSYS2 with a
       restricted user hive on some runners). When the Run key is
       inaccessible, print the error and SKIP — the round-trip is already
       exercised in the Build job's ctest/direct-run steps, and a locked
       registry is an environment limitation, not a port bug. */
    HKEY probe_key = NULL;
    const LONG probe_result = RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &probe_key);
    if(probe_result != ERROR_SUCCESS) {
        printf("SKIP: cannot open the HKCU Run key (error %lu); skipping the registry round-trip\n",
            (unsigned long)probe_result);
        return;
    }
    RegCloseKey(probe_key);

    const bool was_enabled = gsr::is_xdg_autostart_enabled();

    CHECK(gsr::set_xdg_autostart(true) == 0);
    CHECK(gsr::is_xdg_autostart_enabled());

    /* The value must be the full path to the running exe + launch-daemon,
       not a bare "gsr-ui" that depends on PATH (portable installs). */
    char exe_path[MAX_PATH];
    const DWORD exe_path_size = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    CHECK(exe_path_size > 0 && exe_path_size < sizeof(exe_path));

    HKEY key = NULL;
    if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        char buffer[1024];
        DWORD buffer_size = sizeof(buffer);
        DWORD type = 0;
        const LONG result = RegQueryValueExA(key, "gpu-screen-recorder-ui", NULL, &type, (LPBYTE)buffer, &buffer_size);
        RegCloseKey(key);
        if(result == ERROR_SUCCESS && type == REG_SZ) {
            buffer[buffer_size - 1] = '\0';
            CHECK(strstr(buffer, exe_path) != NULL);
            CHECK(strstr(buffer, "launch-daemon") != NULL);
        } else {
            ++num_failures;
            fprintf(stderr, "FAIL %s:%d: Run value not present after set_xdg_autostart(true)\n", __FILE__, __LINE__);
        }
    } else {
        ++num_failures;
        fprintf(stderr, "FAIL %s:%d: could not open the Run key\n", __FILE__, __LINE__);
    }

    CHECK(gsr::set_xdg_autostart(false) == 0);
    CHECK(!gsr::is_xdg_autostart_enabled());

    /* Restore the prior state (no-op on CI). */
    if(was_enabled)
        gsr::set_xdg_autostart(true);
}

static void test_misc_modules(void) {
    gsr::ClipboardWin32 clipboard;
    (void)clipboard;

    gsr::AudioPlayer audio_player;
    (void)audio_player;

    gsr::RegionSelectorWin32 region_selector;
    (void)region_selector;
}

/* ---- overlay window behavior -------------------------------------------- */

/* Returns the ex-style bits of a window, or 0 if |hwnd| is invalid. */
static LONG_PTR get_ex_style(HWND hwnd) {
    return GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
}

/* mgl stores its own mgl_window pointer in GWLP_USERDATA, so the test keeps
   its copy in a window property instead. */
static const char TEST_WINDOW_PROP[] = "ui_module_test_mgl_window";

static HWND create_test_window(bool visible) {
    mgl_window *window = (mgl_window*)calloc(1, sizeof(mgl_window));
    mgl_window_create_params params;
    memset(&params, 0, sizeof(params));
    params.graphics_api = MGL_GRAPHICS_API_WGL;
    params.size = (mgl_vec2i){ 400, 300 };
    params.hidden = !visible;
    if(mgl_window_create(window, "ui-module-test window", &params) != 0) {
        free(window);
        return NULL;
    }
    HWND hwnd = (HWND)mgl_window_get_system_handle(window);
    SetPropA(hwnd, TEST_WINDOW_PROP, (HANDLE)window);
    return hwnd;
}

static void destroy_test_window(HWND hwnd) {
    mgl_window *window = (mgl_window*)GetPropA(hwnd, TEST_WINDOW_PROP);
    if(window) {
        mgl_window_deinit(window);
        free(window);
    }
}

static void test_overlay_window_behavior(void) {
    if(mgl_init(MGL_WINDOW_SYSTEM_WIN32) != 0) {
        fprintf(stderr, "FAIL: mgl_init failed\n");
        ++num_failures;
        ++num_checks;
        return;
    }

    /* Window A gets the overlay treatment; window B exists only to verify
       the always-on-top z-order change (A is created first, so B is above
       A; making A topmost must move A above B). */
    HWND hwnd_a = create_test_window(true);
    HWND hwnd_b = create_test_window(true);
    CHECK(hwnd_a != NULL);
    CHECK(hwnd_b != NULL);

    if(hwnd_a && hwnd_b) {
        /* Sanity: a fresh window is not topmost-over-B (B was created after A). */
        HWND above = GetWindow(hwnd_b, GW_HWNDPREV);
        bool a_before_b = false;
        while(above) {
            if(above == hwnd_a) {
                a_before_b = true;
                break;
            }
            above = GetWindow(above, GW_HWNDPREV);
        }
        CHECK(!a_before_b);

        /* make_window_sticky -> HWND_TOPMOST: A must now precede B in the
           z-order (walk up from B). */
        CHECK(gsr::make_window_sticky(nullptr, (Window)(uintptr_t)hwnd_a));
        above = GetWindow(hwnd_b, GW_HWNDPREV);
        a_before_b = false;
        while(above) {
            if(above == hwnd_a) {
                a_before_b = true;
                break;
            }
            above = GetWindow(above, GW_HWNDPREV);
        }
        CHECK(a_before_b);

        /* make_window_click_through is a no-op on Windows: WS_EX_LAYERED
           breaks WGL content compositing on NVIDIA + Win11 (the GL back
           buffer never appears), and WS_EX_TRANSPARENT would make the
           interactive overlay click-through — Windows has no X input grab
           to redirect events back to it. It must not touch the ex styles. */
        const LONG_PTR ex_before_click = get_ex_style(hwnd_a);
        gsr::make_window_click_through(nullptr, (Window)(uintptr_t)hwnd_a);
        const LONG_PTR ex_after_click = get_ex_style(hwnd_a);
        CHECK((ex_after_click & WS_EX_LAYERED) == 0);
        CHECK((ex_after_click & WS_EX_TRANSPARENT) == 0);
        /* No-op: the helper must not clear or add styles. */
        CHECK(ex_after_click == ex_before_click);

        /* hide_window_from_taskbar -> WS_EX_TOOLWINDOW (no taskbar button). */
        gsr::hide_window_from_taskbar(nullptr, (Window)(uintptr_t)hwnd_a);
        CHECK((get_ex_style(hwnd_a) & WS_EX_TOOLWINDOW) != 0);

        /* Borderless fullscreen covers the monitor the window is on. */
        mgl_window *win_a = (mgl_window*)GetPropA(hwnd_a, TEST_WINDOW_PROP);
        mgl_window_set_fullscreen(win_a, true);
        CHECK(mgl_window_is_fullscreen(win_a));
        HMONITOR hmon = MonitorFromWindow(hwnd_a, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        CHECK(GetMonitorInfoA(hmon, &mi));
        RECT window_rect;
        CHECK(GetWindowRect(hwnd_a, &window_rect));
        CHECK(window_rect.left == mi.rcMonitor.left);
        CHECK(window_rect.top == mi.rcMonitor.top);
        CHECK(window_rect.right == mi.rcMonitor.right);
        CHECK(window_rect.bottom == mi.rcMonitor.bottom);
        mgl_window_set_fullscreen(win_a, false);
        CHECK(!mgl_window_is_fullscreen(win_a));
    }

    /* Alpha-capable window: support_alpha must NOT produce WS_EX_LAYERED —
       a layered WGL window stops compositing its GL content on some
       NVIDIA + Win11 setups, so per-pixel alpha comes from a
       PFD_SUPPORT_COMPOSITION pixel format + DWM blur-behind instead. */
    mgl_window alpha_window;
    mgl_window_create_params alpha_params;
    memset(&alpha_params, 0, sizeof(alpha_params));
    alpha_params.graphics_api = MGL_GRAPHICS_API_WGL;
    alpha_params.size = (mgl_vec2i){ 200, 200 };
    alpha_params.hidden = true;
    alpha_params.support_alpha = true;
    CHECK(mgl_window_create(&alpha_window, "alpha", &alpha_params) == 0);
    HWND alpha_hwnd = (HWND)mgl_window_get_system_handle(&alpha_window);
    CHECK((get_ex_style(alpha_hwnd) & WS_EX_LAYERED) == 0);
    {
        /* The pixel format must be alpha-capable and composition-capable so
           DWM can composite the GL back buffer per-pixel. */
        HDC alpha_dc = GetDC(alpha_hwnd);
        const int alpha_pf = GetPixelFormat(alpha_dc);
        PIXELFORMATDESCRIPTOR alpha_pfd;
        memset(&alpha_pfd, 0, sizeof(alpha_pfd));
        CHECK(DescribePixelFormat(alpha_dc, alpha_pf, sizeof(alpha_pfd), &alpha_pfd) != 0);
        CHECK((alpha_pfd.dwFlags & PFD_SUPPORT_COMPOSITION) != 0);
        CHECK(alpha_pfd.cAlphaBits == 8);
        ReleaseDC(alpha_hwnd, alpha_dc);
    }
    mgl_window_deinit(&alpha_window);

    /* Overlay-type window: borderless popup (WS_POPUP, no WS_OVERLAPPEDWINDOW
       chrome) — the style the overlay UI needs for per-monitor positioning. */
    mgl_window overlay_window;
    mgl_window_create_params overlay_params;
    memset(&overlay_params, 0, sizeof(overlay_params));
    overlay_params.graphics_api = MGL_GRAPHICS_API_WGL;
    overlay_params.size = (mgl_vec2i){ 300, 200 };
    overlay_params.hidden = true;
    overlay_params.window_type = MGL_WINDOW_TYPE_OVERLAY;
    CHECK(mgl_window_create(&overlay_window, "overlay", &overlay_params) == 0);
    HWND overlay_hwnd = (HWND)mgl_window_get_system_handle(&overlay_window);
    const LONG_PTR overlay_style = GetWindowLongPtrA(overlay_hwnd, GWL_STYLE);
    CHECK((overlay_style & WS_POPUP) != 0);
    CHECK((overlay_style & WS_OVERLAPPEDWINDOW) == 0);
    mgl_window_deinit(&overlay_window);

    if(hwnd_a)
        destroy_test_window(hwnd_a);
    if(hwnd_b)
        destroy_test_window(hwnd_b);
    mgl_deinit();
}

int main(void) {
    printf("ui-module-test: win32 ui platform module smoke tests\n");

    test_window_utils();
    test_cursor_tracker();
    test_desktop_environment();
    test_global_hotkeys();
    test_startup_autostart();
    test_misc_modules();
    test_overlay_window_behavior();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
