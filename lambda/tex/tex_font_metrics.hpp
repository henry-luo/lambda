// tex_font_metrics.hpp - TeX Font Metrics
//
// Defines font metric structures for TeX typesetting, including
// math font parameters from TeXBook Appendix G.

#ifndef TEX_FONT_METRICS_HPP
#define TEX_FONT_METRICS_HPP

#include "tex_glue.hpp"
#include <cstdint>

namespace tex {

// ============================================================================
// Math Style (TeXBook Chapter 17)
// ============================================================================

enum class MathStyle : uint8_t {
    Display = 0,      // D  - full-size display
    DisplayPrime = 1, // D' - cramped display
    Text = 2,         // T  - full-size in text
    TextPrime = 3,    // T' - cramped text
    Script = 4,       // S  - first level subscript/superscript
    ScriptPrime = 5,  // S' - cramped script
    ScriptScript = 6, // SS - second level subscript
    ScriptScriptPrime = 7, // SS' - cramped scriptscript
};

inline bool is_cramped(MathStyle style) {
    return ((int)style & 1) != 0;
}

inline bool is_display(MathStyle style) {
    return style == MathStyle::Display || style == MathStyle::DisplayPrime;
}

inline bool is_script(MathStyle style) {
    return style >= MathStyle::Script;
}

// Superscript style: D->S, T->S, S->SS, SS->SS (cramped produces cramped)
inline MathStyle sup_style(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::Text:
            return MathStyle::Script;
        case MathStyle::DisplayPrime:
        case MathStyle::TextPrime:
            return MathStyle::ScriptPrime;
        case MathStyle::Script:
            return MathStyle::ScriptScript;
        case MathStyle::ScriptPrime:
            return MathStyle::ScriptScriptPrime;
        case MathStyle::ScriptScript:
            return MathStyle::ScriptScript;
        case MathStyle::ScriptScriptPrime:
            return MathStyle::ScriptScriptPrime;
    }
    return MathStyle::Script;
}

// Subscript style: always cramped
inline MathStyle sub_style(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayPrime:
        case MathStyle::Text:
        case MathStyle::TextPrime:
            return MathStyle::ScriptPrime;
        case MathStyle::Script:
        case MathStyle::ScriptPrime:
            return MathStyle::ScriptScriptPrime;
        case MathStyle::ScriptScript:
        case MathStyle::ScriptScriptPrime:
            return MathStyle::ScriptScriptPrime;
    }
    return MathStyle::ScriptPrime;
}

// Numerator style: D->T, T->S, S->SS, SS->SS
inline MathStyle num_style(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
            return MathStyle::Text;
        case MathStyle::DisplayPrime:
            return MathStyle::TextPrime;
        default:
            return sup_style(style);
    }
}

// Denominator style: always cramped
inline MathStyle denom_style(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayPrime:
            return MathStyle::TextPrime;
        case MathStyle::Text:
        case MathStyle::TextPrime:
            return MathStyle::ScriptPrime;
        default:
            return MathStyle::ScriptScriptPrime;
    }
}

// Cramped version of current style
inline MathStyle cramped_style(MathStyle style) {
    return (MathStyle)((int)style | 1);
}

// ============================================================================
// Font Parameters from TFM (TeX Font Metrics)
// ============================================================================

// Basic text font parameters (fontdimen parameters 1-7)
struct TextFontParams {
    float slant;              // #1: slant per point
    float interword_space;    // #2: space between words
    float interword_stretch;  // #3: space stretch
    float interword_shrink;   // #4: space shrink
    float x_height;           // #5: height of letter 'x'
    float quad;               // #6: width of em
    float extra_space;        // #7: extra space after period
};

// Math Symbol font parameters (fontdimen 1-22 for symbol fonts)
// TeXBook Appendix G, p.441
struct MathSymbolParams {
    float slant;              // #1
    float interword_space;    // #2
    float interword_stretch;  // #3
    float interword_shrink;   // #4
    float x_height;           // #5
    float quad;               // #6
    float extra_space;        // #7

