#include "../../include/mglpp/window/Keyboard.hpp"

extern "C" {
#include <mgl/window/key.h>
}

namespace mgl {
    // static
    const char* Keyboard::key_to_string(Key key) {
        return mgl_key_to_string((mgl_key)key);
    }

    // static
    bool Keyboard::key_is_modifier(Key key) {
        return mgl_key_is_modifier((mgl_key)key);
    }

    // static
    uint64_t Keyboard::key_to_x11_keysym(Key key) {
        return mgl_key_to_x11_keysym((mgl_key)key);
    }
}