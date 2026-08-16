#include "../../include/mgl/window/wayland.h"
#include "../../include/mgl/window/event.h"
#include "../../include/mgl/window/key.h"
#include "../../include/mgl/window/mouse_button.h"
#include "../../include/mgl/mgl.h"
#include "../../include/mgl/system/utf8.h"
#include "../../include/mgl/system/clock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <poll.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#ifdef MGL_LIBDECOR
#include <libdecor.h>
#endif

static void mgl_window_wayland_deinit(mgl_window *self);

#define MAX_STACKED_EVENTS 32
#define MAX_OFFER_MIMES 16
#define MAX_OFFER_MIME_LEN 128
/* Maximum number of in-flight data offers tracked at once. The server may
   announce a new offer (and even multiple) before sending the selection event
   that promotes one of them. This bound only matters as a safety cap; in
   practice the list holds 0–2 entries. */
#define MAX_PENDING_OFFERS 16

typedef struct {
    mgl_event stack[MAX_STACKED_EVENTS];
    int start;
    int end;
    int size;
} wayland_events_circular_buffer;

typedef struct {
    struct wl_output *output;
    uint32_t wl_name;
    char name[64];
    mgl_vec2i pos;
    mgl_vec2i size;
    int refresh_rate;
    int32_t scale; /* integer wl_output scale factor, 1 if not advertised */
    bool ready; /* true once wl_output::done has fired */
} mgl_wayland_output;

typedef struct {
    struct wl_egl_window *window;
    struct wl_registry *registry;
    struct wl_surface *surface;
    struct wl_compositor *compositor;
    struct wl_shell *shell;
    struct wl_shell_surface *shell_surface;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_shm *shm;

    struct xdg_wm_base *xdg_wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    /* Server-side decorations */
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct zxdg_toplevel_decoration_v1 *toplevel_decoration;

    /* wlr-layer-shell — used when window_type == MGL_WINDOW_TYPE_OVERLAY */
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    bool layer_surface_configured;

    /* wp-viewporter + wp-fractional-scale-v1, used to render at the
       compositor's preferred fractional scale on outputs that aren't an
       integer multiple of 1.0 (e.g. 1.25, 1.5). Only bound on the layer-shell
       path. When viewport is set, we never call wl_surface.set_buffer_scale —
       the viewport's source/destination tells the compositor about the
       buffer↔logical conversion. */
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;

    /* wp-cursor-shape-v1: lets the compositor render the cursor using the
       user's system theme. Preferred over wl_cursor_theme_load (which falls
       back to "default"/Adwaita when XCURSOR_THEME isn't exported, ignoring
       theme settings done via the compositor or DE-specific mechanisms). */
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;

    /* Surface scale in 120ths (so 120 == 1.0×, 180 == 1.5×, 240 == 2.0×).
       When greater than 120, on_resize converts logical → buffer pixels using
       this ratio, and pointer-motion events are converted similarly so
       self->size, cursor_position, and event coordinates are all in the same
       buffer-pixel coordinate system. The default is 120 (no scaling),
       maintained for the xdg-shell paths. */
    int32_t scale_120;
    /* True when |scale_120| was set from a wp_fractional_scale_v1.preferred_scale
       event (so we use wp_viewport.set_destination instead of
       wl_surface.set_buffer_scale). */
    bool scale_via_viewport;
    /* The most recent logical (surface-coordinate) size. Tracked separately
       from self->size (which is buffer pixels) so we can react cleanly to
       independent configure / preferred_scale events without losing one or the
       other. */
    mgl_vec2i logical_size;
    /* Per-surface viewport for the cursor (when wp_viewporter is bound). Lets
       us render the cursor at fractional scales by loading a larger-pixel
       cursor theme and declaring the logical destination size to be 24×24. */
    struct wp_viewport *cursor_viewport;
    /* Pixel size of the cursor theme currently loaded into impl->cursor_theme.
       Reload happens lazily in update_cursor when this drifts from the desired
       scaled size. */
    int32_t cursor_theme_pixel_size;

    /* XKB keyboard state */
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    xkb_mod_index_t xkb_mod_shift;
    xkb_mod_index_t xkb_mod_ctrl;
    xkb_mod_index_t xkb_mod_alt;
    xkb_mod_index_t xkb_mod_logo;

    /* Tracked input state */
    bool keys_pressed[__MGL_NUM_KEYS__];
    uint32_t mouse_buttons_mask;

    /* Modifiers (updated from xkb state) */
    bool mod_shift;
    bool mod_ctrl;
    bool mod_alt;
    bool mod_super;

    /* Cursor */
    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    uint32_t pointer_enter_serial;
    bool cursor_visible;

    /* Pointer frame state */
    bool mouse_moved;
    mgl_vec2i mouse_position;
    bool frame_has_button;
    mgl_event frame_button_event;
    bool frame_has_scroll;
    int frame_scroll_delta;

    /* Last input serial (for clipboard set_selection) */
    uint32_t last_input_serial;

    /* Key repeat */
    bool key_repeat_active;
    uint32_t key_repeat_keycode;
    xkb_keysym_t key_repeat_sym;
    double key_repeat_delay_s;
    double key_repeat_interval_s;
    bool key_repeat_first_fired;
    mgl_clock key_repeat_clock;
    mgl_clock key_repeat_interval_clock;

    /* Monitors */
    mgl_wayland_output outputs[MGL_MAX_MONITORS];
    int num_outputs;
    /* Outputs the surface is currently on */
    int surface_output_indices[MGL_MAX_MONITORS];
    int num_surface_outputs;

    /* Window state */
    bool is_fullscreen;

#ifdef MGL_LIBDECOR
    struct libdecor *libdecor_context;
    struct libdecor_frame *libdecor_frame;
    bool using_libdecor;
#endif

    /* Event buffer */
    wayland_events_circular_buffer events;

    mgl_graphics graphics;
} mgl_window_wayland;

/* Persistent clipboard / wl_data_device state, shared across all windows for
   the lifetime of the wl_display.

   Why persistent: the wl_data_device cannot be safely destroyed and recreated
   on a long-lived wl_display. When it's destroyed client-side it becomes a
   zombie in libwayland's object map; any events the server has queued for it
   (data_offer events advertising new selection ids) are then silently consumed
   without reserving the server-allocated id in the client map. The server's
   id allocator keeps incrementing regardless, and the next data_offer event
   delivered to a *new* (alive) wl_data_device fails wl_map_reserve_new with
   "not a valid new object id" because the client's server_entries count has
   fallen behind. Keeping a single wl_data_device alive for the whole display
   lifetime keeps the two id-spaces synchronized.

   Listeners on g_clipboard.device / data_source / data_offers use
   &g_clipboard as their data pointer, so events stay safe across window
   open/close cycles. */
typedef struct {
    struct wl_data_device_manager *manager;
    struct wl_data_device *device;
    struct wl_data_source *data_source;
    char *clipboard_data;
    size_t clipboard_data_size;
    struct {
        struct wl_data_offer *offer;
        char mimes[MAX_OFFER_MIMES][MAX_OFFER_MIME_LEN];
        int num_mimes;
    } pending_offers[MAX_PENDING_OFFERS];
    int num_pending_offers;
    struct wl_data_offer *current_selection_offer;
    char current_selection_mimes[MAX_OFFER_MIMES][MAX_OFFER_MIME_LEN];
    int num_current_selection_mimes;
} mgl_wayland_clipboard_state;

static mgl_wayland_clipboard_state g_clipboard;

static void wayland_events_circular_buffer_init(wayland_events_circular_buffer *self) {
    self->start = 0;
    self->end = 0;
    self->size = 0;
}

static bool wayland_events_circular_buffer_append(wayland_events_circular_buffer *self, const mgl_event *event) {
    if(self->size == MAX_STACKED_EVENTS)
        return false;
    self->stack[self->end] = *event;
    self->end = (self->end + 1) % MAX_STACKED_EVENTS;
    ++self->size;
    return true;
}

static bool wayland_events_circular_buffer_pop(wayland_events_circular_buffer *self, mgl_event *event) {
    if(self->size == 0)
        return false;
    *event = self->stack[self->start];
    self->start = (self->start + 1) % MAX_STACKED_EVENTS;
    --self->size;
    return true;
}

static bool mgl_window_wayland_append_event(mgl_window_wayland *impl, const mgl_event *event) {
    return wayland_events_circular_buffer_append(&impl->events, event);
}

static bool mgl_window_wayland_pop_event(mgl_window_wayland *impl, mgl_event *event) {
    return wayland_events_circular_buffer_pop(&impl->events, event);
}

