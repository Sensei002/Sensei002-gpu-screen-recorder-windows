#ifndef MGL_CLOCK_H
#define MGL_CLOCK_H

typedef struct {
    double captured_seconds;
} mgl_clock;

void mgl_clock_init(mgl_clock *self);
/* Returns the elapsed time in seconds since the last restart or init, before resetting the clock */
double mgl_clock_restart(mgl_clock *self);
double mgl_clock_get_elapsed_time_seconds(mgl_clock *self);

#endif /* MGL_CLOCK_H */
