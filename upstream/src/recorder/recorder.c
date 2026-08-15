#include "../../include/recorder/recorder.h"
#include "../../include/recorder/error.h"
#include "../../include/recorder/audio_codec.h"
#include "../../include/recorder/video_codec.h"
#include "../../include/recorder/codec_select.h"
#include "../../include/recorder/muxer.h"
#include "../../include/recorder/replay_save.h"
#include "../../include/recorder/audio_capture.h"
#include "../../include/recorder/recording_clock.h"
#include "../../include/recorder/screenshot.h"
#include "../../include/encoder/encoder.h"
#include "../../include/encoder/video/video.h"
#include "../../include/window/window.h"
#include "../../include/color_conversion.h"
#include "../../include/damage.h"
#include "../../include/cursor.h"
#include "../../include/plugins.h"
#include "../../include/utils.h"
#include "../../include/ffmpeg_utils.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <stdatomic.h>

#include <libavutil/time.h>
#include <libavformat/avformat.h>

#include <X11/Xlib.h>

#define GSR_VIDEO_STREAM_INDEX 0

#define GSR_SET_PAUSED_REQUEST_NONE -1
#define GSR_SET_PAUSED_REQUEST_UNPAUSE 0
#define GSR_SET_PAUSED_REQUEST_PAUSE 1

#define GSR_REPLAY_RECORDING_REQUEST_NONE 0
#define GSR_REPLAY_RECORDING_REQUEST_TOGGLE 1
#define GSR_REPLAY_RECORDING_REQUEST_START 2
#define GSR_REPLAY_RECORDING_REQUEST_STOP 3

struct gsr_recorder {
    gsr_recorder_settings settings;
    gsr_recorder_callbacks callbacks;
    gsr_windowing *windowing;
    gsr_egl *egl;
    gsr_window *window;
    gsr_capture_deps *capture_deps;
    gsr_capture_sources *capture_sources;
    gsr_audio_input_tracks *audio_input_tracks;

    char file_extension[32];
    bool force_no_audio_offset;
    double target_fps;
    bool uses_amix;
    bool hdr;
    bool low_power;
    vec2i video_size;

    AVFormatContext *av_format_context;
    AVStream *video_stream;
    AVCodecContext *video_codec_context;
    AVFrame *video_frame;
    gsr_video_sources video_sources_data;
    gsr_video_sources *video_sources;
    gsr_encoder encoder;
    bool encoder_initialized;
    gsr_video_encoder *video_encoder;
    gsr_color_conversion color_conversion;
    bool color_conversion_initialized;
    gsr_color_conversion *output_color_conversion;
    gsr_plugins plugins;
    gsr_recording_clock *recording_clock;
    gsr_audio_capture audio_capture;
    bool audio_capture_initialized;
    gsr_replay_save replay_save;

    gsr_recording_output replay_recording_output;
    size_t replay_recording_items[GSR_MAX_RECORDING_DESTINATIONS];
    size_t num_replay_recording_items;
    char replay_recording_filepath[PATH_MAX];
    bool replay_recording;

    double fps_start_time;
    int fps_counter;
    int damage_fps_counter;
    bool paused;
    double record_start_time;
    int64_t video_pts_counter;
    int64_t video_prev_pts;
    bool hdr_metadata_set;

    atomic_int running;
    atomic_int toggle_pause;
    atomic_int set_paused_request;
    atomic_int replay_recording_request;
    atomic_int replay_recording_state;
    atomic_int save_replay_seconds;
    atomic_int save_replay_restart_replay;
    bool should_stop_error;
    bool force_iframe_frame;
    int audio_max_frame_size;
    bool use_damage_tracking;
    gsr_damage damage;
    const char **plugin_filepaths;
    int num_plugin_filepaths;
#ifdef GSR_APP_AUDIO
    gsr_pipewire_audio *pipewire_audio;
#endif
};

static void gsr_recorder_stop_recording(gsr_recorder *self);

static int recorder_setup_container(gsr_recorder *self) {
    // The output format is automatically guessed by the file extension
    avformat_alloc_output_context2(&self->av_format_context, NULL, self->settings.container_format, self->settings.filename);
    if (!self->av_format_context) {
        if(self->settings.container_format) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Container format '%s' (argument -c) is not valid", self->settings.container_format);
        } else {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to deduce container format from file extension. Use the '-c' option to specify container format");
            args_parser_print_usage();
            return GSR_ERROR_GENERIC;
        }
        return GSR_ERROR_GENERIC;
    }

    set_format_context_options(self->av_format_context);

    const AVOutputFormat *output_format = self->av_format_context->oformat;

    const char *file_extensions = output_format->extensions ? output_format->extensions : "";
    const char *file_extension_end = strchr(file_extensions, ',');
    if(file_extension_end)
        snprintf(self->file_extension, sizeof(self->file_extension), "%.*s", (int)(file_extension_end - file_extensions), file_extensions);
    else
        snprintf(self->file_extension, sizeof(self->file_extension), "%s", file_extensions);

    if(self->file_extension[0] == '\0')
        snprintf(self->file_extension, sizeof(self->file_extension), "%s", self->settings.container_format ? self->settings.container_format : "");

    self->force_no_audio_offset = self->settings.is_livestream || self->settings.is_output_piped || (strcmp(self->file_extension, "mp4") != 0 && strcmp(self->file_extension, "mkv") != 0 && strcmp(self->file_extension, "webm") != 0);
    self->target_fps = 1.0 / (double)self->settings.fps;

    self->uses_amix = gsr_audio_input_tracks_should_use_amix(self->audio_input_tracks);
    self->settings.audio_codec = select_audio_codec_with_fallback(self->settings.audio_codec, self->file_extension, self->uses_amix);

    return GSR_ERROR_OK;
}

