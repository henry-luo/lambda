// tex_math_layout.cpp - TeX Math Layout Implementation
//
// Implementation of TeXBook Appendix G math typesetting algorithms

#include "tex_math_layout.hpp"
#include "../../lib/log.h"
#include <cmath>
#include <algorithm>

namespace tex {

// ============================================================================
// MathLayoutContext Implementation
// ============================================================================

static MathSymbolParams s_default_symbol_params;
static MathExtensionParams s_default_extension_params;

const MathSymbolParams& MathLayoutContext::symbol_params() const {
    // TODO: Get from font provider once implemented
    static bool initialized = false;
    if (!initialized) {
        s_default_symbol_params = default_math_symbol_params(base_size_pt);
        initialized = true;
    }
    return s_default_symbol_params;
}

const MathExtensionParams& MathLayoutContext::extension_params() const {
    static bool initialized = false;
    if (!initialized) {
        s_default_extension_params = default_math_extension_params(base_size_pt);
        initialized = true;
    }
    return s_default_extension_params;
}

// ============================================================================
// Inter-Atom Spacing
// ============================================================================

Glue compute_inter_atom_glue(
    AtomType left,
    AtomType right,
    MathLayoutContext& ctx
) {
    bool tight = is_script(ctx.style);
    int spacing_code = get_inter_atom_spacing(left, right, tight);

    if (spacing_code == 0) {
        return Glue{};
    }

    float mu = spacing_code_to_mu(spacing_code);
    float quad = ctx.symbol_params().quad;
    float size = mu_to_pt(mu, quad);

    // Math spacing has some stretchability
    return Glue{
        size,
        size * 0.5f, GlueOrder::Normal,  // 50% stretch
        size * 0.33f, GlueOrder::Normal  // 33% shrink
    };
}

// ============================================================================
// Script Attachment (TeXBook Rules 18-18f)
// ============================================================================

void position_subscript_only(
    TexBox* nucleus,
    TexBox* subscript,
    MathLayoutContext& ctx
) {
    const MathSymbolParams& sigma = ctx.symbol_params();

    // Rule 18b: subscript shift down
    // shift = max(sub1, nucleus.depth + 1/4 x_height)
    float shift = sigma.sub1;
    shift = std::max(shift, nucleus->depth + sigma.x_height * 0.25f);

    // Also ensure subscript top is at least 4/5 x_height below baseline
    float min_clearance = sigma.x_height * 0.8f;
    shift = std::max(shift, subscript->height - min_clearance);

    subscript->x = nucleus->width;
    subscript->y = shift;  // Positive y = downward shift
}

void position_superscript_only(
    TexBox* nucleus,
    TexBox* superscript,
    bool cramped,
    MathLayoutContext& ctx
) {
    const MathSymbolParams& sigma = ctx.symbol_params();

    // Rule 18c: superscript shift up
    // p = sup1 (display), sup2 (cramped), or sup3 (other)
    float p;
    if (is_display(ctx.style) && !cramped) {
        p = sigma.sup1;
    } else if (cramped) {
        p = sigma.sup2;
    } else {
        p = sigma.sup3;
    }

    // shift = max(p, sup_drop + nucleus.height, (1/4 x_height) + sup.depth)
    float shift = p;
    shift = std::max(shift, sigma.sup_drop + nucleus->height * 0.0f);  // simplified
    shift = std::max(shift, sigma.x_height * 0.25f + superscript->depth);

    superscript->x = nucleus->width;
    superscript->y = -shift;  // Negative y = upward shift
}

void position_both_scripts(
    TexBox* nucleus,
    TexBox* superscript,
    TexBox* subscript,
    MathLayoutContext& ctx
) {
    const MathSymbolParams& sigma = ctx.symbol_params();

    // First position each script independently
    position_superscript_only(nucleus, superscript, is_cramped(ctx.style), ctx);
    position_subscript_only(nucleus, subscript, ctx);

    // Rule 18e: check gap between scripts
    // gap = sup_shift - sup.depth - (sub.height - sub_shift)
    float sup_bottom = -superscript->y + superscript->depth;
    float sub_top = subscript->y - subscript->height;
    float gap = sub_top - sup_bottom;

    float min_gap = 4.0f * ctx.extension_params().default_rule_thickness;

    if (gap < min_gap) {
        // Need to move scripts apart
        float adjustment = (min_gap - gap) * 0.5f;
        superscript->y -= adjustment;  // Move sup up
        subscript->y += adjustment;    // Move sub down

        // Rule 18f: also check sup doesn't go too high
        float sup_max = sigma.x_height * 0.8f;  // 4/5 x_height
        if (-superscript->y - superscript->depth > sup_max) {
            // Move both down
            float down = -superscript->y - superscript->depth - sup_max;
            subscript->y += down;
        }
    }
}

TexBox* attach_scripts(
    ScriptAttachment& scripts,
    AtomType atom_type,
    MathLayoutContext& ctx
) {
    if (!scripts.superscript && !scripts.subscript) {
        // No scripts, just return nucleus
        return scripts.nucleus;
    }

    // Create container for nucleus + scripts
    TexBox* result = make_hlist_box(ctx.arena);
    add_child(result, scripts.nucleus, ctx.arena);

    if (scripts.superscript && scripts.subscript) {
        // Both scripts
        position_both_scripts(
            scripts.nucleus,
            scripts.superscript,
            scripts.subscript,
            ctx
        );

        // Create vlist for stacked scripts
        TexBox* script_stack = make_vlist_box(ctx.arena);
        add_child(script_stack, scripts.superscript, ctx.arena);
        add_child(script_stack, scripts.subscript, ctx.arena);

        // Position the stack relative to nucleus
        script_stack->x = scripts.nucleus->width;
        add_child(result, script_stack, ctx.arena);

    } else if (scripts.superscript) {
        position_superscript_only(
            scripts.nucleus,
            scripts.superscript,
            is_cramped(ctx.style),
            ctx
        );
        add_child(result, scripts.superscript, ctx.arena);

    } else {
        position_subscript_only(
            scripts.nucleus,
            scripts.subscript,
            ctx
        );
        add_child(result, scripts.subscript, ctx.arena);
    }

    compute_hlist_natural_dims(result);
    return result;
}

// ============================================================================
// Fraction Layout (TeXBook Rules 15a-15e)
// ============================================================================

TexBox* layout_fraction(
    FractionParams& params,
    MathLayoutContext& ctx
) {
    const MathSymbolParams& sigma = ctx.symbol_params();
    const MathExtensionParams& xi = ctx.extension_params();

    // Rebox numerator and denominator to same width
    float max_width = std::max(params.numerator->width, params.denominator->width);
    TexBox* num = rebox(params.numerator, max_width, ctx);
    TexBox* denom = rebox(params.denominator, max_width, ctx);

    // Rule 15a-b: determine shifts based on display style
    float num_shift, denom_shift;
    float axis = sigma.axis_height;
    float thickness = params.rule_thickness;

    if (thickness < 0) {
        thickness = xi.default_rule_thickness;
    }

    if (is_display(ctx.style)) {
        num_shift = sigma.num1;
        denom_shift = sigma.denom1;
    } else {
        if (thickness > 0) {
            num_shift = sigma.num2;
        } else {
            num_shift = sigma.num3;  // for \atop
        }
        denom_shift = sigma.denom2;
    }

    // Rule 15c-d: adjust to ensure proper clearance
    if (thickness > 0) {
        // With fraction bar
        float num_clearance = num_shift - num->depth - (axis + thickness * 0.5f);
        float min_clearance = is_display(ctx.style) ?
            3.0f * thickness : thickness;

        if (num_clearance < min_clearance) {
            num_shift += min_clearance - num_clearance;
        }

        float denom_clearance = (axis - thickness * 0.5f) - (denom_shift - denom->height);
        if (denom_clearance < min_clearance) {
            denom_shift += min_clearance - denom_clearance;
        }
    } else {
        // Without fraction bar (\atop)
        float gap = (num_shift - num->depth) - (denom_shift - denom->height);
        float min_gap = is_display(ctx.style) ?
            7.0f * xi.default_rule_thickness :
            3.0f * xi.default_rule_thickness;

        if (gap < min_gap) {
            float adjust = (min_gap - gap) * 0.5f;
            num_shift += adjust;
            denom_shift += adjust;
        }
    }

    // Build the vlist
    TexBox* result = make_vlist_box(ctx.arena);

    // Numerator
    num->y = -num_shift;  // Above baseline
    add_child(result, num, ctx.arena);

    // Fraction bar (if thickness > 0)
    if (thickness > 0) {
        TexBox* rule = make_rule_box(max_width, thickness * 0.5f, thickness * 0.5f, ctx.arena);
        rule->y = -axis;  // Centered on axis
        add_child(result, rule, ctx.arena);
    }

    // Denominator
    denom->y = denom_shift;  // Below baseline
    add_child(result, denom, ctx.arena);

    // Compute dimensions
    result->width = max_width;
    result->height = num_shift + num->height;
    result->depth = denom_shift + denom->depth;

    // Center on axis
    return center_on_axis(result, ctx);
}

TexBox* layout_genfrac(
    TexBox* numerator,
    TexBox* denominator,
    float thickness,
    MathStyle override_style,
    uint32_t left_delim,
    uint32_t right_delim,
    MathLayoutContext& ctx
) {
    // Use overridden style if specified
    MathLayoutContext frac_ctx = ctx;
    if ((int)override_style >= 0) {
        frac_ctx.style = override_style;
    }

    // Layout the basic fraction
    FractionParams params = {};
    params.numerator = numerator;
    params.denominator = denominator;
    params.rule_thickness = thickness;

    TexBox* frac = layout_fraction(params, frac_ctx);

    // Add delimiters if specified
    if (left_delim || right_delim) {
        return layout_delimited(left_delim, frac, right_delim, ctx);
    }

    return frac;
}

// ============================================================================
// Delimiter Sizing (TeXBook Rule 19)
// ============================================================================

SizedDelimiter size_delimiter(
    uint32_t codepoint,
    float target_height,
    float target_depth,
    MathLayoutContext& ctx
) {
    SizedDelimiter result = {};
    result.codepoint = codepoint;
    result.is_extended = false;

    if (codepoint == 0) {
        // Null delimiter
        result.height = 0;
        result.depth = 0;
        return result;
    }

    float target_total = target_height + target_depth;

    // Rule 19: delimiter should be at least (target_total * delim_factor + delim_shortfall)
    // where delim_factor = 901/1000 and delim_shortfall = 5pt typically
    const MathSymbolParams& sigma = ctx.symbol_params();
    float min_size = is_display(ctx.style) ? sigma.delim1 : sigma.delim2;

    target_total = std::max(target_total, min_size);

    // TODO: Look up actual glyph variants from font
    // For now, use target directly
    result.height = target_total * 0.5f;
    result.depth = target_total * 0.5f;

    // If target is very large, may need to build extensible delimiter
    float max_prebuilt_size = 3.0f * ctx.base_size_pt;  // Approximate

    if (target_total > max_prebuilt_size) {
        result.is_extended = true;
        // TODO: Set up extension pieces from font
    }

    return result;
}

TexBox* make_delimiter_box(
    const SizedDelimiter& delim,
    MathLayoutContext& ctx
) {
    if (delim.codepoint == 0) {
        // Null delimiter - return empty box
        return make_empty_box(0, 0, 0, ctx.arena);
    }

    if (!delim.is_extended) {
        // Single glyph delimiter
        TexBox* box = make_glyph_box(delim.codepoint, ctx.arena);
        box->height = delim.height;
        box->depth = delim.depth;
        return box;
    }

    // Extended delimiter - build from pieces
    TexBox* vlist = make_vlist_box(ctx.arena);

    // Top piece
    if (delim.pieces.top) {
        TexBox* top = make_glyph_box(delim.pieces.top, ctx.arena);
        top->height = delim.pieces.top_height;
        add_child(vlist, top, ctx.arena);
    }

    // Repeating pieces
    for (int i = 0; i < delim.pieces.repeat_count; ++i) {
        TexBox* rep = make_glyph_box(delim.pieces.repeat, ctx.arena);
        rep->height = delim.pieces.repeat_height;
        add_child(vlist, rep, ctx.arena);
    }

    // Middle piece (for braces, etc.)
    if (delim.pieces.middle) {
        TexBox* mid = make_glyph_box(delim.pieces.middle, ctx.arena);
        mid->height = delim.pieces.middle_height;
        add_child(vlist, mid, ctx.arena);

        // More repeating pieces after middle
        for (int i = 0; i < delim.pieces.repeat_count; ++i) {
            TexBox* rep = make_glyph_box(delim.pieces.repeat, ctx.arena);
            rep->height = delim.pieces.repeat_height;
            add_child(vlist, rep, ctx.arena);
        }
    }

    // Bottom piece
    if (delim.pieces.bottom) {
        TexBox* bot = make_glyph_box(delim.pieces.bottom, ctx.arena);
        bot->height = delim.pieces.bottom_height;
        add_child(vlist, bot, ctx.arena);
    }

    compute_vlist_natural_dims(vlist);
    return vlist;
}

TexBox* layout_delimited(
    uint32_t left_delim,
    TexBox* content,
    uint32_t right_delim,
    MathLayoutContext& ctx
) {
    // Size delimiters to match content
    SizedDelimiter left = size_delimiter(
        left_delim, content->height, content->depth, ctx);
    SizedDelimiter right = size_delimiter(
        right_delim, content->height, content->depth, ctx);

    // Build horizontal list
    TexBox* result = make_hlist_box(ctx.arena);

    TexBox* left_box = make_delimiter_box(left, ctx);
    left_box->atom_type = AtomType::Open;
    add_child(result, left_box, ctx.arena);

    content->atom_type = AtomType::Inner;
    add_child(result, content, ctx.arena);

    TexBox* right_box = make_delimiter_box(right, ctx);
    right_box->atom_type = AtomType::Close;
    add_child(result, right_box, ctx.arena);

    compute_hlist_natural_dims(result);
    return result;
}

// ============================================================================
// Radical Layout (TeXBook Rules 11-11f)
// ============================================================================

TexBox* layout_radical(
    TexBox* radicand,
    TexBox* degree,
    MathLayoutContext& ctx
) {
    const MathSymbolParams& sigma = ctx.symbol_params();
    const MathExtensionParams& xi = ctx.extension_params();

    // Rule 11: clearance above radicand
    float clearance;
    if (is_display(ctx.style)) {
        clearance = xi.default_rule_thickness + std::abs(sigma.x_height) * 0.25f;
    } else {
        clearance = xi.default_rule_thickness * 1.25f;
    }

    // Total height needed for radical sign
    float radical_height = radicand->height + clearance + xi.default_rule_thickness;
    float radical_depth = radicand->depth;

    // Get sized radical sign (sqrt symbol)
    SizedDelimiter rad = size_delimiter(0x221A, radical_height, radical_depth, ctx);  // √
    TexBox* radical_sign = make_delimiter_box(rad, ctx);

    // Create the vinculum (overline)
    TexBox* rule = make_rule_box(
        radicand->width,
        xi.default_rule_thickness,
        0,
        ctx.arena
    );

    // Build result
    TexBox* result = make_hlist_box(ctx.arena);

    // Add degree if present (for nth root)
    if (degree) {
        // Position degree - scaled down and raised
        float degree_raise = radical_sign->height * 0.6f;
        degree->y = -degree_raise;
        degree->scale = 0.6f;
        add_child(result, degree, ctx.arena);

        // Negative kern to overlap with radical
        TexBox* kern = make_kern_box(-degree->width * 0.5f, ctx.arena);
        add_child(result, kern, ctx.arena);
    }

    // Radical sign
    add_child(result, radical_sign, ctx.arena);

    // Radicand with overline
    TexBox* content = make_vlist_box(ctx.arena);
    rule->y = -radicand->height - clearance;
    add_child(content, rule, ctx.arena);
    radicand->y = 0;
    add_child(content, radicand, ctx.arena);

    add_child(result, content, ctx.arena);

    compute_hlist_natural_dims(result);
    return result;
}

// ============================================================================
// Large Operator Layout (TeXBook Rules 13-13a)
// ============================================================================

TexBox* layout_large_op(
    uint32_t op_codepoint,
    TexBox* above_limit,
    TexBox* below_limit,
    bool display_limits,
    MathLayoutContext& ctx
) {
    const MathExtensionParams& xi = ctx.extension_params();

    // Get operator symbol - larger in display style
    float op_scale = is_display(ctx.style) ? 1.5f : 1.0f;
    TexBox* op_box = make_glyph_box(op_codepoint, ctx.arena);
    op_box->scale = op_scale;
    op_box->atom_type = AtomType::Op;

    if (!display_limits || (!above_limit && !below_limit)) {
        // Limits as scripts (inline style)
        if (above_limit || below_limit) {
            ScriptAttachment scripts = {};
            scripts.nucleus = op_box;
            scripts.superscript = above_limit;
            scripts.subscript = below_limit;
            return attach_scripts(scripts, AtomType::Op, ctx);
        }
        return op_box;
    }

    // Display limits - stack above/below
    TexBox* result = make_vlist_box(ctx.arena);

    float op_width = op_box->width * op_box->scale;

    if (above_limit) {
        // Center above
        TexBox* above_centered = rebox(above_limit, op_width, ctx);
        above_centered->y = -op_box->height - xi.big_op_spacing1 - above_limit->depth;
        add_child(result, above_centered, ctx.arena);
    }

    add_child(result, op_box, ctx.arena);

    if (below_limit) {
        // Center below
        TexBox* below_centered = rebox(below_limit, op_width, ctx);
        below_centered->y = op_box->depth + xi.big_op_spacing2 + below_limit->height;
        add_child(result, below_centered, ctx.arena);
    }

    compute_vlist_natural_dims(result);
    return center_on_axis(result, ctx);
}

// ============================================================================
// Accent Layout (TeXBook Rules 12-12a)
// ============================================================================

TexBox* layout_accent(
    uint32_t accent_codepoint,
    TexBox* base,
    MathLayoutContext& ctx
) {
    // Get accent glyph
    TexBox* accent = make_glyph_box(accent_codepoint, ctx.arena);

    // Rule 12: skew for positioning accent
    // TODO: Get skew from font metrics
    float skew = 0;

    // Position accent centered above base
    float accent_x = (base->width - accent->width) * 0.5f + skew;
    float accent_y = base->height;  // Above base

    // Build result
    TexBox* result = make_vlist_box(ctx.arena);

    accent->x = accent_x;
    accent->y = -accent_y - accent->depth;
    add_child(result, accent, ctx.arena);
    add_child(result, base, ctx.arena);

    result->width = base->width;
    result->height = accent_y + accent->height;
    result->depth = base->depth;

    return result;
}

TexBox* layout_under_accent(
    uint32_t accent_codepoint,
    TexBox* base,
    MathLayoutContext& ctx
) {
    TexBox* accent = make_glyph_box(accent_codepoint, ctx.arena);

    // Position accent centered below base
    float accent_x = (base->width - accent->width) * 0.5f;

    TexBox* result = make_vlist_box(ctx.arena);

    add_child(result, base, ctx.arena);

    accent->x = accent_x;
    accent->y = base->depth + accent->height;
    add_child(result, accent, ctx.arena);

    result->width = base->width;
    result->height = base->height;
    result->depth = base->depth + accent->total_height();

    return result;
}

// ============================================================================
// Box Utilities
// ============================================================================

TexBox* center_on_axis(TexBox* box, MathLayoutContext& ctx) {
    float axis = ctx.symbol_params().axis_height;
    float center = (box->height - box->depth) * 0.5f;
    float shift = axis - center;

    if (std::abs(shift) < 0.01f) {
        return box;  // Already centered
    }

    // Wrap in a shifted box
    TexBox* result = make_hlist_box(ctx.arena);
    box->y = -shift;
    add_child(result, box, ctx.arena);

    result->width = box->width;
    result->height = box->height + shift;
    result->depth = box->depth - shift;

    return result;
}

TexBox* rebox(TexBox* box, float new_width, MathLayoutContext& ctx) {
    if (std::abs(box->width - new_width) < 0.01f) {
        return box;  // Already correct width
    }

    // Create hlist with glue to center
    TexBox* result = make_hlist_box(ctx.arena);

    float padding = (new_width - box->width) * 0.5f;

    if (padding > 0) {
        // Add leading glue
        TexBox* left_glue = make_glue_box(Glue::hss(), ctx.arena);
        add_child(result, left_glue, ctx.arena);
    }

    add_child(result, box, ctx.arena);

    if (padding > 0) {
        // Add trailing glue
        TexBox* right_glue = make_glue_box(Glue::hss(), ctx.arena);
        add_child(result, right_glue, ctx.arena);
    }

    set_hlist_width(result, new_width, ctx.arena);

    return result;
}

TexBox* hstrut(float width, MathLayoutContext& ctx) {
    return make_empty_box(width, 0, 0, ctx.arena);
}

TexBox* vstrut(float height, float depth, MathLayoutContext& ctx) {
    return make_empty_box(0, height, depth, ctx.arena);
}

// ============================================================================
// Math List Layout
// ============================================================================

TexBox* layout_math_list(
    MathAtom* atoms,
    int atom_count,
    MathLayoutContext& ctx
) {
    if (atom_count == 0) {
        return make_empty_box(0, 0, 0, ctx.arena);
    }

    TexBox* result = make_hlist_box(ctx.arena);
    AtomType prev_type = AtomType::Ord;

    for (int i = 0; i < atom_count; ++i) {
        MathAtom& atom = atoms[i];

        // Add inter-atom spacing
        if (i > 0) {
            Glue spacing = compute_inter_atom_glue(prev_type, atom.type, ctx);
            if (spacing.natural > 0) {
                TexBox* glue_box = make_glue_box(spacing, ctx.arena);
                add_child(result, glue_box, ctx.arena);
            }
        }

        // Layout the atom
        TexBox* atom_box = nullptr;

        switch (atom.type) {
            case AtomType::Ord:
                atom_box = layout_ord_atom(&atom, ctx);
                break;
            case AtomType::Op:
                atom_box = layout_op_atom(&atom, ctx);
                break;
            case AtomType::Bin:
                atom_box = layout_bin_atom(&atom, ctx);
                break;
            case AtomType::Rel:
                atom_box = layout_rel_atom(&atom, ctx);
                break;
            case AtomType::Open:
                atom_box = layout_open_atom(&atom, ctx);
                break;
            case AtomType::Close:
                atom_box = layout_close_atom(&atom, ctx);
                break;
            case AtomType::Punct:
                atom_box = layout_punct_atom(&atom, ctx);
                break;
            case AtomType::Inner:
                atom_box = layout_inner_atom(&atom, ctx);
                break;
            default:
                atom_box = atom.nucleus;
                break;
        }

        if (atom_box) {
            atom_box->atom_type = atom.type;
            add_child(result, atom_box, ctx.arena);
        }

        prev_type = atom.type;
    }

    compute_hlist_natural_dims(result);
    return result;
}

// ============================================================================
// Individual Atom Layout
// ============================================================================

static TexBox* attach_atom_scripts(MathAtom* atom, TexBox* nucleus, MathLayoutContext& ctx) {
    if (atom->superscript || atom->subscript) {
        // Layout scripts in appropriate style
        TexBox* sup = atom->superscript;
        TexBox* sub = atom->subscript;

        ScriptAttachment scripts = {};
        scripts.nucleus = nucleus;
        scripts.superscript = sup;
        scripts.subscript = sub;

        return attach_scripts(scripts, atom->type, ctx);
    }
    return nucleus;
}

TexBox* layout_ord_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_op_atom(MathAtom* atom, MathLayoutContext& ctx) {
    if (atom->limits && is_display(ctx.style)) {
        // Limits displayed above/below
        return layout_large_op(
            atom->nucleus->content.glyph.codepoint,
            atom->superscript,
            atom->subscript,
            true,
            ctx
        );
    }
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_bin_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_rel_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_open_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_close_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_punct_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

TexBox* layout_inner_atom(MathAtom* atom, MathLayoutContext& ctx) {
    return attach_atom_scripts(atom, atom->nucleus, ctx);
}

} // namespace tex
