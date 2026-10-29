#ifndef LAMBDA_FONT_GSUB_H
#define LAMBDA_FONT_GSUB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTables FontTables;
typedef struct GsubTable GsubTable;

// registered 'vert' single substitutions, including GSUB extension lookups.
GsubTable* font_gsub_parse(FontTables* tables, void* pool);
// absent coverage succeeds unchanged; malformed/unsupported data returns false.
bool gsub_vertical_glyph(GsubTable* table, uint16_t glyph, uint16_t* result);

#ifdef __cplusplus
}
#endif
#endif
