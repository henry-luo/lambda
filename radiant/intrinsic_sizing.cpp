/**
 * Unified Intrinsic Sizing Implementation for Radiant Layout Engine
 *
 * This is the SINGLE SOURCE OF TRUTH for min-content and max-content calculations.
 * Table, flex, and grid layouts should ALL use these functions.
 */

#include "layout.hpp"
#include "view.hpp" // For FormDefaults
#include "rdt_video.h"
#include "render.hpp"
#include "../lib/font/font.h"
#include "../lib/utf.h"
#include "../lib/str.h"
#include "../lib/strbuf.h"
#include "../lib/log.h"
#include "../lib/tagged.hpp"
// str.h included via view.hpp
#include <cmath>
#include <cstring>
#include <chrono>

void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

enum IntrinsicZeroWidthChar {
    INTRINSIC_ZERO_WIDTH_NONE,
    INTRINSIC_ZERO_WIDTH_BREAK,
    INTRINSIC_ZERO_WIDTH_NO_BREAK,
    INTRINSIC_ZERO_WIDTH_NON_JOINER,
    INTRINSIC_ZERO_WIDTH_JOINER,
};

static IntrinsicZeroWidthChar intrinsic_zero_width_char(const unsigned char* text,
                                                        size_t offset,
                                                        size_t length) {
    if (offset + 2 >= length) return INTRINSIC_ZERO_WIDTH_NONE;
    if (text[offset] == 0xEF && text[offset + 1] == 0xBB && text[offset + 2] == 0xBF) {
        return INTRINSIC_ZERO_WIDTH_NO_BREAK;
    }
    if (text[offset] != 0xE2 || text[offset + 1] != 0x80) {
        return INTRINSIC_ZERO_WIDTH_NONE;
    }
    switch (text[offset + 2]) {
        case 0x8B: return INTRINSIC_ZERO_WIDTH_BREAK;
        case 0x8C: return INTRINSIC_ZERO_WIDTH_NON_JOINER;
        case 0x8D: return INTRINSIC_ZERO_WIDTH_JOINER;
        default: return INTRINSIC_ZERO_WIDTH_NONE;
    }
}

static bool intrinsic_css_function_is(const CssValue* value, const char* name) {
    return value && value->type == CSS_VALUE_TYPE_FUNCTION &&
        value->data.function && value->data.function->name &&
        strcmp(value->data.function->name, name) == 0;
}

static int intrinsic_count_grid_repeat_tracks(const CssFunction* func,
                                              const char* log_context) {
    if (!func || func->arg_count <= 0 || !func->args[0] ||
        func->args[0]->type != CSS_VALUE_TYPE_NUMBER) {
        return 1;
    }
    int repeat_count = (int)func->args[0]->data.number.value; // INT_CAST_OK: CSS repeat() count is an integer track multiplier.
    int tracks_per_repeat = func->arg_count - 1;
    if (tracks_per_repeat < 1) tracks_per_repeat = 1;
    int expanded = repeat_count * tracks_per_repeat;
    log_debug("[INTRINSIC_GRID_REPEAT] %s repeat(%d, %d tracks) = %d tracks",
              log_context ? log_context : "grid-template",
              repeat_count, tracks_per_repeat, expanded);
    return expanded;
}

static int intrinsic_count_grid_template_tracks(const CssValue* value,
                                                const char* log_context) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) return 0;
    if (intrinsic_css_function_is(value, "repeat")) {
        return intrinsic_count_grid_repeat_tracks(value->data.function, log_context);
    }
    if (value->type != CSS_VALUE_TYPE_LIST) return 1;

    int total_tracks = 0;
    int list_count = value->data.list.count;
    CssValue** list_values = value->data.list.values;
    for (int i = 0; i < list_count; i++) {
        CssValue* item = list_values[i];
        if (!item) continue;
        if (intrinsic_css_function_is(item, "repeat")) {
            total_tracks += intrinsic_count_grid_repeat_tracks(item->data.function, log_context);
        } else {
            total_tracks++;
        }
    }
    return total_tracks > 0 ? total_tracks : list_count;
}

static bool intrinsic_style_keyword_declaration(StyleTree* style, CssPropertyId property,
                                                CssEnum* keyword) {
    if (!style) return false;
    CssDeclaration* decl = style_tree_get_declaration(style, property);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    if (keyword) *keyword = decl->value->data.keyword;
    return true;
}

static bool intrinsic_style_display_keyword(StyleTree* style, CssEnum* keyword) {
    return intrinsic_style_keyword_declaration(style, CSS_PROPERTY_DISPLAY, keyword);
}

static bool intrinsic_specified_display_keyword(DomElement* element, CssEnum* keyword) {
    return element && intrinsic_style_display_keyword(element->specified_style, keyword);
}

static bool intrinsic_element_display_matches(DomElement* element, ViewBlock* view,
                                              CssEnum display_inner,
                                              CssEnum inline_display) {
    if (view && view->display.inner == display_inner) return true;
    CssEnum value = (CssEnum)0;
    if (!intrinsic_specified_display_keyword(element, &value)) return false;
    return value == display_inner || value == inline_display;
}

static bool intrinsic_pseudo_style_is_inline(StyleTree* pseudo_style) {
    if (!pseudo_style || !pseudo_style->tree) return true;
    CssEnum display = (CssEnum)0;
    if (!intrinsic_style_display_keyword(pseudo_style, &display)) return true;
    return display != CSS_VALUE_BLOCK && display != CSS_VALUE_TABLE &&
           display != CSS_VALUE_LIST_ITEM && display != CSS_VALUE_FLEX &&
           display != CSS_VALUE_GRID && display != CSS_VALUE_NONE;
}

static int intrinsic_grid_template_column_count(DomElement* element, ViewBlock* view,
                                                int fallback_count,
                                                const char* log_context,
                                                bool log_embed_count,
                                                bool log_specified_type) {
    int column_count = fallback_count;
    GridProp* grid_prop = (view && view->embed) ? view->embed->grid : nullptr;
    if (grid_prop && grid_prop->grid_template_columns) {
        column_count = grid_prop->grid_template_columns->track_count;
        if (log_embed_count) {
            log_debug("%s: grid %s from embed, cols=%d",
                      log_context ? log_context : "intrinsic_grid_template_column_count",
                      element ? element->node_name() : "<null>", column_count);
        }
    }

    int unresolved_threshold = fallback_count > 0 ? fallback_count : 0;
    if (column_count <= unresolved_threshold && element && element->specified_style) {
        CssDeclaration* cols_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_GRID_TEMPLATE_COLUMNS);
        if (cols_decl && cols_decl->value) {
            if (log_specified_type) {
                log_debug("%s: %s grid-template-columns value type=%d",
                          log_context ? log_context : "intrinsic_grid_template_column_count",
                          element->node_name(), cols_decl->value->type);
            }
            column_count = intrinsic_count_grid_template_tracks(cols_decl->value, log_context);
        }
    }
    return column_count;
}

static bool intrinsic_style_length_declaration(LayoutContext* lycon, StyleTree* style,
                                               CssPropertyId property,
                                               CssPropertyId resolve_property,
                                               float* out_value) {
    if (!style || !out_value) return false;

    CssDeclaration* decl = style_tree_get_declaration(style, property);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return false;

    *out_value = resolve_length_value(lycon, resolve_property, decl->value);
    return true;
}

static bool intrinsic_style_length_declaration_or_fallback(LayoutContext* lycon, StyleTree* style,
                                                           CssPropertyId property,
                                                           CssPropertyId fallback_property,
                                                           CssPropertyId resolve_property,
                                                           float* out_value) {
    if (!style || !out_value) return false;

    CssDeclaration* decl = style_tree_get_declaration(style, property);
    if (!decl) decl = style_tree_get_declaration(style, fallback_property);
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return false;

    *out_value = resolve_length_value(lycon, resolve_property, decl->value);
    return true;
}

static bool intrinsic_style_flex_direction_is_row(StyleTree* style, bool* is_row) {
    CssEnum direction = (CssEnum)0;
    if (!intrinsic_style_keyword_declaration(style, CSS_PROPERTY_FLEX_DIRECTION, &direction)) {
        return false;
    }
    if (is_row) *is_row = (direction == CSS_VALUE_ROW || direction == CSS_VALUE_ROW_REVERSE);
    return true;
}

static bool intrinsic_style_flex_wrap_enabled(StyleTree* style, bool* is_wrapping) {
    CssEnum wrap = (CssEnum)0;
    if (!intrinsic_style_keyword_declaration(style, CSS_PROPERTY_FLEX_WRAP, &wrap)) {
        return false;
    }
    if (is_wrapping) *is_wrapping = (wrap == CSS_VALUE_WRAP || wrap == CSS_VALUE_WRAP_REVERSE);
    return true;
}

static float intrinsic_resolve_font_size_value(LayoutContext* lycon, const CssValue* value,
                                               bool* resolved_from_medium) {
    if (resolved_from_medium) *resolved_from_medium = false;
    if (!lycon || !value) return -1.0f;

    float parent_font_size = (lycon->font.style && lycon->font.style->font_size > 0.0f)
        ? lycon->font.style->font_size : 16.0f;
    bool parent_from_medium = lycon->font.style &&
        lycon->font.style->font_size_from_medium;

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        if (value->data.length.unit == CSS_UNIT_EM) {
            if (resolved_from_medium) *resolved_from_medium = parent_from_medium;
            return (float)value->data.length.value * parent_font_size;
        }
        float font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, value);
        if (resolved_from_medium) *resolved_from_medium = false;
        return font_size;
    }

    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (resolved_from_medium) *resolved_from_medium = parent_from_medium;
        return (float)(value->data.percentage.value / 100.0 * parent_font_size);
    }

    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum kw = value->data.keyword;
        if (resolved_from_medium) *resolved_from_medium = true;
        if (kw == CSS_VALUE_INHERIT) {
            if (resolved_from_medium) *resolved_from_medium = parent_from_medium;
            return parent_font_size;
        }
        if (kw == CSS_VALUE_LARGER || kw == CSS_VALUE_SMALLER) {
            float scale = (kw == CSS_VALUE_LARGER) ? 1.2f : (1.0f / 1.2f);
            return parent_font_size * scale;
        }
        return map_lambda_font_size_keyword(kw);
    }

    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        return value->data.number.value == 0.0 ? 0.0f : -1.0f;
    }

    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        return resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, value);
    }

    return -1.0f;
}

static void intrinsic_apply_monospace_font_size_quirk(FontProp* font,
                                                      FontProp* parent_font) {
    if (!font || !font->family || font->font_size <= 0.0f ||
        !font->font_size_from_medium) {
        return;
    }
    bool current_is_mono =
        str_ieq_const(font->family, strlen(font->family), "monospace");
    bool parent_is_mono = parent_font && parent_font->family &&
        str_ieq_const(parent_font->family, strlen(parent_font->family), "monospace");
    if (!current_is_mono || parent_is_mono) return;

    float original_size = font->font_size;
    font->font_size = original_size * 13.0f / 16.0f;
    font->font_size_from_medium = false;
    log_debug("intrinsic monospace generic-family quirk: %.1f -> %.1f",
              original_size, font->font_size);
}

static bool intrinsic_has_ua_font_defaults(uintptr_t tag) {
    return tag == HTM_TAG_B || tag == HTM_TAG_STRONG || tag == HTM_TAG_TH ||
           (tag >= HTM_TAG_H1 && tag <= HTM_TAG_H6) ||
           tag == HTM_TAG_I || tag == HTM_TAG_EM || tag == HTM_TAG_CITE ||
           tag == HTM_TAG_DFN || tag == HTM_TAG_VAR || tag == HTM_TAG_CODE ||
           tag == HTM_TAG_KBD || tag == HTM_TAG_SAMP || tag == HTM_TAG_TT ||
           tag == HTM_TAG_PRE || tag == HTM_TAG_LISTING || tag == HTM_TAG_XMP;
}

static bool intrinsic_apply_ua_font_defaults(DomElement* element, FontProp* font,
                                             FontProp* parent_font) {
    if (!element || !font || !intrinsic_has_ua_font_defaults(element->tag())) return false;

    StyleTree* style = element->specified_style;
    bool specified_shorthand = style_tree_get_declaration(style, CSS_PROPERTY_FONT) != nullptr;
    bool specified_family = specified_shorthand ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_FAMILY) != nullptr;
    bool specified_size = specified_shorthand ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_SIZE) != nullptr;
    bool specified_weight = specified_shorthand ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_WEIGHT) != nullptr;
    bool specified_style = specified_shorthand ||
        style_tree_get_declaration(style, CSS_PROPERTY_FONT_STYLE) != nullptr;
    bool changed = false;
    uintptr_t tag = element->tag();

    // UA font declarations remain the cascade base when an unrelated author
    // font longhand forces temporary intrinsic-measurement font resolution.
    if ((tag == HTM_TAG_B || tag == HTM_TAG_STRONG || tag == HTM_TAG_TH) &&
        !specified_weight) {
        font->font_weight = CSS_VALUE_BOLD;
        font->font_weight_numeric = 700;
        changed = true;
    } else if (tag >= HTM_TAG_H1 && tag <= HTM_TAG_H6) {
        if (!specified_size && parent_font) {
            static const float heading_scale[] = {2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f};
            font->font_size = parent_font->font_size * heading_scale[tag - HTM_TAG_H1];
            font->font_size_from_medium = false;
            changed = true;
        }
        if (!specified_weight) {
            font->font_weight = CSS_VALUE_BOLD;
            font->font_weight_numeric = 700;
            changed = true;
        }
    } else if ((tag == HTM_TAG_I || tag == HTM_TAG_EM || tag == HTM_TAG_CITE ||
                tag == HTM_TAG_DFN || tag == HTM_TAG_VAR) &&
               !specified_style) {
        font->font_style = CSS_VALUE_ITALIC;
        changed = true;
    } else if ((tag == HTM_TAG_CODE || tag == HTM_TAG_KBD || tag == HTM_TAG_SAMP ||
                tag == HTM_TAG_TT || tag == HTM_TAG_PRE || tag == HTM_TAG_LISTING ||
                tag == HTM_TAG_XMP) && !specified_family) {
        radiant_retain_font_family(font, lam::GcPtr<char>((char*)"monospace"));
        changed = true;
    }
    return changed;
}

static void intrinsic_complete_inherited_font(FontProp* font, FontProp* parent_font) {
    if (!font || !parent_font) return;
    if (!font->family) {
        radiant_retain_font_family(font, lam::PoolPtr<char>(parent_font->family));
    }
    if (font->font_size <= 0.0f) {
        font->font_size = parent_font->font_size;
        font->font_size_from_medium = parent_font->font_size_from_medium;
    }
    if (font->font_weight == 0) {
        font->font_weight = parent_font->font_weight;
        font->font_weight_numeric = parent_font->font_weight_numeric;
    }
    if (font->font_style == 0) font->font_style = parent_font->font_style;
    if (font->font_variant == 0) font->font_variant = parent_font->font_variant;
    if (font->font_kerning == 0) font->font_kerning = parent_font->font_kerning;
}

static bool css_has_horizontal_box_decl(StyleTree* style) {
    if (!style) return false;
    return style_tree_get_declaration(style, CSS_PROPERTY_PADDING) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_LEFT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_PADDING_RIGHT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_WIDTH) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_LEFT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_RIGHT) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_LEFT_WIDTH) ||
           style_tree_get_declaration(style, CSS_PROPERTY_BORDER_RIGHT_WIDTH);
}

static float intrinsic_border_width_from_shorthand_value(LayoutContext* lycon, CssPropertyId property,
                                                         const CssValue* value) {
    if (!value) return 0.0f;

    float border_width = -1.0f;
    bool has_visible_style = false;

    auto consider_keyword = [&](CssEnum kw) {
        if (kw == CSS_VALUE_THIN) border_width = 1.0f;
        else if (kw == CSS_VALUE_MEDIUM) border_width = 3.0f;
        else if (kw == CSS_VALUE_THICK) border_width = 5.0f;
        else if (kw == CSS_VALUE_SOLID || kw == CSS_VALUE_DASHED ||
                 kw == CSS_VALUE_DOTTED || kw == CSS_VALUE_DOUBLE ||
                 kw == CSS_VALUE_GROOVE || kw == CSS_VALUE_RIDGE ||
                 kw == CSS_VALUE_INSET || kw == CSS_VALUE_OUTSET) {
            has_visible_style = true;
        } else if (kw == CSS_VALUE_NONE || kw == CSS_VALUE_HIDDEN) {
            border_width = 0.0f;
            has_visible_style = false;
        }
    };

    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item) continue;
            if (item->type == CSS_VALUE_TYPE_LENGTH || item->type == CSS_VALUE_TYPE_NUMBER) {
                border_width = resolve_length_value(lycon, property, item);
            } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                consider_keyword(item->data.keyword);
            }
        }
    } else if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
        border_width = resolve_length_value(lycon, property, value);
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        consider_keyword(value->data.keyword);
    }

    // CSS Backgrounds and Borders §3: a visible border style with the initial
    // border-width computes to medium when no width is specified.
    if (has_visible_style && border_width < 0.0f) border_width = 3.0f;
    return border_width > 0.0f ? border_width : 0.0f;
}

static float intrinsic_border_width_from_spacing_value(LayoutContext* lycon, CssPropertyId property,
                                                       const CssValue* value, bool right_side) {
    if (!value) return 0.0f;

    const CssValue* side_value = css_box_shorthand_side_value(value, right_side ? 1 : 3);

    return resolve_length_value(lycon, property, side_value);
}

static void get_horizontal_border_widths_from_css(LayoutContext* lycon, DomElement* element,
                                                  float* border_left, float* border_right) {
    if (!element || !element->specified_style || !border_left || !border_right) return;

    float left_width = *border_left;
    float right_width = *border_right;
    int64_t left_priority = -1;
    int64_t right_priority = -1;

    auto apply_width = [&](const CssDeclaration* decl, float width, bool apply_left, bool apply_right) {
        if (!decl || !decl->value) return;
        int64_t priority = get_cascade_priority(decl);
        if (apply_left && priority >= left_priority) {
            left_width = width;
            left_priority = priority;
        }
        if (apply_right && priority >= right_priority) {
            right_width = width;
            right_priority = priority;
        }
    };

    CssDeclaration* border_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER);
    if (border_decl && border_decl->value) {
        float width = intrinsic_border_width_from_shorthand_value(
            lycon, CSS_PROPERTY_BORDER_WIDTH, border_decl->value);
        apply_width(border_decl, width, true, true);
    }

    CssDeclaration* border_width_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER_WIDTH);
    if (border_width_decl && border_width_decl->value) {
        apply_width(border_width_decl,
                    intrinsic_border_width_from_spacing_value(
                        lycon, CSS_PROPERTY_BORDER_WIDTH, border_width_decl->value, false),
                    true, false);
        apply_width(border_width_decl,
                    intrinsic_border_width_from_spacing_value(
                        lycon, CSS_PROPERTY_BORDER_WIDTH, border_width_decl->value, true),
                    false, true);
    }

    CssDeclaration* left_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER_LEFT_WIDTH);
    if (left_decl && left_decl->value) {
        apply_width(left_decl,
                    resolve_length_value(lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, left_decl->value),
                    true, false);
    }
    CssDeclaration* right_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER_RIGHT_WIDTH);
    if (right_decl && right_decl->value) {
        apply_width(right_decl,
                    resolve_length_value(lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, right_decl->value),
                    false, true);
    }

    CssDeclaration* left_shorthand_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER_LEFT);
    if (left_shorthand_decl && left_shorthand_decl->value) {
        apply_width(left_shorthand_decl,
                    intrinsic_border_width_from_shorthand_value(
                        lycon, CSS_PROPERTY_BORDER_LEFT_WIDTH, left_shorthand_decl->value),
                    true, false);
    }
    CssDeclaration* right_shorthand_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_BORDER_RIGHT);
    if (right_shorthand_decl && right_shorthand_decl->value) {
        apply_width(right_shorthand_decl,
                    intrinsic_border_width_from_shorthand_value(
                        lycon, CSS_PROPERTY_BORDER_RIGHT_WIDTH, right_shorthand_decl->value),
                    false, true);
    }

    *border_left = left_width;
    *border_right = right_width;
}

static float intrinsic_resolve_box_length(LayoutContext* lycon, CssPropertyId property,
                                          const CssValue* value, float inline_base) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return inline_base > 0.0f ? (float)(value->data.percentage.value / 100.0) * inline_base : 0.0f;
    }
    return resolve_length_value(lycon, property, value);
}

static void get_horizontal_padding_widths_from_css(LayoutContext* lycon, DomElement* element,
                                                   float inline_base,
                                                   float* padding_left, float* padding_right) {
    if (!element || !element->specified_style || !padding_left || !padding_right) return;

    float left_width = *padding_left;
    float right_width = *padding_right;
    int64_t left_priority = -1;
    int64_t right_priority = -1;

    auto apply_width = [&](const CssDeclaration* decl, float width,
                           bool apply_left, bool apply_right) {
        if (!decl || !decl->value) return;
        int64_t priority = get_cascade_priority(decl);
        // A zero-valued longhand still overrides a lower-priority shorthand.
        if (apply_left && priority >= left_priority) {
            left_width = width;
            left_priority = priority;
        }
        if (apply_right && priority >= right_priority) {
            right_width = width;
            right_priority = priority;
        }
    };

    CssDeclaration* padding_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_PADDING);
    if (padding_decl && padding_decl->value) {
        const CssValue* right_value = css_box_shorthand_side_value(padding_decl->value, 1);
        const CssValue* left_value = css_box_shorthand_side_value(padding_decl->value, 3);
        if (right_value) apply_width(padding_decl, intrinsic_resolve_box_length(
            lycon, CSS_PROPERTY_PADDING_RIGHT, right_value, inline_base), false, true);
        if (left_value) apply_width(padding_decl, intrinsic_resolve_box_length(
            lycon, CSS_PROPERTY_PADDING_LEFT, left_value, inline_base), true, false);
    }

    CssDeclaration* left_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_PADDING_LEFT);
    if (left_decl && left_decl->value) {
        apply_width(left_decl, intrinsic_resolve_box_length(
            lycon, CSS_PROPERTY_PADDING_LEFT, left_decl->value, inline_base), true, false);
    }
    CssDeclaration* right_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_PADDING_RIGHT);
    if (right_decl && right_decl->value) {
        apply_width(right_decl, intrinsic_resolve_box_length(
            lycon, CSS_PROPERTY_PADDING_RIGHT, right_decl->value, inline_base), false, true);
    }

    *padding_left = left_width;
    *padding_right = right_width;
}

static inline bool intrinsic_is_simple_latin_shaping_byte(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

static bool intrinsic_can_shape_simple_latin_run(LayoutContext* lycon,
                                                 CssEnum text_transform,
                                                 CssEnum font_variant,
                                                 bool break_anywhere,
                                                 bool break_word) {
    if (!lycon || !lycon->font.style || !lycon->font.font_handle) return false;
    if (text_transform != CSS_VALUE_NONE && text_transform != 0) return false;
    if (font_variant == CSS_VALUE_SMALL_CAPS) return false;
    if (break_anywhere) return false;
    if (break_word) return false;
    if (lycon->font.style->letter_spacing != 0.0f) return false;
    return true;
}

static bool intrinsic_measure_shaped_simple_latin_run(LayoutContext* lycon,
                                                      const unsigned char* str,
                                                      size_t remaining,
                                                      CssEnum text_transform,
                                                      CssEnum font_variant,
                                                      bool break_anywhere,
                                                      bool break_word,
                                                      size_t* out_bytes,
                                                      float* out_width,
                                                      uint32_t* out_first_codepoint,
                                                      uint32_t* out_last_codepoint) {
    if (!str || remaining == 0 || !out_bytes || !out_width ||
        !out_first_codepoint || !out_last_codepoint) {
        return false;
    }
    if (!intrinsic_can_shape_simple_latin_run(lycon, text_transform,
                                              font_variant, break_anywhere, break_word)) {
        return false;
    }
    if (!intrinsic_is_simple_latin_shaping_byte(*str)) return false;

    size_t run_len = 0;
    while (run_len < remaining && intrinsic_is_simple_latin_shaping_byte(str[run_len])) {
        GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, (uint32_t)str[run_len]);
        if (ginfo.id == 0) return false;
        run_len++;
    }
    if (run_len < 2) return false;

    int byte_len = (int)run_len; // INT_CAST_OK: text byte count
    TextExtents ext = font_measure_text(lycon->font.font_handle, (const char*)str, byte_len);
    if (ext.glyph_count <= 0 && ext.width <= 0.0f) return false;

    *out_bytes = run_len;
    *out_width = ext.width;
    *out_first_codepoint = (uint32_t)*str;
    *out_last_codepoint = (uint32_t)str[run_len - 1];
    return true;
}

static bool text_line_has_tab(const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\t') return true;
    }
    return false;
}

static bool text_node_is_ascii_whitespace(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    if (!text) return true;
    while (*text) {
        unsigned char ch = (unsigned char)*text;
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') {
            return false;
        }
        text++;
    }
    return true;
}

static bool intrinsic_white_space_preserves_space_advance(CssEnum white_space) {
    return white_space == CSS_VALUE_PRE ||
           white_space == CSS_VALUE_PRE_WRAP ||
           white_space == CSS_VALUE_BREAK_SPACES;
}

static bool text_node_has_intrinsic_table_content(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    if (!text || !*text) return false;
    if (!text_node_is_ascii_whitespace(node)) return true;
    return intrinsic_white_space_preserves_space_advance(get_white_space_value(node));
}

static DomNode* previous_non_whitespace_sibling(DomNode* node) {
    DomNode* sibling = node ? node->prev_sibling : nullptr;
    while (sibling && text_node_is_ascii_whitespace(sibling)) {
        sibling = sibling->prev_sibling;
    }
    return sibling;
}

static bool element_has_in_flow_intrinsic_content(DomElement* element) {
    if (!element) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (text_node_has_intrinsic_table_content(child)) return true;
            continue;
        }
        if (!child->is_element()) continue;
        ViewBlock* child_block = lam::view_as_block(child->as_element());
        if (!child_block) return true;
        if (layout_block_is_display_none(child_block) ||
            layout_view_is_abs_or_fixed(child_block)) {
            continue;
        }
        return true;
    }
    return false;
}

typedef struct IntrinsicSpacingCandidate {
    CssDeclaration* decl;
    CssValue* value;
    int64_t priority;
} IntrinsicSpacingCandidate;

static CssValue* intrinsic_pair_side_value(const CssValue* value, bool end_side) {
    if (!value) return nullptr;
    if (value->type != CSS_VALUE_TYPE_LIST) return (CssValue*)value;

    int cnt = value->data.list.count;
    CssValue** vals = value->data.list.values;
    if (cnt <= 0 || !vals) return nullptr;

    int idx = (end_side && cnt >= 2) ? 1 : 0;
    return (idx < cnt) ? vals[idx] : nullptr;
}

static void intrinsic_consider_spacing_candidate(IntrinsicSpacingCandidate* candidate,
                                                 CssDeclaration* decl, CssValue* value) {
    if (!candidate || !decl || !value) return;
    int64_t priority = get_cascade_priority(decl);
    if (!candidate->decl || priority >= candidate->priority) {
        candidate->decl = decl;
        candidate->value = value;
        candidate->priority = priority;
    }
}

static bool intrinsic_resolve_margin_value(LayoutContext* lycon, CssValue* value,
                                           float inline_base, float* out) {
    if (!value || !out) return false;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        if (inline_base < 0.0f) return false;
        *out = (float)(value->data.percentage.value / 100.0) * inline_base;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        *out = 0.0f;
        return true;
    }
    *out = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, value);
    return true;
}

static bool intrinsic_resolve_vertical_margins(LayoutContext* lycon, DomElement* element,
                                               float inline_base, float* mt, float* mb) {
    if (!element || !element->specified_style || !mt || !mb) return false;

    IntrinsicSpacingCandidate top = {};
    IntrinsicSpacingCandidate bottom = {};

    CssDeclaration* margin = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN);
    if (margin && margin->value) {
        intrinsic_consider_spacing_candidate(&top, margin, (CssValue*)css_box_shorthand_side_value(margin->value, 0));
        intrinsic_consider_spacing_candidate(&bottom, margin, (CssValue*)css_box_shorthand_side_value(margin->value, 2));
    }

    CssDeclaration* mt_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN_TOP);
    intrinsic_consider_spacing_candidate(&top, mt_decl, mt_decl ? mt_decl->value : nullptr);
    CssDeclaration* mb_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN_BOTTOM);
    intrinsic_consider_spacing_candidate(&bottom, mb_decl, mb_decl ? mb_decl->value : nullptr);

    CssDeclaration* block = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN_BLOCK);
    if (block && block->value) {
        intrinsic_consider_spacing_candidate(&top, block, intrinsic_pair_side_value(block->value, false));
        intrinsic_consider_spacing_candidate(&bottom, block, intrinsic_pair_side_value(block->value, true));
    }
    CssDeclaration* block_start = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN_BLOCK_START);
    intrinsic_consider_spacing_candidate(&top, block_start, block_start ? block_start->value : nullptr);
    CssDeclaration* block_end = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_MARGIN_BLOCK_END);
    intrinsic_consider_spacing_candidate(&bottom, block_end, block_end ? block_end->value : nullptr);

    bool resolved = false;
    if (top.value && intrinsic_resolve_margin_value(lycon, top.value, inline_base, mt)) {
        resolved = true;
    }
    if (bottom.value && intrinsic_resolve_margin_value(lycon, bottom.value, inline_base, mb)) {
        resolved = true;
    }
    return resolved;
}

