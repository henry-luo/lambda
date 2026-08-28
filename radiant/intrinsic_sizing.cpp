#include "layout.hpp"

static bool intrinsic_declared_css_height_is_definite(LayoutContext* lycon,
                                                      DomElement* element);

static bool intrinsic_view_uses_border_box(ViewBlock* view, DomElement* element) {
    if (layout_uses_border_box(view)) return true;
    CssDeclaration* box_decl = element && element->specified_style
        ? style_tree_get_declaration(element->specified_style, CSS_PROPERTY_BOX_SIZING)
        : nullptr;
    return box_decl && box_decl->value &&
        box_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
        box_decl->value->data.keyword == CSS_VALUE_BORDER_BOX;
}

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
#include <utf8proc.h>

IntrinsicFontScope::~IntrinsicFontScope() {
    font_prop_release_handle(prop_a);
    font_prop_release_handle(prop_b);
    if (lycon) lycon->font = saved_font;
}
#include <cstdlib>

void dom_node_resolve_style(DomNode* node, LayoutContext* lycon);

static bool layout_parse_aspect_ratio_number(const char* text, double* out_value) {
    if (!text || !out_value) return false;
    const char* cursor = text;
    char* end = nullptr;
    double first = strtod(cursor, &end);
    if (end == cursor || first <= 0.0) return false;
    cursor = end;
    double second = 0.0;
    bool has_second = false;
    while (*cursor) {
        char* next_end = nullptr;
        double value = strtod(cursor, &next_end);
        if (next_end != cursor) {
            second = value;
            has_second = true;
            break;
        }
        cursor++;
    }
    if (has_second) {
        if (second <= 0.0) return false;
        *out_value = first / second;
    } else {
        *out_value = first;
    }
    return true;
}

float layout_aspect_ratio_value(const CssValue* value) {
    if (!value || value->type == CSS_VALUE_TYPE_KEYWORD) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value > 0.0) {
        return (float)value->data.number.value;
    }
    if (value->type == CSS_VALUE_TYPE_STRING) {
        double ratio = 0.0;
        return layout_parse_aspect_ratio_number(value->data.string, &ratio) ? (float)ratio : 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        double ratio = 0.0;
        return layout_parse_aspect_ratio_number(value->data.custom_property.name, &ratio) ? (float)ratio : 0.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        double numerator = 0.0;
        double denominator = 0.0;
        bool got_numerator = false;
        bool got_denominator = false;
        for (int i = 0; i < value->data.list.count && !got_denominator; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item || item->type != CSS_VALUE_TYPE_NUMBER) continue;
            if (!got_numerator) {
                numerator = item->data.number.value;
                got_numerator = true;
            } else {
                denominator = item->data.number.value;
                got_denominator = true;
            }
        }
        if (got_numerator && got_denominator) {
            // A zero denominator invalidates the ratio; it is not shorthand
            // for the numerator alone.
            return numerator > 0.0 && denominator > 0.0
                ? (float)(numerator / denominator) : 0.0f;
        }
        if (got_numerator && numerator > 0.0) return (float)numerator;
    }
    return 0.0f;
}

float layout_aspect_ratio_height(float width, float aspect_ratio) {
    if (width < 0.0f || aspect_ratio <= 0.0f ||
        !isfinite(width) || !isfinite(aspect_ratio)) {
        return -1.0f;
    }
    float height = width / aspect_ratio;
    // css Values 4 permits approximating an actual value that the layout
    // coordinate range cannot support; leave this automatic transfer unresolved
    // so normal content sizing supplies the safe approximation.
    return isfinite(height) && height <= MAX_LAYOUT_DIMENSION ? height : -1.0f;
}

static bool layout_aspect_ratio_value_has_auto(const CssValue* value) {
    return layout_css_value_any(value, [](const CssValue* item) {
        return item->type == CSS_VALUE_TYPE_KEYWORD &&
               item->data.keyword == CSS_VALUE_AUTO;
    });
}

float layout_preferred_aspect_ratio(ViewBlock* block) {
    if (!block) return 0.0f;
    if (block->flex_item() && block->fi->aspect_ratio > 0.0f) return block->fi->aspect_ratio;
    // Grid intrinsic sizing precedes ViewBlock DOM materialization, but the
    // specified-style tree already contains the cascaded aspect-ratio value.
    CssDeclaration* decl = style_tree_get_declaration(
        block->specified_style, CSS_PROPERTY_ASPECT_RATIO);
    if (!decl && block->is_element()) {
        // Early intrinsic passes may have the DOM style before ViewBlock copies it;
        // ratio-dependent sizing must observe the same cascaded declaration.
        DomElement* element = block->as_element();
        decl = element ? style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_ASPECT_RATIO) : nullptr;
    }
    return decl ? layout_aspect_ratio_value(decl->value) : 0.0f;
}

static float intrinsic_element_preferred_aspect_ratio(ViewBlock* view) {
    // The view resolver already falls back to the DOM style before materialization;
    // re-reading the declaration here used to create two ratio resolution paths.
    return view ? layout_preferred_aspect_ratio(view) : 0.0f;
}

float layout_used_preferred_aspect_ratio(ViewBlock* block) {
    float specified_ratio = layout_preferred_aspect_ratio(block);
    if (!block || !layout_aspect_ratio_uses_content_box(block)) {
        return specified_ratio;
    }

    if (block->tag() == MARKUP_NAME_SVG) {
        Element* native_svg = block->is_element()
            ? dom_element_backing(lam::dom_require_element(block)) : nullptr;
        SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_svg);
        // `auto <ratio>` uses an SVG viewBox/natural ratio only when one exists;
        // the synthetic 300x150 fallback must not override the specified ratio.
        return intrinsic.has_intrinsic_aspect_ratio ? intrinsic.aspect_ratio : specified_ratio;
    }

    if (!block->embed || !block->embedp()->img) return specified_ratio;

    ImageSurface* image = block->embedp()->img;
    if (image->width <= 0 || image->height <= 0) return specified_ratio;
    // SVG fallback dimensions (300x150) are not a natural ratio when the
    // resource has neither explicit dimensions nor a viewBox ratio.
    if (!image->has_intrinsic_size && !image->has_intrinsic_aspect_ratio) {
        return specified_ratio;
    }
    // `auto <ratio>` selects a replaced element's natural ratio once its image is known.
    return (float)image->width / (float)image->height;
}

bool layout_aspect_ratio_uses_content_box(ViewBlock* block) {
    if (!block) return false;
    CssDeclaration* decl = style_tree_get_declaration(
        block->specified_style, CSS_PROPERTY_ASPECT_RATIO);
    if (!decl && block->is_element()) {
        // Intrinsic sizing can see the DOM style before ViewBlock copies it;
        // preserve the auto-ratio box choice during that early pass.
        DomElement* element = block->as_element();
        decl = element ? style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_ASPECT_RATIO) : nullptr;
    }
    return decl && layout_aspect_ratio_value_has_auto(decl->value);
}

static CssEnum layout_intrinsic_size_keyword_from_declaration(CssDeclaration* decl) {
    if (!decl || !decl->value) {
        return CSS_VALUE__UNDEF;
    }
    if (decl->value->type != CSS_VALUE_TYPE_KEYWORD) return CSS_VALUE__UNDEF;
    CssEnum keyword = decl->value->data.keyword;
    return keyword == CSS_VALUE_MIN_CONTENT || keyword == CSS_VALUE_MAX_CONTENT ||
        keyword == CSS_VALUE_FIT_CONTENT ? keyword : CSS_VALUE__UNDEF;
}

static CssEnum layout_intrinsic_size_keyword_from_stored(CssEnum stored) {
    return stored == CSS_VALUE_MIN_CONTENT || stored == CSS_VALUE_MAX_CONTENT ||
        stored == CSS_VALUE_FIT_CONTENT ? stored : CSS_VALUE__UNDEF;
}

CssEnum layout_intrinsic_preferred_size_keyword(ViewBlock* block, bool horizontal) {
    if (!block) return CSS_VALUE__UNDEF;
    DomElement* element = block->is_element() ? block->as_element() : nullptr;
    CssDeclaration* declaration = layout_specified_physical_size_declaration(element, horizontal);
    CssEnum keyword = layout_intrinsic_size_keyword_from_declaration(declaration);
    if (declaration && declaration->value &&
        declaration->value->type == CSS_VALUE_TYPE_FUNCTION &&
        declaration->value->data.function && declaration->value->data.function->name &&
        strcmp(declaration->value->data.function->name, "fit-content") == 0) {
        // fit-content() is a preferred-size function; min/max constraints use
        // their existing constraint path and must not become preferred keywords.
        keyword = CSS_VALUE_FIT_CONTENT;
    }
    if (keyword != CSS_VALUE__UNDEF) return keyword;
    if (!block->blk) return CSS_VALUE__UNDEF;
    LayoutAxisRefs refs(block->block_mut(), horizontal);
    return layout_intrinsic_size_keyword_from_stored(*refs.given_type);
}

static CssEnum layout_intrinsic_minmax_size_keyword(ViewBlock* block, bool horizontal,
                                                     bool minimum) {
    if (!block) return CSS_VALUE__UNDEF;
    DomElement* element = block->is_element() ? block->as_element() : nullptr;
    CssEnum keyword = layout_intrinsic_size_keyword_from_declaration(
        layout_specified_physical_minmax_size_declaration(element, horizontal, minimum));
    if (keyword != CSS_VALUE__UNDEF) return keyword;
    if (!block->blk) return CSS_VALUE__UNDEF;
    LayoutAxisRefs refs(block->block_mut(), horizontal);
    return layout_intrinsic_size_keyword_from_stored(
        minimum ? *refs.minimum_type : *refs.maximum_type);
}

CssEnum layout_intrinsic_min_size_keyword(ViewBlock* block, bool horizontal) {
    return layout_intrinsic_minmax_size_keyword(block, horizontal, true);
}

bool layout_axis_size_is_percentage(ViewBlock* block, bool horizontal) {
    if (!block) return false;

    if (block->blk) {
        LayoutAxisRefs refs(block->block_mut(), horizontal);
        float given_percent = *refs.given_percent;
        if (!isnan(given_percent)) return true;
    }

    DomElement* element = block->is_element() ? block->as_element() : nullptr;
    return layout_axis_size_is_percentage(element, horizontal);
}

bool layout_axis_size_is_percentage(DomElement* element, bool horizontal) {
    CssDeclaration* declaration = layout_specified_physical_size_declaration(
        element, horizontal);
    CssDeclaration* authored_declaration = element
        ? dom_element_get_specified_value(
            element, horizontal ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT)
        : nullptr;
    // During intrinsic measurement, style resolution may already have converted
    // the percentage to a provisional pixel value; preserve the authored type.
    return (declaration && declaration->value &&
            declaration->value->type == CSS_VALUE_TYPE_PERCENTAGE) ||
           (authored_declaration && authored_declaration->value &&
            authored_declaration->value->type == CSS_VALUE_TYPE_PERCENTAGE);
}

CssEnum layout_intrinsic_max_size_keyword(ViewBlock* block, bool horizontal) {
    return layout_intrinsic_minmax_size_keyword(block, horizontal, false);
}

float layout_resolve_intrinsic_size_keyword(CssEnum keyword, float min_size,
                                            float max_size, float available_outer_size) {
    if (min_size < 0.0f) min_size = 0.0f;
    if (max_size < min_size) max_size = min_size;
    if (keyword == CSS_VALUE_MIN_CONTENT) return min_size;
    if (keyword == CSS_VALUE_MAX_CONTENT) return max_size;
    if (keyword != CSS_VALUE_FIT_CONTENT) return -1.0f;
    if (available_outer_size < 0.0f) return max_size;
    return min(max_size, max(min_size, available_outer_size));
}

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

static float intrinsic_resolve_definite_constraint(LayoutContext* lycon,
                                                   CssPropertyCode property,
                                                   const CssValue* value) {
    if (!value || (value->type != CSS_VALUE_TYPE_LENGTH &&
                   value->type != CSS_VALUE_TYPE_FUNCTION)) return -1.0f;
    if (value->type == CSS_VALUE_TYPE_FUNCTION &&
        (!value->data.function || !value->data.function->name ||
         strcmp(value->data.function->name, "calc") != 0 ||
         layout_css_value_has_nonzero_percentage(value))) return -1.0f;
    float resolved = resolve_length_value(lycon, property, value);
    return isfinite(resolved) && resolved >= 0.0f ? resolved : -1.0f;
}

static void intrinsic_apply_grid_item_min_content_floor(
        LayoutContext* lycon, DomElement* child_elem, IntrinsicSizes* child_sizes) {
    if (!lycon || !child_elem || !child_sizes) return;
    ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_elem);
    float explicit_min_width = layout_explicit_min_axis_or(child_view, true, -1.0f);
    if (explicit_min_width < 0.0f && child_elem->specified_style) {
        CssDeclaration* min_decl = style_tree_get_declaration(
            child_elem->specified_style, CSS_PROPERTY_MIN_WIDTH);
        if (min_decl && min_decl->value &&
            (min_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
             (min_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
              min_decl->value->data.number.value == 0.0))) {
            explicit_min_width = resolve_length_value(
                lycon, CSS_PROPERTY_MIN_WIDTH, min_decl->value);
        }
    }
    if (explicit_min_width >= 0.0f) {
        // Grid min-content uses the item's explicit minimum; a native control's
        // natural width must not become an auto minimum after min-width is set.
        child_sizes->min_content = layout_css_size_to_border_box(
            child_view->bound, layout_box_sizing(child_view), explicit_min_width, true);
        child_sizes->max_content = max(child_sizes->max_content, child_sizes->min_content);
    }
}

static int intrinsic_count_grid_repeat_tracks(const CssFunction* func) {
    if (!func || func->arg_count <= 0 || !func->args[0] ||
        func->args[0]->type != CSS_VALUE_TYPE_NUMBER) {
        return 1;
    }
    int repeat_count = (int)func->args[0]->data.number.value; // INT_CAST_OK: CSS repeat() count is an integer track multiplier.
    int tracks_per_repeat = func->arg_count - 1;
    if (tracks_per_repeat < 1) tracks_per_repeat = 1;
    int expanded = repeat_count * tracks_per_repeat;
    return expanded;
}

static int intrinsic_count_grid_template_tracks(const CssValue* value) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) return 0;
    if (intrinsic_css_function_is(value, "repeat")) {
        return intrinsic_count_grid_repeat_tracks(value->data.function);
    }
    if (value->type != CSS_VALUE_TYPE_LIST) return 1;

    int total_tracks = 0;
    int list_count = value->data.list.count;
    CssValue** list_values = value->data.list.values;
    for (int i = 0; i < list_count; i++) {
        CssValue* item = list_values[i];
        if (!item) continue;
        if (intrinsic_css_function_is(item, "repeat")) {
            total_tracks += intrinsic_count_grid_repeat_tracks(item->data.function);
        } else {
            total_tracks++;
        }
    }
    return total_tracks > 0 ? total_tracks : list_count;
}

static bool intrinsic_style_keyword_declaration(StyleTree* style, CssPropertyCode property,
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

static bool intrinsic_element_display_matches(DomElement* element,
                                              CssEnum display_inner,
                                              CssEnum inline_display = CSS_VALUE__UNDEF) {
    if (!element) return false;
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (view && (view->display.inner == display_inner ||
                 view->display.outer == display_inner)) return true;
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
                                                int fallback_count) {
    int column_count = fallback_count;
    GridProp* grid_prop = (view && view->embed) ? view->embedp()->grid : nullptr;
    if (grid_prop && grid_prop->grid_template_columns) {
        column_count = grid_prop->grid_template_columns->track_count;
    }

    int unresolved_threshold = fallback_count > 0 ? fallback_count : 0;
    if (column_count <= unresolved_threshold && element && element->specified_style) {
        CssDeclaration* cols_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_GRID_TEMPLATE_COLUMNS);
        if (cols_decl && cols_decl->value) {
            column_count = intrinsic_count_grid_template_tracks(cols_decl->value);
        }
    }
    return column_count;
}

static float intrinsic_empty_grid_fixed_row_extent(GridProp* grid, float row_gap) {
    GridTrackList* rows = grid ? grid->grid_template_rows : nullptr;
    if (!rows || rows->track_count <= 0) return -1.0f;

    float extent = 0.0f;
    for (int row = 0; row < rows->track_count; row++) {
        GridTrackSize* track = rows->tracks ? rows->tracks[row] : nullptr;
        // An intrinsic grid height can use a track without item contributions
        // only when its used breadth is definite in an indefinite block axis.
        if (!track || track->type != GRID_TRACK_SIZE_LENGTH || track->is_percentage) {
            return -1.0f;
        }
        extent += static_cast<float>(track->value);
    }
    if (rows->track_count > 1) {
        extent += row_gap * static_cast<float>(rows->track_count - 1);
    }
    return extent;
}

static bool intrinsic_style_length_declaration(LayoutContext* lycon, StyleTree* style,
                                               CssPropertyCode property,
                                               CssPropertyCode fallback_property,
                                               CssPropertyCode resolve_property, float* out_value) {
    if (!style || !out_value) return false;

    CssDeclaration* decl = style_tree_get_declaration(style, property);
    if (!decl && fallback_property != CSS_PROPERTY_UNKNOWN) {
        decl = style_tree_get_declaration(style, fallback_property);
    }
    if (!decl || !decl->value || decl->value->type != CSS_VALUE_TYPE_LENGTH) return false;

    *out_value = resolve_length_value(lycon, resolve_property, decl->value);
    return true;
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
}

static bool intrinsic_has_ua_font_defaults(NameId tag) {
    static const NameId ua_font_tags[] = {
        MARKUP_NAME_B, MARKUP_NAME_STRONG, MARKUP_NAME_TH, MARKUP_NAME_I,
        MARKUP_NAME_EM, MARKUP_NAME_CITE, MARKUP_NAME_DFN, MARKUP_NAME_VAR,
        MARKUP_NAME_CODE, MARKUP_NAME_KBD, MARKUP_NAME_SAMP, MARKUP_NAME_TT,
        MARKUP_NAME_PRE, MARKUP_NAME_LISTING, MARKUP_NAME_XMP};
    return (tag >= MARKUP_NAME_H1 && tag <= MARKUP_NAME_H6) ||
        layout_tag_in_list(tag, ua_font_tags, sizeof(ua_font_tags) / sizeof(*ua_font_tags));
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
    NameId tag = element->tag();

    // UA font declarations remain the cascade base when an unrelated author
    // font longhand forces temporary intrinsic-measurement font resolution.
    if ((tag == MARKUP_NAME_B || tag == MARKUP_NAME_STRONG || tag == MARKUP_NAME_TH) &&
        !specified_weight) {
        font->font_weight = CSS_VALUE_BOLD;
        font->font_weight_numeric = 700;
        changed = true;
    } else if (tag >= MARKUP_NAME_H1 && tag <= MARKUP_NAME_H6) {
        if (!specified_size && parent_font) {
            static const float heading_scale[] = {2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f};
            font->font_size = parent_font->font_size * heading_scale[tag - MARKUP_NAME_H1];
            font->font_size_from_medium = false;
            changed = true;
        }
        if (!specified_weight) {
            font->font_weight = CSS_VALUE_BOLD;
            font->font_weight_numeric = 700;
            changed = true;
        }
    } else if ((tag == MARKUP_NAME_I || tag == MARKUP_NAME_EM || tag == MARKUP_NAME_CITE ||
                tag == MARKUP_NAME_DFN || tag == MARKUP_NAME_VAR) &&
               !specified_style) {
        font->font_style = CSS_VALUE_ITALIC;
        changed = true;
    } else if ((tag == MARKUP_NAME_CODE || tag == MARKUP_NAME_KBD || tag == MARKUP_NAME_SAMP ||
                tag == MARKUP_NAME_TT || tag == MARKUP_NAME_PRE || tag == MARKUP_NAME_LISTING ||
                tag == MARKUP_NAME_XMP) && !specified_family) {
        radiant_retain_font_family(font, lam::GcPtr<char>((char*)"monospace"));
        changed = true;
    }
    return changed;
}

static bool intrinsic_element_has_specified_font_size(DomElement* element) {
    if (!element || !element->specified_style) return false;
    return style_tree_get_declaration(element->specified_style, CSS_PROPERTY_FONT) != nullptr ||
        style_tree_get_declaration(element->specified_style, CSS_PROPERTY_FONT_SIZE) != nullptr;
}

static bool intrinsic_element_inside_table_fixup(DomElement* element) {
    for (DomNode* ancestor = element; ancestor; ancestor = ancestor->parent) {
        if (ancestor->is_element() && ancestor->as_element()->is_table_fixup()) {
            return true;
        }
    }
    return false;
}

static void intrinsic_complete_inherited_font(FontProp* font, FontProp* parent_font,
                                              bool preserve_zero_size) {
    radiant_fill_missing_font_values(font, parent_font, preserve_zero_size);
}

static float intrinsic_element_line_height(LayoutContext* lycon,
                                           DomElement* element,
                                           ViewBlock* view) {
    if (!lycon || !element) return 0.0f;
    float font_size = view && view->font && view->fontp()->font_size > 0.0f
        ? view->fontp()->font_size
        : (lycon->font.style && lycon->font.style->font_size > 0.0f
            ? lycon->font.style->font_size : 16.0f);
    float line_height = font_box_handle(&lycon->font)
        ? font_calc_normal_line_height(font_box_handle(&lycon->font))
        : font_size * 1.2f;
    const CssValue* line_height_value = view && view->blk
        ? view->block()->line_height : nullptr;
    if (!line_height_value && element->specified_style) {
        CssDeclaration* declaration = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_LINE_HEIGHT);
        line_height_value = declaration ? declaration->value : nullptr;
    }
    if (line_height_value) {
        float resolved = layout_resolve_line_height_value(
            lycon, line_height_value, element, font_size);
        if (resolved > 0.0f) line_height = resolved;
    }
    return line_height;
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

static float intrinsic_border_width_from_shorthand_value(LayoutContext* lycon, CssPropertyCode property,
                                                         const CssValue* value) {
    if (!value) return 0.0f;

    float border_width = -1.0f;
    bool has_visible_style = false;

    auto consider_keyword = [&](CssEnum kw) {
        if (kw == CSS_VALUE_THIN || kw == CSS_VALUE_MEDIUM || kw == CSS_VALUE_THICK) {
            border_width = layout_css_border_width_keyword(kw);
        } else if (kw == CSS_VALUE_SOLID || kw == CSS_VALUE_DASHED ||
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

static float intrinsic_border_width_from_spacing_value(LayoutContext* lycon, CssPropertyCode property,
                                                       const CssValue* value, int side_index) {
    if (!value) return 0.0f;

    const CssValue* side_value = css_box_shorthand_side_value(value, side_index);

    return resolve_length_value(lycon, property, side_value);
}

static float intrinsic_resolve_box_length(LayoutContext* lycon, CssPropertyCode property,
                                          const CssValue* value, float inline_base) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return inline_base > 0.0f ? (float)(value->data.percentage.value / 100.0) * inline_base : 0.0f;
    }
    return resolve_length_value(lycon, property, value);
}

static void get_intrinsic_box_side_widths_from_css(LayoutContext* lycon, DomElement* element,
                                                   bool horizontal, bool border,
                                                   float inline_base, float* start, float* end) {
    if (!element || !element->specified_style || !start || !end) return;

    float side_width[2] = {*start, *end};
    int64_t priority[2] = {-1, -1};
    const CssPropertyCode side_property[2] = {
        horizontal ? (border ? CSS_PROPERTY_BORDER_LEFT_WIDTH : CSS_PROPERTY_PADDING_LEFT)
                   : (border ? CSS_PROPERTY_BORDER_TOP_WIDTH : CSS_PROPERTY_PADDING_TOP),
        horizontal ? (border ? CSS_PROPERTY_BORDER_RIGHT_WIDTH : CSS_PROPERTY_PADDING_RIGHT)
                   : (border ? CSS_PROPERTY_BORDER_BOTTOM_WIDTH : CSS_PROPERTY_PADDING_BOTTOM)};
    const CssPropertyCode side_shorthand[2] = {
        horizontal ? (border ? CSS_PROPERTY_BORDER_LEFT : CSS_PROPERTY_PADDING_LEFT)
                   : (border ? CSS_PROPERTY_BORDER_TOP : CSS_PROPERTY_PADDING_TOP),
        horizontal ? (border ? CSS_PROPERTY_BORDER_RIGHT : CSS_PROPERTY_PADDING_RIGHT)
                   : (border ? CSS_PROPERTY_BORDER_BOTTOM : CSS_PROPERTY_PADDING_BOTTOM)};
    const int shorthand_index[2] = {
        horizontal ? 3 : 0, horizontal ? 1 : 2};

    auto apply_width = [&](const CssDeclaration* decl, float width, int side) {
        if (!decl || !decl->value) return;
        int64_t declaration_priority = get_cascade_priority(decl);
        if (declaration_priority >= priority[side]) {
            side_width[side] = width;
            priority[side] = declaration_priority;
        }
    };

    CssDeclaration* box_decl = style_tree_get_declaration(
        element->specified_style, border ? CSS_PROPERTY_BORDER : CSS_PROPERTY_PADDING);
    if (box_decl && box_decl->value) {
        if (border) {
            float width = intrinsic_border_width_from_shorthand_value(
                lycon, CSS_PROPERTY_BORDER_WIDTH, box_decl->value);
            apply_width(box_decl, width, 0);
            apply_width(box_decl, width, 1);
        } else {
            for (int side = 0; side < 2; side++) {
                const CssValue* value = css_box_shorthand_side_value(
                    box_decl->value, shorthand_index[side]);
                if (!value) continue;
                apply_width(box_decl, intrinsic_resolve_box_length(
                    lycon, side_property[side], value, inline_base), side);
            }
        }
    }

    if (border) {
        CssDeclaration* border_width_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_BORDER_WIDTH);
        if (border_width_decl && border_width_decl->value) {
            for (int side = 0; side < 2; side++) {
                apply_width(border_width_decl,
                    intrinsic_border_width_from_spacing_value(
                        lycon, CSS_PROPERTY_BORDER_WIDTH, border_width_decl->value,
                        shorthand_index[side]), side);
            }
        }
    }

    for (int side = 0; side < 2; side++) {
        CssDeclaration* declaration = style_tree_get_declaration(
            element->specified_style, side_property[side]);
        if (declaration && declaration->value) {
            float width = border
                ? resolve_length_value(lycon, side_property[side], declaration->value)
                : intrinsic_resolve_box_length(
                    lycon, side_property[side], declaration->value, inline_base);
            apply_width(declaration, width, side);
        }
        CssDeclaration* side_decl = style_tree_get_declaration(
            element->specified_style, side_shorthand[side]);
        if (border && side_decl && side_decl->value) {
            apply_width(side_decl,
                intrinsic_border_width_from_shorthand_value(
                    lycon, side_property[side], side_decl->value), side);
        }
    }

    *start = side_width[0];
    *end = side_width[1];
}

