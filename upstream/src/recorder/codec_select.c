#include "../../include/recorder/codec_select.h"
#include "../../include/recorder/error.h"
#include "../../include/encoder/video/nvenc.h"
#include "../../include/encoder/video/vaapi.h"
#include "../../include/encoder/video/vulkan.h"
#include "../../include/encoder/video/software.h"
#include "../../include/codec_query/nvenc.h"
#include "../../include/codec_query/vaapi.h"
#include "../../include/codec_query/vulkan.h"
#include "../../include/log.h"

#include <libavformat/avformat.h>

#include <string.h>

gsr_video_encoder* create_video_encoder(gsr_egl *egl, const gsr_recorder_settings *settings) {
    const gsr_color_depth color_depth = video_codec_to_bit_depth(settings->video_codec);
    gsr_video_encoder *video_encoder = NULL;

    if(settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU) {
        gsr_video_encoder_software_params params;
        params.egl = egl;
        params.color_depth = color_depth;
        video_encoder = gsr_video_encoder_software_create(&params);
        return video_encoder;
    }

    if(video_codec_is_vulkan(settings->video_codec)) {
        gsr_video_encoder_vulkan_params params;
        params.egl = egl;
        params.color_depth = color_depth;
        video_encoder = gsr_video_encoder_vulkan_create(&params);
        return video_encoder;
    }

    switch(egl->gpu_info.vendor) {
        case GSR_GPU_VENDOR_AMD:
        case GSR_GPU_VENDOR_INTEL:
        case GSR_GPU_VENDOR_BROADCOM:
        case GSR_GPU_VENDOR_APPLE: {
            gsr_video_encoder_vaapi_params params;
            params.egl = egl;
            params.color_depth = color_depth;
            video_encoder = gsr_video_encoder_vaapi_create(&params);
            break;
        }
        case GSR_GPU_VENDOR_NVIDIA: {
            gsr_video_encoder_nvenc_params params;
            params.egl = egl;
            params.color_depth = color_depth;
            video_encoder = gsr_video_encoder_nvenc_create(&params);
            break;
        }
        case GSR_GPU_VENDOR_UNKNOWN:
            /* Windows port addition (§3f): software adapter (WARP); no GPU
               encoder is available, so the caller falls back to the CPU
               encoder. */
            break;
    }

    return video_encoder;
}

bool get_supported_video_codecs(gsr_egl *egl, gsr_video_codec video_codec, bool use_software_video_encoder, bool cleanup, gsr_supported_video_codecs *video_codecs) {
    memset(video_codecs, 0, sizeof(*video_codecs));

    if(use_software_video_encoder) {
        video_codecs->h264.supported = avcodec_find_encoder_by_name("libx264");
        video_codecs->h264.max_resolution = (vec2i){4096, 2304};
        return true;
    }

    if(video_codec_is_vulkan(video_codec))
        return gsr_get_supported_video_codecs_vulkan(video_codecs, egl->card_path, &egl->vulkan_device_index, cleanup);

    switch(egl->gpu_info.vendor) {
        case GSR_GPU_VENDOR_AMD:
        case GSR_GPU_VENDOR_INTEL:
        case GSR_GPU_VENDOR_BROADCOM:
        case GSR_GPU_VENDOR_APPLE:
            return gsr_get_supported_video_codecs_vaapi(video_codecs, egl->card_path, cleanup);
        case GSR_GPU_VENDOR_NVIDIA:
            return gsr_get_supported_video_codecs_nvenc(video_codecs, cleanup);
    }

    return false;
}