static bool intrinsic_element_matches_display(DomElement* elem, CssEnum display_value) {
    if (!elem) return false;
    if (elem->display.inner == display_value || elem->display.outer == display_value) {
        return true;
    }
    CssEnum value = (CssEnum)0;
    return intrinsic_specified_display_keyword(elem, &value) && value == display_value;
}

static bool node_is_table_cell_like(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->view_type == RDT_VIEW_TABLE_CELL ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE_CELL);
}

static bool intrinsic_is_table_box(DomElement* elem) {
    if (!elem) return false;
    return elem->tag() == HTM_TAG_TABLE ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE) ||
           intrinsic_element_matches_display(elem, CSS_VALUE_INLINE_TABLE);
}

static bool intrinsic_is_table_row_group_box(DomElement* elem) {
    if (!elem) return false;
    uintptr_t tag = elem->tag();
    return tag == HTM_TAG_TBODY || tag == HTM_TAG_THEAD || tag == HTM_TAG_TFOOT ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE_ROW_GROUP) ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE_HEADER_GROUP) ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE_FOOTER_GROUP);
}

static bool intrinsic_is_table_row_box(DomElement* elem) {
    if (!elem) return false;
    return elem->tag() == HTM_TAG_TR ||
           intrinsic_element_matches_display(elem, CSS_VALUE_TABLE_ROW);
}

static bool intrinsic_should_skip_height_child(DomElement* elem) {
    if (!elem) return true;
    if (layout_element_is_display_none(elem)) {
        return true;
    }
    ViewBlock* block = lam::view_as_block(elem);
    if (block &&
        (layout_block_is_out_of_flow_positioned(block) ||
         (block->position && element_has_float(block)))) {
        return true;
    }
    return false;
}

static float intrinsic_table_structure_height(LayoutContext* lycon, DomElement* elem, float width) {
    if (!elem) return 0.0f;

    if (intrinsic_is_table_row_box(elem)) {
        float row_height = 0.0f;
        for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;
            DomElement* child_elem = child->as_element();
            if (intrinsic_should_skip_height_child(child_elem)) continue;
            float child_height = calculate_max_content_height(lycon, child, width);
            if (child_height > row_height) row_height = child_height;
        }
        return row_height;
    }

    float total_height = 0.0f;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (intrinsic_should_skip_height_child(child_elem)) continue;

        // Table row groups stack rows, but each row's height is the maximum of
        // its cells; generic block stacking would double-count sibling cells.
        if (intrinsic_is_table_row_box(child_elem) ||
            intrinsic_is_table_row_group_box(child_elem) ||
            intrinsic_is_table_box(child_elem)) {
            total_height += intrinsic_table_structure_height(lycon, child_elem, width);
        } else {
            total_height += calculate_max_content_height(lycon, child, width);
        }
    }
    return total_height;
}

static bool intrinsic_text_ends_with_whitespace(const char* text) {
    if (!text) return false;
    size_t len = strlen(text);
    if (len == 0) return false;
    unsigned char ch = (unsigned char)text[len - 1];
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static bool intrinsic_text_starts_with_whitespace(const char* text) {
    if (!text || !*text) return false;
    unsigned char ch = (unsigned char)text[0];
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static bool intrinsic_element_text_ends_with_whitespace(DomNode* element) {
    if (!element || !element->is_element()) return false;

    DomNode* node = element->as_element()->last_child;
    while (node) {
        if (node->is_text()) {
            return intrinsic_text_ends_with_whitespace((const char*)node->text_data());
        }
        if (node->is_element() && node->as_element()->last_child) {
            node = node->as_element()->last_child;
        } else {
            return false;
        }
    }
    return false;
}

static bool intrinsic_element_text_starts_with_whitespace(DomNode* element) {
    if (!element || !element->is_element()) return false;

    DomNode* node = element->as_element()->first_child;
    while (node) {
        if (node->is_text()) {
            return intrinsic_text_starts_with_whitespace((const char*)node->text_data());
        }
        if (node->is_element() && node->as_element()->first_child) {
            node = node->as_element()->first_child;
        } else {
            return false;
        }
    }
    return false;
}

static float measure_preserved_line_width_with_tabs(LayoutContext* lycon, const char* text,
                                                    size_t length, float start_offset,
                                                    CssEnum text_transform, CssEnum font_variant,
                                                    float tab_size) {
    if (!text || length == 0) return 0.0f;

    float width = 0.0f;
    bool is_word_start = true;
    const unsigned char* str = (const unsigned char*)text;
    size_t i = 0;

    while (i < length) {
        unsigned char ch = str[i];
        if (ch == '\t') {
            float raw_space = layout_measure_space_advance(
                lycon, lycon->font.font_handle, lycon->font.style);
            float tab_period = raw_space * tab_size;
            if (tab_period > 0.0f) {
                float current_x = start_offset + width;
                float half_ch = raw_space * 0.5f;
                float next_tab = tab_period * ceilf((current_x + half_ch) / tab_period);
                width += next_tab - current_x;
            }
            is_word_start = true;
            i++;
            continue;
        }

        if (ch == ' ' || ch == '\r' || ch == '\n') {
            float space_width = layout_measure_space_advance(
                lycon, lycon->font.font_handle, lycon->font.style);
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
                space_width += lycon->font.style->letter_spacing;
            }
            width += space_width;
            is_word_start = true;
            i++;
            continue;
        }

        uint32_t codepoint = ch;
        int bytes = 1;
        if (codepoint >= 128) {
            bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
            if (bytes <= 0) {
                width += lycon->font.style ? lycon->font.style->font_size * 0.5f : 8.0f;
                i++;
                is_word_start = false;
                continue;
            }
        }

        uint32_t tt_out[3];
        int tt_count = apply_text_transform_full(codepoint, text_transform, is_word_start, tt_out);
        codepoint = tt_out[0];
        bool is_small_caps_lower = false;
        if (font_variant == CSS_VALUE_SMALL_CAPS) {
            uint32_t original = codepoint;
            codepoint = apply_text_transform(codepoint, CSS_VALUE_UPPERCASE, false);
            is_small_caps_lower = (codepoint != original);
        }
        is_word_start = false;

        FontStyleDesc sd = font_style_desc_from_prop(lycon->font.style);
        float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0)
            ? lycon->ui_context->pixel_ratio : 1.0f;
        for (int ti = 0; ti < tt_count; ti++) {
            uint32_t cp = (ti == 0) ? codepoint : tt_out[ti];
            if (cp == 0 || text_codepoint_has_zero_advance(cp)) {
                continue;
            }
            LoadedGlyph* glyph = font_load_glyph(lycon->font.font_handle, &sd, cp, false);
            float advance = glyph ? (glyph->advance_x / pixel_ratio)
                                  : (lycon->font.style ? lycon->font.style->font_size * 0.5f : 8.0f);
            if (is_small_caps_lower) advance *= 0.7f;
            if (lycon->font.style) advance += lycon->font.style->letter_spacing;
            width += advance;
        }
        i += (size_t)bytes;
    }

    return width;
}

// ============================================================================
// Text Measurement (Core Implementation)
// ============================================================================

static inline bool is_emoji_for_zwj(uint32_t cp) {
    return utf_is_emoji_for_zwj(cp);
}

static inline bool is_zwj_composition_base(uint32_t cp) {
    return utf_is_zwj_composition_base(cp);
}

static inline bool intrinsic_allows_soft_wrap_after_codepoint(uint32_t cp) {
    // CSS text wrapping follows Unicode line breaking. Hyphen characters create
    // a soft wrap opportunity after the hyphen while keeping the hyphen on the
    // preceding line.
    return cp == 0x002D || cp == 0x2010;
}

static float intrinsic_loaded_glyph_advance(LayoutContext* lycon,
                                            uint32_t codepoint,
                                            bool small_caps_lower,
                                            float kerning,
                                            bool* loaded) {
    FontStyleDesc style = font_style_desc_from_prop(lycon->font.style);
    // Intrinsic sizing must use the current FontStyleDesc because font_get_glyph()
    // can retain stale advances after a dynamic @font-face load.
    LoadedGlyph* glyph = font_load_glyph(
        lycon->font.font_handle, &style, codepoint, false);
    *loaded = glyph != nullptr;
    if (!glyph) return 0.0f;

    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0)
        ? lycon->ui_context->pixel_ratio : 1.0f;
    float advance = glyph->advance_x / pixel_ratio + kerning;
    if (small_caps_lower) advance *= 0.7f;
    if (lycon->font.style) advance += lycon->font.style->letter_spacing;
    return advance;
}

static float intrinsic_apply_full_text_transform(LayoutContext* lycon,
                                                 uint32_t* codepoint,
                                                 CssEnum text_transform,
                                                 bool is_word_start) {
    if (text_transform == CSS_VALUE_NONE || text_transform == 0) return 0.0f;

    uint32_t transformed[3];
    int count = apply_text_transform_full(
        *codepoint, text_transform, is_word_start, transformed);
    *codepoint = transformed[0];

    float extra_advance = 0.0f;
    for (int index = 1; index < count; index++) {
        if (text_codepoint_has_zero_advance(transformed[index])) continue;
        bool loaded = false;
        extra_advance += intrinsic_loaded_glyph_advance(
            lycon, transformed[index], false, 0.0f, &loaded);
    }
    return extra_advance;
}

TextIntrinsicWidths measure_text_intrinsic_widths(LayoutContext* lycon,
                                                   const char* text,
                                                   size_t length,
                                                   CssEnum text_transform,
                                                   CssEnum font_variant,
                                                   CssEnum white_space,
                                                   CssEnum overflow_wrap,
                                                   CssEnum word_break) {
    // CSS Text 3 §3.4: overflow-wrap: anywhere introduces soft wrap opportunities
    // at every typographic character unit for min-content sizing.
    bool break_anywhere = (overflow_wrap == CSS_VALUE_ANYWHERE);
    bool break_word = (overflow_wrap == CSS_VALUE_BREAK_WORD);
    // CSS Text 3 §5.2: CJK ideographic characters (UAX#14 ID class) have implicit
    // break opportunities between each pair under word-break: normal.
    // word-break: keep-all suppresses these breaks.
    bool cjk_breaks = (word_break != CSS_VALUE_KEEP_ALL);
    TextIntrinsicWidths result = {0, 0};

    if (!text || length == 0) {
        return result;
    }

    // if font-size is 0, text has no size
    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        return result;  // min_content=0, max_content=0
    }

    // Check if we have a valid font face
    if (!lycon->font.font_handle) {
        // Fallback: rough estimate without font metrics
        // Use ~8px per character for max, find longest word for min
        float total_width = 0.0f;
        float current_word = 0.0f;
        float longest_word = 0.0f;

        for (size_t i = 0; i < length; i++) {
            unsigned char ch = (unsigned char)text[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                if (white_space == CSS_VALUE_BREAK_SPACES) {
                    // CSS Text 3 §4.2.2: break-spaces — spaces are not hangable,
                    // contribute to min-content. Break opportunity is AFTER the space.
                    current_word += 4.0f;
                    longest_word = fmax(longest_word, current_word);
                    current_word = 0.0f;
                } else {
                    longest_word = fmax(longest_word, current_word);
                    current_word = 0.0f;
                }
                total_width += 4.0f;  // Space width estimate
            } else {
                current_word += 11.0f;  // Use 11.0 to match font fallback width
                total_width += 11.0f;
                if (break_anywhere || intrinsic_allows_soft_wrap_after_codepoint((uint32_t)ch)) {
                    longest_word = fmax(longest_word, current_word);
                    current_word = 0.0f;
                }
            }
        }
        longest_word = fmax(longest_word, current_word);

        result.min_content = longest_word;
        result.max_content = total_width;
        return result;
    }

    float total_width = 0.0f;
    float current_word = 0.0f;
    float longest_word = 0.0f;

    uint32_t prev_codepoint = 0;
    const FontMetrics* fm = lycon->font.font_handle ? font_get_metrics(lycon->font.font_handle) : NULL;
    bool has_kerning = fm ? fm->has_kerning : false;
    if (lycon->font.style && lycon->font.style->font_kerning == CSS_VALUE_NONE) has_kerning = false;
    const unsigned char* str = (const unsigned char*)text;
    bool is_word_start = true;  // for text-transform: capitalize
    bool after_zwj = false;     // track ZWJ for emoji sequence composition
    bool prev_is_zwj_base = false;  // track if previous char is a ZWJ composition base

    for (size_t i = 0; i < length; ) {
        unsigned char ch = str[i];
        IntrinsicZeroWidthChar zero_width = intrinsic_zero_width_char(str, i, length);

        if (zero_width == INTRINSIC_ZERO_WIDTH_BREAK) {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
            is_word_start = true;
            i += 3;
            continue;
        }
        if (zero_width != INTRINSIC_ZERO_WIDTH_NONE) {
            if (zero_width == INTRINSIC_ZERO_WIDTH_JOINER && prev_is_zwj_base) after_zwj = true;
            i += 3;
            continue;
        }

        // Word boundary detection (whitespace breaks words)
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            // Apply kerning between prev character and space (matching layout_text.cpp)
            float kerning_adj = 0.0f;
            if (has_kerning && prev_codepoint) {
                kerning_adj = font_get_kerning(lycon->font.font_handle, prev_codepoint, (uint32_t)ch);
            }

            float space_width = layout_measure_space_advance(
                lycon, lycon->font.font_handle, lycon->font.style);
            // Apply word-spacing to space characters (matching layout_text.cpp)
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
            }
            // Apply letter-spacing to spaces as well (matching layout_text.cpp)
            // CSS 2.1 §16.4: letter-spacing is added after every character
            // Browsers include trailing letter-spacing in measured width
            if (lycon->font.style) {
                space_width += lycon->font.style->letter_spacing;
            }

            if (white_space == CSS_VALUE_BREAK_SPACES) {
                // CSS Text 3 §4.2.2: break-spaces — preserved spaces are not hangable
                // and DO contribute to min-content width. Break opportunity is AFTER
                // each preserved space, so include space in the preceding segment.
                current_word += kerning_adj + space_width;
                longest_word = fmax(longest_word, current_word);
                current_word = 0.0f;
            } else {
                longest_word = fmax(longest_word, current_word);
                current_word = 0.0f;
            }

            total_width += kerning_adj + space_width;

            // Keep tracking codepoint for kerning continuity (layout_text.cpp doesn't reset)
            prev_codepoint = (uint32_t)ch;
            prev_is_zwj_base = false;
            is_word_start = true;  // Next character starts a new word
            i++;
            continue;
        }

        {
            size_t shaped_bytes = 0;
            float shaped_width = 0.0f;
            uint32_t shaped_first_cp = 0;
            uint32_t shaped_last_cp = 0;
            if (intrinsic_measure_shaped_simple_latin_run(
                    lycon, &str[i], length - i, text_transform, font_variant,
                    break_anywhere, break_word, &shaped_bytes, &shaped_width,
                    &shaped_first_cp, &shaped_last_cp)) {
                float kerning = 0.0f;
                if (has_kerning && prev_codepoint) {
                    kerning = font_get_kerning(lycon->font.font_handle,
                                               prev_codepoint, shaped_first_cp);
                }
                float advance = shaped_width + kerning;
                current_word += advance;
                total_width += advance;
                prev_codepoint = shaped_last_cp;
                prev_is_zwj_base = false;
                is_word_start = false;
                i += shaped_bytes;
                continue;
            }
        }

        // Decode UTF-8 codepoint
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
        if (bytes <= 0) {
            // Invalid UTF-8, skip byte - use 11.0 to match font fallback
            current_word += 11.0f;
            total_width += 11.0f;
            i++;
            is_word_start = false;
            if (break_anywhere) {
                longest_word = fmax(longest_word, current_word);
                current_word = 0.0f;
            }
            continue;
        }

        // Apply text-transform if specified (full case mapping: 1→many)
        float transform_extra = intrinsic_apply_full_text_transform(
            lycon, &codepoint, text_transform, is_word_start);
        current_word += transform_extra;
        total_width += transform_extra;

        // Emoji combining marks: skin tone modifiers, variation selectors, and
        // combining enclosing keycap have zero advance width in composed sequences.
        // (Unicode UAX #29 grapheme clusters, CSS Text 3)
        if ((codepoint >= 0x1F3FB && codepoint <= 0x1F3FF) ||  // skin tone modifiers
            codepoint == 0xFE0E || codepoint == 0xFE0F ||       // variation selectors
            codepoint == 0x20E3) {                               // combining keycap
            i += bytes;
            continue;
        }

        // Character after ZWJ: only suppress advance for emoji codepoints that
        // form composed glyphs in ZWJ sequences. Other scripts (CJK, Latin, etc.)
        // keep their independent advance widths. (Unicode UTS #51)
        if (after_zwj) {
            after_zwj = false;
            if (is_emoji_for_zwj(codepoint)) {
                prev_is_zwj_base = is_zwj_composition_base(codepoint);
                i += bytes;
                continue;
            }
        }

        prev_is_zwj_base = is_zwj_composition_base(codepoint);

        if (text_codepoint_has_zero_advance(codepoint)) {
            i += bytes;
            continue;
        }

        // CSS font-variant: small-caps — convert lowercase to uppercase glyphs
        // rendered at ~0.7× size (CSS 2.1 §15.8, matching layout_text.cpp)
        bool is_small_caps_lower = false;
        if (font_variant == CSS_VALUE_SMALL_CAPS) {
            uint32_t original = codepoint;
            codepoint = apply_text_transform(codepoint, CSS_VALUE_UPPERCASE, false);
            is_small_caps_lower = (codepoint != original);
        }

        is_word_start = false;  // No longer at word start after first character

        // Get glyph index for the (possibly transformed) codepoint.
        uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);

        // Kerning is only available from the primary font, matching the former
        // fallback branch's behavior.
        float kerning = 0.0f;
        if (glyph_index && has_kerning && prev_codepoint) {
            kerning = font_get_kerning(lycon->font.font_handle, prev_codepoint, codepoint);
        }

        bool glyph_loaded = false;
        float advance = intrinsic_loaded_glyph_advance(
            lycon, codepoint, is_small_caps_lower, kerning, &glyph_loaded);
        if (!glyph_loaded) advance = 11.0f;
        if (glyph_loaded && codepoint == 0x00A0 && lycon->font.style) {
            advance += lycon->font.style->word_spacing;
            is_word_start = true;
        }
        current_word += advance;
        total_width += advance;

        if (glyph_index) prev_codepoint = codepoint;
        i += bytes;  // Advance by the number of bytes consumed

        // overflow-wrap: anywhere — every character is a break opportunity
        if (break_anywhere || intrinsic_allows_soft_wrap_after_codepoint(codepoint)) {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
        }
        // CSS Text 3 §5.2: CJK ideographic characters (UAX#14 ID class)
        // have break opportunities before and after them.
        else if (cjk_breaks && has_id_line_break_class(codepoint)) {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
        }
    }

    // Don't forget the last word
    longest_word = fmax(longest_word, current_word);

    result.min_content = longest_word;   // Keep float precision
    result.max_content = total_width;    // Keep float precision

    log_debug("measure_text_intrinsic_widths: len=%zu, min=%.2f, max=%.2f, text_transform=%d",
              length, result.min_content, result.max_content, (int)text_transform);

    return result;
}

float measure_direct_text_children_intrinsic_width(LayoutContext* lycon,
                                                   DomElement* element,
                                                   bool use_min_content,
                                                   CssEnum text_transform) {
    float max_width = 0.0f;
    for (DomNode* child = element ? element->first_child : nullptr;
         child; child = child->next_sibling) {
        DomText* text = child->as_text();
        if (!text || !text->text || text->length == 0) continue;
        TextIntrinsicWidths widths = measure_text_intrinsic_widths(
            lycon, text->text, text->length, text_transform);
        float width = use_min_content ? widths.min_content : widths.max_content;
        if (width > max_width) max_width = width;
    }
    return max_width;
}

static TextIntrinsicWidths intrinsic_measure_pseudo_text_widths(LayoutContext* lycon,
                                                                StyleTree* pseudo_style,
                                                                const char* content) {
    TextIntrinsicWidths widths = {0, 0};
    if (content && *content) {
        widths = measure_text_intrinsic_widths(lycon, content, strlen(content));
    }
    CssDeclaration* width_decl = pseudo_style
        ? style_tree_get_declaration(pseudo_style, CSS_PROPERTY_WIDTH) : nullptr;
    if (width_decl && width_decl->value && width_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        float explicit_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, width_decl->value);
        if (!isnan(explicit_width) && explicit_width >= 0) {
            widths.max_content = fmax(widths.max_content, explicit_width);
        }
    }
    return widths;
}

static bool intrinsic_pseudo_border_width_pair(LayoutContext* lycon, StyleTree* pseudo_style,
                                               float* border_l, float* border_r) {
    if (!pseudo_style || !border_l || !border_r) return false;

    CssDeclaration* border_width_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_BORDER_WIDTH);
    if (!border_width_decl || !border_width_decl->value) return false;

    const CssValue* right_value = css_box_shorthand_side_value(border_width_decl->value, 1);
    const CssValue* left_value = css_box_shorthand_side_value(border_width_decl->value, 3);
    if (!right_value || !left_value) return false;

    *border_r = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, right_value);
    *border_l = resolve_length_value(lycon, CSS_PROPERTY_BORDER_WIDTH, left_value);
    return true;
}

static bool intrinsic_pseudo_positive_length_decl(LayoutContext* lycon, StyleTree* pseudo_style,
                                                  CssPropertyId property, float* out_value,
                                                  bool require_length_value) {
    if (!pseudo_style || !out_value) return false;

    CssDeclaration* decl = style_tree_get_declaration(pseudo_style, property);
    if (!decl || !decl->value) return false;
    if (require_length_value && decl->value->type != CSS_VALUE_TYPE_LENGTH) return false;

    float resolved = resolve_length_value(lycon, property, decl->value);
    if (isnan(resolved) || resolved <= 0.0f) return false;

    *out_value = resolved;
    return true;
}

static float intrinsic_pseudo_border_side_shorthand(LayoutContext* lycon,
                                                    StyleTree* pseudo_style,
                                                    CssPropertyId side_property,
                                                    CssPropertyId side_width_property) {
    if (!pseudo_style) return 0.0f;

    CssDeclaration* side_decl = style_tree_get_declaration(pseudo_style, side_property);
    if (!side_decl || !side_decl->value) return 0.0f;

    const CssValue* value = side_decl->value;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        float resolved = resolve_length_value(lycon, side_width_property, value);
        return !isnan(resolved) && resolved > 0.0f ? resolved : 0.0f;
    }
    if (value->type != CSS_VALUE_TYPE_LIST) return 0.0f;

    for (int i = 0; i < value->data.list.count; i++) {
        CssValue* item = value->data.list.values[i];
        if (!item || item->type != CSS_VALUE_TYPE_LENGTH) continue;
        float resolved = resolve_length_value(lycon, side_width_property, item);
        if (!isnan(resolved) && resolved > 0.0f) return resolved;
    }
    return 0.0f;
}

static void intrinsic_pseudo_horizontal_border_widths(LayoutContext* lycon, StyleTree* pseudo_style,
                                                      float* border_l, float* border_r) {
    if (!border_l || !border_r) return;

    *border_l = 0.0f;
    *border_r = 0.0f;

    // Pseudo intrinsic widths intentionally preserve this legacy cascade probe:
    // `border-width` suppresses width longhands only when both horizontal sides exist.
    bool border_width_found = intrinsic_pseudo_border_width_pair(
        lycon, pseudo_style, border_l, border_r);
    if (!border_width_found) {
        intrinsic_pseudo_positive_length_decl(
            lycon, pseudo_style, CSS_PROPERTY_BORDER_LEFT_WIDTH, border_l, true);
        intrinsic_pseudo_positive_length_decl(
            lycon, pseudo_style, CSS_PROPERTY_BORDER_RIGHT_WIDTH, border_r, true);
    }

    if (*border_l == 0.0f) {
        *border_l = intrinsic_pseudo_border_side_shorthand(
            lycon, pseudo_style, CSS_PROPERTY_BORDER_LEFT, CSS_PROPERTY_BORDER_LEFT_WIDTH);
    }
    if (*border_r == 0.0f) {
        *border_r = intrinsic_pseudo_border_side_shorthand(
            lycon, pseudo_style, CSS_PROPERTY_BORDER_RIGHT, CSS_PROPERTY_BORDER_RIGHT_WIDTH);
    }
}

// ============================================================================
// Text Height at Constrained Width (CSS Flexbox §9.4)
// ============================================================================
// Simulates line breaking at a given available width to compute the resulting
// text height. This is needed for hypothetical cross size determination when
// the item's main size (width) is smaller than the text's max-content width.
//
// Algorithm: Walk through text tracking break-unit (word/ZWSP-segment) widths,
// pack them into lines greedily at the available width, count lines.
float compute_text_height_at_width(LayoutContext* lycon,
                                    const char* text,
                                    size_t length,
                                    float available_width,
                                    float line_height,
                                    CssEnum text_transform,
                                    CssEnum font_variant) {
    if (!text || length == 0 || available_width <= 0 || line_height <= 0) {
        return line_height;  // at least one line
    }

    int line_count = 1;
    float current_line_width = 0;
    float current_word_width = 0;

    // Use font metrics if available, otherwise rough estimate
    bool has_font = lycon->font.font_handle != nullptr;
    const FontMetrics* fm2 = has_font ? font_get_metrics(lycon->font.font_handle) : NULL;
    bool has_kerning = fm2 ? fm2->has_kerning : false;
    if (lycon->font.style && lycon->font.style->font_kerning == CSS_VALUE_NONE) has_kerning = false;
    uint32_t prev_codepoint = 0;
    bool is_word_start = true;

    const unsigned char* str = (const unsigned char*)text;

    for (size_t i = 0; i < length; ) {
        unsigned char ch = str[i];
        IntrinsicZeroWidthChar zero_width = intrinsic_zero_width_char(str, i, length);

        if (zero_width == INTRINSIC_ZERO_WIDTH_BREAK) {
            // end of break unit - try to fit on current line
            if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
                line_count++;
                current_line_width = current_word_width;
            } else {
                current_line_width += current_word_width;
            }
            current_word_width = 0;
            is_word_start = true;
            i += 3;
            continue;
        }
        if (zero_width != INTRINSIC_ZERO_WIDTH_NONE) {
            i += 3; continue;
        }

        // Regular space/whitespace - break opportunity
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
                line_count++;
                current_line_width = current_word_width;
            } else {
                current_line_width += current_word_width;
            }
            current_word_width = 0;
            // add space width
            float space_width = layout_measure_space_advance(
                lycon, lycon->font.font_handle, lycon->font.style);
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
            }
            current_line_width += space_width;
            prev_codepoint = (uint32_t)ch;
            is_word_start = true;
            i++;
            continue;
        }

        // Decode UTF-8 codepoint and measure glyph
        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
        if (bytes <= 0) {
            current_word_width += has_font ? 11.0f : 11.0f;
            i++;
            is_word_start = false;
            continue;
        }

        current_word_width += intrinsic_apply_full_text_transform(
            lycon, &codepoint, text_transform, is_word_start);

        // CSS font-variant: small-caps scaling (matching measure_text_intrinsic_widths)
        bool is_small_caps_lower = false;
        if (font_variant == CSS_VALUE_SMALL_CAPS) {
            uint32_t original = codepoint;
            codepoint = apply_text_transform(codepoint, CSS_VALUE_UPPERCASE, false);
            is_small_caps_lower = (codepoint != original);
        }

        is_word_start = false;

        float advance = 0;
        if (text_codepoint_has_zero_advance(codepoint)) {
            i += bytes;
            continue;
        }
        if (has_font) {
            uint32_t glyph_index = font_get_glyph_index(lycon->font.font_handle, codepoint);
            if (glyph_index) {
                float kerning = 0;
                if (has_kerning && prev_codepoint) {
                    kerning = font_get_kerning(lycon->font.font_handle, prev_codepoint, codepoint);
                }
                GlyphInfo ginfo = font_get_glyph(lycon->font.font_handle, codepoint);
                float sc_scale = is_small_caps_lower ? 0.7f : 1.0f;
                advance = (ginfo.id != 0) ? ginfo.advance_x * sc_scale + kerning : 11.0f;
            } else {
                FontStyleDesc _sd = font_style_desc_from_prop(lycon->font.style);
                LoadedGlyph* glyph = font_load_glyph(lycon->font.font_handle, &_sd, codepoint, false);
                if (glyph) {
                    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0) ? lycon->ui_context->pixel_ratio : 1.0f;
                    advance = glyph->advance_x / pixel_ratio;
                    if (is_small_caps_lower) advance *= 0.7f;
                } else {
                    advance = 11.0f;
                }
            }
            // letter-spacing after every character (CSS 2.1 §16.4, matching layout_text.cpp)
            if (lycon->font.style) {
                advance += lycon->font.style->letter_spacing;
            }
            prev_codepoint = codepoint;
        } else {
            advance = 11.0f;
        }

        current_word_width += advance;
        i += bytes;
    }

    // flush last word
    if (current_word_width > 0) {
        if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
            line_count++;
        }
    }

    float result = line_count * line_height;
    log_debug("compute_text_height_at_width: available=%.1f, line_height=%.1f, lines=%d, height=%.1f",
              available_width, line_height, line_count, result);
    return result;
}

// ============================================================================
// Element Measurement (Recursive)
// ============================================================================

