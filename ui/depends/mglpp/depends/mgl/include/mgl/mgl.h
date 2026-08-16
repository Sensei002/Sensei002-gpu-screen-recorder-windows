#ifndef MGL_MGL_H
#define MGL_MGL_H

#include "gl.h"
#include <stdbool.h>

/* Display* on x11, struct wl_display* on Wayland. */
typedef void* mgl_connection;
typedef struct mgl_context mgl_context;
typedef struct mgl_window mgl_window;

struct wl_display;

typedef enum {
    MGL_WINDOW_SYSTEM_NATIVE,  /* Use X11 on X11 and Wayland on Wayland (Win32 on Windows) */
    MGL_WINDOW_SYSTEM_X11,     /* Use X11 on X11 and XWayland on Wayland */
    MGL_WINDOW_SYSTEM_WAYLAND, /* Use Wayland. If user runs on X11 then it fails to connect */
    MGL_WINDOW_SYSTEM_WIN32,   /* Win32 (Windows). Only available when built on Windows */
} mgl_window_system;

struct mgl_context {
    bool display_server_is_wayland;
    mgl_window_system window_system; /* Window system requested with mgl_init */
    mgl_connection connection;
    mgl_gl gl;
    mgl_window *current_window;

    unsigned long wm_delete_window_atom;
    unsigned long net_wm_ping_atom;
    unsigned long net_wm_pid_atom;

    int render_event_base;
    int render_error_base;

    int randr_event_base;
    int randr_error_base;
};

/*
    Safe to call multiple times, but will only be initialized the first time called.
    Returns non-0 value on failure.
    Note: not thread safe.
*/
int mgl_init(mgl_window_system window_system);

/*
    Initialize mgl using an existing, externally-owned Wayland display. The
    caller retains ownership and is responsible for keeping |dpy| alive until
    after the matching mgl_deinit, and for calling wl_display_disconnect
    itself afterwards. mgl will not call wl_display_disconnect on this
    connection.

    The window system is set to MGL_WINDOW_SYSTEM_WAYLAND. Only valid when
    mgl was built with Wayland support.

    Subsequent calls (init_count > 1) ignore |dpy| (the display is already
    borrowed from the first call).

    Returns non-0 on failure.
*/
int mgl_init_with_wayland_display(struct wl_display *dpy);

/*
    Safe to call multiple times, but will only be deinitialized the last time called.
    Note: not thread safe.
*/
void mgl_deinit(void);

mgl_context* mgl_get_context(void);
/* Returns true is mgl_init has been setup successfully and the connection to the display server hasn't been severed */
bool mgl_is_connected_to_display_server(void);
/*
    This can be used when no window has been created to update the connection status to the display server.
    If the connection to the display server has been severed and mgl_ping_display_server has been called then
    |mgl_is_connected_to_display_server| will return false.
*/
void mgl_ping_display_server(void);

#endif /* MGL_MGL_H */
