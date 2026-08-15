#include "../include/damage.h"
#include "../include/log.h"
#include "../include/utils.h"
#include "../include/window/window.h"

#include <string.h>
#include <stdlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrandr.h>

typedef struct {
    vec2i pos;
    vec2i size;
} gsr_rectangle;

static bool rectangles_intersect(gsr_rectangle rect1, gsr_rectangle rect2) {
    return rect1.pos.x < rect2.pos.x + rect2.size.x && rect1.pos.x + rect1.size.x > rect2.pos.x &&
        rect1.pos.y < rect2.pos.y + rect2.size.y && rect1.pos.y + rect1.size.y > rect2.pos.y;
}

static bool xrandr_is_supported(Display *display) {
    int major_version = 0;
    int minor_version = 0;
    if(!XRRQueryVersion(display, &major_version, &minor_version))
        return false;

    return major_version > 1 || (major_version == 1 && minor_version >= 2);
}

static int gsr_damage_get_tracked_monitor_index(const gsr_damage *self, const char *monitor_name) {
    for(int i = 0; i < self->num_monitors_tracked; ++i) {
        if(strcmp(self->monitors_tracked[i].monitor_name, monitor_name) == 0)
            return i;
    }
    return -1;
}

static void add_monitor_callback(const gsr_monitor *monitor, void *userdata) {
    gsr_damage *self = userdata;

    const int damage_monitor_index = gsr_damage_get_tracked_monitor_index(self, monitor->name);
    gsr_damage_monitor *damage_monitor = NULL;
    if(damage_monitor_index != -1) {
        damage_monitor = &self->monitors_tracked[damage_monitor_index];
        damage_monitor->monitor = NULL;
    }

    if(self->num_monitors + 1 > GSR_DAMAGE_MAX_MONITORS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_on_output_change: max monitors reached");
        return;
    }

    char *monitor_name_copy = strdup(monitor->name);
    if(!monitor_name_copy) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_on_output_change: strdup failed for monitor: %s", monitor->name);
        return;
    }

    self->monitors[self->num_monitors] = *monitor;
    self->monitors[self->num_monitors].name = monitor_name_copy;
    ++self->num_monitors;

    if(damage_monitor)
        damage_monitor->monitor = &self->monitors[self->num_monitors - 1];
}

bool gsr_damage_init(gsr_damage *self, gsr_egl *egl, gsr_cursor *cursor, bool track_cursor) {
    memset(self, 0, sizeof(*self));
    self->egl = egl;
    self->track_cursor = track_cursor;
    self->cursor = cursor;

    if(gsr_window_get_display_server(egl->window) != GSR_DISPLAY_SERVER_X11) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_init: damage tracking is not supported on wayland");
        return false;
    }
    self->display = gsr_window_get_display(egl->window);

    if(!XDamageQueryExtension(self->display, &self->damage_event, &self->damage_error)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_init: XDamage is not supported by your X11 server");
        gsr_damage_deinit(self);
        return false;
    }

    if(!XRRQueryExtension(self->display, &self->randr_event, &self->randr_error)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_init: XRandr is not supported by your X11 server");
        gsr_damage_deinit(self);
        return false;
    }

    if(!xrandr_is_supported(self->display)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_init: your X11 randr version is too old");
        gsr_damage_deinit(self);
        return false;
    }

    XRRSelectInput(self->display, DefaultRootWindow(self->display), RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);

    self->monitor_damage = XDamageCreate(self->display, DefaultRootWindow(self->display), XDamageReportNonEmpty);
    if(!self->monitor_damage) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_init: XDamageCreate failed");
        gsr_damage_deinit(self);
        return false;
    }
    XDamageSubtract(self->display, self->monitor_damage, None, None);

    for_each_active_monitor_output_x11_not_cached(self->display, add_monitor_callback, self);

    self->damaged = true;
    return true;
}

static void gsr_damage_deinit_monitors(gsr_damage *self) {
    for(int i = 0; i < self->num_monitors; ++i) {
        free((char*)self->monitors[i].name);
    }
    self->num_monitors = 0;
}

