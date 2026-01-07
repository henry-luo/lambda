// layout_math.cpp - Math layout engine implementation
//
// Converts MathNode trees (Lambda elements) to MathBox trees,
// implementing TeXBook typesetting algorithms.

#include "layout_math.hpp"
#include "math_box.hpp"
#include "math_context.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/math_node.hpp"
#include "../lambda/math_symbols.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include <cstring>
#include <cmath>

namespace radiant {

using namespace lambda;

// ============================================================================
// Font Metrics Constants (defined in math_context.hpp, initialized here)
// ============================================================================

const MathFontMetrics MATH_METRICS_NORMAL;
const MathFontMetrics MATH_METRICS_SCRIPT;
const MathFontMetrics MATH_METRICS_SCRIPTSCRIPT;

// ============================================================================
// Inter-Box Spacing Table (TeXBook, Chapter 18)
// Values in mu: 0=none, 3=thin, 4=medium, 5=thick
// ============================================================================

static const int SPACING_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */   {0,  3,   4,   5,   0,   0,    0,    3},
    /* Op  */   {3,  3,   0,   5,   0,   0,    0,    3},
    /* Bin */   {4,  4,   0,   0,   4,   0,    0,    4},
    /* Rel */   {5,  5,   0,   0,   5,   0,    0,    5},
    /* Open*/   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/  {0,  3,   4,   5,   0,   0,    0,    3},
    /* Punct*/  {3,  3,   0,   3,   3,   0,    3,    3},
    /* Inner*/  {3,  3,   4,   5,   3,   0,    3,    3},
};

// Tight spacing for script/scriptscript styles
static const int TIGHT_SPACING_TABLE[8][8] = {
    //        Ord  Op  Bin  Rel  Open Close Punct Inner
    /* Ord */   {0,  3,   0,   0,   0,   0,    0,    0},
    /* Op  */   {3,  3,   0,   0,   0,   0,    0,    0},
    /* Bin */   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Rel */   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Open*/   {0,  0,   0,   0,   0,   0,    0,    0},
    /* Close*/  {0,  3,   0,   0,   0,   0,    0,    0},
    /* Punct*/  {0,  0,   0,   0,   0,   0,    0,    0},
    /* Inner*/  {0,  3,   0,   0,   0,   0,    0,    0},
};

int get_inter_box_spacing(MathBoxType left, MathBoxType right, bool tight) {
    int l = (int)left;
    int r = (int)right;
    if (l >= 8 || r >= 8) return 0; // Ignore, Lift types
    return tight ? TIGHT_SPACING_TABLE[l][r] : SPACING_TABLE[l][r];
}

// ============================================================================
// Font Loading and Glyph Metrics
// ============================================================================

FT_Face load_math_font(MathContext& ctx) {
    if (!ctx.ui_context) {
        log_error("layout_math: no UI context for font loading");
        return nullptr;
    }

    // Create font properties for math font
    FontProp fprop = {};
    fprop.family = (char*)ctx.font_family;
    fprop.font_size = ctx.font_size();
    fprop.font_style = CSS_VALUE_NORMAL;
    fprop.font_weight = CSS_VALUE_NORMAL;

    // Try to load the math font
    FT_Face face = load_styled_font(ctx.ui_context, ctx.font_family, &fprop);

    if (!face) {
        // Fallback to default serif
        face = load_styled_font(ctx.ui_context, "serif", &fprop);
    }

    return face;
}

void get_glyph_metrics(FT_Face face, int codepoint, float css_font_size, float pixel_ratio,
                       float* width, float* height, float* depth, float* italic) {
    if (!face) {
        *width = *height = *depth = *italic = 0;
        return;
    }

    // CRITICAL: Set font size in PHYSICAL pixels for HiDPI displays
    // The face may have been loaded at a different size, so we must set it correctly
    float physical_font_size = css_font_size * pixel_ratio;
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)physical_font_size);

    // Load glyph without rendering
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0) {
        // Glyph not found - return CSS pixel metrics
        *width = css_font_size * 0.5f;
        *height = css_font_size * 0.7f;
        *depth = 0;
        *italic = 0;
        return;
    }

    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP) != 0) {
        *width = css_font_size * 0.5f;
        *height = css_font_size * 0.7f;
        *depth = 0;
        *italic = 0;
        return;
    }

    FT_GlyphSlot glyph = face->glyph;

    // Metrics from FreeType are in physical pixels (26.6 fixed point)
    // Convert to CSS pixels by dividing by pixel_ratio
    *width = (glyph->metrics.horiAdvance / 64.0f) / pixel_ratio;
    *height = (glyph->metrics.horiBearingY / 64.0f) / pixel_ratio;
    *depth = ((glyph->metrics.height - glyph->metrics.horiBearingY) / 64.0f) / pixel_ratio;
    if (*depth < 0) *depth = 0;

    // Italic correction (approximation) - also in CSS pixels
    *italic = 0;
    if (glyph->metrics.horiBearingX + glyph->metrics.width > glyph->metrics.horiAdvance) {
        *italic = ((glyph->metrics.horiBearingX + glyph->metrics.width - glyph->metrics.horiAdvance) / 64.0f) / pixel_ratio;
    }
}

