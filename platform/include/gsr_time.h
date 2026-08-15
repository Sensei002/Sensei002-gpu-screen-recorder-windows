/* platform/include/gsr_time.h — time interfaces for the Windows port.
 *
 * Named gsr_time.h (not time.h): this header is on the compiler's -I path,
 * and a header literally named time.h would shadow the system <time.h>
 * for every #include <time.h> in the codebase (the compat shim
 * force-includes <time.h> into every translation unit).
 *
 * Phase 3 deliverable (headers only where noted; see
 * docs/platform-interfaces.md for the caller map).
 *
 * Upstream already exposes the engine clock through utils.h
 * (clock_get_monotonic_seconds, gsr_get_date_str and friends), and the
 * recording clock in recorder/recording_clock.h. This header is the port's
 * own layer for the timing primitives the Windows backends and the UI need
 * that upstream's portable code does not provide:
 *
 *   - a nanosecond-resolution monotonic clock (the engine only has seconds);
 *   - a wall-clock millisecond timer for UI pacing.
 *
 * Implementation: platform/windows/gsr_utils_win32.c (Phase 2, QPC based).
 */
#ifndef GSR_PLATFORM_GSR_TIME_H
#define GSR_PLATFORM_GSR_TIME_H

#include <stdint.h>
#include <stddef.h>

/* Monotonic clock in nanoseconds (QueryPerformanceCounter on Windows).
 * Same time base as clock_get_monotonic_seconds(); safe to mix after
 * converting. Never goes backwards. */
int64_t gsr_platform_time_monotonic_ns(void);

/* Monotonic clock in seconds (double). Same base as the engine's
 * clock_get_monotonic_seconds(); the two are interchangeable. */
double gsr_platform_time_monotonic_seconds(void);

/* Monotonic wall-clock time in milliseconds (QPC based, not GetTickCount:
 * unaffected by suspend-to-RAM quirks). For UI timers and timeouts. */
int64_t gsr_platform_time_wall_clock_ms(void);

#endif /* GSR_PLATFORM_GSR_TIME_H */
