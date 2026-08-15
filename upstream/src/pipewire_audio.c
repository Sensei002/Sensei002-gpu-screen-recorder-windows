#include "../include/pipewire_audio.h"
#include "../include/log.h"
#include "../include/utils.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/impl-module.h>

typedef struct {
    const gsr_pipewire_audio_port *output_port;
    const gsr_pipewire_audio_port *input_port;
} gsr_pipewire_audio_desired_link;

static void on_core_info_cb(void *user_data, const struct pw_core_info *info) {
    gsr_pipewire_audio *self = user_data;
    //fprintf(stderr, "server name: %s\n", info->name);
}

static void on_core_error_cb(void *user_data, uint32_t id, int seq, int res, const char *message) {
    gsr_pipewire_audio *self = user_data;
    //fprintf(stderr, "gsr error: pipewire: error id:%u seq:%d res:%d: %s\n", id, seq, res, message);
    pw_thread_loop_signal(self->thread_loop, false);
}

static void on_core_done_cb(void *user_data, uint32_t id, int seq) {
    gsr_pipewire_audio *self = user_data;
    if(id == PW_ID_CORE && self->server_version_sync == seq)
        pw_thread_loop_signal(self->thread_loop, false);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .info = on_core_info_cb,
    .done = on_core_done_cb,
    .error = on_core_error_cb,
};

static gsr_pipewire_audio_node* gsr_pipewire_audio_get_node_by_name_case_insensitive(gsr_pipewire_audio *self, const char *node_name, gsr_pipewire_audio_node_type node_type) {
    for(size_t i = 0; i < self->num_stream_nodes; ++i) {
        const gsr_pipewire_audio_node *node = &self->stream_nodes[i];
        if(node->type == node_type && strcasecmp(node->name, node_name) == 0)
            return &self->stream_nodes[i];
    }
    return NULL;
}

static gsr_pipewire_audio_port* gsr_pipewire_audio_get_node_port_by_name(gsr_pipewire_audio *self, uint32_t node_id, const char *port_name) {
    for(size_t i = 0; i < self->num_ports; ++i) {
        if(self->ports[i].node_id == node_id && strcmp(self->ports[i].name, port_name) == 0)
            return &self->ports[i];
    }
    return NULL;
}

static bool requested_link_matches_name_case_insensitive(const gsr_pipewire_audio_requested_link *requested_link, const char *name) {
    for(int i = 0; i < requested_link->num_outputs; ++i) {
        if(requested_link->outputs[i].type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD && strcasecmp(requested_link->outputs[i].name, name) == 0)
            return true;
    }
    return false;
}

static bool requested_link_matches_name_case_insensitive_any_type(const gsr_pipewire_audio *self, const gsr_pipewire_audio_requested_link *requested_link, const char *name) {
    for(int i = 0; i < requested_link->num_outputs; ++i) {
        switch(requested_link->outputs[i].type) {
            case GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD: {
                if(strcasecmp(requested_link->outputs[i].name, name) == 0)
                    return true;
                break;
            }
            case GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT: {
                if(strcasecmp(self->default_output_device_name, name) == 0)
                    return true;
                break;
            }
            case GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT: {
                if(strcasecmp(self->default_input_device_name, name) == 0)
                    return true;
                break;
            }
        }
    }
    return false;
}

static bool gsr_pipewire_audio_node_is_application(const gsr_pipewire_audio_node *node) {
    return node->type == GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT && node->is_application && !node->is_virtual;
}

static bool requested_link_has_type(const gsr_pipewire_audio_requested_link *requested_link, gsr_pipewire_audio_requested_type type) {
    for(int i = 0; i < requested_link->num_outputs; ++i) {
        if(requested_link->outputs[i].type == type)
            return true;
    }
    return false;
}

static void gsr_pipewire_get_node_input_port_by_type(gsr_pipewire_audio *self, const gsr_pipewire_audio_node *input_node, gsr_pipewire_audio_link_input_type input_type,
    const gsr_pipewire_audio_port **input_fl_port, const gsr_pipewire_audio_port **input_fr_port)
{
    *input_fl_port = NULL;
    *input_fr_port = NULL;

    switch(input_type) {
        case GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM: {
            *input_fl_port = gsr_pipewire_audio_get_node_port_by_name(self, input_node->id, "input_FL");
            *input_fr_port = gsr_pipewire_audio_get_node_port_by_name(self, input_node->id, "input_FR");
            break;
        }
        case GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK: {
            *input_fl_port = gsr_pipewire_audio_get_node_port_by_name(self, input_node->id, "playback_FL");
            *input_fr_port = gsr_pipewire_audio_get_node_port_by_name(self, input_node->id, "playback_FR");
            break;
        }
    }
}

static bool string_starts_with(const char *str, const char *substr) {
    const int len = strlen(str);
    const int substr_len = strlen(substr);
    return len >= substr_len && memcmp(str, substr, substr_len) == 0;
}

static bool string_ends_with(const char *str, const char *substr) {
    const int len = strlen(str);
    const int substr_len = strlen(substr);
    return len >= substr_len && memcmp(str + len - substr_len, substr, substr_len) == 0;
}

