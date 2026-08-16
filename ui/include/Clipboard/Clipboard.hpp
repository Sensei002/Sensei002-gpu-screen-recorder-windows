#pragma once

#include <string>

namespace gsr {
    class Clipboard {
    public:
        enum class FileType {
            JPG,
            PNG
        };

        Clipboard() = default;
        virtual ~Clipboard() = default;
        Clipboard(const Clipboard&) = delete;
        Clipboard& operator=(const Clipboard&) = delete;

        // Set this to an empty string to unset clipboard
        virtual void set_current_file(const std::string &filepath, FileType file_type) = 0;
    };
}
