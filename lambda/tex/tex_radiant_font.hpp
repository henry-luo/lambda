// tex_radiant_font.hpp - Font Provider using Radiant's FreeType infrastructure
//
// Bridges the TeX typesetting engine (lambda/tex) with Radiant's
// font loading and rendering system for actual glyph metrics.

#ifndef TEX_RADIANT_FONT_HPP
#define TEX_RADIANT_FONT_HPP

#include "tex_font_metrics.hpp"
#include "tex_box.hpp"
#include "../../radiant/font_face.h"
#include "../../radiant/view.hpp"
#include "../../lib/arena.h"
#include "../../lib/hashmap.h"

// Forward declarations
struct UiContext;

namespace tex {

// Extended FontMetrics structure for Radiant integration
// Adds arrays for caching character widths
struct FontMetrics {
    enum class Type { Text, MathSymbol, MathExtension, MathItalic };
    Type type;

    float size;               // Point size
    float em;                 // Em width
    float ex;                 // Ex height (height of 'x')

    float ascender;           // Ascender height
    float descender;          // Descender depth (positive)
    float line_height;        // Total line height

    float axis_height;        // Math axis height
    float rule_thickness;     // Default rule thickness
    float space_width;        // Space character width

    // Glyph width cache for ASCII printable range
    float char_widths[95];    // Codepoints 32-126

    // TeX math parameters
    MathSymbolParams symbol_params;
    MathExtensionParams extension_params;
};

// Extended glyph metrics for Radiant integration
struct GlyphMetrics {
    float width;              // Glyph width (advance)
    float height;             // Height above baseline
    float depth;              // Depth below baseline
    float advance;            // Horizontal advance
    float italic_correction;  // Italic correction
};

// Sized delimiter result
struct SizedDelimiter {
    uint32_t glyph_codepoint; // Final glyph codepoint
    float height;
    float depth;
    float width;
    float advance;
    bool is_extensible;       // Built from parts
    int repeat_count;         // Number of extender repetitions
};

// Radical construction data
struct RadicalData {
    float surd_width;         // Width of the surd symbol
    float surd_height;        // Height of surd
    float rule_thickness;     // Thickness of the vinculum
    float rule_kern;          // Gap above radicand
    float degree_kern;        // Kern before degree (negative)
    bool has_degree;
};

// ============================================================================
// Radiant Font Provider - implements FontProvider interface
// ============================================================================

class RadiantFontProvider : public FontProvider {
public:
    // Initialize with Radiant's UI context
    RadiantFontProvider(UiContext* uicon, Arena* arena);
    ~RadiantFontProvider() override = default;

    // FontProvider interface implementation
    const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) override;

    const FontMetrics* get_math_symbol_font(float size_pt) override;
    const FontMetrics* get_math_extension_font(float size_pt) override;
    const FontMetrics* get_math_text_font(float size_pt, bool italic) override;

    float style_size(MathStyle style, float base_size) override;

    // ========================================================================
    // Glyph-level queries (for actual layout)
    // ========================================================================

    // Get metrics for a specific glyph
    GlyphMetrics get_glyph_metrics(uint32_t codepoint, float size_pt, FontFamily family);

    // Get kerning between two glyphs
    float get_kerning(uint32_t left, uint32_t right, float size_pt, FontFamily family);

    // Check if font has a specific glyph
    bool has_glyph(uint32_t codepoint, FontFamily family);

    // Get FreeType face for rendering
    FT_Face get_ft_face(FontFamily family, bool bold, bool italic, float size_pt);

    // ========================================================================
    // Math-specific queries
    // ========================================================================

    // Get sized delimiter (finds appropriate glyph variant or builds extensible)
    SizedDelimiter get_sized_delimiter(
        uint32_t codepoint,
        float target_height,
        MathStyle style
    );

    // Get radical data for square root
    RadicalData get_radical_data(float radicand_height, MathStyle style);

    // Get accent attachment point
    float get_accent_attachment(uint32_t base_codepoint, float size_pt);

    // ========================================================================
    // Font configuration
    // ========================================================================

    // Set math font family (e.g., "Latin Modern Math", "STIX Two Math")
    void set_math_font(const char* family_name);

    // Set text font family
    void set_text_font(const char* family_name);

    // Script size ratios
    void set_script_ratio(float ratio) { script_ratio_ = ratio; }
    void set_scriptscript_ratio(float ratio) { scriptscript_ratio_ = ratio; }

private:
    UiContext* uicon_;
    Arena* arena_;

