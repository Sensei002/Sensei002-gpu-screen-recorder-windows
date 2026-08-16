#ifndef MGL_SHADER_H
#define MGL_SHADER_H

#include "../system/vec.h"
#include "../graphics/color.h"

typedef struct mgl_texture mgl_texture;

typedef struct {
    unsigned int id;
} mgl_shader_program;

typedef enum {
    MGL_SHADER_VERTEX,
    MGL_SHADER_FRAGMENT,
    MGL_SHADER_GEOMETRY
} mgl_shader_type;

int mgl_shader_program_init(mgl_shader_program *self);
void mgl_shader_program_deinit(mgl_shader_program *self);

int mgl_shader_program_add_shader_from_file(mgl_shader_program *self, const char *filepath, mgl_shader_type shader_type);
int mgl_shader_program_add_shader_from_memory(mgl_shader_program *self, const unsigned char *shader_data, int shader_size, mgl_shader_type shader_type);
int mgl_shader_program_finalize(mgl_shader_program *self);

int mgl_shader_program_set_uniform_float(mgl_shader_program *self, const char *uniform_name, float value);
int mgl_shader_program_set_uniform_vec2f(mgl_shader_program *self, const char *uniform_name, mgl_vec2f value);
int mgl_shader_program_set_uniform_vec3f(mgl_shader_program *self, const char *uniform_name, mgl_vec3f value);
int mgl_shader_program_set_uniform_vec4f(mgl_shader_program *self, const char *uniform_name, mgl_vec4f value);
int mgl_shader_program_set_uniform_color(mgl_shader_program *self, const char *uniform_name, mgl_color color);

/* If |shader_program| is NULL then no shader is used */
void mgl_shader_program_use(mgl_shader_program *shader_program);

#endif /* MGL_SHADER_H */