/* Returns number of desired links */
static size_t gsr_pipewire_get_node_output_ports(gsr_pipewire_audio *self, const gsr_pipewire_audio_node *output_node,
    gsr_pipewire_audio_desired_link *desired_links, size_t desired_links_max_size,
    const gsr_pipewire_audio_port *input_fl_port, const gsr_pipewire_audio_port *input_fr_port)
{
    size_t num_desired_links = 0;
    for(size_t i = 0; i < self->num_ports && num_desired_links < desired_links_max_size; ++i) {
        if(self->ports[i].node_id != output_node->id)
            continue;

        if(string_starts_with(self->ports[i].name, "playback_"))
            continue;

        if(string_ends_with(self->ports[i].name, "_MONO") || string_ends_with(self->ports[i].name, "_FC") || string_ends_with(self->ports[i].name, "_LFE")) {
            if(num_desired_links + 2 >= desired_links_max_size)
                break;

            desired_links[num_desired_links + 0] = (gsr_pipewire_audio_desired_link){ .output_port = &self->ports[i], .input_port = input_fl_port };
            desired_links[num_desired_links + 1] = (gsr_pipewire_audio_desired_link){ .output_port = &self->ports[i], .input_port = input_fr_port };
            num_desired_links += 2;
        } else if(string_ends_with(self->ports[i].name, "_FL") || string_ends_with(self->ports[i].name, "_RL") || string_ends_with(self->ports[i].name, "_SL")) {
            if(num_desired_links + 1 >= desired_links_max_size)
                break;

            desired_links[num_desired_links] = (gsr_pipewire_audio_desired_link){ .output_port = &self->ports[i], .input_port = input_fl_port };
            num_desired_links += 1;
        } else if(string_ends_with(self->ports[i].name, "_FR") || string_ends_with(self->ports[i].name, "_RR") || string_ends_with(self->ports[i].name, "_SR")) {
            if(num_desired_links + 1 >= desired_links_max_size)
                break;

            desired_links[num_desired_links] = (gsr_pipewire_audio_desired_link){ .output_port = &self->ports[i], .input_port = input_fr_port };
            num_desired_links += 1;
        }
    }
    return num_desired_links;
}

static void gsr_pipewire_audio_establish_link(gsr_pipewire_audio *self, const gsr_pipewire_audio_port *output_port, const gsr_pipewire_audio_port *input_port) {
    // TODO: Detect if link already exists before so we dont create these proxies when not needed.
    // We could do that by saving which nodes have been linked with which nodes after linking them.

    //fprintf(stderr, "linking!\n");
    // TODO: error check and cleanup
    struct pw_properties *props = pw_properties_new(NULL, NULL);
    pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", output_port->id);
    pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", input_port->id);
    // TODO: Clean this up when removing node
    struct pw_proxy *proxy = pw_core_create_object(self->core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0);
    //self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, self->server_version_sync);
    pw_properties_free(props);
}

static void gsr_pipewire_audio_create_link(gsr_pipewire_audio *self, const gsr_pipewire_audio_requested_link *requested_link) {
    const gsr_pipewire_audio_node_type requested_link_node_type = requested_link->input_type == GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM ? GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT : GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE;
    const gsr_pipewire_audio_node *stream_input_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, requested_link->input_name, requested_link_node_type);
    if(!stream_input_node)
        return;

    const gsr_pipewire_audio_port *input_fl_port = NULL;
    const gsr_pipewire_audio_port *input_fr_port = NULL;
    gsr_pipewire_get_node_input_port_by_type(self, stream_input_node, requested_link->input_type, &input_fl_port, &input_fr_port);
    if(!input_fl_port || !input_fr_port)
        return;

    gsr_pipewire_audio_desired_link desired_links[64];
    for(size_t i = 0; i < self->num_stream_nodes; ++i) {
        const gsr_pipewire_audio_node *output_node = &self->stream_nodes[i];
        if(output_node->type != requested_link->output_type)
            continue;

        if(requested_link->output_type == GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT && !gsr_pipewire_audio_node_is_application(output_node))
            continue;

        const bool requested_link_matches_app = requested_link_matches_name_case_insensitive_any_type(self, requested_link, output_node->name);
        if(requested_link->inverted) {
            if(requested_link_matches_app)
                continue;
        } else {
            if(!requested_link_matches_app)
                continue;
        }

        const size_t num_desired_links = gsr_pipewire_get_node_output_ports(self, output_node, desired_links, 64, input_fl_port, input_fr_port);
        for(size_t j = 0; j < num_desired_links; ++j) {
            gsr_pipewire_audio_establish_link(self, desired_links[j].output_port, desired_links[j].input_port);
        }
    }
}

static void gsr_pipewire_audio_create_links(gsr_pipewire_audio *self) {
    for(size_t i = 0; i < self->num_requested_links; ++i) {
        gsr_pipewire_audio_create_link(self, &self->requested_links[i]);
    }
}

