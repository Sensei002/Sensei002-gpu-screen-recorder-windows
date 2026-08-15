#include "../../include/capture/portal.h"
#include "../../include/log.h"
#include "../../include/color_conversion.h"
#include "../../include/egl.h"
#include "../../include/utils.h"
#include "../../include/dbus.h"
#include "../../include/pipewire_video.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>

#define PORTAL_CAPTURE_CANCELED_BY_USER_EXIT_CODE 60

typedef enum {
    PORTAL_CAPTURE_SETUP_IDLE,
    PORTAL_CAPTURE_SETUP_IN_PROGRESS,
    PORTAL_CAPTURE_SETUP_FINISHED,
    PORTAL_CAPTURE_SETUP_FAILED
} gsr_portal_capture_setup_state;

typedef struct {
    gsr_capture_portal_params params;

    gsr_texture_map texture_map;

    gsr_dbus dbus;
    char *session_handle;

    gsr_pipewire_video pipewire;
    vec2i capture_size;

    gsr_map_texture_output pipewire_data;

    bool should_stop;
    bool stop_is_error;
    bool do_capture;
} gsr_capture_portal;

static void gsr_capture_portal_cleanup_plane_fds(gsr_capture_portal *self) {
    for(int i = 0; i < self->pipewire_data.num_dmabuf_data; ++i) {
        if(self->pipewire_data.dmabuf_data[i].fd > 0) {
            close(self->pipewire_data.dmabuf_data[i].fd);
            self->pipewire_data.dmabuf_data[i].fd = 0;
        }
    }
    self->pipewire_data.num_dmabuf_data = 0;
}

static void gsr_capture_portal_stop(gsr_capture_portal *self) {
    if(self->texture_map.texture_id) {
        self->params.egl->glDeleteTextures(1, &self->texture_map.texture_id);
        self->texture_map.texture_id = 0;
    }

    if(self->texture_map.external_texture_id) {
        self->params.egl->glDeleteTextures(1, &self->texture_map.external_texture_id);
        self->texture_map.external_texture_id = 0;
    }

    if(self->texture_map.cursor_texture_id) {
        self->params.egl->glDeleteTextures(1, &self->texture_map.cursor_texture_id);
        self->texture_map.cursor_texture_id = 0;
    }

    gsr_capture_portal_cleanup_plane_fds(self);
    gsr_pipewire_video_deinit(&self->pipewire);
    gsr_dbus_deinit(&self->dbus);
}

static void gsr_capture_portal_create_input_textures(gsr_capture_portal *self) {
    self->params.egl->glGenTextures(1, &self->texture_map.texture_id);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->texture_map.texture_id);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);

    self->params.egl->glGenTextures(1, &self->texture_map.external_texture_id);
    self->params.egl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, self->texture_map.external_texture_id);
    self->params.egl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    self->params.egl->glGenTextures(1, &self->texture_map.cursor_texture_id);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->texture_map.cursor_texture_id);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);
}

static void get_default_gpu_screen_recorder_restore_token_path(char *buffer, size_t buffer_size) {
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if(xdg_config_home) {
        snprintf(buffer, buffer_size, "%s/gpu-screen-recorder/restore_token", xdg_config_home);
    } else {
        const char *home = getenv("HOME");
        if(!home)
            home = "/tmp";
        snprintf(buffer, buffer_size, "%s/.config/gpu-screen-recorder/restore_token", home);
    }
}

static bool create_directory_to_file(const char *filepath) {
    char dir[PATH_MAX];
    dir[0] = '\0';

    const char *split = strrchr(filepath, '/');
    if(!split) /* Assuming it's the current directory (for example if filepath is "restore_token"), which doesn't need to be created */
        return true;

    snprintf(dir, sizeof(dir), "%.*s", (int)(split - filepath), filepath);
    if(create_directory_recursive(dir) != 0) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_capture_portal_save_restore_token: failed to create directory (%s) for restore token", dir);
        return false;
    }
    return true;
}

static void gsr_capture_portal_save_restore_token(const char *restore_token, const char *portal_session_token_filepath) {
    char restore_token_path[PATH_MAX];
    restore_token_path[0] = '\0';
    if(portal_session_token_filepath)
        snprintf(restore_token_path, sizeof(restore_token_path), "%s", portal_session_token_filepath);
    else
        get_default_gpu_screen_recorder_restore_token_path(restore_token_path, sizeof(restore_token_path));

    if(!create_directory_to_file(restore_token_path))
        return;

    FILE *f = fopen(restore_token_path, "wb");
    if(!f) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_capture_portal_save_restore_token: failed to create restore token file (%s)", restore_token_path);
        return;
    }

    const int restore_token_len = strlen(restore_token);
    if((long)fwrite(restore_token, 1, restore_token_len, f) != restore_token_len) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_capture_portal_save_restore_token: failed to write restore token to file (%s)", restore_token_path);
        fclose(f);
        return;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_save_restore_token: saved restore token to cache (%s)", restore_token);
    fclose(f);
}

