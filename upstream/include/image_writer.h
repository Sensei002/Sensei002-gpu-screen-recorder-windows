#ifndef GSR_IMAGE_WRITER_H
#define GSR_IMAGE_WRITER_H

#include <stdbool.h>

typedef struct gsr_egl gsr_egl;

/* Quality at which yuv444 (no chroma subsampling) is used instead of yuv420 to preserve color detail at high quality */
#define JPEG_YUV444_QUALITY_THRESHOLD 91

typedef enum {
    GSR_IMAGE_FORMAT_JPEG,
    GSR_IMAGE_FORMAT_PNG
} gsr_image_format;

typedef struct {
    gsr_egl *egl;
    int width;
    int height;
    unsigned int texture;

    void *turbojpeg_lib;
    void *turbojpeg_compressor;
    void *tjCompress2_func;
    void *tjFree_func;
    void *tjDestroy_func;
} gsr_image_writer;

bool gsr_image_writer_init_opengl(gsr_image_writer *self, gsr_egl *egl, int width, int height);
void gsr_image_writer_deinit(gsr_image_writer *self);

/* Quality is between 1 and 100 where 100 is the max quality. Quality doesn't apply to lossless formats */
bool gsr_image_writer_write_to_file(gsr_image_writer *self, const char *filepath, gsr_image_format image_format, int quality);

#endif /* GSR_IMAGE_WRITER_H */
