#ifndef GSR_CAPTURE_V4L2_H
#define GSR_CAPTURE_V4L2_H

#include "capture.h"

typedef enum {
    GSR_CAPTURE_V4L2_PIXFMT_AUTO,
    GSR_CAPTURE_V4L2_PIXFMT_YUYV,
    GSR_CAPTURE_V4L2_PIXFMT_MJPEG
} gsr_capture_v4l2_pixfmt;

typedef struct {
    uint32_t width;
    uint32_t height;
} gsr_capture_v4l2_resolution;

typedef struct {
    uint32_t denominator;
    uint32_t numerator;
} gsr_capture_v4l2_framerate;

typedef struct {
    gsr_capture_v4l2_pixfmt pixfmt;
    gsr_capture_v4l2_resolution resolution;
    gsr_capture_v4l2_framerate framerate;
} gsr_capture_v4l2_supported_setup;

typedef struct {
    gsr_egl *egl;
    vec2i output_resolution;
    const char *device_path;
    gsr_capture_v4l2_pixfmt pixfmt;
    uint32_t camera_fps; /* Set to 0 if the best option should be chosen */
    gsr_capture_v4l2_resolution camera_resolution; /* Set to 0, 0 if the best option should be chosen */
} gsr_capture_v4l2_params;

gsr_capture* gsr_capture_v4l2_create(const gsr_capture_v4l2_params *params);

const char* gsr_capture_v4l2_pixfmt_to_string(gsr_capture_v4l2_pixfmt pixfmt);
uint32_t gsr_capture_v4l2_framerate_to_number(gsr_capture_v4l2_framerate framerate);
typedef void (*v4l2_devices_query_callback)(const char *path, const gsr_capture_v4l2_supported_setup *supported_setup, void *userdata);
void gsr_capture_v4l2_list_devices(v4l2_devices_query_callback callback, void *userdata);

#endif /* GSR_CAPTURE_V4L2_H */
