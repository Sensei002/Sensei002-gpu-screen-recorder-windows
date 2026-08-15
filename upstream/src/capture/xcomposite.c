#include "../../include/capture/xcomposite.h"
#include "../../include/log.h"
#include "../../include/window_texture.h"
#include "../../include/utils.h"
#include "../../include/color_conversion.h"
#include "../../include/window/window.h"

#include <stdlib.h>

#include <X11/Xlib.h>

typedef struct {
    gsr_capture_xcomposite_params params;
    Display *display;

    bool should_stop;
    bool stop_is_error;
    bool window_resized;
    bool follow_focused_initialized;
    bool init_new_window;

    Window window;
    vec2i window_pos;
    vec2i window_size;
    vec2i texture_size;
    double window_resize_timer;
    
    WindowTexture window_texture;

    Atom net_active_window_atom;

    bool clear_background;
} gsr_capture_xcomposite;

static void gsr_capture_xcomposite_stop(gsr_capture_xcomposite *self) {
    window_texture_deinit(&self->window_texture);
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static Window get_focused_window(Display *display, Atom net_active_window_atom) {
    Atom type;
    int format = 0;
    unsigned long num_items = 0;
    unsigned long bytes_after = 0;
    unsigned char *properties = NULL;
    if(XGetWindowProperty(display, DefaultRootWindow(display), net_active_window_atom, 0, 1024, False, AnyPropertyType, &type, &format, &num_items, &bytes_after, &properties) == Success && properties) {
        Window focused_window = *(unsigned long*)properties;
        XFree(properties);
        return focused_window;
    }
    return None;
}

static int gsr_capture_xcomposite_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    gsr_capture_xcomposite *self = cap->priv;

    if(self->params.follow_focused) {
        self->net_active_window_atom = XInternAtom(self->display, "_NET_ACTIVE_WINDOW", False);
        if(!self->net_active_window_atom) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_start failed: failed to get _NET_ACTIVE_WINDOW atom");
            return -1;
        }
        self->window = get_focused_window(self->display, self->net_active_window_atom);
    } else {
        self->window = self->params.window;
    }

    /* TODO: Do these in tick, and allow error if follow_focused */

    XWindowAttributes attr;
    if(!XGetWindowAttributes(self->display, self->window, &attr) && !self->params.follow_focused) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_start failed: invalid window id: %lu", self->window);
        return -1;
    }

    self->window_pos.x = attr.x;
    self->window_pos.y = attr.y;

    self->window_size.x = max_int(attr.width, 0);
    self->window_size.y = max_int(attr.height, 0);

    // TODO: Get select and add these on top of it and then restore at the end. Also do the same in other xcomposite
    XSelectInput(self->display, self->window, StructureNotifyMask | ExposureMask);

    if(window_texture_init(&self->window_texture, self->display, self->window, self->params.egl) != 0 && !self->params.follow_focused) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_start: failed to get window texture for window %ld", (long)self->window);
        return -1;
    }

    self->texture_size.x = self->window_texture.window_width;
    self->texture_size.y = self->window_texture.window_height;

    if(self->params.output_resolution.x == 0 && self->params.output_resolution.y == 0) {
        capture_metadata->video_size = self->texture_size;
    } else if(self->params.follow_focused) {
        /* The video size has to stay the same even when the focused window changes, so it cant be based on the size of the captured window */
        capture_metadata->video_size = self->params.output_resolution;
    } else {
        capture_metadata->video_size = scale_keep_aspect_ratio(self->texture_size, self->params.output_resolution);
    }

    self->window_resize_timer = clock_get_monotonic_seconds();
    return 0;
}

static void gsr_capture_xcomposite_tick(gsr_capture *cap) {
    gsr_capture_xcomposite *self = cap->priv;

    if(self->params.follow_focused && !self->follow_focused_initialized) {
        self->init_new_window = true;
    }

    if(self->init_new_window) {
        self->init_new_window = false;
        Window focused_window = get_focused_window(self->display, self->net_active_window_atom);
        if(focused_window != self->window || !self->follow_focused_initialized) {
            self->follow_focused_initialized = true;
            XSelectInput(self->display, self->window, 0);
            self->window = focused_window;
            XSelectInput(self->display, self->window, StructureNotifyMask | ExposureMask);

            XWindowAttributes attr;
            attr.width = 0;
            attr.height = 0;
            if(!XGetWindowAttributes(self->display, self->window, &attr))
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_tick failed: invalid window id: %lu", self->window);

            self->window_pos.x = attr.x;
            self->window_pos.y = attr.y;

            self->window_size.x = max_int(attr.width, 0);
            self->window_size.y = max_int(attr.height, 0);

            window_texture_deinit(&self->window_texture);
            window_texture_init(&self->window_texture, self->display, self->window, self->params.egl); // TODO: Do not do the below window_texture_on_resize after this

            self->texture_size.x = self->window_texture.window_width;
            self->texture_size.y = self->window_texture.window_height;

            self->window_resized = false;
            self->clear_background = true;
        }
    }

    const double window_resize_timeout = 1.0; // 1 second
    if(self->window_resized && clock_get_monotonic_seconds() - self->window_resize_timer >= window_resize_timeout) {
        self->window_resized = false;

        if(window_texture_on_resize(&self->window_texture) != 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_tick: window_texture_on_resize failed");
            //self->should_stop = true;
            //self->stop_is_error = true;
            return;
        }

        self->texture_size.x = self->window_texture.window_width;
        self->texture_size.y = self->window_texture.window_height;

        self->clear_background = true;
    }
}

