#ifndef MGLPP_MOUSE_HPP
#define MGLPP_MOUSE_HPP

namespace mgl {
    class Mouse {
    public:
        /* Has to match mgl_mouse_button */
        enum Button : int {
            Unknown,
            Left,
            Right,
            Middle,
            XButton1,
            XButton2,

            /* This should always be the last mouse button */
            __NumMouseButtons__
        };
    };
}

#endif /* MGLPP_MOUSE_HPP */