static int recorder_setup_video_sources(gsr_recorder *self) {
    self->video_size = (vec2i){0, 0};
    const int video_sources_result = gsr_video_sources_create(&self->video_sources_data, &self->settings, self->egl, self->capture_deps, false, self->capture_sources, &self->video_size);
    if(video_sources_result != GSR_ERROR_OK) {
        return video_sources_result;
    }

    self->video_sources = &self->video_sources_data;

    // (Some?) livestreaming services require at least one audio track to work.
    // If not audio is provided then create one silent audio track.
    if(self->settings.is_livestream && self->audio_input_tracks->num_items == 0) {
        gsr_log(GSR_LOG_LEVEL_INFO, "live streaming but no audio track was added. Adding a silent audio track");
        gsr_merged_audio_inputs silent_audio_track;
        memset(&silent_audio_track, 0, sizeof(silent_audio_track));
        gsr_audio_input silent_audio_input;
        memset(&silent_audio_input, 0, sizeof(silent_audio_input));
        if(!gsr_merged_audio_inputs_add(&silent_audio_track, &silent_audio_input) || !gsr_audio_input_tracks_add(self->audio_input_tracks, &silent_audio_track)) {
            return GSR_ERROR_GENERIC;
        }
    }

    self->video_stream = NULL;

    return GSR_ERROR_OK;
}

static int recorder_setup_video_encoder(gsr_recorder *self) {
    if(self->settings.video_encoder == GSR_VIDEO_ENCODER_HW_CPU && self->settings.video_codec != (gsr_video_codec)GSR_VIDEO_CODEC_AUTO && self->settings.video_codec != GSR_VIDEO_CODEC_H264) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "-encoder cpu was specified but a codec other than h264 was specified. -encoder cpu supports only h264 at the moment");
        return GSR_ERROR_GENERIC;
    }

    self->low_power = false;
    const AVCodec *video_codec_f = NULL;
    const int select_video_codec_result = select_video_codec_with_fallback(self->video_size, &self->settings, self->file_extension, self->egl, &self->low_power, &video_codec_f);
    if(select_video_codec_result != GSR_ERROR_OK) {
        return select_video_codec_result;
    }

    const enum AVPixelFormat video_pix_fmt = get_pixel_format(self->settings.video_codec, self->egl->gpu_info.vendor, self->settings.video_encoder == GSR_VIDEO_ENCODER_HW_CPU);
    self->video_codec_context = create_video_codec_context(video_pix_fmt, video_codec_f, self->egl, &self->settings, self->video_size.x, self->video_size.y);
    if(!self->settings.is_replaying)
        self->video_stream = create_stream(self->av_format_context, self->video_codec_context);

    self->video_frame = av_frame_alloc();
    if(!self->video_frame) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Failed to allocate video frame");
        return GSR_ERROR_GENERIC;
    }
    self->video_frame->format = self->video_codec_context->pix_fmt;
    self->video_frame->width = self->video_size.x;
    self->video_frame->height = self->video_size.y;
    self->video_frame->color_range = self->video_codec_context->color_range;
    self->video_frame->color_primaries = self->video_codec_context->color_primaries;
    self->video_frame->color_trc = self->video_codec_context->color_trc;
    self->video_frame->colorspace = self->video_codec_context->colorspace;
    self->video_frame->chroma_location = self->video_codec_context->chroma_sample_location;

    const size_t estimated_replay_buffer_packets = calculate_estimated_replay_buffer_packets(self->settings.replay_buffer_size_secs, self->settings.fps, self->settings.audio_codec, self->audio_input_tracks);
    self->recording_clock = gsr_recording_clock_create();
    if(!self->recording_clock) {
        return GSR_ERROR_GENERIC;
    }

    self->encoder_initialized = true;
    if(!gsr_encoder_init(&self->encoder, self->settings.replay_storage, estimated_replay_buffer_packets, self->settings.replay_buffer_size_secs, self->settings.filename)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create encoder");
        return GSR_ERROR_GENERIC;
    }

    self->video_encoder = create_video_encoder(self->egl, &self->settings);
    if(!self->video_encoder) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create video encoder");
        return GSR_ERROR_GENERIC;
    }

    if(!gsr_video_encoder_start(self->video_encoder, self->video_codec_context, self->video_frame)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to start video encoder");
        return GSR_ERROR_GENERIC;
    }

    self->video_size.x = self->video_codec_context->width;
    self->video_size.y = self->video_codec_context->height;
    gsr_video_sources_update_with_real_video_size(self->video_sources, self->video_size);

    memset(&self->plugins, 0, sizeof(self->plugins));

    if(gsr_load_plugins(&self->plugins, self->plugin_filepaths, self->num_plugin_filepaths, &self->settings, self->egl, self->video_size) != GSR_ERROR_OK) {
        return GSR_ERROR_GENERIC;

    }

    gsr_color_conversion_params color_conversion_params;
    memset(&color_conversion_params, 0, sizeof(color_conversion_params));
    color_conversion_params.color_range = self->settings.color_range;
    color_conversion_params.egl = self->egl;
    color_conversion_params.load_external_image_shader = gsr_video_sources_uses_external_image(self->video_sources);
    gsr_video_encoder_get_textures(self->video_encoder, color_conversion_params.destination_textures, color_conversion_params.destination_textures_size, &color_conversion_params.num_destination_textures, &color_conversion_params.destination_color);

    self->color_conversion_initialized = true;
    if(gsr_color_conversion_init(&self->color_conversion, &color_conversion_params) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "main: failed to create color conversion");
        return GSR_ERROR_GENERIC;
    }

    gsr_color_conversion_clear(&self->color_conversion);

    self->output_color_conversion = self->plugins.num_plugins > 0 ? &self->plugins.color_conversion : &self->color_conversion;

    if(self->settings.video_encoder == GSR_VIDEO_ENCODER_HW_CPU) {
        if(!open_video_software(self->video_codec_context, &self->settings)) {
            return GSR_ERROR_GENERIC;
        }
    } else {
        if(!open_video_hardware(self->video_codec_context, self->low_power, self->egl, &self->settings)) {
            return GSR_ERROR_GENERIC;
        }
    }

    if(self->video_stream) {
        avcodec_parameters_from_context(self->video_stream->codecpar, self->video_codec_context);
        const size_t video_destination_id = gsr_encoder_add_recording_destination(&self->encoder, self->video_codec_context, self->av_format_context, self->video_stream, 0);
        if(self->settings.write_first_frame_ts && video_destination_id != (size_t)-1) {
            char ts_filepath[PATH_MAX + 4];
            snprintf(ts_filepath, sizeof(ts_filepath), "%s.ts", self->settings.filename);
            gsr_encoder_set_recording_destination_first_frame_ts_filepath(&self->encoder, video_destination_id, ts_filepath);
        }
    }

    return GSR_ERROR_OK;
}

