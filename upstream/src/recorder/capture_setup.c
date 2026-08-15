#include "../../include/recorder/capture_setup.h"
#include "../../include/recorder/error.h"
#include "../../include/recorder/windowing.h"
#include "../../include/capture/nvfbc.h"
#include "../../include/capture/xcomposite.h"
#include "../../include/capture/ximage.h"
#include "../../include/capture/kms.h"
#include "../../include/capture/v4l2.h"
#ifdef GSR_PORTAL
#include "../../include/capture/portal.h"
#endif
#include "../../include/args_parser.h"
#include "../../include/utils.h"
#include "../../include/window/window.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    gsr_window *window;
} monitor_output_callback_userdata;

typedef struct {
    char *output_name;
} first_output_callback_userdata;

typedef struct {
    gsr_window *window;
    vec2i position;
    char *output_name;
    vec2i monitor_pos;
    vec2i monitor_size;
    double monitor_scale_inverted;
} monitor_by_position_callback_userdata;

static void monitor_output_callback_print(const gsr_monitor *monitor, void *userdata) {
    const monitor_output_callback_userdata *options = userdata;
    vec2i monitor_position = monitor->pos;
    vec2i monitor_size = monitor->size;
    if(gsr_window_get_display_server(options->window) == GSR_DISPLAY_SERVER_WAYLAND) {
        gsr_monitor_rotation monitor_rotation = GSR_MONITOR_ROT_0;
        drm_monitor_get_display_server_data(options->window, monitor, &monitor_rotation, &monitor_position);
        if(monitor_rotation == GSR_MONITOR_ROT_90 || monitor_rotation == GSR_MONITOR_ROT_270) {
            const int tmp = monitor_size.x;
            monitor_size.x = monitor_size.y;
            monitor_size.y = tmp;
        }
    }
    fprintf(stderr, "  \"%.*s\"    (%dx%d+%d+%d)\n", monitor->name_len, monitor->name, monitor_size.x, monitor_size.y, monitor_position.x, monitor_position.y);
}

static void monitor_output_callback_print_region(const gsr_monitor *monitor, void *userdata) {
    (void)userdata;
    const vec2i monitor_position = monitor->logical_pos;
    const vec2i monitor_size = monitor->logical_size;
    fprintf(stderr, "  \"%.*s\"    (%dx%d+%d+%d)\n", monitor->name_len, monitor->name, monitor_size.x, monitor_size.y, monitor_position.x, monitor_position.y);
}

static void get_first_output_callback(const gsr_monitor *monitor, void *userdata) {
    first_output_callback_userdata *data = userdata;
    if(!data->output_name)
        data->output_name = strdup(monitor->name);
}

static void get_monitor_by_position_callback(const gsr_monitor *monitor, void *userdata) {
    monitor_by_position_callback_userdata *data = userdata;

    const vec2i monitor_position = monitor->logical_pos;
    const vec2i monitor_size = monitor->size;
    const vec2i monitor_logical_size = monitor->logical_size;

    if(!data->output_name && data->position.x >= monitor_position.x && data->position.x <= monitor_position.x + monitor_logical_size.x
        && data->position.y >= monitor_position.y && data->position.y <= monitor_position.y + monitor_logical_size.y)
    {
        data->output_name = strdup(monitor->name);
        data->monitor_pos = monitor_position;
        data->monitor_size = monitor_size;
        data->monitor_scale_inverted = (double)monitor_size.x / (double)monitor_logical_size.x;
    }
}

void gsr_capture_deps_init(gsr_capture_deps *self) {
    memset(self, 0, sizeof(*self));
}

void gsr_capture_deps_init_cursor(gsr_capture_deps *self, gsr_egl *egl, bool record_cursor) {
    if(gsr_window_get_display_server(egl->window) != GSR_DISPLAY_SERVER_X11 || !record_cursor)
        return;

    self->x11_cursor_display = (Display*)gsr_window_get_display(egl->window);
    gsr_cursor_init(&self->x11_cursor, egl, self->x11_cursor_display);
}

