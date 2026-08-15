/* platform/include/display.h — display/monitor interfaces for the Windows port.
 *
 * Phase 3 deliverable. The monitor *enumeration* is implemented in Phase 4
 * (platform/windows/display.c, DXGI + GetMonitorInfoW); the pure formatting
 * helpers below are implemented now (platform/windows/gsr_display_win32.c)
 * because they define the `--list-monitors` / `--info` output contract the
 * UI parses (docs/upstream-analysis.md §10.1).
 */
#ifndef GSR_PLATFORM_DISPLAY_H
#define GSR_PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char name[64];          /* e.g. "\\\\.\\DISPLAY1" or a friendly alias */
    int position_x;         /* virtual-screen origin */
    int position_y;
    int width;              /* physical pixels */
    int height;
    double refresh_rate;    /* Hz */
    int rotation_degrees;   /* 0/90/180/270 */
    int dpi;                /* horizontal DPI */
    bool is_primary;
    bool hdr;
    char adapter_vendor[32]; /* e.g. "NVIDIA" */
    char adapter_name[128];
} gsr_platform_monitor;

/* Enumerates all active monitors. Allocates an array of |*out_count|
 * gsr_platform_monitor entries with malloc(); the caller frees it. Returns
 * false (and logs the error) when enumeration is unavailable. Implemented
 * in Phase 4 (DXGI + GetMonitorInfoW). */
bool gsr_platform_display_list_monitors(gsr_platform_monitor **out, int *out_count);

/* Formats one monitor as the `--list-monitors` line: "name|WxH".
 * Returns the number of characters written (excluding the NUL), or -1 when
 * the buffer is too small. */
int gsr_platform_display_format_monitor_line(const gsr_platform_monitor *monitor, char *buf, size_t size);

/* --- `--info` output contract -------------------------------------------
 * The engine prints `key|value` lines grouped by `section=<name>` headers
 * (docs/upstream-analysis.md §10.1). These writers keep that exact shape so
 * later phases (4/7) feed real probe data through the same formatter the UI
 * already understands. */

/* Writes "section=<name>\n". Returns characters written or -1. */
int gsr_platform_info_write_section(char *buf, size_t size, const char *name);

/* Writes "<key>|<value>\n". Returns characters written or -1. */
int gsr_platform_info_write_key_value(char *buf, size_t size, const char *key, const char *value);

#endif /* GSR_PLATFORM_DISPLAY_H */