static int recorder_setup_audio_track(gsr_recorder *self, const gsr_merged_audio_inputs *merged_audio_inputs, int audio_stream_index, gsr_audio_track *audio_track) {
    const bool use_amix = gsr_audio_inputs_should_use_amix(merged_audio_inputs);

    memset(audio_track, 0, sizeof(*audio_track));
    audio_track->stream_index = audio_stream_index;
    audio_track->codec_context = create_audio_codec_context(self->settings.fps, self->settings.audio_codec, use_amix, self->settings.audio_bitrate);
    if(!audio_track->codec_context)
        return GSR_ERROR_GENERIC;

    AVStream *audio_stream = NULL;
    if(!self->settings.is_replaying) {
        audio_stream = create_stream(self->av_format_context, audio_track->codec_context);
        if(!audio_stream)
            return GSR_ERROR_GENERIC;

        if(gsr_encoder_add_recording_destination(&self->encoder, audio_track->codec_context, self->av_format_context, audio_stream, 0) == (size_t)-1)
            gsr_log(GSR_LOG_LEVEL_ERROR, "added too many audio sources");
    }

    snprintf(audio_track->name, sizeof(audio_track->name), "%s", merged_audio_inputs->track_name);
    if(audio_stream && audio_track->name[0] != '\0' && !self->settings.exclude_metadata)
        av_dict_set(&audio_stream->metadata, "title", audio_track->name, 0);

    if(!open_audio(audio_track->codec_context, self->settings.ffmpeg_audio_opts))
        return GSR_ERROR_GENERIC;

    if(audio_stream)
        avcodec_parameters_from_context(audio_stream->codecpar, audio_track->codec_context);

    #if LIBAVCODEC_VERSION_MAJOR < 60
    const int num_channels = audio_track->codec_context->channels;
    #else
    const int num_channels = audio_track->codec_context->ch_layout.nb_channels;
    #endif

    AVFilterContext *src_filter_ctx[GSR_MAX_AUDIO_SOURCES_PER_TRACK];
    if(use_amix) {
        if(merged_audio_inputs->num_items > GSR_MAX_AUDIO_SOURCES_PER_TRACK) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "too many audio sources for one audio track, the maximum is %d", GSR_MAX_AUDIO_SOURCES_PER_TRACK);
            return GSR_ERROR_GENERIC;
        }

        if(gsr_audio_init_filter_graph(audio_track->codec_context, &audio_track->graph, &audio_track->sink, src_filter_ctx, merged_audio_inputs->num_items) < 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create audio filter");
            return GSR_ERROR_GENERIC;
        }
    }

    const double audio_fps = (double)audio_track->codec_context->sample_rate / (double)audio_track->codec_context->frame_size;
    const double timeout_sec = 1000.0 / audio_fps / 1000.0;

    const double audio_startup_time_seconds = self->force_no_audio_offset ? 0 : audio_codec_get_desired_delay(self->settings.audio_codec, self->settings.fps);
    const double num_audio_frames_shift = audio_startup_time_seconds / timeout_sec;
    audio_track->pts = -audio_track->codec_context->frame_size * num_audio_frames_shift;

    if(gsr_audio_inputs_has_app_audio(merged_audio_inputs)) {
        assert(!use_amix);
#ifdef GSR_APP_AUDIO
        return gsr_audio_track_init_application_input(audio_track, merged_audio_inputs, audio_track->codec_context, num_channels, num_audio_frames_shift, self->pipewire_audio);
#else
        return GSR_ERROR_UNSUPPORTED;
#endif
    }

    return gsr_audio_track_init_device_inputs(audio_track, merged_audio_inputs, audio_track->codec_context, num_channels, num_audio_frames_shift, src_filter_ctx, use_amix);
}

