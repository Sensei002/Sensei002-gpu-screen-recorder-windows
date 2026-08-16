#include "../include/mgl/mgl.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#ifdef MGL_WAYLAND
#include <wayland-client.h>
#endif

static mgl_context context;
static int init_count = 0;
static XErrorHandler prev_xerror = NULL;
static XIOErrorHandler prev_xioerror = NULL;
static bool connected_to_display_server = false;
/* True when context.connection was provided by the caller (via
   mgl_init_with_wayland_display). Skip wl_display_disconnect on deinit. */
static bool connection_is_borrowed = false;

static int mgl_x_error_handler(Display *display, XErrorEvent *ee) {
    (void)display;
    (void)ee;
    return 0;
}

static int mgl_x_io_error_handler(Display *display) {
    (void)display;
    /* TODO: Do something equivalent for wayland */
    connected_to_display_server = false;
    return 0;
}

static bool xrender_is_supported(Display *display, int *event_base, int *error_base) {
    *event_base = 0;
    *error_base = 0;
    if(!XRenderQueryExtension(display, event_base, error_base))
        return false;

    int major_version = 0;
    int minor_version = 0;
    if(!XRenderQueryVersion(display, &major_version, &minor_version))
        return false;

    return major_version > 0 || (major_version == 0 && minor_version >= 7);
}

static bool xrandr_is_supported(Display *display, int *event_base, int *error_base) {
    *event_base = 0;
    *error_base = 0;
    if(!XRRQueryExtension(display, event_base, error_base))
        return false;

    int major_version = 0;
    int minor_version = 0;
    if(!XRRQueryVersion(display, &major_version, &minor_version))
        return false;

    return major_version > 1 || (major_version == 1 && minor_version >= 2);
}

static bool is_xwayland(Display *dpy) {
    int opcode, event, error;
    return XQueryExtension(dpy, "XWAYLAND", &opcode, &event, &error);
}

static int mgl_init_x11(void) {
    if(!context.connection) {
        context.connection = XOpenDisplay(NULL);
        if(!context.connection) {
            fprintf(stderr, "mgl error: mgl_init_x11: failed to connect to the X11 server\n");
            mgl_deinit();
            return -1;
        }
    }
    connected_to_display_server = true;
    /* If we dont call we will never get a MappingNotify until a key has been pressed */
    XKeysymToKeycode(context.connection, XK_F1);

    prev_xerror = XSetErrorHandler(mgl_x_error_handler);
    prev_xioerror = XSetIOErrorHandler(mgl_x_io_error_handler);

    context.display_server_is_wayland = is_xwayland(context.connection);

    if(!xrender_is_supported(context.connection, &context.render_event_base, &context.render_error_base)) {
        fprintf(stderr, "mgl error: mgl_init_x11: x11 render extension is not supported by your X server\n");
        mgl_deinit();
        return -1;
    }

    if(!xrandr_is_supported(context.connection, &context.randr_event_base, &context.randr_error_base)) {
        fprintf(stderr, "mgl error: mgl_init_x11: x11 randr extension is not supported by your X server\n");
        mgl_deinit();
        return -1;
    }

    XRRSelectInput(context.connection, DefaultRootWindow(context.connection), RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);

    XInitThreads();
    XkbSetDetectableAutoRepeat(context.connection, True, NULL);

    context.wm_delete_window_atom = XInternAtom(context.connection, "WM_DELETE_WINDOW", False);
    context.net_wm_ping_atom = XInternAtom(context.connection, "_NET_WM_PING", False);
    context.net_wm_pid_atom = XInternAtom(context.connection, "_NET_WM_PID", False);
    return 0;
}

#ifdef MGL_WAYLAND
static int mgl_init_wayland(void) {
    context.connection = wl_display_connect(NULL);
    if(!context.connection) {
        fprintf(stderr, "mgl error: mgl_init_wayland: failed to connect to the Wayland server\n");
        mgl_deinit();
        return -1;
    }
    connected_to_display_server = true;
    context.display_server_is_wayland = true;
    return 0;
}
#endif

static int mgl_init_native(void) {
#ifdef MGL_WAYLAND
    context.connection = XOpenDisplay(NULL);
    if(context.connection) {
        context.display_server_is_wayland = is_xwayland(context.connection);
        if(context.display_server_is_wayland) {
            XCloseDisplay(context.connection);
            context.connection = NULL;
        }
    } else {
        context.display_server_is_wayland = true;
    }

    if(context.display_server_is_wayland) {
        context.window_system = MGL_WINDOW_SYSTEM_WAYLAND;
        if(mgl_init_wayland() != 0)
            return -1;
    } else {
        context.window_system = MGL_WINDOW_SYSTEM_X11;
        if(mgl_init_x11() != 0)
            return -1;
    }
#else
    context.connection = XOpenDisplay(NULL);
    if(context.connection)
        context.display_server_is_wayland = is_xwayland(context.connection);
    else
        context.display_server_is_wayland = true;

    context.window_system = MGL_WINDOW_SYSTEM_X11;
    if(mgl_init_x11() != 0)
        return -1;
#endif
    return 0;
}

