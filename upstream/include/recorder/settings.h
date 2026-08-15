#ifndef GSR_RECORDER_SETTINGS_H
#define GSR_RECORDER_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "../defs.h"
#include "../vec2.h"

typedef struct {
    gsr_video_encoder_hardware video_encoder;
    gsr_pixel_format pixel_format;
    gsr_framerate_mode framerate_mode;
    gsr_color_range color_range;
    gsr_tune tune;
    gsr_video_codec video_codec;
    gsr_audio_codec audio_codec;
    gsr_bitrate_mode bitrate_mode;
    gsr_video_quality video_quality;
    gsr_replay_storage replay_storage;

    const char *capture_source;
    const char *container_format;
    const char *filename;
    const char *replay_recording_directory;
    const char *portal_session_token_filepath;
    const char *recording_saved_script;
    const char *ffmpeg_opts;
    const char *ffmpeg_video_opts;
    const char *ffmpeg_audio_opts;
    bool verbose;
    bool gl_debug;
    bool fallback_cpu_encoding;
    bool low_power;
    bool exclude_metadata;
    bool record_cursor;
    bool date_folders;
    bool restore_portal_session;
    bool restart_replay_on_save;
    bool write_first_frame_ts;
    bool is_replaying;
    bool is_livestream;
    bool is_output_piped;
    bool low_latency_recording;
    bool very_old_gpu;
    int64_t video_bitrate;
    int64_t audio_bitrate;
    int64_t fps;
    int64_t replay_buffer_size_secs;
    double keyint;
    vec2i output_resolution;
    vec2i region_size;
    vec2i region_position;
} gsr_recorder_settings;

#endif /* GSR_RECORDER_SETTINGS_H */
