#ifndef HOTPLUG_H
#define HOTPLUG_H

/* C stdlib */
#include <stdbool.h>

typedef struct {
    int fd;
    bool event_is_add;
    bool subsystem_is_input;
    char event_data[1024];
} hotplug_event;

typedef void (*hotplug_device_added_callback)(const char *devname, void *userdata);

bool hotplug_event_init(hotplug_event *self);
void hotplug_event_deinit(hotplug_event *self);

int hotplug_event_steal_fd(hotplug_event *self);
void hotplug_event_process_event_data(hotplug_event *self, int fd, hotplug_device_added_callback callback, void *userdata);

#endif /* HOTPLUG_H */
