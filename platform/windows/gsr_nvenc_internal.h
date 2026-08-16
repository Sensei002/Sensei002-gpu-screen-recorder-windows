/* platform/windows/gsr_nvenc_internal.h — internal test seam for the
 * Windows NVENC encoder (Phase 7, milestone B).
 *
 * The live NVENC path (D3D11 device -> d3d11va hw frames -> h264/hevc/av1
 * nvenc) cannot run on CI: the runner has no NVIDIA GPU, exactly like the
 * WASAPI capture path in Phase 8. What CAN run headless is the pure GPU
 * generation table — which NVENC codecs each NVIDIA generation supports —
 * derived from the adapter description string (e.g. "NVIDIA GeForce RTX
 * 3060"). The live probe (gsr_get_supported_video_codecs_nvenc) is
 * authoritative at runtime; this table is its pre-filter (skip probing
 * codecs a generation cannot have) and the CI-testable logic.
 *
 * The rest of gsr_nvenc_win32.c (the d3d11va encoder vtable and the live
 * probe) stays internal to that file.
 */
#ifndef GSR_NVENC_INTERNAL_H
#define GSR_NVENC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "../../upstream/include/vec2.h"

typedef enum {
    GSR_NVENC_GEN_UNKNOWN = 0,
    GSR_NVENC_GEN_MAXWELL,    /* GTX 9xx                */
    GSR_NVENC_GEN_PASCAL,     /* GTX 10xx               */
    GSR_NVENC_GEN_TURING,     /* GTX 16xx, RTX 20xx     */
    GSR_NVENC_GEN_AMPERE,     /* RTX 30xx               */
    GSR_NVENC_GEN_ADA,        /* RTX 40xx               */
    GSR_NVENC_GEN_BLACKWELL,  /* RTX 50xx               */
    GSR_NVENC_GEN_COUNT
} gsr_nvenc_generation;

typedef struct {
    bool h264;            /* H.264 encode (all NVENC GPUs)               */
    bool hevc;            /* HEVC 8-bit encode                           */
    bool hevc_10bit;      /* HEVC Main10 encode                          */
    bool av1;             /* AV1 8-bit encode (Ampere+)                  */
    bool av1_10bit;       /* AV1 10-bit encode                           */
    vec2i h264_max;       /* session resolution limit for h264_nvenc     */
    vec2i hevc_av1_max;   /* session resolution limit for hevc/av1_nvenc */
} gsr_nvenc_generation_caps;

/* Map an adapter description string ("NVIDIA GeForce RTX 3060", ...) to a
   generation. Case-insensitive substring matching; returns UNKNOWN for
   anything not recognized (future generations probe everything instead). */
gsr_nvenc_generation gsr_nvenc_generation_from_adapter_description(const char *description);

/* The NVENC codec table for a generation. Never NULL (UNKNOWN returns a
   "probe everything" caps). */
const gsr_nvenc_generation_caps *gsr_nvenc_get_generation_caps(gsr_nvenc_generation gen);

/* True when the description names an NVIDIA adapter. */
bool gsr_nvenc_description_is_nvidia(const char *description);

/* ---- live probe helpers (used by gsr_get_supported_video_codecs_nvenc
   and by the self-test to decide its assertions) -------------------------- */

/* Create a hardware D3D11 device and write its adapter description
   (ASCII) into |out|. Returns false when no hardware device exists. */
bool gsr_nvenc_get_adapter_description(char *out, size_t out_size);

#endif /* GSR_NVENC_INTERNAL_H */
