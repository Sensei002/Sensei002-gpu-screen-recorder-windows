#include "../../include/DesktopEnvironment/DesktopEnvironmentWlroots.hpp"

#include <stdio.h>
#include <string.h>
#include <vector>

#include <wayland-client.h>
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace gsr {
    namespace {
        struct ToplevelState {
            DesktopEnvironmentWlroots::Impl *owner = nullptr;
            struct zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
            std::string title;
            std::string app_id;
            std::string pending_title;
            std::string pending_app_id;
            bool pending_activated = false;
            bool activated = false;
            bool closed = false;
        };
    }

    struct DesktopEnvironmentWlroots::Impl {
        struct wl_display *display = nullptr;
        struct wl_registry *registry = nullptr;
        struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = nullptr;
        std::vector<std::unique_ptr<ToplevelState>> toplevels;
        std::string focused_window_title;

        ~Impl() { teardown(); }
        void teardown();
        void recompute_focused_title();
    };

    namespace {
        bool app_id_is_ignored(const std::string &app_id) {
            return app_id == "gsr-ui" || app_id == "gsr-notify";
        }

        void toplevel_handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1*, const char *title) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_title = title ? title : "";
        }

        void toplevel_handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1*, const char *app_id) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_app_id = app_id ? app_id : "";
        }

        void toplevel_handle_output_enter(void*, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output*) {}
        void toplevel_handle_output_leave(void*, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output*) {}

        void toplevel_handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1*, struct wl_array *states) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_activated = false;
            uint32_t *p = (uint32_t*)states->data;
            const size_t n = states->size / sizeof(uint32_t);
            for(size_t i = 0; i < n; ++i) {
                if(p[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
                    top->pending_activated = true;
                    break;
                }
            }
        }

        void toplevel_handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1*);

        void toplevel_handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
            ToplevelState *top = (ToplevelState*)data;
            top->closed = true;
            top->activated = false;
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            top->handle = nullptr;
        }

        void toplevel_handle_parent(void*, struct zwlr_foreign_toplevel_handle_v1*, struct zwlr_foreign_toplevel_handle_v1*) {}

        const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
            toplevel_handle_title, toplevel_handle_app_id,
            toplevel_handle_output_enter, toplevel_handle_output_leave,
            toplevel_handle_state, toplevel_handle_done,
            toplevel_handle_closed, toplevel_handle_parent,
        };

        void toplevel_handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1*) {
            ToplevelState *top = (ToplevelState*)data;
            top->title = top->pending_title;
            top->app_id = top->pending_app_id;
            top->activated = top->pending_activated;
            if(top->owner)
                top->owner->recompute_focused_title();
        }

        void toplevel_manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1*,
            struct zwlr_foreign_toplevel_handle_v1 *handle)
        {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            auto top = std::make_unique<ToplevelState>();
            top->owner = impl;
            top->handle = handle;

            zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_listener, top.get());
            impl->toplevels.push_back(std::move(top));
        }

        void toplevel_manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1*) {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            impl->toplevel_manager = nullptr;
        }

        const struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_listener = {
            toplevel_manager_toplevel, toplevel_manager_finished,
        };

        void registry_global(void *data, struct wl_registry *registry, uint32_t name,
            const char *interface, uint32_t version)
        {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            if(strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
                impl->toplevel_manager = (struct zwlr_foreign_toplevel_manager_v1*)wl_registry_bind(
                    registry, name, &zwlr_foreign_toplevel_manager_v1_interface, version >= 3 ? 3 : version);
                zwlr_foreign_toplevel_manager_v1_add_listener(impl->toplevel_manager, &toplevel_manager_listener, impl);
            }
        }

        void registry_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener registry_listener = {
            registry_global, registry_global_remove,
        };
    }

    void DesktopEnvironmentWlroots::Impl::recompute_focused_title() {
        if(toplevels.empty()) {
            focused_window_title.clear();
            return;
        }

        for(const auto &t : toplevels) {
            if(t->closed || !t->activated)
                continue;
            if(app_id_is_ignored(t->app_id))
                continue;
            focused_window_title = t->title;
            return;
        }
    }

    void DesktopEnvironmentWlroots::Impl::teardown() {
        for(auto &t : toplevels) {
            if(t->handle) {
                zwlr_foreign_toplevel_handle_v1_destroy(t->handle);
                t->handle = nullptr;
            }
        }
        toplevels.clear();

        if(toplevel_manager) {
            zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
            zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
            toplevel_manager = nullptr;
        }

        if(registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        display = nullptr;
        focused_window_title.clear();
    }

    namespace {
        struct ToplevelManagerProbe { bool found = false; };

        void toplevel_manager_probe_global(void *data, struct wl_registry*, uint32_t,
            const char *interface, uint32_t)
        {
            ToplevelManagerProbe *p = (ToplevelManagerProbe*)data;
            if(strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
                p->found = true;
        }

        void toplevel_manager_probe_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener toplevel_manager_probe_registry_listener = {
            toplevel_manager_probe_global, toplevel_manager_probe_global_remove,
        };
    }

    bool DesktopEnvironmentWlroots::is_supported(struct wl_display *dpy) {
        if(!dpy)
            return false;

        ToplevelManagerProbe probe;
        struct wl_registry *registry = wl_display_get_registry(dpy);
        if(!registry)
            return false;

        wl_registry_add_listener(registry, &toplevel_manager_probe_registry_listener, &probe);
        wl_display_roundtrip(dpy);
        wl_registry_destroy(registry);
        return probe.found;
    }

    DesktopEnvironmentWlroots::DesktopEnvironmentWlroots(struct wl_display *dpy) : impl(std::make_unique<Impl>()) {
        impl->display = dpy;
    }

    DesktopEnvironmentWlroots::~DesktopEnvironmentWlroots() = default;

    bool DesktopEnvironmentWlroots::start() {
        if(!impl->display) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: no wayland display\n");
            return false;
        }
        if(impl->registry) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: already started\n");
            return false;
        }

        impl->registry = wl_display_get_registry(impl->display);
        wl_registry_add_listener(impl->registry, &registry_listener, impl.get());
        wl_display_roundtrip(impl->display);

        if(!impl->toplevel_manager) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: wlr-foreign-toplevel-management-v1 not available\n");
            return false;
        }

        wl_display_roundtrip(impl->display);
        impl->recompute_focused_title();
        return true;
    }

    void DesktopEnvironmentWlroots::update() {
    }

    std::string DesktopEnvironmentWlroots::get_focused_window_title() {
        return impl->focused_window_title;
    }
}
