#include "../../include/capture/kms.h"
#include "../../include/log.h"
#include "../../include/utils.h"
#include "../../include/color_conversion.h"
#include "../../include/kde_night_light.h"
#include "../../include/window/window.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <libavutil/mastering_display_metadata.h>

#define FIND_CRTC_BY_NAME_TIMEOUT_SECONDS 2.0

#define HDMI_STATIC_METADATA_TYPE1 0
#define HDMI_EOTF_SMPTE_ST2084 2
#define HDR_PEAK_LUMINANCE_FALLBACK 1000.0f
#define SDR_WHITE_LUMINANCE_REFERENCE 203.0f

#define MAX_CONNECTOR_IDS 32

typedef struct {
    uint32_t connector_ids[MAX_CONNECTOR_IDS];
    int num_connector_ids;
} MonitorId;

typedef struct {
    gsr_capture_kms_params params;

    vec2i capture_pos;
    vec2i capture_size;
    MonitorId monitor_id;

    gsr_monitor_rotation display_server_monitor_rotation;
    gsr_monitor_rotation final_monitor_rotation;

    unsigned int input_texture_id;
    unsigned int external_input_texture_id;
    unsigned int cursor_texture_id;

    bool no_modifiers_fallback;
    bool external_texture_fallback;

    struct hdr_output_metadata hdr_metadata;
    bool hdr_metadata_set;
    bool tone_mapping_message_shown;

    int drm_card_fd;
    uint32_t crtc_connector_id;
    uint32_t crtc_id;
    uint32_t gamma_lut_property_id;
    uint32_t ctm_property_id;
    uint32_t degamma_lut_property_id;
    uint64_t gamma_lut_blob_id;
    uint64_t gamma_lut_uploaded_blob_id;
    bool crtc_color_ops_active;

    bool night_light_message_shown;
    bool hdr_luminance_message_shown;
    float hdr_video_max_luminance;

    bool is_x11;

    //int drm_fd;
    //uint64_t prev_sequence;
    //bool damaged;

    vec2i prev_target_pos;
    vec2i prev_plane_size;

    double last_time_monitor_check;

    bool capture_is_combined_plane;
    gsr_kms_response_item *drm_fd;
    vec2i output_size;
    vec2i target_pos;
} gsr_capture_kms;

static void gsr_capture_kms_stop(gsr_capture_kms *self) {
    if(self->input_texture_id) {
        self->params.egl->glDeleteTextures(1, &self->input_texture_id);
        self->input_texture_id = 0;
    }

    if(self->external_input_texture_id) {
        self->params.egl->glDeleteTextures(1, &self->external_input_texture_id);
        self->external_input_texture_id = 0;
    }

    if(self->cursor_texture_id) {
        self->params.egl->glDeleteTextures(1, &self->cursor_texture_id);
        self->cursor_texture_id = 0;
    }

    if(self->drm_card_fd > 0) {
        close(self->drm_card_fd);
        self->drm_card_fd = -1;
    }

    // if(self->drm_fd > 0) {
    //     close(self->drm_fd);
    //     self->drm_fd = -1;
    // }
}

static int max_int(int a, int b) {
    return a > b ? a : b;
}

static void gsr_capture_kms_create_input_texture_ids(gsr_capture_kms *self) {
    self->params.egl->glGenTextures(1, &self->input_texture_id);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->input_texture_id);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);

    self->params.egl->glGenTextures(1, &self->external_input_texture_id);
    self->params.egl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, self->external_input_texture_id);
    self->params.egl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    const bool cursor_texture_id_is_external = self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA;
    const int cursor_texture_id_target = cursor_texture_id_is_external ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;

    self->params.egl->glGenTextures(1, &self->cursor_texture_id);
    self->params.egl->glBindTexture(cursor_texture_id_target, self->cursor_texture_id);
    self->params.egl->glTexParameteri(cursor_texture_id_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    self->params.egl->glTexParameteri(cursor_texture_id_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    self->params.egl->glBindTexture(cursor_texture_id_target, 0);
}

/* TODO: On monitor reconfiguration, find monitor x, y, width and height again. Do the same for nvfbc. */

typedef struct {
    MonitorId *monitor_id;
    const char *monitor_to_capture;
    int monitor_to_capture_len;
    int num_monitors;
} MonitorCallbackUserdata;

static void monitor_callback(const gsr_monitor *monitor, void *userdata) {
    MonitorCallbackUserdata *monitor_callback_userdata = userdata;
    ++monitor_callback_userdata->num_monitors;

    if(monitor_callback_userdata->monitor_to_capture_len != monitor->name_len || memcmp(monitor_callback_userdata->monitor_to_capture, monitor->name, monitor->name_len) != 0)
        return;

    if(monitor_callback_userdata->monitor_id->num_connector_ids < MAX_CONNECTOR_IDS) {
        monitor_callback_userdata->monitor_id->connector_ids[monitor_callback_userdata->monitor_id->num_connector_ids] = monitor->connector_id;
        ++monitor_callback_userdata->monitor_id->num_connector_ids;
    }

    if(monitor_callback_userdata->monitor_id->num_connector_ids == MAX_CONNECTOR_IDS)
        gsr_log(GSR_LOG_LEVEL_WARNING, "reached max connector ids");
}

static vec2i rotate_capture_size_if_rotated(gsr_capture_kms *self, vec2i capture_size, gsr_monitor_rotation rotation) {
    if(rotation == GSR_MONITOR_ROT_90 || rotation == GSR_MONITOR_ROT_270) {
        int tmp_x = capture_size.x;
        capture_size.x = capture_size.y;
        capture_size.y = tmp_x;
    }
    return capture_size;
}

static int gsr_capture_kms_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    gsr_capture_kms *self = cap->priv;

    gsr_capture_kms_create_input_texture_ids(self);
    self->drm_card_fd = open(self->params.egl->card_path, O_RDONLY);

    gsr_monitor monitor;
    self->monitor_id.num_connector_ids = 0;

    self->is_x11 = gsr_window_get_display_server(self->params.egl->window) == GSR_DISPLAY_SERVER_X11;
    const gsr_connection_type connection_type = self->is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_DRM;

    MonitorCallbackUserdata monitor_callback_userdata = {
        &self->monitor_id,
        self->params.display_to_capture, strlen(self->params.display_to_capture),
        0,
    };
    for_each_active_monitor_output(self->params.egl->window, self->params.egl->card_path, connection_type, monitor_callback, &monitor_callback_userdata);

    if(!get_monitor_by_name(self->params.egl, connection_type, self->params.display_to_capture, &monitor)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_start: failed to find monitor by name \"%s\"", self->params.display_to_capture);
        gsr_capture_kms_stop(self);
        return -1;
    }

    monitor.name = self->params.display_to_capture;
    vec2i monitor_position = {0, 0};
    drm_monitor_get_display_server_data(self->params.egl->window, &monitor, &self->display_server_monitor_rotation, &monitor_position);

    self->capture_pos = monitor.pos;
    /* Monitor size is already rotated on x11 when the monitor is rotated, no need to apply it ourselves */
    if(self->is_x11)
        self->capture_size = monitor.size;
    else
        self->capture_size = rotate_capture_size_if_rotated(self, monitor.size, self->display_server_monitor_rotation);

    vec2i capture_size = self->capture_size;
    if(self->params.region_size.x > 0 && self->params.region_size.y > 0)
        capture_size = self->params.region_size;

    if(self->params.output_resolution.x > 0 && self->params.output_resolution.y > 0) {
        self->params.output_resolution = scale_keep_aspect_ratio(capture_size, self->params.output_resolution);
        capture_metadata->video_size = self->params.output_resolution;
    } else {
        capture_metadata->video_size = capture_size;
    }

    self->last_time_monitor_check = clock_get_monotonic_seconds();
    return 0;
}

