/* platform/windows/gsr_capture_setup_win32.c — Windows capture_setup seam
 * (Phase 7, milestone A).
 *
 * Upstream's capture_setup.c builds X11/KMS/NVFBC/V4L2 captures, none of
 * which exist on Windows. This file implements the same header API
 * (recorder/capture_setup.h) over the Phase 5/6 capture backends:
 *
 *   - monitor capture  -> WGC (primary) or DXGI Desktop Duplication
 *     (fallback), chosen by gsr_platform_capture_select_backend() after
 *     probing both;
 *   - window capture   -> WGC window target (DXGI has no window capture);
 *   - region/focused/portal/v4l2 -> GSR_ERROR_UNSUPPORTED with a clear log
 *     (region lands with the CLI wiring; focused needs WGC follow-focus).
 *
 * The upstream recorder.c drives this seam unchanged: gsr_video_sources_create
 * is called from recorder_setup_video_sources, then gsr_capture_start on
 * each source, and the per-frame capture() draws into the unchanged
 * color-conversion pipeline via the backends' Phase 5b GL integration.
 * Cursor and damage are handled natively by the backends (cursor drawn
 * into the frame; damage via is_damaged()/clear_damage()), so
 * gsr_capture_deps_* are no-ops.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3h.
 */
#include "../../upstream/include/recorder/capture_setup.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/recorder/video_codec.h" /* video_codec_is_hdr */
#include "../../upstream/include/log.h"
#include "../../upstream/include/utils.h"
#include "../include/capture.h"
#include "../include/display.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- capture deps (no-ops on Windows: backends handle cursor/damage) ---- */

void gsr_capture_deps_init(gsr_capture_deps *self) {
    memset(self, 0, sizeof(*self));
}

void gsr_capture_deps_init_cursor(gsr_capture_deps *self, gsr_egl *egl, bool record_cursor) {
    /* Windows: WGC/DXGI draw the cursor into the frame natively when
       record_cursor is set (passed to the backends via the options), so
       there is no separate X11 cursor to track. */
    (void)self;
    (void)egl;
    (void)record_cursor;
}

void gsr_capture_deps_deinit(gsr_capture_deps *self) {
    (void)self;
}

void gsr_capture_deps_cleanup_kms_fds(gsr_capture_deps *self) {
    (void)self;
}

void gsr_capture_deps_update_kms(gsr_capture_deps *self) {
    (void)self;
}

/* ---- capture creation ---------------------------------------------------- */

static gsr_capture *create_monitor_capture(const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_source *capture_source, int *error) {
    /* Resolve the monitor name to an HMONITOR. "screen" means the primary
       monitor (upstream semantics). */
    char resolved_name[GSR_CAPTURE_SOURCE_NAME_MAX_SIZE];
    if(strcmp(capture_source->name, "screen") == 0) {
        gsr_platform_monitor *monitors = NULL;
        int monitor_count = 0;
        if(!gsr_platform_display_list_monitors(&monitors, &monitor_count) || monitor_count == 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_setup_win32: no monitors available");
            *error = GSR_ERROR_MONITOR_NOT_FOUND;
            return NULL;
        }
        const gsr_platform_monitor *primary = &monitors[0];
        for(int i = 0; i < monitor_count; ++i) {
            if(monitors[i].is_primary) {
                primary = &monitors[i];
                break;
            }
        }
        snprintf(resolved_name, sizeof(resolved_name), "%s", primary->name);
        free(monitors);
    } else {
        snprintf(resolved_name, sizeof(resolved_name), "%s", capture_source->name);
    }

    void *hmonitor = gsr_platform_display_find_hmonitor(resolved_name);
    if(!hmonitor) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "display \"%s\" not found, expected one of:", resolved_name);
        gsr_platform_monitor *monitors = NULL;
        int monitor_count = 0;
        if(gsr_platform_display_list_monitors(&monitors, &monitor_count)) {
            for(int i = 0; i < monitor_count; ++i)
                fprintf(stderr, "  \"%s\"\n", monitors[i].name);
            free(monitors);
        }
        *error = GSR_ERROR_MONITOR_NOT_FOUND;
        return NULL;
    }

    /* Backend selection: WGC preferred, DXGI fallback (Phase 3 contract). */
    const bool wgc_supported = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_WGC);
    const bool dxgi_supported = gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_DXGI_DUPLICATION);
    const gsr_capture_backend_type backend = gsr_platform_capture_select_backend(wgc_supported, dxgi_supported);
    gsr_log(GSR_LOG_LEVEL_INFO, "recorder: using the %s capture backend for monitor %s",
        gsr_platform_capture_backend_name(backend), resolved_name);

    const bool record_cursor = settings->record_cursor;
    const bool hdr = video_codec_is_hdr(settings->video_codec);

    if(backend == GSR_CAPTURE_BACKEND_WGC) {
        gsr_platform_wgc_target target;
        memset(&target, 0, sizeof(target));
        target.kind = GSR_PLATFORM_WGC_TARGET_MONITOR;
        target.handle = hmonitor;
        snprintf(target.name, sizeof(target.name), "%s", resolved_name);
        gsr_platform_wgc_options options;
        memset(&options, 0, sizeof(options));
        options.cursor = record_cursor;
        options.hdr = hdr;
        options.egl = egl;
        return gsr_platform_capture_wgc_create(&target, &options);
    }

    gsr_platform_dxgi_target target;
    memset(&target, 0, sizeof(target));
    target.hmonitor = hmonitor;
    snprintf(target.name, sizeof(target.name), "%s", resolved_name);
    gsr_platform_dxgi_options options;
    memset(&options, 0, sizeof(options));
    options.cursor = record_cursor;
    options.hdr = hdr;
    options.egl = egl;
    return gsr_platform_capture_dxgi_create(&target, &options);
}