static const AVCodec* get_ffmpeg_video_codec(gsr_video_codec video_codec, gsr_gpu_vendor vendor) {
    switch(video_codec) {
        case GSR_VIDEO_CODEC_H264:
            return avcodec_find_encoder_by_name(vendor == GSR_GPU_VENDOR_NVIDIA ? "h264_nvenc" : "h264_vaapi");
        case GSR_VIDEO_CODEC_HEVC:
        case GSR_VIDEO_CODEC_HEVC_HDR:
        case GSR_VIDEO_CODEC_HEVC_10BIT:
            return avcodec_find_encoder_by_name(vendor == GSR_GPU_VENDOR_NVIDIA ? "hevc_nvenc" : "hevc_vaapi");
        case GSR_VIDEO_CODEC_AV1:
        case GSR_VIDEO_CODEC_AV1_HDR:
        case GSR_VIDEO_CODEC_AV1_10BIT:
            return avcodec_find_encoder_by_name(vendor == GSR_GPU_VENDOR_NVIDIA ? "av1_nvenc" : "av1_vaapi");
        case GSR_VIDEO_CODEC_VP8:
            return avcodec_find_encoder_by_name(vendor == GSR_GPU_VENDOR_NVIDIA ? "vp8_nvenc" : "vp8_vaapi");
        case GSR_VIDEO_CODEC_VP9:
            return avcodec_find_encoder_by_name(vendor == GSR_GPU_VENDOR_NVIDIA ? "vp9_nvenc" : "vp9_vaapi");
        case GSR_VIDEO_CODEC_H264_VULKAN:
            return avcodec_find_encoder_by_name("h264_vulkan");
        case GSR_VIDEO_CODEC_HEVC_VULKAN:
        case GSR_VIDEO_CODEC_HEVC_HDR_VULKAN:
        case GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN:
            return avcodec_find_encoder_by_name("hevc_vulkan");
        case GSR_VIDEO_CODEC_AV1_VULKAN:
        case GSR_VIDEO_CODEC_AV1_HDR_VULKAN:
        case GSR_VIDEO_CODEC_AV1_10BIT_VULKAN:
            return avcodec_find_encoder_by_name("av1_vulkan");
    }
    return NULL;
}

void set_supported_video_codecs_ffmpeg(gsr_supported_video_codecs *supported_video_codecs, gsr_supported_video_codecs *supported_video_codecs_vulkan, gsr_gpu_vendor vendor) {
    if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_H264, vendor)) {
        supported_video_codecs->h264.supported = false;
    }

    if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_HEVC, vendor)) {
        supported_video_codecs->hevc.supported = false;
        supported_video_codecs->hevc_hdr.supported = false;
        supported_video_codecs->hevc_10bit.supported = false;
    }

    if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_AV1, vendor)) {
        supported_video_codecs->av1.supported = false;
        supported_video_codecs->av1_hdr.supported = false;
        supported_video_codecs->av1_10bit.supported = false;
    }

    if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_VP8, vendor)) {
        supported_video_codecs->vp8.supported = false;
    }

    if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_VP9, vendor)) {
        supported_video_codecs->vp9.supported = false;
    }

    if(supported_video_codecs_vulkan) {
        if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_H264_VULKAN, vendor)) {
            supported_video_codecs_vulkan->h264.supported = false;
        }

        if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_HEVC_VULKAN, vendor)) {
            supported_video_codecs_vulkan->hevc.supported = false;
            supported_video_codecs_vulkan->hevc_hdr.supported = false;
            supported_video_codecs_vulkan->hevc_10bit.supported = false;
        }

        if(!get_ffmpeg_video_codec(GSR_VIDEO_CODEC_AV1_VULKAN, vendor)) {
            supported_video_codecs_vulkan->av1.supported = false;
            supported_video_codecs_vulkan->av1_hdr.supported = false;
            supported_video_codecs_vulkan->av1_10bit.supported = false;
        }
    }
}

