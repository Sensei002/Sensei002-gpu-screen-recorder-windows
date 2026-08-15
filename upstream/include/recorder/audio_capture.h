#ifndef GSR_RECORDER_AUDIO_CAPTURE_H
#define GSR_RECORDER_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include "../sound.h"
#include "recording_clock.h"
#include "../encoder/encoder.h"
#include "audio_input.h"

#ifdef GSR_APP_AUDIO
#include "../pipewire_audio.h"
#endif

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>

#define GSR_MAX_AUDIO_SOURCES_PER_TRACK 32

typedef struct gsr_audio_capture gsr_audio_capture;
typedef struct gsr_audio_track gsr_audio_track;
typedef struct gsr_audio_device_capture gsr_audio_device_capture;

typedef struct {
    gsr_audio_capture *audio_capture;
    gsr_audio_track *track;
    gsr_audio_device_capture *device;
} gsr_audio_device_thread_userdata;

struct gsr_audio_device_capture {
    SoundDevice sound_device;
    gsr_audio_input audio_input;
    AVFilterContext *src_filter_ctx;
    AVFrame *frame;
    pthread_t thread;
    bool thread_created;
    gsr_audio_device_thread_userdata thread_userdata;
};

/* TODO: Instead of having a thread for each audio device, have one thread for all of them and read the data with non-blocking read */
struct gsr_audio_track {
    char name[GSR_AUDIO_TRACK_NAME_MAX_SIZE];
    AVCodecContext *codec_context;
    gsr_audio_device_capture *audio_devices;
    size_t num_audio_devices;
    AVFilterGraph *graph;
    AVFilterContext *sink;
    int stream_index;
    int64_t pts;
};

struct gsr_audio_capture {
    gsr_audio_track *tracks;
    size_t num_tracks;
    size_t capacity_tracks;

    pthread_mutex_t filter_mutex;
    bool filter_mutex_initialized;
    pthread_t amix_thread;
    bool amix_thread_created;
    uint8_t *empty_audio;

    gsr_encoder *encoder;
    gsr_recording_clock *clock;
    const atomic_int *running;
};

/* Returns a |gsr_error| value */
int gsr_audio_capture_init(gsr_audio_capture *self, gsr_encoder *encoder, gsr_recording_clock *clock, const atomic_int *running);
void gsr_audio_capture_deinit(gsr_audio_capture *self);

bool gsr_audio_capture_add_track(gsr_audio_capture *self, const gsr_audio_track *track);
/* Allocates the silence buffer and starts one thread per audio device, plus the amix thread when |uses_amix| is set */
int gsr_audio_capture_start(gsr_audio_capture *self, int audio_max_frame_size, bool uses_amix);
void gsr_audio_capture_join_threads(gsr_audio_capture *self);
void gsr_audio_capture_lock_filter(gsr_audio_capture *self);
void gsr_audio_capture_unlock_filter(gsr_audio_capture *self);

/* Returns 0 on success, or a negative value on failure. |src_filter_ctx| must have room for |num_sources| items */
int gsr_audio_init_filter_graph(AVCodecContext *audio_codec_context, AVFilterGraph **graph, AVFilterContext **sink, AVFilterContext **src_filter_ctx, size_t num_sources);

/* Returns a |gsr_error| value. Opens the sound devices of one audio track */
int gsr_audio_track_init_device_inputs(gsr_audio_track *self, const gsr_merged_audio_inputs *merged_audio_inputs, AVCodecContext *audio_codec_context, int num_channels, double num_audio_frames_shift, AVFilterContext **src_filter_ctx, bool use_amix);
#ifdef GSR_APP_AUDIO
/* Returns a |gsr_error| value. Creates a combined sink that application audio is routed to */
int gsr_audio_track_init_application_input(gsr_audio_track *self, const gsr_merged_audio_inputs *merged_audio_inputs, AVCodecContext *audio_codec_context, int num_channels, double num_audio_frames_shift, gsr_pipewire_audio *pipewire_audio);
#endif
void gsr_audio_track_deinit(gsr_audio_track *self);

#endif /* GSR_RECORDER_AUDIO_CAPTURE_H */
