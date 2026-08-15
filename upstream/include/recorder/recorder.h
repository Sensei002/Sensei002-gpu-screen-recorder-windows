#ifndef GSR_RECORDER_RECORDER_H
#define GSR_RECORDER_RECORDER_H

#include <stdbool.h>
#include "settings.h"
#include "capture_source.h"
#include "capture_setup.h"
#include "audio_input.h"
#include "windowing.h"
#ifdef GSR_APP_AUDIO
#include "../pipewire_audio.h"
#endif

/* Records video and audio to a file, or to a replay buffer that can be saved to a file at any time */
typedef struct gsr_recorder gsr_recorder;

typedef struct {
    /* |filepath| is NULL when the recording failed to save */
    void (*replay_saved)(const char *filepath, void *userdata);
    void (*recording_started)(const char *filepath, void *userdata);
    /* |filepath| is NULL when the recording failed to save */
    void (*recording_stopped)(const char *filepath, void *userdata);
    void (*paused_changed)(bool paused, void *userdata);
    void *userdata;
} gsr_recorder_callbacks;

typedef struct {
    const gsr_recorder_settings *settings;
    gsr_windowing *windowing;
    gsr_capture_deps *capture_deps;
    gsr_capture_sources *capture_sources;
    gsr_audio_input_tracks *audio_input_tracks;
    const char **plugin_filepaths;
    int num_plugin_filepaths;
#ifdef GSR_APP_AUDIO
    gsr_pipewire_audio *pipewire_audio;
#endif
} gsr_recorder_params;

/* Returns NULL on failure and sets |error| to a |gsr_error| value */
gsr_recorder* gsr_recorder_create(const gsr_recorder_params *params, const gsr_recorder_callbacks *callbacks, int *error);
/* This is only called when the program is exiting, so memory that the operating system frees on exit isn't free'd. Only set exiting to true if the program is exiting */
void gsr_recorder_destroy(gsr_recorder *self, bool exiting);

/* Returns a |gsr_error| value. Records until gsr_recorder_stop is called or until the capture target is gone */
int gsr_recorder_run(gsr_recorder *self);

/* These are safe to call from a signal handler or from another thread */
void gsr_recorder_stop(gsr_recorder *self);
void gsr_recorder_toggle_pause(gsr_recorder *self);
/* Does nothing when the recording is already paused/unpaused */
void gsr_recorder_set_paused(gsr_recorder *self, bool paused);
void gsr_recorder_toggle_replay_recording(gsr_recorder *self);
/* Does nothing when a recording is already running */
void gsr_recorder_start_replay_recording(gsr_recorder *self);
/* Does nothing when no recording is running */
void gsr_recorder_stop_replay_recording(gsr_recorder *self);
bool gsr_recorder_is_replay_recording(const gsr_recorder *self);
#define GSR_RESTART_REPLAY_USE_OPTION -1
#define GSR_RESTART_REPLAY_DISABLE 0
#define GSR_RESTART_REPLAY_ENABLE 1

/*
    |seconds| can be GSR_SAVE_REPLAY_SECONDS_FULL to save the whole replay buffer.
    |restart_replay| overrides the -restart-replay-on-save option for this save when it's not GSR_RESTART_REPLAY_USE_OPTION.
*/
void gsr_recorder_save_replay(gsr_recorder *self, int seconds, int restart_replay);

#endif /* GSR_RECORDER_RECORDER_H */
