// tex_math_bridge.cpp - Math Bridge Implementation
//
// Converts math expressions to TeX nodes for typesetting.

#include "tex_math_bridge.hpp"
#include "tex_hlist.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>
#include <cctype>

#ifdef TEX_WITH_LAMBDA
#include "../../lambda/math_node.hpp"
#endif

namespace tex {

// ============================================================================
// Math Style Functions
// ============================================================================

// Note: is_cramped, sup_style, sub_style are inline in tex_font_metrics.hpp
// We only need style_size_factor here

float style_size_factor(MathStyle style) {
    switch (style) {
        case MathStyle::Display:
        case MathStyle::DisplayPrime:
        case MathStyle::Text:
        case MathStyle::TextPrime:
            return 1.0f;
        case MathStyle::Script:
        case MathStyle::ScriptPrime:
            return 0.7f;       // Script size
        case MathStyle::ScriptScript:
        case MathStyle::ScriptScriptPrime:
            return 0.5f;       // ScriptScript size
        default:
            return 1.0f;
    }
}

// ============================================================================
// Font Variant Parsing
// ============================================================================

FontVariant parse_font_variant(const char* cmd) {
    if (!cmd) return FontVariant::Normal;

    // Skip leading "math" prefix if present (e.g., "mathbf" -> "bf")
    if (strncmp(cmd, "math", 4) == 0) {
        cmd += 4;
    }

    // Match the suffix
    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "normal") == 0) {
        return FontVariant::Roman;
    }
    if (strcmp(cmd, "bf") == 0 || strcmp(cmd, "bold") == 0) {
        return FontVariant::Bold;
    }
    if (strcmp(cmd, "it") == 0) {
        return FontVariant::Italic;
    }
    if (strcmp(cmd, "bfit") == 0) {
        return FontVariant::BoldItalic;
    }
    if (strcmp(cmd, "sf") == 0) {
        return FontVariant::SansSerif;
    }
    if (strcmp(cmd, "tt") == 0) {
        return FontVariant::Monospace;
    }
    if (strcmp(cmd, "cal") == 0) {
        return FontVariant::Calligraphic;
    }
    if (strcmp(cmd, "scr") == 0) {
        return FontVariant::Script;
    }
    if (strcmp(cmd, "frak") == 0) {
        return FontVariant::Fraktur;
    }
    if (strcmp(cmd, "bb") == 0) {
        return FontVariant::Blackboard;
    }

    return FontVariant::Normal;
}

// ============================================================================
// Atom Classification
// ============================================================================

AtomType classify_codepoint(int32_t cp) {
    // Binary operators
    if (cp == '+' || cp == '-' || cp == '*' || cp == '/' ||
        cp == 0x00D7 /* × */ || cp == 0x00B7 /* · */ ||
        cp == 0x2212 /* − */ || cp == 0x00B1 /* ± */) {
        return AtomType::Bin;
    }

    // Relations
    if (cp == '=' || cp == '<' || cp == '>' ||
        cp == 0x2264 /* ≤ */ || cp == 0x2265 /* ≥ */ ||
        cp == 0x2260 /* ≠ */ || cp == 0x2248 /* ≈ */ ||
        cp == 0x2261 /* ≡ */ || cp == 0x221D /* ∝ */ ||
        cp == 0x2208 /* ∈ */ || cp == 0x2286 /* ⊆ */) {
        return AtomType::Rel;
    }

    // Opening delimiters
    if (cp == '(' || cp == '[' || cp == '{' ||
        cp == 0x27E8 /* ⟨ */ || cp == 0x230A /* ⌊ */ ||
        cp == 0x2308 /* ⌈ */) {
        return AtomType::Open;
    }

    // Closing delimiters
    if (cp == ')' || cp == ']' || cp == '}' ||
        cp == 0x27E9 /* ⟩ */ || cp == 0x230B /* ⌋ */ ||
        cp == 0x2309 /* ⌉ */) {
        return AtomType::Close;
    }

    // Punctuation
    if (cp == ',' || cp == ';' || cp == ':') {
        return AtomType::Punct;
    }

    // Large operators (commonly used)
    if (cp == 0x2211 /* ∑ */ || cp == 0x220F /* ∏ */ ||
        cp == 0x222B /* ∫ */ || cp == 0x222C /* ∬ */ ||
        cp == 0x222D /* ∭ */ || cp == 0x222E /* ∮ */ ||
        cp == 0x22C2 /* ⋂ */ || cp == 0x22C3 /* ⋃ */ ||
        cp == 0x22C0 /* ⋀ */ || cp == 0x22C1 /* ⋁ */) {
        return AtomType::Op;
    }

    // Everything else is ordinary
    return AtomType::Ord;
}

// ============================================================================
// Inter-Atom Spacing Table (TeXBook Chapter 18)
// Values in mu: 0=none, 3=thin, 4=medium, 5=thick
// ============================================================================

static const float SPACING_MU_TABLE[8][8] = {
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

// Tight spacing for script/scriptscript
static const float TIGHT_SPACING_MU_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */  {0,  3,   0,   0,   0,   0,    0,    0},
    /* Op  */  {3,  3,   0,   0,   0,   0,    0,    0},
    /* Bin */  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Rel */  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Open*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/ {0,  3,   0,   0,   0,   0,    0,    0},
    /* Punct*/ {0,  0,   0,   0,   0,   0,    0,    0},
    /* Inner*/ {0,  3,   0,   0,   0,   0,    0,    0},
};

float get_atom_spacing_mu(AtomType left, AtomType right, MathStyle style) {
    int l = (int)left;
    int r = (int)right;
    if (l >= 8 || r >= 8) return 0;

    bool tight = (style >= MathStyle::Script);
    return tight ? TIGHT_SPACING_MU_TABLE[l][r] : SPACING_MU_TABLE[l][r];
}

float mu_to_pt(float mu, MathContext& ctx) {
    // 1 mu = 1/18 quad
    return mu * ctx.quad / 18.0f;
}

// ============================================================================
// LaTeX Math Parser
// ============================================================================

