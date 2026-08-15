#include "../../include/codec_query/vaapi.h"
#include "../../include/log.h"
#include "../../include/utils.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <va/va.h>
#include <va/va_drm.h>

static bool profile_is_h264(VAProfile profile) {
    switch(profile) {
        case 5: // VAProfileH264Baseline
        case VAProfileH264Main:
        case VAProfileH264High:
        case VAProfileH264ConstrainedBaseline:
            return true;
        default:
            return false;
    }
}

static bool profile_is_hevc_8bit(VAProfile profile) {
    switch(profile) {
        case VAProfileHEVCMain:
            return true;
        default:
            return false;
    }
}

static bool profile_is_hevc_10bit(VAProfile profile) {
    switch(profile) {
        case VAProfileHEVCMain10:
        //case VAProfileHEVCMain12:
        //case VAProfileHEVCMain422_10:
        //case VAProfileHEVCMain422_12:
        //case VAProfileHEVCMain444:
        //case VAProfileHEVCMain444_10:
        //case VAProfileHEVCMain444_12:
            return true;
        default:
            return false;
    }
}

static bool profile_is_av1(VAProfile profile) {
    switch(profile) {
        case VAProfileAV1Profile0:
        case VAProfileAV1Profile1:
            return true;
        default:
            return false;
    }
}

static bool profile_is_vp8(VAProfile profile) {
    switch(profile) {
        case VAProfileVP8Version0_3:
            return true;
        default:
            return false;
    }
}

static bool profile_is_vp9(VAProfile profile) {
    switch(profile) {
        case VAProfileVP9Profile0:
        case VAProfileVP9Profile1:
        case VAProfileVP9Profile2:
        case VAProfileVP9Profile3:
            return true;
        default:
            return false;
    }
}

/* Returns 0, 0 on error */
static vec2i profile_entrypoint_get_max_resolution(VADisplay va_dpy, VAProfile profile, VAEntrypoint entrypoint) {
    VAConfigAttrib attribs[2] = {
        {
            .type = VAConfigAttribMaxPictureWidth,
        },
        {
            .type = VAConfigAttribMaxPictureHeight,
        }
    };
    if(vaGetConfigAttributes(va_dpy, profile, entrypoint, attribs, 2) != VA_STATUS_SUCCESS || (attribs[0].value & VA_ATTRIB_NOT_SUPPORTED) || (attribs[1].value & VA_ATTRIB_NOT_SUPPORTED))
        return (vec2i){0, 0};

    return (vec2i){ attribs[0].value, attribs[1].value };
}

/* Returns 0 on error or if none is supported */
static VAEntrypoint profile_get_video_encoding_entrypoint(VADisplay va_dpy, VAProfile profile) {
    int num_entrypoints = vaMaxNumEntrypoints(va_dpy);
    if(num_entrypoints <= 0)
        return 0;

    VAEntrypoint *entrypoint_list = calloc(num_entrypoints, sizeof(VAEntrypoint));
    if(!entrypoint_list)
        return 0;

    int encoding_entrypoint_index = -1;
    int lower_power_entrypoint_index = -1;
    if(vaQueryConfigEntrypoints(va_dpy, profile, entrypoint_list, &num_entrypoints) == VA_STATUS_SUCCESS) {
        for(int i = 0; i < num_entrypoints; ++i) {
            if(entrypoint_list[i] == VAEntrypointEncSlice)
                encoding_entrypoint_index = i;
            else if(entrypoint_list[i] == VAEntrypointEncSliceLP)
                lower_power_entrypoint_index = i;
        }
    }

    VAEntrypoint encoding_entrypoint = 0;
    if(encoding_entrypoint_index != -1)
        encoding_entrypoint = entrypoint_list[encoding_entrypoint_index];
    else if(lower_power_entrypoint_index != -1)
        encoding_entrypoint = entrypoint_list[lower_power_entrypoint_index];

    free(entrypoint_list);
    return encoding_entrypoint;
}

static bool get_supported_video_codecs(VADisplay va_dpy, gsr_supported_video_codecs *video_codecs, bool cleanup) {
    *video_codecs = (gsr_supported_video_codecs){0};
    bool success = false;
    VAProfile *profile_list = NULL;

    vaSetInfoCallback(va_dpy, NULL, NULL);

    int va_major = 0;
    int va_minor = 0;
    if(vaInitialize(va_dpy, &va_major, &va_minor) != VA_STATUS_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vaapi: vaInitialize failed");
        return false;
    }

    int num_profiles = vaMaxNumProfiles(va_dpy);
    if(num_profiles <= 0)
        goto fail;

    profile_list = calloc(num_profiles, sizeof(VAProfile));
    if(!profile_list || vaQueryConfigProfiles(va_dpy, profile_list, &num_profiles) != VA_STATUS_SUCCESS)
        goto fail;

    for(int i = 0; i < num_profiles; ++i) {
        if(profile_is_h264(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->h264 = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        } else if(profile_is_hevc_8bit(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->hevc = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        } else if(profile_is_hevc_10bit(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->hevc_hdr = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
                video_codecs->hevc_10bit = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        } else if(profile_is_av1(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->av1 = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
                video_codecs->av1_hdr = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
                video_codecs->av1_10bit = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        } else if(profile_is_vp8(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->vp8 = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        } else if(profile_is_vp9(profile_list[i])) {
            const VAEntrypoint encoding_entrypoint = profile_get_video_encoding_entrypoint(va_dpy, profile_list[i]);
            if(encoding_entrypoint != 0) {
                const vec2i max_resolution = profile_entrypoint_get_max_resolution(va_dpy, profile_list[i], encoding_entrypoint);
                video_codecs->vp9 = (gsr_supported_video_codec){ true, encoding_entrypoint == VAEntrypointEncSliceLP, max_resolution };
            }
        }
    }

    success = true;
    fail:
    if(profile_list)
        free(profile_list);

    if(cleanup)
        vaTerminate(va_dpy);

    return success;
}

bool gsr_get_supported_video_codecs_vaapi(gsr_supported_video_codecs *video_codecs, const char *card_path, bool cleanup) {
    memset(video_codecs, 0, sizeof(*video_codecs));
    bool success = false;
    int drm_fd = -1;

    char render_path[128];
    if(!gsr_card_path_get_render_path(card_path, render_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vaapi: failed to get /dev/dri/renderDXXX file from %s", card_path);
        goto done;
    }

    drm_fd = open(render_path, O_RDWR);
    if(drm_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vaapi: failed to open device %s", render_path);
        goto done;
    }

    VADisplay va_dpy = vaGetDisplayDRM(drm_fd);
    if(va_dpy) {
        if(!get_supported_video_codecs(va_dpy, video_codecs, cleanup)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vaapi: failed to query supported video codecs for device %s", render_path);
            goto done;
        }
        success = true;
    }

    done:
    if(cleanup) {
        if(drm_fd > 0)
            close(drm_fd);
    }

    return success;
}