static void gsr_pipewire_audio_create_link_for_default_devices(gsr_pipewire_audio *self, const gsr_pipewire_audio_requested_link *requested_link, gsr_pipewire_audio_requested_type default_device_type) {
    if(default_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD)
        return;

    const char *device_name = default_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT ? self->default_output_device_name : self->default_input_device_name;
    if(device_name[0] == '\0')
        return;

    if(!requested_link_has_type(requested_link, default_device_type))
        return;

    const gsr_pipewire_audio_node_type requested_link_node_type = requested_link->input_type == GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM ? GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT : GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE;
    const gsr_pipewire_audio_node *stream_input_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, requested_link->input_name, requested_link_node_type);
    if(!stream_input_node)
        return;

    const gsr_pipewire_audio_port *input_fl_port = NULL;
    const gsr_pipewire_audio_port *input_fr_port = NULL;
    gsr_pipewire_get_node_input_port_by_type(self, stream_input_node, requested_link->input_type, &input_fl_port, &input_fr_port);
    if(!input_fl_port || !input_fr_port)
        return;

    const gsr_pipewire_audio_node *stream_output_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, device_name, GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE);
    if(!stream_output_node)
        return;

    gsr_pipewire_audio_desired_link desired_links[64];
    const size_t num_desired_links = gsr_pipewire_get_node_output_ports(self, stream_output_node, desired_links, 64, input_fl_port, input_fr_port);
    for(size_t i = 0; i < num_desired_links; ++i) {
        gsr_pipewire_audio_establish_link(self, desired_links[i].output_port, desired_links[i].input_port);
    }
}

static void gsr_pipewire_audio_create_links_for_default_devices(gsr_pipewire_audio *self, gsr_pipewire_audio_requested_type default_device_type) {
    for(size_t i = 0; i < self->num_requested_links; ++i) {
        gsr_pipewire_audio_create_link_for_default_devices(self, &self->requested_links[i], default_device_type);
    }
}

static void gsr_pipewire_audio_destroy_links_by_output_to_input(gsr_pipewire_audio *self, uint32_t output_node_id, uint32_t input_node_id) {
    for(size_t i = 0; i < self->num_links; ++i) {
        if(self->links[i].output_node_id == output_node_id && self->links[i].input_node_id == input_node_id)
            pw_registry_destroy(self->registry, self->links[i].id);
    }
}

static void gsr_pipewire_destroy_default_device_link(gsr_pipewire_audio *self, const gsr_pipewire_audio_requested_link *requested_link, gsr_pipewire_audio_requested_type default_device_type) {
    if(default_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD)
        return;

    const char *device_name = default_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT ? self->default_output_device_name : self->default_input_device_name;
    if(device_name[0] == '\0')
        return;

    if(!requested_link_has_type(requested_link, default_device_type))
        return;

    /* default_output and default_input can be the same device. In that case both are the same link and we dont want to remove the link */
    const gsr_pipewire_audio_requested_type opposite_device_type = default_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT ? GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT : GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT;
    const char *opposite_device_name = opposite_device_type == GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT ? self->default_output_device_name : self->default_input_device_name;
    if(requested_link_has_type(requested_link, opposite_device_type) && strcmp(device_name, opposite_device_name) == 0)
        return;

    const gsr_pipewire_audio_node_type requested_link_node_type = requested_link->input_type == GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM ? GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT : GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE;
    const gsr_pipewire_audio_node *stream_input_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, requested_link->input_name, requested_link_node_type);
    if(!stream_input_node)
        return;

    const gsr_pipewire_audio_node *stream_output_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, device_name, GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE);
    if(!stream_output_node)
        return;

    if(requested_link_matches_name_case_insensitive(requested_link, stream_output_node->name))
        return;

    gsr_pipewire_audio_destroy_links_by_output_to_input(self, stream_output_node->id, stream_input_node->id);
    //fprintf(stderr, "destroying a link from %u to %u\n", stream_output_node->id, stream_input_node->id);
}

static void gsr_pipewire_destroy_default_device_links(gsr_pipewire_audio *self, gsr_pipewire_audio_requested_type default_device_type) {
    for(size_t i = 0; i < self->num_requested_links; ++i) {
        gsr_pipewire_destroy_default_device_link(self, &self->requested_links[i], default_device_type);
    }
}

static bool json_get_value(const char *json_str, const char *key, char *value, size_t value_size) {
    char key_full[32];
    const int key_full_size = snprintf(key_full, sizeof(key_full), "\"%s\":", key);
    const char *start = strstr(json_str, key_full);
    if(!start)
        return false;
    
    start += key_full_size;
    const char *value_start = strchr(start, '"');
    if(!value_start)
        return false;

    value_start += 1;
    const char *value_end = strchr(value_start, '"');
    if(!value_end)
        return false;

    snprintf(value, value_size, "%.*s", (int)(value_end - value_start), value_start);
    return true;
}

