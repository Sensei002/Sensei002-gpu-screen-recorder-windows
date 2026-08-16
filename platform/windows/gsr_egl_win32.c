/* gsr_egl_win32.c — Windows (ANGLE-on-D3D11) implementation of the gsr_egl
 * GL table (Phase 5b, architecture §3.3 Option B).
 *
 * Fills the upstream gsr_egl struct (upstream/include/egl.h) from ANGLE's
 * libEGL.dll + libGLESv2.dll (MSYS2 package mingw-w64-x86_64-angleproject),
 * running ANGLE on a D3D11 device (hardware, WARP fallback). The EGL
 * context is surfaceless (EGL 1.5) — no native window is needed, matching
 * the windowless WGC capture model.
 *
 * Also implements:
 *   - gsr_egl_load_win32()/gsr_egl_unload_win32()  (the egl.c seam)
 *   - gl_get_gpu_info()      (utils.h; upstream's version lives in the
 *     X11/DRM-heavy utils.c, which the Windows build does not compile —
 *     same vendor-string logic, plus a DXGI-adapter fallback for
 *     unrecognized/software adapters)
 *   - the D3D11-texture import helpers (platform/include/egl_win32.h) via
 *     EGL_ANGLE_d3d_texture_client_buffer + EGL_ANGLE_device_d3d.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3f.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "egl_win32.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/log.h"
#include "../../upstream/include/utils.h"
#include "../../upstream/include/library_loader.h"
#include "../../upstream/include/window/window.h"
#include "dlfcn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* egl.h typedefs EGLDisplay/EGLConfig/... as void* but not the integer
   types or EGLDeviceEXT; ANGLE's headers define them pointer-sized. Same
   values the self-test uses. */
typedef intptr_t EGLint;
typedef intptr_t EGLAttrib;
typedef void *EGLDeviceEXT;

/* EGL query constants the loader needs that egl.h does not define. */
#ifndef EGL_EXTENSIONS
#define EGL_EXTENSIONS 0x3055
#endif
#ifndef EGL_VENDOR
#define EGL_VENDOR 0x3053
#endif

/* eglGetPlatformDisplayEXT / eglCreateDeviceANGLE / eglDestroyDeviceANGLE
   are exported by ANGLE's libEGL.dll directly (not dlsym-only). */
typedef EGLDisplay (*FUNC_eglGetPlatformDisplayEXT)(EGLint platform, void *native_display, const EGLint *attrib_list);
typedef EGLDeviceEXT (*FUNC_eglCreateDeviceANGLE)(EGLint device_type, void *native_device, const EGLAttrib *attrib_list);
typedef EGLint (*FUNC_eglDestroyDeviceANGLE)(EGLDeviceEXT device);

/* IID_IDXGIDevice — 54ec77fa-1377-44e6-8c32-88fd5f44c84c. Defined locally
   rather than referencing mingw-w64's IID_IDXGIDevice symbol (some DXGI
   IIDs are not linkable from libdxgi.a; see docs/upstream-porting-notes.md
   §3d). */
static const GUID GSR_IID_IDXGIDevice = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};

/* ---- D3D11 device ------------------------------------------------------- */

/* Creates a D3D11 device (hardware, WARP fallback) with BGRA support (the
   ANGLE interop requirement). Same policy as the WGC backend. */
static ID3D11Device *create_d3d11_device(void) {
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    const UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL got_level = D3D_FEATURE_LEVEL_10_0;
    ID3D11Device *device = NULL;

    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, create_flags,
        levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
        &device, &got_level, NULL);
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_egl_load_win32: hardware device unavailable (0x%08lx), using WARP", (unsigned long)hr);
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, create_flags,
            levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
            &device, &got_level, NULL);
    }
    if(FAILED(hr)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32: D3D11CreateDevice failed (0x%08lx)", (unsigned long)hr);
        return NULL;
    }
    return device;
}

/* ---- GPU info ----------------------------------------------------------- */

/* gl_get_gpu_info (utils.h) — Windows version. Same GL_VENDOR/GL_RENDERER
   logic as upstream (ANGLE reports "Google Inc. (NVIDIA)" etc.), with a
   DXGI-adapter fallback for software/unknown adapters so the loader does
   not fail on WARP (upstream fails hard on an unknown vendor). */