static mgl_key xkb_keysym_to_mgl_key(xkb_keysym_t sym) {
    if(sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
        return MGL_KEY_A + (sym - XKB_KEY_A);
    if(sym >= XKB_KEY_a && sym <= XKB_KEY_z)
        return MGL_KEY_A + (sym - XKB_KEY_a);
    if(sym >= XKB_KEY_0 && sym <= XKB_KEY_9)
        return MGL_KEY_NUM0 + (sym - XKB_KEY_0);
    if(sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return MGL_KEY_NUMPAD0 + (sym - XKB_KEY_KP_0);

    switch(sym) {
        case XKB_KEY_space:                return MGL_KEY_SPACE;
        case XKB_KEY_BackSpace:            return MGL_KEY_BACKSPACE;
        case XKB_KEY_Tab:                  return MGL_KEY_TAB;
        case XKB_KEY_Return:               return MGL_KEY_ENTER;
        case XKB_KEY_Escape:               return MGL_KEY_ESCAPE;
        case XKB_KEY_Control_L:            return MGL_KEY_LCONTROL;
        case XKB_KEY_Shift_L:              return MGL_KEY_LSHIFT;
        case XKB_KEY_Alt_L:                return MGL_KEY_LALT;
        case XKB_KEY_Super_L:              return MGL_KEY_LSYSTEM;
        case XKB_KEY_Control_R:            return MGL_KEY_RCONTROL;
        case XKB_KEY_Shift_R:              return MGL_KEY_RSHIFT;
        case XKB_KEY_Alt_R:                return MGL_KEY_RALT;
        case XKB_KEY_Super_R:              return MGL_KEY_RSYSTEM;
        case XKB_KEY_Delete:               return MGL_KEY_DELETE;
        case XKB_KEY_Home:                 return MGL_KEY_HOME;
        case XKB_KEY_Left:                 return MGL_KEY_LEFT;
        case XKB_KEY_Up:                   return MGL_KEY_UP;
        case XKB_KEY_Right:                return MGL_KEY_RIGHT;
        case XKB_KEY_Down:                 return MGL_KEY_DOWN;
        case XKB_KEY_Page_Up:              return MGL_KEY_PAGEUP;
        case XKB_KEY_Page_Down:            return MGL_KEY_PAGEDOWN;
        case XKB_KEY_End:                  return MGL_KEY_END;
        case XKB_KEY_F1:                   return MGL_KEY_F1;
        case XKB_KEY_F2:                   return MGL_KEY_F2;
        case XKB_KEY_F3:                   return MGL_KEY_F3;
        case XKB_KEY_F4:                   return MGL_KEY_F4;
        case XKB_KEY_F5:                   return MGL_KEY_F5;
        case XKB_KEY_F6:                   return MGL_KEY_F6;
        case XKB_KEY_F7:                   return MGL_KEY_F7;
        case XKB_KEY_F8:                   return MGL_KEY_F8;
        case XKB_KEY_F9:                   return MGL_KEY_F9;
        case XKB_KEY_F10:                  return MGL_KEY_F10;
        case XKB_KEY_F11:                  return MGL_KEY_F11;
        case XKB_KEY_F12:                  return MGL_KEY_F12;
        case XKB_KEY_F13:                  return MGL_KEY_F13;
        case XKB_KEY_F14:                  return MGL_KEY_F14;
        case XKB_KEY_F15:                  return MGL_KEY_F15;
        case XKB_KEY_F16:                  return MGL_KEY_F16;
        case XKB_KEY_F17:                  return MGL_KEY_F17;
        case XKB_KEY_F18:                  return MGL_KEY_F18;
        case XKB_KEY_F19:                  return MGL_KEY_F19;
        case XKB_KEY_F20:                  return MGL_KEY_F20;
        case XKB_KEY_F21:                  return MGL_KEY_F21;
        case XKB_KEY_F22:                  return MGL_KEY_F22;
        case XKB_KEY_F23:                  return MGL_KEY_F23;
        case XKB_KEY_F24:                  return MGL_KEY_F24;
        case XKB_KEY_Insert:               return MGL_KEY_INSERT;
        case XKB_KEY_Pause:                return MGL_KEY_PAUSE;
        case XKB_KEY_Print:                return MGL_KEY_PRINTSCREEN;
        case XKB_KEY_KP_Insert:            return MGL_KEY_NUMPAD0;
        case XKB_KEY_KP_End:               return MGL_KEY_NUMPAD1;
        case XKB_KEY_KP_Down:              return MGL_KEY_NUMPAD2;
        case XKB_KEY_KP_Page_Down:         return MGL_KEY_NUMPAD3;
        case XKB_KEY_KP_Left:              return MGL_KEY_NUMPAD4;
        case XKB_KEY_KP_Begin:             return MGL_KEY_NUMPAD5;
        case XKB_KEY_KP_Right:             return MGL_KEY_NUMPAD6;
        case XKB_KEY_KP_Home:              return MGL_KEY_NUMPAD7;
        case XKB_KEY_KP_Up:                return MGL_KEY_NUMPAD8;
        case XKB_KEY_KP_Page_Up:           return MGL_KEY_NUMPAD9;
        case XKB_KEY_KP_Enter:             return MGL_KEY_NUMPAD_ENTER;
        case XKB_KEY_XF86AudioLowerVolume: return MGL_KEY_AUDIO_LOWER_VOLUME;
        case XKB_KEY_XF86AudioRaiseVolume: return MGL_KEY_AUDIO_RAISE_VOLUME;
        case XKB_KEY_XF86AudioPlay:        return MGL_KEY_AUDIO_PLAY;
        case XKB_KEY_XF86AudioStop:        return MGL_KEY_AUDIO_STOP;
        case XKB_KEY_XF86AudioPause:       return MGL_KEY_AUDIO_PAUSE;
        case XKB_KEY_XF86AudioMute:        return MGL_KEY_AUDIO_MUTE;
        case XKB_KEY_XF86AudioPrev:        return MGL_KEY_AUDIO_PREV;
        case XKB_KEY_XF86AudioNext:        return MGL_KEY_AUDIO_NEXT;
        case XKB_KEY_XF86AudioRewind:      return MGL_KEY_AUDIO_REWIND;
        case XKB_KEY_XF86AudioForward:     return MGL_KEY_AUDIO_FORWARD;
        case XKB_KEY_dead_acute:           return MGL_KEY_DEAD_ACUTE;
        case XKB_KEY_apostrophe:           return MGL_KEY_APOSTROPHE;
        case XKB_KEY_exclam:               return MGL_KEY_EXCLAM;
        case XKB_KEY_quotedbl:             return MGL_KEY_QUOTEDBL;
        case XKB_KEY_numbersign:           return MGL_KEY_NUMBERSIGN;
        case XKB_KEY_dollar:               return MGL_KEY_DOLLAR;
        case XKB_KEY_percent:              return MGL_KEY_PERCENT;
        case XKB_KEY_ampersand:            return MGL_KEY_AMPERSAND;
        case XKB_KEY_parenleft:            return MGL_KEY_PARENLEFT;
        case XKB_KEY_parenright:           return MGL_KEY_PARENRIGHT;
        case XKB_KEY_asterisk:             return MGL_KEY_ASTERISK;
        case XKB_KEY_plus:                 return MGL_KEY_PLUS;
        case XKB_KEY_comma:                return MGL_KEY_COMMA;
        case XKB_KEY_minus:                return MGL_KEY_MINUS;
        case XKB_KEY_period:               return MGL_KEY_PERIOD;
        case XKB_KEY_slash:                return MGL_KEY_SLASH;
        case XKB_KEY_colon:                return MGL_KEY_COLON;
        case XKB_KEY_semicolon:            return MGL_KEY_SEMICOLON;
        case XKB_KEY_less:                 return MGL_KEY_LESS;
        case XKB_KEY_equal:                return MGL_KEY_EQUAL;
        case XKB_KEY_greater:              return MGL_KEY_GREATER;
        case XKB_KEY_question:             return MGL_KEY_QUESTION;
        case XKB_KEY_bracketleft:          return MGL_KEY_BRACKETLEFT;
        case XKB_KEY_backslash:            return MGL_KEY_BACKSLASH;
        case XKB_KEY_bracketright:         return MGL_KEY_BRACKETRIGHT;
        case XKB_KEY_asciicircum:          return MGL_KEY_ASCIICIRCUM;
        case XKB_KEY_underscore:           return MGL_KEY_UNDERSCORE;
        case XKB_KEY_grave:                return MGL_KEY_GRAVE;
    }
    return MGL_KEY_UNKNOWN;
}


static mgl_key_states mgl_window_wayland_get_key_states(const mgl_window_wayland *impl) {
    return (mgl_key_states){
        .shift   = impl->mod_shift,
        .control = impl->mod_ctrl,
        .alt     = impl->mod_alt,
        .system  = impl->mod_super,
    };
}

static void mgl_window_wayland_on_resize(mgl_window *self, int width, int height) {
    mgl_window_wayland *impl = self->impl;
    if(width <= 0 || height <= 0)
        return;

    /* Compositor configures are in surface-coordinate (logical) units. Record
       them as such, then convert to buffer pixels using scale_120 (120ths of
       a scale; 120 == 1.0×). The conversion uses ceiling division so a 1.5×
       scale on a 1080-px-high output still ends up with the full target buffer.
       With scale_120 == 120 this is a no-op (xdg-toplevel default). */
    impl->logical_size.x = width;
    impl->logical_size.y = height;
    const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
    width  = (width  * s120 + 119) / 120;
    height = (height * s120 + 119) / 120;

    /* Keep the viewport destination in sync with the new logical size whenever
       configure delivers fresh dimensions. */
    if(impl->viewport && impl->scale_via_viewport)
        wp_viewport_set_destination(impl->viewport, impl->logical_size.x, impl->logical_size.y);

    const bool size_changed = (width != self->size.x || height != self->size.y);
    self->size.x = width;
    self->size.y = height;
    /* The egl window is created after the first configure on the layer-shell path,
       so |impl->window| can legitimately be NULL during initial setup. */
    if(impl->window)
        wl_egl_window_resize(impl->window, self->size.x, self->size.y, 0, 0);

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });

    if(size_changed) {
        mgl_event ev;
        ev.type = MGL_EVENT_RESIZED;
        ev.size.width = self->size.x;
        ev.size.height = self->size.y;
        mgl_window_wayland_append_event(impl, &ev);
    }
}

static void mgl_window_wayland_set_frame_time_limit_monitor(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    int max_rate = 0;

    for(int i = 0; i < impl->num_surface_outputs; ++i) {
        int idx = impl->surface_output_indices[i];
        if(idx >= 0 && idx < impl->num_outputs) {
            int rate = impl->outputs[idx].refresh_rate;
            if(rate > max_rate)
                max_rate = rate;
        }
    }

    if(max_rate == 0 && self->num_monitors > 0)
        max_rate = self->monitors[0].refresh_rate;
    if(max_rate == 0)
        max_rate = 60;

    self->frame_time_limit_monitor = 1.0 / (double)max_rate;
}

static void mgl_wayland_sync_outputs_to_monitors(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;

    for(int i = 0; i < self->num_monitors; ++i) {
        if(self->monitors[i].name) {
            free((char*)self->monitors[i].name);
            self->monitors[i].name = NULL;
        }
    }
    self->num_monitors = 0;

    for(int i = 0; i < impl->num_outputs; ++i) {
        mgl_wayland_output *out = &impl->outputs[i];
        if(!out->ready || out->size.x == 0 || out->size.y == 0)
            continue;
        if(self->num_monitors >= MGL_MAX_MONITORS)
            break;

        mgl_monitor *mon = &self->monitors[self->num_monitors];
        mon->id = (int)out->wl_name;
        mon->crtc_id = (int)out->wl_name;
        mon->name = strdup(out->name[0] ? out->name : "output");
        if(!mon->name)
            continue;
        mon->pos = out->pos;
        mon->size = out->size;
        mon->refresh_rate = out->refresh_rate;
        self->num_monitors++;
    }
}

static void wl_output_geometry(void *data, struct wl_output *wl_output,
                                int32_t x, int32_t y,
                                int32_t physical_width, int32_t physical_height,
                                int32_t subpixel, const char *make,
                                const char *model, int32_t transform) {
    mgl_wayland_output *out = data;
    (void)wl_output; (void)physical_width; (void)physical_height;
    (void)subpixel; (void)transform;
    out->pos = (mgl_vec2i){ .x = x, .y = y };
    /* Use make+model as a fallback name if not overridden by v4 name event */
    if(!out->name[0] && make && model)
        snprintf(out->name, sizeof(out->name), "%s %s", make, model);
}

static void wl_output_mode(void *data, struct wl_output *wl_output,
                            uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    mgl_wayland_output *out = data;
    (void)wl_output;
    if(flags & WL_OUTPUT_MODE_CURRENT) {
        out->size = (mgl_vec2i){ .x = width, .y = height };
        out->refresh_rate = (refresh > 0) ? (refresh / 1000) : 0;
    }
}

static void wl_output_done(void *data, struct wl_output *wl_output) {
    mgl_wayland_output *out = data;
    (void)wl_output;
    out->ready = true;
}

static void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    mgl_wayland_output *out = data;
    (void)wl_output;
    out->scale = (factor > 0) ? factor : 1;
}

static void wl_output_name(void *data, struct wl_output *wl_output, const char *name) {
    mgl_wayland_output *out = data;
    (void)wl_output;
    if(name)
        snprintf(out->name, sizeof(out->name), "%s", name);
}

static void wl_output_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data; (void)wl_output; (void)description;
}

static const struct wl_output_listener wl_output_listener = {
    .geometry    = wl_output_geometry,
    .mode        = wl_output_mode,
    .done        = wl_output_done,
    .scale       = wl_output_scale,
    .name        = wl_output_name,
    .description = wl_output_description,
};

static int mgl_wayland_find_output_index(mgl_window_wayland *impl, struct wl_output *output) {
    for(int i = 0; i < impl->num_outputs; ++i) {
        if(impl->outputs[i].output == output)
            return i;
    }
    return -1;
}

/* For the legacy integer wl_output.scale path (fractional-scale protocol not
   advertised): if the surface enters an output whose integer scale differs
   from our current buffer scale, re-apply the larger scale so the surface
   stays sharp after being moved between monitors. */
static void mgl_wayland_maybe_update_integer_scale(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    /* The fractional-scale listener handles its own updates; skip here. */
    if(impl->scale_via_viewport)
        return;
    /* No layer surface (regular xdg-toplevel) — don't touch buffer scale. */
    if(!impl->layer_surface || !impl->surface)
        return;

    int32_t want_scale = 1;
    for(int i = 0; i < impl->num_surface_outputs; ++i) {
        const int oi = impl->surface_output_indices[i];
        if(oi >= 0 && oi < impl->num_outputs && impl->outputs[oi].scale > want_scale)
            want_scale = impl->outputs[oi].scale;
    }
    const int32_t want_120 = want_scale * 120;
    if(want_120 == impl->scale_120 || impl->logical_size.x <= 0 || impl->logical_size.y <= 0)
        return;

    impl->scale_120 = want_120;
    wl_surface_set_buffer_scale(impl->surface, want_scale);

    const int32_t new_buf_w = (impl->logical_size.x * want_120 + 119) / 120;
    const int32_t new_buf_h = (impl->logical_size.y * want_120 + 119) / 120;
    self->size.x = new_buf_w;
    self->size.y = new_buf_h;
    if(impl->window)
        wl_egl_window_resize(impl->window, new_buf_w, new_buf_h, 0, 0);

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });
}

static void wl_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)surface;

    const int idx = mgl_wayland_find_output_index(impl, output);
    if(idx < 0)
        return;

    for(int i = 0; i < impl->num_surface_outputs; ++i) {
        if(impl->surface_output_indices[i] == idx)
            return;
    }
    if(impl->num_surface_outputs < MGL_MAX_MONITORS) {
        impl->surface_output_indices[impl->num_surface_outputs++] = idx;
        mgl_window_wayland_set_frame_time_limit_monitor(self);
        mgl_wayland_maybe_update_integer_scale(self);
    }
}

static void wl_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)surface;

    const int idx = mgl_wayland_find_output_index(impl, output);
    if(idx < 0)
        return;

    for(int i = 0; i < impl->num_surface_outputs; ++i) {
        if(impl->surface_output_indices[i] == idx) {
            impl->surface_output_indices[i] = impl->surface_output_indices[--impl->num_surface_outputs];
            mgl_window_wayland_set_frame_time_limit_monitor(self);
            return;
        }
    }
}