static gsr_capture *create_capture_impl(const gsr_recorder_settings *settings, gsr_egl *egl, gsr_capture_source *capture_source, int *error) {
    *error = GSR_ERROR_OK;

    switch(capture_source->type) {
        case GSR_CAPTURE_SOURCE_TYPE_MONITOR:
            return create_monitor_capture(settings, egl, capture_source, error);
        case GSR_CAPTURE_SOURCE_TYPE_REGION:
            gsr_log(GSR_LOG_LEVEL_ERROR, "region capture (-region) is not supported on Windows yet; record the whole monitor instead");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
        case GSR_CAPTURE_SOURCE_TYPE_WINDOW: {
            /* WGC window capture only (DXGI Desktop Duplication has no
               window mode). The window id is the HWND. */
            if(!gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_WGC)) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "window capture requires Windows Graphics Capture, which is unavailable in this session");
                *error = GSR_ERROR_UNSUPPORTED;
                return NULL;
            }
            gsr_platform_wgc_target target;
            memset(&target, 0, sizeof(target));
            target.kind = GSR_PLATFORM_WGC_TARGET_WINDOW;
            target.handle = (void*)(intptr_t)capture_source->window_id;
            snprintf(target.name, sizeof(target.name), "window %lld", (long long)capture_source->window_id);
            gsr_platform_wgc_options options;
            memset(&options, 0, sizeof(options));
            options.cursor = settings->record_cursor;
            options.hdr = video_codec_is_hdr(settings->video_codec);
            options.egl = egl;
            return gsr_platform_capture_wgc_create(&target, &options);
        }
        case GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW:
            gsr_log(GSR_LOG_LEVEL_ERROR, "focused window capture (-w focused) is not supported on Windows yet");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
        case GSR_CAPTURE_SOURCE_TYPE_PORTAL:
            gsr_log(GSR_LOG_LEVEL_ERROR, "desktop portal capture (-w portal) is not supported on Windows");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
        case GSR_CAPTURE_SOURCE_TYPE_V4L2:
            gsr_log(GSR_LOG_LEVEL_ERROR, "v4l2 capture is not supported on Windows");
            *error = GSR_ERROR_UNSUPPORTED;
            return NULL;
    }

    *error = GSR_ERROR_GENERIC;
    return NULL;
}

/* The size and position of a capture source can be relative to the video size, in which case it can't be used to calculate the video size
   (same logic as upstream capture_setup.c). */
static bool video_source_size_is_relative_to_video_size(const gsr_video_source *self) {
    return self->capture_source->pos.x_type == VVEC2I_TYPE_SCALAR || self->capture_source->pos.y_type == VVEC2I_TYPE_SCALAR
        || (self->capture_source->size.x_type == VVEC2I_TYPE_SCALAR && self->capture_source->size.x != 100)
        || (self->capture_source->size.y_type == VVEC2I_TYPE_SCALAR && self->capture_source->size.y != 100);
}

/* The video size is the area that all capture sources cover (upstream logic). */
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
    (void)deps;
    (void)prefer_ximage;
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

        /* -s output resolution: our backends scale via recording_size
           (scale_keep_aspect_ratio in capture()), so surface it here. */
        if(settings->output_resolution.x > 0 && settings->output_resolution.y > 0)
            video_source->metadata.recording_size = settings->output_resolution;

        int error = GSR_ERROR_OK;
        video_source->capture = create_capture_impl(settings, egl, capture_source, &error);
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
    /* The WGC/DXGI backends import their D3D11 textures as plain
       GL_TEXTURE_2D and draw with external_texture=false (Phase 5b), so
       they report uses_external_image=false and the color conversion does
       NOT load the external-image (OES) shader. Kept as a loop to mirror
       upstream; returns false for every Windows backend today. */
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
