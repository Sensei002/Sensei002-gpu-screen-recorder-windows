#include "../../include/mgl/graphics/sprite.h"
#include "../../include/mgl/graphics/texture.h"
#include "../../include/mgl/mgl.h"

void mgl_sprite_init(mgl_sprite *self, mgl_texture *texture) {
    self->texture = texture;
    self->color = (mgl_color){ 255, 255, 255, 255 };
    self->position = (mgl_vec2f){ 0.0f, 0.0f };
    self->scale = (mgl_vec2f){ 1.0f, 1.0f };
    self->origin = (mgl_vec2f){ 0.0f, 0.0f };
    self->rotation = 0.0f;
}

void mgl_sprite_set_texture(mgl_sprite *self, mgl_texture *texture) {
    self->texture = texture;
}

void mgl_sprite_set_position(mgl_sprite *self, mgl_vec2f position) {
    self->position = position;
}

void mgl_sprite_set_color(mgl_sprite *self, mgl_color color) {
    self->color = color;
}

void mgl_sprite_set_rotation(mgl_sprite *self, float degrees) {
    self->rotation = degrees;
}

void mgl_sprite_set_origin(mgl_sprite *self, mgl_vec2f origin) {
    self->origin = origin;
}

void mgl_sprite_set_size(mgl_sprite *self, mgl_vec2f size) {
    if(!self->texture)
        return;

    self->scale.x = size.x / (float)self->texture->width;
    self->scale.y = size.y / (float)self->texture->height;
}

void mgl_sprite_set_width(mgl_sprite *self, float width) {
    if(!self->texture)
        return;

    self->scale.x = width / (float)self->texture->width;
    self->scale.y = self->scale.x;
}

void mgl_sprite_set_height(mgl_sprite *self, float height) {
    if(!self->texture)
        return;

    self->scale.y = height / (float)self->texture->height;
    self->scale.x = self->scale.y;
}

mgl_vec2f mgl_sprite_get_size(const mgl_sprite *self) {
    if(self->texture) {
        return (mgl_vec2f){ (float)self->texture->width * self->scale.x, (float)self->texture->height * self->scale.y };
    } else {
        return (mgl_vec2f){ 0.0f, 0.0f };
    }
}

/* TODO: Cache texture bind to not bind texture if its already bound and do not bind texture 0 */
void mgl_sprite_draw(mgl_context *context, mgl_sprite *sprite) {
    if(!sprite->texture)
        return;

    float texture_right = 1.0f;
    float texture_bottom = 1.0f;
    if(sprite->texture->pixel_coordinates) {
        texture_right = sprite->texture->width;
        texture_bottom = sprite->texture->height;
    }

    context->gl.glColor4ub(sprite->color.r, sprite->color.g, sprite->color.b, sprite->color.a);
    mgl_texture_use(sprite->texture);
    context->gl.glTranslatef(sprite->position.x, sprite->position.y, 0.0f);
    context->gl.glRotatef(sprite->rotation, 0.0f, 0.0f, 1.0f);
    context->gl.glBegin(GL_QUADS);
        context->gl.glTexCoord2f(0.0f, 0.0f);
        context->gl.glVertex3f(-sprite->origin.x * sprite->scale.x, -sprite->origin.y * sprite->scale.y, 0.0f);

        context->gl.glTexCoord2f(texture_right, 0.0f);
        context->gl.glVertex3f((sprite->texture->width - sprite->origin.x) * sprite->scale.x, -sprite->origin.y * sprite->scale.y, 0.0f);

        context->gl.glTexCoord2f(texture_right, texture_bottom);
        context->gl.glVertex3f((sprite->texture->width - sprite->origin.x) * sprite->scale.x, (sprite->texture->height - sprite->origin.y) * sprite->scale.y, 0.0f);

        context->gl.glTexCoord2f(0.0f, texture_bottom);
        context->gl.glVertex3f(-sprite->origin.x * sprite->scale.x, (sprite->texture->height - sprite->origin.y) * sprite->scale.y, 0.0f);
    context->gl.glEnd();
    mgl_texture_use(NULL);
    context->gl.glLoadIdentity(); /* TODO: Remove, but what about glRotatef above */
}
