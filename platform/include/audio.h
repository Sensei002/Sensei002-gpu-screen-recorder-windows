/* platform/include/audio.h — audio device interfaces for the Windows port.
 *
 * Phase 3 deliverable. The WASAPI *capture* implementation lands in Phase 8
 * (platform/windows/audio_wasapi.c) behind the upstream sound_device_*
 * interface (upstream/include/sound.h) — that interface is what the engine
 * calls. This header adds the port-owned pieces: endpoint enumeration for
 * `--list-audio-devices` and the output format contract the UI parses.
 */
#ifndef GSR_PLATFORM_AUDIO_H
#define GSR_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    GSR_PLATFORM_AUDIO_DIRECTION_OUTPUT,
    GSR_PLATFORM_AUDIO_DIRECTION_INPUT
} gsr_platform_audio_direction;

typedef struct {
    char name[64];          /* stable endpoint id / alias (e.g. "default_output") */
    char description[128];  /* human-readable (e.g. "Built-in Speakers")          */
    gsr_platform_audio_direction direction;
    bool is_default;        /* currently the system default in its direction     */
} gsr_platform_audio_device;

/* Enumerates WASAPI endpoints. Allocates an array of |*out_count|
 * gsr_platform_audio_device entries with malloc(); the caller frees it.
 * Implemented in Phase 8 (IMMDeviceEnumerator). */
bool gsr_platform_audio_list_devices(gsr_platform_audio_device **out, int *out_count);

/* Formats one device as the `--list-audio-devices` line "name (description)"
 * (the format upstream uses — e.g. "Default Output (Built-in Speakers)" —
 * and the UI parses). Returns characters written (excluding NUL) or -1
 * when the buffer is too small. */
int gsr_platform_audio_format_device_line(const gsr_platform_audio_device *device, char *buf, size_t size);

#endif /* GSR_PLATFORM_AUDIO_H */
