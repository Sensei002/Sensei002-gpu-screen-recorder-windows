#include <gsr/plugin.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

const char vertex_shader_source[] =
    "attribute vec4 vertex_pos;   \n"
    "void main() {                \n"
    "   gl_Position = vec4(vertex_pos.x, -vertex_pos.y, vertex_pos.z, vertex_pos.w); \n"
    "}";

const char fragment_shader_source[] =
    "precision mediump float;                    \n"
    "uniform vec3 color;                         \n"
    "void main() {                               \n"
    "   gl_FragColor = vec4(color, 1.0); \n"
    "}";

typedef struct {
    GLuint shader_program;
    GLuint vao;
    GLuint vbo;
    GLint color_uniform;
    unsigned int counter;
} Triangle;

static GLuint load_shader(const char *shaderSrc, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderSrc, NULL);
    glCompileShader(shader);
    return shader;
}

static void draw(const gsr_plugin_draw_params *params, void *userdata) {
    Triangle *triangle = userdata;

    GLfloat glverts[6];

    glverts[0] = -0.5f;
    glverts[1] = -0.5f;

    glverts[2] = 0.5f;
    glverts[3] = -0.5f;

    glverts[4] = 0.0f;
    glverts[5] = 0.5f;

    glBindVertexArray(triangle->vao);
    glBindBuffer(GL_ARRAY_BUFFER, triangle->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 6 * sizeof(float), glverts);

    glUseProgram(triangle->shader_program);
    const double pp = triangle->counter * 0.05;
    glUniform3f(triangle->color_uniform, 0.5 + sin(pp)*0.5, 0.5 + cos(pp)*0.5, 0.5 + sin(0.2 + pp)*0.5);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glUseProgram(0);

    ++triangle->counter;
}

static bool is_damaged(void *userdata) {
    return true;
}

static void clear_damage(void *userdata) {
    
}

bool gsr_plugin_init(const gsr_plugin_init_params *params, gsr_plugin_init_return *ret) {
    Triangle *triangle = calloc(1, sizeof(Triangle));
    if(!triangle)
        return false;

    triangle->shader_program = glCreateProgram();

    const GLuint vertex_shader = load_shader(vertex_shader_source, GL_VERTEX_SHADER);
    const GLuint fragment_shader = load_shader(fragment_shader_source, GL_FRAGMENT_SHADER);

    glAttachShader(triangle->shader_program, vertex_shader);
    glAttachShader(triangle->shader_program, fragment_shader);
    glBindAttribLocation(triangle->shader_program, 0, "vertex_pos");
    glLinkProgram(triangle->shader_program);

    glGenVertexArrays(1, &triangle->vao);
    glBindVertexArray(triangle->vao);

    glGenBuffers(1, &triangle->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, triangle->vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);

    glBindVertexArray(0);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    triangle->color_uniform = glGetUniformLocation(triangle->shader_program, "color");

    ret->name = "hello_triangle";
    ret->version = 1;
    ret->userdata = triangle;
    ret->draw = draw;
    ret->is_damaged = is_damaged;
    ret->clear_damage = clear_damage;
    return true;
}

void gsr_plugin_deinit(void *userdata) {
    Triangle *triangle = userdata;
    glDeleteProgram(triangle->shader_program);
    free(triangle);
}
