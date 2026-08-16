#include "../../include/mglpp/graphics/Shader.hpp"

namespace mgl {
    Shader::Shader() {
        shader_program.id = 0;
    }

    Shader::~Shader() {
        mgl_shader_program_deinit(&shader_program);
    }

    bool Shader::load_from_file(const char *filepath, Type type) {
        if(shader_program.id) {
            mgl_shader_program_deinit(&shader_program);
            shader_program.id = 0;
        }
        
        int res = mgl_shader_program_init(&shader_program);
        if(res != 0)
            return false;

        res = mgl_shader_program_add_shader_from_file(&shader_program, filepath, (mgl_shader_type)type);
        if(res != 0)
            return false;

        return mgl_shader_program_finalize(&shader_program) == 0;
    }

    bool Shader::set_uniform(const char *name, float value) {
        return mgl_shader_program_set_uniform_float(&shader_program, name, value) == 0;
    }

    bool Shader::set_uniform(const char *name, vec2f value) {
        return mgl_shader_program_set_uniform_vec2f(&shader_program, name, { value.x, value.y }) == 0;
    }

    bool Shader::set_uniform(const char *name, Color color) {
        return mgl_shader_program_set_uniform_color(&shader_program, name, { color.r, color.g, color.b, color.a }) == 0;
    }

    bool Shader::is_valid() const {
        return shader_program.id != 0;
    }

    // static
    void Shader::use(Shader *shader) {
        if(shader)
            mgl_shader_program_use(&shader->shader_program);
        else
            mgl_shader_program_use(nullptr);
    }
}