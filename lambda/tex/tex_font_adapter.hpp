// tex_font_adapter.hpp - Font Adapters for Unified TeX Pipeline
//
// Provides adapters that implement the FontProvider interface
// using either TFM fonts (for DVI output) or FreeType fonts
// (for direct screen rendering).
//
// This enables the unified pipeline to work with both font sources:
// - TFM: High-fidelity TeX metrics for typesetting and DVI output
// - FreeType: System fonts for screen rendering with hinting
//
// Part of the unified TeX pipeline (Phase 5).

#ifndef TEX_FONT_ADAPTER_HPP
#define TEX_FONT_ADAPTER_HPP

#include "tex_font_metrics.hpp"
#include "tex_tfm.hpp"
#include "tex_glue.hpp"
#include "lib/arena.h"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace tex {

// ============================================================================
// TFM Font Provider (for typesetting and DVI output)
// ============================================================================

/**
 * FontProvider implementation backed by TFM files.
 * Uses the traditional TeX font metric system.
 *
 * Best for:
 * - DVI output
 * - High-fidelity TeX typesetting
 * - Matching reference TeX output
 */
class TFMFontProvider : public FontProvider {
public:
    TFMFontProvider(TFMFontManager* manager, Arena* arena);
    ~TFMFontProvider() override = default;

    const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) override;

    const FontMetrics* get_math_symbol_font(float size_pt) override;
    const FontMetrics* get_math_extension_font(float size_pt) override;
    const FontMetrics* get_math_text_font(float size_pt, bool italic) override;

    // Get underlying TFM font for direct access
    TFMFont* get_tfm_font(const char* name) { return manager_->get_font(name); }

private:
    TFMFontManager* manager_;
    Arena* arena_;

    // Cache of FontMetrics wrappers around TFM fonts
    struct CachedMetrics {
        const char* font_name;
        float size_pt;
        FontMetrics* metrics;
    };
    CachedMetrics* cache_;
    int cache_count_;
    int cache_capacity_;

    // Create FontMetrics from TFMFont
    FontMetrics* wrap_tfm_font(TFMFont* tfm, float size_pt);
    const char* select_font_name(FontFamily family, bool bold, bool italic);
};

// ============================================================================
// FreeType Font Provider (for screen rendering)
// ============================================================================

/**
 * FontProvider implementation backed by FreeType.
 * Uses system fonts (CMU, Latin Modern, etc.) for rendering.
 *
 * Best for:
 * - Direct screen rendering
 * - SVG/PNG output
 * - Interactive editing
 */
class FreeTypeFontProvider : public FontProvider {
public:
    FreeTypeFontProvider(FT_Library ft_lib, Arena* arena);
    ~FreeTypeFontProvider() override;

    const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) override;

    const FontMetrics* get_math_symbol_font(float size_pt) override;
    const FontMetrics* get_math_extension_font(float size_pt) override;
    const FontMetrics* get_math_text_font(float size_pt, bool italic) override;

    // Get FreeType face for glyph rendering
    FT_Face get_face(const char* font_name, float size_pt);

private:
    FT_Library ft_lib_;
    Arena* arena_;

    // Loaded faces cache
    struct LoadedFace {
        const char* font_name;
        float size_pt;
        FT_Face face;
        FontMetrics* metrics;
    };
    LoadedFace* faces_;
    int face_count_;
    int face_capacity_;

    FT_Face load_face(const char* font_name, float size_pt);
    FontMetrics* create_metrics_from_face(FT_Face face, const char* name, float size_pt);
    const char* map_family_to_font(FontFamily family, bool bold, bool italic);
};

// ============================================================================
// CM to Unicode Character Mapping
// ============================================================================

/**
 * Maps Computer Modern font character codes to Unicode codepoints.
 * Used when rendering with system fonts (CMU, Latin Modern).
 *
 * Different CM fonts encode characters differently:
 * - cmr/cmti: Text characters (mostly ASCII-compatible)
 * - cmmi: Math italic (Greek, special symbols)
 * - cmsy: Math symbols
 * - cmex: Extensions (large delimiters, radicals)
 */
struct CMToUnicodeMap {
    // Map character from cmmi (math italic)
    static int32_t from_cmmi(int cm_char);

    // Map character from cmsy (math symbols)
    static int32_t from_cmsy(int cm_char);

    // Map character from cmex (extensions)
    static int32_t from_cmex(int cm_char);

    // Map character from cmr (roman)
    static int32_t from_cmr(int cm_char);

    // Generic mapper based on font name
    static int32_t map(int cm_char, const char* font_name);
};

// ============================================================================
// Dual Font System
// ============================================================================

/**
 * Combines TFM and FreeType font providers.
 * Uses TFM for metrics and FreeType for rendering.
 *
 * This enables accurate typesetting with TFM metrics while
 * rendering with FreeType hinted glyphs.
 */
class DualFontProvider : public FontProvider {
public:
    DualFontProvider(
        TFMFontProvider* tfm_provider,
        FreeTypeFontProvider* ft_provider
    );
    ~DualFontProvider() override = default;

    // Use TFM for metrics (accurate typesetting)
    const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) override;

    const FontMetrics* get_math_symbol_font(float size_pt) override;
    const FontMetrics* get_math_extension_font(float size_pt) override;
    const FontMetrics* get_math_text_font(float size_pt, bool italic) override;

    // Use FreeType for rendering
    FT_Face get_render_face(const char* tfm_name, float size_pt);

    // Access individual providers
    TFMFontProvider* tfm() { return tfm_; }
    FreeTypeFontProvider* freetype() { return ft_; }

private:
    TFMFontProvider* tfm_;
    FreeTypeFontProvider* ft_;
};

// ============================================================================
// Glyph Fallback System
// ============================================================================

/**
 * GlyphFallback - Provides glyph lookup with fallback fonts.
 *
 * When a glyph is not found in the primary font, tries:
 * 1. Unicode mapping from CM font
 * 2. System font fallback (DejaVu, Noto, etc.)
 * 3. Symbol substitution (.notdef box)
 */
struct GlyphFallback {
    FT_Library ft_lib;
    FT_Face* fallback_faces;
    int fallback_count;

    // Initialize with common fallback fonts
    static GlyphFallback* create(FT_Library ft_lib, Arena* arena);

    // Look up glyph with fallback chain
    // Returns the face containing the glyph and the glyph index
    // If not found in any font, returns nullptr and glyph_index=0
    struct Result {
        FT_Face face;
        FT_UInt glyph_index;
        bool found;
    };

    Result find_glyph(FT_Face primary, int32_t codepoint);

    // Try CM→Unicode mapping first, then fallback
    Result find_cm_glyph(FT_Face primary, int cm_char, const char* font_name);
};

// Common fallback font names (in priority order)
extern const char* FALLBACK_FONT_NAMES[];
extern const int FALLBACK_FONT_COUNT;

// ============================================================================
// Factory Functions
// ============================================================================

// Create TFM font provider (requires TFM files)
TFMFontProvider* create_tfm_provider(Arena* arena);

// Create FreeType font provider (requires FT_Library)
FreeTypeFontProvider* create_freetype_provider(FT_Library ft_lib, Arena* arena);

// Create dual provider (best of both worlds)
DualFontProvider* create_dual_provider(
    TFMFontProvider* tfm,
    FreeTypeFontProvider* ft
);

} // namespace tex

#endif // TEX_FONT_ADAPTER_HPP