void gsr_capture_deps_deinit(gsr_capture_deps *self) {
    gsr_cursor_deinit(&self->x11_cursor);
    self->x11_cursor_display = NULL;

    if(self->kms_client_initialized) {
        gsr_capture_deps_cleanup_kms_fds(self);
        gsr_kms_client_deinit(&self->kms_client);
        self->kms_client_initialized = false;
    }

    gsr_kde_night_light_destroy(self->kde_night_light);
    self->kde_night_light = NULL;
    self->kde_night_light_initialized = false;
}

void gsr_capture_deps_cleanup_kms_fds(gsr_capture_deps *self) {
    for(int i = 0; i < self->kms_response.num_items; ++i) {
        for(int j = 0; j < self->kms_response.items[i].num_dma_bufs; ++j) {
            gsr_kms_response_dma_buf *dma_buf = &self->kms_response.items[i].dma_buf[j];
            if(dma_buf->fd > 0) {
                close(dma_buf->fd);
                dma_buf->fd = 0;
            }
        }
        self->kms_response.items[i].num_dma_bufs = 0;
    }
    self->kms_response.num_items = 0;
}

void gsr_capture_deps_update_kms(gsr_capture_deps *self) {
    if(!self->kms_client_initialized)
        return;

    if(gsr_kms_client_get_kms(&self->kms_client, &self->kms_response) != 0)
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to get kms, error: %d (%s)", self->kms_response.result, self->kms_response.err_msg);
}

static int validate_monitor_get_valid(const gsr_egl *egl, const char *window, char *output_name, size_t output_name_size) {
    const bool is_x11 = gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_X11;
    const gsr_connection_type connection_type = is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_DRM;
    const bool capture_use_drm = monitor_capture_use_drm(egl->window, egl->gpu_info.vendor);

    snprintf(output_name, output_name_size, "%s", window);
    if(strcmp(output_name, "screen") == 0) {
        first_output_callback_userdata data;
        data.output_name = NULL;
        for_each_active_monitor_output(egl->window, egl->card_path, connection_type, get_first_output_callback, &data);

        if(data.output_name) {
            snprintf(output_name, output_name_size, "%s", data.output_name);
            free(data.output_name);
        } else {
            gsr_log(GSR_LOG_LEVEL_ERROR, "no usable output found");
            return GSR_ERROR_MONITOR_NOT_FOUND;
        }
    } else if(capture_use_drm || (strcmp(output_name, "screen-direct") != 0 && strcmp(output_name, "screen-direct-force") != 0)) {
        gsr_monitor gmon;
        if(!get_monitor_by_name(egl, connection_type, output_name, &gmon)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "display \"%s\" not found, expected one of:", output_name);
            fprintf(stderr, "  \"screen\"\n");
            if(!capture_use_drm)
                fprintf(stderr, "  \"screen-direct\"\n");

            monitor_output_callback_userdata userdata;
            userdata.window = egl->window;
            for_each_active_monitor_output(egl->window, egl->card_path, connection_type, monitor_output_callback_print, &userdata);
            return GSR_ERROR_MONITOR_NOT_FOUND;
        }
    }

    return GSR_ERROR_OK;
}

static bool get_monitor_by_region_center(const gsr_egl *egl, vec2i region_position, vec2i region_size, char *output_name, size_t output_name_size, vec2i *monitor_pos, vec2i *monitor_size, double *monitor_scale_inverted) {
    const bool is_x11 = gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_X11;
    const gsr_connection_type connection_type = is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_WAYLAND;

    monitor_by_position_callback_userdata data;
    data.window = egl->window;
    data.position = (vec2i){ region_position.x + region_size.x / 2, region_position.y + region_size.y / 2 };
    data.output_name = NULL;
    data.monitor_pos = (vec2i){0, 0};
    data.monitor_size = (vec2i){0, 0};
    data.monitor_scale_inverted = 1.0;
    for_each_active_monitor_output(egl->window, egl->card_path, connection_type, get_monitor_by_position_callback, &data);

    output_name[0] = '\0';
    if(data.output_name) {
        snprintf(output_name, output_name_size, "%s", data.output_name);
        free(data.output_name);
    }
    *monitor_pos = data.monitor_pos;
    *monitor_size = data.monitor_size;
    *monitor_scale_inverted = data.monitor_scale_inverted;
    return output_name[0] != '\0';
}

