/* platform/windows/gsr_recorder_win32.c — Windows recorder-compat shims
 * (Phase 7, milestone A: wiring the upstream recorder end-to-end).
 *
 * Upstream recorder.c runs unchanged on Windows except for the X11-only
 * pieces it references, which are replaced here:
 *
 *   - gl_create_texture()  — upstream's lives in the X11/DRM utils.c,
 *     which is not built on Windows. Used by the software (libx264) video
 *     encoder and the plugin system. Straight GL, no X11.
 *   - gsr_damage_*          — XDamage/XFixes-based damage tracking
 *     (upstream/src/damage.c, X11-only). On Windows the capture backends
 *     (WGC/DXGI) report damage themselves via is_damaged()/clear_damage(),
 *     so these are no-ops. gsr_damage_init returns false, which keeps the
 *     recorder's use_damage_tracking off even when the display-server stub
 *     reports X11.
 *   - gsr_cursor_*          — XFixes cursor tracking (cursor.c, X11-only).
 *     WGC and DXGI draw the cursor into the frame natively (record_cursor
 *     is passed to the backends), so these are no-ops. The recorder only
 *     calls them when x11_cursor_display is set, which it never is here.
 *   - gsr_window_process_event / gsr_window_get_event_data — window.c is
 *     not built; on Windows there is no X11 event loop, so process_event
 *     returns false (the recorder's event pump is a no-op) and
 *     get_event_data returns NULL.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3h.
 */
#include "../../upstream/include/egl.h"
#include "../../upstream/include/damage.h"
#include "../../upstream/include/cursor.h"
#include "../../upstream/include/window/window.h"
#include "../../upstream/include/sound.h"
#include "../../upstream/include/utils.h"
#include "../../upstream/include/log.h"

#include <stdbool.h>
#include <stddef.h>

/* ---- sound_device_* stubs (upstream sound.c is PulseAudio/PipeWire) -----
 * audio_capture.c (built since Phase 2) references this API, but the
 * Windows build has no audio backend yet (that is Phase 8, WASAPI). The
 * recorder with zero audio tracks never calls these — they exist so the
 * audio_capture object links. All return "unavailable". */

int sound_device_get_by_name(SoundDevice *device, const char *node_name, const char *device_name, const char *description, unsigned int num_channels, unsigned int period_frame_size, gsr_audio_format audio_format) {
    (void)device; (void)node_name; (void)device_name; (void)description;
    (void)num_channels; (void)period_frame_size; (void)audio_format;
    return -1; /* no audio backend on Windows yet (Phase 8: WASAPI) */
}

void sound_device_close(SoundDevice *device) {
    (void)device;
}

void sound_device_flush(SoundDevice *device) {
    (void)device;
}

int sound_device_read_next_chunk(SoundDevice *device, void **buffer, double timeout_sec, double *latency_seconds) {
    (void)device; (void)buffer; (void)timeout_sec; (void)latency_seconds;
    return -1; /* no audio backend on Windows yet (Phase 8: WASAPI) */
}

/* ---- gl_create_texture (upstream utils.c, Windows build) ---------------- */

unsigned int gl_create_texture(gsr_egl *egl, int width, int height, int internal_format, unsigned int format, int filter) {
    unsigned int texture_id = 0;
    egl->glGenTextures(1, &texture_id);
    egl->glBindTexture(GL_TEXTURE_2D, texture_id);
    /* glTexStorage2D (immutable storage) so the texture is complete and
       usable as a color-conversion destination (same as upstream). */
    egl->glTexStorage2D(GL_TEXTURE_2D, 1, internal_format, width, height);

    egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);

    egl->glBindTexture(GL_TEXTURE_2D, 0);
    return texture_id;
}

/* ---- gsr_damage_* stubs (X11-only upstream, see file header) ------------ */

bool gsr_damage_init(gsr_damage *self, gsr_egl *egl, gsr_cursor *cursor, bool track_cursor) {
    (void)self;
    (void)egl;
    (void)cursor;
    (void)track_cursor;
    /* Always false: no XDamage on Windows. The capture backends provide
       damage via is_damaged()/clear_damage() instead. */
    return false;
}

void gsr_damage_deinit(gsr_damage *self) {
    (void)self;
}

bool gsr_damage_start_tracking_window(gsr_damage *self, int64_t window) {
    (void)self;
    (void)window;
    return false;
}

void gsr_damage_stop_tracking_window(gsr_damage *self, int64_t window) {
    (void)self;
    (void)window;
}

bool gsr_damage_start_tracking_monitor(gsr_damage *self, const char *monitor_name) {
    (void)self;
    (void)monitor_name;
    return false;
}

void gsr_damage_stop_tracking_monitor(gsr_damage *self, const char *monitor_name) {
    (void)self;
    (void)monitor_name;
}

void gsr_damage_on_event(gsr_damage *self, XEvent *xev) {
    (void)self;
    (void)xev;
}

void gsr_damage_tick(gsr_damage *self) {
    (void)self;
}

bool gsr_damage_is_damaged(gsr_damage *self) {
    (void)self;
    return false;
}

void gsr_damage_clear(gsr_damage *self) {
    (void)self;
}

/* ---- gsr_cursor_* stubs (X11-only upstream, see file header) ------------ */

int gsr_cursor_init(gsr_cursor *self, gsr_egl *egl, Display *display) {
    (void)self;
    (void)egl;
    (void)display;
    return -1; /* unavailable */
}

void gsr_cursor_deinit(gsr_cursor *self) {
    (void)self;
}

bool gsr_cursor_on_event(gsr_cursor *self, XEvent *xev) {
    (void)self;
    (void)xev;
    return false;
}

void gsr_cursor_tick(gsr_cursor *self, Window relative_to) {
    (void)self;
    (void)relative_to;
}

/* ---- gsr_window_* stubs (window.c not built, see file header) ----------- */

bool gsr_window_process_event(gsr_window *self) {
    (void)self;
    return false; /* no X11 event loop on Windows */
}

XEvent *gsr_window_get_event_data(gsr_window *self) {
    (void)self;
    return NULL;
}