static const struct wl_surface_listener wl_surface_listener = {
    .enter = wl_surface_enter,
    .leave = wl_surface_leave,
};

#define MGL_CURSOR_LOGICAL_SIZE 24

static void mgl_wayland_update_cursor(mgl_window_wayland *impl) {
    if(!impl->pointer)
        return;

    if(!impl->cursor_visible) {
        wl_pointer_set_cursor(impl->pointer, impl->pointer_enter_serial, NULL, 0, 0);
        return;
    }

    /* Prefer wp-cursor-shape-v1 when the compositor advertises it. The
       compositor renders the cursor using whatever theme the user actually
       has configured (via the DE, hyprcursor, XCURSOR_THEME, etc.). This
       avoids the wl_cursor_theme_load fallback which only sees XCURSOR_THEME
       — when that env var isn't exported (common on Hyprland / KDE Wayland
       sessions that set the theme differently) it silently falls back to
       Adwaita. */
    if(impl->cursor_shape_manager) {
        struct wp_cursor_shape_device_v1 *device =
            wp_cursor_shape_manager_v1_get_pointer(impl->cursor_shape_manager, impl->pointer);
        if(device) {
            wp_cursor_shape_device_v1_set_shape(device, impl->pointer_enter_serial,
                WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
            wp_cursor_shape_device_v1_destroy(device);
            return;
        }
    }

    if(!impl->cursor_surface || !impl->shm)
        return;

    /* Compute the pixel size we want for the cursor at the current surface scale.
       scale_120 is in 120ths (so 120 == 1.0×, 180 == 1.5×, 240 == 2.0×). Use
       ceiling so a 1.5× scale doesn't round down to 36→36 instead of 36. */
    const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
    int32_t want_pixel = (MGL_CURSOR_LOGICAL_SIZE * s120 + 119) / 120;
    if(want_pixel < MGL_CURSOR_LOGICAL_SIZE)
        want_pixel = MGL_CURSOR_LOGICAL_SIZE;

    /* Reload the cursor theme at the desired pixel size if it doesn't match. */
    if(!impl->cursor_theme || impl->cursor_theme_pixel_size != want_pixel) {
        if(impl->cursor_theme) {
            wl_cursor_theme_destroy(impl->cursor_theme);
            impl->cursor_theme = NULL;
        }
        impl->cursor_theme = wl_cursor_theme_load(NULL, want_pixel, impl->shm);
        impl->cursor_theme_pixel_size = want_pixel;
    }
    if(!impl->cursor_theme)
        return;

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(impl->cursor_theme, "left_ptr");
    if(!cursor || cursor->image_count == 0)
        cursor = wl_cursor_theme_get_cursor(impl->cursor_theme, "default");
    if(!cursor || cursor->image_count == 0)
        return;

    struct wl_cursor_image *image = cursor->images[0];
    struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
    if(!buffer)
        return;

    int32_t hotspot_x = (int32_t)image->hotspot_x;
    int32_t hotspot_y = (int32_t)image->hotspot_y;
    const bool is_integer_scale = (s120 % 120) == 0;
    if(is_integer_scale && s120 > 120) {
        const int int_scale = s120 / 120;
        /* If we previously set a viewport on the cursor (e.g. came from a
           fractional scale), destroy it — viewport overrides set_buffer_scale. */
        if(impl->cursor_viewport) {
            wp_viewport_destroy(impl->cursor_viewport);
            impl->cursor_viewport = NULL;
        }
        wl_surface_set_buffer_scale(impl->cursor_surface, int_scale);
        hotspot_x = (int32_t)image->hotspot_x / int_scale;
        hotspot_y = (int32_t)image->hotspot_y / int_scale;
    } else if(!is_integer_scale && impl->viewporter) {
        if(!impl->cursor_viewport)
            impl->cursor_viewport = wp_viewporter_get_viewport(impl->viewporter, impl->cursor_surface);
        if(impl->cursor_viewport) {
            wp_viewport_set_source(impl->cursor_viewport,
                wl_fixed_from_int(0), wl_fixed_from_int(0),
                wl_fixed_from_int(image->width), wl_fixed_from_int(image->height));
            /* Logical destination size: image dimensions scaled back to ~24 logical px. */
            const int32_t dst_w = (image->width  * 120 + s120 / 2) / s120;
            const int32_t dst_h = (image->height * 120 + s120 / 2) / s120;
            wp_viewport_set_destination(impl->cursor_viewport,
                dst_w > 0 ? dst_w : 1,
                dst_h > 0 ? dst_h : 1);
            /* Hotspot is in surface (logical) coords. */
            hotspot_x = ((int32_t)image->hotspot_x * 120 + s120 / 2) / s120;
            hotspot_y = ((int32_t)image->hotspot_y * 120 + s120 / 2) / s120;
        }
    } else {
        /* scale_120 == 120 (no scaling). Make sure nothing is stale. */
        if(impl->cursor_viewport) {
            wp_viewport_destroy(impl->cursor_viewport);
            impl->cursor_viewport = NULL;
        }
        wl_surface_set_buffer_scale(impl->cursor_surface, 1);
    }

    wl_surface_attach(impl->cursor_surface, buffer, 0, 0);
    wl_surface_damage(impl->cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(impl->cursor_surface);

    wl_pointer_set_cursor(impl->pointer, impl->pointer_enter_serial,
                          impl->cursor_surface,
                          hotspot_x, hotspot_y);
}

static void data_offer_offer(void *data, struct wl_data_offer *offer, const char *mime_type) {
    mgl_wayland_clipboard_state *cb = data;
    /* Find the matching pending offer and append the MIME type to it. */
    for(int i = 0; i < cb->num_pending_offers; ++i) {
        if(cb->pending_offers[i].offer != offer)
            continue;
        if(cb->pending_offers[i].num_mimes < MAX_OFFER_MIMES) {
            strncpy(cb->pending_offers[i].mimes[cb->pending_offers[i].num_mimes], mime_type, MAX_OFFER_MIME_LEN - 1);
            cb->pending_offers[i].mimes[cb->pending_offers[i].num_mimes][MAX_OFFER_MIME_LEN - 1] = '\0';
            cb->pending_offers[i].num_mimes++;
        }
        return;
    }
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_offer,
};

static void data_source_send(void *data, struct wl_data_source *source, const char *mime_type, int32_t fd) {
    mgl_wayland_clipboard_state *cb = data;
    (void)source; (void)mime_type;
    if(cb->clipboard_data && cb->clipboard_data_size > 0) {
        size_t written = 0;
        while(written < cb->clipboard_data_size) {
            ssize_t n = write(fd, cb->clipboard_data + written, cb->clipboard_data_size - written);
            if(n <= 0)
                break;
            written += (size_t)n;
        }
    }
    close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
    mgl_wayland_clipboard_state *cb = data;
    if(cb->data_source && cb->data_source == source) {
        wl_data_source_destroy(cb->data_source);
        cb->data_source = NULL;
    }
}

static void data_source_target(void *data, struct wl_data_source *source, const char *mime_type) {
    (void)data; (void)source; (void)mime_type;
}

static void data_source_dnd_drop_performed(void *data, struct wl_data_source *source) {
    (void)data; (void)source;
}

static void data_source_dnd_finished(void *data, struct wl_data_source *source) {
    (void)data; (void)source;
}

static void data_source_action(void *data, struct wl_data_source *source, uint32_t dnd_action) {
    (void)data; (void)source; (void)dnd_action;
}

static const struct wl_data_source_listener data_source_listener = {
    .target             = data_source_target,
    .send               = data_source_send,
    .cancelled          = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished       = data_source_dnd_finished,
    .action             = data_source_action,
};

static void data_device_data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
    mgl_wayland_clipboard_state *cb = data;
    (void)device;
    if(cb->num_pending_offers >= MAX_PENDING_OFFERS) {
        /* Safety cap: drop the oldest entry. Use wl_proxy_destroy directly so
           we don't send a destroy request the server may consider stale (see
           data_device_selection for the full reasoning). */
        if(cb->pending_offers[0].offer)
            wl_proxy_destroy((struct wl_proxy*)cb->pending_offers[0].offer);
        memmove(&cb->pending_offers[0], &cb->pending_offers[1],
                sizeof(cb->pending_offers[0]) * (MAX_PENDING_OFFERS - 1));
        cb->num_pending_offers = MAX_PENDING_OFFERS - 1;
    }
    const int slot = cb->num_pending_offers++;
    cb->pending_offers[slot].offer = offer;
    cb->pending_offers[slot].num_mimes = 0;
    wl_data_offer_add_listener(offer, &data_offer_listener, cb);
}

static void data_device_enter(void *data, struct wl_data_device *device, uint32_t serial,
                               struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
                               struct wl_data_offer *offer) {
    (void)data; (void)device; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}

static void data_device_leave(void *data, struct wl_data_device *device) {
    (void)data; (void)device;
}

static void data_device_motion(void *data, struct wl_data_device *device,
                                uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)device; (void)time; (void)x; (void)y;
}

static void data_device_drop(void *data, struct wl_data_device *device) {
    (void)data; (void)device;
}

static void data_device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
    mgl_wayland_clipboard_state *cb = data;
    (void)device;

    /* Per the wl_data_device.selection spec:
         "The data_offer is valid until a new data_offer or NULL is received
          or until the client loses keyboard focus."
       So when this event arrives the PREVIOUS current_selection_offer is no
       longer valid on the server. We must release the client-side proxy so
       libwayland's id-map slot is recyclable for future server-allocated
       data_offer ids (which the server can immediately reuse).
       Use wl_proxy_destroy directly instead of wl_data_offer_destroy:
       wl_data_offer_destroy marshals a destroy request to the server, but
       since the server already considers the offer invalidated, that request
       either targets a no-longer-existing id or worse, races with the server
       reusing the same id for the new data_offer event. wl_proxy_destroy
       frees only the client-side state. */
    if(cb->current_selection_offer) {
        wl_proxy_destroy((struct wl_proxy*)cb->current_selection_offer);
        cb->current_selection_offer = NULL;
        cb->num_current_selection_mimes = 0;
    }

    int chosen = -1;
    if(offer) {
        for(int i = 0; i < cb->num_pending_offers; ++i) {
            if(cb->pending_offers[i].offer == offer) {
                chosen = i;
                break;
            }
        }
    }

    if(chosen >= 0) {
        /* Promote the chosen pending offer to current_selection. */
        cb->current_selection_offer = cb->pending_offers[chosen].offer;
        memcpy(cb->current_selection_mimes, cb->pending_offers[chosen].mimes,
               sizeof(cb->current_selection_mimes));
        cb->num_current_selection_mimes = cb->pending_offers[chosen].num_mimes;
    }

    /* Release every non-chosen pending offer client-side so libwayland's
       id-map slots are recyclable. Same reasoning as above. */
    for(int i = 0; i < cb->num_pending_offers; ++i) {
        if(i == chosen)
            continue;
        if(cb->pending_offers[i].offer)
            wl_proxy_destroy((struct wl_proxy*)cb->pending_offers[i].offer);
    }
    cb->num_pending_offers = 0;
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_data_offer,
    .enter      = data_device_enter,
    .leave      = data_device_leave,
    .motion     = data_device_motion,
    .drop       = data_device_drop,
    .selection  = data_device_selection,
};

static void xdg_toplevel_decoration_configure(void *data,
                                               struct zxdg_toplevel_decoration_v1 *decoration,
                                               uint32_t mode) {
    (void)data; (void)decoration;
    (void)mode; /* compositor has chosen a decoration mode; we accept it */
}

static const struct zxdg_toplevel_decoration_v1_listener xdg_toplevel_decoration_listener = {
    .configure = xdg_toplevel_decoration_configure,
};

static void shell_surface_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial) {
    (void)data;
    wl_shell_surface_pong(shell_surface, serial);
}

static void shell_surface_configure(void *data, struct wl_shell_surface *shell_surface,
                                     uint32_t edges, int32_t width, int32_t height) {
    mgl_window *self = data;
    (void)shell_surface; (void)edges;
    mgl_window_wayland_on_resize(self, width, height);
}

static void shell_surface_popup_done(void *data, struct wl_shell_surface *shell_surface) {
    (void)data; (void)shell_surface;
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    .ping       = shell_surface_ping,
    .configure  = shell_surface_configure,
    .popup_done = shell_surface_popup_done,
};

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                      uint32_t serial, uint32_t width, uint32_t height) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    impl->layer_surface_configured = true;

    if(width > 0 && height > 0)
        mgl_window_wayland_on_resize(self, (int32_t)width, (int32_t)height);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    mgl_window *self = data;
    (void)surface;
    self->open = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed    = layer_surface_closed,
};

