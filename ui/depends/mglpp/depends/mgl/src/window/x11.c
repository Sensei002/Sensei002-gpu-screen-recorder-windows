#include "../../include/mgl/window/x11.h"
#include "../../include/mgl/window/event.h"
#include "../../include/mgl/mgl.h"
#include "../../include/mgl/system/utf8.h"
#include "../../include/mgl/system/clock.h"

#ifndef __USE_POSIX
#define __USE_POSIX
#endif

#ifdef __GNU__
#define PATH_MAX 4096
#define HOST_NAME_MAX 256
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <unistd.h>

#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>
#include <X11/XF86keysym.h>

static void mgl_window_x11_deinit(mgl_window *self);

/* TODO: Handle XIM better. Set XIM position to text position on screen (for text input) and reset input when selecting a new text input, etc */
/* TODO: Separate events from windows. Especially when it comes to monitor events */

/* Should be in range [2,] */
#define MAX_STACKED_EVENTS 32

typedef struct {
    mgl_event stack[MAX_STACKED_EVENTS];
    int start;
    int end;
    int size;
} x11_events_circular_buffer;

typedef struct {
    Window window;
    char *clipboard_string;
    size_t clipboard_size;

    mgl_graphics graphics;
    Colormap color_map;
    XIM xim;
    XIC xic;
    Atom clipboard_atom;
    Atom targets_atom;
    Atom text_atom;
    Atom utf8_string_atom;
    Atom image_png_atom;
    Atom image_jpg_atom;
    Atom image_jpeg_atom;
    Atom image_gif_atom;
    Atom incr_atom;
    Atom net_wm_state_atom;
    Atom net_wm_state_fullscreen_atom;
    Atom net_wm_state_above_atom;
    Atom net_wm_name_atom;
    Atom net_wm_window_type_atom;
    Atom net_wm_window_type_normal_atom;
    Atom net_wm_window_type_dialog_atom;
    Atom net_wm_window_type_notification_atom;
    Atom net_wm_window_type_utility;
    Atom motif_wm_hints_atom;
    Cursor default_cursor;
    Cursor invisible_cursor;
    unsigned int prev_keycode_pressed;
    bool key_was_released;
    bool support_alpha;

    XEvent xev;

    /*
        Used to stack text event on top of key press/release events and other text events.
        For example pressing a key should give the user both key press and text events
        and for IM with multiple characters entered (such as chinese), we want to trigger
        an event for all of them.
    */
    x11_events_circular_buffer events;
} mgl_window_x11;

static void x11_events_circular_buffer_init(x11_events_circular_buffer *self) {
    self->start = 0;
    self->end = 0;
    self->size = 0;
}

static bool x11_events_circular_buffer_append(x11_events_circular_buffer *self, const mgl_event *event) {
    if(self->size == MAX_STACKED_EVENTS)
        return false;

    self->stack[self->end] = *event;
    self->end = (self->end + 1) % MAX_STACKED_EVENTS;
    ++self->size;
    return true;
}

static bool x11_events_circular_buffer_pop(x11_events_circular_buffer *self, mgl_event *event) {
    if(self->size == 0)
        return false;

    *event = self->stack[self->start];
    self->start = (self->start + 1) % MAX_STACKED_EVENTS;
    --self->size;
    return true;
}

static void mgl_window_x11_clear_monitors(mgl_window *self) {
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(monitor->name) {
            free((char*)monitor->name);
            monitor->name = NULL;
        }
    }
    self->num_monitors = 0;
}

static bool mgl_window_x11_append_event(mgl_window_x11 *self, const mgl_event *event) {
    return x11_events_circular_buffer_append(&self->events, event);
}

static bool mgl_window_x11_pop_event(mgl_window_x11 *self, mgl_event *event) {
    return x11_events_circular_buffer_pop(&self->events, event);
}

static mgl_monitor* mgl_window_x11_add_monitor(mgl_window *self, RROutput output_id, RRCrtc crtc_id, const char *name, mgl_vec2i pos, mgl_vec2i size, int refresh_rate) {
    if(self->num_monitors == MGL_MAX_MONITORS)
        return NULL;

    mgl_monitor *monitor = &self->monitors[self->num_monitors];
    monitor->id = output_id;
    monitor->crtc_id = crtc_id;
    monitor->name = strdup(name);
    if(!monitor->name)
        return NULL;
    monitor->pos = pos;
    monitor->size = size;
    monitor->refresh_rate = refresh_rate;
    self->num_monitors++;

    return monitor;
}

static mgl_monitor* mgl_window_x11_get_monitor_by_id(mgl_window *self, RROutput output_id) {
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(monitor->id == (int)output_id)
            return monitor;
    }
    return NULL;
}

static mgl_monitor* mgl_window_x11_get_monitor_by_crtc_id(mgl_window *self, RRCrtc crtc_id) {
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(monitor->crtc_id == (int)crtc_id)
            return monitor;
    }
    return NULL;
}

static bool mgl_window_x11_remove_monitor(mgl_window *self, RROutput output_id, mgl_event *event) {
    int index_to_remove = -1;
    for(int i = 0; i < self->num_monitors; ++i) {
        mgl_monitor *monitor = &self->monitors[i];
        if(monitor->id == (int)output_id) {
            index_to_remove = i;
            break;
        }
    }

    if(index_to_remove == -1)
        return false;

    mgl_monitor *monitor = &self->monitors[index_to_remove];
    free((char*)monitor->name);
    monitor->name = NULL;

    for(int i = index_to_remove + 1; i < self->num_monitors; ++i) {
        self->monitors[i - 1] = self->monitors[i];
    }
    self->num_monitors--;

    event->monitor_disconnected.id = output_id;
    event->type = MGL_EVENT_MONITOR_DISCONNECTED;
    return true;
}

static int round_int(double value) {
    return value + 0.5;
}

static int monitor_info_get_framerate(const XRRModeInfo *mode_info) {
    double v_total = mode_info->vTotal;
    if(mode_info->modeFlags & RR_DoubleScan) {
        v_total *= 2;
    }

    if(mode_info->modeFlags & RR_Interlace) {
        v_total /= 2;
    }

    if(mode_info->hTotal > 0 && v_total > 0.0001) {
        return round_int((double)mode_info->dotClock / ((double)mode_info->hTotal * v_total));
    } else {
        return 0;
    }
}

static const XRRModeInfo* get_mode_info(const XRRScreenResources *sr, RRMode id) {
    for(int i = 0; i < sr->nmode; ++i) {
        if(sr->modes[i].id == id)
            return &sr->modes[i];
    }    
    return NULL;
}

static void mgl_window_x11_for_each_active_monitor_output(mgl_window *self, mgl_active_monitor_callback callback, void *userdata) {
    (void)self;
    mgl_context *context = mgl_get_context();
    XRRScreenResources *screen_res = XRRGetScreenResources(context->connection, DefaultRootWindow(context->connection));
    if(!screen_res)
        return;

    char display_name[256];
    for(int i = 0; i < screen_res->noutput; ++i) {
        XRROutputInfo *out_info = XRRGetOutputInfo(context->connection, screen_res, screen_res->outputs[i]);
        if(out_info && out_info->crtc && out_info->connection == RR_Connected) {
            XRRCrtcInfo *crt_info = XRRGetCrtcInfo(context->connection, screen_res, out_info->crtc);
            if(crt_info && crt_info->mode) {
                const XRRModeInfo *mode_info = get_mode_info(screen_res, crt_info->mode);
                if(mode_info) {
                    snprintf(display_name, sizeof(display_name), "%s", out_info->name);
                    mgl_monitor monitor = {
                        .id = screen_res->outputs[i],
                        .crtc_id = out_info->crtc,
                        .name = display_name,
                        .pos = { .x = crt_info->x, .y = crt_info->y },
                        .size = { .x = (int)crt_info->width, .y = (int)crt_info->height },
                        .refresh_rate = monitor_info_get_framerate(mode_info),
                    };
                    callback(&monitor, userdata);
                }
            }
            if(crt_info)
                XRRFreeCrtcInfo(crt_info);
        }
        if(out_info)
            XRRFreeOutputInfo(out_info);
    }

    XRRFreeScreenResources(screen_res);
}

