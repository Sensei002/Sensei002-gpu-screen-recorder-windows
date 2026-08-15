#include "../../include/encoder/encoder.h"
#include "../../include/log.h"
#include "../../include/utils.h"
#include "../../include/ffmpeg_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static uint64_t clock_gettime_microseconds(clockid_t clock_id) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    clock_gettime(clock_id, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void gsr_write_first_frame_timestamp_file(const char *filepath) {
    const uint64_t evdev_compatible_ts = clock_gettime_microseconds(CLOCK_MONOTONIC);
    const uint64_t unix_time_microsec = clock_gettime_microseconds(CLOCK_REALTIME);

    FILE *file = fopen(filepath, "w");
    if(!file) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "failed to open timestamp file '%s': %s", filepath, strerror(errno));
        return;
    }

    fputs("monotonic_microsec\trealtime_microsec\n", file);
    fprintf(file, "%" PRIu64 "\t%" PRIu64 "\n", evdev_compatible_ts, unix_time_microsec);
    fclose(file);
}

bool gsr_encoder_init(gsr_encoder *self, gsr_replay_storage replay_storage, size_t replay_buffer_num_packets, double replay_buffer_time, const char *replay_directory) {
    memset(self, 0, sizeof(*self));
    self->num_recording_destinations = 0;
    self->recording_destination_id_counter = 0;
    self->first_pts = -1;

    if(pthread_mutex_init(&self->file_write_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_init: failed to create mutex");
        gsr_encoder_deinit(self, false);
        return false;
    }
    self->file_write_mutex_created = true;

    if(pthread_mutex_init(&self->replay_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_init: failed to create mutex");
        gsr_encoder_deinit(self, false);
        return false;
    }
    self->replay_mutex_created = true;

    if(replay_buffer_num_packets > 0) {
        self->replay_buffer = gsr_replay_buffer_create(replay_storage, replay_directory, replay_buffer_time, replay_buffer_num_packets);
        if(!self->replay_buffer) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_init: failed to create replay buffer");
            gsr_encoder_deinit(self, false);
            return false;
        }
    }

    return true;
}

void gsr_encoder_deinit(gsr_encoder *self, bool exiting)  {
    if(self->file_write_mutex_created)
        pthread_mutex_lock(&self->file_write_mutex);
    for(size_t i = 0; i < self->num_recording_destinations; ++i) {
        free(self->recording_destinations[i].first_frame_ts_filepath);
        self->recording_destinations[i].first_frame_ts_filepath = NULL;
        self->recording_destinations[i].first_frame_ts_written = false;
    }
    if(self->file_write_mutex_created)
        pthread_mutex_unlock(&self->file_write_mutex);

    if(self->replay_buffer) {
        pthread_mutex_lock(&self->replay_mutex);
        if(exiting)
            gsr_replay_buffer_destroy_at_exit(self->replay_buffer);
        else
            gsr_replay_buffer_destroy(self->replay_buffer);
        self->replay_buffer = NULL;
        pthread_mutex_unlock(&self->replay_mutex);
    }

    if(self->file_write_mutex_created) {
        self->file_write_mutex_created = false;
        pthread_mutex_destroy(&self->file_write_mutex);
    }

    if(self->replay_mutex_created) {
        self->replay_mutex_created = false;
        pthread_mutex_destroy(&self->replay_mutex);
    }

    self->num_recording_destinations = 0;
    self->recording_destination_id_counter = 0;
    self->first_pts = -1;
}

void gsr_encoder_receive_packets(gsr_encoder *self, AVCodecContext *codec_context, int64_t pts, int stream_index) {
    for(;;) {
        AVPacket *av_packet = av_packet_alloc();
        if(!av_packet)
            break;

        av_packet->data = NULL;
        av_packet->size = 0;
        int res = avcodec_receive_packet(codec_context, av_packet);
        if(res == 0) { // we have a packet, send the packet to the muxer
            if(self->first_pts == -1)
                self->first_pts = pts;

            av_packet->stream_index = stream_index;
            av_packet->pts = pts - self->first_pts;
            av_packet->dts = av_packet->pts;

            if(self->replay_buffer) {
                pthread_mutex_lock(&self->replay_mutex);
                const double time_now = clock_get_monotonic_seconds();
                if(!gsr_replay_buffer_append(self->replay_buffer, av_packet, time_now))
                    gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_receive_packets: failed to add replay buffer data");
                pthread_mutex_unlock(&self->replay_mutex);
            }

            pthread_mutex_lock(&self->file_write_mutex);
            const bool is_keyframe = av_packet->flags & AV_PKT_FLAG_KEY;
            for(size_t i = 0; i < self->num_recording_destinations; ++i) {
                gsr_encoder_recording_destination *recording_destination = &self->recording_destinations[i];
                if(recording_destination->codec_context != codec_context)
                    continue;

                if(is_keyframe)
                    recording_destination->has_received_keyframe = true;
                else if(!recording_destination->has_received_keyframe)
                    continue;

                if(recording_destination->first_frame_ts_filepath && !recording_destination->first_frame_ts_written) {
                    gsr_write_first_frame_timestamp_file(recording_destination->first_frame_ts_filepath);
                    recording_destination->first_frame_ts_written = true;
                }

                if(recording_destination->first_pts == -1)
                    recording_destination->first_pts = pts - recording_destination->start_pts;

                av_packet->pts = pts - recording_destination->start_pts - recording_destination->first_pts;
                av_packet->dts = av_packet->pts;

                av_packet_rescale_ts(av_packet, codec_context->time_base, recording_destination->stream->time_base);
                // TODO: Is av_interleaved_write_frame needed?. Answer: might be needed for mkv but dont use it! it causes frames to be inconsistent, skipping frames and duplicating frames.
                // TODO: av_interleaved_write_frame might be needed for cfr, or always for flv
                const int ret = av_write_frame(recording_destination->format_context, av_packet);
                if(ret >= 0)
                    gsr_av_format_context_mark_packet_written(recording_destination->format_context);
                if(ret < 0) {
                    char error_buffer[AV_ERROR_MAX_STRING_SIZE];
                    if(av_strerror(ret, error_buffer, sizeof(error_buffer)) < 0)
                        snprintf(error_buffer, sizeof(error_buffer), "Unknown error");
                    gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_receive_packets: failed to write frame index %d to muxer, reason: %s (%d)", av_packet->stream_index, error_buffer, ret);
                }
            }
            pthread_mutex_unlock(&self->file_write_mutex);

            av_packet_free(&av_packet);
        } else if (res == AVERROR(EAGAIN)) { // we have no packet
                                             // fprintf(stderr, "No packet!\n");
            av_packet_free(&av_packet);
            break;
        } else if (res == AVERROR_EOF) { // this is the end of the stream
            av_packet_free(&av_packet);
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_receive_packets: end of stream");
            break;
        } else {
            av_packet_free(&av_packet);
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_receive_packets: unexpected error: %d", res);
            break;
        }
    }
}

size_t gsr_encoder_add_recording_destination(gsr_encoder *self, AVCodecContext *codec_context, AVFormatContext *format_context, AVStream *stream, int64_t start_pts) {
    if(self->num_recording_destinations >= GSR_MAX_RECORDING_DESTINATIONS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_add_recording_destination: failed to add destination, reached the max amount of recording destinations (%d)", GSR_MAX_RECORDING_DESTINATIONS);
        return (size_t)-1;
    }

    for(size_t i = 0; i < self->num_recording_destinations; ++i) {
        if(self->recording_destinations[i].stream == stream) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_encoder_add_recording_destination: failed to add destination, the stream %p already exists as an output", (void*)stream);
            return (size_t)-1;
        }
    }

    pthread_mutex_lock(&self->file_write_mutex);
    gsr_encoder_recording_destination *recording_destination = &self->recording_destinations[self->num_recording_destinations];
    recording_destination->id = self->recording_destination_id_counter;
    recording_destination->codec_context = codec_context;
    recording_destination->format_context = format_context;
    recording_destination->stream = stream;
    recording_destination->start_pts = start_pts;
    recording_destination->first_pts = -1;
    recording_destination->has_received_keyframe = false;
    recording_destination->first_frame_ts_filepath = NULL;
    recording_destination->first_frame_ts_written = false;

    ++self->recording_destination_id_counter;
    ++self->num_recording_destinations;
    pthread_mutex_unlock(&self->file_write_mutex);

    return recording_destination->id;
}