/* wp_fractional_scale_v1 listener: the compositor tells us the preferred scale
   in 120ths (so scale=180 means 1.5×). When this fires we:
   - update impl->scale_120;
   - resize the wl_egl_window to match the new buffer dimensions;
   - update self->size, the view, and scissor so OpenGL draws at the right res.
   The viewport's destination is the LOGICAL size (in surface coords), which we
   keep as self->size / (scale_120 / 120). */
static void fractional_scale_preferred(void *data,
                                         struct wp_fractional_scale_v1 *fractional_scale,
                                         uint32_t scale) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)fractional_scale;
    if(scale == 0)
        return;

    impl->scale_120 = (int32_t)scale;
    impl->scale_via_viewport = true;

    /* If we haven't received an initial configure yet, logical_size is still
       (0, 0) — fall back to recovering from current buffer with whatever
       scale we had before. The next configure will overwrite logical_size
       cleanly. */
    int32_t logical_w = impl->logical_size.x;
    int32_t logical_h = impl->logical_size.y;
    if(logical_w <= 0 || logical_h <= 0) {
        logical_w = self->size.x;
        logical_h = self->size.y;
        impl->logical_size.x = logical_w;
        impl->logical_size.y = logical_h;
    }

    /* Buffer = logical * new_scale / 120, with ceil so we never truncate. */
    const int32_t new_buf_w = (logical_w * (int32_t)scale + 119) / 120;
    const int32_t new_buf_h = (logical_h * (int32_t)scale + 119) / 120;
    self->size.x = new_buf_w;
    self->size.y = new_buf_h;
    if(impl->window)
        wl_egl_window_resize(impl->window, new_buf_w, new_buf_h, 0, 0);

    if(impl->viewport)
        wp_viewport_set_destination(impl->viewport, logical_w, logical_h);

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });

    /* Re-render the cursor at the new pixel scale. */
    mgl_wayland_update_cursor(impl);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_preferred,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                    int32_t width, int32_t height, struct wl_array *states) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)toplevel;

    bool fullscreen = false;
    uint32_t *state;
    wl_array_for_each(state, states) {
        if(*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
            fullscreen = true;
    }
    impl->is_fullscreen = fullscreen;

    mgl_window_wayland_on_resize(self, width, height);
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)xdg_toplevel;

    self->open = false;
    mgl_event ev;
    ev.type = MGL_EVENT_CLOSED;
    mgl_window_wayland_append_event(impl, &ev);
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
                                           int32_t width, int32_t height) {
    (void)data; (void)toplevel; (void)width; (void)height;
}

static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                          struct wl_array *capabilities) {
    (void)data; (void)toplevel; (void)capabilities;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure        = xdg_toplevel_configure,
    .close            = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities  = xdg_toplevel_wm_capabilities,
};

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, struct wl_surface *surface,
                              wl_fixed_t surface_x, wl_fixed_t surface_y) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer; (void)surface;
    impl->pointer_enter_serial = serial;

    const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
    impl->mouse_moved = true;
    impl->mouse_position = (mgl_vec2i){
        .x = (wl_fixed_to_int(surface_x) * s120 + 60) / 120,
        .y = (wl_fixed_to_int(surface_y) * s120 + 60) / 120
    };

    mgl_wayland_update_cursor(impl);
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)wl_pointer; (void)serial; (void)surface;
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                               uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer; (void)time;
    /* Convert from surface (logical) to buffer-pixel coordinates so the app
       sees a single coordinate system that matches self->size. scale_120 is
       in 120ths (120 == 1.0×, 180 == 1.5×, 240 == 2.0×). */
    const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
    impl->mouse_moved = true;
    impl->mouse_position = (mgl_vec2i){
        .x = (wl_fixed_to_int(surface_x) * s120 + 60) / 120,
        .y = (wl_fixed_to_int(surface_y) * s120 + 60) / 120
    };
}

static mgl_mouse_button wayland_button_to_mgl_button(uint32_t button) {
    switch(button) {
        case BTN_LEFT:   return MGL_BUTTON_LEFT;
        case BTN_MIDDLE: return MGL_BUTTON_MIDDLE;
        case BTN_RIGHT:  return MGL_BUTTON_RIGHT;
        case BTN_SIDE:   return MGL_BUTTON_XBUTTON1;
        case BTN_EXTRA:  return MGL_BUTTON_XBUTTON2;
        default:         return MGL_BUTTON_UNKNOWN;
    }
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
                               uint32_t time, uint32_t button, uint32_t state) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer; (void)time;

    impl->last_input_serial = serial;

    const mgl_mouse_button mgl_button = wayland_button_to_mgl_button(button);
    if(mgl_button == MGL_BUTTON_UNKNOWN)
        return;

    if(state == WL_POINTER_BUTTON_STATE_PRESSED)
        impl->mouse_buttons_mask |= (1u << mgl_button);
    else
        impl->mouse_buttons_mask &= ~(1u << mgl_button);

    impl->frame_has_button = true;
    impl->frame_button_event.type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
        ? MGL_EVENT_MOUSE_BUTTON_PRESSED
        : MGL_EVENT_MOUSE_BUTTON_RELEASED;
    impl->frame_button_event.mouse_button.button = mgl_button;
    impl->frame_button_event.mouse_button.x = self->cursor_position.x;
    impl->frame_button_event.mouse_button.y = self->cursor_position.y;
    impl->frame_button_event.mouse_button.key_states = mgl_window_wayland_get_key_states(impl);
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                             uint32_t time, uint32_t axis, wl_fixed_t value) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer; (void)time;

    if(axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;

    /* Only use axis value if we haven't received a discrete event */
    if(!impl->frame_has_scroll) {
        const double v = wl_fixed_to_double(value);
        impl->frame_has_scroll = true;
        impl->frame_scroll_delta = (v < 0) ? 1 : -1;
    }
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                                    uint32_t axis_source) {
    (void)data; (void)wl_pointer; (void)axis_source;
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, uint32_t axis) {
    (void)data; (void)wl_pointer; (void)time; (void)axis;
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                      uint32_t axis, int32_t discrete) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer;

    if(axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;

    /* Discrete overrides the continuous axis value */
    impl->frame_has_scroll = true;
    impl->frame_scroll_delta = (discrete > 0) ? -1 : 1;
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_pointer;

    if(impl->mouse_moved) {
        impl->mouse_moved = false;
        self->cursor_position = impl->mouse_position;

        mgl_event ev;
        ev.type = MGL_EVENT_MOUSE_MOVED;
        ev.mouse_move.x = self->cursor_position.x;
        ev.mouse_move.y = self->cursor_position.y;
        ev.mouse_move.key_states = mgl_window_wayland_get_key_states(impl);
        mgl_window_wayland_append_event(impl, &ev);
    }

    if(impl->frame_has_button) {
        impl->frame_has_button = false;
        impl->frame_button_event.mouse_button.x = self->cursor_position.x;
        impl->frame_button_event.mouse_button.y = self->cursor_position.y;
        mgl_window_wayland_append_event(impl, &impl->frame_button_event);
    }

    if(impl->frame_has_scroll) {
        impl->frame_has_scroll = false;
        mgl_event ev;
        ev.type = MGL_EVENT_MOUSE_WHEEL_SCROLLED;
        ev.mouse_wheel_scroll.delta = impl->frame_scroll_delta;
        ev.mouse_wheel_scroll.x = self->cursor_position.x;
        ev.mouse_wheel_scroll.y = self->cursor_position.y;
        ev.mouse_wheel_scroll.key_states = mgl_window_wayland_get_key_states(impl);
        mgl_window_wayland_append_event(impl, &ev);
    }
}

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter         = wl_pointer_enter,
    .leave         = wl_pointer_leave,
    .motion        = wl_pointer_motion,
    .button        = wl_pointer_button,
    .axis          = wl_pointer_axis,
    .frame         = wl_pointer_frame,
    .axis_source   = wl_pointer_axis_source,
    .axis_stop     = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void mgl_wayland_handle_key(mgl_window *self, mgl_window_wayland *impl,
                                    uint32_t key, uint32_t state, uint32_t serial) {
    if(!impl->xkb_state)
        return;

    impl->last_input_serial = serial;

    const uint32_t xkb_keycode = key + 8;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(impl->xkb_state, xkb_keycode);
    const mgl_key mgl_k = xkb_keysym_to_mgl_key(sym);

    if(state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if(mgl_k != MGL_KEY_UNKNOWN)
            impl->keys_pressed[mgl_k] = true;

        if(!self->key_repeat_enabled && impl->key_repeat_active && impl->key_repeat_keycode == key)
            return;

        impl->key_repeat_active = true;
        impl->key_repeat_keycode = key;
        impl->key_repeat_sym = sym;
        impl->key_repeat_first_fired = false;
        mgl_clock_init(&impl->key_repeat_clock);

        mgl_event ev;
        ev.type = MGL_EVENT_KEY_PRESSED;
        ev.key.code = mgl_k;
        ev.key.key_states = mgl_window_wayland_get_key_states(impl);
        mgl_window_wayland_append_event(impl, &ev);

        /* Generate text event */
        char utf8_buf[8];
        const int utf8_len = xkb_state_key_get_utf8(impl->xkb_state, xkb_keycode,
                                                      utf8_buf, sizeof(utf8_buf));
        if(utf8_len > 0) {
            const unsigned char *cp = (const unsigned char*)utf8_buf;
            size_t i = 0;
            while(i < (size_t)utf8_len) {
                uint32_t codepoint;
                size_t clen;
                if(!mgl_utf8_decode(cp + i, (size_t)utf8_len - i, &codepoint, &clen)) {
                    codepoint = cp[i];
                    clen = 1;
                }
                /* Skip control characters */
                if(codepoint >= 32 && codepoint != 127) {
                    mgl_event text_ev;
                    text_ev.type = MGL_EVENT_TEXT_ENTERED;
                    text_ev.text.codepoint = codepoint;
                    text_ev.text.size = (int)clen;
                    memcpy(text_ev.text.str, utf8_buf + i, clen);
                    text_ev.text.str[clen] = '\0';
                    mgl_window_wayland_append_event(impl, &text_ev);
                }
                i += clen;
            }
        }
    } else {
        if(mgl_k != MGL_KEY_UNKNOWN)
            impl->keys_pressed[mgl_k] = false;

        if(impl->key_repeat_active && impl->key_repeat_keycode == key)
            impl->key_repeat_active = false;

        mgl_event ev;
        ev.type = MGL_EVENT_KEY_RELEASED;
        ev.key.code = mgl_k;
        ev.key.key_states = mgl_window_wayland_get_key_states(impl);
        mgl_window_wayland_append_event(impl, &ev);
    }
}

static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                                uint32_t format, int32_t fd, uint32_t size) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard;

    if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if(map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_keymap *new_keymap = xkb_keymap_new_from_string(
        impl->xkb_context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if(!new_keymap)
        return;

    struct xkb_state *new_state = xkb_state_new(new_keymap);
    if(!new_state) {
        xkb_keymap_unref(new_keymap);
        return;
    }

    if(impl->xkb_state) xkb_state_unref(impl->xkb_state);
    if(impl->xkb_keymap) xkb_keymap_unref(impl->xkb_keymap);

    impl->xkb_keymap = new_keymap;
    impl->xkb_state  = new_state;

    impl->xkb_mod_shift = xkb_keymap_mod_get_index(impl->xkb_keymap, XKB_MOD_NAME_SHIFT);
    impl->xkb_mod_ctrl  = xkb_keymap_mod_get_index(impl->xkb_keymap, XKB_MOD_NAME_CTRL);
    impl->xkb_mod_alt   = xkb_keymap_mod_get_index(impl->xkb_keymap, XKB_MOD_NAME_ALT);
    impl->xkb_mod_logo  = xkb_keymap_mod_get_index(impl->xkb_keymap, XKB_MOD_NAME_LOGO);
}

static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t serial, struct wl_surface *surface,
                               struct wl_array *keys) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard; (void)serial; (void)surface; (void)keys;

    self->focused = true;
    mgl_event ev;
    ev.type = MGL_EVENT_GAINED_FOCUS;
    mgl_window_wayland_append_event(impl, &ev);
}

