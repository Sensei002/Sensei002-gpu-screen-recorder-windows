#include "../../include/mgl/window/win32.h"
#include "../../include/mgl/window/event.h"
#include "../../include/mgl/mgl.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Should be in range [2,] */
#define MAX_STACKED_EVENTS 32

typedef struct {
    mgl_event stack[MAX_STACKED_EVENTS];
    int start;
    int end;
    int size;
} win32_events_circular_buffer;

typedef struct {
    HWND window;
    bool created_window; /* false when attached to an existing window */
    bool subclassed;
    WNDPROC prev_wnd_proc;

    mgl_graphics graphics;
    bool graphics_ready;

    win32_events_circular_buffer events;

    bool support_alpha;
    bool cursor_visible;
    HCURSOR arrow_cursor;

    /* size limits (client size), (0, 0) = no limit */
    bool size_limits_set;
    mgl_vec2i min_size;
    mgl_vec2i max_size;

    /* fullscreen restore state */
    bool is_fullscreen;
    RECT saved_rect;
    LONG saved_style;
    LONG saved_ex_style;

    /* key repeat suppression */
    int prev_vk;
    bool key_was_released;

    /* utf16 text input state */
    wchar_t pending_high_surrogate;
} mgl_window_win32;

static void mgl_window_win32_deinit(mgl_window *self);
static void mgl_window_win32_on_resize(mgl_window *self, int width, int height);
static void mgl_window_win32_update_frame_time_limit_monitor(mgl_window *self);

static void win32_events_circular_buffer_init(win32_events_circular_buffer *self) {
    self->start = 0;
    self->end = 0;
    self->size = 0;
}

static bool win32_events_circular_buffer_append(win32_events_circular_buffer *self, const mgl_event *event) {
    if(self->size == MAX_STACKED_EVENTS)
        return false;

    self->stack[self->end] = *event;
    self->end = (self->end + 1) % MAX_STACKED_EVENTS;
    ++self->size;
    return true;
}

static bool win32_events_circular_buffer_pop(win32_events_circular_buffer *self, mgl_event *event) {
    if(self->size == 0)
        return false;

    *event = self->stack[self->start];
    self->start = (self->start + 1) % MAX_STACKED_EVENTS;
    --self->size;
    return true;
}

static bool mgl_window_win32_append_event(mgl_window_win32 *self, const mgl_event *event) {
    return win32_events_circular_buffer_append(&self->events, event);
}

static bool mgl_window_win32_pop_event(mgl_window_win32 *self, mgl_event *event) {
    return win32_events_circular_buffer_pop(&self->events, event);
}

/* ---- key mapping -------------------------------------------------------- */

