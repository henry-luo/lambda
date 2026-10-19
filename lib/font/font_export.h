#ifndef LAMBDA_FONT_EXPORT_H
#define LAMBDA_FONT_EXPORT_H

#include "font.h"

// shared with native rasterization: WebKit-compatible synthetic italic (~12 degrees).
#define FONT_SYNTHETIC_ITALIC_SHEAR 0.2126f

#ifdef __cplusplus
extern "C" {
#endif

// an owned, standalone selected face; release bytes with font_export_free().
typedef struct FontExport {
    uint8_t* bytes;
    size_t length;
    uint64_t fingerprint;
    const char* postscript_name; // borrowed from the retained FontHandle
    float bbox[4], ascent, descent, cap_height, italic_angle;
    uint16_t units_per_em;
    bool cff;
    bool monospace;
    bool glyph_program; // native/variable/bitmap faces need generated PDF glyph programs
    bool bitmap_only;
} FontExport;

bool font_export_face(FontHandle* font, FontExport* result, const char** error);
void font_export_free(FontExport* result);
float font_export_glyph_width(FontHandle* font, uint32_t glyph);
float font_export_italic_shear(FontHandle* font);

#ifdef __cplusplus
}
#endif
#endif
