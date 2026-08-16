#include "../../../include/mgl/graphics/backend/egl.h"
#include "../../../include/mgl/mgl.h"

#include <stdlib.h>
#include <stdio.h>

#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

static void mgl_graphics_egl_deinit(mgl_graphics *self);

typedef struct {
    EGLDisplay   display;
    EGLSurface   surface;
    EGLContext   context;
    EGLConfig   *configs;
    EGLConfig    ecfg;
    XVisualInfo *visual_info;
} mgl_graphics_egl;

static int32_t mgl_graphics_egl_get_config_attrib(mgl_graphics_egl *self, EGLConfig ecfg, int32_t attribute_name) {
    mgl_context *context = mgl_get_context();
    int32_t value = 0;
    context->gl.eglGetConfigAttrib(self->display, ecfg, attribute_name, &value);
    return value;
}

static bool xvisual_match_alpha(Display *dpy, XVisualInfo *visual_info, bool alpha) {
    XRenderPictFormat *pict_format = XRenderFindVisualFormat(dpy, visual_info->visual);
    if(!pict_format)
        return false;
    return (alpha && pict_format->direct.alpha > 0) || (!alpha && pict_format->direct.alpha == 0);
}

static bool mgl_graphics_egl_choose_config(mgl_graphics_egl *self, mgl_context *context, bool alpha, const mgl_graphics_create_params *params) {
    const int32_t alpha_size = alpha ? 8 : 0;
    const int32_t depth_size = params->request_depth_buffer ? 24 : 0;
    const int32_t stencil_size = params->request_stencil_buffer ? 8 : 0;
    
    self->configs = NULL;
    self->ecfg = NULL;
    self->visual_info = NULL;

    int32_t num_configs = 0;
    context->gl.eglGetConfigs(self->display, NULL, 0, &num_configs);
    if(num_configs == 0) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_choose_config: no configs found\n");
        return false;
    }

    self->configs = (EGLConfig*)calloc(num_configs, sizeof(EGLConfig));
    if(!self->configs) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_choose_config: failed to allocate %d configs\n", (int)num_configs);
        return false;
    }

    context->gl.eglGetConfigs(self->display, self->configs, num_configs, &num_configs);
    for(int i = 0; i < num_configs; i++) {
        self->ecfg = self->configs[i];

        if((mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_RENDERABLE_TYPE) & EGL_OPENGL_ES2_BIT) == 0)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_COLOR_BUFFER_TYPE) != EGL_RGB_BUFFER)
            continue;

        if(!(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_SURFACE_TYPE) & EGL_WINDOW_BIT))
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_RED_SIZE) != 8)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_GREEN_SIZE) != 8)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_BLUE_SIZE) != 8)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_ALPHA_SIZE) != alpha_size)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_DEPTH_SIZE) != depth_size)
            continue;

        if(mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_STENCIL_SIZE) != stencil_size)
            continue;

        if(context->window_system == MGL_WINDOW_SYSTEM_WAYLAND)
            break;

        XVisualInfo vi = {0};
        vi.visualid = mgl_graphics_egl_get_config_attrib(self, self->ecfg, EGL_NATIVE_VISUAL_ID);
        if(!vi.visualid)
            continue;

        int vis_count = 0;
        self->visual_info = XGetVisualInfo(context->connection, VisualIDMask, &vi, &vis_count);
        if(!self->visual_info)
            continue;

        if(xvisual_match_alpha(context->connection, self->visual_info, alpha)) {
            break;
        } else {
            XFree(self->visual_info);
            self->visual_info = NULL;
            self->ecfg = NULL;
        }
    }

    if(!self->ecfg) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_choose_config: no appropriate egl config found\n");
        return false;
    }

    if(context->window_system == MGL_WINDOW_SYSTEM_X11 && !self->visual_info) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_choose_config: no appropriate visual found\n");
        return false;
    }

    return true;
}

static bool mgl_graphics_egl_make_context_current(mgl_graphics *self, mgl_window_handle window) {
    (void)window;
    mgl_graphics_egl *impl = self->impl;
    mgl_context *context = mgl_get_context();

    if(!impl->surface) {
        impl->surface = context->gl.eglCreateWindowSurface(impl->display, impl->ecfg, (EGLNativeWindowType)window, NULL);
        if(!impl->surface) {
            fprintf(stderr, "mgl error: mgl_graphics_egl_make_context_current: failed to create window surface\n");
            return false;
        }
    }

    return context->gl.eglMakeCurrent(impl->display, impl->surface, impl->surface, impl->context) != 0;
}