// TODO: This is disabled for now because we want to be able to record at a framerate higher than the monitor framerate
// static void gsr_capture_kms_tick(gsr_capture *cap) {
//     gsr_capture_kms *self = cap->priv;

//     if(self->drm_fd <= 0)
//         self->drm_fd = open(self->params.egl->card_path, O_RDONLY);

//     if(self->drm_fd <= 0)
//         return;

//     uint64_t sequence = 0;
//     uint64_t ns = 0;
//     if(drmCrtcGetSequence(self->drm_fd, 79, &sequence, &ns) != 0)
//         return;

//     if(sequence != self->prev_sequence) {
//         self->prev_sequence = sequence;
//         self->damaged = true;
//     }
// }

static gsr_kms_response_item* find_drm_by_connector_id(gsr_kms_response *kms_response, uint32_t connector_id) {
    for(int i = 0; i < kms_response->num_items; ++i) {
        if(kms_response->items[i].connector_id == connector_id && kms_response->items[i].plane_type == KMS_PLANE_TYPE_PRIMARY)
            return &kms_response->items[i];
    }
    return NULL;
}

static gsr_kms_response_item* find_largest_drm(gsr_kms_response *kms_response) {
    if(kms_response->num_items == 0)
        return NULL;

    int64_t largest_size = 0;
    gsr_kms_response_item *largest_drm = &kms_response->items[0];
    for(int i = 0; i < kms_response->num_items; ++i) {
        const int64_t size = (int64_t)kms_response->items[i].width * (int64_t)kms_response->items[i].height;
        if(size > largest_size && kms_response->items[i].plane_type == KMS_PLANE_TYPE_PRIMARY) {
            largest_size = size;
            largest_drm = &kms_response->items[i];
        }
    }
    return largest_drm;
}

static gsr_kms_response_item* find_cursor_drm(gsr_kms_response *kms_response, uint32_t connector_id) {
    gsr_kms_response_item *cursor_drm = NULL;
    for(int i = 0; i < kms_response->num_items; ++i) {
        if(kms_response->items[i].plane_type == KMS_PLANE_TYPE_CURSOR) {
            cursor_drm = &kms_response->items[i];
            if(kms_response->items[i].connector_id == connector_id)
                break;
        }
    }
    return cursor_drm;
}

static bool hdr_metadata_is_supported_format(const struct hdr_output_metadata *hdr_metadata) {
    return hdr_metadata->metadata_type == HDMI_STATIC_METADATA_TYPE1 &&
        hdr_metadata->hdmi_metadata_type1.metadata_type == HDMI_STATIC_METADATA_TYPE1 &&
        hdr_metadata->hdmi_metadata_type1.eotf == HDMI_EOTF_SMPTE_ST2084;
}

static float hdr_metadata_get_max_luminance(const struct hdr_output_metadata *hdr_metadata) {
    float max_luminance = hdr_metadata->hdmi_metadata_type1.max_cll;
    if(max_luminance <= 0.0f)
        max_luminance = hdr_metadata->hdmi_metadata_type1.max_display_mastering_luminance;
    return max_luminance;
}

static bool drm_plane_is_hdr(const gsr_kms_response_item *drm_fd) {
    return drm_fd->has_hdr_metadata && hdr_metadata_is_supported_format(&drm_fd->hdr_metadata);
}

static void gsr_capture_kms_resolve_crtc_color_properties(gsr_capture_kms *self, uint32_t connector_id) {
    self->crtc_connector_id = connector_id;
    self->crtc_id = 0;
    self->gamma_lut_property_id = 0;
    self->ctm_property_id = 0;
    self->degamma_lut_property_id = 0;

    drmModeConnector *connector = drmModeGetConnectorCurrent(self->drm_card_fd, connector_id);
    if(!connector)
        return;

    if(connector->encoder_id) {
        drmModeEncoder *encoder = drmModeGetEncoder(self->drm_card_fd, connector->encoder_id);
        if(encoder) {
            self->crtc_id = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
        }
    }
    drmModeFreeConnector(connector);

    if(self->crtc_id == 0)
        return;

    drmModeObjectProperties *properties = drmModeObjectGetProperties(self->drm_card_fd, self->crtc_id, DRM_MODE_OBJECT_CRTC);
    if(!properties)
        return;

    for(uint32_t i = 0; i < properties->count_props; ++i) {
        drmModePropertyRes *property = drmModeGetProperty(self->drm_card_fd, properties->props[i]);
        if(!property)
            continue;

        if(strcmp(property->name, "GAMMA_LUT") == 0)
            self->gamma_lut_property_id = property->prop_id;
        else if(strcmp(property->name, "CTM") == 0)
            self->ctm_property_id = property->prop_id;
        else if(strcmp(property->name, "DEGAMMA_LUT") == 0)
            self->degamma_lut_property_id = property->prop_id;

        drmModeFreeProperty(property);
    }
    drmModeFreeObjectProperties(properties);
}

static void gsr_capture_kms_read_crtc_color_ops(gsr_capture_kms *self) {
    self->gamma_lut_blob_id = 0;
    self->crtc_color_ops_active = false;

    if(self->crtc_id == 0)
        return;

    drmModeObjectProperties *properties = drmModeObjectGetProperties(self->drm_card_fd, self->crtc_id, DRM_MODE_OBJECT_CRTC);
    if(!properties)
        return;

    for(uint32_t i = 0; i < properties->count_props; ++i) {
        if(properties->prop_values[i] == 0)
            continue;

        if(properties->props[i] == self->gamma_lut_property_id) {
            self->gamma_lut_blob_id = properties->prop_values[i];
            self->crtc_color_ops_active = true;
        } else if(properties->props[i] == self->ctm_property_id || properties->props[i] == self->degamma_lut_property_id) {
            self->crtc_color_ops_active = true;
        }
    }
    drmModeFreeObjectProperties(properties);
}

