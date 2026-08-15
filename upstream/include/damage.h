#ifndef GSR_DAMAGE_H
#define GSR_DAMAGE_H

#include "cursor.h"
#include "utils.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _XDisplay Display;
typedef union _XEvent XEvent;

#define GSR_DAMAGE_MAX_MONITORS 32
#define GSR_DAMAGE_MAX_TRACKED_TARGETS 12

typedef struct {
    int64_t window_id;
    vec2i window_pos;
    vec2i window_size;
    uint64_t damage;
    int refcount;
} gsr_damage_window;

typedef struct {
    char *monitor_name;
    gsr_monitor *monitor;
    int refcount;
} gsr_damage_monitor;

typedef struct {
    gsr_egl *egl;
    Display *display;
    bool track_cursor;

    int damage_event;
    int damage_error;
    bool damaged;

    uint64_t monitor_damage;

    int randr_event;
    int randr_error;

    gsr_cursor *cursor;
    gsr_monitor monitors[GSR_DAMAGE_MAX_MONITORS];
    int num_monitors;

    gsr_damage_window windows_tracked[GSR_DAMAGE_MAX_TRACKED_TARGETS];
    int num_windows_tracked;

    gsr_damage_monitor monitors_tracked[GSR_DAMAGE_MAX_TRACKED_TARGETS];
    int num_monitors_tracked;

    int all_monitors_tracked_refcount;
    vec2i cursor_pos;
} gsr_damage;

bool gsr_damage_init(gsr_damage *self, gsr_egl *egl, gsr_cursor *cursor, bool track_cursor);
void gsr_damage_deinit(gsr_damage *self);

/* This is reference counted */
bool gsr_damage_start_tracking_window(gsr_damage *self, int64_t window);
void gsr_damage_stop_tracking_window(gsr_damage *self, int64_t window);

/* This is reference counted. If |monitor_name| is NULL then all monitors are tracked */
bool gsr_damage_start_tracking_monitor(gsr_damage *self, const char *monitor_name);
void gsr_damage_stop_tracking_monitor(gsr_damage *self, const char *monitor_name);

void gsr_damage_on_event(gsr_damage *self, XEvent *xev);
void gsr_damage_tick(gsr_damage *self);
/* Also returns true if damage tracking is not available */
bool gsr_damage_is_damaged(gsr_damage *self);
void gsr_damage_clear(gsr_damage *self);

#endif /* GSR_DAMAGE_H */