static int on_metadata_property_cb(void *data, uint32_t id, const char *key, const char *type, const char *value) {
	(void)type;
    gsr_pipewire_audio *self = data;

	if(id == PW_ID_CORE && key && value) {
        char value_decoded[128];
        if(strcmp(key, "default.audio.sink") == 0) {
            if(json_get_value(value, "name", value_decoded, sizeof(value_decoded)) && strcmp(value_decoded, self->default_output_device_name) != 0) {
                gsr_pipewire_destroy_default_device_links(self, GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT);
                snprintf(self->default_output_device_name, sizeof(self->default_output_device_name), "%s", value_decoded);
                gsr_pipewire_audio_create_links_for_default_devices(self, GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT);
            }
        } else if(strcmp(key, "default.audio.source") == 0) {
            if(json_get_value(value, "name", value_decoded, sizeof(value_decoded)) && strcmp(value_decoded, self->default_input_device_name) != 0) {
                gsr_pipewire_destroy_default_device_links(self, GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT);
                snprintf(self->default_input_device_name, sizeof(self->default_input_device_name), "%s", value_decoded);
                gsr_pipewire_audio_create_links_for_default_devices(self, GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT);
            }
        }
	}

	return 0;
}

static const struct pw_metadata_events metadata_events = {
	PW_VERSION_METADATA_EVENTS,
	.property = on_metadata_property_cb,
};

static void on_metadata_proxy_removed_cb(void *data) {
    gsr_pipewire_audio *self = data;
    if(self->metadata_proxy) {
        pw_proxy_destroy(self->metadata_proxy);
        self->metadata_proxy = NULL;
    }
}

static void on_metadata_proxy_destroy_cb(void *data) {
	gsr_pipewire_audio *self = data;

	spa_hook_remove(&self->metadata_listener);
	spa_hook_remove(&self->metadata_proxy_listener);
	spa_zero(self->metadata_listener);
	spa_zero(self->metadata_proxy_listener);

	self->metadata_proxy = NULL;
}

static const struct pw_proxy_events metadata_proxy_events = {
	PW_VERSION_PROXY_EVENTS,
	.removed = on_metadata_proxy_removed_cb,
	.destroy = on_metadata_proxy_destroy_cb,
};

static bool gsr_pipewire_audio_listen_on_metadata(gsr_pipewire_audio *self, uint32_t id) {
    if(self->metadata_proxy) {
        pw_proxy_destroy(self->metadata_proxy);
        self->metadata_proxy = NULL;
    }

    self->metadata_proxy = pw_registry_bind(self->registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0);
    if(!self->metadata_proxy) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_audio_listen_on_metadata: failed to bind to registry");
        return false;
    }

    pw_proxy_add_object_listener(self->metadata_proxy, &self->metadata_listener, &metadata_events, self);
    pw_proxy_add_listener(self->metadata_proxy, &self->metadata_proxy_listener, &metadata_proxy_events, self);

    self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, self->server_version_sync);
    return true;
}

struct gsr_pipewire_audio_node_binding {
    gsr_pipewire_audio *self;
    struct pw_proxy *proxy;
    struct spa_hook object_listener;
    struct spa_hook proxy_listener;
    uint32_t node_id;
};

static gsr_pipewire_audio_node* gsr_pipewire_audio_get_node_by_id(gsr_pipewire_audio *self, uint32_t node_id) {
    for(size_t i = 0; i < self->num_stream_nodes; ++i) {
        if(self->stream_nodes[i].id == node_id)
            return &self->stream_nodes[i];
    }
    return NULL;
}

static void gsr_pipewire_audio_add_node(gsr_pipewire_audio *self, uint32_t id, const char *node_name, gsr_pipewire_audio_node_type type, bool is_virtual, bool is_application) {
    if(!gsr_array_ensure_capacity((void**)&self->stream_nodes, self->num_stream_nodes, &self->stream_nodes_capacity_items, sizeof(gsr_pipewire_audio_node)))
        return;

    char *node_name_copy = strdup(node_name);
    if(!node_name_copy)
        return;

    self->stream_nodes[self->num_stream_nodes].id = id;
    self->stream_nodes[self->num_stream_nodes].name = node_name_copy;
    self->stream_nodes[self->num_stream_nodes].type = type;
    self->stream_nodes[self->num_stream_nodes].is_virtual = is_virtual;
    self->stream_nodes[self->num_stream_nodes].is_application = is_application;
    ++self->num_stream_nodes;

    gsr_pipewire_audio_create_links(self);
}

static bool node_properties_is_virtual(const char *node_virtual, const char *media_class) {
    if(node_virtual && (strcmp(node_virtual, "true") == 0 || strcmp(node_virtual, "1") == 0))
        return true;
    return media_class && string_ends_with(media_class, "/Virtual");
}

