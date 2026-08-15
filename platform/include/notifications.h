/* platform/include/notifications.h — notification interfaces for the Windows port.
 *
 * Phase 3 deliverable (headers only). Implemented in Phase 11:
 * gsr-notification.exe (the ShadowPlay-style overlay) replaces the Linux
 * notification path; this interface is what the engine/UI call to show a
 * notification.
 */
#ifndef GSR_PLATFORM_NOTIFICATIONS_H
#define GSR_PLATFORM_NOTIFICATIONS_H

#include <stdbool.h>

typedef enum {
    GSR_PLATFORM_NOTIFICATION_INFO,
    GSR_PLATFORM_NOTIFICATION_WARNING,
    GSR_PLATFORM_NOTIFICATION_ERROR
} gsr_platform_notification_type;

/* Shows a notification (via gsr-notification.exe in Phase 11). Returns
 * false when the notification process could not be spawned. */
bool gsr_platform_notification_show(gsr_platform_notification_type type, const char *title, const char *message);

#endif /* GSR_PLATFORM_NOTIFICATIONS_H */
