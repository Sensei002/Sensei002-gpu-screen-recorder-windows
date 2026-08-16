#include "../../include/GlobalHotkeys/GlobalHotkeysJoystick.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/eventfd.h>

namespace gsr {
    static constexpr int button_pressed = 1;

    // Returns -1 on error
    static int get_dev_input_event_id_from_filepath(const char *dev_input_filepath) {
        if(strncmp(dev_input_filepath, "/dev/input/event", 16) != 0)
            return -1;

        int dev_input_id = -1;
        if(sscanf(dev_input_filepath + 16, "%d", &dev_input_id) == 1)
            return dev_input_id;
        return -1;
    }

    static inline bool supports_key(unsigned char *key_bits, unsigned int key) {
        return key_bits[key/8] & (1 << (key % 8));
    }

    static bool supports_joystick_keys(unsigned char *key_bits) {
        const int keys[7] = { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_SELECT, BTN_START, BTN_SELECT };
        for(int i = 0; i < 7; ++i) {
            if(supports_key(key_bits, keys[i]))
                return true;
        }
        return false;
    }

    static bool is_input_device_joystick(int input_fd) {
        unsigned long evbit = 0;
        ioctl(input_fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
        if((evbit & (1 << EV_SYN)) && (evbit & (1 << EV_KEY))) {
            unsigned char key_bits[KEY_MAX/8 + 1];
            memset(key_bits, 0, sizeof(key_bits));
            ioctl(input_fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), &key_bits);
            return supports_joystick_keys(key_bits);
        }
        return false;
    }

    GlobalHotkeysJoystick::~GlobalHotkeysJoystick() {
        if(event_fd > 0) {
            const uint64_t exit = 1;
            write(event_fd, &exit, sizeof(exit));
        }

        if(read_thread.joinable())
            read_thread.join();

        if(event_fd > 0) {
            close(event_fd);
            event_fd = 0;
        }

        close_fd_cv.notify_one();
        if(close_fd_thread.joinable())
            close_fd_thread.join();

        for(int fd : fds_to_close) {
            close(fd);
        }

        for(int i = 0; i < num_poll_fd; ++i) {
            if(poll_fd[i].fd > 0)
                close(poll_fd[i].fd);
        }
    }

    bool GlobalHotkeysJoystick::start() {
        if(num_poll_fd > 0)
            return false;

        event_fd = eventfd(0, 0);
        if(event_fd <= 0)
            return false;

        event_index = num_poll_fd;
        poll_fd[num_poll_fd] = {
            event_fd,
            POLLIN,
            0
        };
        extra_data[num_poll_fd] = {
            -1
        };
        ++num_poll_fd;

        if(!hotplug.start()) {
            fprintf(stderr, "Warning: failed to setup hotplugging\n");
        } else {
            hotplug_poll_index = num_poll_fd;
            poll_fd[num_poll_fd] = {
                hotplug.steal_fd(),
                POLLIN,
                0
            };
            extra_data[num_poll_fd] = {
                -1
            };
            ++num_poll_fd;
        }

        add_all_joystick_devices();

        read_thread = std::thread(&GlobalHotkeysJoystick::read_events, this);
        close_fd_thread = std::thread(&GlobalHotkeysJoystick::close_fds, this);
        return true;
    }

    bool GlobalHotkeysJoystick::bind_action(const std::string &id, GlobalHotkeyCallback callback) {
        if(num_poll_fd == 0)
            return false;
        return bound_actions_by_id.insert(std::make_pair(id, std::move(callback))).second;
    }

