#include "font_gsub.h"
#include "font_tables.h"
#include "../endian.h"
#include "../mempool.h"

struct GsubTable {
    const uint8_t* data;
    uint32_t length;
    uint32_t* lookups;
    uint16_t count;
    uint16_t glyph_count;
};

static void gsub_collect_vertical_lookup(void* pointer, uint32_t feature,
        uint16_t index, uint32_t offset) {
    GsubTable* table = (GsubTable*)pointer;
    if (feature == FONT_TAG('v','e','r','t') && index < table->count)
        table->lookups[index] = offset;
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
        sizeof(GsubTable) + (size_t)count * sizeof(uint32_t));
    if (!result) return NULL;
    result->data = data;
    result->length = length;
    result->lookups = (uint32_t*)(result + 1);
    result->count = count;
    result->glyph_count = maxp->num_glyphs;
    if (!font_layout_visit_features(data, length, gsub_collect_vertical_lookup, result)) {
        pool_free((Pool*)pool, result);
        return NULL;
    }
    // repeated feature records can share lookups; apply each active lookup once in LookupList order.
    uint16_t active = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (result->lookups[i]) result->lookups[active++] = result->lookups[i];
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

bool gsub_vertical_glyph(GsubTable* table, uint16_t glyph, uint16_t* result) {
    if (!result) return false;
    *result = glyph;
    if (!table) return true;
    if (glyph >= table->glyph_count) return false;
    uint16_t current = glyph;
    for (uint16_t i = 0; i < table->count; i++) {
        uint32_t lookup = table->lookups[i];
        // the registered vert feature uses unfiltered single substitution; other shaping needs GDEF context.
        if (read_be16(table->data + lookup + 2) != 0) return false;
        GsubGlyphQuery query = {current, current, table->glyph_count, false, false};
        bool valid = font_layout_visit_lookup(table->data, table->length, lookup, 7,
            gsub_single_substitution, &query);
        if (query.invalid || (!valid && !query.found)) return false;
        if (query.found) current = query.result;
    }
    *result = current;
    return true;
}
