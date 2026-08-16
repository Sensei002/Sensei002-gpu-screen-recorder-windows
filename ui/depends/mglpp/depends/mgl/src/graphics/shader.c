#include "../../include/mgl/graphics/shader.h"
#include "../../include/mgl/graphics/texture.h"
#include "../../include/mgl/system/fileutils.h"
#include "../../include/mgl/mgl.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    unsigned int id;
    mgl_shader_type shader_type;
} mgl_shader;

static unsigned int mgl_shader_type_to_gl_shader_type(mgl_shader_type shader_type) {
    switch(shader_type) {
        case MGL_SHADER_VERTEX:    return GL_VERTEX_SHADER;
        case MGL_SHADER_FRAGMENT:  return GL_FRAGMENT_SHADER;
        case MGL_SHADER_GEOMETRY:  return GL_GEOMETRY_SHADER;
    }
    return 0;
}

static void print_compile_log(mgl_context *context, unsigned int shader_id, const char *prefix) {
    int log_length = 0;
    context->gl.glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &log_length);
    if(log_length > 0) {
        char *log_str = malloc(log_length + 1);
        if(!log_str) {
            fprintf(stderr, "mgl error: failed to allocate memory for shader compile log\n");
            return;
        }

        log_str[0] = '\0';
        log_str[log_length] = '\0';
        context->gl.glGetShaderInfoLog(shader_id, log_length, NULL, log_str);

        fprintf(stderr, "%s: %s\n", prefix, log_str);
        free(log_str);
    }
}

static void mgl_shader_unload(mgl_shader *self);

static int mgl_shader_load_from_memory(mgl_shader *self, const unsigned char *shader_data, int shader_size, mgl_shader_type shader_type) {
    self->id = 0;
    self->shader_type = shader_type;

    mgl_context *context = mgl_get_context();
    self->id = context->gl.glCreateShader(mgl_shader_type_to_gl_shader_type(shader_type));
    if(self->id == 0) {
        fprintf(stderr, "mgl error: failed to load shader, error: glCreateShader failed\n");
        return -1;
    }

    context->gl.glShaderSource(self->id, 1, (const char* const*)&shader_data, &shader_size);
    context->gl.glCompileShader(self->id);

    int compiled_successfully = GL_FALSE;
    context->gl.glGetShaderiv(self->id, GL_COMPILE_STATUS, &compiled_successfully);
    if(compiled_successfully == GL_FALSE) {
        print_compile_log(context, self->id, "mgl error");
        mgl_shader_unload(self);
        return -1;
    }

    print_compile_log(context, self->id, "mgl warning");
    return 0;
}

static int mgl_shader_load_from_file(mgl_shader *self, const char *filepath, mgl_shader_type shader_type) {
    self->id = 0;
    self->shader_type = shader_type;

    mgl_filedata filedata;
    if(mgl_load_file(filepath, &filedata, NULL) != 0) {
        fprintf(stderr, "mgl error: failed to load shader %s, error: mgl_load_file failed\n", filepath);
        return -1;
    }

    if(filedata.size > INT32_MAX) {
        fprintf(stderr, "mgl error: failed to load shader %s, error: shader size is too large\n", filepath);
        return -1;
    }

    int res = mgl_shader_load_from_memory(self, filedata.data, filedata.size, shader_type);
    mgl_filedata_free(&filedata);
    return res;
}

void mgl_shader_unload(mgl_shader *self) {
    mgl_context *context = mgl_get_context();
    if(self->id) {
        context->gl.glDeleteShader(self->id);
        self->id = 0;
    }
}

int mgl_shader_program_init(mgl_shader_program *self) {
    self->id = 0;

    mgl_context *context = mgl_get_context();
    self->id = context->gl.glCreateProgram();
    if(self->id == 0) {
        fprintf(stderr, "mgl error: failed to create shader program: error glCreateProgram failed\n");
        return -1;
    }

    return 0;
}

void mgl_shader_program_deinit(mgl_shader_program *self){
    mgl_context *context = mgl_get_context();
    if(self->id) {
        context->gl.glDeleteProgram(self->id);
        self->id = 0;
    }
}

/* TODO: Check for attach shader error */

int mgl_shader_program_add_shader_from_file(mgl_shader_program *self, const char *filepath, mgl_shader_type shader_type){
    mgl_shader shader;
    if(mgl_shader_load_from_file(&shader, filepath, shader_type) != 0)
        return -1;

    mgl_get_context()->gl.glAttachShader(self->id, shader.id);
    mgl_shader_unload(&shader); /* TODO: Verify if deleting the shader here is always ok now that we have attached it */
    return 0;
}

