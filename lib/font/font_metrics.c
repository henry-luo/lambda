/**
 * Lambda Unified Font Module — Font Metrics
 *
 * Extracts and caches per-face metrics: FreeType size metrics, OS/2
 * table values (typo ascender/descender, x-height, cap-height),
 * hhea metrics, kerning flag, space width.
 *
 * Also provides Chrome-compatible normal line-height and cell-height
 * computation (including macOS CoreText 15% hack via platform callback).
 *
 * Consolidates metric logic from:
 *  - radiant/font.cpp setup_font() — basic ascender/descender/line_height/space_width
 *  - radiant/layout.cpp get_os2_typo_metrics(), calc_normal_line_height()
 *  - radiant/layout_text.cpp get_font_cell_height()
 *  - radiant/resolve_css_style.cpp get_font_x_height_ratio() — x-height from OS/2 or glyph
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
    m->hhea_line_height = hhea_height;

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
            m->use_typo_metrics = true;
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

    // ---- underline metrics ----
    m->underline_position  = (face->underline_position  / 64.0f);
    m->underline_thickness = (face->underline_thickness / 64.0f);
    if (m->underline_thickness < 1.0f) m->underline_thickness = 1.0f;

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

// ============================================================================
// Chrome-compatible normal line-height computation
// ============================================================================

// platform-specific font metrics — implemented in radiant/font_lookup_platform.c
// returns 1 if metrics retrieved, 0 to fall back to FreeType
extern int get_font_metrics_platform(const char* font_family, float font_size,
                                     float* out_ascent, float* out_descent,
                                     float* out_line_height);

/**
 * Calculate normal CSS line-height following Chrome/Blink exactly.
 *
 * Algorithm:
 *   1. Try platform-specific metrics (CoreText on macOS) which includes
 *      the 15% ascent hack for Times/Helvetica/Courier.
 *   2. If OS/2 USE_TYPO_METRICS flag is set, use sTypo* metrics.
 *   3. Otherwise use HHEA metrics with font-unit scaling.
 *   4. Round each component individually (Chrome's SkScalarRoundToScalar).
 *
 * Returns line-height in CSS pixels.
 */
float font_calc_normal_line_height(FontHandle* handle) {
    if (!handle || !handle->ft_face) return 0;

    FT_Face face = handle->ft_face;
    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    const char* family = face->family_name;

    // derive CSS font size from ppem (or fallback)
    float font_size;
    if (face->size && face->size->metrics.y_ppem != 0) {
        font_size = (float)face->size->metrics.y_ppem / pixel_ratio;
    } else {
        // y_ppem=0 (common with WOFF), use stored CSS size
        font_size = handle->size_px;
        if (font_size <= 0) {
            float height_px = face->size ? (face->size->metrics.height / 64.0f) : 0;
            font_size = height_px / 1.2f / pixel_ratio;
        }
    }

    // 1. try platform-specific metrics first (CoreText on macOS)
    float ascent, descent, line_height;
    if (get_font_metrics_platform(family, font_size, &ascent, &descent, &line_height)) {
        log_debug("font_calc_normal_line_height (platform): %.2f for %s@%.1f",
                  line_height, family, font_size);
        return line_height;
    }

    // 2. check OS/2 USE_TYPO_METRICS flag
    const FontMetrics* m = font_get_metrics(handle);
    if (!m) return 0;

    TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    bool use_typo = os2 && (os2->fsSelection & 0x0080);

    float leading;
    if (use_typo && os2) {
        ascent  = m->typo_ascender;
        descent = m->typo_descender;  // positive value
        leading = m->typo_line_gap;

        float ra = roundf(ascent);
        float rd = roundf(descent);
        float rl = roundf(leading);
        line_height = ra + rd + rl;
    } else {
        // 3. HHEA fallback — use font-unit values for Chrome-accurate rounding
        float scale = font_size / m->em_size;
        float raw_ascent  = (float)face->ascender * scale;
        float raw_descent = -(float)face->descender * scale;
        int hhea_line_gap = face->height - face->ascender + face->descender;
        float raw_leading = (float)hhea_line_gap * scale;

        float ra = roundf(raw_ascent);
        float rd = roundf(raw_descent);
        float rl = roundf(raw_leading);
        line_height = ra + rd + rl;
    }

    log_debug("font_calc_normal_line_height: %.2f for %s@%.1f (use_typo=%d)",
              line_height, family, font_size, use_typo);
    return line_height;
}

/**
 * Get font cell height for text rect height computation.
 *
 * Matches browser's Range.getClientRects() which uses font metrics,
 * not CSS line-height. For Apple's classic fonts (Times/Helvetica/Courier),
 * uses CoreText with 15% hack. For all other fonts, returns
 * FreeType metrics.height (ascent + descent).
 *
 * Returns cell height in CSS pixels.
 */
float font_get_cell_height(FontHandle* handle) {
    if (!handle || !handle->ft_face) return 0;

    FT_Face face = handle->ft_face;
    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    const char* family = face->family_name;

    // derive CSS font size
    float font_size;
    if (face->size && face->size->metrics.y_ppem != 0) {
        font_size = (float)face->size->metrics.y_ppem / pixel_ratio;
    } else {
        font_size = handle->size_px;
        if (font_size <= 0) {
            float height_px = face->size ? (face->size->metrics.height / 64.0f) : 0;
            font_size = height_px / 1.2f / pixel_ratio;
        }
    }

    // Apple's classic fonts need CoreText with 15% hack
    bool needs_mac_hack = family && (
        strcmp(family, "Times") == 0 ||
        strcmp(family, "Helvetica") == 0 ||
        strcmp(family, "Courier") == 0
    );

    if (needs_mac_hack) {
        float ascent, descent, lh;
        if (get_font_metrics_platform(family, font_size, &ascent, &descent, &lh)) {
            return ascent + descent;  // without leading
        }
    }

    // all other fonts: FreeType metrics.height (ascent + descent)
    return face->size->metrics.height / 64.0f / pixel_ratio;
}
