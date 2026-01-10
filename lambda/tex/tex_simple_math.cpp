// tex_simple_math.cpp - Minimal math typesetter implementation
//
// Implements simple math typesetting for DVI comparison testing.

#include "tex_simple_math.hpp"
#include "lib/log.h"
#include <cstring>
#include <cmath>
#include <cctype>

namespace tex {

// ============================================================================
// Computer Modern Font Metrics (Approximations)
// ============================================================================

// CMR10 character widths (scaled from TFM data)
// In units of design size (multiply by font_size/10)
static const float CMR_CHAR_WIDTHS[128] = {
    // 0-31: control characters (not used)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 32-47: space, punctuation
    // sp  !     "     #     $     %     &     '
    3.33, 2.78, 5.00, 8.33, 5.00, 8.33, 7.78, 2.78,
    // (    )     *     +     ,     -     .     /
    3.89, 3.89, 5.00, 7.78, 2.78, 3.33, 2.78, 5.00,
    // 48-57: digits
    // 0    1     2     3     4     5     6     7     8     9
    5.00, 5.00, 5.00, 5.00, 5.00, 5.00, 5.00, 5.00, 5.00, 5.00,
    // 58-63: more punctuation
    // :    ;     <     =     >     ?
    2.78, 2.78, 7.78, 7.78, 7.78, 4.72,
    // 64-90: @ and uppercase
    // @    A     B     C     D     E     F     G
    7.78, 7.50, 7.08, 7.22, 7.64, 6.81, 6.53, 7.85,
    // H    I     J     K     L     M     N     O
    7.50, 3.61, 5.14, 7.78, 6.25, 9.17, 7.50, 7.78,
    // P    Q     R     S     T     U     V     W
    6.81, 7.78, 7.36, 5.56, 7.22, 7.50, 7.50, 10.28,
    // X    Y     Z
    7.50, 7.50, 6.11,
    // 91-96: brackets, etc
    // [    \     ]     ^     _     `
    2.78, 5.00, 2.78, 5.00, 5.00, 2.78,
    // 97-122: lowercase
    // a    b     c     d     e     f     g     h
    5.00, 5.56, 4.44, 5.56, 4.44, 3.06, 5.00, 5.56,
    // i    j     k     l     m     n     o     p
    2.78, 3.06, 5.28, 2.78, 8.33, 5.56, 5.00, 5.56,
    // q    r     s     t     u     v     w     x
    5.28, 3.92, 3.94, 3.89, 5.56, 5.28, 7.22, 5.28,
    // y    z
    5.28, 4.44,
    // 123-127: braces, etc
    // {    |     }     ~    DEL
    4.80, 2.00, 4.80, 5.00, 0
};

// Math italic (cmmi) width adjustments for letters
static const float CMMI_ADJUST[26] = {
    // a-z width multipliers vs roman
    1.0f, 1.0f, 0.9f, 1.0f, 0.9f, 0.85f, 1.0f, 1.0f,
    0.8f, 0.9f, 1.0f, 0.8f, 1.1f, 1.0f, 1.0f, 1.0f,
    1.0f, 0.95f, 0.9f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f
};

SimpleFontMetrics get_cmr_metrics(float size_pt) {
    float scale = size_pt / 10.0f;

    return SimpleFontMetrics{
        .char_width = 5.0f * scale,      // Average
        .x_height = 4.31f * scale,       // cmr10 x-height
        .cap_height = 6.83f * scale,     // cmr10 cap height
        .ascender = 6.94f * scale,
        .descender = 1.94f * scale,
        .quad = 10.0f * scale            // 1em = design size
    };
}

float get_char_width(int32_t codepoint, float size_pt) {
    if (codepoint < 0 || codepoint >= 128) {
        return 5.0f * size_pt / 10.0f;  // Default width
    }
    return CMR_CHAR_WIDTHS[codepoint] * size_pt / 10.0f;
}

float get_math_symbol_width(int32_t codepoint, float size_pt) {
    // Math symbols are typically from cmsy or cmex
    // Approximate widths
    float scale = size_pt / 10.0f;

    switch (codepoint) {
        case '+': case '-':
            return 7.78f * scale;
        case '=':
            return 7.78f * scale;
        case '<': case '>':
            return 7.78f * scale;
        case '(': case ')':
            return 3.89f * scale;
        case '[': case ']':
            return 2.78f * scale;
        case '{': case '}':
            return 4.80f * scale;
        default:
            // Letters in math are italic
            if (codepoint >= 'a' && codepoint <= 'z') {
                float base = CMR_CHAR_WIDTHS[codepoint] * scale;
                return base * CMMI_ADJUST[codepoint - 'a'];
            }
            return get_char_width(codepoint, size_pt);
    }
}

// ============================================================================
// Math Symbol Classification
// ============================================================================

SimpleMathAtom classify_math_char(int32_t codepoint) {
    switch (codepoint) {
        // Binary operators
        case '+': case '-': case '*':
            return SimpleMathAtom::Bin;

        // Relations
        case '=': case '<': case '>':
            return SimpleMathAtom::Rel;

        // Opening delimiters
        case '(': case '[': case '{':
            return SimpleMathAtom::Open;

        // Closing delimiters
        case ')': case ']': case '}':
            return SimpleMathAtom::Close;

        // Punctuation
        case ',': case ';':
            return SimpleMathAtom::Punct;

        // Everything else is ordinary
        default:
            return SimpleMathAtom::Ord;
    }
}

// Inter-atom spacing table (TeXBook Chapter 18)
// Returns spacing in mu (1/18 em)
static const float SPACING_MU[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */  {0,  3,   4,   5,   0,   0,    0,    3},
    /* Op  */  {3,  3,   0,   5,   0,   0,    0,    3},
    /* Bin */  {4,  4,   0,   0,   4,   0,    0,    4},
    /* Rel */  {5,  5,   0,   0,   5,   0,    0,    5},
    /* Open*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/ {0,  3,   4,   5,   0,   0,    0,    3},
    /* Punct*/ {3,  3,   0,   3,   3,   3,    3,    3},
    /* Inner*/ {3,  3,   4,   5,   3,   0,    3,    3},
};

// Convert table value to mu
// 3 = thin space (3mu), 4 = medium space (4mu), 5 = thick space (5mu)
float get_math_spacing_mu(SimpleMathAtom left, SimpleMathAtom right) {
    int l = (int)left;
    int r = (int)right;
    if (l >= 8 || r >= 8) return 0;
    return SPACING_MU[l][r];
}

float mu_to_pt_simple(float mu, float quad) {
    // 1 mu = 1/18 em
    return mu * quad / 18.0f;
}

// ============================================================================
// Output Management
// ============================================================================

TypesetOutput* create_typeset_output(Arena* arena) {
    TypesetOutput* out = (TypesetOutput*)arena_alloc(arena, sizeof(TypesetOutput));
    out->glyphs = nullptr;
    out->glyph_count = 0;
    out->glyph_capacity = 0;
    out->rules = nullptr;
    out->rule_count = 0;
    out->rule_capacity = 0;
    out->total_width = 0;
    out->total_height = 0;
    out->total_depth = 0;
    return out;
}

void add_glyph(TypesetOutput* out, int32_t cp, float x, float y,
               const char* font, float size, Arena* arena) {
    if (out->glyph_count >= out->glyph_capacity) {
        int new_cap = out->glyph_capacity ? out->glyph_capacity * 2 : 32;
        PositionedGlyph* new_glyphs = (PositionedGlyph*)arena_alloc(arena,
            new_cap * sizeof(PositionedGlyph));
        if (out->glyphs) {
            memcpy(new_glyphs, out->glyphs, out->glyph_count * sizeof(PositionedGlyph));
        }
        out->glyphs = new_glyphs;
        out->glyph_capacity = new_cap;
    }

    PositionedGlyph& g = out->glyphs[out->glyph_count++];
    g.codepoint = cp;
    g.x = x;
    g.y = y;
    g.font = font;
    g.size_pt = size;
}

void add_rule(TypesetOutput* out, float x, float y, float w, float h, Arena* arena) {
    if (out->rule_count >= out->rule_capacity) {
        int new_cap = out->rule_capacity ? out->rule_capacity * 2 : 16;
        PositionedRule* new_rules = (PositionedRule*)arena_alloc(arena,
            new_cap * sizeof(PositionedRule));
        if (out->rules) {
            memcpy(new_rules, out->rules, out->rule_count * sizeof(PositionedRule));
        }
        out->rules = new_rules;
        out->rule_capacity = new_cap;
    }

    PositionedRule& r = out->rules[out->rule_count++];
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
}

// ============================================================================
// Simple Math Typesetter
// ============================================================================

TypesetOutput* typeset_simple_math(
    const char* math_expr,
    float font_size_pt,
    float start_x,
    float start_y,
    Arena* arena
) {
    TypesetOutput* out = create_typeset_output(arena);
    SimpleFontMetrics metrics = get_cmr_metrics(font_size_pt);

    float x = start_x;
    float y = start_y;

    SimpleMathAtom prev_atom = SimpleMathAtom::Ord;  // Dummy for first char
    bool first = true;

    for (const char* p = math_expr; *p; p++) {
        int32_t cp = (unsigned char)*p;

        // Skip whitespace in math
        if (isspace(cp)) continue;

        // Classify current character
        SimpleMathAtom curr_atom = classify_math_char(cp);

        // Add inter-atom spacing (except before first character)
        if (!first) {
            float spacing_mu = get_math_spacing_mu(prev_atom, curr_atom);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt_simple(spacing_mu, metrics.quad);
                x += spacing_pt;
            }
        }
        first = false;

        // Determine font based on character type
        const char* font = "cmmi10";  // Math italic for variables
        if (cp >= '0' && cp <= '9') {
            font = "cmr10";  // Roman for digits
        } else if (curr_atom == SimpleMathAtom::Bin ||
                   curr_atom == SimpleMathAtom::Rel) {
            font = "cmsy10";  // Symbol font for operators
        }

        // Add glyph
        add_glyph(out, cp, x, y, font, font_size_pt, arena);

        // Advance position
        float char_width = get_math_symbol_width(cp, font_size_pt);
        x += char_width;

        prev_atom = curr_atom;
    }