int mgl_shader_program_add_shader_from_memory(mgl_shader_program *self, const unsigned char *shader_data, int shader_size, mgl_shader_type shader_type){
    mgl_shader shader;
    if(mgl_shader_load_from_memory(&shader, shader_data, shader_size, shader_type) != 0)
        return -1;

    mgl_get_context()->gl.glAttachShader(self->id, shader.id);
    mgl_shader_unload(&shader); /* TODO: Verify if deleting the shader here is always ok now that we have attached it */
    return 0;
}

static void print_link_log(mgl_context *context, unsigned int shader_id, const char *prefix) {
    int log_length = 0;
    context->gl.glGetProgramiv(shader_id, GL_INFO_LOG_LENGTH, &log_length);
    if(log_length > 0) {
        char *log_str = malloc(log_length + 1);
        if(!log_str) {
            fprintf(stderr, "mgl error: failed to allocate memory for shader link log\n");
            return;
        }

        log_str[0] = '\0';
        log_str[log_length] = '\0';
        context->gl.glGetProgramInfoLog(shader_id, log_length, NULL, log_str);

        fprintf(stderr, "%s: %s\n", prefix, log_str);
        free(log_str);
    }
}

int mgl_shader_program_finalize(mgl_shader_program *self) {
    mgl_context *context = mgl_get_context();

    context->gl.glLinkProgram(self->id);
    int is_linked = GL_TRUE;
    context->gl.glGetProgramiv(self->id, GL_LINK_STATUS, &is_linked);
    if(is_linked == GL_FALSE) {
        print_link_log(context, self->id, "mgl error");
        return -1;
    }

    print_link_log(context, self->id, "mgl warning");
    return 0;
}

/* TODO: Optimize glUseProgram */
/* TODO: Optimize glGetUniformLocation */
/* TODO: Check if the uniform type matches the type of the value we want to set it to */

int mgl_shader_program_set_uniform_float(mgl_shader_program *self, const char *uniform_name, float value) {
    mgl_context *context = mgl_get_context();
    int uniform_location = context->gl.glGetUniformLocation(self->id, uniform_name);
    if(uniform_location == -1) {
        fprintf(stderr, "mgl error: no uniform by the name %s was found in the shader\n", uniform_name);
        return -1;
    }

    context->gl.glUseProgram(self->id);
    context->gl.glUniform1f(uniform_location, value);
    context->gl.glUseProgram(0);
    return 0;
}

int mgl_shader_program_set_uniform_vec2f(mgl_shader_program *self, const char *uniform_name, mgl_vec2f value) {
    mgl_context *context = mgl_get_context();
    int uniform_location = context->gl.glGetUniformLocation(self->id, uniform_name);
    if(uniform_location == -1) {
        fprintf(stderr, "mgl error: no uniform by the name %s was found in the shader\n", uniform_name);
        return -1;
    }

    context->gl.glUseProgram(self->id);
    context->gl.glUniform2f(uniform_location, value.x, value.y);
    context->gl.glUseProgram(0);
    return 0;
}

int mgl_shader_program_set_uniform_vec3f(mgl_shader_program *self, const char *uniform_name, mgl_vec3f value) {
    mgl_context *context = mgl_get_context();
    int uniform_location = context->gl.glGetUniformLocation(self->id, uniform_name);
    if(uniform_location == -1) {
        fprintf(stderr, "mgl error: no uniform by the name %s was found in the shader\n", uniform_name);
        return -1;
    }

    context->gl.glUseProgram(self->id);
    context->gl.glUniform3f(uniform_location, value.x, value.y, value.z);
    context->gl.glUseProgram(0);
    return 0;
}

int mgl_shader_program_set_uniform_vec4f(mgl_shader_program *self, const char *uniform_name, mgl_vec4f value) {
    mgl_context *context = mgl_get_context();
    int uniform_location = context->gl.glGetUniformLocation(self->id, uniform_name);
    if(uniform_location == -1) {
        fprintf(stderr, "mgl error: no uniform by the name %s was found in the shader\n", uniform_name);
        return -1;
    }

    context->gl.glUseProgram(self->id);
    context->gl.glUniform4f(uniform_location, value.x, value.y, value.z, value.w);
    context->gl.glUseProgram(0);
    return 0;
}

int mgl_shader_program_set_uniform_color(mgl_shader_program *self, const char *uniform_name, mgl_color color) {
    return mgl_shader_program_set_uniform_vec4f(self, uniform_name,
        (mgl_vec4f){ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f });
}

/* TODO: Optimize glUseProgram */
void mgl_shader_program_use(mgl_shader_program *shader_program) {
    mgl_context *context = mgl_get_context();
    context->gl.glUseProgram(shader_program ? shader_program->id : 0);
}