// Helper to check if an element has inline-level display from CSS
static bool is_inline_level_element(DomElement* element) {
    if (!element) return false;

    // Anonymous inline-table wrappers store resolved display on the DOM element
    // before a view box exists, so intrinsic sizing must read it directly.
    if (element->display.outer == CSS_VALUE_INLINE ||
        element->display.outer == CSS_VALUE_INLINE_BLOCK) {
        return true;
    }

    // First check if the view has been styled. Inline-block and inline-table
    // are inline-level boxes for intrinsic inline-run accumulation.
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (view->display.outer == CSS_VALUE_INLINE ||
        view->display.outer == CSS_VALUE_INLINE_BLOCK) {
        return true;
    }

    // Fall back to checking specified CSS style
    CssEnum display_value = (CssEnum)0;
    if (intrinsic_specified_display_keyword(element, &display_value)) {
        // Check for inline display values
        if (display_value == CSS_VALUE_INLINE ||
            display_value == CSS_VALUE_INLINE_BLOCK ||
            display_value == CSS_VALUE_INLINE_FLEX ||
            display_value == CSS_VALUE_INLINE_GRID ||
            display_value == CSS_VALUE_INLINE_TABLE) {
            return true;
        }
        // Explicit block display
        if (display_value == CSS_VALUE_BLOCK ||
            display_value == CSS_VALUE_FLEX ||
            display_value == CSS_VALUE_GRID ||
            display_value == CSS_VALUE_TABLE) {
            return false;
        }
    }

    // Fall back to HTML default display for common inline elements
    // These elements are inline by default in HTML
    const char* tag = element->node_name();
    if (tag) {
        if (strcmp(tag, "a") == 0 ||
            strcmp(tag, "span") == 0 ||
            strcmp(tag, "em") == 0 ||
            strcmp(tag, "strong") == 0 ||
            strcmp(tag, "b") == 0 ||
            strcmp(tag, "i") == 0 ||
            strcmp(tag, "u") == 0 ||
            strcmp(tag, "s") == 0 ||
            strcmp(tag, "small") == 0 ||
            strcmp(tag, "big") == 0 ||
            strcmp(tag, "sub") == 0 ||
            strcmp(tag, "sup") == 0 ||
            strcmp(tag, "code") == 0 ||
            strcmp(tag, "tt") == 0 ||
            strcmp(tag, "kbd") == 0 ||
            strcmp(tag, "samp") == 0 ||
            strcmp(tag, "var") == 0 ||
            strcmp(tag, "cite") == 0 ||
            strcmp(tag, "abbr") == 0 ||
            strcmp(tag, "acronym") == 0 ||
            strcmp(tag, "dfn") == 0 ||
            strcmp(tag, "q") == 0 ||
            strcmp(tag, "br") == 0 ||
            strcmp(tag, "time") == 0 ||
            strcmp(tag, "mark") == 0 ||
            strcmp(tag, "label") == 0 ||
            strcmp(tag, "img") == 0 ||
            strcmp(tag, "video") == 0 ||
            strcmp(tag, "audio") == 0 ||
            strcmp(tag, "canvas") == 0 ||
            strcmp(tag, "iframe") == 0 ||
            strcmp(tag, "embed") == 0 ||
            strcmp(tag, "object") == 0 ||
            strcmp(tag, "svg") == 0 ||
            strcmp(tag, "meter") == 0 ||
            strcmp(tag, "progress") == 0 ||
            strcmp(tag, "button") == 0 ||
            strcmp(tag, "input") == 0 ||
            strcmp(tag, "select") == 0 ||
            strcmp(tag, "textarea") == 0) {
            return true;
        }
    }
    return false;
}

static bool intrinsic_white_space_collapses_space_advance(CssEnum white_space) {
    return !intrinsic_white_space_preserves_space_advance(white_space);
}

static bool intrinsic_node_is_collapsible_space_only(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        return text_node_is_ascii_whitespace(node) &&
            intrinsic_white_space_collapses_space_advance(get_white_space_value(node));
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    if (!is_inline_level_element(element)) return false;
    bool saw_child = false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_comment()) continue;
        if (!intrinsic_node_is_collapsible_space_only(child)) return false;
        saw_child = true;
    }
    return saw_child;
}

static bool intrinsic_node_has_inline_boundary_content(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        if (!text_node_is_ascii_whitespace(node)) return true;
        return intrinsic_white_space_preserves_space_advance(get_white_space_value(node));
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    // Anonymous table repair contributes atomic inline-table boxes; whitespace
    // before them must survive max-content sizing just like before text.
    if (node_is_table_cell_like(node) || element->display.inner == CSS_VALUE_TABLE) return true;
    if (!is_inline_level_element(element)) return false;
    for (int pseudo = 1; pseudo <= 2; pseudo++) {
        bool has_content = pseudo == 1 ? dom_element_has_before_content(element)
                                       : dom_element_has_after_content(element);
        StyleTree* style = pseudo == 1 ? element->before_styles : element->after_styles;
        if (!has_content || !intrinsic_pseudo_style_is_inline(style)) continue;
        const char* content = dom_element_get_pseudo_element_content(element, pseudo);
        // Generated text is real inline boundary content, so adjacent collapsed
        // whitespace must contribute to the containing block's max-content size.
        if (content && *content) return true;
    }
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_comment()) continue;
        if (intrinsic_node_has_inline_boundary_content(child)) return true;
    }
    return false;
}

static bool intrinsic_has_following_inline_boundary_content(DomNode* node) {
    DomNode* current = node;
    while (current) {
        for (DomNode* sibling = current->next_sibling; sibling; sibling = sibling->next_sibling) {
            if (sibling->is_comment()) continue;
            if (intrinsic_node_has_inline_boundary_content(sibling)) return true;
            if (intrinsic_node_is_collapsible_space_only(sibling)) continue;
            if (sibling->is_element() && !is_inline_level_element(sibling->as_element())) return false;
        }

        DomNode* parent = current->parent;
        if (!parent || !parent->is_element()) break;
        if (!is_inline_level_element(parent->as_element())) break;
        current = parent;
    }
    return false;
}

static bool intrinsic_node_ends_with_collapsible_space(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        return intrinsic_text_ends_with_whitespace((const char*)node->text_data()) &&
            intrinsic_white_space_collapses_space_advance(get_white_space_value(node));
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    if (!is_inline_level_element(element)) return false;
    for (DomNode* child = element->last_child; child; child = child->prev_sibling) {
        if (child->is_comment()) continue;
        return intrinsic_node_ends_with_collapsible_space(child);
    }
    return false;
}

static float intrinsic_collapsed_space_width(LayoutContext* lycon) {
    float width = layout_measure_space_advance(
        lycon, lycon->font.font_handle, lycon->font.style);
    if (lycon && lycon->font.style) {
        width += lycon->font.style->word_spacing;
        width += lycon->font.style->letter_spacing;
    }
    return width;
}

static bool intrinsic_table_resolve_border_spacing_value(LayoutContext* lycon, const CssValue* value,
        float* spacing, bool* keep_inheriting) {
    if (keep_inheriting) *keep_inheriting = false;
    if (!value) return false;

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        *spacing = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING, value);
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 1 &&
        value->data.list.values[0]) {
        *spacing = resolve_length_value(lycon, CSS_PROPERTY_BORDER_SPACING,
                                        value->data.list.values[0]);
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        *spacing = (float)value->data.number.value;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum kw = value->data.keyword;
        if (kw == CSS_VALUE_INHERIT || kw == CSS_VALUE_UNSET) {
            if (keep_inheriting) *keep_inheriting = true;
            return false;
        }
        if (kw == CSS_VALUE_INITIAL) {
            *spacing = 0.0f;
            return true;
        }
    }
    return false;
}

static bool intrinsic_table_inherit_border_spacing(LayoutContext* lycon, DomElement* element,
        float* spacing) {
    for (DomNode* ancestor = element ? element->parent : nullptr; ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;

        DomElement* anc_elem = ancestor->as_element();
        if (anc_elem->specified_style) {
            CssDeclaration* decl = style_tree_get_declaration(
                anc_elem->specified_style,
                CSS_PROPERTY_BORDER_SPACING);
            if (decl && decl->value) {
                bool keep_inheriting = false;
                if (intrinsic_table_resolve_border_spacing_value(lycon, decl->value,
                        spacing, &keep_inheriting)) {
                    return true;
                }
                if (!keep_inheriting) return false;
            }
        }

        if (anc_elem->item_prop_type == DomElement::ITEM_PROP_TABLE && anc_elem->tb) {
            *spacing = anc_elem->tb->border_spacing_h;
            return true;
        }
        if (anc_elem->tag() == HTM_TAG_TABLE) {
            float inherited_spacing = 2.0f;
            const char* cellspacing_attr = anc_elem->get_attribute("cellspacing");
            if (cellspacing_attr) {
                inherited_spacing = (float)str_to_double_default(
                    cellspacing_attr, strlen(cellspacing_attr), 0.0);
                if (inherited_spacing < 0.0f) inherited_spacing = 0.0f;
            }
            *spacing = inherited_spacing;
            return true;
        }
    }
    return false;
}

static bool intrinsic_is_table_structure_container(CssEnum inner_display) {
    return inner_display == CSS_VALUE_TABLE ||
           inner_display == CSS_VALUE_TABLE_ROW ||
           inner_display == CSS_VALUE_TABLE_ROW_GROUP ||
           inner_display == CSS_VALUE_TABLE_HEADER_GROUP ||
           inner_display == CSS_VALUE_TABLE_FOOTER_GROUP;
}

static bool intrinsic_subtree_contains_node(DomNode* root, DomNode* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    if (!root->is_element()) return false;

    DomElement* element = root->as_element();
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (intrinsic_subtree_contains_node(child, target)) return true;
    }
    return false;
}

static bool intrinsic_pseudo_child_is_materialized(DomElement* element, bool is_before) {
    if (!element || !element->pseudo) return false;

    DomElement* pseudo = is_before ? element->pseudo->before : element->pseudo->after;
    if (!pseudo) return false;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (intrinsic_subtree_contains_node(child, static_cast<DomNode*>(pseudo))) return true;
    }
    return false;
}

static bool intrinsic_pseudo_needs_table_repair(DomElement* element, bool is_before) {
    if (!element) return false;
    StyleTree* pseudo_styles = is_before ? element->before_styles : element->after_styles;
    if (!pseudo_styles || !pseudo_styles->tree) return false;

    CssEnum display = (CssEnum)0;
    return intrinsic_style_display_keyword(pseudo_styles, &display) &&
           is_table_internal_display(display);
}

static void intrinsic_materialize_pseudo_content(LayoutContext* lycon, DomElement* element) {
    if (!lycon || !element) return;

    bool needs_before = dom_element_has_before_content(element) &&
        intrinsic_pseudo_needs_table_repair(element, true);
    bool needs_after = dom_element_has_after_content(element) &&
        intrinsic_pseudo_needs_table_repair(element, false);
    if (!needs_before && !needs_after) return;

    ViewBlock* block = lam::unsafe_view_block_element_storage(element);
    element->pseudo = alloc_pseudo_content_prop(lycon, block);
    if (!element->pseudo) return;

    // Generated table-internal pseudo boxes must exist before anonymous table
    // repair, or shrink-to-fit misses their inline contribution.
    if (needs_before) {
        generate_pseudo_element_content(lycon, block, true);
    }
    if (needs_after) {
        generate_pseudo_element_content(lycon, block, false);
    }
    if (needs_before && element->pseudo->before) {
        insert_pseudo_into_dom(element, element->pseudo->before, true);
    }
    if (needs_after && element->pseudo->after) {
        insert_pseudo_into_dom(element, element->pseudo->after, false);
    }
}

static void intrinsic_prepare_anonymous_table_children(LayoutContext* lycon,
        DomElement* element) {
    if (!lycon || !element || !element->first_child) return;

    DisplayValue display = resolve_display_value((void*)element);
    if (intrinsic_is_table_structure_container(display.inner)) return;

    // CSS 2.1 §17.2.1 anonymous table wrappers affect intrinsic inline-size
    // contributions. Layout creates them before flowing content; intrinsic sizing
    // must do the same before walking children, otherwise column measurement can
    // miss the anonymous table's border-spacing and row/cell structure.
    wrap_orphaned_table_children(lycon, element);
}

static bool intrinsic_element_is_float(DomElement* element) {
    if (!element) return false;
    if (layout_position_is_floated(element->position)) {
        return true;
    }
    if (!element->specified_style) return false;
    CssDeclaration* float_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_FLOAT);
    if (!float_decl || !float_decl->value ||
        float_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    CssEnum float_value = float_decl->value->data.keyword;
    return float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT;
}

static float intrinsic_parent_definite_height(LayoutContext* lycon) {
    BlockContext* parent = lycon ? lycon->block.parent : nullptr;
    if (!parent) return -1.0f;
    if (parent->content_height > 0.0f) return parent->content_height;
    return parent->given_height > 0.0f ? parent->given_height : -1.0f;
}

static bool intrinsic_element_is_abs_or_fixed(DomElement* element) {
    if (!element) return false;
    if (layout_position_is_abs_fixed(element->position)) {
        return true;
    }
    if (!element->specified_style) return false;
    CssDeclaration* pos_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_POSITION);
    if (!pos_decl || !pos_decl->value ||
        pos_decl->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return false;
    }
    CssEnum position_value = pos_decl->value->data.keyword;
    return position_value == CSS_VALUE_ABSOLUTE ||
           position_value == CSS_VALUE_FIXED;
}

// Helper to get text-transform property from an element, traversing parent chain
// since text-transform is an inherited property
CssEnum get_element_text_transform(DomElement* element) {
    // Walk up the DOM tree to find inherited text-transform value.
    // During intrinsic sizing, the view tree hasn't been created yet,
    // so we ONLY check specified_style on DOM elements (not ViewBlock).
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->specified_style) {
                CssDeclaration* decl = style_tree_get_declaration(
                    elem->specified_style, CSS_PROPERTY_TEXT_TRANSFORM);
                if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum val = decl->value->data.keyword;
                    if (val != CSS_VALUE_INHERIT && val != CSS_VALUE__UNDEF) {
                        return val;
                    }
                }
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

struct IntrinsicSvgSize {
    float width;
    float height;
};

static IntrinsicSvgSize intrinsic_svg_size(DomElement* element,
                                           float constrained_width = -1.0f) {
    IntrinsicSvgSize size = {300.0f, 150.0f};
    bool has_width = false;
    bool has_height = false;
    const char* width_attr = element ? element->get_attribute("width") : nullptr;
    const char* height_attr = element ? element->get_attribute("height") : nullptr;
    if (width_attr) {
        float width = (float)atof(width_attr);
        if (width > 0.0f) {
            size.width = width;
            has_width = true;
        }
    }
    if (height_attr) {
        float height = (float)atof(height_attr);
        if (height > 0.0f) {
            size.height = height;
            has_height = true;
        }
    }

    const char* viewbox = element ? element->get_attribute("viewBox") : nullptr;
    float vb_x = 0.0f, vb_y = 0.0f, vb_width = 0.0f, vb_height = 0.0f;
    bool has_viewbox = viewbox &&
        sscanf(viewbox, "%f %f %f %f", &vb_x, &vb_y, &vb_width, &vb_height) == 4 &&
        vb_width > 0.0f && vb_height > 0.0f;
    if (has_viewbox) {
        if (!has_width && !has_height) {
            size.width = vb_width;
            size.height = vb_height;
        } else if (has_width && !has_height) {
            size.height = size.width * vb_height / vb_width;
        } else if (!has_width && has_height) {
            size.width = size.height * vb_width / vb_height;
        }
    }

    // Width and height paths must use the same SVG aspect ratio resolution.
    if (constrained_width > 0.0f && constrained_width < size.width) {
        size.height = constrained_width * size.height / size.width;
        size.width = constrained_width;
    }
    return size;
}

static float intrinsic_image_height(ImageSurface* image, float constrained_width) {
    float height = (float)image->height;
    if (constrained_width > 0.0f && image->width > 0 &&
        (image->format == IMAGE_FORMAT_SVG || constrained_width < image->width)) {
        height = constrained_width * (float)image->height / (float)image->width;
    }
    return height;
}

// Helper to get font-variant property from an element, traversing parent chain
// since font-variant is an inherited property.
// Uses elem->font (available after CSS resolution) rather than specified_style.
CssEnum get_element_font_variant(DomElement* element) {
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->font && elem->font->font_variant == CSS_VALUE_SMALL_CAPS) {
                return CSS_VALUE_SMALL_CAPS;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

static void intrinsic_resolve_specified_horizontal_margins(
        LayoutContext* lycon, DomElement* element, bool include_logical,
        float* margin_left, float* margin_right) {
    StyleTree* style = element->specified_style;
    CssDeclaration* decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_LEFT);
    if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        *margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT, decl->value);
    }
    decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_RIGHT);
    if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
        *margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT, decl->value);
    }
    if (include_logical && *margin_left == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE_START);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            *margin_left = resolve_length_value(
                lycon, CSS_PROPERTY_MARGIN_INLINE_START, decl->value);
        }
    }
    if (include_logical && *margin_right == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE_END);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            *margin_right = resolve_length_value(
                lycon, CSS_PROPERTY_MARGIN_INLINE_END, decl->value);
        }
    }
    if (include_logical && *margin_left == 0.0f && *margin_right == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE);
        if (decl && decl->value && decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            *margin_left = *margin_right = resolve_length_value(
                lycon, CSS_PROPERTY_MARGIN_INLINE, decl->value);
        }
    }
    if (*margin_left != 0.0f || *margin_right != 0.0f) return;

    decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN);
    if (!decl || !decl->value) return;
    const CssValue* right_value = css_box_shorthand_side_value(decl->value, 1);
    const CssValue* left_value = css_box_shorthand_side_value(decl->value, 3);
    if (right_value) {
        *margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, right_value);
    }
    if (left_value) {
        *margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, left_value);
    }
}

