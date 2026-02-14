/**
 * Lambda Unified Font Module — Font Metrics
 *
 * Extracts and caches per-face metrics: FreeType size metrics, OS/2
 * table values (typo ascender/descender, x-height, cap-height),
 * hhea metrics, kerning flag, space width.
 *
 * Consolidates metric logic from:
 *  - radiant/font.cpp setup_font() — basic ascender/descender/line_height/space_width
 *  - radiant/layout.cpp get_os2_typo_metrics() — OS/2 table reading
 *  - radiant/resolve_css_style.cpp get_font_x_height_ratio() — x-height from OS/2 or glyph
 *  - radiant/font_face.cpp compute_enhanced_font_metrics() — enhanced metrics
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"

#include <math.h>

// ============================================================================
// Helpers
// ============================================================================

/**
 * Convert font design units to CSS pixels.
 * scale = ppem / units_per_EM / pixel_ratio
 *
 * Some WOFF fonts have y_ppem=0; fall back to deriving scale from height.
 */
static float units_to_css_px(FT_Face face, float pixel_ratio) {
    if (!face || face->units_per_EM == 0) return 0;

    float ppem;
    if (face->size && face->size->metrics.y_ppem != 0) {
        ppem = (float)face->size->metrics.y_ppem;
    } else {
        // fallback: height ≈ ppem * 1.2 * 64 in 26.6 format
        float height_px = face->size ? (face->size->metrics.height / 64.0f) : 0;
        ppem = height_px / 1.2f;
        log_debug("font_metrics: y_ppem=0 for %s, derived ppem from height: %.1f",
                  face->family_name, ppem);
    }

    return ppem / (float)face->units_per_EM / pixel_ratio;
}

/**
 * Measure x-height using the proper multi-source cascade:
 *   1. OS/2 sxHeight  (most accurate)
 *   2. 'x' glyph bbox  (good fallback)
 *   3. 0.5 * ascender   (last resort)
 */
static float measure_x_height(FT_Face face, float scale, float ascender) {
    // source 1: OS/2 sxHeight
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2 && os2->sxHeight > 0 && face->units_per_EM > 0) {
        float val = os2->sxHeight * scale;
        log_debug("font_metrics: x-height from OS/2: %.2f (sxHeight=%d)", val, os2->sxHeight);
        return val;
    }

    // source 2: measure 'x' glyph in design units
    FT_UInt x_index = FT_Get_Char_Index(face, 'x');
    if (x_index > 0) {
        FT_Error err = FT_Load_Glyph(face, x_index, FT_LOAD_NO_SCALE);
        if (!err && face->units_per_EM > 0) {
            float val = (float)face->glyph->metrics.height * scale;
            log_debug("font_metrics: x-height from 'x' glyph: %.2f", val);
            return val;
        }
    }

    // source 3: estimate
    log_debug("font_metrics: x-height estimated as 0.5 * ascender");
    return ascender * 0.5f;
}

/**
 * Measure cap-height:
 *   1. OS/2 sCapHeight
 *   2. 'H' glyph bbox
 *   3. 0.7 * ascender
 */
static float measure_cap_height(FT_Face face, float scale, float ascender) {
    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2 && os2->sCapHeight > 0 && face->units_per_EM > 0) {
        float val = os2->sCapHeight * scale;
        log_debug("font_metrics: cap-height from OS/2: %.2f (sCapHeight=%d)", val, os2->sCapHeight);
        return val;
    }

    FT_UInt h_index = FT_Get_Char_Index(face, 'H');
    if (h_index > 0) {
        FT_Error err = FT_Load_Glyph(face, h_index, FT_LOAD_NO_SCALE);
        if (!err && face->units_per_EM > 0) {
            float val = (float)face->glyph->metrics.height * scale;
            log_debug("font_metrics: cap-height from 'H' glyph: %.2f", val);
            return val;
        }
    }

    log_debug("font_metrics: cap-height estimated as 0.7 * ascender");
    return ascender * 0.7f;
}

/**
 * Measure space width (advance of U+0020).
 * Fallback: use y_ppem / pixel_ratio.
 */