// Create a math char node with proper TFM metrics
static TexNode* make_char_with_metrics(Arena* arena, int char_code,
                                        AtomType atom_type, FontSpec& font,
                                        TFMFont* tfm, float size) {
    TexNode* node = make_math_char(arena, char_code, atom_type, font);

    float width = 5.0f * size / 10.0f;
    float height = size * 0.7f;
    float depth = 0;
    float italic_corr = 0;

    // TFM stores metrics pre-scaled by design_size, so divide by it
    if (tfm && char_code < 256 && char_code >= 0) {
        float scale = size / tfm->design_size;
        width = tfm->char_width(char_code) * scale;
        height = tfm->char_height(char_code) * scale;
        depth = tfm->char_depth(char_code) * scale;
        italic_corr = tfm->char_italic(char_code) * scale;
    }

    node->width = width;
    node->height = height;
    node->depth = depth;
    node->italic = italic_corr;

    return node;
}

// Build an extensible delimiter from TFM recipe
// Returns a VBox containing the assembled delimiter pieces
TexNode* build_extensible_delimiter(Arena* arena, int base_char,
                                    float target_height, FontSpec& font,
                                    TFMFont* tfm, float size) {
    if (!tfm) return nullptr;

    // First, try finding a pre-built size that's large enough
    int current_char = base_char;
    int steps = 0;
    while (steps < 8) {  // Limit chain length
        float char_height = tfm->char_height(current_char) * size +
                           tfm->char_depth(current_char) * size;
        if (char_height >= target_height) {
            // Found a pre-built size that works
            return make_char_with_metrics(arena, current_char, AtomType::Ord, font, tfm, size);
        }

        int next = tfm->get_next_larger(current_char);
        if (next == 0 || next == current_char) break;
        current_char = next;
        steps++;
    }

    // Need to build from extensible recipe
    const ExtensibleRecipe* recipe = tfm->get_extensible(current_char);
    if (!recipe) {
        // No extensible recipe - use largest available
        return make_char_with_metrics(arena, current_char, AtomType::Ord, font, tfm, size);
    }

    // Build the extensible delimiter as a VBox
    TexNode* vbox = make_vbox(arena, target_height);

    // Get heights of each piece
    float top_h = (recipe->top != 0) ? (tfm->char_height(recipe->top) + tfm->char_depth(recipe->top)) * size : 0;
    float mid_h = (recipe->mid != 0) ? (tfm->char_height(recipe->mid) + tfm->char_depth(recipe->mid)) * size : 0;
    float bot_h = (recipe->bot != 0) ? (tfm->char_height(recipe->bot) + tfm->char_depth(recipe->bot)) * size : 0;
    float rep_h = (tfm->char_height(recipe->rep) + tfm->char_depth(recipe->rep)) * size;

    // Calculate how much space needs to be filled with repeaters
    float fixed_h = top_h + mid_h + bot_h;
    float remaining = target_height - fixed_h;

    // Number of repeater copies needed (divide between top-mid and mid-bot if mid exists)
    int rep_count = (rep_h > 0) ? (int)ceil(remaining / rep_h) : 0;
    if (rep_count < 0) rep_count = 0;

    // Build from top to bottom
    float total_width = 0;

    // Top piece
    if (recipe->top != 0) {
        TexNode* top_node = make_char_with_metrics(arena, recipe->top, AtomType::Ord, font, tfm, size);
        vbox->append_child(top_node);
        if (top_node->width > total_width) total_width = top_node->width;
    }

    // Repeaters (first half if we have middle)
    int reps_before_mid = (recipe->mid != 0) ? rep_count / 2 : rep_count;
    for (int r = 0; r < reps_before_mid; r++) {
        TexNode* rep_node = make_char_with_metrics(arena, recipe->rep, AtomType::Ord, font, tfm, size);
        vbox->append_child(rep_node);
        if (rep_node->width > total_width) total_width = rep_node->width;
    }

    // Middle piece
    if (recipe->mid != 0) {
        TexNode* mid_node = make_char_with_metrics(arena, recipe->mid, AtomType::Ord, font, tfm, size);
        vbox->append_child(mid_node);
        if (mid_node->width > total_width) total_width = mid_node->width;

        // More repeaters after middle
        int reps_after_mid = rep_count - reps_before_mid;
        for (int r = 0; r < reps_after_mid; r++) {
            TexNode* rep_node = make_char_with_metrics(arena, recipe->rep, AtomType::Ord, font, tfm, size);
            vbox->append_child(rep_node);
            if (rep_node->width > total_width) total_width = rep_node->width;
        }
    }

    // Bottom piece
    if (recipe->bot != 0) {
        TexNode* bot_node = make_char_with_metrics(arena, recipe->bot, AtomType::Ord, font, tfm, size);
        vbox->append_child(bot_node);
        if (bot_node->width > total_width) total_width = bot_node->width;
    }

    // Set vbox dimensions
    vbox->width = total_width;
    vbox->height = target_height / 2.0f;  // Above axis
    vbox->depth = target_height / 2.0f;   // Below axis

    log_debug("math_bridge: built extensible delimiter char=%d target=%.1f pieces=%d+%d",
              base_char, target_height, reps_before_mid, rep_count - reps_before_mid);

    return vbox;
}

// Forward declaration for tree-sitter based implementation
TexNode* typeset_latex_math_ts(const char* latex_str, size_t len, MathContext& ctx);

// Public entry point for LaTeX math parsing
TexNode* typeset_latex_math(const char* latex_str, size_t len, MathContext& ctx) {
    log_debug("math_bridge: typeset_latex_math '%.*s'", (int)len, latex_str);
    // Use tree-sitter based parser (tex_math_ts.cpp)
    return typeset_latex_math_ts(latex_str, len, ctx);
}

// ============================================================================
// Fraction Typesetting
// ============================================================================

