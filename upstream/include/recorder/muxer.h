#ifndef GSR_RECORDER_MUXER_H
#define GSR_RECORDER_MUXER_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include "audio_capture.h"
#include "capture_setup.h"
#include "settings.h"

#include <libavformat/avformat.h>

typedef struct {
    const gsr_audio_track *audio_track;
    AVStream *stream;
} gsr_recording_audio_stream;

typedef struct {
    AVFormatContext *av_format_context;
    AVStream *video_stream;
    gsr_recording_audio_stream *audio_streams;
    size_t num_audio_streams;
} gsr_recording_output;

AVStream* create_stream(AVFormatContext *av_format_context, AVCodecContext *codec_context);
bool add_hdr_metadata_to_video_stream(gsr_capture *cap, AVStream *video_stream);
void set_format_context_options(AVFormatContext *av_format_context);
void av_write_header(AVFormatContext *av_format_context, const char *ffmpeg_opts);

/* Returns false on failure. Creates the output file and its video and audio streams */
bool gsr_recording_output_start(gsr_recording_output *self, const char *filename, const gsr_recorder_settings *settings, AVCodecContext *video_codec_context, const gsr_audio_capture *audio_capture, bool hdr, gsr_video_sources *video_sources);
bool gsr_recording_output_stop(gsr_recording_output *self);
gsr_recording_audio_stream* gsr_recording_output_get_audio_stream_by_index(gsr_recording_output *self, int stream_index);

/* |filepath| should be at least PATH_MAX bytes in size. Creates the directories of the filepath */
bool gsr_create_new_recording_filepath_from_timestamp(char *filepath, size_t filepath_size, const char *directory, const char *filename_prefix, const char *file_extension, bool date_folders);

size_t calculate_estimated_replay_buffer_packets(int64_t replay_buffer_size_secs, int fps, gsr_audio_codec audio_codec, const gsr_audio_input_tracks *audio_inputs);

#endif /* GSR_RECORDER_MUXER_H */