static int recorder_setup_audio(gsr_recorder *self) {
    if(gsr_audio_capture_init(&self->audio_capture, &self->encoder, self->recording_clock, &self->running) != GSR_ERROR_OK)
        return GSR_ERROR_GENERIC;

    int audio_stream_index = GSR_VIDEO_STREAM_INDEX + 1;
    for(size_t i = 0; i < self->audio_input_tracks->num_items; ++i) {
        gsr_audio_track audio_track;
        const int audio_track_result = recorder_setup_audio_track(self, &self->audio_input_tracks->items[i], audio_stream_index, &audio_track);
        if(audio_track_result != GSR_ERROR_OK) {
            gsr_audio_track_deinit(&audio_track);
            return audio_track_result;
        }

        if(!gsr_audio_capture_add_track(&self->audio_capture, &audio_track)) {
            gsr_audio_track_deinit(&audio_track);
            return GSR_ERROR_GENERIC;
        }

        ++audio_stream_index;

        if(audio_track.codec_context->frame_size > self->audio_max_frame_size)
            self->audio_max_frame_size = audio_track.codec_context->frame_size;
    }

    return GSR_ERROR_OK;
}

static int recorder_open_output(gsr_recorder *self) {
    if(!self->settings.is_replaying && !(self->av_format_context->oformat->flags & AVFMT_NOFILE)) {
        const int ret = avio_open(&self->av_format_context->pb, self->settings.filename, AVIO_FLAG_WRITE);
        if(ret < 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Could not open '%s': %s", self->settings.filename, gsr_av_error_to_string(ret));
            return GSR_ERROR_GENERIC;
        }
    }

    if(!self->settings.is_replaying)
        av_write_header(self->av_format_context, self->settings.ffmpeg_opts);

    return GSR_ERROR_OK;
}

gsr_recorder* gsr_recorder_create(const gsr_recorder_params *params, const gsr_recorder_callbacks *callbacks, int *error) {
    *error = GSR_ERROR_GENERIC;

    gsr_recorder *self = calloc(1, sizeof(gsr_recorder));
    if(!self) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_recorder_create: failed to allocate recorder");
        return NULL;
    }

    self->settings = *params->settings;
    if(callbacks)
        self->callbacks = *callbacks;
    self->windowing = params->windowing;
    self->egl = &params->windowing->egl;
    self->window = params->windowing->window;
    self->capture_deps = params->capture_deps;
    self->capture_sources = params->capture_sources;
    self->audio_input_tracks = params->audio_input_tracks;
    atomic_init(&self->running, 1);
    atomic_init(&self->toggle_pause, 0);
    atomic_init(&self->set_paused_request, GSR_SET_PAUSED_REQUEST_NONE);
    atomic_init(&self->replay_recording_request, GSR_REPLAY_RECORDING_REQUEST_NONE);
    atomic_init(&self->replay_recording_state, 0);
    atomic_init(&self->save_replay_seconds, 0);
    atomic_init(&self->save_replay_restart_replay, GSR_RESTART_REPLAY_USE_OPTION);
    self->audio_max_frame_size = 1024;
    int error_code = GSR_ERROR_GENERIC;
    self->hdr = video_codec_is_hdr(params->settings->video_codec);
    self->plugin_filepaths = params->plugin_filepaths;
    self->num_plugin_filepaths = params->num_plugin_filepaths;
#ifdef GSR_APP_AUDIO
    self->pipewire_audio = params->pipewire_audio;
#endif

    const struct {
        int (*setup)(gsr_recorder *self);
    } setup_phases[] = {
        { recorder_setup_container },
        { recorder_setup_video_sources },
        { recorder_setup_video_encoder },
        { recorder_setup_audio },
        { recorder_open_output },
    };

    for(size_t i = 0; i < sizeof(setup_phases)/sizeof(setup_phases[0]); ++i) {
        error_code = setup_phases[i].setup(self);
        if(error_code != GSR_ERROR_OK)
            goto fail;
    }

    *error = GSR_ERROR_OK;
    return self;

    fail:
    *error = error_code;
    gsr_recorder_destroy(self, false);
    return NULL;
}

static void recorder_process_events(gsr_recorder *self) {
    while(gsr_window_process_event(self->window)) {
        if(self->capture_deps->x11_cursor_display && self->settings.record_cursor)
            gsr_cursor_on_event(&self->capture_deps->x11_cursor, gsr_window_get_event_data(self->window));

        gsr_damage_on_event(&self->damage, gsr_window_get_event_data(self->window));
        for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &self->video_sources->items[video_source_index];
            gsr_capture_on_event(video_source->capture, self->egl);
        }
    }

    if(self->capture_deps->x11_cursor_display && self->settings.record_cursor)
        gsr_cursor_tick(&self->capture_deps->x11_cursor, DefaultRootWindow(self->capture_deps->x11_cursor_display));
}

static bool recorder_tick_video_sources(gsr_recorder *self) {
    gsr_damage_tick(&self->damage);

    self->should_stop_error = false;
    bool damaged = false;

    if(self->use_damage_tracking)
        damaged = gsr_damage_is_damaged(&self->damage);

    for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
        gsr_video_source *video_source = &self->video_sources->items[video_source_index];
        gsr_capture_tick(video_source->capture);

        if(gsr_capture_should_stop(video_source->capture, &self->should_stop_error)) {
            atomic_store(&self->running, 0);
            break;
        }

        if(video_source->capture_source->type == GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW) {
            assert(video_source->capture->get_window_id);
            const Window damage_target_window = video_source->capture->get_window_id(video_source->capture);

            if((int64_t)damage_target_window != video_source->capture_source->window_id) {
                gsr_damage_stop_tracking_window(&self->damage, video_source->capture_source->window_id);
                if(damage_target_window != 0)
                    gsr_damage_start_tracking_window(&self->damage, damage_target_window);
            }

            video_source->capture_source->window_id = damage_target_window;
        }

        if(video_source->capture->is_damaged)
            damaged |= video_source->capture->is_damaged(video_source->capture);
        else if(!self->use_damage_tracking)
            damaged = true;
    }

    damaged |= gsr_plugins_is_damaged(&self->plugins);

    // TODO: Readd wayland sync warning when removing this
    if(self->settings.framerate_mode != GSR_FRAMERATE_MODE_CONTENT)
        damaged = true;

    if(damaged)
        ++self->damage_fps_counter;

    return damaged;
}