static int region_get_data(gsr_egl *egl, vec2i *region_size, vec2i *region_position, char *output_name, size_t output_name_size) {
    vec2i monitor_pos = {0, 0};
    vec2i monitor_size = {0, 0};
    double monitor_scale_inverted = 1.0;
    if(!get_monitor_by_region_center(egl, *region_position, *region_size, output_name, output_name_size, &monitor_pos, &monitor_size, &monitor_scale_inverted)) {
        const bool is_x11 = gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_X11;
        const gsr_connection_type connection_type = is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_WAYLAND;
        gsr_log(GSR_LOG_LEVEL_ERROR, "the region %dx%d+%d+%d doesn't match any monitor. Available monitors and their regions:", region_size->x, region_size->y, region_position->x, region_position->y);
        for_each_active_monitor_output(egl->window, egl->card_path, connection_type, monitor_output_callback_print_region, NULL);
        return GSR_ERROR_MONITOR_NOT_FOUND;
    }

    /* Capture whole monitor when region size is set to 0x0 */
    if(region_size->x == 0 && region_size->y == 0) {
        region_position->x = 0;
        region_position->y = 0;
    } else {
        region_position->x -= monitor_pos.x;
        region_position->y -= monitor_pos.y;
        /* Match drm plane coordinate space (1x scaling) to wayland coordinate space (which may have scaling set by user) */
        region_position->x *= monitor_scale_inverted;
        region_position->y *= monitor_scale_inverted;

        region_size->x *= monitor_scale_inverted;
        region_size->y *= monitor_scale_inverted;
    }

    return GSR_ERROR_OK;
}

static gsr_capture* create_monitor_capture(const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_deps *deps, const gsr_capture_source *capture_source, bool prefer_ximage, int *error) {
    *error = GSR_ERROR_OK;

    if(gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_X11 && prefer_ximage) {
        gsr_capture_ximage_params ximage_params;
        memset(&ximage_params, 0, sizeof(ximage_params));
        ximage_params.egl = egl;
        ximage_params.cursor = &deps->x11_cursor;
        ximage_params.display_to_capture = capture_source->name;
        ximage_params.record_cursor = settings->record_cursor;
        ximage_params.output_resolution = settings->output_resolution;
        ximage_params.region_size = capture_source->region_size;
        ximage_params.region_position = capture_source->region_pos;
        return gsr_capture_ximage_create(&ximage_params);
    }

    if(monitor_capture_use_drm(egl->window, egl->gpu_info.vendor)) {
        if(!deps->kms_client_initialized) {
            deps->kms_client_initialized = true;
            const int kms_init_res = gsr_kms_client_init(&deps->kms_client, egl->card_path);
            if(kms_init_res != 0) {
                *error = kms_init_res < 0 ? GSR_ERROR_GENERIC : -kms_init_res;
                return NULL;
            }
        }

        if(!deps->kde_night_light_initialized && gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_WAYLAND) {
            deps->kde_night_light_initialized = true;
            deps->kde_night_light = gsr_kde_night_light_create();
        }

        gsr_capture_kms_params kms_params;
        memset(&kms_params, 0, sizeof(kms_params));
        kms_params.egl = egl;
        kms_params.x11_cursor = &deps->x11_cursor;
        kms_params.kms_response = &deps->kms_response;
        kms_params.kde_night_light = deps->kde_night_light;
        kms_params.display_to_capture = capture_source->name;
        kms_params.record_cursor = settings->record_cursor;
        kms_params.hdr = video_codec_is_hdr(settings->video_codec);
        kms_params.fps = settings->fps;
        kms_params.output_resolution = settings->output_resolution;
        kms_params.region_size = capture_source->region_size;
        kms_params.region_position = capture_source->region_pos;
        return gsr_capture_kms_create(&kms_params);
    } else {
        const char *capture_source_real = capture_source->name;
        const bool direct_capture = strcmp(capture_source->name, "screen-direct") == 0 || strcmp(capture_source->name, "screen-direct-force") == 0;
        if(direct_capture) {
            capture_source_real = "screen";
            gsr_log(GSR_LOG_LEVEL_WARNING, "%s capture option is not recommended unless you use G-SYNC as Nvidia has driver issues that can cause your system or games to freeze/crash.", capture_source->name);
        }

        gsr_capture_nvfbc_params nvfbc_params;
        memset(&nvfbc_params, 0, sizeof(nvfbc_params));
        nvfbc_params.egl = egl;
        nvfbc_params.display_to_capture = capture_source_real;
        nvfbc_params.fps = settings->fps;
        nvfbc_params.direct_capture = direct_capture;
        nvfbc_params.record_cursor = settings->record_cursor;
        nvfbc_params.output_resolution = settings->output_resolution;
        nvfbc_params.region_size = capture_source->region_size;
        nvfbc_params.region_position = capture_source->region_pos;
        return gsr_capture_nvfbc_create(&nvfbc_params);
    }
}

