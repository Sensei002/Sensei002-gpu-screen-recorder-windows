# Minimal Graphics Library (C++)
C++ wrapper for [mgl](https://git.dec05eba.com/mgl/about/)
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
mglpp needs to be initialized first and then a window has to be created before other functions are called.
# TODO
Wayland support is not finished yet. Use MGL_WINDOW_SYSTEM_X11 for now in mgl::Init.