    void GlobalHotkeysJoystick::poll_events() {
        if(num_poll_fd == 0)
            return;

        if(save_replay) {
            save_replay = false;
            auto it = bound_actions_by_id.find("save_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_replay");
        }

        if(save_1_min_replay) {
            save_1_min_replay = false;
            auto it = bound_actions_by_id.find("save_1_min_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_1_min_replay");
        }

        if(save_10_min_replay) {
            save_10_min_replay = false;
            auto it = bound_actions_by_id.find("save_10_min_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_10_min_replay");
        }

        if(take_screenshot) {
            take_screenshot = false;
            auto it = bound_actions_by_id.find("take_screenshot");
            if(it != bound_actions_by_id.end())
                it->second("take_screenshot");
        }

        if(toggle_record) {
            toggle_record = false;
            auto it = bound_actions_by_id.find("toggle_record");
            if(it != bound_actions_by_id.end())
                it->second("toggle_record");
        }

        if(toggle_replay) {
            toggle_replay = false;
            auto it = bound_actions_by_id.find("toggle_replay");
            if(it != bound_actions_by_id.end())
                it->second("toggle_replay");
        }

        if(toggle_show) {
            toggle_show = false;
            auto it = bound_actions_by_id.find("toggle_show");
            if(it != bound_actions_by_id.end())
                it->second("toggle_show");
        }
    }

    // Retarded linux takes very long time to close /dev/input/eventN files, even though they are virtual and opened read-only
    void GlobalHotkeysJoystick::close_fds() {
        std::vector<int> current_fds_to_close;
        while(event_fd > 0) {
            {
                std::unique_lock<std::mutex> lock(close_fd_mutex);
                close_fd_cv.wait(lock, [this]{ return !fds_to_close.empty() || event_fd <= 0; });
            }

            {
                std::lock_guard<std::mutex> lock(close_fd_mutex);
                current_fds_to_close = std::move(fds_to_close);
                fds_to_close.clear();
            }

            for(int fd : current_fds_to_close) {
                close(fd);
            }
            current_fds_to_close.clear();
        }
    }

    void GlobalHotkeysJoystick::read_events() {
        input_event event;
        while(poll(poll_fd, num_poll_fd, -1) > 0) {
            for(int i = 0; i < num_poll_fd; ++i) {
                if(poll_fd[i].revents & (POLLHUP|POLLERR|POLLNVAL)) {
                    if(i == event_index)
                        goto done;

                    char dev_input_filepath[256];
                    snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/input/event%d", extra_data[i].dev_input_id);
                    fprintf(stderr, "Info: removed joystick: %s\n", dev_input_filepath);
                    if(remove_poll_fd(i))
                        --i; // This item was removed so we want to repeat the same index to continue to the next item

                    continue;
                }

                if(!(poll_fd[i].revents & POLLIN)) {
                    poll_fd[i].revents = 0;
                    continue;
                }

                if(i == event_index) {
                    goto done;
                } else if(i == hotplug_poll_index) {
                    hotplug.process_event_data(poll_fd[i].fd, [&](HotplugAction hotplug_action, const char *devname) {
                        switch(hotplug_action) {
                            case HotplugAction::ADD: {
                                add_device(devname, false);
                                break;
                            }
                            case HotplugAction::REMOVE: {
                                if(remove_device(devname))
                                    --i; // This item was removed so we want to repeat the same index to continue to the next item
                                break;
                            }
                        }
                    });
                } else {
                    process_input_event(poll_fd[i].fd, event);
                }

                poll_fd[i].revents = 0;
            }
        }

        done:
        ;
    }