static gsr_capture* create_capture_impl(const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_deps *deps, gsr_capture_source *capture_source, bool prefer_ximage, int *error) {
    bool follow_focused = false;
    const bool wayland = gsr_window_get_display_server(egl->window) == GSR_DISPLAY_SERVER_WAYLAND;

    *error = GSR_ERROR_OK;
    gsr_capture *capture = NULL;
    if(capture_source->type == GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW) {
        if(wayland) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "GPU Screen Recorder window capture only works in a pure X11 session. Xwayland is not supported. You can record a monitor instead on wayland");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
        }

        if(settings->output_resolution.x <= 0 || settings->output_resolution.y <= 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid value for option -s '%dx%d' when using -w focused option. expected width and height to be greater than 0", settings->output_resolution.x, settings->output_resolution.y);
            args_parser_print_usage();
            *error = GSR_ERROR_GENERIC;
            return NULL;
        }

        follow_focused = true;
    } else if(capture_source->type == GSR_CAPTURE_SOURCE_TYPE_PORTAL) {
#ifdef GSR_PORTAL
        /* Desktop portal capture on x11 doesn't seem to be hardware accelerated */
        if(!wayland) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "desktop portal capture is not supported on X11");
            *error = GSR_ERROR_GENERIC;
            return NULL;
        }

        gsr_capture_portal_params portal_params;
        memset(&portal_params, 0, sizeof(portal_params));
        portal_params.egl = egl;
        portal_params.record_cursor = settings->record_cursor;
        portal_params.restore_portal_session = settings->restore_portal_session;
        portal_params.portal_session_token_filepath = settings->portal_session_token_filepath;
        portal_params.output_resolution = settings->output_resolution;
        capture = gsr_capture_portal_create(&portal_params);
        if(!capture) {
            *error = GSR_ERROR_GENERIC;
            return NULL;
        }
#else
        gsr_log(GSR_LOG_LEVEL_ERROR, "option '-w portal' used but GPU Screen Recorder was compiled without desktop portal support. Please recompile GPU Screen recorder with the -Dportal=true option");
        *error = GSR_ERROR_UNSUPPORTED;
        return NULL;
