
#include <string.h>
#include <stdlib.h>
#include "view.hpp"
#include "font_face.h"

#include "../lib/log.h"
#include "../lib/font/font.h"
#include "../lib/memtrack.h"

char* load_font_path(FontContext *font_ctx, const char* font_name) {
    if (!font_ctx || !font_name) {
        log_warn("load_font_path: invalid parameters: font_ctx=%p, font_name=%p", font_ctx, font_name);
        return NULL;
    }

    // Use the unified font module to find the best Regular-weight font file path
    return font_find_path(font_ctx, font_name);
}

void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    fbox->style = fprop;
    fbox->current_font_size = fprop->font_size;
    fbox->font_handle = NULL;

    if (!uicon || !uicon->font_ctx) {
        log_error("setup_font: missing UiContext or FontContext");
        return;
    }

    // map CssEnum weight/style → FontWeight/FontSlant
    FontWeight fw = FONT_WEIGHT_NORMAL;
    if (fprop->font_weight == CSS_VALUE_BOLD || fprop->font_weight == CSS_VALUE_BOLDER) fw = FONT_WEIGHT_BOLD;
    else if (fprop->font_weight == CSS_VALUE_LIGHTER) fw = FONT_WEIGHT_LIGHT;

    FontSlant fs = FONT_SLANT_NORMAL;
    if (fprop->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
    else if (fprop->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

    FontStyleDesc style = {};
    style.family  = fprop->family;
    style.size_px = fprop->font_size;
    style.weight  = fw;
    style.slant   = fs;

    // font_resolve handles everything: @font-face descriptors, generic families,
    // database lookup, platform fallback, and fallback font chain — all with caching.
    FontHandle* handle = font_resolve(uicon->font_ctx, &style);
    if (handle) {
        fbox->font_handle = handle;
        fprop->font_handle = handle;

        // populate FontProp derived fields from unified metrics
        const FontMetrics* m = font_get_metrics(handle);
        if (m) {
            fprop->space_width = m->space_width;
            fprop->ascender    = m->hhea_ascender;
            fprop->descender   = -(m->hhea_descender); // FontMetrics.hhea_descender is negative, FontProp expects positive
            fprop->font_height = m->hhea_line_height;
            fprop->has_kerning = m->has_kerning;
        }
        return;
    }

    log_error("setup_font: font_resolve failed for '%s' (and all fallbacks)", fprop->family);
}

void fontface_cleanup(UiContext* uicon) {
    // font faces are now managed by FontContext — no separate FT_Face cache to clean up
    (void)uicon;
}
