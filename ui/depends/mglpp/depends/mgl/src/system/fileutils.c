#include "../../include/mgl/system/fileutils.h"
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdint.h>

int mgl_load_file(const char *filepath, mgl_filedata *filedata, const mgl_file_load_options *load_options) {
    const bool null_terminated = load_options ? load_options->null_terminated : false;

#ifdef _WIN32
    /* MinGW's CRT defaults to text mode, where read() stops at Ctrl-Z
       (0x1A) — never correct for binary file data. */
    int fd = open(filepath, O_RDONLY | O_BINARY);
#else
    int fd = open(filepath, O_RDONLY);
#endif
    if(fd == -1)
        return -1;

    struct stat st;
    if(fstat(fd, &st) == -1) {
        close(fd);
        return -1;
    }

    if(!S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }

    unsigned char *data = malloc(st.st_size + (null_terminated ? 1 : 0));
    if(!data) {
        close(fd);
        return -1;
    }

    if(read(fd, data, st.st_size) != st.st_size) {
        free(data);
        close(fd);
        return -1;
    }

    if(null_terminated)
        data[st.st_size] = '\0';

    filedata->data = data;
    filedata->size = st.st_size;

    close(fd);
    return 0;
}

void mgl_filedata_free(mgl_filedata *self) {
    free(self->data);
    self->data = NULL;
    self->size = 0;
}

static int load_options_to_open_flag(const mgl_memory_mapped_file_load_options *load_options) {
    const bool readable = load_options ? load_options->readable : true;
    const bool writable = load_options ? load_options->writable : true;

    int open_flag = 0;
    if(readable && writable)
        open_flag = O_RDWR;
    else if(readable)
        open_flag = O_RDONLY;
    else if(writable)
        open_flag = O_WRONLY;

    return open_flag;
}

#ifndef _WIN32
static int load_options_to_mmap_prot_flag(const mgl_memory_mapped_file_load_options *load_options) {
    const bool readable = load_options ? load_options->readable : true;
    const bool writable = load_options ? load_options->writable : true;

    int prot_flag = 0;
    if(readable)
        prot_flag |= PROT_READ;
    if(writable)
        prot_flag |= PROT_WRITE;

    return prot_flag;
}
#endif

int mgl_mapped_file_load(const char *filepath, mgl_memory_mapped_file *memory_mapped_file, const mgl_memory_mapped_file_load_options *load_options) {
    memory_mapped_file->data = NULL;
    memory_mapped_file->size = 0;
    memory_mapped_file->fd = -1;

#ifdef _WIN32
    const bool readable = load_options ? load_options->readable : true;
    const bool writable = load_options ? load_options->writable : true;

    HANDLE hfile = CreateFileA(filepath,
        writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, writable ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hfile == INVALID_HANDLE_VALUE)
        return -1;

    LARGE_INTEGER size;
    if(!GetFileSizeEx(hfile, &size) || size.QuadPart <= 0) {
        CloseHandle(hfile);
        return -1;
    }

    HANDLE hmapping = CreateFileMappingA(hfile, NULL, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, NULL);
    if(!hmapping) {
        CloseHandle(hfile);
        return -1;
    }

    void *mapped = MapViewOfFile(hmapping, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hmapping);
    if(!mapped) {
        CloseHandle(hfile);
        return -1;
    }

    memory_mapped_file->data = mapped;
    memory_mapped_file->size = (size_t)size.QuadPart;
    /* The HANDLE is stored in fd and closed in mgl_mapped_file_unload. */
    memory_mapped_file->fd = (int)(intptr_t)hfile;
    return 0;
#else
    int fd = open(filepath, load_options_to_open_flag(load_options));
    if(fd == -1)
        return -1;

    struct stat st;
    if(fstat(fd, &st) == -1) {
        close(fd);
        return -1;
    }

    void *mapped = mmap(0, st.st_size, load_options_to_mmap_prot_flag(load_options), MAP_SHARED, fd, 0);
    if(mapped == MAP_FAILED) {
        close(fd);
        return -1;
    }

    memory_mapped_file->data = mapped;
    memory_mapped_file->size = st.st_size;
    memory_mapped_file->fd = fd;
    return 0;
#endif
}

void mgl_mapped_file_unload(mgl_memory_mapped_file *memory_mapped_file) {
#ifdef _WIN32
    if(memory_mapped_file->data) {
        UnmapViewOfFile(memory_mapped_file->data);
        memory_mapped_file->data = NULL;
    }
    memory_mapped_file->size = 0;

    if(memory_mapped_file->fd != -1) {
        CloseHandle((HANDLE)(intptr_t)memory_mapped_file->fd);
        memory_mapped_file->fd = -1;
    }
#else
    if(memory_mapped_file->data != MAP_FAILED && memory_mapped_file->data != NULL) {
        munmap(memory_mapped_file->data, memory_mapped_file->size);
        memory_mapped_file->data = NULL;
    }
    memory_mapped_file->size = 0;

    if(memory_mapped_file->fd != -1) {
        close(memory_mapped_file->fd);
        memory_mapped_file->fd = -1;
    }
#endif
}
