#ifndef GSR_CODEC_QUERY_VAAPI_H
#define GSR_CODEC_QUERY_VAAPI_H

#include "codec_query.h"

bool gsr_get_supported_video_codecs_vaapi(gsr_supported_video_codecs *video_codecs, const char *card_path, bool cleanup);

#endif /* GSR_CODEC_QUERY_VAAPI_H */