gsr_audio_codec select_audio_codec_with_fallback(gsr_audio_codec audio_codec, const char *file_extension, bool uses_amix) {
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_AAC: {
            if(strcmp(file_extension, "webm") == 0) {
                //audio_codec_to_use = "opus";
                audio_codec = GSR_AUDIO_CODEC_OPUS;
                gsr_log(GSR_LOG_LEVEL_WARNING, ".webm files only support opus audio codec, changing audio codec from aac to opus");
            }
            break;
        }
        case GSR_AUDIO_CODEC_OPUS: {
            if(strcmp(file_extension, "mp4") != 0 && strcmp(file_extension, "mkv") != 0 && strcmp(file_extension, "webm") != 0 && strcmp(file_extension, "ts") != 0 && strcmp(file_extension, "whip") != 0) {
                //audio_codec_to_use = "aac";
                audio_codec = GSR_AUDIO_CODEC_AAC;
                gsr_log(GSR_LOG_LEVEL_WARNING, "opus audio codec is only supported by .mp4, .mkv, .webm and .ts files, falling back to aac instead");
            }
            break;
        }
        case GSR_AUDIO_CODEC_FLAC: {
            // TODO: Also check mpegts?
            if(strcmp(file_extension, "webm") == 0) {
                //audio_codec_to_use = "opus";
                audio_codec = GSR_AUDIO_CODEC_OPUS;
                gsr_log(GSR_LOG_LEVEL_WARNING, ".webm files only support opus audio codec, changing audio codec from flac to opus");
            } else if(strcmp(file_extension, "mp4") != 0 && strcmp(file_extension, "mkv") != 0) {
                //audio_codec_to_use = "aac";
                audio_codec = GSR_AUDIO_CODEC_AAC;
                gsr_log(GSR_LOG_LEVEL_WARNING, "flac audio codec is only supported by .mp4 and .mkv files, falling back to aac instead");
            } else if(uses_amix) {
                // TODO: remove this? is it true anymore?
                //audio_codec_to_use = "opus";
                audio_codec = GSR_AUDIO_CODEC_OPUS;
                gsr_log(GSR_LOG_LEVEL_WARNING, "flac audio codec is not supported when mixing audio sources, falling back to opus instead");
            }
            break;
        }
    }
    return audio_codec;
}

static bool video_codec_only_supports_low_power_mode(const gsr_supported_video_codecs *supported_video_codecs, gsr_video_codec video_codec) {
    switch(video_codec) {
        case GSR_VIDEO_CODEC_H264:              return supported_video_codecs->h264.low_power;
        case GSR_VIDEO_CODEC_HEVC:              return supported_video_codecs->hevc.low_power;
        case GSR_VIDEO_CODEC_HEVC_HDR:          return supported_video_codecs->hevc_hdr.low_power;
        case GSR_VIDEO_CODEC_HEVC_10BIT:        return supported_video_codecs->hevc_10bit.low_power;
        case GSR_VIDEO_CODEC_AV1:               return supported_video_codecs->av1.low_power;
        case GSR_VIDEO_CODEC_AV1_HDR:           return supported_video_codecs->av1_hdr.low_power;
        case GSR_VIDEO_CODEC_AV1_10BIT:         return supported_video_codecs->av1_10bit.low_power;
        case GSR_VIDEO_CODEC_VP8:               return supported_video_codecs->vp8.low_power;
        case GSR_VIDEO_CODEC_VP9:               return supported_video_codecs->vp9.low_power;
        case GSR_VIDEO_CODEC_H264_VULKAN:       return supported_video_codecs->h264.low_power;
        case GSR_VIDEO_CODEC_HEVC_VULKAN:       return supported_video_codecs->hevc.low_power;
        case GSR_VIDEO_CODEC_HEVC_HDR_VULKAN:   return supported_video_codecs->hevc_hdr.low_power;
        case GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN: return supported_video_codecs->hevc_10bit.low_power;
        case GSR_VIDEO_CODEC_AV1_VULKAN:        return supported_video_codecs->av1.low_power;
        case GSR_VIDEO_CODEC_AV1_HDR_VULKAN:    return supported_video_codecs->av1_hdr.low_power;
        case GSR_VIDEO_CODEC_AV1_10BIT_VULKAN:  return supported_video_codecs->av1_10bit.low_power;
    }
    return false;
}

