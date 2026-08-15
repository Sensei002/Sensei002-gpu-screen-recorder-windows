#include "../../include/recorder/audio_input.h"
#include "../../include/recorder/error.h"
#include "../../include/utils.h"
#include "../../include/log.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    gsr_merged_audio_inputs *merged_audio_inputs;
    int error;
} parse_audio_input_userdata;

bool gsr_app_audio_names_add(gsr_app_audio_names *self, const char *name) {
    if(!gsr_array_ensure_capacity((void**)&self->items, self->num_items, &self->capacity_items, sizeof(gsr_app_audio_name)))
        return false;

    snprintf(self->items[self->num_items].name, sizeof(self->items[self->num_items].name), "%s", name);
    ++self->num_items;
    return true;
}

void gsr_app_audio_names_deinit(gsr_app_audio_names *self) {
    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
    self->capacity_items = 0;
}

bool gsr_merged_audio_inputs_add(gsr_merged_audio_inputs *self, const gsr_audio_input *audio_input) {
    if(!gsr_array_ensure_capacity((void**)&self->items, self->num_items, &self->capacity_items, sizeof(gsr_audio_input)))
        return false;

    self->items[self->num_items] = *audio_input;
    ++self->num_items;
    return true;
}

void gsr_merged_audio_inputs_deinit(gsr_merged_audio_inputs *self) {
    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
    self->capacity_items = 0;
}

static bool parse_audio_input_callback(const char *sub, size_t size, void *userdata) {
    parse_audio_input_userdata *parse_userdata = userdata;
    if(size == 0)
        return true;

    gsr_audio_input audio_input;
    memset(&audio_input, 0, sizeof(audio_input));
    snprintf(audio_input.name, sizeof(audio_input.name), "%.*s", (int)size, sub);

    const size_t name_size = strlen(audio_input.name);
    if(gsr_string_starts_with(audio_input.name, name_size, "name:")) {
        snprintf(parse_userdata->merged_audio_inputs->track_name, sizeof(parse_userdata->merged_audio_inputs->track_name), "%s", audio_input.name + 5);
        parse_userdata->merged_audio_inputs->has_custom_name = true;
        return true;
    } else if(gsr_string_starts_with(audio_input.name, name_size, "app:")) {
        memmove(audio_input.name, audio_input.name + 4, name_size - 4 + 1);
        audio_input.type = GSR_AUDIO_INPUT_TYPE_APPLICATION;
        audio_input.inverted = false;
    } else if(gsr_string_starts_with(audio_input.name, name_size, "app-inverse:")) {
        memmove(audio_input.name, audio_input.name + 12, name_size - 12 + 1);
        audio_input.type = GSR_AUDIO_INPUT_TYPE_APPLICATION;
        audio_input.inverted = true;
    } else if(gsr_string_starts_with(audio_input.name, name_size, "device:")) {
        memmove(audio_input.name, audio_input.name + 7, name_size - 7 + 1);
        audio_input.type = GSR_AUDIO_INPUT_TYPE_DEVICE;
    } else {
        audio_input.type = GSR_AUDIO_INPUT_TYPE_DEVICE;
    }

    if(!gsr_merged_audio_inputs_add(parse_userdata->merged_audio_inputs, &audio_input)) {
        parse_userdata->error = GSR_ERROR_GENERIC;
        return false;
    }

    return true;
}

int gsr_merged_audio_inputs_parse(gsr_merged_audio_inputs *self, const char *str) {
    memset(self, 0, sizeof(*self));

    parse_audio_input_userdata userdata;
    userdata.merged_audio_inputs = self;
    userdata.error = GSR_ERROR_OK;

    gsr_string_split(str, '|', parse_audio_input_callback, &userdata);
    if(userdata.error != GSR_ERROR_OK) {
        gsr_merged_audio_inputs_deinit(self);
        return userdata.error;
    }

    return GSR_ERROR_OK;
}

bool gsr_audio_input_tracks_add(gsr_audio_input_tracks *self, const gsr_merged_audio_inputs *merged_audio_inputs) {
    if(!gsr_array_ensure_capacity((void**)&self->items, self->num_items, &self->capacity_items, sizeof(gsr_merged_audio_inputs)))
        return false;

    self->items[self->num_items] = *merged_audio_inputs;
    ++self->num_items;
    return true;
}

void gsr_audio_input_tracks_deinit(gsr_audio_input_tracks *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        gsr_merged_audio_inputs_deinit(&self->items[i]);
    }

    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
    self->capacity_items = 0;
}

