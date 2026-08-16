#include "../../include/mgl/system/fileutils.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int mgl_load_file(const char *filepath, mgl_filedata *filedata, const mgl_file_load_options *load_options) {
    const bool null_terminated = load_options ? load_options->null_terminated : false;

    int fd = open(filepath, O_RDONLY);
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

int mgl_mapped_file_load(const char *filepath, mgl_memory_mapped_file *memory_mapped_file, const mgl_memory_mapped_file_load_options *load_options) {
    memory_mapped_file->data = NULL;
    memory_mapped_file->size = 0;
    memory_mapped_file->fd = -1;

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
}

void mgl_mapped_file_unload(mgl_memory_mapped_file *memory_mapped_file) {
    if(memory_mapped_file->data != MAP_FAILED && memory_mapped_file->data != NULL) {
        munmap(memory_mapped_file->data, memory_mapped_file->size);
        memory_mapped_file->data = NULL;
    }
    memory_mapped_file->size = 0;

    if(memory_mapped_file->fd != -1) {
        close(memory_mapped_file->fd);
        memory_mapped_file->fd = -1;
    }
}
