#include "../../include/window/wayland.h"
#include "../../include/log.h"
#include "../../include/wayland_host_bridge.h"

#include "../../include/vec2.h"
#include "../../include/defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-output-unstable-v1-client-protocol.h"
#include "color-management-v1-client-protocol.h"

#define GSR_MAX_OUTPUTS 32

typedef struct gsr_window_wayland gsr_window_wayland;

typedef struct {
    uint32_t wl_name;
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;
    vec2i pos;
    vec2i size;
    vec2i logical_size;
    int32_t transform;
    char *name;
    struct wp_color_management_output_v1 *color_management_output;
    struct wp_image_description_v1 *image_description;
    struct wp_image_description_info_v1 *image_description_info;
    uint32_t reference_luminance; /* cd/m² (nits) */
    uint32_t max_luminance;       /* cd/m² (nits) */
    bool luminance_valid;
    int32_t primaries[8]; /* CIE 1931 xy chromaticity coordinates multiplied by 1 million, in the order red, green, blue, white */
    bool primaries_valid;
} gsr_wayland_output;

struct gsr_window_wayland {
    struct wl_display *display;
    struct wl_egl_window *window;
    struct wl_registry *registry;
    struct wl_surface *surface;
    struct wl_compositor *compositor;
    gsr_wayland_output outputs[GSR_MAX_OUTPUTS];
    int num_outputs;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct wp_color_manager_v1 *color_manager;
};

static void output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    (void)wl_output;
    (void)phys_width;
    (void)phys_height;
    (void)subpixel;
    (void)make;
    (void)model;
    gsr_wayland_output *gsr_output = data;
    gsr_output->pos.x = x;
    gsr_output->pos.y = y;
    gsr_output->transform = transform;
}

static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)wl_output;
    (void)flags;
    (void)refresh;
    gsr_wayland_output *gsr_output = data;
    gsr_output->size.x = width;
    gsr_output->size.y = height;
}

static void output_handle_done(void *data, struct wl_output *wl_output) {
    (void)data;
    (void)wl_output;
}

static void output_handle_scale(void* data, struct wl_output *wl_output, int32_t factor) {
    (void)data;
    (void)wl_output;
    (void)factor;
}

static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    gsr_wayland_output *gsr_output = data;
    if(gsr_output->name) {
        free(gsr_output->name);
        gsr_output->name = NULL;
    }
    gsr_output->name = strdup(name);
}

static void output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)data;
    (void)wl_output;
    (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
};

static void image_description_info_handle_done(void *data, struct wp_image_description_info_v1 *info) {
    gsr_wayland_output *output = data;
    wp_image_description_info_v1_destroy(info);
    output->image_description_info = NULL;
    output->luminance_valid = output->reference_luminance > 0 || output->max_luminance > 0;
    output->primaries_valid = output->primaries[1] > 0 && output->primaries[3] > 0 && output->primaries[5] > 0 && output->primaries[7] > 0;
}

static void image_description_info_handle_icc_file(void *data, struct wp_image_description_info_v1 *info, int32_t icc, uint32_t icc_size) {
    (void)data; (void)info; (void)icc_size;
    close(icc);
}

static void image_description_info_handle_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y) {
    (void)info;
    gsr_wayland_output *output = data;
    output->primaries[0] = r_x;
    output->primaries[1] = r_y;
    output->primaries[2] = g_x;
    output->primaries[3] = g_y;
    output->primaries[4] = b_x;
    output->primaries[5] = b_y;
    output->primaries[6] = w_x;
    output->primaries[7] = w_y;
}

static void image_description_info_handle_target_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y) {
    (void)data; (void)info; (void)r_x; (void)r_y; (void)g_x; (void)g_y; (void)b_x; (void)b_y; (void)w_x; (void)w_y;
}

static void image_description_info_handle_uint_noop(void *data, struct wp_image_description_info_v1 *info, uint32_t value) {
    (void)data; (void)info; (void)value;
}

static void image_description_info_handle_luminances(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum, uint32_t reference_lum) {
    (void)info; (void)min_lum;
    gsr_wayland_output *output = data;
    output->max_luminance = max_lum;
    output->reference_luminance = reference_lum;
}

static void image_description_info_handle_target_luminance(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum) {
    (void)data; (void)info; (void)min_lum; (void)max_lum;
}

