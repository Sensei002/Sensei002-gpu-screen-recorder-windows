#include "../include/shader.h"
#include "../include/log.h"
#include "../include/egl.h"
#include <assert.h>

static bool print_compile_errors = false;

static int min_int(int a, int b) {
    return a < b ? a : b;
}

static unsigned int load_shader(gsr_egl *egl, unsigned int type, const char *source) {
    unsigned int shader_id = egl->glCreateShader(type);
    if(shader_id == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "load_shader: failed to create shader, error: %d", egl->glGetError());
        return 0;
    }

    egl->glShaderSource(shader_id, 1, &source, NULL);
    egl->glCompileShader(shader_id);

    int compiled = 0;
    egl->glGetShaderiv(shader_id, GL_COMPILE_STATUS, &compiled);
    if(!compiled) {
        int info_length = 0;
        egl->glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &info_length);
        
        if(info_length > 1 && print_compile_errors) {
            char info_log[4096];
            egl->glGetShaderInfoLog(shader_id, min_int(4096, info_length), NULL, info_log);
            gsr_log(GSR_LOG_LEVEL_ERROR, "load_shader: failed to compile shader, error:\n%s\nshader source:\n%s", info_log, source);
        }

        egl->glDeleteShader(shader_id);
        return 0;
    }

    return shader_id;
}

static unsigned int load_program(gsr_egl *egl, const char *vertex_shader, const char *fragment_shader) {
    unsigned int vertex_shader_id = 0;
    unsigned int fragment_shader_id = 0;
    unsigned int program_id = 0;
    int linked = 0;
    bool success = false;

    if(vertex_shader) {
        vertex_shader_id = load_shader(egl, GL_VERTEX_SHADER, vertex_shader);
        if(vertex_shader_id == 0)
            goto done;
    }

    if(fragment_shader) {
        fragment_shader_id = load_shader(egl, GL_FRAGMENT_SHADER, fragment_shader);
        if(fragment_shader_id == 0)
            goto done;
    }

    program_id = egl->glCreateProgram();
    if(program_id == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "load_program: failed to create shader program, error: %d", egl->glGetError());
        goto done;
    }

    if(vertex_shader_id)
        egl->glAttachShader(program_id, vertex_shader_id);

    if(fragment_shader_id)
        egl->glAttachShader(program_id, fragment_shader_id);

    egl->glLinkProgram(program_id);

    egl->glGetProgramiv(program_id, GL_LINK_STATUS, &linked);
    if(!linked) {
        int info_length = 0;
        egl->glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &info_length);
        
        if(info_length > 1) {
            char info_log[4096];
            egl->glGetProgramInfoLog(program_id, min_int(4096, info_length), NULL, info_log);
            gsr_log(GSR_LOG_LEVEL_ERROR, "load program: linking shader program failed, error:\n%s", info_log);            
        }

        goto done;
    }

    success = true;
    done:

    if(!success) {
        if(program_id)
            egl->glDeleteProgram(program_id);
        program_id = 0;
    }
    if(fragment_shader_id)
        egl->glDeleteShader(fragment_shader_id);
    if(vertex_shader_id)
        egl->glDeleteShader(vertex_shader_id);
    return program_id;
}

int gsr_shader_init(gsr_shader *self, gsr_egl *egl, const char *vertex_shader, const char *fragment_shader) {
    assert(egl);
    self->egl = egl;
    self->program_id = 0;

    if(!vertex_shader && !fragment_shader) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_shader_init: vertex and fragment shader can't be NULL at the same time");
        return -1;
    }

    self->program_id = load_program(self->egl, vertex_shader, fragment_shader);
    if(self->program_id == 0)
        return -1;

    return 0;
}

void gsr_shader_deinit(gsr_shader *self) {
    if(!self->egl)
        return;

    if(self->program_id) {
        self->egl->glDeleteProgram(self->program_id);
        self->program_id = 0;
    }

    self->egl = NULL;
}

int gsr_shader_bind_attribute_location(gsr_shader *self, const char *attribute, int location) {
    while(self->egl->glGetError()) {}
    self->egl->glBindAttribLocation(self->program_id, location, attribute);
    return self->egl->glGetError();
}

void gsr_shader_use(gsr_shader *self) {
    self->egl->glUseProgram(self->program_id);
}

void gsr_shader_use_none(gsr_shader *self) {
    self->egl->glUseProgram(0);
}

void gsr_shader_enable_debug_output(bool enable) {
    print_compile_errors = enable;
}
