#include "../include/mgl/gl.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct {
    void **func;
    const char *name;
} dlsym_assign;

static void* dlsym_print_fail(void *handle, const char *name, int required) {
#ifdef _WIN32
    FARPROC sym = GetProcAddress((HMODULE)handle, name);
    if(!sym)
        fprintf(stderr, "mgl %s: GetProcAddress(handle, \"%s\") failed\n", required ? "error" : "warning", name);
    return (void*)sym;
#else
    dlerror();
    void *sym = dlsym(handle, name);
    char *err_str = dlerror();

    if(!sym)
        fprintf(stderr, "mgl %s: dlsym(handle, \"%s\") failed, error: %s\n", required ? "error" : "warning", name, err_str ? err_str : "(null)");

    return sym;
#endif
}

static int mgl_gl_load_gl(mgl_gl *self) {
    const dlsym_assign required_dlsym[] = {
        { (void**)&self->glViewport, "glViewport" },
        { (void**)&self->glScissor, "glScissor" },
        { (void**)&self->glClearColor, "glClearColor" },
        { (void**)&self->glClear, "glClear" },
        { (void**)&self->glEnable, "glEnable" },
        { (void**)&self->glDisable, "glDisable" },
        { (void**)&self->glBlendFunc, "glBlendFunc" },
        { (void**)&self->glBlendFuncSeparate, "glBlendFuncSeparate" },
        { (void**)&self->glGenTextures, "glGenTextures" },
        { (void**)&self->glDeleteTextures, "glDeleteTextures" },
        { (void**)&self->glGetTexLevelParameteriv, "glGetTexLevelParameteriv" },
        { (void**)&self->glTexImage2D, "glTexImage2D" },
        { (void**)&self->glTexSubImage2D, "glTexSubImage2D" },
        { (void**)&self->glBindTexture, "glBindTexture" },
        { (void**)&self->glTexParameteri, "glTexParameteri" },
        { (void**)&self->glTexEnvi, "glTexEnvi" },
        { (void**)&self->glBegin, "glBegin" },
        { (void**)&self->glEnd, "glEnd" },
        { (void**)&self->glVertex3f, "glVertex3f" },
        { (void**)&self->glColor4ub, "glColor4ub" },
        { (void**)&self->glTexCoord2f, "glTexCoord2f" },
        { (void**)&self->glOrtho, "glOrtho" },
        { (void**)&self->glMatrixMode, "glMatrixMode" },
        { (void**)&self->glPushMatrix, "glPushMatrix" },
        { (void**)&self->glPopMatrix, "glPopMatrix" },
        { (void**)&self->glLoadIdentity, "glLoadIdentity" },
        { (void**)&self->glLoadMatrixf, "glLoadMatrixf" },
        { (void**)&self->glTranslatef, "glTranslatef" },
        { (void**)&self->glRotatef, "glRotatef" },
        { (void**)&self->glGenBuffers, "glGenBuffers" },
        { (void**)&self->glBindBuffer, "glBindBuffer" },
        { (void**)&self->glDeleteBuffers, "glDeleteBuffers" },
        { (void**)&self->glBufferData, "glBufferData" },
        { (void**)&self->glBufferSubData, "glBufferSubData" },
        { (void**)&self->glDrawArrays, "glDrawArrays" },
        { (void**)&self->glDrawElements, "glDrawElements" },
        { (void**)&self->glEnableClientState, "glEnableClientState" },
        { (void**)&self->glDisableClientState, "glDisableClientState" },
        { (void**)&self->glVertexPointer, "glVertexPointer" },
        { (void**)&self->glColorPointer, "glColorPointer" },
        { (void**)&self->glTexCoordPointer, "glTexCoordPointer" },
        { (void**)&self->glCompileShader, "glCompileShader" },
        { (void**)&self->glCreateProgram, "glCreateProgram" },
        { (void**)&self->glCreateShader, "glCreateShader" },
        { (void**)&self->glDeleteProgram, "glDeleteProgram" },
        { (void**)&self->glDeleteShader, "glDeleteShader" },
        { (void**)&self->glGetShaderiv, "glGetShaderiv" },
        { (void**)&self->glGetShaderInfoLog, "glGetShaderInfoLog" },
        { (void**)&self->glGetProgramiv, "glGetProgramiv" },
        { (void**)&self->glGetProgramInfoLog, "glGetProgramInfoLog" },
        { (void**)&self->glLinkProgram, "glLinkProgram" },
        { (void**)&self->glShaderSource, "glShaderSource" },
        { (void**)&self->glUseProgram, "glUseProgram" },
        { (void**)&self->glAttachShader, "glAttachShader" },
        { (void**)&self->glGetUniformLocation, "glGetUniformLocation" },
        { (void**)&self->glUniform1f, "glUniform1f" },
        { (void**)&self->glUniform2f, "glUniform2f" },
        { (void**)&self->glUniform3f, "glUniform3f" },
        { (void**)&self->glUniform4f, "glUniform4f" },
        { (void**)&self->glGetError, "glGetError" },
        { (void**)&self->glGetString, "glGetString" },
        { (void**)&self->glGetIntegerv, "glGetIntegerv" },
        { (void**)&self->glPixelStorei, "glPixelStorei" },
        { (void**)&self->glFlush, "glFlush" },
        { (void**)&self->glFinish, "glFinish" },

        { NULL, NULL }
    };

    for(int i = 0; required_dlsym[i].func; ++i) {
        *required_dlsym[i].func = dlsym_print_fail(self->gl_library, required_dlsym[i].name, 1);
        if(!*required_dlsym[i].func)
            return -1;
    }

    const dlsym_assign optional_dlsym[] = {
        { (void**)&self->glGenerateMipmap, "glGenerateMipmap" },

        { NULL, NULL }
    };

    for(int i = 0; optional_dlsym[i].func; ++i) {
        *optional_dlsym[i].func = dlsym_print_fail(self->gl_library, optional_dlsym[i].name, 0);
    }

    return 0;
}

