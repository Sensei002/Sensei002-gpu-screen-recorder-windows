#include "../../include/codec_query/nvenc.h"
#include "../../include/log.h"
#include "../../include/cuda.h"
#include "../../external/nvEncodeAPI.h"

#include <dlfcn.h>
#include <string.h>

#define NVENCAPI_MAJOR_VERSION_470 11
#define NVENCAPI_MINOR_VERSION_470 1
#define NVENCAPI_VERSION_470 (NVENCAPI_MAJOR_VERSION_470 | (NVENCAPI_MINOR_VERSION_470 << 24))
#define NVENCAPI_STRUCT_VERSION_CUSTOM(nvenc_api_version, struct_version)  ((uint32_t)(nvenc_api_version) | ((struct_version)<<16) | (0x7 << 28))

static void* open_nvenc_library(void) {
    dlerror(); /* clear */
    void *lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
    if(!lib) {
        lib = dlopen("libnvidia-encode.so", RTLD_LAZY);
        if(!lib) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc failed: failed to load libnvidia-encode.so/libnvidia-encode.so.1, error: %s", dlerror());
            return NULL;
        }
    }
    return lib;
}

static bool profile_is_h264(const GUID *profile_guid) {
    const GUID *h264_guids[] = {
        &NV_ENC_H264_PROFILE_BASELINE_GUID,
        &NV_ENC_H264_PROFILE_MAIN_GUID,
        &NV_ENC_H264_PROFILE_HIGH_GUID,
        &NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID,
        &NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID
    };

    for(int i = 0; i < 5; ++i) {
        if(memcmp(profile_guid, h264_guids[i], sizeof(GUID)) == 0)
            return true;
    }

    return false;
}

static bool profile_is_hevc(const GUID *profile_guid) {
    const GUID *h264_guids[] = {
        &NV_ENC_HEVC_PROFILE_MAIN_GUID,
    };

    for(int i = 0; i < 1; ++i) {
        if(memcmp(profile_guid, h264_guids[i], sizeof(GUID)) == 0)
            return true;
    }

    return false;
}

static bool profile_is_hevc_10bit(const GUID *profile_guid) {
    const GUID *h264_guids[] = {
        &NV_ENC_HEVC_PROFILE_MAIN10_GUID,
    };

    for(int i = 0; i < 1; ++i) {
        if(memcmp(profile_guid, h264_guids[i], sizeof(GUID)) == 0)
            return true;
    }

    return false;
}

static bool profile_is_av1(const GUID *profile_guid) {
    const GUID *h264_guids[] = {
        &NV_ENC_AV1_PROFILE_MAIN_GUID,
    };

    for(int i = 0; i < 1; ++i) {
        if(memcmp(profile_guid, h264_guids[i], sizeof(GUID)) == 0)
            return true;
    }

    return false;
}

/* Returns 0 on error */
static int nvenc_get_encoding_capability(const NV_ENCODE_API_FUNCTION_LIST *function_list, void *nvenc_encoder, const GUID *encode_guid, uint32_t nvenc_api_version, NV_ENC_CAPS cap) {
    NV_ENC_CAPS_PARAM param = {
        .version = NVENCAPI_STRUCT_VERSION_CUSTOM(nvenc_api_version, 1),
        .capsToQuery = cap
    };

    int value = 0;
    if(function_list->nvEncGetEncodeCaps(nvenc_encoder, *encode_guid, &param, &value) != NV_ENC_SUCCESS)
        return 0;

    return value;
}

static vec2i encoder_get_max_resolution(const NV_ENCODE_API_FUNCTION_LIST *function_list, void *nvenc_encoder, const GUID *encode_guid, uint32_t nvenc_api_version) {
    return (vec2i){
        .x = nvenc_get_encoding_capability(function_list, nvenc_encoder, encode_guid, nvenc_api_version, NV_ENC_CAPS_WIDTH_MAX),
        .y = nvenc_get_encoding_capability(function_list, nvenc_encoder, encode_guid, nvenc_api_version, NV_ENC_CAPS_HEIGHT_MAX),
    };
}

static bool encoder_get_supported_profiles(const NV_ENCODE_API_FUNCTION_LIST *function_list, void *nvenc_encoder, const GUID *encoder_guid, gsr_supported_video_codecs *supported_video_codecs, uint32_t nvenc_api_version) {
    bool success = false;
    GUID *profile_guids = NULL;

    uint32_t profile_guid_count = 0;
    if(function_list->nvEncGetEncodeProfileGUIDCount(nvenc_encoder, *encoder_guid, &profile_guid_count) != NV_ENC_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncGetEncodeProfileGUIDCount failed, error: %s", function_list->nvEncGetLastErrorString(nvenc_encoder));
        goto fail;
    }

    if(profile_guid_count == 0)
        goto fail;

    profile_guids = calloc(profile_guid_count, sizeof(GUID));
    if(!profile_guids) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: failed to allocate %d guids", (int)profile_guid_count);
        goto fail;
    }

    if(function_list->nvEncGetEncodeProfileGUIDs(nvenc_encoder, *encoder_guid, profile_guids, profile_guid_count, &profile_guid_count) != NV_ENC_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncGetEncodeProfileGUIDs failed, error: %s", function_list->nvEncGetLastErrorString(nvenc_encoder));
        goto fail;
    }

    const vec2i max_resolution = encoder_get_max_resolution(function_list, nvenc_encoder, encoder_guid, nvenc_api_version);
    for(uint32_t i = 0; i < profile_guid_count; ++i) {
        if(profile_is_h264(&profile_guids[i])) {
            supported_video_codecs->h264 = (gsr_supported_video_codec){ true, false, max_resolution };
        } else if(profile_is_hevc(&profile_guids[i])) {
            supported_video_codecs->hevc = (gsr_supported_video_codec){ true, false, max_resolution };
        } else if(profile_is_hevc_10bit(&profile_guids[i])) {
            supported_video_codecs->hevc_hdr = (gsr_supported_video_codec){ true, false, max_resolution };
            supported_video_codecs->hevc_10bit = (gsr_supported_video_codec){ true, false, max_resolution };
        } else if(profile_is_av1(&profile_guids[i])) {
            supported_video_codecs->av1 = (gsr_supported_video_codec){ true, false, max_resolution };
            supported_video_codecs->av1_hdr = (gsr_supported_video_codec){ true, false, max_resolution };
            supported_video_codecs->av1_10bit = (gsr_supported_video_codec){ true, false, max_resolution };
        }
    }

    success = true;
    fail:

    if(profile_guids)
        free(profile_guids);

    return success;
}

