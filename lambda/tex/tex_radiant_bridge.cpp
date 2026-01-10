// tex_radiant_bridge.cpp - Implementation of TeX-Radiant bridge
//
// Converts between tex::TexBox and radiant::MathBox representations.

#include "tex_radiant_bridge.hpp"
#include "tex_ast_builder.hpp"
#include "tex_math_layout.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <stdio.h>

namespace tex {

// ============================================================================
// TexBox to MathBox Conversion
// ============================================================================

radiant::MathBox* convert_tex_to_math_box(
    const TexBox* tex_box,
    ConversionContext* ctx
) {
    if (!tex_box || !ctx || !ctx->arena) {
        return nullptr;
    }

    radiant::MathBox* result = radiant::alloc_math_box(ctx->arena);

    // Copy basic dimensions
    result->width = tex_box->width;
    result->height = tex_box->height;
    result->depth = tex_box->depth;
    result->italic = tex_box->italic_correction;
    result->scale = ctx->scale;

    // Convert type
    result->type = tex_to_radiant_type(tex_box->type);

    // Handle different box kinds
    switch (tex_box->kind) {
        case BoxKind::Char: {
            // single character glyph
            result->content_type = radiant::MathBoxContentType::Glyph;
            result->content.glyph.codepoint = tex_box->content.ch.codepoint;

            // get FT_Face from font provider
            if (ctx->font_provider) {
                FT_Face face = ctx->font_provider->get_ft_face(
                    tex_box->content.ch.family,
                    false, // bold
                    false, // italic
                    ctx->base_size * ctx->scale
                );
                result->content.glyph.face = face;
            } else {
                result->content.glyph.face = nullptr;
            }
            break;
        }

        case BoxKind::HBox: {
            // horizontal box - convert to hbox with children
            if (tex_box->content.hbox.count == 0) {
                result->content_type = radiant::MathBoxContentType::Empty;
            } else {
                result->content_type = radiant::MathBoxContentType::HBox;
                int count = tex_box->content.hbox.count;

                // allocate children array
                radiant::MathBox** children = (radiant::MathBox**)arena_alloc(
                    ctx->arena, count * sizeof(radiant::MathBox*)
                );

                // convert each child
                int valid_count = 0;
                for (int i = 0; i < count; i++) {
                    TexBox* child = tex_box->content.hbox.children[i];
                    if (child) {
                        children[valid_count] = convert_tex_to_math_box(child, ctx);
                        if (children[valid_count]) {
                            children[valid_count]->parent = result;
                            valid_count++;
                        }
                    }
                }

                result->content.hbox.children = children;
                result->content.hbox.count = valid_count;
            }
            break;
        }

        case BoxKind::VBox: {
            // vertical box - convert to vbox with shifts
            if (tex_box->content.vbox.count == 0) {
                result->content_type = radiant::MathBoxContentType::Empty;
            } else {
                result->content_type = radiant::MathBoxContentType::VBox;
                int count = tex_box->content.vbox.count;

                // allocate arrays
                radiant::MathBox** children = (radiant::MathBox**)arena_alloc(
                    ctx->arena, count * sizeof(radiant::MathBox*)
                );
                float* shifts = (float*)arena_alloc(ctx->arena, count * sizeof(float));

                // convert each child and compute shifts
                float current_y = tex_box->height;  // start from top
                int valid_count = 0;

                for (int i = 0; i < count; i++) {
                    TexBox* child = tex_box->content.vbox.children[i];
                    if (child) {
                        children[valid_count] = convert_tex_to_math_box(child, ctx);
                        if (children[valid_count]) {
                            children[valid_count]->parent = result;
                            // compute shift: position relative to baseline
                            current_y -= children[valid_count]->height;
                            shifts[valid_count] = current_y;
                            current_y -= children[valid_count]->depth;
                            valid_count++;
                        }
                    }
                }

                result->content.vbox.children = children;
                result->content.vbox.shifts = shifts;
                result->content.vbox.count = valid_count;
            }
            break;
        }

        case BoxKind::Rule: {
            // rule box (fraction bar)
            result->content_type = radiant::MathBoxContentType::Rule;
            result->content.rule.thickness = tex_box->height + tex_box->depth;
            result->type = radiant::MathBoxType::Ignore;
            break;
        }

        case BoxKind::Glue: {
            // glue converts to kern (simplified - ignores stretch/shrink)
            result->content_type = radiant::MathBoxContentType::Kern;
            result->content.kern.amount = tex_box->width;
            result->type = radiant::MathBoxType::Ignore;
            break;
        }

        case BoxKind::Kern: {
            // kern for horizontal spacing
            result->content_type = radiant::MathBoxContentType::Kern;
            result->content.kern.amount = tex_box->width;
            result->type = radiant::MathBoxType::Ignore;
            break;
        }

        case BoxKind::Math: {
            // math content - process the inner nucleus/sub/sup
            // This is handled as an hbox containing the typeset math
            // Recursively convert the typeset content
            if (tex_box->content.math.nucleus) {
                return convert_tex_to_math_box(tex_box->content.math.nucleus, ctx);
            } else {
                result->content_type = radiant::MathBoxContentType::Empty;
            }
            break;
        }

        case BoxKind::Fraction: {
            // fraction box: numerator / rule / denominator
            result->content_type = radiant::MathBoxContentType::VBox;

            // We need 3 children: num, rule, denom
            radiant::MathBox** children = (radiant::MathBox**)arena_alloc(ctx->arena, 3 * sizeof(radiant::MathBox*));
            float* shifts = (float*)arena_alloc(ctx->arena, 3 * sizeof(float));

            // convert numerator and denominator
            if (tex_box->content.fraction.numerator) {
                children[0] = convert_tex_to_math_box(tex_box->content.fraction.numerator, ctx);
            } else {
                children[0] = radiant::make_empty_box(ctx->arena, 0, 0, 0);
            }

            // create rule
            float rule_thickness = tex_box->content.fraction.rule_thickness;
            children[1] = radiant::make_rule(ctx->arena, tex_box->width, rule_thickness);

            if (tex_box->content.fraction.denominator) {
                children[2] = convert_tex_to_math_box(tex_box->content.fraction.denominator, ctx);
            } else {
                children[2] = radiant::make_empty_box(ctx->arena, 0, 0, 0);
            }

            // compute shifts (positions relative to baseline)
            // rule is at axis height, num above, denom below
            float axis = tex_box->content.fraction.axis_height;
            shifts[0] = axis + rule_thickness / 2 + tex_box->content.fraction.num_shift;
            shifts[1] = axis;  // rule at axis
            shifts[2] = axis - rule_thickness / 2 - tex_box->content.fraction.denom_shift;

            result->content.vbox.children = children;
            result->content.vbox.shifts = shifts;
            result->content.vbox.count = 3;
            result->type = radiant::MathBoxType::Inner;
            break;
        }

        case BoxKind::Radical: {
            // radical (square root) box
            result->content_type = radiant::MathBoxContentType::Radical;
            result->type = radiant::MathBoxType::Inner;

            if (tex_box->content.radical.radicand) {
                result->content.radical.radicand = convert_tex_to_math_box(
                    tex_box->content.radical.radicand, ctx
                );
            }
            if (tex_box->content.radical.index) {
                result->content.radical.index = convert_tex_to_math_box(
                    tex_box->content.radical.index, ctx
                );
            }
            result->content.radical.rule_thickness = tex_box->content.radical.rule_thickness;
            result->content.radical.rule_y = tex_box->content.radical.rule_y;
            break;
        }

        case BoxKind::Delimiter: {
            // extensible delimiter
            result->content_type = radiant::MathBoxContentType::Delimiter;
            result->content.delimiter.codepoint = tex_box->content.delimiter.codepoint;
            result->content.delimiter.target_height = tex_box->content.delimiter.target_height;
            result->content.delimiter.is_left = tex_box->content.delimiter.is_left;
            // get face from font provider
            if (ctx->font_provider) {
                result->content.delimiter.face = ctx->font_provider->get_ft_face(
                    FontFamily::MathExtension, false, false, ctx->base_size * ctx->scale
                );
            }
            break;
        }

        case BoxKind::Accent: {
            // accent box - convert as vbox with base and accent
            result->content_type = radiant::MathBoxContentType::VBox;

            radiant::MathBox** children = (radiant::MathBox**)arena_alloc(ctx->arena, 2 * sizeof(radiant::MathBox*));
            float* shifts = (float*)arena_alloc(ctx->arena, 2 * sizeof(float));

            // accent glyph
            radiant::MathBox* accent_box = radiant::alloc_math_box(ctx->arena);
            accent_box->content_type = radiant::MathBoxContentType::Glyph;
            accent_box->content.glyph.codepoint = tex_box->content.accent.accent_char;
            if (ctx->font_provider) {
                accent_box->content.glyph.face = ctx->font_provider->get_ft_face(
                    FontFamily::MathSymbol, false, false, ctx->base_size * ctx->scale
                );
            }
            children[0] = accent_box;

            // base
            if (tex_box->content.accent.base) {
                children[1] = convert_tex_to_math_box(tex_box->content.accent.base, ctx);
            } else {
                children[1] = radiant::make_empty_box(ctx->arena, 0, 0, 0);
            }

            // position accent above base
            shifts[1] = 0;  // base at baseline
            shifts[0] = tex_box->content.accent.accent_shift;

            result->content.vbox.children = children;
            result->content.vbox.shifts = shifts;
            result->content.vbox.count = 2;
            break;
        }

        default:
            // unknown kind - create empty box
            result->content_type = radiant::MathBoxContentType::Empty;
            break;
    }

    return result;
}

// ============================================================================
// MathBox to TexBox Conversion
// ============================================================================

TexBox* convert_math_box_to_tex(
    const radiant::MathBox* math_box,
    Arena* arena,
    RadiantFontProvider* font_provider
) {
    if (!math_box || !arena) {
        return nullptr;
    }

    TexBox* result = (TexBox*)arena_alloc(arena, sizeof(TexBox));
    memset(result, 0, sizeof(TexBox));

    // Copy dimensions
    result->width = math_box->width;
    result->height = math_box->height;
    result->depth = math_box->depth;
    result->italic_correction = math_box->italic;

    // Convert type
    result->type = radiant_to_tex_type(math_box->type);

    // Handle different content types
    switch (math_box->content_type) {
        case radiant::MathBoxContentType::Glyph:
            result->kind = BoxKind::Char;
            result->content.ch.codepoint = math_box->content.glyph.codepoint;
            result->content.ch.family = FontFamily::MathItalic;  // default
            break;

        case radiant::MathBoxContentType::HBox: {
            result->kind = BoxKind::HBox;
            int count = math_box->content.hbox.count;
            result->content.hbox.children = (TexBox**)arena_alloc(arena, count * sizeof(TexBox*));
            result->content.hbox.count = count;
            result->content.hbox.capacity = count;

            for (int i = 0; i < count; i++) {
                result->content.hbox.children[i] = convert_math_box_to_tex(
                    math_box->content.hbox.children[i], arena, font_provider
                );
            }
            break;
        }

        case radiant::MathBoxContentType::VBox: {
            result->kind = BoxKind::VBox;
            int count = math_box->content.vbox.count;
            result->content.vbox.children = (TexBox**)arena_alloc(arena, count * sizeof(TexBox*));
            result->content.vbox.count = count;
            result->content.vbox.capacity = count;

            for (int i = 0; i < count; i++) {
                result->content.vbox.children[i] = convert_math_box_to_tex(
                    math_box->content.vbox.children[i], arena, font_provider
                );
            }
            break;
        }

        case radiant::MathBoxContentType::Kern:
            result->kind = BoxKind::Kern;
            break;

        case radiant::MathBoxContentType::Rule:
            result->kind = BoxKind::Rule;
            break;

        case radiant::MathBoxContentType::Radical:
            result->kind = BoxKind::Radical;
            if (math_box->content.radical.radicand) {
                result->content.radical.radicand = convert_math_box_to_tex(
                    math_box->content.radical.radicand, arena, font_provider
                );
            }
            if (math_box->content.radical.index) {
                result->content.radical.index = convert_math_box_to_tex(
                    math_box->content.radical.index, arena, font_provider
                );
            }
            result->content.radical.rule_thickness = math_box->content.radical.rule_thickness;
            result->content.radical.rule_y = math_box->content.radical.rule_y;
            break;

        case radiant::MathBoxContentType::Delimiter:
            result->kind = BoxKind::Delimiter;
            result->content.delimiter.codepoint = math_box->content.delimiter.codepoint;
            result->content.delimiter.target_height = math_box->content.delimiter.target_height;
            result->content.delimiter.is_left = math_box->content.delimiter.is_left;
            break;

        default:
            result->kind = BoxKind::HBox;  // empty hbox
            result->content.hbox.children = nullptr;
            result->content.hbox.count = 0;
            result->content.hbox.capacity = 0;
            break;
    }

    return result;
}

// ============================================================================
// Radiant Integration
// ============================================================================

radiant::MathBox* layout_math_with_tex(
    Item math_node,
    radiant::MathContext* ctx,
    Arena* arena,
    RadiantFontProvider* font_provider
) {
    if (math_node == ItemNull || !arena) {
        return nullptr;
    }

    // build TeX AST from Lambda math node
    // For now, this is a placeholder - would need AST builder integration
    // The actual flow would be:
    // 1. math_node (Lambda) -> TeX AST (tex_ast_builder)
    // 2. TeX AST -> TexBox tree (tex_math_layout)
    // 3. TexBox tree -> MathBox tree (this bridge)

    // For now, return null to indicate not yet implemented
    log_warn("layout_math_with_tex: not yet fully implemented");
    return nullptr;
}

// ============================================================================
// Rendering
// ============================================================================

void render_tex_box(
    const TexBox* box,
    float x,
    float y,
    radiant::RenderContext* render_ctx,
    RadiantFontProvider* font_provider
) {
    if (!box || !render_ctx) return;

    // This would integrate with Radiant's rendering pipeline
    // For now, just a placeholder
    log_debug("render_tex_box: at (%.1f, %.1f), size=%.1f x %.1f",
              x, y, box->width, box->height + box->depth);
}

// ============================================================================
// Debug Utilities
// ============================================================================

radiant::Rect tex_box_bounds(const TexBox* box) {
    if (!box) return radiant::Rect{0, 0, 0, 0};
    return radiant::Rect{0, -box->height, box->width, box->height + box->depth};
}

static void dump_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        log_debug("  ");
    }
}