TexNode* typeset_fraction(TexNode* numerator, TexNode* denominator,
                          float rule_thickness, MathContext& ctx) {
    Arena* arena = ctx.arena;

    float size_pt = ctx.base_size_pt;

    // TeX sigma table values (from cmsy10 fontdimens, TeXBook p. 445)
    // These are the shift amounts from the main baseline to num/denom baselines
    float axis = 2.5f * size_pt / 10.0f;  // axis_height (fontdimen22)
    float num1 = 6.77f * size_pt / 10.0f;   // display num shift (fontdimen8)
    float num2 = 3.94f * size_pt / 10.0f;   // text num shift with rule (fontdimen9)
    float denom1 = 6.86f * size_pt / 10.0f; // display denom shift (fontdimen11)
    float denom2 = 3.45f * size_pt / 10.0f; // text denom shift (fontdimen12)

    // Minimum clearance between content and rule (3*rule for display, 1*rule for text)
    float default_rule = 0.4f * size_pt / 10.0f;
    float min_gap_display = 3.0f * default_rule;
    float min_gap_text = default_rule;

    float num_shift, denom_shift, min_gap;
    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
        num_shift = num1;
        denom_shift = denom1;
        min_gap = min_gap_display;
    } else {
        num_shift = num2;
        denom_shift = denom2;
        min_gap = min_gap_text;
    }

    // TeX algorithm: position num/denom baselines, then adjust if clearance insufficient
    // num_y is the y-position of numerator baseline (positive = above main baseline)
    // denom_y is the y-position of denominator baseline (negative = below main baseline)
    float num_y = num_shift;
    float denom_y = -denom_shift;

    // Check clearance: gap between bottom of numerator and top of rule
    float rule_top = axis + rule_thickness / 2.0f;
    float num_bottom = num_y - numerator->depth;
    float clearance_above = num_bottom - rule_top;
    if (clearance_above < min_gap) {
        num_y += (min_gap - clearance_above);
    }

    // Check clearance: gap between bottom of rule and top of denominator
    float rule_bottom = axis - rule_thickness / 2.0f;
    float denom_top = denom_y + denominator->height;
    float clearance_below = rule_bottom - denom_top;
    if (clearance_below < min_gap) {
        denom_y -= (min_gap - clearance_below);
    }

    // Create fraction bar at axis height
    float bar_width = fmaxf(numerator->width, denominator->width) + 4.0f;
    TexNode* bar = make_rule(arena, bar_width, rule_thickness, 0);
    bar->y = axis;  // bar is centered on axis

    // Create Fraction node (not VBox!) for proper HTML rendering
    TexNode* frac = make_fraction(arena, numerator, denominator, rule_thickness);

    // Center numerator and denominator
    float total_width = bar_width;
    numerator->x = (total_width - numerator->width) / 2.0f;
    numerator->y = num_y;
    denominator->x = (total_width - denominator->width) / 2.0f;
    denominator->y = denom_y;
    bar->x = 0;

    // Set fraction node dimensions
    frac->width = total_width;
    frac->height = numerator->y + numerator->height;
    frac->depth = -(denominator->y - denominator->depth);

    // Create proper structure - numerator, bar, denominator as children
    frac->first_child = numerator;
    numerator->next_sibling = bar;
    bar->prev_sibling = numerator;
    bar->next_sibling = denominator;
    denominator->prev_sibling = bar;
    frac->last_child = denominator;

    for (TexNode* n = numerator; n; n = n->next_sibling) {
        n->parent = frac;
    }

    log_debug("math_bridge: fraction %.2fpt x %.2fpt", frac->width, frac->height + frac->depth);

    return frac;
}

TexNode* typeset_fraction_strings(const char* num_str, const char* denom_str,
                                   MathContext& ctx) {
    // Typeset numerator in script style
    MathContext num_ctx = ctx;
    num_ctx.style = sup_style(ctx.style);
    TexNode* num = typeset_latex_math(num_str, strlen(num_str), num_ctx);

    // Typeset denominator in script style
    MathContext denom_ctx = ctx;
    denom_ctx.style = sup_style(ctx.style);
    TexNode* denom = typeset_latex_math(denom_str, strlen(denom_str), denom_ctx);

    return typeset_fraction(num, denom, ctx.rule_thickness, ctx);
}

// ============================================================================
// Square Root Typesetting
// ============================================================================

TexNode* typeset_sqrt(TexNode* radicand, MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Radical parameters (TeXBook p. 443)
    float rule = ctx.rule_thickness;
    float phi;  // Clearance above rule

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
        phi = rule + (ctx.x_height / 4.0f);
    } else {
        phi = rule + (rule / 4.0f);
    }

    // Radical sign dimensions
    float rad_width = 8.0f * ctx.base_size_pt / 10.0f;  // Width of √ symbol

    // Create radical node
    TexNode* radical = alloc_node(arena, NodeClass::Radical);
    radical->content.radical.radicand = radicand;
    radical->content.radical.degree = nullptr;
    radical->content.radical.rule_thickness = rule;
    radical->content.radical.rule_y = radicand->height + phi;

    // Total dimensions
    radical->width = rad_width + radicand->width;
    radical->height = radicand->height + phi + rule;
    radical->depth = radicand->depth;

    // Position radicand
    radicand->x = rad_width;
    radicand->y = 0;
    radicand->parent = radical;

    radical->first_child = radicand;
    radical->last_child = radicand;

    log_debug("math_bridge: sqrt %.2fpt x %.2fpt", radical->width, radical->height + radical->depth);

    return radical;
}

TexNode* typeset_root(TexNode* degree, TexNode* radicand, MathContext& ctx) {
    // First typeset the basic sqrt
    TexNode* radical = typeset_sqrt(radicand, ctx);

    if (degree) {
        // Position degree above and to the left of the radical sign
        // In TeX, the root index appears BEFORE the radical sign
        radical->content.radical.degree = degree;

        // Degree is scriptscript style - it's smaller
        // Place degree so it overlaps with the radical sign's left side
        // Using negative x to indicate it's to the left of the radical origin
        float deg_width = degree->width;
        float deg_shift_y = radical->height * 0.6f;  // Raise it up

        // Expand the radical's width to include the degree area on the left
        float extra_left = deg_width;
        radical->width += extra_left;

        // Shift radicand's x position to account for the degree width
        if (radical->content.radical.radicand) {
            radical->content.radical.radicand->x += extra_left;
        }

        // Degree is at the left edge (x=0), radical sign is after it
        degree->x = 0;
        degree->y = deg_shift_y;
        degree->parent = radical;

        log_debug("typeset_root: degree x=%.1f width=%.1f, radicand x=%.1f, total width=%.1f",
                  degree->x, degree->width,
                  radical->content.radical.radicand ? radical->content.radical.radicand->x : 0.0f,
                  radical->width);
    }

    return radical;
}

TexNode* typeset_sqrt_string(const char* content_str, MathContext& ctx) {
    TexNode* radicand = typeset_latex_math(content_str, strlen(content_str), ctx);
    return typeset_sqrt(radicand, ctx);
}

