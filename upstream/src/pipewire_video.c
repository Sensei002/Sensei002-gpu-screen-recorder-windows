#include "../include/pipewire_video.h"
#include "../include/log.h"
#include "../include/egl.h"
#include "../include/utils.h"

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/debug/types.h>

#include <drm_fourcc.h>

#include <fcntl.h>
#include <unistd.h>

/* This code is partially based on xr-video-player pipewire implementation which is based on obs-studio's pipewire implementation */

/* TODO: Make gsr_pipewire_video_init asynchronous */
/* TODO: Support hdr when pipewire supports it */
/* TODO: Test all of the image formats */

#ifndef SPA_POD_PROP_FLAG_DONT_FIXATE
#define SPA_POD_PROP_FLAG_DONT_FIXATE (1 << 4)
#endif

#if !PW_CHECK_VERSION(0, 3, 62)
enum spa_meta_videotransform_value {
    SPA_META_TRANSFORMATION_None = 0,    /**< no transform */
    SPA_META_TRANSFORMATION_90,          /**< 90 degree counter-clockwise */
    SPA_META_TRANSFORMATION_180,         /**< 180 degree counter-clockwise */
    SPA_META_TRANSFORMATION_270,         /**< 270 degree counter-clockwise */
    SPA_META_TRANSFORMATION_Flipped,     /**< 180 degree flipped around the vertical axis. Equivalent
                                           * to a reflexion through the vertical line splitting the
                                           * buffer in two equal sized parts */
    SPA_META_TRANSFORMATION_Flipped90,   /**< flip then rotate around 90 degree counter-clockwise */
    SPA_META_TRANSFORMATION_Flipped180,  /**< flip then rotate around 180 degree counter-clockwise */
    SPA_META_TRANSFORMATION_Flipped270,  /**< flip then rotate around 270 degree counter-clockwise */
};

/** a transformation of the buffer */
struct spa_meta_videotransform {
    uint32_t transform;                  /**< orientation transformation that was applied to the buffer,
                                           *  one of enum spa_meta_videotransform_value */
};

#define SPA_META_VideoTransform 8
#endif

#define CURSOR_META_SIZE(width, height)                                    \
    (sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + \
     width * height * 4)

static bool parse_pw_version(gsr_pipewire_video_data_version *dst, const char *version) {
    const int n_matches = sscanf(version, "%d.%d.%d", &dst->major, &dst->minor, &dst->micro);
    return n_matches == 3;
}

static bool check_pw_version(const gsr_pipewire_video_data_version *pw_version, int major, int minor, int micro) {
    if (pw_version->major != major)
        return pw_version->major > major;
    if (pw_version->minor != minor)
        return pw_version->minor > minor;
    return pw_version->micro >= micro;
}

static void update_pw_versions(gsr_pipewire_video *self, const char *version) {
    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: server version: %s", version);
    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: library version: %s", pw_get_library_version());
    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: header version: %s", pw_get_headers_version());
    if(!parse_pw_version(&self->server_version, version))
        gsr_log(GSR_LOG_LEVEL_ERROR, "pipewire: failed to parse server version");
}

static void on_core_info_cb(void *user_data, const struct pw_core_info *info) {
    gsr_pipewire_video *self = user_data;
    update_pw_versions(self, info->version);
}

static void on_core_error_cb(void *user_data, uint32_t id, int seq, int res, const char *message) {
    gsr_pipewire_video *self = user_data;
    gsr_log(GSR_LOG_LEVEL_ERROR, "pipewire: error id:%u seq:%d res:%d: %s", id, seq, res, message);
    pw_thread_loop_signal(self->thread_loop, false);
}

static void on_core_done_cb(void *user_data, uint32_t id, int seq) {
    gsr_pipewire_video *self = user_data;
    if (id == PW_ID_CORE && self->server_version_sync == seq)
        pw_thread_loop_signal(self->thread_loop, false);
}

static bool is_cursor_format_supported(const enum spa_video_format format) {
    switch(format) {
        case SPA_VIDEO_FORMAT_RGBx:       return true;
        case SPA_VIDEO_FORMAT_BGRx:       return true;
        case SPA_VIDEO_FORMAT_RGBA:       return true;
        case SPA_VIDEO_FORMAT_BGRA:       return true;
        case SPA_VIDEO_FORMAT_RGB:        return true;
        case SPA_VIDEO_FORMAT_BGR:        return true;
        case SPA_VIDEO_FORMAT_ARGB:       return true;
        case SPA_VIDEO_FORMAT_ABGR:       return true;
#if PW_CHECK_VERSION(0, 3, 41)
        case SPA_VIDEO_FORMAT_xRGB_210LE: return true;
        case SPA_VIDEO_FORMAT_xBGR_210LE: return true;
        case SPA_VIDEO_FORMAT_ARGB_210LE: return true;
        case SPA_VIDEO_FORMAT_ABGR_210LE: return true;
#endif
        default:                    break;
    }
    return false;
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .info = on_core_info_cb,
    .done = on_core_done_cb,
    .error = on_core_error_cb,
};

