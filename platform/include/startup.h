/* platform/include/startup.h — startup (autostart) interfaces for the Windows port.
 *
 * Phase 3 deliverable (headers only). Implemented in Phase 12
 * (platform/windows/startup.c): HKCU\Software\Microsoft\Windows\CurrentVersion\Run
 * key (no admin needed), replacing the XDG autostart the UI uses on Linux.
 */
#ifndef GSR_PLATFORM_STARTUP_H
#define GSR_PLATFORM_STARTUP_H

#include <stdbool.h>

/* Enables/disables starting the UI at user logon. The value stored is the
 * path of the running UI executable (a fixed value string is passed so the
 * installer and the UI agree). */
bool gsr_platform_startup_set_enabled(bool enabled, const char *command_line);

/* Whether startup is currently enabled for |command_line| (or, when NULL,
 * whether any value the port previously wrote is present). */
bool gsr_platform_startup_is_enabled(const char *command_line);

#endif /* GSR_PLATFORM_STARTUP_H */
