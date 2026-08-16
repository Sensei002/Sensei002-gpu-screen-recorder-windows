#ifndef MGL_FONT_ATLAS_H
#define MGL_FONT_ATLAS_H

#include "glyph_map.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t      *pixels;
    unsigned int  texture;
    bool          dirty;
    int           shelf_x, shelf_y, shelf_h;
    int           width, height;
    mgl_glyph_map cache;
} mgl_font_atlas;

void mgl_font_atlas_init(mgl_font_atlas *atlas);
void mgl_font_atlas_deinit(mgl_font_atlas *atlas);

void mgl_font_atlas_flush(mgl_font_atlas *atlas);
const mgl_glyph_entry* mgl_font_atlas_get(mgl_font_atlas *atlas, PangoFont *font, PangoGlyph glyph_id);

#endif /* MGL_FONT_ATLAS_H */
