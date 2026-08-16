#include "../../include/mgl/graphics/font_atlas.h"
#include "../../include/mgl/mgl.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <pango/pangofc-font.h>
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

#define WGHT_TAG ((FT_ULong)(((FT_ULong)'w' << 24) | ((FT_ULong)'g' << 16) | ((FT_ULong)'h' << 8) | (FT_ULong)'t'))

/* Returns true if the face was reconfigured via variation axes for the requested weight. */
static bool apply_variation_for_pango_font(FT_Face face, PangoWeight pango_weight) {
    if (!FT_HAS_MULTIPLE_MASTERS(face))
        return false;

    FT_MM_Var *mm = NULL;
    if (FT_Get_MM_Var(face, &mm) != 0 || !mm)
        return false;

    FT_Fixed coords[16];
    FT_UInt num = mm->num_axis;
    if (num > 16)
        num = 16;

    bool has_wght = false;
    for (FT_UInt i = 0; i < num; i++) {
        if (mm->axis[i].tag == WGHT_TAG) {
            coords[i] = ((FT_Fixed)pango_weight) << 16;
            has_wght = true;
        } else {
            coords[i] = mm->axis[i].def;
        }
    }

    FT_Set_Var_Design_Coordinates(face, num, coords);
    FT_Done_MM_Var(face->glyph->library, mm);
    return has_wght;
}

#define ATLAS_W     1024
#define ATLAS_H     1024
#define ATLAS_PAD   2

static uint8_t gamma_lut[256];
static bool gamma_built = false;

static void build_gamma_lut(float gamma) {
    float inv_gamma = 1.0f / gamma;
    gamma_lut[0]   = 0;
    gamma_lut[255] = 255;
    for (int i = 1; i < 255; i++) {
        float corrected = powf((float)i / 255.0f, inv_gamma);
        int value = (int)(corrected * 255.0f + 0.5f);
        gamma_lut[i] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
    }
}

void mgl_font_atlas_init(mgl_font_atlas *atlas) {
    mgl_context *context = mgl_get_context();
    assert(context->current_window);

    if(!gamma_built) {
        gamma_built = true;
        build_gamma_lut(1.0f);
    }

    atlas->width = ATLAS_W;
    atlas->height = ATLAS_H;
    atlas->pixels  = (uint8_t *)calloc(1, (size_t)atlas->width * atlas->height);
    atlas->dirty   = false;
    atlas->shelf_x = ATLAS_PAD;
    atlas->shelf_y = ATLAS_PAD;
    atlas->shelf_h = 0;

    mgl_glyph_map_init(&atlas->cache, 512);

    context->gl.glGenTextures(1, &atlas->texture);
    context->gl.glBindTexture(GL_TEXTURE_2D, atlas->texture);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    context->gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, atlas->width, atlas->height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
}

void mgl_font_atlas_deinit(mgl_font_atlas *atlas) {
    mgl_context *context = mgl_get_context();

    if (atlas->texture) {
        context->gl.glDeleteTextures(1, &atlas->texture);
        atlas->texture = 0;
    }

    free(atlas->pixels);
    atlas->pixels = NULL;
    mgl_glyph_map_deinit(&atlas->cache);
}

/* Double atlas height, copy existing pixels in-place, rescale cached UVs. */
static bool mgl_font_atlas_grow(mgl_font_atlas *atlas) {
    mgl_context *context = mgl_get_context();

    int new_height = atlas->height * 2;
    /* Guard against unreasonably large atlases */
    if (new_height > 8192)
        return false;

    uint8_t *new_pixels = (uint8_t *)calloc(1, (size_t)atlas->width * new_height);
    if (!new_pixels)
        return false;

    /* Preserve every existing row at the same pixel position */
    memcpy(new_pixels, atlas->pixels, (size_t)atlas->width * atlas->height);
    free(atlas->pixels);
    atlas->pixels = new_pixels;

    /* Rescale normalised v-coordinates stored in every cached entry */
    float v_scale = (float)atlas->height / (float)new_height;
    for (uint32_t i = 0; i < atlas->cache.capacity; i++) {
        mgl_glyph_slot *slot = &atlas->cache.slots[i];
        if (slot->hash) {
            slot->value.v0 *= v_scale;
            slot->value.v1 *= v_scale;
        }
    }

    atlas->height = new_height;

    /* Resize the GL texture in-place; flush will upload the pixels */
    context->gl.glBindTexture(GL_TEXTURE_2D, atlas->texture);
    context->gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, atlas->width, atlas->height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    atlas->dirty = true;
    return true;
}