    // Math-specific parameters (fontdimen 8-22)
    float num1;               // #8: numerator shift in display style
    float num2;               // #9: numerator shift in non-display
    float num3;               // #10: numerator shift for \atop
    float denom1;             // #11: denominator shift in display
    float denom2;             // #12: denominator shift in non-display
    float sup1;               // #13: superscript shift in display
    float sup2;               // #14: superscript shift for cramped
    float sup3;               // #15: superscript shift in scriptscript
    float sub1;               // #16: subscript shift normal
    float sub2;               // #17: subscript shift when sup present
    float sup_drop;           // #18: sup baseline drop from nucleus
    float sub_drop;           // #19: sub baseline raise from nucleus
    float delim1;             // #20: delim size for display
    float delim2;             // #21: delim size for non-display
    float axis_height;        // #22: height of fraction line above baseline
};

// Math Extension font parameters (fontdimen 1-13 for extensible chars)
// TeXBook Appendix G, p.441
struct MathExtensionParams {
    float slant;              // #1
    float interword_space;    // #2
    float interword_stretch;  // #3
    float interword_shrink;   // #4
    float x_height;           // #5
    float quad;               // #6
    float extra_space;        // #7

    float default_rule_thickness;  // #8: thickness of fraction bars
    float big_op_spacing1;         // #9: min clearance above/below limits
    float big_op_spacing2;         // #10: min clearance above/below limits
    float big_op_spacing3;         // #11: extra space above limits
    float big_op_spacing4;         // #12: extra space below limits
    float big_op_spacing5;         // #13: padding at top of display
};

// ============================================================================
// Glyph Metrics
// ============================================================================

struct GlyphMetrics {
    uint32_t codepoint;       // Unicode codepoint
    float width;              // Advance width
    float height;             // Height above baseline
    float depth;              // Depth below baseline
    float italic_correction;  // Italic correction for subscripts
    float top_accent;         // Horizontal position of accent attachment
    float math_kern_top_right;
    float math_kern_top_left;
    float math_kern_bottom_right;
    float math_kern_bottom_left;
};

// ============================================================================
// Character Class for Spacing
// ============================================================================

enum class CharClass : uint8_t {
    Ordinary = 0,      // Ordinary letter/number
    LargeOp = 1,       // Large operators (sum, integral)
    BinaryOp = 2,      // Binary operators (+, -)
    Relation = 3,      // Relations (=, <, >)
    Opening = 4,       // Opening delimiters
    Closing = 5,       // Closing delimiters
    Punctuation = 6,   // Comma, semicolon
    Variable = 7,      // Variable-family (changes by mathgroup)
};

// ============================================================================
// Font Family (for TeX's 16 math families)
// ============================================================================

enum class FontFamily : uint8_t {
    Roman = 0,         // Family 0: roman (upright)
    Italic = 1,        // Family 1: math italic
    Symbol = 2,        // Family 2: symbol font (operators, relations)
    Extension = 3,     // Family 3: extension font (big delimiters, radicals)
    // Additional families can be defined by user
};

// ============================================================================
// Complete Font Metrics for a single font
// ============================================================================

struct FontMetrics {
    // Basic info
    const char* font_name;
    float design_size;        // Design size in points
    float scale;              // Current scale factor

    // Font parameters (union to support different font types)
    enum class Type { Text, MathSymbol, MathExtension } type;
    union {
        TextFontParams text;
        MathSymbolParams symbol;
        MathExtensionParams extension;
    } params;

    // Glyph metrics table (indexed by codepoint or character code)
    const GlyphMetrics* glyphs;
    int glyph_count;

    // Kerning table
    struct KernPair {
        uint32_t left;
        uint32_t right;
        float kern;
    };
    const KernPair* kerns;
    int kern_count;

    // Ligature table
    struct Ligature {
        uint32_t left;
        uint32_t right;
        uint32_t result;
    };
    const Ligature* ligatures;
    int ligature_count;

    // Query functions
    const GlyphMetrics* get_glyph(uint32_t codepoint) const;
    float get_kern(uint32_t left, uint32_t right) const;
    uint32_t get_ligature(uint32_t left, uint32_t right) const; // returns 0 if none
};

// ============================================================================
// Font Provider Interface
// ============================================================================

// Abstract interface for obtaining font metrics
struct FontProvider {
    virtual ~FontProvider() = default;

    // Get metrics for a specific font family/style/size
    virtual const FontMetrics* get_font(
        FontFamily family,
        bool bold,
        bool italic,
        float size_pt
    ) = 0;

    // Get the three math fonts (symbol, extension, text)
    virtual const FontMetrics* get_math_symbol_font(float size_pt) = 0;
    virtual const FontMetrics* get_math_extension_font(float size_pt) = 0;
    virtual const FontMetrics* get_math_text_font(float size_pt, bool italic) = 0;

