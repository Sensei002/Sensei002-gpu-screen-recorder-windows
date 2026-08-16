#ifndef MGL_WINDOW_H
#define MGL_WINDOW_H

#include "../graphics/color.h"
#include "../graphics/backend/graphics.h"
#include "../system/vec.h"
#include "../system/clock.h"
#include "key.h"
#include "mouse_button.h"
#include <stdbool.h>
#include <stddef.h>

#define MGL_MAX_MONITORS 12

/* Vsync is automatically set for created windows, if supported by the system */

typedef union _XEvent XEvent;
typedef struct mgl_event mgl_event;
/* x11/wayland window handle */
typedef void* mgl_window_handle;

typedef void* mgl_connection;
typedef struct mgl_window mgl_window;

typedef struct {
    mgl_vec2i position;
    mgl_vec2i size;
} mgl_view;

typedef struct {
    mgl_vec2i position;
    mgl_vec2i size;
} mgl_scissor;

typedef struct {
    int id; /* output id */
    int crtc_id;
    const char *name;
    mgl_vec2i pos;
    mgl_vec2i size;
    int refresh_rate;
} mgl_monitor;

typedef enum {
    MGL_WINDOW_TYPE_NORMAL,
    MGL_WINDOW_TYPE_DIALOG,       /* Also sets the window as always on top */
    MGL_WINDOW_TYPE_NOTIFICATION, /* Also sets the window as always on top */
    /*
        On Wayland sessions where wlr-layer-shell is supported, this maps to a
        zwlr_layer_surface_v1 on the OVERLAY layer. On X11 it's treated like a
        normal window with override_redirect; the caller is expected to enable
        that flag separately if it wants override-redirect behavior.
        See mgl_layer_shell_options to control anchors, margins, keyboard
        interactivity, and exclusive zone.
    */
    MGL_WINDOW_TYPE_OVERLAY
} mgl_window_type;

typedef enum {
    MGL_LAYER_SHELL_ANCHOR_TOP    = 1 << 0,
    MGL_LAYER_SHELL_ANCHOR_BOTTOM = 1 << 1,
    MGL_LAYER_SHELL_ANCHOR_LEFT   = 1 << 2,
    MGL_LAYER_SHELL_ANCHOR_RIGHT  = 1 << 3,
} mgl_layer_shell_anchor_flag;

typedef enum {
    /* Use the mgl default (ON_DEMAND). */
    MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_DEFAULT = 0,
    /* Surface receives no keyboard events. */
    MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_NONE,
    /* Keyboard focus is granted while the pointer is over the surface. */
    MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_ON_DEMAND,
    /* Surface grabs keyboard focus globally (modal). */
    MGL_LAYER_SHELL_KEYBOARD_INTERACTIVITY_EXCLUSIVE,
} mgl_layer_shell_keyboard_interactivity;

typedef struct {
    /*
        Bitmask of mgl_layer_shell_anchor_flag. 0 means "anchor to all four
        edges", which combined with create_params.size = (0, 0) covers the
        whole output. Examples:
            anchor = TOP | RIGHT  - top-right corner; size from create_params is honored.
            anchor = TOP | BOTTOM | LEFT | RIGHT (or 0) - cover whole output.
    */
    uint32_t anchor;

    mgl_layer_shell_keyboard_interactivity keyboard_interactivity;

    /*
        Distance in logical pixels from the corresponding anchored edge.
        Only the margins for edges that are actually anchored have an effect.
    */
    int32_t margin_top;
    int32_t margin_right;
    int32_t margin_bottom;
    int32_t margin_left;

    /*
        Exclusive zone in logical pixels along the anchored edge.
            0 (default) - mgl uses -1 (ignore other surfaces' exclusive zones).
                          This is what an overlay covering the whole output wants.
            > 0         - reserve that many pixels along the anchored edge so
                          other surfaces (e.g. fullscreen apps) avoid the area.
            -1          - explicitly request "ignore other zones".
    */
    int32_t exclusive_zone;

    /*
        Pin the layer surface to a specific monitor. Use the connector name
        reported by wl_output (e.g. "DP-1", "HDMI-A-1", "eDP-1"). Match against
        the |name| field of mgl_monitor.

        NULL or empty (default) - the compositor picks an output, typically the
                                  one the layer surface is opened from (the
                                  focused output on Hyprland/niri/sway/river).

        If a name is set but no matching output is found at create time, mgl
        falls back to letting the compositor choose (logs a warning).
    */
    const char *output_name;
} mgl_layer_shell_options;