static void monitor_callback_add_to_mgl_window_x11(const mgl_monitor *monitor, void *userdata) {
    mgl_window *mgl_window = userdata;
    mgl_window_x11_add_monitor(mgl_window, monitor->id, monitor->crtc_id, monitor->name, monitor->pos, monitor->size, monitor->refresh_rate);
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

static void mgl_window_x11_set_frame_time_limit_monitor(mgl_window *self) {
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

static void mgl_window_x11_on_move(mgl_window *self, int x, int y) {
    self->pos.x = x;
    self->pos.y = y;
    mgl_window_x11_set_frame_time_limit_monitor(self);
}

static void mgl_window_x11_on_resize(mgl_window *self, int width, int height) {
    self->size.x = width;
    self->size.y = height;

    mgl_view view;
    view.position = (mgl_vec2i){ 0, 0 };
    view.size = self->size;
    mgl_window_set_view(self, &view);
    mgl_window_set_scissor(self, &(mgl_scissor){ .position = { 0, 0 }, .size = self->size });
}

static unsigned long mgl_color_to_x11_pixel(mgl_color color) {
    if(color.a == 0)
        return 0;
    return ((uint32_t)color.a << 24) | (((uint32_t)color.r * color.a / 0xFF) << 16) | (((uint32_t)color.g * color.a / 0xFF) << 8) | ((uint32_t)color.b * color.a / 0xFF);
}

static void mgl_set_window_type(mgl_window *self, mgl_window_type window_type) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    switch(window_type) {
        case MGL_WINDOW_TYPE_NORMAL: {
            XChangeProperty(context->connection, impl->window, impl->net_wm_window_type_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&impl->net_wm_window_type_normal_atom, 1L);
            break;
        }
        case MGL_WINDOW_TYPE_DIALOG: {
            XChangeProperty(context->connection, impl->window, impl->net_wm_window_type_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&impl->net_wm_window_type_dialog_atom, 1L);
            XChangeProperty(context->connection, impl->window, impl->net_wm_state_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&impl->net_wm_state_above_atom, 1L);
            break;
        }
        case MGL_WINDOW_TYPE_NOTIFICATION: {
            const Atom data[2] = {
                impl->net_wm_window_type_notification_atom,
                impl->net_wm_window_type_utility
            };
            XChangeProperty(context->connection, impl->window, impl->net_wm_window_type_atom, XA_ATOM, 32, PropModeReplace, (const unsigned char*)data, 2L);
            XChangeProperty(context->connection, impl->window, impl->net_wm_state_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)&impl->net_wm_state_above_atom, 1L);
            break;
        }
    }
}

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} MotifHints;

#define MWM_HINTS_DECORATIONS   2

#define MWM_DECOR_NONE          0
#define MWM_DECOR_ALL           1

static void mgl_window_x11_set_decorations_visible(mgl_window *self, bool visible) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();

    MotifHints motif_hints = {0};
    motif_hints.flags = MWM_HINTS_DECORATIONS;
    motif_hints.decorations = visible ? MWM_DECOR_ALL : MWM_DECOR_NONE;

    XChangeProperty(context->connection, impl->window, impl->motif_wm_hints_atom, impl->motif_wm_hints_atom, 32, PropModeReplace, (unsigned char*)&motif_hints, sizeof(motif_hints) / sizeof(long));
}

static void mgl_window_x11_set_title(mgl_window *self, const char *title) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();

    XStoreName(context->connection, impl->window, title);
    XChangeProperty(context->connection, impl->window, impl->net_wm_name_atom, impl->utf8_string_atom, 8, PropModeReplace, (unsigned char*)title, strlen(title));
    XFlush(context->connection);
}

static void mgl_window_x11_set_size_limits(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum) {
    mgl_window_x11 *impl = self->impl;
    XSizeHints *size_hints = XAllocSizeHints();
    if(size_hints) {
        size_hints->width = self->size.x;
        size_hints->min_width = minimum.x;
        size_hints->max_width = maximum.x;

        size_hints->height = self->size.y;
        size_hints->min_height = minimum.y;
        size_hints->max_height = maximum.y;

        size_hints->flags = PSize;
        if(size_hints->min_width || size_hints->min_height)
            size_hints->flags |= PMinSize;
        if(size_hints->max_width || size_hints->max_height)
            size_hints->flags |= PMaxSize;

        mgl_context *context = mgl_get_context();
        XSetWMNormalHints(context->connection, impl->window, size_hints);
        XFree(size_hints);
        XFlush(context->connection);
    } else {
        fprintf(stderr, "mgl warning: failed to set window size hints\n");
    }
}

static bool mgl_x11_setup_window(mgl_window *self, const char *title, const mgl_window_create_params *params, Window existing_window) {
    mgl_window_x11 *impl = self->impl;
    impl->window = 0;
    impl->clipboard_string = NULL;
    impl->clipboard_size = 0;

    mgl_vec2i window_size = params ? params->size : (mgl_vec2i){ 0, 0 };
    if(window_size.x <= 0 || window_size.y <= 0) {
        window_size.x = 640;
        window_size.y = 480;
    }
    self->size = window_size;

    mgl_vec2i window_pos = params ? params->position : (mgl_vec2i){ 0, 0 };
    mgl_context *context = mgl_get_context();

    Window parent_window = params ? (Window)params->parent_window : None;
    if(parent_window == 0)
        parent_window = DefaultRootWindow(context->connection);

    XVisualInfo *visual_info = mgl_graphics_get_xvisual_info(&impl->graphics);
    impl->color_map = XCreateColormap(context->connection, DefaultRootWindow(context->connection), visual_info->visual, AllocNone);
    if(!impl->color_map) {
        fprintf(stderr, "mgl error: mgl_x11_setup_window: XCreateColormap failed\n");
        return false;
    }

    XSetWindowAttributes window_attr;
    window_attr.override_redirect = params ? params->override_redirect : false;
    window_attr.colormap = impl->color_map;
    window_attr.background_pixel = mgl_color_to_x11_pixel(params ? params->background_color : (mgl_color){ .r = 0, .g = 0, .b = 0, .a = 255 });
    window_attr.border_pixel = 0;
    window_attr.bit_gravity = NorthWestGravity;
    window_attr.event_mask = 
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask | ButtonMotionMask |
        StructureNotifyMask | EnterWindowMask | LeaveWindowMask | VisibilityChangeMask | PropertyChangeMask | FocusChangeMask;

    const bool hide_window = params ? params->hidden : false;

    if(existing_window) {
        if(!XChangeWindowAttributes(context->connection, existing_window, CWColormap | CWEventMask | CWOverrideRedirect | CWBorderPixel | CWBackPixel | CWBitGravity, &window_attr)) {
            fprintf(stderr, "mgl error: mgl_x11_setup_window: XChangeWindowAttributes failed\n");
            return false;
        }

        if(params && params->size.x > 0 && params->size.y > 0) {
            XResizeWindow(context->connection, existing_window, params->size.x, params->size.y);
        }

        impl->window = existing_window;

        if(params && params->hide_decorations) {
            mgl_window_x11_set_decorations_visible(self, false);
        }

        if(hide_window)
            XUnmapWindow(context->connection, existing_window);
    } else {
        impl->window = XCreateWindow(context->connection, parent_window, window_pos.x, window_pos.y,
            window_size.x, window_size.y, 0,
            visual_info->depth, InputOutput, visual_info->visual,
            CWColormap | CWEventMask | CWOverrideRedirect | CWBorderPixel | CWBackPixel | CWBitGravity, &window_attr);
        if(!impl->window) {
            fprintf(stderr, "mgl error: mgl_x11_setup_window: XCreateWindow failed\n");
            return false;
        }

        if(params && params->hide_decorations) {
            mgl_window_x11_set_decorations_visible(self, false);
        }

        mgl_window_x11_set_title(self, title);
        if(!hide_window)
            XMapWindow(context->connection, impl->window);
    }

    if(params)
        mgl_window_x11_set_size_limits(self, params->min_size, params->max_size);

    /* TODO: Call XGetWMProtocols and add wm_delete_window_atom on top, to not overwrite existing wm protocol atoms */
    Atom wm_protocol_atoms[2] = {
        context->wm_delete_window_atom,
        context->net_wm_ping_atom
    };
    XSetWMProtocols(context->connection, impl->window, wm_protocol_atoms, 2);

    if(context->net_wm_pid_atom) {
        const long pid = getpid();
        XChangeProperty(context->connection, impl->window, context->net_wm_pid_atom, XA_CARDINAL,
                        32, PropModeReplace, (const unsigned char*)&pid, 1);
    }

    char host_name[HOST_NAME_MAX + 1];
    if(gethostname(host_name, sizeof(host_name)) == 0) {
        XTextProperty txt_prop;
        txt_prop.value = (unsigned char*)host_name;
        txt_prop.encoding = XA_STRING;
        txt_prop.format = 8;
        txt_prop.nitems = strlen(host_name);
        XSetWMClientMachine(context->connection, impl->window, &txt_prop);
    }

    if(params && params->class_name) {
        XClassHint class_hint = { (char*)params->class_name, (char*)params->class_name };
        XSetClassHint(context->connection, impl->window, &class_hint);
    }

    if(params && params->transient_for_window) {
        XSetTransientForHint(context->connection, impl->window, (Window)params->transient_for_window);
    }

    /* TODO: Move this to above XMapWindow? */
    mgl_window_type window_type = params ? params->window_type : MGL_WINDOW_TYPE_NORMAL;
    mgl_set_window_type(self, window_type);

    XFlush(context->connection);

    if(!mgl_graphics_make_context_current(&impl->graphics, (mgl_window_handle)impl->window)) {
        fprintf(stderr, "mgl error: mgl_x11_setup_window: failed to make window context current\n");
        return false;
    }

    self->vsync_enabled = true;
    mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window, self->vsync_enabled);

    Window dummy_w;
    int dummy_i;
    unsigned int dummy_u;
    XQueryPointer(context->connection, impl->window, &dummy_w, &dummy_w, &dummy_i, &dummy_i, &self->cursor_position.x, &self->cursor_position.y, &dummy_u);

    impl->xim = XOpenIM(context->connection, NULL, NULL, NULL);
    if(!impl->xim) {
        fprintf(stderr, "mgl error: mgl_x11_setup_window: XOpenIM failed\n");
        return false;
    }

    impl->xic = XCreateIC(impl->xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, impl->window, NULL);
    if(!impl->xic) {
        fprintf(stderr, "mgl error: mgl_x11_setup_window: XCreateIC failed\n");
        return false;
    }

    // TODO: This should be done once and monitor events should be done once, no matter how many windows you have
    mgl_window_x11_clear_monitors(self);
    mgl_window_x11_for_each_active_monitor_output(self, monitor_callback_add_to_mgl_window_x11, self);

    mgl_window_x11_on_resize(self, self->size.x, self->size.y);
    mgl_window_x11_on_move(self, window_pos.x, window_pos.y);

    self->open = true;
    self->focused = false; /* TODO: Check if we need to call XGetInputFocus for this, or just wait for focus event */
    return true;
}

