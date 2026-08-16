#ifndef MGLPP_RECT_HPP
#define MGLPP_RECT_HPP

#include "../system/vec.hpp"

namespace mgl {
    template <typename T>
    class Rect {
    public:
        Rect() : position(0, 0), size(0, 0) {}
        Rect(vec2<T> position, vec2<T> size) : position(position), size(size) {}

        bool contains(vec2<T> point) const {
            return point.x >= position.x && point.x <= position.x + size.x
                && point.y >= position.y && point.y <= position.y + size.y;
        }

        vec2<T> position;
        vec2<T> size;
    };

    typedef Rect<int>   IntRect;
    typedef Rect<float> FloatRect;
}

#endif /* MGLPP_RECT_HPP */
