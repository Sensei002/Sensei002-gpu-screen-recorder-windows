#ifndef GSR_SHADER_H
#define GSR_SHADER_H

#include <stdbool.h>

typedef struct gsr_egl gsr_egl;

typedef struct {
    gsr_egl *egl;
    unsigned int program_id;
} gsr_shader;

/* |vertex_shader| or |fragment_shader| may be NULL */
int gsr_shader_init(gsr_shader *self, gsr_egl *egl, const char *vertex_shader, const char *fragment_shader);
void gsr_shader_deinit(gsr_shader *self);

int gsr_shader_bind_attribute_location(gsr_shader *self, const char *attribute, int location);
void gsr_shader_use(gsr_shader *self);
void gsr_shader_use_none(gsr_shader *self);

void gsr_shader_enable_debug_output(bool enable);

#endif /* GSR_SHADER_H */