static void gsr_pipewire_video_read_cursor_metadata(gsr_pipewire_video *self, struct spa_buffer *buffer) {
    const struct spa_meta_cursor *cursor = spa_buffer_find_meta_data(buffer, SPA_META_Cursor, sizeof(*cursor));
    if(!cursor)
        return;

    /* The cursor can become invalid when it moves off the captured window/monitor, in which case it should be hidden */
    const bool cursor_valid = spa_meta_cursor_is_valid(cursor);
    if(cursor_valid != self->cursor.valid) {
        self->cursor.updated = true;
        self->damaged = true;
    }
    self->cursor.valid = cursor_valid;

    if(!self->cursor.visible || !self->cursor.valid)
        return;

    struct spa_meta_bitmap *bitmap = NULL;
    if(cursor->bitmap_offset)
        bitmap = SPA_MEMBER(cursor, cursor->bitmap_offset, struct spa_meta_bitmap);

    // TODO: Maybe check if the cursor is actually visible by checking if there are visible pixels
    if(bitmap && bitmap->size.width > 0 && bitmap->size.height > 0 && is_cursor_format_supported(bitmap->format)) {
        /* Animated cursors update the bitmap for every animation frame, only log when the size changes */
        if((int)bitmap->size.width != self->cursor.width || (int)bitmap->size.height != self->cursor.height) {
            gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: cursor bitmap update, size: %dx%d, format: %s", (int)bitmap->size.width, (int)bitmap->size.height, spa_debug_type_find_name(spa_type_video_format, bitmap->format));
        }

        const uint8_t *bitmap_data = SPA_MEMBER(bitmap, bitmap->offset, uint8_t);
        const size_t bitmap_size = bitmap->size.width * bitmap->size.height * 4;
        uint8_t *new_bitmap_data = realloc(self->cursor.data, bitmap_size);
        if(new_bitmap_data) {
            self->cursor.data = new_bitmap_data;
            /* TODO: Convert bgr and other image formats to rgb here */
            memcpy(self->cursor.data, bitmap_data, bitmap_size);

            self->cursor.hotspot_x = cursor->hotspot.x;
            self->cursor.hotspot_y = cursor->hotspot.y;
            self->cursor.width = bitmap->size.width;
            self->cursor.height = bitmap->size.height;
            self->cursor.updated = true;
            self->damaged = true;
        }
    }

    /* Position changes for a cursor that cant be drawn (no bitmap received yet) shouldn't trigger a redraw */
    if(self->cursor.width > 0 && (cursor->position.x != self->cursor.x || cursor->position.y != self->cursor.y)) {
        self->cursor.updated = true;
        self->damaged = true;
    }

    self->cursor.x = cursor->position.x;
    self->cursor.y = cursor->position.y;

    //fprintf(stderr, "gsr info: pipewire: cursor: %d %d %d %d\n", cursor->hotspot.x, cursor->hotspot.y, cursor->position.x, cursor->position.y);
}

static bool buffer_has_video_content(const struct spa_buffer *buffer) {
    /* Cursor-only updates in cursor metadata mode are sent as buffers without valid video content
       (chunk size 0 on gnome, chunk marked as corrupted on kde plasma) */
    return buffer->n_datas > 0 && buffer->datas[0].chunk->size != 0 && !(buffer->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED);
}