static bool intrinsic_percentage_width_is_indefinite(LayoutContext* lycon);

struct IntrinsicHorizontalBoxEdges {
    float padding_start;
    float padding_end;
    float border_start;
    float border_end;
};

static IntrinsicHorizontalBoxEdges intrinsic_horizontal_box_edges(
        LayoutContext* lycon, DomElement* element, bool resolve_cyclic_padding) {
    IntrinsicHorizontalBoxEdges edges = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!element) return edges;
    if (resolve_display_value(element).outer == CSS_VALUE_CONTENTS) {
        // CSS Display 3: a contents element has no principal box, so its
        // padding and border do not contribute to intrinsic sizing.
        return edges;
    }
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    bool cyclic = resolve_cyclic_padding &&
        intrinsic_percentage_width_is_indefinite(lycon) && element->specified_style;
    if (view->bound) {
        edges.padding_start = view->boundary()->padding.left;
        edges.padding_end = view->boundary()->padding.right;
        if (view->boundary()->border) {
            edges.border_start = view->boundary()->border->width.left;
            edges.border_end = view->boundary()->border->width.right;
        }
        if (!cyclic) return edges;
    }
    if (!element->specified_style) return edges;
    float inline_base = cyclic
        ? (lycon->block.parent ? lycon->block.parent->content_width : lycon->block.content_width)
        : lycon->block.content_width;
    get_intrinsic_box_side_widths_from_css(
        lycon, element, true, false, inline_base,
        &edges.padding_start, &edges.padding_end);
    if (!view->bound) {
        get_intrinsic_box_side_widths_from_css(
            lycon, element, true, true, 0.0f,
            &edges.border_start, &edges.border_end);
    }
    return edges;
}

static bool intrinsic_percentage_width_is_indefinite(LayoutContext* lycon) {
    return lycon && lycon->block.parent && lycon->block.parent->content_width < 0.0f;
}

float layout_intrinsic_padding_border_axis(LayoutContext* lycon, DomElement* element,
                                           bool horizontal, float inline_base) {
    if (!element) return 0.0f;
    if (resolve_display_value(element).outer == CSS_VALUE_CONTENTS) {
        // CSS Display 3: box edges belong to generated boxes, not the boxless
        // element whose descendants participate in the parent formatting context.
        return 0.0f;
    }

    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (view->bound) {
        BoxMetrics metrics = layout_box_metrics(view);
        // A percentage padding contribution is cyclic when the queried parent
        // has an indefinite inline size, so do not reuse the prior definite
        // layout value from the bound view.
        float metric = horizontal ? metrics.pad_border_h : metrics.pad_border_v;
        bool use_bound_metric = horizontal
            ? (!intrinsic_percentage_width_is_indefinite(lycon) || !element->specified_style)
            : metric > 0.0f;
        if (use_bound_metric) {
            return metric;
        }
        float padding_start = horizontal ? metrics.padding.left : metrics.padding.top;
        float padding_end = horizontal ? metrics.padding.right : metrics.padding.bottom;
        float effective_inline_base = intrinsic_percentage_width_is_indefinite(lycon)
            ? -1.0f : inline_base;
        get_intrinsic_box_side_widths_from_css(
            lycon, element, horizontal, false, effective_inline_base,
            &padding_start, &padding_end);
        return padding_start + padding_end + (horizontal ? metrics.border_h : metrics.border_v);
    }

    float padding_start = 0.0f;
    float padding_end = 0.0f;
    float border_start = 0.0f;
    float border_end = 0.0f;
    get_intrinsic_box_side_widths_from_css(
        lycon, element, horizontal, false, inline_base,
        &padding_start, &padding_end);
    get_intrinsic_box_side_widths_from_css(
        lycon, element, horizontal, true, inline_base,
        &border_start, &border_end);
    return padding_start + padding_end + border_start + border_end;
}

static bool intrinsic_apply_small_caps(uint32_t* codepoint, CssEnum font_variant) {
    if (!codepoint || font_variant != CSS_VALUE_SMALL_CAPS) return false;
    uint32_t original = *codepoint;
    uint32_t transformed[3];
    apply_text_transform_full(*codepoint, CSS_VALUE_UPPERCASE, false, transformed);
    *codepoint = transformed[0];
    return *codepoint != original;
}

static bool intrinsic_can_shape_simple_latin_run(LayoutContext* lycon,
                                                 CssEnum text_transform,
                                                 CssEnum font_variant,
                                                 bool break_anywhere,
                                                 bool break_word) {
    if (!lycon || !lycon->font.style || !font_box_handle(&lycon->font)) return false;
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
    LayoutSimpleLatinRun result = {};
    if (!layout_measure_simple_latin_run(
            lycon, font_box_handle(&lycon->font), str, remaining, &result)) {
        return false;
    }
    *out_bytes = result.bytes;
    *out_width = result.width;
    *out_first_codepoint = result.first_codepoint;
    *out_last_codepoint = result.last_codepoint;
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

static bool text_node_has_intrinsic_table_content(DomNode* node) {
    if (!node || !node->is_text()) return false;
    const char* text = (const char*)node->text_data();
    if (!text || !*text) return false;
    if (!text_node_is_ascii_whitespace(node)) return true;
    return white_space_preserves_space_advance(get_white_space_value(node));
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

struct IntrinsicMulticolChild {
    DomElement* element;
    ViewBlock* block;
    bool skipped;
    bool spans_all;
};

static bool intrinsic_multicol_child_info(DomNode* child, IntrinsicMulticolChild* info) {
    if (!child || !child->is_element() || !info) return false;
    info->element = child->as_element();
    info->block = lam::unsafe_view_block_element_storage(info->element);
    // Intrinsic multicol passes need one pre-layout interpretation of hidden, out-of-flow,
    // and column-spanning children; keeping it here prevents the two tree walks diverging.
    info->skipped = info->block && (layout_block_is_display_none(info->block) ||
                                    layout_view_is_abs_or_fixed(info->block));
    info->spans_all = info->block && info->block->multicol_prop() &&
        info->block->multicol_prop()->span == COLUMN_SPAN_ALL;
    if (!info->spans_all && info->element->specified_style) {
        CssDeclaration* span_decl = style_tree_get_declaration(
            info->element->specified_style, CSS_PROPERTY_COLUMN_SPAN);
        info->spans_all = span_decl && span_decl->value &&
            span_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            span_decl->value->data.keyword == CSS_VALUE_ALL;
    }
    return true;
}

static bool multicol_subtree_has_spanner(DomElement* element) {
    if (!element) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicMulticolChild child_info = {};
        if (!intrinsic_multicol_child_info(child, &child_info) || child_info.skipped) continue;
        if (child_info.spans_all) return true;
        if (child_info.block && multicol_spanner_can_escape_child(child_info.block) &&
            multicol_subtree_has_spanner(child_info.element)) {
            return true;
        }
    }
    return false;
}

static bool multicol_has_in_flow_non_spanner_content(DomElement* element) {
    if (!element) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            if (text_node_has_intrinsic_table_content(child)) return true;
            continue;
        }
        if (!child->is_element()) continue;

        IntrinsicMulticolChild child_info = {};
        if (!intrinsic_multicol_child_info(child, &child_info) || child_info.skipped) continue;
        // Intrinsic sizing runs before the child has a resolved multicol property, but
        // its cascaded column-span value already determines the column-flow group.
        if (child_info.spans_all) continue;
        // A spanner may escape several ordinary block wrappers; only content
        // that cannot escape into the column flow contributes to multicol width.
        bool nested_spanner = child_info.block &&
            multicol_spanner_can_escape_child(child_info.block) &&
            multicol_subtree_has_spanner(child_info.element);
        // A leaf block is ordinary column content; recurse only when a deeper
        // escaping spanner is present, otherwise intrinsic width would ignore it.
        if (!nested_spanner || !child_info.block ||
            multicol_has_in_flow_non_spanner_content(child_info.element)) {
            return true;
        }
    }

    // Generated block content participates in column flow even before intrinsic
    // sizing materializes its pseudo node; otherwise a hidden multicol clone loses
    // the column-count contribution from its last remembered size.
    for (int pseudo = 1; pseudo <= 2; pseudo++) {
        bool has_content = pseudo == 1 ? dom_element_has_before_content(element)
                                       : dom_element_has_after_content(element);
        if (!has_content) continue;
        StyleTree* pseudo_style = pseudo == 1
            ? element->pseudo_style(PSEUDO_STYLE_BEFORE)
            : element->pseudo_style(PSEUDO_STYLE_AFTER);
        CssDeclaration* span_decl = pseudo_style && pseudo_style->tree
            ? style_tree_get_declaration(pseudo_style, CSS_PROPERTY_COLUMN_SPAN) : nullptr;
        bool spans_all = span_decl && span_decl->value &&
            span_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            span_decl->value->data.keyword == CSS_VALUE_ALL;
        if (!spans_all) return true;
    }
    return false;
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
    LayoutCssBoxCandidates candidates = {};
    candidates.collect(element->specified_style, true);

    bool resolved = false;
    if (candidates.sides[0].value && intrinsic_resolve_margin_value(
            lycon, candidates.sides[0].value, inline_base, mt)) {
        resolved = true;
    }
    if (candidates.sides[2].value && intrinsic_resolve_margin_value(
            lycon, candidates.sides[2].value, inline_base, mb)) {
        resolved = true;
    }
    return resolved;
}

static float intrinsic_resolve_horizontal_margin_value(LayoutContext* lycon,
                                                       uintptr_t property,
                                                       const CssValue* value) {
    if (!lycon || !value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE ||
        (value->type == CSS_VALUE_TYPE_KEYWORD &&
         value->data.keyword == CSS_VALUE_AUTO)) {
        return 0.0f;
    }

    BlockContext zero_percent_parent = {};
    if (lycon->block.parent) {
        zero_percent_parent = *lycon->block.parent;
    }
    // CSS Sizing 3 resolves percentage terms in an intrinsic margin
    // contribution against zero; preserve fixed terms in calc() instead of
    // dropping the whole expression when no used-width basis exists.
    zero_percent_parent.content_width = 0.0f;
    zero_percent_parent.content_height = 0.0f;
    LayoutContext zero_percent_context = *lycon;
    zero_percent_context.block.parent = &zero_percent_parent;
    float resolved = resolve_length_value(&zero_percent_context, property, value);
    return isnan(resolved) ? 0.0f : resolved;
}

float layout_resolve_intrinsic_margin_side(LayoutContext* lycon,
                                           ViewElement* element,
                                           CssPropertyCode property,
                                           float inline_base,
                                           bool include_bound) {
    if (!element) return 0.0f;
    auto resolve = [&](CssValue* value) -> bool {
        if (!value) return false;
        if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            if (inline_base < 0.0f) return false;
            float resolved = 0.0f;
            if (!layout_resolve_percentage_value(value, inline_base, &resolved)) return false;
            inline_base = resolved;
            return true;
        }
        if (value->type == CSS_VALUE_TYPE_KEYWORD &&
            value->data.keyword == CSS_VALUE_AUTO) {
            inline_base = 0.0f;
            return true;
        }
        float resolved = intrinsic_resolve_horizontal_margin_value(
            lycon, property, value);
        if (!isnan(resolved) && resolved >= 0.0f) {
            inline_base = resolved;
            return true;
        }
        return false;
    };

    if (element->specified_style) {
        CssDeclaration* declaration = style_tree_get_declaration(
            element->specified_style, property);
        if (declaration && declaration->value && resolve(declaration->value)) {
            return inline_base;
        }
        CssDeclaration* shorthand = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MARGIN);
        const CssValue* side = shorthand && shorthand->value
            ? css_box_shorthand_side_value(shorthand->value, radiant_css_box_side(property))
            : nullptr;
        if (side && resolve((CssValue*)side)) return inline_base;
    }

    if (include_bound && element->bound) {
        float* margin = radiant_spacing_value(
            &element->boundary_mut()->margin, radiant_css_box_side(property));
        if (margin && !isnan(*margin) && *margin >= 0.0f) return *margin;
    }
    return 0.0f;
}


static bool node_is_table_cell_like(DomNode* node) {
    if (!node || !node->is_element()) return false;
    DomElement* elem = node->as_element();
    return elem->view_type == RDT_VIEW_TABLE_CELL ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE_CELL);
}

static bool intrinsic_is_table_box(DomElement* elem) {
    if (!elem) return false;
    return elem->tag() == MARKUP_NAME_TABLE ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE) ||
           intrinsic_element_display_matches(elem, CSS_VALUE_INLINE_TABLE);
}

static bool intrinsic_is_table_row_group_box(DomElement* elem) {
    if (!elem) return false;
    NameId tag = elem->tag();
    return tag == MARKUP_NAME_TBODY || tag == MARKUP_NAME_THEAD || tag == MARKUP_NAME_TFOOT ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE_ROW_GROUP) ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE_HEADER_GROUP) ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE_FOOTER_GROUP);
}

static bool intrinsic_is_table_row_box(DomElement* elem) {
    if (!elem) return false;
    return elem->tag() == MARKUP_NAME_TR ||
           intrinsic_element_display_matches(elem, CSS_VALUE_TABLE_ROW);
}

static int intrinsic_table_row_count(DomElement* elem) {
    if (!elem) return 0;
    if (intrinsic_is_table_row_box(elem)) return 1;

    int row_count = 0;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_elem = child->as_element();
        if (intrinsic_is_table_row_box(child_elem) ||
            intrinsic_is_table_row_group_box(child_elem)) {
            row_count += intrinsic_table_row_count(child_elem);
        }
    }
    return row_count;
}

static bool intrinsic_child_is_out_of_flow(DomElement* element, ViewBlock* block) {
    if (!element) return true;
    if (block && layout_block_is_out_of_flow_positioned(block)) {
        return true;
    }
    // Floats are out of normal flow but still contribute to intrinsic inline
    // sizing; the float-run accumulation below must be able to observe them.
    // Script changes can leave an allocated PositionProp stale until the next
    // layout pass, so intrinsic sizing must also consult the current cascade.
    return layout_element_is_abs_or_fixed(element);
}

static bool intrinsic_should_skip_height_child(DomElement* elem) {
    if (!elem) return true;
    if (layout_element_is_display_none(elem)) {
        return true;
    }
    ViewBlock* block = lam::view_as_block(elem);
    // Floats contribute to intrinsic width but not their parent's normal-flow
    // block-size, matching CSS 2.2 §9.5's removal from block flow.
    return (block && block->position && element_has_float(block)) ||
        intrinsic_child_is_out_of_flow(elem, block);
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

    if (intrinsic_is_table_box(elem) && elem->tag() == MARKUP_NAME_TABLE) {
        ViewTable* table = lam::unsafe_view_table_storage(elem);
        if (table && table->tb && !table->tb->border_collapse) {
            // Intrinsic table height must include the outer border-spacing gaps;
            // otherwise a flex/grid query sees a shorter table than table layout.
            int row_count = intrinsic_table_row_count(elem);
            total_height += table->tb->border_spacing_v * (row_count + 1);
        }
    }
    return total_height;
}

static float intrinsic_loaded_glyph_advance(LayoutContext* lycon,
                                            uint32_t codepoint,
                                            bool small_caps_lower,
                                            float kerning,
                                            bool emoji_presentation,
                                            bool* loaded);

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
                lycon, font_box_handle(&lycon->font), lycon->font.style);
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
                lycon, font_box_handle(&lycon->font), lycon->font.style);
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
        bool is_small_caps_lower = intrinsic_apply_small_caps(&codepoint, font_variant);
        is_word_start = false;

        for (int ti = 0; ti < tt_count; ti++) {
            uint32_t cp = (ti == 0) ? codepoint : tt_out[ti];
            if (cp == 0 || text_codepoint_has_zero_advance(cp)) {
                continue;
            }
            bool loaded = false;
            float advance = intrinsic_loaded_glyph_advance(
                lycon, cp, is_small_caps_lower, 0.0f, false, &loaded);
            if (!loaded) {
                advance = lycon->font.style ? lycon->font.style->font_size * 0.5f : 8.0f;
            }
            width += advance;
        }
        i += (size_t)bytes;
    }

    return width;
}

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
                                            bool emoji_presentation,
                                            bool* loaded) {
    FontStyleDesc style = font_style_desc_from_prop(lycon->font.style);
    // Intrinsic sizing must use the current FontStyleDesc because font_get_glyph()
    // can retain stale advances after a dynamic @font-face load.
    LoadedGlyph* glyph = emoji_presentation
        ? font_load_glyph_emoji(font_box_handle(&lycon->font), &style, codepoint, false)
        : font_load_glyph(font_box_handle(&lycon->font), &style, codepoint, false);
    *loaded = glyph != nullptr;
    if (!glyph) return 0.0f;

    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0)
        ? lycon->ui_context->pixel_ratio : 1.0f;
    float advance = glyph->advance_x / pixel_ratio + kerning;
    if (small_caps_lower) {
        advance *= font_get_small_caps_scale(font_box_handle(&lycon->font));
    }
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
            lycon, transformed[index], false, 0.0f, false, &loaded);
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
    bool break_anywhere = (overflow_wrap == CSS_VALUE_ANYWHERE);
    bool break_word = (overflow_wrap == CSS_VALUE_BREAK_WORD);
    bool cjk_breaks = (word_break != CSS_VALUE_KEEP_ALL);
    TextIntrinsicWidths result = {0, 0};

    if (!text || length == 0) {
        return result;
    }

    if (lycon->font.style && lycon->font.style->font_size <= 0.0f) {
        return result;  // min_content=0, max_content=0
    }

    if (!font_box_handle(&lycon->font)) {
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
    uint8_t text_autospace = layout_text_autospace_flags(lycon);
    bool text_autospace_bidi_unsupported = layout_text_contains_rtl_codepoint(
        text, length);

    uint32_t prev_codepoint = 0;
    uint32_t prev_text_autospace_codepoint = 0;
    const FontMetrics* fm = font_box_handle(&lycon->font) ? font_get_metrics(font_box_handle(&lycon->font)) : NULL;
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

        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            float kerning_adj = 0.0f;
            if (has_kerning && prev_codepoint) {
                kerning_adj = font_get_kerning(font_box_handle(&lycon->font), prev_codepoint, (uint32_t)ch);
            }

            float space_width = layout_measure_space_advance(
                lycon, font_box_handle(&lycon->font), lycon->font.style);
            if (lycon->font.style) {
                space_width += lycon->font.style->word_spacing;
            }
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
                    kerning = font_get_kerning(font_box_handle(&lycon->font),
                                               prev_codepoint, shaped_first_cp);
                }
                float advance = shaped_width + kerning;
                if (!text_autospace_bidi_unsupported && layout_text_autospace_pair(
                        text_autospace, prev_text_autospace_codepoint,
                        shaped_first_cp)) {
                    advance += layout_text_autospace_advance(lycon);
                }
                current_word += advance;
                total_width += advance;
                prev_codepoint = shaped_last_cp;
                prev_text_autospace_codepoint = shaped_last_cp;
                prev_is_zwj_base = false;
                is_word_start = false;
                i += shaped_bytes;
                continue;
            }
        }

        uint32_t codepoint;
        int bytes = str_utf8_decode((const char*)&str[i], length - i, &codepoint);
        if (bytes <= 0) {
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

        bool is_small_caps_lower = intrinsic_apply_small_caps(&codepoint, font_variant);

        is_word_start = false;  // No longer at word start after first character

        uint32_t glyph_index = font_get_glyph_index(font_box_handle(&lycon->font), codepoint);

        float kerning = 0.0f;
        if (glyph_index && has_kerning && prev_codepoint) {
            kerning = font_get_kerning(font_box_handle(&lycon->font), prev_codepoint, codepoint);
        }

        bool emoji_presentation = false;
        size_t selector_pos = i + (size_t)bytes;
        if (selector_pos < length) {
            uint32_t selector = 0;
            int selector_bytes = str_utf8_decode(
                (const char*)&str[selector_pos], length - selector_pos, &selector);
            emoji_presentation = selector_bytes > 0 && selector == 0xFE0F;
        }
        bool glyph_loaded = false;
        // VS16 selects the emoji face for both max-content measurement and
        // final layout; measuring the base glyph in the text face undercounts
        // dual-presentation clusters such as the warning sign.
        float advance = intrinsic_loaded_glyph_advance(
            lycon, codepoint, is_small_caps_lower, kerning,
            emoji_presentation, &glyph_loaded);
        if (!glyph_loaded) advance = 11.0f;
        if (!text_autospace_bidi_unsupported && layout_text_autospace_pair(
                text_autospace, prev_text_autospace_codepoint, codepoint)) {
            advance += layout_text_autospace_advance(lycon);
        }
        if (glyph_loaded && codepoint == 0x00A0 && lycon->font.style) {
            advance += lycon->font.style->word_spacing;
            is_word_start = true;
        }
        current_word += advance;
        total_width += advance;

        if (glyph_index) prev_codepoint = codepoint;
        prev_text_autospace_codepoint = codepoint;
        i += bytes;  // Advance by the number of bytes consumed

        if (break_anywhere || intrinsic_allows_soft_wrap_after_codepoint(codepoint)) {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
        }
        else if (cjk_breaks && has_id_line_break_class(codepoint)) {
            longest_word = fmax(longest_word, current_word);
            current_word = 0.0f;
        }
    }

    longest_word = fmax(longest_word, current_word);

    result.min_content = longest_word;   // Keep float precision
    result.max_content = total_width;    // Keep float precision

    return result;
}

