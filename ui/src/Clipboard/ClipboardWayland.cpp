#include "../../include/Clipboard/ClipboardWayland.hpp"
#include "../../include/Clipboard/ClipboardTransfer.hpp"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <wayland-client.h>
#include "ext-data-control-v1-client-protocol.h"

namespace gsr {
    namespace {
        struct ClipboardOffer {
            struct ext_data_control_offer_v1 *offer = nullptr;
        };

        struct ClipboardProbe {
            bool found = false;
        };

        void clipboard_offer_offer(void*, struct ext_data_control_offer_v1*, const char*) {}

        const struct ext_data_control_offer_v1_listener clipboard_offer_listener = {
            clipboard_offer_offer,
        };

        void probe_registry_global(void *data, struct wl_registry*, uint32_t,
            const char *interface, uint32_t)
        {
            ClipboardProbe *probe = (ClipboardProbe*)data;
            if(strcmp(interface, ext_data_control_manager_v1_interface.name) == 0)
                probe->found = true;
        }

        void probe_registry_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener probe_registry_listener = {
            probe_registry_global, probe_registry_global_remove,
        };
    }

    struct ClipboardWayland::Impl {
        struct wl_display *display = nullptr;
        struct wl_registry *registry = nullptr;
        struct wl_seat *seat = nullptr;
        struct ext_data_control_manager_v1 *manager = nullptr;
        struct ext_data_control_device_v1 *device = nullptr;
        struct ext_data_control_source_v1 *source = nullptr;

        ClipboardOffer *pending_offer = nullptr;
        ClipboardOffer *selection_offer = nullptr;
        ClipboardOffer *primary_selection_offer = nullptr;

        struct PendingSend {
            ClipboardTransferFilePtr file;
            int fd = -1;
        };

        ClipboardTransferFilePtr current_file;
        std::deque<PendingSend> pending_sends;
        std::mutex mutex;
        std::condition_variable condition_variable;
        std::thread send_thread;
        bool running = false;
        bool failed = false;

        ~Impl() { teardown(); }
        bool init(wl_display *display);
        void teardown();
        void destroy_source();
        void destroy_offer(ClipboardOffer *offer);
        void clear_selection();
        void send_loop();
    };

    namespace {
        void source_send(void *data, struct ext_data_control_source_v1 *source, const char*, int32_t fd) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            if(!set_fd_nonblocking(fd)) {
                close(fd);
                return;
            }

            ClipboardTransferFilePtr current_file;
            {
                std::lock_guard<std::mutex> lock(impl->mutex);
                current_file = impl->current_file;
                if(!current_file || impl->source != source) {
                    close(fd);
                    return;
                }

                impl->pending_sends.push_back({ std::move(current_file), fd });
            }
            impl->condition_variable.notify_one();
        }

