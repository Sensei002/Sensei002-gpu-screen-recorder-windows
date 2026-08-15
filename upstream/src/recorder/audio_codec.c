#include "../../include/recorder/audio_codec.h"
#include "../../include/ffmpeg_utils.h"
#include "../../include/log.h"

#include <assert.h>
#include <math.h>

#include <libavutil/opt.h>

enum AVCodecID audio_codec_get_id(gsr_audio_codec audio_codec) {
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_AAC:  return AV_CODEC_ID_AAC;
        case GSR_AUDIO_CODEC_OPUS: return AV_CODEC_ID_OPUS;
        case GSR_AUDIO_CODEC_FLAC: return AV_CODEC_ID_FLAC;
    }
    assert(false);
    return AV_CODEC_ID_AAC;
}

enum AVSampleFormat audio_codec_get_sample_format(AVCodecContext *audio_codec_context, gsr_audio_codec audio_codec, const AVCodec *codec, bool mix_audio) {
    (void)audio_codec_context;
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_AAC: {
            return AV_SAMPLE_FMT_FLTP;
        }
        case GSR_AUDIO_CODEC_OPUS: {
            bool supports_s16 = false;
            bool supports_flt = false;

            #if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(61, 15, 0)
            for(size_t i = 0; codec->sample_fmts && codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
                if(codec->sample_fmts[i] == AV_SAMPLE_FMT_S16) {
                    supports_s16 = true;
                } else if(codec->sample_fmts[i] == AV_SAMPLE_FMT_FLT) {
                    supports_flt = true;
                }
            }
            #else
            const enum AVSampleFormat *sample_fmts = NULL;
            if(avcodec_get_supported_config(audio_codec_context, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**)&sample_fmts, NULL) >= 0) {
                if(sample_fmts) {
                    for(size_t i = 0; sample_fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
                        if(sample_fmts[i] == AV_SAMPLE_FMT_S16) {
                            supports_s16 = true;
                        } else if(sample_fmts[i] == AV_SAMPLE_FMT_FLT) {
                            supports_flt = true;
                        }
                    }
                } else {
                    // What a dumb API. It returns NULL if all formats are supported
                    supports_s16 = true;
                    supports_flt = true;
                }
            }
            #endif

            // Amix only works with float audio
            if(mix_audio)
                supports_s16 = false;

            if(!supports_s16 && !supports_flt) {
                gsr_log(GSR_LOG_LEVEL_WARNING, "opus audio codec is chosen but your ffmpeg version does not support s16/flt sample format and performance might be slightly worse.\n"
                    "  You can either rebuild ffmpeg with libopus instead of the built-in opus, use the flatpak version of gpu screen recorder or record with aac audio codec instead (-ac aac).\n"
                    "  Falling back to fltp audio sample format instead.");
            }

            if(supports_s16)
                return AV_SAMPLE_FMT_S16;
            else if(supports_flt)
                return AV_SAMPLE_FMT_FLT;
            else
                return AV_SAMPLE_FMT_FLTP;
        }
        case GSR_AUDIO_CODEC_FLAC: {
            return AV_SAMPLE_FMT_S32;
        }
    }
    assert(false);
    return AV_SAMPLE_FMT_FLTP;
}

int64_t audio_codec_get_get_bitrate(gsr_audio_codec audio_codec) {
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_AAC:  return 160000;
        case GSR_AUDIO_CODEC_OPUS: return 128000;
        case GSR_AUDIO_CODEC_FLAC: return 128000;
    }
    assert(false);
    return 128000;
}

gsr_audio_format audio_codec_context_get_audio_format(const AVCodecContext *audio_codec_context) {
    switch(audio_codec_context->sample_fmt) {
        case AV_SAMPLE_FMT_FLT:   return GSR_AUDIO_FORMAT_F32;
        case AV_SAMPLE_FMT_FLTP:  return GSR_AUDIO_FORMAT_S32;
        case AV_SAMPLE_FMT_S16:   return GSR_AUDIO_FORMAT_S16;
        case AV_SAMPLE_FMT_S32:   return GSR_AUDIO_FORMAT_S32;
        default:                  return GSR_AUDIO_FORMAT_S16;
    }
}

