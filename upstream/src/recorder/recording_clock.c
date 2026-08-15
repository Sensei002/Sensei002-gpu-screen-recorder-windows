#include "../../include/recorder/recording_clock.h"
#include "../../include/utils.h"
#include "../../include/log.h"

#include <stdlib.h>
#include <stdatomic.h>

struct gsr_recording_clock {
    double record_start_time;
    _Atomic double paused_time_offset;
    double paused_time_start;
    atomic_bool paused;
};

gsr_recording_clock* gsr_recording_clock_create(void) {
    gsr_recording_clock *self = calloc(1, sizeof(gsr_recording_clock));
    if(!self) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_recording_clock_create: failed to allocate recording clock");
        return NULL;
    }

    atomic_init(&self->paused_time_offset, 0.0);
    atomic_init(&self->paused, false);
    self->record_start_time = clock_get_monotonic_seconds();
    return self;
}

void gsr_recording_clock_destroy(gsr_recording_clock *self) {
    if(self)
        free(self);
}

void gsr_recording_clock_start(gsr_recording_clock *self) {
    self->record_start_time = clock_get_monotonic_seconds();
}

double gsr_recording_clock_get_start_time(const gsr_recording_clock *self) {
    return self->record_start_time;
}

double gsr_recording_clock_get_time(const gsr_recording_clock *self) {
    return clock_get_monotonic_seconds() - atomic_load(&self->paused_time_offset);
}

void gsr_recording_clock_set_paused(gsr_recording_clock *self, bool paused) {
    if(paused == atomic_load(&self->paused))
        return;

    if(paused) {
        self->paused_time_start = clock_get_monotonic_seconds();
    } else {
        atomic_store(&self->paused_time_offset, atomic_load(&self->paused_time_offset) + (clock_get_monotonic_seconds() - self->paused_time_start));
    }

    atomic_store(&self->paused, paused);
}

bool gsr_recording_clock_is_paused(const gsr_recording_clock *self) {
    return atomic_load(&self->paused);
}