static const struct wp_image_description_info_v1_listener image_description_info_listener = {
    .done = image_description_info_handle_done,
    .icc_file = image_description_info_handle_icc_file,
    .primaries = image_description_info_handle_primaries,
    .primaries_named = image_description_info_handle_uint_noop,
    .tf_power = image_description_info_handle_uint_noop,
    .tf_named = image_description_info_handle_uint_noop,
    .luminances = image_description_info_handle_luminances,
    .target_primaries = image_description_info_handle_target_primaries,
    .target_luminance = image_description_info_handle_target_luminance,
    .target_max_cll = image_description_info_handle_uint_noop,
    .target_max_fall = image_description_info_handle_uint_noop,
};

static void image_description_handle_failed(void *data, struct wp_image_description_v1 *image_description, uint32_t cause, const char *msg) {
    (void)cause; (void)msg;
    gsr_wayland_output *output = data;
    wp_image_description_v1_destroy(image_description);
    output->image_description = NULL;
}

static void image_description_handle_ready(void *data, struct wp_image_description_v1 *image_description, uint32_t identity) {
    (void)identity;
    gsr_wayland_output *output = data;
    if(output->image_description_info)
        wp_image_description_info_v1_destroy(output->image_description_info);

    output->image_description_info = wp_image_description_v1_get_information(image_description);
    wp_image_description_info_v1_add_listener(output->image_description_info, &image_description_info_listener, output);
    wp_image_description_v1_destroy(image_description);
    output->image_description = NULL;
}

static void image_description_handle_ready2(void *data, struct wp_image_description_v1 *image_description, uint32_t identity_hi, uint32_t identity_lo) {
    (void)identity_lo;
    image_description_handle_ready(data, image_description, identity_hi);
}

static const struct wp_image_description_v1_listener image_description_listener = {
    .failed = image_description_handle_failed,
    .ready = image_description_handle_ready,
    .ready2 = image_description_handle_ready2,
};

static void gsr_wayland_output_fetch_image_description(gsr_wayland_output *output) {
    if(output->image_description_info) {
        wp_image_description_info_v1_destroy(output->image_description_info);
        output->image_description_info = NULL;
    }

    if(output->image_description) {
        wp_image_description_v1_destroy(output->image_description);
        output->image_description = NULL;
    }

    output->image_description = wp_color_management_output_v1_get_image_description(output->color_management_output);
    wp_image_description_v1_add_listener(output->image_description, &image_description_listener, output);
}

static void color_management_output_handle_image_description_changed(void *data, struct wp_color_management_output_v1 *color_management_output) {
    (void)color_management_output;
    gsr_wayland_output *output = data;
    gsr_wayland_output_fetch_image_description(output);
}

static const struct wp_color_management_output_v1_listener color_management_output_listener = {
    .image_description_changed = color_management_output_handle_image_description_changed,
};

static void registry_add_object(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    gsr_window_wayland *window_wayland = data;
    if(strcmp(interface, "wl_compositor") == 0) {
        if(window_wayland->compositor)
            return;

        window_wayland->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if(strcmp(interface, wl_output_interface.name) == 0) {
        if(version < 4) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "wl output interface version is < 4, expected >= 4 to capture a monitor");
            return;
        }

        if(window_wayland->num_outputs == GSR_MAX_OUTPUTS) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "reached maximum outputs (%d), ignoring output %u", GSR_MAX_OUTPUTS, name);
            return;
        }

        gsr_wayland_output *gsr_output = &window_wayland->outputs[window_wayland->num_outputs];
        window_wayland->num_outputs++;
        *gsr_output = (gsr_wayland_output) {
            .wl_name = name,
            .output = wl_registry_bind(registry, name, &wl_output_interface, 4),
            .pos = { .x = 0, .y = 0 },
            .size = { .x = 0, .y = 0 },
            .logical_size = { .x = 0, .y = 0 },
            .transform = 0,
            .name = NULL,
        };
        wl_output_add_listener(gsr_output->output, &output_listener, gsr_output);
    } else if(strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        if(version < 1) {
            gsr_log(GSR_LOG_LEVEL_WARNING, "xdg output interface version is < 1, expected >= 1 to capture a monitor");
            return;
        }

        if(window_wayland->xdg_output_manager)
            return;

        window_wayland->xdg_output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 1);
    } else if(strcmp(interface, wp_color_manager_v1_interface.name) == 0) {
        if(window_wayland->color_manager)
            return;

        window_wayland->color_manager = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, 1);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
    // TODO: Remove output
}

