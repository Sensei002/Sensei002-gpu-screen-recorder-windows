/*
    mgl Win32 backend smoke test (Phase 10 milestone A).

    Exercises the full mgl window vtable on Win32 headlessly:
      - window creation (hidden) + handle/size/title
      - WGL context creation + GL sanity (clear/swap/renderer string)
      - input events (key, char, mouse move/buttons, wheel) via synthetic messages
      - clipboard set/get round-trip
      - monitor enumeration
      - fullscreen toggle, size limits, visibility
      - init_from_existing_window (subclassing)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* GL constant values (GL_RENDERER, GL_VERSION, GL_NO_ERROR, ...). mgl's gl.h
   only declares the loader function pointers; the constants come from the
   platform GL header. MinGW-w64 ships GL/gl.h. */
#ifdef _WIN32
#include <GL/gl.h>
#endif

#include <mgl/mgl.h>
#include <mgl/window/window.h>
#include <mgl/window/event.h>
#include <mgl/graphics/text.h>
#include <mgl/graphics/font_atlas.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    ++checks; \
    if(!(cond)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

static void drain_events(mgl_window *window) {
    mgl_event event;
    while(mgl_window_poll_event(window, &event)) {
        /* discard */
    }
}

static bool poll_event_type(mgl_window *window, int expected_type, mgl_event *event) {
    while(mgl_window_poll_event(window, event)) {
        if(event->type == expected_type)
            return true;
    }
    return false;
}

/* Plain Win32 window class for the init_from_existing_window test. */
static LRESULT CALLBACK plain_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void mgl_count_monitor_callback(const mgl_monitor *monitor, void *userdata) {
    (void)monitor;
    (*(int*)userdata)++;
}

static HWND create_plain_window(const wchar_t *class_name, const wchar_t *title, int width, int height) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = plain_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);

    return CreateWindowExW(0, class_name, title, WS_OVERLAPPEDWINDOW,
        0, 0, width, height, NULL, NULL, GetModuleHandleW(NULL), NULL);
}

