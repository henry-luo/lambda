#include "font_gsub.h"
#include "font_tables.h"
#include "../endian.h"
#include "../mempool.h"

typedef struct GsubLookup {
    uint32_t offset;
    uint8_t features;
} GsubLookup;

struct GsubTable {
    const uint8_t* data;
    uint32_t length;
    GsubLookup* lookups;
    uint16_t count;
    uint16_t glyph_count;
};

static uint8_t gsub_feature_mask(uint32_t feature) {
    const uint32_t tags[] = {FONT_TAG('v','e','r','t'), FONT_TAG('h','w','i','d'),
        FONT_TAG('t','w','i','d'), FONT_TAG('q','w','i','d')};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (feature == tags[i]) return (uint8_t)(1u << i);
    }
    return 0;
}

static void gsub_collect_feature_lookup(void* pointer, uint32_t feature,
        uint16_t index, uint32_t offset) {
    GsubTable* table = (GsubTable*)pointer;
    uint8_t mask = gsub_feature_mask(feature);
    if (mask && index < table->count) {
        table->lookups[index].offset = offset;
        table->lookups[index].features |= mask;
    }
}

GsubTable* font_gsub_parse(FontTables* tables, void* pool) {
    if (!tables || !pool) return NULL;
    uint32_t length = 0;
    const uint8_t* data = font_tables_find(tables, FONT_TAG('G','S','U','B'), &length);
    MaxpTable* maxp = font_tables_get_maxp(tables);
    if (!data || length < 10 || !maxp) return NULL;
    uint32_t lookup_offset = read_be16(data + 8);
    if (!font_data_range_valid(length, lookup_offset, 2)) return NULL;
    uint16_t count = read_be16(data + lookup_offset);
    if (!count || !font_data_range_valid(length, lookup_offset + 2, (size_t)count * 2)) return NULL;
    GsubTable* result = (GsubTable*)pool_calloc((Pool*)pool,
        sizeof(GsubTable) + (size_t)count * sizeof(GsubLookup));
    if (!result) return NULL;
    result->data = data;
    result->length = length;
    result->lookups = (GsubLookup*)(result + 1);
    result->count = count;
    result->glyph_count = maxp->num_glyphs;
    if (!font_layout_visit_features(data, length, gsub_collect_feature_lookup, result)) {
        pool_free((Pool*)pool, result);
        return NULL;
    }
    // repeated feature records can share lookups; apply each active lookup once in LookupList order.
    uint16_t active = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (result->lookups[i].features) result->lookups[active++] = result->lookups[i];
    }
    if (!active) { pool_free((Pool*)pool, result); return NULL; }
    result->count = active;
    return result;
}

typedef struct GsubGlyphQuery {
    uint16_t glyph;
    uint16_t result;
    uint16_t glyph_count;
    bool found;
    bool invalid;
} GsubGlyphQuery;

static bool gsub_single_substitution(void* pointer, uint16_t type,
        const uint8_t* data, uint32_t length) {
    GsubGlyphQuery* query = (GsubGlyphQuery*)pointer;
    if (type != 1 || length < 6) { query->invalid = true; return false; }
    uint16_t format = read_be16(data), coverage = read_be16(data + 2);
    if (coverage >= length || (format != 1 && format != 2)) {
        query->invalid = true;
        return false;
    }
    int index = font_coverage_lookup(data + coverage, length - coverage, query->glyph);
    if (index == -2) { query->invalid = true; return false; }
    if (index < 0) return true;
    if (format == 1) {
        query->result = (uint16_t)(query->glyph + read_be16s(data + 4));
    } else {
        uint16_t count = read_be16(data + 4);
        if (index >= count || !font_data_range_valid(length, 6, (size_t)count * 2)) {
            query->invalid = true;
            return false;
        }
        query->result = read_be16(data + 6 + (size_t)index * 2);
    }
    query->invalid = query->result >= query->glyph_count;
    query->found = !query->invalid;
    return false;
}

bool gsub_feature_glyph(GsubTable* table, uint32_t feature, uint16_t glyph,
        uint16_t* result, bool* covered) {
    uint8_t mask = gsub_feature_mask(feature);
    if (!result || !mask) return false;
    if (covered) *covered = false;
    *result = glyph;
    if (!table) return true;
    if (glyph >= table->glyph_count) return false;
    uint16_t current = glyph;
    for (uint16_t i = 0; i < table->count; i++) {
        if (!(table->lookups[i].features & mask)) continue;
        uint32_t lookup = table->lookups[i].offset;
        // registered single substitutions are unfiltered; contextual shaping needs GDEF-aware handling.
        if (read_be16(table->data + lookup + 2) != 0) return false;
        GsubGlyphQuery query = {current, current, table->glyph_count, false, false};
        bool valid = font_layout_visit_lookup(table->data, table->length, lookup, 7,
            gsub_single_substitution, &query);
        if (query.invalid || (!valid && !query.found)) return false;
        if (query.found) {
            current = query.result;
            if (covered) *covered = true;
        }
    }
    *result = current;
    return true;
}

bool gsub_vertical_glyph(GsubTable* table, uint16_t glyph, uint16_t* result) {
    return gsub_feature_glyph(table, FONT_TAG('v','e','r','t'), glyph, result, NULL);
}
