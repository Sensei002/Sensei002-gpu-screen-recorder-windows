#include "../../include/window/x11.h"
#include "../../include/log.h"

#include "../../include/vec2.h"
#include "../../include/defs.h"
#include "../../include/utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <X11/Xlib.h>

#define GSR_MAX_OUTPUTS 32

typedef struct {
    char *name;
    vec2i pos;
    vec2i size;
    uint32_t connector_id;
    gsr_monitor_rotation rotation;
    uint32_t monitor_identifier; /* crtc id */
} gsr_x11_output;

typedef struct {
    Display *display;
    Window window;
    gsr_x11_output outputs[GSR_MAX_OUTPUTS];
    int num_outputs;
    XEvent xev;
} gsr_window_x11;

static void store_x11_monitor(const gsr_monitor *monitor, void *userdata) {
    gsr_window_x11 *window_x11 = userdata;
    if(window_x11->num_outputs == GSR_MAX_OUTPUTS) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "reached maximum outputs (%d), ignoring output %s", GSR_MAX_OUTPUTS, monitor->name);
        return;
    }

    char *monitor_name = strdup(monitor->name);
    if(!monitor_name)
        return;

    const int index = window_x11->num_outputs;
    window_x11->outputs[index].name = monitor_name;
    window_x11->outputs[index].pos = monitor->pos;
    window_x11->outputs[index].size = monitor->size;
    window_x11->outputs[index].connector_id = monitor->connector_id;
    window_x11->outputs[index].rotation = monitor->rotation;
    window_x11->outputs[index].monitor_identifier = monitor->monitor_identifier;
    ++window_x11->num_outputs;
}

static void gsr_window_x11_deinit(gsr_window_x11 *self) {
    if(self->window) {
        XDestroyWindow(self->display, self->window);
        self->window = None;
    }

    for(int i = 0; i < self->num_outputs; ++i) {
        if(self->outputs[i].name) {
            free(self->outputs[i].name);
            self->outputs[i].name = NULL;
        }
    }
    self->num_outputs = 0;
}

static bool gsr_window_x11_init(gsr_window_x11 *self) {
    self->window = XCreateWindow(self->display, DefaultRootWindow(self->display), 0, 0, 16, 16, 0, CopyFromParent, InputOutput, CopyFromParent, 0, NULL);
    if(!self->window) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_window_x11_init failed: failed to create gl window");
        return false;
    }

    self->num_outputs = 0;
    for_each_active_monitor_output_x11_not_cached(self->display, store_x11_monitor, self);
    return true;
}

static void gsr_window_x11_destroy(gsr_window *window) {
    gsr_window_x11 *self = window->priv;
    gsr_window_x11_deinit(self);
    free(self);
    free(window);
}

static bool gsr_window_x11_process_event(gsr_window *window) {
    gsr_window_x11 *self = window->priv;
    if(XPending(self->display)) {
        XNextEvent(self->display, &self->xev);
        return true;
    }
    return false;
}

static XEvent* gsr_window_x11_get_event_data(gsr_window *window) {
    gsr_window_x11 *self = window->priv;
    return &self->xev;
}

static gsr_display_server gsr_window_x11_get_display_server(void) {
    return GSR_DISPLAY_SERVER_X11;
}

static void* gsr_window_x11_get_display(gsr_window *window) {
    gsr_window_x11 *self = window->priv;
    return self->display;
}

static void* gsr_window_x11_get_window(gsr_window *window) {
    gsr_window_x11 *self = window->priv;
    return (void*)self->window;
}

static void gsr_window_x11_for_each_active_monitor_output_cached(const gsr_window *window, active_monitor_callback callback, void *userdata) {
    const gsr_window_x11 *self = window->priv;
    for(int i = 0; i < self->num_outputs; ++i) {
        const gsr_x11_output *output = &self->outputs[i];
        const gsr_monitor monitor = {
            .name = output->name,
            .name_len = strlen(output->name),
            .pos = output->pos,
            .size = output->size,
            .logical_pos = output->pos,
            .logical_size = output->size,
            .connector_id = output->connector_id,
            .rotation = output->rotation,
            .monitor_identifier = output->monitor_identifier
        };
        callback(&monitor, userdata);
    }
}

gsr_window* gsr_window_x11_create(Display *display) {
    gsr_window *window = calloc(1, sizeof(gsr_window));
    if(!window)
        return window;

    gsr_window_x11 *window_x11 = calloc(1, sizeof(gsr_window_x11));
    if(!window_x11) {
        free(window);
        return NULL;
    }

    window_x11->display = display;
    if(!gsr_window_x11_init(window_x11)) {
        free(window_x11);
        free(window);
        return NULL;
    }

    *window = (gsr_window) {
        .destroy = gsr_window_x11_destroy,
        .process_event = gsr_window_x11_process_event,
        .get_event_data = gsr_window_x11_get_event_data,
        .get_display_server = gsr_window_x11_get_display_server,
        .get_display = gsr_window_x11_get_display,
        .get_window = gsr_window_x11_get_window,
        .for_each_active_monitor_output_cached = gsr_window_x11_for_each_active_monitor_output_cached,
        .priv = window_x11
    };

    return window;
}
