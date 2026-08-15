#ifndef GSR_PLUGINS_H
#define GSR_PLUGINS_H

#include "../plugin/plugin.h"
#include <stdbool.h>
#include "color_conversion.h"

#define GSR_MAX_PLUGINS 128

typedef bool (*gsr_plugin_init_func)(const gsr_plugin_init_params *params, gsr_plugin_init_return *ret);
typedef void (*gsr_plugin_deinit_func)(void *userdata);

typedef struct {
    gsr_plugin_init_return data;
    void *lib;
    gsr_plugin_init_func gsr_plugin_init;
    gsr_plugin_deinit_func gsr_plugin_deinit;
} gsr_plugin;

typedef struct {
    gsr_plugin plugins[GSR_MAX_PLUGINS];
    int num_plugins;

    gsr_plugin_init_params init_params;
    gsr_egl *egl;

    unsigned int texture;
    gsr_color_conversion color_conversion;
} gsr_plugins;

bool gsr_plugins_init(gsr_plugins *self, gsr_plugin_init_params init_params, gsr_egl *egl);
/* Plugins are unloaded in reverse order */
void gsr_plugins_deinit(gsr_plugins *self);

bool gsr_plugins_load_plugin(gsr_plugins *self, const char *plugin_filepath);
void gsr_plugins_draw(gsr_plugins *self);
bool gsr_plugins_is_damaged(gsr_plugins *self);
void gsr_plugins_clear_damage(gsr_plugins *self);

#endif /* GSR_PLUGINS_H */