static void gsr_capture_portal_get_restore_token_from_cache(char *buffer, size_t buffer_size, const char *portal_session_token_filepath) {
    assert(buffer_size > 0);
    buffer[0] = '\0';

    char restore_token_path[PATH_MAX];
    restore_token_path[0] = '\0';
    if(portal_session_token_filepath)
        snprintf(restore_token_path, sizeof(restore_token_path), "%s", portal_session_token_filepath);
    else
        get_default_gpu_screen_recorder_restore_token_path(restore_token_path, sizeof(restore_token_path));

    FILE *f = fopen(restore_token_path, "rb");
    if(!f) {
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_get_restore_token_from_cache: no restore token found in cache or failed to load (%s)", restore_token_path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if(file_size > 0 && file_size < 1024 && file_size < (long)buffer_size && (long)fread(buffer, 1, file_size, f) != file_size) {
        buffer[0] = '\0';
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_capture_portal_get_restore_token_from_cache: failed to read restore token (%s)", restore_token_path);
        fclose(f);
        return;
    }

    if(file_size > 0 && file_size < (long)buffer_size)
        buffer[file_size] = '\0';

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_get_restore_token_from_cache: read cached restore token (%s)", buffer);
    fclose(f);
}

static int gsr_capture_portal_setup_dbus(gsr_capture_portal *self, int *pipewire_fd, uint32_t *pipewire_node) {
    *pipewire_fd = 0;
    *pipewire_node = 0;
    int response_status = 0;

    char restore_token[1024];
    restore_token[0] = '\0';
    if(self->params.restore_portal_session)
        gsr_capture_portal_get_restore_token_from_cache(restore_token, sizeof(restore_token), self->params.portal_session_token_filepath);

    if(!gsr_dbus_init(&self->dbus, restore_token))
        return -1;

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup_dbus: CreateSession");
    response_status = gsr_dbus_screencast_create_session(&self->dbus, &self->session_handle);
    if(response_status != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup_dbus: CreateSession failed");
        return response_status;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup_dbus: SelectSources");
    response_status = gsr_dbus_screencast_select_sources(&self->dbus, self->session_handle, GSR_PORTAL_CAPTURE_TYPE_ALL, self->params.record_cursor ? GSR_PORTAL_CURSOR_MODE_METADATA : GSR_PORTAL_CURSOR_MODE_HIDDEN);
    if(response_status != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup_dbus: SelectSources failed");
        return response_status;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup_dbus: Start");
    response_status = gsr_dbus_screencast_start(&self->dbus, self->session_handle, pipewire_node);
    if(response_status != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup_dbus: Start failed");
        return response_status;
    }

    const char *screencast_restore_token = gsr_dbus_screencast_get_restore_token(&self->dbus);
    if(screencast_restore_token)
        gsr_capture_portal_save_restore_token(screencast_restore_token, self->params.portal_session_token_filepath);

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup_dbus: OpenPipeWireRemote");
    if(!gsr_dbus_screencast_open_pipewire_remote(&self->dbus, self->session_handle, pipewire_fd)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup_dbus: OpenPipeWireRemote failed");
        return -1;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup_dbus: desktop portal setup finished");
    return 0;
}

static bool gsr_capture_portal_get_frame_dimensions(gsr_capture_portal *self) {
    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_start: waiting for pipewire negotiation");

    const double start_time = clock_get_monotonic_seconds();
    while(clock_get_monotonic_seconds() - start_time < 5.0) {
        if(gsr_pipewire_video_map_texture(&self->pipewire, self->texture_map, &self->pipewire_data)) {
            self->capture_size.x = self->pipewire_data.region.width;
            self->capture_size.y = self->pipewire_data.region.height;
            gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_start: pipewire negotiation finished");
            return true;
        }
        usleep(30 * 1000); /* 30 milliseconds */
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_start: timed out waiting for pipewire negotiation (5 seconds)");
    return false;
}

static int gsr_capture_portal_setup(gsr_capture_portal *self, int fps) {
    gsr_capture_portal_create_input_textures(self);

    int pipewire_fd = 0;
    uint32_t pipewire_node = 0;
    const int response_status = gsr_capture_portal_setup_dbus(self, &pipewire_fd, &pipewire_node);
    if(response_status != 0) {
        // Response status values:
        // 0: Success, the request is carried out
        // 1: The user cancelled the interaction
        // 2: The user interaction was ended in some other way
        // Response status value 2 happens usually if there was some kind of error in the desktop portal on the system
        if(response_status == 2) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup: desktop portal capture failed. Either you Wayland compositor doesn't support desktop portal capture or it's incorrectly setup on your system");
            return 50;
        } else if(response_status == 1) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup: desktop portal capture failed. It seems like desktop portal capture was canceled by the user.");
            return PORTAL_CAPTURE_CANCELED_BY_USER_EXIT_CODE;
        } else {
            return -1;
        }
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup: setting up pipewire");
    /* TODO: support hdr when pipewire supports it */
    /* gsr_pipewire closes the pipewire fd, even on failure */
    if(!gsr_pipewire_video_init(&self->pipewire, pipewire_fd, pipewire_node, fps, self->params.record_cursor, self->params.egl)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_setup: failed to setup pipewire with fd: %d, node: %" PRIu32, pipewire_fd, pipewire_node);
        return -1;
    }
    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_setup: pipewire setup finished");

    if(!gsr_capture_portal_get_frame_dimensions(self))
        return -1;

    return 0;
}

static int gsr_capture_portal_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    gsr_capture_portal *self = cap->priv;

    const int result = gsr_capture_portal_setup(self, capture_metadata->fps);
    if(result != 0) {
        gsr_capture_portal_stop(self);
        return result;
    }

    if(self->params.output_resolution.x == 0 && self->params.output_resolution.y == 0) {
        capture_metadata->video_size = self->capture_size;
    } else {
        self->params.output_resolution = scale_keep_aspect_ratio(self->capture_size, self->params.output_resolution);
        capture_metadata->video_size = self->params.output_resolution;
    }

    return 0;
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static bool gsr_capture_portal_capture_has_synchronous_task(gsr_capture *cap) {
    gsr_capture_portal *self = cap->priv;
    return gsr_pipewire_video_should_restart(&self->pipewire);
}

static bool fourcc_has_alpha(uint32_t fourcc) {
    const uint8_t *p = (const uint8_t*)&fourcc;
    for(int i = 0; i < 4; ++i) {
        if(p[i] == 'A')
            return true;
    }
    return false;
}

static void gsr_capture_portal_pre_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_portal *self = cap->priv;
    self->do_capture = false;

    if(self->should_stop)
        return;

    if(gsr_pipewire_video_should_restart(&self->pipewire)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_portal_pre_capture: pipewire capture was paused, trying to start capture again");
        gsr_capture_portal_stop(self);
        const int result = gsr_capture_portal_setup(self, capture_metadata->fps);
        if(result != 0) {
            self->stop_is_error = result != PORTAL_CAPTURE_CANCELED_BY_USER_EXIT_CODE;
            self->should_stop = true;
        }
        return;
    }

    /* TODO: Handle formats other than RGB(A) */
    if(self->pipewire_data.num_dmabuf_data == 0) {
        if(gsr_pipewire_video_map_texture(&self->pipewire, self->texture_map, &self->pipewire_data)) {
            if(self->pipewire_data.region.width != self->capture_size.x || self->pipewire_data.region.height != self->capture_size.y) {
                self->capture_size.x = self->pipewire_data.region.width;
                self->capture_size.y = self->pipewire_data.region.height;
                color_conversion->schedule_clear = true;
            }
        } else if(self->pipewire_data.texture_width == 0 || !gsr_pipewire_video_update_cursor(&self->pipewire, self->texture_map, &self->pipewire_data.cursor_region)) {
            /* Theres no new video frame to capture and the cursor hasn't changed (which only happens in cursor metadata mode).
               If the cursor has changed then the latest video frame is redrawn with the new cursor state, otherwise nothing is captured.
               This is needed on gnome which doesn't send new video frames when capturing a window and only the cursor moves. */
            return;
        }
    }

    const bool fourcc_alpha = fourcc_has_alpha(self->pipewire_data.fourcc);
    if(fourcc_alpha)
        color_conversion->schedule_clear = true;

    self->do_capture = true;
}

static int gsr_capture_portal_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    (void)color_conversion;
    gsr_capture_portal *self = cap->priv;

    if(self->should_stop || !self->do_capture)
        return -1;

    const vec2i output_size = scale_keep_aspect_ratio(self->capture_size, capture_metadata->recording_size);
    const vec2i target_pos = gsr_capture_get_target_position(output_size, capture_metadata);

    const vec2i actual_texture_size = {self->pipewire_data.texture_width, self->pipewire_data.texture_height};

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    // TODO: Handle region crop

    gsr_color_conversion_draw(color_conversion, self->pipewire_data.using_external_image ? self->texture_map.external_texture_id : self->texture_map.texture_id,
       target_pos, output_size,
       (vec2i){self->pipewire_data.region.x, self->pipewire_data.region.y}, (vec2i){self->pipewire_data.region.width, self->pipewire_data.region.height}, actual_texture_size,
       gsr_monitor_rotation_to_rotation(self->pipewire_data.rotation), capture_metadata->flip, GSR_SOURCE_COLOR_RGB, self->pipewire_data.using_external_image);

    if(self->params.record_cursor && self->texture_map.cursor_texture_id > 0 && self->pipewire_data.cursor_region.width > 0) {
        const vec2d scale = {
            self->capture_size.x == 0 ? 0 : (double)output_size.x / (double)self->capture_size.x,
            self->capture_size.y == 0 ? 0 : (double)output_size.y / (double)self->capture_size.y
        };

        /* The cursor position is relative to the video buffer, remove the crop offset to make it relative to the visible video content */
        const vec2i cursor_pos = {
            target_pos.x + ((self->pipewire_data.cursor_region.x - self->pipewire_data.region.x) * scale.x),
            target_pos.y + ((self->pipewire_data.cursor_region.y - self->pipewire_data.region.y) * scale.y)
        };

        self->params.egl->glEnable(GL_SCISSOR_TEST);
        self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

        gsr_color_conversion_draw(color_conversion, self->texture_map.cursor_texture_id,
            (vec2i){cursor_pos.x, cursor_pos.y},
            (vec2i){self->pipewire_data.cursor_region.width * scale.x, self->pipewire_data.cursor_region.height * scale.y},
            (vec2i){0, 0},
            (vec2i){self->pipewire_data.cursor_region.width, self->pipewire_data.cursor_region.height},
            (vec2i){self->pipewire_data.cursor_region.width, self->pipewire_data.cursor_region.height},
            gsr_monitor_rotation_to_rotation(self->pipewire_data.rotation), capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

        self->params.egl->glDisable(GL_SCISSOR_TEST);
    }

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    gsr_capture_portal_cleanup_plane_fds(self);

    return 0;
}

static bool gsr_capture_portal_uses_external_image(gsr_capture *cap) {
    (void)cap;
    return true;
}

static bool gsr_capture_portal_should_stop(gsr_capture *cap, bool *err) {
    gsr_capture_portal *self = cap->priv;
    if(err)
        *err = self->stop_is_error;
    return self->should_stop;
}

static bool gsr_capture_portal_is_damaged(gsr_capture *cap) {
    gsr_capture_portal *self = cap->priv;
    return gsr_pipewire_video_is_damaged(&self->pipewire);
}

static void gsr_capture_portal_clear_damage(gsr_capture *cap) {
    gsr_capture_portal *self = cap->priv;
    gsr_pipewire_video_clear_damage(&self->pipewire);
}

static void gsr_capture_portal_destroy(gsr_capture *cap) {
    gsr_capture_portal *self = cap->priv;
    if(cap->priv) {
        gsr_capture_portal_stop(self);
        free(cap->priv);
        cap->priv = NULL;
    }
    free(cap);
}

gsr_capture* gsr_capture_portal_create(const gsr_capture_portal_params *params) {
    if(!params) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_portal_create params is NULL");
        return NULL;
    }

    gsr_capture *cap = calloc(1, sizeof(gsr_capture));
    if(!cap)
        return NULL;

    gsr_capture_portal *cap_portal = calloc(1, sizeof(gsr_capture_portal));
    if(!cap_portal) {
        free(cap);
        return NULL;
    }

    cap_portal->params = *params;
    
    *cap = (gsr_capture) {
        .start = gsr_capture_portal_start,
        .tick = NULL,
        .should_stop = gsr_capture_portal_should_stop,
        .capture_has_synchronous_task = gsr_capture_portal_capture_has_synchronous_task,
        .pre_capture = gsr_capture_portal_pre_capture,
        .capture = gsr_capture_portal_capture,
        .uses_external_image = gsr_capture_portal_uses_external_image,
        .is_damaged = gsr_capture_portal_is_damaged,
        .clear_damage = gsr_capture_portal_clear_damage,
        .destroy = gsr_capture_portal_destroy,
        .priv = cap_portal
    };

    return cap;
}
