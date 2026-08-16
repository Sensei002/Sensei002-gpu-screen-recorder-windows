#ifndef MGL_FILEUTILS_H
#define MGL_FILEUTILS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct mgl_memory_mapped_file mgl_memory_mapped_file;

typedef struct {
    unsigned char *data;
    size_t size;
} mgl_filedata;

typedef struct {
    bool null_terminated; /* false by default */
} mgl_file_load_options;

struct mgl_memory_mapped_file {
    void *data;
    size_t size;
    int fd;
};

typedef struct {
    bool readable; /* true by default */
    bool writable; /* true by default */
} mgl_memory_mapped_file_load_options;

/* |load_options| can be null, in which case the default options are used */
int mgl_load_file(const char *filepath, mgl_filedata *filedata, const mgl_file_load_options *load_options);
void mgl_filedata_free(mgl_filedata *self);

/* |load_options| can be null, in which case the default options are used */
int mgl_mapped_file_load(const char *filepath, mgl_memory_mapped_file *memory_mapped_file, const mgl_memory_mapped_file_load_options *load_options);
void mgl_mapped_file_unload(mgl_memory_mapped_file *memory_mapped_file);

#endif /* MGL_FILEUTILS_H */