// ============================================================================
// Big Operator Limits Typesetting
// ============================================================================

// Typeset limits above/below a big operator (display style)
TexNode* typeset_op_limits(TexNode* op_node, TexNode* subscript, TexNode* superscript,
                           MathContext& ctx) {
    Arena* arena = ctx.arena;

    // If not display style, use regular scripts instead (but skip the MathOp check)
    // Note: We directly build scripts here instead of calling typeset_scripts
    // to avoid infinite recursion
    if (ctx.style != MathStyle::Display && ctx.style != MathStyle::DisplayPrime) {
        // Build inline scripts for non-display style
        // Script parameters (TeXBook p. 445)
        float sup_shift, sub_shift;
        if (is_cramped(ctx.style)) {
            sup_shift = 3.5f * ctx.base_size_pt / 10.0f;  // sup3
            sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
        } else {
            sup_shift = 3.8f * ctx.base_size_pt / 10.0f;  // sup2
            sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
        }

        // Create scripts node
        TexNode* scripts = alloc_node(arena, NodeClass::Scripts);
        scripts->content.scripts.nucleus = op_node;
        scripts->content.scripts.subscript = subscript;
        scripts->content.scripts.superscript = superscript;

        // Start with nucleus dimensions
        float total_width = op_node->width;
        float total_height = op_node->height;
        float total_depth = op_node->depth;

        // Add italic correction to script position
        float italic_corr = op_node->italic;

        // Position superscript
        if (superscript) {
            superscript->x = total_width + italic_corr;
            superscript->y = sup_shift;
            superscript->parent = scripts;

            total_width = superscript->x + superscript->width;
            if (superscript->y + superscript->height > total_height) {
                total_height = superscript->y + superscript->height;
            }
        }

        // Position subscript
        if (subscript) {
            subscript->x = op_node->width;  // No italic correction for subscript
            subscript->y = -sub_shift;
            subscript->parent = scripts;

            if (subscript->x + subscript->width > total_width) {
                total_width = subscript->x + subscript->width;
            }
            if (-subscript->y + subscript->depth > total_depth) {
                total_depth = -subscript->y + subscript->depth;
            }
        }

        // Set dimensions
        scripts->width = total_width;
        scripts->height = total_height;
        scripts->depth = total_depth;

        // Link children
        op_node->parent = scripts;
        scripts->first_child = op_node;
        scripts->last_child = op_node;

        if (superscript) {
            op_node->next_sibling = superscript;
            superscript->prev_sibling = op_node;
            scripts->last_child = superscript;
        }
        if (subscript) {
            scripts->last_child->next_sibling = subscript;
            subscript->prev_sibling = scripts->last_child;
            scripts->last_child = subscript;
        }

        return scripts;
    }

    // Display mode: Use Scripts node with explicit x,y coordinates
    // so DVI output respects the stacking order (superscript above, subscript below)
    TexNode* result = alloc_node(arena, NodeClass::Scripts);
    result->content.scripts.nucleus = op_node;
    result->content.scripts.subscript = subscript;
    result->content.scripts.superscript = superscript;

    // Calculate widths for centering
    float sup_width = superscript ? superscript->width : 0;
    float sub_width = subscript ? subscript->width : 0;
    float max_width = op_node->width;
    if (sup_width > max_width) max_width = sup_width;
    if (sub_width > max_width) max_width = sub_width;

    // Centering offsets
    float sup_offset = (max_width - sup_width) / 2.0f;
    float sub_offset = (max_width - sub_width) / 2.0f;
    float op_offset = (max_width - op_node->width) / 2.0f;

    // Spacing parameters (TeXBook p. 445)
    float big_op_spacing3 = ctx.base_size_pt * 0.2f;  // between op and limits

    // Link children in DVI output order: superscript -> operator (nucleus) -> subscript
    // This matches TeX reference DVI ordering for display-style limits

    // Position operator at center
    op_node->x = op_offset;
    op_node->y = 0;
    op_node->parent = result;

    // Start with operator dimensions
    float total_height = op_node->height;
    float total_depth = op_node->depth;

    // First, position superscript and make it the first child
    TexNode* prev = nullptr;
    if (superscript) {
        superscript->x = sup_offset;
        superscript->y = op_node->height + big_op_spacing3 + superscript->depth;
        superscript->parent = result;
        total_height = superscript->y + superscript->height;

        result->first_child = superscript;
        result->last_child = superscript;
        prev = superscript;
    }

    // Link operator after superscript (or as first child if no superscript)
    if (prev) {
        prev->next_sibling = op_node;
        op_node->prev_sibling = prev;
        result->last_child = op_node;
    } else {
        result->first_child = op_node;
        result->last_child = op_node;
    }
    prev = op_node;

    // Position and link subscript below operator
    if (subscript) {
        subscript->x = sub_offset;
        subscript->y = -(op_node->depth + big_op_spacing3 + subscript->height);
        subscript->parent = result;
        total_depth = -subscript->y + subscript->depth;

        prev->next_sibling = subscript;
        subscript->prev_sibling = prev;
        result->last_child = subscript;
    }

    result->width = max_width;
    result->height = total_height;
    result->depth = total_depth;

    log_debug("math_bridge: op_limits %.2fpt x (%.2f + %.2f)",
              result->width, result->height, result->depth);

    return result;
}

// ============================================================================
// Subscript/Superscript Typesetting
// ============================================================================