static const gsr_audio_device* get_audio_device_by_name(const gsr_audio_devices *audio_devices, const char *name) {
    for(size_t i = 0; i < audio_devices->num_items; ++i) {
        if(strcmp(audio_devices->items[i].name, name) == 0)
            return &audio_devices->items[i];
    }
    return NULL;
}

static void audio_track_title_append(char *title, size_t title_size, size_t *offset, const char *str) {
    const int written = snprintf(title + *offset, *offset < title_size ? title_size - *offset : 0, "%s", str);
    if(written > 0)
        *offset += written;
}

/* Manually check if the audio inputs we give exist. This is only needed for pipewire, not pulseaudio.
   Pipewire instead defaults to the default audio input if the audio input doesn't exist */
static int validate_audio_inputs_get_track_name(const gsr_merged_audio_inputs *merged_audio_inputs, const gsr_audio_devices *audio_devices, char *track_name, size_t track_name_size) {
    size_t offset = 0;
    bool has_devices = false;
    bool has_applications = false;
    bool app_inverse = false;
    track_name[0] = '\0';

    for(size_t i = 0; i < merged_audio_inputs->num_items; ++i) {
        const gsr_audio_input *audio_input = &merged_audio_inputs->items[i];
        if(audio_input->type == GSR_AUDIO_INPUT_TYPE_APPLICATION)
            continue;

        const char *device_description = NULL;
        if(strcmp(audio_input->name, "default_output") == 0) {
            if(audio_devices->default_output[0] == '\0') {
                gsr_log(GSR_LOG_LEVEL_ERROR, "-a default_output was specified but no default audio output is specified in the audio server");
                return GSR_ERROR_UNSUPPORTED;
            }
            device_description = "Default output";
        } else if(strcmp(audio_input->name, "default_input") == 0) {
            if(audio_devices->default_input[0] == '\0') {
                gsr_log(GSR_LOG_LEVEL_ERROR, "-a default_input was specified but no default audio input is specified in the audio server");
                return GSR_ERROR_UNSUPPORTED;
            }
            device_description = "Default input";
        } else {
            const gsr_audio_device *audio_device = get_audio_device_by_name(audio_devices, audio_input->name);
            if(audio_device)
                device_description = audio_device->description;
        }

        if(!device_description) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "Audio device '%s' is not a valid audio device, expected one of:", audio_input->name);
            if(audio_devices->default_output[0] != '\0')
                fprintf(stderr, "    default_output (Default output)\n");
            if(audio_devices->default_input[0] != '\0')
                fprintf(stderr, "    default_input (Default input)\n");
            for(size_t j = 0; j < audio_devices->num_items; ++j) {
                fprintf(stderr, "    %s (%s)\n", audio_devices->items[j].name, audio_devices->items[j].description);
            }
            return GSR_ERROR_AUDIO_DEVICE_NOT_FOUND;
        }

        audio_track_title_append(track_name, track_name_size, &offset, has_devices ? ", " : "Devices: ");
        audio_track_title_append(track_name, track_name_size, &offset, device_description);
        has_devices = true;
    }

    for(size_t i = 0; i < merged_audio_inputs->num_items; ++i) {
        const gsr_audio_input *audio_input = &merged_audio_inputs->items[i];
        if(audio_input->type != GSR_AUDIO_INPUT_TYPE_APPLICATION)
            continue;

        app_inverse = audio_input->inverted;
        if(!has_applications) {
            if(has_devices)
                audio_track_title_append(track_name, track_name_size, &offset, ". ");
            audio_track_title_append(track_name, track_name_size, &offset, app_inverse ? "All applications except: " : "Applications: ");
        } else {
            audio_track_title_append(track_name, track_name_size, &offset, ", ");
        }

        audio_track_title_append(track_name, track_name_size, &offset, audio_input->name);
        has_applications = true;
    }

    return GSR_ERROR_OK;
}

