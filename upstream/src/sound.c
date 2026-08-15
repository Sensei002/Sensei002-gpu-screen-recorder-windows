#include "../include/sound.h"
#include "../include/log.h"
#include "../include/utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>
#include <pulse/xmalloc.h>
#include <pulse/error.h>

#define RECONNECT_TRY_TIMEOUT_SECONDS 0.5
#define DEVICE_NAME_MAX_SIZE 128

#define CHECK_DEAD_GOTO(p, rerror, label)                               \
    do {                                                                \
        if (!(p)->context || !PA_CONTEXT_IS_GOOD(pa_context_get_state((p)->context)) || \
            !(p)->stream || !PA_STREAM_IS_GOOD(pa_stream_get_state((p)->stream))) { \
            if (((p)->context && pa_context_get_state((p)->context) == PA_CONTEXT_FAILED) || \
                ((p)->stream && pa_stream_get_state((p)->stream) == PA_STREAM_FAILED)) { \
                if (rerror)                                             \
                    *(rerror) = pa_context_errno((p)->context);         \
            } else                                                      \
                if (rerror)                                             \
                    *(rerror) = PA_ERR_BADSTATE;                        \
            goto label;                                                 \
        }                                                               \
    } while(false);

typedef enum {
    DEVICE_TYPE_STANDARD,
    DEVICE_TYPE_DEFAULT_OUTPUT,
    DEVICE_TYPE_DEFAULT_INPUT
} DeviceType;

typedef struct {
    pa_context *context;
    pa_stream *stream;
    pa_mainloop *mainloop;

    const void *read_data;
    size_t read_index, read_length;

    uint8_t *output_data;
    size_t output_index, output_length;

    int operation_success;
    double latency_seconds;

    pa_buffer_attr attr;
    pa_sample_spec ss;

    pthread_mutex_t reconnect_mutex;
    bool reconnect_mutex_initialized;
    DeviceType device_type;
    char stream_name[256];
    char node_name[256];
    bool reconnect;
    double reconnect_last_tried_seconds;

    char device_name[DEVICE_NAME_MAX_SIZE];
    char default_output_device_name[DEVICE_NAME_MAX_SIZE];
    char default_input_device_name[DEVICE_NAME_MAX_SIZE];

    pa_proplist *proplist;
    bool connected;
} pa_handle;

static void pa_sound_device_free(pa_handle *p) {
    assert(p);

    if(p->proplist) {
        pa_proplist_free(p->proplist);
        p->proplist = NULL;
    }

    if (p->stream) {
        pa_stream_unref(p->stream);
        p->stream = NULL;
    }

    if (p->context) {
        pa_context_disconnect(p->context);
        pa_context_unref(p->context);
        p->context = NULL;
    }

    if (p->mainloop) {
        pa_mainloop_free(p->mainloop);
        p->mainloop = NULL;
    }

    if (p->output_data) {
        free(p->output_data);
        p->output_data = NULL;
    }

    if (p->reconnect_mutex_initialized) {
        pthread_mutex_destroy(&p->reconnect_mutex);
        p->reconnect_mutex_initialized = false;
    }

    pa_xfree(p);
}