    // Font family names
    const char* math_font_family_;
    const char* text_font_family_;

    // Cached font metrics (keyed by family + size)
    Hashmap* metrics_cache_;

    // Script size ratios
    float script_ratio_;
    float scriptscript_ratio_;

    // Internal helpers
    FontMetrics* create_font_metrics(FT_Face face, float size_pt, FontMetrics::Type type);
    void populate_glyph_metrics(FontMetrics* metrics, FT_Face face);
    FT_Face load_face(const char* family, bool bold, bool italic, float size_pt);
};

// ============================================================================
// Math Font Table Structures
// ============================================================================

// OpenType MATH table constants
struct MathConstants {
    // General
    int16_t scriptPercentScaleDown;
    int16_t scriptScriptPercentScaleDown;
    uint16_t delimitedSubFormulaMinHeight;
    uint16_t displayOperatorMinHeight;

    // Radicals
    int16_t radicalVerticalGap;
    int16_t radicalDisplayStyleVerticalGap;
    int16_t radicalRuleThickness;
    int16_t radicalExtraAscender;
    int16_t radicalKernBeforeDegree;
    int16_t radicalKernAfterDegree;
    int16_t radicalDegreeBottomRaisePercent;

    // Fractions
    int16_t fractionNumeratorShiftUp;
    int16_t fractionNumeratorDisplayStyleShiftUp;
    int16_t fractionDenominatorShiftDown;
    int16_t fractionDenominatorDisplayStyleShiftDown;
    int16_t fractionNumeratorGapMin;
    int16_t fractionNumDisplayStyleGapMin;
    int16_t fractionDenominatorGapMin;
    int16_t fractionDenomDisplayStyleGapMin;
    int16_t fractionRuleThickness;

    // Sub/superscripts
    int16_t superscriptShiftUp;
    int16_t superscriptShiftUpCramped;
    int16_t subscriptShiftDown;
    int16_t superscriptBaselineDropMax;
    int16_t subscriptBaselineDropMin;
    int16_t subscriptTopMax;
    int16_t superscriptBottomMin;
    int16_t subSuperscriptGapMin;
    int16_t superscriptBottomMaxWithSubscript;

    // Limits
    int16_t upperLimitGapMin;
    int16_t upperLimitBaselineRiseMin;
    int16_t lowerLimitGapMin;
    int16_t lowerLimitBaselineDropMin;

    // Stacks (atop, above)
    int16_t stackTopShiftUp;
    int16_t stackTopDisplayStyleShiftUp;
    int16_t stackBottomShiftDown;
    int16_t stackBottomDisplayStyleShiftDown;
    int16_t stackGapMin;
    int16_t stackDisplayStyleGapMin;

    // Accents
    int16_t accentBaseHeight;
    int16_t flattenedAccentBaseHeight;

    // Axis
    int16_t axisHeight;
};

// Read MATH constants from OpenType font
bool read_math_constants(FT_Face face, MathConstants* out);

// ============================================================================
// Delimiter Variant Table
// ============================================================================

struct DelimiterVariant {
    uint32_t glyph_id;
    float advance_height;
};

struct DelimiterConstruction {
    // Pre-built size variants (smallest to largest)
    DelimiterVariant* variants;
    int variant_count;

    // Extensible assembly parts
    struct Part {
        uint32_t glyph_id;
        uint16_t flags;        // 1 = extender (can repeat)
        int16_t start_connector;
        int16_t end_connector;
        int16_t full_advance;
    };
    Part* parts;
    int part_count;
};

// Get delimiter construction info from font
bool get_delimiter_construction(
    FT_Face face,
    uint32_t codepoint,
    bool vertical,
    DelimiterConstruction* out,
    Arena* arena
);

// ============================================================================
// Default Math Font Parameters
// ============================================================================

// Get default parameters when OpenType MATH table is not available
MathConstants default_math_constants();

// Convert MathConstants to MathSymbolParams (for tex_font_metrics.hpp compatibility)
MathSymbolParams math_constants_to_symbol_params(const MathConstants& mc, float size_pt);

// Convert MathConstants to MathExtensionParams
MathExtensionParams math_constants_to_extension_params(const MathConstants& mc, float size_pt);

} // namespace tex

#endif // TEX_RADIANT_FONT_HPP