static const AVCodec* get_av_codec_if_supported(gsr_video_codec video_codec, gsr_egl *egl, bool use_software_video_encoder, const gsr_supported_video_codecs *supported_video_codecs) {
    switch(video_codec) {
        case GSR_VIDEO_CODEC_H264:
        case GSR_VIDEO_CODEC_H264_VULKAN: {
            if(use_software_video_encoder)
                return avcodec_find_encoder_by_name("libx264");
            else if(supported_video_codecs->h264.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_HEVC:
        case GSR_VIDEO_CODEC_HEVC_VULKAN: {
            if(supported_video_codecs->hevc.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_HEVC_HDR:
        case GSR_VIDEO_CODEC_HEVC_HDR_VULKAN: {
            if(supported_video_codecs->hevc_hdr.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_HEVC_10BIT:
         case GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN: {
            if(supported_video_codecs->hevc_10bit.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_AV1:
        case GSR_VIDEO_CODEC_AV1_VULKAN: {
            if(supported_video_codecs->av1.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_AV1_HDR:
        case GSR_VIDEO_CODEC_AV1_HDR_VULKAN: {
            if(supported_video_codecs->av1_hdr.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_AV1_10BIT:
        case GSR_VIDEO_CODEC_AV1_10BIT_VULKAN: {
            if(supported_video_codecs->av1_10bit.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_VP8: {
            if(supported_video_codecs->vp8.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
        case GSR_VIDEO_CODEC_VP9: {
            if(supported_video_codecs->vp9.supported)
                return get_ffmpeg_video_codec(video_codec, egl->gpu_info.vendor);
            break;
        }
    }
    return NULL;
}

vec2i codec_get_max_resolution(gsr_video_codec video_codec, bool use_software_video_encoder, const gsr_supported_video_codecs *supported_video_codecs) {
    switch(video_codec) {
        case GSR_VIDEO_CODEC_H264:
        case GSR_VIDEO_CODEC_H264_VULKAN: {
            if(use_software_video_encoder)
                return (vec2i){4096, 2304};
            else if(supported_video_codecs->h264.supported)
                return supported_video_codecs->h264.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_HEVC:
        case GSR_VIDEO_CODEC_HEVC_VULKAN: {
            if(supported_video_codecs->hevc.supported)
                return supported_video_codecs->hevc.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_HEVC_HDR:
        case GSR_VIDEO_CODEC_HEVC_HDR_VULKAN: {
            if(supported_video_codecs->hevc_hdr.supported)
                return supported_video_codecs->hevc_hdr.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_HEVC_10BIT:
        case GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN: {
            if(supported_video_codecs->hevc_10bit.supported)
                return supported_video_codecs->hevc_10bit.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_AV1:
        case GSR_VIDEO_CODEC_AV1_VULKAN: {
            if(supported_video_codecs->av1.supported)
                return supported_video_codecs->av1.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_AV1_HDR:
        case GSR_VIDEO_CODEC_AV1_HDR_VULKAN: {
            if(supported_video_codecs->av1_hdr.supported)
                return supported_video_codecs->av1_hdr.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_AV1_10BIT:
        case GSR_VIDEO_CODEC_AV1_10BIT_VULKAN: {
            if(supported_video_codecs->av1_10bit.supported)
                return supported_video_codecs->av1_10bit.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_VP8: {
            if(supported_video_codecs->vp8.supported)
                return supported_video_codecs->vp8.max_resolution;
            break;
        }
        case GSR_VIDEO_CODEC_VP9: {
            if(supported_video_codecs->vp9.supported)
                return supported_video_codecs->vp9.max_resolution;
            break;
        }
    }
    return (vec2i){0, 0};
}

bool codec_supports_resolution(vec2i codec_max_resolution, vec2i capture_resolution) {
    if(codec_max_resolution.x == 0 || codec_max_resolution.y == 0)
        return true;
    return codec_max_resolution.x >= capture_resolution.x && codec_max_resolution.y >= capture_resolution.y;
}

static void print_codec_error(gsr_video_codec video_codec) {
    if(video_codec == (gsr_video_codec)GSR_VIDEO_CODEC_AUTO)
        video_codec = GSR_VIDEO_CODEC_H264;

    const char *video_codec_name = video_codec_to_string(video_codec);
    gsr_log(GSR_LOG_LEVEL_ERROR, "your gpu does not support '%s' video codec. If you are sure that your gpu does support '%s' video encoding and you are using an AMD/Intel GPU,\n  then make sure you have installed the GPU specific vaapi packages (intel-media-driver, libva-intel-driver, libva-mesa-driver and linux-firmware).\n  It's also possible that your distro has disabled hardware accelerated video encoding for '%s' video codec.\n  This may be the case on corporate distros such as Manjaro, Fedora or OpenSUSE.\n  You can test this by running 'vainfo | grep VAEntrypointEncSlice' to see if it matches any H264/HEVC/AV1/VP8/VP9 profile.\n  On such distros, you need to manually install mesa from source to enable H264/HEVC hardware acceleration, or use a more user friendly distro. Alternatively record with AV1 if supported by your GPU.\n  You can alternatively use the flatpak version of GPU Screen Recorder (https://flathub.org/apps/com.dec05eba.gpu_screen_recorder) which bypasses system issues with patented H264/HEVC codecs.\n  If your GPU doesn't support hardware accelerated video encoding then you can use '-fallback-cpu-encoding yes' option to encode with your cpu instead.", video_codec_name, video_codec_name, video_codec_name);
}

void force_cpu_encoding(gsr_recorder_settings *settings) {
    settings->video_codec = GSR_VIDEO_CODEC_H264;
    settings->video_encoder = GSR_VIDEO_ENCODER_HW_CPU;
    if(settings->bitrate_mode == GSR_BITRATE_MODE_VBR) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "bitrate mode has been forcefully set to qp because software encoding option doesn't support vbr option");
        settings->bitrate_mode = GSR_BITRATE_MODE_QP;
    }
}

static int pick_video_codec(gsr_egl *egl, gsr_recorder_settings *settings, bool use_fallback_codec, bool *low_power, gsr_supported_video_codecs *supported_video_codecs, const AVCodec **video_codec) {
    // TODO: software encoder for hevc, av1, vp8 and vp9
    *video_codec = NULL;
    *low_power = false;
    const AVCodec *video_codec_f = get_av_codec_if_supported(settings->video_codec, egl, settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU, supported_video_codecs);

    if(!video_codec_f && use_fallback_codec && settings->video_encoder != GSR_VIDEO_ENCODER_HW_CPU) {
        switch(settings->video_codec) {
            case GSR_VIDEO_CODEC_H264: {
                gsr_log(GSR_LOG_LEVEL_ERROR, "selected video codec h264 is not supported by your hardware");
                if(settings->fallback_cpu_encoding) {
                    gsr_log(GSR_LOG_LEVEL_WARNING, "gpu encoding is not available on your system, trying cpu encoding instead because -fallback-cpu-encoding is enabled. Install the proper vaapi drivers on your system (if supported) if you experience performance issues");
                    force_cpu_encoding(settings);
                }
                break;
            }
            case GSR_VIDEO_CODEC_HEVC:
            case GSR_VIDEO_CODEC_HEVC_HDR:
            case GSR_VIDEO_CODEC_HEVC_10BIT: {
                gsr_log(GSR_LOG_LEVEL_WARNING, "selected video codec hevc is not supported by your hardware, trying h264 instead");
                settings->video_codec = GSR_VIDEO_CODEC_H264;
                return pick_video_codec(egl, settings, true, low_power, supported_video_codecs, video_codec);
            }
            case GSR_VIDEO_CODEC_AV1:
            case GSR_VIDEO_CODEC_AV1_HDR:
            case GSR_VIDEO_CODEC_AV1_10BIT: {
                gsr_log(GSR_LOG_LEVEL_WARNING, "selected video codec av1 is not supported by your hardware, trying h264 instead");
                settings->video_codec = GSR_VIDEO_CODEC_H264;
                return pick_video_codec(egl, settings, true, low_power, supported_video_codecs, video_codec);
            }
            case GSR_VIDEO_CODEC_VP8:
            case GSR_VIDEO_CODEC_VP9:
                // TODO: Cant fallback to other codec because webm only supports vp8/vp9
                break;
            case GSR_VIDEO_CODEC_H264_VULKAN: {
                gsr_log(GSR_LOG_LEVEL_WARNING, "selected video codec h264_vulkan is not supported by your hardware, trying h264 instead");
                settings->video_codec = GSR_VIDEO_CODEC_H264;
                // Need to do a query again because this time it's without vulkan
                if(!get_supported_video_codecs(egl, settings->video_codec, false, true, supported_video_codecs)) {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "failed to query for supported video codecs");
                    print_codec_error(settings->video_codec);
                    return GSR_ERROR_VIDEO_CODEC_QUERY_FAILED;
                }
                return pick_video_codec(egl, settings, true, low_power, supported_video_codecs, video_codec);
            }
            case GSR_VIDEO_CODEC_HEVC_VULKAN:
            case GSR_VIDEO_CODEC_HEVC_HDR_VULKAN:
            case GSR_VIDEO_CODEC_HEVC_10BIT_VULKAN: {
                gsr_log(GSR_LOG_LEVEL_WARNING, "selected video codec hevc_vulkan is not supported by your hardware, trying hevc instead");
                settings->video_codec = GSR_VIDEO_CODEC_HEVC;
                // Need to do a query again because this time it's without vulkan
                if(!get_supported_video_codecs(egl, settings->video_codec, false, true, supported_video_codecs)) {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "failed to query for supported video codecs");
                    print_codec_error(settings->video_codec);
                    return GSR_ERROR_VIDEO_CODEC_QUERY_FAILED;
                }
                return pick_video_codec(egl, settings, true, low_power, supported_video_codecs, video_codec);
            }
            case GSR_VIDEO_CODEC_AV1_VULKAN:
            case GSR_VIDEO_CODEC_AV1_HDR_VULKAN:
            case GSR_VIDEO_CODEC_AV1_10BIT_VULKAN: {
                gsr_log(GSR_LOG_LEVEL_WARNING, "selected video codec av1_vulkan is not supported by your hardware, trying av1 instead");
                settings->video_codec = GSR_VIDEO_CODEC_AV1;
                // Need to do a query again because this time it's without vulkan
                if(!get_supported_video_codecs(egl, settings->video_codec, false, true, supported_video_codecs)) {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "failed to query for supported video codecs");
                    print_codec_error(settings->video_codec);
                    return GSR_ERROR_VIDEO_CODEC_QUERY_FAILED;
                }
                return pick_video_codec(egl, settings, true, low_power, supported_video_codecs, video_codec);
            }
        }

        video_codec_f = get_av_codec_if_supported(settings->video_codec, egl, settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU, supported_video_codecs);
    }

    if(!video_codec_f) {
        print_codec_error(settings->video_codec);
        return GSR_ERROR_VIDEO_CODEC_UNSUPPORTED;
    }

    *low_power = video_codec_only_supports_low_power_mode(supported_video_codecs, settings->video_codec);
    *video_codec = video_codec_f;
    return GSR_ERROR_OK;
}

gsr_video_codec select_appropriate_video_codec_automatically(vec2i video_size, const gsr_supported_video_codecs *supported_video_codecs) {
    if(supported_video_codecs->h264.supported && codec_supports_resolution(supported_video_codecs->h264.max_resolution, video_size)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "using h264 encoder because a codec was not specified");
        return GSR_VIDEO_CODEC_H264;
    } else if(supported_video_codecs->hevc.supported && codec_supports_resolution(supported_video_codecs->hevc.max_resolution, video_size)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "using hevc encoder because a codec was not specified and h264 supported max resolution (%dx%d) is less than the capture resolution (%dx%d)", supported_video_codecs->h264.max_resolution.x, supported_video_codecs->h264.max_resolution.y,
            video_size.x, video_size.y);
        return GSR_VIDEO_CODEC_HEVC;
    } else if(supported_video_codecs->av1.supported && codec_supports_resolution(supported_video_codecs->av1.max_resolution, video_size)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "using av1 encoder because a codec was not specified and hevc supported max resolution (%dx%d) is less than the capture resolution (%dx%d)", supported_video_codecs->hevc.max_resolution.x, supported_video_codecs->hevc.max_resolution.y,
            video_size.x, video_size.y);
        return GSR_VIDEO_CODEC_AV1;
    } else {
        return (gsr_video_codec)-1;
    }
}

int select_video_codec_with_fallback(vec2i video_size, gsr_recorder_settings *settings, const char *file_extension, gsr_egl *egl, bool *low_power, const AVCodec **video_codec) {
    gsr_supported_video_codecs supported_video_codecs_non_vulkan;
    get_supported_video_codecs(egl, settings->video_codec, settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU, true, &supported_video_codecs_non_vulkan);

    gsr_supported_video_codecs supported_video_codecs_vulkan = supported_video_codecs_non_vulkan;
    set_supported_video_codecs_ffmpeg(&supported_video_codecs_non_vulkan, &supported_video_codecs_vulkan, egl->gpu_info.vendor);

    gsr_supported_video_codecs *supported_video_codecs = video_codec_is_vulkan(settings->video_codec)
        ? &supported_video_codecs_vulkan
        : &supported_video_codecs_non_vulkan;

    const bool video_codec_auto = settings->video_codec == (gsr_video_codec)GSR_VIDEO_CODEC_AUTO;
    if(video_codec_auto) {
        if(strcmp(file_extension, "webm") == 0) {
            gsr_log(GSR_LOG_LEVEL_INFO, "using vp8 encoder because a codec was not specified and the file extension is .webm");
            settings->video_codec = GSR_VIDEO_CODEC_VP8;
        } else if(settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU) {
            gsr_log(GSR_LOG_LEVEL_INFO, "using h264 encoder because a codec was not specified");
            settings->video_codec = GSR_VIDEO_CODEC_H264;
        } else if(settings->video_encoder != GSR_VIDEO_ENCODER_HW_CPU) {
            settings->video_codec = select_appropriate_video_codec_automatically(video_size, &supported_video_codecs_non_vulkan);
            if(settings->video_codec == (gsr_video_codec)-1) {
                if(settings->fallback_cpu_encoding) {
                    gsr_log(GSR_LOG_LEVEL_WARNING, "gpu encoding is not available on your system or your gpu doesn't support recording at the resolution you are trying to record, trying cpu encoding instead because -fallback-cpu-encoding is enabled. Install the proper vaapi drivers on your system (if supported) if you experience performance issues");
                    force_cpu_encoding(settings);
                } else {
                    gsr_log(GSR_LOG_LEVEL_ERROR, "no video encoder was specified and neither h264, hevc nor av1 are supported on your system or you are trying to capture at a resolution higher than your system supports for each codec.\n"
                        "  Ensure that you have installed the proper vaapi driver. If your gpu doesn't support video encoding then you can run gpu-screen-recorder with \"-fallback-cpu-encoding yes\" option to use cpu encoding.");
                    return GSR_ERROR_NO_VIDEO_CODEC_AVAILABLE;
                }
            }
        }
    }

    if(LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(60, 10, 100) && strcmp(file_extension, "flv") == 0) {
        if(settings->video_codec != GSR_VIDEO_CODEC_H264) {
            settings->video_codec = GSR_VIDEO_CODEC_H264;
            gsr_log(GSR_LOG_LEVEL_WARNING, "hevc/av1 is not compatible with flv in your outdated version of ffmpeg, falling back to h264 instead.");
        }
    } else if(strcmp(file_extension, "m3u8") == 0) {
        if(video_codec_is_av1(settings->video_codec)) {
            settings->video_codec = GSR_VIDEO_CODEC_HEVC;
            gsr_log(GSR_LOG_LEVEL_WARNING, "av1 is not compatible with hls (m3u8), falling back to hevc instead.");
        }
    }

    const AVCodec *codec = NULL;
    const int pick_codec_result = pick_video_codec(egl, settings, true, low_power, supported_video_codecs, &codec);
    if(pick_codec_result != GSR_ERROR_OK)
        return pick_codec_result;

    const vec2i codec_max_resolution = codec_get_max_resolution(settings->video_codec, settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU, supported_video_codecs);
    if(!codec_supports_resolution(codec_max_resolution, video_size)) {
        const char *video_codec_name = video_codec_to_string(settings->video_codec);
        gsr_log(GSR_LOG_LEVEL_ERROR, "The max resolution for video codec %s is %dx%d while you are trying to capture at resolution %dx%d. Change capture resolution or video codec and try again", video_codec_name, codec_max_resolution.x, codec_max_resolution.y, video_size.x, video_size.y);
        return GSR_ERROR_VIDEO_CODEC_RESOLUTION_UNSUPPORTED;
    }

    *video_codec = codec;
    return GSR_ERROR_OK;
}