static void gsr_capture_kms_update_crtc_color_ops(gsr_capture_kms *self, gsr_color_conversion *color_conversion) {
    if(self->drm_card_fd <= 0)
        return;

    if(self->drm_fd->connector_id != self->crtc_connector_id)
        gsr_capture_kms_resolve_crtc_color_properties(self, self->drm_fd->connector_id);

    gsr_capture_kms_read_crtc_color_ops(self);

    if(self->gamma_lut_blob_id == self->gamma_lut_uploaded_blob_id)
        return;

    self->gamma_lut_uploaded_blob_id = self->gamma_lut_blob_id;
    if(self->gamma_lut_uploaded_blob_id == 0) {
        gsr_color_conversion_set_gamma_lut(color_conversion, NULL, 0);
        return;
    }

    drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(self->drm_card_fd, self->gamma_lut_uploaded_blob_id);
    if(!blob) {
        self->gamma_lut_uploaded_blob_id = 0;
        gsr_color_conversion_set_gamma_lut(color_conversion, NULL, 0);
        return;
    }

    const int num_entries = blob->length / sizeof(struct drm_color_lut);
    const struct drm_color_lut *lut = blob->data;
    float *rgb_values = malloc(num_entries * 3 * sizeof(float));
    if(rgb_values && num_entries > 0) {
        for(int i = 0; i < num_entries; ++i) {
            rgb_values[i*3 + 0] = (float)lut[i].red / 65535.0f;
            rgb_values[i*3 + 1] = (float)lut[i].green / 65535.0f;
            rgb_values[i*3 + 2] = (float)lut[i].blue / 65535.0f;
        }
        gsr_color_conversion_set_gamma_lut(color_conversion, rgb_values, num_entries);
    } else {
        self->gamma_lut_uploaded_blob_id = 0;
        gsr_color_conversion_set_gamma_lut(color_conversion, NULL, 0);
    }

    free(rgb_values);
    drmModeFreePropertyBlob(blob);
}

static void gsr_capture_kms_get_monitor_hdr_info(gsr_capture_kms *self, const gsr_kms_response_item *drm_fd, gsr_monitor_hdr_info *monitor_hdr_info) {
    if(!gsr_window_get_monitor_hdr_info(self->params.egl->window, self->params.display_to_capture, monitor_hdr_info))
        memset(monitor_hdr_info, 0, sizeof(*monitor_hdr_info));

    if(monitor_hdr_info->max_peak_luminance <= 0.0f)
        monitor_hdr_info->max_peak_luminance = hdr_metadata_get_max_luminance(&drm_fd->hdr_metadata);

    if(monitor_hdr_info->max_peak_luminance <= 0.0f)
        monitor_hdr_info->max_peak_luminance = HDR_PEAK_LUMINANCE_FALLBACK;

    if(monitor_hdr_info->sdr_white_luminance <= 0.0f)
        monitor_hdr_info->sdr_white_luminance = SDR_WHITE_LUMINANCE_REFERENCE;

    if(monitor_hdr_info->sdr_white_luminance > monitor_hdr_info->max_peak_luminance)
        monitor_hdr_info->sdr_white_luminance = monitor_hdr_info->max_peak_luminance;
}

static void mat3_multiply(const float a[9], const float b[9], float result[9]) {
    for(int row = 0; row < 3; ++row) {
        for(int column = 0; column < 3; ++column) {
            result[row*3 + column] =
                a[row*3 + 0] * b[0*3 + column] +
                a[row*3 + 1] * b[1*3 + column] +
                a[row*3 + 2] * b[2*3 + column];
        }
    }
}

static void gsr_capture_kms_update_hdr_color_transforms(gsr_capture_kms *self, gsr_color_conversion *color_conversion, const gsr_kms_response_item *drm_fd) {
    const bool plane_is_hdr = drm_plane_is_hdr(drm_fd);

    gsr_monitor_hdr_info monitor_hdr_info;
    gsr_capture_kms_get_monitor_hdr_info(self, drm_fd, &monitor_hdr_info);
    const float sdr_white_luminance = monitor_hdr_info.sdr_white_luminance;
    const float hdr_peak_luminance = monitor_hdr_info.max_peak_luminance;

    /* For hdr the night light luminance dimming is normalized away by the reference luminance from the compositor, which the compositor
       dims by the same amount. The hdr image is encoded in the color volume of the monitor reported by the compositor rather than sRGB */
    const gsr_night_light_compensation night_light_compensation = plane_is_hdr ? GSR_NIGHT_LIGHT_COMPENSATION_TINT_KEEP_LUMINANCE : GSR_NIGHT_LIGHT_COMPENSATION_TINT;
    const gsr_color_chromaticities *night_light_container_primaries = plane_is_hdr && monitor_hdr_info.primaries_valid ? &monitor_hdr_info.primaries : NULL;

    float night_light_matrix[9];
    const bool night_light = self->params.kde_night_light && gsr_kde_night_light_get_inverse_matrix(self->params.kde_night_light, night_light_compensation, night_light_container_primaries, night_light_matrix);
    if(night_light && !self->night_light_message_shown) {
        self->night_light_message_shown = true;
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_kms_update_hdr_color_transforms: night light is active, removing the night light tint from the capture");
    }

    /* The compositor applies the night light tint at scanout with the crtc gamma lut when it offloads its color transforms to the crtc
       ("prefer efficiency" color accuracy on kde plasma), so the gamma lut is applied to the captured image to get the same image that the monitor displays */
    gsr_color_conversion_enable_gamma_lut(color_conversion, (plane_is_hdr || night_light) && self->gamma_lut_uploaded_blob_id != 0);

    /* Hdr passthrough: the compositor composites sdr white at the monitors sdr white luminance while the video should have
       sdr white at the reference luminance (203 nits), so the luminance of the video is scaled to match that */
    float luminance_scale = 1.0f;
    if(self->params.hdr && plane_is_hdr) {
        luminance_scale = SDR_WHITE_LUMINANCE_REFERENCE / sdr_white_luminance;
        if(luminance_scale != 1.0f && !self->hdr_luminance_message_shown) {
            self->hdr_luminance_message_shown = true;
            gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_kms_update_hdr_color_transforms: scaling the luminance of the hdr video from sdr white at %d nits to sdr white at %d nits (hdr peak luminance: %d nits)", (int)sdr_white_luminance, (int)SDR_WHITE_LUMINANCE_REFERENCE, (int)(hdr_peak_luminance * luminance_scale));
        }
    }

    const bool convert_sdr_to_hdr = self->params.hdr && !plane_is_hdr;
    if(convert_sdr_to_hdr && !self->hdr_luminance_message_shown) {
        self->hdr_luminance_message_shown = true;
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_kms_update_hdr_color_transforms: the monitor is in sdr mode, converting the captured sdr image to hdr (BT.709 to BT.2020 with sdr white at %d nits)", (int)SDR_WHITE_LUMINANCE_REFERENCE);
    }

    const bool tone_map_hdr_to_sdr = !self->params.hdr && plane_is_hdr;
    gsr_color_conversion_set_hdr_to_sdr_tone_mapping(color_conversion, tone_map_hdr_to_sdr, hdr_peak_luminance, sdr_white_luminance);
    if(tone_map_hdr_to_sdr && !self->tone_mapping_message_shown) {
        self->tone_mapping_message_shown = true;
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_kms_update_hdr_color_transforms: the monitor is in hdr mode, tone mapping the hdr content to sdr (sdr white luminance: %d nits, hdr peak luminance: %d nits). Record with -k hevc_hdr or -k av1_hdr video codec option to record hdr instead", (int)sdr_white_luminance, (int)hdr_peak_luminance);
    }

    if(convert_sdr_to_hdr) {
        /* ITU-R BT.2087 */
        static const float BT709_TO_BT2020[9] = {
            0.627404f, 0.329283f, 0.043313f,
            0.069097f, 0.919540f, 0.011362f,
            0.016391f, 0.088013f, 0.895595f
        };
        float color_matrix[9];
        if(night_light) {
            mat3_multiply(BT709_TO_BT2020, night_light_matrix, color_matrix);
        } else {
            memcpy(color_matrix, BT709_TO_BT2020, sizeof(color_matrix));
        }
        for(int i = 0; i < 9; ++i) {
            color_matrix[i] *= SDR_WHITE_LUMINANCE_REFERENCE / 10000.0f;
        }
        gsr_color_conversion_set_color_matrix(color_conversion, color_matrix, GSR_COLOR_MATRIX_TRANSFER_GAMMA22_TO_PQ);
    } else if(night_light || luminance_scale != 1.0f) {
        float color_matrix[9] = {
            luminance_scale, 0.0f, 0.0f,
            0.0f, luminance_scale, 0.0f,
            0.0f, 0.0f, luminance_scale
        };
        if(night_light) {
            for(int i = 0; i < 9; ++i) {
                color_matrix[i] = night_light_matrix[i] * luminance_scale;
            }
        }
        gsr_color_conversion_set_color_matrix(color_conversion, color_matrix, plane_is_hdr ? GSR_COLOR_MATRIX_TRANSFER_PQ : GSR_COLOR_MATRIX_TRANSFER_GAMMA22);
    } else {
        gsr_color_conversion_set_color_matrix(color_conversion, NULL, GSR_COLOR_MATRIX_TRANSFER_PQ);
    }
}

