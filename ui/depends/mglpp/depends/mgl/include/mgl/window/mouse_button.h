#ifndef _MGL_MOUSE_BUTTON_H_
#define _MGL_MOUSE_BUTTON_H_

typedef enum {
    MGL_BUTTON_UNKNOWN,
    MGL_BUTTON_LEFT,
    MGL_BUTTON_RIGHT,
    MGL_BUTTON_MIDDLE,
    MGL_BUTTON_XBUTTON1,
    MGL_BUTTON_XBUTTON2,

    /* This should always be the last mouse button */
    __MGL_NUM_MOUSE_BUTTONS__
} mgl_mouse_button;

#endif /* _MGL_MOUSE_BUTTON_H_ */