bool gl_get_gpu_info(gsr_egl *egl, gsr_gpu_info *info) {
    const char *gl_vendor = (const char*)egl->glGetString(GL_VENDOR);
    const char *gl_renderer = (const char*)egl->glGetString(GL_RENDERER);

    info->gpu_version = 0;
    info->is_steam_deck = false;
    info->vendor = GSR_GPU_VENDOR_UNKNOWN;

    if(gl_vendor) {
        if(strstr(gl_vendor, "AMD"))
            info->vendor = GSR_GPU_VENDOR_AMD;
        else if(strstr(gl_vendor, "Mesa") && gl_renderer && strstr(gl_renderer, "AMD"))
            info->vendor = GSR_GPU_VENDOR_AMD;
        else if(strstr(gl_vendor, "Intel"))
            info->vendor = GSR_GPU_VENDOR_INTEL;
        else if(strstr(gl_vendor, "NVIDIA"))
            info->vendor = GSR_GPU_VENDOR_NVIDIA;
        else if(strstr(gl_vendor, "Broadcom"))
            info->vendor = GSR_GPU_VENDOR_BROADCOM;
        else if(strstr(gl_vendor, "Mesa") && gl_renderer && strstr(gl_renderer, "Apple"))
            info->vendor = GSR_GPU_VENDOR_APPLE;
    }

    /* ANGLE on a software adapter ("Google Inc. (Microsoft)") is not
       recognized by the string checks; resolve the vendor from the D3D11
       adapter's PCI vendor id instead. */
    if(info->vendor == GSR_GPU_VENDOR_UNKNOWN && egl->d3d11_device) {
        IDXGIDevice *dxgi_device = NULL;
        if(((ID3D11Device*)egl->d3d11_device)->lpVtbl->QueryInterface((ID3D11Device*)egl->d3d11_device,
                &GSR_IID_IDXGIDevice, (void**)&dxgi_device) == S_OK) {
            IDXGIAdapter *adapter = NULL;
            if(dxgi_device->lpVtbl->GetAdapter(dxgi_device, &adapter) == S_OK) {
                DXGI_ADAPTER_DESC desc;
                if(adapter->lpVtbl->GetDesc(adapter, &desc) == S_OK) {
                    switch(desc.VendorId) {
                        case 0x10DE: info->vendor = GSR_GPU_VENDOR_NVIDIA; break;
                        case 0x1002: info->vendor = GSR_GPU_VENDOR_AMD; break;
                        case 0x8086: info->vendor = GSR_GPU_VENDOR_INTEL; break;
                        default: break;
                    }
                    if(info->vendor == GSR_GPU_VENDOR_UNKNOWN)
                        gsr_log(GSR_LOG_LEVEL_WARNING,
                            "gsr_egl_load_win32: unrecognized GPU vendor (DXGI vendor 0x%04x, GL vendor \"%s\") — treating as software/unknown",
                            (unsigned)desc.VendorId, gl_vendor ? gl_vendor : "?");
                }
                adapter->lpVtbl->Release(adapter);
            }
            dxgi_device->lpVtbl->Release(dxgi_device);
        }
    }

    if(info->vendor == GSR_GPU_VENDOR_UNKNOWN) {
        gsr_log(GSR_LOG_LEVEL_WARNING,
            "gsr_egl_load_win32: unknown gpu vendor%s%s (software rendering adapter?)",
            gl_vendor ? ": " : "", gl_vendor ? gl_vendor : "");
    }

    if(gl_renderer && info->vendor == GSR_GPU_VENDOR_NVIDIA)
        sscanf(gl_renderer, "%*s %*s %*s %d", &info->gpu_version);

    return true;
}

/* ---- egl/gl function loading -------------------------------------------- */

