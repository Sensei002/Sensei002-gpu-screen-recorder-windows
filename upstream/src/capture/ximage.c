#include "../../include/capture/ximage.h"
#include "../../include/log.h"
#include "../../include/utils.h"
#include "../../include/color_conversion.h"
#include "../../include/window/window.h"

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>

/* TODO: update when monitors are reconfigured */

typedef struct {
    gsr_capture_ximage_params params;
    Display *display;
    gsr_monitor monitor;
    vec2i capture_pos;
    vec2i capture_size;
    unsigned int texture_id;
    Window root_window;
} gsr_capture_ximage;

static void gsr_capture_ximage_stop(gsr_capture_ximage *self) {
    if(self->texture_id) {
        self->params.egl->glDeleteTextures(1, &self->texture_id);
        self->texture_id = 0;
    }
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static int gsr_capture_ximage_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    gsr_capture_ximage *self = cap->priv;
    self->root_window = DefaultRootWindow(self->display);

    if(!get_monitor_by_name(self->params.egl, GSR_CONNECTION_X11, self->params.display_to_capture, &self->monitor)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_ximage_start: failed to find monitor by name \"%s\"", self->params.display_to_capture);
        gsr_capture_ximage_stop(self);
        return -1;
    }

    self->capture_pos = self->monitor.pos;
    self->capture_size = self->monitor.size;

    if(self->params.region_size.x > 0 && self->params.region_size.y > 0)
        self->capture_size = self->params.region_size;

    if(self->params.output_resolution.x > 0 && self->params.output_resolution.y > 0) {
        self->params.output_resolution = scale_keep_aspect_ratio(self->capture_size, self->params.output_resolution);
        capture_metadata->video_size = self->params.output_resolution;
    } else if(self->params.region_size.x > 0 && self->params.region_size.y > 0) {
        capture_metadata->video_size = self->params.region_size;
    } else {
        capture_metadata->video_size = self->capture_size;
    }

    self->texture_id = gl_create_texture(self->params.egl, self->capture_size.x, self->capture_size.y, GL_RGB8, GL_RGB, GL_LINEAR);
    if(self->texture_id == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_ximage_start: failed to create texture");
        gsr_capture_ximage_stop(self);
        return -1;
    }

    return 0;
}

static bool gsr_capture_ximage_upload_to_texture(gsr_capture_ximage *self, int x, int y, int width, int height) {
    const int max_width = XWidthOfScreen(DefaultScreenOfDisplay(self->display));
    const int max_height = XHeightOfScreen(DefaultScreenOfDisplay(self->display));

    if(x < 0)
        x = 0;
    else if(x >= max_width)
        x = max_width - 1;

    if(y < 0)
        y = 0;
    else if(y >= max_height)
        y = max_height - 1;

    if(width < 0)
        width = 0;
    else if(x + width >= max_width)
        width = max_width - x;

    if(height < 0)
        height = 0;
    else if(y + height >= max_height)
        height = max_height - y;

    XImage *image = XGetImage(self->display, self->root_window, x, y, width, height, AllPlanes, ZPixmap);
    if(!image) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_ximage_upload_to_texture: XGetImage failed");
        return false;
    }

    bool success = false;
    uint8_t *image_data = malloc(image->width * image->height * 3);
    if(!image_data) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_ximage_upload_to_texture: failed to allocate image data");
        goto done;
    }

    for(int y = 0; y < image->height; ++y) {
        for(int x = 0; x < image->width; ++x) {
            unsigned long pixel = XGetPixel(image, x, y);
            unsigned char red = (pixel & image->red_mask) >> 16;
            unsigned char green = (pixel & image->green_mask) >> 8;
            unsigned char blue = pixel & image->blue_mask;

            const size_t texture_data_index = (x + y * image->width) * 3;
            image_data[texture_data_index + 0] = red;
            image_data[texture_data_index + 1] = green;
            image_data[texture_data_index + 2] = blue;
        }
    }

    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->texture_id);
    // TODO: Change to GL_RGBA for better performance? image_data needs alpha then as well
    self->params.egl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image->width, image->height, GL_RGB, GL_UNSIGNED_BYTE, image_data);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);
    success = true;

    done:
    free(image_data);
    XDestroyImage(image);
    return success;
}

static int gsr_capture_ximage_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_ximage *self = cap->priv;

    const vec2i output_size = scale_keep_aspect_ratio(self->capture_size, capture_metadata->recording_size);
    const vec2i target_pos = gsr_capture_get_target_position(output_size, capture_metadata);
    gsr_capture_ximage_upload_to_texture(self, self->capture_pos.x + self->params.region_position.x, self->capture_pos.y + self->params.region_position.y, self->capture_size.x, self->capture_size.y);

    gsr_color_conversion_draw(color_conversion, self->texture_id,
        target_pos, output_size,
        (vec2i){0, 0}, self->capture_size, self->capture_size,
        GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

    if(self->params.record_cursor && self->params.cursor->visible) {
        const vec2d scale = {
            self->capture_size.x == 0 ? 0 : (double)output_size.x / (double)self->capture_size.x,
            self->capture_size.y == 0 ? 0 : (double)output_size.y / (double)self->capture_size.y
        };

        const vec2i cursor_pos = {
            target_pos.x + (self->params.cursor->position.x - self->params.cursor->hotspot.x) * scale.x - self->capture_pos.x - self->params.region_position.x,
            target_pos.y + (self->params.cursor->position.y - self->params.cursor->hotspot.y) * scale.y - self->capture_pos.y - self->params.region_position.y
        };

        self->params.egl->glEnable(GL_SCISSOR_TEST);
        self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

        gsr_color_conversion_draw(color_conversion, self->params.cursor->texture_id,
            cursor_pos, (vec2i){self->params.cursor->size.x * scale.x, self->params.cursor->size.y * scale.y},
            (vec2i){0, 0}, self->params.cursor->size, self->params.cursor->size,
            GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

        self->params.egl->glDisable(GL_SCISSOR_TEST);
    }

    self->params.egl->glFlush();
    self->params.egl->glFinish();

    return 0;
}

static void gsr_capture_ximage_destroy(gsr_capture *cap) {
    gsr_capture_ximage *self = cap->priv;
    if(cap->priv) {
        gsr_capture_ximage_stop(self);
        free((void*)self->params.display_to_capture);
        self->params.display_to_capture = NULL;
        free(self);
        cap->priv = NULL;
    }
    free(cap);
}

gsr_capture* gsr_capture_ximage_create(const gsr_capture_ximage_params *params) {
    if(!params) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_ximage_create params is NULL");
        return NULL;
    }

    gsr_capture *cap = calloc(1, sizeof(gsr_capture));
    if(!cap)
        return NULL;

    gsr_capture_ximage *cap_ximage = calloc(1, sizeof(gsr_capture_ximage));
    if(!cap_ximage) {
        free(cap);
        return NULL;
    }

    const char *display_to_capture = strdup(params->display_to_capture);
    if(!display_to_capture) {
        free(cap);
        free(cap_ximage);
        return NULL;
    }

    cap_ximage->params = *params;
    cap_ximage->display = gsr_window_get_display(params->egl->window);
    cap_ximage->params.display_to_capture = display_to_capture;
    
    *cap = (gsr_capture) {
        .start = gsr_capture_ximage_start,
        .tick = NULL,
        .should_stop = NULL,
        .capture = gsr_capture_ximage_capture,
        .uses_external_image = NULL,
        .get_window_id = NULL,
        .destroy = gsr_capture_ximage_destroy,
        .priv = cap_ximage
    };

    return cap;
}
