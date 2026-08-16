#include "../../include/mgl/system/clock.h"
#include <time.h>

/* TODO: Implement for macOS */

static double clock_get_monotonic_seconds(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 0.000000001;
}

void mgl_clock_init(mgl_clock *self) {
    self->captured_seconds = clock_get_monotonic_seconds();
}

double mgl_clock_restart(mgl_clock *self) {
    const double new_time_seconds = clock_get_monotonic_seconds();
    const double elapsed_time_seconds = new_time_seconds - self->captured_seconds;
    self->captured_seconds = new_time_seconds;
    return elapsed_time_seconds;
}

double mgl_clock_get_elapsed_time_seconds(mgl_clock *self) {
    return clock_get_monotonic_seconds() - self->captured_seconds;
}
