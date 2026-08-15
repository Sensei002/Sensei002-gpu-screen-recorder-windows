#ifndef GSR_CAPTURE_XCOMPOSITE_H
#define GSR_CAPTURE_XCOMPOSITE_H

#include "capture.h"
#include "../vec2.h"
#include "../cursor.h"

typedef struct {
    gsr_egl *egl;
    gsr_cursor *cursor;
    unsigned long window;
    bool follow_focused; /* If this is set then |window| is ignored */
    bool record_cursor;
    vec2i output_resolution;
} gsr_capture_xcomposite_params;

gsr_capture* gsr_capture_xcomposite_create(const gsr_capture_xcomposite_params *params);

#endif /* GSR_CAPTURE_XCOMPOSITE_H */
