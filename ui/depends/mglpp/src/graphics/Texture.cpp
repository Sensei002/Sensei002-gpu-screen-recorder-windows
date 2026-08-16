#include "../../include/mglpp/graphics/Texture.hpp"
#include "../../include/mglpp/graphics/Image.hpp"
#include <string.h>

namespace mgl {
    Texture::Texture() {
        memset(&texture, 0, sizeof(mgl_texture));
    }

    Texture::Texture(unsigned int gl_texture_id, mgl_texture_format format, const ReferenceOptions reference_options) {
        memset(&texture, 0, sizeof(mgl_texture));
        const mgl_texture_reference_options texture_reference_options = {
            reference_options.pixel_coordinates,
            reference_options.scale_type
        };
        mgl_texture_init_reference_existing_gl_texture(&texture, gl_texture_id, format, &texture_reference_options);
    }

    Texture::Texture(Texture &&other) {
        memcpy(&texture, &other.texture, sizeof(mgl_texture));
        owned = other.owned;

        other.texture.id = 0;
        other.owned = false;
    }

    Texture& Texture::operator=(Texture &&other) {
        if(owned)
            mgl_texture_unload(&texture);

        memcpy(&texture, &other.texture, sizeof(mgl_texture));
        owned = other.owned;

        other.texture.id = 0;
        other.owned = false;
        return *this;
    }

    Texture::~Texture() {
        if(owned)
            mgl_texture_unload(&texture);
    }

    // static
    Texture Texture::reference(mgl_texture ref) {
        Texture instance;
        instance.texture = ref;
        instance.owned = false;
        return instance;
    }

    bool Texture::load_from_file(const char *filepath, const LoadOptions load_options) {
        if(texture.id)
            clear();

        if(mgl_texture_init(&texture) != 0)
            return false;

        const mgl_texture_load_options texture_load_options = {
            load_options.compressed,
            load_options.pixel_coordinates,
            load_options.scale_type
        };
        
        if(mgl_texture_load_from_file(&texture, filepath, &texture_load_options) == 0) {
            return true;
        } else {
            clear();
            return false;
        }
    }

    bool Texture::load_from_image(Image &image, const LoadOptions load_options) {
        if(texture.id)
            clear();

        if(mgl_texture_init(&texture) != 0)
            return false;

        const mgl_texture_load_options texture_load_options = {
            load_options.compressed,
            load_options.pixel_coordinates,
            load_options.scale_type
        };
        
        if(mgl_texture_load_from_image(&texture, image.internal_image(), &texture_load_options) == 0) {
            return true;
        } else {
            clear();
            return false;
        }
    }

    bool Texture::load_from_memory(const unsigned char *data, int width, int height, mgl_image_format format, LoadOptions load_options) {
        if(texture.id)
            clear();

        if(mgl_texture_init(&texture) != 0)
            return false;

        const mgl_texture_load_options texture_load_options = {
            load_options.compressed,
            load_options.pixel_coordinates,
            load_options.scale_type
        };
        
        if(mgl_texture_load_from_memory(&texture, data, width, height, format, &texture_load_options) == 0) {
            return true;
        } else {
            clear();
            return false;
        }
    }

    void Texture::clear() {
        if(owned)
            mgl_texture_unload(&texture);
        memset(&texture, 0, sizeof(mgl_texture));
        owned = true;
    }

    vec2i Texture::get_size() const {
        return { texture.width, texture.height };
    }

    bool Texture::is_valid() const {
        return texture.id != 0;
    }

    mgl_texture* Texture::internal_texture() {
        return &texture;
    }
}