static void gsr_kms_set_sdr_in_hdr_metadata(gsr_capture_kms *self) {
    if(self->hdr_metadata_set)
        return;

    self->hdr_metadata_set = true;
    memset(&self->hdr_metadata, 0, sizeof(self->hdr_metadata));
    self->hdr_metadata.metadata_type = HDMI_STATIC_METADATA_TYPE1;
    self->hdr_metadata.hdmi_metadata_type1.metadata_type = HDMI_STATIC_METADATA_TYPE1;
    self->hdr_metadata.hdmi_metadata_type1.eotf = HDMI_EOTF_SMPTE_ST2084;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[0].x = 0.64 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[0].y = 0.33 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[1].x = 0.30 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[1].y = 0.60 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[2].x = 0.15 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.display_primaries[2].y = 0.06 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.white_point.x = 0.3127 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.white_point.y = 0.3290 * 50000;
    self->hdr_metadata.hdmi_metadata_type1.max_display_mastering_luminance = SDR_WHITE_LUMINANCE_REFERENCE;
    self->hdr_metadata.hdmi_metadata_type1.max_cll = SDR_WHITE_LUMINANCE_REFERENCE;
    self->hdr_video_max_luminance = SDR_WHITE_LUMINANCE_REFERENCE;
}

// TODO: Check if this hdr data can be changed after the call to av_packet_side_data_add
static void gsr_kms_set_hdr_metadata(gsr_capture_kms *self, const gsr_kms_response_item *drm_fd) {
    if(self->hdr_metadata_set)
        return;

    self->hdr_metadata_set = true;
    self->hdr_metadata = drm_fd->hdr_metadata;

    gsr_monitor_hdr_info monitor_hdr_info;
    gsr_capture_kms_get_monitor_hdr_info(self, drm_fd, &monitor_hdr_info);
    self->hdr_video_max_luminance = monitor_hdr_info.max_peak_luminance * (SDR_WHITE_LUMINANCE_REFERENCE / monitor_hdr_info.sdr_white_luminance);
}

static vec2i swap_vec2i(vec2i value) {
    int tmp = value.x;
    value.x = value.y;
    value.y = tmp;
    return value;
}

static EGLImage gsr_capture_kms_create_egl_image(gsr_capture_kms *self, const gsr_kms_response_item *drm_fd, const int *fds, const uint32_t *offsets, const uint32_t *pitches, const uint64_t *modifiers, bool use_modifiers) {
    intptr_t img_attr[44];
    setup_dma_buf_attrs(img_attr, drm_fd->pixel_format, drm_fd->width, drm_fd->height, fds, offsets, pitches, modifiers, drm_fd->num_dma_bufs, use_modifiers);
    while(self->params.egl->eglGetError() != EGL_SUCCESS){}
    EGLImage image = self->params.egl->eglCreateImage(self->params.egl->egl_display, 0, EGL_LINUX_DMA_BUF_EXT, NULL, img_attr);
    if(!image || self->params.egl->eglGetError() != EGL_SUCCESS) {
        if(image)
            self->params.egl->eglDestroyImage(self->params.egl->egl_display, image);
        return NULL;
    }
    return image;
}

