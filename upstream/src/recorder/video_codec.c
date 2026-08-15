#include "../../include/recorder/video_codec.h"
#include "../../include/ffmpeg_utils.h"
#include "../../include/log.h"

#include <assert.h>

#include <libavutil/opt.h>

int video_quality_to_h264_equivalent_qp(gsr_video_quality video_quality) {
    switch(video_quality) {
        case GSR_VIDEO_QUALITY_MEDIUM:    return 35;
        case GSR_VIDEO_QUALITY_HIGH:      return 30;
        case GSR_VIDEO_QUALITY_VERY_HIGH: return 25;
        case GSR_VIDEO_QUALITY_ULTRA:     return 22;
    }
    return 22;
}

static int video_quality_to_codec_quality_value(enum AVCodecID codec_id, gsr_video_quality video_quality) {
    const int h264_qp = video_quality_to_h264_equivalent_qp(video_quality);
    switch(codec_id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
            return h264_qp;
        case AV_CODEC_ID_AV1:
        case AV_CODEC_ID_VP9:
            return h264_qp * 4;
        case AV_CODEC_ID_VP8:
            return h264_qp * 2;
        default:
            return h264_qp;
    }
}

static int vbr_get_quality_parameter(AVCodecContext *codec_context, gsr_video_quality video_quality, bool hdr) {
    // 8 bit / 10 bit = 80%
    const float qp_multiply = hdr ? 8.0f/10.0f : 1.0f;
    return video_quality_to_codec_quality_value(codec_context->codec_id, video_quality) * qp_multiply;
}