    out->total_width = x - start_x;
    out->total_height = metrics.cap_height;
    out->total_depth = 0;

    log_debug("typeset_simple_math: '%s' -> %d glyphs, width=%.2fpt",
              math_expr, out->glyph_count, out->total_width);

    return out;
}

// ============================================================================
// Fraction Typesetting
// ============================================================================

float typeset_fraction(
    const char* numerator,
    const char* denominator,
    float font_size_pt,
    float x, float y,
    TypesetOutput* out,
    Arena* arena
) {
    SimpleFontMetrics metrics = get_cmr_metrics(font_size_pt);

    // Script size for numerator/denominator
    float script_size = font_size_pt * 0.7f;  // scriptsize

    // Typeset numerator and denominator to temporary outputs
    TypesetOutput* num_out = typeset_simple_math(numerator, script_size, 0, 0, arena);
    TypesetOutput* den_out = typeset_simple_math(denominator, script_size, 0, 0, arena);

    // Calculate dimensions
    float frac_width = std::max(num_out->total_width, den_out->total_width);
    float rule_thickness = 0.4f * font_size_pt / 10.0f;  // Standard rule thickness

    // Center numerator and denominator
    float num_x = x + (frac_width - num_out->total_width) / 2.0f;
    float den_x = x + (frac_width - den_out->total_width) / 2.0f;

    // Vertical positions (relative to math axis = x-height/2)
    float axis = metrics.x_height / 2.0f;
    float num_shift = axis + rule_thickness / 2.0f + 1.5f * metrics.x_height * 0.7f;
    float den_shift = axis - rule_thickness / 2.0f - 0.8f * metrics.x_height * 0.7f;

    // Copy numerator glyphs with position adjustment
    for (int i = 0; i < num_out->glyph_count; i++) {
        add_glyph(out, num_out->glyphs[i].codepoint,
                  num_x + num_out->glyphs[i].x,
                  y - num_shift,
                  num_out->glyphs[i].font,
                  num_out->glyphs[i].size_pt, arena);
    }

    // Copy denominator glyphs
    for (int i = 0; i < den_out->glyph_count; i++) {
        add_glyph(out, den_out->glyphs[i].codepoint,
                  den_x + den_out->glyphs[i].x,
                  y + den_shift,
                  den_out->glyphs[i].font,
                  den_out->glyphs[i].size_pt, arena);
    }

    // Add fraction rule
    add_rule(out, x, y - axis, frac_width, rule_thickness, arena);

    return frac_width;
}