static EGLImage gsr_capture_kms_create_egl_image_with_fallback(gsr_capture_kms *self, const gsr_kms_response_item *drm_fd) {
    // TODO: This causes a crash sometimes on steam deck, why? is it a driver bug? a vaapi pure version doesn't cause a crash.
    // Even ffmpeg kmsgrab causes this crash. The error is:
    // amdgpu: Failed to allocate a buffer:
    // amdgpu:    size      : 28508160 bytes
    // amdgpu:    alignment : 2097152 bytes
    // amdgpu:    domains   : 4
    // amdgpu:    flags   : 4
    // amdgpu: Failed to allocate a buffer:
    // amdgpu:    size      : 28508160 bytes
    // amdgpu:    alignment : 2097152 bytes
    // amdgpu:    domains   : 4
    // amdgpu:    flags   : 4
    // EE ../jupiter-mesa/src/gallium/drivers/radeonsi/radeon_vcn_enc.c:516 radeon_create_encoder UVD - Can't create CPB buffer.
    // [hevc_vaapi @ 0x55ea72b09840] Failed to upload encode parameters: 2 (resource allocation failed).
    // [hevc_vaapi @ 0x55ea72b09840] Encode failed: -5.
    // Error: avcodec_send_frame failed, error: Input/output error
    // Assertion pic->display_order == pic->encode_order failed at libavcodec/vaapi_encode_h265.c:765
    // kms server info: kms client shutdown, shutting down the server

    int fds[GSR_KMS_MAX_DMA_BUFS];
    uint32_t offsets[GSR_KMS_MAX_DMA_BUFS];
    uint32_t pitches[GSR_KMS_MAX_DMA_BUFS];
    uint64_t modifiers[GSR_KMS_MAX_DMA_BUFS];

    for(int i = 0; i < drm_fd->num_dma_bufs; ++i) {
        fds[i] = drm_fd->dma_buf[i].fd;
        offsets[i] = drm_fd->dma_buf[i].offset;
        pitches[i] = drm_fd->dma_buf[i].pitch;
        modifiers[i] = drm_fd->modifier;
    }

    EGLImage image = NULL;
    if(self->no_modifiers_fallback) {
        image = gsr_capture_kms_create_egl_image(self, drm_fd, fds, offsets, pitches, modifiers, false);
    } else {
        image = gsr_capture_kms_create_egl_image(self, drm_fd, fds, offsets, pitches, modifiers, true);
        if(!image) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_create_egl_image_with_fallback: failed to create egl image with modifiers, trying without modifiers");
            self->no_modifiers_fallback = true;
            image = gsr_capture_kms_create_egl_image(self, drm_fd, fds, offsets, pitches, modifiers, false);
        }
    }
    return image;
}

static bool gsr_capture_kms_bind_image_to_texture(gsr_capture_kms *self, EGLImage image, unsigned int texture_id, bool external_texture) {
    const int texture_target = external_texture ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
    while(self->params.egl->glGetError() != 0){}
    self->params.egl->glBindTexture(texture_target, texture_id);
    self->params.egl->glEGLImageTargetTexture2DOES(texture_target, image);
    const bool success = self->params.egl->glGetError() == 0;
    self->params.egl->glBindTexture(texture_target, 0);
    return success;
}

static void gsr_capture_kms_bind_image_to_input_texture_with_fallback(gsr_capture_kms *self, EGLImage image) {
    if(self->external_texture_fallback) {
        gsr_capture_kms_bind_image_to_texture(self, image, self->external_input_texture_id, true);
    } else {
        if(!gsr_capture_kms_bind_image_to_texture(self, image, self->input_texture_id, false)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_capture: failed to bind image to texture, trying with external texture");
            self->external_texture_fallback = true;
            gsr_capture_kms_bind_image_to_texture(self, image, self->external_input_texture_id, true);
        }
    }
}

static gsr_kms_response_item* find_monitor_drm(gsr_capture_kms *self, bool *capture_is_combined_plane) {
    *capture_is_combined_plane = false;
    gsr_kms_response_item *drm_fd = NULL;

    for(int i = 0; i < self->monitor_id.num_connector_ids; ++i) {
        drm_fd = find_drm_by_connector_id(self->params.kms_response, self->monitor_id.connector_ids[i]);
        if(drm_fd)
            break;
    }

    // Will never happen on wayland unless the target monitor has been disconnected
    if(!drm_fd && self->is_x11) {
        drm_fd = find_largest_drm(self->params.kms_response);
        *capture_is_combined_plane = true;
    }

    return drm_fd;
}

static gsr_kms_response_item* find_cursor_drm_if_on_monitor(gsr_capture_kms *self, uint32_t monitor_connector_id, bool capture_is_combined_plane) {
    gsr_kms_response_item *cursor_drm_fd = find_cursor_drm(self->params.kms_response, monitor_connector_id);
    if(!capture_is_combined_plane && cursor_drm_fd && cursor_drm_fd->connector_id != monitor_connector_id)
        cursor_drm_fd = NULL;
    return cursor_drm_fd;
}

static gsr_monitor_rotation kms_rotation_to_gsr_monitor_rotation(gsr_kms_rotation rotation) {
    // Right now both enums have the same values
    return (gsr_monitor_rotation)rotation;
}

static int remainder_int(int a, int b) {
    return a - (a / b) * b;
}

static gsr_monitor_rotation sub_rotations(gsr_monitor_rotation rot1, gsr_monitor_rotation rot2) {
    return remainder_int(rot1 - rot2, 4);
}

static void render_drm_cursor(gsr_capture_kms *self, gsr_color_conversion *color_conversion, gsr_capture_metadata *capture_metadata, const gsr_kms_response_item *cursor_drm_fd, vec2i target_pos, vec2i output_size, vec2i framebuffer_size) {
    const vec2d scale = {
        self->capture_size.x == 0 ? 0 : (double)output_size.x / (double)self->capture_size.x,
        self->capture_size.y == 0 ? 0 : (double)output_size.y / (double)self->capture_size.y
    };

    const bool cursor_texture_id_is_external = self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA;
    const vec2i cursor_size = {cursor_drm_fd->width, cursor_drm_fd->height};

    const gsr_monitor_rotation cursor_plane_rotation = kms_rotation_to_gsr_monitor_rotation(cursor_drm_fd->rotation);
    const gsr_monitor_rotation rotation = sub_rotations(self->display_server_monitor_rotation, cursor_plane_rotation);

    vec2i cursor_pos = {cursor_drm_fd->dst_x, cursor_drm_fd->dst_y};
    switch(rotation) {
        case GSR_MONITOR_ROT_0:
            break;
        case GSR_MONITOR_ROT_90:
            cursor_pos = swap_vec2i(cursor_pos);
            cursor_pos.x = framebuffer_size.x - cursor_pos.x;
            // TODO: Remove this horrible hack
            cursor_pos.x -= cursor_size.x;
            break;
        case GSR_MONITOR_ROT_180:
            cursor_pos.x = framebuffer_size.x - cursor_pos.x;
            cursor_pos.y = framebuffer_size.y - cursor_pos.y;
            // TODO: Remove this horrible hack
            cursor_pos.x -= cursor_size.x;
            cursor_pos.y -= cursor_size.y;
            break;
        case GSR_MONITOR_ROT_270:
            cursor_pos = swap_vec2i(cursor_pos);
            cursor_pos.y = framebuffer_size.y - cursor_pos.y;
            // TODO: Remove this horrible hack
            cursor_pos.y -= cursor_size.y;
            break;
    }

    cursor_pos.x -= self->params.region_position.x;
    cursor_pos.y -= self->params.region_position.y;

    cursor_pos.x *= scale.x;
    cursor_pos.y *= scale.y;

    cursor_pos.x += target_pos.x;
    cursor_pos.y += target_pos.y;

    int fds[GSR_KMS_MAX_DMA_BUFS];
    uint32_t offsets[GSR_KMS_MAX_DMA_BUFS];
    uint32_t pitches[GSR_KMS_MAX_DMA_BUFS];
    uint64_t modifiers[GSR_KMS_MAX_DMA_BUFS];

    for(int i = 0; i < cursor_drm_fd->num_dma_bufs; ++i) {
        fds[i] = cursor_drm_fd->dma_buf[i].fd;
        offsets[i] = cursor_drm_fd->dma_buf[i].offset;
        pitches[i] = cursor_drm_fd->dma_buf[i].pitch;
        modifiers[i] = cursor_drm_fd->modifier;
    }

    intptr_t img_attr_cursor[44];
    setup_dma_buf_attrs(img_attr_cursor, cursor_drm_fd->pixel_format, cursor_drm_fd->width, cursor_drm_fd->height,
        fds, offsets, pitches, modifiers, cursor_drm_fd->num_dma_bufs, true);

    EGLImage cursor_image = self->params.egl->eglCreateImage(self->params.egl->egl_display, 0, EGL_LINUX_DMA_BUF_EXT, NULL, img_attr_cursor);
    const int target = cursor_texture_id_is_external ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
    self->params.egl->glBindTexture(target, self->cursor_texture_id);
    self->params.egl->glEGLImageTargetTexture2DOES(target, cursor_image);
    self->params.egl->glBindTexture(target, 0);

    if(cursor_image)
        self->params.egl->eglDestroyImage(self->params.egl->egl_display, cursor_image);

    gsr_capture_kms_update_hdr_color_transforms(self, color_conversion, cursor_drm_fd);

    self->params.egl->glEnable(GL_SCISSOR_TEST);
    self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

    gsr_color_conversion_draw(color_conversion, self->cursor_texture_id,
        cursor_pos, (vec2i){cursor_size.x * scale.x, cursor_size.y * scale.y},
        (vec2i){0, 0}, cursor_size, cursor_size,
        gsr_monitor_rotation_to_rotation(rotation), capture_metadata->flip, GSR_SOURCE_COLOR_RGB, cursor_texture_id_is_external);

    self->params.egl->glDisable(GL_SCISSOR_TEST);
}

