#ifndef GSR_CAPTURE_KMS_H
#define GSR_CAPTURE_KMS_H

#include "capture.h"
#include "../cursor.h"
#include "../kde_night_light.h"
#include "../../kms/kms_shared.h"

typedef struct {
    gsr_egl *egl;
    gsr_cursor *x11_cursor;
    gsr_kms_response *kms_response;
    gsr_kde_night_light *kde_night_light; /* optional */
    const char *display_to_capture; /* A copy is made of this */
    bool hdr;
    bool record_cursor;
    int fps;
    vec2i output_resolution;
    vec2i region_size;
    vec2i region_position;
} gsr_capture_kms_params;

gsr_capture* gsr_capture_kms_create(const gsr_capture_kms_params *params);

#endif /* GSR_CAPTURE_KMS_H */