static struct wl_registry_listener registry_listener = {
    .global = registry_add_object,
    .global_remove = registry_remove_object,
};

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *zxdg_output_v1, int32_t x, int32_t y) {
    (void)zxdg_output_v1;
    gsr_wayland_output *gsr_xdg_output = data;
    gsr_xdg_output->pos.x = x;
    gsr_xdg_output->pos.y = y;
}

static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height) {
    (void)xdg_output;
    gsr_wayland_output *gsr_xdg_output = data;
    gsr_xdg_output->logical_size.x = width;
    gsr_xdg_output->logical_size.y = height;
}

static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {
    (void)data;
    (void)xdg_output;
}

static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
    (void)data;
    (void)xdg_output;
    (void)name;
}

static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description) {
    (void)data;
    (void)xdg_output;
    (void)description;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static void gsr_window_wayland_set_monitor_outputs_from_xdg_output(gsr_window_wayland *self) {
    if(!self->xdg_output_manager) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "zxdg_output_manager not found. registered monitor positions might be incorrect");
        return;
    }

    for(int i = 0; i < self->num_outputs; ++i) {
        self->outputs[i].xdg_output = zxdg_output_manager_v1_get_xdg_output(self->xdg_output_manager, self->outputs[i].output);
        zxdg_output_v1_add_listener(self->outputs[i].xdg_output, &xdg_output_listener, &self->outputs[i]);
    }

    // Fetch xdg_output
    wl_display_roundtrip(self->display);
}

// static int monitor_sort_x_pos(const void* a, const void* b) {
//     const gsr_wayland_output *arg1 = *(const gsr_wayland_output**)a;
//     const gsr_wayland_output *arg2 = *(const gsr_wayland_output**)b;
//     return arg1->logical_pos.x - arg2->logical_pos.x;
// }

// static int monitor_sort_y_pos(const void* a, const void* b) {
//     const gsr_wayland_output *arg1 = *(const gsr_wayland_output**)a;
//     const gsr_wayland_output *arg2 = *(const gsr_wayland_output**)b;
//     return arg1->logical_pos.y - arg2->logical_pos.y;
// }

static void gsr_window_wayland_setup_color_management_outputs(gsr_window_wayland *self) {
    if(!self->color_manager)
        return;

    for(int i = 0; i < self->num_outputs; ++i) {
        gsr_wayland_output *output = &self->outputs[i];
        output->color_management_output = wp_color_manager_v1_get_output(self->color_manager, output->output);
        wp_color_management_output_v1_add_listener(output->color_management_output, &color_management_output_listener, output);
        gsr_wayland_output_fetch_image_description(output);
    }

    // Fetch the image descriptions and then the image description infos
    wl_display_roundtrip(self->display);
    wl_display_roundtrip(self->display);
}

static void gsr_window_wayland_set_monitor_real_positions(gsr_window_wayland *self) {
    gsr_wayland_output *sorted_outputs[GSR_MAX_OUTPUTS];
    for(int i = 0; i < self->num_outputs; ++i) {
        sorted_outputs[i] = &self->outputs[i];
    }

    // TODO: set correct physical positions

    // qsort(sorted_outputs, self->num_outputs, sizeof(gsr_wayland_output*), monitor_sort_x_pos);
    // int x_pos = 0;
    // for(int i = 0; i < self->num_outputs; ++i) {
    //     fprintf(stderr, "monitor: %s\n", sorted_outputs[i]->name);
    //     sorted_outputs[i]->pos.x = x_pos;
    //     x_pos += sorted_outputs[i]->logical_size.x;
    // }

    // qsort(sorted_outputs, self->num_outputs, sizeof(gsr_wayland_output*), monitor_sort_y_pos);
    // int y_pos = 0;
    // for(int i = 0; i < self->num_outputs; ++i) {
    //     sorted_outputs[i]->pos.y = y_pos;
    //     y_pos += sorted_outputs[i]->logical_size.y;
    // }
}

