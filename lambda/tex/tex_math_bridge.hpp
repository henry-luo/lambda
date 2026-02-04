// tex_math_bridge.hpp - Bridge between Math Representation and TeX Node System
//
// Converts math content (both string-based and Lambda Item-based) to TexNode
// trees for integration with the TeX typesetting pipeline.
//
// Supports:
// - Inline math ($...$) - embedded in paragraph flow
// - Display math ($$...$$, \[...\]) - centered on its own line
// - Simple math expressions (a + b = c)
// - Fractions, radicals, sub/superscripts
//
// Reference: TeXBook Chapters 17-18, Appendix G

#ifndef TEX_MATH_BRIDGE_HPP
#define TEX_MATH_BRIDGE_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_glue.hpp"
#include "tex_font_metrics.hpp"
#include "../../lib/arena.h"

#ifdef TEX_WITH_LAMBDA
#include "../../lambda/lambda-data.hpp"
#endif

namespace tex {

// MathStyle and related functions are defined in tex_font_metrics.hpp
// Use is_cramped(), sup_style(), sub_style() from there

// Size factors for each style (relative to base size)
float style_size_factor(MathStyle style);

// ============================================================================
// Font Variant for \mathbf, \mathrm, etc.
// ============================================================================

enum class FontVariant : uint8_t {
    Normal = 0,     // default (math italic for letters, roman for digits)
    Roman,          // \mathrm - upright roman
    Bold,           // \mathbf - bold
    Italic,         // \mathit - text italic
    BoldItalic,     // \mathbfit - bold italic
    SansSerif,      // \mathsf - sans-serif
    Monospace,      // \mathtt - typewriter/monospace
    Calligraphic,   // \mathcal - calligraphic
    Script,         // \mathscr - script
    Fraktur,        // \mathfrak - fraktur
    Blackboard,     // \mathbb - blackboard bold
    OperatorName,   // \operatorname - upright for operator names
};

// Parse font variant from command name (e.g., "mathbf" -> Bold)
FontVariant parse_font_variant(const char* cmd);

// ============================================================================
// Math Context for Typesetting
// ============================================================================

struct MathContext {
    Arena* arena;
    TFMFontManager* fonts;

    // Optional unified font provider (for dual TFM/FreeType support)
    FontProvider* font_provider;

    // Current style
    MathStyle style;

    // Font settings
    float base_size_pt;          // Base font size (10pt default)
    FontSpec roman_font;         // cmr for digits, text
    FontSpec italic_font;        // cmmi for variables
    FontSpec symbol_font;        // cmsy for operators, relations
    FontSpec extension_font;     // cmex for large symbols, delimiters

    // Current font (set based on what we're typesetting)
    TFMFont* current_tfm;

    // Current font variant (\mathbf, \mathrm, etc.)
    FontVariant font_variant;

    // Current text color (for \textcolor, \color commands)
    const char* color;

    // Font parameters (from cmr10)
    float x_height;              // Height of 'x' (for positioning)
    float quad;                  // 1em width
    float axis_height;           // Height of math axis (fraction bar center)
    float rule_thickness;        // Default rule thickness

    // Create with defaults
    static MathContext create(Arena* arena, TFMFontManager* fonts, float size_pt) {
        MathContext ctx = {};
        ctx.arena = arena;
        ctx.fonts = fonts;
        ctx.font_provider = nullptr;  // Set separately if dual font support needed
        ctx.style = MathStyle::Text;
        ctx.font_variant = FontVariant::Normal;
        ctx.color = nullptr;  // No color override by default
        ctx.base_size_pt = size_pt;

        // Set up fonts
        ctx.roman_font = FontSpec("cmr10", size_pt, nullptr, 0);
        ctx.italic_font = FontSpec("cmmi10", size_pt, nullptr, 0);
        ctx.symbol_font = FontSpec("cmsy10", size_pt, nullptr, 0);
        ctx.extension_font = FontSpec("cmex10", size_pt, nullptr, 0);

        // Initialize from TFM if available
        ctx.current_tfm = fonts->get_font("cmr10");
        if (ctx.current_tfm) {
            ctx.x_height = ctx.current_tfm->get_param(TFM_PARAM_X_HEIGHT) * size_pt / ctx.current_tfm->design_size;
            ctx.quad = ctx.current_tfm->get_param(TFM_PARAM_QUAD) * size_pt / ctx.current_tfm->design_size;
        } else {
            ctx.x_height = 4.31f * size_pt / 10.0f;
            ctx.quad = size_pt;
        }

        // Standard TeX values
        ctx.axis_height = ctx.x_height * 0.5f;
        ctx.rule_thickness = 0.4f * size_pt / 10.0f;

        return ctx;
    }

