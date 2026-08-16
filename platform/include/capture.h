/* platform/include/capture.h — capture backend interfaces for the Windows port.
 *
 * Phase 3 deliverable (backend identity/selection) + Phase 5 (the WGC
 * capture backend's C API and its pure-logic helpers).
 *
 * The capture *implementation* sits behind the upstream gsr_capture vtable
 * (upstream/include/capture/capture.h) — that vtable is the interface the
 * engine calls, and this port does not change it. This header adds the
 * port-owned pieces the engine does not have: backend identity, automatic
 * backend selection, the WGC backend's C API (implemented in C++/WinRT in
 * platform/windows/gsr_capture_wgc.cpp), and the pure logic that is
 * unit-tested headless (platform/windows/gsr_capture_wgc_helpers.c).
 */
#ifndef GSR_PLATFORM_CAPTURE_H
#define GSR_PLATFORM_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

/* The C API below crosses the C/C++ boundary: it is implemented in
   gsr_capture_wgc.cpp with extern "C" linkage and consumed by the pure-C
   engine and tests, while the helpers in gsr_capture_wgc_helpers.c are C.
   Without this guard the C++ TU would see C++-linkage declarations and
   reference mangled symbols. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct gsr_capture gsr_capture; /* upstream capture vtable type */
typedef struct gsr_egl gsr_egl;         /* upstream GL table (egl.h) */

/* ---- backend identity + selection (Phase 3) ------------------------------ */

typedef enum {
    GSR_CAPTURE_BACKEND_WGC,            /* Windows.Graphics.Capture (primary) */
    GSR_CAPTURE_BACKEND_DXGI_DUPLICATION /* Desktop Duplication (fallback)     */
} gsr_capture_backend_type;

/* Human-readable backend name (used in --info and logs). */
const char *gsr_platform_capture_backend_name(gsr_capture_backend_type backend);

/* Pure backend-selection logic (Phase 6 uses it):
 *
 *   - WGC supported              -> WGC
 *   - WGC unsupported, DXGI ok   -> DXGI_DUPLICATION (monitor-only)
 *   - neither                    -> GSR_CAPTURE_BACKEND_WGC (caller checks
 *                                   gsr_platform_capture_backend_available
 *                                   and fails with a clear error instead of
 *                                   silently recording nothing)
 *
 * |wgc_supported| and |dxgi_supported| come from the Phase 5/6 probes. */
gsr_capture_backend_type gsr_platform_capture_select_backend(bool wgc_supported, bool dxgi_supported);

/* Whether a backend actually exists on this system (probed at runtime).
 * WGC: GraphicsCaptureSession::IsSupported. DXGI: IDXGIOutputDuplication
 * present (Phase 6). */
bool gsr_platform_capture_backend_available(gsr_capture_backend_type backend);

/* ---- WGC capture backend C API (Phase 5) ---------------------------------
 * Implemented in platform/windows/gsr_capture_wgc.cpp (C++/WinRT) with
 * extern "C" linkage; this is the only surface the pure-C engine and tests
 * touch. The backend returns a gsr_capture whose vtable the engine calls
 * unchanged (start/tick/should_stop/capture/...). */

typedef enum {
    GSR_PLATFORM_WGC_TARGET_MONITOR, /* handle = HMONITOR */
    GSR_PLATFORM_WGC_TARGET_WINDOW   /* handle = HWND     */
} gsr_platform_wgc_target_kind;

typedef struct {
    gsr_platform_wgc_target_kind kind;
    void *handle;        /* HMONITOR or HWND */
    char name[256];      /* display/window name for logs */
} gsr_platform_wgc_target;

typedef struct {
    bool cursor;         /* capture the cursor (default true upstream) */
    bool hdr;            /* target is HDR; enables set_hdr_metadata */
    gsr_egl *egl;        /* Phase 5b: the shared ANGLE GL table. When set, the
                            backend creates its WGC frame pool on the SAME
                            D3D11 device ANGLE runs on (architecture §3.3
                            Option B) so the per-frame texture import via
                            EGL_ANGLE_d3d_texture_client_buffer is zero-copy,
                            and capture() draws into the color conversion.
                            NULL = standalone (self-test without a GL
                            pipeline; use gsr_platform_capture_wgc_get_frame). */
} gsr_platform_wgc_options;

/* Creates the WGC capture backend for the target. Returns NULL (and logs)
 * when WGC is unavailable (see gsr_platform_capture_backend_available) or
 * the target cannot be captured. */
gsr_capture *gsr_platform_capture_wgc_create(const gsr_platform_wgc_target *target, const gsr_platform_wgc_options *options);

/* Latest captured frame accessor. Returns the D3D11 texture of the most
 * recent WGC frame (owned by the backend — do not AddRef/Release) and its
 * native size. This is what the GL (ANGLE) integration imports via
 * EGL_ANGLE_d3d_texture_client_buffer (architecture §3.3 Option B) and
 * what the self-test validates. Returns false when no frame yet. */
bool gsr_platform_capture_wgc_get_frame(gsr_capture *cap, void **out_texture, int *width, int *height);

/* ---- DXGI Desktop Duplication capture backend C API (Phase 6) ------------
 * Monitor-only fallback backend (architecture §3.3; used when WGC is
 * unavailable, e.g. older Windows or locked-down sessions). Implemented in
 * platform/windows/gsr_capture_dxgi.c (plain C — Desktop Duplication is a
 * DXGI/D3D11 COM interface, no C++/WinRT needed). Same gsr_capture vtable
 * as WGC; the D3D11 texture is imported zero-copy via the shared ANGLE
 * device exactly like WGC (EGL_ANGLE_d3d_texture_client_buffer). */

