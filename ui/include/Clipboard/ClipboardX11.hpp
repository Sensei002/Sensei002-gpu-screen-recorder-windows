#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <X11/Xlib.h>

#include "Clipboard.hpp"
#include "ClipboardTransfer.hpp"

namespace gsr {
    struct ClipboardX11Copy {
        Window requestor = None;
        uint64_t file_offset = 0;
        Atom property = None;
        Atom requestor_target = None;
    };

    class ClipboardX11 : public Clipboard {
    public:
        explicit ClipboardX11();
        ~ClipboardX11() override;
        ClipboardX11(const ClipboardX11&) = delete;
        ClipboardX11& operator=(const ClipboardX11&) = delete;

        void set_current_file(const std::string &filepath, FileType file_type) override;
    private:
        bool file_type_matches_request_atom(FileType file_type, Atom request_atom);
        const char* file_type_clipboard_get_name(Atom request_atom);
        const char* file_type_get_name(FileType file_type);
        void send_clipboard_start(XSelectionRequestEvent *xselectionrequest);
        void transfer_clipboard_data(XSelectionRequestEvent *xselectionrequest, ClipboardX11Copy *clipboard_copy);
        ClipboardX11Copy* get_clipboard_copy_by_requestor(Window requestor);
        void remove_clipboard_copy(Window requestor);
    private:
        Display *dpy = nullptr;
        Window clipboard_window = None;
        ClipboardTransferFilePtr current_file;
        FileType file_type = FileType::JPG;

        Atom incr_atom = None;
        Atom targets_atom = None;
        Atom clipboard_atom = None;
        Atom image_jpg_atom = None;
        Atom image_jpeg_atom = None;
        Atom image_png_atom = None;

        std::thread event_thread;
        std::mutex mutex;
        bool running = true;

        std::vector<ClipboardX11Copy> clipboard_copies;
        bool should_clear_selection = false;
    };
}
