#include "../../include/recorder/audio_capture.h"
#include "../../include/recorder/error.h"
#include "../../include/recorder/audio_codec.h"
#include "../../include/utils.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

int gsr_audio_init_filter_graph(AVCodecContext *audio_codec_context, AVFilterGraph **graph, AVFilterContext **sink, AVFilterContext **src_filter_ctx, size_t num_sources) {
    char ch_layout[64];
    int err = 0;
    ch_layout[0] = '\0';

    // C89-style variable declaration to
    // avoid problems because of goto
    AVFilterGraph* filter_graph = NULL;
    AVFilterContext* mix_ctx = NULL;

    const AVFilter* mix_filter = NULL;
    const AVFilter* abuffersink = NULL;
    AVFilterContext* abuffersink_ctx = NULL;
    char args[512] = { 0 };
#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(7, 107, 100)
    bool normalize = false;
#endif

    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Unable to create filter graph");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    for(size_t i = 0; i < num_sources; ++i) {
        const AVFilter *abuffer = avfilter_get_by_name("abuffer");
        if (!abuffer) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Could not find the abuffer filter");
            err = AVERROR_FILTER_NOT_FOUND;
            goto fail;
        }

        AVFilterContext *abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, NULL);
        if (!abuffer_ctx) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Could not allocate the abuffer instance");
            err = AVERROR(ENOMEM);
            goto fail;
        }

        #if LIBAVCODEC_VERSION_MAJOR < 60
        av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, AV_CH_LAYOUT_STEREO);
        #else
        av_channel_layout_describe(&audio_codec_context->ch_layout, ch_layout, sizeof(ch_layout));
        #endif
        av_opt_set    (abuffer_ctx, "channel_layout", ch_layout,                                               AV_OPT_SEARCH_CHILDREN);
        av_opt_set    (abuffer_ctx, "sample_fmt",     av_get_sample_fmt_name(audio_codec_context->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        av_opt_set_q  (abuffer_ctx, "time_base",      audio_codec_context->time_base,                          AV_OPT_SEARCH_CHILDREN);
        av_opt_set_int(abuffer_ctx, "sample_rate",    audio_codec_context->sample_rate,                        AV_OPT_SEARCH_CHILDREN);
        av_opt_set_int(abuffer_ctx, "bit_rate",       audio_codec_context->bit_rate,                           AV_OPT_SEARCH_CHILDREN);

        err = avfilter_init_str(abuffer_ctx, NULL);
        if (err < 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Could not initialize the abuffer filter");
            goto fail;
        }

        src_filter_ctx[i] = abuffer_ctx;
    }

    mix_filter = avfilter_get_by_name("amix");
    if (!mix_filter) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the mix filter.\n");
        err = AVERROR_FILTER_NOT_FOUND;
        goto fail;
    }

#if LIBAVFILTER_VERSION_INT >= AV_VERSION_INT(7, 107, 100)
    snprintf(args, sizeof(args), "inputs=%d:normalize=%s", (int)num_sources, normalize ? "true" : "false");
#else
    snprintf(args, sizeof(args), "inputs=%d", (int)num_sources);
    gsr_log(GSR_LOG_LEVEL_WARNING, "your ffmpeg version doesn't support disabling normalizing of mixed audio. Volume might be lower than expected");
#endif

    err = avfilter_graph_create_filter(&mix_ctx, mix_filter, "amix", args, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio amix filter\n");
        goto fail;
    }

    abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not find the abuffersink filter");
        err = AVERROR_FILTER_NOT_FOUND;
        goto fail;
    }

    abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
    if (!abuffersink_ctx) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not allocate the abuffersink instance");
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = avfilter_init_str(abuffersink_ctx, NULL);
    if (err < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not initialize the abuffersink instance");
        goto fail;
    }

    err = 0;
    for(size_t i = 0; i < num_sources; ++i) {
        AVFilterContext *src_ctx = src_filter_ctx[i];
        if (err >= 0)
            err = avfilter_link(src_ctx, 0, mix_ctx, i);
    }
    if (err >= 0)
        err = avfilter_link(mix_ctx, 0, abuffersink_ctx, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error connecting filters\n");
        goto fail;
    }

    err = avfilter_graph_config(filter_graph, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
        goto fail;
    }

    /* Make sure the sink always outputs frames with the exact amount of samples the audio encoder wants,
       otherwise the audio encoder rejects the frame and that piece of audio is lost */
    av_buffersink_set_frame_size(abuffersink_ctx, audio_codec_context->frame_size);

    *graph = filter_graph;
    *sink = abuffersink_ctx;

    return 0;