// Text indent belongs to the first formatted line, so every kind of forced
// break must finalize the same first-line min/max contribution.
static void intrinsic_apply_first_line_indent(
        float text_indent, float first_child_min, float nonfirst_min_max,
        float* inline_max, float* inline_min) {
    *inline_max = fmaxf(*inline_max + text_indent, 0.0f);
    float first_segment = first_child_min >= 0.0f ? first_child_min : *inline_min;
    if (text_indent > 0.0f) {
        *inline_min = fmaxf(first_segment + text_indent, *inline_min);
    } else {
        float indented_min = fmaxf(first_segment + text_indent, 0.0f);
        *inline_min = fmaxf(indented_min, nonfirst_min_max);
    }
}

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element,
                                                bool content_only) {
    IntrinsicSizes sizes = {0, 0};

    if (!element) return sizes;

    // check intrinsic sizing cache to avoid redundant subtree traversals
    if (!content_only && element->styles_resolved && element->has_cached_intrinsic_widths) {
        return {element->cached_min_content_width, element->cached_max_content_width};
    }

    // re-entrancy guard: if we're already measuring this element, break the cycle
    if (element->measuring_intrinsic_width) {
        log_debug("measure_element_intrinsic_widths: cycle detected for %s, returning {0,0}", element->node_name());
        return {0, 0};
    }
    element->measuring_intrinsic_width = true;
    // RAII guard to clear measuring flag on all return paths
    struct MeasureGuard {
        DomElement* e;
        MeasureGuard(DomElement* e) : e(e) {}
        ~MeasureGuard() { e->measuring_intrinsic_width = false; }
    } measure_guard(element);
    View* saved_view = lycon->view;
    lycon->view = static_cast<View*>(element);
    struct IntrinsicViewGuard {
        LayoutContext* l;
        View* saved;
        IntrinsicViewGuard(LayoutContext* l, View* saved) : l(l), saved(saved) {}
        ~IntrinsicViewGuard() { l->view = saved; }
    } view_guard(lycon, saved_view);

    auto t_measure_start = std::chrono::high_resolution_clock::now();

    // CRITICAL FIX: Set up font context for this element BEFORE measuring text children
    // This ensures text measurement uses the element's own font (e.g., monospace for <code>)
    // rather than inheriting from parent context.
    FontBox saved_font = lycon->font;  // Save parent font context
    struct TempIntrinsicFontGuard {
        LayoutContext* lycon;
        FontBox saved_font;
        FontProp* prop_a;
        FontProp* prop_b;
        TempIntrinsicFontGuard(LayoutContext* lycon, FontBox saved_font)
            : lycon(lycon), saved_font(saved_font), prop_a(nullptr), prop_b(nullptr) {}
        ~TempIntrinsicFontGuard() {
            font_prop_release_handle(prop_a);
            font_prop_release_handle(prop_b);
            lycon->font = saved_font;
        }
    } temp_font_guard(lycon, saved_font);
    bool font_changed = false;
    ViewBlock* view_block_font = lam::unsafe_view_block_element_storage(element);

    uintptr_t intrinsic_tag = element->tag();
    if (!element->styles_resolved &&
        (intrinsic_tag == HTM_TAG_BUTTON || intrinsic_tag == HTM_TAG_INPUT)) {
        // Form-control intrinsic sizes depend on UA style, author font, and HTML
        // attributes, so resolve them before the shared control measurement runs.
        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
        dom_node_resolve_style(element, lycon);
    }

    // First check if element has resolved font
    if (view_block_font->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view_block_font->font);
        font_changed = true;
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        // Element has CSS styles but font not yet resolved - check font shorthand
        // and individual font properties. Either alone should trigger font setup.
        FontProp* temp_font_prop = alloc_font_prop(lycon);  // Allocates from pool
        temp_font_guard.prop_a = temp_font_prop;
        bool need_font_setup = false;
        bool spacing_font_ready = false;
        const char* css_family = NULL;

        if (intrinsic_tag == HTM_TAG_TABLE && lycon->doc && lycon->doc->view_tree &&
            is_quirks_mode(lycon->doc->view_tree->html_version)) {
            // the quirks UA table reset is a cascade base for descendant intrinsic metrics.
            temp_font_prop->font_size = 16.0f;
            temp_font_prop->font_size_from_medium = true;
            temp_font_prop->font_weight = CSS_VALUE_NORMAL;
            temp_font_prop->font_weight_numeric = 400;
            temp_font_prop->font_style = CSS_VALUE_NORMAL;
            temp_font_prop->font_variant = CSS_VALUE_NORMAL;
            need_font_setup = true;
        }

        // Check for font shorthand (CSS_PROPERTY_FONT) first.
        // CSS 2.1 §15.8: The font shorthand sets all font sub-properties.
        // Individual longhands with higher source_order may override below.
        CssDeclaration* font_shorthand_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT);
        if (font_shorthand_decl && font_shorthand_decl->value) {
            // CSS Fonts 3: the shorthand resets omitted font subproperties to
            // their initial values instead of inheriting UA or parent values.
            temp_font_prop->font_style = CSS_VALUE_NORMAL;
            temp_font_prop->font_weight = CSS_VALUE_NORMAL;
            temp_font_prop->font_weight_numeric = 400;
            temp_font_prop->font_variant = CSS_VALUE_NORMAL;
            const CssValue* fv = font_shorthand_decl->value;
            if (fv->type == CSS_VALUE_TYPE_KEYWORD) {
                // System font keyword (caption, icon, menu, message-box, small-caption, status-bar)
                const CssEnumInfo* info = css_enum_info(fv->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_SYSTEM_FONT) {
                    css_family = "Arial";
                    temp_font_prop->font_size = 13.333f;
                    temp_font_prop->font_weight = CSS_VALUE_NORMAL;
                    temp_font_prop->font_weight_numeric = 400;
                    temp_font_prop->font_style = CSS_VALUE_NORMAL;
                    need_font_setup = true;
                    log_debug("intrinsic font shorthand: system font '%s' -> Arial", info->name);
                }
            } else if (fv->type == CSS_VALUE_TYPE_LIST && fv->data.list.count >= 2) {
                // Full shorthand: [style] [variant] [weight] size[/line-height] family
                const CssValue* font_group = fv;
                if (fv->data.list.values[0] &&
                    fv->data.list.values[0]->type == CSS_VALUE_TYPE_LIST) {
                    font_group = fv->data.list.values[0];
                }
                size_t count = font_group->data.list.count;
                for (size_t fi = 0; fi < count; fi++) {
                    const CssValue* v = font_group->data.list.values[fi];
                    if (!v) continue;
                    if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        // font-size found
                        bool resolved_from_medium = false;
                        float sz = intrinsic_resolve_font_size_value(
                            lycon, v, &resolved_from_medium);
                        if (sz >= 0.0f) {
                            temp_font_prop->font_size = sz;
                            temp_font_prop->font_size_from_medium = resolved_from_medium;
                        }
                        // skip /line-height, then extract font-family
                        size_t fam_idx = fi + 1;
                        if (fam_idx < count) {
                            const CssValue* next = font_group->data.list.values[fam_idx];
                            if (next && next->type == CSS_VALUE_TYPE_CUSTOM &&
                                next->data.custom_property.name &&
                                strcmp(next->data.custom_property.name, "/") == 0) {
                                fam_idx += 2;
                            }
                        }
                        if (fam_idx < count) {
                            css_family = css_select_font_shorthand_family(
                                lycon, fv, font_group, fam_idx, false);
                        }
                        need_font_setup = true;
                        break;
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(v->data.keyword);
                        if (info) {
                            if (info->group == CSS_VALUE_GROUP_FONT_WEIGHT) {
                                CssEnum kw = v->data.keyword;
                                temp_font_prop->font_weight = (kw == CSS_VALUE_BOLD) ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
                                temp_font_prop->font_weight_numeric = (kw == CSS_VALUE_BOLD) ? (int16_t)700 : (int16_t)400;
                            } else if (info->group == CSS_VALUE_GROUP_FONT_STYLE) {
                                temp_font_prop->font_style = v->data.keyword;
                            } else if (info->group == CSS_VALUE_GROUP_FONT_SIZE) {
                                bool resolved_from_medium = false;
                                float sz = intrinsic_resolve_font_size_value(
                                    lycon, v, &resolved_from_medium);
                                if (sz >= 0.0f) {
                                    temp_font_prop->font_size = sz;
                                    temp_font_prop->font_size_from_medium = resolved_from_medium;
                                }
                                // everything after is font-family
                                size_t fam_idx = fi + 1;
                                if (fam_idx < count) {
                                    css_family = css_select_font_shorthand_family(
                                        lycon, fv, font_group, fam_idx, false);
                                }
                                need_font_setup = true;
                                break;
                            }
                        }
                    } else if (v->type == CSS_VALUE_TYPE_NUMBER) {
                        int w = (int)v->data.number.value; // INT_CAST_OK: CSS numeric value to int
                        if (w >= 1 && w <= 1000) {
                            temp_font_prop->font_weight = (w > 500) ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
                            temp_font_prop->font_weight_numeric = (int16_t)w;
                        }
                    } else if (v->type == CSS_VALUE_TYPE_STRING) {
                        css_family = v->data.string;
                        need_font_setup = true;
                    } else if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                               strcmp(v->data.custom_property.name, "/") != 0) {
                        css_family = v->data.custom_property.name;
                        need_font_setup = true;
                    }
                }
                if (css_family) need_font_setup = true;
            }
            if (css_family) {
                radiant_retain_font_family(temp_font_prop, lam::PoolPtr<char>((char*)css_family));
            }
        }

        // Check for font-family longhand (overrides shorthand if higher source_order)
        CssDeclaration* font_family_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_FAMILY);

        if (font_family_decl && font_family_decl->value &&
            (!font_shorthand_decl || font_family_decl->source_order > font_shorthand_decl->source_order)) {
            const char* longhand_family = css_select_font_family(
                lycon, font_family_decl->value, false);

            if (longhand_family) {
                css_family = longhand_family;
                if (css_family != lycon->font.style->family) {
                    radiant_retain_font_family(temp_font_prop, lam::PoolPtr<char>((char*)css_family));
                    need_font_setup = true;
                }
            }
        }

        // Check for font-size longhand (overrides shorthand if higher source_order).
        // CSS 2.1 §10.2: font-size affects text measurement during intrinsic sizing.
        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value &&
            (!font_shorthand_decl || font_size_decl->source_order > font_shorthand_decl->source_order)) {
            float resolved_size = -1.0f;
            bool resolved_from_medium = false;
            resolved_size = intrinsic_resolve_font_size_value(
                lycon, font_size_decl->value, &resolved_from_medium);
            if (resolved_size >= 0.0f) {
                // When shorthand already triggered setup, apply size unconditionally
                // (even if it matches parent) to override shorthand's default size
                if (need_font_setup || fabs(resolved_size - lycon->font.style->font_size) > 0.1f) {
                    temp_font_prop->font_size = resolved_size;
                    temp_font_prop->font_size_from_medium = resolved_from_medium;
                    need_font_setup = true;
                }
            }
        }

        // Check for font-weight longhand (overrides shorthand if higher source_order)
        CssDeclaration* font_weight_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_WEIGHT);
        if (font_weight_decl && font_weight_decl->value &&
            (!font_shorthand_decl || font_weight_decl->source_order > font_shorthand_decl->source_order)) {
            CssEnum mapped_weight = CSS_VALUE_NORMAL;
            int16_t numeric_weight = 0;
            const CssValue* fw_val = font_weight_decl->value;
            if (fw_val->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = fw_val->data.keyword;
                if (kw == CSS_VALUE_BOLD) { mapped_weight = CSS_VALUE_BOLD; numeric_weight = 700; }
                else if (kw == CSS_VALUE_BOLDER) { mapped_weight = CSS_VALUE_BOLDER; }
                else if (kw == CSS_VALUE_LIGHTER) { mapped_weight = CSS_VALUE_LIGHTER; }
                else { mapped_weight = CSS_VALUE_NORMAL; numeric_weight = 400; }
            } else if (fw_val->type == CSS_VALUE_TYPE_NUMBER) {
                int w = (int)fw_val->data.number.value; // INT_CAST_OK: CSS numeric value to int
                numeric_weight = (w >= 100 && w <= 900) ? (int16_t)w : 0;
                mapped_weight = (w > 500) ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
            }
            CssEnum parent_weight = lycon->font.style ? lycon->font.style->font_weight : CSS_VALUE_NORMAL;
            if (need_font_setup || mapped_weight != parent_weight) {
                temp_font_prop->font_weight = mapped_weight;
                temp_font_prop->font_weight_numeric = numeric_weight;
                need_font_setup = true;
            }
        }

        // Check for font-style longhand (overrides shorthand if higher source_order)
        CssDeclaration* font_style_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_STYLE);
        if (font_style_decl && font_style_decl->value &&
            font_style_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            (!font_shorthand_decl || font_style_decl->source_order > font_shorthand_decl->source_order)) {
            CssEnum css_font_style = font_style_decl->value->data.keyword;
            CssEnum parent_style = lycon->font.style ? lycon->font.style->font_style : CSS_VALUE_NORMAL;
            if (need_font_setup || css_font_style != parent_style) {
                temp_font_prop->font_style = css_font_style;
                need_font_setup = true;
            }
        }

        CssDeclaration* letter_spacing_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_LETTER_SPACING);
        CssDeclaration* word_spacing_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_WORD_SPACING);
        if ((letter_spacing_decl && letter_spacing_decl->value) ||
            (word_spacing_decl && word_spacing_decl->value)) {
            // Font-relative spacing uses this element's computed font size, so
            // finish its font cascade before resolving em/ex/ch spacing units.
            intrinsic_apply_ua_font_defaults(element, temp_font_prop, saved_font.style);
            intrinsic_complete_inherited_font(temp_font_prop, saved_font.style);
            intrinsic_apply_monospace_font_size_quirk(temp_font_prop, saved_font.style);
            setup_font(lycon->ui_context, &lycon->font, temp_font_prop);
            spacing_font_ready = true;
            need_font_setup = true;
        }
        if (letter_spacing_decl && letter_spacing_decl->value) {
            const CssValue* ls_val = letter_spacing_decl->value;
            if (ls_val->type == CSS_VALUE_TYPE_LENGTH) {
                temp_font_prop->letter_spacing =
                    resolve_length_value(lycon, CSS_PROPERTY_LETTER_SPACING, ls_val);
                need_font_setup = true;
            } else if (ls_val->type == CSS_VALUE_TYPE_KEYWORD &&
                       ls_val->data.keyword == CSS_VALUE_NORMAL) {
                temp_font_prop->letter_spacing = 0.0f;
                need_font_setup = true;
            }
        } else if (lycon->font.style) {
            temp_font_prop->letter_spacing = lycon->font.style->letter_spacing;
        }

        if (word_spacing_decl && word_spacing_decl->value) {
            const CssValue* ws_val = word_spacing_decl->value;
            if (ws_val->type == CSS_VALUE_TYPE_LENGTH) {
                temp_font_prop->word_spacing =
                    resolve_length_value(lycon, CSS_PROPERTY_WORD_SPACING, ws_val);
                need_font_setup = true;
            } else if (ws_val->type == CSS_VALUE_TYPE_KEYWORD &&
                       ws_val->data.keyword == CSS_VALUE_NORMAL) {
                temp_font_prop->word_spacing = 0.0f;
                need_font_setup = true;
            }
        } else if (lycon->font.style) {
            temp_font_prop->word_spacing = lycon->font.style->word_spacing;
        }

        if (need_font_setup) {
            if (!spacing_font_ready) {
                intrinsic_apply_ua_font_defaults(element, temp_font_prop, saved_font.style);
                intrinsic_complete_inherited_font(temp_font_prop, saved_font.style);
                intrinsic_apply_monospace_font_size_quirk(temp_font_prop, saved_font.style);
                setup_font(lycon->ui_context, &lycon->font, temp_font_prop);
            }
            font_changed = true;
        }
    }

    // UA font defaults: elements like <b>, <strong>, <i>, <em>, <code>, <th> have
    // font properties from the UA stylesheet that affect text width measurement.
    // Use TEMPORARY font props (not element->font) to avoid corrupting the DOM node's
    // font state, which would prevent proper CSS inheritance during the real layout pass.
    // Note: we check !font_changed (not !specified_style) because every DOM element has
    // a specified_style from creation, but it may be empty (no CSS rules matched).
    // The !font_changed guard ensures we only apply UA defaults when neither resolved
    // font nor CSS styles provided font properties.
    if (!font_changed && !element->font && lycon->ui_context && lycon->font.style &&
        intrinsic_has_ua_font_defaults(element->tag())) {
        FontProp* ua_font = alloc_font_prop(lycon);
        temp_font_guard.prop_b = ua_font;
        if (intrinsic_apply_ua_font_defaults(element, ua_font, lycon->font.style)) {
            intrinsic_complete_inherited_font(ua_font, lycon->font.style);
            setup_font(lycon->ui_context, &lycon->font, ua_font);
            font_changed = true;
        }
    }

    // Resolve CSS display for this element if not already resolved.
    // Intrinsic sizing needs the same outer/inner display mapping as normal
    // layout, especially for CSS table values on non-table HTML elements.
    if (!element->styles_resolved && element->specified_style) {
        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
        (lam::unsafe_view_block_element_storage(element))->display = resolve_display_value((void*)element);
    }

    log_debug("measure_element_intrinsic: tag=%s, outer=%d", element->node_name(),
              (lam::unsafe_view_block_element_storage(element))->display.outer);

    // CSS 2.1 §9.2.4: Elements with display:none do not generate boxes
    // and contribute zero intrinsic size.
    {
        ViewBlock* check_none = lam::unsafe_view_block_element_storage(element);
        if (layout_block_is_display_none(check_none)) {
            return sizes;
        }
    }

    // Detect table elements early (needed to skip explicit width shortcut for tables)
    bool is_table_display = false;
    {
        ViewBlock* tview = lam::unsafe_view_block_element_storage(element);
        if (intrinsic_element_display_matches(
                element, tview, CSS_VALUE_TABLE, CSS_VALUE_INLINE_TABLE)) {
            is_table_display = true;
        }
        if (!is_table_display && element->tag() == HTM_TAG_TABLE) is_table_display = true;
    }

    // Check for explicit CSS width first
    // When content_only is true, skip this early return to measure content-only min-content.
    // This is needed for CSS Flexbox §4.5 content_size_suggestion which represents the
    // min-content of the element's content, NOT the specified CSS width.
    // CSS Tables §4.1: Table content-box inline size is never smaller than its minimum
    // content inline size, so tables skip the explicit width shortcut and measure content.
    // CSS 2.1 §10.3.1: 'width' does not apply to non-replaced inline elements.
    // Use resolve_display_value() to get the computed display with blockification
    // (CSS 2.1 §9.7: floated/abspos elements are blockified and DO get width).
    bool is_inline_non_replaced = false;
    {
        DisplayValue resolved_display = resolve_display_value((void*)element);
        if (resolved_display.outer == CSS_VALUE_INLINE && resolved_display.inner == CSS_VALUE_FLOW) {
            is_inline_non_replaced = true;
        }
    }
    ViewBlock* resolved_width_view = lam::unsafe_view_block_element_storage(element);
    if (!content_only && !is_table_display && !is_inline_non_replaced &&
        resolved_width_view->blk && resolved_width_view->blk->given_width >= 0.0f) {
        float resolved_width = resolved_width_view->blk->given_width;
        bool is_border_box = layout_uses_border_box(resolved_width_view);
        BoxMetrics resolved_box = layout_box_metrics(resolved_width_view);

        if (!is_border_box && resolved_width_view->bound) {
            resolved_width += resolved_box.pad_border_h;
        } else if (is_border_box && resolved_width_view->bound) {
            float pb_w = resolved_box.pad_border_h;
            if (resolved_width < pb_w) resolved_width = pb_w;
        }

        sizes.min_content = resolved_width;
        sizes.max_content = resolved_width;
        return sizes;
    }

    if (element->specified_style && !content_only && !is_table_display && !is_inline_non_replaced) {
        CssDeclaration* width_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->value &&
            (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
             width_decl->value->type == CSS_VALUE_TYPE_FUNCTION ||
             (width_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
              width_decl->value->data.number.value == 0))) {
            float explicit_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH,
                                                            width_decl->value);
            if (explicit_width >= 0) {
                // CSS width property sets content width by default (box-sizing: content-box)
                // We need to add padding and border for the total intrinsic width
                // Check box-sizing first
                bool is_border_box = false;
                ViewBlock* view_for_box = lam::unsafe_view_block_element_storage(element);
                if (layout_uses_border_box(view_for_box)) {
                    is_border_box = true;
                } else {
                    CssDeclaration* box_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_BOX_SIZING);
                    if (box_decl && box_decl->value &&
                        box_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                        box_decl->value->data.keyword == CSS_VALUE_BORDER_BOX) {
                        is_border_box = true;
                    }
                }

                if (!is_border_box) {
                    // Add padding and border to content width
                    float pad_left = 0, pad_right = 0, border_left = 0, border_right = 0;
                    // Read padding from CSS
                    CssDeclaration* pad_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_PADDING);
                    if (pad_decl && pad_decl->value) {
                        const CssValue* right = css_box_shorthand_side_value(pad_decl->value, 1);
                        const CssValue* left = css_box_shorthand_side_value(pad_decl->value, 3);
                        if (right) {
                            pad_right = resolve_length_value(lycon, CSS_PROPERTY_PADDING, right);
                        }
                        if (left) {
                            pad_left = resolve_length_value(lycon, CSS_PROPERTY_PADDING, left);
                        }
                    }
                    // Read border: prefer resolved bound, fall back to CSS shorthand
                    ViewBlock* view_for_bdr = lam::unsafe_view_block_element_storage(element);
                    if (view_for_bdr->bound && view_for_bdr->bound->border) {
                        border_left = view_for_bdr->bound->border->width.left;
                        border_right = view_for_bdr->bound->border->width.right;
                    }
                    if (border_left == 0 && border_right == 0) {
                        get_horizontal_border_widths_from_css(lycon, element, &border_left, &border_right);
                    }
                    explicit_width += pad_left + pad_right + border_left + border_right;
                    log_debug("  -> explicit width: %.0f (after adding padding=%.0f+%.0f, border=%.0f+%.0f)",
                              explicit_width, pad_left, pad_right, border_left, border_right);
                } else {
                    // border-box: floor at padding+border (content-box >= 0)
                    ViewBlock* view_for_pb = lam::unsafe_view_block_element_storage(element);
                    if (view_for_pb->bound) {
                        BoxMetrics box = layout_box_metrics(view_for_pb);
                        float pb_w = box.pad_border_h;
                        if (explicit_width < pb_w) {
                            log_debug("  -> explicit width: %.0f floored to %.0f (border-box, padding+border)", explicit_width, pb_w);
                            explicit_width = pb_w;
                        }
                    }
                    log_debug("  -> explicit width: %.0f (border-box)", explicit_width);
                }

                sizes.min_content = explicit_width;
                sizes.max_content = explicit_width;
                return sizes;
            }
        }
    }

    // Check for aspect-ratio with explicit height or resolvable percentage height
    // If element has height and aspect-ratio, width = height * aspect-ratio
    ViewBlock* view_block_for_aspect = lam::unsafe_view_block_element_storage(element);
    float aspect_ratio = 0;

    // First check fi for resolved aspect-ratio
    if (view_block_for_aspect->item_prop_type == DomElement::ITEM_PROP_FLEX &&
        view_block_for_aspect->fi && view_block_for_aspect->fi->aspect_ratio > 0) {
        aspect_ratio = view_block_for_aspect->fi->aspect_ratio;
    }
    // If not in fi, check specified_style directly
    else if (element->specified_style) {
        CssDeclaration* aspect_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_ASPECT_RATIO);
        if (aspect_decl && aspect_decl->value) {
            if (aspect_decl->value->type == CSS_VALUE_TYPE_NUMBER) {
                aspect_ratio = (float)aspect_decl->value->data.number.value;
                log_debug("  -> aspect-ratio from specified_style: %.3f", aspect_ratio);
            } else if (aspect_decl->value->type == CSS_VALUE_TYPE_LIST &&
                       aspect_decl->value->data.list.count == 2) {
                // Handle "width / height" format
                CssValue* width_val = aspect_decl->value->data.list.values[0];
                CssValue* height_val = aspect_decl->value->data.list.values[1];
                if (width_val && height_val &&
                    width_val->type == CSS_VALUE_TYPE_NUMBER &&
                    height_val->type == CSS_VALUE_TYPE_NUMBER) {
                    float w = (float)width_val->data.number.value;
                    float h = (float)height_val->data.number.value;
                    if (h > 0) {
                        aspect_ratio = w / h;
                        log_debug("  -> aspect-ratio from specified_style list: %.3f (%.1f / %.1f)",
                                  aspect_ratio, w, h);
                    }
                }
            }
        }
    }

    if (aspect_ratio > 0) {
        float height = -1;

        // Check for explicit height from CSS
        if (view_block_for_aspect->blk && view_block_for_aspect->blk->given_height > 0) {
            height = view_block_for_aspect->blk->given_height;
        }

        // If no explicit height, check for percentage height that can resolve
        // against a parent with definite height
        if (height <= 0 && element->specified_style) {
            CssDeclaration* height_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_HEIGHT);
            if (height_decl && height_decl->value &&
                height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float percentage = (float)height_decl->value->data.percentage.value;
                // Check if parent has definite height
                float parent_height = intrinsic_parent_definite_height(lycon);
                if (parent_height > 0) {
                    height = parent_height * percentage / 100.0f;
                    log_debug("  -> percentage height resolved: %.1f%% of %.1f = %.1f",
                              percentage, parent_height, height);
                }
            }
        }

        if (height > 0) {
            float aspect_width = height * aspect_ratio;
            sizes.min_content = aspect_width;
            sizes.max_content = aspect_width;
            log_debug("  -> aspect-ratio width: %.1f (height=%.1f, ratio=%.3f)",
                      aspect_width, height, aspect_ratio);
            return sizes;
        }
    }

    // CSS 2.1 §10.3.2/§10.6.2: Replaced element intrinsic sizing
    // For replaced elements (img, video, iframe, etc.), use intrinsic dimensions
    // when no explicit CSS width is set.
    // Note: we set sizes but do NOT return early — we fall through to the
    // padding/border section at the bottom so padding+border are correctly included.
    bool replaced_intrinsic_set = false;
    ViewBlock* view_block_replaced = lam::unsafe_view_block_element_storage(element);
    // Check both resolved display.inner AND element tag for replaced element detection,
    // since display.inner may not yet be resolved during early intrinsic sizing passes.
    // Form controls (input, select, textarea) are detected by tag as well, because
    // their FormControlProp may not be allocated yet during early intrinsic sizing.
    uintptr_t replaced_tag = element->tag();
    // HTML §4.8.7: <object> is replaced only when it has a data attribute
    // HTML §4.8.9: <audio> is replaced only when it has a controls attribute
    bool is_replaced_element = (view_block_replaced->display.inner == RDT_DISPLAY_REPLACED) ||
        (replaced_tag == HTM_TAG_IMG || replaced_tag == HTM_TAG_VIDEO ||
         replaced_tag == HTM_TAG_IFRAME || replaced_tag == HTM_TAG_HR ||
         replaced_tag == HTM_TAG_SVG || replaced_tag == HTM_TAG_CANVAS ||
         (replaced_tag == HTM_TAG_OBJECT && element->get_attribute("data")) ||
         (replaced_tag == HTM_TAG_AUDIO && element->has_attribute("controls")) ||
         replaced_tag == HTM_TAG_EMBED ||
         replaced_tag == HTM_TAG_INPUT || replaced_tag == HTM_TAG_SELECT ||
         replaced_tag == HTM_TAG_TEXTAREA || replaced_tag == HTM_TAG_METER ||
         replaced_tag == HTM_TAG_PROGRESS) ||
        (view_block_replaced->item_prop_type == DomElement::ITEM_PROP_FORM &&
         view_block_replaced->form);
    if (is_replaced_element) {
        float replaced_width = -1;

        if (replaced_tag == HTM_TAG_IMG) {
            // Try to get image dimensions - first check if already loaded
            if (view_block_replaced->embed && view_block_replaced->embed->img) {
                replaced_width = view_block_replaced->embed->img->width;
                log_debug("  -> replaced IMG intrinsic width: %.0f (from loaded image)", replaced_width);
            }
            // Try to load the image to get intrinsic dimensions
            if (replaced_width < 0) {
                const char* src_value = element->get_attribute("src");
                if (src_value && lycon->ui_context) {
                    if (!view_block_replaced->embed) {
                        view_block_replaced->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                    }
                    size_t src_len = strlen(src_value);
                    StrBuf* src_buf = strbuf_new_cap(src_len);
                    strbuf_append_str_n(src_buf, src_value, src_len);
                    view_block_replaced->embed->img = load_image(lycon->ui_context, src_buf->str);
                    strbuf_free(src_buf);
                    if (view_block_replaced->embed->img) {
                        replaced_width = view_block_replaced->embed->img->width;
                        log_debug("  -> replaced IMG intrinsic width: %.0f (newly loaded)", replaced_width);
                    }
                }
            }
            // CSS 2.1 §10.3.2: HTML width attribute is a presentational hint that
            // overrides intrinsic dimensions. When <img width="128"> has a natural
            // file width of 256, the used width is 128 (the specified value).
            // Check resolved blk->given_width first, then HTML attribute directly
            // (styles may not be resolved yet during early intrinsic sizing).
            {
                float specified_width = -1;
                if (view_block_replaced->blk && view_block_replaced->blk->given_width >= 0) {
                    specified_width = view_block_replaced->blk->given_width;
                } else {
                    const char* attr_w = element->get_attribute("width");
                    if (attr_w && attr_w[0] >= '0' && attr_w[0] <= '9') {
                        // skip percentage widths (can't resolve during intrinsic sizing)
                        size_t len = strlen(attr_w);
                        if (len > 0 && attr_w[len - 1] != '%') {
                            float w = (float)atof(attr_w);
                            if (w > 0) specified_width = w;
                        }
                    }
                }
                if (specified_width >= 0 && replaced_width > 0 && replaced_width != specified_width) {
                    // Override loaded image dimensions with specified value
                    log_debug("  -> replaced IMG width overridden by specified: %.0f (was %.0f)", specified_width, replaced_width);
                    replaced_width = specified_width;
                } else if (specified_width >= 0 && replaced_width < 0) {
                    // image not loaded yet — use specified width as placeholder
                    log_debug("  -> replaced IMG placeholder width: %.0f (image pending)", specified_width);
                    replaced_width = specified_width;
                }
            }
            // Fallback for broken/unloadable images — use small icon size.
            // CSS Images 3: broken images have no intrinsic dimensions.
            // Browsers ignore HTML width/height attributes for broken images
            // and render a small broken image icon with alt text instead.
            if (replaced_width < 0) {
                replaced_width = 0;
                log_debug("  -> replaced IMG fallback width: 0 (no image or attributes)");
            }
        }
        else if (replaced_tag == HTM_TAG_IFRAME) {
            replaced_width = 300;
            log_debug("  -> replaced IFRAME intrinsic width: 300");
        }
        else if (replaced_tag == HTM_TAG_VIDEO || replaced_tag == HTM_TAG_CANVAS) {
            // try actual video dimensions first
            if (replaced_tag == HTM_TAG_VIDEO && view_block_replaced->embed && view_block_replaced->embed->video) {
                int vw = rdt_video_get_width(view_block_replaced->embed->video);
                if (vw > 0) {
                    replaced_width = (float)vw;
                    log_debug("  -> replaced VIDEO intrinsic width: %.0f (from video metadata)", replaced_width);
                } else {
                    replaced_width = 300;
                    log_debug("  -> replaced VIDEO intrinsic width: 300 (metadata not yet loaded)");
                }
            } else {
                replaced_width = 300;
                log_debug("  -> replaced VIDEO/CANVAS intrinsic width: 300");
            }
        }
        else if (replaced_tag == HTM_TAG_AUDIO) {
            replaced_width = 300;
            log_debug("  -> replaced AUDIO intrinsic width: 300");
        }
        else if (replaced_tag == HTM_TAG_SVG) {
            replaced_width = intrinsic_svg_size(element).width;
            log_debug("  -> replaced SVG intrinsic width: %.0f", replaced_width);
        }
        else if (replaced_tag == HTM_TAG_HR) {
            replaced_width = 0;
        }
        else if (replaced_tag == HTM_TAG_EMBED ||
                 (replaced_tag == HTM_TAG_OBJECT && element->get_attribute("data"))) {
            replaced_width = 300;
            log_debug("  -> replaced OBJECT/EMBED intrinsic width: 300");
        }
        else if (replaced_tag == HTM_TAG_METER) {
            replaced_width = FormDefaults::METER_WIDTH;
            log_debug("  -> replaced METER intrinsic width: %.0f", replaced_width);
        }
        else if (replaced_tag == HTM_TAG_PROGRESS) {
            replaced_width = FormDefaults::PROGRESS_WIDTH;
            log_debug("  -> replaced PROGRESS intrinsic width: %.0f", replaced_width);
        }

        // Form controls (INPUT, SELECT, TEXTAREA) have intrinsic sizes.
        // When FormControlProp is allocated (styles resolved), use its intrinsic_width.
        // Otherwise, fall back to tag+type-based defaults for early intrinsic sizing.
        // Note: form->intrinsic_width set by calc_select_size already includes the
        // element's CSS padding+border (i.e. it is border-box). The common code at
        // the bottom of this function adds horizontal padding+border again — for
        // form controls sourced from form->intrinsic_width we therefore mark the
        // value as "already includes pad+border" and skip the bottom addition.
        if (replaced_width < 0 && view_block_replaced->item_prop_type == DomElement::ITEM_PROP_FORM
            && view_block_replaced->form && view_block_replaced->form->intrinsic_width > 0) {
            IntrinsicSize form_size = layout_measure_form_control(lycon, view_block_replaced,
                                                                  lycon->available_space);
            replaced_width = form_size.max_width;
            log_debug("  -> replaced FORM CONTROL intrinsic width: %.1f (central measure)",
                      replaced_width);
        }
        // Tag-based fallback: form prop not yet allocated during early intrinsic sizing
        if (replaced_width < 0) {
            if (replaced_tag == HTM_TAG_INPUT) {
                const char* input_type = element->get_attribute("type");
                if (input_type && (strcmp(input_type, "checkbox") == 0 || strcmp(input_type, "radio") == 0)) {
                    replaced_width = FormDefaults::CHECK_SIZE;
                } else if (input_type && strcmp(input_type, "range") == 0) {
                    replaced_width = FormDefaults::RANGE_WIDTH;
                } else if (input_type && strcmp(input_type, "image") == 0) {
                    replaced_width = FormDefaults::IMAGE_INPUT_WIDTH;
                } else {
                    // text, password, number, email, url, search, tel, etc.
                    // FormDefaults::TEXT_WIDTH is Chrome's UA border-box width.
                    // If author CSS provides padding/border, measure the UA
                    // content box and let the common box addition below apply
                    // the author box. Otherwise keep the UA border-box value.
                    bool has_author_box = css_has_horizontal_box_decl(element->specified_style);
                    if (has_author_box || view_block_replaced->bound) {
                        replaced_width = FormDefaults::TEXT_WIDTH -
                            2.0f * (FormDefaults::TEXT_PADDING_H + FormDefaults::TEXT_BORDER);
                    } else {
                        replaced_width = FormDefaults::TEXT_WIDTH;
                        sizes.replaced_includes_pad_border = true;
                    }
                }
                log_debug("  -> replaced INPUT (tag fallback, type=%s) intrinsic width: %.1f",
                    input_type ? input_type : "text", replaced_width);
            } else if (replaced_tag == HTM_TAG_SELECT) {
                // SELECT (combo box): measure max option text + arrow overhead.
                // calc_select_size in layout_form.cpp may never run when SELECT is a
                // flex/grid item (laid out via measurement path rather than layout_form_control).
                bool is_listbox = view_block_replaced->form &&
                    (view_block_replaced->form->multiple || view_block_replaced->form->select_size > 1);
                if (is_listbox) {
                    replaced_width = FormDefaults::SELECT_WIDTH;
                } else {
                    // For appearance:none, the heavy author CSS padding typically reserves
                    // room for an author-supplied chevron icon (e.g. via ::after) — it is
                    // not part of the replaced content. To match Chrome:
                    //   • min_content = option text min (no CSS padding) — the floor parent
                    //     shrink-to-fit can collapse to.
                    //   • max_content = option text max + CSS padding + border (full
                    //     border-box) — added by the common pad/border code below.
                    // The asymmetry lets parent fit-content clamp to available width
                    // (text floor) when text+padding overflows the container.
                    bool appearance_none = false;
                    {
                        CssDeclaration* ap_decl = dom_element_get_specified_value(element, CSS_PROPERTY_APPEARANCE);
                        if (ap_decl && ap_decl->value && ap_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                            ap_decl->value->data.keyword == CSS_VALUE_NONE) {
                            appearance_none = true;
                        }
                    }
                    float max_text_min = 0;  // longest unbreakable word across options
                    float max_text_max = 0;  // longest no-wrap option text
                    auto include_text = [&](const char* text, size_t length, float indent) {
                        if (!text || length == 0) return;
                        TextIntrinsicWidths tw = measure_text_intrinsic_widths(lycon, text, length);
                        float min_width = tw.min_content + indent;
                        float max_width = tw.max_content + indent;
                        if (indent > 0.0f) {
                            if (min_width < FormDefaults::OPTGROUP_OPTION_MIN_WIDTH) min_width = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
                            if (max_width < FormDefaults::OPTGROUP_OPTION_MIN_WIDTH) max_width = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
                        }
                        if (min_width > max_text_min) max_text_min = min_width;
                        if (max_width > max_text_max) max_text_max = max_width;
                    };
                    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                        DomElement* ce = child->as_element();
                        if (ce && ce->tag() == HTM_TAG_OPTGROUP) {
                            const char* lbl = ce->get_attribute("label");
                            include_text(lbl, lbl ? strlen(lbl) : 0, 0.0f);
                        }
                    }
                    for (DomElement* option = dom_select_next_option(element, nullptr); option;
                         option = dom_select_next_option(element, option)) {
                        DomElement* parent = option->parent ? option->parent->as_element() : nullptr;
                        float indent = parent && parent->tag() == HTM_TAG_OPTGROUP
                            ? FormDefaults::OPTGROUP_OPTION_INDENT : 0.0f;
                        bool has_text = false;
                        for (DomNode* child = option->first_child; child; child = child->next_sibling) {
                            DomText* text = child->as_text();
                            if (text && text->text && text->length > 0) {
                                include_text(text->text, text->length, indent);
                                has_text = true;
                            }
                        }
                        if (!has_text && indent > 0.0f) {
                            // blank optgroup options still reserve the native minimum row width.
                            if (FormDefaults::OPTGROUP_OPTION_MIN_WIDTH > max_text_min) max_text_min = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
                            if (FormDefaults::OPTGROUP_OPTION_MIN_WIDTH > max_text_max) max_text_max = FormDefaults::OPTGROUP_OPTION_MIN_WIDTH;
                        }
                    }
                    // CSS `appearance: none` removes the native arrow region.
                    replaced_width = layout_select_combo_intrinsic_width(
                        max_text_max, !appearance_none);
                    if (view_block_replaced->form)
                        view_block_replaced->form->intrinsic_width = replaced_width;
                    if (appearance_none) {
                        // Establish asymmetric min/max so parent shrink-to-fit can clamp
                        // to available width when the SELECT's full border-box overflows.
                        sizes.min_content = max_text_min;  // pad/border NOT added below
                        sizes.max_content = max_text_max;  // pad/border ADDED below
                        sizes.replaced_min_excludes_pad_border = true;
                        replaced_intrinsic_set = true;
                        // Guard: skip the unconditional `if (replaced_width >= 0)` block
                        // that would overwrite the asymmetric values we just set.
                        replaced_width = -1;
                    }
                }
                log_debug("  -> replaced SELECT (measured) intrinsic width: %.1f", replaced_width);
            } else if (replaced_tag == HTM_TAG_TEXTAREA) {
                // Textarea default: 20 cols * ~8px average char width + padding
                replaced_width = FormDefaults::TEXTAREA_COLS * 8.0f + FormDefaults::TEXTAREA_PADDING * 2;
                log_debug("  -> replaced TEXTAREA (tag fallback) intrinsic width: %.1f", replaced_width);
            }
        }

        if (replaced_width >= 0) {
            sizes.min_content = replaced_width;
            sizes.max_content = replaced_width;
            replaced_intrinsic_set = true;
        }
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (!replaced_intrinsic_set && element->tag() == HTM_TAG_SVG) {
        float svg_width = intrinsic_svg_size(element).width;
        sizes.min_content = svg_width;
        sizes.max_content = svg_width;
        replaced_intrinsic_set = true;
        log_debug("  -> SVG (tag-based) intrinsic width: %.0f", svg_width);
    }

    // ========================================================================
    // Table element special handling: table intrinsic width = sum of cell widths
    // CSS Tables §4.1: Table min/max content width is the maximum over all rows
    // of the sum of cell min/max content widths in that row + border-spacing.
    // ========================================================================
    {
        ViewBlock* tbl_view = lam::unsafe_view_block_element_storage(element);
        bool is_table_element = intrinsic_element_display_matches(
            element, tbl_view, CSS_VALUE_TABLE, CSS_VALUE_INLINE_TABLE);
        if (!is_table_element && element->tag() == HTM_TAG_TABLE) is_table_element = true;

        if (is_table_element) {
            float border_spacing = 0;
            if (element->tb) {
                border_spacing = element->tb->border_spacing_h;
            } else {
                // Table metadata not yet created during intrinsic measurement.
                // Read border-spacing from CSS, inherited computed values, or UA defaults.
                bool found_css = false;
                if (element->specified_style) {
                    CssDeclaration* bs_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_BORDER_SPACING);
                    if (bs_decl && bs_decl->value) {
                        bool keep_inheriting = false;
                        if (intrinsic_table_resolve_border_spacing_value(lycon, bs_decl->value,
                                &border_spacing, &keep_inheriting)) {
                            found_css = true;
                        } else if (keep_inheriting &&
                                   intrinsic_table_inherit_border_spacing(lycon, element, &border_spacing)) {
                            found_css = true;
                        }
                    }
                }
                if (!found_css && element->tag() == HTM_TAG_TABLE) {
                    // HTML UA default: 2px border-spacing for <table>
                    border_spacing = 2.0f;
                    // Check cellspacing attribute override
                    const char* cs = element->get_attribute("cellspacing");
                    if (cs) {
                        border_spacing = (float)str_to_double_default(cs, strlen(cs), 0.0);
                    }
                } else if (!found_css) {
                    // CSS 2.1 §17.6.1: border-spacing is inherited. Anonymous
                    // table boxes have no TableProp during intrinsic sizing, but
                    // their shrink-to-fit width must still include inherited spacing.
                    intrinsic_table_inherit_border_spacing(lycon, element, &border_spacing);
                }
            }

            // CSS Tables §4.1: Table intrinsic width = sum of per-column
            // intrinsic widths, NOT max of per-row sums. Each column's
            // min/max-content width is the maximum of cell min/max-content
            // widths across all rows in that column.

            // Helper: get colspan for a cell element
            auto get_cell_colspan = [](DomElement* cell_elem, ViewBlock* cell_view) -> int {
                // Prefer the resolved td->col_span (set during view pool creation)
                if (cell_view->td && cell_view->td->col_span > 1) {
                    return cell_view->td->col_span;
                }
                // Fallback: read from HTML attribute
                const char* cs = cell_elem->get_attribute("colspan");
                if (cs && *cs) {
                    int csv = (int)str_to_int64_default(cs, strlen(cs), 0); // INT_CAST_OK: span count
                    if (csv > 1) return csv;
                }
                return 1;
            };

            auto classify_table_cell = [&get_cell_colspan](DomElement* cell_elem,
                                                            bool* out_is_cell) -> int {
                ViewBlock* cell_view = lam::unsafe_view_block_element_storage(cell_elem);
                DisplayValue display = resolve_display_value((void*)cell_elem);
                bool is_cell = !intrinsic_element_is_float(cell_elem) &&
                    display.inner == CSS_VALUE_TABLE_CELL;
                if (out_is_cell) *out_is_cell = is_cell;
                return is_cell ? get_cell_colspan(cell_elem, cell_view) : 1;
            };

            enum IntrinsicAnonymousRowContext {
                INTRINSIC_ANON_ROW_NONE,
                INTRINSIC_ANON_ROW_TABLE,
                INTRINSIC_ANON_ROW_GROUP
            };

            auto is_proper_table_child = [](DomElement* child_elem) -> bool {
                if (!child_elem) return false;
                DisplayValue child_display = resolve_display_value((void*)child_elem);
                CssEnum inner = child_display.inner;
                return inner == CSS_VALUE_TABLE_ROW ||
                       inner == CSS_VALUE_TABLE_ROW_GROUP ||
                       inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                       inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
                       inner == CSS_VALUE_TABLE_COLUMN ||
                       inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
                       inner == CSS_VALUE_TABLE_CAPTION;
            };

            auto should_skip_anonymous_row_child = [&is_proper_table_child](
                    DomElement* child_elem, IntrinsicAnonymousRowContext anon_context) -> bool {
                if (anon_context == INTRINSIC_ANON_ROW_TABLE) {
                    return is_proper_table_child(child_elem);
                }
                if (anon_context == INTRINSIC_ANON_ROW_GROUP) {
                    DisplayValue child_display = resolve_display_value((void*)child_elem);
                    return child_display.inner == CSS_VALUE_TABLE_ROW;
                }
                return false;
            };

            auto anonymous_table_child_horizontal_margin = [&lycon](DomElement* child_elem) -> float {
                if (!child_elem) return 0.0f;
                float margin_left = 0.0f;
                float margin_right = 0.0f;
                ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
                if (child_view && child_view->bound) {
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO) {
                        margin_left = child_view->bound->margin.left;
                    }
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO) {
                        margin_right = child_view->bound->margin.right;
                    }
                    return margin_left + margin_right;
                }
                if (!child_elem->specified_style) return 0.0f;

                CssDeclaration* margin_left_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_MARGIN_LEFT);
                if (margin_left_decl && margin_left_decl->value &&
                    margin_left_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT,
                                                       margin_left_decl->value);
                }
                CssDeclaration* margin_right_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
                if (margin_right_decl && margin_right_decl->value &&
                    margin_right_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT,
                                                        margin_right_decl->value);
                }
                if (margin_left != 0.0f || margin_right != 0.0f) {
                    return margin_left + margin_right;
                }

                CssDeclaration* margin_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_MARGIN);
                if (!margin_decl || !margin_decl->value) return 0.0f;
                const CssValue* val = margin_decl->value;
                const CssValue* right_value = css_box_shorthand_side_value(val, 1);
                const CssValue* left_value = css_box_shorthand_side_value(val, 3);
                if (right_value && left_value) {
                    float right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, right_value);
                    float left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, left_value);
                    return left + right;
                }
                return 0.0f;
            };

            auto measure_anonymous_cell_run = [&should_skip_anonymous_row_child,
                    &anonymous_table_child_horizontal_margin, &lycon](
                    DomNode* run_start, IntrinsicAnonymousRowContext anon_context) -> IntrinsicSizes {
                IntrinsicSizes run_sizes = {0.0f, 0.0f};
                float inline_run_max = 0.0f;
                float inline_run_min = 0.0f;
                float float_run_max = 0.0f;
                float float_run_min = 0.0f;
                bool inline_run_has_content = false;
                bool prev_ended_with_space = false;
                bool inline_run_last_content_was_text = false;

                auto flush_current_run = [&]() {
                    float line_max = inline_run_max + float_run_max;
                    if (line_max > run_sizes.max_content) {
                        run_sizes.max_content = line_max;
                    }
                    float line_min = inline_run_min;
                    if (float_run_min > line_min) line_min = float_run_min;
                    if (line_min > run_sizes.min_content) {
                        run_sizes.min_content = line_min;
                    }
                    inline_run_max = 0.0f;
                    inline_run_min = 0.0f;
                    float_run_max = 0.0f;
                    float_run_min = 0.0f;
                    inline_run_has_content = false;
                    prev_ended_with_space = false;
                    inline_run_last_content_was_text = false;
                };

                for (DomNode* item = run_start; item; item = item->next_sibling) {
                    if (item->is_text()) {
                        if (!text_node_has_intrinsic_table_content(item)) {
                            if (inline_run_has_content) prev_ended_with_space = true;
                            continue;
                        }
                        if (text_node_is_ascii_whitespace(item) &&
                            !inline_run_last_content_was_text) {
                            continue;
                        }
                        DomElement* owner = item->parent && item->parent->is_element()
                            ? item->parent->as_element() : nullptr;
                        const char* text = (const char*)item->text_data();
                        if (!text || !*text) continue;
                        if (inline_run_has_content && intrinsic_text_starts_with_whitespace(text)) {
                            inline_run_max += layout_measure_space_advance(
                                lycon, lycon->font.font_handle, lycon->font.style);
                        }
                        TextIntrinsicWidths text_widths = measure_text_intrinsic_widths(
                            lycon, text, strlen(text),
                            owner ? get_element_text_transform(owner) : CSS_VALUE_NONE,
                            owner ? get_element_font_variant(owner) : CSS_VALUE_NONE);
                        inline_run_max += text_widths.max_content;
                        if (text_widths.min_content > inline_run_min) {
                            inline_run_min = text_widths.min_content;
                        }
                        inline_run_has_content = true;
                        prev_ended_with_space = intrinsic_text_ends_with_whitespace(text);
                        inline_run_last_content_was_text = true;
                        continue;
                    }

                    if (!item->is_element()) continue;

                    DomElement* item_elem = item->as_element();
                    if (should_skip_anonymous_row_child(item_elem, anon_context)) break;

                    DisplayValue item_display = resolve_display_value((void*)item_elem);
                    bool item_is_float = intrinsic_element_is_float(item_elem);
                    if (!item_is_float && item_display.inner == CSS_VALUE_TABLE_CELL) break;
                    if (layout_display_is_none(item_display) ||
                        intrinsic_element_is_abs_or_fixed(item_elem)) {
                        continue;
                    }

                    IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, item_elem);
                    float extra = layout_unresolved_html_cell_horizontal_box_extra(item_elem);
                    child_sizes.min_content += extra;
                    child_sizes.max_content += extra;
                    bool item_is_inline_level = is_inline_level_element(item_elem);
                    if (!item_is_inline_level || item_is_float) {
                        float margin_extra = anonymous_table_child_horizontal_margin(item_elem);
                        child_sizes.min_content += margin_extra;
                        child_sizes.max_content += margin_extra;
                    }

                    if (item_is_float) {
                        // CSS Sizing 3 §5: max-content places floats side-by-side,
                        // while min-content stacks them and uses the widest item.
                        // Float layout keeps shrink-to-fit widths at subpixel
                        // precision; max-content must use the same invariant or
                        // a measured-fitting run can wrap during actual layout.
                        float_run_max += child_sizes.max_content;
                        if (child_sizes.min_content > float_run_min) {
                            float_run_min = child_sizes.min_content;
                        }
                    } else if (item_is_inline_level) {
                        bool starts_with_ws = intrinsic_element_text_starts_with_whitespace(item);
                        if (inline_run_has_content && (prev_ended_with_space || starts_with_ws)) {
                            inline_run_max += layout_measure_space_advance(
                                lycon, lycon->font.font_handle, lycon->font.style);
                        }
                        inline_run_max += child_sizes.max_content;
                        if (child_sizes.min_content > inline_run_min) {
                            inline_run_min = child_sizes.min_content;
                        }
                        inline_run_has_content = true;
                        prev_ended_with_space = intrinsic_element_text_ends_with_whitespace(item);
                        inline_run_last_content_was_text = false;
                    } else {
                        flush_current_run();
                        if (child_sizes.max_content > run_sizes.max_content) {
                            run_sizes.max_content = child_sizes.max_content;
                        }
                        if (child_sizes.min_content > run_sizes.min_content) {
                            run_sizes.min_content = child_sizes.min_content;
                        }
                    }
                }

                flush_current_run();
                log_debug("  anonymous table-cell run intrinsic: min=%.1f, max=%.1f",
                          run_sizes.min_content, run_sizes.max_content);
                return run_sizes;
            };

            auto classify_row_item = [&classify_table_cell, &should_skip_anonymous_row_child](
                    DomNode* item, IntrinsicAnonymousRowContext anon_context,
                    bool* in_non_cell_run, bool* last_content_was_text,
                    DomElement** item_element, bool* is_cell, int* span) -> bool {
                *item_element = nullptr;
                *is_cell = false;
                *span = 1;
                if (item->is_text()) {
                    if (!text_node_has_intrinsic_table_content(item) ||
                        (text_node_is_ascii_whitespace(item) && !*last_content_was_text)) {
                        return false;
                    }
                    bool starts_run = !*in_non_cell_run;
                    *in_non_cell_run = true;
                    *last_content_was_text = true;
                    return starts_run;
                }
                if (!item->is_element()) return false;

                *item_element = item->as_element();
                if (should_skip_anonymous_row_child(*item_element, anon_context)) {
                    *in_non_cell_run = false;
                    *last_content_was_text = false;
                    return false;
                }

                *span = classify_table_cell(*item_element, is_cell);
                bool starts_run = !*in_non_cell_run;
                *last_content_was_text = false;
                *in_non_cell_run = !*is_cell;
                return *is_cell || starts_run;
            };

            // Helper: count columns in a row (accounting for colspan)
            auto count_cells = [&classify_row_item](
                    DomElement* row_elem, IntrinsicAnonymousRowContext anon_context) -> int {
                int n = 0;
                bool in_non_cell_run = false;
                bool non_cell_run_last_content_was_text = false;
                for (DomNode* cell = row_elem->first_child; cell; cell = cell->next_sibling) {
                    DomElement* cell_elem;
                    bool is_cell = false;
                    int span = 1;
                    if (classify_row_item(cell, anon_context, &in_non_cell_run,
                                          &non_cell_run_last_content_was_text,
                                          &cell_elem, &is_cell, &span)) {
                        n += span;
                    }
                }
                return n;
            };

            auto with_intrinsic_element_font = [&lycon](DomElement* font_element,
                                                        auto&& callback) {
                FontBox parent_font = lycon->font;
                View* saved_view = lycon->view;
                if (!font_element->styles_resolved) {
                    lycon->view = static_cast<View*>(font_element);
                    radiant::LayoutRunModeScope run_mode_scope(
                        lycon, radiant::RunMode::ComputeSize);
                    dom_node_resolve_style(font_element, lycon);
                }
                lycon->view = saved_view;
                lycon->font = parent_font;
                ViewBlock* font_block =
                    lam::unsafe_view_block_element_storage(font_element);
                if (font_block->font && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, font_block->font);
                }
                callback();
                lycon->font = parent_font;
            };

            // Helper: iterate rows in table (handles direct rows and row groups)
            auto for_each_row = [&](auto&& callback) {
                bool has_direct_anonymous_row_content = false;
                for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                    if (!child->is_element()) continue;
                    DomElement* child_elem = child->as_element();
                    if (!is_proper_table_child(child_elem)) {
                        has_direct_anonymous_row_content = true;
                        break;
                    }
                }
                if (has_direct_anonymous_row_content) {
                    // CSS 2.1 §17.2.1: improper direct children of a table are
                    // wrapped in anonymous row/cell structures. Intrinsic sizing
                    // must include that anonymous row contribution.
                    callback(element, INTRINSIC_ANON_ROW_TABLE);
                }

                for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                    if (!child->is_element()) continue;
                    DomElement* child_elem = child->as_element();
                    DisplayValue child_display = resolve_display_value((void*)child_elem);
                    bool is_row = (child_display.inner == CSS_VALUE_TABLE_ROW);
                    bool is_row_group = (!is_row && (
                        child_display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
                        child_display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
                        child_display.inner == CSS_VALUE_TABLE_FOOTER_GROUP));
                    if (is_row) {
                        // structural table boxes still establish the inherited font for cell percentages.
                        with_intrinsic_element_font(child_elem, [&]() {
                            callback(child_elem, INTRINSIC_ANON_ROW_NONE);
                        });
                    } else if (is_row_group) {
                        with_intrinsic_element_font(child_elem, [&]() {
                            for (DomNode* row = child_elem->first_child; row; row = row->next_sibling) {
                                if (!row->is_element()) continue;
                                DomElement* row_elem = row->as_element();
                                DisplayValue row_display = resolve_display_value((void*)row_elem);
                                if (row_display.inner == CSS_VALUE_TABLE_ROW) {
                                    with_intrinsic_element_font(row_elem, [&]() {
                                        callback(row_elem, INTRINSIC_ANON_ROW_NONE);
                                    });
                                }
                            }
                            bool group_has_anonymous_row_content = false;
                            for (DomNode* item = child_elem->first_child; item; item = item->next_sibling) {
                                if (!item->is_element()) continue;
                                DomElement* item_elem = item->as_element();
                                DisplayValue item_display = resolve_display_value((void*)item_elem);
                                if (item_display.inner != CSS_VALUE_TABLE_ROW) {
                                    group_has_anonymous_row_content = true;
                                    break;
                                }
                            }
                            if (group_has_anonymous_row_content) {
                                // CSS 2.1 §17.2.1: direct children of a row group
                                // that are not table-row boxes are wrapped in an
                                // anonymous table-row.
                                callback(child_elem, INTRINSIC_ANON_ROW_GROUP);
                            }
                        });
                    }
                }
            };

            // Pass 1: determine column count
            int num_columns = 0;
            for_each_row([&](DomElement* row_elem, IntrinsicAnonymousRowContext anon_context) {
                int n = count_cells(row_elem, anon_context);
                if (n > num_columns) num_columns = n;
            });

            // Table column counts come from the DOM, so avoid input-sized stack frames.
            float* col_min = num_columns > 0
                ? (float*)scratch_calloc(&lycon->scratch, (size_t)num_columns * sizeof(float))
                : nullptr;
            float* col_max = num_columns > 0
                ? (float*)scratch_calloc(&lycon->scratch, (size_t)num_columns * sizeof(float))
                : nullptr;
            if (num_columns > 0 && (!col_min || !col_max)) {
                log_error("measure_element_intrinsic_widths: failed to allocate %d table column widths", num_columns);
                scratch_free(&lycon->scratch, col_max);
                scratch_free(&lycon->scratch, col_min);
                return sizes;
            }

            // Pass 2: measure single-span cells, update per-column min/max
            // CSS 2.1 §17.5.2.2: First process cells that span a single column,
            // then distribute multi-span cells across their spanned columns.
            for_each_row([&](DomElement* row_elem, IntrinsicAnonymousRowContext anon_context) {
                int col = 0;
                bool in_non_cell_run = false;
                bool non_cell_run_last_content_was_text = false;
                for (DomNode* cell = row_elem->first_child; cell; cell = cell->next_sibling) {
                    DomElement* cell_elem;
                    bool is_cell = false;
                    int span = 1;
                    if (!classify_row_item(cell, anon_context, &in_non_cell_run,
                                           &non_cell_run_last_content_was_text,
                                           &cell_elem, &is_cell, &span)) continue;
                    if (col + span > num_columns) break;
                    if (span == 1) {
                        IntrinsicSizes cell_sizes = is_cell
                            ? measure_element_intrinsic_widths(lycon, cell_elem)
                            : measure_anonymous_cell_run(cell, anon_context);
                        float extra = is_cell ?
                            layout_unresolved_html_cell_horizontal_box_extra(cell_elem) : 0.0f;
                        float cmin = is_cell
                            ? ceilf(cell_sizes.min_content + extra)
                            : cell_sizes.min_content;
                        float cmax = is_cell
                            ? ceilf(cell_sizes.max_content + extra)
                            : cell_sizes.max_content;
                        if (cmin > col_min[col]) col_min[col] = cmin;
                        if (cmax > col_max[col]) col_max[col] = cmax;
                    }
                    col += span;
                }
            });

            // Pass 3: distribute multi-span cell widths across spanned columns
            // CSS 2.1 §17.5.2.2: If the sum of column widths spanned by a multi-span
            // cell is less than the cell's intrinsic width, distribute the deficit evenly.
            for_each_row([&](DomElement* row_elem, IntrinsicAnonymousRowContext anon_context) {
                int col = 0;
                bool in_non_cell_run = false;
                for (DomNode* cell = row_elem->first_child; cell; cell = cell->next_sibling) {
                    if (!cell->is_element()) continue;
                    DomElement* cell_elem = cell->as_element();
                    if (should_skip_anonymous_row_child(cell_elem, anon_context)) {
                        in_non_cell_run = false;
                        continue;
                    }
                    bool is_cell = false;
                    int span = classify_table_cell(cell_elem, &is_cell);
                    if (!is_cell && in_non_cell_run) continue;
                    if (col + span > num_columns) break;
                    in_non_cell_run = !is_cell;
                    if (span > 1) {
                        IntrinsicSizes cell_sizes = measure_element_intrinsic_widths(lycon, cell_elem);
                        float extra = is_cell ?
                            layout_unresolved_html_cell_horizontal_box_extra(cell_elem) : 0.0f;
                        float cmin = ceilf(cell_sizes.min_content + extra);
                        float cmax = ceilf(cell_sizes.max_content + extra);

                        // Sum current column widths + inter-column border-spacing
                        float current_min = 0, current_max = 0;
                        for (int j = 0; j < span; j++) {
                            current_min += col_min[col + j];
                            current_max += col_max[col + j];
                        }
                        current_min += border_spacing * (span - 1);
                        current_max += border_spacing * (span - 1);

                        // Distribute deficit evenly across spanned columns
                        if (cmin > current_min) {
                            float per_col = (cmin - current_min) / span;
                            for (int j = 0; j < span; j++) {
                                col_min[col + j] += per_col;
                            }
                        }
                        if (cmax > current_max) {
                            float per_col = (cmax - current_max) / span;
                            for (int j = 0; j < span; j++) {
                                col_max[col + j] += per_col;
                            }
                        }
                    }
                    col += span;
                    if (is_cell) in_non_cell_run = false;
                }
            });

            // Sum per-column widths to get table intrinsic widths
            float table_min = 0, table_max = 0;
            for (int i = 0; i < num_columns; i++) {
                table_min += col_min[i];
                table_max += col_max[i];
            }
            // CSS 2.1 §17.5.2.1: In the separated borders model, border-spacing
            // applies between each pair of adjacent columns AND on the left/right
            // edges of the table. Total horizontal spacing = (N + 1) * h-spacing.
            if (num_columns > 0) {
                table_min += border_spacing * (num_columns + 1);
                table_max += border_spacing * (num_columns + 1);
            }
            sizes.min_content = max(sizes.min_content, table_min);
            sizes.max_content = max(sizes.max_content, table_max);
            log_debug("  table intrinsic: %d cols, min=%.1f, max=%.1f",
                      num_columns, table_min, table_max);
            scratch_free(&lycon->scratch, col_max);
            scratch_free(&lycon->scratch, col_min);

            // Handle captions
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* child_elem = child->as_element();
                uintptr_t ctag = child_elem->tag();
                DisplayValue child_display = resolve_display_value((void*)child_elem);
                bool is_caption = (child_display.inner == CSS_VALUE_TABLE_CAPTION || ctag == HTM_TAG_CAPTION);
                if (is_caption) {
                    IntrinsicSizes cap = measure_element_intrinsic_widths(lycon, child_elem);
                    sizes.min_content = max(sizes.min_content, cap.min_content);
                    sizes.max_content = max(sizes.max_content, cap.max_content);
                }
            }

            // Add table's own padding and border
            float pad_left = 0, pad_right = 0, bdr_left = 0, bdr_right = 0;
            if (tbl_view->bound) {
                if (tbl_view->bound->padding.left >= 0) pad_left = tbl_view->bound->padding.left;
                if (tbl_view->bound->padding.right >= 0) pad_right = tbl_view->bound->padding.right;
                if (tbl_view->bound->border) {
                    bdr_left = tbl_view->bound->border->width.left;
                    bdr_right = tbl_view->bound->border->width.right;
                }
            }
            // Fallback: read border from specified CSS if bound not yet resolved
            if (bdr_left == 0 && bdr_right == 0) {
                get_horizontal_border_widths_from_css(lycon, element, &bdr_left, &bdr_right);
            }
            // Fallback: read border from HTML border attribute if still not found
            // WHATWG 15.3.10: table[border] → border-width = N pixels
            if (bdr_left == 0 && bdr_right == 0 && element->tag() == HTM_TAG_TABLE) {
                const char* border_attr = element->get_attribute("border");
                if (border_attr) {
                    float bw = (float)str_to_double_default(border_attr, strlen(border_attr), 0.0);
                    if (bw > 0) { bdr_left = bw; bdr_right = bw; }
                }
            }

            // Only use table-specific result if we actually found table rows/cells.
            // Tables with only text/block children (no row structure) should fall
            // through to the generic block measurement below (CSS anonymous box wrapping
            // will create rows/cells at layout time, but for measurement we use generic).
            if (sizes.min_content > 0 || sizes.max_content > 0) {
                sizes.min_content += pad_left + pad_right + bdr_left + bdr_right;
                sizes.max_content += pad_left + pad_right + bdr_left + bdr_right;

                // CSS Sizing 3 §5.2: If the table has a definite preferred width
                // (from CSS width or HTML width attribute), its max-content contribution
                // is max(specified_width, min_content), not the unconstrained column sum.
                float specified_width = 0;
                if (element->specified_style) {
                    CssDeclaration* wd = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_WIDTH);
                    if (wd && wd->value && wd->value->type == CSS_VALUE_TYPE_LENGTH) {
                        specified_width = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, wd->value);
                    }
                }
                if (specified_width <= 0 && element->tag() == HTM_TAG_TABLE) {
                    const char* w_attr = element->get_attribute("width");
                    if (w_attr) {
                        size_t wlen = strlen(w_attr);
                        if (wlen > 0 && w_attr[wlen - 1] != '%') {
                            StrView wv = strview_init(w_attr, wlen);
                            specified_width = (float)strview_to_int(&wv);
                        }
                    }
                }
                if (specified_width > 0) {
                    // Add padding+border to specified width for the content-box model
                    float total_specified = specified_width + pad_left + pad_right + bdr_left + bdr_right;
                    sizes.max_content = max(total_specified, sizes.min_content);
                    log_debug("  table specified width: %.1f -> max_content capped to %.1f",
                              specified_width, sizes.max_content);
                }

                log_debug("measure_element_intrinsic: TABLE %s: min=%.1f, max=%.1f",
                          element->node_name(), sizes.min_content, sizes.max_content);

                // Restore font if changed
                if (font_changed) lycon->font = saved_font;
                return sizes;
            }
            // No table structure found — fall through to generic measurement
            log_debug("measure_element_intrinsic: TABLE %s has no row structure, falling through",
                      element->node_name());
        }
    }

    intrinsic_materialize_pseudo_content(lycon, element);
    intrinsic_prepare_anonymous_table_children(lycon, element);

    // Track inline-level content separately
    float inline_min_sum = 0.0f;  // Max of min-content widths for inline children
    float inline_max_sum = 0.0f;  // Sum of max-content widths for inline children
    bool has_inline_content = false;
    bool inline_run_ends_with_collapsible_space = false;
    float first_inline_child_min = -1.0f;  // First inline child's min-content (for text-indent)
    float nonfirst_inline_min_max = 0.0f;  // Max min-content of non-first inline children (for neg text-indent)
    // CSS Text 3 §5.2: Track forced line breaks in inline content for propagation
    // to parent's inline run accumulation. first_inline_break_sum captures the
    // inline_max_sum at the point of the first forced break.
    bool inline_has_forced_break = false;
    float first_inline_break_sum = 0.0f;  // inline_max_sum before first forced break

    // CSS 2.1 §16.1: text-indent applies to the first formatted line of a block container.
    // Pre-compute text-indent so it's available for <br> forced break handling.
    float text_indent = 0.0f;
    {
        ViewBlock* vb = lam::unsafe_view_block_element_storage(element);
        if (vb->blk && vb->blk->text_indent != 0.0f) {
            text_indent = vb->blk->text_indent;
        } else if (element->specified_style) {
            CssDeclaration* ti_decl = style_tree_get_declaration(
                element->specified_style, CSS_PROPERTY_TEXT_INDENT);
            if (ti_decl && ti_decl->value && ti_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                text_indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, ti_decl->value);
            }
        }
    }

    // CSS Generated Content §2: ::before participates as the first inline child.
    // Account for it before measuring real children so following collapsible
    // whitespace is preserved as inter-word spacing instead of trimmed as
    // leading whitespace.
    bool has_generated_inline_before = false;
    if (!intrinsic_pseudo_child_is_materialized(element, true) &&
        dom_element_has_before_content(element) && element->before_styles && element->before_styles->tree) {
        const char* before_content = dom_element_get_pseudo_element_content(
            element, 1 /*PSEUDO_ELEMENT_BEFORE*/);
        if (intrinsic_pseudo_style_is_inline(element->before_styles) &&
            before_content && *before_content) {
            TextIntrinsicWidths tw = intrinsic_measure_pseudo_text_widths(
                lycon, element->before_styles, before_content);
            float before_width = tw.max_content;
            has_inline_content = true;
            has_generated_inline_before = true;
            inline_run_ends_with_collapsible_space = false;
            inline_max_sum += before_width;
            inline_min_sum = max(inline_min_sum, tw.min_content);
            if (first_inline_child_min < 0) {
                first_inline_child_min = tw.min_content;
            }
            log_debug("  pseudo ::before intrinsic width: %.1f added before children", before_width);
        }
    }

    // Track floated block-level children for intrinsic sizing
    // CSS Sizing 3 §5: At max-content, floats arrange side-by-side (infinite width),
    // so their max-content widths are summed. At min-content, each float wraps to its
    // own line, so min-content is the max of individual float min-content widths.
    float float_max_sum = 0.0f;   // Sum of floated children's max-content widths
    float float_min_max = 0.0f;   // Max of individual floated children's min-content widths

    // CSS 2.1 §9.5: At max-content, floats and inline content share the same line.
    // The total max-content width = float_widths + inline_content_width.
    // Track accumulated float widths that are adjacent to inline content so we
    // can add them to inline_max_sum at the end.
    float float_width_alongside_inline = 0.0f;

    // Check if this element is a flex container (text content doesn't contribute to intrinsic size)
    // Also check if it's a ROW flex container (children laid out horizontally -> SUM widths)
    bool is_flex_container = false;
    bool is_row_flex = false;
    bool is_flex_wrap = false;
    float flex_gap = 0;
    int flex_child_count = 0;  // Count of flex children for gap calculation
    ViewBlock* view_block = lam::unsafe_view_block_element_storage(element);

    // Check if this is a grid container.
    bool is_grid_container = intrinsic_element_display_matches(
        element, view_block, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);
    if (is_grid_container) {
        log_debug("measure_element_intrinsic_widths: grid container");

        // CSS Grid §10.1: Grid container intrinsic widths are computed column-by-column.
        // For a grid with N explicit columns, the max-content width = sum of column max-contents
        // (each column's max-content = max of all items spanning only that column).
        // This is different from a block container which takes max of block children.
        GridProp* grid_prop = view_block->embed ? view_block->embed->grid : nullptr;
        int col_count = intrinsic_grid_template_column_count(
            element, view_block, 0, "measure_element_intrinsic_widths", false, false);

        if (col_count > 1) {
            // Compute per-column max-content: assign each child to a column (auto-placement)
            // and take max of children's max-content in each column
            float* col_min = (float*)scratch_calloc(&lycon->scratch, col_count * sizeof(float));
            float* col_max = (float*)scratch_calloc(&lycon->scratch, col_count * sizeof(float));

            int item_idx = 0;
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* child_elem = child->as_element();
                // Auto-placement: assign to column item_idx % col_count
                int col = item_idx % col_count;
                IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
                if (child_sizes.min_content > col_min[col]) col_min[col] = child_sizes.min_content;
                if (child_sizes.max_content > col_max[col]) col_max[col] = child_sizes.max_content;

                // Check for explicit fixed-width track
                if (grid_prop && grid_prop->grid_template_columns && col < col_count) {
                    GridTrackSize* track = grid_prop->grid_template_columns->tracks[col];
                    if (track && track->type == GRID_TRACK_SIZE_LENGTH && track->value > 0) {
                        // Fixed length track: the column size is the fixed value, not the content
                        float fixed_px = track->is_percentage ? 0 : (float)track->value;
                        if (fixed_px > 0) {
                            col_max[col] = fixed_px;
                            col_min[col] = fixed_px;
                        }
                    }
                }

                item_idx++;
            }

            // Check if track sizes are fixed-length (from CSS)
            // For fixed tracks, use fixed value regardless of content
            if (grid_prop && grid_prop->grid_template_columns) {
                for (int c = 0; c < col_count; c++) {
                    GridTrackSize* track = grid_prop->grid_template_columns->tracks[c];
                    if (track && track->type == GRID_TRACK_SIZE_LENGTH && !track->is_percentage && track->value > 0) {
                        col_max[c] = (float)track->value;
                        col_min[c] = (float)track->value;
                    }
                }
            }

            // Sum column sizes + gaps
            float column_gap = (grid_prop ? grid_prop->column_gap : 0.0f);
            float total_min = 0.0f, total_max = 0.0f;
            for (int c = 0; c < col_count; c++) {
                total_min += col_min[c];
                total_max += col_max[c];
            }
            if (col_count > 1 && column_gap > 0) {
                total_min += column_gap * (col_count - 1);
                total_max += column_gap * (col_count - 1);
            }

            scratch_free(&lycon->scratch, col_max);
            scratch_free(&lycon->scratch, col_min);

            // Add padding and border
            float pad_left = 0, pad_right = 0, border_left = 0, border_right = 0;
            if (view_block->bound) {
                pad_left = view_block->bound->padding.left;
                pad_right = view_block->bound->padding.right;
                if (view_block->bound->border) {
                    border_left = view_block->bound->border->width.left;
                    border_right = view_block->bound->border->width.right;
                }
            }
            total_min += pad_left + pad_right + border_left + border_right;
            total_max += pad_left + pad_right + border_left + border_right;

            log_debug("measure_element_intrinsic_widths: grid %s with %d columns: min=%.1f, max=%.1f",
                      element->node_name(), col_count, total_min, total_max);

            // Apply max-width constraint (same logic as the generic path below).
            // The grid early-return bypasses the generic constraint code, so we apply it here.
            {
                float gmax = -1;
                if (view_block->blk) gmax = view_block->blk->given_max_width;
                if (gmax < 0 && element->specified_style) {
                    CssDeclaration* mw = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_MAX_WIDTH);
                    if (mw && mw->value && mw->value->type == CSS_VALUE_TYPE_LENGTH) {
                        gmax = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, mw->value);
                    }
                }
                if (gmax >= 0) {
                    float max_bb = gmax + pad_left + pad_right + border_left + border_right;
                    if (total_max > max_bb) total_max = max_bb;
                    if (total_min > max_bb) total_min = max_bb;
                }
            }

            // Restore font
            if (font_changed) lycon->font = saved_font;
            return {total_min, total_max};
        }
    }

    is_flex_container = intrinsic_element_display_matches(
        element, view_block, CSS_VALUE_FLEX, CSS_VALUE_INLINE_FLEX);
    if (is_flex_container && view_block->display.inner != CSS_VALUE_FLEX) {
        log_debug("measure_element_intrinsic_widths: detected flex via specified_style");
    }

    // Check flex direction for row vs column
    bool is_vertical_wm = false;
    if (is_flex_container) {
        // Default flex-direction is row
        is_row_flex = true;  // Assume row by default
        if (view_block->embed && view_block->embed->flex) {
            int dir = view_block->embed->flex->direction;
            is_row_flex = (dir == CSS_VALUE_ROW || dir == CSS_VALUE_ROW_REVERSE ||
                          dir == DIR_ROW || dir == DIR_ROW_REVERSE);
            flex_gap = view_block->embed->flex->column_gap;
            is_vertical_wm = (view_block->embed->flex->writing_mode == WM_VERTICAL_LR ||
                              view_block->embed->flex->writing_mode == WM_VERTICAL_RL);
        } else if (element->specified_style) {
            intrinsic_style_flex_direction_is_row(element->specified_style, &is_row_flex);
            CssEnum wm = (CssEnum)0;
            if (intrinsic_style_keyword_declaration(
                    element->specified_style, CSS_PROPERTY_WRITING_MODE, &wm)) {
                is_vertical_wm = (wm == CSS_VALUE_VERTICAL_LR || wm == CSS_VALUE_VERTICAL_RL);
            }
            intrinsic_style_length_declaration_or_fallback(
                lycon, element->specified_style, CSS_PROPERTY_COLUMN_GAP,
                CSS_PROPERTY_GAP, CSS_PROPERTY_GAP, &flex_gap);
        }
        // In vertical writing modes, the physical axis mapping swaps:
        // column becomes horizontal, row becomes vertical
        if (is_vertical_wm) {
            is_row_flex = !is_row_flex;
            log_debug("measure_element_intrinsic_widths: vertical writing mode, flipped is_row_flex");
        }
        // Check flex-wrap to determine min-content sizing strategy.
        // CSS Flexbox §9.9.1: for wrapping flex containers, min-content main size is the
        // largest flex item's min-content contribution (not the sum).
        if (is_row_flex) {
            if (view_block->embed && view_block->embed->flex) {
                int wrap = view_block->embed->flex->wrap;
                is_flex_wrap = (wrap == CSS_VALUE_WRAP || wrap == CSS_VALUE_WRAP_REVERSE);
            } else if (element->specified_style) {
                intrinsic_style_flex_wrap_enabled(element->specified_style, &is_flex_wrap);
            }
        }
        log_debug("measure_element_intrinsic_widths: %s is_flex=%d, is_row_flex=%d, gap=%.1f, vertical_wm=%d, wrap=%d",
                  element->node_name(), is_flex_container, is_row_flex, flex_gap, is_vertical_wm, is_flex_wrap);
    }

    // Set up parent context for children to inherit definite height
    // This allows children with percentage heights and aspect-ratio to compute their width
    BlockContext* saved_parent = lycon->block.parent;
    BlockContext temp_parent = {};
    bool need_restore_parent = false;

    // Determine if this element has a definite height to propagate
    float element_definite_height = -1;

    log_debug("  -> checking height for %s: blk=%p, given_height=%.1f",
              element->node_name(),
              (void*)(view_block->blk),
              view_block->blk ? view_block->blk->given_height : -999);

    // First check for explicit height from CSS (length value)
    if (view_block->blk && view_block->blk->given_height > 0) {
        element_definite_height = view_block->blk->given_height;
        log_debug("  -> got explicit height from blk: %.1f", element_definite_height);
    } else if (element->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_HEIGHT);
        log_debug("  -> checking specified_style for height: decl=%p, value=%p",
                  (void*)height_decl, height_decl ? (void*)height_decl->value : nullptr);
        if (height_decl && height_decl->value) {
            log_debug("  -> height decl value type=%d (LENGTH=%d, PERCENTAGE=%d)",
                      height_decl->value->type, CSS_VALUE_TYPE_LENGTH, CSS_VALUE_TYPE_PERCENTAGE);
            if (height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                element_definite_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
                log_debug("  -> resolved length height: %.1f", element_definite_height);
            } else if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Check if parent has definite height for percentage resolution
                float percentage = (float)height_decl->value->data.percentage.value;
                float parent_height = intrinsic_parent_definite_height(lycon);
                log_debug("  -> percentage height: %.1f%%, parent_height=%.1f",
                          percentage, parent_height);
                if (parent_height > 0) {
                    element_definite_height = parent_height * percentage / 100.0f;
                    log_debug("  -> element percentage height resolved: %.1f%% of %.1f = %.1f",
                              percentage, parent_height, element_definite_height);
                }
            }
        }
    }

    // If this element has a definite height, propagate it to children
    if (element_definite_height > 0) {
        temp_parent.content_height = element_definite_height;
        temp_parent.given_height = element_definite_height;
        lycon->block.parent = &temp_parent;
        need_restore_parent = true;
        log_debug("  -> set up temp parent context with height=%.1f for children", element_definite_height);
    }

    // Measure children recursively
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicSizes child_sizes = {0, 0};
        bool is_inline = false;

        // CSS 2.1 §1.3: Comment nodes generate no boxes and do not participate in layout
        if (child->is_comment()) {
            continue;
        }

        if (child->is_text()) {
            const char* text = (const char*)child->text_data();
            if (text) {
                size_t text_len = strlen(text);
                // CSS 2.1 §16.6: white-space determines whether to collapse or preserve spaces
                CssEnum ws = get_white_space_value(child);
                bool preserve_spaces = (ws == CSS_VALUE_PRE || ws == CSS_VALUE_PRE_WRAP ||
                                        ws == CSS_VALUE_BREAK_SPACES);
                bool preserve_newlines = preserve_spaces || (ws == CSS_VALUE_PRE_LINE);
                bool preserve_space_after_table_cell = false;

                static thread_local char normalized_buffer[2048];  // LARGE_ARRAY_OK: static buffer — not on call stack.
                size_t out_pos = 0;

                if (preserve_spaces) {
                    // white-space: pre/pre-wrap/break-spaces — preserve all whitespace as-is
                    for (size_t i = 0; i < text_len && out_pos < sizeof(normalized_buffer) - 1; i++) {
                        normalized_buffer[out_pos++] = text[i];
                    }
                } else {
                    // white-space: normal/nowrap/pre-line — collapse consecutive spaces
                    // For pre-line: newlines are preserved as forced breaks; spaces collapse.
                    // Only trim leading whitespace if this is the first child or preceded only by whitespace.
                    // If there's inline content before this text node, leading whitespace should
                    // collapse to a single space (which contributes to intrinsic width).
                    bool has_inline_before = ((child->prev_sibling != nullptr ||
                                               has_generated_inline_before) &&
                                              has_inline_content &&
                                              inline_max_sum > 0);
                    if (node_is_table_cell_like(previous_non_whitespace_sibling(child))) {
                        bool saw_ws = false;
                        for (size_t i = 0; i < text_len; i++) {
                            unsigned char c = (unsigned char)text[i];
                            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
                                saw_ws = true;
                            } else {
                                preserve_space_after_table_cell = saw_ws;
                                break;
                            }
                        }
                    }
                    bool in_whitespace = !has_inline_before;  // Only start as in_whitespace if no inline content before
                    for (size_t i = 0; i < text_len && out_pos < sizeof(normalized_buffer) - 1; i++) {
                        unsigned char ch = (unsigned char)text[i];
                        if (ch == '\n' && preserve_newlines) {
                            // CSS Text 3 §4.1: pre-line preserves newlines as forced breaks
                            normalized_buffer[out_pos++] = '\n';
                            in_whitespace = true;  // spaces after newline collapse
                        } else if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
                            if (!in_whitespace) {
                                normalized_buffer[out_pos++] = ' ';  // Collapse to single space
                                in_whitespace = true;
                            }
                        } else {
                            normalized_buffer[out_pos++] = (char)ch;
                            in_whitespace = false;
                        }
                    }
                    // Only trim trailing whitespace if there's no inline content after this text node.
                    // Trailing whitespace before an inline sibling (like <a>) should be preserved
                    // as it contributes to the inter-word spacing.
                    bool has_inline_after = false;
                    if (child->next_sibling) {
                        DomNode* next = child->next_sibling;
                        // Skip whitespace-only text nodes
                        while (next && next->is_text()) {
                            const char* next_text = (const char*)next->text_data();
                            bool all_ws = true;
                            if (next_text) {
                                for (const char* p = next_text; *p && all_ws; p++) {
                                    unsigned char c = (unsigned char)*p;
                                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f') {
                                        all_ws = false;
                                    }
                                }
                            }
                            if (!all_ws) break;
                            next = next->next_sibling;
                        }
                        if (next && next->is_element()) {
                            // Table-cell boxes inside inline content are wrapped in anonymous
                            // inline-table boxes, so preceding collapsed spaces still render.
                            has_inline_after = is_inline_level_element(next->as_element()) ||
                                (is_inline_level_element(element) && node_is_table_cell_like(next));
                        }
                    }
                    if (!has_inline_after) {
                        // Trim trailing whitespace
                        while (out_pos > 0 && normalized_buffer[out_pos - 1] == ' ') {
                            out_pos--;
                        }
                    }
                    if (out_pos == 1 && normalized_buffer[0] == ' ' &&
                        node_is_table_cell_like(previous_non_whitespace_sibling(child))) {
                        // CSS anonymous table layout discards whitespace following table-cell
                        // boxes; intrinsic sizing must match the rendered inline run.
                        out_pos = 0;
                    }
                }
                normalized_buffer[out_pos] = '\0';

                // Skip if all whitespace (out_pos == 0)
                if (out_pos == 0) {
                    continue;
                }

                // Get text-transform from parent element (text inherits from parent)
                CssEnum text_transform = get_element_text_transform(element);
                CssEnum font_variant = get_element_font_variant(element);

                // Get overflow-wrap from element or ancestors (inherited property)
                // CSS Text 3 §5.2: word-break: break-word is a deprecated keyword that
                // has the same effect as word-break: normal and overflow-wrap: anywhere.
                // Get overflow-wrap and word-break from element or ancestors (inherited properties)
                CssEnum ow = CSS_VALUE_NORMAL;
                CssEnum wb = CSS_VALUE_NORMAL;
                {
                    DomNode* n = static_cast<DomNode*>(element);
                    while (n) {
                        if (n->is_element()) {
                            DomElement* el = lam::dom_require_element(n);
                            if (el->blk) {
                                // Capture word-break from nearest block ancestor
                                if (el->blk->word_break != 0 && wb == CSS_VALUE_NORMAL) {
                                    wb = el->blk->word_break;
                                }
                                // Original overflow-wrap resolution (unchanged)
                                if (el->blk->overflow_wrap != 0) {
                                    ow = el->blk->overflow_wrap;
                                    break;
                                }
                                if (el->blk->word_break == CSS_VALUE_BREAK_WORD) {
                                    ow = CSS_VALUE_ANYWHERE;
                                    break;
                                }
                            }
                        }
                        n = n->parent;
                    }
                }

                TextIntrinsicWidths text_widths;
                float tab_size = (view_block->blk && view_block->blk->tab_size >= 0)
                    ? view_block->blk->tab_size : 8.0f;
                if (preserve_newlines) {
                    // For pre/pre-wrap/break-spaces/pre-line: newlines create forced line breaks.
                    // Measure each line separately; max-content = widest line.
                    text_widths = {0, 0};
                    const char* line_start = normalized_buffer;
                    const char* buf_end = normalized_buffer + out_pos;
                    int line_count = 0;
                    float first_line_width = 0;
                    float last_line_width = 0;
                    while (line_start <= buf_end) {
                        const char* nl = (const char*)memchr(line_start, '\n', buf_end - line_start);
                        size_t line_len = nl ? (size_t)(nl - line_start) : (size_t)(buf_end - line_start);
                        float line_width = 0;
                        if (line_len > 0) {
                            TextIntrinsicWidths lw = measure_text_intrinsic_widths(
                                lycon, line_start, line_len, text_transform, font_variant, ws, ow, wb);
                            if (preserve_spaces && text_line_has_tab(line_start, line_len)) {
                                float start_offset = (line_count == 0) ? inline_max_sum : 0.0f;
                                lw.max_content = measure_preserved_line_width_with_tabs(
                                    lycon, line_start, line_len, start_offset,
                                    text_transform, font_variant, tab_size);
                            }
                            if (lw.max_content > text_widths.max_content)
                                text_widths.max_content = lw.max_content;
                            if (lw.min_content > text_widths.min_content)
                                text_widths.min_content = lw.min_content;
                            line_width = lw.max_content;
                        }
                        if (line_count == 0) first_line_width = line_width;
                        last_line_width = line_width;
                        line_count++;
                        if (!nl) break;
                        line_start = nl + 1;
                    }
                    // CSS Text 3 §5.2: Track forced breaks for inline run accumulation.
                    // When a preserved newline creates a forced break, the inline run
                    // at the parent level must be split at that point.
                    if (line_count > 1) {
                        child_sizes.has_forced_break = true;
                        child_sizes.first_line_max = first_line_width;
                        child_sizes.last_line_max = last_line_width;
                    }
                } else {
                    text_widths = measure_text_intrinsic_widths(
                        lycon, normalized_buffer, out_pos, text_transform, font_variant,
                        CSS_VALUE_NORMAL, ow, wb);
                    if (preserve_space_after_table_cell &&
                        (out_pos == 0 || normalized_buffer[0] != ' ')) {
                        text_widths.max_content += layout_measure_space_advance(
                            lycon, lycon->font.font_handle, lycon->font.style);
                    }
                    if (preserve_spaces && text_line_has_tab(normalized_buffer, out_pos)) {
                        text_widths.max_content = measure_preserved_line_width_with_tabs(
                            lycon, normalized_buffer, out_pos, inline_max_sum,
                            text_transform, font_variant, tab_size);
                    }
                }
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;

                // white-space: nowrap/pre prevents line breaks, so min-content = max-content
                if (ws == CSS_VALUE_NOWRAP || ws == CSS_VALUE_PRE) {
                    child_sizes.min_content = child_sizes.max_content;
                }

                // In vertical writing mode, text flows top-to-bottom.
                // Physical width = font_size (one column of characters),
                // physical height = text inline extent.
                if (is_vertical_wm && is_flex_container) {
                    float font_size = lycon->font.style ? lycon->font.style->font_size : 16.0f;
                    child_sizes.min_content = font_size;
                    child_sizes.max_content = font_size;
                    log_debug("  vertical writing mode: text width -> font_size=%.1f", font_size);
                }

                // In flex containers, text nodes become anonymous flex items
                if (is_flex_container) {
                    log_debug("  flex text child: min=%.1f, max=%.1f, normalized_len=%zu, text='%.30s...'",
                              child_sizes.min_content, child_sizes.max_content, out_pos, normalized_buffer);
                }
            }
            is_inline = true;  // Text nodes are always inline (unless in flex container)
        } else if (child->is_element()) {
            DomElement* child_elem = child->as_element();

            // CSS 2.1 §9.2.4: Skip display:none children — they generate no boxes
            ViewBlock* child_vb = lam::unsafe_view_block_element_storage(child_elem);
            if (layout_block_is_display_none(child_vb)) {
                continue;
            }

            // CSS 2.1 §9.3.1: Absolutely positioned elements are out of normal flow
            // and do not contribute to the containing block's intrinsic size.
            if (layout_block_is_out_of_flow_positioned(child_vb)) {
                log_debug("  skipping absolute/fixed child %s in intrinsic sizing", child_elem->node_name());
                continue;
            }
            if (!child_vb->position && child_elem->specified_style) {
                CssDeclaration* pos_decl = style_tree_get_declaration(
                    child_elem->specified_style, CSS_PROPERTY_POSITION);
                if (pos_decl && pos_decl->value && pos_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum pos_val = pos_decl->value->data.keyword;
                    if (pos_val == CSS_VALUE_ABSOLUTE || pos_val == CSS_VALUE_FIXED) {
                        log_debug("  skipping absolute/fixed child %s in intrinsic sizing (from specified style)", child_elem->node_name());
                        continue;
                    }
                }
            }

            child_sizes = measure_element_intrinsic_widths(lycon, child_elem);

            // Re-check display:none after measurement (display may be resolved during measurement)
            if (layout_block_is_display_none(child_vb)) {
                continue;
            }

            // CSS 2.1 §17.2.1 wraps table-cell boxes in anonymous table boxes;
            // inside an inline parent those wrappers participate as inline-table.
            is_inline = is_inline_level_element(child_elem) ||
                (is_inline_level_element(element) && node_is_table_cell_like(child));

            log_debug("  child %s: min=%.1f, max=%.1f, is_inline=%d",
                      child_elem->node_name(), child_sizes.min_content, child_sizes.max_content, is_inline);

            // For inline elements, also add horizontal margins
            // CSS Sizing 3 §4: Negative margins reduce the element's outer size contribution
            if (is_inline) {
                ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
                if (child_view->bound) {
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO) {
                        child_sizes.max_content += child_view->bound->margin.left;
                    }
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO) {
                        child_sizes.max_content += child_view->bound->margin.right;
                    }
                } else if (child_elem->specified_style) {
                    // Fallback: read margins from specified CSS style when bound isn't allocated
                    float ml = 0, mr = 0;
                    intrinsic_resolve_specified_horizontal_margins(
                        lycon, child_elem, false, &ml, &mr);
                    child_sizes.max_content += ml + mr;
                    child_sizes.min_content += ml + mr;
                }
            }
        }

        // Handle flex container children - all children become flex items
        // In a flex container, both text and element children are flex items
        // They should NOT go through the inline content path
        if (is_flex_container) {
            // CSS Flexbox §4: Absolutely positioned children are out-of-flow
            // and do not participate in flex layout or contribute to intrinsic size
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_block = lam::unsafe_view_block_element_storage(child_elem);
                bool child_is_absolute = false;
                if (layout_block_is_out_of_flow_positioned(child_block)) {
                    child_is_absolute = true;
                } else if (child_elem->specified_style) {
                    CssDeclaration* pos_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_POSITION);
                    if (pos_decl && pos_decl->value && pos_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum pos_val = pos_decl->value->data.keyword;
                        child_is_absolute = (pos_val == CSS_VALUE_ABSOLUTE || pos_val == CSS_VALUE_FIXED);
                    }
                }
                if (child_is_absolute) {
                    log_debug("  skipping absolute child %s in flex intrinsic sizing", child_elem->node_name());
                    continue;
                }
            }

            // CSS Flexbox §9.9.1: Flex item intrinsic contributions include
            // the item's outer size (content + padding + border + margin).
            // Add flex item margins to child_sizes before accumulating.
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
                float ml = 0, mr = 0;
                if (child_view->bound) {
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.left >= 0)
                        ml = child_view->bound->margin.left;
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO &&
                        child_view->bound->margin.right >= 0)
                        mr = child_view->bound->margin.right;
                } else if (child_elem->specified_style) {
                    intrinsic_resolve_specified_horizontal_margins(
                        lycon, child_elem, true, &ml, &mr);
                }
                child_sizes.min_content += ml + mr;
                child_sizes.max_content += ml + mr;
            }

            if (is_row_flex) {
                // Row flex: for wrapping containers, min-content is the largest item
                // (CSS Flexbox §9.9.1). For nowrap, sum all items.
                if (is_flex_wrap) {
                    sizes.min_content = max(sizes.min_content, child_sizes.min_content);
                } else {
                    sizes.min_content += child_sizes.min_content;
                }
                sizes.max_content += child_sizes.max_content;
                flex_child_count++;
                log_debug("  row flex item: min=%.1f, max=%.1f, count=%d, wrap=%d",
                          child_sizes.min_content, child_sizes.max_content, flex_child_count, is_flex_wrap);
            } else {
                // Column flex: take max of widths
                sizes.min_content = max(sizes.min_content, child_sizes.min_content);
                sizes.max_content = max(sizes.max_content, child_sizes.max_content);
                flex_child_count++;
            }
        } else if (is_inline) {
            // For inline content, sum widths for max-content (no wrapping)
            // and take max of min-content (can wrap between items)
            has_inline_content = true;

            bool is_collapsible_space_only = intrinsic_node_is_collapsible_space_only(child);
            if (is_collapsible_space_only) {
                // Recursive inline measurement trims edge spaces inside each inline
                // box, so the parent must own collapsed whitespace runs across boxes.
                if (inline_max_sum > 0.0f && !inline_run_ends_with_collapsible_space &&
                    intrinsic_has_following_inline_boundary_content(child)) {
                    inline_max_sum += intrinsic_collapsed_space_width(lycon);
                    inline_run_ends_with_collapsible_space = true;
                }
                continue;
            }

            // Detect <br> elements as forced line breaks. Unlike text nodes with
            // preserved newlines, <br> is an element that doesn't set has_forced_break
            // on its own IntrinsicSizes. Handle it directly as a forced break point.
            bool is_br_element = child->is_element() &&
                child->as_element()->node_name() &&
                strcmp(child->as_element()->node_name(), "br") == 0;
            if (is_br_element) {
                // Apply text-indent to the first line before flushing.
                // Text-indent only affects the first formatted line.
                if (!inline_has_forced_break && text_indent != 0) {
                    intrinsic_apply_first_line_indent(text_indent, first_inline_child_min,
                        nonfirst_inline_min_max, &inline_max_sum, &inline_min_sum);
                }
                // Record inline_max_sum at the first forced break for parent propagation
                if (!inline_has_forced_break) {
                    first_inline_break_sum = inline_max_sum;
                    inline_has_forced_break = true;
                }
                // Flush the current inline run
                sizes.max_content = max(sizes.max_content, inline_max_sum);
                sizes.min_content = max(sizes.min_content, inline_min_sum);
                // Start new inline run after the break
                inline_max_sum = 0;
                inline_min_sum = 0;
                inline_run_ends_with_collapsible_space = false;
                // Don't reset first_inline_child_min — text-indent only applies to first line
                continue;
            }

            // CSS Text 3 §5.2: If this inline child has forced line breaks (preserved
            // newlines in pre/pre-wrap), split the inline run at the break point.
            // The first line of the child adds to the current inline run; the forced
            // break flushes the run; the last line starts a new run.
            if (child_sizes.has_forced_break) {
                // Add first line to current inline run
                inline_max_sum += child_sizes.first_line_max;
                // CSS 2.1 §16.1: Apply text-indent to the first line before
                // flushing at the forced break. Text-indent only affects the
                // first formatted line, so only apply when this is the first
                // forced break encountered.
                if (!inline_has_forced_break && text_indent != 0) {
                    intrinsic_apply_first_line_indent(text_indent, first_inline_child_min,
                        nonfirst_inline_min_max, &inline_max_sum, &inline_min_sum);
                }
                // Record inline_max_sum at the first forced break for parent propagation
                if (!inline_has_forced_break) {
                    first_inline_break_sum = inline_max_sum;
                    inline_has_forced_break = true;
                }
                // Flush the current inline run (this is a forced line break)
                sizes.max_content = max(sizes.max_content, inline_max_sum);
                // The internal max-content covers the widest internal line
                sizes.max_content = max(sizes.max_content, child_sizes.max_content);
                // Start new inline run with the last line width
                inline_max_sum = child_sizes.last_line_max;
                inline_run_ends_with_collapsible_space = false;
            } else {
                inline_max_sum += child_sizes.max_content;
                inline_run_ends_with_collapsible_space =
                    child_sizes.max_content > 0.0f && intrinsic_node_ends_with_collapsible_space(child);
            }
            inline_min_sum = max(inline_min_sum, child_sizes.min_content);
            // Track first inline child's min-content for text-indent calculation
            if (first_inline_child_min < 0) {
                first_inline_child_min = child_sizes.min_content;
            } else {
                nonfirst_inline_min_max = max(nonfirst_inline_min_max, child_sizes.min_content);
            }
        } else {
            // Block-level child encountered

            // Detect if this block child is floated BEFORE flushing inline content.
            // CSS 2.1 §9.5: Floats don't break inline flow — inline content wraps
            // around them. So preceding inline content must NOT be flushed when we
            // encounter a floated child; it stays in the inline run and will be
            // combined with float_width_alongside_inline at the end.
            bool child_is_float = false;
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
                // Check resolved position first
                if (child_view->position &&
                    !layout_position_is_abs_fixed(child_view->position) &&
                    layout_position_is_floated(child_view->position)) {
                    child_is_float = true;
                } else if (child_elem->specified_style) {
                    // Fall back to specified CSS style
                    CssDeclaration* float_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_FLOAT);
                    if (float_decl && float_decl->value &&
                        float_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum float_val = float_decl->value->data.keyword;
                        if (float_val == CSS_VALUE_LEFT || float_val == CSS_VALUE_RIGHT) {
                            child_is_float = true;
                        }
                    }
                }
            }

            // CSS 2.1: Non-floated block children break inline flow (block-in-inline).
            // Floats do NOT break inline flow — inline content wraps around them.
            // Only flush inline content for non-floated block children.
            if (!child_is_float && inline_max_sum > 0) {
                sizes.min_content = max(sizes.min_content, inline_min_sum);
                sizes.max_content = max(sizes.max_content, inline_max_sum);
                log_debug("  block-in-inline: flushing inline run max=%.1f, min=%.1f before block %s",
                          inline_max_sum, inline_min_sum,
                          child->is_element() ? child->as_element()->node_name() : "?");
                // Reset for next inline run after the block
                inline_max_sum = 0;
                inline_min_sum = 0;
                inline_run_ends_with_collapsible_space = false;
            }

            // For block-level children: take max of each
            // Also include horizontal margins for proper shrink-to-fit calculation
            // CSS 2.2 Section 10.3.5: floated/absolutely positioned elements use shrink-to-fit
            // which includes the margin box of child floats
            float child_width = child_sizes.max_content;
            float margin_left = 0, margin_right = 0;
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
                if (child_view->bound) {
                    // Add margins to the child's width for proper shrink-to-fit
                    // CSS Sizing 3 §4: Negative margins reduce the element's outer size
                    if (child_view->bound->margin.left_type != CSS_VALUE_AUTO) {
                        margin_left = child_view->bound->margin.left;
                        child_width += margin_left;
                    }
                    if (child_view->bound->margin.right_type != CSS_VALUE_AUTO) {
                        margin_right = child_view->bound->margin.right;
                        child_width += margin_right;
                    }
                    log_debug("  block child %s: max=%.1f, margins=(%.1f,%.1f) from bound, total=%.1f",
                              child_elem->node_name(), child_sizes.max_content, margin_left, margin_right, child_width);
                } else if (child_elem->specified_style) {
                    // Read margins directly from specified CSS style
                    // This handles the case during intrinsic sizing when bound isn't allocated yet
                    // First check for individual margin properties
                    CssDeclaration* margin_left_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_LEFT);
                    if (margin_left_decl && margin_left_decl->value &&
                        margin_left_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_LEFT,
                                                           margin_left_decl->value);
                        child_width += margin_left;
                    }
                    CssDeclaration* margin_right_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_MARGIN_RIGHT);
                    if (margin_right_decl && margin_right_decl->value &&
                        margin_right_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                        margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_RIGHT,
                                                            margin_right_decl->value);
                        child_width += margin_right;
                    }
                    // If individual properties not found, check shorthand margin property
                    if (margin_left == 0 && margin_right == 0) {
                        CssDeclaration* margin_decl = style_tree_get_declaration(
                            child_elem->specified_style, CSS_PROPERTY_MARGIN);
                        if (margin_decl && margin_decl->value) {
                            // Handle shorthand: margin: value or margin: v1 v2 v3 v4
                            const CssValue* val = margin_decl->value;
                            const CssValue* right_value = css_box_shorthand_side_value(val, 1);
                            const CssValue* left_value = css_box_shorthand_side_value(val, 3);
                            if (right_value) {
                                margin_right = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, right_value);
                            }
                            if (left_value) {
                                margin_left = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, left_value);
                            }
                            if (right_value || left_value) {
                                child_width += margin_left + margin_right;
                            }
                        }
                    }
                    log_debug("  block child %s: max=%.1f, margins=(%.1f,%.1f) from CSS, total=%.1f",
                              child_elem->node_name(), child_sizes.max_content, margin_left, margin_right, child_width);
                } else {
                    log_debug("  block child %s: max=%.1f, no bound or specified_style",
                              child_elem->node_name(), child_sizes.max_content);
                }
            }
            if (child_is_float) {
                // Floated block child: accumulate for side-by-side arrangement
                // At max-content (infinite width), all floats fit on one line → sum widths
                // At min-content, each float wraps to its own line → max of widths
                // Float layout keeps shrink-to-fit widths at subpixel precision;
                // pre-rounding each child can make a measured-fitting float run
                // wrap once the shrink-wrapped parent is laid out.
                float_max_sum += child_width;
                float_width_alongside_inline += child_width;
                float_min_max = max(float_min_max, child_sizes.min_content);
                log_debug("  float child: accumulating max_sum=%.1f, min_max=%.1f",
                          float_max_sum, float_min_max);
            } else {
                // Non-floated block child: flush any accumulated float run first
                if (float_max_sum > 0) {
                    sizes.min_content = max(sizes.min_content, float_min_max);
                    sizes.max_content = max(sizes.max_content, float_max_sum);
                    log_debug("  flushing float run: max_sum=%.1f, min_max=%.1f before block %s",
                              float_max_sum, float_min_max,
                              child->is_element() ? child->as_element()->node_name() : "?");
                    float_max_sum = 0;
                    float_min_max = 0;
                }
                // A non-float block child creates a full-width line, separating
                // preceding floats from subsequent inline content.
                float_width_alongside_inline = 0;
                sizes.min_content = max(sizes.min_content, child_sizes.min_content);
                sizes.max_content = max(sizes.max_content, child_width);
            }
        }
    }

    // Flush any remaining float run after the child loop
    if (float_max_sum > 0) {
        sizes.min_content = max(sizes.min_content, float_min_max);
        sizes.max_content = max(sizes.max_content, float_max_sum);
        log_debug("  flushing final float run: max_sum=%.1f, min_max=%.1f",
                  float_max_sum, float_min_max);
    }

    // Restore parent context
    if (need_restore_parent) {
        lycon->block.parent = saved_parent;
    }

    // Add gaps for row flex containers
    if (is_row_flex && flex_child_count > 1 && flex_gap > 0) {
        float total_gap = flex_gap * (flex_child_count - 1);
        // For wrapping flex, gaps don't apply to min-content (each item on its own line)
        if (!is_flex_wrap) {
            sizes.min_content += total_gap;
        }
        sizes.max_content += total_gap;
        log_debug("  row flex gap: %d items, %.1fpx gap = %.0fpx total, wrap=%d",
                  flex_child_count, flex_gap, total_gap, is_flex_wrap);
    }

    // CSS 2.1 §12.5.1: For display:list-item with list-style-position:inside,
    // the marker box is the first inline box in the principal block box.
    // It contributes to the intrinsic width as inline content.
    if (view_block->display.outer == CSS_VALUE_LIST_ITEM) {
        bool is_inside_position = false;
        bool has_marker = true;  // default list-style-type is 'disc'

        // Check list-style-position — it's an inherited property, so walk up
        // the ancestor chain if not found directly on this element.
        if (view_block->blk && view_block->blk->list_style_position == 1) {  // 1 = inside
            is_inside_position = true;
        } else {
            // Walk ancestor chain looking for list-style-position (inherited property)
            for (DomNode* anc = static_cast<DomNode*>(element); anc; anc = anc->parent) {
                if (!anc->is_element()) continue;
                DomElement* anc_elem = anc->as_element();
                // Check resolved blk first (if available)
                ViewBlock* anc_view = lam::unsafe_view_block_element_storage(anc_elem);
                if (anc_view->blk && anc_view->blk->list_style_position == 1) {
                    is_inside_position = true;
                    break;
                }
                if (anc_view->blk && anc_view->blk->list_style_position == 2) {
                    break;  // explicitly 'outside'
                }
                // Check specified style
                if (anc_elem->specified_style) {
                    CssDeclaration* lsp_decl = style_tree_get_declaration(
                        anc_elem->specified_style, CSS_PROPERTY_LIST_STYLE_POSITION);
                    if (lsp_decl && lsp_decl->value && lsp_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum lsp_val = lsp_decl->value->data.keyword;
                        if (lsp_val == 1) {  // 1 = inside
                            is_inside_position = true;
                        }
                        break;  // found an explicit value, stop searching
                    }
                }
            }
        }

        // Check list-style-type (if 'none', no marker) — also inherited
        if (view_block->blk && view_block->blk->list_style_type == CSS_VALUE_NONE) {
            has_marker = false;
        } else {
            for (DomNode* anc = static_cast<DomNode*>(element); anc; anc = anc->parent) {
                if (!anc->is_element()) continue;
                DomElement* anc_elem = anc->as_element();
                ViewBlock* anc_view = lam::unsafe_view_block_element_storage(anc_elem);
                if (anc_view->blk && anc_view->blk->list_style_type == CSS_VALUE_NONE) {
                    has_marker = false;
                    break;
                }
                if (anc_view->blk && anc_view->blk->list_style_type != 0) {
                    break;  // has a non-none type, stop
                }
                if (anc_elem->specified_style) {
                    CssDeclaration* lst_decl = style_tree_get_declaration(
                        anc_elem->specified_style, CSS_PROPERTY_LIST_STYLE_TYPE);
                    if (lst_decl && lst_decl->value && lst_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        if (lst_decl->value->data.keyword == CSS_VALUE_NONE) {
                            has_marker = false;
                        }
                        break;  // found explicit value
                    }
                }
            }
        }

        if (is_inside_position && has_marker) {
            // Marker width = font_size * 1.375 (matching layout_block.cpp marker creation)
            float font_size = 16.0f;  // default
            if (view_block->font && view_block->font->font_size > 0) {
                font_size = view_block->font->font_size;
            } else if (lycon->font.current_font_size > 0) {
                font_size = lycon->font.current_font_size;
            }
            float marker_width = font_size * 1.375f;

            // The marker is the first inline box — add to inline content accumulators
            has_inline_content = true;
            inline_max_sum += marker_width;
            inline_min_sum = max(inline_min_sum, marker_width);
            log_debug("  list-item inside marker: added %.1fpx marker width (font=%.1f)",
                      marker_width, font_size);
        }
    }

    // CSS Generated Content §2: ::before/::after pseudo-elements generate boxes that
    // participate in the element's inline formatting context. Their outer width (content +
    // border + padding + margin) contributes to the element's intrinsic inline size.
    {
        float pseudo_extra_width = 0;
        for (int pi = 1; pi < 2; pi++) {
            bool is_before = (pi == 0);
            if (intrinsic_pseudo_child_is_materialized(element, is_before)) continue;
            bool has_pseudo = is_before ? dom_element_has_before_content(element)
                                        : dom_element_has_after_content(element);
            if (!has_pseudo) continue;

            StyleTree* ps = is_before ? element->before_styles : element->after_styles;
            if (!ps || !ps->tree) continue;

            // CSS Generated Content §2: Pseudo-elements default to display:inline.
            // If display:block/table/list-item, the pseudo generates a block-level box
            // that doesn't contribute to inline content sum — skip it here.
            if (!intrinsic_pseudo_style_is_inline(ps)) continue;

            // Measure content text width
            const char* pc = dom_element_get_pseudo_element_content(element,
                is_before ? 1 /*PSEUDO_ELEMENT_BEFORE*/ : 2 /*PSEUDO_ELEMENT_AFTER*/);
            TextIntrinsicWidths content_widths = intrinsic_measure_pseudo_text_widths(lycon, ps, pc);
            float content_w = content_widths.max_content;

            float border_l = 0.0f;
            float border_r = 0.0f;
            intrinsic_pseudo_horizontal_border_widths(lycon, ps, &border_l, &border_r);

            // Resolve horizontal padding
            float pad_l = 0.0f;
            float pad_r = 0.0f;
            intrinsic_pseudo_positive_length_decl(
                lycon, ps, CSS_PROPERTY_PADDING_LEFT, &pad_l, false);
            intrinsic_pseudo_positive_length_decl(
                lycon, ps, CSS_PROPERTY_PADDING_RIGHT, &pad_r, false);

            // Resolve horizontal margin
            float mar_l = 0.0f;
            float mar_r = 0.0f;
            intrinsic_pseudo_positive_length_decl(
                lycon, ps, CSS_PROPERTY_MARGIN_LEFT, &mar_l, false);
            intrinsic_pseudo_positive_length_decl(
                lycon, ps, CSS_PROPERTY_MARGIN_RIGHT, &mar_r, false);

            float outer_w = content_w + border_l + border_r + pad_l + pad_r + mar_l + mar_r;
            if (outer_w > 0) {
                pseudo_extra_width += outer_w;
                log_debug("  pseudo ::%s intrinsic width: content=%.1f border=%.1f+%.1f pad=%.1f+%.1f margin=%.1f+%.1f outer=%.1f",
                          is_before ? "before" : "after", content_w, border_l, border_r, pad_l, pad_r, mar_l, mar_r, outer_w);
            }
        }

        if (pseudo_extra_width > 0) {
            has_inline_content = true;
            inline_max_sum += pseudo_extra_width;
            inline_min_sum += pseudo_extra_width;
            log_debug("  pseudo-element total width: %.1f added to inline content", pseudo_extra_width);
        }
    }

    // Merge inline content measurements
    if (has_inline_content) {
        // CSS 2.1 §16.1: text-indent applies to the first formatted line.
        // If a forced break (<br>) already flushed the first line with text-indent
        // applied, skip indent here — it only applies to the first line.
        if (!inline_has_forced_break) {
            // For max-content: single unwrapped line, text-indent adds to total.
            // For min-content: text-indent only applies to the first breakable segment.
            //   min_content = max(first_segment + text_indent, widest_other_segment)
            //   NOT max(widest_segment) + text_indent (which overcounts).
            if (text_indent > 0) {
                inline_max_sum += text_indent;
                float first_seg_min = (first_inline_child_min >= 0) ? first_inline_child_min : inline_min_sum;
                float first_line_min = first_seg_min + text_indent;
                inline_min_sum = fmaxf(first_line_min, inline_min_sum);
            } else if (text_indent < 0) {
                // Negative text-indent: first line is narrower, but min/max content
                // cannot go below zero from the indent alone.
                inline_max_sum = fmaxf(inline_max_sum + text_indent, 0.0f);
                // For min-content with negative indent: the first segment + indent
                // may be narrower. Use max(first_seg + indent, max_of_nonfirst_segs).
                float first_seg_min = (first_inline_child_min >= 0) ? first_inline_child_min : inline_min_sum;
                float first_line_min = fmaxf(first_seg_min + text_indent, 0.0f);
                inline_min_sum = fmaxf(first_line_min, nonfirst_inline_min_max);
            }
        }
        sizes.min_content = max(sizes.min_content, inline_min_sum);
        // CSS 2.1 §9.5 + CSS Sizing 3 §4: At max-content (infinite width),
        // floats and inline content share the same line. The total width needed
        // is the sum of float widths + inline content width.
        sizes.max_content = max(sizes.max_content, inline_max_sum + float_width_alongside_inline);
        // For min-content, floats stack vertically so inline content gets full width
        // (float_width not added to inline_min_sum)

        // CSS Text 3 §5.2: Propagate forced break info for inline spans.
        // This allows the parent to split its inline run when this element
        // contains preserved newlines that create forced line breaks.
        if (inline_has_forced_break) {
            sizes.has_forced_break = true;
            sizes.first_line_max = first_inline_break_sum;
            sizes.last_line_max = inline_max_sum;
        }
        log_debug("  inline_max_sum=%.1f, inline_min_sum=%.1f, text_indent=%.1f",
                  inline_max_sum, inline_min_sum, text_indent);
    }

    // Add padding and border
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    float pad_left = 0, pad_right = 0;
    float border_left = 0, border_right = 0;

    if (view->bound) {
        log_debug("measure_element_intrinsic: %s has bound allocated", element->node_name());
        if (view->bound->padding.left >= 0) pad_left = view->bound->padding.left;
        if (view->bound->padding.right >= 0) pad_right = view->bound->padding.right;
        if (view->bound->border) {
            border_left = view->bound->border->width.left;
            border_right = view->bound->border->width.right;
        }
    } else if (element->specified_style) {
        get_horizontal_padding_widths_from_css(
            lycon, element, lycon->block.content_width, &pad_left, &pad_right);
        get_horizontal_border_widths_from_css(lycon, element, &border_left, &border_right);
    }

    float horiz_padding = pad_left + pad_right;
    float horiz_border = border_left + border_right;
    if (sizes.replaced_includes_pad_border) {
        log_debug("  -> skipping pad/border add: replaced form control already border-box");
    } else {
        sizes.max_content += horiz_padding + horiz_border;
        if (!sizes.replaced_min_excludes_pad_border) {
            sizes.min_content += horiz_padding + horiz_border;
        } else {
            log_debug("  -> asymmetric pad/border: max+=%.1f, min unchanged",
                      horiz_padding + horiz_border);
        }
    }

    // CSS 2.1 §8.3: For inline elements with forced breaks, the left padding/border
    // applies to the first line and the right padding/border to the last line.
    if (sizes.has_forced_break) {
        sizes.first_line_max += pad_left + border_left;
        sizes.last_line_max += pad_right + border_right;
    }

    // CSS 2.1 §10.4: Apply min-width and max-width constraints to intrinsic sizes.
    // min-width sets a floor: an element can never be narrower than its min-width,
    // so its intrinsic sizes must reflect this constraint.
    // max-width constrains the preferred (max-content) width.
    ViewBlock* view_for_minmax = lam::unsafe_view_block_element_storage(element);
    bool view_is_border_box = layout_uses_border_box(view_for_minmax);
    // Fallback: check CSS if blk hasn't been set up yet
    if (!view_is_border_box && element->specified_style) {
        CssDeclaration* bs_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_BOX_SIZING);
        if (bs_decl && bs_decl->value &&
            bs_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            bs_decl->value->data.keyword == CSS_VALUE_BORDER_BOX) {
            view_is_border_box = true;
        }
    }

    // Apply min-width: floor the intrinsic sizes to ensure they're at least min-width
    float given_min_width = -1;
    if (view_for_minmax->blk) {
        given_min_width = view_for_minmax->blk->given_min_width;
    }
    // Fallback: check CSS if blk hasn't been set up yet
    if (given_min_width < 0 && element->specified_style) {
        CssDeclaration* min_mw_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MIN_WIDTH);
        if (min_mw_decl && min_mw_decl->value &&
            min_mw_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            given_min_width = resolve_length_value(lycon, CSS_PROPERTY_MIN_WIDTH, min_mw_decl->value);
        }
    }
    if (given_min_width > 0) {
        // For border-box: min-width is the border-box total (includes padding+border)
        // For content-box: min-width is content width, so add padding+border for border-box comparison
        float min_border_box = view_is_border_box ? given_min_width
                                                  : (given_min_width + horiz_padding + horiz_border);
        if (sizes.min_content < min_border_box) {
            sizes.min_content = min_border_box;
            log_debug("  -> min-width constraint: raised min_content to %.1f", sizes.min_content);
        }
        if (sizes.max_content < min_border_box) {
            sizes.max_content = min_border_box;
            log_debug("  -> min-width constraint: raised max_content to %.1f", sizes.max_content);
        }
    }
    float given_max_width = -1;
    if (view_for_minmax->blk) {
        given_max_width = view_for_minmax->blk->given_max_width;
    }
    // Fallback: check CSS if blk hasn't been set up yet
    if (given_max_width < 0 && element->specified_style) {
        CssDeclaration* mw_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MAX_WIDTH);
        if (mw_decl && mw_decl->value && mw_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            given_max_width = resolve_length_value(lycon, CSS_PROPERTY_MAX_WIDTH, mw_decl->value);
        }
    }
    if (given_max_width >= 0) {
        // For content-box: max-width is content width, so add padding+border for comparison
        float max_border_box = given_max_width + horiz_padding + horiz_border;
        if (sizes.max_content > max_border_box) {
            sizes.max_content = max_border_box;
            log_debug("  -> max-width constraint: clamped max_content to %.1f", sizes.max_content);
        }
        // CSS Tables §4.1: Table content-box inline size is never smaller than its
        // minimum content inline size, so don't clamp min_content for tables.
        if (!is_table_display && sizes.min_content > max_border_box) {
            sizes.min_content = max_border_box;
        }
    }

    // Restore parent font context
    if (font_changed) {
        lycon->font = saved_font;
    }

    // store result in intrinsic sizing cache
    if (!content_only && element->styles_resolved) {
        element->cached_min_content_width = sizes.min_content;
        element->cached_max_content_width = sizes.max_content;
        element->has_cached_intrinsic_widths = true;
    }

    auto t_measure_end = std::chrono::high_resolution_clock::now();
    double measure_ms = std::chrono::duration<double, std::milli>(t_measure_end - t_measure_start).count();
    if (measure_ms > 100.0) {
        log_warn("SLOW MEASURE: %s took %.0fms", element->source_loc(), measure_ms);
    }

    return sizes;
}

