#include "../../include/RegionSelector/RegionSelectorWayland.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <chrono>

#include <wayland-client.h>
#define namespace _namespace
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#undef namespace

#include <mglpp/system/Rect.hpp>

namespace gsr {
    static const int cursor_window_size = 32;
    static const int cursor_thickness = 5;
    static const int region_border_size = 2;

    namespace {
        struct OutputBuffer {
            void *data = nullptr;
            size_t size = 0;
            struct wl_shm_pool *shm_pool = nullptr;
            struct wl_buffer *wl_buf = nullptr;
            bool busy = false;
        };

        static constexpr int output_num_buffers = 2;

        struct OutputState {
            RegionSelectorWayland::Impl *owner = nullptr;
            uint32_t wl_name = 0;
            struct wl_output *output = nullptr;
            struct zxdg_output_v1 *xdg_output = nullptr;
            struct wl_surface *surface = nullptr;
            struct zwlr_layer_surface_v1 *layer_surface = nullptr;

            mgl::vec2i logical_pos;
            mgl::vec2i logical_size;
            int32_t scale = 1;
            int32_t transform = 0;
            std::string name;

            int32_t buffer_width = 0;
            int32_t buffer_height = 0;
            int32_t buffer_stride = 0;
            OutputBuffer buffers[output_num_buffers];
            int next_buffer_idx = 0;

            bool configured = false;
        };

        struct WlRegionState {
            RegionSelectorWayland::Impl *self_impl = nullptr;
            struct wl_display *display = nullptr;
            struct wl_registry *registry = nullptr;
            struct wl_compositor *compositor = nullptr;
            struct wl_shm *shm = nullptr;
            struct wl_seat *seat = nullptr;
            struct zwlr_layer_shell_v1 *layer_shell = nullptr;
            struct zxdg_output_manager_v1 *xdg_output_manager = nullptr;

            struct wl_pointer *pointer = nullptr;

            std::vector<std::unique_ptr<OutputState>> outputs;

            struct wl_surface *cursor_surface = nullptr;
            struct wl_buffer *cursor_buffer = nullptr;
            struct wl_shm_pool *cursor_shm_pool = nullptr;
            void *cursor_buffer_data = nullptr;
            size_t cursor_buffer_size = 0;

            mgl::vec2i cursor_pos;
            OutputState *pointer_focus_output = nullptr;
            uint32_t pointer_enter_serial = 0;
            bool pointer_inside = false;

            Region region;
            bool selecting_region = false;

            mgl::Color border_color{255, 0, 0, 255};
            uint32_t border_color_argb = 0xFFFF0000;

            RegionSelector::SelectionType selection_type = RegionSelector::SelectionType::NONE;
            bool selected = false;
            bool canceled = false;
            bool failed = false;
            bool started = false;
            bool dirty = true;
        };

        void output_geometry(void*, struct wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t);
        void output_mode(void*, struct wl_output*, uint32_t, int32_t, int32_t, int32_t);
        void output_done(void*, struct wl_output*);
        void output_scale(void*, struct wl_output*, int32_t);
        void output_name(void*, struct wl_output*, const char*);
        void output_description(void*, struct wl_output*, const char*);

        const struct wl_output_listener output_listener = {
            output_geometry, output_mode, output_done, output_scale,
            output_name, output_description,
        };

        void xdg_output_logical_position(void*, struct zxdg_output_v1*, int32_t, int32_t);
        void xdg_output_logical_size(void*, struct zxdg_output_v1*, int32_t, int32_t);
        void xdg_output_done(void*, struct zxdg_output_v1*);
        void xdg_output_name(void*, struct zxdg_output_v1*, const char*);
        void xdg_output_description(void*, struct zxdg_output_v1*, const char*);

        const struct zxdg_output_v1_listener xdg_output_listener = {
            xdg_output_logical_position, xdg_output_logical_size, xdg_output_done,
            xdg_output_name, xdg_output_description,
        };

        void layer_surface_configure(void*, struct zwlr_layer_surface_v1*, uint32_t, uint32_t, uint32_t);
        void layer_surface_closed(void*, struct zwlr_layer_surface_v1*);

        const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
            layer_surface_configure, layer_surface_closed,
        };

