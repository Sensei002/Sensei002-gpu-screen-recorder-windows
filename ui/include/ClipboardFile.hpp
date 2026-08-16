#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <X11/Xlib.h>

namespace gsr {
    struct ClipboardCopy {
        Window requestor = None;
        uint64_t file_offset = 0;
        Atom property = None;
        Atom requestor_target = None;
    };

    class ClipboardFile {
    public:
        enum class FileType {
            JPG,
            PNG
        };

        ClipboardFile();
        ~ClipboardFile();
        ClipboardFile(const ClipboardFile&) = delete;
        ClipboardFile& operator=(const ClipboardFile&) = delete;

        // Set this to an empty string to unset clipboard
        void set_current_file(const std::string &filepath, FileType file_type);
    private:
        bool file_type_matches_request_atom(FileType file_type, Atom request_atom);
        const char* file_type_clipboard_get_name(Atom request_atom);
        const char* file_type_get_name(FileType file_type);
        void send_clipboard_start(XSelectionRequestEvent *xselectionrequest);
        void transfer_clipboard_data(XSelectionRequestEvent *xselectionrequest, ClipboardCopy *clipboard_copy);
        ClipboardCopy* get_clipboard_copy_by_requestor(Window requestor);
        void remove_clipboard_copy(Window requestor);
    private:
        Display *dpy = nullptr;
        Window clipboard_window = None;
        int file_fd = -1;
        uint64_t file_size = 0;
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

        std::vector<ClipboardCopy> clipboard_copies;
        bool should_clear_selection = false;
    };
}