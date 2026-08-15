#ifndef GSR_RECORDER_CODEC_SELECT_H
#define GSR_RECORDER_CODEC_SELECT_H

#include <stdbool.h>
#include "../defs.h"
#include "../egl.h"
#include "../vec2.h"
#include "../codec_query/codec_query.h"
#include "settings.h"

#include <libavcodec/avcodec.h>

typedef struct gsr_video_encoder gsr_video_encoder;

gsr_video_encoder* create_video_encoder(gsr_egl *egl, const gsr_recorder_settings *settings);

bool get_supported_video_codecs(gsr_egl *egl, gsr_video_codec video_codec, bool use_software_video_encoder, bool cleanup, gsr_supported_video_codecs *video_codecs);
void set_supported_video_codecs_ffmpeg(gsr_supported_video_codecs *supported_video_codecs, gsr_supported_video_codecs *supported_video_codecs_vulkan, gsr_gpu_vendor vendor);

vec2i codec_get_max_resolution(gsr_video_codec video_codec, bool use_software_video_encoder, const gsr_supported_video_codecs *supported_video_codecs);
bool codec_supports_resolution(vec2i codec_max_resolution, vec2i capture_resolution);
/* Returns -1 if none is available */
gsr_video_codec select_appropriate_video_codec_automatically(vec2i video_size, const gsr_supported_video_codecs *supported_video_codecs);
void force_cpu_encoding(gsr_recorder_settings *settings);

gsr_audio_codec select_audio_codec_with_fallback(gsr_audio_codec audio_codec, const char *file_extension, bool uses_amix);
/* Returns a |gsr_error| value. |settings| video codec and encoder are updated to the codec that was selected */
int select_video_codec_with_fallback(vec2i video_size, gsr_recorder_settings *settings, const char *file_extension, gsr_egl *egl, bool *low_power, const AVCodec **video_codec);

#endif /* GSR_RECORDER_CODEC_SELECT_H */
