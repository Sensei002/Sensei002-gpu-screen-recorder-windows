#ifndef GSR_WINDOW_H
#define GSR_WINDOW_H

#include "../utils.h"
#include <stdbool.h>

typedef union _XEvent XEvent;
typedef struct gsr_window gsr_window;

typedef enum {
    GSR_DISPLAY_SERVER_X11,
    GSR_DISPLAY_SERVER_WAYLAND
} gsr_display_server;

struct gsr_window {
    void (*destroy)(gsr_window *self);
    /* Returns true if an event is available */
    bool (*process_event)(gsr_window *self);
    XEvent* (*get_event_data)(gsr_window *self); /* can be NULL */
    gsr_display_server (*get_display_server)(void);
    void* (*get_display)(gsr_window *self);
    void* (*get_window)(gsr_window *self);
    void (*for_each_active_monitor_output_cached)(const gsr_window *self, active_monitor_callback callback, void *userdata);
    bool (*get_monitor_hdr_info)(const gsr_window *self, const char *monitor_name, gsr_monitor_hdr_info *hdr_info); /* can be NULL */
    void *priv;
};

void gsr_window_destroy(gsr_window *self);

/* Returns true if an event is available */
bool gsr_window_process_event(gsr_window *self);
XEvent* gsr_window_get_event_data(gsr_window *self);
gsr_display_server gsr_window_get_display_server(const gsr_window *self);
void* gsr_window_get_display(gsr_window *self);
void* gsr_window_get_window(gsr_window *self);
void gsr_window_for_each_active_monitor_output_cached(const gsr_window *self, active_monitor_callback callback, void *userdata);
bool gsr_window_get_monitor_hdr_info(const gsr_window *self, const char *monitor_name, gsr_monitor_hdr_info *hdr_info);

#endif /* GSR_WINDOW_H */
