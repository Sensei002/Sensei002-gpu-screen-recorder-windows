#ifndef GSR_ENCODER_VIDEO_VAAPI_H
#define GSR_ENCODER_VIDEO_VAAPI_H

#include "video.h"

typedef struct gsr_egl gsr_egl;

typedef struct {
    gsr_egl *egl;
    gsr_color_depth color_depth;
} gsr_video_encoder_vaapi_params;

gsr_video_encoder* gsr_video_encoder_vaapi_create(const gsr_video_encoder_vaapi_params *params);

#endif /* GSR_ENCODER_VIDEO_VAAPI_H */