static void gsr_capture_xcomposite_on_event(gsr_capture *cap, gsr_egl *egl) {
    gsr_capture_xcomposite *self = cap->priv;
    XEvent *xev = gsr_window_get_event_data(egl->window);
    switch(xev->type) {
        case DestroyNotify: {
            /* Window died (when not following focused window), so we stop recording */
            if(!self->params.follow_focused && xev->xdestroywindow.window == self->window) {
                self->should_stop = true;
                self->stop_is_error = false;
            }
            break;
        }
        case Expose: {
            /* Requires window texture recreate */
            if(xev->xexpose.count == 0 && xev->xexpose.window == self->window) {
                self->window_resize_timer = clock_get_monotonic_seconds();
                self->window_resized = true;
            }
            break;
        }
        case ConfigureNotify: {
            // TODO: Use PresentConfigureNotify instead
            self->window_pos.x = xev->xconfigure.x;
            self->window_pos.y = xev->xconfigure.y;

            /* Window resized */
            if(xev->xconfigure.window == self->window && (xev->xconfigure.width != self->window_size.x || xev->xconfigure.height != self->window_size.y)) {
                self->window_size.x = max_int(xev->xconfigure.width, 0);
                self->window_size.y = max_int(xev->xconfigure.height, 0);
                self->window_resize_timer = clock_get_monotonic_seconds();
                self->window_resized = true;
            }
            break;
        }
        case PropertyNotify: {
            /* Focused window changed */
            if(self->params.follow_focused && xev->xproperty.atom == self->net_active_window_atom) {
                self->init_new_window = true;
            }
            break;
        }
    }
}

static bool gsr_capture_xcomposite_should_stop(gsr_capture *cap, bool *err) {
    gsr_capture_xcomposite *self = cap->priv;
    if(self->should_stop) {
        if(err)
            *err = self->stop_is_error;
        return true;
    }

    if(err)
        *err = false;
    return false;
}

static void gsr_capture_xcomposite_pre_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    (void)capture_metadata;
    gsr_capture_xcomposite *self = cap->priv;

    if(self->clear_background) {
        self->clear_background = false;
        color_conversion->schedule_clear = true;
    }
}

static int gsr_capture_xcomposite_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_xcomposite *self = cap->priv;

    const vec2i output_size = scale_keep_aspect_ratio(self->texture_size, capture_metadata->recording_size);
    const vec2i target_pos = gsr_capture_get_target_position(output_size, capture_metadata);

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    gsr_color_conversion_draw(color_conversion, window_texture_get_opengl_texture_id(&self->window_texture),
        target_pos, output_size,
        (vec2i){0, 0}, self->texture_size, self->texture_size,
        GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

    if(self->params.record_cursor && self->params.cursor->visible) {
        const vec2d scale = {
            self->texture_size.x == 0 ? 0 : (double)output_size.x / (double)self->texture_size.x,
            self->texture_size.y == 0 ? 0 : (double)output_size.y / (double)self->texture_size.y
        };

        const vec2i cursor_pos = {
            target_pos.x + (self->params.cursor->position.x - self->params.cursor->hotspot.x - self->window_pos.x) * scale.x,
            target_pos.y + (self->params.cursor->position.y - self->params.cursor->hotspot.y - self->window_pos.y) * scale.y
        };

        self->params.egl->glEnable(GL_SCISSOR_TEST);
        self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

        gsr_color_conversion_draw(color_conversion, self->params.cursor->texture_id,
            cursor_pos, (vec2i){self->params.cursor->size.x * scale.x, self->params.cursor->size.y * scale.y},
            (vec2i){0, 0}, self->params.cursor->size, self->params.cursor->size,
            GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

        self->params.egl->glDisable(GL_SCISSOR_TEST);
    }

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    return 0;
}

static uint64_t gsr_capture_xcomposite_get_window_id(gsr_capture *cap) {
    gsr_capture_xcomposite *self = cap->priv;
    return self->window;
}

static void gsr_capture_xcomposite_destroy(gsr_capture *cap) {
    if(cap->priv) {
        gsr_capture_xcomposite_stop(cap->priv);
        free(cap->priv);
        cap->priv = NULL;
    }
    free(cap);
}

gsr_capture* gsr_capture_xcomposite_create(const gsr_capture_xcomposite_params *params) {
    if(!params) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_xcomposite_create params is NULL");
        return NULL;
    }

    gsr_capture *cap = calloc(1, sizeof(gsr_capture));
    if(!cap)
        return NULL;

    gsr_capture_xcomposite *cap_xcomp = calloc(1, sizeof(gsr_capture_xcomposite));
    if(!cap_xcomp) {
        free(cap);
        return NULL;
    }

    cap_xcomp->params = *params;
    cap_xcomp->display = gsr_window_get_display(params->egl->window);
    
    *cap = (gsr_capture) {
        .start = gsr_capture_xcomposite_start,
        .on_event = gsr_capture_xcomposite_on_event,
        .tick = gsr_capture_xcomposite_tick,
        .should_stop = gsr_capture_xcomposite_should_stop,
        .pre_capture = gsr_capture_xcomposite_pre_capture,
        .capture = gsr_capture_xcomposite_capture,
        .uses_external_image = NULL,
        .get_window_id = gsr_capture_xcomposite_get_window_id,
        .destroy = gsr_capture_xcomposite_destroy,
        .priv = cap_xcomp
    };

    return cap;
}
