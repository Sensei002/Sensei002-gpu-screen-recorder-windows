#include "../include/plugins.h"
#include "../include/log.h"
#include "../include/utils.h"
#include <string.h>
#include <dlfcn.h>
#include <assert.h>

static int color_depth_to_gl_internal_format(gsr_plugin_color_depth color_depth) {
    switch(color_depth) {
        case GSR_PLUGIN_COLOR_DEPTH_8_BITS:
            return GL_RGBA8;
        case GSR_PLUGIN_COLOR_DEPTH_10_BITS:
            return GL_RGBA16;
    }
    assert(false);
    return GL_RGBA8;
}

bool gsr_plugins_init(gsr_plugins *self, gsr_plugin_init_params init_params, gsr_egl *egl) {
    memset(self, 0, sizeof(*self));
    self->init_params = init_params;
    self->egl = egl;

    const unsigned int texture = gl_create_texture(egl, init_params.width, init_params.height, color_depth_to_gl_internal_format(init_params.color_depth), GL_RGBA, GL_LINEAR);
    if(texture == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_init failed to create texture");
        return false;
    }
    self->texture = texture;

    gsr_color_conversion_params color_conversion_params = {
        .egl = egl,
        .destination_color = GSR_DESTINATION_COLOR_RGB,
        .destination_textures[0] = self->texture,
        .destination_textures_size[0] = (vec2i){ init_params.width, init_params.height },
        .num_destination_textures = 1,
        .color_range = GSR_COLOR_RANGE_FULL,
        .load_external_image_shader = false,
        //.force_graphics_shader = false,
    };
    color_conversion_params.destination_textures[0] = self->texture;

    if(gsr_color_conversion_init(&self->color_conversion, &color_conversion_params) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_init failed to create color conversion");
        gsr_plugins_deinit(self);
        return false;
    }
    gsr_color_conversion_clear(&self->color_conversion);

    return true;
}

void gsr_plugins_deinit(gsr_plugins *self) {
    for(int i = self->num_plugins - 1; i >= 0; --i) {
        gsr_plugin *plugin = &self->plugins[i];
        plugin->gsr_plugin_deinit(plugin->data.userdata);
        gsr_log(GSR_LOG_LEVEL_INFO, "unloaded plugin: %s", plugin->data.name);
    }
    self->num_plugins = 0;

    if(self->texture > 0) {
        self->egl->glDeleteTextures(1, &self->texture);
        self->texture = 0;
    }

    gsr_color_conversion_deinit(&self->color_conversion);
}

bool gsr_plugins_load_plugin(gsr_plugins *self, const char *plugin_filepath) {
    if(self->num_plugins >= GSR_MAX_PLUGINS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed, more plugins can't load more than %d plugins. Report this as an issue", GSR_MAX_PLUGINS);
        return false;
    }

    gsr_plugin plugin;
    memset(&plugin, 0, sizeof(plugin));

    plugin.lib = dlopen(plugin_filepath, RTLD_LAZY);
    if(!plugin.lib) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to load \"%s\", error: %s", plugin_filepath, dlerror());
        return false;
    }

    plugin.gsr_plugin_init = dlsym(plugin.lib, "gsr_plugin_init");
    if(!plugin.gsr_plugin_init) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to find \"gsr_plugin_init\" in plugin \"%s\"", plugin_filepath);
        goto fail;
    }

    plugin.gsr_plugin_deinit = dlsym(plugin.lib, "gsr_plugin_deinit");
    if(!plugin.gsr_plugin_deinit) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to find \"gsr_plugin_deinit\" in plugin \"%s\"", plugin_filepath);
        goto fail;
    }

    if(!plugin.gsr_plugin_init(&self->init_params, &plugin.data)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to load plugin \"%s\", gsr_plugin_init in the plugin failed", plugin_filepath);
        goto fail;
    }

    if(!plugin.data.name) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to load plugin \"%s\", the plugin didn't set the name (gsr_plugin_init_return.name)", plugin_filepath);
        goto fail;
    }

    if(plugin.data.version == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_plugins_load_plugin failed to load plugin \"%s\", the plugin didn't set the version (gsr_plugin_init_return.version)", plugin_filepath);
        goto fail;
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "loaded plugin: %s, name: %s, version: %u", plugin_filepath, plugin.data.name, plugin.data.version);
    self->plugins[self->num_plugins] = plugin;
    ++self->num_plugins;
    return true;

    fail:
    dlclose(plugin.lib);
    return false;
}

void gsr_plugins_draw(gsr_plugins *self) {
    const gsr_plugin_draw_params params = {
        .width = self->init_params.width,
        .height = self->init_params.height,
    };

    self->egl->glBindFramebuffer(GL_FRAMEBUFFER, self->color_conversion.framebuffers[0]);
    self->egl->glViewport(0, 0, self->init_params.width, self->init_params.height);

    for(int i = 0; i < self->num_plugins; ++i) {
        const gsr_plugin *plugin = &self->plugins[i];
        if(plugin->data.draw)
            plugin->data.draw(&params, plugin->data.userdata);
    }

    self->egl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool gsr_plugins_is_damaged(gsr_plugins *self) {
    bool damaged = false;
    for(int i = 0; i < self->num_plugins; ++i) {
        const gsr_plugin *plugin = &self->plugins[i];
        if(plugin->data.is_damaged)
            damaged |= plugin->data.is_damaged(plugin->data.userdata);
    }
    return damaged;
}

void gsr_plugins_clear_damage(gsr_plugins *self) {
    for(int i = 0; i < self->num_plugins; ++i) {
        const gsr_plugin *plugin = &self->plugins[i];
        if(plugin->data.clear_damage)
            plugin->data.clear_damage(plugin->data.userdata);
    }
}