static void subscribe_update_default_devices(pa_context *ctx, const pa_server_info *server_info, void *userdata) {
    (void)ctx;
    pa_handle *handle = (pa_handle*)userdata;
    pthread_mutex_lock(&handle->reconnect_mutex);

    if(server_info->default_sink_name) {
        // TODO: Size check
        snprintf(handle->default_output_device_name, sizeof(handle->default_output_device_name), "%s.monitor", server_info->default_sink_name);
        if(handle->device_type == DEVICE_TYPE_DEFAULT_OUTPUT && strcmp(handle->device_name, handle->default_output_device_name) != 0) {
            handle->reconnect = true;
            handle->reconnect_last_tried_seconds = clock_get_monotonic_seconds();
            // TODO: Size check
            snprintf(handle->device_name, sizeof(handle->device_name), "%s", handle->default_output_device_name);
        }
    }

    if(server_info->default_source_name) {
        // TODO: Size check
        snprintf(handle->default_input_device_name, sizeof(handle->default_input_device_name), "%s", server_info->default_source_name);
        if(handle->device_type == DEVICE_TYPE_DEFAULT_INPUT && strcmp(handle->device_name, handle->default_input_device_name) != 0) {
            handle->reconnect = true;
            handle->reconnect_last_tried_seconds = clock_get_monotonic_seconds();
            // TODO: Size check
            snprintf(handle->device_name, sizeof(handle->device_name), "%s", handle->default_input_device_name);
        }
    }

    pthread_mutex_unlock(&handle->reconnect_mutex);
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    (void)idx;
    pa_handle *handle = (pa_handle*)userdata;
    if((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER) {
        pa_operation *pa = pa_context_get_server_info(c, subscribe_update_default_devices, handle);
        if(pa)
            pa_operation_unref(pa);
    }
}

static void store_default_devices(pa_context *ctx, const pa_server_info *server_info, void *userdata) {
    (void)ctx;
    pa_handle *handle = (pa_handle*)userdata;
    if(server_info->default_sink_name)
        snprintf(handle->default_output_device_name, sizeof(handle->default_output_device_name), "%s.monitor", server_info->default_sink_name);
    if(server_info->default_source_name)
        snprintf(handle->default_input_device_name, sizeof(handle->default_input_device_name), "%s", server_info->default_source_name);
}

static bool startup_get_default_devices(pa_handle *p, const char *device_name) {
    pa_operation *pa = pa_context_get_server_info(p->context, store_default_devices, p);
    while(pa) {
        pa_operation_state_t state = pa_operation_get_state(pa);
        if(state == PA_OPERATION_DONE) {
            pa_operation_unref(pa);
            break;
        } else if(state == PA_OPERATION_CANCELLED) {
            pa_operation_unref(pa);
            return false;
        }
        pa_mainloop_iterate(p->mainloop, 1, NULL);
    }

    if(p->default_output_device_name[0] == '\0') {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to find default audio output device");
        return false;
    }

    if(strcmp(device_name, "default_output") == 0) {
        snprintf(p->device_name, sizeof(p->device_name), "%s", p->default_output_device_name);
        p->device_type = DEVICE_TYPE_DEFAULT_OUTPUT;
    } else if(strcmp(device_name, "default_input") == 0) {
        snprintf(p->device_name, sizeof(p->device_name), "%s", p->default_input_device_name);
        p->device_type = DEVICE_TYPE_DEFAULT_INPUT;
    } else {
        snprintf(p->device_name, sizeof(p->device_name), "%s", device_name);
        p->device_type = DEVICE_TYPE_STANDARD;
    }

    return true;
}

static pa_handle* pa_sound_device_new(const char *server,
        const char *name,
        const char *device_name,
        const char *stream_name,
        const pa_sample_spec *ss,
        const pa_buffer_attr *attr,
        int *rerror) {
    pa_handle *p;
    int error = PA_ERR_INTERNAL;
    pa_operation *pa = NULL;

    p = pa_xnew0(pa_handle, 1);
    p->attr = *attr;
    p->ss = *ss;
    snprintf(p->node_name, sizeof(p->node_name), "%s", name);
    snprintf(p->stream_name, sizeof(p->stream_name), "%s", stream_name);

    if(pthread_mutex_init(&p->reconnect_mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to initialize reconnect mutex");
        *rerror = -1;
        pa_xfree(p);
        return NULL;
    }
    p->reconnect_mutex_initialized = true;

    p->reconnect = true;
    p->reconnect_last_tried_seconds = clock_get_monotonic_seconds() - (RECONNECT_TRY_TIMEOUT_SECONDS * 1000.0 * 2.0);
    p->default_output_device_name[0] = '\0';
    p->default_input_device_name[0] = '\0';
    p->device_type = DEVICE_TYPE_STANDARD;

    const int buffer_size = attr->fragsize;
    void *buffer = malloc(buffer_size);
    if(!buffer) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate buffer for audio");
        *rerror = -1;
        pa_sound_device_free(p);
        return NULL;
    }

    p->output_data = (uint8_t*)buffer;
    p->output_length = buffer_size;
    p->output_index = 0;

    p->proplist = pa_proplist_new();
    pa_proplist_sets(p->proplist, PA_PROP_MEDIA_ROLE, "production");
    if(strcmp(device_name, "") == 0) {
        pa_proplist_sets(p->proplist, "node.autoconnect", "false");
        pa_proplist_sets(p->proplist, "node.dont-reconnect", "true");
    }

    if (!(p->mainloop = pa_mainloop_new()))
        goto fail;

    if (!(p->context = pa_context_new_with_proplist(pa_mainloop_get_api(p->mainloop), p->node_name, p->proplist)))
        goto fail;

    if (pa_context_connect(p->context, server, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        error = pa_context_errno(p->context);
        goto fail;
    }

    for (;;) {
        pa_context_state_t state = pa_context_get_state(p->context);

        if (state == PA_CONTEXT_READY)
            break;

        if (!PA_CONTEXT_IS_GOOD(state)) {
            error = pa_context_errno(p->context);
            goto fail;
        }

        pa_mainloop_iterate(p->mainloop, 1, NULL);
    }

    if(!startup_get_default_devices(p, device_name))
        goto fail;

    pa_context_set_subscribe_callback(p->context, subscribe_cb, p);
    pa = pa_context_subscribe(p->context, PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
    if(pa)
        pa_operation_unref(pa);

    p->connected = true;
    return p;

fail:
    if (rerror)
        *rerror = error;
    pa_sound_device_free(p);
    return NULL;
}

static void pa_sound_device_update_context_status(pa_handle *p) {
    if(p->connected || !p->context || pa_context_get_state(p->context) != PA_CONTEXT_READY)
        return;

    p->connected = true;
    pa_context_set_subscribe_callback(p->context, subscribe_cb, p);
    pa_operation *pa = pa_context_subscribe(p->context, PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
    if(pa)
        pa_operation_unref(pa);
}

static bool pa_sound_device_handle_context_recreate(pa_handle *p) {
    if(p->context) {
        pa_context_disconnect(p->context);
        pa_context_unref(p->context);
        p->context = NULL;
        p->connected = false;
    }

    if (!(p->context = pa_context_new_with_proplist(pa_mainloop_get_api(p->mainloop), p->node_name, p->proplist))) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "pa_context_new_with_proplist failed");
        goto fail;
    }

    if(pa_context_connect(p->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "pa_context_connect failed");
        goto fail;
    }

    pa_mainloop_iterate(p->mainloop, 0, NULL);
    pa_sound_device_update_context_status(p);
    return true;

    fail:
    if(p->context) {
        pa_context_disconnect(p->context);
        pa_context_unref(p->context);
        p->context = NULL;
    }
    return false;
}

static bool pa_sound_device_should_reconnect(pa_handle *p, double now, char *device_name, size_t device_name_size) {
    bool should_reconnect = false;
    pthread_mutex_lock(&p->reconnect_mutex);

    if(!p->reconnect && (!p->stream || !PA_STREAM_IS_GOOD(pa_stream_get_state(p->stream)))) {
        p->reconnect = true;
        p->reconnect_last_tried_seconds = now;
    }

    if(p->reconnect && now - p->reconnect_last_tried_seconds >= RECONNECT_TRY_TIMEOUT_SECONDS) {
        p->reconnect_last_tried_seconds = now;
        // TODO: Size check
        snprintf(device_name, device_name_size, "%s", p->device_name);
        should_reconnect = true;
    }

    pthread_mutex_unlock(&p->reconnect_mutex);
    return should_reconnect;
}

static bool pa_sound_device_handle_reconnect(pa_handle *p, char *device_name, size_t device_name_size, double now) {
    if(!pa_sound_device_should_reconnect(p, now, device_name, device_name_size))
        return true;

    if(p->stream) {
        pa_stream_disconnect(p->stream);
        pa_stream_unref(p->stream);
        p->stream = NULL;

        pa_sound_device_handle_context_recreate(p);
        if(!p->connected)
            return false;
    }

    pa_proplist *proplist = pa_proplist_new();
    // This prevents microphone recording indicator from being shown on KDE
    pa_proplist_sets(proplist, "node.virtual", "true");

    if(!(p->stream = pa_stream_new_with_proplist(p->context, p->stream_name, &p->ss, NULL, proplist))) {
        //pa_context_errno(p->context);
        pa_proplist_free(proplist);
        return false;
    }

    pa_proplist_free(proplist);

    const int r = pa_stream_connect_record(p->stream, device_name, &p->attr,
        (pa_stream_flags_t)(PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_ADJUST_LATENCY|PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_DONT_MOVE));

    if(r < 0) {
        //pa_context_errno(p->context);
        return false;
    }

    pa_mainloop_iterate(p->mainloop, 0, NULL);

    pthread_mutex_lock(&p->reconnect_mutex);
    p->reconnect = false;
    pthread_mutex_unlock(&p->reconnect_mutex);
    return true;
}

static int pa_sound_device_read(pa_handle *p, double timeout_seconds) {
    assert(p);

    const double start_time = clock_get_monotonic_seconds();
    char device_name[DEVICE_NAME_MAX_SIZE];

    bool success = false;
    int r = 0;
    int *rerror = &r;
    pa_usec_t latency = 0;
    int negative = 0;

    pa_mainloop_iterate(p->mainloop, 0, NULL);

    if(!p->context) {
        if(!pa_sound_device_handle_context_recreate(p))
            goto fail;
    }

    pa_sound_device_update_context_status(p);
    if(!p->connected)
        goto fail;

    if(!pa_sound_device_handle_reconnect(p, device_name, sizeof(device_name), start_time) || !p->stream)
        goto fail;

    if(pa_stream_get_state(p->stream) != PA_STREAM_READY)
        goto fail;

    CHECK_DEAD_GOTO(p, rerror, fail);

    while (p->output_index < p->output_length) {
        if(clock_get_monotonic_seconds() - start_time >= timeout_seconds)
            return -1;

        if(!p->read_data) {
            pa_mainloop_prepare(p->mainloop, 1 * 1000); // 1 ms
            pa_mainloop_poll(p->mainloop);
            pa_mainloop_dispatch(p->mainloop);

            if(pa_stream_peek(p->stream, &p->read_data, &p->read_length) < 0)
                goto fail;

            if(!p->read_data && p->read_length == 0)
                continue;

            if(!p->read_data && p->read_length > 0) {
                // There is a hole in the stream :( drop it. Maybe we should generate silence instead? TODO
                if(pa_stream_drop(p->stream) != 0)
                    goto fail;
                continue;
            }

            if(p->read_length <= 0) {
                p->read_data = NULL;
                if(pa_stream_drop(p->stream) != 0)
                    goto fail;

                CHECK_DEAD_GOTO(p, rerror, fail);
                continue;
            }

            pa_operation_unref(pa_stream_update_timing_info(p->stream, NULL, NULL));
            // TODO: Deal with one pa_stream_peek not being enough. In that case we need to add multiple of these together(?)
            if(pa_stream_get_latency(p->stream, &latency, &negative) >= 0) {
                p->latency_seconds = negative ? -(double)latency : latency;
                if(p->latency_seconds < 0.0)
                    p->latency_seconds = 0.0;
                p->latency_seconds *= 0.0000001;
            }
        }

        const size_t space_free_in_output_buffer = p->output_length - p->output_index;
        if(space_free_in_output_buffer < p->read_length) {
            memcpy(p->output_data + p->output_index, (const uint8_t*)p->read_data + p->read_index, space_free_in_output_buffer);
            p->output_index = 0;
            p->read_index += space_free_in_output_buffer;
            p->read_length -= space_free_in_output_buffer;
            break;
        } else {
            memcpy(p->output_data + p->output_index, (const uint8_t*)p->read_data + p->read_index, p->read_length);
            p->output_index += p->read_length;
            p->read_data = NULL;
            p->read_length = 0;
            p->read_index = 0;

            if(pa_stream_drop(p->stream) != 0)
                goto fail;

            if(p->output_index == p->output_length) {
                p->output_index = 0;
                break;
            }
        }
    }

    success = true;

    fail:
    return success ? 0 : -1;
}

static pa_sample_format_t audio_format_to_pulse_audio_format(gsr_audio_format audio_format) {
    switch(audio_format) {
        case GSR_AUDIO_FORMAT_S16: return PA_SAMPLE_S16LE;
        case GSR_AUDIO_FORMAT_S32: return PA_SAMPLE_S32LE;
        case GSR_AUDIO_FORMAT_F32: return PA_SAMPLE_FLOAT32LE;
    }
    assert(false);
    return PA_SAMPLE_S16LE;
}

static int audio_format_to_get_bytes_per_sample(gsr_audio_format audio_format) {
    switch(audio_format) {
        case GSR_AUDIO_FORMAT_S16: return 2;
        case GSR_AUDIO_FORMAT_S32: return 4;
        case GSR_AUDIO_FORMAT_F32: return 4;
    }
    assert(false);
    return 2;
}

int sound_device_get_by_name(SoundDevice *device, const char *node_name, const char *device_name, const char *description, unsigned int num_channels, unsigned int period_frame_size, gsr_audio_format audio_format) {
    pa_sample_spec ss;
    ss.format = audio_format_to_pulse_audio_format(audio_format);
    ss.rate = 48000;
    ss.channels = num_channels;

    pa_buffer_attr buffer_attr;
    buffer_attr.fragsize = period_frame_size * audio_format_to_get_bytes_per_sample(audio_format) * num_channels; // 2/4 bytes/sample, @num_channels channels
    buffer_attr.tlength = -1;
    buffer_attr.prebuf = -1;
    buffer_attr.minreq = -1;
    buffer_attr.maxlength = buffer_attr.fragsize;

    int error = 0;
    pa_handle *handle = pa_sound_device_new(NULL, node_name, device_name, description, &ss, &buffer_attr, &error);
    if(!handle) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "pa_sound_device_new() failed: %s. Audio input device %s might not be valid", pa_strerror(error), device_name);
        return -1;
    }

    device->handle = handle;
    device->frames = period_frame_size;
    return 0;
}