void gsr_damage_deinit(gsr_damage *self) {
    if(self->monitor_damage) {
        XDamageDestroy(self->display, self->monitor_damage);
        self->monitor_damage = None;
    }

    for(int i = 0; i < self->num_monitors_tracked; ++i) {
        free(self->monitors_tracked[i].monitor_name);
    }
    self->num_monitors_tracked = 0;

    for(int i = 0; i < self->num_windows_tracked; ++i) {
        XSelectInput(self->display, self->windows_tracked[i].window_id, 0);
        XDamageDestroy(self->display, self->windows_tracked[i].damage);
    }
    self->num_windows_tracked = 0;

    self->all_monitors_tracked_refcount = 0;
    gsr_damage_deinit_monitors(self);

    self->damage_event = 0;
    self->damage_error = 0;

    self->randr_event = 0;
    self->randr_error = 0;
}

static int gsr_damage_get_tracked_window_index(const gsr_damage *self, int64_t window) {
    for(int i = 0; i < self->num_windows_tracked; ++i) {
        if(self->windows_tracked[i].window_id == window)
            return i;
    }
    return -1;
}

bool gsr_damage_start_tracking_window(gsr_damage *self, int64_t window) {
    if(self->damage_event == 0 || window == None)
        return false;

    const int damage_window_index = gsr_damage_get_tracked_window_index(self, window);
    if(damage_window_index != -1) {
        ++self->windows_tracked[damage_window_index].refcount;
        return true;
    }

    if(self->num_windows_tracked + 1 > GSR_DAMAGE_MAX_TRACKED_TARGETS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_start_tracking_window: max window targets reached");
        return false;
    }

    XWindowAttributes win_attr;
    win_attr.x = 0;
    win_attr.y = 0;
    win_attr.width = 0;
    win_attr.height = 0;
    if(!XGetWindowAttributes(self->display, window, &win_attr))
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_damage_start_tracking_window failed: failed to get window attributes: %ld", (long)window);

    const Damage damage = XDamageCreate(self->display, window, XDamageReportNonEmpty);
    if(!damage) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_start_tracking_window: XDamageCreate failed");
        return false;
    }
    XDamageSubtract(self->display, damage, None, None);

    XSelectInput(self->display, window, StructureNotifyMask | ExposureMask);

    gsr_damage_window *damage_window = &self->windows_tracked[self->num_windows_tracked];
    ++self->num_windows_tracked;

    damage_window->window_id = window;
    damage_window->window_pos.x = win_attr.x;
    damage_window->window_pos.y = win_attr.y;
    damage_window->window_size.x = win_attr.width;
    damage_window->window_size.y = win_attr.height;
    damage_window->damage = damage;
    damage_window->refcount = 1;
    return true;
}

void gsr_damage_stop_tracking_window(gsr_damage *self, int64_t window) {
    if(window == None)
        return;

    const int damage_window_index = gsr_damage_get_tracked_window_index(self, window);
    if(damage_window_index == -1)
        return;

    gsr_damage_window *damage_window = &self->windows_tracked[damage_window_index];
    --damage_window->refcount;
    if(damage_window->refcount <= 0) {
        XSelectInput(self->display, damage_window->window_id, 0);
        XDamageDestroy(self->display, damage_window->damage);
        self->windows_tracked[damage_window_index] = self->windows_tracked[self->num_windows_tracked - 1];
        --self->num_windows_tracked;
    }
}

static gsr_monitor* gsr_damage_get_monitor_by_id(gsr_damage *self, RRCrtc id) {
    for(int i = 0; i < self->num_monitors; ++i) {
        if(self->monitors[i].monitor_identifier == id)
            return &self->monitors[i];
    }
    return NULL;
}

static gsr_monitor* gsr_damage_get_monitor_by_name(gsr_damage *self, const char *name) {
    for(int i = 0; i < self->num_monitors; ++i) {
        if(strcmp(self->monitors[i].name, name) == 0)
            return &self->monitors[i];
    }
    return NULL;
}