static void gsr_window_wayland_deinit(gsr_window_wayland *self) {
    if(self->window) {
        wl_egl_window_destroy(self->window);
        self->window = NULL;
    }

    if(self->surface) {
        wl_surface_destroy(self->surface);
        self->surface = NULL;
    }

    for(int i = 0; i < self->num_outputs; ++i) {
        if(self->outputs[i].image_description_info) {
            wp_image_description_info_v1_destroy(self->outputs[i].image_description_info);
            self->outputs[i].image_description_info = NULL;
        }

        if(self->outputs[i].image_description) {
            wp_image_description_v1_destroy(self->outputs[i].image_description);
            self->outputs[i].image_description = NULL;
        }

        if(self->outputs[i].color_management_output) {
            wp_color_management_output_v1_destroy(self->outputs[i].color_management_output);
            self->outputs[i].color_management_output = NULL;
        }

        if(self->outputs[i].xdg_output) {
            zxdg_output_v1_destroy(self->outputs[i].xdg_output);
            self->outputs[i].xdg_output = NULL;
        }

        if(self->outputs[i].output) {
            wl_output_destroy(self->outputs[i].output);
            self->outputs[i].output = NULL;
        }

        if(self->outputs[i].name) {
            free(self->outputs[i].name);
            self->outputs[i].name = NULL;
        }
    }
    self->num_outputs = 0;

    if(self->color_manager) {
        wp_color_manager_v1_destroy(self->color_manager);
        self->color_manager = NULL;
    }

    if(self->xdg_output_manager) {
        zxdg_output_manager_v1_destroy(self->xdg_output_manager);
        self->xdg_output_manager = NULL;
    }

    if(self->compositor) {
        wl_compositor_destroy(self->compositor);
        self->compositor = NULL;
    }

    if(self->registry) {
        wl_registry_destroy(self->registry);
        self->registry = NULL;
    }

    if(self->display) {
        wl_display_disconnect(self->display);
        self->display = NULL;
    }
}

static bool gsr_window_wayland_init(gsr_window_wayland *self) {
    self->display = wayland_connect_to_host();
    if(!self->display) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_window_wayland_init failed: failed to connect to the Wayland server");
        goto fail;
    }

    self->registry = wl_display_get_registry(self->display); // TODO: Error checking
    wl_registry_add_listener(self->registry, &registry_listener, self); // TODO: Error checking

    // Fetch globals
    wl_display_roundtrip(self->display);

    // Fetch wl_output
    wl_display_roundtrip(self->display);

    gsr_window_wayland_set_monitor_outputs_from_xdg_output(self);
    gsr_window_wayland_setup_color_management_outputs(self);
    gsr_window_wayland_set_monitor_real_positions(self);

    if(!self->compositor) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_window_wayland_init failed: failed to find compositor");
        goto fail;
    }

    self->surface = wl_compositor_create_surface(self->compositor);
    if(!self->surface) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_window_wayland_init failed: failed to create surface");
        goto fail;
    }

    self->window = wl_egl_window_create(self->surface, 16, 16);
    if(!self->window) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_window_wayland_init failed: failed to create window");
        goto fail;
    }

    return true;

    fail:
    gsr_window_wayland_deinit(self);
    return false;
}

static void gsr_window_wayland_destroy(gsr_window *window) {
    gsr_window_wayland *self = window->priv;
    gsr_window_wayland_deinit(self);
    free(self);
    free(window);
}

static bool gsr_window_wayland_process_event(gsr_window *window) {
    gsr_window_wayland *self = window->priv;
    // TODO: pselect on wl_display_get_fd before doing dispatch
    const bool events_available = wl_display_dispatch_pending(self->display) > 0;
    wl_display_flush(self->display);
    return events_available;
}

static gsr_display_server gsr_wayland_get_display_server(void) {
    return GSR_DISPLAY_SERVER_WAYLAND;
}

static void* gsr_window_wayland_get_display(gsr_window *window) {
    gsr_window_wayland *self = window->priv;
    return self->display;
}

static void* gsr_window_wayland_get_window(gsr_window *window) {
    gsr_window_wayland *self = window->priv;
    return self->window;
}

static gsr_monitor_rotation wayland_transform_to_gsr_rotation(int32_t rot) {
    switch(rot) {
        case 0: return GSR_MONITOR_ROT_0;
        case 1: return GSR_MONITOR_ROT_90;
        case 2: return GSR_MONITOR_ROT_180;
        case 3: return GSR_MONITOR_ROT_270;
    }
    return GSR_MONITOR_ROT_0;
}