static bool gsr_egl_load_egl(gsr_egl *self, void *library) {
    const dlsym_assign required_dlsym[] = {
        { (void**)&self->eglGetError, "eglGetError" },
        { (void**)&self->eglGetDisplay, "eglGetDisplay" },
        { (void**)&self->eglInitialize, "eglInitialize" },
        { (void**)&self->eglTerminate, "eglTerminate" },
        { (void**)&self->eglChooseConfig, "eglChooseConfig" },
        { (void**)&self->eglCreateWindowSurface, "eglCreateWindowSurface" },
        { (void**)&self->eglCreateContext, "eglCreateContext" },
        { (void**)&self->eglMakeCurrent, "eglMakeCurrent" },
        { (void**)&self->eglCreateImage, "eglCreateImage" },
        { (void**)&self->eglDestroyContext, "eglDestroyContext" },
        { (void**)&self->eglDestroySurface, "eglDestroySurface" },
        { (void**)&self->eglDestroyImage, "eglDestroyImage" },
        { (void**)&self->eglSwapInterval, "eglSwapInterval" },
        { (void**)&self->eglSwapBuffers, "eglSwapBuffers" },
        { (void**)&self->eglBindAPI, "eglBindAPI" },
        { (void**)&self->eglGetProcAddress, "eglGetProcAddress" },
        { NULL, NULL }
    };
    return dlsym_load_list(library, required_dlsym);
}

static bool gsr_egl_load_gl(gsr_egl *self, void *library) {
    const dlsym_assign required_dlsym[] = {
        { (void**)&self->glGetError, "glGetError" },
        { (void**)&self->glGetString, "glGetString" },
        { (void**)&self->glFlush, "glFlush" },
        { (void**)&self->glFinish, "glFinish" },
        { (void**)&self->glClear, "glClear" },
        { (void**)&self->glClearColor, "glClearColor" },
        { (void**)&self->glGenTextures, "glGenTextures" },
        { (void**)&self->glDeleteTextures, "glDeleteTextures" },
        { (void**)&self->glActiveTexture, "glActiveTexture" },
        { (void**)&self->glBindTexture, "glBindTexture" },
        { (void**)&self->glBindImageTexture, "glBindImageTexture" },
        { (void**)&self->glTexParameteri, "glTexParameteri" },
        { (void**)&self->glTexParameteriv, "glTexParameteriv" },
        { (void**)&self->glTexParameterfv, "glTexParameterfv" },
        { (void**)&self->glTexImage2D, "glTexImage2D" },
        { (void**)&self->glTexSubImage2D, "glTexSubImage2D" },
        { (void**)&self->glTexStorage2D, "glTexStorage2D" },
        { (void**)&self->glGetTexImage, "glGetTexImage" },
        { (void**)&self->glGenFramebuffers, "glGenFramebuffers" },
        { (void**)&self->glBindFramebuffer, "glBindFramebuffer" },
        { (void**)&self->glDeleteFramebuffers, "glDeleteFramebuffers" },
        { (void**)&self->glMemoryBarrier, "glMemoryBarrier" },
        { (void**)&self->glViewport, "glViewport" },
        { (void**)&self->glFramebufferTexture2D, "glFramebufferTexture2D" },
        { (void**)&self->glDrawBuffers, "glDrawBuffers" },
        { (void**)&self->glCheckFramebufferStatus, "glCheckFramebufferStatus" },
        { (void**)&self->glBindBuffer, "glBindBuffer" },
        { (void**)&self->glGenBuffers, "glGenBuffers" },
        { (void**)&self->glBufferData, "glBufferData" },
        { (void**)&self->glBufferSubData, "glBufferSubData" },
        { (void**)&self->glDeleteBuffers, "glDeleteBuffers" },
        { (void**)&self->glGenVertexArrays, "glGenVertexArrays" },
        { (void**)&self->glBindVertexArray, "glBindVertexArray" },
        { (void**)&self->glDeleteVertexArrays, "glDeleteVertexArrays" },
        { (void**)&self->glCreateProgram, "glCreateProgram" },
        { (void**)&self->glCreateShader, "glCreateShader" },
        { (void**)&self->glAttachShader, "glAttachShader" },
        { (void**)&self->glBindAttribLocation, "glBindAttribLocation" },
        { (void**)&self->glCompileShader, "glCompileShader" },
        { (void**)&self->glLinkProgram, "glLinkProgram" },
        { (void**)&self->glShaderSource, "glShaderSource" },
        { (void**)&self->glUseProgram, "glUseProgram" },
        { (void**)&self->glGetProgramInfoLog, "glGetProgramInfoLog" },
        { (void**)&self->glGetShaderiv, "glGetShaderiv" },
        { (void**)&self->glGetShaderInfoLog, "glGetShaderInfoLog" },
        { (void**)&self->glDeleteProgram, "glDeleteProgram" },
        { (void**)&self->glDeleteShader, "glDeleteShader" },
        { (void**)&self->glGetProgramiv, "glGetProgramiv" },
        { (void**)&self->glVertexAttribPointer, "glVertexAttribPointer" },
        { (void**)&self->glEnableVertexAttribArray, "glEnableVertexAttribArray" },
        { (void**)&self->glDrawArrays, "glDrawArrays" },
        { (void**)&self->glEnable, "glEnable" },
        { (void**)&self->glDisable, "glDisable" },
        { (void**)&self->glBlendFunc, "glBlendFunc" },
        { (void**)&self->glPixelStorei, "glPixelStorei" },
        { (void**)&self->glGetUniformLocation, "glGetUniformLocation" },
        { (void**)&self->glUniform1f, "glUniform1f" },
        { (void**)&self->glUniform2f, "glUniform2f" },
        { (void**)&self->glUniform1i, "glUniform1i" },
        { (void**)&self->glUniform2i, "glUniform2i" },
        { (void**)&self->glUniformMatrix2fv, "glUniformMatrix2fv" },
        { (void**)&self->glUniformMatrix3fv, "glUniformMatrix3fv" },
        { (void**)&self->glDebugMessageCallback, "glDebugMessageCallback" },
        { (void**)&self->glScissor, "glScissor" },
        { (void**)&self->glReadPixels, "glReadPixels" },
        { (void**)&self->glMapBufferRange, "glMapBufferRange" },
        { (void**)&self->glUnmapBuffer, "glUnmapBuffer" },
        { (void**)&self->glGetIntegerv, "glGetIntegerv" },
        { NULL, NULL }
    };
    return dlsym_load_list(library, required_dlsym);
}

