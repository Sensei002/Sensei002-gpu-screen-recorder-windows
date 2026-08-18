#include "../../../include/mgl/graphics/backend/wgl.h"
#include "../../../include/mgl/mgl.h"

#ifdef _WIN32

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    HGLRC context;
    HMODULE opengl32;

    bool pixel_format_set;
    bool alpha;
    bool request_depth_buffer;
    bool request_stencil_buffer;

    /* wgl entry points, loaded from opengl32.dll */
    HGLRC (WINAPI *wglCreateContext)(HDC hdc);
    BOOL  (WINAPI *wglDeleteContext)(HGLRC hglrc);
    BOOL  (WINAPI *wglMakeCurrent)(HDC hdc, HGLRC hglrc);
    BOOL  (WINAPI *wglSwapBuffers)(HDC hdc);
    PROC  (WINAPI *wglGetProcAddress)(LPCSTR lpszProc);

    /* extension entry points, resolved lazily via wglGetProcAddress */
    BOOL  (WINAPI *wglSwapIntervalEXT)(int interval);
} mgl_graphics_wgl;

static void mgl_graphics_wgl_deinit(mgl_graphics *self);

static bool mgl_graphics_wgl_load_entry_points(mgl_graphics_wgl *impl) {
    impl->wglCreateContext   = (HGLRC (WINAPI*)(HDC))GetProcAddress(impl->opengl32, "wglCreateContext");
    impl->wglDeleteContext   = (BOOL  (WINAPI*)(HGLRC))GetProcAddress(impl->opengl32, "wglDeleteContext");
    impl->wglMakeCurrent     = (BOOL  (WINAPI*)(HDC, HGLRC))GetProcAddress(impl->opengl32, "wglMakeCurrent");
    impl->wglSwapBuffers     = (BOOL  (WINAPI*)(HDC))GetProcAddress(impl->opengl32, "wglSwapBuffers");
    impl->wglGetProcAddress  = (PROC  (WINAPI*)(LPCSTR))GetProcAddress(impl->opengl32, "wglGetProcAddress");

    if(!impl->wglCreateContext || !impl->wglDeleteContext || !impl->wglMakeCurrent
        || !impl->wglSwapBuffers || !impl->wglGetProcAddress) {
        fprintf(stderr, "mgl error: mgl_graphics_wgl_load_entry_points: opengl32.dll is missing required wgl entry points\\n");
        return false;
    }
    return true;
}

static bool mgl_graphics_wgl_set_pixel_format(HDC hdc, bool alpha, bool request_depth_buffer, bool request_stencil_buffer) {
    PIXELFORMATDESCRIPTOR pfd = {
        .nSize = sizeof(pfd),
        .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER
            | (alpha ? PFD_SUPPORT_COMPOSITION : 0),
        .iPixelType = PFD_TYPE_RGBA,
        .cColorBits = 32,
        .cRedBits = 0, .cRedShift = 0,
        .cGreenBits = 0, .cGreenShift = 0,
        .cBlueBits = 0, .cBlueShift = 0,
        .cAlphaBits = alpha ? 8 : 0, .cAlphaShift = 0,
        .cAccumBits = 0, .cAccumRedBits = 0, .cAccumGreenBits = 0, .cAccumBlueBits = 0, .cAccumAlphaBits = 0,
        .cDepthBits = request_depth_buffer ? 24 : 0,
        .cStencilBits = request_stencil_buffer ? 8 : 0,
        .cAuxBuffers = 0,
        .iLayerType = PFD_MAIN_PLANE,
        .bReserved = 0,
        .dwLayerMask = 0, .dwVisibleMask = 0, .dwDamageMask = 0,
    };

    /* PFD_SUPPORT_COMPOSITION is required for DWM to composite the GL back
       buffer's alpha per-pixel (used together with the blur-behind empty
       region in the win32 window backend). Without it DWM drops the alpha
       and a transparent overlay shows opaque black. */

    int pixel_format = ChoosePixelFormat(hdc, &pfd);
    if(pixel_format == 0 && (alpha || request_depth_buffer || request_stencil_buffer)) {
        /* Retry without the extras (the Microsoft GDI generic implementation
           is limited; hardware ICDs accept the full request). */
        pfd.dwFlags &= ~PFD_SUPPORT_COMPOSITION;
        pfd.cAlphaBits = 0;
        pfd.cDepthBits = 0;
        pfd.cStencilBits = 0;
        pixel_format = ChoosePixelFormat(hdc, &pfd);
    }
    if(pixel_format == 0)
        return false;

    return SetPixelFormat(hdc, pixel_format, &pfd) != 0;
}