bool gsr_damage_start_tracking_monitor(gsr_damage *self, const char *monitor_name) {
    if(self->damage_event == 0)
        return false;

    if(strcmp(monitor_name, "screen-direct") == 0 || strcmp(monitor_name, "screen-direct-force") == 0)
        monitor_name = NULL;

    if(!monitor_name) {
        ++self->all_monitors_tracked_refcount;
        return true;
    }

    const int damage_monitor_index = gsr_damage_get_tracked_monitor_index(self, monitor_name);
    if(damage_monitor_index != -1) {
        ++self->monitors_tracked[damage_monitor_index].refcount;
        return true;
    }

    if(self->num_monitors_tracked + 1 > GSR_DAMAGE_MAX_TRACKED_TARGETS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_start_tracking_monitor: max monitor targets reached");
        return false;
    }

    char *monitor_name_copy = strdup(monitor_name);
    if(!monitor_name_copy) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_start_tracking_monitor: strdup failed for monitor: %s", monitor_name);
        return false;
    }

    gsr_monitor *monitor = gsr_damage_get_monitor_by_name(self, monitor_name);
    if(!monitor) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_damage_start_tracking_monitor: failed to find monitor: %s", monitor_name);
        free(monitor_name_copy);
        return false;
    }

    gsr_damage_monitor *damage_monitor = &self->monitors_tracked[self->num_monitors_tracked];
    ++self->num_monitors_tracked;

    damage_monitor->monitor_name = monitor_name_copy;
    damage_monitor->monitor = monitor;
    damage_monitor->refcount = 1;
    return true;
}

void gsr_damage_stop_tracking_monitor(gsr_damage *self, const char *monitor_name) {
    if(strcmp(monitor_name, "screen-direct") == 0 || strcmp(monitor_name, "screen-direct-force") == 0)
        monitor_name = NULL;

    if(!monitor_name) {
        --self->all_monitors_tracked_refcount;
        if(self->all_monitors_tracked_refcount < 0)
            self->all_monitors_tracked_refcount = 0;
        return;
    }

    const int damage_monitor_index = gsr_damage_get_tracked_monitor_index(self, monitor_name);
    if(damage_monitor_index == -1)
        return;

    gsr_damage_monitor *damage_monitor = &self->monitors_tracked[damage_monitor_index];
    --damage_monitor->refcount;
    if(damage_monitor->refcount <= 0) {
        free(damage_monitor->monitor_name);
        self->monitors_tracked[damage_monitor_index] = self->monitors_tracked[self->num_monitors_tracked - 1];
        --self->num_monitors_tracked;
    }
}

static void gsr_damage_on_crtc_change(gsr_damage *self, XEvent *xev) {
    const XRRCrtcChangeNotifyEvent *rr_crtc_change_event = (XRRCrtcChangeNotifyEvent*)xev;
    if(rr_crtc_change_event->crtc == 0)
        return;

    if(rr_crtc_change_event->width == 0 || rr_crtc_change_event->height == 0)
        return;

    gsr_monitor *monitor = gsr_damage_get_monitor_by_id(self, rr_crtc_change_event->crtc);
    if(!monitor)
        return;

    if(rr_crtc_change_event->x != monitor->pos.x || rr_crtc_change_event->y != monitor->pos.y ||
        (int)rr_crtc_change_event->width != monitor->size.x || (int)rr_crtc_change_event->height != monitor->size.y) {
        monitor->pos.x = rr_crtc_change_event->x;
        monitor->pos.y = rr_crtc_change_event->y;

        monitor->size.x = rr_crtc_change_event->width;
        monitor->size.y = rr_crtc_change_event->height;
    }
}

static void gsr_damage_on_output_change(gsr_damage *self, XEvent *xev) {
    const XRROutputChangeNotifyEvent *rr_output_change_event = (XRROutputChangeNotifyEvent*)xev;
    if(!rr_output_change_event->output)
        return;

    gsr_damage_deinit_monitors(self);
    for_each_active_monitor_output_x11_not_cached(self->display, add_monitor_callback, self);
}

static void gsr_damage_on_randr_event(gsr_damage *self, XEvent *xev) {
    const XRRNotifyEvent *rr_event = (XRRNotifyEvent*)xev;
    switch(rr_event->subtype) {
        case RRNotify_CrtcChange:
            gsr_damage_on_crtc_change(self, xev);
            break;
        case RRNotify_OutputChange:
            gsr_damage_on_output_change(self, xev);
            break;
    }
}

