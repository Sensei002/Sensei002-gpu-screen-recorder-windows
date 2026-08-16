#ifndef WINDOW_TEXTURE_H
#define WINDOW_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct _XDisplay Display;

typedef void (*FUNC_glEGLImageTargetTexture2DOES)(unsigned int target, void *image);
typedef struct {
    int32_t (*eglGetError)(void);
    void* (*eglCreateImage)(void *dpy, void *ctx, unsigned int target, void *buffer, const intptr_t *attrib_list);
    unsigned int (*eglDestroyImage)(void *dpy, void *image);
    FUNC_glEGLImageTargetTexture2DOES glEGLImageTargetTexture2DOES;
} egl_functions;

typedef struct {
    Display *display;
    void *egl_display;
    uint64_t window;
    uint64_t pixmap;
    unsigned int texture_id;
    int redirected;

    egl_functions egl_funcs;
} WindowTexture;

/* Returns 0 on success */
int window_texture_init(WindowTexture *window_texture, Display *display, void *egl_display, uint64_t window, egl_functions egl_funcs);
void window_texture_deinit(WindowTexture *self);

/*
    This should ONLY be called when the target window is resized.
    Returns 0 on success.
*/
int window_texture_on_resize(WindowTexture *self);

unsigned int window_texture_get_opengl_texture_id(WindowTexture *self);

#ifdef __cplusplus
}
#endif

#endif /* WINDOW_TEXTURE_H */