static void on_process_cb(void *user_data) {
    gsr_pipewire_video *self = user_data;

    /* Find the most recent buffer with video content. The cursor metadata is read from all buffers in the order they arrive
       because a batch of buffers can contain both video buffers and cursor-only buffers (in cursor metadata mode)
       and a video buffer shouldn't be discarded just because a cursor-only buffer arrived after it. */
    bool got_buffer = false;
    struct pw_buffer *pw_buf = NULL;
    for(;;) {
        struct pw_buffer *aux = pw_stream_dequeue_buffer(self->stream);
        if(!aux)
            break;

        got_buffer = true;
        pthread_mutex_lock(&self->mutex);
        gsr_pipewire_video_read_cursor_metadata(self, aux->buffer);
        pthread_mutex_unlock(&self->mutex);

        if(buffer_has_video_content(aux->buffer)) {
            if(pw_buf)
                pw_stream_queue_buffer(self->stream, pw_buf);
            pw_buf = aux;
        } else {
            pw_stream_queue_buffer(self->stream, aux);
        }
    }

    if(!got_buffer) {
        gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: out of buffers!");
        return;
    }

    /* Only cursor-only buffers arrived, theres no new video frame to process */
    if(!pw_buf)
        return;

    struct spa_buffer *buffer = pw_buf->buffer;

    pthread_mutex_lock(&self->mutex);

    bool buffer_updated = false;
    if(buffer->datas[0].type == SPA_DATA_DmaBuf) {
        for(size_t i = 0; i < self->dmabuf_num_planes; ++i) {
            if(self->dmabuf_data[i].fd > 0) {
                close(self->dmabuf_data[i].fd);
                self->dmabuf_data[i].fd = -1;
            }
        }

        self->dmabuf_num_planes = buffer->n_datas;
        if(self->dmabuf_num_planes > GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES)
            self->dmabuf_num_planes = GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES;

        for(size_t i = 0; i < self->dmabuf_num_planes; ++i) {
            self->dmabuf_data[i].fd = dup(buffer->datas[i].fd);
            self->dmabuf_data[i].offset = buffer->datas[i].chunk->offset;
            self->dmabuf_data[i].stride = buffer->datas[i].chunk->stride;
        }

        buffer_updated = true;
    }

    // TODO: Move down to read_metadata
    struct spa_meta_region *region = spa_buffer_find_meta_data(buffer, SPA_META_VideoCrop, sizeof(*region));
    if(region && spa_meta_region_is_valid(region)) {
        // fprintf(stderr, "gsr info: pipewire: crop Region available (%dx%d+%d+%d)\n",
        //      region->region.position.x, region->region.position.y,
        //      region->region.size.width, region->region.size.height);
        self->crop.x = region->region.position.x;
        self->crop.y = region->region.position.y;
        self->crop.width = region->region.size.width;
        self->crop.height = region->region.size.height;
        self->crop.valid = true;
    } else {
        self->crop.valid = false;
    }

    struct spa_meta_videotransform *video_transform = spa_buffer_find_meta_data(buffer, SPA_META_VideoTransform, sizeof(*video_transform));
    enum spa_meta_videotransform_value transform = SPA_META_TRANSFORMATION_None;
    if(video_transform)
        transform = video_transform->transform;

    self->rotation = GSR_MONITOR_ROT_0;
    switch(transform) {
        case SPA_META_TRANSFORMATION_90:
            self->rotation = GSR_MONITOR_ROT_90;
            break;
        case SPA_META_TRANSFORMATION_180:
            self->rotation = GSR_MONITOR_ROT_180;
            break;
        case SPA_META_TRANSFORMATION_270:
            self->rotation = GSR_MONITOR_ROT_270;
            break;
        default:
            // TODO: Support other rotations. Wayland compositors dont use them yet so it's ok to not support it now
            break;
    }

    const struct spa_meta *video_damage = spa_buffer_find_meta(buffer, SPA_META_VideoDamage);
    if(video_damage) {
        struct spa_meta_region *meta_region = NULL;
        spa_meta_for_each(meta_region, video_damage) {
            if(meta_region->region.size.width == 0 || meta_region->region.size.height == 0)
                continue;

            self->damaged = true;
            break;
        }
    } else if(buffer_updated) {
        self->damaged = true;
    }

    pthread_mutex_unlock(&self->mutex);
    pw_stream_queue_buffer(self->stream, pw_buf);
}

static void on_param_changed_cb(void *user_data, uint32_t id, const struct spa_pod *param) {
    gsr_pipewire_video *self = user_data;

    if (!param || id != SPA_PARAM_Format)
        return;

    int result = spa_format_parse(param, &self->format.media_type, &self->format.media_subtype);
    if (result < 0)
        return;

    if (self->format.media_type != SPA_MEDIA_TYPE_video || self->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    pthread_mutex_lock(&self->mutex);
    spa_format_video_raw_parse(param, &self->format.info.raw);
    pthread_mutex_unlock(&self->mutex);

    uint32_t buffer_types = 0;
    const bool has_modifier = spa_pod_find_prop(param, NULL, SPA_FORMAT_VIDEO_modifier) != NULL;
    if(has_modifier || check_pw_version(&self->server_version, 0, 3, 24))
        buffer_types |= 1 << SPA_DATA_DmaBuf;

    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: negotiated format:");

    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire:    Format: %d (%s)", self->format.info.raw.format,
         spa_debug_type_find_name(spa_type_video_format, self->format.info.raw.format));

    self->has_modifier = has_modifier;
    if(self->has_modifier) {
        gsr_log(GSR_LOG_LEVEL_INFO, "pipewire:    Modifier: 0x%" PRIx64, self->format.info.raw.modifier);
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire:    Size: %dx%d", self->format.info.raw.size.width, self->format.info.raw.size.height);
    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire:    Framerate: %d/%d", self->format.info.raw.framerate.num, self->format.info.raw.framerate.denom);

    uint8_t params_buffer[2048];
    struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
    const struct spa_pod *params[5];
    int param_index = 0;

    params[param_index++] = spa_pod_builder_add_object(
        &pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
        SPA_PARAM_META_size,
        SPA_POD_Int(sizeof(struct spa_meta_region)));

    params[param_index++] = spa_pod_builder_add_object(
        &pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage),
        SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
                                sizeof(struct spa_meta_region) * 16,
                                sizeof(struct spa_meta_region) * 1,
                                sizeof(struct spa_meta_region) * 16));

    params[param_index++] = spa_pod_builder_add_object(
        &pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
        SPA_PARAM_META_size,
        SPA_POD_CHOICE_RANGE_Int(CURSOR_META_SIZE(64, 64),
                     CURSOR_META_SIZE(1, 1),
                     CURSOR_META_SIZE(1024, 1024)));

    params[param_index++] = spa_pod_builder_add_object(
        &pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(buffer_types));

#if PW_CHECK_VERSION(0, 3, 62)
	if (check_pw_version(&self->server_version, 0, 3, 62)) {
		/* Video transformation */
		params[param_index++] = spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
								SPA_PARAM_META_type,
								SPA_POD_Id(SPA_META_VideoTransform),
								SPA_PARAM_META_size,
								SPA_POD_Int(sizeof(struct spa_meta_videotransform)));
	}