typedef enum {
    MGL_CLIPBOARD_TYPE_STRING       = 1 << 0,
    MGL_CLIPBOARD_TYPE_IMAGE_PNG    = 1 << 1,
    MGL_CLIPBOARD_TYPE_IMAGE_JPG    = 1 << 2,
    MGL_CLIPBOARD_TYPE_IMAGE_GIF    = 1 << 3,
} mgl_clipboard_type;

#define MGL_CLIPBOARD_TYPE_ALL 0xFFFFFFFF
#define MGL_CLIPBOARD_TYPE_IMAGE (MGL_CLIPBOARD_TYPE_IMAGE_PNG | MGL_CLIPBOARD_TYPE_IMAGE_JPG | MGL_CLIPBOARD_TYPE_IMAGE_GIF)

/*
    Return true to continue. |mgl_window_get_clipboard| returns false if this returns false.
    Note: |size| is the size of the current data, not the total data (if the callback only contains a part of the data).
*/
typedef bool (*mgl_clipboard_callback)(const unsigned char *data, size_t size, mgl_clipboard_type clipboard_type, void *userdata);
typedef void (*mgl_active_monitor_callback)(const mgl_monitor *monitor, void *userdata);

struct mgl_window {
    mgl_window_handle (*get_system_handle)(const mgl_window *self);
    void  (*deinit)(mgl_window *self);
    void  (*close)(mgl_window *self);
    bool  (*poll_event)(mgl_window *self, mgl_event *event);
    bool  (*inject_x11_event)(mgl_window *self, XEvent *xev, mgl_event *event); /* Optional */
    void  (*swap_buffers)(mgl_window *self);
    void  (*set_visible)(mgl_window *self, bool visible);
    bool  (*is_key_pressed)(const mgl_window *self, mgl_key key);
    bool  (*is_mouse_button_pressed)(const mgl_window *self, mgl_mouse_button button);
    void  (*set_title)(mgl_window *self, const char *title);
    void  (*set_cursor_visible)(mgl_window *self, bool visible);
    void  (*set_vsync_enabled)(mgl_window *self, bool enabled);
    bool  (*is_vsync_enabled)(const mgl_window *self);
    void  (*set_fullscreen)(mgl_window *self, bool fullscreen);
    bool  (*is_fullscreen)(const mgl_window *self);
    void  (*set_position)(mgl_window *self, mgl_vec2i position);
    void  (*set_size)(mgl_window *self, mgl_vec2i size);
    void  (*set_size_limits)(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum);
    void  (*set_clipboard)(mgl_window *self, const char *str, size_t size);
    bool  (*get_clipboard)(mgl_window *self, mgl_clipboard_callback callback, void *userdata, uint32_t clipboard_types);
    void  (*set_key_repeat_enabled)(mgl_window *self, bool enabled);
    void  (*flush)(mgl_window *self);
    void* (*get_egl_display)(mgl_window *self);
    void* (*get_egl_context)(mgl_window *self);
    void  (*for_each_active_monitor_output)(mgl_window *self, mgl_active_monitor_callback callback, void *userdata);

    void *impl;

    bool vsync_enabled; /* true by default */
    bool low_latency; /* false by default */
    bool open;
    bool focused;
    bool key_repeat_enabled; /* true by default */
    double frame_time_limit;
    double frame_time_limit_monitor;
    mgl_clock frame_timer;
    mgl_vec2i pos;
    mgl_vec2i size;
    /* relative to the top left of the window. only updates when the cursor is inside the window */
    mgl_vec2i cursor_position;
    mgl_view view;
    mgl_scissor scissor;
    /* This only contains connected and active monitors */
    mgl_monitor monitors[MGL_MAX_MONITORS];
    int num_monitors;
};

/* TODO: Some of these parameters only apply to new window */
typedef struct {
    mgl_vec2i position;
    mgl_vec2i size;
    mgl_vec2i min_size;                      /* (0, 0) = no limit */
    mgl_vec2i max_size;                      /* (0, 0) = no limit */
    mgl_window_handle parent_window;         /* 0 = root window */
    bool hidden;                             /* false by default */
    bool override_redirect;                  /* false by default */
    bool support_alpha;                      /* support alpha for the window, false by default */
    bool hide_decorations;                   /* this is a hint, it may be ignored by the window manager, false by default */
    mgl_color background_color;              /* default: black */
    const char *class_name;                  /* Class name on X11, App ID on Wayland */
    mgl_window_type window_type;             /* default: normal */
    mgl_window_handle transient_for_window;  /* 0 = none */
    mgl_graphics_api graphics_api;           /* Can only be MGL_GRAPHICS_API_GLX in an X11 window. default: MGL_GRAPHICS_API_EGL */
    bool request_depth_buffer;               /* default: false */
    bool request_stencil_buffer;             /* default: false */
    /* Only used when window_type == MGL_WINDOW_TYPE_OVERLAY on Wayland. */
    mgl_layer_shell_options layer_shell_options;
} mgl_window_create_params;

