/* gsr_display_win32.c — Windows implementation of platform/include/display.h.
 *
 * Phase 3 deliverable (pure formatting helpers, golden-tested) + Phase 4
 * deliverable (DXGI + GetMonitorInfoW enumeration).
 *
 * The `--list-monitors` line and the `--info` section/key|value shape are
 * the output contract the UI parses (docs/upstream-analysis.md §10.1).
 * Monitor *enumeration* matches upstream's semantics: the struct stores the
 * NATIVE panel resolution and the rotation is applied at print time
 * (swap for 90/270), exactly like upstream's Wayland `output_monitor_info`.
 */
#include "../../platform/include/display.h"
#include "../../platform/include/filesystem.h" /* gsr_platform_wide_to_utf8 */

#include "../../upstream/include/log.h"

#include <windows.h>
#include <dxgi.h>
#include <dxgi1_6.h>       /* IDXGIOutput6::GetDesc1 (HDR state) */
#include <shellscalingapi.h> /* GetDpiForMonitor (per-monitor DPI) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>   /* _wcsicmp */
#include <ctype.h>

/* ---- pure logic helpers (no Win32 calls — unit-tested headless) --------- */

bool gsr_platform_display_effective_size(const gsr_platform_monitor *monitor, int *width, int *height) {
    if(!monitor || !width || !height)
        return false;

    int w = monitor->width;
    int h = monitor->height;
    if(monitor->rotation_degrees == 90 || monitor->rotation_degrees == 270) {
        const int tmp = w;
        w = h;
        h = tmp;
    }
    *width = w;
    *height = h;
    return true;
}

/* Case-insensitive byte comparison. tolower() is only well-defined for
   unsigned chars; non-ASCII bytes pass through unchanged (so accented
   friendly-name variants of the same letter do not match — acceptable). */