#endif

    pw_stream_update_params(self->stream, params, param_index);
    self->negotiated = true;
}

static void on_state_changed_cb(void *user_data, enum pw_stream_state prev_state, enum pw_stream_state new_state, const char *error) {
    gsr_pipewire_video *self = user_data;

    gsr_log(GSR_LOG_LEVEL_INFO, "pipewire: stream %p previous state: \"%s\", new state: \"%s\" (error: %s)", (void*)self->stream, pw_stream_state_as_string(prev_state), pw_stream_state_as_string(new_state),
         error ? error : "none");

    if(new_state == PW_STREAM_STATE_ERROR && error && strstr(error, "alloc buffers")) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "pipewire: the desktop portal failed to provide dmabuf (GPU) video frames. This happens when the compositor only provides the frames as shared memory (CPU), which gpu-screen-recorder intentionally doesn't support for portal capture. Capture the monitor directly (for example '-w screen' or '-w <monitor-name>', see 'gpu-screen-recorder --list-capture-options') instead of '-w portal'.");
    }

    pthread_mutex_lock(&self->mutex);
    if(new_state == PW_STREAM_STATE_PAUSED) {
        self->paused_start_secs = clock_get_monotonic_seconds();
        self->paused = true;
    } else {
        self->paused = false;
    }
    self->streaming = (new_state == PW_STREAM_STATE_STREAMING);
    pthread_mutex_unlock(&self->mutex);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed_cb,
    .param_changed = on_param_changed_cb,
    .process = on_process_cb,
};

static inline struct spa_pod *build_format(struct spa_pod_builder *b,
                       const gsr_pipewire_video_video_info *ovi,
                       uint32_t format, const uint64_t *modifiers,
                       size_t modifier_count)
{
    struct spa_pod_frame format_frame;

    spa_pod_builder_push_object(b, &format_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);

    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);

    if (modifier_count > 0) {
        struct spa_pod_frame modifier_frame;

        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &modifier_frame, SPA_CHOICE_Enum, 0);

        /* The first element of choice pods is the preferred value. Here
         * we arbitrarily pick the first modifier as the preferred one.
         */
        // TODO:
        spa_pod_builder_long(b, modifiers[0]);

        for(uint32_t i = 0; i < modifier_count; i++)
            spa_pod_builder_long(b, modifiers[i]);

        spa_pod_builder_pop(b, &modifier_frame);
    }

    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size,
                SPA_POD_CHOICE_RANGE_Rectangle(
                    &SPA_RECTANGLE(32, 32),
                    &SPA_RECTANGLE(1, 1),
                    &SPA_RECTANGLE(16384, 16384)),
                SPA_FORMAT_VIDEO_framerate,
                SPA_POD_CHOICE_RANGE_Fraction(
                    &SPA_FRACTION(ovi->fps_num, ovi->fps_den),
                    &SPA_FRACTION(0, 1), &SPA_FRACTION(500, 1)),
                0);
    return spa_pod_builder_pop(b, &format_frame);
}

/* https://gstreamer.freedesktop.org/documentation/additional/design/mediatype-video-raw.html?gi-language=c#formats */
/* For some reason gstreamer formats are in opposite order to drm formats */
static int64_t spa_video_format_to_drm_format(const enum spa_video_format format) {
    switch(format) {
        case SPA_VIDEO_FORMAT_RGBx:       return DRM_FORMAT_XBGR8888;
        case SPA_VIDEO_FORMAT_BGRx:       return DRM_FORMAT_XRGB8888;
        case SPA_VIDEO_FORMAT_RGBA:       return DRM_FORMAT_ABGR8888;
        case SPA_VIDEO_FORMAT_BGRA:       return DRM_FORMAT_ARGB8888;
        case SPA_VIDEO_FORMAT_RGB:        return DRM_FORMAT_XBGR8888;
        case SPA_VIDEO_FORMAT_BGR:        return DRM_FORMAT_XRGB8888;
        //case SPA_VIDEO_FORMAT_ARGB:       return DRM_FORMAT_BGRA8888;
        //case SPA_VIDEO_FORMAT_ABGR:       return DRM_FORMAT_RGBA8888;
#if PW_CHECK_VERSION(0, 3, 41)
        case SPA_VIDEO_FORMAT_xRGB_210LE: return DRM_FORMAT_XRGB2101010;
        case SPA_VIDEO_FORMAT_xBGR_210LE: return DRM_FORMAT_XBGR2101010;
        case SPA_VIDEO_FORMAT_ARGB_210LE: return DRM_FORMAT_ARGB2101010;
        case SPA_VIDEO_FORMAT_ABGR_210LE: return DRM_FORMAT_ABGR2101010;
#endif
        default:                          break;
    }
    return DRM_FORMAT_INVALID;
}

#if PW_CHECK_VERSION(0, 3, 41)
#define GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS GSR_PIPEWIRE_VIDEO_MAX_VIDEO_FORMATS
#else
#define GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS 6
#endif