AVCodecContext *create_video_codec_context(enum AVPixelFormat pix_fmt, const AVCodec *codec, const gsr_egl *egl, const gsr_recorder_settings *settings, int width, int height) {
    const bool use_software_video_encoder = settings->video_encoder == GSR_VIDEO_ENCODER_HW_CPU;
    const bool hdr = video_codec_is_hdr(settings->video_codec);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);

    //double fps_ratio = (double)fps / 30.0;

    assert(codec->type == AVMEDIA_TYPE_VIDEO);
    codec_context->codec_id = codec->id;
    codec_context->width = width;
    codec_context->height = height;
    // Timebase: This is the fundamental unit of time (in seconds) in terms
    // of which frame timestamps are represented. For fixed-fps content,
    // timebase should be 1/framerate and timestamp increments should be
    // identical to 1
    codec_context->time_base.num = 1;
    codec_context->time_base.den = settings->framerate_mode == GSR_FRAMERATE_MODE_CONSTANT ? settings->fps : AV_TIME_BASE;
    codec_context->framerate.num = settings->fps;
    codec_context->framerate.den = 1;
    codec_context->sample_aspect_ratio.num = 0;
    codec_context->sample_aspect_ratio.den = 0;
    if(settings->low_latency_recording) {
        codec_context->flags |= (AV_CODEC_FLAG_CLOSED_GOP | AV_CODEC_FLAG_LOW_DELAY);
        codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
        //codec_context->gop_size = std::numeric_limits<int>::max();
        //codec_context->keyint_min = std::numeric_limits<int>::max();
        codec_context->gop_size = settings->fps * settings->keyint;
    } else {
        // High values reduce file size but increases time it takes to seek
        codec_context->gop_size = settings->fps * settings->keyint;
    }
    codec_context->max_b_frames = 0;
    codec_context->pix_fmt = pix_fmt;
    codec_context->color_range = settings->color_range == GSR_COLOR_RANGE_LIMITED ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
    if(hdr) {
        codec_context->color_primaries = AVCOL_PRI_BT2020;
        codec_context->color_trc = AVCOL_TRC_SMPTE2084;
        codec_context->colorspace = AVCOL_SPC_BT2020_NCL;
    } else {
        codec_context->color_primaries = AVCOL_PRI_BT709;
        codec_context->color_trc = AVCOL_TRC_BT709;
        codec_context->colorspace = AVCOL_SPC_BT709;
    }
    //codec_context->chroma_sample_location = AVCHROMA_LOC_CENTER;
    // Can't use this because it's fucking broken in ffmpeg 8 or new mesa. It produces garbage output
    //if(codec->id == AV_CODEC_ID_HEVC)
    //    codec_context->codec_tag = MKTAG('h', 'v', 'c', '1'); // QuickTime on MacOS requires this or the video wont be playable

    if(settings->bitrate_mode == GSR_BITRATE_MODE_CBR) {
        codec_context->bit_rate = settings->video_bitrate;
        codec_context->rc_max_rate = codec_context->bit_rate;
        //codec_context->rc_min_rate = codec_context->bit_rate;
        codec_context->rc_buffer_size = codec_context->bit_rate;//codec_context->bit_rate / 10;
        codec_context->rc_initial_buffer_occupancy = 0;//codec_context->bit_rate;//codec_context->bit_rate * 1000;
    } else if(settings->bitrate_mode == GSR_BITRATE_MODE_VBR) {
        const int quality = vbr_get_quality_parameter(codec_context, settings->video_quality, hdr);
        switch(settings->video_quality) {
            case GSR_VIDEO_QUALITY_MEDIUM:
                codec_context->qmin = quality;
                codec_context->qmax = quality;
                codec_context->bit_rate = 100000;//4500000 + (codec_context->width * codec_context->height)*0.75;
                break;
            case GSR_VIDEO_QUALITY_HIGH:
                codec_context->qmin = quality;
                codec_context->qmax = quality;
                codec_context->bit_rate = 100000;//10000000-9000000 + (codec_context->width * codec_context->height)*0.75;
                break;
            case GSR_VIDEO_QUALITY_VERY_HIGH:
                codec_context->qmin = quality;
                codec_context->qmax = quality;
                codec_context->bit_rate = 100000;//10000000-9000000 + (codec_context->width * codec_context->height)*0.75;
                break;
            case GSR_VIDEO_QUALITY_ULTRA:
                codec_context->qmin = quality;
                codec_context->qmax = quality;
                codec_context->bit_rate = 100000;//10000000-9000000 + (codec_context->width * codec_context->height)*0.75;
                break;
        }

        codec_context->rc_max_rate = codec_context->bit_rate;
        //codec_context->rc_min_rate = codec_context->bit_rate;
        codec_context->rc_buffer_size = codec_context->bit_rate;//codec_context->bit_rate / 10;
        codec_context->rc_initial_buffer_occupancy = codec_context->bit_rate;//codec_context->bit_rate * 1000;
    } else {
        //codec_context->rc_buffer_size = 50000 * 1000;
    }
    //codec_context->profile = FF_PROFILE_H264_MAIN;
    if (codec_context->codec_id == AV_CODEC_ID_MPEG1VIDEO)
        codec_context->mb_decision = 2;

    const bool uses_vaapi_encoder = !use_software_video_encoder && egl->gpu_info.vendor != GSR_GPU_VENDOR_NVIDIA && !video_codec_is_vulkan(settings->video_codec);
    if(uses_vaapi_encoder && settings->bitrate_mode != GSR_BITRATE_MODE_CBR) {
        // 8 bit / 10 bit = 80%, and increase it even more
        const float quality_multiply = hdr ? (8.0f/10.0f * 0.7f) : 1.0f;
        codec_context->global_quality = video_quality_to_codec_quality_value(codec_context->codec_id, settings->video_quality) * quality_multiply;
    }

    av_opt_set_int(codec_context->priv_data, "b_ref_mode", 0, 0);
    //av_opt_set_int(codec_context->priv_data, "cbr", true, 0);

    if(egl->gpu_info.vendor != GSR_GPU_VENDOR_NVIDIA || video_codec_is_vulkan(settings->video_codec)) {
        // TODO: More options, better options
        //codec_context->bit_rate = codec_context->width * codec_context->height;
        switch(settings->bitrate_mode) {
            case GSR_BITRATE_MODE_QP: {
                if(video_codec_is_vulkan(settings->video_codec))
                    av_opt_set(codec_context->priv_data, "rc_mode", "cqp", 0);
                else if(egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA)
                    av_opt_set(codec_context->priv_data, "rc", "constqp", 0);
                else
                    av_opt_set(codec_context->priv_data, "rc_mode", "CQP", 0);
                break;
            }
            case GSR_BITRATE_MODE_VBR: {
                if(video_codec_is_vulkan(settings->video_codec))
                    av_opt_set(codec_context->priv_data, "rc_mode", "vbr", 0);
                else if(egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA)
                    av_opt_set(codec_context->priv_data, "rc", "vbr", 0);
                else
                    av_opt_set(codec_context->priv_data, "rc_mode", "VBR", 0);
                break;
            }
            case GSR_BITRATE_MODE_CBR: {
                if(video_codec_is_vulkan(settings->video_codec))
                    av_opt_set(codec_context->priv_data, "rc_mode", "cbr", 0);
                else if(egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA)
                    av_opt_set(codec_context->priv_data, "rc", "cbr", 0);
                else
                    av_opt_set(codec_context->priv_data, "rc_mode", "CBR", 0);
                break;
            }
        }
        //codec_context->global_quality = 4;
        //codec_context->compression_level = 2;
    }

    //av_opt_set(codec_context->priv_data, "bsf", "hevc_metadata=colour_primaries=9:transfer_characteristics=16:matrix_coefficients=9", 0);

    if(settings->tune == GSR_TUNE_QUALITY)
        codec_context->max_b_frames = 2;

    codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    return codec_context;
}