static bool mgl_font_atlas_insert(mgl_font_atlas *atlas, const uint8_t *coverage,
                                  int width, int height, int *out_x, int *out_y) {
    if (atlas->shelf_x + width + ATLAS_PAD > atlas->width) {
        atlas->shelf_y += atlas->shelf_h + ATLAS_PAD;
        atlas->shelf_x  = ATLAS_PAD;
        atlas->shelf_h  = 0;
    }
    if (atlas->shelf_y + height + ATLAS_PAD > atlas->height) {
        if (!mgl_font_atlas_grow(atlas))
            return false;
        if (atlas->shelf_y + height + ATLAS_PAD > atlas->height)
            return false;
    }

    *out_x = atlas->shelf_x;
    *out_y = atlas->shelf_y;

    for (int row = 0; row < height; row++) {
        uint8_t       *dst = atlas->pixels + (atlas->shelf_y + row) * atlas->width + atlas->shelf_x;
        const uint8_t *src = coverage + row * width;
        for (int col = 0; col < width; col++)
            dst[col] = gamma_lut[src[col]];
    }

    atlas->shelf_x += width + ATLAS_PAD;
    if (height > atlas->shelf_h)
        atlas->shelf_h = height;
    atlas->dirty = true;
    return true;
}

void mgl_font_atlas_flush(mgl_font_atlas *atlas) {
    mgl_context *context = mgl_get_context();

    if (!atlas->dirty)
        return;

    context->gl.glBindTexture(GL_TEXTURE_2D, atlas->texture);
    // TODO: Do this smarter. Only update the regions that have changed since last time.
    // Maybe upload the atlas glyph immediately as a sub image in the destination region.
    // That would also allow removing atlas->pixels
    context->gl.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlas->width, atlas->height, GL_ALPHA, GL_UNSIGNED_BYTE, atlas->pixels);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    atlas->dirty = false;
}

const mgl_glyph_entry* mgl_font_atlas_get(mgl_font_atlas *atlas, PangoFont *font, PangoGlyph glyph_id) {
    GlyphKey key = { font, glyph_id };
    int was_new = 0;
    mgl_glyph_entry *entry = mgl_glyph_map_get_or_insert(&atlas->cache, key, &was_new);
    if (!was_new)
        return entry;

    memset(entry, 0, sizeof(*entry));

    FT_Face face = pango_fc_font_lock_face(PANGO_FC_FONT(font));
    if (!face)
        return entry;

    if (FT_HAS_COLOR(face)) {
        pango_fc_font_unlock_face(PANGO_FC_FONT(font));
        return entry;
    }

    PangoWeight pango_weight = PANGO_WEIGHT_NORMAL;
    PangoFontDescription *pdesc = pango_font_describe(font);
    if (pdesc) {
        pango_weight = pango_font_description_get_weight(pdesc);
        pango_font_description_free(pdesc);
    }

    bool wght_applied = apply_variation_for_pango_font(face, pango_weight);

    FT_Error err = FT_Load_Glyph(face, glyph_id, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP);

    /* Synthesize bold if the user asked for bold but the face isn't already bold
       (no variable-weight axis applied, and no native bold style flag). */
    if (!err
        && pango_weight >= PANGO_WEIGHT_SEMIBOLD
        && !wght_applied
        && !(face->style_flags & FT_STYLE_FLAG_BOLD)
        && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        FT_Pos strength = FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / 48;
        FT_Outline_Embolden(&face->glyph->outline, strength);
    }

    if (!err)
        err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LIGHT);

    if (!err) {
        FT_Bitmap *bm = &face->glyph->bitmap;
        entry->width     = (int)bm->width;
        entry->height    = (int)bm->rows;
        entry->bearing_x = face->glyph->bitmap_left;
        entry->bearing_y = face->glyph->bitmap_top;
        entry->advance_x = (int)(face->glyph->advance.x) >> 6;

        if (entry->width > 0 && entry->height > 0) {
            int pitch = abs(bm->pitch);
            const uint8_t *src = bm->buffer;
            uint8_t *tight = NULL;

            if (pitch != entry->width) {
                tight = (uint8_t *)malloc((size_t)(entry->width * entry->height));
                for (int row = 0; row < entry->height; row++)
                    memcpy(tight + row * entry->width,
                           bm->buffer + row * pitch,
                           (size_t)entry->width);
                src = tight;
            }

            int atlas_x, atlas_y;
            if (mgl_font_atlas_insert(atlas, src, entry->width, entry->height, &atlas_x, &atlas_y)) {
                entry->u0 = (float)atlas_x / atlas->width;
                entry->v0 = (float)atlas_y / atlas->height;
                entry->u1 = (float)(atlas_x + entry->width) / atlas->width;
                entry->v1 = (float)(atlas_y + entry->height) / atlas->height;
            }
            free(tight);
        }
    }

    pango_fc_font_unlock_face(PANGO_FC_FONT(font));
    return entry;
}
