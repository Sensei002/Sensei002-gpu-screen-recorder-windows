#ifndef MGL_SPRITE_H
#define MGL_SPRITE_H

#include "../system/vec.h"
#include "color.h"

typedef struct mgl_context mgl_context;
typedef struct mgl_texture mgl_texture;

typedef struct {
    mgl_texture *texture; /* nullable */
    mgl_color color;
    mgl_vec2f position;
    mgl_vec2f scale;
    mgl_vec2f origin; /* top left by default (0, 0) */
    float rotation; /* in degrees */
} mgl_sprite;

/* |texture| may be NULL */
void mgl_sprite_init(mgl_sprite *self, mgl_texture *texture);

/* |texture| may be NULL */
void mgl_sprite_set_texture(mgl_sprite *self, mgl_texture *texture);
void mgl_sprite_set_position(mgl_sprite *self, mgl_vec2f position);
void mgl_sprite_set_color(mgl_sprite *self, mgl_color color);
void mgl_sprite_set_rotation(mgl_sprite *self, float degrees);
void mgl_sprite_set_origin(mgl_sprite *self, mgl_vec2f origin);
/* This only has an effect if the sprite has a texture set */
void mgl_sprite_set_size(mgl_sprite *self, mgl_vec2f size);
/* This only has an effect if the sprite has a texture set. Scales height in proportion */
void mgl_sprite_set_width(mgl_sprite *self, float width);
/* This only has an effect if the sprite has a texture set. Scales width in proportion */
void mgl_sprite_set_height(mgl_sprite *self, float height);
/* Texture size multiplied by the sprite scaling */
mgl_vec2f mgl_sprite_get_size(const mgl_sprite *self);
void mgl_sprite_draw(mgl_context *context, mgl_sprite *sprite);

#endif /* MGL_SPRITE_H */