static void recorder_update_fps_counters(gsr_recorder *self) {
    ++self->fps_counter;
    const double time_now = clock_get_monotonic_seconds();
    //const double frame_timer_elapsed = time_now - frame_timer_start;
    const double elapsed = time_now - self->fps_start_time;
    if (elapsed >= 1.0) {
        if(self->settings.verbose) {
            gsr_log(GSR_LOG_LEVEL_INFO, "update fps: %d, damage fps: %d", self->fps_counter, self->damage_fps_counter);
        }
        self->fps_start_time = time_now;
        self->fps_counter = 0;
        self->damage_fps_counter = 0;
    }
}

static void recorder_capture_and_encode_frame(gsr_recorder *self, bool damaged) {
    const double this_video_frame_time = gsr_recording_clock_get_time(self->recording_clock);
    const int64_t expected_frames = floor((this_video_frame_time - self->record_start_time) / self->target_fps);
    const int64_t num_missed_frames = expected_frames - self->video_pts_counter;

    if(damaged && num_missed_frames >= 1 && !self->paused) {
        // TODO: Dont do this if no damage?
        self->egl->glClear(0);

        gsr_damage_clear(&self->damage);
        gsr_plugins_clear_damage(&self->plugins);
        gsr_capture_deps_cleanup_kms_fds(self->capture_deps);

        gsr_capture_deps_update_kms(self->capture_deps);

        bool capture_has_synchronous_task = false;
        for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &self->video_sources->items[video_source_index];
            if(video_source->capture->clear_damage)
                video_source->capture->clear_damage(video_source->capture);

            if(video_source->capture->capture_has_synchronous_task) {
                capture_has_synchronous_task = video_source->capture->capture_has_synchronous_task(video_source->capture);
                if(capture_has_synchronous_task) {
                    self->paused = true;
                    gsr_recording_clock_set_paused(self->recording_clock, true);
                }
            }
        }

        for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &self->video_sources->items[video_source_index];
            if(video_source->capture->pre_capture)
                video_source->capture->pre_capture(video_source->capture, &video_source->metadata, self->output_color_conversion);
        }

        if(self->output_color_conversion->schedule_clear) {
            self->output_color_conversion->schedule_clear = false;
            gsr_color_conversion_clear(self->output_color_conversion);
        }

        for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &self->video_sources->items[video_source_index];
            gsr_capture_capture(video_source->capture, &video_source->metadata, self->output_color_conversion);
        }

        gsr_capture_deps_cleanup_kms_fds(self->capture_deps);

        if(self->plugins.num_plugins > 0) {
            gsr_plugins_draw(&self->plugins);
            gsr_color_conversion_draw(&self->color_conversion, self->plugins.texture,
            (vec2i){0, 0}, self->video_size,
            (vec2i){0, 0}, self->video_size,
            self->video_size, GSR_ROT_0, GSR_FLIP_NONE, GSR_SOURCE_COLOR_RGB, false);
        }

        if(capture_has_synchronous_task) {
            self->paused = false;
            gsr_recording_clock_set_paused(self->recording_clock, false);
        }

        gsr_egl_swap_buffers(self->egl);
        gsr_video_encoder_copy_textures_to_frame(self->video_encoder, self->video_frame, self->output_color_conversion);

        for(size_t video_source_index = 0; video_source_index < self->video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &self->video_sources->items[video_source_index];
            if(self->hdr && !self->hdr_metadata_set && !self->settings.is_replaying && add_hdr_metadata_to_video_stream(video_source->capture, self->video_stream))
                self->hdr_metadata_set = true;
        }

        // TODO: Check if duplicate frame can be saved just by writing it with a different pts instead of sending it again
        const int num_frames_to_encode = self->settings.framerate_mode == GSR_FRAMERATE_MODE_CONSTANT ? num_missed_frames : 1;
        for(int i = 0; i < num_frames_to_encode; ++i) {
            if(self->settings.framerate_mode == GSR_FRAMERATE_MODE_CONSTANT) {
                self->video_frame->pts = self->video_pts_counter + i;
            } else {
                self->video_frame->pts = (this_video_frame_time - self->record_start_time) * (double)AV_TIME_BASE;
                const bool same_pts = self->video_frame->pts == self->video_prev_pts;
                self->video_prev_pts = self->video_frame->pts;
                if(same_pts)
                    continue;
            }

            if(self->force_iframe_frame) {
                self->video_frame->pict_type = AV_PICTURE_TYPE_I;
            }

            int ret = avcodec_send_frame(self->video_codec_context, self->video_frame);
            if(ret == 0) {
                // TODO: Move to separate thread because this could write to network (for example when livestreaming)
                gsr_encoder_receive_packets(&self->encoder, self->video_codec_context, self->video_frame->pts, GSR_VIDEO_STREAM_INDEX);
            } else {
                gsr_log(GSR_LOG_LEVEL_ERROR, "avcodec_send_frame failed, error: %s", gsr_av_error_to_string(ret));
            }

            if(self->force_iframe_frame) {
                self->force_iframe_frame = false;
                self->video_frame->pict_type = AV_PICTURE_TYPE_NONE;
            }
        }

        self->video_pts_counter += num_missed_frames;
    }
}