static bool get_supported_video_codecs(const NV_ENCODE_API_FUNCTION_LIST *function_list, void *nvenc_encoder, gsr_supported_video_codecs *supported_video_codecs, uint32_t nvenc_api_version) {
    bool success = false;
    GUID *encoder_guids = NULL;
    *supported_video_codecs = (gsr_supported_video_codecs){0};

    uint32_t encode_guid_count = 0;
    if(function_list->nvEncGetEncodeGUIDCount(nvenc_encoder, &encode_guid_count) != NV_ENC_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncGetEncodeGUIDCount failed, error: %s", function_list->nvEncGetLastErrorString(nvenc_encoder));
        goto fail;
    }

    if(encode_guid_count == 0)
        goto fail;

    encoder_guids = calloc(encode_guid_count, sizeof(GUID));
    if(!encoder_guids) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: failed to allocate %d guids", (int)encode_guid_count);
        goto fail;
    }

    if(function_list->nvEncGetEncodeGUIDs(nvenc_encoder, encoder_guids, encode_guid_count, &encode_guid_count) != NV_ENC_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncGetEncodeGUIDs failed, error: %s", function_list->nvEncGetLastErrorString(nvenc_encoder));
        goto fail;
    }

    for(uint32_t i = 0; i < encode_guid_count; ++i) {
        encoder_get_supported_profiles(function_list, nvenc_encoder, &encoder_guids[i], supported_video_codecs, nvenc_api_version);
    }

    success = true;
    fail:

    if(encoder_guids)
        free(encoder_guids);

    return success;
}

bool gsr_get_supported_video_codecs_nvenc(gsr_supported_video_codecs *video_codecs, bool cleanup) {
    memset(video_codecs, 0, sizeof(*video_codecs));

    bool success = false;
    void *nvenc_lib = NULL;
    void *nvenc_encoder = NULL;
    gsr_cuda cuda;
    memset(&cuda, 0, sizeof(cuda));

    if(!gsr_cuda_load(&cuda)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: failed to load cuda");
        goto done;
    }

    nvenc_lib = open_nvenc_library();
    if(!nvenc_lib)
        goto done;

    typedef NVENCSTATUS NVENCAPI (*FUNC_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST *functionList);
    FUNC_NvEncodeAPICreateInstance nvEncodeAPICreateInstance = (FUNC_NvEncodeAPICreateInstance)dlsym(nvenc_lib, "NvEncodeAPICreateInstance");
    if(!nvEncodeAPICreateInstance) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: failed to find NvEncodeAPICreateInstance in libnvidia-encode.so");
        goto done;
    }

    NV_ENCODE_API_FUNCTION_LIST function_list;
    memset(&function_list, 0, sizeof(function_list));
    function_list.version = NVENCAPI_STRUCT_VERSION(2);
    if(nvEncodeAPICreateInstance(&function_list) != NV_ENC_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncodeAPICreateInstance failed");
        goto done;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.version = NVENCAPI_STRUCT_VERSION(1);
    params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    params.device = cuda.cu_ctx;
    params.apiVersion = NVENCAPI_VERSION;
    if(function_list.nvEncOpenEncodeSessionEx(&params, &nvenc_encoder) != NV_ENC_SUCCESS) {
        // Old nvidia gpus dont support the new nvenc api (which is required for av1).
        // In such cases fallback to old api version if possible and try again.
        function_list.version = NVENCAPI_STRUCT_VERSION_CUSTOM(NVENCAPI_VERSION_470, 2);
        if(nvEncodeAPICreateInstance(&function_list) != NV_ENC_SUCCESS) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncodeAPICreateInstance (retry) failed");
            goto done;
        }

        params.version = NVENCAPI_STRUCT_VERSION_CUSTOM(NVENCAPI_VERSION_470, 1);
        params.apiVersion = NVENCAPI_VERSION_470;
        if(function_list.nvEncOpenEncodeSessionEx(&params, &nvenc_encoder) != NV_ENC_SUCCESS) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_nvenc: nvEncOpenEncodeSessionEx (retry) failed");
            goto done;
        }
    }

    success = get_supported_video_codecs(&function_list, nvenc_encoder, video_codecs, params.apiVersion);

    done:
    if(cleanup) {
        if(nvenc_encoder)
            function_list.nvEncDestroyEncoder(nvenc_encoder);
        if(nvenc_lib)
            dlclose(nvenc_lib);
        gsr_cuda_unload(&cuda);
    }

    return success;
}
