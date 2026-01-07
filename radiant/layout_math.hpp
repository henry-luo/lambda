// layout_math.hpp - Math layout function declarations
//
// Functions for converting MathNode trees to MathBox trees,
// implementing TeXBook typesetting algorithms.

#ifndef RADIANT_LAYOUT_MATH_HPP
#define RADIANT_LAYOUT_MATH_HPP

#include "math_box.hpp"
#include "math_context.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/math_node.hpp"

namespace radiant {

// ============================================================================
// Main Layout Entry Point
// ============================================================================

/**
 * Layout a math node tree into a MathBox tree.
 *
 * @param node The root math node (Lambda element)
 * @param ctx The math context (style, font, etc.)
 * @param arena Arena for allocating MathBox structures
 * @return Root MathBox of the laid-out expression
 */
MathBox* layout_math(Item node, MathContext& ctx, Arena* arena);

// ============================================================================
// Layout Functions for Specific Node Types
// ============================================================================

// Layout a symbol (single character)
MathBox* layout_symbol(Item node, MathContext& ctx, Arena* arena);

// Layout a number
MathBox* layout_number(Item node, MathContext& ctx, Arena* arena);

// Layout a command (Greek letter, operator, etc.)
MathBox* layout_command(Item node, MathContext& ctx, Arena* arena);

// Layout a row (horizontal sequence)
MathBox* layout_row(Item node, MathContext& ctx, Arena* arena);

// Layout a group
MathBox* layout_group(Item node, MathContext& ctx, Arena* arena);

// Layout a fraction (\frac{num}{denom})
MathBox* layout_fraction(Item node, MathContext& ctx, Arena* arena);

// Layout a binomial (\binom{n}{k})
MathBox* layout_binomial(Item node, MathContext& ctx, Arena* arena);

// Layout a radical (\sqrt[n]{x})
MathBox* layout_radical(Item node, MathContext& ctx, Arena* arena);

// Layout subscript/superscript (x_i^2)
MathBox* layout_subsup(Item node, MathContext& ctx, Arena* arena);

// Layout a delimiter group (\left( ... \right))
MathBox* layout_delimiter(Item node, MathContext& ctx, Arena* arena);

// Layout an accent (\hat{x}, \vec{v})
MathBox* layout_accent(Item node, MathContext& ctx, Arena* arena);

// Layout a big operator (\sum, \int with limits)
MathBox* layout_big_operator(Item node, MathContext& ctx, Arena* arena);

// Layout text mode (\text{...})
MathBox* layout_text(Item node, MathContext& ctx, Arena* arena);

// Layout a style change (\mathbf, \displaystyle)
MathBox* layout_style(Item node, MathContext& ctx, Arena* arena);

// Layout explicit space (\quad, \,)
MathBox* layout_space(Item node, MathContext& ctx, Arena* arena);

// ============================================================================
// Inter-Box Spacing
// ============================================================================

/**
 * Apply inter-box spacing to a MathBox tree.
 * Inserts kerns between adjacent boxes according to TeXBook spacing rules.
 *
 * @param root The root MathBox
 * @param ctx The math context
 * @param arena Arena for allocating kern boxes
 */
void apply_inter_box_spacing(MathBox* root, MathContext& ctx, Arena* arena);

/**
 * Get spacing between two box types (in mu, 1mu = 1/18 em)
 *
 * @param left Left box type
 * @param right Right box type
 * @param tight True if in script/scriptscript style
 * @return Spacing in mu (0, 3, 4, or 5)
 */
int get_inter_box_spacing(MathBoxType left, MathBoxType right, bool tight);

// ============================================================================
// Glyph Metrics Helpers
// ============================================================================

/**
 * Get glyph metrics from FreeType.
 *
 * @param face FreeType font face
 * @param codepoint Unicode codepoint
 * @param font_size Font size in pixels
 * @param width Output: advance width
 * @param height Output: height above baseline
 * @param depth Output: depth below baseline
 * @param italic Output: italic correction
 */
void get_glyph_metrics(FT_Face face, int codepoint, float font_size,
                       float* width, float* height, float* depth, float* italic);

/**
 * Load a glyph and get a MathBox for it.
 *
 * @param ctx Math context (for font settings)
 * @param codepoint Unicode codepoint
 * @param type Box type for spacing
 * @param arena Arena for allocation
 * @return MathBox containing the glyph
 */
MathBox* make_glyph(MathContext& ctx, int codepoint, MathBoxType type, Arena* arena);

/**
 * Load font for math rendering.
 *
 * @param ctx Math context
 * @return FreeType font face
 */
FT_Face load_math_font(MathContext& ctx);

// ============================================================================
// Delimiter Helpers
// ============================================================================

/**
 * Create an extensible delimiter of the given height.
 *
 * @param ctx Math context
 * @param delimiter Delimiter string (e.g., "(", ")", "[", "]", "|")
 * @param target_height Desired height
 * @param is_left True for left delimiter, false for right
 * @param arena Arena for allocation
 * @return MathBox for the delimiter
 */
MathBox* make_delimiter(MathContext& ctx, const char* delimiter,
                        float target_height, bool is_left, Arena* arena);

// ============================================================================
// Radical Helpers
// ============================================================================

/**
 * Create a radical symbol (square root) of the given height.
 *
 * @param ctx Math context
 * @param radicand_box The laid-out radicand
 * @param index_box Optional index box (for nth roots)
 * @param arena Arena for allocation
 * @return MathBox for the complete radical
 */
MathBox* make_radical_box(MathContext& ctx, MathBox* radicand_box,
                          MathBox* index_box, Arena* arena);

} // namespace radiant

#endif // RADIANT_LAYOUT_MATH_HPP
