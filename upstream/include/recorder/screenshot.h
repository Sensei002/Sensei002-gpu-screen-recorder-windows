#ifndef GSR_RECORDER_SCREENSHOT_H
#define GSR_RECORDER_SCREENSHOT_H

#include <stdbool.h>
#include <stdatomic.h>
#include "../egl.h"
#include "../image_writer.h"
#include "../plugins.h"
#include "capture_source.h"
#include "capture_setup.h"
#include "settings.h"

typedef struct {
    const gsr_recorder_settings *settings;
    gsr_egl *egl;
    gsr_window *window;
    gsr_capture_deps *capture_deps;
    gsr_capture_sources *capture_sources;
    gsr_image_format image_format;
    const char **plugin_filepaths;
    int num_plugin_filepaths;
    const atomic_int *running;
    void (*screenshot_saved)(const char *filepath, void *userdata);
    void *userdata;
} gsr_screenshot_params;

bool get_image_format_from_filename(const char *filename, gsr_image_format *image_format);
gsr_color_range image_format_to_color_range(gsr_image_format image_format, int image_quality);
int video_quality_to_image_quality_value(gsr_video_quality video_quality);

/* Returns a |gsr_error| value. Captures one frame and writes it to |settings->filename| */
int gsr_screenshot_take(const gsr_screenshot_params *params);

int gsr_load_plugins(gsr_plugins *plugins, const char **plugin_filepaths, int num_plugin_filepaths, const gsr_recorder_settings *settings, gsr_egl *egl, vec2i video_size);

#endif /* GSR_RECORDER_SCREENSHOT_H */