static void gsr_damage_on_damage_event(gsr_damage *self, XEvent *xev) {
    const XDamageNotifyEvent *de = (XDamageNotifyEvent*)xev;
    XserverRegion region = XFixesCreateRegion(self->display, NULL, 0);
    /* Subtract all the damage, repairing the window */
    XDamageSubtract(self->display, de->damage, None, region);

    if(self->all_monitors_tracked_refcount > 0)
        self->damaged = true;

    if(!self->damaged) {
        for(int i = 0; i < self->num_windows_tracked; ++i) {
            const gsr_damage_window *damage_window = &self->windows_tracked[i];
            if(damage_window->window_id == (int64_t)de->drawable) {
                self->damaged = true;
                break;
            }
        }
    }

    if(!self->damaged) {
        int num_rectangles = 0;
        XRectangle *rectangles = XFixesFetchRegion(self->display, region, &num_rectangles);
        if(rectangles) {
            for(int i = 0; i < num_rectangles; ++i) {
                const gsr_rectangle damage_region = { (vec2i){rectangles[i].x, rectangles[i].y}, (vec2i){rectangles[i].width, rectangles[i].height} };
                for(int j = 0; j < self->num_monitors_tracked; ++j) {
                    const gsr_monitor *monitor = self->monitors_tracked[j].monitor;
                    if(!monitor)
                        continue;

                    const gsr_rectangle monitor_region = { monitor->pos, monitor->size };
                    self->damaged = rectangles_intersect(monitor_region, damage_region);
                    if(self->damaged)
                        goto intersection_found;
                }
            }

            intersection_found:
            XFree(rectangles);
        }
    }

    XFixesDestroyRegion(self->display, region);
    XFlush(self->display);
}

static void gsr_damage_on_tick_cursor(gsr_damage *self) {
    if(self->cursor->position.x == self->cursor_pos.x && self->cursor->position.y == self->cursor_pos.y)
        return;

    self->cursor_pos = self->cursor->position;
    if(self->damaged)
        return;

    const gsr_rectangle cursor_region = { self->cursor->position, self->cursor->size };

    if(self->all_monitors_tracked_refcount > 0)
        self->damaged = true;

    if(!self->damaged) {
        for(int i = 0; i < self->num_windows_tracked; ++i) {
            const gsr_damage_window *damage_window = &self->windows_tracked[i];
            const gsr_rectangle window_region = { damage_window->window_pos, damage_window->window_size };
            if(rectangles_intersect(window_region, cursor_region)) {
                self->damaged = true;
                break;
            }
        }
    }

    if(!self->damaged) {
        for(int i = 0; i < self->num_monitors_tracked; ++i) {
            const gsr_monitor *monitor = self->monitors_tracked[i].monitor;
            if(!monitor)
                continue;

            const gsr_rectangle monitor_region = { monitor->pos, monitor->size };
            if(rectangles_intersect(monitor_region, cursor_region)) {
                self->damaged = true;
                break;
            }
        }
    }
}

static void gsr_damage_on_window_configure_notify(gsr_damage *self, XEvent *xev) {
    for(int i = 0; i < self->num_windows_tracked; ++i) {
        gsr_damage_window *damage_window = &self->windows_tracked[i];
        if(damage_window->window_id == (int64_t)xev->xconfigure.window) {
            damage_window->window_pos.x = xev->xconfigure.x;
            damage_window->window_pos.y = xev->xconfigure.y;

            damage_window->window_size.x = xev->xconfigure.width;
            damage_window->window_size.y = xev->xconfigure.height;
            break;
        }
    }
}

void gsr_damage_on_event(gsr_damage *self, XEvent *xev) {
    if(self->damage_event == 0)
        return;

    if(xev->type == ConfigureNotify)
        gsr_damage_on_window_configure_notify(self, xev);

    if(self->randr_event) {
        if(xev->type == self->randr_event + RRScreenChangeNotify)
            XRRUpdateConfiguration(xev);

        if(xev->type == self->randr_event + RRNotify)
            gsr_damage_on_randr_event(self, xev);
    }

    if(self->damage_event && xev->type == self->damage_event + XDamageNotify)
        gsr_damage_on_damage_event(self, xev);
}

void gsr_damage_tick(gsr_damage *self) {
    if(self->damage_event == 0)
        return;

    if(self->track_cursor && self->cursor->visible)
        gsr_damage_on_tick_cursor(self);
}

bool gsr_damage_is_damaged(gsr_damage *self) {
    return self->damage_event == 0 || self->damaged;
}

void gsr_damage_clear(gsr_damage *self) {
    self->damaged = false;
}
