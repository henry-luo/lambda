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
 * scale = size_px / units_per_em * bitmap_scale
 *
 * Uses FontTables head.units_per_em when available, falls back to FreeType.
 */
static float units_to_css_px(FontHandle* handle) {
    float upem = 0;
    if (handle->tables) {
        HeadTable* head = font_tables_get_head(handle->tables);
        if (head && head->units_per_em > 0) upem = (float)head->units_per_em;
    }
    if (upem == 0 && handle->ft_face) {
        upem = (float)handle->ft_face->units_per_EM;
    }
    if (upem == 0) return 0;
    return handle->size_px / upem * handle->bitmap_scale;
}

/**
 * Measure x-height using the proper multi-source cascade:
 *   1. OS/2 sxHeight  (most accurate)
 *   2. 'x' glyph bbox  (good fallback)
 *   3. 0.5 * ascender   (last resort)
 */
static float measure_x_height(FontHandle* handle, float scale, float ascender) {
    FontTables* ft = handle->tables;
    FT_Face face = handle->ft_face;

    // source 1: OS/2 sxHeight — try FontTables first, then FreeType
    if (ft) {
        Os2Table* os2t = font_tables_get_os2(ft);
        if (os2t && os2t->sx_height > 0) {
            float val = os2t->sx_height * scale;
            log_debug("font_metrics: x-height from FontTables OS/2: %.2f (sxHeight=%d)", val, os2t->sx_height);
            return val;
        }
    }
    if (!ft && face) {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        if (os2 && os2->sxHeight > 0) {
            float val = os2->sxHeight * scale;
            log_debug("font_metrics: x-height from OS/2: %.2f (sxHeight=%d)", val, os2->sxHeight);
            return val;
        }
    }

    // source 2: measure 'x' glyph in design units
    // try FontTables cmap → glyph metrics (no FreeType needed)
    if (ft) {
        CmapTable* cmap = font_tables_get_cmap(ft);
        HeadTable* head = font_tables_get_head(ft);
        if (cmap && head && head->units_per_em > 0) {
            uint16_t x_gid = cmap_lookup(cmap, 'x');
            if (x_gid > 0) {
                // for 'x' height we need the glyph bbox height, not advance
                // hmtx only gives advance width; fall through to FreeType for bbox
            }
        }
    }
    if (face) {
        FT_UInt x_index = FT_Get_Char_Index(face, 'x');
        if (x_index > 0) {
            FT_Error err = FT_Load_Glyph(face, x_index, FT_LOAD_NO_SCALE);
            if (!err) {
                float val = (float)face->glyph->metrics.height * scale;
                log_debug("font_metrics: x-height from 'x' glyph: %.2f", val);
                return val;
            }
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
static float measure_cap_height(FontHandle* handle, float scale, float ascender) {
    FontTables* ft = handle->tables;
    FT_Face face = handle->ft_face;

    // try FontTables first
    if (ft) {
        Os2Table* os2t = font_tables_get_os2(ft);
        if (os2t && os2t->s_cap_height > 0) {
            float val = os2t->s_cap_height * scale;
            log_debug("font_metrics: cap-height from FontTables OS/2: %.2f (sCapHeight=%d)", val, os2t->s_cap_height);
            return val;
        }
    }
    if (!ft && face) {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        if (os2 && os2->sCapHeight > 0) {
            float val = os2->sCapHeight * scale;
            log_debug("font_metrics: cap-height from OS/2: %.2f (sCapHeight=%d)", val, os2->sCapHeight);
            return val;
        }
    }

    if (face) {
        FT_UInt h_index = FT_Get_Char_Index(face, 'H');
        if (h_index > 0) {
            FT_Error err = FT_Load_Glyph(face, h_index, FT_LOAD_NO_SCALE);
            if (!err) {
                float val = (float)face->glyph->metrics.height * scale;
                log_debug("font_metrics: cap-height from 'H' glyph: %.2f", val);
                return val;
            }
        }
    }

    log_debug("font_metrics: cap-height estimated as 0.7 * ascender");
    return ascender * 0.7f;
}

/**
 * Measure space width (advance of U+0020).
 * Fallback: use y_ppem / pixel_ratio.
 */
static float measure_space_width(FontHandle* handle) {
    FontTables* ft = handle->tables;
    FT_Face face = handle->ft_face;

    // primary: FontTables cmap + hmtx
    if (ft) {
        CmapTable* cmap = font_tables_get_cmap(ft);
        HmtxTable* hmtx = font_tables_get_hmtx(ft);
        HeadTable* head = font_tables_get_head(ft);
        if (cmap && hmtx && head && head->units_per_em > 0) {
            uint16_t gid = cmap_lookup(cmap, ' ');
            if (gid > 0) {
                uint16_t adv = hmtx_get_advance(hmtx, gid);
                return adv * handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
            }
        }
    }

    // secondary: FreeType
    if (face) {
        FT_Int32 load_flags = FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING;
        float pixel_ratio = (handle->ctx && handle->ctx->config.pixel_ratio > 0)
                                ? handle->ctx->config.pixel_ratio : 1.0f;
        if (FT_Load_Char(face, ' ', load_flags) == 0) {
            return (face->glyph->advance.x / 64.0f) / pixel_ratio;
        }
    }

    // fallback
    log_debug("font_metrics: space glyph missing, estimating space_width");
    return (handle->size_px > 0) ? (handle->size_px * 0.25f) : 8.0f;
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
    FontTables* ft = handle->tables;
    if (!face && !ft) return NULL;

    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    FontMetrics* m = &handle->metrics;
    float bscale = handle->bitmap_scale; // 1.0 for scalable fonts, <1 for fixed-size bitmap fonts

    // ---- HHEA / basic metrics ----
    // Primary: FontTables hhea+head → scale from font units to CSS pixels
    // Secondary: FreeType size metrics (26.6 fixed-point physical pixels)
    HheaTable* hhea = ft ? font_tables_get_hhea(ft) : NULL;
    HeadTable* head = ft ? font_tables_get_head(ft) : NULL;

    if (hhea && head && head->units_per_em > 0) {
        float uscale = handle->size_px / (float)head->units_per_em * bscale;
        m->hhea_ascender  =  hhea->ascender  * uscale;
        m->hhea_descender =  hhea->descender * uscale;  // negative in font units
        float hhea_height = (hhea->ascender - hhea->descender + hhea->line_gap) * uscale;
        m->hhea_line_gap  = hhea->line_gap * uscale;
        m->hhea_line_height = hhea_height;
    } else if (face && face->size) {
        m->hhea_ascender  =  (face->size->metrics.ascender  / 64.0f) * bscale / pixel_ratio;
        m->hhea_descender =  (face->size->metrics.descender / 64.0f) * bscale / pixel_ratio;
        float hhea_height =  (face->size->metrics.height     / 64.0f) * bscale / pixel_ratio;
        m->hhea_line_gap  = hhea_height - (m->hhea_ascender - m->hhea_descender);
        m->hhea_line_height = hhea_height;
    }

    // default ascender/descender from hhea
    m->ascender  =  m->hhea_ascender;
    m->descender =  m->hhea_descender;   // negative value

    // ---- OS/2 table metrics ----
    float scale = units_to_css_px(handle);

    // Use FontTables for OS/2 when available, fall back to FreeType
    Os2Table* os2t = ft ? font_tables_get_os2(ft) : NULL;
    TT_OS2* os2 = (os2t || !face) ? NULL : (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);

    if (os2t) {
        m->typo_ascender  =  os2t->s_typo_ascender  * scale;
        m->typo_descender = -os2t->s_typo_descender  * scale; // make positive
        m->typo_line_gap  = (os2t->s_typo_line_gap > 0) ? (os2t->s_typo_line_gap * scale) : 0.0f;
        m->win_ascent     =  os2t->us_win_ascent  * scale;
        m->win_descent    =  os2t->us_win_descent * scale;

        uint16_t USE_TYPO_METRICS = 0x0080;
        if (os2t->fs_selection & USE_TYPO_METRICS) {
            m->ascender  =  m->typo_ascender;
            m->descender = -m->typo_descender;
            m->use_typo_metrics = true;
        }
    } else if (os2) {
        m->typo_ascender  =  os2->sTypoAscender  * scale;
        m->typo_descender = -os2->sTypoDescender  * scale; // make positive
        m->typo_line_gap  = (os2->sTypoLineGap > 0) ? (os2->sTypoLineGap * scale) : 0.0f;
        m->win_ascent     =  os2->usWinAscent  * scale;
        m->win_descent    =  os2->usWinDescent * scale;

        uint16_t USE_TYPO_METRICS = 0x0080;
        if (os2->fsSelection & USE_TYPO_METRICS) {
            m->ascender  =  m->typo_ascender;
            m->descender = -m->typo_descender;
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
    // Use FontTables post table when available
    PostTable* postt = ft ? font_tables_get_post(ft) : NULL;
    if (postt && head && head->units_per_em > 0) {
        float uscale = handle->size_px / (float)head->units_per_em * bscale;
        m->underline_position  = postt->underline_position * uscale;
        m->underline_thickness = postt->underline_thickness * uscale;
    } else if (face) {
        m->underline_position  = (face->underline_position  / 64.0f) * bscale;
        m->underline_thickness = (face->underline_thickness / 64.0f) * bscale;
    }
    if (m->underline_thickness < 1.0f) m->underline_thickness = 1.0f;

    // ---- typographic measures ----
    m->x_height    = measure_x_height(handle, scale, m->ascender);
    m->cap_height  = measure_cap_height(handle, scale, m->ascender);
    m->space_width = measure_space_width(handle);

    // em_size: FontTables primary, FreeType fallback
    if (head && head->units_per_em > 0) {
        m->em_size = (float)head->units_per_em;
    } else if (face) {
        m->em_size = (float)face->units_per_EM;
    } else {
        m->em_size = 1000.0f; // reasonable default
    }

    // kerning: FontTables kern table primary, FreeType fallback, CoreText GPOS tertiary
    KernTable* kern = ft ? font_tables_get_kern(ft) : NULL;
    m->has_kerning = (kern && kern->valid && kern->num_pairs > 0);
    if (!m->has_kerning && face) {
        m->has_kerning = FT_HAS_KERNING(face);
    }
#ifdef __APPLE__
    if (!m->has_kerning && handle->ct_font_ref) {
        m->has_kerning = true;
    }
#endif

    handle->metrics_ready = true;

    const char* fname = handle->family_name ? handle->family_name
                      : (face && face->family_name ? face->family_name : "?");
    log_info("font_metrics: %s @%.0fpx — asc=%.1f desc=%.1f lh=%.1f xh=%.1f ch=%.1f sp=%.1f em=%.0f kern=%d",
             fname, handle->physical_size_px,
             m->ascender, m->descender, m->line_height,
             m->x_height, m->cap_height, m->space_width,
             m->em_size, m->has_kerning);

    return m;
}

// ============================================================================
// Chrome-compatible normal line-height computation
// ============================================================================

// get_font_metrics_platform() — implemented in font_platform.c
// declared in font_internal.h

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
    if (!handle) return 0;

    FT_Face face = handle->ft_face;
    FontTables* ft = handle->tables;
    if (!face && !ft) return 0;

    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    const char* family = handle->family_name ? handle->family_name
                       : (face ? face->family_name : NULL);

    // derive CSS font size from FontTables/handle or FreeType
    float font_size = handle->size_px;
    if (font_size <= 0 && face && face->size && face->size->metrics.y_ppem != 0) {
        font_size = (float)face->size->metrics.y_ppem * handle->bitmap_scale / pixel_ratio;
    }
    if (font_size <= 0 && face && face->size) {
        float height_px = face->size->metrics.height / 64.0f;
        font_size = height_px / 1.2f / pixel_ratio;
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

    // use FontTables when available, else FreeType
    bool use_typo = false;
    if (ft) {
        Os2Table* os2t = font_tables_get_os2(ft);
        use_typo = os2t && (os2t->fs_selection & 0x0080);
    } else if (face) {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        use_typo = os2 && (os2->fsSelection & 0x0080);
    }

    float leading;
    if (use_typo) {
        ascent  = m->typo_ascender;
        descent = m->typo_descender;  // positive value
        leading = m->typo_line_gap;

        float ra = roundf(ascent);
        float rd = roundf(descent);
        float rl = roundf(leading);
        line_height = ra + rd + rl;
    } else {
        // 3. HHEA fallback — use font-unit values for Chrome-accurate rounding
        if (m->em_size <= 0) {
            // bitmap-only fonts may have no units_per_EM; use pre-scaled hhea metrics
            line_height = m->hhea_line_height;
        } else {
            float fscale = font_size / m->em_size;
            // primary: FontTables hhea for font-unit values
            HheaTable* hhea = ft ? font_tables_get_hhea(ft) : NULL;
            float raw_ascent, raw_descent, raw_leading;
            if (hhea) {
                raw_ascent  = (float)hhea->ascender * fscale;
                raw_descent = -(float)hhea->descender * fscale;
                int hhea_gap = hhea->line_gap;
                raw_leading = (float)hhea_gap * fscale;
            } else if (face) {
                raw_ascent  = (float)face->ascender * fscale;
                raw_descent = -(float)face->descender * fscale;
                int hhea_line_gap = face->height - face->ascender + face->descender;
                raw_leading = (float)hhea_line_gap * fscale;
            } else {
                raw_ascent = m->hhea_ascender;
                raw_descent = -m->hhea_descender;
                raw_leading = m->hhea_line_gap;
            }

            float ra = roundf(raw_ascent);
            float rd = roundf(raw_descent);
            float rl = roundf(raw_leading);
            line_height = ra + rd + rl;
        }
    }

    log_debug("font_calc_normal_line_height: %.2f for %s@%.1f (use_typo=%d)",
              line_height, family, font_size, use_typo);
    return line_height;
}

/**
 * Get normal line-height split into ascender/descender following Chrome/Blink:
 *   - On macOS (CoreText): ascender = asc + desc, descender = leading
 *   - USE_TYPO_METRICS path: ascender = typo_asc + typo_desc, descender = typo_line_gap
 *   - HHEA fallback: ascender = hhea_asc + |hhea_desc|, descender = hhea_line_gap
 * Both *out_ascender and *out_descender are positive values (above/below baseline).
 */
void font_get_normal_lh_split(FontHandle* handle, float* out_ascender, float* out_descender) {
    if (!handle || !out_ascender || !out_descender) {
        if (out_ascender) *out_ascender = 0;
        if (out_descender) *out_descender = 0;
        return;
    }

    FT_Face face = handle->ft_face;
    FontTables* ft = handle->tables;
    if (!face && !ft) {
        *out_ascender = 0;
        *out_descender = 0;
        return;
    }

    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    const char* family = handle->family_name ? handle->family_name
                       : (face ? face->family_name : NULL);

    float font_size = handle->size_px;
    if (font_size <= 0 && face && face->size && face->size->metrics.y_ppem != 0) {
        font_size = (float)face->size->metrics.y_ppem * handle->bitmap_scale / pixel_ratio;
    }
    if (font_size <= 0 && face && face->size) {
        float height_px = face->size->metrics.height / 64.0f;
        font_size = height_px / 1.2f / pixel_ratio;
    }

    // 1. OS/2 USE_TYPO_METRICS fonts: Chrome uses sTypo metrics regardless of platform
    const FontMetrics* m = font_get_metrics(handle);
    bool use_typo = false;
    if (ft) {
        Os2Table* os2t = font_tables_get_os2(ft);
        use_typo = os2t && (os2t->fs_selection & 0x0080);
    } else if (face) {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
        use_typo = os2 && (os2->fsSelection & 0x0080);
    }

    if (use_typo && m) {
        float ra = roundf(m->typo_ascender);
        float rd = roundf(m->typo_descender);  // positive
        float rl = roundf(m->typo_line_gap);
        *out_ascender = ra + rd;
        *out_descender = rl;
        log_debug("font_get_normal_lh_split (typo): asc=%f desc=%f for %s@%.1f",
                  *out_ascender, *out_descender, family, font_size);
        return;
    }

    // 2. Platform-specific metrics (CoreText on macOS) for non-USE_TYPO_METRICS fonts
    float ascent, descent, line_height;
    if (get_font_metrics_platform(family, font_size, &ascent, &descent, &line_height)) {
        float leading = line_height - (ascent + descent);
        float half_leading = leading / 2.0f;
        *out_ascender = ascent + half_leading;
        *out_descender = descent + half_leading;
        if (*out_descender < 0) *out_descender = 0;
        log_debug("font_get_normal_lh_split (platform, half-leading): asc=%f desc=%f lead=%f for %s@%.1f",
                  *out_ascender, *out_descender, leading, family, font_size);
        return;
    }

    // 3. HHEA fallback
    if (m) {
        if (m->em_size <= 0) {
            *out_ascender = m->hhea_ascender + (-m->hhea_descender);
            *out_descender = m->hhea_line_height - *out_ascender;
            if (*out_descender < 0) *out_descender = 0;
        } else {
            float fscale = font_size / m->em_size;
            // primary: FontTables hhea
            HheaTable* hhea = ft ? font_tables_get_hhea(ft) : NULL;
            float raw_ascent, raw_descent, raw_leading;
            if (hhea) {
                raw_ascent  = (float)hhea->ascender * fscale;
                raw_descent = -(float)hhea->descender * fscale;
                raw_leading = (float)hhea->line_gap * fscale;
            } else if (face) {
                raw_ascent  = (float)face->ascender * fscale;
                raw_descent = -(float)face->descender * fscale;
                int hhea_line_gap = face->height - face->ascender + face->descender;
                raw_leading = (float)hhea_line_gap * fscale;
            } else {
                raw_ascent = m->hhea_ascender;
                raw_descent = -m->hhea_descender;
                raw_leading = m->hhea_line_gap;
            }

            float ra = roundf(raw_ascent);
            float rd = roundf(raw_descent);
            float rl = roundf(raw_leading);
            *out_ascender = ra + rd;
            *out_descender = rl;
        }
    } else {
        *out_ascender = 0;
        *out_descender = 0;
    }
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
    if (!handle) return 0;

    FT_Face face = handle->ft_face;
    FontTables* ft = handle->tables;
    if (!face && !ft) return 0;

    FontContext* ctx = handle->ctx;
    float pixel_ratio = (ctx && ctx->config.pixel_ratio > 0)
                            ? ctx->config.pixel_ratio : 1.0f;

    const char* family = handle->family_name ? handle->family_name
                       : (face ? face->family_name : NULL);

    // derive CSS font size
    float font_size = handle->size_px;
    if (font_size <= 0 && face && face->size && face->size->metrics.y_ppem != 0) {
        font_size = (float)face->size->metrics.y_ppem * handle->bitmap_scale / pixel_ratio;
    }
    if (font_size <= 0 && face && face->size) {
        float height_px = face->size->metrics.height / 64.0f;
        font_size = height_px / 1.2f / pixel_ratio;
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

    // primary: FontTables hhea+head → ascent + descent in CSS pixels
    HheaTable* hhea = ft ? font_tables_get_hhea(ft) : NULL;
    HeadTable* head = ft ? font_tables_get_head(ft) : NULL;
    if (hhea && head && head->units_per_em > 0) {
        float uscale = handle->size_px / (float)head->units_per_em * handle->bitmap_scale;
        return (hhea->ascender - hhea->descender) * uscale;
    }

    // secondary: FreeType metrics.height (ascent + descent)
    if (face && face->size) {
        return face->size->metrics.height / 64.0f * handle->bitmap_scale / pixel_ratio;
    }

    return 0;
}