int gsr_audio_input_tracks_parse(gsr_audio_input_tracks *self, const char **audio_input_args, int num_audio_input_args, const gsr_audio_devices *audio_devices) {
    memset(self, 0, sizeof(*self));

    for(int i = 0; i < num_audio_input_args; ++i) {
        const char *audio_input = audio_input_args[i];
        if(!audio_input || audio_input[0] == '\0')
            continue;

        gsr_merged_audio_inputs merged_audio_inputs;
        const int parse_result = gsr_merged_audio_inputs_parse(&merged_audio_inputs, audio_input);
        if(parse_result != GSR_ERROR_OK) {
            gsr_audio_input_tracks_deinit(self);
            return parse_result;
        }

        char track_name[GSR_AUDIO_TRACK_NAME_MAX_SIZE];
        const int validate_result = validate_audio_inputs_get_track_name(&merged_audio_inputs, audio_devices, track_name, sizeof(track_name));
        if(validate_result != GSR_ERROR_OK) {
            gsr_merged_audio_inputs_deinit(&merged_audio_inputs);
            gsr_audio_input_tracks_deinit(self);
            return validate_result;
        }

        if(!merged_audio_inputs.has_custom_name)
            snprintf(merged_audio_inputs.track_name, sizeof(merged_audio_inputs.track_name), "%s", track_name);

        if(!gsr_audio_input_tracks_add(self, &merged_audio_inputs)) {
            gsr_merged_audio_inputs_deinit(&merged_audio_inputs);
            gsr_audio_input_tracks_deinit(self);
            return GSR_ERROR_GENERIC;
        }
    }

    return GSR_ERROR_OK;
}

bool gsr_audio_inputs_has_app_audio(const gsr_merged_audio_inputs *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type == GSR_AUDIO_INPUT_TYPE_APPLICATION)
            return true;
    }
    return false;
}

/* Should use amix if more than 1 audio device and 0 application audio, merged */
bool gsr_audio_inputs_should_use_amix(const gsr_merged_audio_inputs *self) {
    int num_audio_devices = 0;
    int num_app_audio = 0;

    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type == GSR_AUDIO_INPUT_TYPE_DEVICE)
            ++num_audio_devices;
        else if(self->items[i].type == GSR_AUDIO_INPUT_TYPE_APPLICATION)
            ++num_app_audio;
    }

    return num_audio_devices > 1 && num_app_audio == 0;
}

bool gsr_audio_input_tracks_has_app_audio(const gsr_audio_input_tracks *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(gsr_audio_inputs_has_app_audio(&self->items[i]))
            return true;
    }
    return false;
}

bool gsr_audio_input_tracks_should_use_amix(const gsr_audio_input_tracks *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(gsr_audio_inputs_should_use_amix(&self->items[i]))
            return true;
    }
    return false;
}

static void match_app_audio_input_to_available_apps(const gsr_merged_audio_inputs *merged_audio_inputs, const gsr_app_audio_names *app_audio_names) {
    for(size_t i = 0; i < merged_audio_inputs->num_items; ++i) {
        const gsr_audio_input *audio_input = &merged_audio_inputs->items[i];
        if(audio_input->type != GSR_AUDIO_INPUT_TYPE_APPLICATION || audio_input->inverted)
            continue;

        bool match = false;
        for(size_t j = 0; j < app_audio_names->num_items; ++j) {
            if(strcasecmp(app_audio_names->items[j].name, audio_input->name) == 0) {
                match = true;
                break;
            }
        }

        if(!match) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "no audio application with the name \"%s\" was found, expected one of the following:", audio_input->name);
            for(size_t j = 0; j < app_audio_names->num_items; ++j) {
                fprintf(stderr, "  * %s\n", app_audio_names->items[j].name);
            }
            fprintf(stderr, "  assuming this is intentional (if you are trying to record audio for applications that haven't started yet).\n");
        }
    }
}

int gsr_audio_input_tracks_validate_app_audio(const gsr_audio_input_tracks *self, const gsr_app_audio_names *app_audio_names) {
    for(size_t i = 0; i < self->num_items; ++i) {
        const gsr_merged_audio_inputs *merged_audio_inputs = &self->items[i];
        int num_app_audio = 0;
        int num_app_inverted_audio = 0;

        for(size_t j = 0; j < merged_audio_inputs->num_items; ++j) {
            const gsr_audio_input *audio_input = &merged_audio_inputs->items[j];
            if(audio_input->type == GSR_AUDIO_INPUT_TYPE_APPLICATION) {
                if(audio_input->inverted)
                    ++num_app_inverted_audio;
                else
                    ++num_app_audio;
            }
        }

        match_app_audio_input_to_available_apps(merged_audio_inputs, app_audio_names);

        if(num_app_audio > 0 && num_app_inverted_audio > 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "argument -a was provided with both app: and app-inverse:, only one of them can be used for one audio track");
            return GSR_ERROR_UNSUPPORTED;
        }
    }

    return GSR_ERROR_OK;
}