        void registry_global(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
        void registry_global_remove(void*, struct wl_registry*, uint32_t);

        const struct wl_registry_listener registry_listener = {
            registry_global, registry_global_remove,
        };

        void seat_capabilities(void*, struct wl_seat*, uint32_t);
        void seat_name(void*, struct wl_seat*, const char*);

        const struct wl_seat_listener seat_listener = {
            seat_capabilities, seat_name,
        };

        void pointer_enter(void*, struct wl_pointer*, uint32_t, struct wl_surface*, wl_fixed_t, wl_fixed_t);
        void pointer_leave(void*, struct wl_pointer*, uint32_t, struct wl_surface*);
        void pointer_motion(void*, struct wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t);
        void pointer_button(void*, struct wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t);
        void pointer_axis(void*, struct wl_pointer*, uint32_t, uint32_t, wl_fixed_t);
        void pointer_frame(void*, struct wl_pointer*);
        void pointer_axis_source(void*, struct wl_pointer*, uint32_t);
        void pointer_axis_stop(void*, struct wl_pointer*, uint32_t, uint32_t);
        void pointer_axis_discrete(void*, struct wl_pointer*, uint32_t, int32_t);
        void pointer_axis_value120(void*, struct wl_pointer*, uint32_t, int32_t);
        void pointer_axis_relative_direction(void*, struct wl_pointer*, uint32_t, uint32_t);

        const struct wl_pointer_listener pointer_listener = {
            pointer_enter, pointer_leave, pointer_motion, pointer_button,
            pointer_axis, pointer_frame, pointer_axis_source, pointer_axis_stop,
            pointer_axis_discrete, pointer_axis_value120, pointer_axis_relative_direction,
        };

        void output_buffer_release(void *data, struct wl_buffer*) {
            ((OutputBuffer*)data)->busy = false;
        }

        const struct wl_buffer_listener output_buffer_listener = {
            output_buffer_release,
        };
    }

    struct RegionSelectorWayland::Impl {
        WlRegionState s;

        ~Impl() { teardown(); }

        bool init();
        void teardown();
        bool create_output_surfaces();
        void destroy_output_surfaces();
        bool create_cursor_surface();
        void destroy_cursor_surface();
        bool ensure_output_buffer(OutputState *out, int32_t width, int32_t height);
        void render_all();
        void render_output(OutputState *out);
    };

    namespace {
        static uint32_t mgl_color_to_argb(mgl::Color c) {
            // ARGB8888 with premultiplied alpha (wl_shm WL_SHM_FORMAT_ARGB8888 expects premultiplied).
            const uint32_t a = c.a;
            const uint32_t r = (uint32_t)c.r * a / 0xFF;
            const uint32_t g = (uint32_t)c.g * a / 0xFF;
            const uint32_t b = (uint32_t)c.b * a / 0xFF;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }

        void output_geometry(void *data, struct wl_output*, int32_t, int32_t,
            int32_t, int32_t, int32_t, const char*, const char*, int32_t transform)
        {
            OutputState *out = (OutputState*)data;
            out->transform = transform;
        }

        void output_mode(void *data, struct wl_output*, uint32_t, int32_t width, int32_t height, int32_t) {
            OutputState *out = (OutputState*)data;
            if(out->logical_size.x == 0 && out->logical_size.y == 0) {
                out->logical_size.x = width;
                out->logical_size.y = height;
            }
        }

        void output_done(void*, struct wl_output*) {}

        void output_scale(void *data, struct wl_output*, int32_t factor) {
            OutputState *out = (OutputState*)data;
            out->scale = factor > 0 ? factor : 1;
        }

        void output_name(void *data, struct wl_output*, const char *name) {
            OutputState *out = (OutputState*)data;
            out->name = name ? name : "";
        }

        void output_description(void*, struct wl_output*, const char*) {}

        void xdg_output_logical_position(void *data, struct zxdg_output_v1*, int32_t x, int32_t y) {
            OutputState *out = (OutputState*)data;
            out->logical_pos = {x, y};
        }

        void xdg_output_logical_size(void *data, struct zxdg_output_v1*, int32_t w, int32_t h) {
            OutputState *out = (OutputState*)data;
            out->logical_size = {w, h};
        }

        void xdg_output_done(void*, struct zxdg_output_v1*) {}
        void xdg_output_name(void*, struct zxdg_output_v1*, const char*) {}
        void xdg_output_description(void*, struct zxdg_output_v1*, const char*) {}