static void dict_set_profile(AVCodecContext *codec_context, gsr_gpu_vendor vendor, gsr_color_depth color_depth, gsr_video_codec video_codec, AVDictionary **options) {
    #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(61, 17, 100)
    if(codec_context->codec_id == AV_CODEC_ID_H264) {
        // TODO: Only for vaapi
        //if(color_depth == GSR_COLOR_DEPTH_10_BITS)
        //    av_dict_set(options, "profile", "high10", 0);
        //else
        av_dict_set(options, "profile", "high", 0);
    } else if(codec_context->codec_id == AV_CODEC_ID_AV1) {
        if(vendor == GSR_GPU_VENDOR_NVIDIA) {
            if(color_depth == GSR_COLOR_DEPTH_10_BITS)
                av_dict_set_int(options, "highbitdepth", 1, 0);
        } else {
            av_dict_set(options, "profile", "main", 0); // TODO: use professional instead?
        }
    } else if(codec_context->codec_id == AV_CODEC_ID_HEVC) {
        if(color_depth == GSR_COLOR_DEPTH_10_BITS)
            av_dict_set(options, "profile", "main10", 0);
        else
            av_dict_set(options, "profile", "main", 0);
    }
    #else
    const bool use_nvidia_values = vendor == GSR_GPU_VENDOR_NVIDIA && !video_codec_is_vulkan(video_codec);
    if(codec_context->codec_id == AV_CODEC_ID_H264) {
        // TODO: Only for vaapi
        //if(color_depth == GSR_COLOR_DEPTH_10_BITS)
        //    av_dict_set_int(options, "profile", AV_PROFILE_H264_HIGH_10, 0);
        //else
        av_dict_set_int(options, "profile", use_nvidia_values ? 2 : AV_PROFILE_H264_HIGH, 0);
    } else if(codec_context->codec_id == AV_CODEC_ID_AV1) {
        if(use_nvidia_values) {
            if(color_depth == GSR_COLOR_DEPTH_10_BITS)
                av_dict_set_int(options, "highbitdepth", 1, 0);
        } else {
            av_dict_set_int(options, "profile", AV_PROFILE_AV1_MAIN, 0); // TODO: use professional instead?
        }
    } else if(codec_context->codec_id == AV_CODEC_ID_HEVC) {
        if(color_depth == GSR_COLOR_DEPTH_10_BITS)
            av_dict_set_int(options, "profile", use_nvidia_values ? 1 : AV_PROFILE_HEVC_MAIN_10, 0);
        else
            av_dict_set_int(options, "profile", use_nvidia_values ? 0 : AV_PROFILE_HEVC_MAIN, 0);
    }
    #endif
}

static void video_software_set_qp(AVCodecContext *codec_context, gsr_video_quality video_quality, bool hdr, AVDictionary **options) {
    // 8 bit / 10 bit = 80%
    const float qp_multiply = hdr ? 8.0f/10.0f : 1.0f;
    av_dict_set_int(options, "qp", video_quality_to_codec_quality_value(codec_context->codec_id, video_quality) * qp_multiply, 0);
}

