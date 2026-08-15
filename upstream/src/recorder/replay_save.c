#include "../../include/recorder/replay_save.h"
#include "../../include/ffmpeg_utils.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

void gsr_replay_save_init(gsr_replay_save *self) {
    memset(self, 0, sizeof(*self));
    atomic_init(&self->finished, 0);
}

bool gsr_replay_save_is_running(const gsr_replay_save *self) {
    return self->thread_created;
}

static void gsr_replay_save_cleanup(gsr_replay_save *self) {
    if(self->cloned_replay_buffer) {
        pthread_mutex_lock(&self->encoder->replay_mutex);
        gsr_replay_buffer_destroy(self->cloned_replay_buffer);
        pthread_mutex_unlock(&self->encoder->replay_mutex);
        self->cloned_replay_buffer = NULL;
    }

    if(self->audio_pts_offsets) {
        free(self->audio_pts_offsets);
        self->audio_pts_offsets = NULL;
    }
    self->num_audio_pts_offsets = 0;
}

static void* replay_save_thread(void *userdata) {
    gsr_replay_save *self = userdata;
    bool success = true;
    gsr_replay_buffer_iterator replay_iterator = self->video_start_iterator;

    for(;;) {
        AVPacket *replay_packet = gsr_replay_buffer_iterator_get_packet(self->cloned_replay_buffer, replay_iterator);
        uint8_t *replay_packet_data = NULL;
        if(replay_packet) {
            pthread_mutex_lock(&self->encoder->replay_mutex);
            replay_packet_data = gsr_replay_buffer_iterator_get_packet_data(self->cloned_replay_buffer, replay_iterator);
            pthread_mutex_unlock(&self->encoder->replay_mutex);
        }

        if(!replay_packet) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_save: no replay packet");
            success = false;
            break;
        }

        if(!replay_packet->data && !replay_packet_data) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_save: no replay packet data");
            success = false;
            break;
        }

        // TODO: Check if successful
        AVPacket av_packet;
        memset(&av_packet, 0, sizeof(av_packet));
        //av_packet_from_data(av_packet, replay_packet->data, replay_packet->size);
        av_packet.data = replay_packet->data ? replay_packet->data : replay_packet_data;
        av_packet.size = replay_packet->size;
        av_packet.stream_index = replay_packet->stream_index;
        av_packet.pts = replay_packet->pts;
        av_packet.dts = replay_packet->pts;
        av_packet.flags = replay_packet->flags;
        //av_packet.duration = replay_packet->duration;

        AVStream *stream = self->recording_output.video_stream;
        AVCodecContext *codec_context = self->video_codec_context;

        if(av_packet.stream_index == self->video_stream_index) {
            av_packet.pts -= self->video_pts_offset;
            av_packet.dts -= self->video_pts_offset;
        } else {
            gsr_recording_audio_stream *recording_start_audio = gsr_recording_output_get_audio_stream_by_index(&self->recording_output, av_packet.stream_index);
            if(!recording_start_audio) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_replay_save: failed to find audio stream by index: %d", av_packet.stream_index);
                free(replay_packet_data);
                continue;
            }

            const gsr_audio_track *audio_track = recording_start_audio->audio_track;
            stream = recording_start_audio->stream;
            codec_context = audio_track->codec_context;

            const gsr_audio_pts_offset *audio_pts_offset = &self->audio_pts_offsets[av_packet.stream_index - 1];
            assert(audio_pts_offset->stream_index == av_packet.stream_index);
            av_packet.pts -= audio_pts_offset->pts_offset;
            av_packet.dts -= audio_pts_offset->pts_offset;
        }

        //av_packet.stream_index = stream->index;
        av_packet_rescale_ts(&av_packet, codec_context->time_base, stream->time_base);

        const int ret = av_write_frame(self->recording_output.av_format_context, &av_packet);
        if(ret >= 0)
            gsr_av_format_context_mark_packet_written(self->recording_output.av_format_context);
        else
            gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to write frame index %d to muxer, reason: %s (%d)", av_packet.stream_index, gsr_av_error_to_string(ret), ret);

        free(replay_packet_data);

        //av_packet_free(&av_packet);
        if(!gsr_replay_buffer_iterator_next(self->cloned_replay_buffer, &replay_iterator))
            break;
    }

    gsr_recording_output_stop(&self->recording_output);

    self->success = success;
    gsr_replay_save_cleanup(self);
    atomic_store(&self->finished, 1);
    return NULL;
}

