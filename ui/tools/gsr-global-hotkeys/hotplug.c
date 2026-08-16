#include "hotplug.h"

/* C stdlib */
#include <string.h>

/* POSIX */
#include <unistd.h>
#include <sys/socket.h>

/* LINUX */
#include <linux/types.h>
#include <linux/netlink.h>

bool hotplug_event_init(hotplug_event *self) {
    memset(self, 0, sizeof(*self));

    struct sockaddr_nl nls = {
        .nl_family = AF_NETLINK,
        .nl_pid = getpid(),
        .nl_groups = -1
    };

    const int fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if(fd == -1)
        return false;

    if(bind(fd, (void*)&nls, sizeof(struct sockaddr_nl))) {
        close(fd);
        return false;
    }

    self->fd = fd;
    return true;
}

void hotplug_event_deinit(hotplug_event *self) {
    if(self->fd > 0) {
        close(self->fd);
        self->fd = -1;
    }
}

int hotplug_event_steal_fd(hotplug_event *self) {
    const int fd = self->fd;
    self->fd = -1;
    return fd;
}

/* TODO: This assumes SUBSYSTEM= is output before DEVNAME=, is that always true? */
static void hotplug_event_parse_netlink_data(hotplug_event *self, const char *line, hotplug_device_added_callback callback, void *userdata) {
    const char *at_symbol = strchr(line, '@');
    if(at_symbol) {
        self->event_is_add = strncmp(line, "add@", 4) == 0;
        self->subsystem_is_input = false;
    } else if(self->event_is_add) {
        if(strcmp(line, "SUBSYSTEM=input") == 0)
            self->subsystem_is_input = true;

        if(self->subsystem_is_input && strncmp(line, "DEVNAME=", 8) == 0) {
            callback(line+8, userdata);
            self->event_is_add = false;
        }
    }
}

/* Netlink uevent structure is documented here: https://web.archive.org/web/20160127215232/https://www.kernel.org/doc/pending/hotplug.txt */
void hotplug_event_process_event_data(hotplug_event *self, int fd, hotplug_device_added_callback callback, void *userdata) {
    const int bytes_read = read(fd, self->event_data, sizeof(self->event_data) - 1);
    if(bytes_read <= 0)
        return;
    self->event_data[bytes_read] = '\0';

    /* Hotplug data ends with a newline and a null terminator */
    int data_index = 0;
    while(data_index < bytes_read) {
        hotplug_event_parse_netlink_data(self, self->event_data + data_index, callback, userdata);
        data_index += strlen(self->event_data + data_index) + 1; /* Skip null terminator as well */
    }
}