/* Returns MGL_KEY_UNKNOWN on no match. */
static mgl_key vk_to_mgl_key(unsigned int vk, bool extended) {
    if(vk >= 'A' && vk <= 'Z')
        return MGL_KEY_A + (vk - 'A');
    if(vk >= '0' && vk <= '9')
        return MGL_KEY_NUM0 + (vk - '0');
    if(vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return MGL_KEY_NUMPAD0 + (vk - VK_NUMPAD0);

    switch(vk) {
        case VK_SPACE:                return MGL_KEY_SPACE;
        case VK_BACK:                 return MGL_KEY_BACKSPACE;
        case VK_TAB:                  return MGL_KEY_TAB;
        case VK_RETURN:               return extended ? MGL_KEY_NUMPAD_ENTER : MGL_KEY_ENTER;
        case VK_ESCAPE:               return MGL_KEY_ESCAPE;
        case VK_LCONTROL:             return MGL_KEY_LCONTROL;
        case VK_LSHIFT:               return MGL_KEY_LSHIFT;
        case VK_LMENU:                return MGL_KEY_LALT;
        case VK_LWIN:                 return MGL_KEY_LSYSTEM;
        case VK_RCONTROL:             return MGL_KEY_RCONTROL;
        case VK_RSHIFT:               return MGL_KEY_RSHIFT;
        case VK_RMENU:                return MGL_KEY_RALT;
        case VK_RWIN:                 return MGL_KEY_RSYSTEM;
        case VK_APPS:                 return MGL_KEY_MENU;
        case VK_DELETE:               return MGL_KEY_DELETE;
        case VK_HOME:                 return MGL_KEY_HOME;
        case VK_LEFT:                 return MGL_KEY_LEFT;
        case VK_UP:                   return MGL_KEY_UP;
        case VK_RIGHT:                return MGL_KEY_RIGHT;
        case VK_DOWN:                 return MGL_KEY_DOWN;
        case VK_PRIOR:                return MGL_KEY_PAGEUP;
        case VK_NEXT:                 return MGL_KEY_PAGEDOWN;
        case VK_END:                  return MGL_KEY_END;
        case VK_INSERT:               return MGL_KEY_INSERT;
        case VK_PAUSE:                return MGL_KEY_PAUSE;
        case VK_SNAPSHOT:             return MGL_KEY_PRINTSCREEN;
        case VK_ADD:                  return MGL_KEY_ADD;
        case VK_SUBTRACT:             return MGL_KEY_SUBTRACT;
        case VK_MULTIPLY:             return MGL_KEY_MULTIPLY;
        case VK_DIVIDE:               return MGL_KEY_DIVIDE;
        case VK_OEM_PLUS:             return MGL_KEY_EQUAL;
        case VK_OEM_MINUS:            return MGL_KEY_HYPHEN;
        case VK_OEM_1:                return MGL_KEY_SEMICOLON;
        case VK_OEM_2:                return MGL_KEY_SLASH;
        case VK_OEM_3:                return MGL_KEY_TILDE;
        case VK_OEM_4:                return MGL_KEY_LBRACKET;
        case VK_OEM_5:                return MGL_KEY_BACKSLASH;
        case VK_OEM_6:                return MGL_KEY_RBRACKET;
        case VK_OEM_7:                return MGL_KEY_QUOTE;
        case VK_OEM_COMMA:            return MGL_KEY_COMMA;
        case VK_OEM_PERIOD:           return MGL_KEY_PERIOD;
        case VK_VOLUME_DOWN:          return MGL_KEY_AUDIO_LOWER_VOLUME;
        case VK_VOLUME_UP:            return MGL_KEY_AUDIO_RAISE_VOLUME;
        case VK_VOLUME_MUTE:          return MGL_KEY_AUDIO_MUTE;
        case VK_MEDIA_NEXT_TRACK:     return MGL_KEY_AUDIO_NEXT;
        case VK_MEDIA_PREV_TRACK:     return MGL_KEY_AUDIO_PREV;
        case VK_MEDIA_STOP:           return MGL_KEY_AUDIO_STOP;
        case VK_MEDIA_PLAY_PAUSE:     return MGL_KEY_AUDIO_PLAY;
    }

    if(vk >= VK_F1 && vk <= VK_F24)
        return MGL_KEY_F1 + (vk - VK_F1);

    return MGL_KEY_UNKNOWN;
}

/* Returns 0 on no match. */
static unsigned int mgl_key_to_vk(mgl_key key) {
    if(key >= MGL_KEY_A && key <= MGL_KEY_Z)
        return 'A' + (key - MGL_KEY_A);
    if(key >= MGL_KEY_NUM0 && key <= MGL_KEY_NUM9)
        return '0' + (key - MGL_KEY_NUM0);
    if(key >= MGL_KEY_NUMPAD0 && key <= MGL_KEY_NUMPAD9)
        return VK_NUMPAD0 + (key - MGL_KEY_NUMPAD0);
    if(key >= MGL_KEY_F1 && key <= MGL_KEY_F24)
        return VK_F1 + (key - MGL_KEY_F1);

    switch(key) {
        case MGL_KEY_SPACE:                return VK_SPACE;
        case MGL_KEY_BACKSPACE:            return VK_BACK;
        case MGL_KEY_TAB:                  return VK_TAB;
        case MGL_KEY_ENTER:                return VK_RETURN;
        case MGL_KEY_NUMPAD_ENTER:         return VK_RETURN;
        case MGL_KEY_ESCAPE:               return VK_ESCAPE;
        case MGL_KEY_LCONTROL:             return VK_LCONTROL;
        case MGL_KEY_LSHIFT:               return VK_LSHIFT;
        case MGL_KEY_LALT:                 return VK_LMENU;
        case MGL_KEY_LSYSTEM:              return VK_LWIN;
        case MGL_KEY_RCONTROL:             return VK_RCONTROL;
        case MGL_KEY_RSHIFT:               return VK_RSHIFT;
        case MGL_KEY_RALT:                 return VK_RMENU;
        case MGL_KEY_RSYSTEM:              return VK_RWIN;
        case MGL_KEY_MENU:                 return VK_APPS;
        case MGL_KEY_DELETE:               return VK_DELETE;
        case MGL_KEY_HOME:                 return VK_HOME;
        case MGL_KEY_LEFT:                 return VK_LEFT;
        case MGL_KEY_UP:                   return VK_UP;
        case MGL_KEY_RIGHT:                return VK_RIGHT;
        case MGL_KEY_DOWN:                 return VK_DOWN;
        case MGL_KEY_PAGEUP:               return VK_PRIOR;
        case MGL_KEY_PAGEDOWN:             return VK_NEXT;
        case MGL_KEY_END:                  return VK_END;
        case MGL_KEY_INSERT:               return VK_INSERT;
        case MGL_KEY_PAUSE:                return VK_PAUSE;
        case MGL_KEY_PRINTSCREEN:          return VK_SNAPSHOT;
        case MGL_KEY_ADD:                  return VK_ADD;
        case MGL_KEY_SUBTRACT:             return VK_SUBTRACT;
        case MGL_KEY_MULTIPLY:             return VK_MULTIPLY;
        case MGL_KEY_DIVIDE:               return VK_DIVIDE;
        case MGL_KEY_EQUAL:                return VK_OEM_PLUS;
        case MGL_KEY_HYPHEN:               return VK_OEM_MINUS;
        case MGL_KEY_SEMICOLON:            return VK_OEM_1;
        case MGL_KEY_SLASH:                return VK_OEM_2;
        case MGL_KEY_TILDE:                return VK_OEM_3;
        case MGL_KEY_LBRACKET:             return VK_OEM_4;
        case MGL_KEY_BACKSLASH:            return VK_OEM_5;
        case MGL_KEY_RBRACKET:             return VK_OEM_6;
        case MGL_KEY_QUOTE:                return VK_OEM_7;
        case MGL_KEY_COMMA:                return VK_OEM_COMMA;
        case MGL_KEY_PERIOD:               return VK_OEM_PERIOD;
        case MGL_KEY_AUDIO_LOWER_VOLUME:   return VK_VOLUME_DOWN;
        case MGL_KEY_AUDIO_RAISE_VOLUME:   return VK_VOLUME_UP;
        case MGL_KEY_AUDIO_MUTE:           return VK_VOLUME_MUTE;
        case MGL_KEY_AUDIO_NEXT:           return VK_MEDIA_NEXT_TRACK;
        case MGL_KEY_AUDIO_PREV:           return VK_MEDIA_PREV_TRACK;
        case MGL_KEY_AUDIO_STOP:           return VK_MEDIA_STOP;
        case MGL_KEY_AUDIO_PLAY:           return VK_MEDIA_PLAY_PAUSE;
        default:                           return 0;
    }
}

static mgl_key_states mgl_win32_key_states(void) {
    return (mgl_key_states) {
        .alt = ((GetKeyState(VK_MENU) & 0x8000) != 0),
        .control = ((GetKeyState(VK_CONTROL) & 0x8000) != 0),
        .shift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0),
        .system = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0),
    };
}