static int mgl_gl_load_glx(mgl_gl *self) {
    const dlsym_assign required_dlsym[] = {
        { (void**)&self->glXGetProcAddress, "glXGetProcAddress" },
        { (void**)&self->glXCreateNewContext, "glXCreateNewContext" },
        { (void**)&self->glXMakeContextCurrent, "glXMakeContextCurrent" },
        { (void**)&self->glXDestroyContext, "glXDestroyContext" },
        { (void**)&self->glXSwapBuffers, "glXSwapBuffers" },
        { (void**)&self->glXChooseFBConfig, "glXChooseFBConfig" },
        { (void**)&self->glXGetVisualFromFBConfig, "glXGetVisualFromFBConfig" },

        { NULL, NULL }
    };

    /* In some distros (alpine for example libGLX doesn't exist, but libGL can be used instead) */
    void *library = self->glx_library ? self->glx_library : self->gl_library;

    for(int i = 0; required_dlsym[i].func; ++i) {
        *required_dlsym[i].func = dlsym_print_fail(library, required_dlsym[i].name, 1);
        if(!*required_dlsym[i].func)
            return -1;
    }

    self->glXSwapIntervalEXT = (FUNC_glXSwapIntervalEXT)self->glXGetProcAddress((const unsigned char*)"glXSwapIntervalEXT");
    self->glXSwapIntervalMESA = (FUNC_glXSwapIntervalMESA)self->glXGetProcAddress((const unsigned char*)"glXSwapIntervalMESA");
    self->glXSwapIntervalSGI = (FUNC_glXSwapIntervalSGI)self->glXGetProcAddress((const unsigned char*)"glXSwapIntervalSGI");

    return 0;
}

static int mgl_gl_load_egl(mgl_gl *self) {
    const dlsym_assign required_dlsym[] = {
        { (void**)&self->eglGetProcAddress, "eglGetProcAddress" },
        { (void**)&self->eglGetDisplay, "eglGetDisplay" },
        { (void**)&self->eglInitialize, "eglInitialize" },
        { (void**)&self->eglTerminate, "eglTerminate" },
        { (void**)&self->eglGetConfigs, "eglGetConfigs" },
        { (void**)&self->eglCreateWindowSurface, "eglCreateWindowSurface" },
        { (void**)&self->eglCreateContext, "eglCreateContext" },
        { (void**)&self->eglMakeCurrent, "eglMakeCurrent" },
        { (void**)&self->eglDestroyContext, "eglDestroyContext" },
        { (void**)&self->eglDestroySurface, "eglDestroySurface" },
        { (void**)&self->eglSwapInterval, "eglSwapInterval" },
        { (void**)&self->eglSwapBuffers, "eglSwapBuffers" },
        { (void**)&self->eglBindAPI, "eglBindAPI" },
        { (void**)&self->eglGetConfigAttrib, "eglGetConfigAttrib" },

        { NULL, NULL }
    };

    for(int i = 0; required_dlsym[i].func; ++i) {
        *required_dlsym[i].func = dlsym_print_fail(self->egl_library, required_dlsym[i].name, 1);
        if(!*required_dlsym[i].func)
            return -1;
    }

    return 0;
}

int mgl_gl_load(mgl_gl *self) {
    memset(self, 0, sizeof(*self));

#ifdef _WIN32
    /* On Windows all GL entry points are exported by opengl32.dll (the system
       software implementation, or the vendor ICD installed on top of it).
       GLX and EGL are not used: the WGL graphics backend is used instead,
       which loads its own wgl* entry points from opengl32.dll. */
    self->gl_library = LoadLibraryA("opengl32.dll");
    if(!self->gl_library) {
        fprintf(stderr, "mgl error: LoadLibraryA(\"opengl32.dll\") failed\n");
        mgl_gl_unload(self);
        return -1;
    }

    if(mgl_gl_load_gl(self) != 0) {
        mgl_gl_unload(self);
        return -1;
    }
#else
    self->gl_library = dlopen("libGL.so.1", RTLD_LAZY);
    if(!self->gl_library) {
        fprintf(stderr, "mgl error:dlopen(\"%s\", RTLD_LAZY) failed\n", "libGL.so.1");
        mgl_gl_unload(self);
        return -1;
    }

    self->glx_library = dlopen("libGLX.so.0", RTLD_LAZY);

    self->egl_library = dlopen("libEGL.so.1", RTLD_LAZY);
    if(!self->egl_library) {
        fprintf(stderr, "mgl error:dlopen(\"%s\", RTLD_LAZY) failed\n", "libEGL.so.1");
        mgl_gl_unload(self);
        return -1;
    }

    if(mgl_gl_load_gl(self) != 0) {
        mgl_gl_unload(self);
        return -1;
    }

    if(mgl_gl_load_glx(self) != 0) {
        mgl_gl_unload(self);
        return -1;
    }

    if(mgl_gl_load_egl(self) != 0) {
        mgl_gl_unload(self);
        return -1;
    }
#endif

    return 0;
}

void mgl_gl_unload(mgl_gl *self) {
#ifdef _WIN32
    if(self->gl_library) {
        FreeLibrary((HMODULE)self->gl_library);
        self->gl_library = NULL;
    }
#else
    if(self->egl_library) {
        dlclose(self->egl_library);
        self->egl_library = NULL;
    }

    if(self->glx_library) {
        dlclose(self->glx_library);
        self->glx_library = NULL;
    }

    if(self->gl_library) {
        dlclose(self->gl_library);
        self->gl_library = NULL;
    }
#endif
}