#define GL_DEBUG_TYPE_ERROR 0x824C
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
static void debug_callback(unsigned int source, unsigned int type, unsigned int id, unsigned int severity, int length, const char *message, const void *userParam) {
    (void)source;
    (void)id;
    (void)length;
    (void)userParam;
    if(severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        gsr_log(GSR_LOG_LEVEL_INFO, "gl callback: %s type = 0x%x, severity = 0x%x, message = %s", type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "", type, severity, message);
}

/* ---- loader ------------------------------------------------------------- */

bool gsr_egl_load_win32(gsr_egl *self, gsr_window *window, bool enable_debug) {
    memset(self, 0, sizeof(gsr_egl));
    self->context_type = GSR_GL_CONTEXT_TYPE_EGL;
    self->window = window;

    dlerror(); /* clear */
    self->egl_library = dlopen("libEGL.dll", RTLD_LAZY);
    if(!self->egl_library) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32: failed to load libEGL.dll (ANGLE), error: %s", dlerror() ? dlerror() : "?");
        goto fail;
    }

    self->gl_library = dlopen("libGLESv2.dll", RTLD_LAZY);
    if(!self->gl_library) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32: failed to load libGLESv2.dll (ANGLE), error: %s", dlerror() ? dlerror() : "?");
        goto fail;
    }

    if(!gsr_egl_load_egl(self, self->egl_library)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: missing required symbols in libEGL.dll");
        goto fail;
    }

    if(!gsr_egl_load_gl(self, self->gl_library)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: missing required symbols in libGLESv2.dll");
        goto fail;
    }

    /* Extension entry points (exported by libEGL.dll; dlsym'd by name). */
    FUNC_eglGetPlatformDisplayEXT eglGetPlatformDisplayEXT = (FUNC_eglGetPlatformDisplayEXT)dlsym(self->egl_library, "eglGetPlatformDisplayEXT");
    FUNC_eglCreateDeviceANGLE eglCreateDeviceANGLE = (FUNC_eglCreateDeviceANGLE)dlsym(self->egl_library, "eglCreateDeviceANGLE");
    self->glEGLImageTargetTexture2DOES = (FUNC_glEGLImageTargetTexture2DOES)self->eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if(!eglGetPlatformDisplayEXT || !eglCreateDeviceANGLE || !self->glEGLImageTargetTexture2DOES) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: ANGLE build lacks eglGetPlatformDisplayEXT/eglCreateDeviceANGLE/glEGLImageTargetTexture2DOES");
        goto fail;
    }

    /* 1. D3D11 device (hardware, WARP fallback). Capture backends read this
       via gsr_platform_egl_get_d3d11_device() so everything runs on one
       device and the WGC texture import is zero-copy. */
    self->d3d11_device = create_d3d11_device();
    if(!self->d3d11_device)
        goto fail;

    ID3D11Device *device = (ID3D11Device*)self->d3d11_device;
    device->lpVtbl->GetImmediateContext(device, (ID3D11DeviceContext**)&self->d3d11_device_context);

    /* 2. ANGLE device + platform display on that device
       (EGL_ANGLE_device_d3d + EGL_ANGLE_platform_angle). */
    self->egl_angle_device = eglCreateDeviceANGLE(EGL_D3D11_DEVICE_ANGLE, self->d3d11_device, NULL);
    if(!self->egl_angle_device) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: eglCreateDeviceANGLE failed (error 0x%x)", self->eglGetError());
        goto fail;
    }

    const EGLint platform_attrs[] = {
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D11_ANGLE,
        EGL_NONE
    };
    self->egl_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, self->egl_angle_device, platform_attrs);
    if(!self->egl_display) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: eglGetPlatformDisplayEXT failed (error 0x%x)", self->eglGetError());
        goto fail;
    }

    if(!self->eglInitialize(self->egl_display, NULL, NULL)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: eglInitialize failed (error 0x%x)", self->eglGetError());
        goto fail;
    }

    /* 3. ES3 context, surfaceless (no window surface needed for capture). */
    if(!self->eglBindAPI(EGL_OPENGL_ES_API)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: eglBindAPI failed (error 0x%x)", self->eglGetError());
        goto fail;
    }

    /* NOTE: the gsr_egl struct's fn ptrs take int32_t attrib lists (upstream
       egl.h), not EGLint — keep these arrays int32_t. */
    EGLConfig ecfg = NULL;
    int32_t num_config = 0;
    const int32_t config_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };
    if(!self->eglChooseConfig(self->egl_display, config_attrs, &ecfg, 1, &num_config) || num_config != 1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: failed to find a matching config (error 0x%x)", self->eglGetError());
        goto fail;
    }

    int32_t context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE, EGL_NONE, EGL_NONE
    };
    if(enable_debug) {
        context_attrs[2] = EGL_CONTEXT_OPENGL_DEBUG;
        context_attrs[3] = EGL_TRUE;
    }
    self->egl_context = self->eglCreateContext(self->egl_display, ecfg, NULL, context_attrs);
    if(!self->egl_context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: failed to create egl context (error 0x%x)", self->eglGetError());
        goto fail;
    }

    if(!self->eglMakeCurrent(self->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, self->egl_context)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_egl_load_win32 failed: failed to make surfaceless egl context current (error 0x%x)", self->eglGetError());
        goto fail;
    }

    /* 4. GPU info — tolerant of software/unknown adapters. */
    if(!gl_get_gpu_info(self, &self->gpu_info))
        goto fail;

    if(enable_debug) {
        self->glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        self->glDebugMessageCallback(debug_callback, NULL);
    }

    self->glEnable(GL_BLEND);
    self->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    self->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    self->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    self->eglSwapInterval(self->egl_display, 0); /* disable vsync */

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_egl_load_win32: ANGLE on D3D11 (%s), vendor %s, renderer %s",
        self->gpu_info.vendor == GSR_GPU_VENDOR_UNKNOWN ? "software adapter" : "GPU",
        (const char*)self->glGetString(GL_VENDOR), (const char*)self->glGetString(GL_RENDERER));

    return true;

    fail:
    gsr_egl_unload_win32(self);
    return false;
}

