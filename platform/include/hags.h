/* platform/include/hags.h — Hardware Accelerated GPU Scheduling detection.
 *
 * Phase 11 (HAGS hardening): HAGS moves GPU scheduling off the CPU onto the
 * GPU. It is the root cause of choppy captures in recorders that rely on
 * DXGI Desktop Duplication (it captures pre-compositor frames and its frame
 * delivery is affected by the scheduler). The port is WGC-primary and
 * GPU-to-GPU (NVENC) on the encode path specifically so HAGS can stay
 * enabled; this flag is surfaced in `--info` and the capture-backend logs so
 * the choice is transparent.
 */
#ifndef GSR_PLATFORM_HAGS_H
#define GSR_PLATFORM_HAGS_H

#include <stdbool.h>

/* Whether HAGS (HwSchMode = 2 under HKLM\SYSTEM\CurrentControlSet\Control\
 * GraphicsDrivers) is currently enabled. Returns false when the value is
 * absent or unreadable (the conservative default). */
bool gsr_platform_hags_enabled(void);

#endif /* GSR_PLATFORM_HAGS_H */
