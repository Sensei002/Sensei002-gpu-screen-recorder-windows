#ifndef GSR_ARGS_PARSER_H
#define GSR_ARGS_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include "defs.h"
#include "vec2.h"
#include "recorder/settings.h"

typedef struct gsr_egl gsr_egl;

#define NUM_ARGS 39

typedef enum {
    GSR_CAPTURE_SOURCE_TYPE_WINDOW,
    GSR_CAPTURE_SOURCE_TYPE_FOCUSED_WINDOW,
    GSR_CAPTURE_SOURCE_TYPE_MONITOR,
    GSR_CAPTURE_SOURCE_TYPE_REGION,
    GSR_CAPTURE_SOURCE_TYPE_PORTAL,
    GSR_CAPTURE_SOURCE_TYPE_V4L2
} CaptureSourceType;

typedef enum {
    ARG_TYPE_STRING,
    ARG_TYPE_BOOLEAN,
    ARG_TYPE_ENUM,
    ARG_TYPE_I64,
    ARG_TYPE_DOUBLE,
} ArgType;

typedef struct {
    const char *name;
    int value;
} ArgEnum;

typedef struct {
    ArgType type;
    const char **values;
    int capacity_num_values;
    int num_values;

    const char *key;
    bool optional;
    bool list;

    const ArgEnum *enum_values;
    int num_enum_values;

    int64_t integer_value_min;
    int64_t integer_value_max;

    union {
        bool boolean;
        int enum_value;
        int64_t i64_value;
        double d_value;
    } typed_value;
} Arg;

/* These return the exit code that gpu-screen-recorder should exit with */
typedef struct {
    int (*version)(void *userdata);
    int (*info)(void *userdata);
    int (*list_audio_devices)(void *userdata);
    int (*list_application_audio)(void *userdata);
    int (*list_v4l2_devices)(void *userdata);
    int (*list_capture_options)(const char *card_path, void *userdata);
    int (*list_monitors)(void *userdata);
} args_handlers;

typedef enum {
    ARGS_PARSE_RESULT_ERROR,
    ARGS_PARSE_RESULT_OK,
    /* One of the |args_handlers| ran and |command_exit_code| is set */
    ARGS_PARSE_RESULT_COMMAND_HANDLED
} args_parse_result;

typedef struct {
    Arg args[NUM_ARGS];
    gsr_recorder_settings settings;
} args_parser;

/* |argv| is stored as a reference */
args_parse_result args_parser_parse(args_parser *self, int argc, char **argv, const args_handlers *args_handlers, void *userdata, int *command_exit_code);
void args_parser_deinit(args_parser *self);

bool args_parser_validate_with_gl_info(args_parser *self, gsr_egl *egl);
void args_parser_print_usage(void);
Arg* args_parser_get_arg(args_parser *self, const char *arg_name);

#endif /* GSR_ARGS_PARSER_H */