TexNode* typeset_scripts(TexNode* nucleus, TexNode* subscript, TexNode* superscript,
                         MathContext& ctx) {
    Arena* arena = ctx.arena;

    // Check if nucleus is a big operator that should use limits
    if (nucleus && nucleus->node_class == NodeClass::MathOp) {
        if (nucleus->content.math_op.limits) {
            return typeset_op_limits(nucleus, subscript, superscript, ctx);
        }
    }

    // Script parameters (TeXBook p. 445)
    float sup_shift, sub_shift;

    if (ctx.style == MathStyle::Display || ctx.style == MathStyle::DisplayPrime) {
        sup_shift = 4.0f * ctx.base_size_pt / 10.0f;  // sup1
        sub_shift = 2.5f * ctx.base_size_pt / 10.0f;  // sub1
    } else if (is_cramped(ctx.style)) {
        sup_shift = 3.5f * ctx.base_size_pt / 10.0f;  // sup3
        sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
    } else {
        sup_shift = 3.8f * ctx.base_size_pt / 10.0f;  // sup2
        sub_shift = 2.0f * ctx.base_size_pt / 10.0f;  // sub2
    }

    // Create scripts node
    TexNode* scripts = alloc_node(arena, NodeClass::Scripts);
    scripts->content.scripts.nucleus = nucleus;
    scripts->content.scripts.subscript = subscript;
    scripts->content.scripts.superscript = superscript;

    // Position nucleus at origin
    nucleus->x = 0;
    nucleus->y = 0;

    // Start with nucleus dimensions
    float total_width = nucleus->width;
    float total_height = nucleus->height;
    float total_depth = nucleus->depth;

    // Add italic correction to script position
    float italic_corr = nucleus->italic;

    // Position superscript
    if (superscript) {
        superscript->x = total_width + italic_corr;
        superscript->y = sup_shift;
        superscript->parent = scripts;

        total_width = superscript->x + superscript->width;
        if (superscript->y + superscript->height > total_height) {
            total_height = superscript->y + superscript->height;
        }
    }

    // Position subscript
    if (subscript) {
        subscript->x = nucleus->width;  // No italic correction for subscript
        subscript->y = -sub_shift;
        subscript->parent = scripts;

        if (subscript->x + subscript->width > total_width) {
            total_width = subscript->x + subscript->width;
        }
        if (-subscript->y + subscript->depth > total_depth) {
            total_depth = -subscript->y + subscript->depth;
        }
    }

    // Set dimensions
    scripts->width = total_width;
    scripts->height = total_height;
    scripts->depth = total_depth;

    // Link children
    nucleus->parent = scripts;
    scripts->first_child = nucleus;
    scripts->last_child = nucleus;

    if (superscript) {
        nucleus->next_sibling = superscript;
        superscript->prev_sibling = nucleus;
        scripts->last_child = superscript;
    }
    if (subscript) {
        scripts->last_child->next_sibling = subscript;
        subscript->prev_sibling = scripts->last_child;
        scripts->last_child = subscript;
    }

    log_debug("math_bridge: scripts %.2fpt x %.2fpt", scripts->width,
              scripts->height + scripts->depth);

    return scripts;
}

// ============================================================================
// Delimiter Typesetting
// ============================================================================

// Map Unicode/ASCII delimiter to cmex10 starting character code
// cmex10 has delimiter chains: char→next_larger→next_larger...→extensible recipe
static int delim_to_cmex_start(int32_t delim, bool is_left) {
    switch (delim) {
        case '(':  return is_left ? 0 : 1;   // small parens at 0,1
        case ')':  return is_left ? 0 : 1;
        case '[':  return is_left ? 2 : 3;   // small brackets at 2,3
        case ']':  return is_left ? 2 : 3;
        case '{':  return is_left ? 8 : 9;   // small braces at 8,9
        case '}':  return is_left ? 8 : 9;
        case '|':  return 12;                // vertical bar at 12
        case 0x2016: return 13;              // double vertical at 13 (‖)
        default:   return is_left ? 0 : 1;   // fallback to parens
    }
}

// Find best-sized delimiter from cmex10 next-larger chain
// Returns the character code and its metrics
static TexNode* make_sized_delimiter(Arena* arena, int32_t delim, bool is_left,
                                      float target_height, MathContext& ctx) {
    if (delim == 0) return nullptr;

    TFMFont* cmex = ctx.fonts->get_font("cmex10");
    if (!cmex) {
        // Fallback to simple char if no TFM available
        FontSpec font("cmr10", ctx.base_size_pt, nullptr, 0);
        TexNode* node = make_char(arena, delim, font);
        node->width = ctx.base_size_pt * 0.4f;
        node->height = ctx.x_height;
        node->depth = 0;
        return node;
    }

    float size = ctx.font_size();
    float scale = size / cmex->design_size;

    // Get starting code for this delimiter in cmex10
    int current = delim_to_cmex_start(delim, is_left);
    int best = current;
    float best_total = 0;

    // Walk the next-larger chain to find best fit
    // cmex10 has chains like: small → medium → large → extensible
    int max_iterations = 10;  // prevent infinite loops
    while (current != 0 && max_iterations-- > 0) {
        if (!cmex->has_char(current)) break;

        float h = cmex->char_height(current) * scale;
        float d = cmex->char_depth(current) * scale;
        float total = h + d;

        if (total >= target_height) {
            // Found one big enough
            best = current;
            best_total = total;
            break;
        }

        // Keep this as best so far
        if (total > best_total) {
            best = current;
            best_total = total;
        }

        // Try next larger
        int next = cmex->get_next_larger(current);
        if (next == 0 || next == current) break;
        current = next;
    }

    // Create character node with proper metrics
    FontSpec font("cmex10", size, nullptr, 0);
    TexNode* node = make_char(arena, best, font);

    float h = cmex->char_height(best) * scale;
    float d = cmex->char_depth(best) * scale;
    float w = cmex->char_width(best) * scale;

    node->width = w > 0 ? w : ctx.base_size_pt * 0.4f;
    node->height = h;
    node->depth = d;

    log_debug("[DELIM] sized delimiter: cp=%d target=%.2f actual=%.2f (h=%.2f d=%.2f)",
              best, target_height, h + d, h, d);

    return node;
}

