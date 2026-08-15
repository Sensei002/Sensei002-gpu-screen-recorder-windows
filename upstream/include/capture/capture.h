#ifndef GSR_CAPTURE_CAPTURE_H
#define GSR_CAPTURE_CAPTURE_H

#include "../color_conversion.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AVCodecContext AVCodecContext;
typedef struct AVStream AVStream;
typedef struct AVFrame AVFrame;
typedef struct AVMasteringDisplayMetadata AVMasteringDisplayMetadata;
typedef struct AVContentLightMetadata AVContentLightMetadata;
typedef struct gsr_capture gsr_capture;
typedef struct gsr_capture_metadata gsr_capture_metadata;

typedef enum {
    GSR_CAPTURE_ALIGN_START,
    GSR_CAPTURE_ALIGN_CENTER,
    GSR_CAPTURE_ALIGN_END
} gsr_capture_alignment;

struct gsr_capture_metadata {
    // Size of the video
    vec2i video_size;
    // The captured output gets scaled to this size. By default this will be the same size as the captured target
    vec2i recording_size;
    vec2i position;
    int fps;
    gsr_capture_alignment halign;
    gsr_capture_alignment valign;
    gsr_flip flip;
};

struct gsr_capture {
    /* These methods should not be called manually. Call gsr_capture_* instead. |capture_metadata->video_size| should be set by this function */
    int (*start)(gsr_capture *cap, gsr_capture_metadata *capture_metadata);
    void (*on_event)(gsr_capture *cap, gsr_egl *egl); /* can be NULL */
    void (*tick)(gsr_capture *cap); /* can be NULL. If there is an event then |on_event| is called before this */
    bool (*should_stop)(gsr_capture *cap, bool *err); /* can be NULL. If NULL, return false */
    bool (*capture_has_synchronous_task)(gsr_capture *cap); /* can be NULL. If this returns true then the time spent in |capture| is ignored for video/audio (capture is paused while the synchronous task happens) */
    void (*pre_capture)(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion); /* can be NULL */
    int (*capture)(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion); /* Return 0 if the frame was captured */
    bool (*uses_external_image)(gsr_capture *cap); /* can be NULL. If NULL, return false */
    bool (*set_hdr_metadata)(gsr_capture *cap, AVMasteringDisplayMetadata *mastering_display_metadata, AVContentLightMetadata *light_metadata); /* can be NULL. If NULL, return false */
    uint64_t (*get_window_id)(gsr_capture *cap); /* can be NULL. Returns 0 if unknown */
    bool (*is_damaged)(gsr_capture *cap); /* can be NULL */
    void (*clear_damage)(gsr_capture *cap); /* can be NULL */
    void (*destroy)(gsr_capture *cap);

    void *priv; /* can be NULL */
    bool started;
};

int gsr_capture_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata);
void gsr_capture_on_event(gsr_capture *cap, gsr_egl *egl);
void gsr_capture_tick(gsr_capture *cap);
bool gsr_capture_should_stop(gsr_capture *cap, bool *err);
int gsr_capture_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion);
bool gsr_capture_uses_external_image(gsr_capture *cap);
bool gsr_capture_set_hdr_metadata(gsr_capture *cap, AVMasteringDisplayMetadata *mastering_display_metadata, AVContentLightMetadata *light_metadata);
void gsr_capture_destroy(gsr_capture *cap);

#endif /* GSR_CAPTURE_CAPTURE_H */
