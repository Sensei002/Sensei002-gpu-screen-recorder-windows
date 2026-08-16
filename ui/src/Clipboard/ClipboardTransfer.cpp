#include "../../include/Clipboard/ClipboardTransfer.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gsr {
    namespace {
        static bool set_fd_cloexec(int fd) {
            const int flags = fcntl(fd, F_GETFD);
            if(flags < 0)
                return false;

            if(flags & FD_CLOEXEC)
                return true;

            return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
        }

        static bool wait_until_writable(int fd, int timeout_ms) {
            pollfd poll_fd;
            poll_fd.fd = fd;
            poll_fd.events = POLLOUT;
            poll_fd.revents = 0;

            while(true) {
                const int poll_result = poll(&poll_fd, 1, timeout_ms);
                if(poll_result > 0)
                    return (poll_fd.revents & (POLLOUT | POLLERR | POLLHUP)) == POLLOUT;

                if(poll_result == 0) {
                    errno = ETIMEDOUT;
                    return false;
                }

                if(errno != EINTR)
                    return false;
            }
        }
    }

    ClipboardTransferFile::ClipboardTransferFile(int fd, uint64_t size) : fd(fd), size(size) {}

    ClipboardTransferFile::~ClipboardTransferFile() {
        if(fd >= 0)
            close(fd);
    }

    ClipboardTransferFilePtr create_clipboard_transfer_file(const std::string &filepath) {
        if(filepath.empty()) {
            errno = EINVAL;
            return {};
        }

        const int input_fd = open(filepath.c_str(), O_RDONLY | O_CLOEXEC);
        if(input_fd < 0)
            return {};

        if(!set_fd_cloexec(input_fd)) {
            close(input_fd);
            return {};
        }

        struct stat stat;
        if(fstat(input_fd, &stat) == -1) {
            close(input_fd);
            return {};
        }

        return std::make_shared<ClipboardTransferFile>(input_fd, stat.st_size);
    }

    bool set_fd_nonblocking(int fd) {
        const int flags = fcntl(fd, F_GETFL);
        if(flags < 0)
            return false;

        if(flags & O_NONBLOCK)
            return true;

        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool read_clipboard_transfer_chunk(const ClipboardTransferFilePtr &file, uint64_t offset, unsigned char *buffer, size_t buffer_size, ssize_t *bytes_read) {
        if(!file || file->fd < 0 || !buffer || !bytes_read) {
            errno = EINVAL;
            return false;
        }

        while(true) {
            *bytes_read = pread(file->fd, buffer, buffer_size, offset);
            if(*bytes_read < 0 && errno == EINTR)
                continue;
            return *bytes_read >= 0;
        }
    }

    bool write_clipboard_transfer_data(int fd, const void *data, size_t size, int timeout_ms) {
        const unsigned char *ptr = (const unsigned char*)data;
        while(size > 0) {
            const ssize_t bytes_written = write(fd, ptr, size);
            if(bytes_written < 0) {
                if(errno == EINTR)
                    continue;

                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    if(wait_until_writable(fd, timeout_ms))
                        continue;
                }

                return false;
            }

            ptr += bytes_written;
            size -= bytes_written;
        }
        return true;
    }

    bool transfer_clipboard_transfer_file(const ClipboardTransferFilePtr &file, int output_fd, int timeout_ms) {
        unsigned char buffer[clipboard_transfer_chunk_size];
        uint64_t offset = 0;
        while(true) {
            ssize_t bytes_read = 0;
            if(!read_clipboard_transfer_chunk(file, offset, buffer, sizeof(buffer), &bytes_read))
                return false;

            if(bytes_read == 0)
                return true;

            if(!write_clipboard_transfer_data(output_fd, buffer, bytes_read, timeout_ms))
                return false;

            offset += bytes_read;
        }
    }
}
