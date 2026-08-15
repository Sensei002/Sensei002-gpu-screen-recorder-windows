/* platform/include/codec_caps.h — encoder/codec capability logic for the
 * Windows port.
 *
 * Phase 3 deliverable. Implementation: platform/windows/gsr_codec_caps_win32.c.
 *
 * The engine's capability probing lives in src/codec_query/ (nvenc.c etc.)
 * and fills the upstream gsr_supported_video_codecs struct
 * (upstream/include/codec_query/codec_query.h). This module is the pure
 * decision layer on top of that data: which `-k` codec options are
 * actually available to the user, whether HDR/10-bit variants show up, and
 * whether the requested encoder (`-encoder gpu|cpu` with
 * `-fallback-cpu-encoding`) can be honored. No GPU, driver or FFmpeg
 * probing happens here — the Phase 7 probe fills the struct, this logic
 * turns it into decisions, and the tests exercise every combination
 * headlessly (brief §64: capability logic is CI-testable without hardware).
 */
#ifndef GSR_PLATFORM_CODEC_CAPS_H
#define GSR_PLATFORM_CODEC_CAPS_H

#include <stdbool.h>
#include <stddef.h>

#include "../../upstream/include/codec_query/codec_query.h"

/* The `-k` codec values upstream accepts, minus the vulkan variants (the
 * Windows port has no vulkan encoder path) and 'auto'/'h265' aliases. */
typedef enum {
    GSR_PLATFORM_CODEC_H264,
    GSR_PLATFORM_CODEC_HEVC,
    GSR_PLATFORM_CODEC_HEVC_HDR,
    GSR_PLATFORM_CODEC_HEVC_10BIT,
    GSR_PLATFORM_CODEC_AV1,
    GSR_PLATFORM_CODEC_AV1_HDR,
    GSR_PLATFORM_CODEC_AV1_10BIT,
    GSR_PLATFORM_CODEC_VP8,
    GSR_PLATFORM_CODEC_VP9,
    GSR_PLATFORM_CODEC_COUNT
} gsr_platform_codec;

/* The `-k` value for a codec ("h264", "hevc_hdr", ...). Never NULL. */
const char *gsr_platform_codec_to_k_option(gsr_platform_codec codec);

/* Whether |codec| can be offered given the hardware caps and the encoder
 * mode. Software encoders (libx264, libvpx for vp8/vp9) are always
 * available in the port's FFmpeg build; hardware codecs need their
 * gsr_supported_video_codec.supported flag AND hardware encoding to be
 * active. HDR variants additionally require the matching 10-bit profile
 * (upstream only reports hevc_hdr/av1_hdr together with _10bit). */
bool gsr_platform_codec_supported(const gsr_supported_video_codecs *hw, bool hw_encoding, gsr_platform_codec codec);

/* Fills |out| with the offered codecs in the same order the UI lists them
 * (h264, hevc, hevc_hdr, hevc_10bit, av1, av1_hdr, av1_10bit, vp8, vp9),
 * skipping unavailable ones. Returns the number of entries written. */
int gsr_platform_codec_build_available_list(const gsr_supported_video_codecs *hw, bool hw_encoding, gsr_platform_codec *out, int out_capacity);

/* Encoder selection (the `-encoder gpu|cpu` + `-fallback-cpu-encoding`
 * semantics, pure):
 *
 *   - hardware requested and available            -> GPU
 *   - hardware requested, unavailable, fallback
 *     allowed and cpu available                   -> CPU (fallback)
 *   - hardware requested and nothing available    -> NONE (error path)
 *   - cpu requested (and available)               -> CPU
 */
typedef enum {
    GSR_PLATFORM_ENCODER_GPU,
    GSR_PLATFORM_ENCODER_CPU,
    GSR_PLATFORM_ENCODER_NONE
} gsr_platform_encoder_choice;

gsr_platform_encoder_choice gsr_platform_encoder_select(bool hw_requested, bool hw_available, bool fallback_cpu_allowed, bool cpu_available);

#endif /* GSR_PLATFORM_CODEC_CAPS_H */