static bool name_equals_ci(const char *a, const char *b) {
    if(!a || !b)
        return false;
    while(*a && *b) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* Monitor device names have two spellings depending on the API that
   produced them: GetMonitorInfoW returns "\\.\DISPLAY1" while DXGI's
   DXGI_OUTPUT_DESC.DeviceName (what --list-capture-options lists) returns
   "\.\DISPLAY1". The recorder passes the listed name straight to the
   lookup, so strip leading backslashes on both sides before comparing.
   "screen" and friendly (EDID) names are unaffected. */
static const char *skip_leading_backslashes(const char *s) {
    if(!s)
        return s;
    while(*s == '\\')
        ++s;
    return s;
}

static bool monitor_name_matches(const char *a, const char *b) {
    if(!a || !b)
        return false;
    return name_equals_ci(skip_leading_backslashes(a), skip_leading_backslashes(b));
}

int gsr_platform_display_find_monitor(const gsr_platform_monitor *monitors, int count, const char *name) {
    if(!monitors || !name || count <= 0)
        return -1;

    /* Canonical device name first ("\\.\DISPLAY1") */
    for(int i = 0; i < count; ++i) {
        if(monitor_name_matches(monitors[i].name, name))
            return i;
    }
    /* Then the EDID friendly name */
    for(int i = 0; i < count; ++i) {
        if(monitors[i].friendly_name[0] != '\0' && name_equals_ci(monitors[i].friendly_name, name))
            return i;
    }
    return -1;
}

const char *gsr_platform_display_vendor_name(uint32_t vendor_id) {
    switch(vendor_id) {
    case 0x10DE: return "NVIDIA";
    case 0x1002: /* ATI/AMD */
    case 0x1022: /* AMD */
        return "AMD";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";   /* Basic Display Adapter / virtual */
    case 0x15AD: return "VMware";
    case 0x1AB4: return "Parallels";
    case 0x5143: return "Qualcomm";
    default:     return "Unknown";
    }
}

int gsr_platform_display_format_monitor_line(const gsr_platform_monitor *monitor, char *buf, size_t size) {
    if(!monitor || !buf)
        return -1;

    int w = 0;
    int h = 0;
    if(!gsr_platform_display_effective_size(monitor, &w, &h))
        return -1;

    const int written = snprintf(buf, size, "%s|%dx%d", monitor->name, w, h);
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

/* ---- DXGI + GetMonitorInfoW enumeration (Phase 4) ------------------------ */

/* EnumDisplayMonitors callback for gsr_platform_display_find_hmonitor. */
typedef struct {
    const char *name;   /* requested monitor name (device or friendly) */
    HMONITOR found;     /* matching HMONITOR, or NULL */
} find_hmonitor_userdata;

static BOOL CALLBACK find_hmonitor_callback(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lp) {
    (void)hdc;
    (void)rect;
    find_hmonitor_userdata *ud = (find_hmonitor_userdata*)lp;

    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if(!GetMonitorInfoW(hmon, (MONITORINFO*)&mi))
        return TRUE; /* keep looking */

    char device_name[64];
    gsr_platform_wide_to_utf8(mi.szDevice, device_name, sizeof(device_name));
    if(monitor_name_matches(device_name, ud->name)) {
        ud->found = hmon;
        return FALSE;
    }

    /* Friendly-name match: second EnumDisplayDevices call, like
       get_friendly_name, but against this specific HMONITOR's device. */
    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    if(EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
        char friendly[128];
        if(gsr_platform_wide_to_utf8(dd.DeviceString, friendly, sizeof(friendly)) && friendly[0] != '\0' && name_equals_ci(friendly, ud->name)) {
            ud->found = hmon;
            return FALSE;
        }
    }
    return TRUE;
}

void *gsr_platform_display_find_hmonitor(const char *name) {
    if(!name)
        return NULL;

    find_hmonitor_userdata ud;
    ud.name = name;
    ud.found = NULL;
    EnumDisplayMonitors(NULL, NULL, find_hmonitor_callback, (LPARAM)&ud);
    return (void*)ud.found;
}

/* IID_IDXGIOutput6: mingw-w64's libdxgi.a provides the older DXGI IIDs
 * (IID_IDXGIFactory1, ...) as linkable data exports, but not this
 * Win10-era one, so define it locally. Value matches mingw-w64 dxgi1_6.h
 * and the Microsoft docs: 068346e8-aaec-4b84-add7-137f513f77a1. */
static const GUID GSR_IID_IDXGIOutput6 = {0x068346e8, 0xaaec, 0x4b84, {0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1}};

/* Picks the native (largest-area) mode from the output's mode list and
 * fills *width/*height (native, pre-rotation) and *refresh_rate. Falls back
 * to the desktop-coordinate size (post-rotation) when the mode list is
 * unavailable, so the fields are always populated. */
static void get_native_mode_info(IDXGIOutput *output, const DXGI_OUTPUT_DESC *odesc, int *width, int *height, double *refresh_rate) {
    *width = odesc->DesktopCoordinates.right - odesc->DesktopCoordinates.left;
    *height = odesc->DesktopCoordinates.bottom - odesc->DesktopCoordinates.top;
    *refresh_rate = 0.0;

    UINT num_modes = 0;
    if(FAILED(output->lpVtbl->GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, NULL)) || num_modes == 0)
        return;

    DXGI_MODE_DESC *modes = (DXGI_MODE_DESC*)malloc((size_t)num_modes * sizeof(DXGI_MODE_DESC));
    if(!modes)
        return;

    UINT got = num_modes;
    if(SUCCEEDED(output->lpVtbl->GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &got, modes))) {
        UINT64 best_area = 0;
        for(UINT i = 0; i < got; ++i) {
            const UINT64 area = (UINT64)modes[i].Width * modes[i].Height;
            if(area > best_area) {
                best_area = area;
                *width = (int)modes[i].Width;
                *height = (int)modes[i].Height;
                *refresh_rate = modes[i].RefreshRate.Denominator != 0
                    ? (double)modes[i].RefreshRate.Numerator / (double)modes[i].RefreshRate.Denominator
                    : 0.0;
            }
        }
    }
    free(modes);
}

/* Looks up the EDID friendly name for a device ("\\\\.\\DISPLAYn") via
 * EnumDisplayDevices (first call = adapter, second = attached monitor,
 * whose DeviceString is the friendly name). */
static void get_friendly_name(const wchar_t *device_name_w, char *out, size_t out_size) {
    out[0] = '\0';
    if(out_size == 0)
        return;

    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    for(DWORD i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); ++i) {
        if(!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
            memset(&dd, 0, sizeof(dd));
            dd.cb = sizeof(dd);
            continue;
        }
        if(_wcsicmp(dd.DeviceName, device_name_w) == 0) {
            DISPLAY_DEVICEW mon;
            memset(&mon, 0, sizeof(mon));
            mon.cb = sizeof(mon);
            if(EnumDisplayDevicesW(dd.DeviceName, 0, &mon, 0))
                gsr_platform_wide_to_utf8(mon.DeviceString, out, out_size);
            break;
        }
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
    }
}