void sound_device_close(SoundDevice *device) {
    if(device->handle)
        pa_sound_device_free((pa_handle*)device->handle);
    device->handle = NULL;
}

void sound_device_flush(SoundDevice *device) {
    pa_handle *p = (pa_handle*)device->handle;
    if(!p || !p->stream)
        return;

    if(pa_stream_get_state(p->stream) != PA_STREAM_READY)
        return;

    pa_operation *op = pa_stream_flush(p->stream, NULL, NULL);
    if(!op)
        return;

    /* The flush operation should finish immediately, the timeout is to not freeze if pulseaudio is in a bad state */
    const double start_time = clock_get_monotonic_seconds();
    while(pa_operation_get_state(op) == PA_OPERATION_RUNNING && clock_get_monotonic_seconds() - start_time < 0.1) {
        pa_mainloop_iterate(p->mainloop, 1, NULL);
    }
    pa_operation_unref(op);
}

int sound_device_read_next_chunk(SoundDevice *device, void **buffer, double timeout_sec, double *latency_seconds) {
    pa_handle *pa = (pa_handle*)device->handle;
    if(pa_sound_device_read(pa, timeout_sec) < 0) {
        //gsr_log(GSR_LOG_LEVEL_ERROR, "pa_simple_read() failed: %s", pa_strerror(error));
        *latency_seconds = 0.0;
        return -1;
    }
    *buffer = pa->output_data;
    *latency_seconds = pa->latency_seconds;
    return device->frames;
}

