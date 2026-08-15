#ifndef GSR_RECORDER_AUDIO_CODEC_H
#define GSR_RECORDER_AUDIO_CODEC_H

#include <stdbool.h>
#include <stdint.h>
#include "../defs.h"
#include "../sound.h"

#include <libavcodec/avcodec.h>

#define GSR_AUDIO_SAMPLE_RATE 48000

gsr_audio_format audio_codec_context_get_audio_format(const AVCodecContext *audio_codec_context);
enum AVSampleFormat audio_format_to_sample_format(const gsr_audio_format audio_format);

AVCodecContext* create_audio_codec_context(int fps, gsr_audio_codec audio_codec, bool mix_audio, int64_t audio_bitrate);
bool open_audio(AVCodecContext *audio_codec_context, const char *ffmpeg_audio_opts);
AVFrame* create_audio_frame(AVCodecContext *audio_codec_context);

double audio_codec_get_desired_delay(gsr_audio_codec audio_codec, int fps);
int audio_codec_get_frame_size(gsr_audio_codec audio_codec);

#endif /* GSR_RECORDER_AUDIO_CODEC_H */
