#ifndef LEDS_H
#define LEDS_H

/* C stdlib */
#include <stdbool.h>

typedef struct {
    /* These are set to -1 if not supported */
    int scroll_lock_brightness;
    int num_lock_brightness;
    int caps_lock_brightness;
} ggh_leds;

bool set_leds(const char *led_name, bool enabled);
bool get_leds(int event_number, ggh_leds *leds);

#endif /* LEDS_H */
