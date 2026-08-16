#ifndef MGLPP_MEMORY_MAPPED_FILE_HPP
#define MGLPP_MEMORY_MAPPED_FILE_HPP

extern "C" {
#include <mgl/system/fileutils.h>
}

namespace mgl {
    class MemoryMappedFile {
    public:
        struct LoadOptions {
            bool readable = true;
            bool writable = true;
        };

        MemoryMappedFile();
        MemoryMappedFile(const MemoryMappedFile&) = delete;
        MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;
        ~MemoryMappedFile();
        
        bool load(const char *filepath, LoadOptions load_options = { true, true });

        void* data();
        size_t size();

        const mgl_memory_mapped_file* internal_mapped_file() const;
    private:
        mgl_memory_mapped_file memory_mapped_file;
    };
}

#endif /* MGLPP_MEMORY_MAPPED_FILE_HPP */
