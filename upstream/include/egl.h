#ifndef GSR_EGL_H
#define GSR_EGL_H

/* OpenGL EGL library with a hidden window context (to allow using the opengl functions) */

/* Windows port modification (see docs/upstream-porting-notes.md): on Windows the
   X11 headers are not available, so the handful of X11 types this header uses
   (XID, Bool, Display, Window) are provided directly. The Linux build is unchanged.

   Phase 5b also adds the ANGLE-on-D3D11 pieces: the EGL_ANGLE_* platform
   constants used by gsr_egl_load_win32() (platform/windows/gsr_egl_win32.c),
   EGL_D3D11_TEXTURE_ANGLE for the zero-copy WGC texture import
   (EGL_ANGLE_d3d_texture_client_buffer), and the Windows-only gsr_egl fields
   that hold the shared D3D11 device/context and the ANGLE EGLDeviceEXT. */
#if defined(_WIN32)
#include <stdint.h>
typedef unsigned long XID;   /* X11-compatible: 64-bit on x64 */
typedef unsigned long Window;
typedef struct _XDisplay Display;
typedef int Bool;

/* EGL 1.5 sentinels (used by the surfaceless-context path on Windows). */
#ifndef EGL_NO_DISPLAY
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#endif
#ifndef EGL_NO_CONTEXT
#define EGL_NO_CONTEXT ((EGLContext)0)
#endif
#ifndef EGL_NO_SURFACE
#define EGL_NO_SURFACE ((EGLSurface)0)
#endif
#ifndef EGL_NO_IMAGE
#define EGL_NO_IMAGE ((EGLImage)0)
#endif

/* ANGLE platform display (EGL_ANGLE_platform_angle) and the
   device-based display path used on Windows (EGL_EXT_platform_device +
   EGL_ANGLE_device_d3d): wrap the shared D3D11 device with
   eglCreateDeviceANGLE(EGL_D3D11_DEVICE_ANGLE, ...) and hand the resulting
   EGLDeviceEXT to eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, ...).
   Values verified against the MSYS2 angleproject headers / ANGLE's
   EGL_ANGLE_device_d3d spec (docs/upstream-porting-notes.md §3f). */
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#define EGL_D3D11_DEVICE_ANGLE 0x33A1

/* EGL_ANGLE_d3d_texture_client_buffer / EGL_ANGLE_image_d3d11_texture:
   import a D3D11 texture as a GL_TEXTURE_2D sibling with no copy when the
   display shares the device (the WGC capture import, architecture §3.3
   Option B). NOTE: for eglCreateImage the D3D11 target is
   EGL_D3D11_TEXTURE_ANGLE (0x3484) — EGL_D3D_TEXTURE_ANGLE (0x33A3) is only
   a client-buffer type for eglCreatePbufferFromClientBuffer and is rejected
   as an image target with EGL_BAD_PARAMETER (docs/upstream-porting-notes.md
   §3f). */
#define EGL_D3D11_TEXTURE_ANGLE 0x3484
#define EGL_D3D_TEXTURE_ANGLE 0x33A3
#define EGL_TEXTURE_ANGLE 0x33A4
#else
#include <X11/X.h>
#include <X11/Xutil.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include "vec2.h"
#include "defs.h"

typedef struct gsr_window gsr_window;

#ifdef _WIN64
typedef signed   long long int khronos_intptr_t;
typedef unsigned long long int khronos_uintptr_t;
typedef signed   long long int khronos_ssize_t;
typedef unsigned long long int khronos_usize_t;
#else
typedef signed   long  int     khronos_intptr_t;
typedef unsigned long  int     khronos_uintptr_t;
typedef signed   long  int     khronos_ssize_t;
typedef unsigned long  int     khronos_usize_t;
#endif

typedef void* EGLDisplay;
typedef void* EGLNativeDisplayType;
typedef uintptr_t EGLNativeWindowType;
typedef uintptr_t EGLNativePixmapType;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLClientBuffer;
typedef void* EGLImage;
typedef void* EGLImageKHR;
typedef void *GLeglImageOES;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef struct __GLXcontextRec *GLXContext;
typedef XID GLXDrawable;
typedef void(*__GLXextFuncPtr)(void);

