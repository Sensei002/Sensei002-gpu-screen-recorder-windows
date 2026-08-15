#include "../../include/window/window.h"
#include <stddef.h>

void gsr_window_destroy(gsr_window *self) {
    if(self)
        self->destroy(self);
}

bool gsr_window_process_event(gsr_window *self) {
    return self->process_event(self);
}

XEvent* gsr_window_get_event_data(gsr_window *self) {
    if(self->get_event_data)
        return self->get_event_data(self);
    return NULL;
}

gsr_display_server gsr_window_get_display_server(const gsr_window *self) {
    return self->get_display_server();
}

void* gsr_window_get_display(gsr_window *self) {
    return self->get_display(self);
}

void* gsr_window_get_window(gsr_window *self) {
    return self->get_window(self);
}

void gsr_window_for_each_active_monitor_output_cached(const gsr_window *self, active_monitor_callback callback, void *userdata) {
    self->for_each_active_monitor_output_cached(self, callback, userdata);
}

bool gsr_window_get_monitor_hdr_info(const gsr_window *self, const char *monitor_name, gsr_monitor_hdr_info *hdr_info) {
    if(self->get_monitor_hdr_info)
        return self->get_monitor_hdr_info(self, monitor_name, hdr_info);
    return false;
}