/* Returns MGL_KEY_UNKNOWN on no match */
static mgl_key x11_keysym_to_mgl_key(KeySym key_sym) {
    if(key_sym >= XK_A && key_sym <= XK_Z)
        return MGL_KEY_A + (key_sym - XK_A);
    /* TODO: Check if this ever happens */
    if(key_sym >= XK_a && key_sym <= XK_z)
        return MGL_KEY_A + (key_sym - XK_a);
    if(key_sym >= XK_0 && key_sym <= XK_9)
        return MGL_KEY_NUM0 + (key_sym - XK_0);
    if(key_sym >= XK_KP_0 && key_sym <= XK_KP_9)
        return MGL_KEY_NUMPAD0 + (key_sym - XK_KP_0);

    /* TODO: Fill in the rest */
    switch(key_sym) {
        case XK_space:                return MGL_KEY_SPACE;
        case XK_BackSpace:            return MGL_KEY_BACKSPACE;
        case XK_Tab:                  return MGL_KEY_TAB;
        case XK_Return:               return MGL_KEY_ENTER;
        case XK_Escape:               return MGL_KEY_ESCAPE;
        case XK_Control_L:            return MGL_KEY_LCONTROL;
        case XK_Shift_L:              return MGL_KEY_LSHIFT;
        case XK_Alt_L:                return MGL_KEY_LALT;
        case XK_Super_L:              return MGL_KEY_LSYSTEM;
        case XK_Control_R:            return MGL_KEY_RCONTROL;
        case XK_Shift_R:              return MGL_KEY_RSHIFT;
        case XK_Alt_R:                return MGL_KEY_RALT;
        case XK_Super_R:              return MGL_KEY_RSYSTEM;
        case XK_Delete:               return MGL_KEY_DELETE;
        case XK_Home:                 return MGL_KEY_HOME;
        case XK_Left:                 return MGL_KEY_LEFT;
        case XK_Up:                   return MGL_KEY_UP;
        case XK_Right:                return MGL_KEY_RIGHT;
        case XK_Down:                 return MGL_KEY_DOWN;
        case XK_Page_Up:              return MGL_KEY_PAGEUP;
        case XK_Page_Down:            return MGL_KEY_PAGEDOWN;
        case XK_End:                  return MGL_KEY_END;
        case XK_F1:                   return MGL_KEY_F1;
        case XK_F2:                   return MGL_KEY_F2;
        case XK_F3:                   return MGL_KEY_F3;
        case XK_F4:                   return MGL_KEY_F4;
        case XK_F5:                   return MGL_KEY_F5;
        case XK_F6:                   return MGL_KEY_F6;
        case XK_F7:                   return MGL_KEY_F7;
        case XK_F8:                   return MGL_KEY_F8;
        case XK_F9:                   return MGL_KEY_F9;
        case XK_F10:                  return MGL_KEY_F10;
        case XK_F11:                  return MGL_KEY_F11;
        case XK_F12:                  return MGL_KEY_F12;
        case XK_F13:                  return MGL_KEY_F13;
        case XK_F14:                  return MGL_KEY_F14;
        case XK_F15:                  return MGL_KEY_F15;
        case XK_Insert:               return MGL_KEY_INSERT;
        case XK_Pause:                return MGL_KEY_PAUSE;
        case XK_Print:                return MGL_KEY_PRINTSCREEN;
        case XK_KP_Insert:            return MGL_KEY_NUMPAD0;
        case XK_KP_End:               return MGL_KEY_NUMPAD1;
        case XK_KP_Down:              return MGL_KEY_NUMPAD2;
        case XK_KP_Page_Down:         return MGL_KEY_NUMPAD3;
        case XK_KP_Left:              return MGL_KEY_NUMPAD4;
        case XK_KP_Begin:             return MGL_KEY_NUMPAD5;
        case XK_KP_Right:             return MGL_KEY_NUMPAD6;
        case XK_KP_Home:              return MGL_KEY_NUMPAD7;
        case XK_KP_Up:                return MGL_KEY_NUMPAD8;
        case XK_KP_Page_Up:           return MGL_KEY_NUMPAD9;
        case XK_KP_Enter:             return MGL_KEY_NUMPAD_ENTER;
        case XF86XK_AudioLowerVolume: return MGL_KEY_AUDIO_LOWER_VOLUME;
        case XF86XK_AudioRaiseVolume: return MGL_KEY_AUDIO_RAISE_VOLUME;
        case XF86XK_AudioPlay:        return MGL_KEY_AUDIO_PLAY;
        case XF86XK_AudioStop:        return MGL_KEY_AUDIO_STOP;
        case XF86XK_AudioPause:       return MGL_KEY_AUDIO_PAUSE;
        case XF86XK_AudioMute:        return MGL_KEY_AUDIO_MUTE;
        case XF86XK_AudioPrev:        return MGL_KEY_AUDIO_PREV;
        case XF86XK_AudioNext:        return MGL_KEY_AUDIO_NEXT;
        case XF86XK_AudioRewind:      return MGL_KEY_AUDIO_REWIND;
        case XF86XK_AudioForward:     return MGL_KEY_AUDIO_FORWARD;
        case XK_dead_acute:           return MGL_KEY_DEAD_ACUTE;
        case XK_apostrophe:           return MGL_KEY_APOSTROPHE;
        case XK_F16:                  return MGL_KEY_F16;
        case XK_F17:                  return MGL_KEY_F17;
        case XK_F18:                  return MGL_KEY_F18;
        case XK_F19:                  return MGL_KEY_F19;
        case XK_F20:                  return MGL_KEY_F20;
        case XK_F21:                  return MGL_KEY_F21;
        case XK_F22:                  return MGL_KEY_F22;
        case XK_F23:                  return MGL_KEY_F23;
        case XK_F24:                  return MGL_KEY_F24;
        case XK_exclam:               return MGL_KEY_EXCLAM;
        case XK_quotedbl:             return MGL_KEY_QUOTEDBL;
        case XK_numbersign:           return MGL_KEY_NUMBERSIGN;
        case XK_dollar:               return MGL_KEY_DOLLAR;
        case XK_percent:              return MGL_KEY_PERCENT;
        case XK_ampersand:            return MGL_KEY_AMPERSAND;
        case XK_parenleft:            return MGL_KEY_PARENLEFT;
        case XK_parenright:           return MGL_KEY_PARENRIGHT;
        case XK_asterisk:             return MGL_KEY_ASTERISK;
        case XK_plus:                 return MGL_KEY_PLUS;
        case XK_comma:                return MGL_KEY_COMMA;
        case XK_minus:                return MGL_KEY_MINUS;
        case XK_period:               return MGL_KEY_PERIOD;
        case XK_slash:                return MGL_KEY_SLASH;
        case XK_colon:                return MGL_KEY_COLON;
        case XK_semicolon:            return MGL_KEY_SEMICOLON;
        case XK_less:                 return MGL_KEY_LESS;
        case XK_equal:                return MGL_KEY_EQUAL;
        case XK_greater:              return MGL_KEY_GREATER;
        case XK_question:             return MGL_KEY_QUESTION;
        case XK_bracketleft:          return MGL_KEY_BRACKETLEFT;
        case XK_backslash:            return MGL_KEY_BACKSLASH;
        case XK_bracketright:         return MGL_KEY_BRACKETRIGHT;
        case XK_asciicircum:          return MGL_KEY_ASCIICIRCUM;
        case XK_underscore:           return MGL_KEY_UNDERSCORE;
        case XK_grave:                return MGL_KEY_GRAVE;
    }
    return MGL_KEY_UNKNOWN;
}