#define EGL_SUCCESS                             0x3000
#define EGL_BUFFER_SIZE                         0x3020
#define EGL_RENDERABLE_TYPE                     0x3040
#define EGL_OPENGL_API                          0x30A2
#define EGL_OPENGL_ES_API                       0x30A0
#define EGL_OPENGL_BIT                          0x0008
#define EGL_OPENGL_ES_BIT                       0x0001
#define EGL_OPENGL_ES2_BIT                      0x0004
#define EGL_OPENGL_ES3_BIT                      0x00000040
#define EGL_NONE                                0x3038
#define EGL_CONTEXT_CLIENT_VERSION              0x3098
#define EGL_CONTEXT_OPENGL_DEBUG                0x31B0
#define EGL_BACK_BUFFER                         0x3084
#define EGL_GL_TEXTURE_2D                       0x30B1
#define EGL_TRUE                                1
#define EGL_IMAGE_PRESERVED_KHR                 0x30D2
#define EGL_NATIVE_PIXMAP_KHR                   0x30B0
#define EGL_LINUX_DRM_FOURCC_EXT                0x3271
#define EGL_WIDTH                               0x3057
#define EGL_HEIGHT                              0x3056
#define EGL_DMA_BUF_PLANE0_FD_EXT               0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT           0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT            0x3274
#define EGL_DMA_BUF_PLANE1_FD_EXT               0x3275
#define EGL_DMA_BUF_PLANE1_OFFSET_EXT           0x3276
#define EGL_DMA_BUF_PLANE1_PITCH_EXT            0x3277
#define EGL_DMA_BUF_PLANE2_FD_EXT               0x3278
#define EGL_DMA_BUF_PLANE2_OFFSET_EXT           0x3279
#define EGL_DMA_BUF_PLANE2_PITCH_EXT            0x327A
#define EGL_DMA_BUF_PLANE3_FD_EXT               0x3440
#define EGL_DMA_BUF_PLANE3_OFFSET_EXT           0x3441
#define EGL_DMA_BUF_PLANE3_PITCH_EXT            0x3442
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT      0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT      0x3444
#define EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT      0x3445
#define EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT      0x3446
#define EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT      0x3447
#define EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT      0x3448
#define EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT      0x3449
#define EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT      0x344A
#define EGL_LINUX_DMA_BUF_EXT                   0x3270
#define EGL_RED_SIZE                            0x3024
#define EGL_ALPHA_SIZE                          0x3021
#define EGL_BLUE_SIZE                           0x3022
#define EGL_GREEN_SIZE                          0x3023
#define EGL_DEVICE_EXT                          0x322C
#define EGL_DRM_DEVICE_FILE_EXT                 0x3233