bool open_video_software(AVCodecContext *codec_context, const gsr_recorder_settings *settings) {
    const bool hdr = video_codec_is_hdr(settings->video_codec);
    AVDictionary *options = NULL;

    if(settings->bitrate_mode == GSR_BITRATE_MODE_QP)
        video_software_set_qp(codec_context, settings->video_quality, hdr, &options);

    av_dict_set(&options, "preset", "veryfast", 0);
    av_dict_set(&options, "tune", "film", 0);
    av_dict_set_int(&options, "forced-idr", 1, 0);

    if(codec_context->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&options, "coder", "cabac", 0); // TODO: cavlc is faster than cabac but worse compression. Which to use?
    }

    av_dict_set(&options, "strict", "experimental", 0);

    if(settings->ffmpeg_video_opts)
        av_dict_parse_string(&options, settings->ffmpeg_video_opts, "=", ";", 0);

    int ret = avcodec_open2(codec_context, codec_context->codec, &options);
    if (ret < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not open video codec: %s", gsr_av_error_to_string(ret));
        return false;
    }

    return true;
}

static void video_set_rc(gsr_video_codec video_codec, gsr_gpu_vendor vendor, gsr_bitrate_mode bitrate_mode, AVDictionary **options) {
    switch(bitrate_mode) {
        case GSR_BITRATE_MODE_QP: {
            if(video_codec_is_vulkan(video_codec))
                av_dict_set(options, "rc_mode", "cqp", 0);
            else if(vendor == GSR_GPU_VENDOR_NVIDIA)
                av_dict_set(options, "rc", "constqp", 0);
            else
                av_dict_set(options, "rc_mode", "CQP", 0);
            break;
        }
        case GSR_BITRATE_MODE_VBR: {
            if(video_codec_is_vulkan(video_codec))
                av_dict_set(options, "rc_mode", "vbr", 0);
            else if(vendor == GSR_GPU_VENDOR_NVIDIA)
                av_dict_set(options, "rc", "vbr", 0);
            else
                av_dict_set(options, "rc_mode", "VBR", 0);
            break;
        }
        case GSR_BITRATE_MODE_CBR: {
            if(video_codec_is_vulkan(video_codec))
                av_dict_set(options, "rc_mode", "cbr", 0);
            else if(vendor == GSR_GPU_VENDOR_NVIDIA)
                av_dict_set(options, "rc", "cbr", 0);
            else
                av_dict_set(options, "rc_mode", "CBR", 0);
            break;
        }
    }
}

static void video_hardware_set_qp(AVCodecContext *codec_context, gsr_video_quality video_quality, bool hdr, AVDictionary **options) {
    // 8 bit / 10 bit = 80%
    const float qp_multiply = hdr ? 8.0f/10.0f : 1.0f;
    av_dict_set_int(options, "qp", video_quality_to_codec_quality_value(codec_context->codec_id, video_quality) * qp_multiply, 0);
}