static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t serial, struct wl_surface *surface) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard; (void)serial; (void)surface;

    self->focused = false;
    impl->key_repeat_active = false;
    memset(impl->keys_pressed, 0, sizeof(impl->keys_pressed));

    mgl_event ev;
    ev.type = MGL_EVENT_LOST_FOCUS;
    mgl_window_wayland_append_event(impl, &ev);
}

static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                              uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard; (void)time;
    mgl_wayland_handle_key(self, impl, key, state, serial);
}

static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                                   uint32_t serial, uint32_t mods_depressed,
                                   uint32_t mods_latched, uint32_t mods_locked,
                                   uint32_t group) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard; (void)serial; (void)self;

    if(!impl->xkb_state)
        return;

    xkb_state_update_mask(impl->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);

    impl->mod_shift = xkb_state_mod_index_is_active(impl->xkb_state, impl->xkb_mod_shift,
                                                      XKB_STATE_MODS_EFFECTIVE) > 0;
    impl->mod_ctrl  = xkb_state_mod_index_is_active(impl->xkb_state, impl->xkb_mod_ctrl,
                                                      XKB_STATE_MODS_EFFECTIVE) > 0;
    impl->mod_alt   = xkb_state_mod_index_is_active(impl->xkb_state, impl->xkb_mod_alt,
                                                      XKB_STATE_MODS_EFFECTIVE) > 0;
    impl->mod_super = xkb_state_mod_index_is_active(impl->xkb_state, impl->xkb_mod_logo,
                                                      XKB_STATE_MODS_EFFECTIVE) > 0;
}

static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                     int32_t rate, int32_t delay) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)wl_keyboard; (void)self;

    impl->key_repeat_delay_s    = (rate > 0) ? (delay / 1000.0) : 0.0;
    impl->key_repeat_interval_s = (rate > 0) ? (1.0 / rate)     : 0.0;
    if(rate == 0)
        impl->key_repeat_active = false;
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap      = wl_keyboard_keymap,
    .enter       = wl_keyboard_enter,
    .leave       = wl_keyboard_leave,
    .key         = wl_keyboard_key,
    .modifiers   = wl_keyboard_modifiers,
    .repeat_info = wl_keyboard_repeat_info,
};

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    const bool have_pointer  = (capabilities & WL_SEAT_CAPABILITY_POINTER)  != 0;
    const bool have_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

    if(have_pointer && !impl->pointer) {
        impl->pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(impl->pointer, &wl_pointer_listener, self);
    } else if(!have_pointer && impl->pointer) {
        wl_pointer_release(impl->pointer);
        impl->pointer = NULL;
    }

    if(have_keyboard && !impl->keyboard) {
        impl->keyboard = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(impl->keyboard, &wl_keyboard_listener, self);
    } else if(!have_keyboard && impl->keyboard) {
        wl_keyboard_release(impl->keyboard);
        impl->keyboard = NULL;
    }
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data; (void)wl_seat; (void)name;
}

static const struct wl_seat_listener wl_seat_listener = {
    .capabilities = wl_seat_capabilities,
    .name         = wl_seat_name,
};