float layout_vertical_text_block_extent(const char* text, size_t length,
                                         float line_advance,
                                         float available_inline_size) {
    if (line_advance <= 0.0f) return 0.0f;
    if (!text || length == 0 || available_inline_size <= 0.0f) {
        return line_advance;
    }

    size_t units_per_line = (size_t)floorf(available_inline_size / line_advance);
    if (units_per_line == 0) units_per_line = 1;

    size_t codepoint_count = 0;
    bool has_non_whitespace = false;
    for (size_t offset = 0; offset < length;) {
        uint32_t codepoint = 0;
        int byte_count = str_utf8_decode(text + offset, length - offset, &codepoint);
        if (byte_count <= 0) byte_count = 1;
        offset += (size_t)byte_count;
        if (codepoint == '\n' || codepoint == '\r' ||
            text_codepoint_has_zero_advance(codepoint)) {
            continue;
        }
        bool is_whitespace = codepoint == ' ' || codepoint == '\t' ||
            codepoint == '\f' || codepoint == 0x00A0 ||
            utf8proc_category(static_cast<utf8proc_int32_t>(codepoint)) ==
                UTF8PROC_CATEGORY_ZS;
        if (!is_whitespace) has_non_whitespace = true;
        codepoint_count++;
    }
    // Collapsed whitespace has no independent vertical column; counting each
    // nbsp in a whitespace-only float inflated its intrinsic block-size before
    // the definite line-height could establish the one real line box.
    if (!has_non_whitespace) return line_advance;
    if (codepoint_count == 0) return line_advance;

    size_t line_count = (codepoint_count + units_per_line - 1) / units_per_line;
    return (float)line_count * line_advance;
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
                                                  CssPropertyCode property, float* out_value,
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
                                                    CssPropertyCode side_property,
                                                    CssPropertyCode side_width_property) {
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

float compute_text_height_at_width(LayoutContext* lycon,
                                    const char* text,
                                    size_t length,
                                    float available_width,
                                    float line_height,
                                    CssEnum text_transform,
                                    CssEnum font_variant,
                                    bool preserve_forced_line_breaks) {
    if (!text || length == 0 || available_width <= 0 || line_height <= 0) {
        return line_height;  // at least one line
    }

    int line_count = 1;
    float current_line_width = 0;
    float current_word_width = 0;

    // Use font metrics if available, otherwise rough estimate
    bool has_font = font_box_handle(&lycon->font) != nullptr;
    const FontMetrics* fm2 = has_font ? font_get_metrics(font_box_handle(&lycon->font)) : NULL;
    bool has_kerning = fm2 ? fm2->has_kerning : false;
    if (lycon->font.style && lycon->font.style->font_kerning == CSS_VALUE_NONE) has_kerning = false;
    uint32_t prev_codepoint = 0;
    bool is_word_start = true;

    const unsigned char* str = (const unsigned char*)text;

    for (size_t i = 0; i < length; ) {
        unsigned char ch = str[i];
        if (preserve_forced_line_breaks && ch == '\v') {
            if (current_line_width > 0 && current_line_width + current_word_width > available_width) {
                line_count++;
            }
            current_line_width = 0.0f;
            current_word_width = 0.0f;
            prev_codepoint = 0;
            is_word_start = true;
            line_count++;
            i++;
            continue;
        }
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
                lycon, font_box_handle(&lycon->font), lycon->font.style);
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

        // CSS font-variant: small-caps uses uppercase glyphs with scaled advance.
        bool is_small_caps_lower = intrinsic_apply_small_caps(&codepoint, font_variant);

        is_word_start = false;

        if (text_codepoint_has_zero_advance(codepoint)) {
            i += bytes;
            continue;
        }
        bool glyph_loaded = false;
        bool primary_glyph = has_font &&
            font_get_glyph_index(font_box_handle(&lycon->font), codepoint) != 0;
        float kerning = primary_glyph && has_kerning && prev_codepoint
            ? font_get_kerning(font_box_handle(&lycon->font), prev_codepoint, codepoint) : 0.0f;
        float advance = has_font ? intrinsic_loaded_glyph_advance(
            lycon, codepoint, is_small_caps_lower, kerning, false, &glyph_loaded) : 0.0f;
        if (!glyph_loaded) advance = 11.0f;
        if (primary_glyph) {
            prev_codepoint = codepoint;
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
    return result;
}

static bool is_inline_level_element(DomElement* element);

static bool is_inline_level_element(DomElement* element) {
    if (!element) return false;

    DisplayValue resolved_display = resolve_display_value((void*)element);
    if (resolved_display.outer == CSS_VALUE_CONTENTS) {
        // CSS Display 3: display:contents contributes no box; expose its
        // descendants as inline content only when they contain no block flow box.
        return !layout_display_contents_has_block_child(element);
    }

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
    static const char* const default_inline_tags[] = {
        "a", "span", "em", "strong", "b", "i", "u", "s", "small", "big",
        "sub", "sup", "code", "tt", "kbd", "samp", "var", "cite", "abbr",
        "acronym", "dfn", "q", "br", "time", "mark", "label", "img", "video",
        "audio", "canvas", "iframe", "embed", "object", "svg", "meter",
        "progress", "button", "input", "select", "textarea"
    };
    for (size_t i = 0; tag && i < sizeof(default_inline_tags) / sizeof(default_inline_tags[0]); i++) {
        if (strcmp(tag, default_inline_tags[i]) == 0) return true;
    }
    return false;
}

static bool intrinsic_element_has_percentage_height(DomElement* element) {
    if (!element) return false;

    CssDeclaration* height_decl = layout_specified_physical_size_declaration(element, false);
    if (height_decl && height_decl->value &&
        height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return true;
    }
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    return view->blk && !isnan(view->block()->given_height_percent);
}

static bool intrinsic_has_cyclic_percentage_descendant(
        LayoutContext* lycon, DomElement* element, bool require_ratio) {
    if (!element || (require_ratio &&
        (!lycon || intrinsic_declared_css_height_is_definite(lycon, element)))) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!child->is_element()) continue;
        DomElement* child_element = child->as_element();
        ViewBlock* child_view = lam::unsafe_view_block_element_storage(child_element);
        if (layout_block_is_display_none(child_view) ||
            layout_view_is_abs_or_fixed(child_view) ||
            (child_view->position && element_has_float(child_view))) {
            continue;
        }
        bool matches = intrinsic_element_has_percentage_height(child_element) &&
            (!require_ratio || intrinsic_element_preferred_aspect_ratio(
                child_view) > 0.0f);
        if ((!require_ratio && layout_element_is_replaced(child_element) && matches) ||
            (require_ratio && matches)) {
            return true;
        }
        if (intrinsic_has_cyclic_percentage_descendant(
                lycon, child_element, require_ratio)) {
            return true;
        }
    }
    return false;
}

bool layout_has_cyclic_percentage_replaced_descendant(DomElement* element) {
    return intrinsic_has_cyclic_percentage_descendant(nullptr, element, false);
}

bool layout_has_cyclic_percentage_ratio_descendant(LayoutContext* lycon, DomElement* element) {
    return intrinsic_has_cyclic_percentage_descendant(lycon, element, true);
}

static bool intrinsic_element_is_atomic_inline(DomElement* element) {
    if (!element || !is_inline_level_element(element)) return false;
    if (layout_element_is_replaced(element)) return true;

    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if ((element->display.inner == CSS_VALUE_FLOW_ROOT ||
         view->display.inner == CSS_VALUE_FLOW_ROOT) &&
        (element->display.outer == CSS_VALUE_INLINE ||
         element->display.outer == CSS_VALUE_INLINE_BLOCK ||
         view->display.outer == CSS_VALUE_INLINE ||
         view->display.outer == CSS_VALUE_INLINE_BLOCK)) {
        // CSS Display 3: `inline flow-root` is an atomic inline whose children
        // are measured as block-flow content, not as one inline run.
        return true;
    }
    if (view->display.outer == CSS_VALUE_INLINE_BLOCK ||
        view->display.outer == CSS_VALUE_INLINE_FLEX ||
        view->display.outer == CSS_VALUE_INLINE_GRID ||
        view->display.outer == CSS_VALUE_INLINE_TABLE) {
        return true;
    }

    CssEnum display = (CssEnum)0;
    if (intrinsic_specified_display_keyword(element, &display)) {
        return display == CSS_VALUE_INLINE_BLOCK ||
            display == CSS_VALUE_INLINE_FLEX ||
            display == CSS_VALUE_INLINE_GRID ||
            display == CSS_VALUE_INLINE_TABLE;
    }
    return false;
}

static bool intrinsic_white_space_collapses_space_advance(CssEnum white_space) {
    return !white_space_preserves_space_advance(white_space);
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
        return white_space_preserves_space_advance(get_white_space_value(node));
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    // Anonymous table repair contributes atomic inline-table boxes; whitespace
    // before them must survive max-content sizing just like before text.
    if (node_is_table_cell_like(node) || element->display.inner == CSS_VALUE_TABLE) return true;
    // Atomic inline boxes are boundaries even when their resolved outer display
    // is not materialized yet during the intrinsic measurement pass.
    if (intrinsic_element_is_atomic_inline(element)) return true;
    if (!is_inline_level_element(element)) return false;
    // Replaced boxes are atomic inline content even though they have no DOM children.
    if (layout_element_is_replaced(element)) return true;
    for (int pseudo = 1; pseudo <= 2; pseudo++) {
        bool has_content = pseudo == 1 ? dom_element_has_before_content(element)
                                       : dom_element_has_after_content(element);
        StyleTree* style = pseudo == 1 ? element->pseudo_style(PSEUDO_STYLE_BEFORE) : element->pseudo_style(PSEUDO_STYLE_AFTER);
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

static bool intrinsic_node_ends_with_forced_break(DomNode* node);

static bool intrinsic_has_inline_boundary_content_in_direction(DomNode* node, bool following) {
    DomNode* current = node;
    while (current) {
        for (DomNode* sibling = following ? current->next_sibling : current->prev_sibling;
             sibling; sibling = following ? sibling->next_sibling : sibling->prev_sibling) {
            if (sibling->is_comment()) continue;
            if (intrinsic_node_has_inline_boundary_content(sibling)) {
                // a preserved newline starts a new inline formatting line.
                return following ? true : !intrinsic_node_ends_with_forced_break(sibling);
            }
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

static bool intrinsic_node_ends_with_forced_break(DomNode* node) {
    if (!node) return false;
    if (node->is_text()) {
        const char* text = (const char*)node->text_data();
        if (!text || !*text) return false;
        size_t length = strlen(text);
        CssEnum white_space = get_white_space_value(node);
        bool preserves_newlines = white_space == CSS_VALUE_PRE ||
            white_space == CSS_VALUE_PRE_WRAP ||
            white_space == CSS_VALUE_BREAK_SPACES ||
            white_space == CSS_VALUE_PRE_LINE;
        return preserves_newlines &&
            (text[length - 1] == '\n' || text[length - 1] == '\r');
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    if (!is_inline_level_element(element)) return false;
    for (DomNode* child = element->last_child; child; child = child->prev_sibling) {
        if (child->is_comment()) continue;
        return intrinsic_node_ends_with_forced_break(child);
    }
    return false;
}

static bool intrinsic_node_has_collapsible_space_at_edge(DomNode* node, bool end) {
    if (!node) return false;
    if (node->is_text()) {
        return layout_text_edge_has_whitespace((const char*)node->text_data(), end) &&
            intrinsic_white_space_collapses_space_advance(get_white_space_value(node));
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    if (!is_inline_level_element(element)) return false;
    for (DomNode* child = end ? element->last_child : element->first_child;
         child; child = end ? child->prev_sibling : child->next_sibling) {
        if (child->is_comment()) continue;
        return intrinsic_node_has_collapsible_space_at_edge(child, end);
    }
    return false;
}

static CssEnum intrinsic_element_box_decoration_break(DomElement* element) {
    if (!element || !is_inline_level_element(element)) {
        return CSS_VALUE__UNDEF;
    }
    // Intrinsic edge accounting must not treat the initial computed `slice` as
    // authored decoration; doing so suppresses normal line-edge whitespace trim.
    CssDeclaration* declaration = dom_element_get_specified_value(
        element, CSS_PROPERTY_BOX_DECORATION_BREAK);
    if (!declaration || !declaration->value ||
        declaration->value->type != CSS_VALUE_TYPE_KEYWORD) {
        return CSS_VALUE__UNDEF;
    }
    return declaration->value->data.keyword;
}

static bool intrinsic_element_has_explicit_box_decoration_break(DomElement* element) {
    CssEnum decoration_break = intrinsic_element_box_decoration_break(element);
    return decoration_break == CSS_VALUE_SLICE || decoration_break == CSS_VALUE_CLONE;
}

static float intrinsic_collapsed_space_width(LayoutContext* lycon) {
    float width = layout_measure_space_advance(
        lycon, font_box_handle(&lycon->font), lycon->font.style);
    if (lycon && lycon->font.style) {
        width += lycon->font.style->word_spacing;
        width += lycon->font.style->letter_spacing;
    }
    return width;
}

static bool intrinsic_pseudo_child_is_materialized(DomElement* element, bool is_before) {
    if (!element || !element->pseudo) return false;

    DomElement* pseudo = is_before ? element->pseudo->before : element->pseudo->after;
    if (!pseudo) return false;

    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (dom_subtree_contains_node(child, static_cast<DomNode*>(pseudo))) return true;
    }
    return false;
}

static bool intrinsic_pseudo_needs_intrinsic_materialization(DomElement* element, bool is_before) {
    if (!element) return false;
    StyleTree* pseudo_styles = is_before ? element->pseudo_style(PSEUDO_STYLE_BEFORE) : element->pseudo_style(PSEUDO_STYLE_AFTER);
    if (!pseudo_styles || !pseudo_styles->tree) return false;
    if (intrinsic_pseudo_child_is_materialized(element, is_before)) return false;

    CssEnum display = (CssEnum)0;
    if (!intrinsic_style_display_keyword(pseudo_styles, &display)) return false;
    // Block-level generated boxes are real in-flow children for intrinsic sizing;
    // deferring their materialization until normal layout drops their contribution.
    return !intrinsic_pseudo_style_is_inline(pseudo_styles);
}

static bool intrinsic_list_item_has_table_ancestor(DomElement* element) {
    for (DomNode* ancestor = element ? element->parent : nullptr;
         ancestor; ancestor = ancestor->parent) {
        if (!ancestor->is_element()) continue;
        DisplayValue display = resolve_display_value(ancestor);
        // A list inside an existing table cell already sizes against that cell;
        // only content entering an anonymous cell from a row/group needs the
        // marker contribution at this level.
        if (display.inner == CSS_VALUE_TABLE_CELL) {
            return layout_element_is_anonymous_table_fixup(ancestor->as_element());
        }
        if (display.inner == CSS_VALUE_TABLE ||
            display.inner == CSS_VALUE_TABLE_ROW ||
            layout_display_is_table_row_group(display.inner)) {
            return true;
        }
    }
    return false;
}

static float intrinsic_list_item_marker_width(LayoutContext* lycon,
                                              ViewBlock* view_block) {
    if (view_block && view_block->pseudo && view_block->pseudo->marker &&
        view_block->pseudo->marker->blk) {
        MarkerProp* marker = reinterpret_cast<MarkerProp*>(
            view_block->pseudo->marker->blk);
        if (marker->width > 0.0f) {
            float width = marker->width;
            DomElement* element = lam::dom_as<DOM_NODE_ELEMENT>(view_block);
            if (element && !marker->is_outside &&
                !marker->trailing_space_trimmed &&
                marker->trailing_space_width > 0.0f &&
                !layout_list_item_has_in_flow_content(element)) {
                width -= marker->trailing_space_width;
            }
            return max(width, 0.0f);
        }
    }
    float font_size = 16.0f;
    if (view_block && view_block->font && view_block->fontp()->font_size > 0.0f) {
        font_size = view_block->fontp()->font_size;
    } else if (lycon && lycon->font.current_font_size > 0.0f) {
        font_size = lycon->font.current_font_size;
    }
    return font_size * 1.375f;
}

static bool intrinsic_css_list_position_is_inside(const CssValue* value,
                                                  bool* found) {
    if (!value || !found) return false;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (intrinsic_css_list_position_is_inside(value->data.list.values[i], found)) {
                return true;
            }
            if (*found) return false;
        }
        return false;
    }
    const char* name = nullptr;
    if (value->type == CSS_VALUE_TYPE_CUSTOM) {
        name = value->data.custom_property.name;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        name = info ? info->name : nullptr;
    }
    if (!name || (strcmp(name, "inside") != 0 && strcmp(name, "outside") != 0)) {
        return false;
    }
    *found = true;
    return strcmp(name, "inside") == 0;
}

static bool intrinsic_list_item_marker_is_inside(DomElement* element,
                                                 ViewBlock* view_block) {
    for (DomNode* node = element; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* node_element = node->as_element();
        ViewBlock* node_view = node == element
            ? view_block
            : lam::unsafe_view_block_element_storage(node_element);
        if (node_view && node_view->blk &&
            node_view->block()->list_style_position != 0) {
            // CSS Lists 3 §3.5: inside markers are inline content and therefore
            // belong to the list item's intrinsic inline-size contribution.
            return node_view->block()->list_style_position == 1;
        }
        // Intrinsic measurement can run before the cascade populates BlockProp;
        // read the inherited declaration directly instead of treating inside as outside.
        if (!node_element->specified_style) continue;
        CssPropertyCode properties[] = {
            CSS_PROPERTY_LIST_STYLE_POSITION, CSS_PROPERTY_LIST_STYLE
        };
        for (CssPropertyCode property : properties) {
            CssDeclaration* declaration = style_tree_get_declaration(
                node_element->specified_style, property);
            if (!declaration || !declaration->value) continue;
            bool found = false;
            bool inside = intrinsic_css_list_position_is_inside(
                declaration->value, &found);
            if (found) return inside;
        }
    }
    return false;
}

static void intrinsic_materialize_pseudo_content(LayoutContext* lycon, DomElement* element) {
    if (!lycon || !element) return;

    NameId tag = element->tag();
    ViewBlock* element_view = lam::unsafe_view_block_element_storage(element);
    bool select_listbox = tag == MARKUP_NAME_SELECT &&
        ((element_view && element_view->form && element_view->form->multiple) ||
         element->has_attribute("multiple"));
    if (tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_SVG ||
        tag == MARKUP_NAME_CANVAS || tag == MARKUP_NAME_IFRAME ||
        tag == MARKUP_NAME_VIDEO || tag == MARKUP_NAME_AUDIO ||
        tag == MARKUP_NAME_EMBED || tag == MARKUP_NAME_OBJECT ||
        (tag == MARKUP_NAME_SELECT && !select_listbox)) {
        // Replaced content does not generate ::before/::after boxes; materializing
        // one here would turn the remembered replaced size into pseudo-content size.
        return;
    }

    bool needs_before = dom_element_has_before_content(element) &&
        intrinsic_pseudo_needs_intrinsic_materialization(element, true);
    bool needs_after = dom_element_has_after_content(element) &&
        intrinsic_pseudo_needs_intrinsic_materialization(element, false);
    if (!needs_before && !needs_after) return;

    // Generated block-level pseudo boxes must exist before the child walk, or
    // shrink-to-fit misses their intrinsic contribution.
    layout_materialize_pseudo_content(
        lycon, lam::unsafe_view_block_element_storage(element));
}

static void intrinsic_prepare_anonymous_table_children(LayoutContext* lycon,
        DomElement* element) {
    if (!lycon || !element || !element->first_child) return;

    DisplayValue display = resolve_display_value((void*)element);
    if (layout_display_is_table_structure(display.inner)) return;

    // CSS 2.1 §17.2.1 anonymous table wrappers affect intrinsic inline-size
    // contributions. Layout creates them before flowing content; intrinsic sizing
    // must do the same before walking children, otherwise column measurement can
    // miss the anonymous table's border-spacing and row/cell structure.
    wrap_orphaned_table_children(lycon, element);
}

static float intrinsic_parent_definite_height(LayoutContext* lycon) {
    BlockContext* parent = lycon ? lycon->block.parent : nullptr;
    if (!parent) return -1.0f;
    if (parent->content_height > 0.0f) return parent->content_height;
    return parent->given_height > 0.0f ? parent->given_height : -1.0f;
}

static float intrinsic_resolved_height_limit_or(LayoutContext* lycon,
                                                DomElement* element,
                                                ViewBlock* view, bool minimum) {
    if (element && element->specified_style) {
        CssDeclaration* declaration = layout_specified_physical_minmax_size_declaration(
            element, false, minimum);
        const CssValue* value = declaration ? declaration->value : nullptr;
        if (value && (value->type == CSS_VALUE_TYPE_LENGTH ||
                      value->type == CSS_VALUE_TYPE_PERCENTAGE ||
                      value->type == CSS_VALUE_TYPE_FUNCTION ||
                      (value->type == CSS_VALUE_TYPE_NUMBER &&
                       value->data.number.value == 0))) {
            float resolved = resolve_length_value(
                lycon, minimum ? CSS_PROPERTY_MIN_HEIGHT : CSS_PROPERTY_MAX_HEIGHT, value);
            if (!isnan(resolved)) {
                // intrinsic measurement may precede full child style resolution;
                // resolve the declaration against the current definite parent.
                return resolved;
            }
        }
    }
    return minimum ? layout_explicit_min_axis_or(view, false, -1.0f)
                   : layout_explicit_max_axis_or(view, false, -1.0f);
}

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

static bool intrinsic_svg_is_ratio_only(DomElement* element, ViewBlock* view) {
    if (!element || element->tag() != MARKUP_NAME_SVG) return false;
    Element* native_svg = dom_element_backing(lam::dom_require_element(element));
    SvgIntrinsicSize intrinsic = calculate_svg_intrinsic_size(native_svg);
    float preferred_ratio = layout_used_preferred_aspect_ratio(view);
    return !intrinsic.has_intrinsic_width && !intrinsic.has_intrinsic_height &&
        (intrinsic.has_intrinsic_aspect_ratio || preferred_ratio > 0.0f);
}

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
    // A lone intrinsic height has no natural ratio to scale through the HTML
    // 300px fallback width; preserve that height for min/max-content queries.
    bool has_intrinsic_ratio = has_viewbox || (has_width && has_height);
    if (has_intrinsic_ratio && constrained_width > 0.0f && constrained_width < size.width) {
        size.height = constrained_width * size.height / size.width;
        size.width = constrained_width;
    }
    return size;
}

static float intrinsic_replaced_max_height(LayoutContext* lycon, DomElement* element,
                                           ViewBlock* view) {
    if (!element) return -1.0f;

    CssDeclaration* declaration = layout_specified_physical_minmax_size_declaration(
        element, false, false);
    if (declaration && declaration->value) {
        const CssValue* value = declaration->value;
        if (value->type == CSS_VALUE_TYPE_LENGTH ||
            value->type == CSS_VALUE_TYPE_PERCENTAGE ||
            value->type == CSS_VALUE_TYPE_NUMBER) {
            float resolved = resolve_length_value(lycon, CSS_PROPERTY_MAX_HEIGHT, value);
            if (!isnan(resolved)) return resolved;
        }
    }
    return layout_explicit_max_axis_or(view, false, -1.0f);
}

static float intrinsic_replaced_width_with_max_height(LayoutContext* lycon,
                                                      DomElement* element,
                                                      ViewBlock* view,
                                                      float width,
                                                      float natural_width,
                                                      float natural_height) {
    if (width <= 0.0f || natural_width <= 0.0f || natural_height <= 0.0f) return width;

    float max_height = intrinsic_replaced_max_height(lycon, element, view);
    if (max_height < 0.0f) return width;

    // CSS Sizing transfers a definite max-height through a replaced object's
    // natural ratio before its intrinsic inline contribution is consumed.
    float ratio_limited_width = max_height * natural_width / natural_height;
    return ratio_limited_width < width ? ratio_limited_width : width;
}

static float intrinsic_image_height(ImageSurface* image, float constrained_width,
                                    bool allow_upscale) {
    float height = (float)image->height;
    if (constrained_width > 0.0f && image->width > 0 &&
        (allow_upscale || image->format == IMAGE_FORMAT_SVG || constrained_width < image->width)) {
        height = constrained_width * (float)image->height / (float)image->width;
    }
    return height;
}

static char* layout_first_srcset_candidate(const char* srcset) {
    if (!srcset) return nullptr;
    const char* cursor = srcset;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') {
            cursor++;
        }
        if (!*cursor) return nullptr;
        const char* end = cursor;
        while (*end && *end != ' ' && *end != '\t' && *end != '\r' &&
               *end != '\n' && *end != ',') {
            end++;
        }
        if (end > cursor) {
            size_t length = (size_t)(end - cursor);
            char* candidate = (char*)mem_alloc(length + 1, MEM_CAT_LAYOUT);
            if (!candidate) return nullptr;
            memcpy(candidate, cursor, length);
            candidate[length] = '\0';
            return candidate;
        }
        cursor = end;
    }
    return nullptr;
}

char* layout_resolve_replaced_image_source(DomElement* element) {
    if (!element || element->tag() != MARKUP_NAME_IMG) return nullptr;

    const char* src = element->get_attribute("src");
    if (src && src[0] != '\0') return mem_strdup(src, MEM_CAT_LAYOUT);

    DomNode* parent = element->parent;
    if (!parent || !parent->is_element() ||
        parent->as_element()->tag() != MARKUP_NAME_PICTURE) {
        return nullptr;
    }

    DomElement* picture = parent->as_element();
    for (DomNode* child = picture->first_child; child; child = child->next_sibling) {
        if (!child->is_element() || child->as_element()->tag() != MARKUP_NAME_SOURCE) continue;
        DomElement* source = child->as_element();
        const char* media = source->get_attribute("media");
        const char* type = source->get_attribute("type");
        // Media/type matching is not available in the layout resource loader;
        // leave conditional sources for a future media-capability pass.
        if ((media && media[0] != '\0') || (type && type[0] != '\0')) continue;
        char* candidate = layout_first_srcset_candidate(source->get_attribute("srcset"));
        if (candidate) {
            return candidate;
        }
    }
    return nullptr;
}

ImageSurface* layout_ensure_replaced_image_surface(LayoutContext* lycon,
                                                    ViewBlock* block,
                                                    DomElement* element) {
    if (!block || !element) return nullptr;
    NameId tag = element->tag();
    bool loadable = tag == MARKUP_NAME_IMG || tag == MARKUP_NAME_EMBED ||
        (tag == MARKUP_NAME_OBJECT && element->get_attribute(MARKUP_NAME_DATA));
    if (!loadable) return nullptr;
    if (block->embed && block->embedp()->img) return block->embedp()->img;
    const char* src_value = tag == MARKUP_NAME_IMG
        ? element->get_attribute("src") : element->get_attribute(MARKUP_NAME_DATA);
    char* selected_source = tag == MARKUP_NAME_IMG
        ? layout_resolve_replaced_image_source(element) : nullptr;
    if (selected_source) src_value = selected_source;
    if (!src_value || !lycon || !lycon->ui_context) {
        if (selected_source) mem_free(selected_source);
        return nullptr;
    }

    if (!block->embed) block->ensure_embed(lycon);
    size_t src_len = strlen(src_value);
    StrBuf* src_buf = strbuf_new_cap(src_len);
    strbuf_append_str_n(src_buf, src_value, src_len);
    block->embed->img = load_image(lycon->ui_context, src_buf->str);
    strbuf_free(src_buf);
    if (selected_source) mem_free(selected_source);
    return block->embedp()->img;
}

CssEnum get_element_font_variant(DomElement* element) {
    DomNode* node = element;
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->font && elem->fontp()->font_variant == CSS_VALUE_SMALL_CAPS) {
                return CSS_VALUE_SMALL_CAPS;
            }
        }
        node = node->parent;
    }
    return CSS_VALUE_NONE;
}

