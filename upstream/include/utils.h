#ifndef GSR_UTILS_H
#define GSR_UTILS_H

#include "vec2.h"
#include "../include/egl.h"
#include "../include/defs.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct AVCodecContext AVCodecContext;
typedef struct AVFrame AVFrame;
typedef struct gsr_capture_metadata gsr_capture_metadata;

typedef struct {
    const char *name;
    int name_len;
    vec2i pos; /* This is 0, 0 on wayland. Use |drm_monitor_get_display_server_data| to get the position */
    vec2i size;
    vec2i logical_pos;
    vec2i logical_size;
    uint32_t connector_id; /* Only on x11 and drm */
    gsr_monitor_rotation rotation; /* Only on x11 and wayland */
    uint32_t monitor_identifier; /* On x11 this is the crtc id */
} gsr_monitor;

typedef struct {
    const char *name;
    int name_len;
    gsr_monitor *monitor;
    bool found_monitor;
} get_monitor_by_name_userdata;

double clock_get_monotonic_seconds(void);
bool generate_random_characters(char *buffer, int buffer_size, const char *alphabet, size_t alphabet_size);
bool generate_random_characters_standard_alphabet(char *buffer, int buffer_size);

typedef void (*active_monitor_callback)(const gsr_monitor *monitor, void *userdata);
void for_each_active_monitor_output_x11_not_cached(Display *display, active_monitor_callback callback, void *userdata);
void for_each_active_monitor_output(const gsr_window *window, const char *card_path, gsr_connection_type connection_type, active_monitor_callback callback, void *userdata);
bool get_monitor_by_name(const gsr_egl *egl, gsr_connection_type connection_type, const char *name, gsr_monitor *monitor);
bool drm_monitor_get_display_server_data(const gsr_window *window, const gsr_monitor *monitor, gsr_monitor_rotation *monitor_rotation, vec2i *monitor_position);

int get_connector_type_by_name(const char *name);
int get_connector_type_id_by_name(const char *name);
uint32_t monitor_identifier_from_type_and_count(int monitor_type_index, int monitor_type_count);

bool gl_get_gpu_info(gsr_egl *egl, gsr_gpu_info *info);

bool try_card_has_valid_plane(const char *card_path);
/* |output| should be at least 128 bytes in size */
bool gsr_get_valid_card_path(gsr_egl *egl, char *output, bool is_monitor_capture);
/* |render_path| should be at least 128 bytes in size */
bool gsr_card_path_get_render_path(const char *card_path, char *render_path);

int create_directory_recursive(char *path);

/* |img_attr| needs to be at least 44 in size */
void setup_dma_buf_attrs(intptr_t *img_attr, uint32_t format, uint32_t width, uint32_t height, const int *fds, const uint32_t *offsets, const uint32_t *pitches, const uint64_t *modifiers, int num_planes, bool use_modifier);

vec2i scale_keep_aspect_ratio(vec2i from, vec2i to);
vec2i gsr_capture_get_target_position(vec2i output_size, gsr_capture_metadata *capture_metadata);

unsigned int gl_create_texture(gsr_egl *egl, int width, int height, int internal_format, unsigned int format, int filter);

bool get_nvidia_driver_version(int *major, int *minor);

/* Return false from the callback to stop splitting */
typedef bool (*gsr_string_split_callback)(const char *str, size_t size, void *userdata);
void gsr_string_split(const char *str, char delimiter, gsr_string_split_callback callback, void *userdata);

bool gsr_string_starts_with(const char *str, size_t str_size, const char *substr);
bool gsr_string_ends_with(const char *str, const char *substr);

bool gsr_string_to_int(const char *str, size_t size, int *number);
bool gsr_string_to_int64(const char *str, size_t size, int64_t *number);

bool gsr_array_ensure_capacity(void **array, size_t num_items, size_t *capacity_items, size_t item_size);

/* These write a formatted local date/time into |str|, which should be at least 128 bytes in size */
void gsr_get_date_str(char *str, size_t size);
void gsr_get_date_only_str(char *str, size_t size);
void gsr_get_time_only_str(char *str, size_t size);

#endif /* GSR_UTILS_H */
