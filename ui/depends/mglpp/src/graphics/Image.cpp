#include "../../include/mglpp/graphics/Image.hpp"
#include <string.h>

namespace mgl {
    Image::Image() {
        memset(&image, 0, sizeof(image));
    }

    Image::~Image() {
        mgl_image_unload(&image);
    }

    bool Image::load_from_file(const char *filepath) {
        if(image.data) {
            mgl_image_unload(&image);
            memset(&image, 0, sizeof(image));
        }
        return mgl_image_load_from_file(&image, filepath) == 0;
    }

    bool Image::load_from_memory(const unsigned char *data, size_t size) {
        if(image.data) {
            mgl_image_unload(&image);
            memset(&image, 0, sizeof(image));
        }
        return mgl_image_load_from_memory(&image, data, size) == 0;
    }

    unsigned char* Image::data() {
        return image.data;
    }

    size_t Image::get_byte_size() {
        return mgl_image_get_size(&image);
    }

    vec2i Image::get_size() const {
        return { image.width, image.height };
    }

    int Image::get_num_channels() const {
        return mgl_image_get_num_channels(&image);
    }

    mgl_image* Image::internal_image() {
        return &image;
    }
}