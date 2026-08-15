#ifndef GSR_RECORDER_CAPTURE_SOURCE_H
#define GSR_RECORDER_CAPTURE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../vec2.h"
#include "../args_parser.h"
#include "../capture/capture.h"
#include "../capture/v4l2.h"

#define GSR_CAPTURE_SOURCE_NAME_MAX_SIZE 256

typedef enum {
    VVEC2I_TYPE_PIXELS,
    VVEC2I_TYPE_SCALAR
} vvec2i_type;

typedef struct {
    int x;
    int y;
    vvec2i_type x_type;
    vvec2i_type y_type;
} vvec2i;

typedef struct {
    char name[GSR_CAPTURE_SOURCE_NAME_MAX_SIZE];
    CaptureSourceType type;
    gsr_capture_alignment halign;
    gsr_capture_alignment valign;
    gsr_capture_v4l2_pixfmt v4l2_pixfmt;
    uint32_t flip;
    vvec2i pos;
    vvec2i size;
    vec2i region_pos;
    vec2i region_size;
    bool region_set;
    int64_t window_id;
    int camera_fps;
    vec2i camera_resolution;
} gsr_capture_source;

typedef struct {
    gsr_capture_source *items;
    size_t num_items;
    size_t capacity_items;
} gsr_capture_sources;

void gsr_capture_source_init(gsr_capture_source *self, vec2i region_position, vec2i region_size);

/* Returns a |gsr_error| value. Parses the -w option value, which may contain multiple capture sources separated by | */
int gsr_capture_sources_parse(gsr_capture_sources *self, const char *capture_source_arg, vec2i region_position, vec2i region_size);
void gsr_capture_sources_deinit(gsr_capture_sources *self);

bool gsr_capture_sources_has_type(const gsr_capture_sources *self, CaptureSourceType type);
bool gsr_capture_sources_has_damage_tracked_target(const gsr_capture_sources *self);
bool gsr_capture_sources_has_region_set(const gsr_capture_sources *self);
bool gsr_capture_sources_has_monitor_or_region(const gsr_capture_sources *self);

#endif /* GSR_RECORDER_CAPTURE_SOURCE_H */
