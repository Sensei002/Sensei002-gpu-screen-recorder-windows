#ifndef GSR_DEFS_H
#define GSR_DEFS_H

#include <stdbool.h>

#define GSR_VIDEO_CODEC_AUTO -1
#define GSR_BITRATE_MODE_AUTO -1

typedef enum {
    GSR_GPU_VENDOR_AMD,
    GSR_GPU_VENDOR_INTEL,
    GSR_GPU_VENDOR_NVIDIA,
    GSR_GPU_VENDOR_BROADCOM,
    GSR_GPU_VENDOR_APPLE
} gsr_gpu_vendor;

typedef struct {
    gsr_gpu_vendor vendor;
    int gpu_version; /* 0 if unknown */
    bool is_steam_deck;
} gsr_gpu_info;

typedef enum {
    GSR_MONITOR_ROT_0,
    GSR_MONITOR_ROT_90,
    GSR_MONITOR_ROT_180,
    GSR_MONITOR_ROT_270,
} gsr_monitor_rotation;

typedef struct {
    float red_x, red_y;
    float green_x, green_y;
    float blue_x, blue_y;
    float white_x, white_y;
} gsr_color_chromaticities;

typedef struct {
    float sdr_white_luminance; /* cd/m² (nits) */
    float max_peak_luminance;  /* cd/m² (nits) */
    gsr_color_chromaticities primaries; /* The color volume that the monitor image is encoded in, valid when |primaries_valid| is true */
    bool primaries_valid;
} gsr_monitor_hdr_info;

typedef enum {
    GSR_CONNECTION_X11,
    GSR_CONNECTION_WAYLAND,
    GSR_CONNECTION_DRM,
} gsr_connection_type;

typedef enum {
    GSR_VIDEO_QUALITY_MEDIUM,
    GSR_VIDEO_QUALITY_HIGH,
    GSR_VIDEO_QUALITY_VERY_HIGH,
    GSR_VIDEO_QUALITY_ULTRA,
} gsr_video_quality;

typedef enum {
    GSR_VIDEO_CODEC_H264,
    GSR_VIDEO_CODEC_HEVC,
    GSR_VIDEO_CODEC_HEVC_HDR,
    GSR_VIDEO_CODEC_HEVC_10BIT,
    GSR_VIDEO_CODEC_AV1,
    GSR_VIDEO_CODEC_AV1_HDR,
    GSR_VIDEO_CODEC_AV1_10BIT,
    GSR_VIDEO_CODEC_VP8,
    GSR_VIDEO_CODEC_VP9,
    GSR_VIDEO_CODEC_H264_VULKAN,
    GSR_VIDEO_CODEC_HEVC_VULKAN,
    GSR_VIDEO_CODEC_HEVC_HDR_VULKAN,
    GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN,
    GSR_VIDEO_CODEC_AV1_VULKAN,
    GSR_VIDEO_CODEC_AV1_HDR_VULKAN,
    GSR_VIDEO_CODEC_AV1_10BIT_VULKAN,
} gsr_video_codec;

typedef enum {
    GSR_AUDIO_CODEC_AAC,
    GSR_AUDIO_CODEC_OPUS,
    GSR_AUDIO_CODEC_FLAC,
} gsr_audio_codec;

typedef enum {
    GSR_PIXEL_FORMAT_YUV420,
    GSR_PIXEL_FORMAT_YUV444,
} gsr_pixel_format;

typedef enum {
    GSR_FRAMERATE_MODE_CONSTANT,
    GSR_FRAMERATE_MODE_VARIABLE,
    GSR_FRAMERATE_MODE_CONTENT,
} gsr_framerate_mode;

typedef enum {
    GSR_BITRATE_MODE_QP,
    GSR_BITRATE_MODE_VBR,
    GSR_BITRATE_MODE_CBR,
} gsr_bitrate_mode;

typedef enum {
    GSR_TUNE_PERFORMANCE,
    GSR_TUNE_QUALITY,
} gsr_tune;

typedef enum {
    GSR_VIDEO_ENCODER_HW_GPU,
    GSR_VIDEO_ENCODER_HW_CPU,
} gsr_video_encoder_hardware;

typedef enum {
    GSR_COLOR_RANGE_LIMITED,
    GSR_COLOR_RANGE_FULL,
} gsr_color_range;

typedef enum {
    GSR_COLOR_DEPTH_8_BITS,
    GSR_COLOR_DEPTH_10_BITS,
} gsr_color_depth;

typedef enum {
    GSR_REPLAY_STORAGE_RAM,
    GSR_REPLAY_STORAGE_DISK,
} gsr_replay_storage;

bool video_codec_is_hdr(gsr_video_codec video_codec);
gsr_video_codec hdr_video_codec_to_sdr_video_codec(gsr_video_codec video_codec);
gsr_color_depth video_codec_to_bit_depth(gsr_video_codec video_codec);
const char* video_codec_to_string(gsr_video_codec video_codec);
bool video_codec_is_av1(gsr_video_codec video_codec);
bool video_codec_is_vulkan(gsr_video_codec video_codec);
const char* audio_codec_get_name(gsr_audio_codec audio_codec);

#endif /* GSR_DEFS_H */
