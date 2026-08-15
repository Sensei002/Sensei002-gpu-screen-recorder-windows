#ifndef GSR_CAPTURE_NVFBC_H
#define GSR_CAPTURE_NVFBC_H

#include "capture.h"
#include "../vec2.h"

typedef struct {
    gsr_egl *egl;
    const char *display_to_capture; /* if this is "screen", then the entire x11 screen is captured (all displays). A copy is made of this */
    int fps;
    bool direct_capture;
    bool record_cursor;
    vec2i output_resolution;
    vec2i region_size;
    vec2i region_position;
} gsr_capture_nvfbc_params;

gsr_capture* gsr_capture_nvfbc_create(const gsr_capture_nvfbc_params *params);

#endif /* GSR_CAPTURE_NVFBC_H */