static const enum spa_video_format video_formats[GSR_PIPEWIRE_VIDEO_MAX_VIDEO_FORMATS] = {
    SPA_VIDEO_FORMAT_BGRx,
    SPA_VIDEO_FORMAT_BGR,
    SPA_VIDEO_FORMAT_RGBx,
    SPA_VIDEO_FORMAT_RGB,
    SPA_VIDEO_FORMAT_RGBA,
    SPA_VIDEO_FORMAT_BGRA,
    //SPA_VIDEO_FORMAT_ARGB,
    //SPA_VIDEO_FORMAT_ABGR,
#if PW_CHECK_VERSION(0, 3, 41)
    SPA_VIDEO_FORMAT_xRGB_210LE,
    SPA_VIDEO_FORMAT_xBGR_210LE,
    SPA_VIDEO_FORMAT_ARGB_210LE,
    SPA_VIDEO_FORMAT_ABGR_210LE,
#endif
};

static bool gsr_pipewire_video_build_format_params(gsr_pipewire_video *self, struct spa_pod_builder *pod_builder, struct spa_pod **params, uint32_t *num_params) {
    *num_params = 0;

    if(check_pw_version(&self->server_version, 0, 3, 33)) {
        for(size_t i = 0; i < GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS; i++) {
            if(self->supported_video_formats[i].modifiers_size == 0)
                continue;
            params[*num_params] = build_format(pod_builder, &self->video_info, self->supported_video_formats[i].format, self->modifiers + self->supported_video_formats[i].modifiers_index, self->supported_video_formats[i].modifiers_size);
            ++(*num_params);
        }
    }

    /* Fallback formats without modifiers, needed for negotiation when the compositor doesn't support dma-buf modifier negotiation */
    for(size_t i = 0; i < GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS; i++) {
        params[*num_params] = build_format(pod_builder, &self->video_info, self->supported_video_formats[i].format, NULL, 0);
        ++(*num_params);
    }

    return true;
}

static void renegotiate_format(void *data, uint64_t expirations) {
    (void)expirations;
    gsr_pipewire_video *self = (gsr_pipewire_video*)data;

    pw_thread_loop_lock(self->thread_loop);

    struct spa_pod *params[GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS * 2];
    uint32_t num_video_formats = 0;
    uint8_t params_buffer[8192];
    struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
    if (!gsr_pipewire_video_build_format_params(self, &pod_builder, params, &num_video_formats)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "renegotiate_format: failed to build formats");
        pw_thread_loop_unlock(self->thread_loop);
        return;
    }

    pw_stream_update_params(self->stream, (const struct spa_pod**)params, num_video_formats);
    pw_thread_loop_unlock(self->thread_loop);
}

static bool spa_video_format_get_modifiers(gsr_pipewire_video *self, const enum spa_video_format format, uint64_t *modifiers, int32_t max_modifiers, int32_t *num_modifiers) {
    *num_modifiers = 0;

    if(max_modifiers == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "spa_video_format_get_modifiers: no space for modifiers left");
        //modifiers[0] = DRM_FORMAT_MOD_LINEAR;
        //modifiers[1] = DRM_FORMAT_MOD_INVALID;
        //*num_modifiers = 2;
        return false;
    }

    if(!self->egl->eglQueryDmaBufModifiersEXT) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "spa_video_format_get_modifiers: failed to initialize modifiers because eglQueryDmaBufModifiersEXT is not available");
        //modifiers[0] = DRM_FORMAT_MOD_LINEAR;
        //modifiers[1] = DRM_FORMAT_MOD_INVALID;
        //*num_modifiers = 2;
        modifiers[0] = DRM_FORMAT_MOD_INVALID;
        *num_modifiers = 1;
        return false;
    }

    const int64_t drm_format = spa_video_format_to_drm_format(format);
    if(drm_format == DRM_FORMAT_INVALID) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "spa_video_format_get_modifiers: unsupported format: %d", (int)format);
        return false;
    }

    if(!self->egl->eglQueryDmaBufModifiersEXT(self->egl->egl_display, drm_format, max_modifiers, modifiers, NULL, num_modifiers)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "spa_video_format_get_modifiers: eglQueryDmaBufModifiersEXT failed with drm format %d, %" PRIi64, (int)format, drm_format);
        modifiers[0] = DRM_FORMAT_MOD_INVALID;
        *num_modifiers = 1;
        return false;
    }

    if(*num_modifiers < max_modifiers) {
        modifiers[*num_modifiers] = DRM_FORMAT_MOD_INVALID;
        *num_modifiers += 1;
    }
    return true;
}

static void gsr_pipewire_video_init_modifiers(gsr_pipewire_video *self) {
    for(size_t i = 0; i < GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS; i++) {
        self->supported_video_formats[i].format = video_formats[i];
        int32_t num_modifiers_video_format = 0;
        spa_video_format_get_modifiers(self, self->supported_video_formats[i].format, self->modifiers + self->num_modifiers, GSR_PIPEWIRE_VIDEO_MAX_MODIFIERS - self->num_modifiers, &num_modifiers_video_format);
        self->supported_video_formats[i].modifiers_index = self->num_modifiers;
        self->supported_video_formats[i].modifiers_size = num_modifiers_video_format;
        self->num_modifiers += num_modifiers_video_format;
    }
}