        void registry_global(void *data, struct wl_registry *registry, uint32_t name,
            const char *interface, uint32_t version)
        {
            WlRegionState *s = (WlRegionState*)data;
            if(strcmp(interface, wl_compositor_interface.name) == 0) {
                s->compositor = (struct wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, version >= 4 ? 4 : version);
            } else if(strcmp(interface, wl_shm_interface.name) == 0) {
                s->shm = (struct wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
            } else if(strcmp(interface, wl_seat_interface.name) == 0) {
                s->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, version >= 5 ? 5 : version);
                wl_seat_add_listener(s->seat, &seat_listener, s);
            } else if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
                s->layer_shell = (struct zwlr_layer_shell_v1*)wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version >= 4 ? 4 : version);
            } else if(strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
                s->xdg_output_manager = (struct zxdg_output_manager_v1*)wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, version >= 2 ? 2 : version);
            } else if(strcmp(interface, wl_output_interface.name) == 0) {
                auto out = std::make_unique<OutputState>();
                out->owner = s->self_impl;
                out->wl_name = name;
                out->output = (struct wl_output*)wl_registry_bind(registry, name, &wl_output_interface, version >= 4 ? 4 : version);
                wl_output_add_listener(out->output, &output_listener, out.get());
                s->outputs.push_back(std::move(out));
            }
        }

        void registry_global_remove(void*, struct wl_registry*, uint32_t) {}

        void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
            WlRegionState *s = (WlRegionState*)data;
            const bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;

            if(has_pointer && !s->pointer) {
                s->pointer = wl_seat_get_pointer(seat);
                wl_pointer_add_listener(s->pointer, &pointer_listener, s);
            } else if(!has_pointer && s->pointer) {
                wl_pointer_destroy(s->pointer);
                s->pointer = nullptr;
            }
        }

        void seat_name(void*, struct wl_seat*, const char*) {}

        void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surf,
            uint32_t serial, uint32_t width, uint32_t height)
        {
            OutputState *out = (OutputState*)data;
            zwlr_layer_surface_v1_ack_configure(surf, serial);

            int32_t buf_w = (int32_t)width * out->scale;
            int32_t buf_h = (int32_t)height * out->scale;
            if(buf_w <= 0) buf_w = out->logical_size.x * out->scale;
            if(buf_h <= 0) buf_h = out->logical_size.y * out->scale;

            out->buffer_width = buf_w;
            out->buffer_height = buf_h;
            out->configured = true;

            if(out->owner)
                out->owner->render_output(out);
        }

        void layer_surface_closed(void *data, struct zwlr_layer_surface_v1*) {
            OutputState *out = (OutputState*)data;
            out->configured = false;
        }

        void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
            struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
        {
            WlRegionState *s = (WlRegionState*)data;
            s->pointer_enter_serial = serial;
            const int hotspot = cursor_window_size / 2;
            wl_pointer_set_cursor(pointer, serial, s->cursor_surface, hotspot, hotspot);

            OutputState *out = nullptr;
            for(auto &o : s->outputs) {
                if(o->surface == surface) { out = o.get(); break; }
            }
            s->pointer_focus_output = out;
            s->pointer_inside = (out != nullptr);
            if(out) {
                s->cursor_pos.x = out->logical_pos.x + (int)wl_fixed_to_double(sx);
                s->cursor_pos.y = out->logical_pos.y + (int)wl_fixed_to_double(sy);
            }
            s->dirty = true;
        }

        void pointer_leave(void *data, struct wl_pointer*, uint32_t, struct wl_surface*) {
            WlRegionState *s = (WlRegionState*)data;
            s->pointer_focus_output = nullptr;
            s->pointer_inside = false;
            s->dirty = true;
        }

        void pointer_motion(void *data, struct wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
            WlRegionState *s = (WlRegionState*)data;
            if(!s->pointer_focus_output)
                return;
            s->cursor_pos.x = s->pointer_focus_output->logical_pos.x + (int)wl_fixed_to_double(sx);
            s->cursor_pos.y = s->pointer_focus_output->logical_pos.y + (int)wl_fixed_to_double(sy);
            if(s->selecting_region) {
                s->region.size.x = s->cursor_pos.x - s->region.pos.x;
                s->region.size.y = s->cursor_pos.y - s->region.pos.y;
            }
            s->dirty = true;
        }

        void pointer_button(void *data, struct wl_pointer*, uint32_t, uint32_t,
            uint32_t button, uint32_t state)
        {
            WlRegionState *s = (WlRegionState*)data;
            if(button != BTN_LEFT)
                return;

            if(state == WL_POINTER_BUTTON_STATE_PRESSED) {
                if(s->selection_type == RegionSelector::SelectionType::REGION) {
                    s->region.pos = s->cursor_pos;
                    s->region.size = {0, 0};
                    s->selecting_region = true;
                    s->dirty = true;
                }
            } else if(state == WL_POINTER_BUTTON_STATE_RELEASED) {
                if(s->selection_type == RegionSelector::SelectionType::REGION && !s->selecting_region)
                    return;

                if(s->region.size.x < 0) {
                    s->region.pos.x += s->region.size.x;
                    s->region.size.x = -s->region.size.x;
                }
                if(s->region.size.y < 0) {
                    s->region.pos.y += s->region.size.y;
                    s->region.size.y = -s->region.size.y;
                }
                if(s->region.size.x > 0) s->region.size.x += 1;
                if(s->region.size.y > 0) s->region.size.y += 1;

                s->selecting_region = false;
                s->selected = true;
            }
        }

        void pointer_axis(void*, struct wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
        void pointer_frame(void*, struct wl_pointer*) {}
        void pointer_axis_source(void*, struct wl_pointer*, uint32_t) {}
        void pointer_axis_stop(void*, struct wl_pointer*, uint32_t, uint32_t) {}
        void pointer_axis_discrete(void*, struct wl_pointer*, uint32_t, int32_t) {}
        void pointer_axis_value120(void*, struct wl_pointer*, uint32_t, int32_t) {}
        void pointer_axis_relative_direction(void*, struct wl_pointer*, uint32_t, uint32_t) {}
    }

    namespace {
        struct FrameWait { int pending = 0; };

        static void frame_wait_done(void *data, struct wl_callback *cb, uint32_t) {
            ((FrameWait*)data)->pending--;
            wl_callback_destroy(cb);
        }

        static const struct wl_callback_listener frame_wait_listener = {
            frame_wait_done,
        };

        static int allocate_shm_fd(size_t size) {
            int fd = memfd_create("gsr-region-selector", MFD_CLOEXEC);
            if(fd < 0)
                return -1;
            if(ftruncate(fd, (off_t)size) < 0) {
                close(fd);
                return -1;
            }
            return fd;
        }

        static void fill_rect_clipped(uint8_t *dst, int width, int height, int stride,
            int rx, int ry, int rw, int rh, uint32_t argb)
        {
            if(rw <= 0 || rh <= 0) return;
            int x0 = rx, y0 = ry;
            int x1 = rx + rw, y1 = ry + rh;
            if(x0 < 0) x0 = 0;
            if(y0 < 0) y0 = 0;
            if(x1 > width) x1 = width;
            if(y1 > height) y1 = height;
            if(x0 >= x1 || y0 >= y1) return;
            for(int y = y0; y < y1; ++y) {
                uint32_t *row = (uint32_t*)(dst + (size_t)y * stride);
                for(int x = x0; x < x1; ++x)
                    row[x] = argb;
            }
        }

        static void clear_buffer(uint8_t *dst, int height, int stride) {
            memset(dst, 0, (size_t)stride * (size_t)height);
        }

        static void draw_rect_border(uint8_t *dst, int width, int height, int stride,
            int rx, int ry, int rw, int rh, int thickness, uint32_t argb)
        {
            if(rw < 0) { rx += rw; rw = -rw; }
            if(rh < 0) { ry += rh; rh = -rh; }
            if(rw == 0 || rh == 0) return;
            // Top
            fill_rect_clipped(dst, width, height, stride, rx, ry, rw, thickness, argb);
            // Bottom
            fill_rect_clipped(dst, width, height, stride, rx, ry + rh - thickness, rw, thickness, argb);
            // Left
            fill_rect_clipped(dst, width, height, stride, rx, ry + thickness, thickness, rh - thickness*2, argb);
            // Right
            fill_rect_clipped(dst, width, height, stride, rx + rw - thickness, ry + thickness, thickness, rh - thickness*2, argb);
        }

        static zwlr_layer_surface_v1_keyboard_interactivity compositor_to_keyboard_interactivity() {
            const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
            if(xdg_current_desktop && strstr(xdg_current_desktop, "Hyprland"))
                return ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
            else
                return ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
        }
    }

    bool RegionSelectorWayland::Impl::init() {
        if(!s.display) {
            fprintf(stderr, "Error: RegionSelectorWayland: no wayland display\n");
            return false;
        }
        s.registry = wl_display_get_registry(s.display);
        wl_registry_add_listener(s.registry, &registry_listener, &s);
        wl_display_roundtrip(s.display); // globals
        wl_display_roundtrip(s.display); // output / seat events

        if(!s.compositor || !s.shm || !s.layer_shell || !s.seat) {
            fprintf(stderr, "Error: RegionSelectorWayland: missing required wayland globals (compositor=%p, shm=%p, layer_shell=%p, seat=%p)\n",
                (void*)s.compositor, (void*)s.shm, (void*)s.layer_shell, (void*)s.seat);
            return false;
        }

        if(s.xdg_output_manager) {
            for(auto &out : s.outputs) {
                out->xdg_output = zxdg_output_manager_v1_get_xdg_output(s.xdg_output_manager, out->output);
                zxdg_output_v1_add_listener(out->xdg_output, &xdg_output_listener, out.get());
            }
            wl_display_roundtrip(s.display);
        }

        return true;
    }

    bool RegionSelectorWayland::Impl::create_output_surfaces() {
        for(auto &out : s.outputs) {
            if(out->logical_size.x <= 0 || out->logical_size.y <= 0) {
                fprintf(stderr, "Warning: RegionSelectorWayland: skipping output '%s' with invalid size\n", out->name.c_str());
                continue;
            }

            out->surface = wl_compositor_create_surface(s.compositor);
            if(!out->surface) {
                fprintf(stderr, "Error: RegionSelectorWayland: failed to create surface\n");
                return false;
            }

            out->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
                s.layer_shell, out->surface, out->output,
                ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "gsr-region-selector");
            if(!out->layer_surface) {
                fprintf(stderr, "Error: RegionSelectorWayland: failed to create layer surface\n");
                return false;
            }

            zwlr_layer_surface_v1_add_listener(out->layer_surface, &layer_surface_listener, out.get());
            zwlr_layer_surface_v1_set_anchor(out->layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_exclusive_zone(out->layer_surface, -1);
            zwlr_layer_surface_v1_set_keyboard_interactivity(out->layer_surface, compositor_to_keyboard_interactivity());
            zwlr_layer_surface_v1_set_size(out->layer_surface, (uint32_t)out->logical_size.x, (uint32_t)out->logical_size.y);

            wl_surface_set_buffer_scale(out->surface, out->scale);
            wl_surface_commit(out->surface);
        }

        // Wait for all configures.
        wl_display_roundtrip(s.display);
        return true;
    }

    void RegionSelectorWayland::Impl::destroy_output_surfaces() {
        FrameWait fw;
        for(auto &out : s.outputs) {
            if(!out->surface)
                continue;

            OutputBuffer *buf = nullptr;
            for(auto &b : out->buffers) {
                if(!b.busy && b.data) { buf = &b; break; }
            }
            if(!buf && out->buffers[0].data)
                buf = &out->buffers[0];
            if(!buf || !buf->data || !buf->size)
                continue;

            memset(buf->data, 0, buf->size);

            struct wl_callback *cb = wl_surface_frame(out->surface);
            wl_callback_add_listener(cb, &frame_wait_listener, &fw);
            fw.pending++;

            buf->busy = true;
            wl_surface_attach(out->surface, buf->wl_buf, 0, 0);
            wl_surface_damage_buffer(out->surface, 0, 0, out->buffer_width, out->buffer_height);
            wl_surface_commit(out->surface);
        }

        if(fw.pending > 0 && s.display) {
            wl_display_flush(s.display);

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            while(fw.pending > 0) {
                const auto now = std::chrono::steady_clock::now();
                if(now >= deadline)
                    break;
                const int remaining_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

                while(wl_display_prepare_read(s.display) != 0) {
                    if(wl_display_dispatch_pending(s.display) < 0)
                        goto done_waiting;
                }
                wl_display_flush(s.display);

                struct pollfd pfd = { wl_display_get_fd(s.display), POLLIN, 0 };
                const int ret = poll(&pfd, 1, remaining_ms);
                if(ret > 0 && (pfd.revents & POLLIN)) {
                    if(wl_display_read_events(s.display) < 0)
                        break;
                    if(wl_display_dispatch_pending(s.display) < 0)
                        break;
                } else {
                    wl_display_cancel_read(s.display);
                    break;
                }
            }
            done_waiting: ;
        }

        for(auto &out : s.outputs) {
            for(auto &buf : out->buffers) {
                if(buf.wl_buf) { wl_buffer_destroy(buf.wl_buf); buf.wl_buf = nullptr; }
                if(buf.shm_pool) { wl_shm_pool_destroy(buf.shm_pool); buf.shm_pool = nullptr; }
                if(buf.data && buf.size) {
                    munmap(buf.data, buf.size);
                    buf.data = nullptr;
                    buf.size = 0;
                }
                buf.busy = false;
            }
            if(out->layer_surface) { zwlr_layer_surface_v1_destroy(out->layer_surface); out->layer_surface = nullptr; }
            if(out->surface) { wl_surface_destroy(out->surface); out->surface = nullptr; }
        }
    }

    bool RegionSelectorWayland::Impl::create_cursor_surface() {
        const int size = cursor_window_size;
        const int stride = size * 4;
        const size_t buffer_size = (size_t)stride * (size_t)size;

        const int fd = allocate_shm_fd(buffer_size);
        if(fd < 0) {
            fprintf(stderr, "Error: RegionSelectorWayland::create_cursor_surface: shm allocation failed: %s\n", strerror(errno));
            return false;
        }
        void *data = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(data == MAP_FAILED) {
            fprintf(stderr, "Error: RegionSelectorWayland::create_cursor_surface: mmap failed: %s\n", strerror(errno));
            close(fd);
            return false;
        }

        s.cursor_shm_pool = wl_shm_create_pool(s.shm, fd, (int32_t)buffer_size);
        s.cursor_buffer = wl_shm_pool_create_buffer(s.cursor_shm_pool, 0, size, size, stride, WL_SHM_FORMAT_ARGB8888);
        close(fd);

        s.cursor_buffer_data = data;
        s.cursor_buffer_size = buffer_size;

        // Draw the crosshair into the cursor buffer
        uint8_t *dst = (uint8_t*)data;
        memset(dst, 0, buffer_size);
        const int t = cursor_thickness;
        // Vertical bar
        fill_rect_clipped(dst, size, size, stride, size/2 - t/2, 0, t, size, s.border_color_argb);
        // Horizontal bar
        fill_rect_clipped(dst, size, size, stride, 0, size/2 - t/2, size, t, s.border_color_argb);

        s.cursor_surface = wl_compositor_create_surface(s.compositor);
        if(!s.cursor_surface) {
            fprintf(stderr, "Error: RegionSelectorWayland::create_cursor_surface: failed to create cursor wl_surface\n");
            return false;
        }
        wl_surface_attach(s.cursor_surface, s.cursor_buffer, 0, 0);
        wl_surface_damage_buffer(s.cursor_surface, 0, 0, size, size);
        wl_surface_commit(s.cursor_surface);
        return true;
    }

    void RegionSelectorWayland::Impl::destroy_cursor_surface() {
        if(s.cursor_buffer) { wl_buffer_destroy(s.cursor_buffer); s.cursor_buffer = nullptr; }
        if(s.cursor_shm_pool) { wl_shm_pool_destroy(s.cursor_shm_pool); s.cursor_shm_pool = nullptr; }
        if(s.cursor_buffer_data && s.cursor_buffer_size) {
            munmap(s.cursor_buffer_data, s.cursor_buffer_size);
            s.cursor_buffer_data = nullptr;
            s.cursor_buffer_size = 0;
        }
        if(s.cursor_surface) { wl_surface_destroy(s.cursor_surface); s.cursor_surface = nullptr; }
    }

    bool RegionSelectorWayland::Impl::ensure_output_buffer(OutputState *out, int32_t width, int32_t height) {
        const int32_t stride = width * 4;
        const size_t needed = (size_t)stride * (size_t)height;
        if(out->buffers[0].data && out->buffers[0].size == needed && out->buffers[0].wl_buf
            && out->buffers[1].data && out->buffers[1].size == needed && out->buffers[1].wl_buf) {
            return true;
        }

        for(auto &buf : out->buffers) {
            if(buf.wl_buf) { wl_buffer_destroy(buf.wl_buf); buf.wl_buf = nullptr; }
            if(buf.shm_pool) { wl_shm_pool_destroy(buf.shm_pool); buf.shm_pool = nullptr; }
            if(buf.data && buf.size) {
                munmap(buf.data, buf.size);
                buf.data = nullptr;
                buf.size = 0;
            }
            buf.busy = false;
        }

        for(auto &buf : out->buffers) {
            const int fd = allocate_shm_fd(needed);
            if(fd < 0) {
                fprintf(stderr, "Error: RegionSelectorWayland: shm allocation failed: %s\n", strerror(errno));
                return false;
            }

            void *data = mmap(nullptr, needed, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if(data == MAP_FAILED) {
                fprintf(stderr, "Error: RegionSelectorWayland: mmap failed: %s\n", strerror(errno));
                close(fd);
                return false;
            }

            buf.shm_pool = wl_shm_create_pool(s.shm, fd, (int32_t)needed);
            buf.wl_buf = wl_shm_pool_create_buffer(buf.shm_pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
            wl_buffer_add_listener(buf.wl_buf, &output_buffer_listener, &buf);
            close(fd);

            buf.data = data;
            buf.size = needed;
        }

        out->buffer_stride = stride;
        out->buffer_width = width;
        out->buffer_height = height;
        out->next_buffer_idx = 0;
        return true;
    }

    void RegionSelectorWayland::Impl::render_output(OutputState *out) {
        if(!out->configured)
            return;
        if(!ensure_output_buffer(out, out->buffer_width, out->buffer_height))
            return;

        OutputBuffer *buf = nullptr;
        for(int i = 0; i < output_num_buffers; ++i) {
            const int idx = (out->next_buffer_idx + i) % output_num_buffers;
            if(!out->buffers[idx].busy) {
                buf = &out->buffers[idx];
                out->next_buffer_idx = (idx + 1) % output_num_buffers;
                break;
            }
        }
        if(!buf)
            return;

        uint8_t *dst = (uint8_t*)buf->data;
        const int w = out->buffer_width;
        const int h = out->buffer_height;
        const int stride = out->buffer_stride;

        clear_buffer(dst, h, stride);

        const int scale = out->scale;
        const uint32_t color = s.border_color_argb;

        if(s.selection_type == RegionSelector::SelectionType::REGION) {
            const int thickness = region_border_size * scale;
            if(s.selecting_region) {
                Region r = s.region;
                if(r.size.x < 0) { r.pos.x += r.size.x; r.size.x = -r.size.x; }
                if(r.size.y < 0) { r.pos.y += r.size.y; r.size.y = -r.size.y; }
                const int local_x = (r.pos.x - out->logical_pos.x) * scale;
                const int local_y = (r.pos.y - out->logical_pos.y) * scale;
                const int local_w = r.size.x * scale;
                const int local_h = r.size.y * scale;
                draw_rect_border(dst, w, h, stride, local_x, local_y, local_w, local_h, thickness, color);
            } else if(s.pointer_inside) {
                const mgl::IntRect output_rect(out->logical_pos, out->logical_size);
                if(output_rect.contains(s.cursor_pos))
                    draw_rect_border(dst, w, h, stride, 0, 0, w, h, thickness, color);
            }
        }

        buf->busy = true;
        wl_surface_attach(out->surface, buf->wl_buf, 0, 0);
        wl_surface_damage_buffer(out->surface, 0, 0, w, h);
        wl_surface_commit(out->surface);
    }

    void RegionSelectorWayland::Impl::render_all() {
        for(auto &out : s.outputs)
            render_output(out.get());
    }

    void RegionSelectorWayland::Impl::teardown() {
        destroy_output_surfaces();
        destroy_cursor_surface();

        for(auto &out : s.outputs) {
            if(out->xdg_output) { zxdg_output_v1_destroy(out->xdg_output); out->xdg_output = nullptr; }
            if(out->output) { wl_output_destroy(out->output); out->output = nullptr; }
        }
        s.outputs.clear();

        if(s.pointer) { wl_pointer_destroy(s.pointer); s.pointer = nullptr; }
        if(s.seat) { wl_seat_destroy(s.seat); s.seat = nullptr; }
        if(s.xdg_output_manager) { zxdg_output_manager_v1_destroy(s.xdg_output_manager); s.xdg_output_manager = nullptr; }
        if(s.layer_shell) { zwlr_layer_shell_v1_destroy(s.layer_shell); s.layer_shell = nullptr; }
        if(s.shm) { wl_shm_destroy(s.shm); s.shm = nullptr; }
        if(s.compositor) { wl_compositor_destroy(s.compositor); s.compositor = nullptr; }
        if(s.registry) { wl_registry_destroy(s.registry); s.registry = nullptr; }
        if(s.display)
            wl_display_flush(s.display);

        s.started = false;
        s.selecting_region = false;
        s.pointer_focus_output = nullptr;
        s.pointer_inside = false;
    }

    namespace {
        struct LayerShellProbe { bool found = false; };

        void layer_shell_probe_global(void *data, struct wl_registry*, uint32_t,
            const char *interface, uint32_t)
        {
            LayerShellProbe *p = (LayerShellProbe*)data;
            if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
                p->found = true;
        }

        void layer_shell_probe_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener layer_shell_probe_registry_listener = {
            layer_shell_probe_global, layer_shell_probe_global_remove,
        };
    }

    bool RegionSelectorWayland::is_supported(struct wl_display *dpy) {
        if(!dpy)
            return false;

        LayerShellProbe probe;
        struct wl_registry *registry = wl_display_get_registry(dpy);
        if(!registry)
            return false;
        wl_registry_add_listener(registry, &layer_shell_probe_registry_listener, &probe);
        wl_display_roundtrip(dpy);
        wl_registry_destroy(registry);
        return probe.found;
    }

    RegionSelectorWayland::RegionSelectorWayland(struct wl_display *dpy) : impl(std::make_unique<Impl>()) {
        impl->s.display = dpy;
        impl->s.self_impl = impl.get();
    }
    RegionSelectorWayland::~RegionSelectorWayland() { stop(); }

    bool RegionSelectorWayland::start(SelectionType selection_type, mgl::Color border_color) {
        if(impl->s.started)
            return false;

        struct wl_display *dpy = impl->s.display;
        impl->s = WlRegionState{};
        impl->s.display = dpy;
        impl->s.self_impl = impl.get();
        impl->s.border_color = border_color;
        impl->s.border_color_argb = mgl_color_to_argb(border_color);
        impl->s.selection_type = selection_type;

        if(!impl->init()) {
            impl->s.failed = true;
            impl->teardown();
            return false;
        }

        if(selection_type == SelectionType::WINDOW)
            fprintf(stderr, "Warning: RegionSelectorWayland: WINDOW selection mode is not supported on Wayland; selection will return None\n");

        if(!impl->create_cursor_surface()) {
            impl->s.failed = true;
            impl->teardown();
            return false;
        }

        if(!impl->create_output_surfaces()) {
            impl->s.failed = true;
            impl->teardown();
            return false;
        }

        impl->render_all();
        impl->s.dirty = false;
        wl_display_flush(impl->s.display);

        impl->s.started = true;
        return true;
    }

    void RegionSelectorWayland::stop() {
        if(!impl->s.started && !impl->s.display)
            return;
        impl->teardown();
    }

    void RegionSelectorWayland::cancel() {
        impl->s.canceled = true;
        impl->s.selected = false;
        stop();
    }

    bool RegionSelectorWayland::is_started() const {
        return impl->s.started;
    }

    bool RegionSelectorWayland::failed() const {
        return impl->s.failed || !impl->s.display;
    }

    void RegionSelectorWayland::handle_event(void *native_event) {
        (void)native_event;
        if(!impl->s.started)
            return;

        if(impl->s.selected || impl->s.canceled) {
            stop();
            return;
        }

        if(!impl->s.dirty)
            return;

        impl->render_all();
        impl->s.dirty = false;
        wl_display_flush(impl->s.display);
    }

    bool RegionSelectorWayland::take_selection() {
        const bool r = impl->s.selected;
        impl->s.selected = false;
        return r;
    }

    bool RegionSelectorWayland::take_canceled() {
        const bool r = impl->s.canceled;
        impl->s.canceled = false;
        return r;
    }

    Region RegionSelectorWayland::get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const {
        (void)x11_dpy;
        (void)wayland_dpy;
        assert(impl->s.selection_type == SelectionType::REGION);
        return impl->s.region;
    }

    Window RegionSelectorWayland::get_window_selection() const {
        assert(impl->s.selection_type == SelectionType::WINDOW);
        return None;
    }

    RegionSelector::SelectionType RegionSelectorWayland::get_selection_type() const {
        return impl->s.selection_type;
    }
}
