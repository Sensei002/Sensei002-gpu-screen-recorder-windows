/* gsr_config_win32.c — platform/include/config.h implementation.
 *
 * Phase 3 deliverable. Schema-driven config in upstream's config_ui
 * key=value-line format (docs/upstream-analysis.md §4.2). Files are opened
 * through the wide-char helpers so non-ASCII paths (%APPDATA% under a
 * non-ASCII user name) work.
 */
#include "../../platform/include/config.h"
#include "../../platform/include/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

/* ---- file I/O through UTF-16 paths --------------------------------------- */

static FILE *config_fopen(const char *path, const char *mode) {
    wchar_t wpath[1024];
    wchar_t wmode[8];
    if(!gsr_platform_utf8_to_wide(path, wpath, sizeof(wpath) / sizeof(wpath[0])))
        return NULL;
    if(!gsr_platform_utf8_to_wide(mode, wmode, sizeof(wmode) / sizeof(wmode[0])))
        return NULL;
    return _wfopen(wpath, wmode);
}

/* ---- lookup --------------------------------------------------------------- */

static const gsr_config_option *find_option(const gsr_config *config, const char *name, size_t *index) {
    for(size_t i = 0; i < config->num_options; ++i) {
        if(strcmp(config->schema[i].name, name) == 0) {
            if(index)
                *index = i;
            return &config->schema[i];
        }
    }
    return NULL;
}

/* ---- lifecycle ------------------------------------------------------------- */

bool gsr_config_init(gsr_config *config, const gsr_config_option *schema, size_t num_options) {
    memset(config, 0, sizeof(*config));
    config->schema = schema;
    config->num_options = num_options;

    config->bool_values = calloc(num_options, sizeof(bool));
    config->int_values = calloc(num_options, sizeof(int64_t));
    config->string_values = calloc(num_options, sizeof(char *));
    if(!config->bool_values || !config->int_values || !config->string_values) {
        gsr_config_deinit(config);
        return false;
    }

    for(size_t i = 0; i < num_options; ++i) {
        switch(schema[i].type) {
            case GSR_CONFIG_TYPE_BOOL:
                config->bool_values[i] = schema[i].bool_default;
                break;
            case GSR_CONFIG_TYPE_INT:
                config->int_values[i] = schema[i].int_default;
                break;
            case GSR_CONFIG_TYPE_STRING:
                config->string_values[i] = strdup(schema[i].string_default ? schema[i].string_default : "");
                if(!config->string_values[i]) {
                    gsr_config_deinit(config);
                    return false;
                }
                break;
        }
    }
    return true;
}

void gsr_config_deinit(gsr_config *config) {
    if(!config)
        return;
    if(config->string_values) {
        for(size_t i = 0; i < config->num_options; ++i)
            free(config->string_values[i]);
    }
    free(config->string_values);
    free(config->int_values);
    free(config->bool_values);
    memset(config, 0, sizeof(*config));
}

/* ---- load / save ------------------------------------------------------------ */

bool gsr_config_load(gsr_config *config, const char *path, int *num_errors) {
    if(num_errors)
        *num_errors = 0;

    FILE *file = config_fopen(path, "r");
    if(!file)
        return true; /* missing file = defaults */

    char line[4096];
    while(fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        const char *eq = strchr(line, '=');
        if(!eq)
            continue; /* malformed line: skip */

        /* key = trimmed */
        const char *key_start = line;
        const char *key_end = eq;
        while(key_start < key_end && (*key_start == ' ' || *key_start == '\t'))
            ++key_start;
        while(key_end > key_start && (key_end[-1] == ' ' || key_end[-1] == '\t'))
            --key_end;

        /* value = trimmed */
        const char *val_start = eq + 1;
        const char *val_end = line + len;
        while(val_start < val_end && (*val_start == ' ' || *val_start == '\t'))
            ++val_start;
        while(val_end > val_start && (val_end[-1] == ' ' || val_end[-1] == '\t'))
            --val_end;

        size_t opt_index = 0;
        const gsr_config_option *opt = NULL;
        for(size_t i = 0; i < config->num_options; ++i) {
            const size_t name_len = strlen(config->schema[i].name);
            if(name_len == (size_t)(key_end - key_start) && strncmp(config->schema[i].name, key_start, name_len) == 0) {
                opt = &config->schema[i];
                opt_index = i;
                break;
            }
        }
        if(!opt)
            continue; /* unknown key: forward compatibility */

        char value[1024];
        size_t value_len = (size_t)(val_end - val_start);
        if(value_len >= sizeof(value))
            value_len = sizeof(value) - 1;
        memcpy(value, val_start, value_len);
        value[value_len] = '\0';

        bool ok = false;
        switch(opt->type) {
            case GSR_CONFIG_TYPE_BOOL:
                if(strcmp(value, "true") == 0) {
                    config->bool_values[opt_index] = true;
                    ok = true;
                } else if(strcmp(value, "false") == 0) {
                    config->bool_values[opt_index] = false;
                    ok = true;
                }
                break;

            case GSR_CONFIG_TYPE_INT: {
                errno = 0;
                char *end = NULL;
                const long long parsed = strtoll(value, &end, 10);
                if(errno == 0 && end != value && *end == '\0') {
                    const bool has_range = opt->int_min <= opt->int_max;
                    if(!has_range || (parsed >= opt->int_min && parsed <= opt->int_max)) {
                        config->int_values[opt_index] = parsed;
                        ok = true;
                    }
                }
                break;
            }

            case GSR_CONFIG_TYPE_STRING:
                if(opt->values) {
                    for(const char *const *v = opt->values; *v; ++v) {
                        if(strcmp(*v, value) == 0) {
                            ok = true;
                            break;
                        }
                    }
                } else {
                    ok = true;
                }
                if(ok) {
                    free(config->string_values[opt_index]);
                    config->string_values[opt_index] = strdup(value);
                    if(!config->string_values[opt_index])
                        ok = false;
                }
                break;
        }

        if(!ok && num_errors)
            ++(*num_errors);
    }

    fclose(file);
    return true;
}