static mgl_mouse_button x11_button_to_mgl_button(unsigned int button) {
    switch(button) {
        case Button1: return MGL_BUTTON_LEFT;
        case Button2: return MGL_BUTTON_MIDDLE;
        case Button3: return MGL_BUTTON_RIGHT;
        case 8:       return MGL_BUTTON_XBUTTON1;
        case 9:       return MGL_BUTTON_XBUTTON2;
    }
    return MGL_BUTTON_UNKNOWN;
}

static mgl_key_states mgl_x11_key_states_to_mgl_key_states(unsigned int states) {
    return (mgl_key_states) {
        .alt = ((states & Mod1Mask) != 0) || ((states & Mod5Mask) != 0),
        .control = ((states & ControlMask) != 0),
        .shift = ((states & ShiftMask) != 0),
        .system = ((states & Mod4Mask) != 0),
    };
}

static void mgl_window_handle_key_event(XKeyEvent *xkey, mgl_event *event, mgl_context *context) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    event->key.code = x11_keysym_to_mgl_key(XKeycodeToKeysym(context->connection, xkey->keycode, 0));
    #pragma GCC diagnostic pop
    event->key.key_states = mgl_x11_key_states_to_mgl_key_states(xkey->state);
}

static void mgl_window_handle_text_event(mgl_window_x11 *self, XEvent *xev) {
    /* Ignore silent keys */
    if(XFilterEvent(xev, None))
        return;

    char buf[128];

    Status status;
    KeySym ksym;
    const size_t input_str_len = Xutf8LookupString(self->xic, &xev->xkey, buf, sizeof(buf), &ksym, &status);
    /* TODO: Handle XBufferOverflow */
    if(status == XBufferOverflow || input_str_len == 0)
        return;

    /* TODO: Set XIC location on screen with XSetICValues */

    for(size_t i = 0; i < input_str_len;) {
        const unsigned char *cp = (const unsigned char*)&buf[i];
        uint32_t codepoint;
        size_t clen;
        if(!mgl_utf8_decode(cp, input_str_len - i, &codepoint, &clen)) {
            codepoint = *cp;
            clen = 1;
        }

        mgl_event text_event;
        text_event.type = MGL_EVENT_TEXT_ENTERED;
        text_event.text.codepoint = codepoint;
        text_event.text.size = clen;
        memcpy(text_event.text.str, &buf[i], clen);
        text_event.text.str[clen] = '\0';
        /* Text may contain multiple codepoints so they need to separated into multiple events */
        if(!mgl_window_x11_append_event(self, &text_event))
            break;

        i += clen;
    }
}

static bool mgl_on_monitor_added(Display *display, mgl_window *mgl_window, XRROutputChangeNotifyEvent *rr_output_change_event, XRRScreenResources *screen_res, RROutput output_id, XRROutputInfo *out_info, mgl_event *event) {
    char display_name[256];
    mgl_monitor *monitor = NULL;
    XRRCrtcInfo *crt_info = NULL;
    const XRRModeInfo *mode_info = NULL;

    if(!rr_output_change_event->mode)
        return false;

    if(mgl_window_x11_get_monitor_by_id(mgl_window, output_id))
        return false;

    if(out_info->nameLen >= (int)sizeof(display_name))
        return false;

    crt_info = XRRGetCrtcInfo(display, screen_res, out_info->crtc);
    if(!crt_info)
        goto done;

    mode_info = get_mode_info(screen_res, rr_output_change_event->mode);
    if(!mode_info)
        goto done;

    memcpy(display_name, out_info->name, out_info->nameLen);
    display_name[out_info->nameLen] = '\0';

    monitor = mgl_window_x11_add_monitor(mgl_window, output_id, out_info->crtc, display_name,
        (mgl_vec2i){ .x = crt_info->x, .y = crt_info->y },
        (mgl_vec2i){ .x = (int)crt_info->width, .y = (int)crt_info->height },
        monitor_info_get_framerate(mode_info));

    if(!monitor)
        goto done;

    event->monitor_connected.id = monitor->id;
    event->monitor_connected.name = monitor->name;
    event->monitor_connected.x = monitor->pos.x;
    event->monitor_connected.y = monitor->pos.y;
    event->monitor_connected.width = monitor->size.x;
    event->monitor_connected.height = monitor->size.y;
    event->monitor_connected.refresh_rate = monitor->refresh_rate;
    event->type = MGL_EVENT_MONITOR_CONNECTED;

    done:
    if(crt_info)
        XRRFreeCrtcInfo(crt_info);
    return monitor != NULL;
}

static bool mgl_on_monitor_state_changed(Display *display, mgl_window *mgl_window, XRROutputChangeNotifyEvent *rr_output_change_event, mgl_event *event) {
    bool state_changed = false;
    XRROutputInfo *out_info = NULL;

    if(!rr_output_change_event->output)
        return false;

    XRRScreenResources *screen_res = XRRGetScreenResources(display, DefaultRootWindow(display));
    if(!screen_res)
        return false;

    out_info = XRRGetOutputInfo(display, screen_res, rr_output_change_event->output);
    if(out_info && out_info->crtc && out_info->connection == RR_Connected) {
        state_changed = mgl_on_monitor_added(display, mgl_window, rr_output_change_event, screen_res, rr_output_change_event->output, out_info, event);
    } else {
        state_changed = mgl_window_x11_remove_monitor(mgl_window, rr_output_change_event->output, event);
    }

    if(out_info)
        XRRFreeOutputInfo(out_info);
    
    XRRFreeScreenResources(screen_res);
    return state_changed;
}

static bool mgl_on_monitor_property_changed(Display *display, mgl_window *mgl_window, XRRCrtcChangeNotifyEvent *rr_crtc_change_event, mgl_event *event) {
    if(!rr_crtc_change_event->crtc)
        return false;

    mgl_monitor *monitor = mgl_window_x11_get_monitor_by_crtc_id(mgl_window, rr_crtc_change_event->crtc);
    if(!monitor)
        return false;

    XRRScreenResources *screen_res = XRRGetScreenResources(display, DefaultRootWindow(display));
    if(!screen_res)
        return false;

    monitor->pos = (mgl_vec2i){ .x = rr_crtc_change_event->x, .y = rr_crtc_change_event->y };
    monitor->size = (mgl_vec2i){ .x = rr_crtc_change_event->width, .y = rr_crtc_change_event->height };
    const XRRModeInfo *mode_info = get_mode_info(screen_res, rr_crtc_change_event->mode);
    if(mode_info)
        monitor->refresh_rate = monitor_info_get_framerate(mode_info);

    XRRFreeScreenResources(screen_res);

    event->monitor_property_changed.id = monitor->id;
    event->monitor_property_changed.name = monitor->name;
    event->monitor_property_changed.x = monitor->pos.x;
    event->monitor_property_changed.y = monitor->pos.y;
    event->monitor_property_changed.width = monitor->size.x;
    event->monitor_property_changed.height = monitor->size.y;
    event->monitor_property_changed.refresh_rate = monitor->refresh_rate;
    event->type = MGL_EVENT_MONITOR_PROPERTY_CHANGED;
    return true;
}

