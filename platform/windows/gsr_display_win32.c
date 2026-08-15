/* gsr_display_win32.c — platform/include/display.h pure formatting helpers.
 *
 * Phase 3 deliverable. The `--list-monitors` line and the `--info`
 * section/key|value shape are the output contract the UI parses
 * (docs/upstream-analysis.md §10.1); they are implemented and golden-tested
 * now. Monitor *enumeration* (gsr_platform_display_list_monitors) lands in
 * Phase 4 (DXGI + GetMonitorInfoW).
 */
#include "../../platform/include/display.h"

#include <stdio.h>
#include <string.h>

int gsr_platform_display_format_monitor_line(const gsr_platform_monitor *monitor, char *buf, size_t size) {
    if(!monitor || !buf)
        return -1;

    const int written = snprintf(buf, size, "%s|%dx%d", monitor->name, monitor->width, monitor->height);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

int gsr_platform_info_write_section(char *buf, size_t size, const char *name) {
    if(!buf || !name)
        return -1;

    const int written = snprintf(buf, size, "section=%s\n", name);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

int gsr_platform_info_write_key_value(char *buf, size_t size, const char *key, const char *value) {
    if(!buf || !key || !value)
        return -1;

    const int written = snprintf(buf, size, "%s|%s\n", key, value);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}