fail:
    avfilter_graph_free(&filter_graph);
    memset(src_filter_ctx, 0, num_sources * sizeof(AVFilterContext*));  // possibly unnecessary?
    return err;
}

static void* audio_device_thread(void *userdata) {
    const gsr_audio_device_thread_userdata *thread_userdata = userdata;
    gsr_audio_capture *self = thread_userdata->audio_capture;
    gsr_audio_track *track = thread_userdata->track;
    gsr_audio_device_capture *device = thread_userdata->device;
    gsr_recording_clock *clock = self->clock;
    const atomic_int *running = self->running;

    const enum AVSampleFormat sound_device_sample_format = audio_format_to_sample_format(audio_codec_context_get_audio_format(track->codec_context));
    /* TODO: Always do conversion for now. This fixes issue with stuttering audio on pulseaudio with opus + multiple audio sources merged */
    const bool needs_audio_conversion = true;
    SwrContext *swr = NULL;
    if(needs_audio_conversion) {
        swr = swr_alloc();
        if(!swr) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create SwrContext");
            return NULL;
        }
        #if LIBAVUTIL_VERSION_MAJOR <= 56
        av_opt_set_channel_layout(swr, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        av_opt_set_channel_layout(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        #elif LIBAVUTIL_VERSION_MAJOR >= 59
        av_opt_set_chlayout(swr, "in_chlayout", &track->codec_context->ch_layout, 0);
        av_opt_set_chlayout(swr, "out_chlayout", &track->codec_context->ch_layout, 0);
        #else
        av_opt_set_chlayout(swr, "in_channel_layout", &track->codec_context->ch_layout, 0);
        av_opt_set_chlayout(swr, "out_channel_layout", &track->codec_context->ch_layout, 0);
        #endif
        av_opt_set_int(swr, "in_sample_rate", track->codec_context->sample_rate, 0);
        av_opt_set_int(swr, "out_sample_rate", track->codec_context->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt", sound_device_sample_format, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", track->codec_context->sample_fmt, 0);
        swr_init(swr);
    }

    const double audio_fps = (double)track->codec_context->sample_rate / (double)track->codec_context->frame_size;
    const int64_t timeout_ms = llround(1000.0 / audio_fps);
    const double timeout_sec = 1000.0 / audio_fps / 1000.0;
    int64_t num_received_frames = 0;

    /* The sound device is opened before the recording starts, so it can contain old audio from before the recording started.
       Discard it so the recording doesn't start with old audio. */
    if(device->sound_device.handle)
        sound_device_flush(&device->sound_device);

    while(atomic_load(running)) {
        void *sound_buffer;
        int sound_buffer_size = -1;
        const double time_before_read_seconds = clock_get_monotonic_seconds();
        if(device->sound_device.handle) {
            // TODO: use this instead of calculating time to read. But this can fluctuate and we dont want to go back in time,
            // also it's 0.0 for some users???
            double latency_seconds = 0.0;
            sound_buffer_size = sound_device_read_next_chunk(&device->sound_device, &sound_buffer, timeout_sec * 2.0, &latency_seconds);
        }

        const bool got_audio_data = sound_buffer_size >= 0;
        //fprintf(stderr, "got audio data: %s\n", got_audio_data ? "yes" : "no");
        //fprintf(stderr, "time to read: %f, %s, %f\n", time_to_read_seconds, got_audio_data ? "yes" : "no", timeout_sec);
        const double this_audio_frame_time = gsr_recording_clock_get_time(clock);

        if(gsr_recording_clock_is_paused(clock)) {
            if(!device->sound_device.handle)
                av_usleep(timeout_ms * 1000);

            continue;
        }

        int ret = av_frame_make_writable(device->frame);
        if (ret < 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to make audio frame writable");
            break;
        }

        // TODO: Is this |received_audio_time| really correct?
        const int64_t num_expected_frames = floor((this_audio_frame_time - gsr_recording_clock_get_start_time(clock)) / timeout_sec);
        int64_t num_missing_frames = num_expected_frames > num_received_frames ? num_expected_frames - num_received_frames : 0;

        if(got_audio_data)
            num_missing_frames = num_missing_frames > 1 ? num_missing_frames - 1 : 0;

        if(!device->sound_device.handle)
            num_missing_frames = num_missing_frames < 1 ? 1 : num_missing_frames;

        // Fucking hell is there a better way to do this? I JUST WANT TO KEEP VIDEO AND AUDIO SYNCED HOLY FUCK I WANT TO KILL MYSELF NOW.
        // THIS PIECE OF SHIT WANTS EMPTY FRAMES OTHERWISE VIDEO PLAYS TOO FAST TO KEEP UP WITH AUDIO OR THE AUDIO PLAYS TOO EARLY.
        // BUT WE CANT USE DELAYS TO GIVE DUMMY DATA BECAUSE PULSEAUDIO MIGHT GIVE AUDIO A BIG DELAYED!!!
        // This garbage is needed because we want to produce constant frame rate videos instead of variable frame rate
        // videos because bad software such as video editing software and VLC do not support variable frame rate software,
        // despite nvidia shadowplay and xbox game bar producing variable frame rate videos.
        // So we have to make sure we produce frames at the same relative rate as the video.
        if((num_missing_frames >= 1 && got_audio_data) || num_missing_frames >= 5 || !device->sound_device.handle) {
            // Fill the missing frames with silence. Duplicating the previous audio frame to fill the gap instead
            // sounds like a stutter and it's especially noticeable at the start of the recording when the audio device
            // hasn't started to deliver audio at a stable rate yet, which repeats the first audio frame multiple times.
            if(needs_audio_conversion)
                swr_convert(swr, &device->frame->data[0], track->codec_context->frame_size, (const uint8_t**)&self->empty_audio, track->codec_context->frame_size);
            else
                device->frame->data[0] = self->empty_audio;

            // TODO: Check if duplicate frame can be saved just by writing it with a different pts instead of sending it again
            pthread_mutex_lock(&self->filter_mutex);
            for(int i = 0; i < num_missing_frames; ++i) {
                if(track->graph) {
                    // TODO: av_buffersrc_add_frame
                    if(av_buffersrc_write_frame(device->src_filter_ctx, device->frame) < 0) {
                        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to add audio frame to filter");
                    }
                } else {
                    ret = avcodec_send_frame(track->codec_context, device->frame);
                    if(ret >= 0) {
                        // TODO: Move to separate thread because this could write to network (for example when livestreaming)
                        gsr_encoder_receive_packets(self->encoder, track->codec_context, device->frame->pts, track->stream_index);
                    } else {
                        gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to encode audio");
                    }
                    track->pts += track->codec_context->frame_size;
                }

                device->frame->pts += track->codec_context->frame_size;
                num_received_frames++;
            }
            pthread_mutex_unlock(&self->filter_mutex);
        }

        if(!device->sound_device.handle) {
            av_usleep(timeout_ms * 1000);
        } else if(got_audio_data) {
            // The frame has to be made writable again if the frame was already sent to the audio filter above (when filling missing frames)
            // because the audio filter only references the frame data instead of copying it. Without this the sent frames data would be
            // overwritten with the audio data below, causing the audio to repeat instead of the missing frames being silent.
            ret = av_frame_make_writable(device->frame);
            if (ret < 0) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to make audio frame writable");
                break;
            }

            // TODO: Instead of converting audio, get float audio from alsa. Or does alsa do conversion internally to get this format?
            if(needs_audio_conversion)
                swr_convert(swr, &device->frame->data[0], track->codec_context->frame_size, (const uint8_t**)&sound_buffer, track->codec_context->frame_size);
            else
                device->frame->data[0] = (uint8_t*)sound_buffer;

            pthread_mutex_lock(&self->filter_mutex);

            if(track->graph) {
                // TODO: av_buffersrc_add_frame
                if(av_buffersrc_write_frame(device->src_filter_ctx, device->frame) < 0) {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "failed to add audio frame to filter");
                }
            } else {
                ret = avcodec_send_frame(track->codec_context, device->frame);
                if(ret >= 0) {
                    // TODO: Move to separate thread because this could write to network (for example when livestreaming)
                    gsr_encoder_receive_packets(self->encoder, track->codec_context, device->frame->pts, track->stream_index);
                } else {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to encode audio");
                }
                track->pts += track->codec_context->frame_size;
            }

            device->frame->pts += track->codec_context->frame_size;
            num_received_frames++;
            pthread_mutex_unlock(&self->filter_mutex);
        } else {
            // TODO: Maybe sleep for time_to_sleep_until_next_frame/4? for better latency
            const double time_after_read_seconds = clock_get_monotonic_seconds();
            const double time_to_read_seconds = time_after_read_seconds - time_before_read_seconds;
            const double time_to_sleep_until_next_frame = timeout_sec - time_to_read_seconds;
            if(time_to_sleep_until_next_frame > 0.0)
                av_usleep(time_to_sleep_until_next_frame * 1000ULL * 1000ULL);
        }
    }

    if(swr)
        swr_free(&swr);

    return NULL;
}