/* Returns true if an event was generated */
static bool mgl_on_rr_notify(mgl_context *context, mgl_window *mgl_window, XEvent *xev, int subtype, mgl_event *event) {
    switch(subtype) {
        case RRNotify_CrtcChange: {
            XRRCrtcChangeNotifyEvent *rr_crtc_change_event = (XRRCrtcChangeNotifyEvent*)xev;
            return mgl_on_monitor_property_changed(context->connection, mgl_window, rr_crtc_change_event, event);
        }
        case RRNotify_OutputChange: {
            XRROutputChangeNotifyEvent *rr_output_change_event = (XRROutputChangeNotifyEvent*)xev;
            return mgl_on_monitor_state_changed(context->connection, mgl_window, rr_output_change_event, event);
        }
    }
    return false;
}

static void mgl_window_x11_on_receive_event(mgl_window *self, XEvent *xev, mgl_event *event, mgl_context *context, bool injected) {
    mgl_window_x11 *impl = self->impl;
    switch(xev->type - context->randr_event_base) {
        case RRScreenChangeNotify: {
            XRRUpdateConfiguration(xev);
            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
        case RRNotify: {
            XRRNotifyEvent *rr_event = (XRRNotifyEvent*)xev;
            if(mgl_on_rr_notify(context, self, xev, rr_event->subtype, event)) {
                // TODO:
                //impl->num_monitors = mgl_window_x11->num_monitors;
                return;
            }

            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
    }

    switch(xev->type) {
        case KeyPress: {
            if(!self->key_repeat_enabled && xev->xkey.keycode == impl->prev_keycode_pressed && !impl->key_was_released) {
                event->type = MGL_EVENT_UNKNOWN;
                return;
            }

            impl->prev_keycode_pressed = xev->xkey.keycode;
            impl->key_was_released = false;

            event->type = MGL_EVENT_KEY_PRESSED;
            mgl_window_handle_key_event(&xev->xkey, event, context);
            mgl_window_handle_text_event(impl, xev);
            return;
        }
        case KeyRelease: {
            if(xev->xkey.keycode == impl->prev_keycode_pressed)
                impl->key_was_released = true;

            event->type = MGL_EVENT_KEY_RELEASED;
            mgl_window_handle_key_event(&xev->xkey, event, context);
            return;
        }
        case ButtonPress: {
            if(xev->xbutton.button == Button4) {
                /* Mouse scroll up */
                event->type = MGL_EVENT_MOUSE_WHEEL_SCROLLED;
                event->mouse_wheel_scroll.delta = 1;
                event->mouse_wheel_scroll.x = xev->xbutton.x;
                event->mouse_wheel_scroll.y = xev->xbutton.y;
                event->mouse_wheel_scroll.key_states = mgl_x11_key_states_to_mgl_key_states(xev->xbutton.state);
            } else if(xev->xbutton.button == Button5) {
                /* Mouse scroll down */
                event->type = MGL_EVENT_MOUSE_WHEEL_SCROLLED;
                event->mouse_wheel_scroll.delta = -1;
                event->mouse_wheel_scroll.x = xev->xbutton.x;
                event->mouse_wheel_scroll.y = xev->xbutton.y;
                event->mouse_wheel_scroll.key_states = mgl_x11_key_states_to_mgl_key_states(xev->xbutton.state);
            } else {
                event->type = MGL_EVENT_MOUSE_BUTTON_PRESSED;
                event->mouse_button.button = x11_button_to_mgl_button(xev->xbutton.button);
                event->mouse_button.x = xev->xbutton.x;
                event->mouse_button.y = xev->xbutton.y;
                event->mouse_button.key_states = mgl_x11_key_states_to_mgl_key_states(xev->xbutton.state);
            }
            return;
        }
        case ButtonRelease: {
            event->type = MGL_EVENT_MOUSE_BUTTON_RELEASED;
            event->mouse_button.button = x11_button_to_mgl_button(xev->xbutton.button);
            event->mouse_button.x = xev->xbutton.x;
            event->mouse_button.y = xev->xbutton.y;
            event->mouse_button.key_states = mgl_x11_key_states_to_mgl_key_states(xev->xbutton.state);
            return;
        }
        case FocusIn: {
            XSetICFocus(impl->xic);

            XWMHints* hints = XGetWMHints(context->connection, impl->window);
            if(hints) {
                hints->flags &= ~XUrgencyHint;
                XSetWMHints(context->connection, impl->window, hints);
                XFree(hints);
            }

            event->type = MGL_EVENT_GAINED_FOCUS;
            self->focused = true;
            return;
        }
        case FocusOut: {
            XUnsetICFocus(impl->xic);
            event->type = MGL_EVENT_LOST_FOCUS;
            self->focused = false;
            return;
        }
        case ConfigureNotify: {
            if(!injected)
                while(XCheckTypedWindowEvent(context->connection, impl->window, ConfigureNotify, xev)) {}

            if(xev->xconfigure.x != self->pos.x || xev->xconfigure.y != self->pos.y) {
                mgl_window_x11_on_move(self, xev->xconfigure.x, xev->xconfigure.y);
            }

            if(xev->xconfigure.width != self->size.x || xev->xconfigure.height != self->size.y) {
                mgl_window_x11_on_resize(self, xev->xconfigure.width, xev->xconfigure.height);

                event->type = MGL_EVENT_RESIZED;
                event->size.width = self->size.x;
                event->size.height = self->size.y;
                return;
            }

            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
        case MotionNotify: {
            if(!injected)
                while(XCheckTypedWindowEvent(context->connection, impl->window, MotionNotify, xev)) {}

            self->cursor_position.x = xev->xmotion.x;
            self->cursor_position.y = xev->xmotion.y;

            event->type = MGL_EVENT_MOUSE_MOVED;
            event->mouse_move.x = self->cursor_position.x;
            event->mouse_move.y = self->cursor_position.y;
            return;
        }
        case SelectionClear: {
            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
        case SelectionRequest: {
            XSelectionEvent selection_event;
            selection_event.type = SelectionNotify;
            selection_event.requestor = xev->xselectionrequest.requestor;
            selection_event.selection = xev->xselectionrequest.selection;
            selection_event.property = xev->xselectionrequest.property;
            selection_event.time = xev->xselectionrequest.time;
            selection_event.target = xev->xselectionrequest.target;

            if(selection_event.selection == impl->clipboard_atom) {
                /* TODO: Support ascii text separately by unsetting the 8th bit in the clipboard string? */
                if(selection_event.target == impl->targets_atom) {
                    /* A client requested for our valid conversion targets */
                    int num_targets = 3;
                    Atom targets[4];
                    targets[0] = impl->targets_atom;
                    targets[1] = impl->text_atom;
                    targets[2] = XA_STRING;
                    if(impl->utf8_string_atom) {
                        targets[3] = impl->utf8_string_atom;
                        num_targets = 4;
                    }

                    XChangeProperty(context->connection, selection_event.requestor, selection_event.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)targets, num_targets);
                    selection_event.target = impl->targets_atom;
                    XSendEvent(context->connection, selection_event.requestor, False, NoEventMask, (XEvent*)&selection_event);
                    XFlush(context->connection);
                    event->type = MGL_EVENT_UNKNOWN;
                    return;
                } else if(selection_event.target == XA_STRING || (!impl->utf8_string_atom && selection_event.target == impl->text_atom)) {
                    /* A client requested ascii clipboard */
                    XChangeProperty(context->connection, selection_event.requestor, selection_event.property, XA_STRING, 8, PropModeReplace, (const unsigned char*)impl->clipboard_string, impl->clipboard_size);
                    selection_event.target = XA_STRING;
                    XSendEvent(context->connection, selection_event.requestor, False, NoEventMask, (XEvent*)&selection_event);
                    XFlush(context->connection);
                    event->type = MGL_EVENT_UNKNOWN;
                    return;
                } else if(impl->utf8_string_atom && (selection_event.target == impl->utf8_string_atom || selection_event.target == impl->text_atom)) {
                    /* A client requested utf8 clipboard */
                    XChangeProperty(context->connection, selection_event.requestor, selection_event.property, impl->utf8_string_atom, 8, PropModeReplace, (const unsigned char*)impl->clipboard_string, impl->clipboard_size);
                    selection_event.target = impl->utf8_string_atom;
                    XSendEvent(context->connection, selection_event.requestor, False, NoEventMask, (XEvent*)&selection_event);
                    XFlush(context->connection);
                    event->type = MGL_EVENT_UNKNOWN;
                    return;
                }
            }

            selection_event.property = None;
            XSendEvent(context->connection, selection_event.requestor, False, NoEventMask, (XEvent*)&selection_event);
            XFlush(context->connection);
            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
        case ClientMessage: {
            if(xev->xclient.format == 32 && (unsigned long)xev->xclient.data.l[0] == context->wm_delete_window_atom) {
                event->type = MGL_EVENT_CLOSED;
                self->open = false;
                return;
            } else if(xev->xclient.format == 32 && (unsigned long)xev->xclient.data.l[0] == context->net_wm_ping_atom) {
                xev->xclient.window = DefaultRootWindow(context->connection);
                XSendEvent(context->connection, DefaultRootWindow(context->connection), False, SubstructureNotifyMask | SubstructureRedirectMask, xev);
                XFlush(context->connection);
            }
            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
        case MappingNotify: {
            /* TODO: Only handle this globally once for mgl, not for each window */
            XRefreshKeyboardMapping(&xev->xmapping);
            event->type = MGL_EVENT_MAPPING_CHANGED;
            event->mapping_changed.type = MappingModifier;
            switch(xev->xmapping.request) {
                case MappingModifier:
                    event->mapping_changed.type = MGL_MAPPING_CHANGED_MODIFIER;
                    break;
                case MappingKeyboard:
                    event->mapping_changed.type = MGL_MAPPING_CHANGED_KEYBOARD;
                    break;
                case MappingPointer:
                    event->mapping_changed.type = MGL_MAPPING_CHANGED_POINTER;
                    break;
            }
            return;
        }
        default: {
            event->type = MGL_EVENT_UNKNOWN;
            return;
        }
    }
}

static bool mgl_window_x11_poll_event(mgl_window *self, mgl_event *event) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    Display *display = context->connection;

    if(mgl_window_x11_pop_event(impl, event))
        return true;

    if(XPending(display)) {
        XNextEvent(display, &impl->xev);
        if(impl->xev.xany.window == impl->window || impl->xev.type == ClientMessage || impl->xev.type == MappingNotify || impl->xev.type - context->randr_event_base >= 0)
            mgl_window_x11_on_receive_event(self, &impl->xev, event, context, false);
        else
            event->type = MGL_EVENT_UNKNOWN;
        return true;
    } else {
        return false;
    }
}

static bool mgl_window_x11_inject_x11_event(mgl_window *self, XEvent *xev, mgl_event *event) {
    mgl_context *context = mgl_get_context();
    event->type = MGL_EVENT_UNKNOWN;
    mgl_window_x11_on_receive_event(self, xev, event, context, true);
    return event->type != MGL_EVENT_UNKNOWN;
}

static void mgl_window_x11_swap_buffers(mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    mgl_graphics_swap_buffers(&impl->graphics, (mgl_window_handle)impl->window);
}

static void mgl_window_x11_set_visible(mgl_window *self, bool visible) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    if(visible)
        XMapWindow(context->connection, impl->window);
    else
        XUnmapWindow(context->connection, impl->window);
    XFlush(context->connection);
}

/* TODO: Track keys with events instead, but somehow handle window focus lost */
static bool mgl_window_x11_is_key_pressed(const mgl_window *self, mgl_key key) {
    (void)self;
    if(key < 0 || key >= __MGL_NUM_KEYS__)
        return false;
    
    mgl_context *context = mgl_get_context();
    const KeySym keysym = mgl_key_to_x11_keysym(key);
    if(keysym == XK_VoidSymbol)
        return false;

    KeyCode keycode = XKeysymToKeycode(context->connection, keysym);
    if(keycode == 0)
        return false;

    char keys[32];
    XQueryKeymap(context->connection, keys);

    return (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
}

/* TODO: Track keys with events instead, but somehow handle window focus lost */
static bool mgl_window_x11_is_mouse_button_pressed(const mgl_window *self, mgl_mouse_button button) {
    (void)self;
    if(button < 0 || button >= __MGL_NUM_MOUSE_BUTTONS__)
        return false;

    mgl_context *context = mgl_get_context();
    Window root, child;
    int root_x, root_y, win_x, win_y;

    unsigned int buttons_mask = 0;
    XQueryPointer(context->connection, DefaultRootWindow(context->connection), &root, &child, &root_x, &root_y, &win_x, &win_y, &buttons_mask);

    switch(button) {
        case MGL_BUTTON_LEFT:      return buttons_mask & Button1Mask;
        case MGL_BUTTON_MIDDLE:    return buttons_mask & Button2Mask;
        case MGL_BUTTON_RIGHT:     return buttons_mask & Button3Mask;
        case MGL_BUTTON_XBUTTON1:  return false; /* Not supported by x11 */
        case MGL_BUTTON_XBUTTON2:  return false; /* Not supported by x11 */
        default:                   return false;
    }

    return false;
}

static mgl_window_handle mgl_window_x11_get_system_handle(const mgl_window *self) {
    const mgl_window_x11 *impl = self->impl;
    return (mgl_window_handle)impl->window;
}

static void mgl_window_x11_close(mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    if(impl->window) {
        XDestroyWindow(context->connection, impl->window);
        XSync(context->connection, False);
        impl->window = None;
    }
    self->open = false;
}

static void mgl_window_x11_set_cursor_visible(mgl_window *self, bool visible) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    XDefineCursor(context->connection, impl->window, visible ? impl->default_cursor : impl->invisible_cursor);
}

static void mgl_window_x11_set_vsync_enabled(mgl_window *self, bool enabled) {
    mgl_window_x11 *impl = self->impl;
    self->vsync_enabled = enabled;
    mgl_graphics_set_swap_interval(&impl->graphics, (mgl_window_handle)impl->window, self->vsync_enabled);
}

static bool mgl_window_x11_is_vsync_enabled(const mgl_window *self) {
    return self->vsync_enabled;
}

static void mgl_window_x11_set_net_wm_state_atom_unmapped(mgl_window_x11 *impl, Atom atom, bool enable) {
    mgl_context *context = mgl_get_context();
    if(enable) {
        XChangeProperty(context->connection, impl->window, impl->net_wm_state_atom, XA_ATOM, 32, PropModeAppend, (unsigned char*)&atom, 1L);
        XFlush(context->connection);
        return;
    }

    Atom type = None;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining_bytes = 0;
    unsigned char *data = NULL;
    if(XGetWindowProperty(context->connection, impl->window, impl->net_wm_state_atom, 0, 1024, False, XA_ATOM, &type, &format, &items, &remaining_bytes, &data) != Success || !data)
        return;

    if(format == 32) {
        Atom *atoms = (Atom*)data;
        unsigned long num_atoms = 0;
        for(unsigned long i = 0; i < items; ++i) {
            if(atoms[i] != atom)
                atoms[num_atoms++] = atoms[i];
        }
        XChangeProperty(context->connection, impl->window, impl->net_wm_state_atom, XA_ATOM, 32, PropModeReplace, (unsigned char*)atoms, num_atoms);
        XFlush(context->connection);
    }
    XFree(data);
}

static void mgl_window_x11_set_fullscreen(mgl_window *self, bool fullscreen) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();

    XWindowAttributes window_attr;
    window_attr.map_state = IsUnmapped;
    XGetWindowAttributes(context->connection, impl->window, &window_attr);
    if(window_attr.map_state == IsUnmapped) {
        mgl_window_x11_set_net_wm_state_atom_unmapped(impl, impl->net_wm_state_fullscreen_atom, fullscreen);
        return;
    }

    XEvent xev;
    xev.type = ClientMessage;
    xev.xclient.window = impl->window;
    xev.xclient.message_type = impl->net_wm_state_atom;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = fullscreen ? 1 : 0;
    xev.xclient.data.l[1] = impl->net_wm_state_fullscreen_atom;
    xev.xclient.data.l[2] = 0;
    xev.xclient.data.l[3] = 1;
    xev.xclient.data.l[4] = 0;

    if(!XSendEvent(context->connection, DefaultRootWindow(context->connection), 0, SubstructureRedirectMask | SubstructureNotifyMask, &xev)) {
        fprintf(stderr, "mgl warning: failed to change window fullscreen state\n");
        return;
    }

    XFlush(context->connection);
}

static bool mgl_window_x11_is_fullscreen(const mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();

    bool is_fullscreen = false;
    Atom type = None;
    int format = 0;
    unsigned long items = 0;
    unsigned long remaining_bytes = 0;
    unsigned char *data = NULL;
    if(XGetWindowProperty(context->connection, impl->window, impl->net_wm_state_atom, 0, 1024, False, PropertyNewValue, &type, &format, &items, &remaining_bytes, &data) == Success && data) {
        is_fullscreen = format == 32 && *(unsigned long*)data == impl->net_wm_state_fullscreen_atom;
        XFree(data);
    }
    return is_fullscreen;
}

static void mgl_window_x11_set_position(mgl_window *self, mgl_vec2i position) {
    mgl_window_x11 *impl = self->impl;
    XMoveWindow(mgl_get_context()->connection, impl->window, position.x, position.y);
    XFlush(mgl_get_context()->connection);
}

static void mgl_window_x11_set_size(mgl_window *self, mgl_vec2i size) {
    mgl_window_x11 *impl = self->impl;
    if(size.x < 0)
        size.x = 0;
    if(size.y < 0)
        size.y = 0;
    XResizeWindow(mgl_get_context()->connection, impl->window, size.x, size.y);
    XFlush(mgl_get_context()->connection);
}

static void mgl_window_x11_set_clipboard(mgl_window *self, const char *str, size_t size) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();

    if(impl->clipboard_string) {
        free(impl->clipboard_string);
        impl->clipboard_string = NULL;
        impl->clipboard_size = 0;
    }

    XSetSelectionOwner(context->connection, impl->clipboard_atom, impl->window, CurrentTime);
    XFlush(context->connection);

    /* Check if setting the selection owner was successful */
    if(XGetSelectionOwner(context->connection, impl->clipboard_atom) != impl->window) {
        fprintf(stderr, "mgl error: mgl_window_x11_set_clipboard failed\n");
        return;
    }

    impl->clipboard_string = malloc(size + 1);
    if(!impl->clipboard_string) {
        fprintf(stderr, "mgl error: failed to allocate string for clipboard\n");
        return;
    }
    memcpy(impl->clipboard_string, str, size);
    impl->clipboard_string[size] = '\0';
    impl->clipboard_size = size;
}

static Atom find_matching_atom(const Atom *supported_atoms, size_t num_supported_atoms, const Atom *atoms, size_t num_atoms) {
    for(size_t j = 0; j < num_supported_atoms; ++j) {
        for(size_t i = 0; i < num_atoms; ++i) {
            if(atoms[i] == supported_atoms[j])
                return atoms[i];
        }
    }
    return None;
}

static mgl_clipboard_type atom_type_to_supported_clipboard_type(mgl_window_x11 *mgl_window_x11, Atom type, int format) {
    if((type == mgl_window_x11->utf8_string_atom || type == XA_STRING || type == mgl_window_x11->text_atom) && format == 8) {
        return MGL_CLIPBOARD_TYPE_STRING;
    } else if(type == mgl_window_x11->image_png_atom && format == 8){
        return MGL_CLIPBOARD_TYPE_IMAGE_PNG;
    } else if((type == mgl_window_x11->image_jpg_atom || type == mgl_window_x11->image_jpeg_atom) && format == 8){
        return MGL_CLIPBOARD_TYPE_IMAGE_JPG;
    } else if(type == mgl_window_x11->image_gif_atom && format == 8){
        return MGL_CLIPBOARD_TYPE_IMAGE_GIF;
    } else {
        return -1;
    }
}

static bool mgl_window_x11_get_clipboard(mgl_window *self, mgl_clipboard_callback callback, void *userdata, uint32_t clipboard_types) {
    mgl_window_x11 *impl = self->impl;
    assert(callback);

    mgl_context *context = mgl_get_context();
    Window selection_owner = XGetSelectionOwner(context->connection, impl->clipboard_atom);

    if(!selection_owner)
        return false;

    /* Return data immediately if we are the owner of the clipboard, because we can't process other events in the below event loop */
    if(selection_owner == impl->window) {
        if(!impl->clipboard_string)
            return false;

        return callback((const unsigned char*)impl->clipboard_string, impl->clipboard_size, MGL_CLIPBOARD_TYPE_STRING, userdata);
    }

    XEvent xev;
    while(XCheckTypedWindowEvent(context->connection, impl->window, SelectionNotify, &xev)) {}

    /* Sorted by preference */
    /* TODO: Support more types (BITMAP?, PIXMAP?) */
    Atom supported_clipboard_types[7];

    int supported_clipboard_type_index = 0;
    if(clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_PNG) {
        supported_clipboard_types[supported_clipboard_type_index++] = impl->image_png_atom;
    }

    if(clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_JPG) {
        supported_clipboard_types[supported_clipboard_type_index++] = impl->image_jpeg_atom;
    }

    if(clipboard_types & MGL_CLIPBOARD_TYPE_IMAGE_GIF) {
        supported_clipboard_types[supported_clipboard_type_index++] = impl->image_gif_atom;
    }

    if(clipboard_types & MGL_CLIPBOARD_TYPE_STRING) {
        supported_clipboard_types[supported_clipboard_type_index++] = impl->utf8_string_atom;
        supported_clipboard_types[supported_clipboard_type_index++] = XA_STRING;
        supported_clipboard_types[supported_clipboard_type_index++] = impl->text_atom;
    }

    const unsigned long num_supported_clipboard_types = supported_clipboard_type_index;

    Atom requested_clipboard_type = None;
    const Atom XA_TARGETS = XInternAtom(context->connection, "TARGETS", False);
    XConvertSelection(context->connection, impl->clipboard_atom, XA_TARGETS, impl->clipboard_atom, impl->window, CurrentTime);

    mgl_clock timeout_timer;
    mgl_clock_init(&timeout_timer);
    bool success = false;

    const double timeout_seconds = 5.0;
    while(mgl_clock_get_elapsed_time_seconds(&timeout_timer) < timeout_seconds) {
        /* TODO: Wait for SelectionNotify event instead */
        while(XCheckTypedWindowEvent(context->connection, impl->window, SelectionNotify, &xev)) {
            if(mgl_clock_get_elapsed_time_seconds(&timeout_timer) >= timeout_seconds)
                break;

            if(!xev.xselection.property)
                continue;

            if(!xev.xselection.target || xev.xselection.selection != impl->clipboard_atom)
                continue;

            Atom type = None;
            int format;
            unsigned long items;
            unsigned long remaining_bytes = 0;
            unsigned char *data = NULL;

            if(xev.xselection.target == XA_TARGETS && requested_clipboard_type == None) {
                /* TODO: Wait for PropertyNotify event instead */
                if(XGetWindowProperty(context->connection, xev.xselection.requestor, xev.xselection.property, 0, 1024, False, PropertyNewValue, &type, &format, &items, &remaining_bytes, &data) == Success && data) {
                    if(type != impl->incr_atom && type == XA_ATOM && format == 32) {
                        requested_clipboard_type = find_matching_atom(supported_clipboard_types, num_supported_clipboard_types, (Atom*)data, items);
                        if(requested_clipboard_type == None) {
                            /* Pasting clipboard data type we dont support */
                            XFree(data);
                            goto done;
                        } else {
                            XConvertSelection(context->connection, impl->clipboard_atom, requested_clipboard_type, impl->clipboard_atom, impl->window, CurrentTime);
                        }
                    }

                    XFree(data);
                }
            } else if(xev.xselection.target == requested_clipboard_type) {
                bool got_data = false;
                long chunk_size = 65536;

                //XDeleteProperty(context->connection, self->window, mgl_window_x11->incr_atom);
                //XFlush(context->connection);

                while(mgl_clock_get_elapsed_time_seconds(&timeout_timer) < timeout_seconds) {
                    unsigned long offset = 0;

                    /* offset is specified in XGetWindowProperty as a multiple of 32-bit (4 bytes) */
                    /* TODO: Wait for PropertyNotify event instead */
                    while(XGetWindowProperty(context->connection, xev.xselection.requestor, xev.xselection.property, offset/4, chunk_size, True, PropertyNewValue, &type, &format, &items, &remaining_bytes, &data) == Success) {
                        if(mgl_clock_get_elapsed_time_seconds(&timeout_timer) >= timeout_seconds)
                            break;

                        if(type == impl->incr_atom) {
                            XDeleteProperty(context->connection, impl->window, impl->incr_atom);
                            XFlush(context->connection);
                            XFree(data);
                            data = NULL;
                            break;
                        } else if(type == requested_clipboard_type) {
                            got_data = true;
                        }

                        if(!got_data && items == 0) {
                            XDeleteProperty(context->connection, impl->window, type);
                            XFlush(context->connection);
                            if(data) {
                                XFree(data);
                                data = NULL;
                            }
                            break;
                        }

                        const size_t num_items_bytes = items * (format/8); /* format is the bit size of the data */
                        if(got_data) {
                            const mgl_clipboard_type clipboard_type = atom_type_to_supported_clipboard_type(impl, type, format);
                            if(data && num_items_bytes > 0 && (int)clipboard_type != -1) {
                                if(!callback(data, num_items_bytes, clipboard_type, userdata)) {
                                    XFree(data);
                                    goto done;
                                }
                            }
                        }
                        offset += num_items_bytes;

                        if(data) {
                            XFree(data);
                            data = NULL;
                        }

                        if(got_data && num_items_bytes == 0/* && format == 8*/) {
                            success = true;
                            goto done;
                        }

                        if(remaining_bytes == 0)
                           break;
                    }
                }

                goto done;
            }
        }
    }

    done:
    if(requested_clipboard_type)
        XDeleteProperty(context->connection, impl->window, requested_clipboard_type);
    return success;
}

static void mgl_window_x11_set_key_repeat_enabled(mgl_window *self, bool enabled) {
    self->key_repeat_enabled = enabled;
}

static void mgl_window_x11_flush(mgl_window *self) {
    (void)self;
    mgl_context *context = mgl_get_context();
    XFlush(context->connection);
}

static void* mgl_window_x11_get_egl_display(mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    if(impl->graphics.graphics_api == MGL_GRAPHICS_API_EGL)
        return mgl_graphics_get_display(&impl->graphics);
    else
        return NULL;
}

static void* mgl_window_x11_get_egl_context(mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    if(impl->graphics.graphics_api == MGL_GRAPHICS_API_EGL)
        return mgl_graphics_get_context(&impl->graphics);
    else
        return NULL;
}

bool mgl_window_x11_init(mgl_window *self, const char *title, const mgl_window_create_params *params, mgl_window_handle existing_window) {
    mgl_window_x11 *impl = calloc(1, sizeof(mgl_window_x11));
    if(!impl)
        return false;

    self->get_system_handle = mgl_window_x11_get_system_handle;
    self->deinit = mgl_window_x11_deinit;
    self->close = mgl_window_x11_close;
    self->inject_x11_event = mgl_window_x11_inject_x11_event;
    self->poll_event = mgl_window_x11_poll_event;
    self->swap_buffers = mgl_window_x11_swap_buffers;
    self->set_visible = mgl_window_x11_set_visible;
    self->is_key_pressed = mgl_window_x11_is_key_pressed;
    self->is_mouse_button_pressed = mgl_window_x11_is_mouse_button_pressed;
    self->set_title = mgl_window_x11_set_title;
    self->set_cursor_visible = mgl_window_x11_set_cursor_visible;
    self->set_vsync_enabled = mgl_window_x11_set_vsync_enabled;
    self->is_vsync_enabled = mgl_window_x11_is_vsync_enabled;
    self->set_fullscreen = mgl_window_x11_set_fullscreen;
    self->is_fullscreen = mgl_window_x11_is_fullscreen;
    self->set_position = mgl_window_x11_set_position;
    self->set_size = mgl_window_x11_set_size;
    self->set_size_limits = mgl_window_x11_set_size_limits;
    self->set_clipboard = mgl_window_x11_set_clipboard;
    self->get_clipboard = mgl_window_x11_get_clipboard;
    self->set_key_repeat_enabled = mgl_window_x11_set_key_repeat_enabled;
    self->flush = mgl_window_x11_flush;
    self->get_egl_display = mgl_window_x11_get_egl_display;
    self->get_egl_context = mgl_window_x11_get_egl_context;
    self->for_each_active_monitor_output = mgl_window_x11_for_each_active_monitor_output;
    self->impl = impl;

    /* TODO: Use CLIPBOARD_MANAGER and SAVE_TARGETS to save clipboard in clipboard manager on exit */

    mgl_context *context = mgl_get_context();
    /* TODO: Create all of these with one XInternAtoms call instead */
    impl->clipboard_atom = XInternAtom(context->connection, "CLIPBOARD", False);
    impl->targets_atom = XInternAtom(context->connection, "TARGETS", False);
    impl->text_atom = XInternAtom(context->connection, "TEXT", False);
    impl->utf8_string_atom = XInternAtom(context->connection, "UTF8_STRING", False);
    impl->image_png_atom = XInternAtom(context->connection, "image/png", False);
    impl->image_jpg_atom = XInternAtom(context->connection, "image/jpg", False);
    impl->image_jpeg_atom = XInternAtom(context->connection, "image/jpeg", False);
    impl->image_gif_atom = XInternAtom(context->connection, "image/gif", False);
    impl->incr_atom = XInternAtom(context->connection, "INCR", False);
    impl->net_wm_state_atom = XInternAtom(context->connection, "_NET_WM_STATE", False);
    impl->net_wm_state_fullscreen_atom = XInternAtom(context->connection, "_NET_WM_STATE_FULLSCREEN", False);
    impl->net_wm_state_above_atom = XInternAtom(context->connection, "_NET_WM_STATE_ABOVE", False);
    impl->net_wm_name_atom = XInternAtom(context->connection, "_NET_WM_NAME", False);
    impl->net_wm_window_type_atom = XInternAtom(context->connection, "_NET_WM_WINDOW_TYPE", False);
    impl->net_wm_window_type_normal_atom = XInternAtom(context->connection, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    impl->net_wm_window_type_dialog_atom = XInternAtom(context->connection, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    impl->net_wm_window_type_notification_atom = XInternAtom(context->connection, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    impl->net_wm_window_type_utility = XInternAtom(context->connection, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    impl->motif_wm_hints_atom = XInternAtom(context->connection, "_MOTIF_WM_HINTS", False);

    impl->support_alpha = params && params->support_alpha;

    const mgl_graphics_create_params g_create_params = {
        .graphics_api = params ? params->graphics_api : MGL_GRAPHICS_API_EGL,
        .alpha = params && params->support_alpha,
        .request_depth_buffer = params && params->request_depth_buffer,
        .request_stencil_buffer = params && params->request_stencil_buffer,
    };

    if(!mgl_graphics_init(&impl->graphics, &g_create_params)) {
        mgl_window_x11_deinit(self);
        return false;
    }

    impl->default_cursor = XCreateFontCursor(context->connection, XC_arrow);
    if(!impl->default_cursor) {
        mgl_window_x11_deinit(self);
        return false;
    }

    const char data[1] = {0};
    Pixmap blank_bitmap = XCreateBitmapFromData(context->connection, DefaultRootWindow(context->connection), data, 1, 1);
    if(!blank_bitmap) {
        mgl_window_x11_deinit(self);
        return false;
    }

    XColor dummy;
    impl->invisible_cursor = XCreatePixmapCursor(context->connection, blank_bitmap, blank_bitmap, &dummy, &dummy, 0, 0);
    XFreePixmap(context->connection, blank_bitmap);
    if(!impl->invisible_cursor) {
        mgl_window_x11_deinit(self);
        return false;
    }
    
    x11_events_circular_buffer_init(&impl->events);

    if(!mgl_x11_setup_window(self, title, params, (Window)existing_window)) {
        mgl_window_x11_deinit(self);
        return false;
    }

    context->current_window = self;
    return true;
}

void mgl_window_x11_deinit(mgl_window *self) {
    mgl_window_x11 *impl = self->impl;
    mgl_context *context = mgl_get_context();
    if(!impl)
        return;

    mgl_window_x11_clear_monitors(self);

    if(impl->color_map) {
        XFreeColormap(context->connection, impl->color_map);
        impl->color_map = None;
    }

    if(impl->invisible_cursor) {
        XFreeCursor(context->connection, impl->invisible_cursor);
        impl->invisible_cursor = None;
    }

    if(impl->default_cursor) {
        XFreeCursor(context->connection, impl->default_cursor);
        impl->default_cursor = None;
    }

    if(impl->xic) {
        XDestroyIC(impl->xic);
        impl->xic = NULL;
    }

    if(impl->xim) {
        XCloseIM(impl->xim);
        impl->xim = NULL;
    }

    mgl_graphics_deinit(&impl->graphics);
    mgl_window_x11_close(self);

    if(impl->clipboard_string) {
        free(impl->clipboard_string);
        impl->clipboard_string = NULL;
    }
    impl->clipboard_size = 0;

    self->open = false;

    if(context->current_window == self)
        context->current_window = NULL;

    free(self->impl);
    self->impl = NULL;
}
