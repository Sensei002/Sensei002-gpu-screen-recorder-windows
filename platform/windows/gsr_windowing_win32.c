/* gsr_windowing_win32.c — Windows implementation of the engine's windowing
 * abstraction (upstream/include/recorder/windowing.h, Phase 11). Upstream's
 * windowing.c connects to X11/Wayland and finds a DRM card; on Windows the
 * display-server half does not exist and the GL context comes from the
 * ANGLE-on-D3D11 loader (platform/windows/gsr_egl_win32.c), exactly as the
 * recorder self-test wires it.
 *
 * The zeroed gsr_window member is what the rest of the engine consumes on
 * Windows: gsr_window_get_display_server returns X11 (the Windows capture
 * backends and UI already branch on #ifdef _WIN32 for the X11-style paths),
 * and gsr_window_process_event is a no-op (gsr_recorder_win32.c).
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3q.
 */
#include "../../upstream/include/recorder/windowing.h"
#include "../../upstream/include/recorder/error.h"
#include "../../upstream/include/log.h"
#include "../../upstream/include/egl.h"

#include <string.h>

int gsr_windowing_init(gsr_windowing *self, const gsr_windowing_params *params) {
    (void)params;
    memset(self, 0, sizeof(*self));
    /* No X11/Wayland on Windows; the window is the zeroed placeholder the
       recorder only stores. card_path_found is set when EGL loads. */
    self->display = NULL;
    return GSR_ERROR_OK;
}

int gsr_windowing_load_egl(gsr_windowing *self, const gsr_windowing_params *params) {
    /* self->window is already a gsr_window* member (zeroed by init); the
       win32 loader accepts NULL. */
    if(!gsr_egl_load_win32(&self->egl, self->window, params->gl_debug)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to load ANGLE/OpenGL");
        return GSR_ERROR_OPENGL_LOAD_FAILED;
    }
    self->egl_loaded = true;
    /* Windows has no DRM card: the D3D11 adapter ANGLE uses IS the
       "card". The engine main treats card_path_found as "GL is usable". */
    self->card_path_found = true;
    return GSR_ERROR_OK;
}

void gsr_windowing_deinit(gsr_windowing *self) {
    if(self->egl_loaded) {
        gsr_egl_unload_win32(&self->egl);
        self->egl_loaded = false;
    }
}

bool gsr_windowing_is_wayland(const gsr_windowing *self) {
    (void)self;
    return false;
}

bool gsr_windowing_is_using_prime_run(void) {
    return false;
}

void gsr_windowing_disable_prime_run(void) {
}

bool monitor_capture_use_drm(const gsr_window *window, gsr_gpu_vendor vendor) {
    (void)window;
    (void)vendor;
    return false;
}
