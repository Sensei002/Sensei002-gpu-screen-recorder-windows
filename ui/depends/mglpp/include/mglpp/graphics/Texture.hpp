#ifndef MGLPP_TEXTURE_HPP
#define MGLPP_TEXTURE_HPP

#include "../system/vec.hpp"

extern "C" {
#include <mgl/graphics/texture.h>
}

namespace mgl {
    class Image;
    class Texture {
    public:
        struct LoadOptions {
            bool compressed = false;
            bool pixel_coordinates = false;
            mgl_texture_scale_type scale_type = MGL_TEXTURE_SCALE_LINEAR;
        };

        struct ReferenceOptions {
            bool pixel_coordinates = false;
            mgl_texture_scale_type scale_type = MGL_TEXTURE_SCALE_LINEAR;
        };

        Texture();
        Texture(unsigned int gl_texture_id, mgl_texture_format format, const ReferenceOptions reference_options = {false, MGL_TEXTURE_SCALE_LINEAR});
        Texture(Texture &&other);
        Texture& operator=(Texture &&other);
        ~Texture();

        static Texture reference(mgl_texture ref);

        bool load_from_file(const char *filepath, const LoadOptions load_options = {false, false, MGL_TEXTURE_SCALE_LINEAR});
        bool load_from_image(Image &image, const LoadOptions load_options = {false, false, MGL_TEXTURE_SCALE_LINEAR});
        bool load_from_memory(const unsigned char *data, int width, int height, mgl_image_format format, LoadOptions load_options = {false, false, MGL_TEXTURE_SCALE_LINEAR});
        void clear();
        vec2i get_size() const;
        bool is_valid() const;

        mgl_texture* internal_texture();
    private:
        Texture(const Texture&);
        Texture& operator=(const Texture&);
    private:
        mgl_texture texture;
        bool owned = true;
    };
}

#endif /* MGLPP_TEXTURE_HPP */