enum AVSampleFormat audio_format_to_sample_format(const gsr_audio_format audio_format) {
    switch(audio_format) {
        case GSR_AUDIO_FORMAT_S16:   return AV_SAMPLE_FMT_S16;
        case GSR_AUDIO_FORMAT_S32:   return AV_SAMPLE_FMT_S32;
        case GSR_AUDIO_FORMAT_F32:   return AV_SAMPLE_FMT_FLT;
    }
    assert(false);
    return AV_SAMPLE_FMT_S16;
}

AVCodecContext* create_audio_codec_context(int fps, gsr_audio_codec audio_codec, bool mix_audio, int64_t audio_bitrate) {
    (void)fps;
    const AVCodec *codec = avcodec_find_encoder(audio_codec_get_id(audio_codec));
    if (!codec) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "Could not find %s audio encoder", audio_codec_get_name(audio_codec));
        return NULL;
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);

    assert(codec->type == AVMEDIA_TYPE_AUDIO);
    codec_context->codec_id = codec->id;
    codec_context->sample_fmt = audio_codec_get_sample_format(codec_context, audio_codec, codec, mix_audio);
    codec_context->bit_rate = audio_bitrate == 0 ? audio_codec_get_get_bitrate(audio_codec) : audio_bitrate;
    codec_context->sample_rate = GSR_AUDIO_SAMPLE_RATE;
    if(audio_codec == GSR_AUDIO_CODEC_AAC) {
#if LIBAVCODEC_VERSION_MAJOR < 62
        codec_context->profile = FF_PROFILE_AAC_LOW;
#else
        codec_context->profile = AV_PROFILE_AAC_LOW;
#endif
    }
#if LIBAVCODEC_VERSION_MAJOR < 60
    codec_context->channel_layout = AV_CH_LAYOUT_STEREO;
    codec_context->channels = 2;
#else
    av_channel_layout_default(&codec_context->ch_layout, 2);
#endif

    codec_context->time_base.num = 1;
    codec_context->time_base.den = codec_context->sample_rate;
    codec_context->thread_count = 1;
    codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    return codec_context;
}

bool open_audio(AVCodecContext *audio_codec_context, const char *ffmpeg_audio_opts) {
    AVDictionary *options = NULL;
    av_dict_set(&options, "strict", "experimental", 0);

    if(ffmpeg_audio_opts)
        av_dict_parse_string(&options, ffmpeg_audio_opts, "=", ";", 0);

    int ret;
    ret = avcodec_open2(audio_codec_context, audio_codec_context->codec, &options);
    if(ret < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to open audio codec, reason: %s", gsr_av_error_to_string(ret));
        return false;
    }

    return true;
}

AVFrame* create_audio_frame(AVCodecContext *audio_codec_context) {
    AVFrame *frame = av_frame_alloc();
    if(!frame) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate audio frame");
        return NULL;
    }

    frame->sample_rate = audio_codec_context->sample_rate;
    frame->nb_samples = audio_codec_context->frame_size;
    frame->format = audio_codec_context->sample_fmt;
#if LIBAVCODEC_VERSION_MAJOR < 60
    frame->channels = audio_codec_context->channels;
    frame->channel_layout = audio_codec_context->channel_layout;
#else
    av_channel_layout_copy(&frame->ch_layout, &audio_codec_context->ch_layout);
#endif

    int ret = av_frame_get_buffer(frame, 0);
    if(ret < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to allocate audio data buffers, reason: %s", gsr_av_error_to_string(ret));
        av_frame_free(&frame);
        return NULL;
    }

    return frame;
}

double audio_codec_get_desired_delay(gsr_audio_codec audio_codec, int fps) {
    const double fps_inv = 1.0 / (double)fps;
    const double base = 0.01 + 1.0/165.0;
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_OPUS:
            return fmax(0.0, base - fps_inv);
        case GSR_AUDIO_CODEC_AAC:
            return fmax(0.0, (base + 0.008) * 2.0 - fps_inv);
        case GSR_AUDIO_CODEC_FLAC:
            // TODO: Test
            return fmax(0.0, base - fps_inv);
    }
    assert(false);
    return fmax(0.0, base - fps_inv);
}

int audio_codec_get_frame_size(gsr_audio_codec audio_codec) {
    switch(audio_codec) {
        case GSR_AUDIO_CODEC_AAC: return 1024;
        case GSR_AUDIO_CODEC_OPUS: return 960;
        case GSR_AUDIO_CODEC_FLAC:
            assert(false);
            return 1024;
    }
    assert(false);
    return 1024;
}