static void* amix_thread(void *userdata) {
    gsr_audio_capture *self = userdata;
    AVFrame *aframe = av_frame_alloc();
    while(atomic_load(self->running)) {
        pthread_mutex_lock(&self->filter_mutex);
        for(size_t i = 0; i < self->num_tracks; ++i) {
            gsr_audio_track *track = &self->tracks[i];
            if(!track->sink)
                continue;

            int err = 0;
            while((err = av_buffersink_get_frame(track->sink, aframe)) >= 0) {
                aframe->pts = track->pts;
                err = avcodec_send_frame(track->codec_context, aframe);
                if(err >= 0) {
                    /* TODO: Move to separate thread because this could write to network (for example when livestreaming) */
                    gsr_encoder_receive_packets(self->encoder, track->codec_context, aframe->pts, track->stream_index);
                } else {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to encode audio");
                }
                av_frame_unref(aframe);
                track->pts += track->codec_context->frame_size;
            }
        }
        pthread_mutex_unlock(&self->filter_mutex);
        av_usleep(5 * 1000); /* 5 milliseconds */
    }
    av_frame_free(&aframe);
    return NULL;
}

int gsr_audio_capture_init(gsr_audio_capture *self, gsr_encoder *encoder, gsr_recording_clock *clock, const atomic_int *running) {
    memset(self, 0, sizeof(*self));
    self->encoder = encoder;
    self->clock = clock;
    self->running = running;

    if(pthread_mutex_init(&self->filter_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_audio_capture_init: failed to initialize mutex");
        return GSR_ERROR_GENERIC;
    }
    self->filter_mutex_initialized = true;

    return GSR_ERROR_OK;
}

void gsr_audio_capture_deinit(gsr_audio_capture *self) {
    gsr_audio_capture_join_threads(self);

    for(size_t i = 0; i < self->num_tracks; ++i) {
        gsr_audio_track_deinit(&self->tracks[i]);
    }

    if(self->tracks) {
        free(self->tracks);
        self->tracks = NULL;
    }
    self->num_tracks = 0;
    self->capacity_tracks = 0;

    if(self->empty_audio) {
        free(self->empty_audio);
        self->empty_audio = NULL;
    }

    if(self->filter_mutex_initialized) {
        pthread_mutex_destroy(&self->filter_mutex);
        self->filter_mutex_initialized = false;
    }
}

bool gsr_audio_capture_add_track(gsr_audio_capture *self, const gsr_audio_track *track) {
    if(!gsr_array_ensure_capacity((void**)&self->tracks, self->num_tracks, &self->capacity_tracks, sizeof(gsr_audio_track)))
        return false;

    self->tracks[self->num_tracks] = *track;
    ++self->num_tracks;
    return true;
}

int gsr_audio_capture_start(gsr_audio_capture *self, int audio_max_frame_size, bool uses_amix) {
    const size_t audio_buffer_size = audio_max_frame_size * 4 * 2; /* max 4 bytes/sample, 2 channels */
    self->empty_audio = calloc(1, audio_buffer_size);
    if(!self->empty_audio) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create empty audio");
        return GSR_ERROR_GENERIC;
    }

    for(size_t i = 0; i < self->num_tracks; ++i) {
        gsr_audio_track *track = &self->tracks[i];
        for(size_t j = 0; j < track->num_audio_devices; ++j) {
            gsr_audio_device_capture *device = &track->audio_devices[j];
            device->thread_userdata.audio_capture = self;
            device->thread_userdata.track = track;
            device->thread_userdata.device = device;
            if(pthread_create(&device->thread, NULL, audio_device_thread, &device->thread_userdata) != 0) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create audio thread");
                return GSR_ERROR_GENERIC;
            }
            device->thread_created = true;
        }
    }

    if(uses_amix) {
        if(pthread_create(&self->amix_thread, NULL, amix_thread, self) != 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create audio mix thread");
            return GSR_ERROR_GENERIC;
        }
        self->amix_thread_created = true;
    }

    return GSR_ERROR_OK;
}

