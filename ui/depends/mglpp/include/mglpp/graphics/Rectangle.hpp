#ifndef MGLPP_RECTANGLE_HPP
#define MGLPP_RECTANGLE_HPP

#include "Drawable.hpp"

extern "C" {
#include <mgl/graphics/rectangle.h>
}

namespace mgl {
    class Rectangle : public Drawable {
    public:
        Rectangle();
        Rectangle(vec2f size);
        Rectangle(vec2f position, vec2f size);

        void set_position(vec2f position) override;
        void set_color(Color color) override;
        vec2f get_position() const override;
        void set_size(vec2f size);
        vec2f get_size() const;
    protected:
        void draw(Window &window) override;
    private:
        mgl_rectangle rectangle;
    };
}

#endif /* MGLPP_RECTANGLE_HPP */