    // Current font size based on style
    float font_size() const {
        return base_size_pt * style_size_factor(style);
    }

    // Create with FontProvider for dual font support
    static MathContext create_with_provider(Arena* arena, FontProvider* provider, float size_pt) {
        MathContext ctx = {};
        ctx.arena = arena;
        ctx.fonts = nullptr;  // Not used when provider is set
        ctx.font_provider = provider;
        ctx.style = MathStyle::Text;
        ctx.font_variant = FontVariant::Normal;
        ctx.base_size_pt = size_pt;

        // Set up font specs
        ctx.roman_font = FontSpec("cmr10", size_pt, nullptr, 0);
        ctx.italic_font = FontSpec("cmmi10", size_pt, nullptr, 0);
        ctx.symbol_font = FontSpec("cmsy10", size_pt, nullptr, 0);
        ctx.extension_font = FontSpec("cmex10", size_pt, nullptr, 0);

        // Initialize from FontProvider
        const FontMetrics* text_font = provider->get_math_text_font(size_pt, false);
        if (text_font) {
            ctx.x_height = text_font->params.text.x_height;
            ctx.quad = text_font->params.text.quad;
        } else {
            ctx.x_height = 4.31f * size_pt / 10.0f;
            ctx.quad = size_pt;
        }

        ctx.axis_height = ctx.x_height * 0.5f;
        ctx.rule_thickness = 0.4f * size_pt / 10.0f;
        ctx.current_tfm = nullptr;  // Not used with provider

        return ctx;
    }
};

// ============================================================================
// Math Atom Types (TeXBook Chapter 17)
// ============================================================================

// Already defined in tex_node.hpp as AtomType
// Re-exported here for convenience

// Classify a Unicode codepoint as a math atom type
AtomType classify_codepoint(int32_t cp);

// ============================================================================
// LaTeX Math Parser
// ============================================================================

// Parse and typeset LaTeX math notation including:
// - Greek letters (\alpha, \beta, etc.)
// - Fractions (\frac{num}{denom})
// - Square roots (\sqrt{x}, \sqrt[n]{x})
// - Subscripts/superscripts (x^2, x_i, x_i^n)
// - Symbols (\sum, \int, \times, etc.)
// Returns an HBox containing the typeset math
TexNode* typeset_latex_math(
    const char* latex_str,
    size_t len,
    MathContext& ctx
);

// ============================================================================
// Fraction Support
// ============================================================================

// Typeset a fraction: \frac{num}{denom}
// Returns a VBox containing the fraction
TexNode* typeset_fraction(
    TexNode* numerator,
    TexNode* denominator,
    float rule_thickness,      // 0 for \atop
    MathContext& ctx
);

// Convenience: build fraction from strings
TexNode* typeset_fraction_strings(
    const char* num_str,
    const char* denom_str,
    MathContext& ctx
);

// ============================================================================
// Radical (Square Root) Support
// ============================================================================

// Typeset a square root: \sqrt{radicand}
TexNode* typeset_sqrt(
    TexNode* radicand,
    MathContext& ctx
);

// Typeset nth root: \sqrt[n]{radicand}
TexNode* typeset_root(
    TexNode* degree,
    TexNode* radicand,
    MathContext& ctx
);

// Convenience: build sqrt from string
TexNode* typeset_sqrt_string(
    const char* content_str,
    MathContext& ctx
);

// ============================================================================
// Subscript/Superscript Support
// ============================================================================

// Typeset subscript and/or superscript: x_i^2
TexNode* typeset_scripts(
    TexNode* nucleus,
    TexNode* subscript,      // nullptr if none
    TexNode* superscript,    // nullptr if none
    MathContext& ctx
);

// Typeset limits above/below a big operator (display style)
TexNode* typeset_op_limits(
    TexNode* op_node,
    TexNode* subscript,      // nullptr if none (lower limit)
    TexNode* superscript,    // nullptr if none (upper limit)
    MathContext& ctx
);

// ============================================================================
// Extensible Delimiter Support
// ============================================================================

// Build an extensible delimiter to match target height
// Uses TFM extensible recipes (top/mid/bot/rep pieces)
TexNode* build_extensible_delimiter(
    Arena* arena,
    int base_char,           // Base character code
    float target_height,     // Desired height
    FontSpec& font,
    TFMFont* tfm,
    float size
);

// ============================================================================
// Delimiter Support
// ============================================================================

// Typeset left/right delimiters around content
TexNode* typeset_delimited(
    int32_t left_delim,      // '(', '[', '{', '|', or 0 for none
    TexNode* content,
    int32_t right_delim,
    MathContext& ctx,
    bool extensible = true   // True for \left/\right, false for matrix delimiters
);

// ============================================================================
// Inter-Atom Spacing (TeXBook Chapter 18)
// ============================================================================

// Get spacing between atom types in mu (1/18 quad)
float get_atom_spacing_mu(AtomType left, AtomType right, MathStyle style);

// Convert mu to points
float mu_to_pt(float mu, MathContext& ctx);

// Insert spacing nodes between atoms in a list
void apply_math_spacing(TexNode* first, MathContext& ctx);

// ============================================================================
// HList Integration - Inline Math
// ============================================================================

struct InlineMathResult {
    TexNode* before;         // Text nodes before math
    TexNode* math;           // Math HBox
    TexNode* after;          // Remaining text
    bool found;              // Whether math was found
};

// Find and extract first inline math ($...$) from text
// Splits text at math boundaries
InlineMathResult extract_inline_math(
    const char* text,
    size_t len,
    MathContext& ctx
);

// Process text with inline math, building complete HList
// Handles multiple inline math expressions
TexNode* process_text_with_math(
    const char* text,
    size_t len,
    MathContext& ctx,
    TFMFontManager* fonts
);

// ============================================================================
// Display Math for VList Integration
// ============================================================================

struct DisplayMathParams {
    float line_width;          // Width of text line (for centering)
    float above_skip;          // Vertical space above
    float below_skip;          // Vertical space below
    bool numbered;             // Whether to include equation number

