#ifndef GSR_RECORDER_VIDEO_CODEC_H
#define GSR_RECORDER_VIDEO_CODEC_H

#include <stdbool.h>
#include "../defs.h"
#include "../egl.h"
#include "settings.h"

#include <libavcodec/avcodec.h>

int video_quality_to_h264_equivalent_qp(gsr_video_quality video_quality);
enum AVPixelFormat get_pixel_format(gsr_video_codec video_codec, gsr_gpu_vendor vendor, bool use_software_video_encoder);

AVCodecContext* create_video_codec_context(enum AVPixelFormat pix_fmt, const AVCodec *codec, const gsr_egl *egl, const gsr_recorder_settings *settings, int width, int height);
bool open_video_software(AVCodecContext *codec_context, const gsr_recorder_settings *settings);
bool open_video_hardware(AVCodecContext *codec_context, bool low_power, const gsr_egl *egl, const gsr_recorder_settings *settings);

#endif /* GSR_RECORDER_VIDEO_CODEC_H */