void layout_resolve_intrinsic_horizontal_margins(
        LayoutContext* lycon, DomElement* element, bool include_logical,
        float* margin_left, float* margin_right, bool include_shorthand) {
    if (!element || !element->specified_style || !margin_left || !margin_right) return;
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    StyleTree* style = element->specified_style;
    const Margin* resolved = view && view->bound ? &view->boundary()->margin : nullptr;
    if (resolved && resolved->left_type != CSS_VALUE__PERCENTAGE &&
        resolved->left_type != CSS_VALUE_AUTO) *margin_left = resolved->left;
    if (resolved && resolved->right_type != CSS_VALUE__PERCENTAGE &&
        resolved->right_type != CSS_VALUE_AUTO) *margin_right = resolved->right;
    auto resolve_decl = [&](CssPropertyCode property, const CssValue* value,
                            float* output) {
        if (value && value->type != CSS_VALUE_TYPE_PERCENTAGE) {
            *output = intrinsic_resolve_horizontal_margin_value(lycon, property, value);
        }
    };
    CssDeclaration* decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_LEFT);
    if (decl) resolve_decl(CSS_PROPERTY_MARGIN_LEFT, decl->value, margin_left);
    decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_RIGHT);
    if (decl) resolve_decl(CSS_PROPERTY_MARGIN_RIGHT, decl->value, margin_right);
    if (include_logical && *margin_left == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE_START);
        if (decl) resolve_decl(CSS_PROPERTY_MARGIN_INLINE_START, decl->value, margin_left);
    }
    if (include_logical && *margin_right == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE_END);
        if (decl) resolve_decl(CSS_PROPERTY_MARGIN_INLINE_END, decl->value, margin_right);
    }
    if (include_logical && *margin_left == 0.0f && *margin_right == 0.0f) {
        decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN_INLINE);
        if (decl) {
            resolve_decl(CSS_PROPERTY_MARGIN_INLINE, decl->value, margin_left);
            *margin_right = *margin_left;
        }
    }
    if (!include_shorthand || *margin_left != 0.0f || *margin_right != 0.0f) return;
    decl = style_tree_get_declaration(style, CSS_PROPERTY_MARGIN);
    if (!decl || !decl->value) return;
    resolve_decl(CSS_PROPERTY_MARGIN, css_box_shorthand_side_value(decl->value, 1), margin_right);
    resolve_decl(CSS_PROPERTY_MARGIN, css_box_shorthand_side_value(decl->value, 3), margin_left);
}

LayoutIntrinsicMarginPair layout_intrinsic_horizontal_margin_pair(
        LayoutContext* lycon, DomElement* element,
        LayoutIntrinsicMarginOptions options) {
    LayoutIntrinsicMarginPair result = {0.0f, 0.0f, false};
    if (!element) return result;

    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (options.include_bound && view && view->bound) {
        result.used_bound = true;
        const Margin& margin = view->boundary()->margin;
        if (margin.left_type != CSS_VALUE_AUTO &&
            (!options.nonnegative_bound || margin.left >= 0.0f)) {
            result.left = margin.left;
        }
        if (margin.right_type != CSS_VALUE_AUTO &&
            (!options.nonnegative_bound || margin.right >= 0.0f)) {
            result.right = margin.right;
        }
        if (!options.fallback_zero_bound ||
            result.left != 0.0f || result.right != 0.0f || !element->specified_style) {
            return result;
        }
        result.used_bound = false;
    }

    if (element->specified_style) {
        layout_resolve_intrinsic_horizontal_margins(
            lycon, element, options.include_logical,
            &result.left, &result.right, options.include_shorthand);
    }
    return result;
}

static float intrinsic_inline_edge(LayoutContext* lycon, DomElement* element,
                                   bool inline_start, bool include_margin = true) {
    if (!lycon || !element) return 0.0f;
    float margin_left = 0.0f, margin_right = 0.0f;
    layout_resolve_intrinsic_horizontal_margins(
        lycon, element, false, &margin_left, &margin_right);
    IntrinsicHorizontalBoxEdges edges =
        intrinsic_horizontal_box_edges(lycon, element, false);
    float margin = inline_start ? margin_left : margin_right;
    float decoration = inline_start
        ? edges.border_start + edges.padding_start
        : edges.border_end + edges.padding_end;
    return (include_margin ? margin : 0.0f) + decoration;
}