static void on_node_info_cb(void *data, const struct pw_node_info *info) {
    gsr_pipewire_audio_node_binding *node_binding = data;
    gsr_pipewire_audio *self = node_binding->self;
    if(!info || !info->props || !self->running)
        return;

    const char *node_name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
    if(!node_name)
        return;

    const char *media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
    const char *node_virtual = spa_dict_lookup(info->props, PW_KEY_NODE_VIRTUAL);
    const bool is_virtual = node_properties_is_virtual(node_virtual, media_class);

    /* application.id is optional and not all applications set it (firefox for example doesn't).
       Nodes not created by an application (loopback and filter streams for example) have none of these properties. */
    const bool is_application = spa_dict_lookup(info->props, PW_KEY_APP_ID) != NULL
        || spa_dict_lookup(info->props, PW_KEY_APP_NAME) != NULL
        || spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_BINARY) != NULL;

    gsr_pipewire_audio_node *existing_node = gsr_pipewire_audio_get_node_by_id(self, node_binding->node_id);
    if(existing_node) {
        existing_node->is_virtual = is_virtual;
        if(is_application)
            existing_node->is_application = true;
    } else {
        gsr_pipewire_audio_add_node(self, node_binding->node_id, node_name, GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, is_virtual, is_application);
    }
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = on_node_info_cb,
};

static void on_node_proxy_removed_cb(void *data) {
    gsr_pipewire_audio_node_binding *node_binding = data;
    pw_proxy_destroy(node_binding->proxy);
}

static void gsr_pipewire_audio_forget_node_binding(gsr_pipewire_audio *self, const gsr_pipewire_audio_node_binding *node_binding) {
    for(size_t i = 0; i < self->num_node_bindings; ++i) {
        if(self->node_bindings[i] == node_binding) {
            self->node_bindings[i] = self->node_bindings[self->num_node_bindings - 1];
            --self->num_node_bindings;
            break;
        }
    }
}

static void on_node_proxy_destroy_cb(void *data) {
    gsr_pipewire_audio_node_binding *node_binding = data;
    spa_hook_remove(&node_binding->object_listener);
    spa_hook_remove(&node_binding->proxy_listener);
    gsr_pipewire_audio_forget_node_binding(node_binding->self, node_binding);
    free(node_binding);
}

static const struct pw_proxy_events node_proxy_events = {
    PW_VERSION_PROXY_EVENTS,
    .removed = on_node_proxy_removed_cb,
    .destroy = on_node_proxy_destroy_cb,
};

/* The registry only broadcasts a small set of the node properties (node.name, media.class, application.name, etc).
   Binding to the node is required to get the other properties (node.virtual, application.id, etc) from the node info event. */
static void gsr_pipewire_audio_bind_node(gsr_pipewire_audio *self, uint32_t id) {
    if(!gsr_array_ensure_capacity((void**)&self->node_bindings, self->num_node_bindings, &self->node_bindings_capacity_items, sizeof(gsr_pipewire_audio_node_binding*)))
        return;

    gsr_pipewire_audio_node_binding *node_binding = calloc(1, sizeof(gsr_pipewire_audio_node_binding));
    if(!node_binding)
        return;

    struct pw_proxy *proxy = pw_registry_bind(self->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
    if(!proxy) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_audio_bind_node: failed to bind to node %u", id);
        free(node_binding);
        return;
    }

    node_binding->self = self;
    node_binding->proxy = proxy;
    node_binding->node_id = id;
    pw_proxy_add_object_listener(proxy, &node_binding->object_listener, &node_events, node_binding);
    pw_proxy_add_listener(proxy, &node_binding->proxy_listener, &node_proxy_events, node_binding);

    self->node_bindings[self->num_node_bindings] = node_binding;
    ++self->num_node_bindings;
}

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
                  const char *type, uint32_t version,
                  const struct spa_dict *props)
{
    //fprintf(stderr, "add: id: %d, type: %s\n", (int)id, type);
    gsr_pipewire_audio *self = (gsr_pipewire_audio*)data;
    if(!props || !type || !self->running)
        return;

    //pw_properties_new_dict(props);

    if(strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char *node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        //fprintf(stderr, "  node id: %u, node name: %s, media class: %s\n", id, node_name, media_class);
        const bool is_stream_output = media_class && strcmp(media_class, "Stream/Output/Audio") == 0;
        const bool is_stream_input = media_class && strcmp(media_class, "Stream/Input/Audio") == 0;
        const bool is_sink = media_class && string_starts_with(media_class, "Audio/Sink"); // Matches Audio/Sink/Virtual as well
        const bool is_source = media_class && string_starts_with(media_class, "Audio/Source"); // Matches Audio/Source/Virtual as well
        if(node_name && is_stream_output) {
            /* Stream output nodes (application audio) are added when the full node info is received instead */
            gsr_pipewire_audio_bind_node(self, id);
        } else if(node_name && (is_stream_input || is_sink || is_source)) {
            gsr_pipewire_audio_node_type node_type = GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE;
            if(is_stream_input)
                node_type = GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT;

            gsr_pipewire_audio_add_node(self, id, node_name, node_type, node_properties_is_virtual(NULL, media_class), false);
        }
    } else if(strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {
        const char *port_name = spa_dict_lookup(props, PW_KEY_PORT_NAME);

        const char *port_direction = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
        gsr_pipewire_audio_port_direction direction = -1;
        if(port_direction && strcmp(port_direction, "in") == 0)
            direction = GSR_PIPEWIRE_AUDIO_PORT_DIRECTION_INPUT;
        else if(port_direction && strcmp(port_direction, "out") == 0)
            direction = GSR_PIPEWIRE_AUDIO_PORT_DIRECTION_OUTPUT;

        const char *node_id = spa_dict_lookup(props, PW_KEY_NODE_ID);
        const int node_id_num = node_id ? atoi(node_id) : 0;

        if(port_name && direction >= 0 && node_id_num > 0) {
            if(!gsr_array_ensure_capacity((void**)&self->ports, self->num_ports, &self->ports_capacity_items, sizeof(gsr_pipewire_audio_port)))
                return;

            //fprintf(stderr, "  port name: %s, node id: %d, direction: %s\n", port_name, node_id_num, port_direction);
            char *port_name_copy = strdup(port_name);
            if(port_name_copy) {
                //fprintf(stderr, "  port id: %u, node id: %u, name: %s\n", id, node_id_num, port_name_copy);
                self->ports[self->num_ports].id = id;
                self->ports[self->num_ports].node_id = node_id_num;
                self->ports[self->num_ports].direction = direction;
                self->ports[self->num_ports].name = port_name_copy;
                ++self->num_ports;

                gsr_pipewire_audio_create_links(self);
            }
        }
    } else if(strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        const char *output_node = spa_dict_lookup(props, PW_KEY_LINK_OUTPUT_NODE);
        const char *input_node = spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE);

        const uint32_t output_node_id_num = output_node ? atoi(output_node) : 0;
        const uint32_t input_node_id_num = input_node ? atoi(input_node) : 0;
        if(output_node_id_num > 0 && input_node_id_num > 0) {
            if(!gsr_array_ensure_capacity((void**)&self->links, self->num_links, &self->links_capacity_items, sizeof(gsr_pipewire_audio_link)))
                return;

            //fprintf(stderr, "  new link (%u): %u -> %u\n", id, output_node_id_num, input_node_id_num);
            self->links[self->num_links].id = id;
            self->links[self->num_links].output_node_id = output_node_id_num;
            self->links[self->num_links].input_node_id = input_node_id_num;
            ++self->num_links;
        }
    } else if(strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if(name && strcmp(name, "default") == 0)
            gsr_pipewire_audio_listen_on_metadata(self, id);
    }
}

