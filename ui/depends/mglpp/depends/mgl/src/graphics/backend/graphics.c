#include "../../../include/mgl/graphics/backend/graphics.h"
#include "../../../include/mgl/graphics/backend/glx.h"
#include "../../../include/mgl/graphics/backend/egl.h"
#ifdef _WIN32
#include "../../../include/mgl/graphics/backend/wgl.h"
#endif
#include "../../../include/mgl/mgl.h"

#include <string.h>
#include <stdio.h>

bool mgl_graphics_init(mgl_graphics *self, const mgl_graphics_create_params *params) {
    memset(self, 0, sizeof(*self));
#ifdef _WIN32
    self->graphics_api = params ? params->graphics_api : MGL_GRAPHICS_API_WGL;
#else
    self->graphics_api = params ? params->graphics_api : MGL_GRAPHICS_API_EGL;
#endif
    self->alpha = params && params->alpha;

    switch(self->graphics_api) {
        case MGL_GRAPHICS_API_GLX:
#ifndef _WIN32
            return mgl_graphics_glx_init(self, params);
#else
            fprintf(stderr, "mgl error: mgl_graphics_init: GLX is not supported on Windows, use MGL_GRAPHICS_API_WGL\n");
            return false;
#endif
        case MGL_GRAPHICS_API_EGL:
#ifndef _WIN32
            return mgl_graphics_egl_init(self, params);
#else
            fprintf(stderr, "mgl error: mgl_graphics_init: EGL is not supported on Windows yet, use MGL_GRAPHICS_API_WGL\n");
            return false;
#endif
        case MGL_GRAPHICS_API_WGL:
#ifdef _WIN32
            return mgl_graphics_wgl_init(self, params);
#else
            fprintf(stderr, "mgl error: mgl_graphics_init: WGL is only supported on Windows\n");
            return false;
#endif
    }
    return false;
}

void mgl_graphics_deinit(mgl_graphics *self) {
    if(self->deinit)
        self->deinit(self);
}

bool mgl_graphics_make_context_current(mgl_graphics *self, mgl_window_handle window) {
    const bool result = self->make_context_current(self, window);
    if(result) {
        mgl_context *context = mgl_get_context();
        context->gl.glEnable(GL_TEXTURE_2D);
        context->gl.glEnable(GL_BLEND);
        context->gl.glEnable(GL_SCISSOR_TEST);
        context->gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        context->gl.glEnableClientState(GL_VERTEX_ARRAY);
        context->gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        context->gl.glEnableClientState(GL_COLOR_ARRAY);
        context->gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        context->gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
    return result;
}

void mgl_graphics_swap_buffers(mgl_graphics *self, mgl_window_handle window) {
    self->swap_buffers(self, window);
}

bool mgl_graphics_set_swap_interval(mgl_graphics *self, mgl_window_handle window, bool enabled) {
    return self->set_swap_interval(self, window, enabled);
}

void* mgl_graphics_get_xvisual_info(mgl_graphics *self) {
    return self->get_xvisual_info(self);
}

void* mgl_graphics_get_display(mgl_graphics *self) {
    if(self->get_display)
        return self->get_display(self);
    else
        return NULL;
}

void* mgl_graphics_get_context(mgl_graphics *self) {
    if(self->get_context)
        return self->get_context(self);
    else
        return NULL;
}