static void mgl_graphics_egl_swap_buffers(mgl_graphics *self, mgl_window_handle window) {
    (void)window;
    mgl_graphics_egl *impl = self->impl;
    mgl_context *context = mgl_get_context();
    context->gl.eglSwapBuffers(impl->display, impl->surface);
}

static bool mgl_graphics_egl_set_swap_interval(mgl_graphics *self, mgl_window_handle window, bool enabled) {
    (void)window;
    mgl_graphics_egl *impl = self->impl;
    mgl_context *context = mgl_get_context();
    return context->gl.eglSwapInterval(impl->display, (int32_t)enabled) == 1;
}

static void* mgl_graphics_egl_get_xvisual_info(mgl_graphics *self) {
    mgl_graphics_egl *impl = self->impl;
    return impl->visual_info;
}

static void* mgl_graphics_egl_get_display(mgl_graphics *self) {
    mgl_graphics_egl *impl = self->impl;
    return impl->display;
}

static void* mgl_graphics_egl_get_context(mgl_graphics *self) {
    mgl_graphics_egl *impl = self->impl;
    return impl->context;
}

bool mgl_graphics_egl_init(mgl_graphics *self, const mgl_graphics_create_params *params) {
    mgl_graphics_egl *impl = calloc(1, sizeof(mgl_graphics_egl));
    if(!impl)
        return false;

    self->deinit = mgl_graphics_egl_deinit;
    self->make_context_current = mgl_graphics_egl_make_context_current;
    self->swap_buffers = mgl_graphics_egl_swap_buffers;
    self->set_swap_interval = mgl_graphics_egl_set_swap_interval;
    self->get_xvisual_info = mgl_graphics_egl_get_xvisual_info;
    self->get_display = mgl_graphics_egl_get_display;
    self->get_context = mgl_graphics_egl_get_context;
    self->impl = impl;

    const int32_t ctxattr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE, EGL_NONE
    };

    mgl_context *context = mgl_get_context();
    context->gl.eglBindAPI(EGL_OPENGL_API);

    impl->display = context->gl.eglGetDisplay((EGLNativeDisplayType)context->connection);
    if(!impl->display) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_init: eglGetDisplay failed\n");
        mgl_graphics_egl_deinit(self);
        return false;
    }

    if(!context->gl.eglInitialize(impl->display, NULL, NULL)) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_init: eglInitialize failed\n");
        mgl_graphics_egl_deinit(self);
        return false;
    }

    if(!mgl_graphics_egl_choose_config(impl, context, self->alpha, params)) {
        mgl_graphics_egl_deinit(self);
        return false;
    }
    
    impl->context = context->gl.eglCreateContext(impl->display, impl->ecfg, NULL, ctxattr);
    if(!impl->context) {
        fprintf(stderr, "mgl error: mgl_graphics_egl_init: failed to create egl context\n");
        mgl_graphics_egl_deinit(self);
        return false;
    }

    return true;
}

void mgl_graphics_egl_deinit(mgl_graphics *self) {
    mgl_graphics_egl *impl = self->impl;
    if(!impl)
        return;

    mgl_context *context = mgl_get_context();

    if(impl->visual_info) {
        XFree(impl->visual_info);
        impl->visual_info = NULL;
    }

    if(impl->configs) {
        free(impl->configs);
        impl->configs = NULL;
    }

    if(impl->context) {
        context->gl.eglMakeCurrent(impl->display, NULL, NULL, NULL);
        context->gl.eglDestroyContext(impl->display, impl->context);
        impl->context = NULL;
    }

    if(impl->surface) {
        context->gl.eglDestroySurface(impl->display, impl->surface);
        impl->surface = NULL;
    }

    if(impl->display) {
        context->gl.eglTerminate(impl->display);
        impl->display = NULL;
    }

    if(impl->visual_info) {
        XFree(impl->visual_info);
        impl->visual_info = NULL;
    }

    free(self->impl);
    self->impl = NULL;
}