static bool gsr_pipewire_audio_remove_node_by_id(gsr_pipewire_audio *self, uint32_t node_id) {
    for(size_t i = 0; i < self->num_stream_nodes; ++i) {
        if(self->stream_nodes[i].id != node_id)
            continue;

        free(self->stream_nodes[i].name);
        self->stream_nodes[i] = self->stream_nodes[self->num_stream_nodes - 1];
        --self->num_stream_nodes;
        return true;
    }
    return false;
}

static bool gsr_pipewire_audio_remove_port_by_id(gsr_pipewire_audio *self, uint32_t port_id) {
    for(size_t i = 0; i < self->num_ports; ++i) {
        if(self->ports[i].id != port_id)
            continue;

        free(self->ports[i].name);
        self->ports[i] = self->ports[self->num_ports - 1];
        --self->num_ports;
        return true;
    }
    return false;
}

static bool gsr_pipewire_audio_remove_link_by_id(gsr_pipewire_audio *self, uint32_t link_id) {
    for(size_t i = 0; i < self->num_links; ++i) {
        if(self->links[i].id != link_id)
            continue;

        self->links[i] = self->links[self->num_links - 1];
        --self->num_links;
        return true;
    }
    return false;
}

static void registry_event_global_remove(void *data, uint32_t id) {
    //fprintf(stderr, "remove: %d\n", (int)id);
    gsr_pipewire_audio *self = (gsr_pipewire_audio*)data;
    if(gsr_pipewire_audio_remove_node_by_id(self, id)) {
        //fprintf(stderr, "removed node\n");
        return;
    }

    if(gsr_pipewire_audio_remove_port_by_id(self, id)) {
        //fprintf(stderr, "removed port\n");
        return;
    }

    if(gsr_pipewire_audio_remove_link_by_id(self, id)) {
        //fprintf(stderr, "removed link\n");
        return;
    }
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

bool gsr_pipewire_audio_init(gsr_pipewire_audio *self) {
    memset(self, 0, sizeof(*self));
    self->running = true;

    pw_init(NULL, NULL);
    
    self->thread_loop = pw_thread_loop_new("gsr screen capture", NULL);
    if(!self->thread_loop) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_audio_init: failed to create pipewire thread");
        gsr_pipewire_audio_deinit(self);
        return false;
    }

    self->context = pw_context_new(pw_thread_loop_get_loop(self->thread_loop), NULL, 0);
    if(!self->context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_audio_init: failed to create pipewire context");
        gsr_pipewire_audio_deinit(self);
        return false;
    }

    pw_context_load_module(self->context, "libpipewire-module-link-factory", NULL, NULL);

    if(pw_thread_loop_start(self->thread_loop) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_pipewire_audio_init: failed to start thread");
        gsr_pipewire_audio_deinit(self);
        return false;
    }

    pw_thread_loop_lock(self->thread_loop);

    self->core = pw_context_connect(self->context, pw_properties_new(PW_KEY_REMOTE_NAME, NULL, NULL), 0);
    if(!self->core) {
        pw_thread_loop_unlock(self->thread_loop);
        gsr_pipewire_audio_deinit(self);
        return false;
    }

    // TODO: Error check
    pw_core_add_listener(self->core, &self->core_listener, &core_events, self);

    self->registry = pw_core_get_registry(self->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(self->registry, &self->registry_listener, &registry_events, self);

    self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, self->server_version_sync);
    pw_thread_loop_wait(self->thread_loop);

    pw_thread_loop_unlock(self->thread_loop);
    return true;
}