bool gsr_config_save(const gsr_config *config, const char *path) {
    FILE *file = config_fopen(path, "w");
    if(!file)
        return false;

    for(size_t i = 0; i < config->num_options; ++i) {
        const gsr_config_option *opt = &config->schema[i];
        switch(opt->type) {
            case GSR_CONFIG_TYPE_BOOL:
                fprintf(file, "%s=%s\r\n", opt->name, config->bool_values[i] ? "true" : "false");
                break;
            case GSR_CONFIG_TYPE_INT:
                fprintf(file, "%s=%" PRId64 "\r\n", opt->name, config->int_values[i]);
                break;
            case GSR_CONFIG_TYPE_STRING:
                fprintf(file, "%s=%s\r\n", opt->name, config->string_values[i] ? config->string_values[i] : "");
                break;
        }
    }

    return fclose(file) == 0;
}

/* ---- typed getters / setters ------------------------------------------------ */

bool gsr_config_get_bool(const gsr_config *config, const char *name, bool *value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_BOOL)
        return false;
    *value = config->bool_values[index];
    return true;
}

bool gsr_config_get_int(const gsr_config *config, const char *name, int64_t *value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_INT)
        return false;
    *value = config->int_values[index];
    return true;
}

bool gsr_config_get_string(const gsr_config *config, const char *name, const char **value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_STRING)
        return false;
    *value = config->string_values[index];
    return true;
}

bool gsr_config_set_bool(gsr_config *config, const char *name, bool value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_BOOL)
        return false;
    config->bool_values[index] = value;
    return true;
}

bool gsr_config_set_int(gsr_config *config, const char *name, int64_t value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_INT)
        return false;
    if(opt->int_min <= opt->int_max && (value < opt->int_min || value > opt->int_max))
        return false;
    config->int_values[index] = value;
    return true;
}

bool gsr_config_set_string(gsr_config *config, const char *name, const char *value) {
    size_t index = 0;
    const gsr_config_option *opt = find_option(config, name, &index);
    if(!opt || opt->type != GSR_CONFIG_TYPE_STRING)
        return false;
    if(opt->values) {
        bool allowed = false;
        for(const char *const *v = opt->values; *v; ++v) {
            if(strcmp(*v, value) == 0) {
                allowed = true;
                break;
            }
        }
        if(!allowed)
            return false;
    }
    char *copy = strdup(value ? value : "");
    if(!copy)
        return false;
    free(config->string_values[index]);
    config->string_values[index] = copy;
    return true;
}

/* ---- the port's config_ui schema -------------------------------------------- */

static const char *const ui_codec_values[] = { "h264", "hevc", "av1", NULL };
static const char *const ui_storage_values[] = { "ram", "disk", NULL };

/* NOTE: provisional schema (Phase 3). The option names mirror the upstream
   config_ui keys documented in docs/upstream-analysis.md §4.2; the default
   values are the engine's/UI's known defaults where documented and sensible
   placeholders otherwise. Phase 10 replaces this table with the complete
   upstream config_ui option set. */
static const gsr_config_option ui_schema[] = {
    { "main.config_file_version",  GSR_CONFIG_TYPE_INT,    false, 1,      1, 100, NULL,   NULL },
    { "main.show_hide_hotkey",     GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "alt+z", NULL },
    { "main.tint_color",           GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "00000080", NULL },
    { "main.language",             GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "en",   NULL },
    { "main.hotkeys_enable_option",GSR_CONFIG_TYPE_BOOL,   true,  0,      0, 0,   NULL,   NULL },
    { "main.exclude_metadata",     GSR_CONFIG_TYPE_BOOL,   false, 0,      0, 0,   NULL,   NULL },
    { "record.video_codec",        GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "h264", ui_codec_values },
    { "record.fps",                GSR_CONFIG_TYPE_INT,    false, 60,     1, 240, NULL,   NULL },
    { "record.bitrate",            GSR_CONFIG_TYPE_INT,    false, 20000,  1000, 200000, NULL, NULL },
    { "record.save_directory",     GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "",     NULL },
    { "replay.video_codec",        GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "h264", ui_codec_values },
    { "replay.fps",                GSR_CONFIG_TYPE_INT,    false, 60,     1, 240, NULL,   NULL },
    { "replay.bitrate",            GSR_CONFIG_TYPE_INT,    false, 20000,  1000, 200000, NULL, NULL },
    { "replay.replay_buffer_size", GSR_CONFIG_TYPE_INT,    false, 20,     1, 120, NULL,   NULL },
    { "replay.replay_storage",     GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "ram",  ui_storage_values },
    { "replay.save_directory",     GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "",     NULL },
    { "screenshot.save_directory", GSR_CONFIG_TYPE_STRING, false, 0,      0, 0,   "",     NULL },
};

const gsr_config_option *gsr_config_get_ui_schema(size_t *num_options) {
    if(num_options)
        *num_options = sizeof(ui_schema) / sizeof(ui_schema[0]);
    return ui_schema;
}
