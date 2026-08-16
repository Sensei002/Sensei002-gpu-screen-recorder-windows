#ifndef MGL_GL_H
#define MGL_GL_H

#include "gl_macro.h"
#include <stdint.h>
#include <sys/types.h>

typedef struct _XVisualInfo _XVisualInfo;
typedef struct _XDisplay Display;
typedef struct __GLXcontextRec *GLXContext;
typedef unsigned long GLXDrawable;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef void(*__GLXextFuncPtr)(void);

typedef void (*FUNC_glXSwapIntervalEXT)(Display * dpy, GLXDrawable drawable, int interval);
typedef int (*FUNC_glXSwapIntervalMESA)(unsigned int interval);
typedef int (*FUNC_glXSwapIntervalSGI)(int interval);

typedef void* EGLDisplay;
typedef void* EGLNativeDisplayType;
typedef uintptr_t EGLNativeWindowType;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);

typedef struct {
    void *gl_library;
    void *glx_library;
    void *egl_library;

    __GLXextFuncPtr (*glXGetProcAddress)(const unsigned char *procName);
    GLXContext (*glXCreateNewContext)(Display *dpy, GLXFBConfig config, int renderType, GLXContext shareList, int direct);
    int (*glXMakeContextCurrent)(Display *dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx);
    void (*glXDestroyContext)(Display *dpy, GLXContext ctx);
    void (*glXSwapBuffers)(Display *dpy, GLXDrawable drawable);
    GLXFBConfig* (*glXChooseFBConfig)(Display *dpy, int screen, const int *attribList, int *nitems);
    _XVisualInfo* (*glXGetVisualFromFBConfig)(Display *dpy, GLXFBConfig config);

    __eglMustCastToProperFunctionPointerType (*eglGetProcAddress)(const char *procname);
    EGLDisplay (*eglGetDisplay)(EGLNativeDisplayType display_id);
    unsigned int (*eglInitialize)(EGLDisplay dpy, int32_t *major, int32_t *minor);
    unsigned int (*eglTerminate)(EGLDisplay dpy);
    unsigned int (*eglGetConfigs)(EGLDisplay dpy, EGLConfig *configs, int32_t config_size, int32_t *num_config);
    EGLSurface (*eglCreateWindowSurface)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const int32_t *attrib_list);
    EGLContext (*eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const int32_t *attrib_list);
    unsigned int (*eglMakeCurrent)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
    unsigned int (*eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
    unsigned int (*eglDestroySurface)(EGLDisplay dpy, EGLSurface surface);
    unsigned int (*eglSwapInterval)(EGLDisplay dpy, int32_t interval);
    unsigned int (*eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
    unsigned int (*eglBindAPI)(unsigned int api);
    unsigned int (*eglGetConfigAttrib)(EGLDisplay dpy, EGLConfig config, int32_t attribute, int32_t *value);

    void (*glViewport)(int x, int y, int width, int height);
    void (*glScissor)(int x, int y, int width, int height);
    void (*glClearColor)(float red, float green, float blue, float alpha);
    void (*glClear)(unsigned int mask);
    void (*glEnable)(unsigned int cap);
    void (*glDisable)(unsigned int cap);
    void (*glBlendFunc)(unsigned int sfactor, unsigned int dfactor);
    void (*glBlendFuncSeparate)(unsigned int sfactorRGB, unsigned int dfactorRGB, unsigned int sfactorAlpha, unsigned int dfactorAlpha);
    void (*glGenTextures)(int n, unsigned int *textures);
    void (*glDeleteTextures)(int n, const unsigned int *textures);
    void (*glGetTexLevelParameteriv)(unsigned int target, int level, unsigned int pname, int *params);
    void (*glTexImage2D)(unsigned int target, int level, int internalFormat, int width, int height, int border, unsigned int format, unsigned int type, const void *pixels);
    void (*glTexSubImage2D)(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, unsigned int type, const void *pixels);
    void (*glBindTexture)(unsigned int target, unsigned int texture);
    void (*glTexParameteri)(unsigned int target, unsigned int pname, int param);
    void (*glTexEnvi)(unsigned int target, unsigned int pname, int param);
    void (*glBegin)(unsigned int mode);
    void (*glEnd)(void);
    void (*glVertex3f)(float x, float y, float z);
    void (*glColor4ub)(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
    void (*glTexCoord2f)(float s, float t);
    void (*glOrtho)(double left, double right, double bottom, double top, double near_val, double far_val);
    void (*glMatrixMode)(unsigned int mode);
    void (*glPushMatrix)(void);
    void (*glPopMatrix)(void);
    void (*glLoadIdentity)(void);
    void (*glLoadMatrixf)(const float *m);
    void (*glTranslatef)(float x, float y, float z);
    void (*glRotatef)(float angle, float x, float y, float z);
    void (*glGenBuffers)(int n, unsigned int *buffers);
    void (*glBindBuffer)(unsigned int target, unsigned int buffer);
    void (*glDeleteBuffers)(int n, const unsigned int *buffers);
    void (*glBufferData)(unsigned int target, ssize_t size, const void *data, unsigned int usage);
    void (*glBufferSubData)(unsigned int target, ssize_t offset, ssize_t size, const void *data);
    void (*glDrawArrays)(unsigned int mode, int first, int count);
    void (*glDrawElements)(unsigned int mode, int count, unsigned int type, const void *indices);
    void (*glEnableClientState)(unsigned int cap);
    void (*glDisableClientState)(unsigned int cap);
    void (*glVertexPointer)(int size, unsigned int type, int stride, const void *ptr);
    void (*glColorPointer)(int size, unsigned int type, int stride, const void *ptr);
    void (*glTexCoordPointer)(int size, unsigned int type, int stride, const void *ptr);
    void (*glCompileShader)(unsigned int shader);
    unsigned int (*glCreateProgram)(void);
    unsigned int (*glCreateShader)(unsigned int type);
    void (*glDeleteProgram)(unsigned int program);
    void (*glDeleteShader)(unsigned int shader);
    void (*glGetShaderiv)(unsigned int shader, unsigned int pname, int *params);
    void (*glGetShaderInfoLog)(unsigned int shader, int bufSize, int *length, char *infoLog);
    void (*glGetProgramiv)(unsigned int program, unsigned int pname, int *params);
    void (*glGetProgramInfoLog)(unsigned int program, int bufSize, int *length, char *infoLog);
    void (*glLinkProgram)(unsigned int program);
    void (*glShaderSource)(unsigned int shader, int count, const char *const*string, const int *length);
    void (*glUseProgram)(unsigned int program);
    void (*glAttachShader)(unsigned int program, unsigned int shader);
    int (*glGetUniformLocation)(unsigned int program, const char *name);
    void (*glUniform1f)(int location, float v0);
    void (*glUniform2f)(int location, float v0, float v1);
    void (*glUniform3f)(int location, float v0, float v1, float v2);
    void (*glUniform4f)(int location, float v0, float v1, float v2, float v3);
    unsigned int (*glGetError)(void);
    const unsigned char* (*glGetString)(unsigned int name);
    void (*glGetIntegerv)(unsigned int pname, int *params);
    void (*glPixelStorei)(unsigned int pname, int param);
    void (*glFlush)(void);
    void (*glFinish)(void);

    /* Optional*/
    FUNC_glXSwapIntervalEXT glXSwapIntervalEXT;
    FUNC_glXSwapIntervalMESA glXSwapIntervalMESA;
    FUNC_glXSwapIntervalSGI glXSwapIntervalSGI;
    void (*glGenerateMipmap)(unsigned int target);
} mgl_gl;

int mgl_gl_load(mgl_gl *self);
void mgl_gl_unload(mgl_gl *self);

#ifdef _WIN32
/* Resolve the GL 1.2+ entry points that opengl32.dll does not export. Call
   after a WGL context is current (wglGetProcAddress requires one). Fills
   only NULL slots; the GL 1.1 core comes from opengl32.dll exports. */
void mgl_gl_load_windows_extensions(mgl_gl *self);
#endif

#endif /* MGL_GL_H */
