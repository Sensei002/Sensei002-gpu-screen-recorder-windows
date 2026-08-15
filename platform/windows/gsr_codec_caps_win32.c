/* gsr_codec_caps_win32.c — platform/include/codec_caps.h implementation.
 *
 * Phase 3 deliverable. Pure decision logic over the upstream
 * gsr_supported_video_codecs probe data (filled by the Phase 7 NVENC
 * probe): which `-k` options are offered and which encoder runs. No
 * hardware or FFmpeg probing here — fully testable headless.
 */
#include "../../platform/include/codec_caps.h"

#include <string.h>

const char *gsr_platform_codec_to_k_option(gsr_platform_codec codec) {
    static const char *const names[GSR_PLATFORM_CODEC_COUNT] = {
        "h264", "hevc", "hevc_hdr", "hevc_10bit",
        "av1", "av1_hdr", "av1_10bit", "vp8", "vp9"
    };
    return (codec < GSR_PLATFORM_CODEC_COUNT) ? names[codec] : "h264";
}

static const gsr_supported_video_codec *codec_entry(const gsr_supported_video_codecs *hw, gsr_platform_codec codec) {
    switch(codec) {
        case GSR_PLATFORM_CODEC_H264:       return &hw->h264;
        case GSR_PLATFORM_CODEC_HEVC:       return &hw->hevc;
        case GSR_PLATFORM_CODEC_HEVC_HDR:   return &hw->hevc_hdr;
        case GSR_PLATFORM_CODEC_HEVC_10BIT: return &hw->hevc_10bit;
        case GSR_PLATFORM_CODEC_AV1:        return &hw->av1;
        case GSR_PLATFORM_CODEC_AV1_HDR:    return &hw->av1_hdr;
        case GSR_PLATFORM_CODEC_AV1_10BIT:  return &hw->av1_10bit;
        case GSR_PLATFORM_CODEC_VP8:        return &hw->vp8;
        case GSR_PLATFORM_CODEC_VP9:        return &hw->vp9;
        default:                            return NULL;
    }
}

/* Software encoders in the port's pinned FFmpeg build: libx264 only
   (libvpx is not built, so vp8/vp9 are never offered; the pinned build is
   documented in scripts/ffmpeg-sources.sh). Keeping this table-driven
   makes a future build change a one-line edit. */
static bool codec_is_software(gsr_platform_codec codec) {
    return codec == GSR_PLATFORM_CODEC_H264;
}

bool gsr_platform_codec_supported(const gsr_supported_video_codecs *hw, bool hw_encoding, gsr_platform_codec codec) {
    if(!hw || codec >= GSR_PLATFORM_CODEC_COUNT)
        return false;

    if(codec_is_software(codec))
        return true;

    if(!hw_encoding)
        return false;

    const gsr_supported_video_codec *entry = codec_entry(hw, codec);
    if(!entry || !entry->supported)
        return false;

    /* HDR variants are only offered together with the matching 10-bit
       profile (upstream reports hevc_hdr/av1_hdr only when the _10bit
       profile exists). */
    if(codec == GSR_PLATFORM_CODEC_HEVC_HDR && !hw->hevc_10bit.supported)
        return false;
    if(codec == GSR_PLATFORM_CODEC_AV1_HDR && !hw->av1_10bit.supported)
        return false;

    return true;
}

int gsr_platform_codec_build_available_list(const gsr_supported_video_codecs *hw, bool hw_encoding, gsr_platform_codec *out, int out_capacity) {
    /* The order the UI lists codecs in (upstream's codec table order). */
    static const gsr_platform_codec order[] = {
        GSR_PLATFORM_CODEC_H264,
        GSR_PLATFORM_CODEC_HEVC,
        GSR_PLATFORM_CODEC_HEVC_HDR,
        GSR_PLATFORM_CODEC_HEVC_10BIT,
        GSR_PLATFORM_CODEC_AV1,
        GSR_PLATFORM_CODEC_AV1_HDR,
        GSR_PLATFORM_CODEC_AV1_10BIT,
        GSR_PLATFORM_CODEC_VP8,
        GSR_PLATFORM_CODEC_VP9
    };

    int count = 0;
    for(size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if(!gsr_platform_codec_supported(hw, hw_encoding, order[i]))
            continue;
        if(out && count < out_capacity)
            out[count] = order[i];
        ++count;
    }
    return count;
}

gsr_platform_encoder_choice gsr_platform_encoder_select(bool hw_requested, bool hw_available, bool fallback_cpu_allowed, bool cpu_available) {
    if(hw_requested && hw_available)
        return GSR_PLATFORM_ENCODER_GPU;
    if(hw_requested && fallback_cpu_allowed && cpu_available)
        return GSR_PLATFORM_ENCODER_CPU;
    if(!hw_requested && cpu_available)
        return GSR_PLATFORM_ENCODER_CPU;
    return GSR_PLATFORM_ENCODER_NONE;
}
