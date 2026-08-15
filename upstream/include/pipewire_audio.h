#ifndef GSR_PIPEWIRE_AUDIO_H
#define GSR_PIPEWIRE_AUDIO_H

#include <pipewire/thread-loop.h>
#include <pipewire/context.h>
#include <pipewire/core.h>
#include <spa/utils/hook.h>

#include <stdbool.h>

typedef enum {
    GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_OUTPUT, /* Application audio */
    GSR_PIPEWIRE_AUDIO_NODE_TYPE_STREAM_INPUT,  /* Audio recording input */
    GSR_PIPEWIRE_AUDIO_NODE_TYPE_SINK_OR_SOURCE /* Audio output or input device or combined (virtual) sink */
} gsr_pipewire_audio_node_type;

typedef struct {
    uint32_t id;
    char *name;
    gsr_pipewire_audio_node_type type;
    bool is_virtual;
    bool is_application; /* The node has application properties (application.id, application.name or application.process.binary), so it's an application and not a device or a module stream */
} gsr_pipewire_audio_node;

typedef struct gsr_pipewire_audio_node_binding gsr_pipewire_audio_node_binding;

typedef enum {
    GSR_PIPEWIRE_AUDIO_PORT_DIRECTION_INPUT,
    GSR_PIPEWIRE_AUDIO_PORT_DIRECTION_OUTPUT
} gsr_pipewire_audio_port_direction;

typedef struct {
    uint32_t id;
    uint32_t node_id;
    gsr_pipewire_audio_port_direction direction;
    char *name;
} gsr_pipewire_audio_port;

typedef struct {
    uint32_t id;
    uint32_t output_node_id;
    uint32_t input_node_id;
} gsr_pipewire_audio_link;

typedef enum {
    GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_STREAM, /* Application */
    GSR_PIPEWIRE_AUDIO_LINK_INPUT_TYPE_SINK    /* Combined (virtual) sink */
} gsr_pipewire_audio_link_input_type;

typedef enum {
    GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_STANDARD,
    GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_OUTPUT,
    GSR_PIPEWIRE_AUDIO_REQUESTED_TYPE_DEFAULT_INPUT
} gsr_pipewire_audio_requested_type;

typedef struct {
    char *name;
    gsr_pipewire_audio_requested_type type;
} gsr_pipewire_audio_requested_output;

typedef struct {
    gsr_pipewire_audio_requested_output *outputs;
    int num_outputs;
    char *input_name;
    bool inverted;
    gsr_pipewire_audio_node_type output_type;
    gsr_pipewire_audio_link_input_type input_type;
} gsr_pipewire_audio_requested_link;

typedef struct {
    struct pw_thread_loop *thread_loop;
    struct pw_context *context;
    struct pw_core *core;
    struct spa_hook core_listener;
    struct pw_registry *registry;
    struct spa_hook registry_listener;
    int server_version_sync;

    struct pw_proxy *metadata_proxy;
    struct spa_hook metadata_listener;
    struct spa_hook metadata_proxy_listener;
    char default_output_device_name[128];
    char default_input_device_name[128];

    gsr_pipewire_audio_node *stream_nodes;
    size_t num_stream_nodes;
    size_t stream_nodes_capacity_items;

    gsr_pipewire_audio_node_binding **node_bindings;
    size_t num_node_bindings;
    size_t node_bindings_capacity_items;

    gsr_pipewire_audio_port *ports;
    size_t num_ports;
    size_t ports_capacity_items;

    gsr_pipewire_audio_link *links;
    size_t num_links;
    size_t links_capacity_items;

    gsr_pipewire_audio_requested_link *requested_links;
    size_t num_requested_links;
    size_t requested_links_capacity_items;

    bool running;
} gsr_pipewire_audio;

bool gsr_pipewire_audio_init(gsr_pipewire_audio *self);
void gsr_pipewire_audio_deinit(gsr_pipewire_audio *self);

/*
    This function links audio source outputs from applications that match the name |app_names| to the input
    that matches the name |stream_name_input|.
    If an application or a new application starts outputting audio after this function is called and the app name matches
    then it will automatically link the audio sources.
    |app_names| and |stream_name_input| are case-insensitive matches.
*/
bool gsr_pipewire_audio_add_link_from_apps_to_stream(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *stream_name_input);
/*
    This function links audio source outputs from all applications except the ones that match the name |app_names| to the input
    that matches the name |stream_name_input|.
    If an application or a new application starts outputting audio after this function is called and the app name doesn't match
    then it will automatically link the audio sources.
    |app_names| and |stream_name_input| are case-insensitive matches.
*/
bool gsr_pipewire_audio_add_link_from_apps_to_stream_inverted(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *stream_name_input);

/*
    This function links audio source outputs from applications that match the name |app_names| to the input
    that matches the name |sink_name_input|.
    If an application or a new application starts outputting audio after this function is called and the app name matches
    then it will automatically link the audio sources.
    |app_names| and |sink_name_input| are case-insensitive matches.
*/
bool gsr_pipewire_audio_add_link_from_apps_to_sink(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *sink_name_input);
/*
    This function links audio source outputs from all applications except the ones that match the name |app_names| to the input
    that matches the name |sink_name_input|.
    If an application or a new application starts outputting audio after this function is called and the app name doesn't match
    then it will automatically link the audio sources.
    |app_names| and |sink_name_input| are case-insensitive matches.
*/
bool gsr_pipewire_audio_add_link_from_apps_to_sink_inverted(gsr_pipewire_audio *self, const char **app_names, int num_app_names, const char *sink_name_input);

/*
    This function links audio source outputs from devices that match the name |source_names| to the input
    that matches the name |stream_name_input|.
    If a device or a new device starts outputting audio after this function is called and the device name matches
    then it will automatically link the audio sources.
    |source_names| and |stream_name_input| are case-insensitive matches.
    |source_names| can include "default_output" or "default_input" to use the default output/input
    and it will automatically switch when the default output/input is changed in system audio settings.
*/
bool gsr_pipewire_audio_add_link_from_sources_to_stream(gsr_pipewire_audio *self, const char **source_names, int num_source_names, const char *stream_name_input);

/*
    This function links audio source outputs from devices that match the name |source_names| to the input
    that matches the name |sink_name_input|.
    If a device or a new device starts outputting audio after this function is called and the device name matches
    then it will automatically link the audio sources.
    |source_names| and |sink_name_input| are case-insensitive matches.
    |source_names| can include "default_output" or "default_input" to use the default output/input
    and it will automatically switch when the default output/input is changed in system audio settings.
*/
bool gsr_pipewire_audio_add_link_from_sources_to_sink(gsr_pipewire_audio *self, const char **source_names, int num_source_names, const char *sink_name_input);

/* Return true to continue */
typedef bool (*gsr_pipewire_audio_app_query_callback)(const char *app_name, void *userdata);
void gsr_pipewire_audio_for_each_app(gsr_pipewire_audio *self, gsr_pipewire_audio_app_query_callback callback, void *userdata);

#endif /* GSR_PIPEWIRE_AUDIO_H */
