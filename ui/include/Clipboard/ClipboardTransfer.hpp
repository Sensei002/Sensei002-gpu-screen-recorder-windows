#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace gsr {
    struct ClipboardTransferFile {
        ClipboardTransferFile(int fd, uint64_t size);
        ~ClipboardTransferFile();
        ClipboardTransferFile(const ClipboardTransferFile&) = delete;
        ClipboardTransferFile& operator=(const ClipboardTransferFile&) = delete;

        int fd = -1;
        uint64_t size = 0;
    };

    using ClipboardTransferFilePtr = std::shared_ptr<ClipboardTransferFile>;

    constexpr int clipboard_transfer_chunk_size = 1 << 16;
    constexpr int clipboard_transfer_write_timeout_ms = 5000;

    ClipboardTransferFilePtr create_clipboard_transfer_file(const std::string &filepath);
    bool set_fd_nonblocking(int fd);
    bool read_clipboard_transfer_chunk(const ClipboardTransferFilePtr &file, uint64_t offset, unsigned char *buffer, size_t buffer_size, ssize_t *bytes_read);
    bool write_clipboard_transfer_data(int fd, const void *data, size_t size, int timeout_ms);
    bool transfer_clipboard_transfer_file(const ClipboardTransferFilePtr &file, int output_fd, int timeout_ms);
}
