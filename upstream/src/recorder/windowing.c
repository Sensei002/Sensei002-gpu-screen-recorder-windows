#include "../../include/recorder/windowing.h"
#include "../../include/recorder/error.h"
#include "../../include/window/x11.h"
#include "../../include/window/wayland.h"
#include "../../include/utils.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>

static int x11_error_handler(Display *display, XErrorEvent *event) {
    (void)display;
    (void)event;
    return 0;
}

static int x11_io_error_handler(Display *display) {
    (void)display;
    return 0;
}

static void xwayland_check_callback(const gsr_monitor *monitor, void *userdata) {
    bool *xwayland_found = (bool*)userdata;
    if(monitor->name_len >= 8 && strncmp(monitor->name, "XWAYLAND", 8) == 0)
        *xwayland_found = true;
    else if(memmem(monitor->name, monitor->name_len, "X11", 3))
        *xwayland_found = true;
}

static bool is_xwayland(Display *display) {
    int opcode, event, error;
    if(XQueryExtension(display, "XWAYLAND", &opcode, &event, &error))
        return true;

    bool xwayland_found = false;
    for_each_active_monitor_output_x11_not_cached(display, xwayland_check_callback, &xwayland_found);
    return xwayland_found;
}

bool gsr_windowing_is_using_prime_run(void) {
    const char *prime_render_offload = getenv("__NV_PRIME_RENDER_OFFLOAD");
    return (prime_render_offload && strcmp(prime_render_offload, "1") == 0) || getenv("DRI_PRIME");
}

void gsr_windowing_disable_prime_run(void) {
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__VK_LAYER_NV_optimus");
    unsetenv("DRI_PRIME");
}

static gsr_window* window_create(Display *display, bool wayland) {
    if(wayland)
        return gsr_window_wayland_create();
    else
        return gsr_window_x11_create(display);
}

bool monitor_capture_use_drm(const gsr_window *window, gsr_gpu_vendor vendor) {
    return gsr_window_get_display_server(window) == GSR_DISPLAY_SERVER_WAYLAND || vendor != GSR_GPU_VENDOR_NVIDIA;
}

int gsr_windowing_init(gsr_windowing *self, const gsr_windowing_params *params) {
    memset(self, 0, sizeof(*self));
    self->card_path_found = true;

    bool wayland = false;
    self->display = XOpenDisplay(NULL);
    if(self->display) {
        if(params->listen_to_x11_events)
            XSelectInput(self->display, DefaultRootWindow(self->display), PropertyChangeMask);
    } else {
        wayland = true;
        gsr_log(GSR_LOG_LEVEL_WARNING, "failed to connect to the X server. Assuming wayland is running without Xwayland");
    }

    XSetErrorHandler(x11_error_handler);
    XSetIOErrorHandler(x11_io_error_handler);

    if(!wayland)
        wayland = is_xwayland(self->display);

    if(!wayland && gsr_windowing_is_using_prime_run()) {
        // Disable prime-run and similar options as it doesn't work, the monitor to capture has to be run on the same device.
        // This is fine on wayland since nvidia uses drm interface there and the monitor query checks the monitors connected
        // to the drm device.
        gsr_log(GSR_LOG_LEVEL_WARNING, "use of prime-run on X11 is not supported. Disabling prime-run");
        gsr_windowing_disable_prime_run();
    }

    self->window = window_create(self->display, wayland);
    if(!self->window) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to create window");
        return GSR_ERROR_GENERIC;
    }

    return GSR_ERROR_OK;
}

int gsr_windowing_load_egl(gsr_windowing *self, const gsr_windowing_params *params) {
    if(!gsr_egl_load(&self->egl, self->window, params->monitor_capture, params->gl_debug)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to load opengl");
        return GSR_ERROR_OPENGL_LOAD_FAILED;
    }
    self->egl_loaded = true;

    self->egl.card_path[0] = '\0';
    if(monitor_capture_use_drm(self->window, self->egl.gpu_info.vendor)) {
        // TODO: Allow specifying another card, and in other places
        if(!gsr_get_valid_card_path(&self->egl, self->egl.card_path, params->monitor_capture))
            self->card_path_found = false;
    } else {
        gsr_get_valid_card_path(&self->egl, self->egl.card_path, false);
    }

    return GSR_ERROR_OK;
}

void gsr_windowing_deinit(gsr_windowing *self) {
    if(self->egl_loaded) {
        gsr_egl_unload(&self->egl);
        self->egl_loaded = false;
    }

    if(self->window) {
        gsr_window_destroy(self->window);
        self->window = NULL;
    }

    if(self->display) {
        /* TODO: XCloseDisplay causes a crash, why? maybe some other library dlclose xlib and that also happened to unload this??? */
        //XCloseDisplay(self->display);
        self->display = NULL;
    }
}

bool gsr_windowing_is_wayland(const gsr_windowing *self) {
    return gsr_window_get_display_server(self->window) == GSR_DISPLAY_SERVER_WAYLAND;
}