bool gsr_platform_display_list_monitors(gsr_platform_monitor **out, int *out_count) {
    if(!out || !out_count)
        return false;
    *out = NULL;
    *out_count = 0;

    IDXGIFactory1 *factory = NULL;
    const HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_platform_display: CreateDXGIFactory1 failed (0x%08lx)", (unsigned long)hr);
        return false;
    }

    /* Pass 1: count attached-to-desktop outputs. */
    int count = 0;
    IDXGIAdapter1 *adapter = NULL;
    for(UINT ai = 0; factory->lpVtbl->EnumAdapters1(factory, ai, &adapter) != DXGI_ERROR_NOT_FOUND; ++ai) {
        IDXGIOutput *output = NULL;
        for(UINT oi = 0; adapter->lpVtbl->EnumOutputs(adapter, oi, &output) != DXGI_ERROR_NOT_FOUND; ++oi) {
            DXGI_OUTPUT_DESC odesc;
            if(SUCCEEDED(output->lpVtbl->GetDesc(output, &odesc)) && odesc.AttachedToDesktop)
                ++count;
            output->lpVtbl->Release(output);
        }
        adapter->lpVtbl->Release(adapter);
    }

    /* Zero attached monitors is not an error (disconnected RDP/headless). */
    if(count == 0) {
        factory->lpVtbl->Release(factory);
        return true;
    }

    gsr_platform_monitor *monitors = (gsr_platform_monitor*)calloc((size_t)count, sizeof(gsr_platform_monitor));
    if(!monitors) {
        factory->lpVtbl->Release(factory);
        return false;
    }

    /* Pass 2: fill the array. */
    int idx = 0;
    for(UINT ai = 0; factory->lpVtbl->EnumAdapters1(factory, ai, &adapter) != DXGI_ERROR_NOT_FOUND && idx < count; ++ai) {
        DXGI_ADAPTER_DESC1 adesc;
        if(SUCCEEDED(adapter->lpVtbl->GetDesc1(adapter, &adesc))) {
            IDXGIOutput *output = NULL;
            for(UINT oi = 0; adapter->lpVtbl->EnumOutputs(adapter, oi, &output) != DXGI_ERROR_NOT_FOUND && idx < count; ++oi) {
                DXGI_OUTPUT_DESC odesc;
                if(FAILED(output->lpVtbl->GetDesc(output, &odesc)) || !odesc.AttachedToDesktop) {
                    output->lpVtbl->Release(output);
                    continue;
                }

                gsr_platform_monitor *m = &monitors[idx];

                gsr_platform_wide_to_utf8(odesc.DeviceName, m->name, sizeof(m->name));
                get_friendly_name(odesc.DeviceName, m->friendly_name, sizeof(m->friendly_name));

                m->position_x = odesc.DesktopCoordinates.left;
                m->position_y = odesc.DesktopCoordinates.top;

                switch(odesc.Rotation) {
                case DXGI_MODE_ROTATION_IDENTITY: m->rotation_degrees = 0; break;
                case DXGI_MODE_ROTATION_ROTATE90: m->rotation_degrees = 90; break;
                case DXGI_MODE_ROTATION_ROTATE180: m->rotation_degrees = 180; break;
                case DXGI_MODE_ROTATION_ROTATE270: m->rotation_degrees = 270; break;
                default: m->rotation_degrees = 0; break;
                }

                get_native_mode_info(output, &odesc, &m->width, &m->height, &m->refresh_rate);

                MONITORINFO mi;
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                m->is_primary = GetMonitorInfoW(odesc.Monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY);

                UINT dpi_x = 0;
                UINT dpi_y = 0;
                m->dpi = (SUCCEEDED(GetDpiForMonitor(odesc.Monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x > 0)
                    ? (int)dpi_x : 96;

                /* HDR10: color space == HDR10 PQ (the standard check). */
                IDXGIOutput6 *output6 = NULL;
                if(SUCCEEDED(output->lpVtbl->QueryInterface(output, &GSR_IID_IDXGIOutput6, (void**)&output6))) {
                    DXGI_OUTPUT_DESC1 odesc1;
                    if(SUCCEEDED(output6->lpVtbl->GetDesc1(output6, &odesc1)))
                        m->hdr = (odesc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                    output6->lpVtbl->Release(output6);
                }

                snprintf(m->adapter_vendor, sizeof(m->adapter_vendor), "%s", gsr_platform_display_vendor_name(adesc.VendorId));
                gsr_platform_wide_to_utf8(adesc.Description, m->adapter_name, sizeof(m->adapter_name));

                ++idx;
                output->lpVtbl->Release(output);
            }
        }
        adapter->lpVtbl->Release(adapter);
    }

    factory->lpVtbl->Release(factory);

    *out = monitors;
    *out_count = idx;
    return true;
}