static void render_x11_cursor(gsr_capture_kms *self, gsr_color_conversion *color_conversion, gsr_capture_metadata *capture_metadata, vec2i capture_pos, vec2i target_pos, vec2i output_size) {
    if(!self->params.x11_cursor->visible)
        return;

    const vec2d scale = {
        self->capture_size.x == 0 ? 0 : (double)output_size.x / (double)self->capture_size.x,
        self->capture_size.y == 0 ? 0 : (double)output_size.y / (double)self->capture_size.y
    };

    const vec2i cursor_pos = {
        target_pos.x + (self->params.x11_cursor->position.x - self->params.x11_cursor->hotspot.x - capture_pos.x) * scale.x,
        target_pos.y + (self->params.x11_cursor->position.y - self->params.x11_cursor->hotspot.y - capture_pos.y) * scale.y
    };

    self->params.egl->glEnable(GL_SCISSOR_TEST);
    self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

    gsr_color_conversion_draw(color_conversion, self->params.x11_cursor->texture_id,
        cursor_pos, (vec2i){self->params.x11_cursor->size.x * scale.x, self->params.x11_cursor->size.y * scale.y},
        (vec2i){0, 0}, self->params.x11_cursor->size, self->params.x11_cursor->size,
        GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_RGB, false);

    self->params.egl->glDisable(GL_SCISSOR_TEST);
}

static void gsr_capture_kms_update_capture_size_change(gsr_capture_kms *self, gsr_color_conversion *color_conversion, vec2i target_pos, const gsr_kms_response_item *drm_fd) {
    if(target_pos.x != self->prev_target_pos.x || target_pos.y != self->prev_target_pos.y || drm_fd->src_w != self->prev_plane_size.x || drm_fd->src_h != self->prev_plane_size.y) {
        self->prev_target_pos = target_pos;
        self->prev_plane_size = self->capture_size;
        color_conversion->schedule_clear = true;
    }
}

static void gsr_capture_kms_update_connector_ids(gsr_capture_kms *self) {
    const double now = clock_get_monotonic_seconds();
    if(now - self->last_time_monitor_check < FIND_CRTC_BY_NAME_TIMEOUT_SECONDS)
        return;

    self->last_time_monitor_check = now;
    /* TODO: Assume for now that there is only 1 framebuffer for all monitors and it doesn't change */
    if(self->is_x11)
        return;

    self->crtc_connector_id = 0;

    self->monitor_id.num_connector_ids = 0;
    const gsr_connection_type connection_type = self->is_x11 ? GSR_CONNECTION_X11 : GSR_CONNECTION_DRM;
    // MonitorCallbackUserdata monitor_callback_userdata = {
    //     &self->monitor_id,
    //     self->params.display_to_capture, strlen(self->params.display_to_capture),
    //     0,
    // };
    // for_each_active_monitor_output(self->params.egl->window, self->params.egl->card_path, connection_type, monitor_callback, &monitor_callback_userdata);

    gsr_monitor monitor;
    if(!get_monitor_by_name(self->params.egl, connection_type, self->params.display_to_capture, &monitor)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_update_connector_ids: failed to find monitor by name \"%s\"", self->params.display_to_capture);
        return;
    }

    self->monitor_id.num_connector_ids = 1;
    self->monitor_id.connector_ids[0] = monitor.connector_id;

    monitor.name = self->params.display_to_capture;
    vec2i monitor_position = {0, 0};
    // TODO: This is cached. We need it updated.
    drm_monitor_get_display_server_data(self->params.egl->window, &monitor, &self->display_server_monitor_rotation, &monitor_position);

    self->capture_pos = monitor.pos;
    /* Monitor size is already rotated on x11 when the monitor is rotated, no need to apply it ourselves */
    if(self->is_x11)
        self->capture_size = monitor.size;
    else
        self->capture_size = rotate_capture_size_if_rotated(self, monitor.size, self->display_server_monitor_rotation);
}