void gsr_egl_unload_win32(gsr_egl *self) {
    if(self->egl_context) {
        self->eglMakeCurrent(self->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, NULL);
        self->eglDestroyContext(self->egl_display, self->egl_context);
        self->egl_context = NULL;
    }

    if(self->egl_display) {
        self->eglTerminate(self->egl_display);
        self->egl_display = NULL;
    }

    if(self->egl_angle_device) {
        FUNC_eglDestroyDeviceANGLE eglDestroyDeviceANGLE = (FUNC_eglDestroyDeviceANGLE)
            (self->egl_library ? dlsym(self->egl_library, "eglDestroyDeviceANGLE") : NULL);
        if(eglDestroyDeviceANGLE)
            eglDestroyDeviceANGLE((EGLDeviceEXT)self->egl_angle_device);
        self->egl_angle_device = NULL;
    }

    if(self->d3d11_device_context) {
        ((ID3D11DeviceContext*)self->d3d11_device_context)->lpVtbl->Release((ID3D11DeviceContext*)self->d3d11_device_context);
        self->d3d11_device_context = NULL;
    }
    if(self->d3d11_device) {
        ((ID3D11Device*)self->d3d11_device)->lpVtbl->Release((ID3D11Device*)self->d3d11_device);
        self->d3d11_device = NULL;
    }

    if(self->egl_library) {
        dlclose(self->egl_library);
        self->egl_library = NULL;
    }
    if(self->gl_library) {
        dlclose(self->gl_library);
        self->gl_library = NULL;
    }

    memset(self, 0, sizeof(gsr_egl));
}