int main(void) {
    const int mgl_init_result = mgl_init(MGL_WINDOW_SYSTEM_WIN32);
    if(mgl_init_result != 0) {
        fprintf(stderr, "FAIL: mgl_init failed\n");
        return 1;
    }
    CHECK(mgl_is_connected_to_display_server());

    /* ---- window creation ------------------------------------------------ */
    mgl_window window;
    mgl_window_create_params params;
    memset(&params, 0, sizeof(params));
    params.graphics_api = MGL_GRAPHICS_API_WGL; /* EGL is enum value 0; the zeroed struct would request it */
    params.size = (mgl_vec2i){ 800, 600 };
    params.hidden = true;

    CHECK(mgl_window_create(&window, "mgl win32 test", &params) == 0);
    CHECK(mgl_window_is_open(&window));
    CHECK(!mgl_window_has_focus(&window));

    HWND hwnd = (HWND)mgl_window_get_system_handle(&window);
    CHECK(hwnd != NULL);
    CHECK(IsWindow(hwnd));

    CHECK(window.size.x == 800);
    CHECK(window.size.y == 600);

    drain_events(&window);

    /* ---- WGL context ---------------------------------------------------- */
    mgl_context *context = mgl_get_context();
    CHECK(context->gl.glGetString != NULL);

    const char *renderer = (const char*)context->gl.glGetString(GL_RENDERER);
    const char *version = (const char*)context->gl.glGetString(GL_VERSION);
    CHECK(renderer != NULL);
    CHECK(version != NULL);
    printf("GL_RENDERER: %s\n", renderer ? renderer : "(null)");
    printf("GL_VERSION: %s\n", version ? version : "(null)");

    context->gl.glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    context->gl.glClear(GL_COLOR_BUFFER_BIT);
    CHECK(context->gl.glGetError() == GL_NO_ERROR);
    mgl_window_display(&window);

    /* The WGL backend has no EGL display/context (documented; the preview
       sharing integration is a later milestone). */
    CHECK(mgl_window_get_egl_display(&window) == NULL);
    CHECK(mgl_window_get_egl_context(&window) == NULL);

    /* ---- text pipeline (Phase 10 milestone B) ---------------------------- */
    /* The default UI font must resolve without GSettings on Windows. */
    char default_font[64];
    CHECK(mgl_text_get_default_font_name(default_font, sizeof(default_font)));
    CHECK(strlen(default_font) > 0);
    fprintf(stderr, "default font: %s\n", default_font);

    mgl_text text;
    mgl_text_init(&text, "Hello mgl text", -1, "Sans 16");
    CHECK(mgl_text_get_string(&text, NULL) != NULL);
    CHECK(mgl_text_get_font_size(&text) > 0);

    mgl_vec2i text_size = mgl_text_get_size(&text);
    CHECK(text_size.x > 0);
    CHECK(text_size.y > 0);
    fprintf(stderr, "text size: %dx%d\n", text_size.x, text_size.y);

    /* Wrapping + max rows (ellipsis). */
    mgl_text_set_wrap_width(&text, 100);
    mgl_text_set_max_rows(&text, 2);
    mgl_text_set_wrap_width(&text, 0);
    mgl_text_set_max_rows(&text, 0);

    /* Caret/position lookups. */
    mgl_vec2f char_pos = mgl_text_find_character_pos(&text, 2);
    CHECK(char_pos.x >= 0.0f);
    mgl_index_codepoint_pair caret =
        mgl_text_find_closest_caret_index_by_position(&text, (mgl_vec2f){ 4.0f, 4.0f });
    CHECK(caret.byte_index >= 0);
    CHECK(caret.codepoint_index >= 0);

    /* Copy + string set. */
    mgl_text text_copy;
    mgl_text_copy(&text, &text_copy);
    CHECK(mgl_text_get_size(&text_copy).x > 0);
    mgl_text_set_string(&text, "short");
    int text_len = 0;
    const char *str = mgl_text_get_string(&text, &text_len);
    CHECK(text_len == 5);
    CHECK(strcmp(str, "short") == 0);

    /* Draw: rasterizes glyphs into the font atlas (freetype via pangoft2)
       and draws the quads through the GL 1.1 batch — the full path. */
    mgl_text_set_string(&text, "Hello mgl text", -1);
    mgl_text_set_position(&text, (mgl_vec2f){ 8.0f, 8.0f });
    mgl_text_set_color(&text, (mgl_color){ 255, 255, 255, 255 });
    mgl_text_draw(&text);

    const mgl_font_atlas *atlas =
        mgl_text_renderer_get_atlas(mgl_get_text_renderer());
    CHECK(atlas->cache.count > 0);
    fprintf(stderr, "atlas glyphs: %u\n", atlas->cache.count);

    /* Second draw reuses the cached glyphs (no new atlas entries needed
       for the same string). */
    const uint32_t glyphs_after_first_draw = atlas->cache.count;
    mgl_text_draw(&text);
    CHECK(atlas->cache.count == glyphs_after_first_draw);

    /* Mixed-script text exercises font fallback (e.g. Yu Gothic for the
       CJK glyphs when fontconfig finds it). Informational only — the CI
       runner's font set is not guaranteed to include a CJK face. */
    mgl_text fallback_text;
    mgl_text_init(&fallback_text, "Hello 世界", -1, "Sans 14");
    CHECK(mgl_text_get_size(&fallback_text).x > 0);
    mgl_text_draw(&fallback_text);
    fprintf(stderr, "fallback atlas glyphs: %u\n", atlas->cache.count);
    mgl_text_deinit(&fallback_text);

    mgl_text_deinit(&text_copy);
    mgl_text_deinit(&text);

    /* ---- title ----------------------------------------------------------- */
    mgl_window_set_title(&window, "renamed");
    wchar_t title_buf[64];
    CHECK(GetWindowTextW(hwnd, title_buf, 64) > 0);
    CHECK(wcscmp(title_buf, L"renamed") == 0);

    /* ---- visibility ------------------------------------------------------ */
    CHECK(!IsWindowVisible(hwnd));
    mgl_window_set_visible(&window, true);
    CHECK(IsWindowVisible(hwnd));
    mgl_window_set_visible(&window, false);
    CHECK(!IsWindowVisible(hwnd));

    /* Showing the window queued a real WM_SIZE (800x600); drop it so the
       synthetic resize below is the next RESIZED event. */
    drain_events(&window);

    /* ---- resize event ---------------------------------------------------- */
    SendMessageW(hwnd, WM_SIZE, 0, MAKELPARAM(640, 480));
    mgl_event event;
    CHECK(poll_event_type(&window, MGL_EVENT_RESIZED, &event));
    CHECK(event.size.width == 640);
    CHECK(event.size.height == 480);
    CHECK(window.size.x == 640);
    CHECK(window.size.y == 480);

    /* ---- key events ------------------------------------------------------ */
    SendMessageW(hwnd, WM_KEYDOWN, 'A', 1);
    CHECK(poll_event_type(&window, MGL_EVENT_KEY_PRESSED, &event));
    CHECK(event.key.code == MGL_KEY_A);

    SendMessageW(hwnd, WM_KEYUP, 'A', 1);
    CHECK(poll_event_type(&window, MGL_EVENT_KEY_RELEASED, &event));
    CHECK(event.key.code == MGL_KEY_A);

    SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 1);
    CHECK(poll_event_type(&window, MGL_EVENT_KEY_PRESSED, &event));
    CHECK(event.key.code == MGL_KEY_ESCAPE);

    CHECK(!mgl_window_is_key_pressed(&window, MGL_KEY_UNKNOWN));

    /* ---- text events ----------------------------------------------------- */
    SendMessageW(hwnd, WM_CHAR, L'a', 1);
    CHECK(poll_event_type(&window, MGL_EVENT_TEXT_ENTERED, &event));
    CHECK(event.text.codepoint == 'a');
    CHECK(strcmp(event.text.str, "a") == 0);

    /* surrogate pair: U+1F600 */
    SendMessageW(hwnd, WM_CHAR, 0xD83D, 1);
    SendMessageW(hwnd, WM_CHAR, 0xDE00, 1);
    CHECK(poll_event_type(&window, MGL_EVENT_TEXT_ENTERED, &event));
    CHECK(event.text.codepoint == 0x1F600);

    /* ---- mouse events ---------------------------------------------------- */
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    CHECK(poll_event_type(&window, MGL_EVENT_MOUSE_MOVED, &event));
    CHECK(event.mouse_move.x == 10);
    CHECK(event.mouse_move.y == 20);
    CHECK(window.cursor_position.x == 10);
    CHECK(window.cursor_position.y == 20);

    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 6));
    CHECK(poll_event_type(&window, MGL_EVENT_MOUSE_BUTTON_PRESSED, &event));
    CHECK(event.mouse_button.button == MGL_BUTTON_LEFT);
    CHECK(event.mouse_button.x == 5);
    CHECK(event.mouse_button.y == 6);

    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(5, 6));
    CHECK(poll_event_type(&window, MGL_EVENT_MOUSE_BUTTON_RELEASED, &event));
    CHECK(event.mouse_button.button == MGL_BUTTON_LEFT);

    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0);
    CHECK(poll_event_type(&window, MGL_EVENT_MOUSE_WHEEL_SCROLLED, &event));
    CHECK(event.mouse_wheel_scroll.delta == 1);

    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (DWORD)-120), 0);
    CHECK(poll_event_type(&window, MGL_EVENT_MOUSE_WHEEL_SCROLLED, &event));
    CHECK(event.mouse_wheel_scroll.delta == -1);

    /* ---- size limits ----------------------------------------------------- */
    mgl_window_set_size_limits(&window, (mgl_vec2i){ 320, 240 }, (mgl_vec2i){ 1280, 1024 });
    MINMAXINFO mmi;
    memset(&mmi, 0, sizeof(mmi));
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&mmi);
    CHECK(mmi.ptMinTrackSize.x > 0);
    CHECK(mmi.ptMaxTrackSize.x > 0);

    /* ---- fullscreen ------------------------------------------------------ */
    mgl_window_set_fullscreen(&window, true);
    CHECK(mgl_window_is_fullscreen(&window));
    mgl_window_set_fullscreen(&window, false);
    CHECK(!mgl_window_is_fullscreen(&window));

    /* ---- clipboard ------------------------------------------------------- */
    mgl_window_set_clipboard(&window, "hello clipboard", 15);
    char *clip = NULL;
    size_t clip_size = 0;
    CHECK(mgl_window_get_clipboard_string(&window, &clip, &clip_size));
    CHECK(clip_size == 15);
    CHECK(clip && memcmp(clip, "hello clipboard", 15) == 0);
    free(clip);

    /* ---- monitors -------------------------------------------------------- */
    CHECK(window.num_monitors >= 1);
    int monitor_callback_count = 0;
    mgl_window_for_each_active_monitor_output(&window, mgl_count_monitor_callback, &monitor_callback_count);
    CHECK(monitor_callback_count == window.num_monitors);

    /* ---- init_from_existing_window (subclassing) ------------------------- */
    HWND plain_hwnd = create_plain_window(L"mgl_test_plain", L"plain", 400, 300);
    CHECK(plain_hwnd != NULL);

    mgl_window existing_window;
    CHECK(mgl_window_init_from_existing_window(&existing_window, (mgl_window_handle)plain_hwnd) == 0);
    CHECK(mgl_window_get_system_handle(&existing_window) == (mgl_window_handle)plain_hwnd);
    /* mgl window size is the client size (frame chrome excluded). */
    RECT client_rect;
    GetClientRect(plain_hwnd, &client_rect);
    CHECK(existing_window.size.x == client_rect.right - client_rect.left);
    CHECK(existing_window.size.y == client_rect.bottom - client_rect.top);

    mgl_window_deinit(&existing_window);
    CHECK(DestroyWindow(plain_hwnd) != 0);

    /* ---- close + deinit -------------------------------------------------- */
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    CHECK(poll_event_type(&window, MGL_EVENT_CLOSED, &event));
    CHECK(!mgl_window_is_open(&window));

    mgl_window_deinit(&window);
    mgl_deinit();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
