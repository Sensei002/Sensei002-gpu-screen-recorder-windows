#ifndef MGL_EVENT_H
#define MGL_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct mgl_event mgl_event;

typedef struct {
    int width;
    int height;
} mgl_size_event;

typedef struct {
    uint32_t codepoint;
    int size;
    char str[5]; /* utf8, null terminated */
} mgl_text_event;

typedef struct {
    bool alt;
    bool control;
    bool shift;
    bool system;
} mgl_key_states;

typedef struct {
    int code; /* mgl_key */
    mgl_key_states key_states;
} mgl_key_event;

typedef struct {
    int button; /* mgl_mouse_button */
    int x; /* mouse position relative to left of the window */
    int y; /* mouse position relative to top of the window */
    mgl_key_states key_states;
} mgl_mouse_button_event;

typedef struct {
    int delta; /* positive = up, negative = down */
    int x; /* mouse position relative to left of the window */
    int y; /* mouse position relative to left of the window */
    mgl_key_states key_states;
} mgl_mouse_wheel_scroll_event;

typedef struct {
    int x; /* relative to left of the window */
    int y; /* relative to the top of the window */
    mgl_key_states key_states;
} mgl_mouse_move_event;

typedef struct {
    const char *name; /* This name may only be valid until the next time |mgl_window_poll_event| is called */
    int id;
    int x;
    int y;
    int width;
    int height;
    int refresh_rate; /* 0 if unknown */
} mgl_monitor_generic_event;

typedef mgl_monitor_generic_event mgl_monitor_connected_event;
typedef mgl_monitor_generic_event mgl_monitor_property_changed_event;

typedef struct {
    int id;
} mgl_monitor_disconnected_event;

typedef enum {
    MGL_MAPPING_CHANGED_MODIFIER,
    MGL_MAPPING_CHANGED_KEYBOARD,
    MGL_MAPPING_CHANGED_POINTER
} mgl_mapping_changed_type;

typedef struct {
    int type; /* mgl_mapping_changed_type */
} mgl_mapping_changed_event;

typedef enum {
    MGL_EVENT_UNKNOWN,
    MGL_EVENT_CLOSED, /* Window closed */
    MGL_EVENT_RESIZED, /* Window resized */
    MGL_EVENT_LOST_FOCUS,
    MGL_EVENT_GAINED_FOCUS,
    MGL_EVENT_TEXT_ENTERED,
    MGL_EVENT_KEY_PRESSED,
    MGL_EVENT_KEY_RELEASED,
    MGL_EVENT_MOUSE_BUTTON_PRESSED,
    MGL_EVENT_MOUSE_BUTTON_RELEASED,
    MGL_EVENT_MOUSE_WHEEL_SCROLLED,
    MGL_EVENT_MOUSE_MOVED,
    MGL_EVENT_MONITOR_CONNECTED,
    MGL_EVENT_MONITOR_DISCONNECTED,
    MGL_EVENT_MONITOR_PROPERTY_CHANGED,
    MGL_EVENT_MAPPING_CHANGED
} mgl_event_type;

struct mgl_event {
    int type; /* mgl_event_type */
    union {
        mgl_size_event size;
        mgl_text_event text;
        mgl_key_event key;
        mgl_mouse_button_event mouse_button;
        mgl_mouse_wheel_scroll_event mouse_wheel_scroll;
        mgl_mouse_move_event mouse_move;
        mgl_monitor_connected_event monitor_connected;
        mgl_monitor_disconnected_event monitor_disconnected;
        mgl_monitor_property_changed_event monitor_property_changed;
        mgl_mapping_changed_event mapping_changed;
    };
};

#endif /* MGL_EVENT_H */
