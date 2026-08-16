#include "../../include/mgl/graphics/glyph_map.h"

#include <stdlib.h>
#include <string.h>

static uint64_t glyphkey_hash(GlyphKey key) {
    uint64_t h = (uint64_t)(uintptr_t)key.font;
    h ^= (uint64_t)key.glyph * 2654435761ULL;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h | 1;   /* ensure non-zero */
}

static int glyphkey_eq(GlyphKey a, GlyphKey b) {
    return a.font == b.font && a.glyph == b.glyph;
}

static inline uint32_t probe_distance(const mgl_glyph_map *map, uint64_t hash, uint32_t slot_idx) {
    uint32_t ideal = (uint32_t)(hash & (uint64_t)(map->capacity - 1));
    return (slot_idx + map->capacity - ideal) & (map->capacity - 1);
}

void mgl_glyph_map_init(mgl_glyph_map *map, uint32_t initial_cap) {
    uint32_t cap = 64;
    while (cap < initial_cap)
        cap <<= 1;
    map->slots    = (mgl_glyph_slot *)calloc(cap, sizeof(mgl_glyph_slot));
    map->capacity = cap;
    map->count    = 0;
}

void mgl_glyph_map_deinit(mgl_glyph_map *map) {
    free(map->slots);
    map->slots    = NULL;
    map->capacity = 0;
    map->count    = 0;
}

mgl_glyph_entry *mgl_glyph_map_find(const mgl_glyph_map *map, GlyphKey key) {
    uint64_t hash = glyphkey_hash(key);
    uint32_t mask = map->capacity - 1;
    uint32_t idx  = (uint32_t)(hash & mask);
    uint32_t dist = 0;

    for (;;) {
        const mgl_glyph_slot *slot = &map->slots[idx];
        if (!slot->hash)
            return NULL;
        if (slot->hash == hash && glyphkey_eq(slot->key, key))
            return (mgl_glyph_entry *)&slot->value;
        if (probe_distance(map, slot->hash, idx) < dist)
            return NULL;
        idx = (idx + 1) & mask;
        dist++;
    }
}

void mgl_glyph_map_insert_new(mgl_glyph_map *map, GlyphKey key) {
    if (map->count * 4 >= map->capacity * 3)
        mgl_glyph_map_grow(map);

    uint64_t hash = glyphkey_hash(key);
    uint32_t mask = map->capacity - 1;
    uint32_t idx  = (uint32_t)(hash & mask);

    mgl_glyph_slot incoming;
    incoming.key  = key;
    incoming.hash = hash;
    memset(&incoming.value, 0, sizeof(mgl_glyph_entry));

    uint32_t dist = 0;
    for (;;) {
        mgl_glyph_slot *slot = &map->slots[idx];
        if (!slot->hash) {
            *slot = incoming;
            map->count++;
            return;
        }
        uint32_t existing_dist = probe_distance(map, slot->hash, idx);
        if (existing_dist < dist) {
            mgl_glyph_slot tmp = *slot;
            *slot = incoming;
            incoming = tmp;
            dist = existing_dist;
        }
        idx = (idx + 1) & mask;
        dist++;
    }
}

mgl_glyph_entry *mgl_glyph_map_get_or_insert(mgl_glyph_map *map, GlyphKey key, int *was_new) {
    mgl_glyph_entry *existing = mgl_glyph_map_find(map, key);
    if (existing) {
        if (was_new) *was_new = 0;
        return existing;
    }
    mgl_glyph_map_insert_new(map, key);
    if (was_new) *was_new = 1;
    return mgl_glyph_map_find(map, key);
}

void mgl_glyph_map_grow(mgl_glyph_map *map) {
    uint32_t   old_cap   = map->capacity;
    mgl_glyph_slot *old_slots = map->slots;
    uint32_t   new_cap   = old_cap * 2;

    map->slots    = (mgl_glyph_slot *)calloc(new_cap, sizeof(mgl_glyph_slot));
    map->capacity = new_cap;
    map->count    = 0;

    for (uint32_t i = 0; i < old_cap; i++) {
        if (old_slots[i].hash) {
            mgl_glyph_map_insert_new(map, old_slots[i].key);
            *mgl_glyph_map_find(map, old_slots[i].key) = old_slots[i].value;
        }
    }
    free(old_slots);
}
