#include "../../include/mglpp/system/MemoryMappedFile.hpp"

namespace mgl {
    MemoryMappedFile::MemoryMappedFile() {
        memory_mapped_file.data = nullptr;
        memory_mapped_file.size = 0;
        memory_mapped_file.fd = -1;
    }

    MemoryMappedFile::~MemoryMappedFile() {
        mgl_mapped_file_unload(&memory_mapped_file);
    }
    
    bool MemoryMappedFile::load(const char *filepath, LoadOptions load_options) {
        if(memory_mapped_file.fd != -1) {
            mgl_mapped_file_unload(&memory_mapped_file);
            memory_mapped_file.data = nullptr;
            memory_mapped_file.size = 0;
            memory_mapped_file.fd = -1;
        }

        mgl_memory_mapped_file_load_options mapped_file_load_options = {
            load_options.readable,
            load_options.writable
        };
        return mgl_mapped_file_load(filepath, &memory_mapped_file, &mapped_file_load_options) == 0;
    }

    void* MemoryMappedFile::data() {
        return memory_mapped_file.data;
    }

    size_t MemoryMappedFile::size() {
        return memory_mapped_file.size;
    }

    const mgl_memory_mapped_file* MemoryMappedFile::internal_mapped_file() const {
        return &memory_mapped_file;
    }
}