#ifndef GSR_RECORDER_ERROR_H
#define GSR_RECORDER_ERROR_H

/* The negated value of each error is the exit code that gpu-screen-recorder exits with */
typedef enum {
    GSR_ERROR_OK = 0,
    GSR_ERROR_GENERIC = -1,
    GSR_ERROR_UNSUPPORTED = -2,
    GSR_ERROR_CAPTURE_FAILED = -3,
    GSR_ERROR_VIDEO_CODEC_QUERY_FAILED = -11,
    GSR_ERROR_OPENGL_LOAD_FAILED = -22,
    GSR_ERROR_AUDIO_DEVICE_NOT_FOUND = -50,
    GSR_ERROR_MONITOR_NOT_FOUND = -51,
    GSR_ERROR_NO_VIDEO_CODEC_AVAILABLE = -52,
    GSR_ERROR_VIDEO_CODEC_RESOLUTION_UNSUPPORTED = -53,
    GSR_ERROR_VIDEO_CODEC_UNSUPPORTED = -54
} gsr_error;

static inline int gsr_error_to_exit_code(int error) {
    return error < 0 ? -error : 0;
}

#endif /* GSR_RECORDER_ERROR_H */