// ============================================================================
// Main API Implementation
// ============================================================================

static IntrinsicSizes calculate_node_intrinsic_widths(LayoutContext* lycon, DomNode* node) {
    IntrinsicSizes sizes = {};
    if (!node) return sizes;

    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (text) {
            CssEnum text_transform = CSS_VALUE_NONE;
            CssEnum font_variant = CSS_VALUE_NONE;
            if (node->parent && node->parent->is_element()) {
                text_transform = get_element_text_transform(node->parent->as_element());
                font_variant = get_element_font_variant(node->parent->as_element());
            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(
                lycon, text, strlen(text), text_transform, font_variant);
            sizes.min_content = widths.min_content;
            sizes.max_content = widths.max_content;
        }
        return sizes;
    }

    DomElement* element = node->as_element();
    return element ? measure_element_intrinsic_widths(lycon, element) : sizes;
}

float calculate_min_content_width(LayoutContext* lycon, DomNode* node) {
    return calculate_node_intrinsic_widths(lycon, node).min_content;
}

float calculate_max_content_width(LayoutContext* lycon, DomNode* node) {
    return calculate_node_intrinsic_widths(lycon, node).max_content;
}

float calculate_min_content_height(LayoutContext* lycon, DomNode* node, float width) {
    // For block containers, min-content height == max-content height
    // (CSS Sizing Level 3: https://www.w3.org/TR/css-sizing-3/#min-content-block-size)
    return calculate_max_content_height(lycon, node, width);
}