static float intrinsic_inline_start_margin(LayoutContext* lycon, DomElement* element) {
    return intrinsic_inline_edge(lycon, element, true) -
        intrinsic_inline_edge(lycon, element, true, false);
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

static float intrinsic_store_ratio_width_from_height(
        LayoutContext* lycon, DomElement* element, ViewBlock* view,
        float aspect_ratio, float height, bool minimum_height,
        bool height_is_border_box, bool is_scroll_container,
        IntrinsicSizes* sizes) {
    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(view) &&
        layout_uses_border_box(view);
    float ratio_height = height;
    if (ratio_uses_border_box && !height_is_border_box) {
        ratio_height = layout_css_size_to_border_box(
            view->bound, layout_box_sizing(view), ratio_height, false);
    } else if (!ratio_uses_border_box && height_is_border_box &&
               layout_aspect_ratio_uses_content_box(view)) {
        ratio_height = layout_content_size_from_border_box(view, ratio_height, false);
    }
    float aspect_width = layout_apply_min_max_axis(
        view, ratio_height * aspect_ratio, true, layout_uses_border_box(view));
    bool width_is_border_box = ratio_uses_border_box ||
        (!layout_aspect_ratio_uses_content_box(view) &&
         intrinsic_view_uses_border_box(view, element));
    float padding_border = layout_intrinsic_padding_border_axis(
        lycon, element, true, lycon->block.content_width);
    float aspect_border_width = width_is_border_box
        ? aspect_width : aspect_width + padding_border;
    if (is_scroll_container) {
        float explicit_min = layout_explicit_min_axis_or(view, true, -1.0f);
        sizes->min_content = explicit_min >= 0.0f
            ? layout_css_size_to_border_box(view->bound, layout_box_sizing(view), explicit_min, true)
            : 0.0f;
    } else {
        sizes->min_content = aspect_border_width;
    }
    sizes->max_content = is_scroll_container || minimum_height
        ? aspect_border_width : sizes->min_content;
    return aspect_width;
}

IntrinsicSizes measure_element_intrinsic_widths(LayoutContext* lycon, DomElement* element,
                                                bool content_only) {
    IntrinsicSizes sizes = {0, 0};

    if (!element) return sizes;
    if (!content_only && !intrinsic_percentage_width_is_indefinite(lycon) &&
        element->styles_resolved() && element->has_cached_intrinsic_widths()) {
        assert(element->layout_cache);
        return {element->layout_cache->intrinsic_min_content_width,
                element->layout_cache->intrinsic_max_content_width};
    }

    if (element->measuring_intrinsic_width()) {
        return {0, 0};
    }
    IntrinsicMeasureScope measure_scope(lycon, element);

    auto t_measure_start = std::chrono::high_resolution_clock::now();

    // CRITICAL FIX: Set up font context for this element BEFORE measuring text children
    // This ensures text measurement uses the element's own font (e.g., monospace for <code>)
    // rather than inheriting from parent context.
    FontBox saved_font = lycon->font;  // Save parent font context
    IntrinsicFontScope temp_font_guard(lycon, saved_font);
    bool font_changed = false;
    ViewBlock* view_block_font = lam::unsafe_view_block_element_storage(element);
    NameId intrinsic_tag = element->tag();
    // CSS Sizing 3: a figure's UA margins are used box edges, so resolve them
    // before an anonymous table cell records the element's contribution.
    bool intrinsic_needs_resolved_style =
        intrinsic_tag == MARKUP_NAME_BUTTON || intrinsic_tag == MARKUP_NAME_INPUT ||
        intrinsic_tag == MARKUP_NAME_UL || intrinsic_tag == MARKUP_NAME_OL ||
        intrinsic_tag == MARKUP_NAME_MENU || intrinsic_tag == MARKUP_NAME_RUBY ||
        intrinsic_tag == MARKUP_NAME_RT || intrinsic_tag == MARKUP_NAME_FIGURE;
    bool intrinsic_needs_multicol_style = element->specified_style &&
        (style_tree_get_declaration(element->specified_style, CSS_PROPERTY_COLUMNS) ||
         style_tree_get_declaration(element->specified_style, CSS_PROPERTY_COLUMN_COUNT) ||
         style_tree_get_declaration(element->specified_style, CSS_PROPERTY_COLUMN_WIDTH));
    intrinsic_needs_resolved_style = intrinsic_needs_resolved_style ||
        intrinsic_needs_multicol_style;
    if (intrinsic_needs_resolved_style &&
        (!element->styles_resolved() || intrinsic_needs_multicol_style)) {
        // Intrinsic contributions need used sizing declarations before measuring
        // descendants; multicol declarations otherwise leave intrinsic width with
        // no column structure even though the final layout is fragmented.
        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
        dom_node_resolve_style(element, lycon);
    }
    bool stale_table_fixup_font = !element->styles_resolved() &&
        intrinsic_element_inside_table_fixup(element);
    // A retained table box can still carry reset-time font data while its
    // effective inherited font is available in the intrinsic context.
    if (view_block_font->font && !stale_table_fixup_font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view_block_font->font);
        font_changed = true;
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        FontProp* temp_font_prop = alloc_font_prop(lycon);  // Allocates from pool
        temp_font_guard.prop_a = temp_font_prop;
        bool need_font_setup = false;
        bool spacing_font_ready = false;
        const char* css_family = NULL;

        if (intrinsic_tag == MARKUP_NAME_TABLE && lycon->doc && lycon->doc->view_tree &&
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

        CssDeclaration* font_shorthand_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT);
        if (font_shorthand_decl && font_shorthand_decl->value) {
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
                }
            } else {
                LayoutFontShorthandParts parts;
                if (layout_parse_font_shorthand(fv, &parts)) {
                    bool resolved_from_medium = false;
                    float sz = layout_resolve_font_size(
                        lycon, parts.size, lycon->font.style, false,
                        &resolved_from_medium);
                    if (sz >= 0.0f) {
                        temp_font_prop->font_size = sz;
                        temp_font_prop->font_size_from_medium = resolved_from_medium;
                    }
                    css_family = css_select_font_shorthand_family(
                        lycon, fv, parts.group, parts.family_start, false);
                    if (parts.weight) {
                        temp_font_prop->font_weight = map_font_weight(parts.weight);
                        temp_font_prop->font_weight_numeric = map_font_weight_numeric(parts.weight);
                    }
                    if (parts.style) temp_font_prop->font_style = parts.style->data.keyword;
                    if (parts.small_caps) temp_font_prop->font_variant = CSS_VALUE_SMALL_CAPS;
                    need_font_setup = true;
                }
            }
            if (css_family) {
                radiant_retain_font_family(temp_font_prop, lam::PoolPtr<char>((char*)css_family));
            }
        }

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

        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value &&
            (!font_shorthand_decl || font_size_decl->source_order > font_shorthand_decl->source_order)) {
            float resolved_size = -1.0f;
            bool resolved_from_medium = false;
            resolved_size = layout_resolve_font_size(
                lycon, font_size_decl->value, lycon->font.style, false,
                &resolved_from_medium);
            if (resolved_size >= 0.0f) {
                if (need_font_setup || fabs(resolved_size - lycon->font.style->font_size) > 0.1f) {
                    temp_font_prop->font_size = resolved_size;
                    temp_font_prop->font_size_from_medium = resolved_from_medium;
                    need_font_setup = true;
                }
            }
        }

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
            intrinsic_complete_inherited_font(
                temp_font_prop, saved_font.style,
                intrinsic_element_has_specified_font_size(element));
            intrinsic_apply_monospace_font_size_quirk(temp_font_prop, saved_font.style);
            setup_font(lycon->ui_context, &lycon->font, temp_font_prop);
            spacing_font_ready = true;
            need_font_setup = true;
        }
        if (letter_spacing_decl && letter_spacing_decl->value) {
            const CssValue* ls_val = letter_spacing_decl->value;
            if (ls_val->type == CSS_VALUE_TYPE_LENGTH ||
                ls_val->type == CSS_VALUE_TYPE_PERCENTAGE ||
                ls_val->type == CSS_VALUE_TYPE_FUNCTION) {
                temp_font_prop->letter_spacing =
                    resolve_length_value(lycon, CSS_PROPERTY_LETTER_SPACING, ls_val);
                temp_font_prop->letter_spacing_is_percent = ls_val->type == CSS_VALUE_TYPE_PERCENTAGE;
                temp_font_prop->letter_spacing_percent = temp_font_prop->letter_spacing_is_percent
                    ? (float)ls_val->data.percentage.value : 0.0f;
                need_font_setup = true;
            } else if (ls_val->type == CSS_VALUE_TYPE_KEYWORD &&
                       ls_val->data.keyword == CSS_VALUE_NORMAL) {
                temp_font_prop->letter_spacing = 0.0f;
                temp_font_prop->letter_spacing_is_percent = false;
                temp_font_prop->letter_spacing_percent = 0.0f;
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
            temp_font_prop->word_spacing_percent = lycon->font.style->word_spacing_percent;
            temp_font_prop->word_spacing_is_percent = lycon->font.style->word_spacing_is_percent;
        }

        if (need_font_setup) {
            if (!spacing_font_ready) {
                intrinsic_apply_ua_font_defaults(element, temp_font_prop, saved_font.style);
                intrinsic_complete_inherited_font(
                    temp_font_prop, saved_font.style,
                    intrinsic_element_has_specified_font_size(element));
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
            intrinsic_complete_inherited_font(
                ua_font, lycon->font.style,
                intrinsic_element_has_specified_font_size(element));
            intrinsic_apply_monospace_font_size_quirk(ua_font, lycon->font.style);
            setup_font(lycon->ui_context, &lycon->font, ua_font);
            font_changed = true;
        }
    }

    if (element->specified_style) {
        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
        (lam::unsafe_view_block_element_storage(element))->display = resolve_display_value((void*)element);
    }

    {
        ViewBlock* check_none = lam::unsafe_view_block_element_storage(element);
        if (layout_block_is_display_none(check_none)) {
            return sizes;
        }
    }

    bool is_table_display = false;
    {
        if (intrinsic_element_display_matches(
                element, CSS_VALUE_TABLE, CSS_VALUE_INLINE_TABLE)) {
            is_table_display = true;
        }
        if (!is_table_display && element->tag() == MARKUP_NAME_TABLE) is_table_display = true;
    }

    bool is_inline_non_replaced = false;
    {
        DisplayValue resolved_display = resolve_display_value((void*)element);
        if (resolved_display.outer == CSS_VALUE_INLINE && resolved_display.inner == CSS_VALUE_FLOW) {
            is_inline_non_replaced = true;
        }
    }
    ViewBlock* resolved_width_view = lam::unsafe_view_block_element_storage(element);
    bool physical_width_is_vertical_block_axis =
        layout_block_inline_axis_is_vertical(resolved_width_view);
    bool has_intrinsic_min_width = layout_intrinsic_min_size_keyword(
        resolved_width_view, true) != CSS_VALUE__UNDEF;
    bool has_intrinsic_preferred_width = layout_intrinsic_preferred_size_keyword(
        resolved_width_view, true) != CSS_VALUE__UNDEF;
    bool has_intrinsic_max_width = layout_intrinsic_max_size_keyword(
        resolved_width_view, true) != CSS_VALUE__UNDEF;
    bool has_definite_width = resolved_width_view->blk &&
        resolved_width_view->block()->given_width >= 0.0f &&
        resolved_width_view->block()->given_width_type != CSS_VALUE_AUTO &&
        resolved_width_view->block()->given_width_type != CSS_VALUE__UNDEF &&
        !has_intrinsic_preferred_width;
    ViewBlock* intrinsic_parent = element->parent && element->parent->is_element()
        ? lam::view_as_block(element->parent->as_element()) : nullptr;
    bool parent_has_intrinsic_width = intrinsic_parent && intrinsic_parent->blk &&
        (intrinsic_parent->block()->given_width_type == CSS_VALUE_MIN_CONTENT ||
         intrinsic_parent->block()->given_width_type == CSS_VALUE_MAX_CONTENT);
    bool parent_has_auto_inline_width = intrinsic_parent &&
        (!intrinsic_parent->blk ||
         ((intrinsic_parent->block()->given_width_type == CSS_VALUE_AUTO ||
           intrinsic_parent->block()->given_width_type == CSS_VALUE__UNDEF) &&
          isnan(intrinsic_parent->block()->given_width_percent))) &&
        (intrinsic_parent->display.outer == CSS_VALUE_INLINE_BLOCK ||
         intrinsic_parent->display.outer == CSS_VALUE_INLINE_FLEX ||
         intrinsic_parent->display.outer == CSS_VALUE_INLINE_GRID ||
         intrinsic_parent->display.outer == CSS_VALUE_INLINE_TABLE ||
         layout_element_is_floated(intrinsic_parent->as_element()));
    // Percentage widths resolve as auto during intrinsic sizing; their temporary
    // zero base must not become a definite width in the measurement cache. An
    // auto-sized inline-level or shrink-to-fit parent is also an indefinite
    // containing block; resolving against its provisional width creates a
    // circular contribution.
    CssDeclaration* width_declaration = layout_specified_physical_size_declaration(element, true);
    CssDeclaration* max_width_declaration =
        layout_specified_physical_minmax_size_declaration(element, true, false);
    CssDeclaration* min_width_declaration =
        layout_specified_physical_minmax_size_declaration(element, true, true);
    bool authored_definite_width = width_declaration && width_declaration->value &&
        (width_declaration->value->type == CSS_VALUE_TYPE_LENGTH ||
         (width_declaration->value->type == CSS_VALUE_TYPE_NUMBER &&
          width_declaration->value->data.number.value == 0.0));
    has_definite_width = has_definite_width || authored_definite_width;
    bool width_is_percentage = resolved_width_view->blk &&
        !isnan(resolved_width_view->block()->given_width_percent);
    if (!width_is_percentage && width_declaration && width_declaration->value) {
        width_is_percentage = layout_css_value_has_percentage(width_declaration->value);
    }
    bool max_width_is_percentage = resolved_width_view->blk &&
        !isnan(resolved_width_view->block()->given_max_width_percent);
    if (!max_width_is_percentage && max_width_declaration && max_width_declaration->value) {
        max_width_is_percentage = layout_css_value_has_percentage(max_width_declaration->value);
    }
    bool percentage_size_resolves_to_zero = lycon->available_space.width.is_definite() &&
        lycon->available_space.width.value <= 0.0f &&
        (width_is_percentage || max_width_is_percentage) &&
        !parent_has_auto_inline_width;
    bool percentage_width_is_intrinsic_auto = width_is_percentage &&
        (lycon->available_space.width.is_intrinsic() || parent_has_intrinsic_width ||
         parent_has_auto_inline_width);
    bool percentage_size_is_intrinsic_auto =
        (width_is_percentage || max_width_is_percentage) &&
        (lycon->available_space.width.is_intrinsic() || parent_has_intrinsic_width ||
         parent_has_auto_inline_width);
    bool percentage_replaced_size_is_intrinsic_auto = max_width_is_percentage &&
        layout_element_is_replaced(element) &&
        ((has_definite_width &&
          (lycon->available_space.width.is_intrinsic() || parent_has_intrinsic_width ||
           parent_has_auto_inline_width)) ||
         percentage_size_resolves_to_zero);
    // CSS Sizing 3 §5.2.1: a replaced max percentage contributes zero to
    // min-content in an indefinite containing block; otherwise its natural size
    // inflates the shrink-to-fit parent before the percentage can resolve.
    bool percentage_width_min_content_zero =
        (percentage_width_is_intrinsic_auto || percentage_replaced_size_is_intrinsic_auto) &&
        !layout_block_inline_axis_is_vertical(resolved_width_view);
    bool percentage_min_width_intrinsic_zero = min_width_declaration &&
        min_width_declaration->value &&
        layout_css_value_has_nonzero_percentage(min_width_declaration->value) &&
        (lycon->available_space.width.is_intrinsic() || parent_has_intrinsic_width ||
         parent_has_auto_inline_width) &&
        (!layout_block_inline_axis_is_vertical(resolved_width_view) ||
         parent_has_auto_inline_width);
    auto intrinsic_apply_definite_width_constraints = [&](float border_width) {
        float min_border = -1.0f;
        if (!percentage_min_width_intrinsic_zero) {
            float min_width = resolved_width_view->blk
                ? resolved_width_view->block()->given_min_width : -1.0f;
            if (min_width < 0.0f && min_width_declaration && min_width_declaration->value) {
                min_width = intrinsic_resolve_definite_constraint(
                    lycon, CSS_PROPERTY_MIN_WIDTH, min_width_declaration->value);
            }
            if (min_width >= 0.0f) {
                float pad_border = layout_intrinsic_padding_border_axis(
                    lycon, element, true, lycon->block.content_width);
                min_border = intrinsic_view_uses_border_box(resolved_width_view, element)
                    ? min_width : min_width + pad_border;
                min_border = layout_floor_border_box_axis(resolved_width_view, min_border, true);
            }
        }

        float max_border = -1.0f;
        if (!max_width_is_percentage) {
            // A definite max-width constrains a definite intrinsic width; percentage
            // components are deferred because their containing block is not definite.
            float max_width = resolved_width_view->blk
                ? resolved_width_view->block()->given_max_width : -1.0f;
            if (max_width < 0.0f && max_width_declaration && max_width_declaration->value) {
                max_width = intrinsic_resolve_definite_constraint(
                    lycon, CSS_PROPERTY_MAX_WIDTH, max_width_declaration->value);
            }
            if (max_width >= 0.0f) {
                float pad_border = layout_intrinsic_padding_border_axis(
                    lycon, element, true, lycon->block.content_width);
                max_border = intrinsic_view_uses_border_box(resolved_width_view, element)
                    ? max_width : max_width + pad_border;
                max_border = layout_floor_border_box_axis(resolved_width_view, max_border, true);
            }
        }

        // A larger minimum overrides max-width; otherwise apply both to the
        // explicit intrinsic contribution before returning from the shortcut.
        if (max_border >= 0.0f && (min_border < 0.0f || min_border <= max_border)) {
            border_width = min(border_width, max_border);
        }
        if (min_border >= 0.0f) border_width = max(border_width, min_border);
        return border_width;
    };
    float contain_intrinsic_width = -1.0f;
    float contain_intrinsic_height = -1.0f;
    layout_resolve_contain_intrinsic_size(lycon, element, &contain_intrinsic_width,
                                          &contain_intrinsic_height);
    bool contain_intrinsic_width_auto = resolved_width_view->blk &&
        resolved_width_view->block()->contain_intrinsic_width_auto;
    if (resolved_width_view->blk &&
        resolved_width_view->block()->content_visibility_hidden &&
        contain_intrinsic_width_auto && element->has_last_remembered_width() &&
        !has_definite_width) {
        float remembered_width = element->last_remembered_width();
        float padding_border = layout_intrinsic_padding_border_axis(
            lycon, element, true, lycon->block.content_width);
        float intrinsic_width = remembered_width;
        // The remembered fragment width must replace hidden content's missing
        // max-content contribution before the containing multicol is sized.
        return {intrinsic_width + padding_border, intrinsic_width + padding_border};
    }
    if (contain_intrinsic_width >= 0.0f && !contain_intrinsic_width_auto &&
        !has_definite_width) {
        float padding_border = layout_intrinsic_padding_border_axis(
            lycon, element, true, lycon->block.content_width);
        float intrinsic_width = contain_intrinsic_width + padding_border;
        // Size containment replaces descendant contributions with this synthetic box
        // in every intrinsic-width query, including flex and grid measurements.
        return {intrinsic_width, intrinsic_width};
    }
    bool has_empty_size_containment =
        layout_block_has_size_containment_in_axis(resolved_width_view, true) &&
        !contain_intrinsic_width_auto &&
        contain_intrinsic_width < 0.0f && !has_definite_width &&
        (resolved_width_view->display.inner != CSS_VALUE_GRID ||
         resolved_width_view->block()->content_visibility_hidden) &&
        (!resolved_width_view->form ||
         resolved_width_view->block()->content_visibility_hidden);
    if (has_empty_size_containment) {
        float padding_border = layout_intrinsic_padding_border_axis(
            lycon, element, true, lycon->block.content_width);
        float intrinsic_width = layout_block_empty_content_size_in_axis(
            resolved_width_view, true) + padding_border;
        // An omitted fallback removes descendants from intrinsic sizing, while
        // preserving the box's own padding, borders, and fixed multicol tracks.
        return {intrinsic_width, intrinsic_width};
    }
    bool can_use_definite_width = !content_only && !has_intrinsic_min_width &&
        !has_intrinsic_preferred_width && !has_intrinsic_max_width &&
        !(percentage_width_is_intrinsic_auto || percentage_replaced_size_is_intrinsic_auto) &&
        !percentage_size_resolves_to_zero &&
        !is_table_display && !is_inline_non_replaced &&
        !physical_width_is_vertical_block_axis;
    if (can_use_definite_width) {
        float explicit_width = -1.0f;
        bool width_is_from_resolved_style = resolved_width_view->blk &&
            resolved_width_view->block()->given_width >= 0.0f;
        if (width_is_from_resolved_style) {
            explicit_width = resolved_width_view->block()->given_width;
        } else if (element->specified_style) {
            // intrinsic sizing must honor the logical inline-size that normal
            // layout maps to this physical axis; reading width alone drops it.
            CssDeclaration* width_decl = layout_specified_physical_size_declaration(
                element, true);
            bool supported_width = width_decl && width_decl->value &&
                (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
                 width_decl->value->type == CSS_VALUE_TYPE_FUNCTION ||
                 (width_decl->value->type == CSS_VALUE_TYPE_NUMBER &&
                  width_decl->value->data.number.value == 0));
            if (supported_width) {
                explicit_width = resolve_length_value(
                    lycon, width_decl->property_code, width_decl->value);
            }
        }
        if (explicit_width >= 0.0f) {
            ViewBlock* view_for_box = lam::unsafe_view_block_element_storage(element);
            bool is_border_box = width_is_from_resolved_style
                ? layout_uses_border_box(resolved_width_view)
                : intrinsic_view_uses_border_box(view_for_box, element);
            float pad_border_width = layout_intrinsic_padding_border_axis(
                lycon, element, true, lycon->block.content_width);
            if (!is_border_box) {
                explicit_width += pad_border_width;
            } else if (explicit_width < pad_border_width) {
                // border-box widths cannot make the content box negative, including before bounds exist.
                explicit_width = pad_border_width;
            }
            explicit_width = intrinsic_apply_definite_width_constraints(explicit_width);
            sizes.min_content = explicit_width;
            sizes.max_content = explicit_width;
            return sizes;
        }
    }

    // Check for aspect-ratio with explicit height or resolvable percentage height
    // If element has height and aspect-ratio, width = height * aspect-ratio
    ViewBlock* view_block_for_aspect = lam::unsafe_view_block_element_storage(element);
    // Load an image before resolving `auto <ratio>` so intrinsic contributions
    // use its natural ratio rather than the declaration's fallback ratio.
    if (layout_aspect_ratio_uses_content_box(view_block_for_aspect)) {
        layout_ensure_replaced_image_surface(lycon, view_block_for_aspect, element);
    }
    float aspect_ratio = layout_used_preferred_aspect_ratio(view_block_for_aspect);
    if (aspect_ratio <= 0.0f && element->tag() == MARKUP_NAME_IMG) {
        ImageSurface* image = layout_ensure_replaced_image_surface(
            lycon, view_block_for_aspect, element);
        if (image && image->width > 0 && image->height > 0) {
            // An intrinsic preferred width stays symbolic until a definite height
            // can transfer through the image's natural ratio.
            aspect_ratio = (float)image->width / (float)image->height;
        }
    }
    if (aspect_ratio <= 0.0f && element->tag() == MARKUP_NAME_CANVAS) {
        float natural_width = 0.0f;
        float natural_height = 0.0f;
        if (layout_canvas_natural_size(view_block_for_aspect, &natural_width, &natural_height) &&
            natural_width > 0.0f && natural_height > 0.0f) {
            aspect_ratio = natural_width / natural_height;
        }
    }
    float aspect_ratio_min_width = 0.0f;
    bool aspect_ratio_width_uses_border_box = false;
    float aspect_ratio_max_width = 0.0f;
    bool aspect_ratio_max_width_uses_border_box = false;
    bool aspect_ratio_width_is_intrinsic_keyword = false;

    if (aspect_ratio > 0) {
        float height = -1;
        ViewBlock* aspect_parent = element->parent && element->parent->is_element()
            ? lam::view_as_block(element->parent->as_element()) : nullptr;
        bool parent_has_intrinsic_inline_size = aspect_parent && aspect_parent->blk &&
            (aspect_parent->block()->given_width_type == CSS_VALUE_FIT_CONTENT ||
             aspect_parent->block()->given_width_type == CSS_VALUE_MIN_CONTENT ||
             aspect_parent->block()->given_width_type == CSS_VALUE_MAX_CONTENT);
        bool parent_has_abspos_auto_inline_size = aspect_parent &&
            layout_view_is_abs_or_fixed(aspect_parent) && aspect_parent->blk &&
            aspect_parent->block()->given_width < 0.0f &&
            !(aspect_parent->position && aspect_parent->positionp()->has_left &&
              aspect_parent->positionp()->has_right);
        CssDeclaration* aspect_height_decl = layout_specified_physical_size_declaration(
            element, false);
        bool declared_percentage_height = aspect_height_decl && aspect_height_decl->value &&
            aspect_height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE;
        bool percentage_height_is_intrinsic_auto =
            layout_element_is_replaced(element) &&
            element->tag() != MARKUP_NAME_CANVAS &&
            (declared_percentage_height ||
             (view_block_for_aspect->blk &&
              !isnan(view_block_for_aspect->block()->given_height_percent))) &&
            (lycon->available_space.width.is_intrinsic() || parent_has_intrinsic_inline_size ||
             parent_has_abspos_auto_inline_size);
        // percentage height becomes intrinsic-auto only for replaced contributions;
        // applying it to non-replaced aspect-ratio boxes breaks definite grid sizing.
        // canvas keeps its attribute aspect ratio; the generic intrinsic-auto rule
        // loses that ratio when its definite percentage height is transferred.

        // Check for explicit height from CSS
        if (!percentage_height_is_intrinsic_auto &&
            !layout_css_size_is_automatic(view_block_for_aspect, false) &&
            view_block_for_aspect->blk &&
            view_block_for_aspect->block_mut()->given_height > 0) {
            // Ignore a provisional ratio height during intrinsic measurement;
            // only an authored definite height may seed the width contribution.
            height = view_block_for_aspect->block()->given_height;
        }

        if (element->tag() == MARKUP_NAME_CANVAS &&
            layout_css_size_is_automatic(view_block_for_aspect, false)) {
            // Canvas attributes are natural dimensions and cannot make an intrinsic CSS height definite.
            height = -1.0f;
        }
        if (element->tag() == MARKUP_NAME_CANVAS && view_block_for_aspect->blk &&
            view_block_for_aspect->block()->content_visibility_hidden &&
            view_block_for_aspect->block()->contain_intrinsic_height_auto) {
            // Last-remembered canvas height must not reuse the provisional max-content
            // height produced by the hidden pass; the bitmap's natural ratio is the memory source.
            height = -1.0f;
        }

        // Intrinsic sizing may run before normal layout creates BlockProp, so
        // resolve a specified definite height before transferring it by ratio.
        if (height <= 0 && element->specified_style) {
            CssDeclaration* height_decl = layout_specified_physical_size_declaration(
                element, false);
            if (height_decl && height_decl->value &&
                height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT,
                                              height_decl->value);
            } else if (!percentage_height_is_intrinsic_auto && height_decl && height_decl->value &&
                       height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float percentage = (float)height_decl->value->data.percentage.value;
                float parent_height = intrinsic_parent_definite_height(lycon);
                if (parent_height > 0) {
                    height = parent_height * percentage / 100.0f;
                }
            }
        }

        bool height_has_intrinsic_constraint =
            layout_intrinsic_min_size_keyword(view_block_for_aspect, false) != CSS_VALUE__UNDEF ||
            layout_intrinsic_max_size_keyword(view_block_for_aspect, false) != CSS_VALUE__UNDEF;
        if (height_has_intrinsic_constraint) {
            bool width_is_intrinsic_keyword =
                layout_intrinsic_preferred_size_keyword(view_block_for_aspect, true) != CSS_VALUE__UNDEF;
            if (width_is_intrinsic_keyword && height > 0.0f) {
                bool height_constraint_is_intrinsic_keyword =
                    layout_intrinsic_min_size_keyword(view_block_for_aspect, false) != CSS_VALUE__UNDEF ||
                    layout_intrinsic_max_size_keyword(view_block_for_aspect, false) != CSS_VALUE__UNDEF;
                if (height_constraint_is_intrinsic_keyword) {
                    // An intrinsic min/max-height must derive from the natural
                    // contribution; the authored height is only a provisional
                    // ratio input and would make the intrinsic result circular.
                    height = -1.0f;
                } else {
                    // A numeric min/max-height constrains the authored height
                    // before it transfers through the preferred ratio.
                    height = layout_apply_min_max_axis(
                        view_block_for_aspect, height, false, layout_uses_border_box(view_block_for_aspect));
                }
            } else {
                // Intrinsic height constraints resolve before other ratio transfer;
                // using an unconstrained specified height would create a circular
                // width contribution.
                height = -1.0f;
            }
        }

        bool is_scroll_container = !layout_element_is_replaced(element) &&
            view_block_for_aspect->scroller &&
            view_block_for_aspect->scroll()->overflow_x != CSS_VALUE_VISIBLE;
        if (height > 0) {
            aspect_ratio_width_uses_border_box =
                !layout_aspect_ratio_uses_content_box(view_block_for_aspect) &&
                layout_uses_border_box(view_block_for_aspect);
            float ratio_height = layout_apply_min_max_axis(
                view_block_for_aspect, height, false, layout_uses_border_box(view_block_for_aspect));
            float resolved_min_height = intrinsic_resolved_height_limit_or(
                lycon, element, view_block_for_aspect, true);
            float resolved_max_height = intrinsic_resolved_height_limit_or(
                lycon, element, view_block_for_aspect, false);
            if (resolved_min_height >= 0.0f && ratio_height < resolved_min_height) {
                ratio_height = resolved_min_height;
            }
            if (resolved_max_height >= 0.0f && ratio_height > resolved_max_height &&
                (resolved_min_height < 0.0f || resolved_min_height <= resolved_max_height)) {
                ratio_height = resolved_max_height;
            }
            // Ratio transfer uses the constrained used height; transferring the
            // raw specified height ignores min/max-height before width:min-content.
            if (layout_aspect_ratio_uses_content_box(view_block_for_aspect) &&
                intrinsic_view_uses_border_box(view_block_for_aspect, element)) {
                // `auto <ratio>` measures the content box; remove border-box
                // padding before transferring a definite height to the width.
                float pad_border_height = layout_intrinsic_padding_border_axis(
                    lycon, element, false, lycon->block.content_width);
                ratio_height = max(ratio_height - pad_border_height, 0.0f);
            }
            float aspect_width = intrinsic_store_ratio_width_from_height(
                lycon, element, view_block_for_aspect, aspect_ratio, ratio_height,
                false, layout_uses_border_box(view_block_for_aspect),
                is_scroll_container, &sizes);
            if (!is_scroll_container && !layout_element_is_replaced(element) &&
                element_has_in_flow_intrinsic_content(element)) {
                // A non-replaced preferred ratio is an intrinsic-width floor;
                // in-flow content must still contribute before flex auto-min sizing.
                aspect_ratio_min_width = aspect_width;
                sizes = {0.0f, 0.0f};
            } else {
                return sizes;
            }
        } else {
            float min_height = intrinsic_resolved_height_limit_or(
                lycon, element, view_block_for_aspect, true);
            if (min_height >= 0.0f) {
                // An indefinite percentage height is auto for intrinsic sizing,
                // but its definite min-height still transfers through the ratio.
                float aspect_width = intrinsic_store_ratio_width_from_height(
                    lycon, element, view_block_for_aspect, aspect_ratio, min_height,
                    true, false, is_scroll_container, &sizes);
                if (!is_scroll_container && !layout_element_is_replaced(element) &&
                    element_has_in_flow_intrinsic_content(element)) {
                    aspect_ratio_min_width = aspect_width;
                    sizes = {0.0f, 0.0f};
                } else {
                    return sizes;
                }
            }
            if (min_height < 0.0f) {
                float max_height = intrinsic_resolved_height_limit_or(
                    lycon, element, view_block_for_aspect, false);
                if (max_height >= 0.0f) {
                    aspect_ratio_width_is_intrinsic_keyword =
                        layout_intrinsic_preferred_size_keyword(
                            view_block_for_aspect, true) != CSS_VALUE__UNDEF;
                    bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(
                        view_block_for_aspect) && layout_uses_border_box(view_block_for_aspect);
                    float ratio_height = ratio_uses_border_box
                        ? layout_css_size_to_border_box(view_block_for_aspect->bound,
                            layout_box_sizing(view_block_for_aspect), max_height, false)
                        : max_height;
                    float max_width = layout_apply_min_max_axis(
                        view_block_for_aspect, ratio_height * aspect_ratio, true,
                        layout_uses_border_box(view_block_for_aspect));
                    aspect_ratio_max_width = max_width;
                    aspect_ratio_max_width_uses_border_box =
                        layout_uses_border_box(view_block_for_aspect);
                }
            }
        }
    }

    // CSS 2.1 §10.3.2/§10.6.2: Replaced element intrinsic sizing
    // For replaced elements (img, video, iframe, etc.), use intrinsic dimensions
    // when no explicit CSS width is set.
    // Note: we set sizes but do NOT return early — we fall through to the
    // padding/border section at the bottom so padding+border are correctly included.
    bool replaced_intrinsic_set = false;
    ViewBlock* view_block_replaced = lam::unsafe_view_block_element_storage(element);
    // Detection also uses HTML tag semantics because early intrinsic passes may
    // run before display and form-control state are fully resolved.
    NameId replaced_tag = element->tag();
    bool is_replaced_element = layout_element_is_replaced(element);
    if (is_replaced_element) {
        float replaced_width = -1;

        if (replaced_tag == MARKUP_NAME_IMG) {
            ImageSurface* image = layout_ensure_replaced_image_surface(
                lycon, view_block_replaced, element);
            if (image) {
                replaced_width = (float)image->width;
            }
            // CSS 2.1 §10.3.2: HTML width attribute is a presentational hint that
            // overrides intrinsic dimensions. When <img width="128"> has a natural
            // file width of 256, the used width is 128 (the specified value).
            // Check resolved blk->given_width first, then HTML attribute directly
            // (styles may not be resolved yet during early intrinsic sizing).
            {
                float specified_width = -1;
                if (view_block_replaced->blk && view_block_replaced->block_mut()->given_width >= 0) {
                    specified_width = view_block_replaced->block()->given_width;
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
                    replaced_width = specified_width;
                } else if (specified_width >= 0 && replaced_width < 0) {
                    // image not loaded yet — use specified width as placeholder
                    replaced_width = specified_width;
                }
            }
            if (image) {
                replaced_width = intrinsic_replaced_width_with_max_height(
                    lycon, element, view_block_replaced, replaced_width,
                    (float)image->width, (float)image->height);
            }
            // Fallback for broken/unloadable images — use small icon size.
            // CSS Images 3: broken images have no intrinsic dimensions.
            // Browsers ignore HTML width/height attributes for broken images
            // and render a small broken image icon with alt text instead.
            if (replaced_width < 0) {
                replaced_width = 0;
            }
        }
        else if (replaced_tag == MARKUP_NAME_IFRAME) {
            replaced_width = 300;
        }
        else if (replaced_tag == MARKUP_NAME_VIDEO || replaced_tag == MARKUP_NAME_CANVAS) {
            // try actual video dimensions first
            if (replaced_tag == MARKUP_NAME_VIDEO && view_block_replaced->embed && view_block_replaced->embedp()->video) {
                int vw = rdt_video_get_width(view_block_replaced->embedp()->video);
                if (vw > 0) {
                    replaced_width = (float)vw;
                } else {
                    replaced_width = 300;
                }
            } else if (replaced_tag == MARKUP_NAME_CANVAS) {
                float natural_width = 0.0f;
                float natural_height = 0.0f;
                // Canvas bitmap attributes are its intrinsic contribution, not a CSS 300px fallback.
                if (layout_canvas_natural_size(view_block_replaced, &natural_width, &natural_height)) {
                    replaced_width = natural_width;
                    // A definite max-height constrains canvas's intrinsic inline
                    // contribution through its natural ratio, like other replaced boxes.
                    replaced_width = intrinsic_replaced_width_with_max_height(
                        lycon, element, view_block_replaced, replaced_width,
                        natural_width, natural_height);
                } else {
                    replaced_width = 300.0f;
                }
            } else {
                replaced_width = 300;
            }
        }
        else if (replaced_tag == MARKUP_NAME_AUDIO) {
            replaced_width = 300;
        }
        else if (replaced_tag == MARKUP_NAME_SVG) {
            const char* width_attr = element->get_attribute("width");
            bool width_attr_is_percentage = width_attr && strchr(width_attr, '%') != nullptr;
            bool svg_ratio_only = intrinsic_svg_is_ratio_only(element, view_block_replaced);
            if (width_attr_is_percentage || width_is_percentage) {
                // An unresolved percentage width uses the default replaced-object
                // contribution; the percentage is resolved after the parent size exists.
                replaced_width = 300.0f;
            } else if (svg_ratio_only) {
                replaced_width = 0.0f;
            } else {
                replaced_width = intrinsic_svg_size(element).width;
            }
            IntrinsicSvgSize svg_intrinsic = intrinsic_svg_size(element);
            float ratio_limited_width = intrinsic_replaced_width_with_max_height(
                lycon, element, view_block_replaced, replaced_width,
                svg_intrinsic.width, svg_intrinsic.height);
            if (ratio_limited_width < replaced_width) {
                replaced_width = ratio_limited_width;
            }
        }
        else if (replaced_tag == MARKUP_NAME_HR) {
            replaced_width = 0;
        }
        else if (replaced_tag == MARKUP_NAME_EMBED ||
                 (replaced_tag == MARKUP_NAME_OBJECT &&
                  (element->get_attribute(MARKUP_NAME_DATA) ||
                   layout_object_uses_default_size(element)))) {
            replaced_width = 300;
        }
        else if (replaced_tag == MARKUP_NAME_METER) {
            replaced_width = form_control_em_size(
                lycon, view_block_replaced, FormDefaults::METER_INLINE_SIZE_EM);
        }
        else if (replaced_tag == MARKUP_NAME_PROGRESS) {
            replaced_width = form_control_em_size(
                lycon, view_block_replaced, FormDefaults::PROGRESS_INLINE_SIZE_EM);
        }

        // Form controls (INPUT, SELECT, TEXTAREA) have intrinsic sizes.
        // When FormControlProp is allocated (styles resolved), use its intrinsic_width.
        // Otherwise, fall back to tag+type-based defaults for early intrinsic sizing.
        // Note: form->intrinsic_width set by calc_select_size already includes the
        // element's CSS padding+border (i.e. it is border-box). The common code at
        // the bottom of this function adds horizontal padding+border again — for
        // form controls sourced from form->intrinsic_width we therefore mark the
        // value as "already includes pad+border" and skip the bottom addition.
        bool input_button_has_native_label = view_block_replaced->form &&
            view_block_replaced->form->control_type == FORM_CONTROL_BUTTON &&
            replaced_tag == MARKUP_NAME_INPUT &&
            form_button_label_text(view_block_replaced,
                                   view_block_replaced->form);
        bool has_measurable_form_control = view_block_replaced->form &&
            (view_block_replaced->form->intrinsic_width > 0 ||
             input_button_has_native_label);
        // Input buttons have no DOM text child, so their native label must be measured centrally.
        if (replaced_width < 0 && view_block_replaced->role_kind() == DomElement::ROLE_FORM
            && has_measurable_form_control) {
            IntrinsicSize form_size = layout_measure_form_control(lycon, view_block_replaced,
                                                                  lycon->available_space);
            replaced_width = form_size.max_width;
        }
        // Tag-based fallback: form prop not yet allocated during early intrinsic sizing
        if (replaced_width < 0) {
            if (replaced_tag == MARKUP_NAME_INPUT) {
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
            } else if (replaced_tag == MARKUP_NAME_SELECT) {
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
                        if (ce && ce->tag() == MARKUP_NAME_OPTGROUP) {
                            const char* lbl = ce->get_attribute("label");
                            include_text(lbl, lbl ? strlen(lbl) : 0, 0.0f);
                        }
                    }
                    for (DomElement* option = dom_select_next_option(element, nullptr); option;
                         option = dom_select_next_option(element, option)) {
                        DomElement* parent = option->parent ? option->parent->as_element() : nullptr;
                        float indent = parent && parent->tag() == MARKUP_NAME_OPTGROUP
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
            } else if (replaced_tag == MARKUP_NAME_TEXTAREA) {
                // Textarea default: 20 cols * ~8px average char width + padding
                replaced_width = FormDefaults::TEXTAREA_COLS * 8.0f + FormDefaults::TEXTAREA_PADDING * 2;
            }
        }

        if (replaced_width >= 0) {
            float intrinsic_replaced_width = replaced_width;
            if (percentage_size_is_intrinsic_auto && view_block_replaced->form &&
                view_block_replaced->form->intrinsic_width > 0.0f) {
                // A provisional percentage used for the measurement pass must not
                // replace the form control's natural max-content contribution.
                intrinsic_replaced_width = view_block_replaced->form->intrinsic_width;
            }
            // CSS Sizing §5.2: a percentage in an indefinite containing block
            // contributes zero to min-content, while the replaced object's
            // natural size remains its max-content contribution.
            sizes.min_content = percentage_width_min_content_zero ? 0.0f : intrinsic_replaced_width;
            sizes.max_content = intrinsic_replaced_width;
            replaced_intrinsic_set = true;
        }
        if (percentage_size_resolves_to_zero) {
            // Percentage replaced sizes resolve against the definite zero containing
            // block; natural dimensions must not leak into shrink-to-fit contributions.
            sizes.min_content = 0.0f;
            sizes.max_content = 0.0f;
            replaced_intrinsic_set = true;
        }
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (!replaced_intrinsic_set && element->tag() == MARKUP_NAME_SVG) {
        float svg_width = intrinsic_svg_size(element).width;
        sizes.min_content = svg_width;
        sizes.max_content = svg_width;
        replaced_intrinsic_set = true;
    }

    // ========================================================================
    // Table element special handling: table intrinsic width = sum of cell widths
    // CSS Tables §4.1: Table min/max content width is the maximum over all rows
    // of the sum of cell min/max content widths in that row + border-spacing.
    // ========================================================================
    {
        ViewBlock* tbl_view = lam::unsafe_view_block_element_storage(element);
        bool is_table_element = intrinsic_element_display_matches(
            element, CSS_VALUE_TABLE, CSS_VALUE_INLINE_TABLE);
        if (!is_table_element && element->tag() == MARKUP_NAME_TABLE) is_table_element = true;

        if (is_table_element) {
            // Intrinsic sizing can run before normal table-tree construction;
            // refresh retained fixups here so generated cells inherit the
            // authored table font before their contributions are measured.
            layout_refresh_anonymous_table_fixup_inheritance(
                lycon, element, lycon->font.style);
            float border_spacing = 0;
            bool border_collapse = layout_table_has_collapsed_borders(lycon, element);
            if (border_collapse) {
                // CSS 2.1 §17.6.2 suppresses border-spacing for collapsed borders.
                border_spacing = 0.0f;
            } else if (element->tb) {
                border_spacing = element->tb->border_spacing_h;
            } else {
                // Table metadata not yet created during intrinsic measurement.
                // Read border-spacing from CSS, inherited computed values, or UA defaults.
                bool found_css = false;
                if (element->specified_style) {
                    CssDeclaration* bs_decl = style_tree_get_declaration(
                        element->specified_style, CSS_PROPERTY_BORDER_SPACING);
                    if (bs_decl && bs_decl->value) {
                        LayoutBorderSpacingValue resolved =
                            layout_resolve_border_spacing_value(lycon, bs_decl->value);
                        if (resolved.resolved) {
                            border_spacing = resolved.horizontal;
                            found_css = true;
                        } else if (resolved.keep_inheriting) {
                            float inherited_spacing_v = 0.0f;
                            found_css = layout_inherit_table_border_spacing(
                                lycon, element, &border_spacing, &inherited_spacing_v);
                        }
                    }
                }
                if (!found_css && element->tag() == MARKUP_NAME_TABLE) {
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
                    float inherited_spacing_v = 0.0f;
                    layout_inherit_table_border_spacing(
                        lycon, element, &border_spacing, &inherited_spacing_v);
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
                bool is_cell = !layout_element_is_floated(cell_elem) &&
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
                    if (child_view->boundary()->margin.left_type != CSS_VALUE_AUTO) {
                        margin_left = child_view->boundary()->margin.left;
                    }
                    if (child_view->boundary()->margin.right_type != CSS_VALUE_AUTO) {
                        margin_right = child_view->boundary()->margin.right;
                    }
                    return margin_left + margin_right;
                }
                if (!child_elem->specified_style) return 0.0f;
                layout_resolve_intrinsic_horizontal_margins(
                    lycon, child_elem, false, &margin_left, &margin_right);
                return margin_left + margin_right;
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
                        if (inline_run_has_content && layout_text_edge_has_whitespace(text, false)) {
                            inline_run_max += layout_measure_space_advance(
                                lycon, font_box_handle(&lycon->font), lycon->font.style);
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
                        prev_ended_with_space = layout_text_edge_has_whitespace(text, true);
                        inline_run_last_content_was_text = true;
                        continue;
                    }

                    if (!item->is_element()) continue;

                    DomElement* item_elem = item->as_element();
                    if (should_skip_anonymous_row_child(item_elem, anon_context)) break;

                    DisplayValue item_display = resolve_display_value((void*)item_elem);
                    bool item_is_float = layout_element_is_floated(item_elem);
                    if (!item_is_float && item_display.inner == CSS_VALUE_TABLE_CELL) break;
                    if (layout_display_is_none(item_display) ||
                        layout_element_is_abs_or_fixed(item_elem)) {
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
                        bool starts_with_ws = layout_element_edge_has_whitespace(item, false);
                        if (inline_run_has_content && (prev_ended_with_space || starts_with_ws)) {
                            inline_run_max += layout_measure_space_advance(
                                lycon, font_box_handle(&lycon->font), lycon->font.style);
                        }
                        inline_run_max += child_sizes.max_content;
                        if (child_sizes.min_content > inline_run_min) {
                            inline_run_min = child_sizes.min_content;
                        }
                        inline_run_has_content = true;
                        prev_ended_with_space = layout_element_edge_has_whitespace(item, true);
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
                IntrinsicFontScope font_scope(lycon, lycon->font);
                if (!font_element->styles_resolved()) {
                    LayoutViewScope view_scope(lycon);
                    lycon->view = static_cast<View*>(font_element);
                    radiant::LayoutRunModeScope run_mode_scope(
                        lycon, radiant::RunMode::ComputeSize);
                    dom_node_resolve_style(font_element, lycon);
                }
                ViewBlock* font_block =
                    lam::unsafe_view_block_element_storage(font_element);
                if (font_block->font && lycon->ui_context) {
                    setup_font(lycon->ui_context, &lycon->font, font_block->font);
                }
                callback();
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
                    bool is_row_group = !is_row &&
                        layout_display_is_table_row_group(child_display.inner);
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
            scratch_free(&lycon->scratch, col_max);
            scratch_free(&lycon->scratch, col_min);

            // Handle captions
            for (DomNode* child = element->first_child; child; child = child->next_sibling) {
                if (!child->is_element()) continue;
                DomElement* child_elem = child->as_element();
                NameId ctag = child_elem->tag();
                DisplayValue child_display = resolve_display_value((void*)child_elem);
                bool is_caption = (child_display.inner == CSS_VALUE_TABLE_CAPTION || ctag == MARKUP_NAME_CAPTION);
                if (is_caption) {
                    IntrinsicSizes cap = measure_element_intrinsic_widths(lycon, child_elem);
                    sizes.min_content = max(sizes.min_content, cap.min_content);
                    sizes.max_content = max(sizes.max_content, cap.max_content);
                }
            }

            // Add table's own padding and border
            float pad_left = 0, pad_right = 0, bdr_left = 0, bdr_right = 0;
            if (tbl_view->bound) {
                if (tbl_view->boundary_mut()->padding.left >= 0) pad_left = tbl_view->boundary_mut()->padding.left;
                if (tbl_view->boundary_mut()->padding.right >= 0) pad_right = tbl_view->boundary_mut()->padding.right;
                if (tbl_view->boundary()->border) {
                    bdr_left = tbl_view->boundary()->border->width.left;
                    bdr_right = tbl_view->boundary()->border->width.right;
                }
            }
            // Fallback: read border from specified CSS if bound not yet resolved
            if (bdr_left == 0 && bdr_right == 0) {
                get_intrinsic_box_side_widths_from_css(
                    lycon, element, true, true, 0.0f, &bdr_left, &bdr_right);
            }
            bool table_inline_axis_vertical =
                layout_block_inline_axis_is_vertical(tbl_view);
            if (bdr_left == 0 && bdr_right == 0 && element->specified_style &&
                !table_inline_axis_vertical) {
                // logical border-inline contributes to the table's intrinsic
                // inline size before physical border sides are materialized.
                CssDeclaration* inline_border = style_tree_get_declaration(
                    element->specified_style, CSS_PROPERTY_BORDER_INLINE);
                if (inline_border && inline_border->value) {
                    float width = intrinsic_border_width_from_shorthand_value(
                        lycon, CSS_PROPERTY_BORDER_WIDTH, inline_border->value);
                    bdr_left = width;
                    bdr_right = width;
                }
            }
            // Fallback: read border from HTML border attribute if still not found
            // WHATWG 15.3.10: table[border] → border-width = N pixels
            if (bdr_left == 0 && bdr_right == 0 && element->tag() == MARKUP_NAME_TABLE) {
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
                if (specified_width <= 0 && element->tag() == MARKUP_NAME_TABLE) {
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
                }

                return sizes;
            }
            // No table structure found — fall through to generic measurement
        }
    }

    intrinsic_materialize_pseudo_content(lycon, element);
    intrinsic_prepare_anonymous_table_children(lycon, element);

    // Track inline-level content separately
    float inline_min_sum = 0.0f;  // Sum of the current unbreakable inline run
    float inline_max_sum = 0.0f;  // Sum of max-content widths for inline children
    float vertical_block_axis_max = 0.0f;
    float vertical_text_block_axis_max = 0.0f;
    ViewBlock* view_block = lam::unsafe_view_block_element_storage(element);
    bool element_inline_axis_is_vertical = layout_element_inline_axis_is_vertical(element);
    float vertical_inline_basis = 0.0f;
    if (element_inline_axis_is_vertical) {
        // A vertical text query needs the containing inline-size to know how many
        // glyph advances fit in one column; without it the content stays one column.
        if (view_block->blk && view_block->block()->given_height >= 0.0f) {
            vertical_inline_basis = layout_uses_border_box(view_block)
                ? layout_content_size_from_border_box(
                    view_block, view_block->block()->given_height, false)
                : view_block->block()->given_height;
        } else if (lycon->block.parent && lycon->block.parent->content_height > 0.0f) {
            vertical_inline_basis = lycon->block.parent->content_height;
        }
    }
    float vertical_line_advance = element_inline_axis_is_vertical
        ? intrinsic_element_line_height(lycon, element, view_block) : 0.0f;
    float vertical_block_axis_sum = 0.0f;
    bool has_inline_content = false;
    bool has_in_flow_block_child = false;
    bool is_multicol = is_multicol_container(view_block);
    IntrinsicSizes multicol_spanner_sizes = {};
    bool intrinsic_list_item_scope = false;
    if (lycon->counter_context &&
        view_block->display.outer == CSS_VALUE_LIST_ITEM &&
        intrinsic_list_item_has_table_ancestor(element)) {
        // CSS Lists 3: intrinsic sizing must use the list item's own counter
        // scope before measuring its marker and generated list-item children.
        counter_push_scope(lycon->counter_context);
        intrinsic_list_item_scope = true;
        if (view_block->blk && view_block->block_mut()->counter_reset) {
            counter_reset(lycon->counter_context,
                          view_block->block()->counter_reset);
            compute_reversed_counter_initial(lycon, element);
        }
        if (view_block->blk && view_block->block_mut()->counter_increment) {
            counter_increment(lycon->counter_context,
                              view_block->block()->counter_increment);
        }
        if (view_block->blk && view_block->block_mut()->counter_set) {
            counter_set(lycon->counter_context, view_block->block()->counter_set);
        }
        process_list_item(lycon, view_block, element, element,
                          view_block->display);
    }
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
        if (vb->blk && vb->block_mut()->text_indent != 0.0f) {
            text_indent = vb->block()->text_indent;
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
        dom_element_has_before_content(element) && element->pseudo_style(PSEUDO_STYLE_BEFORE) && element->pseudo_style(PSEUDO_STYLE_BEFORE)->tree) {
        const char* before_content = dom_element_get_pseudo_element_content(
            element, 1 /*PSEUDO_ELEMENT_BEFORE*/);
        if (intrinsic_pseudo_style_is_inline(element->pseudo_style(PSEUDO_STYLE_BEFORE)) &&
            before_content && *before_content) {
            TextIntrinsicWidths tw = intrinsic_measure_pseudo_text_widths(
                lycon, element->pseudo_style(PSEUDO_STYLE_BEFORE), before_content);
            float before_width = tw.max_content;
            has_inline_content = true;
            has_generated_inline_before = true;
            inline_run_ends_with_collapsible_space = false;
            inline_max_sum += before_width;
            inline_min_sum = max(inline_min_sum, tw.min_content);
            if (first_inline_child_min < 0) {
                first_inline_child_min = tw.min_content;
            }
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
    // Check if this is a grid container.
    bool is_grid_container = intrinsic_element_display_matches(
        element, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);
    if (is_grid_container) {

        // CSS Grid §10.1: Grid container intrinsic widths are computed column-by-column.
        // For a grid with N explicit columns, the max-content width = sum of column max-contents
        // (each column's max-content = max of all items spanning only that column).
        // This is different from a block container which takes max of block children.
        GridProp* grid_prop = view_block->embed ? view_block->embedp()->grid : nullptr;
        int col_count = intrinsic_grid_template_column_count(
            element, view_block, 0);

        // A one-column grid still has a grid track whose content contribution
        // defines min/max-content width; skipping it collapses width:max-content.
        if (col_count > 0) {
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
                intrinsic_apply_grid_item_min_content_floor(lycon, child_elem, &child_sizes);
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
                pad_left = view_block->boundary()->padding.left;
                pad_right = view_block->boundary()->padding.right;
                if (view_block->boundary()->border) {
                    border_left = view_block->boundary()->border->width.left;
                    border_right = view_block->boundary()->border->width.right;
                }
            }
            total_min += pad_left + pad_right + border_left + border_right;
            total_max += pad_left + pad_right + border_left + border_right;

            // Apply max-width constraint (same logic as the generic path below).
            // The grid early-return bypasses the generic constraint code, so we apply it here.
            {
                float gmax = -1;
                if (view_block->blk) gmax = view_block->block()->given_max_width;
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

            return {total_min, total_max};
        }
    }

    is_flex_container = intrinsic_element_display_matches(
        element, CSS_VALUE_FLEX, CSS_VALUE_INLINE_FLEX);
    // Check flex direction for row vs column
    bool is_vertical_wm = false;
    if (is_flex_container) {
        LayoutFlexStyleInfo flex_style = layout_flex_style_info(lycon, view_block);
        is_row_flex = flex_style.row;
        flex_gap = flex_style.column_gap;
        if (view_block->embed && view_block->embedp()->flex) {
            WritingMode writing_mode = layout_block_writing_mode(view_block);
            is_vertical_wm = writing_mode == WM_VERTICAL_LR ||
                writing_mode == WM_VERTICAL_RL;
        } else {
            CssEnum wm = (CssEnum)0;
            if (intrinsic_style_keyword_declaration(element->specified_style,
                                                    CSS_PROPERTY_WRITING_MODE, &wm)) {
                is_vertical_wm = layout_writing_mode_from_css(wm) != WM_HORIZONTAL_TB;
            }
        }
        // In vertical writing modes, the physical axis mapping swaps:
        // column becomes horizontal, row becomes vertical
        if (is_vertical_wm) {
            is_row_flex = !is_row_flex;
        }
        // Check flex-wrap to determine min-content sizing strategy.
        // CSS Flexbox §9.9.1: for wrapping flex containers, min-content main size is the
        // largest flex item's min-content contribution (not the sum).
        is_flex_wrap = is_row_flex && flex_style.wrapping;
    }
    // Determine if this element has a definite height to propagate
    float element_definite_height = -1;
    bool hidden_auto_height = view_block->blk &&
        view_block->block()->content_visibility_hidden &&
        view_block->block()->contain_intrinsic_height_auto;

    // First check for explicit height from CSS (length value)
    if (!hidden_auto_height && view_block->blk && view_block->block_mut()->given_height > 0) {
        element_definite_height = view_block->block()->given_height;
        if (layout_uses_border_box(view_block)) {
            // Intrinsic percentage children use the parent's content box; the
            // raw border-box height would include padding and over-transfer ratio sizes.
            element_definite_height = layout_content_size_from_border_box(
                view_block, element_definite_height, false);
        }
    } else if (element->specified_style) {
        CssDeclaration* height_decl = layout_specified_physical_size_declaration(
            element, false);
        if (height_decl && height_decl->value) {
            if (height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
                element_definite_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, height_decl->value);
            } else if (height_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Check if parent has definite height for percentage resolution
                float percentage = (float)height_decl->value->data.percentage.value;
                float parent_height = intrinsic_parent_definite_height(lycon);
                if (parent_height > 0) {
                    element_definite_height = parent_height * percentage / 100.0f;
                }
            }
        }
    }

    // Preserve the inherited inline basis while overriding only the block
    // basis; replacing the parent context made cyclic percentage padding
    // resolve against a stale definite width.
    LayoutContainingBlockScope definite_height_scope(
        lycon, LAYOUT_AXIS_Y, element_definite_height, element_definite_height > 0.0f);

    // Measure children recursively
    bool explicit_box_decoration_inline =
        intrinsic_element_has_explicit_box_decoration_break(element);
    bool explicit_box_decoration_run_sizing = false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (child->is_element() &&
            intrinsic_element_has_explicit_box_decoration_break(child->as_element())) {
            explicit_box_decoration_run_sizing = true;
            break;
        }
    }
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        IntrinsicSizes child_sizes = {0, 0};
        bool is_inline = false;
        bool child_is_float = false;
        bool child_is_flex_contents = false;
        int flattened_flex_child_count = 0;

        if (child->is_element() &&
            layout_marker_is_outside(static_cast<View*>(child->as_element()))) {
            continue;
        }

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
                    bool propagates_inline_edge_space =
                        is_inline_level_element(element) &&
                        !intrinsic_element_is_atomic_inline(element) &&
                        ws == CSS_VALUE_NOWRAP;
                    bool has_preceding_boundary = propagates_inline_edge_space &&
                        intrinsic_has_inline_boundary_content_in_direction(element, false);
                    // nowrap keeps this collapsed separator in the unwrapped intrinsic
                    // run when a sibling boundary exists outside this recursive query.
                    bool in_whitespace = !has_inline_before &&
                        !has_preceding_boundary && !explicit_box_decoration_inline;
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
                                (is_inline_level_element(element) &&
                                 !intrinsic_element_is_atomic_inline(element) &&
                                 node_is_table_cell_like(next));
                        }
                    }
                    bool has_following_boundary = propagates_inline_edge_space &&
                        intrinsic_has_inline_boundary_content_in_direction(element, true);
                    // A child intrinsic query cannot see the containing line's next
                    // inline boundary; retain its trailing separator for that parent.
                    if (!has_inline_after && !has_following_boundary &&
                        !explicit_box_decoration_inline) {
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
                                if (el->block()->word_break != 0 && wb == CSS_VALUE_NORMAL) {
                                    wb = el->block()->word_break;
                                }
                                // Original overflow-wrap resolution (unchanged)
                                if (el->block()->overflow_wrap != 0) {
                                    ow = el->block()->overflow_wrap;
                                    break;
                                }
                                if (el->block()->word_break == CSS_VALUE_BREAK_WORD) {
                                    ow = CSS_VALUE_ANYWHERE;
                                    break;
                                }
                            }
                        }
                        n = n->parent;
                    }
                }

                TextIntrinsicWidths text_widths;
                float vertical_text_extent = 0.0f;
                float tab_size = (view_block->blk && view_block->block_mut()->tab_size >= 0)
                    ? view_block->block()->tab_size : 8.0f;
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
                            if (element_inline_axis_is_vertical) {
                                vertical_text_extent = max(vertical_text_extent,
                                    layout_vertical_text_block_extent(
                                        line_start, line_len,
                                        vertical_line_advance,
                                        vertical_inline_basis));
                            }
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
                            lycon, font_box_handle(&lycon->font), lycon->font.style);
                    }
                    if (preserve_spaces && text_line_has_tab(normalized_buffer, out_pos)) {
                        text_widths.max_content = measure_preserved_line_width_with_tabs(
                            lycon, normalized_buffer, out_pos, inline_max_sum,
                            text_transform, font_variant, tab_size);
                    }
                    if (element_inline_axis_is_vertical) {
                        vertical_text_extent = layout_vertical_text_block_extent(
                            normalized_buffer, out_pos, vertical_line_advance,
                            vertical_inline_basis);
                    }
                }
                child_sizes.min_content = text_widths.min_content;
                child_sizes.max_content = text_widths.max_content;

                if (element_inline_axis_is_vertical) {
                    vertical_text_block_axis_max = max(vertical_text_block_axis_max,
                        vertical_inline_basis > 0.0f
                            ? vertical_text_extent : text_widths.max_content);
                }

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
                }

            }
            is_inline = true;  // Text nodes are always inline (unless in flex container)
        } else if (child->is_element()) {
            DomElement* child_elem = child->as_element();
            // CSS 2.1 §9.2.4: Skip display:none children — they generate no boxes
            ViewBlock* child_vb = lam::unsafe_view_block_element_storage(child_elem);
            // A style-only measurement can leave the cached display empty; query the
            // computed display instead of mistaking that placeholder for `none`.
            if (layout_display_is_none(resolve_display_value(child_elem))) {
                continue;
            }

            // CSS 2.1 §9.3.1: Absolutely positioned elements are out of normal flow
            // and do not contribute to the containing block's intrinsic size.
            if (intrinsic_child_is_out_of_flow(child_elem, child_vb)) {
                continue;
            }

            DisplayValue child_display = resolve_display_value(child_elem);
            child_is_flex_contents = is_flex_container &&
                child_display.outer == CSS_VALUE_CONTENTS;
            child_sizes = child_is_flex_contents
                ? flex_measure_display_contents_intrinsic_widths(
                    lycon, view_block, child_elem, is_row_flex, is_flex_wrap,
                    &flattened_flex_child_count)
                : measure_element_intrinsic_widths(lycon, child_elem);
            if (is_grid_container) {
                // Auto-track grids use the normal child walk; retain the grid
                // item's explicit minimum in that path as well.
                intrinsic_apply_grid_item_min_content_floor(lycon, child_elem, &child_sizes);
            }

            // Re-check display:none after measurement (display may be resolved during measurement)
            if (layout_block_is_display_none(child_vb)) {
                continue;
            }

            if (is_multicol) {
                IntrinsicMulticolChild child_info = {};
                if (intrinsic_multicol_child_info(child, &child_info) &&
                    child_info.spans_all) {
                    // css multicol: spanners are not column content, so their
                    // intrinsic width must not be multiplied by column-count.
                    multicol_spanner_sizes.min_content = max(
                        multicol_spanner_sizes.min_content, child_sizes.min_content);
                    multicol_spanner_sizes.max_content = max(
                        multicol_spanner_sizes.max_content, child_sizes.max_content);
                    continue;
                }
            }

            // CSS 2.1 §17.2.1 wraps table-cell boxes in anonymous table boxes;
            // inside an inline parent those wrappers participate as inline-table.
            // Floats are blockified, so whitespace between them must not enter
            // the inline max-content run used by shrink-to-fit sizing.
            child_is_float = layout_element_is_floated(child_elem);
            is_inline = !child_is_float && (is_inline_level_element(child_elem) ||
                (is_inline_level_element(element) &&
                 !intrinsic_element_is_atomic_inline(element) &&
                 node_is_table_cell_like(child)));

            if (is_inline && element_inline_axis_is_vertical) {
                // With a definite vertical inline-size, inline descendants that
                // overflow that size form additional physical columns; their
                // block-axis contributions therefore add instead of collapsing to
                // the widest child.
                if (vertical_inline_basis > 0.0f) {
                    vertical_block_axis_sum += child_sizes.max_content;
                } else {
                    vertical_block_axis_max = max(vertical_block_axis_max,
                        child_sizes.max_content);
                }
            }

            // For inline elements, also add horizontal margins
            // CSS Sizing 3 §4: Negative margins reduce the element's outer size contribution
            if (is_inline) {
                LayoutIntrinsicMarginPair margins =
                    layout_intrinsic_horizontal_margin_pair(lycon, child_elem);
                float margin_sum = margins.left + margins.right;
                child_sizes.max_content += margin_sum;
                if (explicit_box_decoration_run_sizing || !margins.used_bound) {
                    child_sizes.min_content += margin_sum;
                }
            }
        }

        // Handle flex container children - all children become flex items
        // In a flex container, both text and element children are flex items
        // They should NOT go through the inline content path
        if (is_flex_container) {
            // CSS Flexbox §4: Absolutely positioned children are out-of-flow
            // and do not participate in flex layout or contribute to intrinsic size
            if (child->is_element() && !child_is_flex_contents) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_block = lam::unsafe_view_block_element_storage(child_elem);
                bool child_is_absolute = false;
                if (layout_block_is_out_of_flow_positioned(child_block)) {
                    child_is_absolute = true;
                } else if (child_elem->specified_style) {
                    child_is_absolute = layout_element_is_abs_or_fixed(child_elem);
                }
                if (child_is_absolute) {
                    continue;
                }
            }

            // CSS Flexbox §9.9.1: Flex item intrinsic contributions include
            // the item's outer size (content + padding + border + margin).
            // Add flex item margins to child_sizes before accumulating.
            if (child->is_element() && !child_is_flex_contents) {
                DomElement* child_elem = child->as_element();
                LayoutIntrinsicMarginPair margins =
                    layout_intrinsic_horizontal_margin_pair(lycon, child_elem,
                        {true, true, true, true, false});
                child_sizes.min_content += margins.left + margins.right;
                child_sizes.max_content += margins.left + margins.right;
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
                flex_child_count += child_is_flex_contents
                    ? flattened_flex_child_count : 1;
            } else {
                // Column flex: take max of widths
                sizes.min_content = max(sizes.min_content, child_sizes.min_content);
                sizes.max_content = max(sizes.max_content, child_sizes.max_content);
                flex_child_count += child_is_flex_contents
                    ? flattened_flex_child_count : 1;
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
                    intrinsic_has_inline_boundary_content_in_direction(child, true)) {
                    inline_max_sum += intrinsic_collapsed_space_width(lycon);
                    inline_run_ends_with_collapsible_space = true;
                }
                // A collapsed separator ends the current unbreakable run; it must
                // not reduce min-content to the widest individual inline child.
                if (explicit_box_decoration_run_sizing) {
                    sizes.min_content = max(sizes.min_content, inline_min_sum);
                    inline_min_sum = 0.0f;
                }
                inline_run_ends_with_collapsible_space = true;
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
            bool child_is_atomic_inline = child->is_element() &&
                intrinsic_element_is_atomic_inline(child->as_element());
            if (child_sizes.has_forced_break && !child_is_atomic_inline) {
                // An atomic inline-level box contains its own formatting context;
                // descendant forced breaks cannot split the containing inline run.
                // Add first line to current inline run
                float first_line_max = child_sizes.first_line_max;
                float last_line_max = child_sizes.last_line_max;
                if (child->is_element() &&
                    intrinsic_element_box_decoration_break(child->as_element()) !=
                        CSS_VALUE_CLONE) {
                    LayoutIntrinsicMarginPair margins =
                        layout_intrinsic_horizontal_margin_pair(
                            lycon, child->as_element());
                    // CSS 2.1 §8.3: forced-break line fragments carry an
                    // inline element's start margin on the first line and its
                    // end margin on the last; propagated widths omit them.
                    first_line_max += margins.left;
                    last_line_max += margins.right;
                }
                inline_max_sum += first_line_max;
                if (child->is_element() &&
                    intrinsic_element_box_decoration_break(child->as_element()) ==
                        CSS_VALUE_CLONE) {
                    // CSS Break 3 repeats both inline edges on every clone fragment;
                    // the propagated first-line width already contains the start
                    // decoration, so add the end decoration and start margin here.
                    inline_max_sum += intrinsic_inline_edge(
                        lycon, child->as_element(), false, false);
                    inline_max_sum += intrinsic_inline_start_margin(
                        lycon, child->as_element());
                }
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
                inline_max_sum = last_line_max;
                inline_run_ends_with_collapsible_space = false;
            } else {
                bool break_before_child = explicit_box_decoration_run_sizing &&
                    (inline_run_ends_with_collapsible_space ||
                     intrinsic_node_has_collapsible_space_at_edge(child, false));
                if (break_before_child) {
                    // A leading space in a sliced inline can leave its start edge
                    // on the previous fragment; include that edge before flushing
                    // the preceding min-content run.
                    bool starts_after_text = child->is_element() &&
                        intrinsic_node_has_collapsible_space_at_edge(child, false) &&
                        child->prev_sibling && child->prev_sibling->is_text();
                    if (starts_after_text) {
                        inline_min_sum += intrinsic_inline_edge(
                            lycon, child->as_element(), true);
                        // CSS Break 3: clone repeats the inline-end decoration
                        // on the fragment containing a leading collapsed space;
                        // that fragment still contributes when the word wraps.
                        if (intrinsic_element_box_decoration_break(child->as_element()) ==
                                CSS_VALUE_CLONE &&
                            !intrinsic_has_inline_boundary_content_in_direction(child, true)) {
                            inline_min_sum += intrinsic_inline_edge(
                                lycon, child->as_element(), false);
                        }
                    }
                    // Inline decorations do not create a break; only collapsed
                    // whitespace may terminate the min-content run.
                    sizes.min_content = max(sizes.min_content, inline_min_sum);
                    inline_min_sum = 0.0f;
                }
                inline_max_sum += child_sizes.max_content;
                inline_run_ends_with_collapsible_space =
                    child_sizes.max_content > 0.0f && intrinsic_node_has_collapsible_space_at_edge(child, true);
            }
            if (explicit_box_decoration_run_sizing) {
                // CSS Sizing 3 min-content is the widest unbreakable run; explicit
                // box-decoration-break inline boxes preserve that run across fragments.
                inline_min_sum += child_sizes.min_content;
            } else {
                inline_min_sum = max(inline_min_sum, child_sizes.min_content);
            }
            // Track first inline child's min-content for text-indent calculation
            if (first_inline_child_min < 0) {
                first_inline_child_min = child_sizes.min_content;
            } else {
                nonfirst_inline_min_max = max(nonfirst_inline_min_max, child_sizes.min_content);
            }
        } else {
            // Block-level child encountered
            if (!child_is_float) {
                has_in_flow_block_child = true;
            }

            // Detect if this block child is floated BEFORE flushing inline content.
            // CSS 2.1 §9.5: Floats don't break inline flow — inline content wraps
            // around them. So preceding inline content must NOT be flushed when we
            // encounter a floated child; it stays in the inline run and will be
            // combined with float_width_alongside_inline at the end.
            // CSS 2.1: Non-floated block children break inline flow (block-in-inline).
            // Floats do NOT break inline flow — inline content wraps around them.
            // Only flush inline content for non-floated block children.
            if (!child_is_float && inline_max_sum > 0) {
                sizes.min_content = max(sizes.min_content, inline_min_sum);
                sizes.max_content = max(sizes.max_content, inline_max_sum);
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
            float child_min_width = child_sizes.min_content;
            float margin_left = 0, margin_right = 0;
            if (child->is_element()) {
                DomElement* child_elem = child->as_element();
                LayoutIntrinsicMarginPair margins =
                    layout_intrinsic_horizontal_margin_pair(lycon, child_elem,
                        {true, true, true, false, false});
                margin_left = margins.left;
                margin_right = margins.right;
                child_width += margin_left + margin_right;
            }
            // a float's outer min/max contributions share its margins; otherwise a negative
            // margin can make min-content exceed max-content and force an oversized clamp.
            child_min_width += margin_left + margin_right;
            if (child_is_float) {
                // Floated block child: accumulate for side-by-side arrangement
                // At max-content (infinite width), all floats fit on one line → sum widths
                // At min-content, each float wraps to its own line → max of widths
                // Float layout keeps shrink-to-fit widths at subpixel precision;
                // pre-rounding each child can make a measured-fitting float run
                // wrap once the shrink-wrapped parent is laid out.
                float_max_sum += child_width;
                float_width_alongside_inline += child_width;
                float_min_max = max(float_min_max, child_min_width);
            } else {
                // Non-floated block child: flush any accumulated float run first
                if (float_max_sum > 0) {
                    sizes.min_content = max(sizes.min_content, float_min_max);
                    sizes.max_content = max(sizes.max_content, float_max_sum);
                    float_max_sum = 0;
                    float_min_max = 0;
                }
                // A non-float block child creates a full-width line, separating
                // preceding floats from subsequent inline content.
                float_width_alongside_inline = 0;
                // The child's outer min-content contribution includes fixed
                // terms from its margins, including calc() terms resolved
                // against the zero intrinsic percentage basis.
                sizes.min_content = max(sizes.min_content, child_min_width);
                sizes.max_content = max(sizes.max_content, child_width);
            }
        }
    }

    // Flush any remaining float run after the child loop
    if (float_max_sum > 0) {
        sizes.min_content = max(sizes.min_content, float_min_max);
        sizes.max_content = max(sizes.max_content, float_max_sum);
    }

    // Add gaps for row flex containers
    if (is_row_flex && flex_child_count > 1 && flex_gap > 0) {
        float total_gap = flex_gap * (flex_child_count - 1);
        // For wrapping flex, gaps don't apply to min-content (each item on its own line)
        if (!is_flex_wrap) {
            sizes.min_content += total_gap;
        }
        sizes.max_content += total_gap;
    }

    // CSS Lists 3 §3: marker content contributes to the list item's intrinsic
    // inline size when the list item participates in shrink-to-fit sizing.
    if (view_block->display.outer == CSS_VALUE_LIST_ITEM) {
        bool has_marker = true;  // default list-style-type is 'disc'

        bool marker_has_shrink_to_fit_ancestor = false;
        for (DomNode* anc = static_cast<DomNode*>(element); anc; anc = anc->parent) {
            if (!anc->is_element()) continue;
            DomElement* anc_elem = anc->as_element();
            ViewBlock* anc_view = lam::unsafe_view_block_element_storage(anc_elem);
            if (!anc_view) continue;
            if (layout_element_is_floated(anc_elem)) {
                bool auto_width = !anc_view->blk ||
                    anc_view->block()->given_width_type == CSS_VALUE_AUTO ||
                    anc_view->block()->given_width_type == CSS_VALUE__UNDEF;
                if (auto_width) {
                    marker_has_shrink_to_fit_ancestor = true;
                    break;
                }
            }
        }

        // Check list-style-type (if 'none', no marker) — also inherited
        if (view_block->blk && view_block->block_mut()->list_style_type == CSS_VALUE_NONE) {
            has_marker = false;
        } else {
            for (DomNode* anc = static_cast<DomNode*>(element); anc; anc = anc->parent) {
                if (!anc->is_element()) continue;
                DomElement* anc_elem = anc->as_element();
                ViewBlock* anc_view = lam::unsafe_view_block_element_storage(anc_elem);
                if (anc_view->blk && anc_view->block_mut()->list_style_type == CSS_VALUE_NONE) {
                    has_marker = false;
                    break;
                }
                if (anc_view->blk && anc_view->block_mut()->list_style_type != 0) {
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

        bool marker_is_inside = intrinsic_list_item_marker_is_inside(element, view_block);
        bool marker_has_table_ancestor = intrinsic_list_item_has_table_ancestor(element);
        if (has_marker &&
            ((marker_is_inside && !has_in_flow_block_child) ||
             (marker_has_shrink_to_fit_ancestor && !has_in_flow_block_child) ||
             marker_has_table_ancestor)) {
            // CSS Tables sizes anonymous cells from all in-flow descendants;
            // outside markers contribute only in the shrink-to-fit/table paths.
            float marker_width = intrinsic_list_item_marker_width(lycon, view_block);

            // The marker participates in the list item's intrinsic inline size.
            has_inline_content = true;
            inline_max_sum += marker_width;
            inline_min_sum = max(inline_min_sum, marker_width);
        }
    }

    if (has_inline_content && element_inline_axis_is_vertical) {
        float block_axis_line_extent = intrinsic_element_line_height(
            lycon, element, view_block);
        // In vertical writing, line-height is the line box's physical block
        // extent; text glyph width alone would under-measure shrink-to-fit floats.
        float vertical_child_extent = vertical_inline_basis > 0.0f
            ? vertical_block_axis_sum : vertical_block_axis_max;
        float block_axis_extent = max(block_axis_line_extent, vertical_child_extent);
        sizes.min_content = max(sizes.min_content, block_axis_extent);
        sizes.max_content = max(sizes.max_content, block_axis_extent);
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

            StyleTree* ps = is_before ? element->pseudo_style(PSEUDO_STYLE_BEFORE) : element->pseudo_style(PSEUDO_STYLE_AFTER);
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
            }
        }

        if (pseudo_extra_width > 0) {
            has_inline_content = true;
            inline_max_sum += pseudo_extra_width;
            inline_min_sum += pseudo_extra_width;
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
        if (element_inline_axis_is_vertical) {
            // CSS Writing Modes maps this container's physical width to the
            // block axis, so its inline line is sized by the widest item.
            float vertical_child_extent = vertical_inline_basis > 0.0f
                ? vertical_block_axis_sum : vertical_block_axis_max;
            float block_axis_extent = max(
                intrinsic_element_line_height(lycon, element, view_block),
                max(vertical_child_extent, vertical_text_block_axis_max));
            if (layout_element_css_writing_mode(element) == CSS_VALUE_SIDEWAYS_RL &&
                vertical_block_axis_max > 0.0f) {
                float sideways_ascender = 0.0f;
                float sideways_descender = 0.0f;
                if (font_box_handle(&lycon->font)) {
                    // sideways-rl maps the font's cross-axis content area into
                    // the inline box; the descender therefore contributes to
                    // the physical block extent alongside the child box.
                    font_get_content_area_split(font_box_handle(&lycon->font),
                        &sideways_ascender, &sideways_descender);
                }
                block_axis_extent = max(block_axis_extent,
                    vertical_block_axis_max + sideways_descender);
            }
            sizes.min_content = max(sizes.min_content, block_axis_extent);
            sizes.max_content = max(sizes.max_content, block_axis_extent);
        } else {
            sizes.min_content = max(sizes.min_content, inline_min_sum);
            // CSS 2.1 §9.5 + CSS Sizing 3 §4: At max-content (infinite width),
            // floats and inline content share the same line. The total width needed
            // is the sum of float widths + inline content width.
            sizes.max_content = max(sizes.max_content, inline_max_sum + float_width_alongside_inline);
        }
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
        DomNode* white_space_probe = element->first_child
            ? element->first_child : static_cast<DomNode*>(element);
        CssEnum element_white_space = get_white_space_value(white_space_probe);
        if (element_white_space == CSS_VALUE_PRE ||
            element_white_space == CSS_VALUE_NOWRAP) {
            // Preserved/nowrap inline content has no wrapping opportunities;
            // its min-content contribution is the unwrapped max-content line.
            sizes.min_content = sizes.max_content;
        }
    }

    if (view_block->multicol_prop() &&
        (view_block->multicol_prop()->column_count > 1 ||
         (view_block->multicol_prop()->column_count == 0 &&
          view_block->multicol_prop()->column_width > 0.0f)) &&
        multicol_has_in_flow_non_spanner_content(element)) {
        const MultiColumnProp* multicol = view_block->multicol_prop();
        int intrinsic_column_count = multicol->column_count > 0
            ? multicol->column_count : 1;
        float gap = multicol->column_gap_is_normal
            ? multicol_normal_gap_size(view_block)
            : multicol->column_gap_is_percent ? 0.0f : multicol->column_gap;
        if (gap < 0.0f) gap = 0.0f;
        float total_gap = gap * (intrinsic_column_count - 1);
        if (layout_block_inline_axis_is_vertical(view_block) &&
            !multicol->column_height_is_specified &&
            multicol->fill == COLUMN_FILL_BALANCE) {
            // In vertical writing, columns advance along the inline axis; the
            // physical width is the balanced block-axis extent of one column,
            // not the horizontal sum of all column tracks.
            float block_extent = multicol_intrinsic_vertical_block_extent(
                lycon, view_block, element);
            if (block_extent > 0.0f) {
                sizes.min_content = max(sizes.min_content, block_extent);
                sizes.max_content = max(sizes.max_content, block_extent);
            }
        } else {
            float column_floor = multicol->column_width;
            float column_min = max(sizes.min_content, column_floor);
            float column_max = max(sizes.max_content, column_floor);
            // Auto column-count resolves to one column for this intrinsic query; the
            // fixed column width is still a floor even when content is hidden.
            sizes.min_content = column_min * intrinsic_column_count + total_gap;
            sizes.max_content = column_max * intrinsic_column_count + total_gap;
        }
    }

    if (is_multicol) {
        sizes.min_content = max(sizes.min_content, multicol_spanner_sizes.min_content);
        sizes.max_content = max(sizes.max_content, multicol_spanner_sizes.max_content);
    }

    IntrinsicHorizontalBoxEdges edges =
        intrinsic_horizontal_box_edges(lycon, element, true);
    float pad_left = edges.padding_start;
    float pad_right = edges.padding_end;
    float border_left = edges.border_start;
    float border_right = edges.border_end;

    float horiz_padding = pad_left + pad_right;
    float horiz_border = border_left + border_right;
    if (!sizes.replaced_includes_pad_border) {
        sizes.max_content += horiz_padding + horiz_border;
        if (!sizes.replaced_min_excludes_pad_border) {
            sizes.min_content += horiz_padding + horiz_border;
        }
    }

    if (aspect_ratio_min_width > 0.0f) {
        float aspect_border_width = aspect_ratio_width_uses_border_box
            ? aspect_ratio_min_width
            : aspect_ratio_min_width + horiz_padding + horiz_border;
        sizes.min_content = max(sizes.min_content, aspect_border_width);
        sizes.max_content = max(sizes.max_content, aspect_border_width);
    }

    if (aspect_ratio_max_width > 0.0f && aspect_ratio_width_is_intrinsic_keyword) {
        float aspect_border_width = aspect_ratio_max_width_uses_border_box
            ? aspect_ratio_max_width
            : aspect_ratio_max_width + horiz_padding + horiz_border;
        // A transferred maximum constrains intrinsic preferred contributions;
        // the later min-width floor still wins when the two conflict.
        sizes.min_content = min(sizes.min_content, aspect_border_width);
        sizes.max_content = min(sizes.max_content, aspect_border_width);
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
    bool view_is_border_box = intrinsic_view_uses_border_box(view_for_minmax, element);
    bool width_is_inline_axis = !layout_block_inline_axis_is_vertical(view_for_minmax);

    // Apply min-width: floor the intrinsic sizes to ensure they're at least min-width
    float given_min_width = -1;
    if (view_for_minmax->blk) {
        given_min_width = view_for_minmax->block()->given_min_width;
    }
    if (percentage_min_width_intrinsic_zero) {
        // CSS Sizing resolves cyclic percentage terms against zero during the
        // intrinsic contribution, while preserving fixed terms in calc().
        given_min_width = intrinsic_resolve_horizontal_margin_value(
            lycon, CSS_PROPERTY_MIN_WIDTH, min_width_declaration->value);
    }
    // Fallback: check CSS if blk hasn't been set up yet. Early intrinsic
    // measurement must retain definite terms from calc(... + 0%); dropping
    // function-valued min-width loses the child's used minimum in its parent.
    if (given_min_width < 0 && element->specified_style) {
        CssDeclaration* min_mw_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_MIN_WIDTH);
        if (min_mw_decl && min_mw_decl->value) {
            given_min_width = intrinsic_resolve_definite_constraint(
                lycon, CSS_PROPERTY_MIN_WIDTH, min_mw_decl->value);
        }
    }
    float min_border_box = -1.0f;
    if (given_min_width >= 0.0f) {
        // For border-box: min-width is the border-box total (includes padding+border)
        // For content-box: min-width is content width, so add padding+border for border-box comparison
        min_border_box = view_is_border_box ? given_min_width
                                            : (given_min_width + horiz_padding + horiz_border);
        min_border_box = layout_floor_border_box_axis(view_for_minmax, min_border_box, true);
        if (sizes.min_content < min_border_box) {
            sizes.min_content = min_border_box;
        }
        if (sizes.max_content < min_border_box) {
            sizes.max_content = min_border_box;
        }
    }
    if (width_is_inline_axis &&
        layout_intrinsic_min_size_keyword(view_for_minmax, true) == CSS_VALUE_MAX_CONTENT) {
        // A max-content minimum floors the min-content contribution at max-content.
        sizes.min_content = sizes.max_content;
    } else if (width_is_inline_axis &&
               layout_intrinsic_min_size_keyword(view_for_minmax, true) == CSS_VALUE_MIN_CONTENT &&
               !percentage_width_min_content_zero) {
        // A min-content minimum caps the preferred contribution at min-content.
        sizes.max_content = sizes.min_content;
    }
    CssEnum max_width_keyword = layout_intrinsic_max_size_keyword(view_for_minmax, true);
    if (width_is_inline_axis && max_width_keyword == CSS_VALUE_MIN_CONTENT) {
        // max-width:min-content limits shrink-to-fit contributions before
        // float sizing; deferring this keyword to block layout leaks the
        // unconstrained max-content width into the containing float.
        sizes.max_content = sizes.min_content;
    } else if (width_is_inline_axis && max_width_keyword == CSS_VALUE_MAX_CONTENT) {
        // max-width:max-content clamps a wider specified width to the
        // preferred contribution, which also becomes the minimum here.
        sizes.min_content = sizes.max_content;
    }
    float given_max_width = -1;
    if (view_for_minmax->blk) {
        given_max_width = view_for_minmax->block()->given_max_width;
    }
    // Fallback: check CSS if blk hasn't been set up yet
    if (given_max_width < 0 && max_width_declaration && max_width_declaration->value &&
        max_width_declaration->value->type == CSS_VALUE_TYPE_LENGTH) {
        // Intrinsic passes may have no resolved BlockProp yet; use the already
        // cascade-selected physical declaration so a vertical child’s definite
        // max-width still constrains its parent’s shrink-to-fit contribution.
        given_max_width = resolve_length_value(
            lycon, CSS_PROPERTY_MAX_WIDTH, max_width_declaration->value);
    }
    if (given_max_width >= 0 &&
        max_width_keyword != CSS_VALUE_FIT_CONTENT &&
        !(max_width_is_percentage && percentage_size_is_intrinsic_auto)) {
        // CSS Sizing 3 §§5.1–5.2 measure intrinsic sizes with an auto-sized
        // hypothetical float, so max-width:fit-content() constrains used
        // sizing but must not cap this intrinsic contribution.
        // For content-box: max-width is content width, so add padding+border for comparison.
        float max_border_box = view_is_border_box ? given_max_width
                                                  : (given_max_width + horiz_padding + horiz_border);
        // min/max values cannot make a border box smaller than its padding and border.
        max_border_box = layout_floor_border_box_axis(view_for_minmax, max_border_box, true);
        if (min_border_box <= max_border_box) {
            if (sizes.max_content > max_border_box) {
                sizes.max_content = max_border_box;
            }
            // CSS Tables §4.1: Table content-box inline size is never smaller than its
            // minimum content inline size, so don't clamp min_content for tables.
            if (!is_table_display && sizes.min_content > max_border_box) {
                sizes.min_content = max_border_box;
            }
        }
    }

    // CSS Sizing requires max-content to be no smaller than min-content;
    // preserve the measured minimum before keyword normalization can copy max.
    sizes.max_content = max(sizes.max_content, sizes.min_content);
    CssEnum preferred_width_keyword = layout_intrinsic_preferred_size_keyword(
        view_for_minmax, true);
    if (width_is_inline_axis && preferred_width_keyword == CSS_VALUE_MAX_CONTENT) {
        // A max-content preferred size raises the min-content contribution;
        // otherwise an auto-width parent can shrink below the specified size.
        sizes.min_content = sizes.max_content;
    } else if (width_is_inline_axis && preferred_width_keyword == CSS_VALUE_MIN_CONTENT) {
        // A min-content preferred size caps the max-content contribution;
        // otherwise shrink-to-fit parents incorrectly expand the child.
        sizes.max_content = sizes.min_content;
    }

    if (physical_width_is_vertical_block_axis && !content_only) {
        float explicit_width = -1.0f;
        if (view_for_minmax->blk &&
            view_for_minmax->block()->given_width >= 0.0f &&
            view_for_minmax->block()->given_width_type != CSS_VALUE_AUTO) {
            explicit_width = view_for_minmax->block()->given_width;
        } else if (element->specified_style) {
            CssDeclaration* width_decl = layout_specified_physical_size_declaration(
                element, true);
            if (width_decl && width_decl->value &&
                (width_decl->value->type == CSS_VALUE_TYPE_LENGTH ||
                 width_decl->value->type == CSS_VALUE_TYPE_FUNCTION)) {
                explicit_width = resolve_length_value(
                    lycon, CSS_PROPERTY_WIDTH, width_decl->value);
            }
        }
        if (explicit_width >= 0.0f) {
            float explicit_border_width = intrinsic_view_uses_border_box(
                view_for_minmax, element)
                ? explicit_width : explicit_width + horiz_padding + horiz_border;
            explicit_border_width = layout_floor_border_box_axis(
                view_for_minmax, explicit_border_width, true);
            // The physical block-axis width path runs after the generic
            // min/max pass; reapply definite max-width here or the authored
            // width restores an intrinsic contribution that the used box later
            // clamps away.
            explicit_border_width = intrinsic_apply_definite_width_constraints(
                explicit_border_width);
            bool width_has_intrinsic_constraint = has_intrinsic_min_width ||
                has_intrinsic_preferred_width || has_intrinsic_max_width;
            if (!width_has_intrinsic_constraint) {
                // A definite physical block-size owns the containing box's
                // intrinsic contribution. Descendant overflow remains local;
                // letting it enlarge this size makes a fixed vertical block
                // inflate its horizontal shrink-to-fit parent.
                sizes.min_content = explicit_border_width;
                sizes.max_content = explicit_border_width;
            } else {
                // Intrinsic min/max keywords intentionally expose descendant
                // contributions instead of the definite physical block-size.
                sizes.min_content = max(sizes.min_content, explicit_border_width);
                sizes.max_content = max(sizes.max_content, explicit_border_width);
            }
        }
    }

    // css Values 4 permits approximating intrinsic contributions when their
    // used extent exceeds the layout coordinate range; clamp after margins,
    // padding, and sizing constraints have been aggregated.
    sizes.min_content = layout_clamp_dimension(sizes.min_content);
    sizes.max_content = layout_clamp_dimension(sizes.max_content);

    if (intrinsic_list_item_scope) {
        counter_pop_scope_propagate(lycon->counter_context, true);
    }

    // store result in intrinsic sizing cache
    if (!content_only && !intrinsic_percentage_width_is_indefinite(lycon) &&
        element->styles_resolved()) {
        radiant::LayoutCache* cache = radiant::layout_pass_ensure_cache(lycon, element);
        if (cache) {
            cache->intrinsic_min_content_width = sizes.min_content;
            cache->intrinsic_max_content_width = sizes.max_content;
            element->set_has_cached_intrinsic_widths(true);
        }
    }

    auto t_measure_end = std::chrono::high_resolution_clock::now();
    double measure_ms = std::chrono::duration<double, std::milli>(t_measure_end - t_measure_start).count();
    if (measure_ms > 100.0) {
        log_warn("SLOW MEASURE: %s took %.0fms", element->source_loc(), measure_ms);
    }

    return sizes;
}

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


static bool intrinsic_declared_css_height_is_definite(LayoutContext* lycon,
                                                      DomElement* element) {
    if (!lycon || !element || !element->specified_style) return false;

    CssDeclaration* height_decl = layout_specified_physical_size_declaration(element, false);
    if (!height_decl || !height_decl->value) return false;
    const CssValue* value = height_decl->value;
    if (value->type != CSS_VALUE_TYPE_LENGTH && value->type != CSS_VALUE_TYPE_FUNCTION &&
        !(value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value == 0)) {
        return false;
    }
    return resolve_length_value(lycon, CSS_PROPERTY_HEIGHT, value) >= 0.0f;
}

static bool intrinsic_has_definite_css_height(LayoutContext* lycon, DomElement* element,
                                              ViewBlock* view) {
    if (!lycon || !element || !view) return false;
    if (view->blk && view->block()->given_height >= 0.0f &&
        view->block()->given_height_type != CSS_VALUE_AUTO &&
        view->block()->given_height_type != CSS_VALUE__UNDEF) {
        return true;
    }
    return intrinsic_declared_css_height_is_definite(lycon, element);
}



static bool intrinsic_append_inline_text(StrBuf* output, DomNode* node,
                                         bool* pending_space, bool* has_content,
                                         bool allow_inline_descendants) {
    if (!output || !node || !pending_space || !has_content) return false;
    if (node->is_text()) {
        const char* data = reinterpret_cast<const char*>(node->text_data());
        if (!data) return true;
        for (size_t i = 0; data[i] != '\0'; i++) {
            if (data[i] == ' ' || data[i] == '\t' ||
                data[i] == '\n' || data[i] == '\r') {
                *pending_space = true;
            } else {
                if (*pending_space && output->length > 0 &&
                    output->str[output->length - 1] != '\v') {
                    strbuf_append_char(output, ' ');
                }
                strbuf_append_char(output, data[i]);
                *pending_space = false;
                *has_content = true;
            }
        }
        return true;
    }
    if (!node->is_element()) return false;

    DomElement* element = node->as_element();
    ViewElement* element_view = lam::view_require_element(static_cast<View*>(element));
    if (layout_display_is_none(element_view->display)) return true;
    if (element->tag_id == MARKUP_NAME_BR) {
        // preserve a forced break without making it ordinary collapsible whitespace.
        strbuf_append_char(output, '\v');
        *pending_space = false;
        *has_content = true;
        return true;
    }

    DisplayValue display = resolve_display_value(node);
    if (display.outer != CSS_VALUE_INLINE && display.outer != CSS_VALUE_CONTENTS) {
        return false;
    }
    if (!allow_inline_descendants) return false;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!intrinsic_append_inline_text(output, child, pending_space, has_content,
                                          allow_inline_descendants)) {
            return false;
        }
    }
    return true;
}

