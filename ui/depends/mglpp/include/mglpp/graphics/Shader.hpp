#ifndef MGLPP_SHADER_HPP
#define MGLPP_SHADER_HPP

#include "../system/vec.hpp"
#include "../graphics/Color.hpp"

extern "C" {
#include <mgl/graphics/shader.h>
}

namespace mgl {
    class Shader {
    public:
        enum Type {
            Vertex,
            Fragment,
            Geometry
        };

        Shader();
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        ~Shader();

        bool load_from_file(const char *filepath, Type type);
        bool set_uniform(const char *name, float value);
        bool set_uniform(const char *name, vec2f value);
        bool set_uniform(const char *name, Color color);

        bool is_valid() const;

        // If |shader| is nullptr then no shader is used
        static void use(Shader *shader);
    private:
        mgl_shader_program shader_program;
    };
}

#endif /* MGLPP_SHADER_HPP */