static bool intrinsic_height_should_collapse_whitespace(CssEnum white_space) {
    return white_space == CSS_VALUE_NORMAL || white_space == CSS_VALUE_NOWRAP ||
           white_space == CSS_VALUE_PRE_LINE || white_space == 0;
}

float calculate_max_content_height(LayoutContext* lycon, DomNode* node, float width) {
    if (!node) return 0;

    // For text nodes, estimate based on line height and text width
    if (node->is_text()) {
        // Check if text is whitespace-only (shouldn't contribute to height in block context)
        const char* text = (const char*)node->text_data();
        if (!text || *text == '\0') return 0;

        bool is_whitespace_only = true;
        for (const char* p = text; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                is_whitespace_only = false;
                break;
            }
        }
        if (is_whitespace_only) {
            return 0;  // Whitespace-only text doesn't contribute to height
        }

        // Calculate line height
        float font_size = 16.0f;
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            font_size = lycon->font.style->font_size;
        }
        // Use actual font metrics for line-height:normal when available
        float line_height = font_size * 1.2f;  // fallback if no font loaded
        if (lycon->font.font_handle) {
            float normal_lh = font_calc_normal_line_height(lycon->font.font_handle);
            if (normal_lh > 0) line_height = normal_lh;
        }

        // Check ancestor elements for resolved CSS line-height
        // Walk up the DOM tree since intermediate parents may not have blk resolved yet
        for (DomNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->is_element()) continue;
            ViewBlock* anc_view = lam::unsafe_view_block_element_storage(ancestor->as_element());
            if (anc_view->blk && anc_view->blk->line_height) {
                const CssValue* lh = anc_view->blk->line_height;
                float lh_px = layout_resolve_line_height_value(
                    lycon, lh, ancestor->as_element(), font_size);
                if (lh_px > 0) line_height = lh_px;
                break;
            }
            if (anc_view->specified_style) {
                CssDeclaration* lh_decl = style_tree_get_declaration(
                    anc_view->specified_style, CSS_PROPERTY_LINE_HEIGHT);
                if (lh_decl && lh_decl->value) {
                    float lh_px = layout_resolve_line_height_value(
                        lycon, lh_decl->value, ancestor->as_element(), font_size);
                    if (lh_px > 0) line_height = lh_px;
                    break;
                }
            }
        }

	        // Estimate how many lines the text will take based on available width
	        if (width > 0) {
	            size_t text_len = strlen(text);
	            CssEnum ws_val = get_white_space_value(node);
	            const char* measure_text = text;
	            size_t measure_len = text_len;
	            static thread_local char normalized_text[4096];  // LARGE_ARRAY_OK: static buffer — not on call stack.
	            if (intrinsic_height_should_collapse_whitespace(ws_val)) {
	                measure_len = layout_normalize_collapsible_whitespace(text, text_len,
	                                                              normalized_text,
	                                                              sizeof(normalized_text));
	                if (measure_len == 0) return 0;
	                measure_text = normalized_text;
	            }
	            // Get text-transform and font-variant from parent element (inherited)
	            CssEnum text_transform = CSS_VALUE_NONE;
	            CssEnum font_variant = CSS_VALUE_NONE;
	            if (node->parent && node->parent->is_element()) {
	                text_transform = get_element_text_transform(node->parent->as_element());
	                font_variant = get_element_font_variant(node->parent->as_element());
	            }
            TextIntrinsicWidths widths = measure_text_intrinsic_widths(lycon, measure_text, measure_len,
                                                                       text_transform, font_variant);
            float text_width = widths.max_content;

            // Calculate wrapped text height with the same greedy break-unit packing
            // used by the intrinsic text-height helper.  The old division-based
            // estimate used min-content width as a fixed packing unit, which
            // over-counted normal word wrapping for varied word lengths.
            float measured_height = line_height;
            if (ws_val != CSS_VALUE_NOWRAP && ws_val != CSS_VALUE_PRE &&
                text_width > width + 0.005f) {
                measured_height = compute_text_height_at_width(
                    lycon, measure_text, measure_len, width, line_height,
                    text_transform, font_variant);
            }

            int num_lines = (int)ceilf(measured_height / line_height); // INT_CAST_OK: diagnostic line count
            log_debug("calculate_max_content_height: text len=%zu, measure_len=%zu, text_width=%.1f, available_width=%.1f, lines=%d",
                      text_len, measure_len, text_width, width, num_lines);

            return measured_height;
        }

        return line_height;
    }

	    // For elements, we'd need to do a full layout pass
	    // For now, use a simplified estimation
	    DomElement* element = node->as_element();
	    if (!element) return 0;

	    {
	        FontBox saved_style_font = lycon->font;
	        View* saved_style_view = lycon->view;
	        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
	        lycon->view = static_cast<View*>(element);
	        dom_node_resolve_style(element, lycon);
	        lycon->view = saved_style_view;
	        lycon->font = saved_style_font;
	    }

	    // CSS 2.1 §9.2.4: Elements with display: none do not generate boxes
	    // and contribute zero height.
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (layout_block_is_display_none(view)) {
        return 0;
    }

    float height = 0;

    // Set up element's own font context for accurate text measurement.
    // child text nodes inherit lycon->font, so we must set the element's font
    // before processing children (mirrors the logic in measure_element_intrinsic_widths).
    FontBox saved_font_h = lycon->font;
    struct TempHeightFontGuard {
        LayoutContext* lycon;
        FontBox saved_font;
        FontProp* prop;
        TempHeightFontGuard(LayoutContext* lycon, FontBox saved_font)
            : lycon(lycon), saved_font(saved_font), prop(nullptr) {}
        ~TempHeightFontGuard() {
            font_prop_release_handle(prop);
            lycon->font = saved_font;
        }
    } temp_height_font_guard(lycon, saved_font_h);
    bool height_font_changed = false;
    if (view->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view->font);
        height_font_changed = true;
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value) {
            bool resolved_from_medium = false;
            float resolved_size = intrinsic_resolve_font_size_value(
                lycon, font_size_decl->value, &resolved_from_medium);
            if (resolved_size >= 0.0f && fabsf(resolved_size - lycon->font.style->font_size) > 0.1f) {
                FontProp* tfp = alloc_font_prop(lycon);
                if (tfp) {
                    temp_height_font_guard.prop = tfp;
                    if (lycon->font.style) {
                        radiant_retain_font_family(tfp, lam::PoolPtr<char>(lycon->font.style->family));
                    }
                    tfp->font_size = resolved_size;
                    tfp->font_size_from_medium = resolved_from_medium;
                    setup_font(lycon->ui_context, &lycon->font, tfp);
                    height_font_changed = true;
                }
            }
        }
    }

    // Check for explicit height from CSS (e.g., iframe with height: 580px)
    if (view->blk && view->blk->given_height > 0) {
        // Element has explicit height specified in CSS
        float explicit_height = view->blk->given_height;

        // Check box-sizing: if border-box, the height already includes padding/border
        // Only add padding/border for content-box (default)
        bool is_border_box = layout_uses_border_box(view);

        if (!is_border_box && view->bound) {
            // content-box: height is content only, add padding and border
            if (view->bound->padding.top >= 0) explicit_height += view->bound->padding.top;
            if (view->bound->padding.bottom >= 0) explicit_height += view->bound->padding.bottom;
            if (view->bound->border) {
                explicit_height += view->bound->border->width.top;
                explicit_height += view->bound->border->width.bottom;
            }
        }
        // For border-box, given_height already includes padding/border, return as-is

        log_debug("calculate_max_content_height: %s has explicit height=%.1f (box_sizing=%s)",
                  element->node_name(), explicit_height, is_border_box ? "border-box" : "content-box");
        return explicit_height;
    }

    // Also check specified_style for height declaration if not yet resolved
    if (element->specified_style) {
        CssDeclaration* height_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_HEIGHT);
        if (height_decl && height_decl->value &&
            height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            float explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            if (explicit_height > 0) {
                log_debug("calculate_max_content_height: %s has specified height=%.1f",
                          element->node_name(), explicit_height);
                return explicit_height;
            }
        }
    }

    // CSS 2.1 §10.6.2: Replaced element intrinsic height
    if (view->display.inner == RDT_DISPLAY_REPLACED) {
        uintptr_t elem_tag = element->tag();
        if (elem_tag == HTM_TAG_IMG) {
            if (view->embed && view->embed->img) {
                ImageSurface* img = view->embed->img;
                float img_height = intrinsic_image_height(img, width);
                log_debug("calculate_max_content_height: IMG intrinsic height=%.1f", img_height);
                return img_height;
            }
            // Try to load image
            const char* src_value = element->get_attribute("src");
            if (src_value && lycon->ui_context) {
                if (!view->embed) {
                    view->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
                }
                size_t src_len = strlen(src_value);
                StrBuf* src_buf = strbuf_new_cap(src_len);
                strbuf_append_str_n(src_buf, src_value, src_len);
                view->embed->img = load_image(lycon->ui_context, src_buf->str);
                strbuf_free(src_buf);
                if (view->embed->img) {
                    ImageSurface* img = view->embed->img;
                    float img_height = intrinsic_image_height(img, width);
                    log_debug("calculate_max_content_height: IMG intrinsic height=%.1f (loaded)", img_height);
                    return img_height;
                }
            }
            const char* attr_h = element->get_attribute("height");
            if (attr_h) {
                int h = atoi(attr_h);
                if (h > 0) return (float)h;
            }
            // also check blk->given_height from resolved CSS/HTML attributes
            if (view->blk && view->blk->given_height >= 0) {
                return view->blk->given_height;
            }
            return 0.0f;  // no image loaded, no dimensions specified
        }
        else if (elem_tag == HTM_TAG_IFRAME || elem_tag == HTM_TAG_VIDEO || elem_tag == HTM_TAG_CANVAS) {
            // try actual video dimensions for aspect-correct height
            if (elem_tag == HTM_TAG_VIDEO && view->embed && view->embed->video) {
                int vw = rdt_video_get_width(view->embed->video);
                int vh = rdt_video_get_height(view->embed->video);
                if (vw > 0 && vh > 0) {
                    float video_height = (float)vh;
                    // scale proportionally if width was constrained
                    if (width > 0 && width != (float)vw) {
                        video_height = width * (float)vh / (float)vw;
                    }
                    log_debug("calculate_max_content_height: VIDEO intrinsic height=%.1f", video_height);
                    return video_height;
                }
            }
            return 150.0f;  // CSS default 300x150
        }
        else if (elem_tag == HTM_TAG_AUDIO) {
            return 54.0f;  // audio controls default height
        }
        else if (elem_tag == HTM_TAG_SVG) {
            float svg_height = intrinsic_svg_size(element, width).height;
            log_debug("calculate_max_content_height: SVG intrinsic height=%.1f", svg_height);
            return svg_height;
        }
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (element->tag() == HTM_TAG_SVG) {
        float svg_height = intrinsic_svg_size(element, width).height;
        log_debug("calculate_max_content_height: SVG (tag-based) intrinsic height=%.1f", svg_height);
        return svg_height;
    }

    // Form controls (input, select, textarea) — replaced elements with intrinsic height.
    // Per HTML/CSS, these are replaced elements: their layout children (e.g. <option>)
    // do NOT contribute to the visible box height. Return only the CONTENT height here;
    // the shared padding/border code at the bottom adds the rest. Skip child traversal.
    bool is_form_control_replaced = false;
    {
        uintptr_t etag = element->tag();
        if (etag == HTM_TAG_INPUT || etag == HTM_TAG_SELECT || etag == HTM_TAG_TEXTAREA) {
            float font_size = 16.0f;
            if (lycon->font.style && lycon->font.style->font_size > 0)
                font_size = lycon->font.style->font_size;

            if (etag == HTM_TAG_TEXTAREA) {
                int rows = FormDefaults::TEXTAREA_ROWS;
                height = rows * font_size * 1.2f + 2 * FormDefaults::TEXTAREA_PADDING;
            } else if (etag == HTM_TAG_INPUT) {
                const char* type_attr = element->get_attribute("type");
                if (type_attr && (strcmp(type_attr, "checkbox") == 0 || strcmp(type_attr, "radio") == 0)) {
                    return FormDefaults::CHECK_SIZE;  // fixed size, no CSS padding/border
                }
                height = font_size + 2 * FormDefaults::TEXT_PADDING_V;
            } else {
                // select
                height = font_size + 2 * FormDefaults::TEXT_PADDING_V;
            }

            log_debug("calculate_max_content_height: form control %s content_height=%.1f",
                      element->node_name(), height);
            is_form_control_replaced = true;
            // Skip children traversal below; pad/border still added at end.
        }
    }

    // Check if this is a grid container - need to detect column count
    bool is_grid_container = intrinsic_element_display_matches(
        element, view, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);
    int grid_column_count = 1;  // Default: single column = vertical stacking
    float grid_row_gap = 0;

    if (is_grid_container) {
        grid_column_count = intrinsic_grid_template_column_count(
            element, view, grid_column_count, "calculate_max_content_height", true, true);
        // Get row gap
        if (view->embed && view->embed->grid) {
            grid_row_gap = view->embed->grid->row_gap;
        }
        if (grid_row_gap <= 0) {
            intrinsic_style_length_declaration_or_fallback(
                lycon, element->specified_style, CSS_PROPERTY_ROW_GAP,
                CSS_PROPERTY_GAP, CSS_PROPERTY_GAP, &grid_row_gap);
        }
        log_debug("calculate_max_content_height: grid %s with %d columns, gap=%.1f",
                  element->node_name(), grid_column_count, grid_row_gap);
    }

    // Check if this is a grid container with column flow
    // In column flow, items are placed in columns (side-by-side), so height = max(child_heights)
    bool is_grid_column_flow = false;
    if (view->display.inner == CSS_VALUE_GRID) {
        // Check if grid-auto-flow is column
        if (view->embed && view->embed->grid) {
            if (view->embed->grid->grid_auto_flow == CSS_VALUE_COLUMN) {
                is_grid_column_flow = true;
            }
        }
    }

    // Also check for flex containers with row direction (items side-by-side)
    bool is_flex_row = false;
    bool is_flex_wrap = false;
    float flex_row_gap = 0;
    float flex_column_gap = 0;
    bool is_flex_container = intrinsic_element_display_matches(
        element, view, CSS_VALUE_FLEX, CSS_VALUE_INLINE_FLEX);

    if (is_flex_container) {
        // Default flex direction is row, wrap is nowrap
        is_flex_row = true;
        is_flex_wrap = false;

        if (view->embed && view->embed->flex) {
            if (view->embed->flex->direction == DIR_ROW ||
                view->embed->flex->direction == DIR_ROW_REVERSE) {
                is_flex_row = true;
            } else {
                is_flex_row = false;
            }
            // Check for flex-wrap
            if (view->embed->flex->wrap == WRAP_WRAP ||
                view->embed->flex->wrap == WRAP_WRAP_REVERSE) {
                is_flex_wrap = true;
            }
            flex_row_gap = view->embed->flex->row_gap;
            flex_column_gap = view->embed->flex->column_gap;
        } else if (element->specified_style) {
            intrinsic_style_flex_direction_is_row(element->specified_style, &is_flex_row);
            intrinsic_style_flex_wrap_enabled(element->specified_style, &is_flex_wrap);
            float gap = 0.0f;
            if (intrinsic_style_length_declaration(
                    lycon, element->specified_style, CSS_PROPERTY_GAP, CSS_PROPERTY_GAP, &gap)) {
                flex_row_gap = gap;
                flex_column_gap = gap;
            }
            intrinsic_style_length_declaration(
                lycon, element->specified_style, CSS_PROPERTY_ROW_GAP,
                CSS_PROPERTY_ROW_GAP, &flex_row_gap);
            intrinsic_style_length_declaration(
                lycon, element->specified_style, CSS_PROPERTY_COLUMN_GAP,
                CSS_PROPERTY_COLUMN_GAP, &flex_column_gap);
        }
    }
    bool is_empty_auto_container = !is_form_control_replaced &&
        view->display.inner != RDT_DISPLAY_REPLACED &&
        (!view->blk || view->blk->given_height < 0.0f) &&
        !element_has_in_flow_intrinsic_content(element);

    // Check if this block element has only inline content (text and inline elements).
    // In that case, children flow inline on the same line(s), not stacked vertically.
    bool has_only_inline_content = false;

    // First check if display is resolved; if not, try to resolve it
    CssEnum display_inner = view->display.inner;
    if (display_inner == 0 && element->specified_style) {
        // Display not resolved yet, try to get from CSS
        CssEnum display_value = (CssEnum)0;
        if (intrinsic_specified_display_keyword(element, &display_value)) {
            display_inner = display_value;
            // block => flow layout
            if (display_inner == CSS_VALUE_BLOCK) {
                display_inner = CSS_VALUE_FLOW;
            }
        } else {
            // Default: block element uses flow layout
            display_inner = CSS_VALUE_FLOW;
        }
    }

    if (!is_grid_container && !is_flex_row && display_inner == CSS_VALUE_FLOW) {
        // Block element with flow layout - check if all children are inline
        has_only_inline_content = true;
        for (DomNode* c = element->first_child; c; c = c->next_sibling) {
            if (c->is_text()) {
                continue;  // Text nodes are inline
            }
            if (c->is_element()) {
                DomElement* child_elem = c->as_element();
                // Skip display:none elements — they generate no boxes (CSS 2.1 §9.2.4)
                ViewElement* child_ve = lam::view_require_element(static_cast<View*>(child_elem));
                if (layout_display_is_none(child_ve->display)) {
                    continue;
                }
                // Resolve display.outer if not yet resolved (may happen during early
                // measurement before resolve_htm_style runs).
                CssEnum child_outer = child_ve->display.outer;
                if (child_outer == 0) {
                    DisplayValue dv = resolve_display_value((void*)c);
                    child_outer = dv.outer;
                }
                // Check if child is an inline element
                const char* child_tag = child_elem->node_name();
                if (child_tag && (
                    strcmp(child_tag, "a") == 0 ||
                    strcmp(child_tag, "span") == 0 ||
                    strcmp(child_tag, "strong") == 0 ||
                    strcmp(child_tag, "b") == 0 ||
                    strcmp(child_tag, "em") == 0 ||
                    strcmp(child_tag, "i") == 0 ||
                    strcmp(child_tag, "code") == 0 ||
                    strcmp(child_tag, "br") == 0 ||
                    strcmp(child_tag, "abbr") == 0 ||
                    strcmp(child_tag, "small") == 0 ||
                    strcmp(child_tag, "sub") == 0 ||
                    strcmp(child_tag, "sup") == 0)) {
                    continue;  // Known inline elements
                }
                // Check display.outer for inline OR inline-block (both flow inline)
                if (child_outer == CSS_VALUE_INLINE ||
                    child_outer == CSS_VALUE_INLINE_BLOCK) {
                    continue;  // CSS says it's inline-level
                }
                // Found a block element
                has_only_inline_content = false;
                break;
            }
        }
        if (has_only_inline_content) {
            log_debug("calculate_max_content_height: %s has only inline content", element->node_name());
        }
    }

    // Track previous margin-bottom for block-flow margin collapse (used in else branch below,
    // declared here so the last-child-margin fix after padding/border resolution can access them)
    float prev_margin_bottom = 0;
    bool is_block_flow = !is_grid_container && !is_flex_container && !has_only_inline_content;
    bool block_flow_has_prior_content = false;

    float block_flow_pad_top = 0.0f;
    float block_flow_border_top = 0.0f;
    if (view->bound) {
        block_flow_pad_top = view->bound->padding.top;
        if (view->bound->border) {
            block_flow_border_top = view->bound->border->width.top;
        }
    } else if (element->specified_style && width > 0.0f) {
        CssDeclaration* pt = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_TOP);
        if (pt && pt->value) {
            if (pt->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                block_flow_pad_top = (float)(pt->value->data.percentage.value / 100.0) * width;
            } else if (pt->value->type == CSS_VALUE_TYPE_LENGTH) {
                block_flow_pad_top = resolve_length_value(lycon, CSS_PROPERTY_PADDING_TOP, pt->value);
            }
        } else {
            CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
            if (pad_decl && pad_decl->value) {
                if (pad_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                    block_flow_pad_top = (float)(pad_decl->value->data.percentage.value / 100.0) * width;
                } else if (pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    block_flow_pad_top = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
                }
            }
        }
    }
    bool block_flow_first_margin_collapses = is_block_flow &&
        block_flow_pad_top <= 0.0f && block_flow_border_top <= 0.0f &&
        !block_context_establishes_bfc(view);

    bool is_table_structure_box = intrinsic_is_table_box(element) ||
        intrinsic_is_table_row_group_box(element) ||
        intrinsic_is_table_row_box(element);

    // For multi-column grids, calculate height based on rows
    if (is_form_control_replaced) {
        // Replaced form control: skip children entirely; padding/border added below.
    } else if (is_empty_auto_container) {
        // Empty auto-height containers have zero content height. Padding and border
        // are still added below, but line-height must not synthesize a line.
        height = 0.0f;
    } else if (is_table_structure_box) {
        // CSS tables size rows from the max cell height; block stacking would sum
        // sibling cells and overstate intrinsic table height.
        height = intrinsic_table_structure_height(lycon, element, width);
    } else if (is_grid_container && grid_column_count > 1) {
        // Collect child heights
        int child_count = 0;
        for (DomNode* c = element->first_child; c; c = c->next_sibling) {
            if (c->is_element()) child_count++;
        }

        if (child_count > 0) {
            // Calculate number of rows
            int row_count = (child_count + grid_column_count - 1) / grid_column_count;

            // Grid child counts come from the DOM, so avoid input-sized stack frames.
            float* child_heights = (float*)scratch_calloc(&lycon->scratch, (size_t)child_count * sizeof(float));
            if (!child_heights) {
                log_error("calculate_max_content_height: failed to allocate %d grid child heights", child_count);
                return height;
            }
            float col_share = width / grid_column_count;
            int idx = 0;
            for (DomNode* c = element->first_child; c; c = c->next_sibling) {
                if (c->is_element()) {
                    // Auto columns can't shrink below their min-content width,
                    // so use max(share, min_content) to get the actual column width.
                    float child_w = col_share;
                    DomElement* ce = lam::dom_require_element(c);
                    IntrinsicSizes cs = measure_element_intrinsic_widths(lycon, ce);
                    if (cs.min_content > child_w) child_w = cs.min_content;
                    child_heights[idx++] = calculate_max_content_height(lycon, c, child_w);
                }
            }

            // Sum up max height of each row
            for (int row = 0; row < row_count; row++) {
                float row_max_height = 0;
                for (int col = 0; col < grid_column_count; col++) {
                    int item_idx = row * grid_column_count + col;
                    if (item_idx < child_count) {
                        row_max_height = fmax(row_max_height, child_heights[item_idx]);
                    }
                }
                height += row_max_height;
                if (row > 0) {
                    height += grid_row_gap;
                }
            }
            log_debug("calculate_max_content_height: grid %s rows=%d, total_height=%.1f",
                      element->node_name(), row_count, height);
            scratch_free(&lycon->scratch, child_heights);
        }
    } else if (is_flex_row && is_flex_wrap && width > 0) {
        // Special handling for wrapping flex row containers
        // Simulate how items wrap based on available width
        log_debug("calculate_max_content_height: wrapping flex row %s, available_width=%.1f, row_gap=%.1f, col_gap=%.1f",
                  element->node_name(), width, flex_row_gap, flex_column_gap);

        // Collect child widths and heights
        float current_line_width = 0;
        float current_line_height = 0;
        float total_height = 0;
        int line_count = 0;
        bool first_on_line = true;

        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            if (!child->is_element()) continue;

            // Get child's intrinsic width
            DomElement* child_elem = child->as_element();
            IntrinsicSizes child_sizes = measure_element_intrinsic_widths(lycon, child_elem);
            float child_width = child_sizes.max_content;  // Use max-content width for flex items
            float child_height = calculate_max_content_height(lycon, child, width);

            // Check if we need to wrap to a new line
            float width_with_gap = first_on_line ? child_width : (flex_column_gap + child_width);
            if (!first_on_line && current_line_width + width_with_gap > width) {
                // Wrap to new line
                total_height += current_line_height;
                if (line_count > 0) {
                    total_height += flex_row_gap;
                }
                line_count++;
                current_line_width = child_width;
                current_line_height = child_height;
                first_on_line = false;
            } else {
                // Add to current line
                current_line_width += width_with_gap;
                current_line_height = fmax(current_line_height, child_height);
                first_on_line = false;
            }
        }

        // Add the last line
        if (current_line_height > 0) {
            total_height += current_line_height;
            if (line_count > 0) {
                total_height += flex_row_gap;
            }
            line_count++;
        }

        height = total_height;
        log_debug("calculate_max_content_height: wrapping flex %s, lines=%d, height=%.1f",
                  element->node_name(), line_count, height);
    } else {
        // Calculate children's heights (original logic for non-grid or single-column)

        // Determine the element's own content width for children's percentage resolution.
        // The `width` parameter is the available width from the parent (containing block),
        // but if this element has an explicit CSS width, children resolve percentages
        // against this element's content width, not the grandparent's.
        float content_w = width;
        if (element->specified_style) {
            CssDeclaration* w_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_WIDTH);
            if (w_decl && w_decl->value) {
                if (w_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE && width > 0) {
                    content_w = (float)(w_decl->value->data.percentage.value / 100.0) * width;
                } else if (w_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                    content_w = resolve_length_value(lycon, CSS_PROPERTY_WIDTH, w_decl->value);
                }
            }
        }

        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            // Skip display:none elements — they generate no boxes (CSS 2.1 §9.2.4)
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewElement* child_ve = lam::view_require_element(static_cast<View*>(child_elem));
                if (layout_display_is_none(child_ve->display)) {
                    continue;
                }
                ViewBlock* child_block = lam::view_as_block(child_ve);
                if (child_block &&
                    (layout_block_is_out_of_flow_positioned(child_block) ||
                     (child_block->position && element_has_float(child_block)))) {
                    continue;
                }
                if ((!child_block || !child_block->position) && child_elem->specified_style) {
                    CssDeclaration* pos_decl = style_tree_get_declaration(
                        child_elem->specified_style, CSS_PROPERTY_POSITION);
                    if (pos_decl && pos_decl->value &&
                        pos_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum pos_val = pos_decl->value->data.keyword;
                        if (pos_val == CSS_VALUE_ABSOLUTE || pos_val == CSS_VALUE_FIXED) {
                            log_debug("calculate_max_content_height: skipping absolute/fixed child %s from specified style",
                                      child_elem->node_name());
                            continue;
                        }
                    }
                }
            }
            // CSS 2.1 §10.3.3: The available width for a child element's content
            // is the parent's content width minus the child's own padding and border.
            // Pass the child's content-box width so its descendants wrap correctly.
            float child_content_w = content_w;
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                ViewElement* child_ve = lam::view_require_element(static_cast<View*>(child_elem));
                float child_padding_border = 0.0f;
                if (child_ve->bound) {
                    BoxMetrics child_box = layout_boundary_metrics(child_ve->bound);
                    child_padding_border = child_box.pad_border_h;
                } else if (child_elem->specified_style) {
                    float pad_left = 0.0f;
                    float pad_right = 0.0f;
                    float border_left = 0.0f;
                    float border_right = 0.0f;
                    get_horizontal_padding_widths_from_css(
                        lycon, child_elem, content_w, &pad_left, &pad_right);
                    get_horizontal_border_widths_from_css(
                        lycon, child_elem, &border_left, &border_right);
                    child_padding_border = pad_left + pad_right + border_left + border_right;
                }
                if (child_padding_border > 0.0f && child_content_w > 0.0f) {
                    child_content_w -= child_padding_border;
                    if (child_content_w < 0.0f) child_content_w = 0.0f;
                }
            }
            float child_height = calculate_max_content_height(lycon, child, child_content_w);

            // Resolve child's vertical margins for height estimation
            bool block_flow_margin_handled = false;
            if (child->is_element() && content_w > 0) {
                DomElement* child_elem = child->as_element();
                float mt = 0, mb = 0;

                ViewElement* child_ve = lam::view_require_element(static_cast<View*>(child_elem));
                bool resolved_from_style = intrinsic_resolve_vertical_margins(
                    lycon, child_elem, content_w, &mt, &mb);

                // Fall back to resolved margins when no specified margin candidate exists.
                if (!resolved_from_style && child_ve->bound) {
                    mt = child_ve->bound->margin.top;
                    mb = child_ve->bound->margin.bottom;
                }

                // Older styles can arrive without bounds during early measurement.
                if (!resolved_from_style && mt == 0 && mb == 0 && child_elem->specified_style) {
                    CssDeclaration* mt_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_TOP);
                    if (mt_decl && mt_decl->value) {
                        if (mt_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE)
                            mt = (float)(mt_decl->value->data.percentage.value / 100.0) * content_w;
                        else if (mt_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                            mt = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_TOP, mt_decl->value);
                    }
                    CssDeclaration* mb_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN_BOTTOM);
                    if (mb_decl && mb_decl->value) {
                        if (mb_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE)
                            mb = (float)(mb_decl->value->data.percentage.value / 100.0) * content_w;
                        else if (mb_decl->value->type == CSS_VALUE_TYPE_LENGTH)
                            mb = resolve_length_value(lycon, CSS_PROPERTY_MARGIN_BOTTOM, mb_decl->value);
                    }
                    if (mt == 0 && mb == 0) {
                        CssDeclaration* m_decl = style_tree_get_declaration(child_elem->specified_style, CSS_PROPERTY_MARGIN);
                        if (m_decl && m_decl->value) {
                            if (m_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                float m = (float)(m_decl->value->data.percentage.value / 100.0) * content_w;
                                mt = mb = m;
                            } else if (m_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                                float m = resolve_length_value(lycon, CSS_PROPERTY_MARGIN, m_decl->value);
                                mt = mb = m;
                            }
                        }
                    }
                }

                if (is_grid_container || is_flex_container) {
                    // Flex/grid: margins don't collapse, add full margins
                    child_height += mt + mb;
                } else if (is_block_flow &&
                           (child_elem->display.outer != CSS_VALUE_INLINE ||
                            child_ve->view_type == RDT_VIEW_INLINE_BLOCK)) {
                    // Block flow: collapse margins between adjacent block siblings
                    // CSS2 §8.3.1: adjoining margins collapse to max(mt, prev_mb)
                    float collapsed = (!block_flow_has_prior_content &&
                                       block_flow_first_margin_collapses) ?
                        0.0f : fmax(prev_margin_bottom, mt);
                    height += collapsed;
                    prev_margin_bottom = mb;
                    block_flow_margin_handled = true;
                }
            }

            if (is_grid_column_flow || is_flex_row || has_only_inline_content) {
                // Items are laid out horizontally - take max height
                height = fmax(height, child_height);
            } else {
                // Items are stacked vertically - sum heights
                height += child_height;
            }
            if (child_height > 0.0f) {
                block_flow_has_prior_content = true;
            } else if (block_flow_margin_handled) {
                block_flow_has_prior_content = true;
            }
        }
    }

    // Add padding and border
    float pad_top = 0, pad_bottom = 0;
    float border_top = 0, border_bottom = 0;

    // CSS percentage padding (top/bottom) resolves against the containing block's WIDTH.
    // During intrinsic sizing, view->bound may have padding=0 from stylesheet defaults
    // while specified_style has an unresolved percentage override. Check specified_style
    // for percentage padding FIRST, since percentages must be resolved against 'width'.
    bool resolved_pad_from_pct = false;
    if (element->specified_style && width > 0) {
        CssDeclaration* pt = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_TOP);
        if (pt && pt->value && pt->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            pad_top = (float)(pt->value->data.percentage.value / 100.0) * width;
            resolved_pad_from_pct = true;
        }
        CssDeclaration* pb = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_BOTTOM);
        if (pb && pb->value && pb->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            pad_bottom = (float)(pb->value->data.percentage.value / 100.0) * width;
            resolved_pad_from_pct = true;
        }
        if (!resolved_pad_from_pct) {
            CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
            if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float pad = (float)(pad_decl->value->data.percentage.value / 100.0) * width;
                pad_top = pad_bottom = pad;
                resolved_pad_from_pct = true;
            }
        }
    }

    if (!resolved_pad_from_pct) {
        if (view->bound) {
            if (view->bound->padding.top >= 0) pad_top = view->bound->padding.top;
            if (view->bound->padding.bottom >= 0) pad_bottom = view->bound->padding.bottom;
        } else if (element->specified_style) {
            CssDeclaration* pad_decl = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING);
            if (pad_decl && pad_decl->value && pad_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                float pad = resolve_length_value(lycon, CSS_PROPERTY_PADDING, pad_decl->value);
                pad_top = pad_bottom = pad;
            } else {
                CssDeclaration* pt = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_TOP);
                if (pt && pt->value && pt->value->type == CSS_VALUE_TYPE_LENGTH) {
                    pad_top = resolve_length_value(lycon, CSS_PROPERTY_PADDING_TOP, pt->value);
                }
                CssDeclaration* pb = style_tree_get_declaration(element->specified_style, CSS_PROPERTY_PADDING_BOTTOM);
                if (pb && pb->value && pb->value->type == CSS_VALUE_TYPE_LENGTH) {
                    pad_bottom = resolve_length_value(lycon, CSS_PROPERTY_PADDING_BOTTOM, pb->value);
                }
            }
        }
    }

    if (view->bound && view->bound->border) {
        border_top = view->bound->border->width.top;
        border_bottom = view->bound->border->width.bottom;
    }

    // CSS 2.1 §8.3.1: The last child's margin-bottom does not collapse through the
    // parent when the parent has padding-bottom or border-bottom. In that case, the
    // last child's margin contributes to the parent's content height.
    if (is_block_flow && prev_margin_bottom > 0 && (pad_bottom > 0 || border_bottom > 0)) {
        height += prev_margin_bottom;
    }

    height += pad_top + pad_bottom + border_top + border_bottom;

    if (height_font_changed) lycon->font = saved_font_h;
    return height;
}