#endif
    } else if(capture_source->type == GSR_CAPTURE_SOURCE_TYPE_REGION) {
        const int region_result = region_get_data(egl, &capture_source->region_size, &capture_source->region_pos, capture_source->name, sizeof(capture_source->name));
        if(region_result != GSR_ERROR_OK) {
            *error = region_result;
            return NULL;
        }

        capture = create_monitor_capture(settings, egl, deps, capture_source, prefer_ximage, error);
        if(!capture) {
            if(*error == GSR_ERROR_OK)
                *error = GSR_ERROR_GENERIC;
            return NULL;
        }
    } else if(capture_source->type == GSR_CAPTURE_SOURCE_TYPE_MONITOR) {
        char monitor_name[GSR_CAPTURE_SOURCE_NAME_MAX_SIZE];
        const int monitor_result = validate_monitor_get_valid(egl, capture_source->name, monitor_name, sizeof(monitor_name));
        if(monitor_result != GSR_ERROR_OK) {
            *error = monitor_result;
            return NULL;
        }
        snprintf(capture_source->name, sizeof(capture_source->name), "%s", monitor_name);

        capture = create_monitor_capture(settings, egl, deps, capture_source, prefer_ximage, error);
        if(!capture) {
            if(*error == GSR_ERROR_OK)
                *error = GSR_ERROR_GENERIC;
            return NULL;
        }
    } else if(capture_source->type == GSR_CAPTURE_SOURCE_TYPE_V4L2) {
        gsr_capture_v4l2_params v4l2_params;
        memset(&v4l2_params, 0, sizeof(v4l2_params));
        v4l2_params.egl = egl;
        v4l2_params.output_resolution = settings->output_resolution;
        v4l2_params.device_path = capture_source->name;
        v4l2_params.pixfmt = capture_source->v4l2_pixfmt;
        v4l2_params.camera_fps = capture_source->camera_fps;
        v4l2_params.camera_resolution.width = capture_source->camera_resolution.x;
        v4l2_params.camera_resolution.height = capture_source->camera_resolution.y;
        capture = gsr_capture_v4l2_create(&v4l2_params);
        if(!capture) {
            *error = GSR_ERROR_GENERIC;
            return NULL;
        }
    } else {
        if(wayland) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "GPU Screen Recorder window capture only works in a pure X11 session. Xwayland is not supported. You can record a monitor instead on wayland or use -w portal option which supports window capture if your wayland compositor supports window capture");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
        }
    }

    if(!capture) {
        gsr_capture_xcomposite_params xcomposite_params;
        memset(&xcomposite_params, 0, sizeof(xcomposite_params));
        xcomposite_params.egl = egl;
        xcomposite_params.cursor = &deps->x11_cursor;
        xcomposite_params.window = capture_source->window_id;
        xcomposite_params.follow_focused = follow_focused;
        xcomposite_params.record_cursor = settings->record_cursor;
        xcomposite_params.output_resolution = settings->output_resolution;
        capture = gsr_capture_xcomposite_create(&xcomposite_params);
        if(!capture) {
            *error = GSR_ERROR_GENERIC;
            return NULL;
        }
    }

    return capture;
}

/* The size and position of a capture source can be relative to the video size, in which case it can't be used to calculate the video size */
static bool video_source_size_is_relative_to_video_size(const gsr_video_source *self) {
    return self->capture_source->pos.x_type == VVEC2I_TYPE_SCALAR || self->capture_source->pos.y_type == VVEC2I_TYPE_SCALAR
        || (self->capture_source->size.x_type == VVEC2I_TYPE_SCALAR && self->capture_source->size.x != 100)
        || (self->capture_source->size.y_type == VVEC2I_TYPE_SCALAR && self->capture_source->size.y != 100);
}

/* The video size is the area that all capture sources cover */
static vec2i video_sources_get_total_size(const gsr_video_sources *self) {
    vec2i start_pos = {99999, 99999};
    vec2i end_pos = {-99999, -99999};
    for(size_t i = 0; i < self->num_items; ++i) {
        const gsr_video_source *video_source = &self->items[i];
        if(video_source_size_is_relative_to_video_size(video_source))
            continue;

        const vec2i video_source_start_pos = {
            video_source->capture_source->pos.x,
            video_source->capture_source->pos.y
        };

        const vec2i video_source_end_pos = {
            video_source_start_pos.x + video_source->metadata.video_size.x,
            video_source_start_pos.y + video_source->metadata.video_size.y
        };

        if(video_source_start_pos.x < start_pos.x)
            start_pos.x = video_source_start_pos.x;
        if(video_source_start_pos.y < start_pos.y)
            start_pos.y = video_source_start_pos.y;

        if(video_source_end_pos.x > end_pos.x)
            end_pos.x = video_source_end_pos.x;
        if(video_source_end_pos.y > end_pos.y)
            end_pos.y = video_source_end_pos.y;
    }

    /* Every capture source is relative to the video size, so use the size of the capture sources themselves as the video size */
    if(end_pos.x <= start_pos.x || end_pos.y <= start_pos.y) {
        start_pos = (vec2i){0, 0};
        end_pos = (vec2i){0, 0};
        for(size_t i = 0; i < self->num_items; ++i) {
            const vec2i capture_size = self->items[i].metadata.video_size;
            if(capture_size.x > end_pos.x)
                end_pos.x = capture_size.x;
            if(capture_size.y > end_pos.y)
                end_pos.y = capture_size.y;
        }
    }

    vec2i video_size = { end_pos.x - start_pos.x, end_pos.y - start_pos.y };
    if(video_size.x < 0)
        video_size.x = 0;
    if(video_size.y < 0)
        video_size.y = 0;

    return video_size;
}