/* Returns the number of modifiers  */
static size_t gsr_pipewire_video_format_remove_modifier(gsr_pipewire_video *self, gsr_video_format *video_format, uint64_t modifier) {
    for(size_t i = 0; i < video_format->modifiers_size; ++i) {
        if(self->modifiers[video_format->modifiers_index + i] == modifier) {
            for(size_t j = i + 1; j < video_format->modifiers_size; ++j) {
                self->modifiers[video_format->modifiers_index + j - 1] = self->modifiers[video_format->modifiers_index + j];
            }

            --video_format->modifiers_size;
            break;
        }
    }
    return video_format->modifiers_size;
}

static void gsr_pipewire_video_remove_modifier(gsr_pipewire_video *self, uint64_t modifier) {
    for(size_t i = 0; i < GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS; i++) {
        gsr_pipewire_video_format_remove_modifier(self, &self->supported_video_formats[i], modifier);
    }
}

static bool gsr_pipewire_video_setup_stream(gsr_pipewire_video *self) {
    struct spa_pod *params[GSR_PIPEWIRE_VIDEO_NUM_VIDEO_FORMATS * 2];
    uint32_t num_video_formats = 0;
    uint8_t params_buffer[8192];
    struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));

    self->thread_loop = pw_thread_loop_new("gsr screen capture", NULL);
    if(!self->thread_loop) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to create pipewire thread");
        goto error;
    }

    self->context = pw_context_new(pw_thread_loop_get_loop(self->thread_loop), NULL, 0);
    if(!self->context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to create pipewire context");
        goto error;
    }

    if(pw_thread_loop_start(self->thread_loop) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to start thread");
        goto error;
    }

    pw_thread_loop_lock(self->thread_loop);

    // TODO: Why pass 5 to fcntl?
    self->core = pw_context_connect_fd(self->context, fcntl(self->fd, F_DUPFD_CLOEXEC, 5), NULL, 0);
    if(!self->core) {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to connect to fd %d", self->fd);
        goto error;
    }

    // TODO: Error check
    pw_core_add_listener(self->core, &self->core_listener, &core_events, self);

    self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, 0);
    pw_thread_loop_wait(self->thread_loop);

    gsr_pipewire_video_init_modifiers(self);

    // TODO: Cleanup?
    self->reneg = pw_loop_add_event(pw_thread_loop_get_loop(self->thread_loop), renegotiate_format, self);
    if(!self->reneg) {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: pw_loop_add_event failed");
        goto error;
    }

    self->stream = pw_stream_new(self->core, "com.dec05eba.gpu_screen_recorder",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                          PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Screen", NULL));
    if(!self->stream) {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to create stream");
        goto error;
    }
    pw_stream_add_listener(self->stream, &self->stream_listener, &stream_events, self);

    if(!gsr_pipewire_video_build_format_params(self, &pod_builder, params, &num_video_formats)) {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to build format params");
        goto error;
    }

    if(pw_stream_connect(
        self->stream, PW_DIRECTION_INPUT, self->node,
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, (const struct spa_pod**)params,
        num_video_formats) < 0)
    {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_setup_stream: failed to connect stream");
        goto error;
    }

    pw_thread_loop_unlock(self->thread_loop);
    return true;

    error:
    if(self->thread_loop) {
        //pw_thread_loop_wait(self->thread_loop);
        pw_thread_loop_stop(self->thread_loop);
    }

    if(self->stream) {
        pw_stream_disconnect(self->stream);
        pw_stream_destroy(self->stream);
        self->stream = NULL;
    }

    if(self->core) {
        pw_core_disconnect(self->core);
        self->core = NULL;
    }

    if(self->context) {
        pw_context_destroy(self->context);
        self->context = NULL;
    }

    if(self->thread_loop) {
        pw_thread_loop_destroy(self->thread_loop);
        self->thread_loop = NULL;
    }
    return false;
}

static int pw_init_counter = 0;
bool gsr_pipewire_video_init(gsr_pipewire_video *self, int pipewire_fd, uint32_t pipewire_node, int fps, bool capture_cursor, gsr_egl *egl) {
    if(pw_init_counter == 0)
        pw_init(NULL, NULL);
    ++pw_init_counter;

    memset(self, 0, sizeof(*self));
    self->egl = egl;
    self->fd = pipewire_fd;
    self->node = pipewire_node;
    if(pthread_mutex_init(&self->mutex, NULL) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_init: failed to initialize mutex");
        gsr_pipewire_video_deinit(self);
        return false;
    }
    self->mutex_initialized = true;
    self->video_info.fps_num = fps;
    self->video_info.fps_den = 1;
    self->cursor.visible = capture_cursor;
    
    if(!gsr_pipewire_video_setup_stream(self)) {
        gsr_pipewire_video_deinit(self);
        return false;
    }

    return true;
}