static void registry_add_object(void *data, struct wl_registry *registry,
                                 uint32_t name, const char *interface, uint32_t version) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;

    if(strcmp(interface, wl_compositor_interface.name) == 0) {
        if(!impl->compositor) {
            /* v3 is the minimum that supports wl_surface.set_buffer_scale,
               which we use on the integer-scale HiDPI fallback. v4 added
               wl_surface.damage_buffer and is virtually universal. Bind at
               the highest available version capped at v4. */
            const uint32_t bind_version = (version < 4) ? version : 4;
            impl->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version);
        }
    } else if(strcmp(interface, wl_shm_interface.name) == 0) {
        if(!impl->shm)
            impl->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if(strcmp(interface, wl_output_interface.name) == 0) {
        if(impl->num_outputs >= MGL_MAX_MONITORS) {
            fprintf(stderr, "mgl warning: reached maximum outputs (%d), ignoring output %u\n",
                    MGL_MAX_MONITORS, name);
            return;
        }
        const uint32_t bind_version = (version < 4) ? version : 4;
        mgl_wayland_output *out = &impl->outputs[impl->num_outputs++];
        out->wl_name = name;
        out->output  = wl_registry_bind(registry, name, &wl_output_interface, bind_version);
        out->ready   = false;
        out->scale   = 1;
        out->name[0] = '\0';
        wl_output_add_listener(out->output, &wl_output_listener, out);
    } else if(strcmp(interface, wl_shell_interface.name) == 0) {
        if(!impl->shell)
            impl->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    } else if(strcmp(interface, xdg_wm_base_interface.name) == 0) {
        if(!impl->xdg_wm_base)
            impl->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    } else if(strcmp(interface, wl_seat_interface.name) == 0) {
        if(!impl->seat) {
            const uint32_t bind_version = (version < 5) ? version : 5;
            impl->seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
            /* Attach listener immediately. The server sends wl_seat.capabilities
               in response to the bind, and that event is delivered during the
               next dispatch — if no listener is attached by then, libwayland
               silently drops it and we never create wl_pointer / wl_keyboard. */
            wl_seat_add_listener(impl->seat, &wl_seat_listener, self);
        }
    } else if(strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        /* The data_device manager + device are shared across all windows for
           the lifetime of the wl_display — see mgl_wayland_clipboard_state
           comment. Bind once. */
        if(!g_clipboard.manager) {
            const uint32_t bind_version = 1;
            g_clipboard.manager = wl_registry_bind(registry, name,
                &wl_data_device_manager_interface, bind_version);
        }
    } else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        if(!impl->decoration_manager)
            impl->decoration_manager = wl_registry_bind(registry, name,
                &zxdg_decoration_manager_v1_interface, 1);
    } else if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        if(!impl->layer_shell) {
            const uint32_t bind_version = (version < 4) ? version : 4;
            impl->layer_shell = wl_registry_bind(registry, name,
                &zwlr_layer_shell_v1_interface, bind_version);
        }
    } else if(strcmp(interface, wp_viewporter_interface.name) == 0) {
        if(!impl->viewporter)
            impl->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if(strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        if(!impl->fractional_scale_manager)
            impl->fractional_scale_manager = wl_registry_bind(registry, name,
                &wp_fractional_scale_manager_v1_interface, 1);
    } else if(strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        if(!impl->cursor_shape_manager)
            impl->cursor_shape_manager = wl_registry_bind(registry, name,
                &wp_cursor_shape_manager_v1_interface, 1);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name) {
    mgl_window *self = data;
    mgl_window_wayland *impl = self->impl;
    (void)registry;

    for(int i = 0; i < impl->num_outputs; ++i) {
        if(impl->outputs[i].wl_name == name) {
            if(impl->outputs[i].output) {
                wl_output_destroy(impl->outputs[i].output);
                impl->outputs[i].output = NULL;
            }
            for(int j = i + 1; j < impl->num_outputs; ++j)
                impl->outputs[j - 1] = impl->outputs[j];
            impl->num_outputs--;
            mgl_wayland_sync_outputs_to_monitors(self);
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_add_object,
    .global_remove = registry_remove_object,
};

#ifdef MGL_LIBDECOR
static void libdecor_frame_configure_cb(struct libdecor_frame *frame,
                                         struct libdecor_configuration *config,
                                         void *user_data) {
    mgl_window *self = user_data;
    mgl_window_wayland *impl = self->impl;

    int width = self->size.x, height = self->size.y;
    libdecor_configuration_get_content_size(config, frame, &width, &height);
    if(width <= 0)  width  = self->size.x;
    if(height <= 0) height = self->size.y;

    enum libdecor_window_state window_state;
    if(libdecor_configuration_get_window_state(config, &window_state))
        impl->is_fullscreen = (window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN) != 0;

    struct libdecor_state *state = libdecor_state_new(width, height);
    if(state) {
        libdecor_frame_commit(frame, state, config);
        libdecor_state_free(state);
    }

    mgl_window_wayland_on_resize(self, width, height);
}

static void libdecor_frame_close_cb(struct libdecor_frame *frame, void *user_data) {
    mgl_window *self = user_data;
    mgl_window_wayland *impl = self->impl;
    (void)frame;
    self->open = false;
    mgl_event ev;
    ev.type = MGL_EVENT_CLOSED;
    mgl_window_wayland_append_event(impl, &ev);
}

static void libdecor_frame_commit_cb(struct libdecor_frame *frame, void *user_data) {
    mgl_window *self = user_data;
    mgl_window_wayland *impl = self->impl;
    (void)frame;
    if(impl->surface)
        wl_surface_commit(impl->surface);
}

static const struct libdecor_frame_interface libdecor_frame_iface = {
    .configure = libdecor_frame_configure_cb,
    .close     = libdecor_frame_close_cb,
    .commit    = libdecor_frame_commit_cb,
};

static void libdecor_error_cb(struct libdecor *context, enum libdecor_error error,
                               const char *message) {
    (void)context;
    fprintf(stderr, "mgl: libdecor error %d: %s\n", error, message);
}

static const struct libdecor_interface libdecor_iface = {
    .error = libdecor_error_cb,
};
#endif /* MGL_LIBDECOR */

static bool mgl_wayland_setup_window(mgl_window *self, const char *title,
                                      const mgl_window_create_params *params,
                                      mgl_window_handle existing_window) {
    (void)existing_window;
    mgl_window_wayland *impl = self->impl;
    mgl_context *context = mgl_get_context();

    mgl_vec2i window_size = params ? params->size : (mgl_vec2i){ 0, 0 };
    if(window_size.x <= 0 || window_size.y <= 0) {
        window_size.x = 640;
        window_size.y = 480;
    }
    self->size = window_size;
    impl->scale_120 = 120;
    impl->scale_via_viewport = false;

    impl->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(!impl->xkb_context)
        fprintf(stderr, "mgl warning: mgl_wayland_setup_window: xkb_context_new failed\n");

    impl->registry = wl_display_get_registry(context->connection);
    if(!impl->registry) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: wl_display_get_registry failed\n");
        return false;
    }
    wl_registry_add_listener(impl->registry, &registry_listener, self);

    /* Fetch globals (compositor, shell, seat, outputs, etc.) */
    wl_display_roundtrip(context->connection);
    /* Fetch output geometry/mode/done events */
    wl_display_roundtrip(context->connection);

    if(!impl->compositor) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: compositor not found\n");
        return false;
    }
    const bool want_overlay = params && params->window_type == MGL_WINDOW_TYPE_OVERLAY && impl->layer_shell;
    if(!want_overlay && !impl->xdg_wm_base && !impl->shell) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: no shell available\n");
        return false;
    }
    if(params && params->window_type == MGL_WINDOW_TYPE_OVERLAY && !impl->layer_shell) {
        fprintf(stderr, "mgl warning: mgl_wayland_setup_window: MGL_WINDOW_TYPE_OVERLAY requested but compositor doesn't advertise wlr-layer-shell-unstable-v1; falling back to a regular toplevel\n");
    }
    if(!impl->seat) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: seat not found\n");
        return false;
    }

    /* Another roundtrip to get seat capabilities (pointer/keyboard).
       The seat listener was attached when we bound the seat in registry_add_object. */
    wl_display_roundtrip(context->connection);
    /* And one more to get the keyboard keymap */
    wl_display_roundtrip(context->connection);

    /* Create the persistent wl_data_device the first time we have a seat to
       bind it to. Subsequent windows reuse it — see the comment on
       mgl_wayland_clipboard_state for why this must outlive the window. */
    if(g_clipboard.manager && impl->seat && !g_clipboard.device) {
        g_clipboard.device = wl_data_device_manager_get_data_device(
            g_clipboard.manager, impl->seat);
        if(g_clipboard.device)
            wl_data_device_add_listener(g_clipboard.device, &data_device_listener, &g_clipboard);
    }

    /* Cursor setup. The theme is loaded lazily in update_cursor at the
       scale-appropriate pixel size; here we just create the surface. */
    impl->cursor_visible = true;
    impl->cursor_theme_pixel_size = 0;
    if(impl->shm)
        impl->cursor_surface = wl_compositor_create_surface(impl->compositor);

    /* Initialize XKB */
    /* Create surface */
    impl->surface = wl_compositor_create_surface(impl->compositor);
    if(!impl->surface) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: wl_compositor_create_surface failed\n");
        return false;
    }
    wl_surface_add_listener(impl->surface, &wl_surface_listener, self);

    /* Set up shell */
    if(want_overlay) {
        const mgl_layer_shell_options *opts_pre = params ? &params->layer_shell_options : NULL;
        struct wl_output *target_output = NULL;
        if(opts_pre && opts_pre->output_name && opts_pre->output_name[0]) {
            for(int i = 0; i < impl->num_outputs; ++i) {
                if(strcmp(impl->outputs[i].name, opts_pre->output_name) == 0) {
                    target_output = impl->outputs[i].output;
                    break;
                }
            }
            if(!target_output) {
                fprintf(stderr, "mgl warning: mgl_wayland_setup_window: layer_shell_options.output_name=\"%s\" not found; letting compositor pick the output\n",
                    opts_pre->output_name);
            }
        }

        impl->layer_surface = zwlr_layer_shell_v1_get_layer_surface(impl->layer_shell,
            impl->surface, target_output,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
            params->class_name ? params->class_name : "mgl-overlay");
        if(!impl->layer_surface) {
            fprintf(stderr, "mgl error: mgl_wayland_setup_window: zwlr_layer_shell_v1_get_layer_surface failed\n");
            return false;
        }
        zwlr_layer_surface_v1_add_listener(impl->layer_surface, &layer_surface_listener, self);

        const mgl_layer_shell_options *opts = params ? &params->layer_shell_options : NULL;

        /* Anchor: bitmask of mgl flags maps 1:1 to wlr-layer-shell anchor flags.
           When the user passes 0, anchor to all four edges (covers the full
           output when size is 0,0). */
        uint32_t anchor;
        if(opts && opts->anchor != 0) {
            anchor = 0;
            if(opts->anchor & MGL_LAYER_SHELL_ANCHOR_TOP)    anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            if(opts->anchor & MGL_LAYER_SHELL_ANCHOR_BOTTOM) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            if(opts->anchor & MGL_LAYER_SHELL_ANCHOR_LEFT)   anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            if(opts->anchor & MGL_LAYER_SHELL_ANCHOR_RIGHT)  anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        } else {
            anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        }
        zwlr_layer_surface_v1_set_anchor(impl->layer_surface, anchor);

        /* Exclusive zone: 0 in opts means "use mgl default of -1". */
        const int32_t exclusive_zone = (opts && opts->exclusive_zone != 0) ? opts->exclusive_zone : -1;
        zwlr_layer_surface_v1_set_exclusive_zone(impl->layer_surface, exclusive_zone);

        uint32_t k_int = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
        if(opts) {
            switch(opts->keyboard_interactivity) {
                case MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_NONE:
                    k_int = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE; break;
                case MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_EXCLUSIVE:
                    k_int = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE; break;
                case MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_DEFAULT:
                case MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_ON_DEMAND:
                default: break;
            }
        }
        zwlr_layer_surface_v1_set_keyboard_interactivity(impl->layer_surface, k_int);

        if(opts) {
            zwlr_layer_surface_v1_set_margin(impl->layer_surface,
                opts->margin_top, opts->margin_right, opts->margin_bottom, opts->margin_left);
        }

        /* Size: passes through create_params.size. The compositor ignores the
           size on axes spanned by opposing anchors (e.g. anchor=top+bottom+left+right
           with size=0,0 covers the full output). For a corner-anchored surface
           (e.g. anchor=top+right) the size is honored as the exact surface size. */
        const int32_t set_size_x = params->size.x > 0 ? params->size.x : 0;
        const int32_t set_size_y = params->size.y > 0 ? params->size.y : 0;
        zwlr_layer_surface_v1_set_size(impl->layer_surface, set_size_x, set_size_y);

        /* HiDPI: attach wp_viewport + wp_fractional_scale_v1 (when available)
           BEFORE the first commit so the compositor delivers a preferred_scale
           event with the initial configure. The fractional-scale listener will
           then update impl->scale_120, resize the EGL window, and call
           wp_viewport.set_destination(logical_w, logical_h).
           When the manager isn't advertised we fall through to the legacy
           integer wl_output.scale path after configure. */
        if(impl->viewporter && impl->fractional_scale_manager) {
            impl->viewport = wp_viewporter_get_viewport(impl->viewporter, impl->surface);
            impl->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
                impl->fractional_scale_manager, impl->surface);
            if(impl->fractional_scale)
                wp_fractional_scale_v1_add_listener(impl->fractional_scale,
                    &fractional_scale_listener, self);
        }

        wl_surface_commit(impl->surface);
        /* Wait for the initial configure so we have a real size before creating the EGL window. */
        while(!impl->layer_surface_configured)
            wl_display_dispatch(context->connection);

        if(impl->scale_via_viewport) {
            /* preferred_scale fired before we returned from the configure roundtrip.
               on_resize (called from layer_surface_configure with logical width/height)
               already produced a buffer-pixel self->size using the new scale_120.
               Make sure the viewport's destination is set to the logical size, in case
               preferred_scale arrived BEFORE configure. */
            const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
            const int32_t logical_w = (self->size.x * 120 + s120 / 2) / s120;
            const int32_t logical_h = (self->size.y * 120 + s120 / 2) / s120;
            wp_viewport_set_destination(impl->viewport, logical_w, logical_h);
        } else {
            /* No fractional-scale path. Fall back to the legacy integer wl_output.scale.
               Pick the scale of the target output (or the highest among all outputs
               if none was specified). */
            int32_t pick_scale = 1;
            if(target_output) {
                for(int i = 0; i < impl->num_outputs; ++i) {
                    if(impl->outputs[i].output == target_output) {
                        if(impl->outputs[i].scale > pick_scale)
                            pick_scale = impl->outputs[i].scale;
                        break;
                    }
                }
            } else {
                for(int i = 0; i < impl->num_outputs; ++i) {
                    if(impl->outputs[i].scale > pick_scale)
                        pick_scale = impl->outputs[i].scale;
                }
            }
            if(pick_scale > 1) {
                impl->scale_120 = pick_scale * 120;
                wl_surface_set_buffer_scale(impl->surface, pick_scale);
                self->size.x *= pick_scale;
                self->size.y *= pick_scale;
            }
        }
    } else if(impl->xdg_wm_base) {
        xdg_wm_base_add_listener(impl->xdg_wm_base, &wm_base_listener, self);

        const bool hide_decorations = params && params->hide_decorations;
        const bool want_ssd = !hide_decorations && impl->decoration_manager;

#ifdef MGL_LIBDECOR
        const bool want_libdecor = !want_ssd && !hide_decorations;
        if(want_libdecor) {
            impl->libdecor_context = libdecor_new(context->connection, &libdecor_iface);
            if(impl->libdecor_context) {
                impl->libdecor_frame = libdecor_decorate(impl->libdecor_context, impl->surface,
                                                          &libdecor_frame_iface, self);
                if(impl->libdecor_frame) {
                    impl->using_libdecor = true;
                    libdecor_frame_set_title(impl->libdecor_frame, title);
                    if(params && params->class_name)
                        libdecor_frame_set_app_id(impl->libdecor_frame, params->class_name);
                    if(params) {
                        if(params->min_size.x || params->min_size.y)
                            libdecor_frame_set_min_content_size(impl->libdecor_frame,
                                params->min_size.x, params->min_size.y);
                        if(params->max_size.x || params->max_size.y)
                            libdecor_frame_set_max_content_size(impl->libdecor_frame,
                                params->max_size.x, params->max_size.y);
                    }
                    libdecor_frame_map(impl->libdecor_frame);
                    /* Wait for the initial configure from libdecor */
                    wl_display_roundtrip(context->connection);
                    wl_display_roundtrip(context->connection);
                }
            }
        }
        if(!impl->using_libdecor)
#endif /* MGL_LIBDECOR */
        {
            impl->xdg_surface = xdg_wm_base_get_xdg_surface(impl->xdg_wm_base, impl->surface);
            if(!impl->xdg_surface) {
                fprintf(stderr, "mgl error: mgl_wayland_setup_window: xdg_wm_base_get_xdg_surface failed\n");
                return false;
            }
            xdg_surface_add_listener(impl->xdg_surface, &xdg_surface_listener, self);

            impl->xdg_toplevel = xdg_surface_get_toplevel(impl->xdg_surface);
            if(!impl->xdg_toplevel) {
                fprintf(stderr, "mgl error: mgl_wayland_setup_window: xdg_surface_get_toplevel failed\n");
                return false;
            }
            xdg_toplevel_add_listener(impl->xdg_toplevel, &xdg_toplevel_listener, self);
            xdg_toplevel_set_title(impl->xdg_toplevel, title);

            if(params && params->class_name)
                xdg_toplevel_set_app_id(impl->xdg_toplevel, params->class_name);

            /* Request server-side decorations if available */
            if(want_ssd) {
                impl->toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
                    impl->decoration_manager, impl->xdg_toplevel);
                if(impl->toplevel_decoration) {
                    zxdg_toplevel_decoration_v1_add_listener(impl->toplevel_decoration,
                        &xdg_toplevel_decoration_listener, impl);
                    zxdg_toplevel_decoration_v1_set_mode(impl->toplevel_decoration,
                        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
                }
            }

            wl_surface_commit(impl->surface);
            wl_display_roundtrip(context->connection);
        }
    } else {
        impl->shell_surface = wl_shell_get_shell_surface(impl->shell, impl->surface);
        if(!impl->shell_surface) {
            fprintf(stderr, "mgl error: mgl_wayland_setup_window: wl_shell_get_shell_surface failed\n");
            return false;
        }
        wl_shell_surface_add_listener(impl->shell_surface, &shell_surface_listener, self);
        wl_shell_surface_set_toplevel(impl->shell_surface);
        wl_shell_surface_set_title(impl->shell_surface, title);
        if(params && params->class_name)
            wl_shell_surface_set_class(impl->shell_surface, params->class_name);

        wl_surface_commit(impl->surface);
        wl_display_roundtrip(context->connection);
    }

    /* Create EGL window */
    impl->window = wl_egl_window_create(impl->surface, self->size.x, self->size.y);
    if(!impl->window) {
        fprintf(stderr, "mgl error: mgl_wayland_setup_window: wl_egl_window_create failed\n");
        return false;
    }

    /* Sync monitor info */
    mgl_wayland_sync_outputs_to_monitors(self);
    mgl_window_wayland_set_frame_time_limit_monitor(self);

    /* Trigger initial resize to set view/scissor */
    {
        mgl_view view;
        view.position = (mgl_vec2i){ 0, 0 };
        view.size = self->size;
        mgl_window_set_view(self, &view);
        mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });
    }

    /* Initialize key repeat defaults */
    impl->key_repeat_delay_s    = 0.4;
    impl->key_repeat_interval_s = 1.0 / 25.0;

    self->open    = true;
    self->focused = false;
    return true;
}

static bool mgl_window_wayland_fire_key_repeat(mgl_window *self, mgl_window_wayland *impl,
                                                mgl_event *event) {
    if(!impl->key_repeat_active || !self->key_repeat_enabled)
        return false;
    if(impl->key_repeat_interval_s <= 0.0)
        return false;

    const double elapsed = mgl_clock_get_elapsed_time_seconds(&impl->key_repeat_clock);

    if(!impl->key_repeat_first_fired) {
        if(elapsed < impl->key_repeat_delay_s)
            return false;
        impl->key_repeat_first_fired = true;
        mgl_clock_init(&impl->key_repeat_interval_clock);
    } else {
        const double iv = mgl_clock_get_elapsed_time_seconds(&impl->key_repeat_interval_clock);
        if(iv < impl->key_repeat_interval_s)
            return false;
        mgl_clock_init(&impl->key_repeat_interval_clock);
    }

    const mgl_key mgl_k = xkb_keysym_to_mgl_key(impl->key_repeat_sym);
    event->type     = MGL_EVENT_KEY_PRESSED;
    event->key.code = mgl_k;
    event->key.key_states = mgl_window_wayland_get_key_states(impl);

    /* Generate text event for repeated key */
    if(impl->xkb_state) {
        const uint32_t xkb_keycode = impl->key_repeat_keycode + 8;
        char utf8_buf[8];
        const int utf8_len = xkb_state_key_get_utf8(impl->xkb_state, xkb_keycode,
                                                      utf8_buf, sizeof(utf8_buf));
        if(utf8_len > 0) {
            uint32_t codepoint;
            size_t clen;
            if(mgl_utf8_decode((const unsigned char*)utf8_buf, (size_t)utf8_len,
                               &codepoint, &clen) && codepoint >= 32 && codepoint != 127) {
                mgl_event text_ev;
                text_ev.type = MGL_EVENT_TEXT_ENTERED;
                text_ev.text.codepoint = codepoint;
                text_ev.text.size = (int)clen;
                memcpy(text_ev.text.str, utf8_buf, clen);
                text_ev.text.str[clen] = '\0';
                mgl_window_wayland_append_event(impl, &text_ev);
            }
        }
    }

    return true;
}

