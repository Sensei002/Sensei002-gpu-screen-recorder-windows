#ifndef GSR_DBUS_H
#define GSR_DBUS_H

#include <stdbool.h>
#include <stdint.h>
#include <dbus/dbus.h>

#define DBUS_RANDOM_STR_SIZE 16

typedef enum {
    GSR_PORTAL_CAPTURE_TYPE_MONITOR = 1 << 0,
    GSR_PORTAL_CAPTURE_TYPE_WINDOW  = 1 << 1,
    GSR_PORTAL_CAPTURE_TYPE_VIRTUAL = 1 << 2,
    GSR_PORTAL_CAPTURE_TYPE_ALL = GSR_PORTAL_CAPTURE_TYPE_MONITOR | GSR_PORTAL_CAPTURE_TYPE_WINDOW | GSR_PORTAL_CAPTURE_TYPE_VIRTUAL
} gsr_portal_capture_type;

typedef enum {
    GSR_PORTAL_CURSOR_MODE_HIDDEN   = 1 << 0,
    GSR_PORTAL_CURSOR_MODE_EMBEDDED = 1 << 1,
    GSR_PORTAL_CURSOR_MODE_METADATA = 1 << 2
} gsr_portal_cursor_mode;

typedef struct {
    DBusConnection *con;
    DBusError err;
    char random_str[DBUS_RANDOM_STR_SIZE + 1];
    unsigned int handle_counter;
    bool desktop_portal_rule_added;
    uint32_t screencast_version;
    char *screencast_restore_token;
} gsr_dbus;

/* Blocking. TODO: Make non-blocking */
bool gsr_dbus_init(gsr_dbus *self, const char *screencast_restore_token);
void gsr_dbus_deinit(gsr_dbus *self);

/* The following functions should be called in order to setup ScreenCast properly */
/* These functions that return an int return the response status code */
int gsr_dbus_screencast_create_session(gsr_dbus *self, char **session_handle);
/*
    |capture_type| is a bitmask of gsr_portal_capture_type values. gsr_portal_capture_type values that are not supported by the desktop portal will be ignored.
    |gsr_portal_cursor_mode| is a bitmask of gsr_portal_cursor_mode values. gsr_portal_cursor_mode values that are not supported will be ignored.
*/
int gsr_dbus_screencast_select_sources(gsr_dbus *self, const char *session_handle, uint32_t capture_type, uint32_t cursor_mode);
int gsr_dbus_screencast_start(gsr_dbus *self, const char *session_handle, uint32_t *pipewire_node);
bool gsr_dbus_screencast_open_pipewire_remote(gsr_dbus *self, const char *session_handle, int *pipewire_fd);
const char* gsr_dbus_screencast_get_restore_token(gsr_dbus *self);

#endif /* GSR_DBUS_H */
