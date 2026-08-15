/* platform/include/capture.h — capture backend interfaces for the Windows port.
 *
 * Phase 3 deliverable. The capture *implementation* lands in Phase 5
 * (Windows Graphics Capture) and Phase 6 (DXGI Desktop Duplication) behind
 * the upstream gsr_capture vtable (upstream/include/capture/capture.h) —
 * that vtable is the interface the engine calls, and this port does not
 * change it. This header adds the port-owned pieces the engine does not
 * have: backend identity and automatic backend selection.
 */
#ifndef GSR_PLATFORM_CAPTURE_H
#define GSR_PLATFORM_CAPTURE_H

#include <stdbool.h>

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
 * WGC: IsGraphicsCaptureSupported. DXGI: IDXGIOutputDuplication present.
 * Implemented in Phases 5/6. */
bool gsr_platform_capture_backend_available(gsr_capture_backend_type backend);

#endif /* GSR_PLATFORM_CAPTURE_H */