#define GL_FLOAT                                0x1406
#define GL_FALSE                                0
#define GL_TRUE                                 1
#define GL_TRIANGLES                            0x0004
#define GL_TEXTURE_2D                           0x0DE1
#define GL_TEXTURE_EXTERNAL_OES                 0x8D65
#define GL_RED                                  0x1903
#define GL_GREEN                                0x1904
#define GL_BLUE                                 0x1905
#define GL_ALPHA                                0x1906
#define GL_TEXTURE_SWIZZLE_RGBA                 0x8E46
#define GL_RG                                   0x8227
#define GL_RGB                                  0x1907
#define GL_RGBA                                 0x1908
#define GL_RGB8                                 0x8051
#define GL_RGBA8                                0x8058
#define GL_RGBA16                               0x805B
#define GL_R8                                   0x8229
#define GL_RG8                                  0x822B
#define GL_R16                                  0x822A
#define GL_RG16                                 0x822C
#define GL_RGB16                                0x8054
#define GL_RGBA32F                              0x8814
#define GL_RGB32F                               0x8815
#define GL_UNSIGNED_BYTE                        0x1401
#define GL_UNSIGNED_SHORT                       0x1403
#define GL_COLOR_BUFFER_BIT                     0x00004000
#define GL_TEXTURE_WRAP_S                       0x2802
#define GL_TEXTURE_WRAP_T                       0x2803
#define GL_TEXTURE_MAG_FILTER                   0x2800
#define GL_TEXTURE_MIN_FILTER                   0x2801
#define GL_NEAREST                              0x2600
#define GL_CLAMP_TO_EDGE                        0x812F
#define GL_LINEAR                               0x2601
#define GL_FRAMEBUFFER                          0x8D40
#define GL_COLOR_ATTACHMENT0                    0x8CE0
#define GL_FRAMEBUFFER_COMPLETE                 0x8CD5
#define GL_DYNAMIC_DRAW                         0x88E8
#define GL_ARRAY_BUFFER                         0x8892
#define GL_BLEND                                0x0BE2
#define GL_SRC_ALPHA                            0x0302
#define GL_ONE_MINUS_SRC_ALPHA                  0x0303
#define GL_DEBUG_OUTPUT_SYNCHRONOUS             0x8242
#define GL_SCISSOR_TEST                         0x0C11
#define GL_PACK_ALIGNMENT                       0x0D05
#define GL_UNPACK_ALIGNMENT                     0x0CF5
#define GL_TEXTURE0                             0x84C0
#define GL_TEXTURE1                             0x84C1
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT      0x00000020
#define GL_ALL_BARRIER_BITS                     0xFFFFFFFF
#define GL_PIXEL_UNPACK_BUFFER                  0x88EC
#define GL_MAP_WRITE_BIT                        0x0002

#define GL_VENDOR                               0x1F00
#define GL_RENDERER                             0x1F01
#define GL_VERSION                              0x1F02

#define GL_COMPILE_STATUS                       0x8B81
#define GL_INFO_LOG_LENGTH                      0x8B84
#define GL_FRAGMENT_SHADER                      0x8B30
#define GL_VERTEX_SHADER                        0x8B31
#define GL_COMPILE_STATUS                       0x8B81
#define GL_LINK_STATUS                          0x8B82

typedef unsigned int (*FUNC_eglExportDMABUFImageQueryMESA)(EGLDisplay dpy, EGLImageKHR image, int *fourcc, int *num_planes, uint64_t *modifiers);
typedef unsigned int (*FUNC_eglExportDMABUFImageMESA)(EGLDisplay dpy, EGLImageKHR image, int *fds, int32_t *strides, int32_t *offsets);
typedef void (*FUNC_glEGLImageTargetTexture2DOES)(unsigned int target, GLeglImageOES image);
typedef GLXContext (*FUNC_glXCreateContextAttribsARB)(Display *dpy, GLXFBConfig config, GLXContext share_context, Bool direct, const int *attrib_list);
typedef void (*FUNC_glXSwapIntervalEXT)(Display * dpy, GLXDrawable drawable, int interval);
typedef int (*FUNC_glXSwapIntervalMESA)(unsigned int interval);
typedef int (*FUNC_glXSwapIntervalSGI)(int interval);
typedef void (*GLDEBUGPROC)(unsigned int source, unsigned int type, unsigned int id, unsigned int severity, int length, const char *message, const void *userParam);
typedef int (*FUNC_eglQueryDisplayAttribEXT)(EGLDisplay dpy, int32_t attribute, intptr_t *value);
typedef const char* (*FUNC_eglQueryDeviceStringEXT)(void *device, int32_t name);
typedef int (*FUNC_eglQueryDmaBufModifiersEXT)(EGLDisplay dpy, int32_t format, int32_t max_modifiers, uint64_t *modifiers, int *external_only, int32_t *num_modifiers);
typedef void (*FUNC_glCreateMemoryObjectsEXT)(int n, unsigned int *memoryObjects);
typedef void (*FUNC_glImportMemoryFdEXT)(unsigned int memory, uint64_t size, unsigned int handleType, int fd);
typedef unsigned char (*FUNC_glIsMemoryObjectEXT)(unsigned int memoryObject);
typedef void (*FUNC_glTexStorageMem2DEXT)(unsigned int target, int levels, unsigned int internalFormat, int width, int height, unsigned int memory, uint64_t offset);
typedef void (*FUNC_glBufferStorageMemEXT)(unsigned int target, ssize_t size, unsigned int memory, uint64_t offset);
typedef void (*FUNC_glNamedBufferStorageMemEXT)(unsigned int buffer, ssize_t size, unsigned int memory, uint64_t offset);
typedef void (*FUNC_glMemoryObjectParameterivEXT)(unsigned int memoryObject, unsigned int pname, const int *params);
typedef void (*FUNC_glGenSemaphoresEXT)(int n, unsigned int *semaphores);
typedef void (*FUNC_glImportSemaphoreFdEXT)(unsigned int semaphore, unsigned int handleType, int fd);
typedef void (*FUNC_glSignalSemaphoreEXT)(unsigned int semaphore, unsigned int numBufferBarriers, const unsigned int *buffers, unsigned int numTextureBarriers, const unsigned int *textures, const unsigned int *dstLayouts);

