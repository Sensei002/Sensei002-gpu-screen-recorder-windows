#include "../../../include/mgl/graphics/backend/glx.h"
#include "../../../include/mgl/mgl.h"

#include <stdlib.h>
#include <stdio.h>

#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

static void mgl_graphics_glx_deinit(mgl_graphics *self);

typedef struct {
    GLXContext   glx_context;
    GLXFBConfig *fbconfigs;
    GLXFBConfig  fbconfig;
    XVisualInfo *visual_info;
} mgl_graphics_glx;

static bool xvisual_match_alpha(Display *dpy, XVisualInfo *visual_info, bool alpha) {
    XRenderPictFormat *pict_format = XRenderFindVisualFormat(dpy, visual_info->visual);
    if(!pict_format)
        return false;
    return (alpha && pict_format->direct.alphaMask > 0) || (!alpha && pict_format->direct.alphaMask == 0);
}

static bool mgl_graphics_glx_choose_config(mgl_graphics_glx *self, mgl_context *context, bool alpha, const mgl_graphics_create_params *params) {
    const int attr[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_DOUBLEBUFFER, True,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, alpha ? 8 : 0,
        GLX_DEPTH_SIZE, params->request_depth_buffer ? 24 : 0,
        GLX_STENCIL_SIZE, params->request_stencil_buffer ? 8 : 0,
        None
    };

    self->fbconfigs = NULL;
    self->visual_info = NULL;
    self->fbconfig = NULL;

    int numfbconfigs = 0;
    self->fbconfigs = context->gl.glXChooseFBConfig(context->connection, DefaultScreen(context->connection), attr, &numfbconfigs);
    for(int i = 0; i < numfbconfigs; i++) {
        self->visual_info = (XVisualInfo*)context->gl.glXGetVisualFromFBConfig(context->connection, self->fbconfigs[i]);
        if(!self->visual_info)
            continue;

        if(xvisual_match_alpha(context->connection, self->visual_info, alpha)) {
            self->fbconfig = self->fbconfigs[i];
            break;
        } else {
            XFree(self->visual_info);
            self->visual_info = NULL;
            self->fbconfig = NULL;
        }
    }

    if(!self->fbconfig) {
        fprintf(stderr, "mgl error: mgl_graphics_glx_choose_config: no appropriate glx config found\n");
        return false;
    }

    if(!self->visual_info) {
        fprintf(stderr, "mgl error: mgl_graphics_glx_choose_config: no appropriate visual found\n");
        return false;
    }

    return true;
}

static bool mgl_graphics_glx_make_context_current(mgl_graphics *self, mgl_window_handle window) {
    mgl_graphics_glx *impl = self->impl;
    mgl_context *context = mgl_get_context();
    return context->gl.glXMakeContextCurrent(context->connection, (GLXDrawable)window, (GLXDrawable)window, impl->glx_context) != 0;
}

static void mgl_graphics_glx_swap_buffers(mgl_graphics *self, mgl_window_handle window) {
    (void)self;
    mgl_context *context = mgl_get_context();
    context->gl.glXSwapBuffers(context->connection, (GLXDrawable)window);
}

/* TODO: Use gl OML present for other platforms than nvidia? nvidia doesn't support present yet */
/* TODO: check for glx swap control extension string (GLX_EXT_swap_control, etc) */
static bool mgl_graphics_glx_set_swap_interval(mgl_graphics *self, mgl_window_handle window, bool enabled) {
    (void)self;
    mgl_context *context = mgl_get_context();

    int result = 0;
    if(context->gl.glXSwapIntervalEXT) {
        context->gl.glXSwapIntervalEXT(context->connection, (GLXDrawable)window, enabled);
    } else if(context->gl.glXSwapIntervalMESA) {
        result = context->gl.glXSwapIntervalMESA(enabled);
    } else if(context->gl.glXSwapIntervalSGI) {
        result = context->gl.glXSwapIntervalSGI(enabled);
    } else {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "mgl warning: setting vertical sync not supported\n");
        }
    }

    if(result != 0)
        fprintf(stderr, "mgl warning: setting vertical sync failed\n");

    return result == 0;
}

static void* mgl_graphics_glx_get_xvisual_info(mgl_graphics *self) {
    mgl_graphics_glx *impl = self->impl;
    return impl->visual_info;
}

bool mgl_graphics_glx_init(mgl_graphics *self, const mgl_graphics_create_params *params) {
    mgl_graphics_glx *impl = calloc(1, sizeof(mgl_graphics_glx));
    if(!impl)
        return false;

    self->deinit = mgl_graphics_glx_deinit;
    self->make_context_current = mgl_graphics_glx_make_context_current;
    self->swap_buffers = mgl_graphics_glx_swap_buffers;
    self->set_swap_interval = mgl_graphics_glx_set_swap_interval;
    self->get_xvisual_info = mgl_graphics_glx_get_xvisual_info;
    self->impl = impl;

    mgl_context *context = mgl_get_context();
    if(!mgl_graphics_glx_choose_config(impl, context, self->alpha, params)) {
        mgl_graphics_glx_deinit(self);
        return false;
    }

    impl->glx_context = context->gl.glXCreateNewContext(context->connection, impl->fbconfig, GLX_RGBA_TYPE, 0, True);
    if(!impl->glx_context) {
        fprintf(stderr, "mgl error: mgl_graphics_glx_init: glXCreateContext failed\n");
        mgl_graphics_glx_deinit(self);
        return false;
    }

    return true;
}

void mgl_graphics_glx_deinit(mgl_graphics *self) {
    mgl_graphics_glx *impl = self->impl;
    if(!impl)
        return;

    mgl_context *context = mgl_get_context();

    if(impl->glx_context) {
        context->gl.glXMakeContextCurrent(context->connection, None, None, NULL);
        context->gl.glXDestroyContext(context->connection, impl->glx_context);
        impl->glx_context = NULL;
    }

    if(impl->visual_info) {
        XFree(impl->visual_info);
        impl->visual_info = NULL;
    }

    if(impl->fbconfigs) {
        XFree(impl->fbconfigs);
        impl->fbconfigs = NULL;
    }

    impl->fbconfig = NULL;

    free(self->impl);
    self->impl = NULL;
}