static mgl_mouse_button win32_button_to_mgl_button(unsigned int button) {
    switch(button) {
        case MK_LBUTTON:   return MGL_BUTTON_LEFT;
        case MK_RBUTTON:   return MGL_BUTTON_RIGHT;
        case MK_MBUTTON:   return MGL_BUTTON_MIDDLE;
        case MK_XBUTTON1:  return MGL_BUTTON_XBUTTON1;
        case MK_XBUTTON2:  return MGL_BUTTON_XBUTTON2;
    }
    return MGL_BUTTON_UNKNOWN;
}

/* ---- text input --------------------------------------------------------- */

static void mgl_utf32_encode_codepoint(uint32_t codepoint, char *out, int *size) {
    unsigned char *o = (unsigned char*)out;
    if(codepoint < 0x80) {
        o[0] = (unsigned char)codepoint;
        *size = 1;
    } else if(codepoint < 0x800) {
        o[0] = (unsigned char)(0xC0 | (codepoint >> 6));
        o[1] = (unsigned char)(0x80 | (codepoint & 0x3F));
        *size = 2;
    } else if(codepoint < 0x10000) {
        o[0] = (unsigned char)(0xE0 | (codepoint >> 12));
        o[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (codepoint & 0x3F));
        *size = 3;
    } else {
        o[0] = (unsigned char)(0xF0 | (codepoint >> 18));
        o[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3F));
        o[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        o[3] = (unsigned char)(0x80 | (codepoint & 0x3F));
        *size = 4;
    }
}

static void mgl_window_win32_handle_text(mgl_window_win32 *impl, wchar_t wch) {
    uint32_t codepoint;
    if(wch >= 0xD800 && wch <= 0xDBFF) {
        /* High surrogate: wait for the matching low surrogate. */
        impl->pending_high_surrogate = wch;
        return;
    } else if(wch >= 0xDC00 && wch <= 0xDFFF) {
        if(impl->pending_high_surrogate) {
            codepoint = 0x10000 + ((uint32_t)(impl->pending_high_surrogate - 0xD800) << 10) + (uint32_t)(wch - 0xDC00);
        } else {
            codepoint = 0xFFFD; /* lone low surrogate */
        }
        impl->pending_high_surrogate = 0;
    } else {
        impl->pending_high_surrogate = 0;
        codepoint = (uint32_t)wch;
    }

    mgl_event event;
    event.type = MGL_EVENT_TEXT_ENTERED;
    event.text.codepoint = codepoint;
    mgl_utf32_encode_codepoint(codepoint, event.text.str, &event.text.size);
    event.text.str[event.text.size] = '\0';
    mgl_window_win32_append_event(impl, &event);
}

/* ---- monitors ----------------------------------------------------------- */

static void mgl_window_win32_clear_monitors(mgl_window *self) {
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(monitor->name) {
            free((char*)monitor->name);
            monitor->name = NULL;
        }
    }
    self->num_monitors = 0;
}

static bool mgl_window_win32_add_monitor(mgl_window *self, int id, const char *name, mgl_vec2i pos, mgl_vec2i size, int refresh_rate) {
    if(self->num_monitors == MGL_MAX_MONITORS)
        return false;

    mgl_monitor *monitor = &self->monitors[self->num_monitors];
    monitor->id = id;
    monitor->crtc_id = 0;
    monitor->name = strdup(name);
    if(!monitor->name)
        return false;
    monitor->pos = pos;
    monitor->size = size;
    monitor->refresh_rate = refresh_rate;
    self->num_monitors++;

    return true;
}

static BOOL CALLBACK mgl_window_win32_monitor_enum_callback(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    (void)hdc;
    (void)rect;
    mgl_window *self = (mgl_window*)lparam;

    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if(!GetMonitorInfoW(hmon, (MONITORINFO*)&mi))
        return TRUE;

    DEVMODEW dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    int refresh_rate = 0;
    if(EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        refresh_rate = (int)dm.dmDisplayFrequency;

    char name[64];
    int name_len = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name, sizeof(name), NULL, NULL);
    if(name_len <= 0)
        strcpy(name, "Unknown");

    mgl_window_win32_add_monitor(self, (int)(intptr_t)hmon, name,
        (mgl_vec2i){ .x = mi.rcMonitor.left, .y = mi.rcMonitor.top },
        (mgl_vec2i){ .x = mi.rcMonitor.right - mi.rcMonitor.left, .y = mi.rcMonitor.bottom - mi.rcMonitor.top },
        refresh_rate);
    return TRUE;
}