static gsr_pipewire_audio_link* gsr_pipewire_audio_get_first_link_to_node(gsr_pipewire_audio *self, uint32_t node_id) {
    for(size_t i = 0; i < self->num_links; ++i) {
        if(self->links[i].input_node_id == node_id)
            return &self->links[i];
    }
    return NULL;
}

static void gsr_pipewire_audio_destroy_requested_links(gsr_pipewire_audio *self) {
    pw_thread_loop_lock(self->thread_loop);

    self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, self->server_version_sync);
    pw_thread_loop_wait(self->thread_loop);

    for(size_t requested_link_index = 0; requested_link_index < self->num_requested_links; ++requested_link_index) {
        const gsr_pipewire_audio_node_type requested_link_node_type = self->requested_links[requested_link_index].input_type == GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM ? GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT : GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE;
        const gsr_pipewire_audio_node *stream_input_node = gsr_pipewire_audio_get_node_by_name_case_insensitive(self, self->requested_links[requested_link_index].input_name, requested_link_node_type);
        if(!stream_input_node)
            continue;

        for(;;) {
            gsr_pipewire_audio_link *link = gsr_pipewire_audio_get_first_link_to_node(self, stream_input_node->id);
            if(!link)
                break;

            pw_registry_destroy(self->registry, link->id);

            self->server_version_sync = pw_core_sync(self->core, PW_ID_CORE, self->server_version_sync);
            pw_thread_loop_wait(self->thread_loop);

            usleep(10 * 1000);
        }
    }

    pw_thread_loop_unlock(self->thread_loop);
}

void gsr_pipewire_audio_deinit(gsr_pipewire_audio *self) {
    self->running = false;

    if(self->thread_loop) {
        /* We need to manually destroy links first, otherwise the linked audio sources will be paused when closing the program */
        gsr_pipewire_audio_destroy_requested_links(self);
        //pw_thread_loop_wait(self->thread_loop);
        pw_thread_loop_stop(self->thread_loop);
    }

    if(self->metadata_proxy) {
        spa_hook_remove(&self->metadata_listener);
        spa_hook_remove(&self->metadata_proxy_listener);
        pw_proxy_destroy(self->metadata_proxy);
        spa_zero(self->metadata_listener);
        spa_zero(self->metadata_proxy_listener);
        self->metadata_proxy = NULL;
    }

    /* pw_proxy_destroy removes the node binding from node_bindings (in the proxy destroy event) */
    while(self->num_node_bindings > 0) {
        pw_proxy_destroy(self->node_bindings[0]->proxy);
    }

    if(self->node_bindings) {
        free(self->node_bindings);
        self->node_bindings = NULL;
        self->node_bindings_capacity_items = 0;
    }

    spa_hook_remove(&self->registry_listener);
    spa_hook_remove(&self->core_listener);

    if(self->registry) {
        pw_proxy_destroy((struct pw_proxy*)self->registry);
        self->registry = NULL;
    }

    if(self->core) {
        pw_core_disconnect(self->core);
        self->core = NULL;
    }

    if(self->context) {
        pw_context_destroy(self->context);
        self->context = NULL;
    }

    if(self->thread_loop) {
        pw_thread_loop_destroy(self->thread_loop);
        self->thread_loop = NULL;
    }

    if(self->stream_nodes) {
        for(size_t i = 0; i < self->num_stream_nodes; ++i) {
            free(self->stream_nodes[i].name);
        }
        self->num_stream_nodes = 0;
        self->stream_nodes_capacity_items = 0;

        free(self->stream_nodes);
        self->stream_nodes = NULL;
    }

    if(self->ports) {
        for(size_t i = 0; i < self->num_ports; ++i) {
            free(self->ports[i].name);
        }
        self->num_ports = 0;
        self->ports_capacity_items = 0;

        free(self->ports);
        self->ports = NULL;
    }

    if(self->links) {
        self->num_links = 0;
        self->links_capacity_items = 0;

        free(self->links);
        self->links = NULL;
    }

    if(self->requested_links) {
        for(size_t i = 0; i < self->num_requested_links; ++i) {
            for(int j = 0; j < self->requested_links[i].num_outputs; ++j) {
                free(self->requested_links[i].outputs[j].name);
            }
            free(self->requested_links[i].outputs);
            free(self->requested_links[i].input_name);
        }
        self->num_requested_links = 0;
        self->requested_links_capacity_items = 0;

        free(self->requested_links);
        self->requested_links = NULL;
    }

#if PW_CHECK_VERSION(0, 3, 49)
    pw_deinit();
#endif
}

