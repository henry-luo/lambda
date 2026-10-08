#include "font_math.h"
#include "../endian.h"
#include <string.h>

enum MathConstantKind { VALUE, UNSIGNED, PERCENT };
static const struct {
    const char* name;
    uint16_t offset;
    enum MathConstantKind kind;
} math_constants[] = {
#define FONT_MATH_CONSTANT(name, offset, kind) {#name, offset, kind},
#include "font_math_constants.def"
#undef FONT_MATH_CONSTANT
};

// offsets are relative to their owning subtable, never to the whole font.
static bool math_subtable(const FontMathTable* parent, uint32_t offset, uint32_t minimum,
    FontMathTable* child) {
    *child = (FontMathTable){0};
    if (!parent->data || !offset || !font_data_range_valid(parent->length, offset, minimum)) return false;
    child->data = parent->data + offset;
    child->length = parent->length - offset;
    return true;
}

bool font_math_open(FontTables* tables, FontMathTable* math) {
    if (!math) return false;
    *math = (FontMathTable){0};
    if (!tables) return false;
    math->data = font_tables_find(tables, FONT_TAG('M','A','T','H'), &math->length);
    if (!math->data || math->length < 10 || read_be16(math->data) != 1 || read_be16(math->data + 2) != 0) return false;
    FontMathTable sub;
    return math_subtable(math, read_be16(math->data + 4), 214, &sub) &&
        math_subtable(math, read_be16(math->data + 6), 8, &sub) &&
        math_subtable(math, read_be16(math->data + 8), 10, &sub);
}

const char* font_math_constant_name(FontMathConstant constant) {
    return (unsigned)constant < FONT_MATH_CONSTANT_COUNT ? math_constants[constant].name : NULL;
}

bool font_math_constant_is_percent(FontMathConstant constant) {
    return (unsigned)constant < FONT_MATH_CONSTANT_COUNT && math_constants[constant].kind == PERCENT;
}

bool font_math_constant(const FontMathTable* math, FontMathConstant constant, int32_t* value) {
    if (!math || !math->data || math->length < 10 || !value || (unsigned)constant >= FONT_MATH_CONSTANT_COUNT) return false;
    FontMathTable constants;
    if (!math_subtable(math, read_be16(math->data + 4), 214, &constants)) return false;
    const uint8_t* data = constants.data + math_constants[constant].offset;
    // device corrections are deliberately ignored for resolution-independent layout.
    *value = math_constants[constant].kind == VALUE || constant < 2 ? read_be16s(data) : read_be16(data);
    return true;
}

static int math_coverage(const FontMathTable* table, uint16_t offset, uint16_t glyph) {
    FontMathTable coverage;
    return math_subtable(table, offset, 4, &coverage)
        ? font_coverage_lookup(coverage.data, coverage.length, glyph) : -2;
}

static bool math_glyph_value(const FontMathTable* info, uint16_t offset, uint16_t glyph,
    int16_t* value, bool* present) {
    *present = false;
    if (!offset) return true;
    FontMathTable table;
    if (!math_subtable(info, offset, 4, &table)) return false;
    uint16_t count = read_be16(table.data + 2);
    if (!font_data_range_valid(table.length, 4, (size_t)count * 4)) return false;
    int index = math_coverage(&table, read_be16(table.data), glyph);
    if (index < -1) return false;
    if (index < 0) return true;
    if (index >= count) return false;
    *present = true;
    *value = read_be16s(table.data + 4 + (size_t)index * 4);
    return true;
}