static void mgl_window_win32_refresh_monitors(mgl_window *self) {
    mgl_window_win32_clear_monitors(self);
    EnumDisplayMonitors(NULL, NULL, mgl_window_win32_monitor_enum_callback, (LPARAM)self);
}

typedef struct {
    mgl_active_monitor_callback callback;
    void *userdata;
} mgl_window_win32_monitor_callback_ctx;

static BOOL CALLBACK mgl_window_win32_monitor_for_each_callback(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    (void)hdc;
    (void)rect;
    mgl_window_win32_monitor_callback_ctx *ctx = (mgl_window_win32_monitor_callback_ctx*)lparam;

    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if(!GetMonitorInfoW(hmon, (MONITORINFO*)&mi))
        return TRUE;

    DEVMODEW dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    int refresh_rate = 0;
    if(EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        refresh_rate = (int)dm.dmDisplayFrequency;

    char name[64];
    int name_len = WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name, sizeof(name), NULL, NULL);
    if(name_len <= 0)
        strcpy(name, "Unknown");

    mgl_monitor monitor = {
        .id = (int)(intptr_t)hmon,
        .crtc_id = 0,
        .name = name,
        .pos = { .x = mi.rcMonitor.left, .y = mi.rcMonitor.top },
        .size = { .x = mi.rcMonitor.right - mi.rcMonitor.left, .y = mi.rcMonitor.bottom - mi.rcMonitor.top },
        .refresh_rate = refresh_rate,
    };
    ctx->callback(&monitor, ctx->userdata);
    return TRUE;
}

static void mgl_window_win32_for_each_active_monitor_output(mgl_window *self, mgl_active_monitor_callback callback, void *userdata) {
    (void)self;
    mgl_window_win32_monitor_callback_ctx ctx = { .callback = callback, .userdata = userdata };
    EnumDisplayMonitors(NULL, NULL, mgl_window_win32_monitor_for_each_callback, (LPARAM)&ctx);
}

static bool rectangle_intersects(mgl_vec2i pos1, mgl_vec2i size1, mgl_vec2i pos2, mgl_vec2i size2, int shrink) {
    pos1.x += shrink;
    pos1.y += shrink;

    size1.x -= shrink;
    size1.y -= shrink;

    pos2.x += shrink;
    pos2.y += shrink;

    size2.x -= shrink;
    size2.y -= shrink;

    return (pos1.x + size1.x >= pos2.x && pos1.x <= pos2.x + size2.x)
        && (pos1.y + size1.y >= pos2.y && pos1.y <= pos2.y + size2.y);
}

static void mgl_window_win32_update_frame_time_limit_monitor(mgl_window *self) {
    int monitor_refresh_rate = 0;
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(rectangle_intersects(self->pos, self->size, monitor->pos, monitor->size, 1)) {
            if(monitor->refresh_rate > monitor_refresh_rate)
                monitor_refresh_rate = monitor->refresh_rate;
        }
    }

    if(monitor_refresh_rate == 0 && self->num_monitors > 0)
        monitor_refresh_rate = self->monitors[0].refresh_rate;

    if(monitor_refresh_rate == 0)
        monitor_refresh_rate = 60;

    self->frame_time_limit_monitor = 1.0 / (double)monitor_refresh_rate;
}

/* ---- window state helpers ----------------------------------------------- */

static void mgl_window_win32_on_move(mgl_window *self, int x, int y) {
    self->pos.x = x;
    self->pos.y = y;
    mgl_window_win32_update_frame_time_limit_monitor(self);
}

static void mgl_window_win32_on_resize(mgl_window *self, int width, int height) {
    self->size.x = width;
    self->size.y = height;

    mgl_window_win32 *impl = self->impl;
    if(!impl->graphics_ready)
        return;

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });
}

static void mgl_window_win32_set_size_limits(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum) {
    mgl_window_win32 *impl = self->impl;
    impl->size_limits_set = minimum.x || minimum.y || maximum.x || maximum.y;
    impl->min_size = minimum;
    impl->max_size = maximum;
}

/* ---- window proc -------------------------------------------------------- */

static const wchar_t *mgl_win32_class_name = L"mgl_window_win32";
static bool mgl_win32_class_registered = false;

static LRESULT CALLBACK mgl_window_win32_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void mgl_win32_register_class(void) {
    if(mgl_win32_class_registered)
        return;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC; /* stable DC for GL */
    wc.lpfnWndProc = mgl_window_win32_wnd_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = mgl_win32_class_name;
    wc.cbWndExtra = sizeof(void*); /* stores the mgl_window* */
    RegisterClassExW(&wc);
    mgl_win32_class_registered = true;
}