typedef struct {
    void *hmonitor;      /* HMONITOR (from gsr_platform_display_find_hmonitor) */
    char name[256];      /* monitor name for logs */
} gsr_platform_dxgi_target;

typedef struct {
    bool cursor;         /* capture the cursor (DD delivers the desktop image
                            with the cursor already drawn in when visible) */
    bool hdr;            /* target is HDR (informational; DD surfaces are
                            always B8G8R8A8_UNORM, so HDR is not captured) */
    gsr_egl *egl;        /* shared ANGLE GL table; when set, the backend uses
                            the SAME D3D11 device ANGLE runs on so the
                            per-frame import is zero-copy, and capture()
                            draws into the color conversion. NULL = standalone
                            (self-test). */
} gsr_platform_dxgi_options;

/* Creates the DD capture backend for the target monitor. Returns NULL (and
   logs) when the desktop cannot be duplicated (see
   gsr_platform_capture_backend_available). */
gsr_capture *gsr_platform_capture_dxgi_create(const gsr_platform_dxgi_target *target, const gsr_platform_dxgi_options *options);

/* Latest captured frame accessor (same contract as the WGC one): the D3D11
   texture of the most recent DD frame and its native (un-rotated) size.
   Returns false when no frame yet. */
bool gsr_platform_capture_dxgi_get_frame(gsr_capture *cap, void **out_texture, int *width, int *height);

/* Whether Desktop Duplication actually works on this system (probed at
   runtime: a hardware D3D11 device can DuplicateOutput the primary
   monitor). Unlike WGC, DD needs no WinRT interop runtime, so it can be
   available on Server SKUs where WGC is not. */
bool gsr_platform_capture_dxgi_available(void);

/* ---- pure logic helpers (Phase 5/6, headless-tested) --------------------- */

/* Rotation mapping: monitor rotation degrees -> gsr_rotation enum value.
 * The values match upstream's gsr_rotation (color_conversion.h) exactly
 * (GSR_ROT_0..GSR_ROT_270). */
typedef enum {
    GSR_PLATFORM_WGC_ROT_0 = 0,
    GSR_PLATFORM_WGC_ROT_90 = 1,
    GSR_PLATFORM_WGC_ROT_180 = 2,
    GSR_PLATFORM_WGC_ROT_270 = 3
} gsr_platform_wgc_rotation;
gsr_platform_wgc_rotation gsr_platform_wgc_rotation_from_monitor(int rotation_degrees);

/* Rotation mapping: DXGI_MODE_ROTATION value (IDENTITY=1..ROTATE270=4) to
   the gsr rotation enum. GSR_ROT_90 is a 90° clockwise rotation, matching
   DXGI's, so the mapping is identity-minus-one. */
gsr_platform_wgc_rotation gsr_platform_dxgi_rotation_from_dxgi(uint32_t dxgi_rotation);

/* Whether the rotation swaps width and height (90°/270°) — the DD surface
   is native/un-rotated, so the effective (video) size is the swapped one. */
bool gsr_platform_dxgi_rotation_swaps_size(gsr_platform_wgc_rotation rotation);

/* Flip bits, matching upstream's gsr_flip values (color_conversion.h). */
#define GSR_PLATFORM_WGC_FLIP_NONE       0u
#define GSR_PLATFORM_WGC_FLIP_HORIZONTAL (1u << 0)
#define GSR_PLATFORM_WGC_FLIP_VERTICAL   (1u << 1)
/* Maps upstream capture-source flip flags (GSR_FLIP_*) to the WGC flip bits. */
uint32_t gsr_platform_wgc_flip_from_source(uint32_t source_flip);

/* Source color for the color-conversion draw: WGC frames are BGRA8, which
 * upstream's gsr_color_conversion handles as GSR_SOURCE_COLOR_BGR. */
typedef enum {
    GSR_PLATFORM_WGC_SOURCE_BGR = 0,
    GSR_PLATFORM_WGC_SOURCE_RGB = 1
} gsr_platform_wgc_source_color;
gsr_platform_wgc_source_color gsr_platform_wgc_source_color_from_pixel_format(uint32_t dxgi_format);

/* D3D11 device selection: prefer hardware, fall back to WARP (the pure
 * decision; the backend probes hardware availability itself). */
typedef enum {
    GSR_PLATFORM_WGC_DEVICE_HARDWARE,
    GSR_PLATFORM_WGC_DEVICE_WARP
} gsr_platform_wgc_device;
gsr_platform_wgc_device gsr_platform_wgc_select_device(bool hardware_available);

/* Damage state machine matching the recorder's contract: WGC delivers a new
 * frame whenever the captured content changes (tick()); the recorder then
 * calls is_damaged(), clear_damage() and capture() for that frame. */
typedef struct {
    bool frame_pending;  /* a new WGC frame arrived and is not yet consumed */
    bool consumed;       /* the pending frame was consumed by capture()     */
} gsr_platform_wgc_damage;

void gsr_platform_wgc_damage_init(gsr_platform_wgc_damage *self);
void gsr_platform_wgc_damage_on_frame(gsr_platform_wgc_damage *self);
bool gsr_platform_wgc_damage_is_damaged(const gsr_platform_wgc_damage *self);
void gsr_platform_wgc_damage_consume(gsr_platform_wgc_damage *self);

#ifdef __cplusplus
}
#endif

#endif /* GSR_PLATFORM_CAPTURE_H */
