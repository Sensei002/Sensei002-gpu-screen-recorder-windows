#ifndef MGLPP_COLOR_HPP
#define MGLPP_COLOR_HPP

#include <stdint.h>

namespace mgl {
    struct Color {
        Color(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255) : r(r), g(g), b(b), a(a) {

        }

        bool operator == (const Color &other) const {
            return r == other.r
                && g == other.g
                && b == other.b
                && a == other.a;
        }

        bool operator != (const Color &other) const {
            return !(operator==(other));
        }

        uint8_t r, g, b, a;
    };
}

#endif /* MGLPP_COLOR_HPP */