static void gsr_capture_kms_pre_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_kms *self = cap->priv;

    if(self->params.kms_response->num_items == 0) {
        static bool error_shown = false;
        if(!error_shown) {
            error_shown = true;
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_pre_capture: no drm found, capture will fail");
        }
        return;
    }

    gsr_capture_kms_update_connector_ids(self);

    self->capture_is_combined_plane = false;
    self->drm_fd = find_monitor_drm(self, &self->capture_is_combined_plane);
    if(!self->drm_fd)
        return;

    if(self->params.kde_night_light || drm_plane_is_hdr(self->drm_fd))
        gsr_capture_kms_update_crtc_color_ops(self, color_conversion);

    if(self->params.hdr) {
        if(drm_plane_is_hdr(self->drm_fd))
            gsr_kms_set_hdr_metadata(self, self->drm_fd);
        else
            gsr_kms_set_sdr_in_hdr_metadata(self);
    }

    const gsr_monitor_rotation plane_rotation = kms_rotation_to_gsr_monitor_rotation(self->drm_fd->rotation);
    self->final_monitor_rotation = self->capture_is_combined_plane ? GSR_MONITOR_ROT_0 : sub_rotations(self->display_server_monitor_rotation, plane_rotation);

    self->capture_size = rotate_capture_size_if_rotated(self, (vec2i){ self->drm_fd->src_w, self->drm_fd->src_h }, self->final_monitor_rotation);
    if(self->params.region_size.x > 0 && self->params.region_size.y > 0)
        self->capture_size = self->params.region_size;

    self->output_size = scale_keep_aspect_ratio(self->capture_size, capture_metadata->recording_size);
    self->target_pos = gsr_capture_get_target_position(self->output_size, capture_metadata);
    gsr_capture_kms_update_capture_size_change(self, color_conversion, self->target_pos, self->drm_fd);
}

static void render_monitor_plane(gsr_capture_kms *self, gsr_color_conversion *color_conversion, gsr_capture_metadata *capture_metadata) {
    vec2i capture_pos = self->capture_pos;
    if(!self->capture_is_combined_plane)
        capture_pos = (vec2i){self->drm_fd->src_x, self->drm_fd->src_y};

    capture_pos.x += self->params.region_position.x;
    capture_pos.y += self->params.region_position.y;

    EGLImage image = gsr_capture_kms_create_egl_image_with_fallback(self, self->drm_fd);
    if(image) {
        gsr_capture_kms_bind_image_to_input_texture_with_fallback(self, image);
        self->params.egl->eglDestroyImage(self->params.egl->egl_display, image);
    }

    gsr_capture_kms_update_hdr_color_transforms(self, color_conversion, self->drm_fd);

    gsr_color_conversion_draw(color_conversion, self->external_texture_fallback ? self->external_input_texture_id : self->input_texture_id,
        self->target_pos, self->output_size,
        capture_pos, self->capture_size, (vec2i){ self->drm_fd->width, self->drm_fd->height },
        gsr_monitor_rotation_to_rotation(self->final_monitor_rotation), capture_metadata->flip, GSR_SOURCE_COLOR_RGB, self->external_texture_fallback);
}

/* Renders an overlay plane on top of (or below, depending on render order) the monitor plane, scaled to the output */
static void render_drm_plane(gsr_capture_kms *self, gsr_color_conversion *color_conversion, gsr_capture_metadata *capture_metadata, const gsr_kms_response_item *plane_drm_fd, vec2i target_pos, vec2i output_size, vec2i framebuffer_size) {
    const vec2d scale = {
        self->capture_size.x == 0 ? 0 : (double)output_size.x / (double)self->capture_size.x,
        self->capture_size.y == 0 ? 0 : (double)output_size.y / (double)self->capture_size.y
    };

    const gsr_monitor_rotation plane_rotation = kms_rotation_to_gsr_monitor_rotation(plane_drm_fd->rotation);
    const gsr_monitor_rotation rotation = sub_rotations(self->display_server_monitor_rotation, plane_rotation);

    const vec2i plane_size = {plane_drm_fd->dst_w, plane_drm_fd->dst_h};
    vec2i plane_pos = {plane_drm_fd->dst_x, plane_drm_fd->dst_y};
    switch(rotation) {
        case GSR_MONITOR_ROT_0:
            break;
        case GSR_MONITOR_ROT_90:
            plane_pos = swap_vec2i(plane_pos);
            plane_pos.x = framebuffer_size.x - plane_pos.x;
            // TODO: Remove this horrible hack
            plane_pos.x -= plane_size.x;
            break;
        case GSR_MONITOR_ROT_180:
            plane_pos.x = framebuffer_size.x - plane_pos.x;
            plane_pos.y = framebuffer_size.y - plane_pos.y;
            // TODO: Remove this horrible hack
            plane_pos.x -= plane_size.x;
            plane_pos.y -= plane_size.y;
            break;
        case GSR_MONITOR_ROT_270:
            plane_pos = swap_vec2i(plane_pos);
            plane_pos.y = framebuffer_size.y - plane_pos.y;
            // TODO: Remove this horrible hack
            plane_pos.y -= plane_size.y;
            break;
    }

    plane_pos.x -= self->params.region_position.x;
    plane_pos.y -= self->params.region_position.y;

    plane_pos.x *= scale.x;
    plane_pos.y *= scale.y;

    plane_pos.x += target_pos.x;
    plane_pos.y += target_pos.y;

    EGLImage image = gsr_capture_kms_create_egl_image_with_fallback(self, plane_drm_fd);
    if(!image)
        return;

    gsr_capture_kms_bind_image_to_input_texture_with_fallback(self, image);
    self->params.egl->eglDestroyImage(self->params.egl->egl_display, image);

    gsr_capture_kms_update_hdr_color_transforms(self, color_conversion, plane_drm_fd);

    self->params.egl->glEnable(GL_SCISSOR_TEST);
    self->params.egl->glScissor(target_pos.x, target_pos.y, output_size.x, output_size.y);

    gsr_color_conversion_draw(color_conversion, self->external_texture_fallback ? self->external_input_texture_id : self->input_texture_id,
        plane_pos, (vec2i){plane_size.x * scale.x, plane_size.y * scale.y},
        (vec2i){plane_drm_fd->src_x, plane_drm_fd->src_y}, (vec2i){plane_drm_fd->src_w, plane_drm_fd->src_h}, (vec2i){plane_drm_fd->width, plane_drm_fd->height},
        gsr_monitor_rotation_to_rotation(rotation), capture_metadata->flip, GSR_SOURCE_COLOR_RGB, self->external_texture_fallback);

    self->params.egl->glDisable(GL_SCISSOR_TEST);
}

