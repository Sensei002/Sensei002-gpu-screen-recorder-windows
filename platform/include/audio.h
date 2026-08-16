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
    char name[128];         /* stable endpoint id / alias (e.g. "default_output") */
    char description[128];  /* human-readable (e.g. "Built-in Speakers")          */
    gsr_platform_audio_direction direction;
    bool is_default;        /* currently the system default in its direction     */
} gsr_platform_audio_device;

typedef struct {
    char name[256];         /* session display name ("Spotify", "System Sounds")  */
    unsigned long pid;      /* owning process id (0 = system session)             */
    int state;              /* 0 inactive, 1 active, 2 expired                    */
} gsr_platform_audio_app;

/* Enumerates the audio sessions on the default render endpoint (what the
 * Windows Volume Mixer shows). Allocates an array of |*out_count|
 * gsr_platform_audio_app entries with malloc(); the caller frees it with
 * gsr_platform_audio_apps_free. Returns true on success (including a
 * valid empty list when no session exists); false on COM failure.
 * Implemented in Phase 8 milestone B (IAudioSessionManager2). This is the
 * Windows equivalent of upstream's `--list-application-audio` (PulseAudio
 * app streams). Per-app CAPTURE is not feasible with WASAPI (loopback is
 * endpoint-wide; there is no per-session capture client) — see
 * docs/upstream-porting-notes.md §3k.
 */
bool gsr_platform_audio_list_apps(gsr_platform_audio_app **out, int *out_count);

void gsr_platform_audio_apps_free(gsr_platform_audio_app *items);

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
