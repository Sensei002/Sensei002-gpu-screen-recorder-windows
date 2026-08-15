#ifndef GSR_PIPEWIRE_VIDEO_H
#define GSR_PIPEWIRE_VIDEO_H

#include "defs.h"
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <spa/utils/hook.h>
#include <spa/param/video/format.h>

#define GSR_PIPEWIRE_VIDEO_MAX_MODIFIERS 1024
#define GSR_PIPEWIRE_VIDEO_MAX_VIDEO_FORMATS 10
#define GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES 4

typedef struct gsr_egl gsr_egl;

typedef struct {
    int major;
    int minor;
    int micro;
} gsr_pipewire_video_data_version;

typedef struct {
    uint32_t fps_num;
    uint32_t fps_den;
} gsr_pipewire_video_video_info;

typedef struct {
    int fd;
    uint32_t offset;
    int32_t stride;
} gsr_pipewire_video_dmabuf_data;

typedef struct {
    int x, y;
    int width, height;
} gsr_pipewire_video_region;

typedef struct {
    enum spa_video_format format;
    size_t modifiers_index;
    size_t modifiers_size;
} gsr_video_format;

typedef struct {
    unsigned int texture_id;
    unsigned int external_texture_id;
    unsigned int cursor_texture_id;
} gsr_texture_map;

typedef struct {
    gsr_pipewire_video_region region;
    gsr_pipewire_video_region cursor_region;
    gsr_pipewire_video_dmabuf_data dmabuf_data[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    int num_dmabuf_data;
    uint32_t fourcc;
    uint64_t modifiers;
    bool using_external_image;
    gsr_monitor_rotation rotation;
    int texture_width;
    int texture_height;
} gsr_map_texture_output;

typedef struct {
    gsr_egl *egl;
    int fd;
    uint32_t node;
    pthread_mutex_t mutex;
    bool mutex_initialized;

    struct pw_thread_loop *thread_loop;
    struct pw_context *context;
    struct pw_core *core;
    struct spa_hook core_listener;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_source *reneg;
    struct spa_video_info format;
    int server_version_sync;
    bool negotiated;
    bool damaged;
    bool has_modifier;

    struct {
        bool visible;
        bool valid;
        bool updated;
        uint8_t *data;
        int x, y;
        int hotspot_x, hotspot_y;
        int width, height;
    } cursor;

    struct {
        bool valid;
        int x, y;
        uint32_t width, height;
    } crop;

    gsr_video_format supported_video_formats[GSR_PIPEWIRE_VIDEO_MAX_VIDEO_FORMATS];

    gsr_pipewire_video_data_version server_version;
    gsr_pipewire_video_video_info video_info;
    gsr_pipewire_video_dmabuf_data dmabuf_data[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    size_t dmabuf_num_planes;

    bool no_modifiers_fallback;
    bool external_texture_fallback;

    uint64_t modifiers[GSR_PIPEWIRE_VIDEO_MAX_MODIFIERS];
    size_t num_modifiers;

    bool paused;
    double paused_start_secs;
    bool streaming;

    gsr_monitor_rotation rotation;
} gsr_pipewire_video;

/*
    |capture_cursor| only applies to when capturing a window or region.
    In other cases |pipewire_node|'s setup will determine if the cursor is included.
    Note that the cursor is not guaranteed to be shown even if set to true, it depends on the wayland compositor.
*/
bool gsr_pipewire_video_init(gsr_pipewire_video *self, int pipewire_fd, uint32_t pipewire_node, int fps, bool capture_cursor, gsr_egl *egl);
void gsr_pipewire_video_deinit(gsr_pipewire_video *self);

bool gsr_pipewire_video_map_texture(gsr_pipewire_video *self, gsr_texture_map texture_map, gsr_map_texture_output *output);
/*
    Returns true if the cursor has changed (moved, changed image or changed visibility) since the last call to
    gsr_pipewire_video_map_texture or gsr_pipewire_video_update_cursor. This only happens when the wayland compositor
    supports cursor metadata mode. |cursor_region| and the cursor texture in |texture_map| are updated with the latest cursor state.
*/
bool gsr_pipewire_video_update_cursor(gsr_pipewire_video *self, gsr_texture_map texture_map, gsr_pipewire_video_region *cursor_region);
bool gsr_pipewire_video_is_damaged(gsr_pipewire_video *self);
void gsr_pipewire_video_clear_damage(gsr_pipewire_video *self);
bool gsr_pipewire_video_should_restart(gsr_pipewire_video *self);

#endif /* GSR_PIPEWIRE_VIDEO_H */