/* |params| can be NULL. Note: vsync is enabled by default */
int mgl_window_create(mgl_window *self, const char *title, const mgl_window_create_params *params);
int mgl_window_init_from_existing_window(mgl_window *self, mgl_window_handle existing_window);
void mgl_window_deinit(mgl_window *self);

void mgl_window_clear(mgl_window *self, mgl_color color);
bool mgl_window_poll_event(mgl_window *self, mgl_event *event);
bool mgl_window_inject_x11_event(mgl_window *self, XEvent *xev, mgl_event *event);
void mgl_window_display(mgl_window *self);

/*
    This should be called every frame to retain the view.
    Make sure to set the view back to the previous view after rendering items
    by saving the previous view with |mgl_window_get_view| and then call
    |mgl_window_set_view| with that saved view.
    The view is set to the window size when the window is resized (window resize event).
*/
void mgl_window_set_view(mgl_window *self, mgl_view *new_view);
void mgl_window_get_view(mgl_window *self, mgl_view *view);

/*
    This should be called every frame to retain the scissor.
    Make sure to set the scissor back to the previous view after rendering items
    by saving the previous scissor with |mgl_window_get_scissor| and then call
    |mgl_window_set_scissor| with that saved scissor.
    The scissor is set to the window size when the window is resized (window resize event).
*/
void mgl_window_set_scissor(mgl_window *self, const mgl_scissor *new_scissor);
void mgl_window_get_scissor(mgl_window *self, mgl_scissor *scissor);

void mgl_window_set_visible(mgl_window *self, bool visible);
bool mgl_window_is_open(const mgl_window *self);
bool mgl_window_has_focus(const mgl_window *self);
bool mgl_window_is_key_pressed(const mgl_window *self, mgl_key key);
bool mgl_window_is_mouse_button_pressed(const mgl_window *self, mgl_mouse_button button);

/* Returns 0 if none is available */
mgl_window_handle mgl_window_get_system_handle(const mgl_window *self);
void mgl_window_close(mgl_window *self);
void mgl_window_set_title(mgl_window *self, const char *title);
void mgl_window_set_cursor_visible(mgl_window *self, bool visible);
/* 0 = no fps limit, or limit fps to vsync if vsync is enabled */
void mgl_window_set_framerate_limit(mgl_window *self, int fps);
void mgl_window_set_vsync_enabled(mgl_window *self, bool enabled);
bool mgl_window_is_vsync_enabled(const mgl_window *self);
void mgl_window_set_fullscreen(mgl_window *self, bool fullscreen);
bool mgl_window_is_fullscreen(const mgl_window *self);
/* Enabling low latency may slightly increase cpu usage */
void mgl_window_set_low_latency(mgl_window *self, bool low_latency);
bool mgl_window_is_low_latency_enabled(const mgl_window *self);

/* This also flushes the X11 data so the position is changed as soon as possible */
void mgl_window_set_position(mgl_window *self, mgl_vec2i position);
/*
    Note that window size in mgl_window doesn't update immediately as the change size request can be ignored by the window manager. A MGL_EVENT_RESIZED event will be received if the resize occurred.
    This also flushes the X11 data so the size is changed as soon as possible.
*/
void mgl_window_set_size(mgl_window *self, mgl_vec2i size);
/*
    If |minimum| is (0, 0) then there is no minimum limit, if |maximum| is (0, 0) then there is no maximum limit.
    This also flushes the X11 data so the size is changed as soon as possible.
*/
void mgl_window_set_size_limits(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum);

void mgl_window_set_clipboard(mgl_window *self, const char *str, size_t size);
/* clipboard_types should be a bit-or of mgl_clipboard_type */
bool mgl_window_get_clipboard(mgl_window *self, mgl_clipboard_callback callback, void *userdata, uint32_t clipboard_types);
/*
    A new string is allocated and the pointer is copied to |str| with the size returned in |size|.
    |str| should be deallocated with |free| by the user.
    This function returns false if there is nothing to copy, or if the clipboard
    contains clipboard data that is not a string or if it fails to copy the data
    for any other reason.
    Note: The string is not null terminated.
*/
bool mgl_window_get_clipboard_string(mgl_window *self, char **str, size_t *size);
void mgl_window_set_key_repeat_enabled(mgl_window *self, bool enabled);

void mgl_window_flush(mgl_window *self);

void* mgl_window_get_egl_display(mgl_window *self);
void* mgl_window_get_egl_context(mgl_window *self);

void mgl_window_for_each_active_monitor_output(mgl_window *self, mgl_active_monitor_callback callback, void *userdata);

#endif /* MGL_WINDOW_H */
