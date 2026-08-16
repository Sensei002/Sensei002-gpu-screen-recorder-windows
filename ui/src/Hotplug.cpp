#include "../include/Hotplug.hpp"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>

namespace gsr {
    Hotplug::~Hotplug() {
        if(fd > 0)
            close(fd);
    }

    bool Hotplug::start() {
        if(started)
            return false;

        struct sockaddr_nl nls = {
            AF_NETLINK,
            0,
            (unsigned int)getpid(),
            (unsigned int)-1
        };

        fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
        if(fd == -1)
            return false; /* Not root user */

        if(bind(fd, (const struct sockaddr*)&nls, sizeof(struct sockaddr_nl))) {
            close(fd);
            fd = -1;
            return false;
        }

        started = true;
        return true;
    }

    int Hotplug::steal_fd() {
        const int val = fd;
        fd = -1;
        return val;
    }

    void Hotplug::process_event_data(int fd, const HotplugEventCallback &callback) {
        const int bytes_read = read(fd, event_data, sizeof(event_data) - 1);
        if(bytes_read <= 0)
            return;
        event_data[bytes_read] = '\0';

        /* Hotplug data ends with a newline and a null terminator */
        int data_index = 0;
        while(data_index < bytes_read) {
            parse_netlink_data(event_data + data_index, callback);
            data_index += strlen(event_data + data_index) + 1; /* Skip null terminator as well */
        }
    }

    /* TODO: This assumes SUBSYSTEM= is output before DEVNAME=, is that always true? */
    void Hotplug::parse_netlink_data(const char *line, const HotplugEventCallback &callback) {
        if(strncmp(line, "ACTION=", 7) == 0) {
            event_is_add = strncmp(line+7, "add", 3) == 0;
            event_is_remove = strncmp(line+7, "remove", 6) == 0;
            subsystem_is_input = false;
        } else if(event_is_add || event_is_remove) {
            if(strcmp(line, "SUBSYSTEM=input") == 0)
                subsystem_is_input = true;

            if(subsystem_is_input && strncmp(line, "DEVNAME=", 8) == 0) {
                if(event_is_add)
                    callback(HotplugAction::ADD, line+8);
                else if(event_is_remove)
                    callback(HotplugAction::REMOVE, line+8);

                event_is_add = false;
                event_is_remove = false;
            }
        }
    }
}