static void recorder_apply_pause_toggle(gsr_recorder *self) {
    const bool toggle_pause = atomic_exchange(&self->toggle_pause, 0) == 1;
    const int set_paused_request = atomic_exchange(&self->set_paused_request, GSR_SET_PAUSED_REQUEST_NONE);
    if(self->settings.is_replaying)
        return;

    bool new_paused = self->paused;
    if(toggle_pause)
        new_paused = !new_paused;
    if(set_paused_request != GSR_SET_PAUSED_REQUEST_NONE)
        new_paused = set_paused_request == GSR_SET_PAUSED_REQUEST_PAUSE;

    if(new_paused != self->paused) {
        self->paused = new_paused;
        gsr_recording_clock_set_paused(self->recording_clock, self->paused);
        gsr_log(GSR_LOG_LEVEL_INFO, self->paused ? "Paused" : "Unpaused");
    }
}

static void recorder_apply_replay_recording_toggle(gsr_recorder *self) {
    const int request = atomic_exchange(&self->replay_recording_request, GSR_REPLAY_RECORDING_REQUEST_NONE);
    if(request == GSR_REPLAY_RECORDING_REQUEST_NONE)
        return;

    if(!self->settings.replay_recording_directory) {
        if(request != GSR_REPLAY_RECORDING_REQUEST_STOP && self->callbacks.recording_started)
            self->callbacks.recording_started(NULL, self->callbacks.userdata);
        return;
    }

    bool new_replay_recording_state = !self->replay_recording;
    if(request == GSR_REPLAY_RECORDING_REQUEST_START)
        new_replay_recording_state = true;
    else if(request == GSR_REPLAY_RECORDING_REQUEST_STOP)
        new_replay_recording_state = false;

    if(new_replay_recording_state == self->replay_recording)
        return;

    if(new_replay_recording_state) {
        gsr_audio_capture_lock_filter(&self->audio_capture);
        self->num_replay_recording_items = 0;
        const bool filepath_created = gsr_create_new_recording_filepath_from_timestamp(self->replay_recording_filepath, sizeof(self->replay_recording_filepath), self->settings.replay_recording_directory, "Video", self->file_extension, self->settings.date_folders);
        if(filepath_created && gsr_recording_output_start(&self->replay_recording_output, self->replay_recording_filepath, &self->settings, self->video_codec_context, &self->audio_capture, self->hdr, self->video_sources)) {
            const size_t video_recording_destination_id = gsr_encoder_add_recording_destination(&self->encoder, self->video_codec_context, self->replay_recording_output.av_format_context, self->replay_recording_output.video_stream, self->video_frame->pts);
            if(self->settings.write_first_frame_ts && video_recording_destination_id != (size_t)-1) {
                char ts_filepath[PATH_MAX + 4];
                snprintf(ts_filepath, sizeof(ts_filepath), "%s.ts", self->replay_recording_filepath);
                gsr_encoder_set_recording_destination_first_frame_ts_filepath(&self->encoder, video_recording_destination_id, ts_filepath);
            }

            if(video_recording_destination_id != (size_t)-1 && self->num_replay_recording_items < GSR_MAX_RECORDING_DESTINATIONS) {
                self->replay_recording_items[self->num_replay_recording_items] = video_recording_destination_id;
                ++self->num_replay_recording_items;
            }

            for(size_t i = 0; i < self->replay_recording_output.num_audio_streams; ++i) {
                const gsr_recording_audio_stream *audio_stream = &self->replay_recording_output.audio_streams[i];
                const size_t audio_recording_destination_id = gsr_encoder_add_recording_destination(&self->encoder, audio_stream->audio_track->codec_context, self->replay_recording_output.av_format_context, audio_stream->stream, audio_stream->audio_track->pts);
                if(audio_recording_destination_id != (size_t)-1 && self->num_replay_recording_items < GSR_MAX_RECORDING_DESTINATIONS) {
                    self->replay_recording_items[self->num_replay_recording_items] = audio_recording_destination_id;
                    ++self->num_replay_recording_items;
                }
            }

            self->replay_recording = true;
            atomic_store(&self->replay_recording_state, 1);
            self->force_iframe_frame = true;
            gsr_log(GSR_LOG_LEVEL_INFO, "Started recording");
            if(self->callbacks.recording_started)
                self->callbacks.recording_started(self->replay_recording_filepath, self->callbacks.userdata);
        } else {
            if(self->callbacks.recording_started)
                self->callbacks.recording_started(NULL, self->callbacks.userdata);
        }
        gsr_audio_capture_unlock_filter(&self->audio_capture);
    } else if(self->replay_recording_output.av_format_context) {
        for(size_t i = 0; i < self->num_replay_recording_items; ++i) {
            gsr_encoder_remove_recording_destination(&self->encoder, self->replay_recording_items[i]);
        }
        self->num_replay_recording_items = 0;

        if(gsr_recording_output_stop(&self->replay_recording_output)) {
            gsr_log(GSR_LOG_LEVEL_INFO, "Stopped recording");
            if(self->callbacks.recording_stopped)
                self->callbacks.recording_stopped(self->replay_recording_filepath, self->callbacks.userdata);
        } else {
            if(self->callbacks.recording_stopped)
                self->callbacks.recording_stopped(NULL, self->callbacks.userdata);
        }

        self->replay_recording = false;
        atomic_store(&self->replay_recording_state, 0);
        self->replay_recording_filepath[0] = '\0';
    }
}