static LRESULT CALLBACK mgl_window_win32_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch(msg) {
        case WM_NCCREATE: {
            const CREATESTRUCTW *cs = (const CREATESTRUCTW*)lparam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
    }

    mgl_window *self = (mgl_window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if(!self)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    mgl_window_win32 *impl = self->impl;
    mgl_event event;
    memset(&event, 0, sizeof(event));
    event.type = MGL_EVENT_UNKNOWN;

    switch(msg) {
        case WM_CLOSE: {
            event.type = MGL_EVENT_CLOSED;
            self->open = false;
            mgl_window_win32_append_event(impl, &event);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        case WM_SIZE: {
            if(wparam != SIZE_MINIMIZED) {
                const int width = (int)(short)LOWORD(lparam);
                const int height = (int)(short)HIWORD(lparam);
                mgl_window_win32_on_resize(self, width, height);
                event.type = MGL_EVENT_RESIZED;
                event.size.width = width;
                event.size.height = height;
                mgl_window_win32_append_event(impl, &event);
            }
            return 0;
        }
        case WM_MOVE: {
            const int x = (int)(short)LOWORD(lparam);
            const int y = (int)(short)HIWORD(lparam);
            mgl_window_win32_on_move(self, x, y);
            return 0;
        }
        case WM_SETFOCUS: {
            self->focused = true;
            event.type = MGL_EVENT_GAINED_FOCUS;
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_KILLFOCUS: {
            self->focused = false;
            event.type = MGL_EVENT_LOST_FOCUS;
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if(!self->key_repeat_enabled && (int)wparam == impl->prev_vk && !impl->key_was_released)
                return 0;

            impl->prev_vk = (int)wparam;
            impl->key_was_released = false;

            event.type = MGL_EVENT_KEY_PRESSED;
            event.key.code = vk_to_mgl_key((unsigned int)wparam, (lparam & 0x01000000) != 0);
            event.key.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if((int)wparam == impl->prev_vk)
                impl->key_was_released = true;

            event.type = MGL_EVENT_KEY_RELEASED;
            event.key.code = vk_to_mgl_key((unsigned int)wparam, (lparam & 0x01000000) != 0);
            event.key.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_CHAR: {
            mgl_window_win32_handle_text(impl, (wchar_t)wparam);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int x = (int)(short)LOWORD(lparam);
            const int y = (int)(short)HIWORD(lparam);
            self->cursor_position.x = x;
            self->cursor_position.y = y;

            event.type = MGL_EVENT_MOUSE_MOVED;
            event.mouse_move.x = x;
            event.mouse_move.y = y;
            event.mouse_move.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            unsigned int button = (msg == WM_LBUTTONDOWN) ? MK_LBUTTON
                : (msg == WM_RBUTTONDOWN) ? MK_RBUTTON
                : (msg == WM_MBUTTONDOWN) ? MK_MBUTTON
                : (unsigned int)(HIWORD(wparam) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2);
            event.type = MGL_EVENT_MOUSE_BUTTON_PRESSED;
            event.mouse_button.button = win32_button_to_mgl_button(button);
            event.mouse_button.x = (int)(short)LOWORD(lparam);
            event.mouse_button.y = (int)(short)HIWORD(lparam);
            event.mouse_button.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP: {
            unsigned int button = (msg == WM_LBUTTONUP) ? MK_LBUTTON
                : (msg == WM_RBUTTONUP) ? MK_RBUTTON
                : (msg == WM_MBUTTONUP) ? MK_MBUTTON
                : (unsigned int)(HIWORD(wparam) == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2);
            event.type = MGL_EVENT_MOUSE_BUTTON_RELEASED;
            event.mouse_button.button = win32_button_to_mgl_button(button);
            event.mouse_button.x = (int)(short)LOWORD(lparam);
            event.mouse_button.y = (int)(short)HIWORD(lparam);
            event.mouse_button.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt;
            pt.x = (int)(short)LOWORD(lparam);
            pt.y = (int)(short)HIWORD(lparam);
            ScreenToClient(hwnd, &pt);

            event.type = MGL_EVENT_MOUSE_WHEEL_SCROLLED;
            event.mouse_wheel_scroll.delta = ((int)(short)HIWORD(wparam)) / WHEEL_DELTA;
            event.mouse_wheel_scroll.x = pt.x;
            event.mouse_wheel_scroll.y = pt.y;
            event.mouse_wheel_scroll.key_states = mgl_win32_key_states();
            mgl_window_win32_append_event(impl, &event);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            if(impl->size_limits_set) {
                MINMAXINFO *mmi = (MINMAXINFO*)lparam;
                const LONG style = GetWindowLongPtrW(hwnd, GWL_STYLE);
                const LONG ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                RECT min_rect = { 0, 0, impl->min_size.x, impl->min_size.y };
                RECT max_rect = { 0, 0, impl->max_size.x, impl->max_size.y };
                AdjustWindowRectEx(&min_rect, style, FALSE, ex_style);
                AdjustWindowRectEx(&max_rect, style, FALSE, ex_style);
                if(impl->min_size.x || impl->min_size.y) {
                    mmi->ptMinTrackSize.x = min_rect.right - min_rect.left;
                    mmi->ptMinTrackSize.y = min_rect.bottom - min_rect.top;
                }
                if(impl->max_size.x || impl->max_size.y) {
                    mmi->ptMaxTrackSize.x = max_rect.right - max_rect.left;
                    mmi->ptMaxTrackSize.y = max_rect.bottom - max_rect.top;
                }
            }
            return 0;
        }
        case WM_SETCURSOR: {
            if(LOWORD(lparam) == HTCLIENT) {
                SetCursor(impl->cursor_visible ? impl->arrow_cursor : NULL);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_ERASEBKGND: {
            return 1; /* GL paints everything; avoid flicker */
        }
        default: {
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }
}

/* ---- mgl_window vtable -------------------------------------------------- */

static mgl_window_handle mgl_window_win32_get_system_handle(const mgl_window *self) {
    mgl_window_win32 *impl = self->impl;
    return (mgl_window_handle)impl->window;
}

static void mgl_window_win32_deinit(mgl_window *self) {
    mgl_window_win32 *impl = self->impl;
    if(!impl)
        return;

    mgl_graphics_deinit(&impl->graphics);

    if(mgl_get_context()->current_window == self)
        mgl_get_context()->current_window = NULL;

    if(impl->subclassed && impl->window && IsWindow(impl->window)) {
        SetWindowLongPtrW(impl->window, GWLP_WNDPROC, (LONG_PTR)impl->prev_wnd_proc);
        SetWindowLongPtrW(impl->window, GWLP_USERDATA, 0);
    }

    if(impl->created_window && impl->window && IsWindow(impl->window))
        DestroyWindow(impl->window);

    free(impl);
    self->impl = NULL;
}

static void mgl_window_win32_close(mgl_window *self) {
    mgl_window_win32 *impl = self->impl;
    self->open = false;
    if(impl->window && IsWindow(impl->window)) {
        mgl_event event;
        memset(&event, 0, sizeof(event));
        event.type = MGL_EVENT_CLOSED;
        mgl_window_win32_append_event(impl, &event);
        ShowWindow(impl->window, SW_HIDE);
    }
}

static bool mgl_window_win32_poll_event(mgl_window *self, mgl_event *event) {
    mgl_window_win32 *impl = self->impl;

    if(mgl_window_win32_pop_event(impl, event))
        return true;

    MSG msg;
    while(PeekMessageW(&msg, impl->window, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if(mgl_window_win32_pop_event(impl, event))
            return true;
    }
    return false;
}

static void mgl_window_win32_swap_buffers(mgl_window *self) {
    mgl_window_win32 *impl = self->impl;
    mgl_graphics_swap_buffers(&impl->graphics, (mgl_window_handle)impl->window);
}

static void mgl_window_win32_set_visible(mgl_window *self, bool visible) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window)
        return;
    ShowWindow(impl->window, visible ? SW_SHOW : SW_HIDE);
    if(visible)
        UpdateWindow(impl->window);
}

static bool mgl_window_win32_is_key_pressed(const mgl_window *self, mgl_key key) {
    (void)self;
    const unsigned int vk = mgl_key_to_vk(key);
    if(vk == 0)
        return false;
    return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
}

static bool mgl_window_win32_is_mouse_button_pressed(const mgl_window *self, mgl_mouse_button button) {
    (void)self;
    switch(button) {
        case MGL_BUTTON_LEFT:     return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        case MGL_BUTTON_RIGHT:    return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        case MGL_BUTTON_MIDDLE:   return (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        case MGL_BUTTON_XBUTTON1: return (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
        case MGL_BUTTON_XBUTTON2: return (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
        default:                  return false;
    }
}

static void mgl_window_win32_set_title(mgl_window *self, const char *title) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window)
        return;

    wchar_t title_w[256];
    if(!title || MultiByteToWideChar(CP_UTF8, 0, title, -1, title_w, 256) <= 0)
        title_w[0] = L'\0';
    SetWindowTextW(impl->window, title_w);
}

static void mgl_window_win32_set_cursor_visible(mgl_window *self, bool visible) {
    mgl_window_win32 *impl = self->impl;
    impl->cursor_visible = visible;
    if(!visible)
        SetCursor(NULL);
    else
        SetCursor(impl->arrow_cursor);
    PostMessageW(impl->window, WM_SETCURSOR, 0, 0);
}

static void mgl_window_win32_set_vsync_enabled(mgl_window *self, bool enabled) {
    mgl_window_win32 *impl = self->impl;
    self->vsync_enabled = enabled;
    mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window, enabled);
}

static bool mgl_window_win32_is_vsync_enabled(const mgl_window *self) {
    return self->vsync_enabled;
}

static void mgl_window_win32_set_fullscreen(mgl_window *self, bool fullscreen) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window || fullscreen == impl->is_fullscreen)
        return;

    if(fullscreen) {
        GetWindowRect(impl->window, &impl->saved_rect);
        impl->saved_style = (LONG)GetWindowLongPtrW(impl->window, GWL_STYLE);
        impl->saved_ex_style = (LONG)GetWindowLongPtrW(impl->window, GWL_EXSTYLE);

        HMONITOR hmon = MonitorFromWindow(impl->window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hmon, &mi);

        SetWindowLongPtrW(impl->window, GWL_STYLE, impl->saved_style & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLongPtrW(impl->window, GWL_EXSTYLE, impl->saved_ex_style & ~(WS_DLGFRAME | WS_THICKFRAME));
        SetWindowPos(impl->window, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        impl->is_fullscreen = true;
    } else {
        SetWindowLongPtrW(impl->window, GWL_STYLE, impl->saved_style);
        SetWindowLongPtrW(impl->window, GWL_EXSTYLE, impl->saved_ex_style);
        SetWindowPos(impl->window, HWND_NOTOPMOST,
            impl->saved_rect.left, impl->saved_rect.top,
            impl->saved_rect.right - impl->saved_rect.left, impl->saved_rect.bottom - impl->saved_rect.top,
            SWP_FRAMECHANGED | SWP_NOZORDER);
        impl->is_fullscreen = false;
    }
}

static bool mgl_window_win32_is_fullscreen(const mgl_window *self) {
    mgl_window_win32 *impl = self->impl;
    return impl->is_fullscreen;
}

static void mgl_window_win32_set_position(mgl_window *self, mgl_vec2i position) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window)
        return;
    SetWindowPos(impl->window, NULL, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    mgl_window_win32_on_move(self, position.x, position.y);
}

static void mgl_window_win32_set_size(mgl_window *self, mgl_vec2i size) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window)
        return;

    const LONG style = GetWindowLongPtrW(impl->window, GWL_STYLE);
    const LONG ex_style = GetWindowLongPtrW(impl->window, GWL_EXSTYLE);
    RECT rect = { 0, 0, size.x, size.y };
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);

    SetWindowPos(impl->window, NULL, 0, 0,
        rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void mgl_window_win32_set_size_limits_impl(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum) {
    mgl_window_win32_set_size_limits(self, minimum, maximum);
}

static void mgl_window_win32_set_clipboard(mgl_window *self, const char *str, size_t size) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window || !str)
        return;

    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, str, (int)size, NULL, 0);
    if(wide_len <= 0)
        return;

    wchar_t *wide = malloc((size_t)(wide_len + 1) * sizeof(wchar_t));
    if(!wide)
        return;
    MultiByteToWideChar(CP_UTF8, 0, str, (int)size, wide, wide_len);
    wide[wide_len] = L'\0';

    if(!OpenClipboard(impl->window)) {
        free(wide);
        return;
    }
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wide_len + 1) * sizeof(wchar_t));
    if(mem) {
        void *dst = GlobalLock(mem);
        if(dst) {
            memcpy(dst, wide, (size_t)(wide_len + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
    }
    CloseClipboard();
    free(wide);
}

static bool mgl_window_win32_get_clipboard(mgl_window *self, mgl_clipboard_callback callback, void *userdata, uint32_t clipboard_types) {
    mgl_window_win32 *impl = self->impl;
    if(!impl->window || !(clipboard_types & MGL_CLIPBOARD_TYPE_STRING))
        return false;

    if(!OpenClipboard(impl->window))
        return false;

    bool result = false;
    if(IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if(data) {
            wchar_t *wide = GlobalLock(data);
            if(wide) {
                const SIZE_T wide_bytes = GlobalSize(data);
                int wide_chars = (int)(wide_bytes / sizeof(wchar_t));
                /* Clipboard text is conventionally null-terminated; the null
                   is part of the payload, not the string. */
                if(wide_chars > 0 && wide[wide_chars - 1] == L'\0')
                    --wide_chars;
                const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_chars, NULL, 0, NULL, NULL);
                if(utf8_len > 0) {
                    char *utf8 = malloc((size_t)utf8_len);
                    if(utf8) {
                        WideCharToMultiByte(CP_UTF8, 0, wide, wide_chars, utf8, utf8_len, NULL, NULL);
                        result = callback((const unsigned char*)utf8, (size_t)utf8_len, MGL_CLIPBOARD_TYPE_STRING, userdata);
                        free(utf8);
                    }
                }
                GlobalUnlock(data);
            }
        }
    }
    CloseClipboard();
    return result;
}

static void mgl_window_win32_set_key_repeat_enabled(mgl_window *self, bool enabled) {
    self->key_repeat_enabled = enabled;
}

static void mgl_window_win32_flush(mgl_window *self) {
    (void)self;
    /* Nothing to flush on Win32; windows messages are dispatched eagerly. */
}

static void* mgl_window_win32_get_egl_display(mgl_window *self) {
    (void)self;
    /* The WGL backend has no EGL display. The video preview integration
       (sharing the recorder's GL context) is a later milestone. */
    return NULL;
}

static void* mgl_window_win32_get_egl_context(mgl_window *self) {
    (void)self;
    return NULL;
}

/* ---- init --------------------------------------------------------------- */

static bool mgl_window_win32_setup(mgl_window *self, const char *title, const mgl_window_create_params *params, HWND existing_window) {
    mgl_window_win32 *impl = self->impl;

    self->get_system_handle = mgl_window_win32_get_system_handle;
    self->deinit = mgl_window_win32_deinit;
    self->close = mgl_window_win32_close;
    self->poll_event = mgl_window_win32_poll_event;
    self->inject_x11_event = NULL;
    self->swap_buffers = mgl_window_win32_swap_buffers;
    self->set_visible = mgl_window_win32_set_visible;
    self->is_key_pressed = mgl_window_win32_is_key_pressed;
    self->is_mouse_button_pressed = mgl_window_win32_is_mouse_button_pressed;
    self->set_title = mgl_window_win32_set_title;
    self->set_cursor_visible = mgl_window_win32_set_cursor_visible;
    self->set_vsync_enabled = mgl_window_win32_set_vsync_enabled;
    self->is_vsync_enabled = mgl_window_win32_is_vsync_enabled;
    self->set_fullscreen = mgl_window_win32_set_fullscreen;
    self->is_fullscreen = mgl_window_win32_is_fullscreen;
    self->set_position = mgl_window_win32_set_position;
    self->set_size = mgl_window_win32_set_size;
    self->set_size_limits = mgl_window_win32_set_size_limits_impl;
    self->set_clipboard = mgl_window_win32_set_clipboard;
    self->get_clipboard = mgl_window_win32_get_clipboard;
    self->set_key_repeat_enabled = mgl_window_win32_set_key_repeat_enabled;
    self->flush = mgl_window_win32_flush;
    self->get_egl_display = mgl_window_win32_get_egl_display;
    self->get_egl_context = mgl_window_win32_get_egl_context;
    self->for_each_active_monitor_output = mgl_window_win32_for_each_active_monitor_output;

    mgl_vec2i window_size = params ? params->size : (mgl_vec2i){ 0, 0 };
    if(window_size.x <= 0 || window_size.y <= 0) {
        window_size.x = 640;
        window_size.y = 480;
    }

    const bool hide_decorations = params && params->hide_decorations;
    impl->support_alpha = params && params->support_alpha;

    DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    if(hide_decorations)
        style |= WS_POPUP;
    else
        style |= WS_OVERLAPPEDWINDOW;

    DWORD ex_style = 0;
    if(impl->support_alpha)
        ex_style |= WS_EX_LAYERED;

    mgl_window_type window_type = params ? params->window_type : MGL_WINDOW_TYPE_NORMAL;
    if(window_type == MGL_WINDOW_TYPE_DIALOG || window_type == MGL_WINDOW_TYPE_NOTIFICATION)
        ex_style |= WS_EX_TOPMOST;
    else if(window_type == MGL_WINDOW_TYPE_OVERLAY) {
        /* Borderless, always-on-top window. Real overlay layer behavior
           (per-monitor positioning, click-through) is a later milestone. */
        style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        ex_style |= WS_EX_TOPMOST;
    }

    mgl_win32_register_class();

    if(existing_window) {
        impl->window = existing_window;
        impl->created_window = false;
        impl->subclassed = true;
        impl->prev_wnd_proc = (WNDPROC)SetWindowLongPtrW(existing_window, GWLP_WNDPROC, (LONG_PTR)mgl_window_win32_wnd_proc);
        SetWindowLongPtrW(existing_window, GWLP_USERDATA, (LONG_PTR)self);

        RECT client;
        GetClientRect(existing_window, &client);
        self->size.x = client.right - client.left;
        self->size.y = client.bottom - client.top;
        if(self->size.x <= 0 || self->size.y <= 0)
            self->size = window_size;

        RECT rect;
        GetWindowRect(existing_window, &rect);
        self->pos.x = rect.left;
        self->pos.y = rect.top;
    } else {
        wchar_t title_w[256];
        if(!title || MultiByteToWideChar(CP_UTF8, 0, title, -1, title_w, 256) <= 0)
            title_w[0] = L'\0';

        RECT rect = { 0, 0, window_size.x, window_size.y };
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);

        mgl_vec2i window_pos = params ? params->position : (mgl_vec2i){ 0, 0 };
        HWND parent = params ? (HWND)params->parent_window : NULL;

        impl->window = CreateWindowExW(ex_style, mgl_win32_class_name, title_w, style,
            window_pos.x, window_pos.y,
            rect.right - rect.left, rect.bottom - rect.top,
            parent, NULL, GetModuleHandleW(NULL), self);
        if(!impl->window) {
            fprintf(stderr, "mgl error: mgl_window_win32_setup: CreateWindowExW failed\\n");
            return false;
        }
        impl->created_window = true;
        self->size = window_size;
        self->pos = window_pos;
    }

    mgl_graphics_create_params gparams;
    memset(&gparams, 0, sizeof(gparams));
    gparams.graphics_api = params ? params->graphics_api : MGL_GRAPHICS_API_WGL;
    gparams.alpha = impl->support_alpha;
    gparams.request_depth_buffer = params && params->request_depth_buffer;
    gparams.request_stencil_buffer = params && params->request_stencil_buffer;

    if(!mgl_graphics_init(&impl->graphics, &gparams)) {
        fprintf(stderr, "mgl error: mgl_window_win32_setup: failed to init graphics\\n");
        return false;
    }

    if(!mgl_graphics_make_context_current(&impl->graphics, (mgl_window_handle)impl->window)) {
        fprintf(stderr, "mgl error: mgl_window_win32_setup: failed to make window context current\\n");
        return false;
    }
    impl->graphics_ready = true;

    /* The GL 1.2+ entry points (VBOs, shaders, glBlendFuncSeparate, ...)
       are resolved inside mgl_graphics_make_context_current, right after
       the context is current. */
    self->vsync_enabled = true;
    mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window, self->vsync_enabled);

    if(params && (params->min_size.x || params->min_size.y || params->max_size.x || params->max_size.y))
        mgl_window_win32_set_size_limits(self, params->min_size, params->max_size);

    if(params && params->transient_for_window)
        SetWindowLongPtrW(impl->window, GWLP_HWNDPARENT, (LONG_PTR)params->transient_for_window);

    const bool hidden = params && params->hidden;
    if(!hidden) {
        ShowWindow(impl->window, SW_SHOW);
        UpdateWindow(impl->window);
    }

    mgl_window_win32_refresh_monitors(self);
    mgl_window_win32_update_frame_time_limit_monitor(self);
    mgl_window_win32_on_resize(self, self->size.x, self->size.y);

    self->open = true;
    self->focused = false;
    return true;
}

bool mgl_window_win32_init(mgl_window *self, const char *title, const mgl_window_create_params *params, mgl_window_handle existing_window) {
    mgl_window_win32 *impl = calloc(1, sizeof(mgl_window_win32));
    if(!impl)
        return false;

    self->impl = impl;
    impl->cursor_visible = true;
    impl->arrow_cursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    win32_events_circular_buffer_init(&impl->events);

    if(!mgl_window_win32_setup(self, title, params, (HWND)existing_window)) {
        mgl_window_win32_deinit(self);
        return false;
    }
    mgl_get_context()->current_window = self;
    return true;
}

#endif /* _WIN32 */
