#ifndef MGL_GRAPHICS_WGL
#define MGL_GRAPHICS_WGL

#include "graphics.h"

/* Win32-only desktop GL backend (opengl32.dll + WGL). */
bool mgl_graphics_wgl_init(mgl_graphics *self, const mgl_graphics_create_params *params);

#endif /* MGL_GRAPHICS_WGL */