TexNode* typeset_delimited(int32_t left_delim, TexNode* content,
                           int32_t right_delim, MathContext& ctx, bool extensible) {
    Arena* arena = ctx.arena;

    // Calculate delimiter height needed
    float target_height = content->height + content->depth;
    float delim_extra = 2.0f;  // Extra height above/below
    float delim_size = target_height + delim_extra;

    float total_width = content->width;
    float total_height = content->height;
    float total_depth = content->depth;

    TexNode* left = nullptr;
    TexNode* right = nullptr;

    // For non-extensible delimiters (matrix environments), use cmex10 next-larger chain
    // to find best-fit delimiter size without using extensible recipes
    // For extensible delimiters (\left/\right), use Delimiter nodes that trigger
    // the full extensible recipe in DVI output
    if (!extensible) {
        // Non-extensible: use cmex10 next-larger chain for properly sized delimiters
        left = make_sized_delimiter(arena, left_delim, true, delim_size, ctx);
        if (left) {
            total_width += left->width;
            total_height = fmaxf(total_height, left->height);
            total_depth = fmaxf(total_depth, left->depth);
        }

        right = make_sized_delimiter(arena, right_delim, false, delim_size, ctx);
        if (right) {
            total_width += right->width;
            total_height = fmaxf(total_height, right->height);
            total_depth = fmaxf(total_depth, right->depth);
        }
    } else {
        // Extensible: use Delimiter nodes (will be sized by DVI output)
        if (left_delim != 0) {
            left = make_delimiter(arena, left_delim, delim_size, true);
            left->width = ctx.base_size_pt * 0.4f;
            left->height = delim_size / 2.0f + ctx.axis_height;
            left->depth = delim_size / 2.0f - ctx.axis_height;
            total_width += left->width;
        }

        if (right_delim != 0) {
            right = make_delimiter(arena, right_delim, delim_size, false);
            right->width = ctx.base_size_pt * 0.4f;
            right->height = delim_size / 2.0f + ctx.axis_height;
            right->depth = delim_size / 2.0f - ctx.axis_height;
            total_width += right->width;
        }
    }

    // Create containing HBox
    TexNode* hbox = make_hbox(arena);
    hbox->width = total_width;
    hbox->height = fmaxf(total_height, left ? left->height : 0);
    hbox->height = fmaxf(hbox->height, right ? right->height : 0);
    hbox->depth = fmaxf(total_depth, left ? left->depth : 0);
    hbox->depth = fmaxf(hbox->depth, right ? right->depth : 0);

    // Link children
    float x = 0;
    TexNode* prev = nullptr;

    if (left) {
        left->x = x;
        left->parent = hbox;
        hbox->first_child = left;
        x += left->width;
        prev = left;
    }

    content->x = x;
    content->parent = hbox;
    if (prev) {
        prev->next_sibling = content;
        content->prev_sibling = prev;
    } else {
        hbox->first_child = content;
    }
    x += content->width;
    prev = content;

    if (right) {
        right->x = x;
        right->parent = hbox;
        prev->next_sibling = right;
        right->prev_sibling = prev;
        hbox->last_child = right;
    } else {
        hbox->last_child = content;
    }

    return hbox;
}

// ============================================================================
// Apply Math Spacing
// ============================================================================

void apply_math_spacing(TexNode* first, MathContext& ctx) {
    if (!first) return;

    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    for (TexNode* node = first; node; ) {
        TexNode* next = node->next_sibling;

        // Get atom type
        AtomType curr_type = AtomType::Ord;
        if (node->node_class == NodeClass::MathChar) {
            curr_type = node->content.math_char.atom_type;
        }

        // Insert spacing kern if needed
        if (!is_first && next) {
            float spacing_mu = get_atom_spacing_mu(prev_type, curr_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(ctx.arena, spacing_pt);

                // Insert kern before current node
                kern->prev_sibling = node->prev_sibling;
                kern->next_sibling = node;
                if (node->prev_sibling) {
                    node->prev_sibling->next_sibling = kern;
                }
                node->prev_sibling = kern;
            }
        }

        is_first = false;
        prev_type = curr_type;
        node = next;
    }
}

// ============================================================================
// Inline Math Extraction
// ============================================================================

InlineMathResult extract_inline_math(const char* text, size_t len, MathContext& ctx) {
    InlineMathResult result = {};
    result.found = false;

    // Find first $ that's not escaped
    const char* start = nullptr;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '$') {
            // Check if escaped
            if (i > 0 && text[i-1] == '\\') continue;
            // Check if display math ($$)
            if (i + 1 < len && text[i+1] == '$') continue;

            if (!start) {
                start = &text[i];
            } else {
                // Found closing $
                size_t math_start = start - text + 1;
                size_t math_end = i;
                size_t math_len = math_end - math_start;

                // Typeset the math content
                result.math = typeset_latex_math(text + math_start, math_len, ctx);

                // Build text before math
                if (math_start > 1) {
                    // Would need HList context to build text properly
                    // For now, just mark the offset
                }

                result.found = true;
                return result;
            }
        }
    }

    return result;
}

// ============================================================================
// Math Region Finding
// ============================================================================

MathRegionList find_math_regions(const char* text, size_t len, Arena* arena) {
    MathRegionList list = {};
    list.capacity = 16;
    list.regions = (MathRegion*)arena_alloc(arena, list.capacity * sizeof(MathRegion));
    list.count = 0;

    const char* p = text;
    const char* end = text + len;

    while (p < end) {
        // Look for $ or \[
        if (*p == '$') {
            if (p + 1 < end && *(p + 1) == '$') {
                // Display math $$...$$
                const char* content_start = p + 2;
                const char* content_end = content_start;
                while (content_end + 1 < end) {
                    if (*content_end == '$' && *(content_end + 1) == '$') {
                        break;
                    }
                    content_end++;
                }

                if (content_end + 1 < end) {
                    // Found closing $$
                    if (list.count >= list.capacity) {
                        // Grow array
                        int new_cap = list.capacity * 2;
                        MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                            new_cap * sizeof(MathRegion));
                        memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                        list.regions = new_regions;
                        list.capacity = new_cap;
                    }

                    MathRegion& r = list.regions[list.count++];
                    r.start = p - text;
                    r.end = content_end + 2 - text;
                    r.is_display = true;
                    r.content = content_start;
                    r.content_len = content_end - content_start;

                    p = content_end + 2;
                    continue;
                }
            } else {
                // Inline math $...$
                const char* content_start = p + 1;
                const char* content_end = content_start;
                while (content_end < end) {
                    if (*content_end == '$' && (content_end == text || *(content_end - 1) != '\\')) {
                        break;
                    }
                    content_end++;
                }

                if (content_end < end) {
                    if (list.count >= list.capacity) {
                        int new_cap = list.capacity * 2;
                        MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                            new_cap * sizeof(MathRegion));
                        memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                        list.regions = new_regions;
                        list.capacity = new_cap;
                    }

                    MathRegion& r = list.regions[list.count++];
                    r.start = p - text;
                    r.end = content_end + 1 - text;
                    r.is_display = false;
                    r.content = content_start;
                    r.content_len = content_end - content_start;

                    p = content_end + 1;
                    continue;
                }
            }
        } else if (*p == '\\' && p + 1 < end && *(p + 1) == '[') {
            // Display math \[...\]
            const char* content_start = p + 2;
            const char* content_end = content_start;
            while (content_end + 1 < end) {
                if (*content_end == '\\' && *(content_end + 1) == ']') {
                    break;
                }
                content_end++;
            }

            if (content_end + 1 < end) {
                if (list.count >= list.capacity) {
                    int new_cap = list.capacity * 2;
                    MathRegion* new_regions = (MathRegion*)arena_alloc(arena,
                        new_cap * sizeof(MathRegion));
                    memcpy(new_regions, list.regions, list.count * sizeof(MathRegion));
                    list.regions = new_regions;
                    list.capacity = new_cap;
                }

                MathRegion& r = list.regions[list.count++];
                r.start = p - text;
                r.end = content_end + 2 - text;
                r.is_display = true;
                r.content = content_start;
                r.content_len = content_end - content_start;

                p = content_end + 2;
                continue;
            }
        }

        p++;
    }

    log_debug("math_bridge: found %d math regions in text", list.count);
    return list;
}

