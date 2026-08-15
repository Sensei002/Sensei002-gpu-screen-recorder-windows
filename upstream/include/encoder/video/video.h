#ifndef GSR_ENCODER_VIDEO_H
#define GSR_ENCODER_VIDEO_H

#include "../../color_conversion.h"
#include <stdbool.h>

#define GSR_MAX_RECORDING_DESTINATIONS 128

typedef struct gsr_video_encoder gsr_video_encoder;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVFrame AVFrame;

struct gsr_video_encoder {
    bool (*start)(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame);
    void (*destroy)(gsr_video_encoder *encoder, AVCodecContext *video_codec_context);
    void (*copy_textures_to_frame)(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion); /* Can be NULL */
    /* |textures| should be able to fit 2 elements */
    void (*get_textures)(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color);

    void *priv;
    bool started;
};

/* Set |replay_buffer_time_seconds| and |fps| to 0 to disable replay buffer */
bool gsr_video_encoder_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame);
void gsr_video_encoder_destroy(gsr_video_encoder *encoder, AVCodecContext *video_codec_context);
void gsr_video_encoder_copy_textures_to_frame(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion);
void gsr_video_encoder_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color);

#endif /* GSR_ENCODER_VIDEO_H */