static float measure_space_width(FT_Face face, float pixel_ratio) {
    FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
    if (FT_Load_Char(face, ' ', load_flags) == 0) {
        return (face->glyph->advance.x / 64.0f) / pixel_ratio;
    }

    // fallback
    log_debug("font_metrics: space glyph missing, estimating space_width");
    float ppem = face->size ? (face->size->metrics.y_ppem / 64.0f) : 0;
    if (ppem <= 0 && face->size) {
        // y_ppem is 0 (common with WOFF), try height
        ppem = face->size->metrics.height / 64.0f / 1.2f;
    }
    return (ppem > 0) ? (ppem / pixel_ratio) : 8.0f; // last-resort 8px
}

// ============================================================================
// Public: compute and cache FontMetrics
// ============================================================================

const FontMetrics* font_get_metrics(FontHandle* handle) {
    if (!handle) return NULL;

    // fast path: already computed
    if (handle->metrics_ready) {
        return &handle->metrics;
    }

    FT_Face face = handle->ft_face;
    if (!face) return NULL;

    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    FontMetrics* m = &handle->metrics;

    // ---- HHEA / basic metrics (from FreeType size metrics) ----
    // These are in 26.6 fixed-point physical pixels; convert to CSS pixels
    m->hhea_ascender  =  (face->size->metrics.ascender  / 64.0f) / pixel_ratio;
    m->hhea_descender =  (face->size->metrics.descender / 64.0f) / pixel_ratio;
    float hhea_height =  (face->size->metrics.height     / 64.0f) / pixel_ratio;
    m->hhea_line_gap  = hhea_height - (m->hhea_ascender - m->hhea_descender);

    // default ascender/descender from hhea
    m->ascender  =  m->hhea_ascender;
    m->descender =  m->hhea_descender;   // negative value

    // ---- OS/2 table metrics ----
    float scale = units_to_css_px(face, pixel_ratio);

    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2) {
        m->typo_ascender  =  os2->sTypoAscender  * scale;
        m->typo_descender = -os2->sTypoDescender  * scale; // make positive
        m->typo_line_gap  = (os2->sTypoLineGap > 0) ? (os2->sTypoLineGap * scale) : 0.0f;
        m->win_ascent     =  os2->usWinAscent  * scale;
        m->win_descent    =  os2->usWinDescent * scale;

        // Chrome's rule: if fsSelection bit 7 (USE_TYPO_METRICS) is set,
        // use typo metrics for ascender/descender.
        uint16_t USE_TYPO_METRICS = 0x0080;
        if (os2->fsSelection & USE_TYPO_METRICS) {
            m->ascender  =  m->typo_ascender;
            m->descender = -m->typo_descender; // restore negative
        }
    } else {
        // no OS/2 table — copy hhea values as typo values
        m->typo_ascender  = m->hhea_ascender;
        m->typo_descender = -m->hhea_descender;
        m->typo_line_gap  = (m->hhea_line_gap > 0) ? m->hhea_line_gap : 0.0f;
        m->win_ascent     = m->hhea_ascender;
        m->win_descent    = -m->hhea_descender;
    }

    // ---- computed metrics ----
    m->line_gap    = m->typo_line_gap; // use OS/2 line gap if available
    m->line_height = m->ascender - m->descender + m->line_gap;

    // ---- typographic measures ----
    m->x_height    = measure_x_height(face, scale, m->ascender);
    m->cap_height  = measure_cap_height(face, scale, m->ascender);
    m->space_width = measure_space_width(face, pixel_ratio);
    m->em_size     = (float)face->units_per_EM;
    m->has_kerning = FT_HAS_KERNING(face);

    handle->metrics_ready = true;

    log_info("font_metrics: %s @%.0fpx — asc=%.1f desc=%.1f lh=%.1f xh=%.1f ch=%.1f sp=%.1f em=%.0f kern=%d",
             face->family_name ? face->family_name : "?",
             handle->physical_size_px,
             m->ascender, m->descender, m->line_height,
             m->x_height, m->cap_height, m->space_width,
             m->em_size, m->has_kerning);

    return m;
}
