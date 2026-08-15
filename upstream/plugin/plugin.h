#ifndef GSR_PLUGIN_H
#define GSR_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define GSR_PLUGIN_INTERFACE_MAJOR_VERSION 0
#define GSR_PLUGIN_INTERFACE_MINOR_VERSION 1

#define GSR_PLUGIN_INTERFACE_MAKE_VERSION(major, minor) (((major) << 16) | (minor))
#define GSR_PLUGIN_INTERFACE_VERSION GSR_PLUGIN_INTERFACE_MAKE_VERSION(GSR_PLUGIN_INTERFACE_MAJOR_VERSION, GSR_PLUGIN_INTERFACE_MINOR_VERSION)

#include <stdbool.h>

typedef enum {
    GSR_PLUGIN_GRAPHICS_API_EGL_ES,
    GSR_PLUGIN_GRAPHICS_API_GLX,
} gsr_plugin_graphics_api;

typedef enum {
    GSR_PLUGIN_COLOR_DEPTH_8_BITS,
    GSR_PLUGIN_COLOR_DEPTH_10_BITS,
} gsr_plugin_color_depth;

typedef struct {
    unsigned int width;
    unsigned int height;
} gsr_plugin_draw_params;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int fps;
    gsr_plugin_color_depth color_depth;
    gsr_plugin_graphics_api graphics_api;
} gsr_plugin_init_params;

typedef struct {
    const char *name; /* Mandatory */
    unsigned int version; /* Mandatory, can't be 0 */
    void *userdata; /* Optional */

    /* Optional, called when the plugin is expected to draw something to the current framebuffer */
    void (*draw)(const gsr_plugin_draw_params *params, void *userdata);
    /* Optional, returns false if this function is NULL. If this function is defined then this should return true
       if there is an update visually that the plugin wants to display. This is useful when used with the "content" framerate mode
       which only captures a frame when the frame has changed */
    bool (*is_damaged)(void *userdata);
    /* Optional */
    void (*clear_damage)(void *userdata);
} gsr_plugin_init_return;

/* The plugin is expected to implement these functions and export them: */
bool gsr_plugin_init(const gsr_plugin_init_params *params, gsr_plugin_init_return *ret);
void gsr_plugin_deinit(void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* GSR_PLUGIN_H */
