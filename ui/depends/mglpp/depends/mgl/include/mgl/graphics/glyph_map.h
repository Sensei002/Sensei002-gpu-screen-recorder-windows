#ifndef MGL_GLYPH_MAP_H
#define MGL_GLYPH_MAP_H

/* =========================================================================
 * Robin Hood hash map (open-addressing, power-of-two, 75% max load)
 * ========================================================================= */

#include <stdint.h>

typedef struct _PangoFont PangoFont;
typedef uint32_t PangoGlyph;

typedef struct {
    PangoFont  *font;
    PangoGlyph  glyph;
} GlyphKey;

typedef struct {
    float u0, v0, u1, v1;
    int   width, height;
    int   bearing_x, bearing_y;
    int   advance_x;
} mgl_glyph_entry;

typedef struct {
    GlyphKey        key;
    mgl_glyph_entry value;
    uint64_t        hash;    /* 0 = empty slot */
} mgl_glyph_slot;

typedef struct {
    mgl_glyph_slot *slots;
    uint32_t        capacity;
    uint32_t        count;
} mgl_glyph_map;

void mgl_glyph_map_init(mgl_glyph_map *map, uint32_t initial_cap);
void mgl_glyph_map_deinit(mgl_glyph_map *map);

mgl_glyph_entry *mgl_glyph_map_find(const mgl_glyph_map *map, GlyphKey key);
void mgl_glyph_map_insert_new(mgl_glyph_map *map, GlyphKey key);
mgl_glyph_entry *mgl_glyph_map_get_or_insert(mgl_glyph_map *map, GlyphKey key, int *was_new);
void mgl_glyph_map_grow(mgl_glyph_map *map);

#endif /* MGL_GLYPH_MAP_H */
