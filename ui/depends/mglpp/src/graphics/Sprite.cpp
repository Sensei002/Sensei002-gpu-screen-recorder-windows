#include "../../include/mglpp/graphics/Sprite.hpp"
#include "../../include/mglpp/graphics/Texture.hpp"

extern "C" {
#include <mgl/mgl.h>
}

namespace mgl {
    Sprite::Sprite() : Sprite(nullptr, vec2f(0.0f, 0.0f)) {}

    Sprite::Sprite(Texture *texture, vec2f position) : texture(texture) {
        mgl_sprite_init(&sprite, texture ? texture->internal_texture() : nullptr);
        set_position(position);
    }

    Sprite::~Sprite() {
        
    }

    void Sprite::set_texture(Texture *texture) {
        this->texture = texture;
        mgl_sprite_set_texture(&sprite, texture ? texture->internal_texture() : nullptr);
    }

    void Sprite::set_position(vec2f position) {
        mgl_sprite_set_position(&sprite, {position.x, position.y});
    }

    void Sprite::set_color(Color color) {
        mgl_sprite_set_color(&sprite, {color.r, color.g, color.b, color.a});
    }

    vec2f Sprite::get_position() const {
        return { sprite.position.x, sprite.position.y };
    }

    vec2f Sprite::get_size() const {
        const mgl_vec2f v = mgl_sprite_get_size(&sprite);
        return { v.x, v.y };
    }

    void Sprite::set_scale(vec2f scale) {
        sprite.scale = { scale.x, scale.y };
    }

    void Sprite::set_scale(float scale) {
        sprite.scale = { scale, scale };
    }

    void Sprite::set_size(vec2f size) {
        mgl_sprite_set_size(&sprite, mgl_vec2f{size.x, size.y});
    }

    void Sprite::set_width(float width) {
        mgl_sprite_set_width(&sprite, width);
    }

    void Sprite::set_height(float height) {
        mgl_sprite_set_height(&sprite, height);
    }

    void Sprite::set_rotation(float degrees) {
        mgl_sprite_set_rotation(&sprite, degrees);
    }

    void Sprite::set_origin(vec2f origin) {
        mgl_sprite_set_origin(&sprite, mgl_vec2f{origin.x, origin.y});
    }

    vec2f Sprite::get_scale() const {
        return { sprite.scale.x, sprite.scale.y };
    }

    const Texture* Sprite::get_texture() const {
        return texture;
    }

    void Sprite::draw(Window&) {
        mgl_sprite_draw(mgl_get_context(), &sprite);
    }
}