/* platform/include/display.h — display/monitor interfaces for the Windows port.
 *
 * Phase 3 deliverable (formatting contract) + Phase 4 (DXGI enumeration).
 * The pure formatting helpers below define the `--list-monitors` / `--info`
 * output contract the UI parses (docs/upstream-analysis.md §10.1) and are
 * implemented in platform/windows/gsr_display_win32.c; the monitor
 * *enumeration* (gsr_platform_display_list_monitors, DXGI +
 * GetMonitorInfoW) landed in Phase 4 in the same file.
 */
#ifndef GSR_PLATFORM_DISPLAY_H
#define GSR_PLATFORM_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[64];          /* Win32 device name, e.g. "\\\\.\\DISPLAY1" (canonical) */
    char friendly_name[128];/* EDID friendly name, e.g. "DELL U2720Q"; may be empty */
    int position_x;         /* virtual-screen origin (post-rotation layout) */
    int position_y;
    int width;              /* NATIVE panel resolution (pre-rotation), physical pixels */
    int height;
    double refresh_rate;    /* Hz */
    int rotation_degrees;   /* 0/90/180/270 */
    int dpi;                /* horizontal DPI (MDT_EFFECTIVE_DPI); 96 fallback */
    bool is_primary;
    bool hdr;               /* HDR10 output (DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) */
    char adapter_vendor[32]; /* e.g. "NVIDIA" */
    char adapter_name[128];  /* e.g. "NVIDIA GeForce RTX 4090" */
} gsr_platform_monitor;

/* Enumerates all active (attached-to-desktop) monitors. Allocates an array
 * of |*out_count| gsr_platform_monitor entries with malloc(); the caller
 * frees it. Returns false (and logs the error) when enumeration is
 * unavailable. Zero attached monitors is NOT an error (headless/remote
 * sessions): count == 0 and true are returned. Implemented in Phase 4
 * (DXGI + GetMonitorInfoW); the runner's virtual display yields >= 1. */
bool gsr_platform_display_list_monitors(gsr_platform_monitor **out, int *out_count);

/* Effective (post-rotation) size. 90/270 degree rotations swap width and
 * height; 0/180 leave them. Pure logic, no Win32 calls — unit-tested
 * headless. Returns false on NULL inputs. */
bool gsr_platform_display_effective_size(const gsr_platform_monitor *monitor, int *width, int *height);

/* Finds a monitor by name, case-insensitively, against the canonical
 * device name ("\\\\.\\DISPLAY1") first and then the friendly name. This is
 * the mapping the capture backends (Phases 5/6) use to resolve a -w
 * monitor argument to a monitor. Pure logic (no Win32 calls) so it is
 * unit-testable headless. Returns the index into |monitors|, or -1.
 * Upstream-style DRM connector names ("DP-1") have no Windows equivalent
 * and return -1 unless a device/friendly name matches. */
int gsr_platform_display_find_monitor(const gsr_platform_monitor *monitors, int count, const char *name);

/* Maps a PCI vendor id to a displayable vendor string (NVIDIA/AMD/Intel/
 * Microsoft/VMware/Parallels/Qualcomm, "Unknown" otherwise). Pure logic. */
const char *gsr_platform_display_vendor_name(uint32_t vendor_id);

/* Formats one monitor as the `--list-monitors` line: "name|WxH" using the
 * EFFECTIVE (post-rotation) size, matching upstream's Wayland output
 * (native size stored in the struct; swapped at print time for 90/270).
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