void gsr_pipewire_video_deinit(gsr_pipewire_video *self) {
    if(self->thread_loop) {
        //pw_thread_loop_wait(self->thread_loop);
        pw_thread_loop_stop(self->thread_loop);
    }

    if(self->stream) {
        pw_stream_disconnect(self->stream);
        pw_stream_destroy(self->stream);
        self->stream = NULL;
    }

    if(self->core) {
        pw_core_disconnect(self->core);
        self->core = NULL;
    }

    if(self->context) {
        pw_context_destroy(self->context);
        self->context = NULL;
    }

    if(self->thread_loop) {
        pw_thread_loop_destroy(self->thread_loop);
        self->thread_loop = NULL;
    }

    if(self->fd > 0) {
        close(self->fd);
        self->fd = -1;
    }

    for(size_t i = 0; i < self->dmabuf_num_planes; ++i) {
        if(self->dmabuf_data[i].fd > 0) {
            close(self->dmabuf_data[i].fd);
            self->dmabuf_data[i].fd = -1;
        }
    }
    self->dmabuf_num_planes = 0;

    self->negotiated = false;

    if(self->mutex_initialized) {
        pthread_mutex_destroy(&self->mutex);
        self->mutex_initialized = false;
    }

    if(self->cursor.data) {
        free(self->cursor.data);
        self->cursor.data = NULL;
    }

    --pw_init_counter;
    if(pw_init_counter == 0) {
#if PW_CHECK_VERSION(0, 3, 49)
        pw_deinit();
#endif
    }
}

static EGLImage gsr_pipewire_video_create_egl_image(gsr_pipewire_video *self, const int *fds, const uint32_t *offsets, const uint32_t *pitches, const uint64_t *modifiers, bool use_modifiers) {
    intptr_t img_attr[44];
    setup_dma_buf_attrs(img_attr, spa_video_format_to_drm_format(self->format.info.raw.format), self->format.info.raw.size.width, self->format.info.raw.size.height,
        fds, offsets, pitches, modifiers, self->dmabuf_num_planes, use_modifiers);
    while(self->egl->eglGetError() != EGL_SUCCESS){}
    EGLImage image = self->egl->eglCreateImage(self->egl->egl_display, 0, EGL_LINUX_DMA_BUF_EXT, NULL, img_attr);
    if(!image || self->egl->eglGetError() != EGL_SUCCESS) {
        if(image)
            self->egl->eglDestroyImage(self->egl->egl_display, image);
        return NULL;
    }
    return image;
}

