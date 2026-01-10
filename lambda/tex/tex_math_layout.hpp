// tex_math_layout.hpp - TeX Math Layout Engine
//
// Implements TeXBook Appendix G math typesetting rules:
// - Atom classification and spacing
// - Script positioning (superscript/subscript)
// - Fraction layout
// - Delimiter sizing
// - Radical layout

#ifndef TEX_MATH_LAYOUT_HPP
#define TEX_MATH_LAYOUT_HPP

#include "tex_box.hpp"
#include "tex_glue.hpp"
#include "tex_font_metrics.hpp"
#include "../../lib/arena.h"

namespace tex {

// Forward declarations
struct MathLayoutContext;
struct TexNode;

// ============================================================================
// Math Atom Structure
// ============================================================================

// A math atom represents a single unit for spacing calculations
// TeXBook Chapter 17, p.158
struct MathAtom {
    AtomType type;
    TexBox* nucleus;      // The main content
    TexBox* superscript;  // Optional superscript
    TexBox* subscript;    // Optional subscript

    // For Op atoms
    bool limits;          // True for \limits, false for \nolimits

    // For Rad atoms
    TexBox* degree;       // Optional root degree (e.g., cube root)

    // Source for delimiter atoms
    uint32_t delimiter_codepoint;
};

// ============================================================================
// Math Layout Context
// ============================================================================

struct MathLayoutContext {
    Arena* arena;
    FontProvider* fonts;

    // Current state
    MathStyle style;
    float base_size_pt;     // Base font size in points

    // Computed values
    float current_size() const {
        return fonts->style_size(style, base_size_pt);
    }

    const MathSymbolParams& symbol_params() const;
    const MathExtensionParams& extension_params() const;

