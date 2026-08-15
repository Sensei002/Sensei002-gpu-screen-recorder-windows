#ifndef GSR_KDE_NIGHT_LIGHT_H
#define GSR_KDE_NIGHT_LIGHT_H

#include "defs.h"
#include <stdbool.h>

typedef struct gsr_kde_night_light gsr_kde_night_light;

typedef enum {
    GSR_NIGHT_LIGHT_COMPENSATION_TINT,                /* The captured image contains the night light tint, including its luminance dimming */
    GSR_NIGHT_LIGHT_COMPENSATION_TINT_KEEP_LUMINANCE  /* The captured image contains the tint and the luminance dimming is normalized away by the compositors reference luminance */
} gsr_night_light_compensation;

gsr_kde_night_light* gsr_kde_night_light_create(void);
void gsr_kde_night_light_destroy(gsr_kde_night_light *self);
/* Returns true when night light is active. |inverse_matrix| is filled with the row major 3x3 matrix that removes the night light color change from linear rgb values.
   |container_primaries| is the color volume that the captured image is encoded in, or NULL for sRGB */
bool gsr_kde_night_light_get_inverse_matrix(gsr_kde_night_light *self, gsr_night_light_compensation compensation, const gsr_color_chromaticities *container_primaries, float inverse_matrix[9]);

#endif /* GSR_KDE_NIGHT_LIGHT_H */