static void recorder_poll_replay_save(gsr_recorder *self) {
    bool replay_save_result = false;
    const char *replay_save_output_filepath = NULL;
    if(gsr_replay_save_poll(&self->replay_save, &replay_save_result, &replay_save_output_filepath)) {
        if(self->callbacks.replay_saved)
            self->callbacks.replay_saved(replay_save_output_filepath[0] == '\0' || !replay_save_result ? NULL : replay_save_output_filepath, self->callbacks.userdata);
    }

    if(atomic_load(&self->save_replay_seconds) != 0 && !gsr_replay_save_is_running(&self->replay_save) && self->settings.is_replaying) {
        int current_save_replay_seconds = atomic_load(&self->save_replay_seconds);
        if(current_save_replay_seconds > 0)
            current_save_replay_seconds += self->settings.keyint;

        atomic_store(&self->save_replay_seconds, 0);
        const int restart_replay_request = atomic_exchange(&self->save_replay_restart_replay, GSR_RESTART_REPLAY_USE_OPTION);
        const bool restart_replay = restart_replay_request == GSR_RESTART_REPLAY_USE_OPTION ? self->settings.restart_replay_on_save : restart_replay_request == GSR_RESTART_REPLAY_ENABLE;
        const bool replay_start_result = gsr_replay_save_start(&self->replay_save, self->video_codec_context, GSR_VIDEO_STREAM_INDEX, &self->audio_capture, &self->encoder, &self->settings, self->file_extension, self->hdr, self->video_sources, current_save_replay_seconds);
        if(!replay_start_result && self->callbacks.replay_saved)
            self->callbacks.replay_saved(NULL, self->callbacks.userdata);

        if(restart_replay && current_save_replay_seconds == GSR_SAVE_REPLAY_SECONDS_FULL) {
            pthread_mutex_lock(&self->encoder.replay_mutex);
            gsr_replay_buffer_clear(self->encoder.replay_buffer);
            pthread_mutex_unlock(&self->encoder.replay_mutex);
        }
    }
}

static void recorder_sleep_until_next_frame(gsr_recorder *self) {
    const double time_at_frame_end = gsr_recording_clock_get_time(self->recording_clock);
    const double time_elapsed_total = time_at_frame_end - self->record_start_time;
    const int64_t frames_elapsed = floor(time_elapsed_total / self->target_fps);
    const double time_at_next_frame = (frames_elapsed + 1) * self->target_fps;
    double time_to_next_frame = time_at_next_frame - time_elapsed_total;
    if(time_to_next_frame > self->target_fps)
        time_to_next_frame = self->target_fps;
    const int64_t end_num_missed_frames = frames_elapsed - self->video_pts_counter;

    if(time_to_next_frame > 0.0 && end_num_missed_frames <= 0)
        av_usleep(time_to_next_frame * 1000.0 * 1000.0);
    else {
        if(self->paused)
            av_usleep(20.0 * 1000.0); // 20 milliseconds
        else if(self->settings.framerate_mode == GSR_FRAMERATE_MODE_CONTENT)
            av_usleep(2.8 * 1000.0); // 2.8 milliseconds
    }
}

int gsr_recorder_run(gsr_recorder *self) {
    self->fps_start_time = clock_get_monotonic_seconds();
    //double frame_timer_start = self->fps_start_time;
    self->fps_counter = 0;
    self->damage_fps_counter = 0;

    self->paused = false;
    self->replay_recording = false;

    memset(&self->replay_recording_output, 0, sizeof(self->replay_recording_output));

    gsr_replay_save_init(&self->replay_save);

    self->force_iframe_frame = false;

    gsr_recording_clock_start(self->recording_clock);
    self->record_start_time = gsr_recording_clock_get_start_time(self->recording_clock);

    if(gsr_audio_capture_start(&self->audio_capture, self->audio_max_frame_size, self->uses_amix) != GSR_ERROR_OK) {
        /* The audio threads that did start have to stop before they can be joined */
        atomic_store(&self->running, 0);
        return GSR_ERROR_GENERIC;
    }

    // Set update_fps to 24 to test if duplicate/delayed frames cause video/audio desync or too fast/slow video.
    //const double update_fps = fps + 190;
    self->should_stop_error = false;

    self->video_pts_counter = 0;
    self->video_prev_pts = 0;

    self->hdr_metadata_set = false;
    self->hdr = video_codec_is_hdr(self->settings.video_codec);

    memset(&self->damage, 0, sizeof(self->damage));
    if(self->settings.framerate_mode == GSR_FRAMERATE_MODE_CONTENT && gsr_capture_sources_has_damage_tracked_target(self->capture_sources)) {
        if(gsr_window_get_display_server(self->window) == GSR_DISPLAY_SERVER_X11) {
            gsr_damage_init(&self->damage, self->egl, &self->capture_deps->x11_cursor, self->settings.record_cursor);
            self->use_damage_tracking = true;

            for(size_t i = 0; i < self->capture_sources->num_items; ++i) {
                const gsr_capture_source *capture_source = &self->capture_sources->items[i];
                switch(capture_source->type) {
                    case GSR_CAPTURE_SOURCE_TYPE_WINDOW:
                        gsr_damage_start_tracking_window(&self->damage, capture_source->window_id);
                        break;
                    case GSR_CAPTURE_SOURCE_TYPE_MONITOR:
                    case GSR_CAPTURE_SOURCE_TYPE_REGION:
                        // TODO: When capturing a region only track damage in that region
                        gsr_damage_start_tracking_monitor(&self->damage, capture_source->name);
                        break;
                    default:
                        break;
                }
            }
        } else if(gsr_capture_sources_has_monitor_or_region(self->capture_sources)) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "\"-fm content\" has no effect on Wayland when recording a monitor. Either record a monitor on X11 or capture with desktop portal instead (-w portal)");
        }
    }

    while(atomic_load(&self->running)) {
        recorder_process_events(self);
        const bool damaged = recorder_tick_video_sources(self);
        recorder_update_fps_counters(self);
        recorder_capture_and_encode_frame(self, damaged);
        recorder_apply_pause_toggle(self);
        recorder_apply_replay_recording_toggle(self);
        recorder_poll_replay_save(self);
        recorder_sleep_until_next_frame(self);
    }

    gsr_recorder_stop_recording(self);
    return self->should_stop_error ? GSR_ERROR_CAPTURE_FAILED : GSR_ERROR_OK;
}

