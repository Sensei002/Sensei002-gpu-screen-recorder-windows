#include "../../include/recorder/screenshot.h"
#include "../../include/recorder/error.h"
#include "../../include/color_conversion.h"
#include "../../include/window/window.h"
#include "../../include/log.h"

#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <X11/Xlib.h>

#define JPEG_YUV444_QUALITY_THRESHOLD 91

gsr_color_range image_format_to_color_range(gsr_image_format image_format, int image_quality) {
    switch(image_format) {
        case GSR_IMAGE_FORMAT_JPEG: return image_quality >= JPEG_YUV444_QUALITY_THRESHOLD ? GSR_COLOR_RANGE_FULL : GSR_COLOR_RANGE_LIMITED;
        case GSR_IMAGE_FORMAT_PNG:  return GSR_COLOR_RANGE_FULL;
    }
    assert(false);
    return GSR_COLOR_RANGE_FULL;
}

int video_quality_to_image_quality_value(gsr_video_quality video_quality) {
    switch(video_quality) {
        case GSR_VIDEO_QUALITY_MEDIUM:
            return 75;
        case GSR_VIDEO_QUALITY_HIGH:
            return 85;
        case GSR_VIDEO_QUALITY_VERY_HIGH:
            return JPEG_YUV444_QUALITY_THRESHOLD; // Quality above 90 makes the jpeg image encoder (stb_image_writer) use yuv444 instead of yuv420, which greatly improves small colored text quality on dark background
        case GSR_VIDEO_QUALITY_ULTRA:
            return 97;
    }
    assert(false);
    return 90;
}

int gsr_load_plugins(gsr_plugins *plugins, const char **plugin_filepaths, int num_plugin_filepaths, const gsr_recorder_settings *settings, gsr_egl *egl, vec2i video_size) {
    if(num_plugin_filepaths == 0)
        return GSR_ERROR_OK;

    const gsr_color_depth color_depth = video_codec_to_bit_depth(settings->video_codec);
    assert(color_depth == GSR_COLOR_DEPTH_8_BITS || color_depth == GSR_COLOR_DEPTH_10_BITS);

    gsr_plugin_init_params plugin_init_params;
    plugin_init_params.width = video_size.x;
    plugin_init_params.height = video_size.y;
    plugin_init_params.fps = settings->fps;
    plugin_init_params.color_depth = color_depth == GSR_COLOR_DEPTH_8_BITS ? GSR_PLUGIN_COLOR_DEPTH_8_BITS : GSR_PLUGIN_COLOR_DEPTH_10_BITS;
    plugin_init_params.graphics_api = egl->context_type == GSR_GL_CONTEXT_TYPE_GLX ? GSR_PLUGIN_GRAPHICS_API_GLX : GSR_PLUGIN_GRAPHICS_API_EGL_ES;

    if(!gsr_plugins_init(plugins, plugin_init_params, egl))
        return GSR_ERROR_GENERIC;

    for(int i = 0; i < num_plugin_filepaths; ++i) {
        if(!gsr_plugins_load_plugin(plugins, plugin_filepaths[i]))
            return GSR_ERROR_GENERIC;
    }

    return GSR_ERROR_OK;
}

