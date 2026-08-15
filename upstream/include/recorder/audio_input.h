#ifndef GSR_RECORDER_AUDIO_INPUT_H
#define GSR_RECORDER_AUDIO_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include "../sound.h"

#define GSR_AUDIO_INPUT_NAME_MAX_SIZE 256
#define GSR_AUDIO_TRACK_NAME_MAX_SIZE 512

typedef enum {
    GSR_AUDIO_INPUT_TYPE_DEVICE,
    GSR_AUDIO_INPUT_TYPE_APPLICATION
} gsr_audio_input_type;

typedef struct {
    char name[GSR_AUDIO_INPUT_NAME_MAX_SIZE];
    gsr_audio_input_type type;
    bool inverted;
} gsr_audio_input;

typedef struct {
    char track_name[GSR_AUDIO_TRACK_NAME_MAX_SIZE];
    bool has_custom_name;
    gsr_audio_input *items;
    size_t num_items;
    size_t capacity_items;
} gsr_merged_audio_inputs;

typedef struct {
    gsr_merged_audio_inputs *items;
    size_t num_items;
    size_t capacity_items;
} gsr_audio_input_tracks;

typedef struct {
    char name[GSR_AUDIO_INPUT_NAME_MAX_SIZE];
} gsr_app_audio_name;

typedef struct {
    gsr_app_audio_name *items;
    size_t num_items;
    size_t capacity_items;
} gsr_app_audio_names;

bool gsr_app_audio_names_add(gsr_app_audio_names *self, const char *name);
void gsr_app_audio_names_deinit(gsr_app_audio_names *self);

/* Returns a |gsr_error| value. Parses one -a option value, which may contain multiple audio inputs separated by | */
int gsr_merged_audio_inputs_parse(gsr_merged_audio_inputs *self, const char *str);
bool gsr_merged_audio_inputs_add(gsr_merged_audio_inputs *self, const gsr_audio_input *audio_input);
void gsr_merged_audio_inputs_deinit(gsr_merged_audio_inputs *self);

/* Returns a |gsr_error| value. Parses every -a option value and validates that the audio devices exist */
int gsr_audio_input_tracks_parse(gsr_audio_input_tracks *self, const char **audio_input_args, int num_audio_input_args, const gsr_audio_devices *audio_devices);
bool gsr_audio_input_tracks_add(gsr_audio_input_tracks *self, const gsr_merged_audio_inputs *merged_audio_inputs);
void gsr_audio_input_tracks_deinit(gsr_audio_input_tracks *self);

bool gsr_audio_inputs_has_app_audio(const gsr_merged_audio_inputs *self);
bool gsr_audio_inputs_should_use_amix(const gsr_merged_audio_inputs *self);
bool gsr_audio_input_tracks_has_app_audio(const gsr_audio_input_tracks *self);
bool gsr_audio_input_tracks_should_use_amix(const gsr_audio_input_tracks *self);
/* Returns a |gsr_error| value. Warns about application audio names that don't match any running application */
int gsr_audio_input_tracks_validate_app_audio(const gsr_audio_input_tracks *self, const gsr_app_audio_names *app_audio_names);

#endif /* GSR_RECORDER_AUDIO_INPUT_H */
