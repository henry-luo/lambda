// tex_simple_math.hpp - Minimal math typesetter for DVI comparison testing
//
// A simplified, self-contained math typesetter that bypasses the complex
// AST infrastructure and directly produces positioned glyphs.
//
// This is designed for testing purposes - to validate our TeX algorithms
// against DVI reference output.

#ifndef TEX_SIMPLE_MATH_HPP
#define TEX_SIMPLE_MATH_HPP

#include "tex_glue.hpp"
#include "tex_box.hpp"
#include "lib/arena.h"
#include <cstdint>

namespace tex {

// ============================================================================
// Positioned Glyph Output
// ============================================================================

struct PositionedGlyph {
    int32_t codepoint;      // Character code
    float x;                // Horizontal position (pt)
    float y;                // Vertical position (pt)
    const char* font;       // Font name
    float size_pt;          // Font size
};

struct PositionedRule {
    float x, y;             // Position (pt)
    float width, height;    // Dimensions (pt)
};

struct TypesetOutput {
    PositionedGlyph* glyphs;
    int glyph_count;
    int glyph_capacity;

    PositionedRule* rules;
    int rule_count;
    int rule_capacity;

    float total_width;
    float total_height;
    float total_depth;
};

// ============================================================================
// Simple Math Typesetter
// ============================================================================

// Initialize output structure
TypesetOutput* create_typeset_output(Arena* arena);

// Add a positioned glyph
void add_glyph(TypesetOutput* out, int32_t cp, float x, float y,
               const char* font, float size, Arena* arena);

// Add a positioned rule (for fraction bars, etc.)
void add_rule(TypesetOutput* out, float x, float y, float w, float h, Arena* arena);

// ============================================================================
// Simple Math Expression Typesetter
// ============================================================================

// Typeset a simple math expression like "a + b = c"
// Input: ASCII math string
// Output: Positioned glyphs ready for DVI comparison
TypesetOutput* typeset_simple_math(
    const char* math_expr,
    float font_size_pt,
    float start_x,
    float start_y,
    Arena* arena
);

// Typeset a fraction: \frac{num}{den}
// Returns width of fraction
float typeset_fraction(
    const char* numerator,
    const char* denominator,
    float font_size_pt,
    float x, float y,
    TypesetOutput* out,
    Arena* arena
);

// Typeset a square root: \sqrt{content}
float typeset_sqrt(
    const char* content,
    float font_size_pt,
    float x, float y,
    TypesetOutput* out,
    Arena* arena
);

// ============================================================================
// Font Metrics (Simplified - Computer Modern approximation)
// ============================================================================

struct SimpleFontMetrics {
    float char_width;       // Average character width
    float x_height;         // Height of lowercase 'x'
    float cap_height;       // Height of capitals
    float ascender;         // Max ascender height
    float descender;        // Max descender depth
    float quad;             // 1em in this font
};

// Get metrics for Computer Modern Roman at given size
SimpleFontMetrics get_cmr_metrics(float size_pt);

// Get width of a character in cmr
float get_char_width(int32_t codepoint, float size_pt);

// Get width of a math symbol
float get_math_symbol_width(int32_t codepoint, float size_pt);

// ============================================================================
// Math Symbol Classification (for spacing)
// ============================================================================

enum class SimpleMathAtom {
    Ord,        // Ordinary (variables, numbers)
    Op,         // Large operators (sum, integral)
    Bin,        // Binary operators (+, -, ×)
    Rel,        // Relations (=, <, >)
    Open,       // Opening delimiters
    Close,      // Closing delimiters
    Punct,      // Punctuation
    Inner       // Fractions, delimited expressions
};

SimpleMathAtom classify_math_char(int32_t codepoint);

// Get inter-atom spacing in mu (math units)
float get_math_spacing_mu(SimpleMathAtom left, SimpleMathAtom right);

// Convert mu to pt given font size
float mu_to_pt_simple(float mu, float quad);

} // namespace tex

#endif // TEX_SIMPLE_MATH_HPP