bool open_video_hardware(AVCodecContext *codec_context, bool low_power, const gsr_egl *egl, const gsr_recorder_settings *settings) {
    const gsr_color_depth color_depth = video_codec_to_bit_depth(settings->video_codec);
    const bool hdr = video_codec_is_hdr(settings->video_codec);
    AVDictionary *options = NULL;

    if(settings->bitrate_mode == GSR_BITRATE_MODE_QP)
        video_hardware_set_qp(codec_context, settings->video_quality, hdr, &options);

    video_set_rc(settings->video_codec, egl->gpu_info.vendor, settings->bitrate_mode, &options);

    // TODO: Enable multipass

    dict_set_profile(codec_context, egl->gpu_info.vendor, color_depth, settings->video_codec, &options);

    if(video_codec_is_vulkan(settings->video_codec)) {
        av_dict_set_int(&options, "async_depth", 3, 0);
        av_dict_set(&options, "tune", "ll", 0); // Low latency
        av_dict_set(&options, "usage", settings->is_livestream ? "stream" : "record", 0);
        av_dict_set(&options, "content", "rendered", 0); // Game or 3D content

        if(codec_context->codec_id == AV_CODEC_ID_H264) {
            // Removed because it causes stutter in games for some people
            //av_dict_set_int(&options, "quality", 5, 0); // quality preset
        } else if(codec_context->codec_id == AV_CODEC_ID_AV1) {
            av_dict_set(&options, "tier", "main", 0);
        } else if(codec_context->codec_id == AV_CODEC_ID_HEVC) {
            if(hdr)
                av_dict_set(&options, "sei", "hdr", 0);
        }
    } else if(egl->gpu_info.vendor == GSR_GPU_VENDOR_NVIDIA) {
        // TODO: These dont seem to be necessary
        // av_dict_set_int(&options, "zerolatency", 1, 0);
        // if(codec_context->codec_id == AV_CODEC_ID_AV1) {
        //     av_dict_set(&options, "tune", "ll", 0);
        // } else if(codec_context->codec_id == AV_CODEC_ID_H264 || codec_context->codec_id == AV_CODEC_ID_HEVC) {
        //     av_dict_set(&options, "preset", "llhq", 0);
        //     av_dict_set(&options, "tune", "ll", 0);
        // }
        av_dict_set(&options, "tune", "ll", 0);
        av_dict_set_int(&options, "forced-idr", 1, 0);

        switch(settings->tune) {
            case GSR_TUNE_PERFORMANCE:
                //av_dict_set(&options, "multipass", "qres", 0);
                break;
            case GSR_TUNE_QUALITY:
                av_dict_set(&options, "multipass", "fullres", 0);
                av_dict_set(&options, "preset", "p6", 0);
                av_dict_set_int(&options, "rc-lookahead", 0, 0);
                break;
        }

        if(codec_context->codec_id == AV_CODEC_ID_H264) {
            // TODO: h264 10bit?
            // TODO:
            // switch(pixel_format) {
            //     case GSR_PIXEL_FORMAT_YUV420:
            //         av_dict_set_int(&options, "profile", AV_PROFILE_H264_HIGH, 0);
            //         break;
            //     case GSR_PIXEL_FORMAT_YUV444:
            //         av_dict_set_int(&options, "profile", AV_PROFILE_H264_HIGH_444, 0);
            //         break;
            // }
        } else if(codec_context->codec_id == AV_CODEC_ID_AV1) {
            switch(settings->pixel_format) {
                case GSR_PIXEL_FORMAT_YUV420:
                    av_dict_set(&options, "rgb_mode", "yuv420", 0);
                    break;
                case GSR_PIXEL_FORMAT_YUV444:
                    av_dict_set(&options, "rgb_mode", "yuv444", 0);
                    break;
            }
        } else if(codec_context->codec_id == AV_CODEC_ID_HEVC) {
            //av_dict_set(&options, "pix_fmt", "yuv420p16le", 0);
        }
    } else {
        // TODO: More quality options
        if(low_power)
            av_dict_set_int(&options, "low_power", 1, 0);
        // Improves performance but increases vram.
        // TODO: Might need a different async_depth for optimal performance on different amd/intel gpus
        av_dict_set_int(&options, "async_depth", 3, 0);

        if(codec_context->codec_id == AV_CODEC_ID_H264) {
            // Removed because it causes stutter in games for some people
            //av_dict_set_int(&options, "quality", 5, 0); // quality preset
        } else if(codec_context->codec_id == AV_CODEC_ID_AV1) {
            av_dict_set(&options, "tier", "main", 0);
        } else if(codec_context->codec_id == AV_CODEC_ID_HEVC) {
            if(hdr)
                av_dict_set(&options, "sei", "hdr", 0);
        }

        // TODO: vp8/vp9 10bit
    }

    if(codec_context->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&options, "coder", "cabac", 0); // TODO: cavlc is faster than cabac but worse compression. Which to use?
    }

    av_dict_set(&options, "strict", "experimental", 0);

    if(settings->ffmpeg_video_opts)
        av_dict_parse_string(&options, settings->ffmpeg_video_opts, "=", ";", 0);

    int ret = avcodec_open2(codec_context, codec_context->codec, &options);
    if (ret < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not open video codec: %s", gsr_av_error_to_string(ret));
        return false;
    }

    return true;
}

enum AVPixelFormat get_pixel_format(gsr_video_codec video_codec, gsr_gpu_vendor vendor, bool use_software_video_encoder) {
    if(use_software_video_encoder) {
        return AV_PIX_FMT_NV12;
    } else {
        if(video_codec_is_vulkan(video_codec))
            return AV_PIX_FMT_VULKAN;
        else
            return vendor == GSR_GPU_VENDOR_NVIDIA ? AV_PIX_FMT_CUDA : AV_PIX_FMT_VAAPI;
    }
}