// ============================================================================
// Process Text with Math
// ============================================================================

TexNode* process_text_with_math(const char* text, size_t len, MathContext& ctx,
                                 TFMFontManager* fonts) {
    Arena* arena = ctx.arena;

    // Find all math regions
    MathRegionList regions = find_math_regions(text, len, arena);

    if (regions.count == 0) {
        // No math - just convert text to HList
        HListContext hctx(arena, fonts);
        set_font(hctx, "cmr10", ctx.base_size_pt);
        return text_to_hlist(text, len, hctx);
    }

    // Build HList with interleaved text and math
    TexNode* hlist = make_hlist(arena);
    TexNode* last_node = nullptr;
    size_t text_pos = 0;

    HListContext hctx(arena, fonts);
    set_font(hctx, "cmr10", ctx.base_size_pt);

    for (int i = 0; i < regions.count; i++) {
        MathRegion& r = regions.regions[i];

        // Handle display math separately (should not be inline)
        if (r.is_display) {
            log_debug("math_bridge: skipping display math in inline processing");
            continue;
        }

        // Add text before this math region
        if (r.start > text_pos) {
            TexNode* text_nodes = text_to_hlist(text + text_pos, r.start - text_pos, hctx);
            if (text_nodes) {
                // Append to hlist
                if (!hlist->first_child) {
                    hlist->first_child = text_nodes->first_child;
                }
                if (last_node && text_nodes->first_child) {
                    last_node->next_sibling = text_nodes->first_child;
                    text_nodes->first_child->prev_sibling = last_node;
                }
                // Find last node of text
                for (TexNode* n = text_nodes->first_child; n; n = n->next_sibling) {
                    n->parent = hlist;
                    last_node = n;
                }
                hlist->last_child = last_node;
            }
        }

        // Typeset the inline math
        TexNode* math = typeset_latex_math(r.content, r.content_len, ctx);
        if (math) {
            if (!hlist->first_child) {
                hlist->first_child = math;
            }
            if (last_node) {
                last_node->next_sibling = math;
                math->prev_sibling = last_node;
            }
            math->parent = hlist;
            last_node = math;
            hlist->last_child = math;
        }

        text_pos = r.end;
    }

    // Add remaining text after last math region
    if (text_pos < len) {
        TexNode* text_nodes = text_to_hlist(text + text_pos, len - text_pos, hctx);
        if (text_nodes) {
            if (!hlist->first_child) {
                hlist->first_child = text_nodes->first_child;
            }
            if (last_node && text_nodes->first_child) {
                last_node->next_sibling = text_nodes->first_child;
                text_nodes->first_child->prev_sibling = last_node;
            }
            for (TexNode* n = text_nodes->first_child; n; n = n->next_sibling) {
                n->parent = hlist;
                last_node = n;
            }
            hlist->last_child = last_node;
        }
    }

    // Measure total dimensions
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    for (TexNode* n = hlist->first_child; n; n = n->next_sibling) {
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
    }
    hlist->width = total_width;
    hlist->height = max_height;
    hlist->depth = max_depth;

    return hlist;
}

// ============================================================================
// Display Math Typesetting
// ============================================================================

TexNode* typeset_display_math(const char* math_str, MathContext& ctx,
                               const DisplayMathParams& params) {
    // Use display style
    MathContext display_ctx = ctx;
    display_ctx.style = MathStyle::Display;

    // Typeset the math content
    TexNode* content = typeset_latex_math(math_str, strlen(math_str), display_ctx);

    return typeset_display_math_node(content, ctx, params);
}

TexNode* typeset_display_math_node(TexNode* content, MathContext& ctx,
                                    const DisplayMathParams& params) {
    Arena* arena = ctx.arena;

    // Create centered line
    TexNode* centered = center_math(content, params.line_width, arena);

    // Create VList with spacing
    TexNode* vlist = make_vlist(arena);

    // Add above skip
    TexNode* above_glue = make_glue(arena, Glue::flexible(params.above_skip, 3.0f, 3.0f));
    vlist->append_child(above_glue);

    // Add math line
    vlist->append_child(centered);

    // Add below skip
    TexNode* below_glue = make_glue(arena, Glue::flexible(params.below_skip, 3.0f, 3.0f));
    vlist->append_child(below_glue);

    // Calculate dimensions
    vlist->height = params.above_skip + centered->height;
    vlist->depth = centered->depth + params.below_skip;
    vlist->width = params.line_width;

    log_debug("math_bridge: display math %.2fpt x %.2fpt",
              vlist->width, vlist->height + vlist->depth);

    return vlist;
}

// ============================================================================
// Utility Functions
// ============================================================================

TexNode* make_math_hbox(TexNode* first_atom, MathContext& ctx) {
    // Apply spacing
    apply_math_spacing(first_atom, ctx);

    // Wrap in HBox
    TexNode* hbox = make_hbox(ctx.arena);
    hbox->first_child = first_atom;

    // Measure and link
    float total_width = 0;
    float max_height = 0;
    float max_depth = 0;
    TexNode* last = nullptr;

    for (TexNode* n = first_atom; n; n = n->next_sibling) {
        n->parent = hbox;
        total_width += n->width;
        if (n->height > max_height) max_height = n->height;
        if (n->depth > max_depth) max_depth = n->depth;
        last = n;
    }

    hbox->last_child = last;
    hbox->width = total_width;
    hbox->height = max_height;
    hbox->depth = max_depth;

    return hbox;
}