static EGLImage gsr_pipewire_video_create_egl_image_with_fallback(gsr_pipewire_video *self) {
    int fds[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    uint32_t offsets[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    uint32_t pitches[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    uint64_t modifiers[GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES];
    for(size_t i = 0; i < self->dmabuf_num_planes; ++i) {
        fds[i] = self->dmabuf_data[i].fd;
        offsets[i] = self->dmabuf_data[i].offset;
        pitches[i] = self->dmabuf_data[i].stride;
        modifiers[i] = self->format.info.raw.modifier;
    }

    EGLImage image = NULL;
    if(self->no_modifiers_fallback || !self->has_modifier) {
        image = gsr_pipewire_video_create_egl_image(self, fds, offsets, pitches, modifiers, false);
    } else {
        image = gsr_pipewire_video_create_egl_image(self, fds, offsets, pitches, modifiers, true);
        if(!image) {
            if(self->format.info.raw.modifier == DRM_FORMAT_MOD_INVALID) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_create_egl_image_with_fallback: failed to create egl image with modifiers, trying without modifiers");
                self->no_modifiers_fallback = true;
                image = gsr_pipewire_video_create_egl_image(self, fds, offsets, pitches, modifiers, false);
            } else {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_create_egl_image_with_fallback: failed to create egl image with modifier 0x%" PRIx64 ", renegotiating with a different modifier", self->format.info.raw.modifier);
                self->negotiated = false;
                pw_thread_loop_lock(self->thread_loop);
                gsr_pipewire_video_remove_modifier(self, self->format.info.raw.modifier);
                pw_loop_signal_event(pw_thread_loop_get_loop(self->thread_loop), self->reneg);
                pw_thread_loop_unlock(self->thread_loop);
            }
        }
    }
    return image;
}

static bool gsr_pipewire_video_bind_image_to_texture(gsr_pipewire_video *self, EGLImage image, unsigned int texture_id, bool external_texture) {
    const int texture_target = external_texture ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
    while(self->egl->glGetError() != 0){}
    self->egl->glBindTexture(texture_target, texture_id);
    self->egl->glEGLImageTargetTexture2DOES(texture_target, image);
    const bool success = self->egl->glGetError() == 0;
    self->egl->glBindTexture(texture_target, 0);
    return success;
}

static void gsr_pipewire_video_bind_image_to_texture_with_fallback(gsr_pipewire_video *self, gsr_texture_map texture_map, EGLImage image) {
    if(self->external_texture_fallback) {
        gsr_pipewire_video_bind_image_to_texture(self, image, texture_map.external_texture_id, true);
    } else {
        if(!gsr_pipewire_video_bind_image_to_texture(self, image, texture_map.texture_id, false)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_video_map_texture: failed to bind image to texture, trying with external texture");
            self->external_texture_fallback = true;
            gsr_pipewire_video_bind_image_to_texture(self, image, texture_map.external_texture_id, true);
        }
    }
}

static void gsr_pipewire_video_update_cursor_texture(gsr_pipewire_video *self, gsr_texture_map texture_map) {
    if(!self->cursor.data)
        return;

    self->egl->glBindTexture(GL_TEXTURE_2D, texture_map.cursor_texture_id);
    // TODO: glTextureSubImage2D if same size
    self->egl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, self->cursor.width, self->cursor.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, self->cursor.data);
    self->egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->egl->glBindTexture(GL_TEXTURE_2D, 0);

    free(self->cursor.data);
    self->cursor.data = NULL;
}

static void gsr_pipewire_video_update_cursor_data(gsr_pipewire_video *self, gsr_texture_map texture_map, gsr_pipewire_video_region *cursor_region) {
    self->cursor.updated = false;
    gsr_pipewire_video_update_cursor_texture(self, texture_map);

    if(self->cursor.valid) {
        cursor_region->x = self->cursor.x - self->cursor.hotspot_x;
        cursor_region->y = self->cursor.y - self->cursor.hotspot_y;

        cursor_region->width = self->cursor.width;
        cursor_region->height = self->cursor.height;
    } else {
        cursor_region->x = 0;
        cursor_region->y = 0;

        cursor_region->width = 0;
        cursor_region->height = 0;
    }
}

bool gsr_pipewire_video_map_texture(gsr_pipewire_video *self, gsr_texture_map texture_map, gsr_map_texture_output *output) {
    pthread_mutex_lock(&self->mutex);

    if(!self->negotiated || !self->streaming || self->dmabuf_data[0].fd <= 0) {
        pthread_mutex_unlock(&self->mutex);
        return false;
    }

    EGLImage image = gsr_pipewire_video_create_egl_image_with_fallback(self);
    if(!image) {
        pthread_mutex_unlock(&self->mutex);
        return false;
    }

    /* |output| is only written to from this point on. When this function returns false the caller
       can keep using the data from the previous successful call to redraw the previous video frame. */
    for(int i = 0; i < GSR_PIPEWIRE_VIDEO_DMABUF_MAX_PLANES; ++i) {
        memset(&output->dmabuf_data[i], 0, sizeof(gsr_pipewire_video_dmabuf_data));
    }

    gsr_pipewire_video_bind_image_to_texture_with_fallback(self, texture_map, image);
    output->using_external_image = self->external_texture_fallback;
    self->egl->eglDestroyImage(self->egl->egl_display, image);

    output->texture_width = self->format.info.raw.size.width;
    output->texture_height = self->format.info.raw.size.height;

    output->region.x = 0;
    output->region.y = 0;

    output->region.width = self->format.info.raw.size.width;
    output->region.height = self->format.info.raw.size.height;

    if(self->crop.valid) {
        output->region.x = self->crop.x;
        output->region.y = self->crop.y;

        output->region.width = self->crop.width;
        output->region.height = self->crop.height;
    }

    // TODO: Test transform + cropping
    if(self->rotation == GSR_MONITOR_ROT_90 || self->rotation == GSR_MONITOR_ROT_270) {
        const int temp = output->region.width;
        output->region.width = output->region.height;
        output->region.height = temp;
    }

    gsr_pipewire_video_update_cursor_data(self, texture_map, &output->cursor_region);

    for(size_t i = 0; i < self->dmabuf_num_planes; ++i) {
        output->dmabuf_data[i] = self->dmabuf_data[i];
        self->dmabuf_data[i].fd = -1;
    }
    output->num_dmabuf_data = self->dmabuf_num_planes;
    output->fourcc = spa_video_format_to_drm_format(self->format.info.raw.format);
    output->modifiers = self->format.info.raw.modifier;
    output->rotation = self->rotation;
    self->dmabuf_num_planes = 0;

    pthread_mutex_unlock(&self->mutex);
    return true;
}

bool gsr_pipewire_video_update_cursor(gsr_pipewire_video *self, gsr_texture_map texture_map, gsr_pipewire_video_region *cursor_region) {
    pthread_mutex_lock(&self->mutex);

    if(!self->negotiated || !self->streaming || !self->cursor.updated) {
        pthread_mutex_unlock(&self->mutex);
        return false;
    }

    gsr_pipewire_video_update_cursor_data(self, texture_map, cursor_region);

    pthread_mutex_unlock(&self->mutex);
    return true;
}

bool gsr_pipewire_video_is_damaged(gsr_pipewire_video *self) {
    if(!self->mutex_initialized)
        return false;

    bool damaged = false;
    pthread_mutex_lock(&self->mutex);
    damaged = self->damaged;
    pthread_mutex_unlock(&self->mutex);
    return damaged;
}

void gsr_pipewire_video_clear_damage(gsr_pipewire_video *self) {
    if(!self->mutex_initialized)
        return;

    pthread_mutex_lock(&self->mutex);
    self->damaged = false;
    pthread_mutex_unlock(&self->mutex);
}

bool gsr_pipewire_video_should_restart(gsr_pipewire_video *self) {
    if(!self->mutex_initialized)
        return false;

    bool should_restart = false;
    pthread_mutex_lock(&self->mutex);
    should_restart = self->paused && clock_get_monotonic_seconds() - self->paused_start_secs >= 3.0;
    pthread_mutex_unlock(&self->mutex);
    return should_restart;
}