static int gsr_capture_kms_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_kms *self = cap->priv;

    if(!self->drm_fd || self->params.kms_response->num_items == 0)
        return -1;

    const vec2i framebuffer_size = rotate_capture_size_if_rotated(self, (vec2i){ self->drm_fd->src_w, self->drm_fd->src_h }, self->final_monitor_rotation);

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    /* Gather all planes that are displayed on the captured monitor. Overlay planes are not used on x11 (combined plane) */
    const gsr_kms_response_item *planes[GSR_KMS_MAX_ITEMS];
    int num_planes = 0;
    planes[num_planes++] = self->drm_fd;
    if(!self->capture_is_combined_plane) {
        for(int i = 0; i < self->params.kms_response->num_items && num_planes < GSR_KMS_MAX_ITEMS; ++i) {
            const gsr_kms_response_item *item = &self->params.kms_response->items[i];
            if(item->plane_type == KMS_PLANE_TYPE_OVERLAY && item->connector_id == self->drm_fd->connector_id)
                planes[num_planes++] = item;
        }
    }

    /* Sort the planes by zpos, from bottom to top. Insertion sort to keep planes with the same zpos in the order the drm driver returned them (stable) */
    for(int i = 1; i < num_planes; ++i) {
        const gsr_kms_response_item *plane = planes[i];
        int j = i - 1;
        for(; j >= 0 && planes[j]->zpos > plane->zpos; --j) {
            planes[j + 1] = planes[j];
        }
        planes[j + 1] = plane;
    }

    for(int i = 0; i < num_planes; ++i) {
        if(planes[i] == self->drm_fd)
            render_monitor_plane(self, color_conversion, capture_metadata);
        else
            render_drm_plane(self, color_conversion, capture_metadata, planes[i], self->target_pos, self->output_size, framebuffer_size);
    }

    if(self->params.record_cursor) {
        gsr_kms_response_item *cursor_drm_fd = find_cursor_drm_if_on_monitor(self, self->drm_fd->connector_id, self->capture_is_combined_plane);
        // The cursor is handled by x11 on x11 instead of using the cursor drm plane because on prime systems with a dedicated nvidia gpu
        // the cursor plane is not available when the cursor is on the monitor controlled by the nvidia device.
        // TODO: This doesn't work properly with software cursor on x11 since it will draw the x11 cursor on top of the cursor already in the framebuffer.
        // Detect if software cursor is used on x11 somehow.
        if(self->is_x11) {
            vec2i cursor_monitor_offset = self->capture_pos;
            cursor_monitor_offset.x += self->params.region_position.x;
            cursor_monitor_offset.y += self->params.region_position.y;
            render_x11_cursor(self, color_conversion, capture_metadata, cursor_monitor_offset, self->target_pos, self->output_size);
        } else if(cursor_drm_fd) {
            render_drm_cursor(self, color_conversion, capture_metadata, cursor_drm_fd, self->target_pos, self->output_size, framebuffer_size);
        }
    }

    gsr_color_conversion_set_hdr_to_sdr_tone_mapping(color_conversion, false, 0.0f, 0.0f);
    gsr_color_conversion_enable_gamma_lut(color_conversion, false);
    gsr_color_conversion_set_color_matrix(color_conversion, NULL, GSR_COLOR_MATRIX_TRANSFER_PQ);

    //self->params.egl->glFlush();
    //self->params.egl->glFinish();

    return 0;
}

static bool gsr_capture_kms_should_stop(gsr_capture *cap, bool *err) {
    (void)cap;
    if(err)
        *err = false;
    return false;
}

static bool gsr_capture_kms_uses_external_image(gsr_capture *cap) {
    (void)cap;
    return true;
}

static bool gsr_capture_kms_set_hdr_metadata(gsr_capture *cap, AVMasteringDisplayMetadata *mastering_display_metadata, AVContentLightMetadata *light_metadata) {
    gsr_capture_kms *self = cap->priv;

    if(!self->hdr_metadata_set)
        return false;

    light_metadata->MaxCLL = self->hdr_metadata.hdmi_metadata_type1.max_cll;
    light_metadata->MaxFALL = self->hdr_metadata.hdmi_metadata_type1.max_fall;

    for(int i = 0; i < 3; ++i) {
        mastering_display_metadata->display_primaries[i][0] = av_make_q(self->hdr_metadata.hdmi_metadata_type1.display_primaries[i].x, 50000);
        mastering_display_metadata->display_primaries[i][1] = av_make_q(self->hdr_metadata.hdmi_metadata_type1.display_primaries[i].y, 50000);
    }

    mastering_display_metadata->white_point[0] = av_make_q(self->hdr_metadata.hdmi_metadata_type1.white_point.x, 50000);
    mastering_display_metadata->white_point[1] = av_make_q(self->hdr_metadata.hdmi_metadata_type1.white_point.y, 50000);

    mastering_display_metadata->min_luminance = av_make_q(self->hdr_metadata.hdmi_metadata_type1.min_display_mastering_luminance, 10000);
    mastering_display_metadata->max_luminance = av_make_q(self->hdr_metadata.hdmi_metadata_type1.max_display_mastering_luminance, 1);

    if(self->hdr_video_max_luminance > 0.0f) {
        light_metadata->MaxCLL = (unsigned int)(self->hdr_video_max_luminance + 0.5f);
        mastering_display_metadata->max_luminance = av_make_q((int)(self->hdr_video_max_luminance + 0.5f), 1);
    }

    mastering_display_metadata->has_primaries = true;
    mastering_display_metadata->has_luminance = true;

    return true;
}

// static bool gsr_capture_kms_is_damaged(gsr_capture *cap) {
//     gsr_capture_kms *self = cap->priv;
//     return self->damaged;
// }

// static void gsr_capture_kms_clear_damage(gsr_capture *cap) {
//     gsr_capture_kms *self = cap->priv;
//     self->damaged = false;
// }

static void gsr_capture_kms_destroy(gsr_capture *cap) {
    gsr_capture_kms *self = cap->priv;
    if(cap->priv) {
        gsr_capture_kms_stop(self);
        free((void*)self->params.display_to_capture);
        self->params.display_to_capture = NULL;
        free(cap->priv);
        cap->priv = NULL;
    }
    free(cap);
}

gsr_capture* gsr_capture_kms_create(const gsr_capture_kms_params *params) {
    if(!params) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_kms_create params is NULL");
        return NULL;
    }

    gsr_capture *cap = calloc(1, sizeof(gsr_capture));
    if(!cap)
        return NULL;

    gsr_capture_kms *cap_kms = calloc(1, sizeof(gsr_capture_kms));
    if(!cap_kms) {
        free(cap);
        return NULL;
    }

    const char *display_to_capture = strdup(params->display_to_capture);
    if(!display_to_capture) {
        free(cap);
        free(cap_kms);
        return NULL;
    }

    cap_kms->params = *params;
    cap_kms->params.display_to_capture = display_to_capture;
    
    *cap = (gsr_capture) {
        .start = gsr_capture_kms_start,
        //.tick = gsr_capture_kms_tick,
        .should_stop = gsr_capture_kms_should_stop,
        .pre_capture = gsr_capture_kms_pre_capture,
        .capture = gsr_capture_kms_capture,
        .uses_external_image = gsr_capture_kms_uses_external_image,
        .set_hdr_metadata = gsr_capture_kms_set_hdr_metadata,
        //.is_damaged = gsr_capture_kms_is_damaged,
        //.clear_damage = gsr_capture_kms_clear_damage,
        .destroy = gsr_capture_kms_destroy,
        .priv = cap_kms
    };

    return cap;
}