    static DisplayMathParams defaults(float width) {
        DisplayMathParams p = {};
        p.line_width = width;
        p.above_skip = 12.0f;    // \abovedisplayskip
        p.below_skip = 12.0f;    // \belowdisplayskip
        p.numbered = false;
        return p;
    }
};

// Typeset display math (centered with surrounding space)
// Returns a VList containing the display with spacing
TexNode* typeset_display_math(
    const char* math_str,
    MathContext& ctx,
    const DisplayMathParams& params
);

// Typeset display math from pre-built content
TexNode* typeset_display_math_node(
    TexNode* content,
    MathContext& ctx,
    const DisplayMathParams& params
);

// ============================================================================
// Document Integration - Extract and Process Math
// ============================================================================

struct MathRegion {
    size_t start;            // Start offset in text
    size_t end;              // End offset in text
    bool is_display;         // true for $$...$$ or \[...\]
    const char* content;     // Math content (without delimiters)
    size_t content_len;
};

// Find all math regions in text
// Returns array of MathRegion (arena-allocated)
struct MathRegionList {
    MathRegion* regions;
    int count;
    int capacity;
};

MathRegionList find_math_regions(
    const char* text,
    size_t len,
    Arena* arena
);

// ============================================================================
// Lambda Item Math Conversion (for Radiant bridge)
// ============================================================================

#ifdef TEX_WITH_LAMBDA
// Convert a Lambda math node (Item) to TexNode tree
// This is the bridge between Lambda's math representation and TeX nodes
TexNode* convert_lambda_math(
    Item math_node,
    MathContext& ctx
);
#endif // TEX_WITH_LAMBDA

// ============================================================================
// Utility Functions
// ============================================================================

// Create math HBox from list of atoms (applies spacing)
TexNode* make_math_hbox(TexNode* first_atom, MathContext& ctx);

// Measure width of math content
float measure_math_width(TexNode* node);

// Center content to target width (returns HBox)
TexNode* center_math(TexNode* content, float target_width, Arena* arena);

} // namespace tex

#endif // TEX_MATH_BRIDGE_HPP