    // Size for different math styles
    virtual float style_size(MathStyle style, float base_size) {
        switch (style) {
            case MathStyle::Display:
            case MathStyle::DisplayPrime:
            case MathStyle::Text:
            case MathStyle::TextPrime:
                return base_size;
            case MathStyle::Script:
            case MathStyle::ScriptPrime:
                return base_size * 0.7f;  // Default script size ratio
            case MathStyle::ScriptScript:
            case MathStyle::ScriptScriptPrime:
                return base_size * 0.5f;  // Default scriptscript ratio
        }
        return base_size;
    }
};

// ============================================================================
// Math Spacing in mu (math units, 1/18 em)
// ============================================================================

// Standard math spacing values (TeXBook Chapter 18)
constexpr float MU_THIN = 3.0f;      // thin space = 3mu
constexpr float MU_MEDIUM = 4.0f;    // medium space = 4mu
constexpr float MU_THICK = 5.0f;     // thick space = 5mu

// Convert mu to points given quad width
inline float mu_to_pt(float mu, float quad) {
    return mu * quad / 18.0f;
}

// Get spacing in mu for spacing code
inline float spacing_code_to_mu(int code) {
    switch (code) {
        case 0: return 0;
        case 1: return MU_THIN;
        case 2: return MU_MEDIUM;
        case 3: return MU_THIN;
        case 4: return MU_MEDIUM;
        case 5: return MU_THICK;
        default: return 0;
    }
}

// ============================================================================
// Delimiter Size Computation
// ============================================================================

// Information about a sized delimiter
struct SizedDelimiter {
    uint32_t codepoint;       // The actual glyph to use
    float height;
    float depth;
    bool is_extended;         // True if built from extension pieces

    // For extended delimiters (built from top/middle/bottom/repeat pieces)
    struct {
        uint32_t top;
        uint32_t middle;      // 0 if no middle piece
        uint32_t bottom;
        uint32_t repeat;
        float top_height;
        float middle_height;
        float bottom_height;
        float repeat_height;
        int repeat_count;
    } pieces;
};

// ============================================================================
// Radical Data
// ============================================================================

struct RadicalData {
    uint32_t radical_glyph;   // The radical sign
    float rule_thickness;     // Thickness of the vinculum (overline)
    float kern_before_degree; // Space before degree
    float kern_after_degree;  // Space after degree
    float raise_degree;       // How much to raise the degree
};

// ============================================================================
// Default Font Parameters (Computer Modern-like)
// ============================================================================

// Default math symbol parameters (approximately CM10)
inline MathSymbolParams default_math_symbol_params(float size) {
    MathSymbolParams p = {};
    p.slant = 0.0f;
    p.interword_space = 0.0f;
    p.interword_stretch = 0.0f;
    p.interword_shrink = 0.0f;
    p.x_height = 0.430556f * size;
    p.quad = size;
    p.extra_space = 0.0f;

    // Math-specific (TeXBook Appendix G, approximately)
    p.num1 = 0.676508f * size;
    p.num2 = 0.393732f * size;
    p.num3 = 0.443731f * size;
    p.denom1 = 0.685951f * size;
    p.denom2 = 0.344841f * size;
    p.sup1 = 0.412892f * size;
    p.sup2 = 0.362892f * size;
    p.sup3 = 0.288889f * size;
    p.sub1 = 0.150000f * size;
    p.sub2 = 0.247217f * size;
    p.sup_drop = 0.386108f * size;
    p.sub_drop = 0.050000f * size;
    p.delim1 = 2.390000f * size;
    p.delim2 = 1.010000f * size;
    p.axis_height = 0.250000f * size;

    return p;
}

// Default math extension parameters
inline MathExtensionParams default_math_extension_params(float size) {
    MathExtensionParams p = {};
    p.slant = 0.0f;
    p.x_height = 0.430556f * size;
    p.quad = size;

    p.default_rule_thickness = 0.04f * size;
    p.big_op_spacing1 = 0.111112f * size;
    p.big_op_spacing2 = 0.166667f * size;
    p.big_op_spacing3 = 0.2f * size;
    p.big_op_spacing4 = 0.6f * size;
    p.big_op_spacing5 = 0.1f * size;

    return p;
}

} // namespace tex

#endif // TEX_FONT_METRICS_HPP
