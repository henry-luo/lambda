#ifndef LAMBDA_FONT_GSUB_H
#define LAMBDA_FONT_GSUB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTables FontTables;
typedef struct GsubTable GsubTable;

// registered vertical and width-variant single substitutions, including extension lookups.
GsubTable* font_gsub_parse(FontTables* tables, void* pool);
// absent coverage succeeds unchanged; malformed/unsupported data returns false.
bool gsub_vertical_glyph(GsubTable* table, uint16_t glyph, uint16_t* result);
bool gsub_feature_glyph(GsubTable* table, uint32_t feature, uint16_t glyph,
    uint16_t* result, bool* covered);

#ifdef __cplusplus
}
#endif
#endif
