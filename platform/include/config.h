/* platform/include/config.h — configuration interfaces for the Windows port.
 *
 * Phase 3 deliverable. Implementation: platform/windows/gsr_config_win32.c.
 *
 * Upstream's UI stores its settings in a custom key=value-line file
 * (~/.config/gpu-screen-recorder/config_ui) with keys like
 * main.show_hide_hotkey, main.tint_color, record.video_codec, ... and
 * built-in defaults (docs/upstream-analysis.md §4.2). The port keeps that
 * exact file format on Windows (%APPDATA%\\gpu-screen-recorder\\config_ui)
 * so the UI's config schema carries over unchanged (Phase 10).
 *
 * This module is the schema-driven machinery behind it: an option table
 * (name, type, default, constraints), load/save of the key=value format,
 * and typed getters. The schema returned by gsr_config_get_ui_schema()
 * covers the keys documented for upstream today; Phase 10 completes it to
 * the full UI set.
 */
#ifndef GSR_PLATFORM_CONFIG_H
#define GSR_PLATFORM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GSR_CONFIG_TYPE_BOOL,
    GSR_CONFIG_TYPE_INT,
    GSR_CONFIG_TYPE_STRING
} gsr_config_value_type;

typedef struct {
    const char *name;          /* "main.config_file_version" etc. */
    gsr_config_value_type type;
    bool bool_default;
    int64_t int_default;
    int64_t int_min;           /* inclusive; ignored when int_min > int_max */
    int64_t int_max;
    const char *string_default;
    /* For GSR_CONFIG_TYPE_STRING: NULL-terminated list of allowed values,
       or NULL when any string is allowed. */
    const char *const *values;
} gsr_config_option;

typedef struct {
    const gsr_config_option *schema;
    size_t num_options;
    /* Values, one per schema entry, indexed by the schema order. */
    bool *bool_values;
    int64_t *int_values;
    char **string_values;
} gsr_config;

/* Loads |config| with |schema| (the array must stay alive for the config's
 * lifetime). All values start at their defaults. Returns false only on
 * allocation failure. */
bool gsr_config_init(gsr_config *config, const gsr_config_option *schema, size_t num_options);

/* Frees the value storage (not the schema). */
void gsr_config_deinit(gsr_config *config);

/* Loads the key=value file at |path| (missing file = defaults; unknown
 * keys and malformed lines are skipped for forward compatibility; out-of-
 * range values keep their default and are reported via |num_errors| when
 * non-NULL). Values not present in the file keep their current value. */
bool gsr_config_load(gsr_config *config, const char *path, int *num_errors);

/* Saves all options as key=value lines (CRLF line endings, the format
 * Windows tools expect). Returns false when the file cannot be written. */
bool gsr_config_save(const gsr_config *config, const char *path);

/* Typed getters. Returns false when |name| is not in the schema. */
bool gsr_config_get_bool(const gsr_config *config, const char *name, bool *value);
bool gsr_config_get_int(const gsr_config *config, const char *name, int64_t *value);
bool gsr_config_get_string(const gsr_config *config, const char *name, const char **value);

/* Typed setters. For int: rejects out-of-range values (returns false and
 * leaves the value unchanged). For string: rejects values not in the
 * option's allowed list when one is set. */
bool gsr_config_set_bool(gsr_config *config, const char *name, bool value);
bool gsr_config_set_int(gsr_config *config, const char *name, int64_t value);
bool gsr_config_set_string(gsr_config *config, const char *name, const char *value);

/* The port's config_ui schema: keys documented for upstream
 * (docs/upstream-analysis.md §4.2) plus the per-feature sections. The
 * arrays returned are static; Phase 10 extends this list with the full UI
 * option set. */
const gsr_config_option *gsr_config_get_ui_schema(size_t *num_options);

#endif /* GSR_PLATFORM_CONFIG_H */