void gsr_audio_capture_join_threads(gsr_audio_capture *self) {
    for(size_t i = 0; i < self->num_tracks; ++i) {
        gsr_audio_track *track = &self->tracks[i];
        for(size_t j = 0; j < track->num_audio_devices; ++j) {
            gsr_audio_device_capture *device = &track->audio_devices[j];
            if(device->thread_created) {
                pthread_join(device->thread, NULL);
                device->thread_created = false;
            }
        }
    }

    if(self->amix_thread_created) {
        pthread_join(self->amix_thread, NULL);
        self->amix_thread_created = false;
    }
}

void gsr_audio_capture_lock_filter(gsr_audio_capture *self) {
    pthread_mutex_lock(&self->filter_mutex);
}

void gsr_audio_capture_unlock_filter(gsr_audio_capture *self) {
    pthread_mutex_unlock(&self->filter_mutex);
}

static int audio_track_alloc_devices(gsr_audio_track *self, size_t num_devices) {
    self->audio_devices = calloc(num_devices, sizeof(gsr_audio_device_capture));
    if(!self->audio_devices) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate audio devices");
        return GSR_ERROR_GENERIC;
    }
    return GSR_ERROR_OK;
}

int gsr_audio_track_init_device_inputs(gsr_audio_track *self, const gsr_merged_audio_inputs *merged_audio_inputs, AVCodecContext *audio_codec_context, int num_channels, double num_audio_frames_shift, AVFilterContext **src_filter_ctx, bool use_amix) {
    const int alloc_result = audio_track_alloc_devices(self, merged_audio_inputs->num_items);
    if(alloc_result != GSR_ERROR_OK)
        return alloc_result;

    for(size_t i = 0; i < merged_audio_inputs->num_items; ++i) {
        const gsr_audio_input *audio_input = &merged_audio_inputs->items[i];
        gsr_audio_device_capture *device = &self->audio_devices[i];
        device->audio_input = *audio_input;
        device->src_filter_ctx = use_amix ? src_filter_ctx[i] : NULL;

        if(audio_input->name[0] == '\0') {
            device->sound_device.handle = NULL;
            device->sound_device.frames = 0;
        } else {
            char description[GSR_AUDIO_INPUT_NAME_MAX_SIZE + 8];
            snprintf(description, sizeof(description), "gsr-%s", audio_input->name);
            if(sound_device_get_by_name(&device->sound_device, description, audio_input->name, description, num_channels, audio_codec_context->frame_size, audio_codec_context_get_audio_format(audio_codec_context)) != 0) {
                // Windows port: a device that cannot be opened (e.g. a broken WASAPI capture stack)
                // must not abort the whole recording. Fall back to a silent track so video-only
                // recordings still work; the capture thread treats a NULL handle as silence.
                gsr_log(GSR_LOG_LEVEL_WARNING, "failed to open \"%s\" audio device, recording without it", audio_input->name);
                device->sound_device.handle = NULL;
                device->sound_device.frames = 0;
            }
        }

        device->frame = create_audio_frame(audio_codec_context);
        if(!device->frame)
            return GSR_ERROR_GENERIC;
        device->frame->pts = -audio_codec_context->frame_size * num_audio_frames_shift;

        ++self->num_audio_devices;
    }

    return GSR_ERROR_OK;
}