static bool string_remove_suffix(char *str, const char *suffix) {
    int str_len = strlen(str);
    int suffix_len = strlen(suffix);
    if(str_len >= suffix_len && memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0) {
        str[str_len - suffix_len] = '\0';
        return true;
    } else {
        return false;
    }
}

static bool gsr_pipewire_audio_add_links_to_output(gsr_pipewire_audio *self, const char **output_names, int num_output_names, const char *input_name, gsr_pipewire_audio_node_type output_type, gsr_pipewire_audio_link_input_type input_type, bool inverted) {
    if(!gsr_array_ensure_capacity((void**)&self->requested_links, self->num_requested_links, &self->requested_links_capacity_items, sizeof(gsr_pipewire_audio_requested_link)))
        return false;
    
    gsr_pipewire_audio_requested_output *outputs = calloc(num_output_names, sizeof(gsr_pipewire_audio_requested_output));
    if(!outputs)
        return false;

    char *input_name_copy = strdup(input_name);
    if(!input_name_copy)
        goto error;

    if(input_type == GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK)
        string_remove_suffix(input_name_copy, ".monitor");

    for(int i = 0; i < num_output_names; ++i) {
        outputs[i].name = strdup(output_names[i]);
        if(!outputs[i].name)
            goto error;

        outputs[i].type = GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD;
        if(output_type == GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE) {
            string_remove_suffix(outputs[i].name, ".monitor");

            if(strcmp(outputs[i].name, "default_output") == 0)
                outputs[i].type = GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT;
            else if(strcmp(outputs[i].name, "default_input") == 0)
                outputs[i].type = GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT;
            else
                outputs[i].type = GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD;
        }
    }

    pw_thread_loop_lock(self->thread_loop);
    self->requested_links[self->num_requested_links].outputs = outputs;
    self->requested_links[self->num_requested_links].num_outputs = num_output_names;
    self->requested_links[self->num_requested_links].input_name = input_name_copy;
    self->requested_links[self->num_requested_links].output_type = output_type;
    self->requested_links[self->num_requested_links].input_type = input_type;
    self->requested_links[self->num_requested_links].inverted = inverted;
    ++self->num_requested_links;
    gsr_pipewire_audio_create_link(self, &self->requested_links[self->num_requested_links - 1]);
    // TODO: Remove these?
    gsr_pipewire_audio_create_link_for_default_devices(self, &self->requested_links[self->num_requested_links - 1], GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT);
    gsr_pipewire_audio_create_link_for_default_devices(self, &self->requested_links[self->num_requested_links - 1], GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT);
    pw_thread_loop_unlock(self->thread_loop);

    return true;

    error:
    free(input_name_copy);
    for(int i = 0; i < num_output_names; ++i) {
        free(outputs[i].name);
    }
    free(outputs);
    return false;
}

bool gsr_pipewire_audio_add_link_from_apps_to_stream(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *stream_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, app_names, num_app_names, stream_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM, false);
}

bool gsr_pipewire_audio_add_link_from_apps_to_stream_inverted(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *stream_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, app_names, num_app_names, stream_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM, true);
}

bool gsr_pipewire_audio_add_link_from_apps_to_sink(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *sink_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, app_names, num_app_names, sink_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK, false);
}

bool gsr_pipewire_audio_add_link_from_apps_to_sink_inverted(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *sink_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, app_names, num_app_names, sink_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK, true);
}

bool gsr_pipewire_audio_add_link_from_sources_to_stream(gsr_pipewire_audio *self, const char **source_names, int num_source_names, const char *stream_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, source_names, num_source_names, stream_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM, false);
}

bool gsr_pipewire_audio_add_link_from_sources_to_sink(gsr_pipewire_audio *self, const char **source_names, int num_source_names, const char *sink_name_input) {
    return gsr_pipewire_audio_add_links_to_output(self, source_names, num_source_names, sink_name_input, GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE, GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK, false);
}

void gsr_pipewire_audio_for_each_app(gsr_pipewire_audio *self, gsr_pipewire_audio_app_query_callback callback, void *userdata) {
    pw_thread_loop_lock(self->thread_loop);
    for(int i = 0; i < (int)self->num_stream_nodes; ++i) {
        const gsr_pipewire_audio_node *node = &self->stream_nodes[i];
        if(!gsr_pipewire_audio_node_is_application(node))
            continue;

        bool duplicate_app = false;
        for(int j = i - 1; j >= 0; --j) {
            const gsr_pipewire_audio_node *prev_node = &self->stream_nodes[j];
            if(prev_node->type != GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT)
                continue;

            if(strcasecmp(node->name, prev_node->name) == 0) {
                duplicate_app = true;
                break;
            }
        }

        if(duplicate_app)
            continue;

        if(!callback(node->name, userdata))
            break;
    }
    pw_thread_loop_unlock(self->thread_loop);
}
