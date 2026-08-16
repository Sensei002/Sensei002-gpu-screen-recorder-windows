#ifndef MGLPP_SPRITE_HPP
#define MGLPP_SPRITE_HPP

#include "Drawable.hpp"

extern "C" {
#include <mgl/graphics/sprite.h>
}

namespace mgl {
    class Texture;
    class Sprite : public Drawable {
    public:
        Sprite();
        Sprite(Texture *texture, vec2f position = vec2f(0.0f, 0.0f));
        ~Sprite();

        void set_texture(Texture *texture);

        void set_position(vec2f position) override;
        void set_color(Color color) override;
        vec2f get_position() const override;
        vec2f get_size() const;

        void set_scale(vec2f scale);
        void set_scale(float scale);
        void set_size(vec2f size);
        void set_width(float width);
        void set_height(float height);
        void set_rotation(float degrees);
        void set_origin(vec2f origin);

        vec2f get_scale() const;

        const Texture* get_texture() const;
    protected:
        void draw(Window &window) override;
    private:
        mgl_sprite sprite;
        Texture *texture;
    };
}

#endif /* MGLPP_SPRITE_HPP */
