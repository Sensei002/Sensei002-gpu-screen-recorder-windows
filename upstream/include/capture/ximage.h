#ifndef GSR_CAPTURE_XIMAGE_H
#define GSR_CAPTURE_XIMAGE_H

#include "capture.h"
#include "../vec2.h"
#include "../cursor.h"

typedef struct {
    gsr_egl *egl;
    gsr_cursor *cursor;
    const char *display_to_capture; /* A copy is made of this */
    bool record_cursor;
    vec2i output_resolution;
    vec2i region_size;
    vec2i region_position;
} gsr_capture_ximage_params;

gsr_capture* gsr_capture_ximage_create(const gsr_capture_ximage_params *params);

#endif /* GSR_CAPTURE_XIMAGE_H */