/* ---- shared device accessor --------------------------------------------- */

void *gsr_platform_egl_get_d3d11_device(gsr_egl *egl) {
    if(!egl || !egl->d3d11_device)
        return NULL;
    ID3D11Device *device = (ID3D11Device*)egl->d3d11_device;
    device->lpVtbl->AddRef(device);
    return device;
}

/* ---- D3D11 texture import ------------------------------------------------ */

typedef struct gsr_egl_imported_texture {
    EGLImage image;         /* current EGL image backing the GL texture */
    void *last_texture;     /* the ID3D11Texture2D* the image was created from */
    unsigned int texture_id; /* GL_TEXTURE_2D (stable across rebinds) */
} gsr_egl_imported_texture;

/* (Re)binds |imp| to |texture|: destroys the previous EGL image, creates a
   new one from the D3D11 texture (EGL_ANGLE_d3d_texture_client_buffer) and
   redefines the GL texture with it. */
static bool imported_texture_bind(gsr_egl *egl, gsr_egl_imported_texture *imp, void *texture) {
    if(imp->image)
        egl->eglDestroyImage(egl->egl_display, imp->image);
    imp->image = egl->eglCreateImage(egl->egl_display, EGL_NO_CONTEXT,
        EGL_D3D_TEXTURE_ANGLE, (EGLClientBuffer)texture, NULL);
    if(!imp->image) {
        imp->last_texture = NULL;
        return false;
    }
    egl->glBindTexture(GL_TEXTURE_2D, imp->texture_id);
    egl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)imp->image);
    egl->glBindTexture(GL_TEXTURE_2D, 0);
    imp->last_texture = texture;
    return true;
}

void *gsr_platform_egl_import_texture(gsr_egl *egl, void *texture) {
    if(!egl || !texture || !egl->glEGLImageTargetTexture2DOES)
        return NULL;

    gsr_egl_imported_texture *imp = (gsr_egl_imported_texture*)calloc(1, sizeof(*imp));
    if(!imp)
        return NULL;

    egl->glGenTextures(1, &imp->texture_id);
    if(!imp->texture_id) {
        free(imp);
        return NULL;
    }

    if(!imported_texture_bind(egl, imp, texture)) {
        egl->glDeleteTextures(1, &imp->texture_id);
        free(imp);
        return NULL;
    }
    return imp;
}

bool gsr_platform_egl_update_texture(gsr_egl *egl, void *handle, void *texture) {
    if(!egl || !handle || !texture)
        return false;
    gsr_egl_imported_texture *imp = (gsr_egl_imported_texture*)handle;
    if(imp->last_texture == texture)
        return true;
    return imported_texture_bind(egl, imp, texture);
}

unsigned int gsr_platform_egl_texture_id(gsr_egl *egl, void *handle) {
    (void)egl;
    if(!handle)
        return 0;
    return ((gsr_egl_imported_texture*)handle)->texture_id;
}

void gsr_platform_egl_destroy_imported_texture(gsr_egl *egl, void *handle) {
    if(!egl || !handle)
        return;
    gsr_egl_imported_texture *imp = (gsr_egl_imported_texture*)handle;
    if(imp->image)
        egl->eglDestroyImage(egl->egl_display, imp->image);
    if(imp->texture_id)
        egl->glDeleteTextures(1, &imp->texture_id);
    free(imp);
}