float calculate_fit_content_width(LayoutContext* lycon, DomNode* node, float available_width) {
    // measure both min and max content in a single call to avoid redundant subtree traversals
    float min_content, max_content;
    if (node && node->is_element()) {
        DomElement* element = node->as_element();
        if (element) {
            IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, element);
            min_content = sizes.min_content;
            max_content = sizes.max_content;
        } else {
            min_content = calculate_min_content_width(lycon, node);
            max_content = calculate_max_content_width(lycon, node);
        }
    } else {
        min_content = calculate_min_content_width(lycon, node);
        max_content = calculate_max_content_width(lycon, node);
    }

    // CSS Sizing 3 §4.1: fit-content = max(min-content, min(max-content, available))
    // This ensures min-content is always respected as a floor, even when max-content
    // is smaller than min-content (e.g., negative text-indent).
    return fmaxf(min_content, fminf(max_content, available_width));
}

// ============================================================================
// Unified Intrinsic Sizing API Implementation (Section 4.2)
// ============================================================================

IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space
) {
    IntrinsicSizesBidirectional result = {0, 0, 0, 0};

    if (!lycon || !element) {
        return result;
    }

    DomNode* node = static_cast<DomNode*>(element);

    // Step 1: Measure width intrinsic sizes
    // Width measurement doesn't depend on available width
    // measure both min and max content widths in a single call
    if (node->is_element()) {
        DomElement* elem = node->as_element();
        if (elem) {
            IntrinsicSizes sizes = measure_element_intrinsic_widths(lycon, elem);
            result.min_content_width = sizes.min_content;
            result.max_content_width = sizes.max_content;
        } else {
            result.min_content_width = calculate_min_content_width(lycon, node);
            result.max_content_width = calculate_max_content_width(lycon, node);
        }
    } else {
        result.min_content_width = calculate_min_content_width(lycon, node);
        result.max_content_width = calculate_max_content_width(lycon, node);
    }

    // Step 2: Determine the width to use for height measurement
    // Height depends on width due to text wrapping
    float width_for_height;

    if (available_space.width.is_definite()) {
        // Use the definite available width for BOTH height measurements
        // This is critical for grid row sizing where the column width is known
        width_for_height = available_space.width.value;
    } else if (available_space.width.is_min_content()) {
        // Use min-content width for height calculation
        width_for_height = result.min_content_width;
    } else {
        // MaxContent or Indefinite: use max-content width
        width_for_height = result.max_content_width;
    }

    // Step 3: Measure height intrinsic sizes at the determined width
    // IMPORTANT: When a definite width is available, use it for BOTH min and max height
    // This matches the original grid behavior where both heights are computed at the same width
    result.min_content_height = calculate_min_content_height(lycon, node, width_for_height);
    result.max_content_height = calculate_max_content_height(lycon, node, width_for_height);

    log_debug("measure_intrinsic_sizes: %s -> width(min=%.1f, max=%.1f), height(min=%.1f, max=%.1f) at width=%.1f",
              element->node_name(),
              result.min_content_width, result.max_content_width,
              result.min_content_height, result.max_content_height,
              width_for_height);

    return result;
}

// ============================================================================
// Backward Compatibility Notes
// ============================================================================
//
// The following functions are now wrappers or remain for backward compatibility:
// - calculate_min_content_width() - use measure_intrinsic_sizes().min_content_width
// - calculate_max_content_width() - use measure_intrinsic_sizes().max_content_width
// - calculate_min_content_height() - use measure_intrinsic_sizes().min_content_height
// - calculate_max_content_height() - use measure_intrinsic_sizes().max_content_height
// - measure_element_intrinsic_widths() - use measure_intrinsic_sizes() for width
//
// These will be gradually deprecated in favor of the unified API.
// ============================================================================