static float intrinsic_text_br_content_height(LayoutContext* lycon, DomElement* element,
                                              ViewBlock* view, float width) {
    if (!lycon || !element || !view || width <= 0.0f) return -1.0f;

    StrBuf* text = strbuf_new();
    if (!text) return -1.0f;
    bool has_content = false;
    bool pending_space = false;
    // balance measurement must flatten nested inline runs before probing line count;
    // ordinary flex intrinsic heights retain their block-flow fallback.
    bool allow_inline_descendants = lycon->block.text_wrap_style == CSS_VALUE_BALANCE;
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (!intrinsic_append_inline_text(text, child, &pending_space, &has_content,
                                          allow_inline_descendants)) {
            strbuf_free(text);
            return -1.0f;
        }
    }
    if (!has_content || text->length == 0) {
        strbuf_free(text);
        return -1.0f;
    }

    float line_height = intrinsic_element_line_height(lycon, element, view);
    float height = compute_text_height_at_width(
        lycon, text->str, text->length, width, line_height,
        get_element_text_transform(element), get_element_font_variant(element), true);
    strbuf_free(text);
    return height;
}

float calculate_max_content_height(LayoutContext* lycon, DomNode* node, float width,
                                   bool ignore_definite_block_size) {
    if (!node) return 0;

    // For text nodes, estimate based on line height and text width
    if (node->is_text()) {
        // Check if text is whitespace-only (shouldn't contribute to height in block context)
        const char* text = (const char*)node->text_data();
        if (!text || *text == '\0') return 0;


        if (text_node_is_ascii_whitespace(node)) return 0;
        // Calculate line height
        float font_size = 16.0f;
        if (lycon->font.style && lycon->font.style->font_size > 0) {
            font_size = lycon->font.style->font_size;
        }
        // Use actual font metrics for line-height:normal when available
        float line_height = font_size * 1.2f;  // fallback if no font loaded
        if (font_box_handle(&lycon->font)) {
            float normal_lh = font_calc_normal_line_height(font_box_handle(&lycon->font));
            if (normal_lh > 0) line_height = normal_lh;
        }

        // Check ancestor elements for resolved CSS line-height
        // Walk up the DOM tree since intermediate parents may not have blk resolved yet
        for (DomNode* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->is_element()) continue;
            ViewBlock* anc_view = lam::unsafe_view_block_element_storage(ancestor->as_element());
            if (anc_view->blk && anc_view->block_mut()->line_height) {
                const CssValue* lh = anc_view->block()->line_height;
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
            if (layout_white_space_collapses(ws_val)) {
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

            return measured_height;
        }

        return line_height;
    }

	    // For elements, we'd need to do a full layout pass
	    // For now, use a simplified estimation
	    DomElement* element = node->as_element();
	    if (!element) return 0;

    {
        IntrinsicFontScope style_font_scope(lycon, lycon->font);
        LayoutViewScope style_view_scope(lycon);
        radiant::LayoutRunModeScope run_mode_scope(lycon, radiant::RunMode::ComputeSize);
        lycon->view = static_cast<View*>(element);
        dom_node_resolve_style(element, lycon);
    }

	    // CSS 2.1 §9.2.4: Elements with display: none do not generate boxes
	    // and contribute zero height.
    ViewBlock* view = lam::unsafe_view_block_element_storage(element);
    if (layout_block_is_display_none(view)) {
        return 0;
    }
    bool replaced_intrinsic_width_query = ignore_definite_block_size && view->blk &&
        (view->block()->given_width_type == CSS_VALUE_MIN_CONTENT ||
         view->block()->given_width_type == CSS_VALUE_MAX_CONTENT);
    float replaced_query_width = replaced_intrinsic_width_query ? -1.0f : width;

    float height = 0;

    // Set up element's own font context for accurate text measurement.
    // child text nodes inherit lycon->font, so we must set the element's font
    // before processing children (mirrors the logic in measure_element_intrinsic_widths).
    FontBox saved_font_h = lycon->font;
    IntrinsicFontScope temp_height_font_guard(lycon, saved_font_h);
    if (view->font && lycon->ui_context) {
        setup_font(lycon->ui_context, &lycon->font, view->font);
    } else if (element->specified_style && lycon->ui_context && lycon->font.style) {
        CssDeclaration* font_size_decl = style_tree_get_declaration(
            element->specified_style, CSS_PROPERTY_FONT_SIZE);
        if (font_size_decl && font_size_decl->value) {
            bool resolved_from_medium = false;
            float resolved_size = layout_resolve_font_size(
                lycon, font_size_decl->value, lycon->font.style, false,
                &resolved_from_medium);
            if (resolved_size >= 0.0f && fabsf(resolved_size - lycon->font.style->font_size) > 0.1f) {
                FontProp* tfp = alloc_font_prop(lycon);
                if (tfp) {
                    temp_height_font_guard.prop_a = tfp;
                    if (lycon->font.style) {
                        radiant_retain_font_family(tfp, lam::PoolPtr<char>(lycon->font.style->family));
                    }
                    tfp->font_size = resolved_size;
                    tfp->font_size_from_medium = resolved_from_medium;
                    setup_font(lycon->ui_context, &lycon->font, tfp);
                }
            }
        }
    }

    // Check for explicit height from CSS (e.g., iframe with height: 580px)
    float explicit_height = -1.0f;
    bool contain_intrinsic_height_auto = view->blk &&
        view->block()->contain_intrinsic_height_auto;
    if (element->specified_style) {
        CssDeclaration* height_decl = layout_specified_physical_size_declaration(element, false);
        if (height_decl && height_decl->value &&
            height_decl->value->type == CSS_VALUE_TYPE_LENGTH) {
            explicit_height = resolve_length_value(lycon, CSS_PROPERTY_HEIGHT,
                                                   height_decl->value);
        }
    }
    if (!ignore_definite_block_size && explicit_height <= 0.0f &&
        view->blk && view->block_mut()->given_height > 0.0f) {
        // UA intrinsic defaults also populate given_height, so a winning author
        // height declaration must take precedence during intrinsic measurement.
            explicit_height = view->block()->given_height;
    }
    bool has_intrinsic_height_constraint =
        layout_intrinsic_min_size_keyword(view, false) != CSS_VALUE__UNDEF ||
        layout_intrinsic_max_size_keyword(view, false) != CSS_VALUE__UNDEF;
    if (!ignore_definite_block_size && !has_intrinsic_height_constraint &&
        !contain_intrinsic_height_auto &&
        explicit_height > 0.0f) {
        // Intrinsic min/max block-size keywords resolve from content; a specified
        // height must not short-circuit that contribution before the keyword is used.

        // Check box-sizing: if border-box, the height already includes padding/border
        // Only add padding/border for content-box (default)
        bool is_border_box = intrinsic_view_uses_border_box(view, element);

        if (!is_border_box && view->bound) {
            // content-box: height is content only, add padding and border
            if (view->boundary_mut()->padding.top >= 0) explicit_height += view->boundary_mut()->padding.top;
            if (view->boundary_mut()->padding.bottom >= 0) explicit_height += view->boundary_mut()->padding.bottom;
            if (view->boundary()->border) {
                explicit_height += view->boundary()->border->width.top;
                explicit_height += view->boundary()->border->width.bottom;
            }
        }
        // For border-box, given_height already includes padding/border, return as-is

        return explicit_height;
    }

    // `auto <ratio>` switches to an image's natural ratio once it is available;
    // load it before a definite grid width transfers into the block axis.
    if (layout_aspect_ratio_uses_content_box(view)) {
        layout_ensure_replaced_image_surface(lycon, view, element);
    }
    float aspect_ratio_min_height = 0.0f;
    float preferred_aspect_ratio = layout_used_preferred_aspect_ratio(view);
    if (preferred_aspect_ratio > 0.0f && replaced_query_width > 0.0f) {
        // A definite inline size transfers through a preferred ratio even when
        // an otherwise empty non-replaced element has no content height.
        float ratio_height = replaced_query_width / preferred_aspect_ratio;
        ratio_height = layout_apply_min_max_axis(view, ratio_height, false,
                                                   layout_uses_border_box(view));
        if (!element_has_in_flow_intrinsic_content(element)) {
            return ratio_height;
        }
        // A preferred ratio is an auto-size floor; in-flow content can grow a
        // non-replaced box beyond it and must remain in its intrinsic height.
        aspect_ratio_min_height = ratio_height;
    }

    float contain_intrinsic_width = -1.0f;
    float contain_intrinsic_height = -1.0f;
    layout_resolve_contain_intrinsic_size(lycon, element, &contain_intrinsic_width,
                                          &contain_intrinsic_height);
    bool has_contain_intrinsic_height = contain_intrinsic_height >= 0.0f &&
        !contain_intrinsic_height_auto &&
        !intrinsic_has_definite_css_height(lycon, element, view);
    if (view->blk && view->block()->content_visibility_hidden &&
        contain_intrinsic_height_auto && element->has_last_remembered_height() &&
        !intrinsic_has_definite_css_height(lycon, element, view)) {
        float remembered_height = element->last_remembered_height();
        // The remembered flow height includes every fragment, while the
        // hidden box otherwise measures only the empty fallback.
        return remembered_height;
    }
    bool has_empty_size_containment =
        layout_block_has_size_containment_in_axis(view, false) &&
        !contain_intrinsic_height_auto &&
        contain_intrinsic_height < 0.0f &&
        (!view->form || view->block()->content_visibility_hidden) &&
        (view->display.inner != CSS_VALUE_GRID ||
         view->block()->content_visibility_hidden);

    // CSS 2.1 §10.6.2: Replaced element intrinsic height
    // Size containment substitutes the fallback before a natural image ratio can
    // reintroduce the descendant contribution that containment removed.
    if ((view->display.inner == RDT_DISPLAY_REPLACED ||
         layout_object_uses_default_size(element)) &&
        !has_contain_intrinsic_height && !has_empty_size_containment) {
        NameId elem_tag = element->tag();
        if (elem_tag == MARKUP_NAME_IMG) {
            ImageSurface* img = layout_ensure_replaced_image_surface(lycon, view, element);
            if (img) {
                float image_query_width = replaced_query_width;
                bool width_is_definite = !layout_block_has_automatic_size(view, true) &&
                    view->blk && view->block()->given_width >= 0.0f;
                if (image_query_width <= 0.0f && width_is_definite) {
                    // Intrinsic height queries must use the definite CSS width;
                    // otherwise a loaded image's natural height hides its transferred size.
                    image_query_width = layout_uses_border_box(view)
                        ? layout_content_size_from_border_box(view, view->block()->given_width, true)
                        : view->block()->given_width;
                }
                float img_height = intrinsic_image_height(img, image_query_width,
                                                          width_is_definite);
                return img_height;
            }
            const char* attr_h = element->get_attribute("height");
            if (attr_h) {
                int h = atoi(attr_h);
                if (h > 0) return (float)h;
            }
            // also check blk->given_height from resolved CSS/HTML attributes
            if (view->blk && view->block_mut()->given_height >= 0) {
                return view->block()->given_height;
            }
            return 0.0f;  // no image loaded, no dimensions specified
        }
        else if (elem_tag == MARKUP_NAME_IFRAME || elem_tag == MARKUP_NAME_VIDEO ||
                 elem_tag == MARKUP_NAME_CANVAS ||
                 (elem_tag == MARKUP_NAME_OBJECT &&
                  layout_object_uses_default_size(element))) {
            // try actual video dimensions for aspect-correct height
            if (elem_tag == MARKUP_NAME_VIDEO && view->embed && view->embedp()->video) {
                int vw = rdt_video_get_width(view->embedp()->video);
                int vh = rdt_video_get_height(view->embedp()->video);
                if (vw > 0 && vh > 0) {
                    float video_height = (float)vh;
                    // scale proportionally if width was constrained
                    if (replaced_query_width > 0 && replaced_query_width != (float)vw) {
                        video_height = replaced_query_width * (float)vh / (float)vw;
                    }
                    return video_height;
                }
            }
            if (elem_tag == MARKUP_NAME_CANVAS) {
                float natural_width = 0.0f;
                float natural_height = 0.0f;
                // Canvas bitmap attributes scale through its natural ratio for intrinsic block sizing.
                if (layout_canvas_natural_size(view, &natural_width, &natural_height)) {
                    return replaced_query_width > 0.0f && natural_width > 0.0f
                        ? replaced_query_width * natural_height / natural_width : natural_height;
                }
            }
            return 150.0f;  // CSS default 300x150
        }
        else if (elem_tag == MARKUP_NAME_AUDIO) {
            return 54.0f;  // audio controls default height
        }
        else if (elem_tag == MARKUP_NAME_SVG) {
            if (intrinsic_svg_is_ratio_only(element, view) && replaced_query_width <= 0.0f) {
                return 0.0f;
            }
            float svg_height = intrinsic_svg_size(element, replaced_query_width).height;
            return svg_height;
        }
    }

    // SVG fallback: handle SVG elements even when display.inner is not yet resolved
    if (element->tag() == MARKUP_NAME_SVG && !has_empty_size_containment &&
        !has_contain_intrinsic_height) {
        if (intrinsic_svg_is_ratio_only(element, view) && replaced_query_width <= 0.0f) {
            return 0.0f;
        }
        float svg_height = intrinsic_svg_size(element, replaced_query_width).height;
        return svg_height;
    }

    // Form controls (input, select, textarea) — replaced elements with intrinsic height.
    // Per HTML/CSS, these are replaced elements: their layout children (e.g. <option>)
    // do NOT contribute to the visible box height. Return only the CONTENT height here;
    // the shared padding/border code at the bottom adds the rest. Skip child traversal.
    bool is_form_control_replaced = false;
    {
        uintptr_t etag = element->tag();
        if (etag == MARKUP_NAME_INPUT || etag == MARKUP_NAME_SELECT || etag == MARKUP_NAME_TEXTAREA) {
            float font_size = 16.0f;
            if (lycon->font.style && lycon->font.style->font_size > 0)
                font_size = lycon->font.style->font_size;

            if (etag == MARKUP_NAME_TEXTAREA) {
                int rows = FormDefaults::TEXTAREA_ROWS;
                height = rows * font_size * 1.2f + 2 * FormDefaults::TEXTAREA_PADDING;
            } else if (etag == MARKUP_NAME_INPUT) {
                const char* type_attr = element->get_attribute("type");
                if (type_attr && (strcmp(type_attr, "checkbox") == 0 || strcmp(type_attr, "radio") == 0)) {
                    return FormDefaults::CHECK_SIZE;  // fixed size, no CSS padding/border
                }
                if (type_attr && strcmp(type_attr, "range") == 0 && view->form &&
                    view->form->intrinsic_height > 0.0f) {
                    // Range controls use a UA border-box metric; the generic
                    // text-input formula is 0.7px short and distorts grid rows.
                    return view->form->intrinsic_height;
                }
                height = font_size + 2 * FormDefaults::TEXT_PADDING_V;
            } else {
                // select
                bool is_listbox = view->form &&
                    (view->form->multiple || view->form->select_size > 1);
                if (is_listbox) {
                    int visible_rows = view->form->select_size > 0
                        ? view->form->select_size : 4;
                    height = visible_rows * FormDefaults::SELECT_OPTION_ROW_HEIGHT;
                } else {
                    height = font_size + 2 * FormDefaults::TEXT_PADDING_V;
                }
            }

            is_form_control_replaced = true;
            // Skip children traversal below; pad/border still added at end.
        }
    }

    // Check if this is a grid container - need to detect column count
    bool is_grid_container = intrinsic_element_display_matches(
        element, CSS_VALUE_GRID, CSS_VALUE_INLINE_GRID);
    int grid_column_count = 1;  // Default: single column = vertical stacking
    float grid_row_gap = 0;
    float empty_grid_fixed_row_extent = -1.0f;

    if (is_grid_container) {
        grid_column_count = intrinsic_grid_template_column_count(
            element, view, grid_column_count);
        // Get row gap
        if (view->embed && view->embedp()->grid) {
            grid_row_gap = view->embedp()->grid->row_gap;
        }
        if (grid_row_gap <= 0) {
            intrinsic_style_length_declaration(
                lycon, element->specified_style, CSS_PROPERTY_ROW_GAP,
                CSS_PROPERTY_GAP, CSS_PROPERTY_GAP, &grid_row_gap);
        }
        empty_grid_fixed_row_extent = intrinsic_empty_grid_fixed_row_extent(
            view->embed ? view->embedp()->grid : nullptr, grid_row_gap);
    }

    // Check if this is a grid container with column flow
    // In column flow, items are placed in columns (side-by-side), so height = max(child_heights)
    bool is_grid_column_flow = false;
    if (view->display.inner == CSS_VALUE_GRID) {
        // Check if grid-auto-flow is column
        if (view->embed && view->embedp()->grid) {
            if (view->embedp()->grid->grid_auto_flow == CSS_VALUE_COLUMN) {
                is_grid_column_flow = true;
            }
        }
    }

    bool is_balanced_multicol = view->multicol_prop() &&
        is_multicol_container(view) &&
        view->multicol_prop()->column_count > 1 &&
        !view->multicol_prop()->column_height_is_specified &&
        view->multicol_prop()->fill == COLUMN_FILL_BALANCE &&
        multicol_has_in_flow_non_spanner_content(element);

    // Also check for flex containers with row direction (items side-by-side)
    bool is_flex_row = false;
    bool is_flex_wrap = false;
    float flex_row_gap = 0;
    float flex_column_gap = 0;
    bool is_flex_container = intrinsic_element_display_matches(

        element, CSS_VALUE_FLEX, CSS_VALUE_INLINE_FLEX);
    if (is_flex_container) {
        LayoutFlexStyleInfo flex_style = layout_flex_style_info(lycon, view);
        is_flex_row = flex_style.row;
        is_flex_wrap = flex_style.wrapping;
        flex_row_gap = flex_style.row_gap;
        flex_column_gap = flex_style.column_gap;
    }
    // content-visibility's auto intrinsic size must see generated block content;
    // deciding emptiness before materializing ::before made auto height fall back
    // even though the remembered layout contained a generated box.
    intrinsic_materialize_pseudo_content(lycon, element);
    bool is_empty_auto_container = !is_form_control_replaced &&
        view->display.inner != RDT_DISPLAY_REPLACED &&
        (!view->blk || view->block()->given_height < 0.0f) &&
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

    if (!is_grid_container && !is_flex_row &&
        (display_inner == CSS_VALUE_FLOW || display_inner == CSS_VALUE_CONTENTS)) {
        // Flow and contents boxes expose their inline descendants to this formatting context.
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
                if (!is_inline_level_element(child_elem)) {
                    // CSS Display 3: a contents wrapper is inline here only when
                    // flattening it does not expose a block-flow descendant.
                    has_only_inline_content = false;
                    break;
                }
            }
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
        block_flow_pad_top = view->boundary()->padding.top;
        if (view->boundary()->border) {
            block_flow_border_top = view->boundary()->border->width.top;
        }
    } else if (element->specified_style && width > 0.0f) {
        float unused_padding_bottom = 0.0f;
        get_intrinsic_box_side_widths_from_css(
            lycon, element, false, false, width,
            &block_flow_pad_top, &unused_padding_bottom);
    }
    bool block_flow_first_margin_collapses = is_block_flow &&
        block_flow_pad_top <= 0.0f && block_flow_border_top <= 0.0f &&
        !block_context_establishes_bfc(view);

    bool is_table_structure_box = intrinsic_is_table_box(element) ||
        intrinsic_is_table_row_group_box(element) ||
        intrinsic_is_table_row_box(element);

    // For multi-column grids, calculate height based on rows
    if (has_contain_intrinsic_height) {
        // Size containment removes descendant contributions; its fallback replaces
        // the content-box block contribution before padding and border are added.
        height = contain_intrinsic_height;
    } else if (has_empty_size_containment) {
        // Descendant layout has no block-axis contribution without a fallback.
        height = layout_block_empty_content_size_in_axis(view, false);
    } else if (is_grid_container && empty_grid_fixed_row_extent >= 0.0f &&
               !element_has_in_flow_intrinsic_content(element)) {
        // Fixed grid rows establish the empty grid's intrinsic block size.
        height = empty_grid_fixed_row_extent;
    } else if (is_form_control_replaced) {
        // Replaced form control: skip children entirely; padding/border added below.
    } else if (is_empty_auto_container) {
        // Empty auto-height containers have zero content height. Padding and border
        // are still added below, but line-height must not synthesize a line.
        height = 0.0f;
    } else if (is_table_structure_box) {
        // CSS tables size rows from the max cell height; block stacking would sum
        // sibling cells and overstate intrinsic table height.
        height = intrinsic_table_structure_height(lycon, element, width);
    } else if (is_balanced_multicol) {
        float flow_height = 0.0f;
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            flow_height += calculate_max_content_height(lycon, child, width);
        }
        int column_count = view->multicol_prop()->column_count;
        // An unbounded balanced multicol block distributes its flow contribution
        // across the fragmentainers before publishing its remembered block size.
        height = ceilf(flow_height / (float)column_count);
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
            scratch_free(&lycon->scratch, child_heights);
        }
    } else if (is_flex_row && is_flex_wrap && width > 0) {
        // Special handling for wrapping flex row containers
        // Simulate how items wrap based on available width

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
    } else {
        float inline_run_height = has_only_inline_content
            ? intrinsic_text_br_content_height(lycon, element, view, width) : -1.0f;
        if (inline_run_height >= 0.0f) {
            // Inline fragments share line boxes; taking their maximum loses
            // wrapped and <br>-separated lines from the intrinsic block size.
            height = inline_run_height;
        } else {
        // Calculate children's heights (original logic for non-grid or single-column)

        // Determine the element's own content width for children's percentage resolution.
        // The `width` parameter is the available width from the parent (containing block),
        // but if this element has an explicit CSS width, children resolve percentages
        // against this element's content width, not the grandparent's.
        float content_w = width;
        if (element->specified_style) {
            CssDeclaration* w_decl = layout_specified_physical_size_declaration(element, true);
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
                if (intrinsic_should_skip_height_child(child_elem)) continue;
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
                    get_intrinsic_box_side_widths_from_css(
                        lycon, child_elem, true, false, content_w,
                        &pad_left, &pad_right);
                    get_intrinsic_box_side_widths_from_css(
                        lycon, child_elem, true, true, 0.0f,
                        &border_left, &border_right);
                    child_padding_border = pad_left + pad_right + border_left + border_right;
                }
                if (child_padding_border > 0.0f && child_content_w > 0.0f) {
                    child_content_w -= child_padding_border;
                    if (child_content_w < 0.0f) child_content_w = 0.0f;
                }
            }
            float child_height = calculate_max_content_height(lycon, child, child_content_w);
            bool cyclic_percentage_ratio_child = false;
            if (child->is_element() &&
                !intrinsic_declared_css_height_is_definite(lycon, element)) {
                DomElement* child_elem = child->as_element();
                ViewBlock* child_block = lam::unsafe_view_block_element_storage(child_elem);
                float child_ratio = intrinsic_element_preferred_aspect_ratio(child_block);
                cyclic_percentage_ratio_child = intrinsic_element_has_percentage_height(child_elem) &&
                    child_block && child_ratio > 0.0f;
                if (cyclic_percentage_ratio_child && !element_has_in_flow_intrinsic_content(child_elem)) {
                    // An unresolved percentage height uses the preferred ratio for
                    // an empty child; its width-based fallback is not a real height.
                    child_height = layout_apply_min_max_axis(
                        child_block, child_content_w / child_ratio, false,
                        layout_uses_border_box(child_block));
                }
            }

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
                    mt = child_ve->boundary()->margin.top;
                    mb = child_ve->boundary()->margin.bottom;
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
                // These layouts take the maximum block contribution among same-line children.
                height = fmax(height, child_height);
            } else if (cyclic_percentage_ratio_child) {
                // A cyclic percentage child's ratio overflow is not additive to the auto parent's block size.
                height = fmax(height, child_height);
            } else {
                // Block-flow children stack in the block axis.
                height += child_height;
            }
            if (child_height > 0.0f) {
                block_flow_has_prior_content = true;
            } else if (block_flow_margin_handled) {
                block_flow_has_prior_content = true;
            }
        }
        }
    }

    // Add padding and border
    float pad_top = 0, pad_bottom = 0;
    float border_top = 0, border_bottom = 0;

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
            if (view->boundary_mut()->padding.top >= 0) pad_top = view->boundary_mut()->padding.top;
            if (view->boundary_mut()->padding.bottom >= 0) pad_bottom = view->boundary_mut()->padding.bottom;
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

    if (view->bound && view->boundary_mut()->border) {
        border_top = view->boundary()->border->width.top;
        border_bottom = view->boundary()->border->width.bottom;
    }

    // CSS 2.1 §8.3.1: The last child's margin-bottom does not collapse through the
    // parent when the parent has padding-bottom or border-bottom. In that case, the
    // last child's margin contributes to the parent's content height.
    if (is_block_flow && prev_margin_bottom > 0 && (pad_bottom > 0 || border_bottom > 0)) {
        height += prev_margin_bottom;
    }

    height += pad_top + pad_bottom + border_top + border_bottom;

    if (aspect_ratio_min_height > 0.0f) {
        bool ratio_uses_border_box = !layout_aspect_ratio_uses_content_box(view) &&
            layout_uses_border_box(view);
        float aspect_ratio_border_height = aspect_ratio_min_height;
        if (!ratio_uses_border_box) {
            aspect_ratio_border_height += pad_top + pad_bottom + border_top + border_bottom;
        }
        if (height < aspect_ratio_border_height) {
            height = aspect_ratio_border_height;
        }
    }

    return height;
}

