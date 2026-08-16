#ifndef MGL_GRAPHICS_H
#define MGL_GRAPHICS_H

#include <stdbool.h>

/* Each window should have its own mgl_graphics */

typedef struct mgl_graphics mgl_graphics;
typedef void* mgl_window_handle;

typedef enum {
    MGL_GRAPHICS_API_EGL,
    MGL_GRAPHICS_API_GLX /* Only available when using X11 (or XWayland) */
} mgl_graphics_api;

struct mgl_graphics {
    void  (*deinit)(mgl_graphics *self);
    bool  (*make_context_current)(mgl_graphics *self, mgl_window_handle window);
    void  (*swap_buffers)(mgl_graphics *self, mgl_window_handle window);
    bool  (*set_swap_interval)(mgl_graphics *self, mgl_window_handle window, bool enabled);
    void* (*get_xvisual_info)(mgl_graphics *self);

    /* Optional */
    void* (*get_display)(mgl_graphics *self);
    void* (*get_context)(mgl_graphics *self);

    void *impl;
    
    mgl_graphics_api graphics_api;
    bool alpha;
};

typedef struct {
    mgl_graphics_api graphics_api; /* The graphics api to use. MGL_GRAPHICS_API_EGL by default */
    bool alpha;                    /* Support window alpha transparency. False by default */
    bool request_depth_buffer;     /* default: false */
    bool request_stencil_buffer;   /* default: false */
} mgl_graphics_create_params;

bool mgl_graphics_init(mgl_graphics *self, const mgl_graphics_create_params *params);
void mgl_graphics_deinit(mgl_graphics *self);

bool mgl_graphics_make_context_current(mgl_graphics *self, mgl_window_handle window);
void mgl_graphics_swap_buffers(mgl_graphics *self, mgl_window_handle window);
bool mgl_graphics_set_swap_interval(mgl_graphics *self, mgl_window_handle window, bool enabled);
void* mgl_graphics_get_xvisual_info(mgl_graphics *self);
void* mgl_graphics_get_display(mgl_graphics *self);
void* mgl_graphics_get_context(mgl_graphics *self);

#endif /* MGL_GRAPHICS_H */