static const char* box_kind_name(BoxKind kind) {
    switch (kind) {
        case BoxKind::Char:      return "Char";
        case BoxKind::HBox:      return "HBox";
        case BoxKind::VBox:      return "VBox";
        case BoxKind::Rule:      return "Rule";
        case BoxKind::Glue:      return "Glue";
        case BoxKind::Kern:      return "Kern";
        case BoxKind::Math:      return "Math";
        case BoxKind::Fraction:  return "Fraction";
        case BoxKind::Radical:   return "Radical";
        case BoxKind::Delimiter: return "Delimiter";
        case BoxKind::Accent:    return "Accent";
        default:                 return "Unknown";
    }
}

void dump_tex_box(const TexBox* box, int indent) {
    if (!box) {
        log_debug("%*s(null)", indent * 2, "");
        return;
    }

    log_debug("%*s%s: w=%.2f h=%.2f d=%.2f",
              indent * 2, "",
              box_kind_name(box->kind),
              box->width, box->height, box->depth);

    switch (box->kind) {
        case BoxKind::Char:
            log_debug("%*s  char=U+%04X", indent * 2, "", box->content.ch.codepoint);
            break;

        case BoxKind::HBox:
            for (int i = 0; i < box->content.hbox.count; i++) {
                dump_tex_box(box->content.hbox.children[i], indent + 1);
            }
            break;

        case BoxKind::VBox:
            for (int i = 0; i < box->content.vbox.count; i++) {
                dump_tex_box(box->content.vbox.children[i], indent + 1);
            }
            break;

        case BoxKind::Fraction:
            log_debug("%*s  numerator:", indent * 2, "");
            dump_tex_box(box->content.fraction.numerator, indent + 2);
            log_debug("%*s  denominator:", indent * 2, "");
            dump_tex_box(box->content.fraction.denominator, indent + 2);
            break;

        case BoxKind::Radical:
            log_debug("%*s  radicand:", indent * 2, "");
            dump_tex_box(box->content.radical.radicand, indent + 2);
            if (box->content.radical.index) {
                log_debug("%*s  index:", indent * 2, "");
                dump_tex_box(box->content.radical.index, indent + 2);
            }
            break;

        default:
            break;
    }
}

bool compare_box_trees(const TexBox* tex, const radiant::MathBox* radiant, float tolerance) {
    if (!tex && !radiant) return true;
    if (!tex || !radiant) return false;

    // compare dimensions
    if (fabsf(tex->width - radiant->width) > tolerance) return false;
    if (fabsf(tex->height - radiant->height) > tolerance) return false;
    if (fabsf(tex->depth - radiant->depth) > tolerance) return false;

    // For deeper comparison, would need to match up children
    // This is a simplified version

    return true;
}

} // namespace tex