typedef enum {
    GSR_GL_CONTEXT_TYPE_EGL,
    GSR_GL_CONTEXT_TYPE_GLX
} gsr_gl_context_type;

typedef struct gsr_egl gsr_egl;
struct gsr_egl {
    void *egl_library;
    void *glx_library;
    void *gl_library;

    gsr_gl_context_type context_type;
    gsr_window *window;

    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    const char *dri_card_path;

#ifdef _WIN32
    /* Phase 5b (architecture §3.3 Option B): the D3D11 device/context that
       ANGLE runs on and that capture backends share (WGC's frame pool must
       use the SAME device for a zero-copy EGL_ANGLE_d3d_texture_client_buffer
       import). Owned by the Windows gsr_egl loader. */
    void *d3d11_device;       /* ID3D11Device* */
    void *d3d11_device_context; /* ID3D11DeviceContext* */
    void *egl_angle_device;   /* EGLDeviceEXT (EGL_ANGLE_device_d3d) */
#endif

    void *glx_context;
    void *glx_fb_config;

    gsr_gpu_info gpu_info;

    char card_path[128];
    int vulkan_device_index;

    int32_t (*eglGetError)(void);
    EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType display_id);
    unsigned int (*eglInitialize)(EGLDisplay dpy, int32_t *major, int32_t *minor);
    unsigned int (*eglTerminate)(EGLDisplay dpy);
    unsigned int (*eglChooseConfig)(EGLDisplay dpy, const int32_t *attrib_list, EGLConfig *configs, int32_t config_size, int32_t *num_config);
    EGLSurface (*eglCreateWindowSurface)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const int32_t *attrib_list);
    EGLContext (*eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const int32_t *attrib_list);
    unsigned int (*eglMakeCurrent)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
    EGLImage (*eglCreateImage)(EGLDisplay dpy, EGLContext ctx, unsigned int target, EGLClientBuffer buffer, const intptr_t *attrib_list);
    unsigned int (*eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
    unsigned int (*eglDestroySurface)(EGLDisplay dpy, EGLSurface surface);
    unsigned int (*eglDestroyImage)(EGLDisplay dpy, EGLImage image);
    unsigned int (*eglSwapInterval)(EGLDisplay dpy, int32_t interval);
    unsigned int (*eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
    unsigned int (*eglBindAPI)(unsigned int api);
    __eglMustCastToProperFunctionPointerType (*eglGetProcAddress)(const char *procname);

    FUNC_eglExportDMABUFImageQueryMESA eglExportDMABUFImageQueryMESA;
    FUNC_eglExportDMABUFImageMESA eglExportDMABUFImageMESA;
    FUNC_glEGLImageTargetTexture2DOES glEGLImageTargetTexture2DOES;
    FUNC_eglQueryDisplayAttribEXT eglQueryDisplayAttribEXT;
    FUNC_eglQueryDeviceStringEXT eglQueryDeviceStringEXT;
    FUNC_eglQueryDmaBufModifiersEXT eglQueryDmaBufModifiersEXT;
    FUNC_glCreateMemoryObjectsEXT glCreateMemoryObjectsEXT;
    FUNC_glImportMemoryFdEXT glImportMemoryFdEXT;
    FUNC_glIsMemoryObjectEXT glIsMemoryObjectEXT;
    FUNC_glTexStorageMem2DEXT glTexStorageMem2DEXT;
    FUNC_glBufferStorageMemEXT glBufferStorageMemEXT;
    FUNC_glNamedBufferStorageMemEXT glNamedBufferStorageMemEXT;
    FUNC_glMemoryObjectParameterivEXT glMemoryObjectParameterivEXT;
    FUNC_glGenSemaphoresEXT glGenSemaphoresEXT;
    FUNC_glImportSemaphoreFdEXT glImportSemaphoreFdEXT;
    FUNC_glSignalSemaphoreEXT glSignalSemaphoreEXT;

    __GLXextFuncPtr (*glXGetProcAddress)(const unsigned char *procName);
    GLXFBConfig* (*glXChooseFBConfig)(Display *dpy, int screen, const int *attribList, int *nitems);
    Bool (*glXMakeContextCurrent)(Display *dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx);
    // TODO: Remove
    GLXContext (*glXCreateNewContext)(Display *dpy, GLXFBConfig config, int renderType, GLXContext shareList, Bool direct);
    void (*glXDestroyContext)(Display *dpy, GLXContext ctx);
    void (*glXSwapBuffers)(Display *dpy, GLXDrawable drawable);
    FUNC_glXCreateContextAttribsARB glXCreateContextAttribsARB;

    /* Optional */
    FUNC_glXSwapIntervalEXT glXSwapIntervalEXT;
    FUNC_glXSwapIntervalMESA glXSwapIntervalMESA;
    FUNC_glXSwapIntervalSGI glXSwapIntervalSGI;

    unsigned int (*glGetError)(void);
    const unsigned char* (*glGetString)(unsigned int name);
    void (*glFlush)(void);
    void (*glFinish)(void);
    void (*glClear)(unsigned int mask);
    void (*glClearColor)(float red, float green, float blue, float alpha);
    void (*glGenTextures)(int n, unsigned int *textures);
    void (*glDeleteTextures)(int n, const unsigned int *texture);
    void (*glActiveTexture)(unsigned int texture);
    void (*glBindTexture)(unsigned int target, unsigned int texture);
    void (*glBindImageTexture)(unsigned int unit, unsigned int texture, int level, unsigned char layered, int layer, unsigned int access, unsigned int format);
    void (*glTexParameteri)(unsigned int target, unsigned int pname, int param);
    void (*glTexParameteriv)(unsigned int target, unsigned int pname, const int *params);
    void (*glTexParameterfv)(unsigned int target, unsigned int pname, const float *params);
    void (*glTexImage2D)(unsigned int target, int level, int internalFormat, int width, int height, int border, unsigned int format, unsigned int type, const void *pixels);
    void (*glTexSubImage2D)(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned format, unsigned type, const void *pixels);
    void (*glTexStorage2D)(unsigned int target, int levels, unsigned int internalformat, int width, int height);
    void (*glGetTexImage)(unsigned int target, int level, unsigned int format, unsigned int type, void *pixels);
    void (*glGenFramebuffers)(int n, unsigned int *framebuffers);
    void (*glBindFramebuffer)(unsigned int target, unsigned int framebuffer);
    void (*glDeleteFramebuffers)(int n, const unsigned int *framebuffers);
    /* TODO: Note: this is only available since OpenGL ES 3.1 */
    void (*glMemoryBarrier)(unsigned int barriers);
    void (*glViewport)(int x, int y, int width, int height);
    void (*glFramebufferTexture2D)(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level);
    void (*glDrawBuffers)(int n, const unsigned int *bufs);
    unsigned int (*glCheckFramebufferStatus)(unsigned int target);
    void (*glBindBuffer)(unsigned int target, unsigned int buffer);
    void (*glGenBuffers)(int n, unsigned int *buffers);
    void (*glBufferData)(unsigned int target, khronos_ssize_t size, const void *data, unsigned int usage);
    void (*glBufferSubData)(unsigned int target, khronos_intptr_t offset, khronos_ssize_t size, const void *data);
    void (*glDeleteBuffers)(int n, const unsigned int *buffers);
    void (*glGenVertexArrays)(int n, unsigned int *arrays);
    void (*glBindVertexArray)(unsigned int array);
    void (*glDeleteVertexArrays)(int n, const unsigned int *arrays);
    unsigned int (*glCreateProgram)(void);
    unsigned int (*glCreateShader)(unsigned int type);
    void (*glAttachShader)(unsigned int program, unsigned int shader);
    void (*glBindAttribLocation)(unsigned int program, unsigned int index, const char *name);
    void (*glCompileShader)(unsigned int shader);
    void (*glLinkProgram)(unsigned int program);
    void (*glShaderSource)(unsigned int shader, int count, const char *const*string, const int *length);
    void (*glUseProgram)(unsigned int program);
    void (*glGetProgramInfoLog)(unsigned int program, int bufSize, int *length, char *infoLog);
    void (*glGetShaderiv)(unsigned int shader, unsigned int pname, int *params);
    void (*glGetShaderInfoLog)(unsigned int shader, int bufSize, int *length, char *infoLog);
    void (*glDeleteProgram)(unsigned int program);
    void (*glDeleteShader)(unsigned int shader);
    void (*glGetProgramiv)(unsigned int program, unsigned int pname, int *params);
    void (*glVertexAttribPointer)(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void *pointer);
    void (*glEnableVertexAttribArray)(unsigned int index);
    void (*glDrawArrays)(unsigned int mode, int first, int count);
    void (*glEnable)(unsigned int cap);
    void (*glDisable)(unsigned int cap);
    void (*glBlendFunc)(unsigned int sfactor, unsigned int dfactor);
    void (*glPixelStorei)(unsigned int pname, int param);
    int (*glGetUniformLocation)(unsigned int program, const char *name);
    void (*glUniform1f)(int location, float v0);
    void (*glUniform2f)(int location, float v0, float v1);
    void (*glUniform1i)(int location, int v0);
    void (*glUniform2i)(int location, int v0, int v1);
    void (*glUniformMatrix2fv)(int location, int count, unsigned char transpose, const float *value);
    void (*glUniformMatrix3fv)(int location, int count, unsigned char transpose, const float *value);
    void (*glDebugMessageCallback)(GLDEBUGPROC callback, const void *userParam);
    void (*glScissor)(int x, int y, int width, int height);
    void (*glCreateBuffers)(int n, unsigned int *buffers);
    void (*glReadPixels)(int x, int y, int width, int height, unsigned int format, unsigned int type, void *pixels);
    void* (*glMapBufferRange)(unsigned int target, intptr_t offset, ssize_t length, unsigned int access);
    unsigned char (*glUnmapBuffer)(unsigned int target);
    void (*glGetIntegerv)(unsigned int pname, int *params);
};

bool gsr_egl_load(gsr_egl *self, gsr_window *window, bool is_monitor_capture, bool enable_debug);
void gsr_egl_unload(gsr_egl *self);

#ifdef _WIN32
/* Phase 5b: the Windows (ANGLE-on-D3D11) implementations of the loader,
   in platform/windows/gsr_egl_win32.c. |window| may be NULL — the ANGLE
   context is surfaceless and needs no native window. */
bool gsr_egl_load_win32(gsr_egl *self, gsr_window *window, bool enable_debug);
void gsr_egl_unload_win32(gsr_egl *self);
#endif

/* Does opengl swap with egl or glx, depending on which one is active */
void gsr_egl_swap_buffers(gsr_egl *self);

#endif /* GSR_EGL_H */