        void source_cancelled(void *data, struct ext_data_control_source_v1 *source) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            if(impl->source == source)
                impl->source = nullptr;
            ext_data_control_source_v1_destroy(source);
        }

        const struct ext_data_control_source_v1_listener source_listener = {
            source_send, source_cancelled,
        };

        void device_data_offer(void *data, struct ext_data_control_device_v1*, struct ext_data_control_offer_v1 *offer) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            impl->destroy_offer(impl->pending_offer);
            impl->pending_offer = new ClipboardOffer();
            impl->pending_offer->offer = offer;
            ext_data_control_offer_v1_add_listener(offer, &clipboard_offer_listener, impl->pending_offer);
        }

        void device_selection(void *data, struct ext_data_control_device_v1*, struct ext_data_control_offer_v1 *offer) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            impl->destroy_offer(impl->selection_offer);
            impl->selection_offer = nullptr;

            if(offer && impl->pending_offer && impl->pending_offer->offer == offer) {
                impl->selection_offer = impl->pending_offer;
                impl->pending_offer = nullptr;
            } else {
                impl->destroy_offer(impl->pending_offer);
                impl->pending_offer = nullptr;
            }
        }

        void device_finished(void *data, struct ext_data_control_device_v1 *device) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            if(impl->device == device)
                impl->device = nullptr;
            ext_data_control_device_v1_destroy(device);
        }

        void device_primary_selection(void *data, struct ext_data_control_device_v1*, struct ext_data_control_offer_v1 *offer) {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            impl->destroy_offer(impl->primary_selection_offer);
            impl->primary_selection_offer = nullptr;

            if(offer && impl->pending_offer && impl->pending_offer->offer == offer) {
                impl->primary_selection_offer = impl->pending_offer;
                impl->pending_offer = nullptr;
            } else {
                impl->destroy_offer(impl->pending_offer);
                impl->pending_offer = nullptr;
            }
        }

        const struct ext_data_control_device_v1_listener device_listener = {
            device_data_offer,
            device_selection,
            device_finished,
            device_primary_selection,
        };

        void registry_global(void *data, struct wl_registry *registry, uint32_t name,
            const char *interface, uint32_t version)
        {
            ClipboardWayland::Impl *impl = (ClipboardWayland::Impl*)data;
            if(strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
                impl->manager = (struct ext_data_control_manager_v1*)wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
            } else if(strcmp(interface, wl_seat_interface.name) == 0 && !impl->seat) {
                impl->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, version >= 5 ? 5 : version);
            }
        }

        void registry_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener registry_listener = {
            registry_global, registry_global_remove,
        };
    }

    bool ClipboardWayland::Impl::init(wl_display *wayland_display) {
        display = wayland_display;
        if(!display) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to connect to the Wayland server\n");
            failed = true;
            return false;
        }

        registry = wl_display_get_registry(display);
        if(!registry) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to get wayland registry\n");
            failed = true;
            return false;
        }

        wl_registry_add_listener(registry, &registry_listener, this);
        wl_display_roundtrip(display);

        if(!manager) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: ext-data-control-v1 is not available\n");
            failed = true;
            return false;
        }

        if(!seat) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: no wayland seat available\n");
            failed = true;
            return false;
        }

        device = ext_data_control_manager_v1_get_data_device(manager, seat);
        if(!device) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to create data device\n");
            failed = true;
            return false;
        }

        ext_data_control_device_v1_add_listener(device, &device_listener, this);
        running = true;
        send_thread = std::thread([this] { send_loop(); });
        return true;
    }

    void ClipboardWayland::Impl::send_loop() {
        while(true) {
            PendingSend pending_send;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition_variable.wait(lock, [this] { return !running || !pending_sends.empty(); });
                if(!running && pending_sends.empty())
                    return;

                pending_send = std::move(pending_sends.front());
                pending_sends.pop_front();
            }

            // TODO: Use epoll instead of timeout
            if(!transfer_clipboard_transfer_file(pending_send.file, pending_send.fd, clipboard_transfer_write_timeout_ms))
                fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to send clipboard data, error: %s\n", strerror(errno));
            close(pending_send.fd);
        }
    }

    void ClipboardWayland::Impl::destroy_offer(ClipboardOffer *offer) {
        if(!offer)
            return;

        if(offer->offer) {
            ext_data_control_offer_v1_destroy(offer->offer);
            offer->offer = nullptr;
        }

        delete offer;
    }

    void ClipboardWayland::Impl::destroy_source() {
        if(source) {
            ext_data_control_source_v1_destroy(source);
            source = nullptr;
        }
    }

    void ClipboardWayland::Impl::clear_selection() {
        if(device)
            ext_data_control_device_v1_set_selection(device, nullptr);
        destroy_source();

        std::lock_guard<std::mutex> lock(mutex);
        current_file.reset();
    }

    void ClipboardWayland::Impl::teardown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
        }
        condition_variable.notify_all();
        if(send_thread.joinable())
            send_thread.join();

        for(PendingSend &pending_send : pending_sends) {
            if(pending_send.fd >= 0)
                close(pending_send.fd);
        }
        pending_sends.clear();

        clear_selection();

        destroy_offer(pending_offer);
        destroy_offer(selection_offer);
        destroy_offer(primary_selection_offer);
        pending_offer = nullptr;
        selection_offer = nullptr;
        primary_selection_offer = nullptr;

        if(device) {
            ext_data_control_device_v1_destroy(device);
            device = nullptr;
        }

        if(seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }

        if(manager) {
            ext_data_control_manager_v1_destroy(manager);
            manager = nullptr;
        }

        if(registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        display = nullptr;
    }

    bool ClipboardWayland::is_supported(wl_display *dpy) {
        if(!dpy)
            return false;

        ClipboardProbe probe;
        struct wl_registry *registry = wl_display_get_registry(dpy);
        if(!registry)
            return false;

        wl_registry_add_listener(registry, &probe_registry_listener, &probe);
        wl_display_roundtrip(dpy);
        wl_registry_destroy(registry);
        return probe.found;
    }

    ClipboardWayland::ClipboardWayland(wl_display *display) : impl(std::make_unique<Impl>()) {
        impl->init(display);
    }

    ClipboardWayland::~ClipboardWayland() = default;

    void ClipboardWayland::set_current_file(const std::string &filepath, FileType file_type) {
        if(!impl || impl->failed || !impl->device)
            return;

        impl->clear_selection();

        if(filepath.empty()) {
            wl_display_flush(impl->display);
            return;
        }

        const ClipboardTransferFilePtr current_file = create_clipboard_transfer_file(filepath);
        if(!current_file) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to open clipboard file %s, error: %s\n", filepath.c_str(), strerror(errno));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->current_file = current_file;
        }

        impl->source = ext_data_control_manager_v1_create_data_source(impl->manager);
        if(!impl->source) {
            fprintf(stderr, "gsr ui: error: ClipboardWayland: failed to create data source\n");
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->current_file.reset();
            return;
        }

        ext_data_control_source_v1_add_listener(impl->source, &source_listener, impl.get());
        // switch(file_type) {
        //     case FileType::JPG:
        //         ext_data_control_source_v1_offer(impl->source, "image/jpg");
        //         ext_data_control_source_v1_offer(impl->source, "image/jpeg");
        //         break;
        //     case FileType::PNG:
        //         ext_data_control_source_v1_offer(impl->source, "image/png");
        //         break;
        // }
        // TODO: Convert image to requested image type. Right now sending a jpg file when a png file is requested works ok in browsers (discord and element)
        ext_data_control_source_v1_offer(impl->source, "image/jpg");
        ext_data_control_source_v1_offer(impl->source, "image/jpeg");
        ext_data_control_source_v1_offer(impl->source, "image/png");

        ext_data_control_device_v1_set_selection(impl->device, impl->source);
        wl_display_flush(impl->display);
    }
}
