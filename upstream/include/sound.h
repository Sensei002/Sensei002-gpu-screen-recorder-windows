#ifndef GSR_SOUND_H
#define GSR_SOUND_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void *handle;
    unsigned int frames;
} SoundDevice;

typedef struct {
    char name[256];
    char description[256];
} gsr_audio_device;

typedef struct {
    char default_output[256];
    char default_input[256];
    gsr_audio_device *items;
    size_t num_items;
    size_t capacity_items;
} gsr_audio_devices;

typedef enum {
    GSR_AUDIO_FORMAT_S16,
    GSR_AUDIO_FORMAT_S32,
    GSR_AUDIO_FORMAT_F32
} gsr_audio_format;

/*
    Get a sound device by name, returning the device into the |device| parameter.
    |device_name| can be a device name or "default_output", "default_input" or "" to not connect to any device (used for app audio for example).
    If the device name is "default_output" or "default_input" then it will automatically switch which
    device is records from when the default output/input is changed in the system audio settings.
    Returns 0 on success, or a negative value on failure.
*/
int sound_device_get_by_name(SoundDevice *device, const char *node_name, const char *device_name, const char *description, unsigned int num_channels, unsigned int period_frame_size, gsr_audio_format audio_format);

void sound_device_close(SoundDevice *device);

/*
    Discards the audio that has been captured so far.
    Call this before the first call to sound_device_read_next_chunk to not get audio that was captured before that point,
    since the sound device can be created a while before audio capture starts.
*/
void sound_device_flush(SoundDevice *device);

/*
    Returns the next chunk of audio into @buffer.
    Returns the number of frames read, or a negative value on failure.
*/
int sound_device_read_next_chunk(SoundDevice *device, void **buffer, double timeout_sec, double *latency_seconds);

void get_pulseaudio_inputs(gsr_audio_devices *audio_devices);
void gsr_audio_devices_deinit(gsr_audio_devices *self);
bool pulseaudio_server_is_pipewire(void);

#endif /* GSR_SOUND_H */