static void pa_state_cb(pa_context *c, void *userdata) {
    pa_context_state_t state = pa_context_get_state(c);
    int *pa_ready = (int*)userdata;
    switch(state) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            *pa_ready = 2;
            break;
        case PA_CONTEXT_READY:
            *pa_ready = 1;
            break;
    }
}

static void pa_sourcelist_cb(pa_context *ctx, const pa_source_info *source_info, int eol, void *userdata) {
    (void)ctx;
    if(eol > 0)
        return;

    gsr_audio_devices *audio_devices = (gsr_audio_devices*)userdata;
    if(!gsr_array_ensure_capacity((void**)&audio_devices->items, audio_devices->num_items, &audio_devices->capacity_items, sizeof(gsr_audio_device)))
        return;

    gsr_audio_device *audio_device = &audio_devices->items[audio_devices->num_items];
    snprintf(audio_device->name, sizeof(audio_device->name), "%s", source_info->name);
    snprintf(audio_device->description, sizeof(audio_device->description), "%s", source_info->description);
    ++audio_devices->num_items;
}

static void pa_server_info_cb(pa_context *ctx, const pa_server_info *server_info, void *userdata) {
    (void)ctx;
    gsr_audio_devices *audio_devices = (gsr_audio_devices*)userdata;
    if(server_info->default_sink_name)
        snprintf(audio_devices->default_output, sizeof(audio_devices->default_output), "%s.monitor", server_info->default_sink_name);
    if(server_info->default_source_name)
        snprintf(audio_devices->default_input, sizeof(audio_devices->default_input), "%s", server_info->default_source_name);
}