int gsr_screenshot_take(const gsr_screenshot_params *params) {
    const gsr_recorder_settings *settings = params->settings;
    gsr_egl *egl = params->egl;
    gsr_window *window = params->window;
    gsr_capture_deps *capture_deps = params->capture_deps;
    gsr_capture_sources *capture_sources = params->capture_sources;
    const atomic_int *running = params->running;
    const int image_quality = video_quality_to_image_quality_value(settings->video_quality);
    const gsr_color_range color_range = image_format_to_color_range(params->image_format, image_quality);

    vec2i video_size = {0, 0};
    gsr_video_sources video_sources_data;
    const int video_sources_result = gsr_video_sources_create(&video_sources_data, settings, egl, capture_deps, true, capture_sources, &video_size);
    if(video_sources_result != GSR_ERROR_OK)
        return video_sources_result;
    gsr_video_sources *video_sources = &video_sources_data;
    gsr_video_sources_update_with_real_video_size(video_sources, video_size);

    gsr_plugins plugins;
    memset(&plugins, 0, sizeof(plugins));

    const int load_plugins_result = gsr_load_plugins(&plugins, params->plugin_filepaths, params->num_plugin_filepaths, settings, egl, video_size);
    if(load_plugins_result != GSR_ERROR_OK) {
        gsr_video_sources_deinit(video_sources);
        return load_plugins_result;
    }

    int result = GSR_ERROR_GENERIC;
    gsr_image_writer image_writer;
    memset(&image_writer, 0, sizeof(image_writer));
    gsr_color_conversion color_conversion;
    memset(&color_conversion, 0, sizeof(color_conversion));

    if(!gsr_image_writer_init_opengl(&image_writer, egl, video_size.x, video_size.y)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_screenshot_take: gsr_image_write_gl_init failed");
        goto done;
    }

    gsr_color_conversion_params color_conversion_params;
    memset(&color_conversion_params, 0, sizeof(color_conversion_params));
    color_conversion_params.color_range = color_range;
    color_conversion_params.egl = egl;
    color_conversion_params.load_external_image_shader = gsr_video_sources_uses_external_image(video_sources);

    color_conversion_params.destination_textures[0] = image_writer.texture;
    color_conversion_params.destination_textures_size[0] = video_size;
    color_conversion_params.num_destination_textures = 1;
    color_conversion_params.destination_color = GSR_DESTINATION_COLOR_RGB;

    if(gsr_color_conversion_init(&color_conversion, &color_conversion_params) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_screenshot_take: failed to create color conversion");
        goto done;
    }

    gsr_color_conversion_clear(&color_conversion);

    gsr_color_conversion *output_color_conversion = plugins.num_plugins > 0 ? &plugins.color_conversion : &color_conversion;

    bool should_stop_error = false;
    egl->glClear(0);

    while(atomic_load(running)) {
        while(gsr_window_process_event(window)) {
            if(capture_deps->x11_cursor_display && settings->record_cursor)
                gsr_cursor_on_event(&capture_deps->x11_cursor, gsr_window_get_event_data(window));

            for(size_t video_source_index = 0; video_source_index < video_sources->num_items; ++video_source_index) {
                gsr_video_source *video_source = &video_sources->items[video_source_index];
                gsr_capture_on_event(video_source->capture, egl);
            }
        }

        if(capture_deps->x11_cursor_display && settings->record_cursor)
            gsr_cursor_tick(&capture_deps->x11_cursor, DefaultRootWindow(capture_deps->x11_cursor_display));

        gsr_capture_deps_cleanup_kms_fds(capture_deps);

        gsr_capture_deps_update_kms(capture_deps);

        should_stop_error = false;
        for(size_t video_source_index = 0; video_source_index < video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &video_sources->items[video_source_index];
            gsr_capture_tick(video_source->capture);
            if(gsr_capture_should_stop(video_source->capture, &should_stop_error)) {
                break;
                break;
            }
        }

        for(size_t video_source_index = 0; video_source_index < video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &video_sources->items[video_source_index];
            if(video_source->capture->pre_capture)
                video_source->capture->pre_capture(video_source->capture, &video_source->metadata, output_color_conversion);
        }

        if(output_color_conversion->schedule_clear) {
            output_color_conversion->schedule_clear = false;
            gsr_color_conversion_clear(output_color_conversion);
        }

        bool all_sources_captured = true;
        for(size_t video_source_index = 0; video_source_index < video_sources->num_items; ++video_source_index) {
            gsr_video_source *video_source = &video_sources->items[video_source_index];
            // It can fail, for example when capturing portal and the target is a monitor that hasn't been updated.
            // This can also happen for example if the system suspends and the monitor to capture's framebuffer is gone, or if the target window disappeared.
            if(gsr_capture_capture(video_source->capture, &video_source->metadata, output_color_conversion) != 0)
                all_sources_captured = false;
        }

        gsr_capture_deps_cleanup_kms_fds(capture_deps);

        if(all_sources_captured)
            break;

        if(atomic_load(running))
            usleep(30 * 1000); // 30 ms
    }

    if(plugins.num_plugins > 0) {
        gsr_plugins_draw(&plugins);
        gsr_color_conversion_draw(&color_conversion, plugins.texture,
            (vec2i){0, 0}, video_size,
            (vec2i){0, 0}, video_size,
            video_size, GSR_ROT_0, GSR_FLIP_NONE, GSR_SOURCE_COLOR_RGB, false);
    }

    gsr_egl_swap_buffers(egl);

    result = should_stop_error ? GSR_ERROR_CAPTURE_FAILED : GSR_ERROR_OK;
    if(!should_stop_error) {
        if(!gsr_image_writer_write_to_file(&image_writer, settings->filename, params->image_format, image_quality)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_screenshot_take: failed to write opengl texture to image output file %s", settings->filename);
            result = GSR_ERROR_GENERIC;
        }

        if(result == GSR_ERROR_OK && params->screenshot_saved)
            params->screenshot_saved(settings->filename, params->userdata);
    }

    done:
    gsr_color_conversion_deinit(&color_conversion);
    gsr_plugins_deinit(&plugins);
    gsr_image_writer_deinit(&image_writer);
    gsr_video_sources_deinit(video_sources);
    return result;
}

bool get_image_format_from_filename(const char *filename, gsr_image_format *image_format) {
    if(gsr_string_ends_with(filename, ".jpg") || gsr_string_ends_with(filename, ".jpeg")) {
        *image_format = GSR_IMAGE_FORMAT_JPEG;
        return true;
    } else if(gsr_string_ends_with(filename, ".png")) {
        *image_format = GSR_IMAGE_FORMAT_PNG;
        return true;
    } else {
        return false;
    }
}
