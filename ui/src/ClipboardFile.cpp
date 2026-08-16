#include "../include/ClipboardFile.hpp"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <X11/Xatom.h>

#define FORMAT_I64 "%" PRIi64
#define FORMAT_U64 "%" PRIu64

namespace gsr {
    ClipboardFile::ClipboardFile() {
        dpy = XOpenDisplay(nullptr);
        if(!dpy) {
            fprintf(stderr, "gsr ui: error: ClipboardFile: failed to connect to the X11 server\n");
            return;
        }

        clipboard_window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 8, 8, 0, 0, 0);
        if(!clipboard_window) {
            fprintf(stderr, "gsr ui: error: ClipboardFile: failed to create clipboard window\n");
            XCloseDisplay(dpy);
            dpy = nullptr;
            return;
        }

        incr_atom = XInternAtom(dpy, "INCR", False);
        targets_atom = XInternAtom(dpy, "TARGETS", False);
        clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
        image_jpg_atom = XInternAtom(dpy, "image/jpg", False);
        image_jpeg_atom = XInternAtom(dpy, "image/jpeg", False);
        image_png_atom = XInternAtom(dpy, "image/png", False);

        event_thread = std::thread([&]() {
            pollfd poll_fds[1];
            poll_fds[0].fd = ConnectionNumber(dpy);
            poll_fds[0].events = POLLIN;
            poll_fds[0].revents = 0;

            XEvent xev;
            while(running) {
                poll_fds[0].revents = 0;
                poll(poll_fds, 1, 100);
                while(XPending(dpy)) {
                    XNextEvent(dpy, &xev);
                    switch(xev.type) {
                        case SelectionClear: {
                            bool clear_current_file = false;
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                should_clear_selection = true;
                                if(clipboard_copies.empty()) {
                                    should_clear_selection = false;
                                    clear_current_file = true;
                                }
                            }

                            if(clear_current_file)
                                set_current_file("", file_type);
                            break;
                        }
                        case SelectionRequest:
                            send_clipboard_start(&xev.xselectionrequest);
                            break;
                        case PropertyNotify: {
                            if(xev.xproperty.state == PropertyDelete) {
                                std::lock_guard<std::mutex> lock(mutex);
                                ClipboardCopy *clipboard_copy = get_clipboard_copy_by_requestor(xev.xproperty.window);
                                if(!clipboard_copy || xev.xproperty.atom != clipboard_copy->property)
                                    return;

                                XSelectionRequestEvent xselectionrequest;
                                xselectionrequest.display = xev.xproperty.display;;
                                xselectionrequest.requestor = xev.xproperty.window;
                                xselectionrequest.selection = clipboard_atom;
                                xselectionrequest.target = clipboard_copy->requestor_target;
                                xselectionrequest.property = clipboard_copy->property;
                                xselectionrequest.time = xev.xproperty.time;
                                transfer_clipboard_data(&xselectionrequest, clipboard_copy);
                            }
                            break;
                        }
                    }
                }
            }
        });
    }

    ClipboardFile::~ClipboardFile() {
        running = false;
        if(event_thread.joinable())
            event_thread.join();

        if(file_fd > 0)
            close(file_fd);

        if(dpy) {
            XDestroyWindow(dpy, clipboard_window);
            XCloseDisplay(dpy);
        }
    }

    bool ClipboardFile::file_type_matches_request_atom(FileType file_type, Atom request_atom) {
        switch(file_type) {
            case FileType::JPG:
                return request_atom == image_jpg_atom || request_atom == image_jpeg_atom;
            case FileType::PNG:
                return request_atom == image_png_atom;
        }
        return false;
    }

    const char* ClipboardFile::file_type_clipboard_get_name(Atom request_atom) {
        if(request_atom == image_jpg_atom)
            return "image/jpg";
        else if(request_atom == image_jpeg_atom)
            return "image/jpeg";
        else if(request_atom == image_png_atom)
            return "image/png";
        return "Unknown";
    }

    const char* ClipboardFile::file_type_get_name(FileType file_type) {
        switch(file_type) {
            case FileType::JPG:
                return "image/jpeg";
            case FileType::PNG:
                return "image/png";
        }
        return "Unknown";
    }

    void ClipboardFile::send_clipboard_start(XSelectionRequestEvent *xselectionrequest) {
        std::lock_guard<std::mutex> lock(mutex);
        if(file_fd <= 0) {
            fprintf(stderr, "gsr ui: warning: ClipboardFile::send_clipboard: requestor window " FORMAT_I64 " tried to get clipboard from us but we don't have any clipboard file open\n", (int64_t)xselectionrequest->requestor);
            return;
        }

        if(xselectionrequest->selection != clipboard_atom) {
            fprintf(stderr, "gsr ui: warning: ClipboardFile::send_clipboard: requestor window " FORMAT_I64 " tried to non-clipboard selection from us\n", (int64_t)xselectionrequest->requestor);
            return;
        }

        XSelectionEvent selection_event;
        selection_event.type = SelectionNotify;
        selection_event.display = xselectionrequest->display;
        selection_event.requestor = xselectionrequest->requestor;
        selection_event.selection = xselectionrequest->selection;
        selection_event.property = xselectionrequest->property;
        selection_event.time = xselectionrequest->time;
        selection_event.target = xselectionrequest->target;

        if(xselectionrequest->target == targets_atom) {
            int num_targets = 1;
            Atom targets[4];
            targets[0] = targets_atom;

            switch(file_type) {
                case FileType::JPG:
                    num_targets = 4;
                    targets[1] = image_jpg_atom;
                    targets[2] = image_jpeg_atom;
                    targets[3] = image_png_atom;
                    break;
                case FileType::PNG:
                    num_targets = 2;
                    targets[1] = image_png_atom;
                    targets[2] = image_jpg_atom;
                    targets[3] = image_jpeg_atom;
                    break;
            }

            XChangeProperty(dpy, selection_event.requestor, selection_event.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)targets, num_targets);
        } else if(xselectionrequest->target == image_jpg_atom || xselectionrequest->target == image_jpeg_atom || xselectionrequest->target == image_png_atom) {
            // TODO: Convert image to requested image type. Right now sending a jpg file when a png file is requested works ok in browsers (discord and element)
            if(!file_type_matches_request_atom(file_type, xselectionrequest->target)) {
                const char *expected_file_type = file_type_get_name(file_type);
                fprintf(stderr, "gsr ui: warning: ClipboardFile::send_clipboard: requestor window " FORMAT_I64 " tried to request clipboard of type %s, but %s was expected. Ignoring requestor and sending as %s\n", (int64_t)xselectionrequest->requestor, file_type_clipboard_get_name(xselectionrequest->target), expected_file_type, expected_file_type);
                //return;
            }

            ClipboardCopy *clipboard_copy = get_clipboard_copy_by_requestor(xselectionrequest->requestor);
            if(!clipboard_copy) {
                clipboard_copies.push_back({ xselectionrequest->requestor, 0 });
                clipboard_copy = &clipboard_copies.back();
            }

            *clipboard_copy = { xselectionrequest->requestor, 0 };
            clipboard_copy->property = selection_event.property;
            clipboard_copy->requestor_target = selection_event.target;
            XSelectInput(dpy, selection_event.requestor, PropertyChangeMask);

            const long lower_bound = std::min((uint64_t)1<<16, file_size);
            XChangeProperty(dpy, selection_event.requestor, selection_event.property, incr_atom, 32, PropModeReplace, (const unsigned char*)&lower_bound, 1);
        } else {
            char *target_clipboard_name = XGetAtomName(dpy, xselectionrequest->target);
            fprintf(stderr, "gsr ui: warning: ClipboardFile::send_clipboard: requestor window " FORMAT_I64 " tried to request clipboard of type %s, expected TARGETS, image/jpg, image/jpeg or image/png\n", (int64_t)xselectionrequest->requestor, target_clipboard_name ? target_clipboard_name : "Unknown");
            if(target_clipboard_name)
                XFree(target_clipboard_name);
            selection_event.property = None;
        }

        XSendEvent(dpy, selection_event.requestor, False, NoEventMask, (XEvent*)&selection_event);
        XFlush(dpy);
    }

    void ClipboardFile::transfer_clipboard_data(XSelectionRequestEvent *xselectionrequest, ClipboardCopy *clipboard_copy) {
        uint8_t file_buffer[1<<16];
        ssize_t file_bytes_read = 0;

        if(file_fd <= 0)
            return;

        if(lseek(file_fd, clipboard_copy->file_offset, SEEK_SET) == -1) {
            fprintf(stderr, "gsr ui: error: ClipboardFile::send_clipboard: failed to seek in clipboard file to offset " FORMAT_U64 " for requestor window " FORMAT_I64 ", error: %s\n", (uint64_t)clipboard_copy->file_offset, (int64_t)xselectionrequest->requestor, strerror(errno));
            clipboard_copy->file_offset = 0;
            // TODO: Cancel transfer
            return;
        }

        file_bytes_read = read(file_fd, file_buffer, sizeof(file_buffer));
        if(file_bytes_read < 0) {
            fprintf(stderr, "gsr ui: error: ClipbaordFile::send_clipboard: failed to read data from offset " FORMAT_U64 " for requestor window " FORMAT_I64 ", error: %s\n", (uint64_t)clipboard_copy->file_offset, (int64_t)xselectionrequest->requestor, strerror(errno));
            clipboard_copy->file_offset = 0;
            // TODO: Cancel transfer
            return;
        }

        XChangeProperty(dpy, xselectionrequest->requestor, xselectionrequest->property, xselectionrequest->target, 8, PropModeReplace, (const unsigned char*)file_buffer, file_bytes_read);
        XSendEvent(dpy, xselectionrequest->requestor, False, NoEventMask, (XEvent*)xselectionrequest);
        XFlush(dpy);

        clipboard_copy->file_offset += file_bytes_read;
        if(file_bytes_read == 0)
            remove_clipboard_copy(clipboard_copy->requestor);
    }

    ClipboardCopy* ClipboardFile::get_clipboard_copy_by_requestor(Window requestor) {
        for(ClipboardCopy &clipboard_copy : clipboard_copies) {
            if(clipboard_copy.requestor == requestor)
                return &clipboard_copy;
        }
        return nullptr;
    }

    void ClipboardFile::remove_clipboard_copy(Window requestor) {
        for(auto it = clipboard_copies.begin(), end = clipboard_copies.end(); it != end; ++it) {
            if(it->requestor == requestor) {
                clipboard_copies.erase(it);
                XSelectInput(dpy, requestor, 0);

                if(clipboard_copies.empty() && should_clear_selection) {
                    should_clear_selection = false;
                    set_current_file("", file_type);
                }
                return;
            }
        }
    }

    void ClipboardFile::set_current_file(const std::string &filepath, FileType file_type) {
        if(!dpy)
            return;

        std::lock_guard<std::mutex> lock(mutex);
        for(ClipboardCopy &clipboard_copy : clipboard_copies) {
            XSelectInput(dpy, clipboard_copy.requestor, 0);
        }
        clipboard_copies.clear();

        if(XGetSelectionOwner(dpy, clipboard_atom) == clipboard_window) {
            XSetSelectionOwner(dpy, clipboard_atom, None, CurrentTime);
            XFlush(dpy);
        }

        if(filepath.empty()) {
            // TODO: Cancel transfer
            if(file_fd > 0) {
                close(file_fd);
                file_fd = -1;
            }
            file_size = 0;
            return;
        }

        if(file_fd > 0) {
            close(file_fd);
            file_fd = -1;
            file_size = 0;
        }

        file_fd = open(filepath.c_str(), O_RDONLY);
        if(file_fd <= 0) {
            fprintf(stderr, "gsr ui: error: ClipboardFile::set_current_file: failed to open file %s, error: %s\n", filepath.c_str(), strerror(errno));
            return;
        }

        struct stat stat;
        if(fstat(file_fd, &stat) == -1) {
            fprintf(stderr, "gsr ui: error: ClipboardFile::set_current_file: failed to get file size for file %s, error: %s\n", filepath.c_str(), strerror(errno));
            close(file_fd);
            file_fd = -1;
            return;
        }
        file_size = stat.st_size;
        this->file_type = file_type;

        XSetSelectionOwner(dpy, clipboard_atom, clipboard_window, CurrentTime);
        XFlush(dpy);
    }
}
