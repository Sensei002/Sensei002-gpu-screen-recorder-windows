#include "../../include/recorder/capture_source.h"
#include "../../include/recorder/error.h"
#include "../../include/utils.h"
#include "../../include/log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    gsr_capture_source *capture_source;
    bool is_first_column;
    int error;
} parse_capture_source_options_userdata;

typedef struct {
    gsr_capture_sources *capture_sources;
    vec2i region_position;
    vec2i region_size;
    bool has_multiple_capture_sources;
    int error;
} parse_capture_source_arg_userdata;

void gsr_capture_source_init(gsr_capture_source *self, vec2i region_position, vec2i region_size) {
    memset(self, 0, sizeof(*self));
    self->type = GSR_CAPTURE_SOURCE_TYPE_WINDOW;
    self->halign = GSR_CAPTURE_ALIGN_CENTER;
    self->valign = GSR_CAPTURE_ALIGN_CENTER;
    self->v4l2_pixfmt = GSR_CAPTURE_V4L2_PIXFMT_AUTO;
    self->flip = GSR_FLIP_NONE;
    self->pos = (vvec2i){0, 0, VVEC2I_TYPE_PIXELS, VVEC2I_TYPE_PIXELS};
    self->size = (vvec2i){100, 100, VVEC2I_TYPE_SCALAR, VVEC2I_TYPE_SCALAR};
    self->region_pos = region_position;
    self->region_size = region_size;
}

static bool is_hex_num(char c) {
    return (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
}

static bool contains_non_hex_number(const char *str) {
    bool hex_start = false;
    size_t len = strlen(str);
    if(len >= 2 && memcmp(str, "0x", 2) == 0) {
        str += 2;
        len -= 2;
        hex_start = true;
    }

    bool is_hex = false;
    for(size_t i = 0; i < len; ++i) {
        char c = str[i];
        if(c == '\0')
            return false;
        if(!is_hex_num(c))
            return true;
        if((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            is_hex = true;
    }

    return is_hex && !hex_start;
}

static void capture_source_type_from_string(const char *capture_source_str, size_t size, gsr_capture_source *capture_source) {
    char capture_source_str_n[64];
    snprintf(capture_source_str_n, sizeof(capture_source_str_n), "%.*s", (int)size, capture_source_str);

    if(size == 7 && memcmp(capture_source_str_n, "focused", 7) == 0) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW;
    } else if(size == 6 && memcmp(capture_source_str_n, "portal", 6) == 0) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_PORTAL;
    } else if(size == 6 && memcmp(capture_source_str_n, "region", 6) == 0) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_REGION;
    } else if(size >= 10 && memcmp(capture_source_str_n, "/dev/video", 10) == 0) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_V4L2;
    } else if(sscanf(capture_source_str_n, "%dx%d+%d+%d", &capture_source->region_size.x, &capture_source->region_size.y, &capture_source->region_pos.x, &capture_source->region_pos.y) == 4) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_REGION;
        capture_source->region_set = true;
    } else if(contains_non_hex_number(capture_source_str_n)) {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_MONITOR;
    } else {
        capture_source->type = GSR_CAPTURE_SOURCE_TYPE_WINDOW;
    }
}

static bool string_to_capture_alignment(const char *str, size_t len, gsr_capture_alignment *alignment) {
    if(len == 5 && memcmp(str, "start", 5) == 0) {
        *alignment = GSR_CAPTURE_ALIGN_START;
        return true;
    } else if(len == 6 && memcmp(str, "center", 6) == 0) {
        *alignment = GSR_CAPTURE_ALIGN_CENTER;
        return true;
    } else if(len == 3 && memcmp(str, "end", 3) == 0) {
        *alignment = GSR_CAPTURE_ALIGN_END;
        return true;
    } else {
        return false;
    }
}

static bool string_to_v4l2_pixfmt(const char *str, size_t len, gsr_capture_v4l2_pixfmt *pixfmt) {
    if(len == 4 && memcmp(str, "auto", 4) == 0) {
        *pixfmt = GSR_CAPTURE_V4L2_PIXFMT_AUTO;
        return true;
    } else if(len == 4 && memcmp(str, "yuyv", 4) == 0) {
        *pixfmt = GSR_CAPTURE_V4L2_PIXFMT_YUYV;
        return true;
    } else if(len == 5 && memcmp(str, "mjpeg", 5) == 0) {
        *pixfmt = GSR_CAPTURE_V4L2_PIXFMT_MJPEG;
        return true;
    } else {
        return false;
    }
}

static bool string_to_bool(const char *str, size_t len, bool *value) {
    if(len == 4 && memcmp(str, "true", 4) == 0) {
        *value = true;
        return true;
    } else if(len == 5 && memcmp(str, "false", 5) == 0) {
        *value = false;
        return true;
    } else {
        return false;
    }
}