bool gsr_replay_save_start(gsr_replay_save *self, AVCodecContext *video_codec_context, int video_stream_index, const gsr_audio_capture *audio_capture, gsr_encoder *encoder, const gsr_recorder_settings *settings, const char *file_extension, bool hdr, gsr_video_sources *video_sources, int current_save_replay_seconds) {
    if(self->thread_created)
        return true;

    self->encoder = encoder;
    self->video_codec_context = video_codec_context;
    self->video_stream_index = video_stream_index;
    self->output_filepath[0] = '\0';
    self->success = false;
    atomic_store(&self->finished, 0);

    pthread_mutex_lock(&encoder->replay_mutex);
    self->cloned_replay_buffer = gsr_replay_buffer_clone(encoder->replay_buffer);
    pthread_mutex_unlock(&encoder->replay_mutex);
    if(!self->cloned_replay_buffer) {
        /* TODO: Return this error to mark the replay as failed */
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to save replay: failed to clone replay buffer");
        return false;
    }

    const gsr_replay_buffer_iterator start_iterator = {0, 0};
    const gsr_replay_buffer_iterator search_start_iterator = current_save_replay_seconds == GSR_SAVE_REPLAY_SECONDS_FULL ? start_iterator : gsr_replay_buffer_find_packet_index_by_time_passed(self->cloned_replay_buffer, current_save_replay_seconds);
    self->video_start_iterator = gsr_replay_buffer_find_keyframe(self->cloned_replay_buffer, search_start_iterator, video_stream_index, false);
    if(self->video_start_iterator.packet_index == (size_t)-1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to save replay: failed to find a video keyframe. perhaps replay was saved too fast, before anything has been recorded");
        gsr_replay_save_cleanup(self);
        return false;
    }

    self->video_pts_offset = gsr_replay_buffer_iterator_get_packet(self->cloned_replay_buffer, self->video_start_iterator)->pts;

    if(audio_capture->num_tracks > 0) {
        self->audio_pts_offsets = calloc(audio_capture->num_tracks, sizeof(gsr_audio_pts_offset));
        if(!self->audio_pts_offsets) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to save replay: failed to allocate audio pts offsets");
            gsr_replay_save_cleanup(self);
            return false;
        }
    }

    for(size_t i = 0; i < audio_capture->num_tracks; ++i) {
        const gsr_audio_track *audio_track = &audio_capture->tracks[i];
        const gsr_replay_buffer_iterator audio_start_iterator = gsr_replay_buffer_find_keyframe(self->cloned_replay_buffer, self->video_start_iterator, audio_track->stream_index, false);
        const int64_t audio_pts_offset = audio_start_iterator.packet_index == (size_t)-1 ? 0 : gsr_replay_buffer_iterator_get_packet(self->cloned_replay_buffer, audio_start_iterator)->pts;
        self->audio_pts_offsets[i].pts_offset = audio_pts_offset;
        self->audio_pts_offsets[i].stream_index = audio_track->stream_index;
        ++self->num_audio_pts_offsets;
    }

    if(!gsr_create_new_recording_filepath_from_timestamp(self->output_filepath, sizeof(self->output_filepath), settings->filename, "Replay", file_extension, settings->date_folders)) {
        gsr_replay_save_cleanup(self);
        return false;
    }

    if(!gsr_recording_output_start(&self->recording_output, self->output_filepath, settings, video_codec_context, audio_capture, hdr, video_sources)) {
        gsr_replay_save_cleanup(self);
        return false;
    }

    if(pthread_create(&self->thread, NULL, replay_save_thread, self) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to save replay: failed to create thread");
        gsr_recording_output_stop(&self->recording_output);
        gsr_replay_save_cleanup(self);
        return false;
    }

    self->thread_created = true;
    return true;
}

static bool gsr_replay_save_finish(gsr_replay_save *self, bool *success, const char **output_filepath) {
    pthread_join(self->thread, NULL);
    self->thread_created = false;
    *success = self->success;
    *output_filepath = self->output_filepath;
    return true;
}

bool gsr_replay_save_poll(gsr_replay_save *self, bool *success, const char **output_filepath) {
    if(!self->thread_created || !atomic_load(&self->finished))
        return false;

    return gsr_replay_save_finish(self, success, output_filepath);
}

bool gsr_replay_save_join(gsr_replay_save *self, bool *success, const char **output_filepath) {
    if(!self->thread_created)
        return false;

    return gsr_replay_save_finish(self, success, output_filepath);
}
