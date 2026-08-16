/* platform/windows/stubs/X11/Xlib.h — stub <X11/Xlib.h> for the Windows build.
 *
 * Some upstream headers (recorder/capture_setup.h) include <X11/Xlib.h>
 * even though the Windows port never uses X11. The handful of X11 types
 * those headers actually reference (Display, Window, XID, Bool) are already
 * provided by the _WIN32 branch of upstream/include/egl.h, and XEvent by
 * the force-included gsr_win32_compat.h. This file exists only so the
 * #include resolves; it must NOT redefine any of those types.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3c.
 */
#ifndef GSR_STUB_X11_XLIB_H
#define GSR_STUB_X11_XLIB_H

/* Nothing to provide: Display/Window/XID/Bool come from egl.h's _WIN32
   branch (included before <X11/Xlib.h> everywhere), XEvent from the
   compat shim. */

/* recorder.c's cursor-tick call is dead code on Windows (the X11 cursor
   display is always NULL), but it must still compile. Xlib's
   DefaultRootWindow is a function-like macro; return a NULL Window. */
#ifndef DefaultRootWindow
#define DefaultRootWindow(display) ((Window)0)
#endif

#endif /* GSR_STUB_X11_XLIB_H */
