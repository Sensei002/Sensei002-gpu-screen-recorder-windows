#ifndef WINDOW_TEXTURE_H
#define WINDOW_TEXTURE_H

#include "egl.h"

typedef struct {
    Display *display;
    Window window;
    Pixmap pixmap;
    EGLImage image;
    unsigned int texture_id;
    int redirected;
    gsr_egl *egl;
    unsigned int window_width;
    unsigned int window_height;
} WindowTexture;

/* Returns 0 on success */
int window_texture_init(WindowTexture *window_texture, Display *display, Window window, gsr_egl *egl);
void window_texture_deinit(WindowTexture *self);

/*
    This should ONLY be called when the target window is resized.
    Returns 0 on success.
*/
int window_texture_on_resize(WindowTexture *self);

unsigned int window_texture_get_opengl_texture_id(WindowTexture *self);

#endif /* WINDOW_TEXTURE_H */