MathBox* make_glyph(MathContext& ctx, int codepoint, MathBoxType type, Arena* arena) {
    FT_Face face = load_math_font(ctx);

    // Get pixel_ratio for HiDPI support
    float pixel_ratio = (ctx.ui_context && ctx.ui_context->pixel_ratio > 0) ? ctx.ui_context->pixel_ratio : 1.0f;

    float width, height, depth, italic;
    get_glyph_metrics(face, codepoint, ctx.font_size(), pixel_ratio, &width, &height, &depth, &italic);

    MathBox* box = make_glyph_box(arena, codepoint, face, width, height, depth, type);
    box->italic = italic;
    box->scale = ctx.scaling_factor();

    return box;
}

// ============================================================================
// Node Field Access Helpers
// ============================================================================

static const char* get_string_field(Item node, const char* field) {
    if (node.item == ItemNull.item) return nullptr;
    TypeId type = get_type_id(node);
    if (type != LMD_TYPE_MAP) return nullptr;

    Map* map = node.map;
    ConstItem val = map->get(field);
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

static Item get_item_field(Item node, const char* field) {
    if (node.item == ItemNull.item) return ItemNull;
    TypeId type = get_type_id(node);
    if (type != LMD_TYPE_MAP) return ItemNull;

    Map* map = node.map;
    ConstItem val = map->get(field);
    return *(Item*)&val;
}

static int get_int_field(Item node, const char* field, int default_val) {
    if (node.item == ItemNull.item) return default_val;
    TypeId type = get_type_id(node);
    if (type != LMD_TYPE_MAP) return default_val;

    Map* map = node.map;
    ConstItem val = map->get(field);
    if (val.item == ItemNull.item) return default_val;

    TypeId vtype = val.type_id();
    if (vtype == LMD_TYPE_INT) {
        // LMD_TYPE_INT is stored inline in the item's int_val field
        Item item = *(Item*)&val;
        return item.int_val;
    } else if (vtype == LMD_TYPE_INT64) {
        // LMD_TYPE_INT64 is stored via pointer
        return (int)((Item*)&val)->get_int64();
    }
    return default_val;
}

static List* get_list_field(Item node, const char* field) {
    Item items = get_item_field(node, field);
    if (items.item == ItemNull.item) return nullptr;
    if (get_type_id(items) == LMD_TYPE_LIST) {
        return items.list;
    }
    return nullptr;
}

// ============================================================================
// Main Layout Dispatcher
// ============================================================================

MathBox* layout_math(Item node, MathContext& ctx, Arena* arena) {
    if (node.item == ItemNull.item) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathNodeType node_type = get_math_node_type(node);

    switch (node_type) {
        case MathNodeType::Symbol:
            return layout_symbol(node, ctx, arena);
        case MathNodeType::Number:
            return layout_number(node, ctx, arena);
        case MathNodeType::Command:
            return layout_command(node, ctx, arena);
        case MathNodeType::Row:
            return layout_row(node, ctx, arena);
        case MathNodeType::Group:
            return layout_group(node, ctx, arena);
        case MathNodeType::Fraction:
            return layout_fraction(node, ctx, arena);
        case MathNodeType::Binomial:
            return layout_binomial(node, ctx, arena);
        case MathNodeType::Radical:
            return layout_radical(node, ctx, arena);
        case MathNodeType::Subsup:
            return layout_subsup(node, ctx, arena);
        case MathNodeType::Delimiter:
            return layout_delimiter(node, ctx, arena);
        case MathNodeType::Accent:
            return layout_accent(node, ctx, arena);
        case MathNodeType::BigOperator:
            return layout_big_operator(node, ctx, arena);
        case MathNodeType::Text:
            return layout_text(node, ctx, arena);
        case MathNodeType::Style:
            return layout_style(node, ctx, arena);
        case MathNodeType::Space:
            return layout_space(node, ctx, arena);
        default:
            log_debug("layout_math: unknown node type %d", (int)node_type);
            return make_empty_box(arena, 0, 0, 0);
    }
}

// ============================================================================
// Symbol Layout
// ============================================================================

MathBox* layout_symbol(Item node, MathContext& ctx, Arena* arena) {
    const char* value = get_string_field(node, "value");
    if (!value || !*value) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathAtomType atom_type = get_math_atom_type(node);
    MathBoxType box_type = atom_to_box_type(atom_type);

    // Get the first codepoint (for single-char symbols)
    int codepoint = (unsigned char)value[0];

    // Handle UTF-8 for multi-byte characters
    if ((codepoint & 0x80) != 0) {
        // Decode UTF-8
        if ((codepoint & 0xE0) == 0xC0) {
            codepoint = ((codepoint & 0x1F) << 6) | (value[1] & 0x3F);
        } else if ((codepoint & 0xF0) == 0xE0) {
            codepoint = ((codepoint & 0x0F) << 12) | ((value[1] & 0x3F) << 6) | (value[2] & 0x3F);
        } else if ((codepoint & 0xF8) == 0xF0) {
            codepoint = ((codepoint & 0x07) << 18) | ((value[1] & 0x3F) << 12) |
                        ((value[2] & 0x3F) << 6) | (value[3] & 0x3F);
        }
    }

    MathBox* box = make_glyph(ctx, codepoint, box_type, arena);
    box->source_node = node;
    return box;
}

// ============================================================================
// Number Layout
// ============================================================================

MathBox* layout_number(Item node, MathContext& ctx, Arena* arena) {
    const char* value = get_string_field(node, "value");
    if (!value || !*value) {
        return make_empty_box(arena, 0, 0, 0);
    }

    // Layout each digit
    int len = strlen(value);
    MathBox** boxes = (MathBox**)arena_alloc(arena, len * sizeof(MathBox*));
    int count = 0;

    for (int i = 0; i < len; i++) {
        char c = value[i];
        if (c >= '0' && c <= '9') {
            boxes[count++] = make_glyph(ctx, c, MathBoxType::Ord, arena);
        } else if (c == '.') {
            boxes[count++] = make_glyph(ctx, c, MathBoxType::Punct, arena);
        }
    }

    if (count == 0) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathBox* box = make_hbox(arena, boxes, count, MathBoxType::Ord);
    box->source_node = node;
    return box;
}

// ============================================================================
// Command Layout (Greek letters, operators, etc.)
// ============================================================================

MathBox* layout_command(Item node, MathContext& ctx, Arena* arena) {
    const char* cmd = get_string_field(node, "cmd");
    int codepoint = get_int_field(node, "codepoint", 0);

    MathAtomType atom_type = get_math_atom_type(node);
    MathBoxType box_type = atom_to_box_type(atom_type);

    // If no codepoint stored, look it up
    if (codepoint == 0 && cmd) {
        int cp;
        MathAtomType at;
        if (lookup_math_symbol(cmd, &cp, &at)) {
            codepoint = cp;
            box_type = atom_to_box_type(at);
        }
    }

    if (codepoint == 0) {
        // Unknown command - render as text
        log_debug("layout_command: unknown command '%s'", cmd ? cmd : "(null)");
        return make_empty_box(arena, ctx.font_size() * 0.5f, ctx.font_size() * 0.7f, 0);
    }

    MathBox* box = make_glyph(ctx, codepoint, box_type, arena);
    box->source_node = node;
    return box;
}

// ============================================================================
// Row Layout (horizontal sequence)
// ============================================================================

MathBox* layout_row(Item node, MathContext& ctx, Arena* arena) {
    List* items = get_list_field(node, "items");
    if (!items || items->length == 0) {
        return make_empty_box(arena, 0, 0, 0);
    }

    int count = items->length;
    MathBox** boxes = (MathBox**)arena_alloc(arena, count * sizeof(MathBox*));
    int actual_count = 0;

    for (int i = 0; i < count; i++) {
        ConstItem citem = items->get(i);
        Item child = *(Item*)&citem;  // cast ConstItem to Item
        if (child.item != ItemNull.item) {
            MathBox* box = layout_math(child, ctx, arena);
            if (box && box->width > 0) {
                boxes[actual_count++] = box;
            }
        }
    }

    if (actual_count == 0) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathBox* row = make_hbox(arena, boxes, actual_count, MathBoxType::Ord);
    row->source_node = node;
    return row;
}

// ============================================================================
// Group Layout
// ============================================================================

MathBox* layout_group(Item node, MathContext& ctx, Arena* arena) {
    Item content = get_item_field(node, "content");
    if (content.item == ItemNull.item) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathBox* box = layout_math(content, ctx, arena);
    box->type = MathBoxType::Lift; // Group lifts children's types for spacing
    box->source_node = node;
    return box;
}

// ============================================================================
// Fraction Layout (TeXBook Rule 15)
// ============================================================================

MathBox* layout_fraction(Item node, MathContext& ctx, Arena* arena) {
    Item numer = get_item_field(node, "numer");
    Item denom = get_item_field(node, "denom");

    // Layout numerator and denominator in appropriate styles
    MathContext num_ctx = ctx.derive_frac_num();
    MathContext den_ctx = ctx.derive_frac_den();

    MathBox* numer_box = layout_math(numer, num_ctx, arena);
    MathBox* denom_box = layout_math(denom, den_ctx, arena);

    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();
    float rule_thickness = m.default_rule_thickness * font_size;

    // Calculate widths and center alignment
    float frac_width = max(numer_box->width, denom_box->width);
    float axis = m.axis_height * font_size;

    // Calculate shifts (Rule 15b)
    float numer_shift, denom_shift;
    float min_clearance;

    if (ctx.is_display_style()) {
        numer_shift = m.num1 * font_size;
        denom_shift = m.denom1 * font_size;
        min_clearance = 3 * rule_thickness;
    } else {
        numer_shift = m.num2 * font_size;
        denom_shift = m.denom2 * font_size;
        min_clearance = rule_thickness;
    }

    // Adjust for minimum clearance (Rule 15c)
    float numer_bottom = numer_shift - numer_box->depth;
    float rule_top = axis + rule_thickness / 2;
    float gap_above = numer_bottom - rule_top;
    if (gap_above < min_clearance) {
        numer_shift += min_clearance - gap_above;
    }

    float rule_bottom = axis - rule_thickness / 2;
    float denom_top = -denom_shift + denom_box->height;
    float gap_below = rule_bottom - denom_top;
    if (gap_below < min_clearance) {
        denom_shift += min_clearance - gap_below;
    }

    // Create centering kerns for numerator
    float numer_left_kern = (frac_width - numer_box->width) / 2;
    MathBox* numer_kern = make_kern(arena, numer_left_kern);
    MathBox* numer_items[] = { numer_kern, numer_box };
    MathBox* centered_numer = make_hbox(arena, numer_items, 2, MathBoxType::Ord);
    centered_numer->width = frac_width;

    // Create centering kerns for denominator
    float denom_left_kern = (frac_width - denom_box->width) / 2;
    MathBox* denom_kern = make_kern(arena, denom_left_kern);
    MathBox* denom_items[] = { denom_kern, denom_box };
    MathBox* centered_denom = make_hbox(arena, denom_items, 2, MathBoxType::Ord);
    centered_denom->width = frac_width;

    // Create fraction bar
    MathBox* rule_box = make_rule(arena, frac_width, rule_thickness, axis);

    // Build vertical stack
    MathBox* children[] = { centered_numer, rule_box, centered_denom };
    float shifts[] = { numer_shift, axis, -denom_shift };

    MathBox* frac = make_vbox(arena, children, shifts, 3, MathBoxType::Inner);
    frac->source_node = node;

    return frac;
}

// ============================================================================
// Binomial Layout (like fraction but with delimiters)
// ============================================================================

MathBox* layout_binomial(Item node, MathContext& ctx, Arena* arena) {
    Item top = get_item_field(node, "top");
    Item bottom = get_item_field(node, "bottom");

    // Layout top and bottom like fraction but without bar
    MathContext top_ctx = ctx.derive_frac_num();
    MathContext bot_ctx = ctx.derive_frac_den();

    MathBox* top_box = layout_math(top, top_ctx, arena);
    MathBox* bot_box = layout_math(bottom, bot_ctx, arena);

    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();

    // Similar to fraction but with more gap (no rule)
    float frac_width = max(top_box->width, bot_box->width);
    float axis = m.axis_height * font_size;
    float gap = m.default_rule_thickness * font_size * 3; // larger gap for binomial

    float top_shift, bot_shift;
    if (ctx.is_display_style()) {
        top_shift = m.num3 * font_size; // use num3 for no-bar fraction
        bot_shift = m.denom2 * font_size;
    } else {
        top_shift = m.num3 * font_size * 0.7f;
        bot_shift = m.denom2 * font_size * 0.7f;
    }

    // Center alignment
    float top_kern = (frac_width - top_box->width) / 2;
    float bot_kern = (frac_width - bot_box->width) / 2;

    MathBox* top_kern_box = make_kern(arena, top_kern);
    MathBox* bot_kern_box = make_kern(arena, bot_kern);

    MathBox* top_items[] = { top_kern_box, top_box };
    MathBox* bot_items[] = { bot_kern_box, bot_box };

    MathBox* centered_top = make_hbox(arena, top_items, 2, MathBoxType::Ord);
    MathBox* centered_bot = make_hbox(arena, bot_items, 2, MathBoxType::Ord);
    centered_top->width = frac_width;
    centered_bot->width = frac_width;

    // Build vertical stack (no rule)
    MathBox* children[] = { centered_top, centered_bot };
    float shifts[] = { top_shift, -bot_shift };

    MathBox* inner = make_vbox(arena, children, shifts, 2, MathBoxType::Inner);

    // Add delimiters
    float total_height = inner->height + inner->depth;
    MathBox* left_paren = make_delimiter(ctx, "(", total_height, true, arena);
    MathBox* right_paren = make_delimiter(ctx, ")", total_height, false, arena);

    MathBox* result_items[] = { left_paren, inner, right_paren };
    MathBox* result = make_hbox(arena, result_items, 3, MathBoxType::Inner);
    result->source_node = node;

    return result;
}

// ============================================================================
// Radical Layout
// ============================================================================

MathBox* layout_radical(Item node, MathContext& ctx, Arena* arena) {
    Item radicand = get_item_field(node, "radicand");
    Item index = get_item_field(node, "index");

    MathBox* radicand_box = layout_math(radicand, ctx, arena);
    MathBox* index_box = nullptr;

    if (index.item != ItemNull.item) {
        MathContext index_ctx = ctx.derive(MathStyle::ScriptScript);
        index_box = layout_math(index, index_ctx, arena);
    }

    MathBox* radical = make_radical_box(ctx, radicand_box, index_box, arena);
    radical->source_node = node;

    return radical;
}

MathBox* make_radical_box(MathContext& ctx, MathBox* radicand_box,
                          MathBox* index_box, Arena* arena) {
    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();

    // Radical parameters
    float gap = ctx.is_display_style()
        ? m.radical_display_style_vertical_gap * font_size
        : m.radical_vertical_gap * font_size;
    float rule_thickness = m.radical_rule_thickness * font_size;
    float extra = m.radical_extra_ascender * font_size;

    // Calculate total height needed
    float radicand_height = radicand_box->height + gap + rule_thickness + extra;
    float radicand_depth = radicand_box->depth;
    float total_height = radicand_height + radicand_depth;

    // Get radical symbol glyph (√)
    int radical_codepoint = 0x221A;
    MathBox* radical_glyph = make_glyph(ctx, radical_codepoint, MathBoxType::Ord, arena);

    // Scale radical to match needed height
    float scale_factor = total_height / (radical_glyph->height + radical_glyph->depth);
    if (scale_factor < 1.0f) scale_factor = 1.0f;

    // For now, use the basic glyph (extensible radicals would need font support)
    radical_glyph->scale = scale_factor;
    radical_glyph->height *= scale_factor;
    radical_glyph->depth *= scale_factor;
    radical_glyph->width *= scale_factor;

    // Create the overline (rule)
    float rule_width = radicand_box->width + font_size * 0.1f; // small padding
    float rule_y = radicand_box->height + gap + rule_thickness / 2;
    MathBox* rule = make_rule(arena, rule_width, rule_thickness, rule_y);

    // Build the radical structure
    // radical_glyph | (radicand_box over rule)

    // Create inner vbox for radicand + rule
    MathBox* vbox_children[] = { radicand_box };
    float vbox_shifts[] = { 0 };
    MathBox* inner = make_vbox(arena, vbox_children, vbox_shifts, 1, MathBoxType::Ord);

    // Combine radical glyph, rule area, and radicand
    MathBox* parts[] = { radical_glyph, inner };
    MathBox* result = make_hbox(arena, parts, 2, MathBoxType::Ord);

    // Adjust height for the rule
    result->height = radicand_box->height + gap + rule_thickness + extra;

    // Handle index (for nth roots)
    if (index_box) {
        float kern_before = m.radical_kern_before_degree * font_size;
        float raise = m.radical_degree_bottom_raise_percent * (result->height - result->depth);

        // Position index above and to the left of the radical
        MathBox* index_kern = make_kern(arena, kern_before);

        // Create shifted index
        MathBox* shifted_index_arr[] = { index_box };
        float index_shifts[] = { raise };
        MathBox* shifted_index = make_vbox(arena, shifted_index_arr, index_shifts, 1, MathBoxType::Ord);

        MathBox* final_parts[] = { shifted_index, result };
        result = make_hbox(arena, final_parts, 2, MathBoxType::Ord);
    }

    return result;
}

// ============================================================================
// Subscript/Superscript Layout (TeXBook Rules 18a-f)
// ============================================================================

MathBox* layout_subsup(Item node, MathContext& ctx, Arena* arena) {
    Item base_node = get_item_field(node, "base");
    Item sub_node = get_item_field(node, "sub");
    Item sup_node = get_item_field(node, "sup");

    MathBox* base = layout_math(base_node, ctx, arena);
    MathBox* sub_box = nullptr;
    MathBox* sup_box = nullptr;

    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();

    float sup_shift = 0, sub_shift = 0;

    // Rule 18a: Render superscript
    if (sup_node.item != ItemNull.item) {
        MathContext sup_ctx = ctx.derive_sup();
        sup_box = layout_math(sup_node, sup_ctx, arena);
        sup_shift = base->height - m.sup_drop * sup_ctx.scaling_factor() * font_size;
    }

    // Render subscript
    if (sub_node.item != ItemNull.item) {
        MathContext sub_ctx = ctx.derive_sub();
        sub_box = layout_math(sub_node, sub_ctx, arena);
        sub_shift = base->depth + m.sub_drop * sub_ctx.scaling_factor() * font_size;
    }

    // Rule 18c: Minimum superscript shift
    if (sup_box) {
        float min_sup_shift;
        if (ctx.is_display_style()) {
            min_sup_shift = m.sup1 * font_size;
        } else if (ctx.is_cramped()) {
            min_sup_shift = m.sup3 * font_size;
        } else {
            min_sup_shift = m.sup2 * font_size;
        }

        sup_shift = max(sup_shift, min_sup_shift);
        sup_shift = max(sup_shift, sup_box->depth + 0.25f * m.x_height * font_size);
    }

    // Rule 18b: Minimum subscript shift
    if (sub_box && !sup_box) {
        sub_shift = max(sub_shift, m.sub1 * font_size);
        sub_shift = max(sub_shift, sub_box->height - 0.8f * m.x_height * font_size);
    }

    // Rule 18e: Both sub and sup - ensure minimum gap
    if (sub_box && sup_box) {
        float gap = (sup_shift - sup_box->depth) - (sub_box->height - sub_shift);
        float min_gap = 4 * m.default_rule_thickness * font_size;
        if (gap < min_gap) {
            sub_shift += (min_gap - gap);

            // Rule 18f: Additional adjustment for cramped
            float psi = 0.8f * m.x_height * font_size - (sup_shift - sup_box->depth);
            if (psi > 0) {
                sup_shift += psi;
                sub_shift -= psi;
            }
        }
    }

    // Build result
    // Base followed by stacked scripts
    MathBox* result;

    if (sup_box && sub_box) {
        // Both scripts: stack them vertically, then attach to base
        MathBox* script_children[] = { sup_box, sub_box };
        float script_shifts[] = { sup_shift, -sub_shift };
        MathBox* scripts = make_vbox(arena, script_children, script_shifts, 2, MathBoxType::Ord);

        // Add italic correction kern before scripts
        float kern_amount = base->italic;
        MathBox* kern = make_kern(arena, kern_amount);

        MathBox* parts[] = { base, kern, scripts };
        result = make_hbox(arena, parts, 3, MathBoxType::Ord);
    } else if (sup_box) {
        // Superscript only
        MathBox* sup_arr[] = { sup_box };
        float sup_shifts[] = { sup_shift };
        MathBox* shifted_sup = make_vbox(arena, sup_arr, sup_shifts, 1, MathBoxType::Ord);

        float kern_amount = base->italic;
        MathBox* kern = make_kern(arena, kern_amount);

        MathBox* parts[] = { base, kern, shifted_sup };
        result = make_hbox(arena, parts, 3, MathBoxType::Ord);
    } else if (sub_box) {
        // Subscript only
        MathBox* sub_arr[] = { sub_box };
        float sub_shifts[] = { -sub_shift };
        MathBox* shifted_sub = make_vbox(arena, sub_arr, sub_shifts, 1, MathBoxType::Ord);

        MathBox* parts[] = { base, shifted_sub };
        result = make_hbox(arena, parts, 2, MathBoxType::Ord);
    } else {
        result = base;
    }

    result->source_node = node;
    return result;
}

// ============================================================================
// Delimiter Layout
// ============================================================================

MathBox* make_delimiter(MathContext& ctx, const char* delimiter,
                        float target_height, bool is_left, Arena* arena) {
    // Map delimiter string to codepoint
    int codepoint = '(';
    MathBoxType type = is_left ? MathBoxType::Open : MathBoxType::Close;

    if (strcmp(delimiter, "(") == 0) codepoint = '(';
    else if (strcmp(delimiter, ")") == 0) codepoint = ')';
    else if (strcmp(delimiter, "[") == 0) codepoint = '[';
    else if (strcmp(delimiter, "]") == 0) codepoint = ']';
    else if (strcmp(delimiter, "\\{") == 0 || strcmp(delimiter, "{") == 0) codepoint = '{';
    else if (strcmp(delimiter, "\\}") == 0 || strcmp(delimiter, "}") == 0) codepoint = '}';
    else if (strcmp(delimiter, "|") == 0) codepoint = '|';
    else if (strcmp(delimiter, "\\|") == 0) codepoint = 0x2016; // double vertical
    else if (strcmp(delimiter, "\\langle") == 0) codepoint = 0x27E8;
    else if (strcmp(delimiter, "\\rangle") == 0) codepoint = 0x27E9;
    else if (strcmp(delimiter, "\\lfloor") == 0) codepoint = 0x230A;
    else if (strcmp(delimiter, "\\rfloor") == 0) codepoint = 0x230B;
    else if (strcmp(delimiter, "\\lceil") == 0) codepoint = 0x2308;
    else if (strcmp(delimiter, "\\rceil") == 0) codepoint = 0x2309;
    else if (strcmp(delimiter, ".") == 0) {
        // Null delimiter
        return make_empty_box(arena, 0, target_height / 2, target_height / 2);
    }

    // Get basic glyph
    MathBox* box = make_glyph(ctx, codepoint, type, arena);

    // Scale if needed
    float current_height = box->height + box->depth;
    if (current_height < target_height && target_height > 0) {
        float scale = target_height / current_height;
        box->height *= scale;
        box->depth *= scale;
        box->width *= scale;
        box->scale = scale;
    }

    // Store delimiter info
    box->content_type = MathBoxContentType::Delimiter;
    box->content.delimiter.codepoint = codepoint;
    box->content.delimiter.target_height = target_height;
    box->content.delimiter.is_left = is_left;

    return box;
}

MathBox* layout_delimiter(Item node, MathContext& ctx, Arena* arena) {
    const char* left = get_string_field(node, "left");
    const char* right = get_string_field(node, "right");
    Item content = get_item_field(node, "content");

    // Layout content first to get height
    MathBox* content_box = layout_math(content, ctx, arena);
    float target_height = content_box->height + content_box->depth;

    // Add some extra height for delimiters
    target_height *= 1.1f;

    // Create delimiters
    MathBox* left_delim = make_delimiter(ctx, left ? left : "(", target_height, true, arena);
    MathBox* right_delim = make_delimiter(ctx, right ? right : ")", target_height, false, arena);

    // Small kern between delimiter and content
    float kern_amount = ctx.font_size() * 0.05f;
    MathBox* left_kern = make_kern(arena, kern_amount);
    MathBox* right_kern = make_kern(arena, kern_amount);

    // Combine
    MathBox* parts[] = { left_delim, left_kern, content_box, right_kern, right_delim };
    MathBox* result = make_hbox(arena, parts, 5, MathBoxType::Inner);
    result->source_node = node;

    return result;
}

// ============================================================================
// Accent Layout
// ============================================================================

MathBox* layout_accent(Item node, MathContext& ctx, Arena* arena) {
    const char* cmd = get_string_field(node, "cmd");
    Item base = get_item_field(node, "base");

    MathBox* base_box = layout_math(base, ctx, arena);

    // Get accent codepoint
    int accent_codepoint = 0x0302; // default: circumflex
    if (cmd) {
        if (strcmp(cmd, "\\hat") == 0) accent_codepoint = 0x0302;
        else if (strcmp(cmd, "\\check") == 0) accent_codepoint = 0x030C;
        else if (strcmp(cmd, "\\tilde") == 0) accent_codepoint = 0x0303;
        else if (strcmp(cmd, "\\acute") == 0) accent_codepoint = 0x0301;
        else if (strcmp(cmd, "\\grave") == 0) accent_codepoint = 0x0300;
        else if (strcmp(cmd, "\\dot") == 0) accent_codepoint = 0x0307;
        else if (strcmp(cmd, "\\ddot") == 0) accent_codepoint = 0x0308;
        else if (strcmp(cmd, "\\breve") == 0) accent_codepoint = 0x0306;
        else if (strcmp(cmd, "\\bar") == 0) accent_codepoint = 0x0304;
        else if (strcmp(cmd, "\\vec") == 0) accent_codepoint = 0x20D7;
        else if (strcmp(cmd, "\\widehat") == 0) accent_codepoint = 0x0302;
        else if (strcmp(cmd, "\\widetilde") == 0) accent_codepoint = 0x0303;
    }

    // Get accent glyph
    MathBox* accent_box = make_glyph(ctx, accent_codepoint, MathBoxType::Ord, arena);

    // Position accent above base
    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();
    float accent_shift = base_box->height + font_size * 0.05f;

    // Center accent over base
    float accent_kern = (base_box->width - accent_box->width) / 2;
    if (accent_kern < 0) accent_kern = 0;

    // Apply skew correction
    accent_kern += base_box->skew;

    MathBox* kern = make_kern(arena, accent_kern);
    MathBox* accent_row_items[] = { kern, accent_box };
    MathBox* accent_row = make_hbox(arena, accent_row_items, 2, MathBoxType::Ord);
    accent_row->width = base_box->width;

    // Stack accent above base
    MathBox* children[] = { accent_row, base_box };
    float shifts[] = { accent_shift, 0 };

    MathBox* result = make_vbox(arena, children, shifts, 2, MathBoxType::Ord);
    result->source_node = node;

    return result;
}

// ============================================================================
// Big Operator Layout
// ============================================================================

MathBox* layout_big_operator(Item node, MathContext& ctx, Arena* arena) {
    const char* op = get_string_field(node, "op");
    Item lower = get_item_field(node, "lower");
    Item upper = get_item_field(node, "upper");

    // Get operator codepoint
    int codepoint = 0x2211; // default: sum
    MathAtomType atom;
    if (op && lookup_math_symbol(op, &codepoint, &atom)) {
        // Found
    }

    // Create operator glyph (larger in display style)
    MathBox* op_box;
    if (ctx.is_display_style()) {
        // Use larger size for display style
        MathContext big_ctx = ctx;
        big_ctx.base_font_size *= 1.4f;
        op_box = make_glyph(big_ctx, codepoint, MathBoxType::Op, arena);
    } else {
        op_box = make_glyph(ctx, codepoint, MathBoxType::Op, arena);
    }

    const MathFontMetrics& m = ctx.metrics();
    float font_size = ctx.font_size();

    // Layout limits
    MathBox* lower_box = nullptr;
    MathBox* upper_box = nullptr;

    if (lower.item != ItemNull.item) {
        MathContext limit_ctx = ctx.derive(MathStyle::Script);
        lower_box = layout_math(lower, limit_ctx, arena);
    }

    if (upper.item != ItemNull.item) {
        MathContext limit_ctx = ctx.derive(MathStyle::Script);
        upper_box = layout_math(upper, limit_ctx, arena);
    }

    // In display style, limits go above/below
    // In text style, limits go as subscript/superscript
    if (ctx.is_display_style()) {
        // Limits above and below
        float max_width = op_box->width;
        if (lower_box && lower_box->width > max_width) max_width = lower_box->width;
        if (upper_box && upper_box->width > max_width) max_width = upper_box->width;

        // Center all components
        // Build from bottom to top

        int child_count = 1;
        MathBox* children[3];
        float shifts[3];

        float current_y = 0;

        // Lower limit
        if (lower_box) {
            float kern = (max_width - lower_box->width) / 2;
            MathBox* kern_box = make_kern(arena, kern);
            MathBox* row_items[] = { kern_box, lower_box };
            MathBox* centered = make_hbox(arena, row_items, 2, MathBoxType::Ord);
            centered->width = max_width;

            children[0] = centered;
            shifts[0] = -op_box->depth - m.big_op_spacing3 * font_size - lower_box->height;
            child_count = 1;
        }

        // Operator
        {
            float kern = (max_width - op_box->width) / 2;
            MathBox* kern_box = make_kern(arena, kern);
            MathBox* row_items[] = { kern_box, op_box };
            MathBox* centered = make_hbox(arena, row_items, 2, MathBoxType::Op);
            centered->width = max_width;

            children[child_count] = centered;
            shifts[child_count] = 0;
            child_count++;
        }

        // Upper limit
        if (upper_box) {
            float kern = (max_width - upper_box->width) / 2;
            MathBox* kern_box = make_kern(arena, kern);
            MathBox* row_items[] = { kern_box, upper_box };
            MathBox* centered = make_hbox(arena, row_items, 2, MathBoxType::Ord);
            centered->width = max_width;

            children[child_count] = centered;
            shifts[child_count] = op_box->height + m.big_op_spacing1 * font_size + upper_box->depth;
            child_count++;
        }

        MathBox* result = make_vbox(arena, children, shifts, child_count, MathBoxType::Op);
        result->width = max_width;
        result->source_node = node;
        return result;
    } else {
        // Limits as sub/superscript - reuse subsup layout
        // Create a synthetic subsup node
        MathBox* result = op_box;

        if (upper_box || lower_box) {
            MathBox* script_children[2];
            float script_shifts[2];
            int count = 0;

            if (upper_box) {
                script_children[count] = upper_box;
                script_shifts[count] = op_box->height + font_size * 0.2f;
                count++;
            }
            if (lower_box) {
                script_children[count] = lower_box;
                script_shifts[count] = -op_box->depth - lower_box->height - font_size * 0.1f;
                count++;
            }

            MathBox* scripts = make_vbox(arena, script_children, script_shifts, count, MathBoxType::Ord);
            MathBox* parts[] = { op_box, scripts };
            result = make_hbox(arena, parts, 2, MathBoxType::Op);
        }

        result->source_node = node;
        return result;
    }
}

// ============================================================================
// Text Layout
// ============================================================================

MathBox* layout_text(Item node, MathContext& ctx, Arena* arena) {
    const char* content = get_string_field(node, "content");
    if (!content || !*content) {
        return make_empty_box(arena, 0, 0, 0);
    }

    // Layout text in roman font
    // For now, just layout each character
    int len = strlen(content);
    MathBox** boxes = (MathBox**)arena_alloc(arena, len * sizeof(MathBox*));
    int count = 0;

    for (int i = 0; i < len; i++) {
        char c = content[i];
        if (c == ' ') {
            // Space
            boxes[count++] = make_kern(arena, ctx.font_size() * 0.25f);
        } else {
            boxes[count++] = make_glyph(ctx, c, MathBoxType::Ord, arena);
        }
    }

    MathBox* result = make_hbox(arena, boxes, count, MathBoxType::Ord);
    result->source_node = node;
    return result;
}

// ============================================================================
// Style Layout
// ============================================================================

MathBox* layout_style(Item node, MathContext& ctx, Arena* arena) {
    const char* cmd = get_string_field(node, "cmd");
    Item content = get_item_field(node, "content");

    // Modify context based on style command
    MathContext new_ctx = ctx;

    if (cmd) {
        if (strcmp(cmd, "\\displaystyle") == 0) {
            new_ctx.style = MathStyle::Display;
        } else if (strcmp(cmd, "\\textstyle") == 0) {
            new_ctx.style = MathStyle::Text;
        } else if (strcmp(cmd, "\\scriptstyle") == 0) {
            new_ctx.style = MathStyle::Script;
        } else if (strcmp(cmd, "\\scriptscriptstyle") == 0) {
            new_ctx.style = MathStyle::ScriptScript;
        }
        // Font style commands would modify font_family
    }

    if (content.item == ItemNull.item) {
        return make_empty_box(arena, 0, 0, 0);
    }

    MathBox* result = layout_math(content, new_ctx, arena);
    result->source_node = node;
    return result;
}

// ============================================================================
// Space Layout
// ============================================================================

MathBox* layout_space(Item node, MathContext& ctx, Arena* arena) {
    const char* cmd = get_string_field(node, "cmd");
    float font_size = ctx.font_size();
    float em = font_size;
    float mu = em / 18.0f;

    float space = 0;

    if (cmd) {
        if (strcmp(cmd, "\\,") == 0) space = 3 * mu;       // thin space
        else if (strcmp(cmd, "\\:") == 0) space = 4 * mu;  // medium space
        else if (strcmp(cmd, "\\;") == 0) space = 5 * mu;  // thick space
        else if (strcmp(cmd, "\\!") == 0) space = -3 * mu; // negative thin
        else if (strcmp(cmd, "\\ ") == 0) space = em / 4;  // normal space
        else if (strcmp(cmd, "\\quad") == 0) space = em;
        else if (strcmp(cmd, "\\qquad") == 0) space = 2 * em;
        else if (strcmp(cmd, "\\hspace") == 0) space = em; // would need argument
        else if (strcmp(cmd, "\\enspace") == 0) space = em / 2;
    }

    MathBox* result = make_kern(arena, space);
    result->source_node = node;
    return result;
}

// ============================================================================
// Inter-Box Spacing Application
// ============================================================================

// Helper to get effective box type (handling Lift and Ignore)
static MathBoxType effective_type(MathBox* box) {
    if (!box) return MathBoxType::Ignore;
    if (box->type == MathBoxType::Ignore) return MathBoxType::Ignore;
    if (box->type == MathBoxType::Lift) {
        // Lift: use type of first/last child
        if (box->content_type == MathBoxContentType::HBox && box->content.hbox.count > 0) {
            return effective_type(box->content.hbox.children[0]);
        }
        return MathBoxType::Ord;
    }
    return box->type;
}

static MathBoxType effective_type_right(MathBox* box) {
    if (!box) return MathBoxType::Ignore;
    if (box->type == MathBoxType::Ignore) return MathBoxType::Ignore;
    if (box->type == MathBoxType::Lift) {
        // Lift: use type of last child
        if (box->content_type == MathBoxContentType::HBox && box->content.hbox.count > 0) {
            return effective_type_right(box->content.hbox.children[box->content.hbox.count - 1]);
        }
        return MathBoxType::Ord;
    }
    return box->type;
}

void apply_inter_box_spacing(MathBox* root, MathContext& ctx, Arena* arena) {
    if (!root || root->content_type != MathBoxContentType::HBox) {
        return;
    }

    float em = ctx.font_size();
    float thin = 3.0f / 18.0f * em;
    float med = 4.0f / 18.0f * em;
    float thick = 5.0f / 18.0f * em;

    bool tight = ctx.is_tight();

    // Process children
    int count = root->content.hbox.count;
    if (count < 2) return;

    // First pass: recursively apply spacing to children
    for (int i = 0; i < count; i++) {
        MathBox* child = root->content.hbox.children[i];
        if (child->content_type == MathBoxContentType::HBox) {
            apply_inter_box_spacing(child, ctx, arena);
        }
    }

    // Second pass: insert kerns between adjacent boxes
    // We need to create a new children array with kerns inserted
    int max_new_count = count * 2; // worst case: kern between every pair
    MathBox** new_children = (MathBox**)arena_alloc(arena, max_new_count * sizeof(MathBox*));
    int new_count = 0;

    for (int i = 0; i < count; i++) {
        MathBox* current = root->content.hbox.children[i];

        if (i > 0) {
            MathBox* prev = root->content.hbox.children[i - 1];
            MathBoxType left_type = effective_type_right(prev);
            MathBoxType right_type = effective_type(current);

            int spacing = get_inter_box_spacing(left_type, right_type, tight);
            if (spacing > 0) {
                float amount = (spacing == 3) ? thin :
                               (spacing == 4) ? med : thick;
                new_children[new_count++] = make_kern(arena, amount);
            }
        }

        new_children[new_count++] = current;
    }

    // Update the hbox
    root->content.hbox.children = new_children;
    root->content.hbox.count = new_count;

    // Recalculate width
    float w = 0;
    for (int i = 0; i < new_count; i++) {
        w += new_children[i]->width;
    }
    root->width = w;
}

} // namespace radiant
