#pragma once

#include <functional>

namespace gsr {
    enum class HotplugAction {
        ADD,
        REMOVE
    };

    using HotplugEventCallback = std::function<void(HotplugAction hotplug_action, const char *devname)>;

    class Hotplug {
    public:
        Hotplug() = default;
        Hotplug(const Hotplug&) = delete;
        Hotplug& operator=(const Hotplug&) = delete;
        ~Hotplug();

        bool start();
        int steal_fd();
        void process_event_data(int fd, const HotplugEventCallback &callback);
    private:
        void parse_netlink_data(const char *line, const HotplugEventCallback &callback);
    private:
        int fd = -1;
        bool started = false;
        bool event_is_add = false;
        bool event_is_remove = false;
        bool subsystem_is_input = false;
        char event_data[1024];
    };
}