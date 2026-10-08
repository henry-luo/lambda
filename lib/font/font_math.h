#ifndef LAMBDA_FONT_MATH_H
#define LAMBDA_FONT_MATH_H

#include "font_tables.h"

#ifdef __cplusplus
extern "C" {
#endif

// borrowed, bounds-checked OpenType views; all distances are font design units.
typedef struct FontMathTable {
    const uint8_t* data;
    uint32_t length;
} FontMathTable;

typedef enum FontMathConstant {
#define FONT_MATH_CONSTANT(name, offset, kind) FONT_MATH_##name,
#include "font_math_constants.def"
#undef FONT_MATH_CONSTANT
    FONT_MATH_CONSTANT_COUNT
} FontMathConstant;

typedef struct FontMathGlyph {
    int16_t italic, accent;
    bool has_italic, has_accent, extended;
    FontMathTable kern[4]; // top-right, top-left, bottom-right, bottom-left
} FontMathGlyph;

typedef struct FontMathConstruction {
    FontMathTable variants, assembly;
    uint16_t variant_count, part_count, min_overlap;
    int16_t italic;
} FontMathConstruction;

typedef struct FontMathVariant {
    uint16_t glyph, advance;
} FontMathVariant;

typedef struct FontMathPart {
    uint16_t glyph, start_connector, end_connector, advance;
    bool extender;
} FontMathPart;

bool font_math_open(FontTables* tables, FontMathTable* math);
const char* font_math_constant_name(FontMathConstant constant);
bool font_math_constant_is_percent(FontMathConstant constant);
bool font_math_constant(const FontMathTable* math, FontMathConstant constant, int32_t* value);
bool font_math_glyph(const FontMathTable* math, uint16_t glyph, FontMathGlyph* info);
bool font_math_kern_entry(const FontMathTable* kern, uint16_t index, int16_t* height, int16_t* value);
bool font_math_construction(const FontMathTable* math, uint16_t glyph, bool vertical,
    FontMathConstruction* construction);
bool font_math_variant(const FontMathConstruction* construction, uint16_t index, FontMathVariant* variant);
bool font_math_part(const FontMathConstruction* construction, uint16_t index, FontMathPart* part);

#ifdef __cplusplus
}
#endif
#endif