bool font_math_glyph(const FontMathTable* math, uint16_t glyph, FontMathGlyph* info) {
    if (!math || !math->data || math->length < 10 || !info) return false;
    *info = (FontMathGlyph){0};
    FontMathTable table;
    if (!math_subtable(math, read_be16(math->data + 6), 8, &table)) return false;
    if (!math_glyph_value(&table, read_be16(table.data), glyph, &info->italic, &info->has_italic) ||
        !math_glyph_value(&table, read_be16(table.data + 2), glyph, &info->accent, &info->has_accent)) return false;
    uint16_t extended = read_be16(table.data + 4), kern = read_be16(table.data + 6);
    int extended_index = extended ? math_coverage(&table, extended, glyph) : -1;
    if (extended_index < -1) return false;
    info->extended = extended_index >= 0;
    if (!kern) return true;
    FontMathTable kern_info;
    if (!math_subtable(&table, kern, 4, &kern_info)) return false;
    uint16_t count = read_be16(kern_info.data + 2);
    if (!font_data_range_valid(kern_info.length, 4, (size_t)count * 8)) return false;
    int index = math_coverage(&kern_info, read_be16(kern_info.data), glyph);
    if (index < -1) return false;
    if (index < 0) return true;
    if (index >= count) return false;
    for (unsigned corner = 0; corner < 4; corner++) {
        uint16_t offset = read_be16(kern_info.data + 4 + (size_t)index * 8 + corner * 2);
        if (!offset) continue;
        if (!math_subtable(&kern_info, offset, 2, &info->kern[corner])) return false;
        uint16_t heights = read_be16(info->kern[corner].data);
        if (!font_data_range_valid(info->kern[corner].length, 2, (size_t)heights * 8 + 4)) return false;
    }
    return true;
}

bool font_math_kern_entry(const FontMathTable* kern, uint16_t index, int16_t* height, int16_t* value) {
    if (!kern || !kern->data || kern->length < 2 || !height || !value) return false;
    uint16_t count = read_be16(kern->data);
    if (index > count || !font_data_range_valid(kern->length, 2, (size_t)count * 8 + 4)) return false;
    *height = index < count ? read_be16s(kern->data + 2 + (size_t)index * 4) : 0;
    *value = read_be16s(kern->data + 2 + (size_t)(count + index) * 4);
    return true;
}

bool font_math_construction(const FontMathTable* math, uint16_t glyph, bool vertical,
    FontMathConstruction* construction) {
    if (!math || !math->data || math->length < 10 || !construction) return false;
    *construction = (FontMathConstruction){0};
    FontMathTable variants;
    if (!math_subtable(math, read_be16(math->data + 8), 10, &variants)) return false;
    construction->min_overlap = read_be16(variants.data);
    uint16_t vert_count = read_be16(variants.data + 6), horiz_count = read_be16(variants.data + 8);
    if (!font_data_range_valid(variants.length, 10, ((size_t)vert_count + horiz_count) * 2)) return false;
    uint16_t coverage = read_be16(variants.data + (vertical ? 2 : 4));
    if (!coverage) return true;
    int index = math_coverage(&variants, coverage, glyph);
    if (index < -1) return false;
    if (index < 0) return true;
    if (index >= (vertical ? vert_count : horiz_count)) return false;
    size_t slot = vertical ? (size_t)index : (size_t)vert_count + index;
    FontMathTable table;
    if (!math_subtable(&variants, read_be16(variants.data + 10 + slot * 2), 4, &table)) return false;
    construction->variant_count = read_be16(table.data + 2);
    if (!font_data_range_valid(table.length, 4, (size_t)construction->variant_count * 4)) return false;
    construction->variants = table;
    uint16_t assembly = read_be16(table.data);
    if (!assembly) return true;
    if (!math_subtable(&table, assembly, 6, &construction->assembly)) return false;
    construction->italic = read_be16s(construction->assembly.data);
    construction->part_count = read_be16(construction->assembly.data + 4);
    return font_data_range_valid(construction->assembly.length, 6, (size_t)construction->part_count * 10);
}

bool font_math_variant(const FontMathConstruction* construction, uint16_t index, FontMathVariant* variant) {
    if (!construction || !variant || index >= construction->variant_count) return false;
    const uint8_t* data = construction->variants.data + 4 + (size_t)index * 4;
    variant->glyph = read_be16(data); variant->advance = read_be16(data + 2);
    return true;
}

bool font_math_part(const FontMathConstruction* construction, uint16_t index, FontMathPart* part) {
    if (!construction || !part || index >= construction->part_count) return false;
    const uint8_t* data = construction->assembly.data + 6 + (size_t)index * 10;
    part->glyph = read_be16(data); part->start_connector = read_be16(data + 2);
    part->end_connector = read_be16(data + 4); part->advance = read_be16(data + 6);
    part->extender = (read_be16(data + 8) & 1) != 0;
    return part->start_connector <= part->advance && part->end_connector <= part->advance;
}
