#ifndef GSR_CURSOR_H
#define GSR_CURSOR_H

#include "egl.h"
#include "vec2.h"

typedef struct {
    gsr_egl *egl;
    Display *display;
    int x_fixes_event_base;

    unsigned int texture_id;
    vec2i size;
    vec2i hotspot;
    vec2i position;

    bool cursor_image_set;
    bool visible;
} gsr_cursor;

int gsr_cursor_init(gsr_cursor *self, gsr_egl *egl, Display *display);
void gsr_cursor_deinit(gsr_cursor *self);

/* Returns true if the cursor image has updated or if the cursor has moved */
bool gsr_cursor_on_event(gsr_cursor *self, XEvent *xev);
void gsr_cursor_tick(gsr_cursor *self, Window relative_to);

#endif /* GSR_CURSOR_H */
