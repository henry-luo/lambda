
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
    // CSS 2.1 §15.6: Use numeric weight for precise matching (100-900)
    FontWeight fw = FONT_WEIGHT_NORMAL;
    if (fprop->font_weight_numeric > 0) {
        // Use precise numeric weight from CSS (100-900)
        fw = (FontWeight)fprop->font_weight_numeric;
    } else if (fprop->font_weight == CSS_VALUE_BOLD || fprop->font_weight == CSS_VALUE_BOLDER) {
        fw = FONT_WEIGHT_BOLD;
    } else if (fprop->font_weight == CSS_VALUE_LIGHTER) {
        fw = FONT_WEIGHT_LIGHT;
    }

    FontSlant fs = FONT_SLANT_NORMAL;
    if (fprop->font_style == CSS_VALUE_ITALIC) fs = FONT_SLANT_ITALIC;
    else if (fprop->font_style == CSS_VALUE_OBLIQUE) fs = FONT_SLANT_OBLIQUE;

    // Font5 §4.1: If the parent fbox already has a handle with identical font
    // properties, reuse it directly — skip font_resolve() entirely.
    FontHandle* parent_handle = fbox->font_handle;
    if (parent_handle && fprop->family) {
        const char* ph_family = NULL;
        float ph_size = 0;
        FontWeight ph_weight = FONT_WEIGHT_NORMAL;
        FontSlant ph_slant = FONT_SLANT_NORMAL;
        if (font_handle_get_style(parent_handle, &ph_family, &ph_size, &ph_weight, &ph_slant)) {
            if (ph_family && strcmp(ph_family, fprop->family) == 0 &&
                ph_size == fprop->font_size &&
                ph_weight == fw &&
                ph_slant == fs) {
                // identical font — reuse parent handle
                font_handle_retain(parent_handle);
                fbox->font_handle = parent_handle;
                fprop->font_handle = parent_handle;
                const FontMetrics* m = font_get_metrics(parent_handle);
                if (m) {
                    fprop->space_width = m->space_width;
                    {
                        GlyphInfo sp = font_get_glyph(parent_handle, (uint32_t)' ');
                        if (sp.advance_x > 0.0f) fprop->space_width = sp.advance_x;
                    }
                    float _lh_asc, _lh_desc;
                    font_get_normal_lh_split(parent_handle, &_lh_asc, &_lh_desc);
                    fprop->ascender    = _lh_asc;
                    fprop->descender   = _lh_desc;
                    fprop->font_height = m->hhea_line_height;
                    fprop->has_kerning = m->has_kerning;
                }
                return;
            }
        }
    }

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
        // When the font has CoreText metrics (e.g., SFNS / -apple-system), prefer
        // the CT-based space advance over FreeType to match Chrome text widths.
        // font_get_glyph() returns CT advance in CSS pixels when ct_font_ref is set.
        {
            GlyphInfo sp = font_get_glyph(handle, (uint32_t)' ');
            if (sp.advance_x > 0.0f) fprop->space_width = sp.advance_x;
        }
            // Use normal line-height split (platform-based with half-leading) for
            // ascender/descender.  This ensures font->ascender == init_ascender used
            // by the layout engine's strut baseline, which is critical for correct
            // vertical alignment: when the inline font matches the block font the
            // vertical-align pass is skipped and the text baseline falls at
            // text_rect.y + font->ascender, which must equal init_ascender + lead_y.
            float _lh_asc, _lh_desc;
            font_get_normal_lh_split(handle, &_lh_asc, &_lh_desc);
            fprop->ascender    = _lh_asc;
            fprop->descender   = _lh_desc;
            fprop->font_height = m->hhea_line_height;
            fprop->has_kerning = m->has_kerning;
        }
        return;
    }

    log_error("setup_font: font_resolve failed for '%s' (and all fallbacks)", fprop->family);
}

void fontface_cleanup(UiContext* uicon) {
    // font faces are now managed by FontContext — no separate cache to clean up
    (void)uicon;
}
