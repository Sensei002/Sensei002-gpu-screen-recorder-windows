#ifndef GSR_FFMPEG_UTILS_H
#define GSR_FFMPEG_UTILS_H

#include <stdbool.h>

typedef struct AVFormatContext AVFormatContext;

const char* gsr_av_error_to_string(int err);

/* Marks that a packet has been successfully written to |av_format_context|, see gsr_av_format_context_write_trailer */
void gsr_av_format_context_mark_packet_written(AVFormatContext *av_format_context);
/*
    The same as av_write_trailer, except that the trailer is not written when no packet has been written to a muxer
    that uses the hybrid_fragmented movflags option (see set_format_context_options), because the mov muxer in FFmpeg
    crashes when it finalizes a hybrid_fragmented file that has no packets. Returns 0 on success, just like av_write_trailer.
*/
int gsr_av_format_context_write_trailer(AVFormatContext *av_format_context);

#endif /* GSR_FFMPEG_UTILS_H */