#ifdef GSR_APP_AUDIO
int gsr_audio_track_init_application_input(gsr_audio_track *self, const gsr_merged_audio_inputs *merged_audio_inputs, AVCodecContext *audio_codec_context, int num_channels, double num_audio_frames_shift, gsr_pipewire_audio *pipewire_audio) {
    const int alloc_result = audio_track_alloc_devices(self, 1);
    if(alloc_result != GSR_ERROR_OK)
        return alloc_result;

    gsr_audio_device_capture *device = &self->audio_devices[0];
    device->frame = create_audio_frame(audio_codec_context);
    if(!device->frame)
        return GSR_ERROR_GENERIC;
    device->frame->pts = -audio_codec_context->frame_size * num_audio_frames_shift;
    ++self->num_audio_devices;

    char random_str[8];
    if(!generate_random_characters_standard_alphabet(random_str, sizeof(random_str))) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to generate random string");
        return GSR_ERROR_GENERIC;
    }

    char combined_sink_name[64];
    snprintf(combined_sink_name, sizeof(combined_sink_name), "gsr-combined-%.*s.monitor", (int)sizeof(random_str), random_str);

    if(sound_device_get_by_name(&device->sound_device, combined_sink_name, "", "gpu-screen-recorder", num_channels, audio_codec_context->frame_size, audio_codec_context_get_audio_format(audio_codec_context)) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to setup audio recording to combined sink");
        return GSR_ERROR_GENERIC;
    }

    const char **audio_devices_sources = calloc(merged_audio_inputs->num_items, sizeof(const char*));
    const char **app_names = calloc(merged_audio_inputs->num_items, sizeof(const char*));
    if(!audio_devices_sources || !app_names) {
        free(audio_devices_sources);
        free(app_names);
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate application audio names");
        return GSR_ERROR_GENERIC;
    }

    size_t num_audio_devices_sources = 0;
    size_t num_app_names = 0;
    bool app_audio_inverted = false;
    for(size_t i = 0; i < merged_audio_inputs->num_items; ++i) {
        const gsr_audio_input *audio_input = &merged_audio_inputs->items[i];
        if(audio_input->type == GSR_AUDIO_INPUT_TYPE_DEVICE) {
            audio_devices_sources[num_audio_devices_sources] = audio_input->name;
            ++num_audio_devices_sources;
        } else if(audio_input->type == GSR_AUDIO_INPUT_TYPE_APPLICATION) {
            app_names[num_app_names] = audio_input->name;
            ++num_app_names;
            app_audio_inverted = audio_input->inverted;
        }
    }

    int result = GSR_ERROR_OK;
    if(num_audio_devices_sources > 0) {
        if(!gsr_pipewire_audio_add_link_from_sources_to_stream(pipewire_audio, audio_devices_sources, num_audio_devices_sources, combined_sink_name)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to add application audio link");
            result = GSR_ERROR_GENERIC;
        }
    }

    if(result == GSR_ERROR_OK) {
        const bool link_added = app_audio_inverted
            ? gsr_pipewire_audio_add_link_from_apps_to_stream_inverted(pipewire_audio, app_names, num_app_names, combined_sink_name)
            : gsr_pipewire_audio_add_link_from_apps_to_stream(pipewire_audio, app_names, num_app_names, combined_sink_name);
        if(!link_added) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to add application audio link");
            result = GSR_ERROR_GENERIC;
        }
    }

    free(audio_devices_sources);
    free(app_names);
    return result;
}
#endif

void gsr_audio_track_deinit(gsr_audio_track *self) {
    for(size_t i = 0; i < self->num_audio_devices; ++i) {
        gsr_audio_device_capture *device = &self->audio_devices[i];
        sound_device_close(&device->sound_device);
        if(device->frame)
            av_frame_free(&device->frame);
    }

    if(self->audio_devices) {
        free(self->audio_devices);
        self->audio_devices = NULL;
    }
    self->num_audio_devices = 0;

    if(self->graph)
        avfilter_graph_free(&self->graph);

    if(self->codec_context)
        avcodec_free_context(&self->codec_context);
}

