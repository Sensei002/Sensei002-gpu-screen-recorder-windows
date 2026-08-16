#ifndef MGLPP_DRAWABLE_HPP
#define MGLPP_DRAWABLE_HPP

#include "Color.hpp"
#include "../system/vec.hpp"

namespace mgl {
    class Window;
    class Drawable {
        friend class Window;
    public:
        virtual ~Drawable() = default;

        virtual void set_position(vec2f position) = 0;
        virtual void set_color(Color color) = 0;

        virtual vec2f get_position() const = 0;
    protected:
        virtual void draw(Window &window) = 0;
    };
}

#endif /* MGLPP_DRAWABLE_HPP */