static bool mgl_graphics_wgl_make_context_current(mgl_graphics *self, mgl_window_handle window) {
    mgl_graphics_wgl *impl = self->impl;
    HDC hdc = GetDC((HWND)window);
    if(!hdc)
        return false;

    bool result = false;
    if(!impl->pixel_format_set) {
        if(!mgl_graphics_wgl_set_pixel_format(hdc, impl->alpha, impl->request_depth_buffer, impl->request_stencil_buffer)) {
            fprintf(stderr, "mgl error: mgl_graphics_wgl_make_context_current: failed to set pixel format\\n");
            ReleaseDC((HWND)window, hdc);
            return false;
        }
        impl->pixel_format_set = true;
    }

    if(!impl->context) {
        impl->context = impl->wglCreateContext(hdc);
        if(!impl->context) {
            fprintf(stderr, "mgl error: mgl_graphics_wgl_make_context_current: wglCreateContext failed\\n");
            ReleaseDC((HWND)window, hdc);
            return false;
        }
    }

    result = impl->wglMakeCurrent(hdc, impl->context) != 0;
    ReleaseDC((HWND)window, hdc);
    return result;
}

static void mgl_graphics_wgl_swap_buffers(mgl_graphics *self, mgl_window_handle window) {
    mgl_graphics_wgl *impl = self->impl;
    HDC hdc = GetDC((HWND)window);
    if(!hdc)
        return;

    impl->wglSwapBuffers(hdc);
    ReleaseDC((HWND)window, hdc);
}

static bool mgl_graphics_wgl_set_swap_interval(mgl_graphics *self, mgl_window_handle window, bool enabled) {
    (void)window;
    mgl_graphics_wgl *impl = self->impl;
    if(!impl->wglSwapIntervalEXT) {
        impl->wglSwapIntervalEXT = (BOOL (WINAPI*)(int))impl->wglGetProcAddress("wglSwapIntervalEXT");
        if(!impl->wglSwapIntervalEXT)
            return false;
    }
    return impl->wglSwapIntervalEXT(enabled ? 1 : 0) != 0;
}

static void* mgl_graphics_wgl_get_xvisual_info(mgl_graphics *self) {
    (void)self;
    return NULL;
}

static void* mgl_graphics_wgl_get_display(mgl_graphics *self) {
    (void)self;
    return NULL;
}

static void* mgl_graphics_wgl_get_context(mgl_graphics *self) {
    mgl_graphics_wgl *impl = self->impl;
    return impl->context;
}

bool mgl_graphics_wgl_init(mgl_graphics *self, const mgl_graphics_create_params *params) {
    mgl_graphics_wgl *impl = calloc(1, sizeof(mgl_graphics_wgl));
    if(!impl)
        return false;

    self->deinit = mgl_graphics_wgl_deinit;
    self->make_context_current = mgl_graphics_wgl_make_context_current;
    self->swap_buffers = mgl_graphics_wgl_swap_buffers;
    self->set_swap_interval = mgl_graphics_wgl_set_swap_interval;
    self->get_xvisual_info = mgl_graphics_wgl_get_xvisual_info;
    self->get_display = mgl_graphics_wgl_get_display;
    self->get_context = mgl_graphics_wgl_get_context;
    self->impl = impl;

    impl->alpha = params && params->alpha;
    impl->request_depth_buffer = params && params->request_depth_buffer;
    impl->request_stencil_buffer = params && params->request_stencil_buffer;

    impl->opengl32 = LoadLibraryA("opengl32.dll");
    if(!impl->opengl32) {
        fprintf(stderr, "mgl error: mgl_graphics_wgl_init: failed to load opengl32.dll\\n");
        mgl_graphics_wgl_deinit(self);
        return false;
    }

    if(!mgl_graphics_wgl_load_entry_points(impl)) {
        mgl_graphics_wgl_deinit(self);
        return false;
    }

    return true;
}

static void mgl_graphics_wgl_deinit(mgl_graphics *self) {
    mgl_graphics_wgl *impl = self->impl;
    if(!impl)
        return;

    if(impl->context) {
        impl->wglMakeCurrent(NULL, NULL);
        impl->wglDeleteContext(impl->context);
        impl->context = NULL;
    }

    if(impl->opengl32) {
        FreeLibrary(impl->opengl32);
        impl->opengl32 = NULL;
    }

    free(self->impl);
    self->impl = NULL;
}

#endif /* _WIN32 */