    // Derived contexts
    MathLayoutContext with_style(MathStyle new_style) const {
        MathLayoutContext ctx = *this;
        ctx.style = new_style;
        return ctx;
    }
};

// ============================================================================
// Math Layout Functions
// ============================================================================

// Main entry point: layout a math list
TexBox* layout_math_list(
    MathAtom* atoms,
    int atom_count,
    MathLayoutContext& ctx
);

// Layout individual atom types
TexBox* layout_ord_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_op_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_bin_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_rel_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_open_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_close_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_punct_atom(MathAtom* atom, MathLayoutContext& ctx);
TexBox* layout_inner_atom(MathAtom* atom, MathLayoutContext& ctx);

// ============================================================================
// Script Attachment (TeXBook Appendix G, Rules 18-18f)
// ============================================================================

struct ScriptAttachment {
    TexBox* nucleus;
    TexBox* superscript;   // may be null
    TexBox* subscript;     // may be null
};

// Attach scripts to a nucleus, returning the combined box
TexBox* attach_scripts(
    ScriptAttachment& scripts,
    AtomType atom_type,
    MathLayoutContext& ctx
);

// Position subscript only
void position_subscript_only(
    TexBox* nucleus,
    TexBox* subscript,
    MathLayoutContext& ctx
);

// Position superscript only
void position_superscript_only(
    TexBox* nucleus,
    TexBox* superscript,
    bool cramped,
    MathLayoutContext& ctx
);

// Position both scripts together
void position_both_scripts(
    TexBox* nucleus,
    TexBox* superscript,
    TexBox* subscript,
    MathLayoutContext& ctx
);

// ============================================================================
// Fraction Layout (TeXBook Appendix G, Rules 15a-15e)
// ============================================================================

struct FractionParams {
    TexBox* numerator;
    TexBox* denominator;
    float rule_thickness;     // 0 for \atop
    float left_delim;         // 0 for none
    float right_delim;        // 0 for none
    uint32_t left_delim_cp;   // Delimiter codepoint
    uint32_t right_delim_cp;
};

TexBox* layout_fraction(
    FractionParams& params,
    MathLayoutContext& ctx
);

// Generalized fraction (\genfrac in LaTeX)
TexBox* layout_genfrac(
    TexBox* numerator,
    TexBox* denominator,
    float thickness,          // rule thickness, <0 for default
    MathStyle override_style, // style to use, or (MathStyle)-1 for auto
    uint32_t left_delim,
    uint32_t right_delim,
    MathLayoutContext& ctx
);

// ============================================================================
// Delimiter Sizing (TeXBook Appendix G, Rule 19)
// ============================================================================

// Size a delimiter to match a given height+depth
SizedDelimiter size_delimiter(
    uint32_t codepoint,
    float target_height,
    float target_depth,
    MathLayoutContext& ctx
);

// Create a delimiter box from sized info
TexBox* make_delimiter_box(
    const SizedDelimiter& delim,
    MathLayoutContext& ctx
);

// Layout delimited expression: left_delim content right_delim
TexBox* layout_delimited(
    uint32_t left_delim,
    TexBox* content,
    uint32_t right_delim,
    MathLayoutContext& ctx
);

// ============================================================================
// Radical Layout (TeXBook Appendix G, Rules 11-11f)
// ============================================================================

TexBox* layout_radical(
    TexBox* radicand,
    TexBox* degree,           // may be null for sqrt
    MathLayoutContext& ctx
);

// ============================================================================
// Operator Layout (TeXBook Appendix G, Rules 13-13a)
// ============================================================================

// Layout a large operator with optional limits
TexBox* layout_large_op(
    uint32_t op_codepoint,
    TexBox* above_limit,      // may be null
    TexBox* below_limit,      // may be null
    bool display_limits,      // true to place limits above/below in display
    MathLayoutContext& ctx
);

// ============================================================================
// Accent Layout (TeXBook Appendix G, Rules 12-12a)
// ============================================================================

TexBox* layout_accent(
    uint32_t accent_codepoint,
    TexBox* base,
    MathLayoutContext& ctx
);

// Under-accent (underline, underbrace, etc.)
TexBox* layout_under_accent(
    uint32_t accent_codepoint,
    TexBox* base,
    MathLayoutContext& ctx
);

// ============================================================================
// Array/Matrix Layout
// ============================================================================

struct ArrayCell {
    TexBox* content;
    enum class Align { Left, Center, Right } align;
};

struct ArrayParams {
    ArrayCell** cells;        // [row][col]
    int row_count;
    int col_count;
    float* col_widths;        // null for auto
    float row_spacing;        // extra space between rows
    float col_spacing;        // space between columns
    bool has_hlines;          // draw horizontal lines
    bool has_vlines;          // draw vertical lines
};

TexBox* layout_array(
    ArrayParams& params,
    MathLayoutContext& ctx
);

// ============================================================================
// Spacing Utilities
// ============================================================================

// Compute spacing between two atoms
Glue compute_inter_atom_glue(
    AtomType left,
    AtomType right,
    MathLayoutContext& ctx
);

// Insert glue between atoms in a horizontal list
void insert_math_spacing(
    TexBox* hlist,
    MathLayoutContext& ctx
);

// ============================================================================
// Box Adjustments
// ============================================================================

// Center a box on the math axis
TexBox* center_on_axis(TexBox* box, MathLayoutContext& ctx);

// Rebox: create new hbox with specified width, content centered
TexBox* rebox(TexBox* box, float new_width, MathLayoutContext& ctx);

// Create horizontal strut (empty box with width only)
TexBox* hstrut(float width, MathLayoutContext& ctx);

// Create vertical strut
TexBox* vstrut(float height, float depth, MathLayoutContext& ctx);

// ============================================================================
// Display Math Layout
// ============================================================================

// Layout for display math (centered with equation number)
struct DisplayMathParams {
    TexBox* content;
    TexBox* equation_number;  // may be null
    bool number_on_left;
    float line_width;
};

TexBox* layout_display_math(
    DisplayMathParams& params,
    MathLayoutContext& ctx
);

// Multi-line display (align, gather, etc.)
struct AlignmentLine {
    TexBox** cells;           // Alternating left/right aligned
    int cell_count;
};

TexBox* layout_alignment(
    AlignmentLine* lines,
    int line_count,
    float line_width,
    MathLayoutContext& ctx
);

// ============================================================================
// Inline Helpers
// ============================================================================

// Get axis height for current style
inline float axis_height(MathLayoutContext& ctx) {
    return ctx.symbol_params().axis_height;
}

// Get rule thickness
inline float rule_thickness(MathLayoutContext& ctx) {
    return ctx.extension_params().default_rule_thickness;
}

// Check if operator limits should be placed above/below
inline bool should_use_limits(bool has_limits_modifier, MathLayoutContext& ctx) {
    if (has_limits_modifier) return true;
    return is_display(ctx.style);
}

} // namespace tex

#endif // TEX_MATH_LAYOUT_HPP