#ifdef MGL_WAYLAND
int mgl_init_with_wayland_display(struct wl_display *dpy) {
    if(!dpy) {
        fprintf(stderr, "mgl error: mgl_init_with_wayland_display: dpy is NULL\n");
        return -1;
    }
    ++init_count;
    if(init_count == 1) {
        setenv("__GL_MaxFramesAllowed", "1", true);
        memset(&context, 0, sizeof(context));
        context.window_system = MGL_WINDOW_SYSTEM_WAYLAND;
        context.connection = (mgl_connection)dpy;
        context.display_server_is_wayland = true;
        connected_to_display_server = true;
        connection_is_borrowed = true;

        if(mgl_gl_load(&context.gl) != 0) {
            mgl_deinit();
            return -1;
        }
    }
    return 0;
}
#else
int mgl_init_with_wayland_display(struct wl_display *dpy) {
    (void)dpy;
    fprintf(stderr, "mgl error: mgl_init_with_wayland_display: mgl was built without Wayland support\n");
    return -1;
}
#endif

int mgl_init(mgl_window_system window_system) {
    ++init_count;
    if(init_count == 1) {
        setenv("__GL_MaxFramesAllowed", "1", true);
        memset(&context, 0, sizeof(context));
        context.window_system = window_system;
        connection_is_borrowed = false;

        switch(window_system) {
            case MGL_WINDOW_SYSTEM_NATIVE: {
                if(mgl_init_native() != 0)
                    return -1;
                break;
            }
            case MGL_WINDOW_SYSTEM_X11: {
                if(mgl_init_x11() != 0)
                    return -1;
                break;
            }
            case MGL_WINDOW_SYSTEM_WAYLAND: {
#ifdef MGL_WAYLAND
                if(mgl_init_wayland() != 0)
                    return -1;
#else
                fprintf(stderr, "mgl error: mgl_init: init called with MGL_WINDOW_SYSTEM_WAYLAND, but mgl was built without Wayland support\n");
                return -1;
#endif
                break;
            }
        }

        if(mgl_gl_load(&context.gl) != 0) {
            mgl_deinit();
            return -1;
        }
    }
    return 0;
}

static void mgl_deinit_x11(void) {
    if(context.connection) {
        XCloseDisplay(context.connection);
        context.connection = NULL;
        connected_to_display_server = false;
    }

    if(prev_xioerror) {
        XSetIOErrorHandler(prev_xioerror);
        prev_xioerror = NULL;
    }

    if(prev_xerror) {
        XSetErrorHandler(prev_xerror);
        prev_xerror = NULL;
    }
}

#ifdef MGL_WAYLAND
static void mgl_deinit_wayland(void) {
    if(context.connection) {
        /* Only disconnect if we own the connection. When the connection was
           supplied by the caller via mgl_init_with_wayland_display, the
           caller is responsible for wl_display_disconnect. */
        if(!connection_is_borrowed)
            wl_display_disconnect(context.connection);
        context.connection = NULL;
        connected_to_display_server = false;
    }
    connection_is_borrowed = false;
}
#endif

void mgl_deinit(void) {
    if(init_count == 1) {
        switch(context.window_system) {
            case MGL_WINDOW_SYSTEM_NATIVE:
                assert(false);
                break;
            case MGL_WINDOW_SYSTEM_X11: {
                mgl_deinit_x11();
                break;
            }
            case MGL_WINDOW_SYSTEM_WAYLAND: {
#ifdef MGL_WAYLAND
                mgl_deinit_wayland();
#endif
                break;
            }
        }

        mgl_gl_unload(&context.gl);
        context.current_window = NULL;
    }

    if(init_count > 0)
        --init_count;
}

mgl_context* mgl_get_context(void) {
#ifndef NDEBUG
    if(init_count == 0) {
        fprintf(stderr, "mgl error: mgl_get_context was called before mgl_init\n");
        abort();
    }
#endif
    return &context;
}

bool mgl_is_connected_to_display_server(void) {
    return connected_to_display_server;
}

void mgl_ping_display_server(void) {
    if(!context.connection)
        return;

    if(context.window_system == MGL_WINDOW_SYSTEM_X11) {
        XNoOp(context.connection);
        XFlush(context.connection);
        return;
    }

#ifdef MGL_WAYLAND
    if(context.window_system == MGL_WINDOW_SYSTEM_WAYLAND) {
        /* wl_display_flush returns -1 with errno set when the connection is
           broken (e.g. EPIPE / EBADF). wl_display_get_error returns non-zero
           if libwayland has marked the connection fatal (protocol error from
           the compositor, or an IO error already detected). */
        if(wl_display_flush(context.connection) == -1) {
            if(errno != EAGAIN)
                connected_to_display_server = false;
        }
        if(wl_display_get_error(context.connection) != 0)
            connected_to_display_server = false;
    }
#endif
}
