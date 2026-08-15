#ifndef GSR_WINDOW_X11_H
#define GSR_WINDOW_X11_H

#include "window.h"

typedef struct _XDisplay Display;

gsr_window* gsr_window_x11_create(Display *display);

#endif /* GSR_WINDOW_X11_H */