static vec2i get_monitor_size_rotated(int width, int height, gsr_monitor_rotation rotation) {
    vec2i size = { .x = width, .y = height };
    if(rotation == GSR_MONITOR_ROT_90 || rotation == GSR_MONITOR_ROT_270) {
        int tmp_x = size.x;
        size.x = size.y;
        size.y = tmp_x;
    }
    return size;
}

static void gsr_window_wayland_for_each_active_monitor_output_cached(const gsr_window *window, active_monitor_callback callback, void *userdata) {
    const gsr_window_wayland *self = window->priv;
    for(int i = 0; i < self->num_outputs; ++i) {
        const gsr_wayland_output *output = &self->outputs[i];
        if(!output->name)
            continue;

        const gsr_monitor_rotation rotation = wayland_transform_to_gsr_rotation(output->transform);

        vec2i size = { .x = output->size.x, .y = output->size.y };
        size = get_monitor_size_rotated(size.x, size.y, rotation);

        vec2i logical_size = { .x = output->logical_size.x, .y = output->logical_size.y };
        if(logical_size.x == 0 || logical_size.y == 0)
            logical_size = size;

        const int connector_type_index = get_connector_type_by_name(output->name);
        const int connector_type_id = get_connector_type_id_by_name(output->name);
        const gsr_monitor monitor = {
            .name = output->name,
            .name_len = strlen(output->name),
            .pos = { .x = output->pos.x, .y = output->pos.y },
            .size = size,
            .logical_pos = { .x = output->pos.x, .y = output->pos.y },
            .logical_size = logical_size,
            .connector_id = 0,
            .rotation = rotation,
            .monitor_identifier = (connector_type_index != -1 && connector_type_id != -1) ? monitor_identifier_from_type_and_count(connector_type_index, connector_type_id) : 0
        };
        callback(&monitor, userdata);
    }
}

static bool gsr_window_wayland_get_monitor_hdr_info(const gsr_window *window, const char *monitor_name, gsr_monitor_hdr_info *hdr_info) {
    const gsr_window_wayland *self = window->priv;
    for(int i = 0; i < self->num_outputs; ++i) {
        const gsr_wayland_output *output = &self->outputs[i];
        if(!output->name || strcmp(output->name, monitor_name) != 0 || !output->luminance_valid)
            continue;

        hdr_info->sdr_white_luminance = output->reference_luminance;
        hdr_info->max_peak_luminance = output->max_luminance;
        hdr_info->primaries_valid = output->primaries_valid;
        if(output->primaries_valid) {
            hdr_info->primaries.red_x = output->primaries[0] / 1000000.0f;
            hdr_info->primaries.red_y = output->primaries[1] / 1000000.0f;
            hdr_info->primaries.green_x = output->primaries[2] / 1000000.0f;
            hdr_info->primaries.green_y = output->primaries[3] / 1000000.0f;
            hdr_info->primaries.blue_x = output->primaries[4] / 1000000.0f;
            hdr_info->primaries.blue_y = output->primaries[5] / 1000000.0f;
            hdr_info->primaries.white_x = output->primaries[6] / 1000000.0f;
            hdr_info->primaries.white_y = output->primaries[7] / 1000000.0f;
        } else {
            memset(&hdr_info->primaries, 0, sizeof(hdr_info->primaries));
        }
        return true;
    }
    return false;
}

gsr_window* gsr_window_wayland_create(void) {
    gsr_window *window = calloc(1, sizeof(gsr_window));
    if(!window)
        return window;

    gsr_window_wayland *window_wayland = calloc(1, sizeof(gsr_window_wayland));
    if(!window_wayland) {
        free(window);
        return NULL;
    }

    if(!gsr_window_wayland_init(window_wayland)) {
        free(window_wayland);
        free(window);
        return NULL;
    }

    *window = (gsr_window) {
        .destroy = gsr_window_wayland_destroy,
        .process_event = gsr_window_wayland_process_event,
        .get_event_data = NULL,
        .get_display_server = gsr_wayland_get_display_server,
        .get_display = gsr_window_wayland_get_display,
        .get_window = gsr_window_wayland_get_window,
        .for_each_active_monitor_output_cached = gsr_window_wayland_for_each_active_monitor_output_cached,
        .get_monitor_hdr_info = gsr_window_wayland_get_monitor_hdr_info,
        .priv = window_wayland
    };

    return window;
}
