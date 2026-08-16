#pragma once

#include "Clipboard.hpp"
#include <memory>

struct wl_display;

namespace gsr {
    class ClipboardWayland : public Clipboard {
    public:
        static bool is_supported(wl_display *dpy);

        explicit ClipboardWayland(wl_display *dpy);
        ~ClipboardWayland() override;
        ClipboardWayland(const ClipboardWayland&) = delete;
        ClipboardWayland& operator=(const ClipboardWayland&) = delete;

        void set_current_file(const std::string &filepath, FileType file_type) override;

        struct Impl;
    private:
        std::unique_ptr<Impl> impl;
    };
}