bool gsr_encoder_remove_recording_destination(gsr_encoder *self, size_t id) {
    bool found = false;
    pthread_mutex_lock(&self->file_write_mutex);
    for(size_t i = 0; i < self->num_recording_destinations; ++i) {
        if(self->recording_destinations[i].id == id) {
            free(self->recording_destinations[i].first_frame_ts_filepath);
            self->recording_destinations[i].first_frame_ts_filepath = NULL;
            self->recording_destinations[i].first_frame_ts_written = false;
            self->recording_destinations[i] = self->recording_destinations[self->num_recording_destinations - 1];
            --self->num_recording_destinations;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&self->file_write_mutex);
    return found;
}

bool gsr_encoder_set_recording_destination_first_frame_ts_filepath(gsr_encoder *self, size_t id, const char *filepath) {
    if(!filepath)
        return false;

    bool found = false;
    pthread_mutex_lock(&self->file_write_mutex);
    for(size_t i = 0; i < self->num_recording_destinations; ++i) {
        if(self->recording_destinations[i].id == id) {
            char *filepath_copy = strdup(filepath);
            if(!filepath_copy)
                break;

            free(self->recording_destinations[i].first_frame_ts_filepath);
            self->recording_destinations[i].first_frame_ts_filepath = filepath_copy;
            self->recording_destinations[i].first_frame_ts_written = false;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&self->file_write_mutex);
    return found;
}
