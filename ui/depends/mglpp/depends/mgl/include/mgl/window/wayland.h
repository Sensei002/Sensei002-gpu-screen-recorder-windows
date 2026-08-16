#ifndef MGL_WINDOW_WAYLAND_H
#define MGL_WINDOW_WAYLAND_H

#include "window.h"

bool mgl_window_wayland_init(mgl_window *self, const char *title, const mgl_window_create_params *params, mgl_window_handle existing_window);

#endif /* MGL_WINDOW_WAYLAND_H */
