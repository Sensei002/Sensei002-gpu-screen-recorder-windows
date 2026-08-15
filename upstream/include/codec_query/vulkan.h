#ifndef GSR_CODEC_QUERY_VULKAN_H
#define GSR_CODEC_QUERY_VULKAN_H

#include "codec_query.h"

bool gsr_get_supported_video_codecs_vulkan(gsr_supported_video_codecs *video_codecs, const char *card_path, int *device_index_ret, bool cleanup);

#endif /* GSR_CODEC_QUERY_VULKAN_H */