static mgl_window_handle mgl_window_wayland_get_system_handle(const mgl_window *self) {
    const mgl_window_wayland *impl = self->impl;
    return (mgl_window_handle)impl->window;
}

static void mgl_window_wayland_close(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;

    if(impl->window) {
        wl_egl_window_destroy(impl->window);
        impl->window = NULL;
    }
    /* Destroy role objects before the wl_surface that owns them — Hyprland
       and other strict compositors will raise a protocol error otherwise,
       which puts the wl_display into a permanent error state. */
#ifdef MGL_LIBDECOR
    if(impl->libdecor_frame) {
        libdecor_frame_unref(impl->libdecor_frame);
        impl->libdecor_frame = NULL;
        impl->using_libdecor = false;
    }
#endif
    if(impl->toplevel_decoration) {
        zxdg_toplevel_decoration_v1_destroy(impl->toplevel_decoration);
        impl->toplevel_decoration = NULL;
    }
    /* Destroy wp_fractional_scale_v1 and wp_viewport before the wl_surface they
       reference. */
    if(impl->fractional_scale) {
        wp_fractional_scale_v1_destroy(impl->fractional_scale);
        impl->fractional_scale = NULL;
    }
    if(impl->viewport) {
        wp_viewport_destroy(impl->viewport);
        impl->viewport = NULL;
    }
    if(impl->layer_surface) {
        zwlr_layer_surface_v1_destroy(impl->layer_surface);
        impl->layer_surface = NULL;
        impl->layer_surface_configured = false;
    }
    if(impl->xdg_toplevel) {
        xdg_toplevel_destroy(impl->xdg_toplevel);
        impl->xdg_toplevel = NULL;
    }
    if(impl->xdg_surface) {
        xdg_surface_destroy(impl->xdg_surface);
        impl->xdg_surface = NULL;
    }
    if(impl->shell_surface) {
        wl_shell_surface_destroy(impl->shell_surface);
        impl->shell_surface = NULL;
    }
    if(impl->surface) {
        wl_surface_destroy(impl->surface);
        impl->surface = NULL;
    }
    /* Flush the destroy requests so the compositor actually tears the surface
       down right away rather than leaving the layer-shell visual on screen
       until something else triggers a flush. */
    wl_display_flush(mgl_get_context()->connection);
    self->open = false;
}

static bool mgl_window_wayland_poll_event(mgl_window *self, mgl_event *event) {
    mgl_window_wayland *impl = self->impl;
    mgl_context *context = mgl_get_context();
    struct wl_display *display = context->connection;

    /* Return any buffered events first */
    if(mgl_window_wayland_pop_event(impl, event))
        return true;

    /* Check key repeat */
    if(mgl_window_wayland_fire_key_repeat(self, impl, event))
        return true;

    /* Non-blocking dispatch of new events from the server */
#ifdef MGL_LIBDECOR
    if(impl->using_libdecor && impl->libdecor_context) {
        libdecor_dispatch(impl->libdecor_context, 0);
    } else
#endif
    {
        wl_display_flush(display);
        if(wl_display_prepare_read(display) == 0) {
            struct pollfd pfd = { .fd = wl_display_get_fd(display), .events = POLLIN };
            poll(&pfd, 1, 0);
            if(pfd.revents & POLLIN)
                wl_display_read_events(display);
            else
                wl_display_cancel_read(display);
        }
        wl_display_dispatch_pending(display);
    }

    if(mgl_window_wayland_pop_event(impl, event))
        return true;

    return false;
}

static void mgl_window_wayland_swap_buffers(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    mgl_graphics_swap_buffers(&impl->graphics, (mgl_window_handle)impl->window);
}

static void mgl_window_wayland_set_visible(mgl_window *self, bool visible) {
    /* Wayland doesn't have a direct hide/show API for xdg_toplevel.
       Minimizing is the closest option, but can't be un-done programmatically. */
    (void)self; (void)visible;
}

static bool mgl_window_wayland_is_key_pressed(const mgl_window *self, mgl_key key) {
    const mgl_window_wayland *impl = self->impl;
    if(key <= MGL_KEY_UNKNOWN || key >= __MGL_NUM_KEYS__)
        return false;
    return impl->keys_pressed[key];
}

static bool mgl_window_wayland_is_mouse_button_pressed(const mgl_window *self, mgl_mouse_button button) {
    const mgl_window_wayland *impl = self->impl;
    if(button <= MGL_BUTTON_UNKNOWN || button >= __MGL_NUM_MOUSE_BUTTONS__)
        return false;
    return (impl->mouse_buttons_mask & (1u << button)) != 0;
}

static void mgl_window_wayland_set_title(mgl_window *self, const char *title) {
    mgl_window_wayland *impl = self->impl;
#ifdef MGL_LIBDECOR
    if(impl->using_libdecor && impl->libdecor_frame) {
        libdecor_frame_set_title(impl->libdecor_frame, title);
        return;
    }
#endif
    if(impl->xdg_toplevel)
        xdg_toplevel_set_title(impl->xdg_toplevel, title);
    else if(impl->shell_surface)
        wl_shell_surface_set_title(impl->shell_surface, title);
}

static void mgl_window_wayland_set_cursor_visible(mgl_window *self, bool visible) {
    mgl_window_wayland *impl = self->impl;
    impl->cursor_visible = visible;
    mgl_wayland_update_cursor(impl);
}

static void mgl_window_wayland_set_vsync_enabled(mgl_window *self, bool enabled) {
    mgl_window_wayland *impl = self->impl;
    self->vsync_enabled = enabled;
    if(!mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window,
                                        self->vsync_enabled))
        fprintf(stderr, "mgl warning: mgl_window_wayland_set_vsync_enabled: failed\n");
}

static bool mgl_window_wayland_is_vsync_enabled(const mgl_window *self) {
    return self->vsync_enabled;
}

static void mgl_window_wayland_set_fullscreen(mgl_window *self, bool fullscreen) {
    mgl_window_wayland *impl = self->impl;
#ifdef MGL_LIBDECOR
    if(impl->using_libdecor && impl->libdecor_frame) {
        if(fullscreen)
            libdecor_frame_set_fullscreen(impl->libdecor_frame, NULL);
        else
            libdecor_frame_unset_fullscreen(impl->libdecor_frame);
        return;
    }
#endif
    if(impl->xdg_toplevel) {
        if(fullscreen)
            xdg_toplevel_set_fullscreen(impl->xdg_toplevel, NULL);
        else
            xdg_toplevel_unset_fullscreen(impl->xdg_toplevel);
    } else if(impl->shell_surface) {
        if(fullscreen)
            wl_shell_surface_set_fullscreen(impl->shell_surface,
                WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, NULL);
        else
            wl_shell_surface_set_toplevel(impl->shell_surface);
    }
}

static bool mgl_window_wayland_is_fullscreen(const mgl_window *self) {
    const mgl_window_wayland *impl = self->impl;
    return impl->is_fullscreen;
}

static void mgl_window_wayland_set_position(mgl_window *self, mgl_vec2i position) {
    /* Window positioning is not supported in the Wayland protocol */
    (void)self; (void)position;
}

static void mgl_window_wayland_set_size(mgl_window *self, mgl_vec2i size) {
    mgl_window_wayland *impl = self->impl;
    if(size.x <= 0) size.x = 1;
    if(size.y <= 0) size.y = 1;
    /* |size| is in buffer pixels (matches self->size). Convert to logical
       pixels for the surface-level requests below. */
    const int s120 = impl->scale_120 > 0 ? impl->scale_120 : 120;
    const int32_t logical_w = (size.x * 120 + s120 / 2) / s120;
    const int32_t logical_h = (size.y * 120 + s120 / 2) / s120;
    self->size = size;
    impl->logical_size.x = logical_w;
    impl->logical_size.y = logical_h;
    if(impl->window)
        wl_egl_window_resize(impl->window, size.x, size.y, 0, 0);

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });

#ifdef MGL_LIBDECOR
    if(impl->using_libdecor && impl->libdecor_frame) {
        /* libdecor takes logical content size. */
        struct libdecor_state *state = libdecor_state_new(logical_w, logical_h);
        if(state) {
            libdecor_frame_commit(impl->libdecor_frame, state, NULL);
            libdecor_state_free(state);
        }
        return;
    }
#endif

    /* Layer-shell set_size is in logical pixels. The compositor will reply
       with a configure event whose dimensions become authoritative; on_resize
       handles converting that back to buffer pixels. */
    if(impl->layer_surface)
        zwlr_layer_surface_v1_set_size(impl->layer_surface, (uint32_t)logical_w, (uint32_t)logical_h);

    /* Keep wp_viewport's destination in sync when we're driving scale via the
       viewporter; otherwise the compositor would still display the surface
       at the old logical size. */
    if(impl->viewport && impl->scale_via_viewport)
        wp_viewport_set_destination(impl->viewport, logical_w, logical_h);

    if(impl->surface)
        wl_surface_commit(impl->surface);
}

static void mgl_window_wayland_set_size_limits(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum) {
    mgl_window_wayland *impl = self->impl;
#ifdef MGL_LIBDECOR
    if(impl->using_libdecor && impl->libdecor_frame) {
        libdecor_frame_set_min_content_size(impl->libdecor_frame, minimum.x, minimum.y);
        libdecor_frame_set_max_content_size(impl->libdecor_frame, maximum.x, maximum.y);
        return;
    }
#endif
    if(impl->xdg_toplevel) {
        xdg_toplevel_set_min_size(impl->xdg_toplevel, minimum.x, minimum.y);
        xdg_toplevel_set_max_size(impl->xdg_toplevel, maximum.x, maximum.y);
    }
}