float measure_math_width(TexNode* node) {
    if (!node) return 0;

    float width = 0;
    if (node->first_child) {
        for (TexNode* n = node->first_child; n; n = n->next_sibling) {
            width += n->width;
        }
    } else {
        width = node->width;
    }
    return width;
}

TexNode* center_math(TexNode* content, float target_width, Arena* arena) {
    float content_width = content->width;
    float margin = (target_width - content_width) / 2.0f;

    if (margin <= 0) {
        // Content wider than target - just return as-is
        return content;
    }

    // Create HBox with centering glue
    TexNode* hbox = make_hbox(arena);
    hbox->content.box.set_width = target_width;

    // Add left fill
    TexNode* left_glue = make_glue(arena, hfil_glue());
    hbox->append_child(left_glue);

    // Add content
    hbox->append_child(content);

    // Add right fill
    TexNode* right_glue = make_glue(arena, hfil_glue());
    hbox->append_child(right_glue);

    hbox->width = target_width;
    hbox->height = content->height;
    hbox->depth = content->depth;

    return hbox;
}

// ============================================================================
// Lambda Item Math Conversion
// ============================================================================

#ifdef TEX_WITH_LAMBDA

// Helper to get string field from a map Item
static const char* get_map_string(Item item, const char* key) {
    if (item.item == ItemNull.item) return nullptr;
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_MAP) return nullptr;

    Map* map = item.map;
    ConstItem val = map->get(key);
    if (val.item == ItemNull.item) return nullptr;

    TypeId vtype = val.type_id();
    if (vtype == LMD_TYPE_STRING) {
        String* str = ((Item*)&val)->get_string();
        return str ? str->chars : nullptr;
    }
    if (vtype == LMD_TYPE_SYMBOL) {
        String* str = ((Item*)&val)->get_symbol();
        return str ? str->chars : nullptr;
    }
    return nullptr;
}

// Helper to get Item field from a map
static Item get_map_item(Item item, const char* key) {
    if (item.item == ItemNull.item) return ItemNull;
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_MAP) return ItemNull;

    Map* map = item.map;
    ConstItem val = map->get(key);
    return *(Item*)&val;
}

TexNode* convert_lambda_math(Item math_node, MathContext& ctx) {
    using namespace lambda;

    if (math_node.item == ItemNull.item) {
        return make_hbox(ctx.arena);
    }

    // Get node type using Lambda's math_node.hpp
    MathNodeType node_type = get_math_node_type(math_node);

    switch (node_type) {
        case MathNodeType::Symbol: {
            // Single character symbol
            const char* value = get_map_string(math_node, "value");
            if (value && *value) {
                return typeset_latex_math(value, strlen(value), ctx);
            }
            return make_hbox(ctx.arena);
        }

        case MathNodeType::Number: {
            const char* value = get_map_string(math_node, "value");
            if (value) {
                return typeset_latex_math(value, strlen(value), ctx);
            }
            return make_hbox(ctx.arena);
        }

        case MathNodeType::Row: {
            // Horizontal sequence
            Item items = get_map_item(math_node, "items");
            if (items.item == ItemNull.item || get_type_id(items) != LMD_TYPE_LIST) {
                return make_hbox(ctx.arena);
            }

            List* list = items.list;
            TexNode* hbox = make_hbox(ctx.arena);
            TexNode* last = nullptr;

            for (int i = 0; i < list->length; i++) {
                Item child = list_get(list, i);
                TexNode* child_node = convert_lambda_math(child, ctx);
                if (child_node) {
                    child_node->parent = hbox;
                    if (!hbox->first_child) {
                        hbox->first_child = child_node;
                    }
                    if (last) {
                        last->next_sibling = child_node;
                        child_node->prev_sibling = last;
                    }
                    last = child_node;
                }
            }
            hbox->last_child = last;

            // Measure
            float w = 0, h = 0, d = 0;
            for (TexNode* n = hbox->first_child; n; n = n->next_sibling) {
                w += n->width;
                if (n->height > h) h = n->height;
                if (n->depth > d) d = n->depth;
            }
            hbox->width = w;
            hbox->height = h;
            hbox->depth = d;

            return hbox;
        }

        case MathNodeType::Fraction: {
            Item num = get_map_item(math_node, "numerator");
            Item denom = get_map_item(math_node, "denominator");

            MathContext script_ctx = ctx;
            script_ctx.style = sup_style(ctx.style);

            TexNode* num_node = convert_lambda_math(num, script_ctx);
            TexNode* denom_node = convert_lambda_math(denom, script_ctx);

            return typeset_fraction(num_node, denom_node, ctx.rule_thickness, ctx);
        }

        case MathNodeType::Radical: {
            Item content = get_map_item(math_node, "content");
            Item degree = get_map_item(math_node, "degree");

            TexNode* radicand = convert_lambda_math(content, ctx);

            if (degree.item != ItemNull.item) {
                MathContext ss_ctx = ctx;
                ss_ctx.style = sub_style(sub_style(ctx.style));
                TexNode* degree_node = convert_lambda_math(degree, ss_ctx);
                return typeset_root(degree_node, radicand, ctx);
            }

            return typeset_sqrt(radicand, ctx);
        }

        case MathNodeType::Subsup: {
            Item base = get_map_item(math_node, "base");
            Item sub = get_map_item(math_node, "subscript");
            Item sup = get_map_item(math_node, "superscript");

            TexNode* nucleus = convert_lambda_math(base, ctx);

            MathContext script_ctx = ctx;
            script_ctx.style = sup_style(ctx.style);

            TexNode* sub_node = (sub.item != ItemNull.item)
                ? convert_lambda_math(sub, script_ctx) : nullptr;
            TexNode* sup_node = (sup.item != ItemNull.item)
                ? convert_lambda_math(sup, script_ctx) : nullptr;

            return typeset_scripts(nucleus, sub_node, sup_node, ctx);
        }

        default:
            log_debug("math_bridge: unhandled math node type %d", (int)node_type);
            return make_hbox(ctx.arena);
    }
}

#endif // TEX_WITH_LAMBDA

} // namespace tex
