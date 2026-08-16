# Minimal Graphics Library
Written in C and uses OpenGL 2.1 to support as many platforms as possible.\
Mgl supports both x11 and wayland and allows you to choose either glx or egl at runtime.
# Dependencies
## Build
* `libx11, libxrender, libxrandr`
* `wayland-client, wayland-egl, wayland-scanner, libxkbcommon` (Only needed if mgl is built with the `-Dwayland=true` meson option, which is set by default)
* `pango` (`pangoft2`)
## Runtime
`libglvnd (libGL.so, libGLX.so, libEGL.so)`
# Notes
Every window _get_ function is cached from the last event poll, no calls to x11/wayland is made.\
Only one window can be created and used at once.\
mgl needs to be initialized first and then a window has to be created before other functions are called.
# TODO
Wayland support is not finished yet. Use MGL_WINDOW_SYSTEM_X11 for now in mgl_init.