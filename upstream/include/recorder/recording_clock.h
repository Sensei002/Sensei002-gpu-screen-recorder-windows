#ifndef GSR_RECORDER_RECORDING_CLOCK_H
#define GSR_RECORDER_RECORDING_CLOCK_H

#include <stdbool.h>

/* Monotonic clock that excludes the time the recording has been paused. Safe to use from multiple threads */
typedef struct gsr_recording_clock gsr_recording_clock;

gsr_recording_clock* gsr_recording_clock_create(void);
void gsr_recording_clock_destroy(gsr_recording_clock *self);

/* Sets the time the recording started to now */
void gsr_recording_clock_start(gsr_recording_clock *self);
double gsr_recording_clock_get_start_time(const gsr_recording_clock *self);
/* Returns the current time, excluding the time the recording has been paused */
double gsr_recording_clock_get_time(const gsr_recording_clock *self);

void gsr_recording_clock_set_paused(gsr_recording_clock *self, bool paused);
bool gsr_recording_clock_is_paused(const gsr_recording_clock *self);

#endif /* GSR_RECORDER_RECORDING_CLOCK_H */
