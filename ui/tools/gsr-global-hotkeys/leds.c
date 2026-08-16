#include "leds.h"

/* C stdlib */
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stddef.h>

/* POSIX */
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

/* LINUX */
#include <linux/input.h>

/* Returns -1 on error */
static int read_int_from_file(const char *filepath) {
    const int fd = open(filepath, O_RDONLY);
    if(fd == -1) {
        fprintf(stderr, "Warning: get_max_brightness open error: %s\n", strerror(errno));
        return -1;
    }

    bool success = false;
    int value = 0;
    char buffer[32];
    const ssize_t num_bytes_read = read(fd, buffer, sizeof(buffer));
    if(num_bytes_read > 0) {
        buffer[num_bytes_read] = '\0';
        success = sscanf(buffer, "%d", &value) == 1;
    }

    close(fd);
    return success ? value : -1;
}

static int get_max_brightness(const char *sys_class_path, const char *filename) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/max_brightness", sys_class_path, filename);
    return read_int_from_file(path);
}

static int get_brightness(const char *sys_class_path, const char *filename) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/brightness", sys_class_path, filename);
    return read_int_from_file(path);
}

static bool string_starts_with(const char *str, const char *sub) {
    const int str_len = strlen(str);
    const int sub_len = strlen(sub);
    return str_len >= sub_len && memcmp(str, sub, sub_len) == 0;
}

static bool string_ends_with(const char *str, const char *sub) {
    const int str_len = strlen(str);
    const int sub_len = strlen(sub);
    return str_len >= sub_len && memcmp(str + str_len - sub_len, sub, sub_len) == 0;
}

static int sys_class_led_path_get_event_number(const char *sys_class_path, const char *filename) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s/device", sys_class_path, filename);

    DIR *dir = opendir(path);
    if(!dir)
        return -1;

    int event_number = -1;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        int v = -1;
        if(sscanf(entry->d_name, "event%d", &v) == 1 && v >= 0) {
            event_number = v;
            break;
        }
    }

    closedir(dir);
    return event_number;
}

/*
    We have to do this retardation instead of setting /sys/class/leds brightness since it doesn't work with /dev/uinput
    and we cant loop all /dev/input devices and open and write to them either since closing a /dev/input is very slow on linux.
    So we instead check which devices have the led before opening it.
*/
static bool set_device_leds(const char *led_name_path, bool enabled) {
    DIR *dir = opendir("/sys/class/leds");
    if(!dir)
        return false;

    char dev_input_filepath[1024];
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.')
            continue;

        if(!string_starts_with(entry->d_name, "input") || !string_ends_with(entry->d_name, led_name_path))
            continue;

        const int event_number = sys_class_led_path_get_event_number("/sys/class/leds", entry->d_name);
        if(event_number == -1)
            continue;

        snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/input/event%d", event_number);
        
        const int device_fd = open(dev_input_filepath, O_WRONLY);
        if(device_fd == -1)
            continue;

        int brightness = 0;
        if(enabled) {
            brightness = get_max_brightness("/sys/class/leds", entry->d_name);
            if(brightness < 0)
                brightness = 1;
        }

        struct input_event led_data = {
            .type = EV_LED,
            .code = LED_SCROLLL,
            .value = brightness
        };
        write(device_fd, &led_data, sizeof(led_data));

        struct input_event syn_data = {
            .type = EV_SYN,
            .code = 0,
            .value = 0
        };
        write(device_fd, &syn_data, sizeof(syn_data));

        close(device_fd);
    }

    closedir(dir);
    return true;
}

bool set_leds(const char *led_name, bool enabled) {
    if(strcmp(led_name, "Scroll Lock") == 0) {
        return set_device_leds("::scrolllock", enabled);
    } else {
        fprintf(stderr, "Error: invalid led: \"%s\", expected \"Scroll Lock\"\n", led_name);
        return false;
    }
}

bool get_leds(int event_number, ggh_leds *leds) {
    leds->scroll_lock_brightness = -1;
    leds->num_lock_brightness = -1;
    leds->caps_lock_brightness = -1;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/class/input/event%d/device", event_number);

    DIR *dir = opendir(path);
    if(!dir)
        return false;

    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.')
            continue;

        if(!string_starts_with(entry->d_name, "input"))
            continue;

        if(string_ends_with(entry->d_name, "::scrolllock"))
            leds->scroll_lock_brightness = get_brightness(path, entry->d_name);
        else if(string_ends_with(entry->d_name, "::numlock"))
            leds->num_lock_brightness = get_brightness(path, entry->d_name);
        else if(string_ends_with(entry->d_name, "::capslock"))
            leds->caps_lock_brightness = get_brightness(path, entry->d_name);
    }

    closedir(dir);
    return true;
}