int gsr_video_sources_create(gsr_video_sources *self, const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_deps *deps, bool prefer_ximage, gsr_capture_sources *capture_sources, vec2i *video_size) {
    memset(self, 0, sizeof(*self));
    if(capture_sources->num_items == 0)
        return GSR_ERROR_GENERIC;

    self->items = calloc(capture_sources->num_items, sizeof(gsr_video_source));
    if(!self->items) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate video sources");
        return GSR_ERROR_GENERIC;
    }

    for(size_t i = 0; i < capture_sources->num_items; ++i) {
        gsr_capture_source *capture_source = &capture_sources->items[i];
        gsr_video_source *video_source = &self->items[i];

        memset(&video_source->metadata, 0, sizeof(video_source->metadata));
        video_source->metadata.fps = settings->fps;
        video_source->metadata.halign = capture_source->halign;
        video_source->metadata.valign = capture_source->valign;
        video_source->metadata.flip = (gsr_flip)capture_source->flip;
        video_source->capture_source = capture_source;

        int error = GSR_ERROR_OK;
        video_source->capture = create_capture_impl(settings, egl, deps, capture_source, prefer_ximage, &error);
        if(!video_source->capture) {
            gsr_video_sources_deinit(self);
            return error;
        }

        ++self->num_items;
    }

    for(size_t i = 0; i < self->num_items; ++i) {
        const int capture_result = gsr_capture_start(self->items[i].capture, &self->items[i].metadata);
        if(capture_result != 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_start failed");
            gsr_video_sources_deinit(self);
            return -capture_result;
        }
    }

    *video_size = video_sources_get_total_size(self);

    for(size_t i = 0; i < self->num_items; ++i) {
        self->items[i].metadata.video_size = *video_size;
    }

    return GSR_ERROR_OK;
}

void gsr_video_sources_update_with_real_video_size(gsr_video_sources *self, vec2i video_size) {
    for(size_t i = 0; i < self->num_items; ++i) {
        gsr_video_source *video_source = &self->items[i];
        const gsr_capture_source *capture_source = video_source->capture_source;

        video_source->metadata.recording_size = video_source->metadata.video_size;
        /* TODO: What if this updated resolution is above max resolution? */
        video_source->metadata.video_size = video_size;

        if(capture_source->pos.x != 0 || capture_source->pos.y != 0) {
            video_source->metadata.position.x = capture_source->pos.x;
            video_source->metadata.position.y = capture_source->pos.y;

            if(capture_source->pos.x_type == VVEC2I_TYPE_SCALAR)
                video_source->metadata.position.x = video_source->metadata.video_size.x * ((double)video_source->metadata.position.x / 100.0);

            if(capture_source->pos.y_type == VVEC2I_TYPE_SCALAR)
                video_source->metadata.position.y = video_source->metadata.video_size.y * ((double)video_source->metadata.position.y / 100.0);
        }

        if(capture_source->size.x != 0 || capture_source->size.y != 0) {
            video_source->metadata.recording_size.x = capture_source->size.x;
            video_source->metadata.recording_size.y = capture_source->size.y;

            if(capture_source->size.x_type == VVEC2I_TYPE_SCALAR)
                video_source->metadata.recording_size.x = video_source->metadata.video_size.x * ((double)video_source->metadata.recording_size.x / 100.0);

            if(capture_source->size.y_type == VVEC2I_TYPE_SCALAR)
                video_source->metadata.recording_size.y = video_source->metadata.video_size.y * ((double)video_source->metadata.recording_size.y / 100.0);
        }
    }
}

bool gsr_video_sources_uses_external_image(const gsr_video_sources *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(gsr_capture_uses_external_image(self->items[i].capture))
            return true;
    }
    return false;
}

void gsr_video_sources_deinit(gsr_video_sources *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].capture) {
            gsr_capture_destroy(self->items[i].capture);
            self->items[i].capture = NULL;
        }
    }

    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
}
