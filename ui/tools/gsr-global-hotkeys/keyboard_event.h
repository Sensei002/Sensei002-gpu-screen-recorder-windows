#ifndef KEYBOARD_EVENT_H
#define KEYBOARD_EVENT_H

/* Read keyboard input from linux /dev/input/eventN devices, with hotplug support */

#include "hotplug.h"

/* C stdlib */
#include <stdbool.h>
#include <stdint.h>

/* POSIX */
#include <poll.h>
#include <pthread.h>

/* LINUX */
#include <linux/input-event-codes.h>

#define MAX_EVENT_POLLS 32
#define MAX_CLOSE_FDS 256
#define MAX_GLOBAL_HOTKEYS 40

typedef enum {
    KEYBOARD_MODKEY_LALT   = 1 << 0,
    KEYBOARD_MODKEY_RALT   = 1 << 1,
    KEYBOARD_MODKEY_LSUPER = 1 << 2,
    KEYBOARD_MODKEY_RSUPER = 1 << 3,
    KEYBOARD_MODKEY_LCTRL  = 1 << 4,
    KEYBOARD_MODKEY_RCTRL  = 1 << 5,
    KEYBOARD_MODKEY_LSHIFT = 1 << 6,
    KEYBOARD_MODKEY_RSHIFT = 1 << 7
} keyboard_modkeys;

typedef enum {
    KEYBOARD_BUTTON_RELEASED,
    KEYBOARD_BUTTON_PRESSED
} keyboard_button_state;

typedef struct {
    int dev_input_id;
    bool grabbed;
    bool is_non_keyboard_device;
    bool is_possibly_non_keyboard_device;
    bool gsr_ui_virtual_keyboard;
    unsigned char *key_states;
    unsigned char *key_presses_grabbed;
    int num_keys_pressed;
} event_extra_data;

typedef enum {
    KEYBOARD_GRAB_TYPE_ALL,
    KEYBOARD_GRAB_TYPE_VIRTUAL,
    KEYBOARD_GRAB_TYPE_NO_GRAB
} keyboard_grab_type;

typedef struct {
    uint32_t key;
    uint32_t modifiers; /* keyboard_modkeys bitmask */
    char *action;
} global_hotkey;

typedef struct {
    struct pollfd event_polls[MAX_EVENT_POLLS];      /* Current size is |num_event_polls| */
    event_extra_data event_extra_data[MAX_EVENT_POLLS]; /* Current size is |num_event_polls| */
    int num_event_polls;

    int hotplug_event_index;
    int uinput_fd;
    int timer_fd;
    bool stdin_failed;
    bool check_grab_lock;
    double uinput_written_time_seconds;
    keyboard_grab_type grab_type;

    pthread_t close_dev_input_fds_thread;
    pthread_mutex_t close_dev_input_mutex;
    int close_fds[MAX_CLOSE_FDS];
    int num_close_fds;
    bool running;

    char stdin_command_data[512];
    int stdin_command_data_size;

    global_hotkey global_hotkeys[MAX_GLOBAL_HOTKEYS];
    int num_global_hotkeys;

    hotplug_event hotplug_ev;

    uint32_t modifier_button_states;
} keyboard_event;

bool keyboard_event_init(keyboard_event *self, bool exclusive_grab, keyboard_grab_type grab_type);
void keyboard_event_deinit(keyboard_event *self);

/* If |timeout_milliseconds| is -1 then wait until an event is received */
void keyboard_event_poll_events(keyboard_event *self, int timeout_milliseconds);
bool keyboard_event_stdin_has_failed(const keyboard_event *self);

#endif /* KEYBOARD_EVENT_H */