    void GlobalHotkeysJoystick::process_input_event(int fd, input_event &event) {
        if(read(fd, &event, sizeof(event)) != sizeof(event))
            return;

        if(event.type == EV_KEY) {
            switch(event.code) {
                case BTN_MODE: {
                    playstation_button_pressed = (event.value == button_pressed);
                    break;
                }
                case BTN_START: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        toggle_show = true;
                    break;
                }
                case BTN_SOUTH: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        save_1_min_replay = true;
                    break;
                }
                case BTN_NORTH: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        save_10_min_replay = true;
                    break;
                }
            }
        } else if(event.type == EV_ABS && playstation_button_pressed) {
            const bool prev_up_pressed = up_pressed;
            const bool prev_down_pressed = down_pressed;
            const bool prev_left_pressed = left_pressed;
            const bool prev_right_pressed = right_pressed;

            if(event.code == ABS_HAT0Y) {
                up_pressed = event.value == -1;
                down_pressed = event.value == 1;
            } else if(event.code == ABS_HAT0X) {
                left_pressed = event.value == -1;
                right_pressed = event.value == 1;
            }

            if(up_pressed && !prev_up_pressed)
                take_screenshot = true;
            else if(down_pressed && !prev_down_pressed)
                save_replay = true;
            else if(left_pressed && !prev_left_pressed)
                toggle_record = true;
            else if(right_pressed && !prev_right_pressed)
                toggle_replay = true;
        }
    }

    void GlobalHotkeysJoystick::add_all_joystick_devices() {
        DIR *dir = opendir("/dev/input");
        if(!dir) {
            fprintf(stderr, "Error: failed to open /dev/input, error: %s\n", strerror(errno));
            return;
        }

        char dev_input_filepath[1024];
        for(;;) {
            struct dirent *entry = readdir(dir);
            if(!entry)
                break;

            if(strncmp(entry->d_name, "event", 5) != 0)
                continue;

            snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/input/%s", entry->d_name);
            add_device(dev_input_filepath, false);
        }

        closedir(dir);
    }

    bool GlobalHotkeysJoystick::add_device(const char *dev_input_filepath, bool print_error) {
        if(num_poll_fd >= max_js_poll_fd) {
            fprintf(stderr, "Warning: failed to add joystick device %s, too many joysticks have been added\n", dev_input_filepath);
            return false;
        }

        const int dev_input_id = get_dev_input_event_id_from_filepath(dev_input_filepath);
        if(dev_input_id == -1)
            return false;

        const int fd = open(dev_input_filepath, O_RDONLY);
        if(fd <= 0) {
            if(print_error)
                fprintf(stderr, "Error: failed to add joystick %s, error: %s\n", dev_input_filepath, strerror(errno));
            return false;
        }

        if(!is_input_device_joystick(fd)) {
            {
                std::lock_guard<std::mutex> lock(close_fd_mutex);
                fds_to_close.push_back(fd);
            }
            close_fd_cv.notify_one();
            return false;
        }

        poll_fd[num_poll_fd] = {
            fd,
            POLLIN,
            0
        };

        extra_data[num_poll_fd] = {
            dev_input_id
        };

        //const DeviceId device_id = joystick_get_device_id(dev_input_filepath);

        ++num_poll_fd;
        fprintf(stderr, "Info: added joystick: %s\n", dev_input_filepath);
        return true;
    }

    bool GlobalHotkeysJoystick::remove_device(const char *dev_input_filepath) {
        const int dev_input_id = get_dev_input_event_id_from_filepath(dev_input_filepath);
        if(dev_input_id == -1)
            return false;

        const int poll_fd_index = get_poll_fd_index_by_dev_input_id(dev_input_id);
        if(poll_fd_index == -1)
            return false;

        fprintf(stderr, "Info: removed joystick: %s\n", dev_input_filepath);
        return remove_poll_fd(poll_fd_index);
    }

    bool GlobalHotkeysJoystick::remove_poll_fd(int index) {
        if(index < 0 || index >= num_poll_fd)
            return false;

        if(poll_fd[index].fd > 0) {
            {
                std::lock_guard<std::mutex> lock(close_fd_mutex);
                fds_to_close.push_back(poll_fd[index].fd);
            }
            close_fd_cv.notify_one();
        }

        for(int i = index + 1; i < num_poll_fd; ++i) {
            poll_fd[i - 1] = poll_fd[i];
            extra_data[i - 1] = extra_data[i];
        }
        --num_poll_fd;
        return true;
    }

    int GlobalHotkeysJoystick::get_poll_fd_index_by_dev_input_id(int dev_input_id) const {
        for(int i = 0; i < num_poll_fd; ++i) {
            if(dev_input_id == extra_data[i].dev_input_id)
                return i;
        }
        return -1;
    }
}