static void mgl_window_wayland_set_clipboard(mgl_window *self, const char *str, size_t size) {
    mgl_window_wayland *impl = self->impl;
    mgl_context *context = mgl_get_context();

    if(!g_clipboard.manager || !g_clipboard.device)
        return;

    /* Store clipboard content */
    free(g_clipboard.clipboard_data);
    g_clipboard.clipboard_data = malloc(size);
    if(!g_clipboard.clipboard_data) {
        g_clipboard.clipboard_data_size = 0;
        return;
    }
    memcpy(g_clipboard.clipboard_data, str, size);
    g_clipboard.clipboard_data_size = size;

    /* Destroy previous source */
    if(g_clipboard.data_source) {
        wl_data_source_destroy(g_clipboard.data_source);
        g_clipboard.data_source = NULL;
    }

    g_clipboard.data_source = wl_data_device_manager_create_data_source(g_clipboard.manager);
    if(!g_clipboard.data_source)
        return;

    wl_data_source_add_listener(g_clipboard.data_source, &data_source_listener, &g_clipboard);
    wl_data_source_offer(g_clipboard.data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(g_clipboard.data_source, "text/plain");
    wl_data_source_offer(g_clipboard.data_source, "STRING");
    wl_data_source_offer(g_clipboard.data_source, "UTF8_STRING");

    wl_data_device_set_selection(g_clipboard.device, g_clipboard.data_source, impl->last_input_serial);
    wl_display_flush(context->connection);
}

static bool mgl_window_wayland_get_clipboard(mgl_window *self, mgl_clipboard_callback callback,
                                              void *userdata, uint32_t clipboard_types) {
    (void)self;
    mgl_context *context = mgl_get_context();
    assert(callback);

    if(!g_clipboard.current_selection_offer)
        return false;

    /* Choose best available MIME type */
    const char *selected_mime = NULL;
    mgl_clipboard_type selected_type = MGL_CLIPBOARD_TYPE_STRING;

    if((clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_PNG)) {
        for(int i = 0; i < g_clipboard.num_current_selection_mimes; ++i) {
            if(strcmp(g_clipboard.current_selection_mimes[i], "image/png") == 0) {
                selected_mime = "image/png";
                selected_type = MGL_CLIPBOARD_TYPE_IMAGE_PNG;
                break;
            }
        }
    }
    if(!selected_mime && (clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_JPG)) {
        for(int i = 0; i < g_clipboard.num_current_selection_mimes; ++i) {
            if(strcmp(g_clipboard.current_selection_mimes[i], "image/jpeg") == 0 ||
               strcmp(g_clipboard.current_selection_mimes[i], "image/jpg") == 0) {
                selected_mime = g_clipboard.current_selection_mimes[i];
                selected_type = MGL_CLIPBOARD_TYPE_IMAGE_JPG;
                break;
            }
        }
    }
    if(!selected_mime && (clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_GIF)) {
        for(int i = 0; i < g_clipboard.num_current_selection_mimes; ++i) {
            if(strcmp(g_clipboard.current_selection_mimes[i], "image/gif") == 0) {
                selected_mime = "image/gif";
                selected_type = MGL_CLIPBOARD_TYPE_IMAGE_GIF;
                break;
            }
        }
    }
    if(!selected_mime && (clipboard_types & MGL_CLIPBOARD_TYPE_STRING)) {
        /* Prefer UTF-8 */
        for(int i = 0; i < g_clipboard.num_current_selection_mimes; ++i) {
            if(strcmp(g_clipboard.current_selection_mimes[i], "text/plain;charset=utf-8") == 0 ||
               strcmp(g_clipboard.current_selection_mimes[i], "UTF8_STRING") == 0) {
                selected_mime = g_clipboard.current_selection_mimes[i];
                selected_type = MGL_CLIPBOARD_TYPE_STRING;
                break;
            }
        }
        if(!selected_mime) {
            for(int i = 0; i < g_clipboard.num_current_selection_mimes; ++i) {
                if(strcmp(g_clipboard.current_selection_mimes[i], "text/plain") == 0 ||
                   strcmp(g_clipboard.current_selection_mimes[i], "STRING") == 0) {
                    selected_mime = g_clipboard.current_selection_mimes[i];
                    selected_type = MGL_CLIPBOARD_TYPE_STRING;
                    break;
                }
            }
        }
    }

    if(!selected_mime)
        return false;

    int fds[2];
    if(pipe(fds) == -1)
        return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    wl_data_offer_receive(g_clipboard.current_selection_offer, selected_mime, fds[1]);
    close(fds[1]);
    wl_display_roundtrip(context->connection);

    /* Read clipboard data */
    unsigned char buf[4096];
    unsigned char *data = NULL;
    size_t total = 0;
    ssize_t n;
    while((n = read(fds[0], buf, sizeof(buf))) > 0) {
        unsigned char *new_data = realloc(data, total + (size_t)n);
        if(!new_data) {
            free(data);
            close(fds[0]);
            return false;
        }
        data = new_data;
        memcpy(data + total, buf, (size_t)n);
        total += (size_t)n;
    }
    close(fds[0]);

    if(!data || total == 0) {
        free(data);
        return false;
    }

    const bool result = callback(data, total, selected_type, userdata);
    free(data);
    return result;
}

static void mgl_window_wayland_set_key_repeat_enabled(mgl_window *self, bool enabled) {
    self->key_repeat_enabled = enabled;
    if(!enabled) {
        mgl_window_wayland *impl = self->impl;
        impl->key_repeat_active = false;
    }
}

static void mgl_window_wayland_flush(mgl_window *self) {
    (void)self;
    mgl_context *context = mgl_get_context();
    wl_display_flush(context->connection);
}

static void* mgl_window_wayland_get_egl_display(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    if(impl->graphics.graphics_api == MGL_GRAPHICS_API_EGL)
        return mgl_graphics_get_display(&impl->graphics);
    return NULL;
}

static void* mgl_window_wayland_get_egl_context(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    if(impl->graphics.graphics_api == MGL_GRAPHICS_API_EGL)
        return mgl_graphics_get_context(&impl->graphics);
    return NULL;
}

static void mgl_window_wayland_for_each_active_monitor_output(mgl_window *self,
                                                               mgl_active_monitor_callback callback,
                                                               void *userdata) {
    for(int i = 0; i < self->num_monitors; ++i)
        callback(&self->monitors[i], userdata);
}

bool mgl_window_wayland_init(mgl_window *self, const char *title,
                              const mgl_window_create_params *params,
                              mgl_window_handle existing_window) {
    mgl_window_wayland *impl = calloc(1, sizeof(mgl_window_wayland));
    if(!impl)
        return false;

    self->get_system_handle          = mgl_window_wayland_get_system_handle;
    self->deinit                     = mgl_window_wayland_deinit;
    self->close                      = mgl_window_wayland_close;
    self->inject_x11_event           = NULL;
    self->poll_event                 = mgl_window_wayland_poll_event;
    self->swap_buffers               = mgl_window_wayland_swap_buffers;
    self->set_visible                = mgl_window_wayland_set_visible;
    self->is_key_pressed             = mgl_window_wayland_is_key_pressed;
    self->is_mouse_button_pressed    = mgl_window_wayland_is_mouse_button_pressed;
    self->set_title                  = mgl_window_wayland_set_title;
    self->set_cursor_visible         = mgl_window_wayland_set_cursor_visible;
    self->set_vsync_enabled          = mgl_window_wayland_set_vsync_enabled;
    self->is_vsync_enabled           = mgl_window_wayland_is_vsync_enabled;
    self->set_fullscreen             = mgl_window_wayland_set_fullscreen;
    self->is_fullscreen              = mgl_window_wayland_is_fullscreen;
    self->set_position               = mgl_window_wayland_set_position;
    self->set_size                   = mgl_window_wayland_set_size;
    self->set_size_limits            = mgl_window_wayland_set_size_limits;
    self->set_clipboard              = mgl_window_wayland_set_clipboard;
    self->get_clipboard              = mgl_window_wayland_get_clipboard;
    self->set_key_repeat_enabled     = mgl_window_wayland_set_key_repeat_enabled;
    self->flush                      = mgl_window_wayland_flush;
    self->get_egl_display            = mgl_window_wayland_get_egl_display;
    self->get_egl_context            = mgl_window_wayland_get_egl_context;
    self->for_each_active_monitor_output = mgl_window_wayland_for_each_active_monitor_output;
    self->impl = impl;

    wayland_events_circular_buffer_init(&impl->events);

    if(!mgl_wayland_setup_window(self, title, params, existing_window)) {
        mgl_window_wayland_deinit(self);
        return false;
    }

    assert(!params || params->graphics_api == MGL_GRAPHICS_API_EGL);

    const mgl_graphics_create_params g_create_params = {
        .graphics_api         = MGL_GRAPHICS_API_EGL,
        .alpha                = params && params->support_alpha,
        .request_depth_buffer = params && params->request_depth_buffer,
        .request_stencil_buffer = params && params->request_stencil_buffer,
    };

    if(!mgl_graphics_init(&impl->graphics, &g_create_params)) {
        mgl_window_wayland_deinit(self);
        return false;
    }

    if(!mgl_graphics_make_context_current(&impl->graphics, impl->window)) {
        fprintf(stderr, "mgl error: mgl_window_wayland_init: failed to make context current\n");
        mgl_window_wayland_deinit(self);
        return false;
    }

    self->vsync_enabled = true;
    mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window,
                                    self->vsync_enabled);

    /* For layer-shell surfaces, present an initial transparent frame so the
       compositor sees the surface as fully mapped (with a buffer) before we
       return. Hyprland and other strict compositors won't deliver pointer
       enter / keyboard events to a configured-but-unbuffered surface, which
       would leave the overlay unresponsive until the first draw cycle. */
    if(impl->layer_surface) {
        wl_display_roundtrip(mgl_get_context()->connection);
        mgl_window_wayland_set_size(self, self->size);

        mgl_color clear_color = { .r = 0, .g = 0, .b = 0, .a = 0 };
        mgl_window_clear(self, clear_color);
        mgl_graphics_swap_buffers(&impl->graphics, (mgl_window_handle)impl->window);
        wl_display_roundtrip(mgl_get_context()->connection);
    }

    mgl_context *context = mgl_get_context();
    context->current_window = self;
    return true;
}

void mgl_window_wayland_deinit(mgl_window *self) {
    mgl_window_wayland *impl = self->impl;
    if(!impl)
        return;

    mgl_graphics_deinit(&impl->graphics);
    mgl_window_wayland_close(self);

#ifdef MGL_LIBDECOR
    if(impl->libdecor_frame) {
        libdecor_frame_unref(impl->libdecor_frame);
        impl->libdecor_frame = NULL;
    }
    if(impl->libdecor_context) {
        libdecor_unref(impl->libdecor_context);
        impl->libdecor_context = NULL;
    }
#endif
    if(impl->toplevel_decoration) {
        zxdg_toplevel_decoration_v1_destroy(impl->toplevel_decoration);
        impl->toplevel_decoration = NULL;
    }
    if(impl->decoration_manager) {
        zxdg_decoration_manager_v1_destroy(impl->decoration_manager);
        impl->decoration_manager = NULL;
    }
    if(impl->xdg_toplevel) {
        xdg_toplevel_destroy(impl->xdg_toplevel);
        impl->xdg_toplevel = NULL;
    }
    if(impl->xdg_surface) {
        xdg_surface_destroy(impl->xdg_surface);
        impl->xdg_surface = NULL;
    }
    if(impl->layer_surface) {
        zwlr_layer_surface_v1_destroy(impl->layer_surface);
        impl->layer_surface = NULL;
    }
    if(impl->layer_shell) {
        zwlr_layer_shell_v1_destroy(impl->layer_shell);
        impl->layer_shell = NULL;
    }
    if(impl->fractional_scale_manager) {
        wp_fractional_scale_manager_v1_destroy(impl->fractional_scale_manager);
        impl->fractional_scale_manager = NULL;
    }
    if(impl->viewporter) {
        wp_viewporter_destroy(impl->viewporter);
        impl->viewporter = NULL;
    }
    if(impl->cursor_shape_manager) {
        wp_cursor_shape_manager_v1_destroy(impl->cursor_shape_manager);
        impl->cursor_shape_manager = NULL;
    }
    if(impl->shell_surface) {
        wl_shell_surface_destroy(impl->shell_surface);
        impl->shell_surface = NULL;
    }
    /* Note: g_clipboard (data_device_manager, data_device, data_source,
       pending/current data_offers, clipboard_data) is intentionally NOT
       destroyed here. It outlives the window for the wl_display's lifetime —
       see the mgl_wayland_clipboard_state comment for the rationale (avoids
       client/server id-space desync that protocol-errors a new data_device
       on the next window cycle). */

    /* cursor_viewport references cursor_surface — destroy it first. */
    if(impl->cursor_viewport) {
        wp_viewport_destroy(impl->cursor_viewport);
        impl->cursor_viewport = NULL;
    }
    if(impl->cursor_surface) {
        wl_surface_destroy(impl->cursor_surface);
        impl->cursor_surface = NULL;
    }
    if(impl->cursor_theme) {
        wl_cursor_theme_destroy(impl->cursor_theme);
        impl->cursor_theme = NULL;
    }
    impl->cursor_theme_pixel_size = 0;
    if(impl->xkb_state) {
        xkb_state_unref(impl->xkb_state);
        impl->xkb_state = NULL;
    }
    if(impl->xkb_keymap) {
        xkb_keymap_unref(impl->xkb_keymap);
        impl->xkb_keymap = NULL;
    }
    if(impl->xkb_context) {
        xkb_context_unref(impl->xkb_context);
        impl->xkb_context = NULL;
    }
    if(impl->pointer) {
        wl_pointer_release(impl->pointer);
        impl->pointer = NULL;
    }
    if(impl->keyboard) {
        wl_keyboard_release(impl->keyboard);
        impl->keyboard = NULL;
    }
    if(impl->seat) {
        wl_seat_destroy(impl->seat);
        impl->seat = NULL;
    }

    for(int i = 0; i < impl->num_outputs; ++i) {
        if(impl->outputs[i].output) {
            wl_output_destroy(impl->outputs[i].output);
            impl->outputs[i].output = NULL;
        }
    }
    impl->num_outputs = 0;

    if(impl->shm) {
        wl_shm_destroy(impl->shm);
        impl->shm = NULL;
    }
    if(impl->shell) {
        wl_shell_destroy(impl->shell);
        impl->shell = NULL;
    }
    if(impl->xdg_wm_base) {
        xdg_wm_base_destroy(impl->xdg_wm_base);
        impl->xdg_wm_base = NULL;
    }
    if(impl->compositor) {
        wl_compositor_destroy(impl->compositor);
        impl->compositor = NULL;
    }
    if(impl->registry) {
        wl_registry_destroy(impl->registry);
        impl->registry = NULL;
    }

    for(int i = 0; i < self->num_monitors; ++i) {
        if(self->monitors[i].name) {
            free((char*)self->monitors[i].name);
            self->monitors[i].name = NULL;
        }
    }
    self->num_monitors = 0;

    mgl_context *context = mgl_get_context();
    if(context->current_window == self)
        context->current_window = NULL;

    free(self->impl);
    self->impl = NULL;
}
