#ifndef GSR_RECORDER_REPLAY_SAVE_H
#define GSR_RECORDER_REPLAY_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include "muxer.h"
#include "audio_capture.h"
#include "capture_setup.h"
#include "settings.h"

#include "../encoder/encoder.h"
#include "../replay_buffer/replay_buffer.h"

#define GSR_SAVE_REPLAY_SECONDS_FULL -1

typedef struct {
    int64_t pts_offset;
    int stream_index;
} gsr_audio_pts_offset;

/* Saves the replay buffer to a file on a separate thread */
typedef struct {
    pthread_t thread;
    bool thread_created;
    atomic_int finished;
    bool success;
    char output_filepath[PATH_MAX];

    AVCodecContext *video_codec_context;
    int video_stream_index;
    gsr_recording_output recording_output;
    gsr_replay_buffer_iterator video_start_iterator;
    int64_t video_pts_offset;
    gsr_audio_pts_offset *audio_pts_offsets;
    size_t num_audio_pts_offsets;
    gsr_replay_buffer *cloned_replay_buffer;
    gsr_encoder *encoder;
} gsr_replay_save;

void gsr_replay_save_init(gsr_replay_save *self);
bool gsr_replay_save_is_running(const gsr_replay_save *self);

/* Returns false if the replay failed to start. |current_save_replay_seconds| can be GSR_SAVE_REPLAY_SECONDS_FULL to save the whole replay buffer */
bool gsr_replay_save_start(gsr_replay_save *self, AVCodecContext *video_codec_context, int video_stream_index, const gsr_audio_capture *audio_capture, gsr_encoder *encoder, const gsr_recorder_settings *settings, const char *file_extension, bool hdr, gsr_video_sources *video_sources, int current_save_replay_seconds);
/* Returns true when the replay finished saving, in which case |success| and |output_filepath| are set. |output_filepath| is empty when nothing was saved */
bool gsr_replay_save_poll(gsr_replay_save *self, bool *success, const char **output_filepath);
/* Waits for an ongoing replay save to finish. Returns the same values as gsr_replay_save_poll */
bool gsr_replay_save_join(gsr_replay_save *self, bool *success, const char **output_filepath);

#endif /* GSR_RECORDER_REPLAY_SAVE_H */