static void gsr_recorder_stop_recording(gsr_recorder *self) {
    atomic_store(&self->running, 0);

    bool final_replay_save_result = false;
    const char *final_replay_save_output_filepath = NULL;
    if(gsr_replay_save_join(&self->replay_save, &final_replay_save_result, &final_replay_save_output_filepath)) {
        if(final_replay_save_output_filepath[0] != '\0' && self->callbacks.replay_saved)
            self->callbacks.replay_saved(final_replay_save_output_filepath, self->callbacks.userdata);
    }

    gsr_plugins_deinit(&self->plugins);

    if(self->replay_recording_output.av_format_context) {
        for(size_t i = 0; i < self->num_replay_recording_items; ++i) {
            gsr_encoder_remove_recording_destination(&self->encoder, self->replay_recording_items[i]);
        }
        self->num_replay_recording_items = 0;

        if(gsr_recording_output_stop(&self->replay_recording_output)) {
            gsr_log(GSR_LOG_LEVEL_INFO, "Stopped recording");
            if(self->callbacks.recording_stopped)
                self->callbacks.recording_stopped(self->replay_recording_filepath, self->callbacks.userdata);
        } else {
            if(self->callbacks.recording_stopped)
                self->callbacks.recording_stopped(NULL, self->callbacks.userdata);
        }
    }

    gsr_audio_capture_join_threads(&self->audio_capture);

    // TODO: Replace this with start_recording_create_steams
    if(!self->settings.is_replaying && gsr_av_format_context_write_trailer(self->av_format_context) != 0) {
        //fprintf(stderr, "Failed to write trailer\n");
    }

    if(!self->settings.is_replaying && self->callbacks.recording_stopped)
        self->callbacks.recording_stopped(self->settings.filename, self->callbacks.userdata);
}

void gsr_recorder_destroy(gsr_recorder *self, bool exiting) {
    if(!self)
        return;

    gsr_audio_capture_deinit(&self->audio_capture);
    gsr_plugins_deinit(&self->plugins);

    if(self->use_damage_tracking)
        gsr_damage_deinit(&self->damage);

    if(self->color_conversion_initialized)
        gsr_color_conversion_deinit(&self->color_conversion);

    if(self->video_frame)
        av_frame_free(&self->video_frame);

    if(self->video_codec_context)
        avcodec_free_context(&self->video_codec_context);

    if(self->video_encoder)
        gsr_video_encoder_destroy(self->video_encoder, NULL);

    if(self->encoder_initialized)
        gsr_encoder_deinit(&self->encoder, exiting);

    gsr_video_sources_deinit(&self->video_sources_data);

    if(self->av_format_context) {
        if(self->av_format_context->pb && !(self->av_format_context->oformat->flags & AVFMT_NOFILE))
            avio_close(self->av_format_context->pb);
        avformat_free_context(self->av_format_context);
    }

    gsr_recording_clock_destroy(self->recording_clock);
    free(self);
}

void gsr_recorder_stop(gsr_recorder *self) {
    atomic_store(&self->running, 0);
}

void gsr_recorder_toggle_pause(gsr_recorder *self) {
    atomic_store(&self->toggle_pause, 1);
}

void gsr_recorder_set_paused(gsr_recorder *self, bool paused) {
    atomic_store(&self->set_paused_request, paused ? GSR_SET_PAUSED_REQUEST_PAUSE : GSR_SET_PAUSED_REQUEST_UNPAUSE);
}

void gsr_recorder_toggle_replay_recording(gsr_recorder *self) {
    atomic_store(&self->replay_recording_request, GSR_REPLAY_RECORDING_REQUEST_TOGGLE);
}

void gsr_recorder_start_replay_recording(gsr_recorder *self) {
    atomic_store(&self->replay_recording_request, GSR_REPLAY_RECORDING_REQUEST_START);
}

void gsr_recorder_stop_replay_recording(gsr_recorder *self) {
    atomic_store(&self->replay_recording_request, GSR_REPLAY_RECORDING_REQUEST_STOP);
}

bool gsr_recorder_is_replay_recording(const gsr_recorder *self) {
    return atomic_load(&self->replay_recording_state) == 1;
}

void gsr_recorder_save_replay(gsr_recorder *self, int seconds, int restart_replay) {
    atomic_store(&self->save_replay_restart_replay, restart_replay);
    atomic_store(&self->save_replay_seconds, seconds);
}
