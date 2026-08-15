#ifndef GSR_RECORDER_CAPTURE_SETUP_H
#define GSR_RECORDER_CAPTURE_SETUP_H

#include <stdbool.h>
#include <stddef.h>
#include "../egl.h"
#include "../cursor.h"
#include "../kde_night_light.h"
#include "../capture/capture.h"
#include "../../kms/client/kms_client.h"
#include "capture_source.h"
#include "settings.h"

#include <X11/Xlib.h>

typedef struct {
    gsr_kms_client kms_client;
    bool kms_client_initialized;
    gsr_kms_response kms_response;
    gsr_kde_night_light *kde_night_light;
    bool kde_night_light_initialized;
    gsr_cursor x11_cursor;
    Display *x11_cursor_display;
} gsr_capture_deps;

typedef struct {
    gsr_capture *capture;
    gsr_capture_metadata metadata;
    gsr_capture_source *capture_source;
} gsr_video_source;

typedef struct {
    gsr_video_source *items;
    size_t num_items;
} gsr_video_sources;

void gsr_capture_deps_init(gsr_capture_deps *self);
void gsr_capture_deps_init_cursor(gsr_capture_deps *self, gsr_egl *egl, bool record_cursor);
void gsr_capture_deps_deinit(gsr_capture_deps *self);
void gsr_capture_deps_cleanup_kms_fds(gsr_capture_deps *self);
void gsr_capture_deps_update_kms(gsr_capture_deps *self);

/* Returns a |gsr_error| value, or the error returned by gsr_capture_start. |video_size| is set to the size of all capture sources combined */
int gsr_video_sources_create(gsr_video_sources *self, const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_deps *deps, bool prefer_ximage, gsr_capture_sources *capture_sources, vec2i *video_size);
void gsr_video_sources_update_with_real_video_size(gsr_video_sources *self, vec2i video_size);
bool gsr_video_sources_uses_external_image(const gsr_video_sources *self);
void gsr_video_sources_deinit(gsr_video_sources *self);

#endif /* GSR_RECORDER_CAPTURE_SETUP_H */
