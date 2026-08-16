#ifndef MGLPP_IMAGE_HPP
#define MGLPP_IMAGE_HPP

#include "../system/vec.hpp"

extern "C" {
#include <mgl/graphics/image.h>
}

namespace mgl {
    class Image {
    public:
        Image();
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        ~Image();

        bool load_from_file(const char *filepath);
        bool load_from_memory(const unsigned char *data, size_t size);

        unsigned char* data();
        size_t get_byte_size();
        vec2i get_size() const;
        int get_num_channels() const;

        mgl_image* internal_image();
    private:
        mgl_image image;
    };
}

#endif /* MGLPP_IMAGE_HPP */
