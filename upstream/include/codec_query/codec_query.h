#ifndef GSR_CODEC_QUERY_H
#define GSR_CODEC_QUERY_H

#include <stdbool.h>
#include "../vec2.h"

typedef struct {
    bool supported;
    bool low_power;
    vec2i max_resolution;
} gsr_supported_video_codec;

typedef struct {
    gsr_supported_video_codec h264;
    gsr_supported_video_codec hevc;
    gsr_supported_video_codec hevc_hdr;
    gsr_supported_video_codec hevc_10bit;
    gsr_supported_video_codec av1;
    gsr_supported_video_codec av1_hdr;
    gsr_supported_video_codec av1_10bit;
    gsr_supported_video_codec vp8;
    gsr_supported_video_codec vp9;
} gsr_supported_video_codecs;

#endif /* GSR_CODEC_QUERY_H */