float calculate_fit_content_width(LayoutContext* lycon, DomNode* node, float available_width) {
    // measure both min and max content in a single call to avoid redundant subtree traversals
    IntrinsicSizes sizes = calculate_node_intrinsic_widths(lycon, node);

    // CSS Sizing 3 §4.1: fit-content = max(min-content, min(max-content, available))
    // This ensures min-content is always respected as a floor, even when max-content
    // is smaller than min-content (e.g., negative text-indent).
    return fmaxf(sizes.min_content, fminf(sizes.max_content, available_width));
}

IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space
) {
    IntrinsicSizesBidirectional result = {0, 0, 0, 0};

    if (!lycon || !element) {
        return result;
    }

    if (flex_item_is_anonymous_text(element)) {
        // The anonymous item has no DOM Element backing; its text contribution
        // was measured when the flex item was created and must not enter the
        // element/custom-layout intrinsic path.
        result.min_content_width = element->fi->intrinsic_width.min_content;
        result.max_content_width = element->fi->intrinsic_width.max_content;
        result.min_content_height = element->fi->intrinsic_height.min_content;
        result.max_content_height = element->fi->intrinsic_height.max_content;
        return result;
    }

    DomNode* node = static_cast<DomNode*>(element);

    // Step 1: width measurement does not depend on available width.
    IntrinsicSizes width_sizes = calculate_node_intrinsic_widths(lycon, node);
    result.min_content_width = width_sizes.min_content;
    result.max_content_width = width_sizes.max_content;

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

    float content_height = calculate_max_content_height(lycon, node, width_for_height);
    result.min_content_height = content_height;
    result.max_content_height = content_height;
    return result;
}