static bool parse_capture_source_options_callback(const char *sub, size_t size, void *userdata) {
    parse_capture_source_options_userdata *parse_userdata = userdata;
    gsr_capture_source *capture_source = parse_userdata->capture_source;
    if(size == 0)
        return true;

    /* First column contains the capture target */
    if(parse_userdata->is_first_column) {
        parse_userdata->is_first_column = false;
        return true;
    }

    if(gsr_string_starts_with(sub, size, "x=")) {
        capture_source->pos.x_type = sub[size - 1] == '%' ? VVEC2I_TYPE_SCALAR : VVEC2I_TYPE_PIXELS;
        sub += 2;
        size -= 2;
        if(!gsr_string_to_int(sub, size, &capture_source->pos.x)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option x: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "y=")) {
        capture_source->pos.y_type = sub[size - 1] == '%' ? VVEC2I_TYPE_SCALAR : VVEC2I_TYPE_PIXELS;
        sub += 2;
        size -= 2;
        if(!gsr_string_to_int(sub, size, &capture_source->pos.y)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option y: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "width=")) {
        capture_source->size.x_type = sub[size - 1] == '%' ? VVEC2I_TYPE_SCALAR : VVEC2I_TYPE_PIXELS;
        sub += 6;
        size -= 6;
        if(!gsr_string_to_int(sub, size, &capture_source->size.x)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option width: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "height=")) {
        capture_source->size.y_type = sub[size - 1] == '%' ? VVEC2I_TYPE_SCALAR : VVEC2I_TYPE_PIXELS;
        sub += 7;
        size -= 7;
        if(!gsr_string_to_int(sub, size, &capture_source->size.y)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option height: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "halign=")) {
        sub += 7;
        size -= 7;
        if(!string_to_capture_alignment(sub, size, &capture_source->halign)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option halign: \"%.*s\", expected a \"start\", \"center\" or \"end\"", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "valign=")) {
        sub += 7;
        size -= 7;
        if(!string_to_capture_alignment(sub, size, &capture_source->valign)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option valign: \"%.*s\", expected a \"start\", \"center\" or \"end\"", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "pixfmt=")) {
        sub += 7;
        size -= 7;
        if(!string_to_v4l2_pixfmt(sub, size, &capture_source->v4l2_pixfmt)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid v4l2 pixfmt value for option pixfmt: \"%.*s\", expected a \"auto\", \"yuyv\" or \"mjpeg\"", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "hflip=")) {
        sub += 6;
        size -= 6;
        bool hflip = false;
        if(!string_to_bool(sub, size, &hflip)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid bool value for option hflip: \"%.*s\", expected a \"true\" or \"false\"", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }

        if(hflip)
            capture_source->flip |= GSR_FLIP_HORIZONTAL;
    } else if(gsr_string_starts_with(sub, size, "vflip=")) {
        sub += 6;
        size -= 6;
        bool vflip = false;
        if(!string_to_bool(sub, size, &vflip)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid bool value for option vflip: \"%.*s\", expected a \"true\" or \"false\"", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }

        if(vflip)
            capture_source->flip |= GSR_FLIP_VERTICAL;
    } else if(gsr_string_starts_with(sub, size, "camera_fps=")) {
        sub += 11;
        size -= 11;
        if(!gsr_string_to_int(sub, size, &capture_source->camera_fps)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option camera_fps: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "camera_width=")) {
        sub += 13;
        size -= 13;
        if(!gsr_string_to_int(sub, size, &capture_source->camera_resolution.x)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option camera_width: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else if(gsr_string_starts_with(sub, size, "camera_height=")) {
        sub += 14;
        size -= 14;
        if(!gsr_string_to_int(sub, size, &capture_source->camera_resolution.y)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target value for option camera_height: \"%.*s\", expected a number", (int)size, sub);
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    } else {
        gsr_log(GSR_LOG_LEVEL_ERROR, "invalid capture target option \"%.*s\", expected x, y, width, height, halign, valign, pixfmt, hflip, vflip, camera_fps, camera_width or camera_height", (int)size, sub);
        parse_userdata->error = GSR_ERROR_GENERIC;
        return false;
    }

    return true;
}

static int parse_capture_source_options(const char *capture_source_str, size_t capture_source_str_size, gsr_capture_source *capture_source) {
    char options[1024];
    snprintf(options, sizeof(options), "%.*s", (int)capture_source_str_size, capture_source_str);

    parse_capture_source_options_userdata userdata;
    userdata.capture_source = capture_source;
    userdata.is_first_column = true;
    userdata.error = GSR_ERROR_OK;
    gsr_string_split(options, ';', parse_capture_source_options_callback, &userdata);
    return userdata.error;
}

static bool parse_capture_source_arg_callback(const char *sub, size_t size, void *userdata) {
    parse_capture_source_arg_userdata *parse_userdata = userdata;
    if(size == 0)
        return true;

    const char *substr_start = sub;
    const size_t substr_size = size;
    size_t capture_source_size = size;
    const char *capture_source_end = memchr(sub, ';', size);
    if(capture_source_end)
        capture_source_size = capture_source_end - sub;

    gsr_capture_source capture_source;
    gsr_capture_source_init(&capture_source, parse_userdata->region_position, parse_userdata->region_size);

    if(gsr_string_starts_with(sub, capture_source_size, "monitor:")) {
        capture_source.type = GSR_CAPTURE_SOURCE_TYPE_MONITOR;
        sub += 8;
        capture_source_size -= 8;
    } else if(gsr_string_starts_with(sub, capture_source_size, "window:")) {
        capture_source.type = GSR_CAPTURE_SOURCE_TYPE_WINDOW;
        sub += 7;
        capture_source_size -= 7;
    } else if(gsr_string_starts_with(sub, capture_source_size, "v4l2:")) {
        capture_source.type = GSR_CAPTURE_SOURCE_TYPE_V4L2;
        sub += 5;
        capture_source_size -= 5;
    } else {
        capture_source_type_from_string(sub, capture_source_size, &capture_source);
    }

    snprintf(capture_source.name, sizeof(capture_source.name), "%.*s", (int)capture_source_size, sub);

    if(capture_source.type == GSR_CAPTURE_SOURCE_TYPE_WINDOW) {
        if(!gsr_string_to_int64(capture_source.name, strlen(capture_source.name), &capture_source.window_id)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "invalid window number %s", capture_source.name);
            args_parser_print_usage();
            parse_userdata->error = GSR_ERROR_GENERIC;
            return false;
        }
    }

    if(parse_userdata->has_multiple_capture_sources) {
        capture_source.halign = GSR_CAPTURE_ALIGN_START;
        capture_source.valign = GSR_CAPTURE_ALIGN_START;
        capture_source.pos = (vvec2i){0, 0, VVEC2I_TYPE_PIXELS, VVEC2I_TYPE_PIXELS};
    }

    const int parse_options_result = parse_capture_source_options(substr_start, substr_size, &capture_source);
    if(parse_options_result != GSR_ERROR_OK) {
        parse_userdata->error = parse_options_result;
        return false;
    }

    gsr_capture_sources *capture_sources = parse_userdata->capture_sources;
    if(!gsr_array_ensure_capacity((void**)&capture_sources->items, capture_sources->num_items, &capture_sources->capacity_items, sizeof(gsr_capture_source))) {
        parse_userdata->error = GSR_ERROR_GENERIC;
        return false;
    }

    capture_sources->items[capture_sources->num_items] = capture_source;
    ++capture_sources->num_items;
    return true;
}

int gsr_capture_sources_parse(gsr_capture_sources *self, const char *capture_source_arg, vec2i region_position, vec2i region_size) {
    memset(self, 0, sizeof(*self));

    parse_capture_source_arg_userdata userdata;
    userdata.capture_sources = self;
    userdata.region_position = region_position;
    userdata.region_size = region_size;
    userdata.has_multiple_capture_sources = strchr(capture_source_arg, '|') != NULL;
    userdata.error = GSR_ERROR_OK;

    gsr_string_split(capture_source_arg, '|', parse_capture_source_arg_callback, &userdata);
    if(userdata.error != GSR_ERROR_OK)
        gsr_capture_sources_deinit(self);

    return userdata.error;
}

void gsr_capture_sources_deinit(gsr_capture_sources *self) {
    if(self->items) {
        free(self->items);
        self->items = NULL;
    }
    self->num_items = 0;
    self->capacity_items = 0;
}

bool gsr_capture_sources_has_type(const gsr_capture_sources *self, CaptureSourceType type) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type == type)
            return true;
    }
    return false;
}

bool gsr_capture_sources_has_damage_tracked_target(const gsr_capture_sources *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type != GSR_CAPTURE_SOURCE_TYPE_V4L2)
            return true;
    }
    return false;
}

bool gsr_capture_sources_has_region_set(const gsr_capture_sources *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type == GSR_CAPTURE_SOURCE_TYPE_REGION && self->items[i].region_set)
            return true;
    }
    return false;
}

bool gsr_capture_sources_has_monitor_or_region(const gsr_capture_sources *self) {
    for(size_t i = 0; i < self->num_items; ++i) {
        if(self->items[i].type == GSR_CAPTURE_SOURCE_TYPE_MONITOR || self->items[i].type == GSR_CAPTURE_SOURCE_TYPE_REGION)
            return true;
    }
    return false;
}