static void server_info_callback(pa_context *ctx, const pa_server_info *server_info, void *userdata) {
    (void)ctx;
    bool *is_server_pipewire = (bool*)userdata;
    if(server_info->server_name && strstr(server_info->server_name, "PipeWire"))
        *is_server_pipewire = true;
}

static void get_pulseaudio_default_inputs(gsr_audio_devices *audio_devices) {
    int state = 0;
    int pa_ready = 0;
    pa_operation *pa_op = NULL;

    pa_mainloop *main_loop = pa_mainloop_new();
    if(!main_loop)
        return;

    pa_context *ctx = pa_context_new(pa_mainloop_get_api(main_loop), "gpu-screen-recorder");
    if(pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
        goto done;

    pa_context_set_state_callback(ctx, pa_state_cb, &pa_ready);

    for(;;) {
        // Not ready
        if(pa_ready == 0) {
            pa_mainloop_iterate(main_loop, 1, NULL);
            continue;
        }

        switch(state) {
            case 0: {
                pa_op = pa_context_get_server_info(ctx, pa_server_info_cb, audio_devices);
                ++state;
                break;
            }
        }

        // Couldn't get connection to the server
        if(pa_ready == 2 || (state == 1 && pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE))
            break;

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    done:
    if(pa_op)
        pa_operation_unref(pa_op);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(main_loop);
}

void get_pulseaudio_inputs(gsr_audio_devices *audio_devices) {
    memset(audio_devices, 0, sizeof(*audio_devices));

    int state = 0;
    int pa_ready = 0;
    pa_operation *pa_op = NULL;

    // TODO: Do this in the same connection below instead of two separate connections
    get_pulseaudio_default_inputs(audio_devices);

    pa_mainloop *main_loop = pa_mainloop_new();
    if(!main_loop)
        return;

    pa_context *ctx = pa_context_new(pa_mainloop_get_api(main_loop), "gpu-screen-recorder");
    if(pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
        goto done;

    pa_context_set_state_callback(ctx, pa_state_cb, &pa_ready);

    for(;;) {
        // Not ready
        if(pa_ready == 0) {
            pa_mainloop_iterate(main_loop, 1, NULL);
            continue;
        }

        switch(state) {
            case 0: {
                pa_op = pa_context_get_source_info_list(ctx, pa_sourcelist_cb, audio_devices);
                ++state;
                break;
            }
        }

        // Couldn't get connection to the server
        if(pa_ready == 2 || (state == 1 && pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE))
            break;

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    done:
    if(pa_op)
        pa_operation_unref(pa_op);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(main_loop);
}

void gsr_audio_devices_deinit(gsr_audio_devices *self) {
    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
    self->capacity_items = 0;
    self->default_output[0] = '\0';
    self->default_input[0] = '\0';
}

bool pulseaudio_server_is_pipewire(void) {
    int state = 0;
    int pa_ready = 0;
    pa_operation *pa_op = NULL;
    bool is_server_pipewire = false;

    pa_mainloop *main_loop = pa_mainloop_new();
    if(!main_loop)
        return is_server_pipewire;

    pa_context *ctx = pa_context_new(pa_mainloop_get_api(main_loop), "gpu-screen-recorder");
    if(pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
        goto done;

    pa_context_set_state_callback(ctx, pa_state_cb, &pa_ready);

    for(;;) {
        // Not ready
        if(pa_ready == 0) {
            pa_mainloop_iterate(main_loop, 1, NULL);
            continue;
        }

        switch(state) {
            case 0: {
                pa_op = pa_context_get_server_info(ctx, server_info_callback, &is_server_pipewire);
                ++state;
                break;
            }
        }

        // Couldn't get connection to the server
        if(pa_ready == 2 || (state == 1 && pa_op && pa_operation_get_state(pa_op) == PA_OPERATION_DONE))
            break;

        pa_mainloop_iterate(main_loop, 1, NULL);
    }

    done:
    if(pa_op)
        pa_operation_unref(pa_op);
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(main_loop);
    return is_server_pipewire;
}