// ============================================================================
// Square Root Typesetting
// ============================================================================

float typeset_sqrt(
    const char* content,
    float font_size_pt,
    float x, float y,
    TypesetOutput* out,
    Arena* arena
) {
    SimpleFontMetrics metrics = get_cmr_metrics(font_size_pt);

    // Typeset content
    TypesetOutput* content_out = typeset_simple_math(content, font_size_pt, 0, 0, arena);

    // Radical sign dimensions (simplified)
    float surd_width = 5.0f * font_size_pt / 10.0f;
    float rule_thickness = 0.4f * font_size_pt / 10.0f;
    float clearance = rule_thickness * 1.5f;

    // Content height with clearance
    float content_height = metrics.cap_height + clearance;

    // Add surd (radical) glyph - character 112 in cmex
    // For simplicity, we'll skip the actual surd glyph for now
    // and just add the overline (vinculum)

    // Position content after surd
    float content_x = x + surd_width;

    // Copy content glyphs
    for (int i = 0; i < content_out->glyph_count; i++) {
        add_glyph(out, content_out->glyphs[i].codepoint,
                  content_x + content_out->glyphs[i].x,
                  y,
                  content_out->glyphs[i].font,
                  content_out->glyphs[i].size_pt, arena);
    }

    // Add vinculum (overline)
    float vinculum_y = y - content_height;
    add_rule(out, content_x, vinculum_y, content_out->total_width, rule_thickness, arena);

    return surd_width + content_out->total_width;
}

} // namespace tex
