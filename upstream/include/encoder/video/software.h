#ifndef GSR_ENCODER_VIDEO_SOFTWARE_H
#define GSR_ENCODER_VIDEO_SOFTWARE_H

#include "video.h"

typedef struct gsr_egl gsr_egl;

typedef struct {
    gsr_egl *egl;
    gsr_color_depth color_depth;
} gsr_video_encoder_software_params;

gsr_video_encoder* gsr_video_encoder_software_create(const gsr_video_encoder_software_params *params);

#endif /* GSR_ENCODER_VIDEO_SOFTWARE_H */
