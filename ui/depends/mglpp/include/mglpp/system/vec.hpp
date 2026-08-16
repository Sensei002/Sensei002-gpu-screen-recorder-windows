#ifndef MGLPP_VEC_HPP
#define MGLPP_VEC_HPP

namespace mgl {
    template <typename T>
    struct vec2 {
        vec2() : x(0), y(0) {}
        vec2(T x, T y) : x(x), y(y) {}

        vec2<T> operator + (vec2<T> other) const {
            return { x + other.x, y + other.y };
        }

        vec2<T> operator - (vec2<T> other) const {
            return { x - other.x, y - other.y };
        }

        vec2<T> operator * (vec2<T> other) const {
            return { x * other.x, y * other.y };
        }

        vec2<T> operator / (vec2<T> other) const {
            return { x / other.x, y / other.y };
        }

        vec2<T> operator * (T v) const {
            return { x * v, y * v };
        }

        vec2<T> operator / (T v) const {
            return { x / v, y / v };
        }

        vec2<T>& operator += (vec2<T> other) {
            x += other.x;
            y += other.y;
            return *this;
        }

        vec2<T>& operator -= (vec2<T> other) {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        vec2<T>& operator *= (vec2<T> other) {
            x *= other.x;
            y *= other.y;
            return *this;
        }

        vec2<T>& operator *= (T v) {
            x *= v;
            y *= v;
            return *this;
        }

        vec2<T>& operator /= (T v) {
            x /= v;
            y /= v;
            return *this;
        }

        vec2<float> to_vec2f() const {
            return { static_cast<float>(x), static_cast<float>(y) };
        }

        vec2<int> to_vec2i() const {
            return { static_cast<int>(x), static_cast<int>(y) };
        }

        vec2<T> floor() const {
            return { static_cast<T>(static_cast<int>(x)), static_cast<T>(static_cast<int>(y)) };
        }

        T x;
        T y;
    };

    using vec2f = vec2<float>;
    using vec2d = vec2<double>;
    using vec2i = vec2<int>;
    using vec2u = vec2<unsigned int>;
}

#endif /* MGLPP_VEC_HPP */
