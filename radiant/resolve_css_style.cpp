#include "layout.hpp"
#include "view.hpp"
#include "render.hpp"
#include "../lib/font/font.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "../lib/tagged.hpp"
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <sys/stat.h>
#include <cmath>

// maximum grid span or repeat count to prevent excessive allocation
#define MAX_GRID_SPAN 1000

// Forward declaration for CSS variable lookup
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name);
Color resolve_color_value(LayoutContext* lycon, const CssValue* value);
static bool css_value_is_background_color_candidate(const CssValue* value);
static CssEnum css_resolve_content_alignment_keyword(const CssValue* value);
static float resolve_margin_with_inherit(LayoutContext* lycon, CssPropertyCode prop_id, const CssValue* value);
static float resolve_padding_with_inherit(LayoutContext* lycon, CssPropertyCode prop_id, const CssValue* value);

static DomElement* dom_parent_element(DomElement* element) {
    return (element && element->parent) ? lam::dom_require_element(element->parent) : nullptr;
}

// release builds compile log_debug arguments away, so trace-only helpers must be allowed to vanish.
[[maybe_unused]] static const char* css_enum_name_or_unknown(const CssEnumInfo* info) {
    return info ? info->name : "unknown";
}

static BackgroundProp* parent_computed_background(LayoutContext* lycon) {
    if (!lycon || !lycon->view || !lycon->view->is_element()) return nullptr;
    DomElement* element = lam::dom_require_element(lycon->view);
    DomElement* parent = dom_parent_element(element);
    return (parent && parent->bound) ? parent->boundary()->background : nullptr;
}

static BoundaryProp* ensure_span_bound(LayoutContext* lycon, ViewSpan* span) {
    return span->ensure_boundary(lycon);
}

static void ensure_span_background(LayoutContext* lycon, ViewSpan* span) {
    BoundaryProp* bound = ensure_span_bound(lycon, span);
    if (!bound->background) {
        bound->background = (BackgroundProp*)alloc_prop(lycon, sizeof(BackgroundProp));
    }
}

static BorderProp* ensure_span_border(LayoutContext* lycon, ViewSpan* span) {
    BoundaryProp* bound = ensure_span_bound(lycon, span);
    if (!bound->border) {
        bound->border = (BorderProp*)alloc_prop(lycon, sizeof(BorderProp));
    }
    return bound->border;
}

static OutlineProp* ensure_span_outline(LayoutContext* lycon, ViewSpan* span) {
    BoundaryProp* bound = ensure_span_bound(lycon, span);
    if (!bound->outline) {
        bound->outline = (OutlineProp*)alloc_prop(lycon, sizeof(OutlineProp));
    }
    return bound->outline;
}

static BlockProp* ensure_span_block(LayoutContext* lycon, ViewSpan* span) {
    return span->ensure_block(lycon);
}

static MultiColumnProp* ensure_multicol_prop(LayoutContext* lycon, ViewSpan* span) {
    return span->ensure_multicol(lycon);
}

static TransformProp* ensure_transform_prop(LayoutContext* lycon, ViewSpan* span) {
    return span->ensure_transform(lycon);
}

typedef struct FilterAmountSpec {
    const char* name;
    FilterFunctionType type;
    bool clamp_unit_interval;
} FilterAmountSpec;

static const FilterAmountSpec FILTER_AMOUNT_SPECS[] = {
    {"brightness", FILTER_BRIGHTNESS, false},
    {"contrast", FILTER_CONTRAST, false},
    {"grayscale", FILTER_GRAYSCALE, true},
    {"invert", FILTER_INVERT, true},
    {"opacity", FILTER_OPACITY, true},
    {"saturate", FILTER_SATURATE, false},
    {"sepia", FILTER_SEPIA, true},
};

static const FilterAmountSpec* find_filter_amount_spec(const char* name) {
    for (const FilterAmountSpec& spec : FILTER_AMOUNT_SPECS) {
        if (strcmp(name, spec.name) == 0) return &spec;
    }
    return nullptr;
}

static float resolve_filter_amount(const CssValue* value, bool clamp_unit_interval) {
    float amount = 1.0f;
    if (value && value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        amount = (float)value->data.percentage.value / 100.0f;
    } else if (value && value->type == CSS_VALUE_TYPE_NUMBER) {
        amount = (float)value->data.number.value;
    }
    if (clamp_unit_interval) {
        if (amount > 1.0f) amount = 1.0f;
        if (amount < 0.0f) amount = 0.0f;
    }
    return amount;
}

static bool shorthand_overrides_longhand(LayoutContext* lycon,
                                         CssPropertyCode shorthand_id,
                                         const CssDeclaration* longhand,
                                         const char* longhand_name) {
    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    if (!elem || !elem->specified_style) return false;
    CssDeclaration* shorthand = style_tree_get_declaration(
        elem->specified_style, shorthand_id);
    // shorthand components and longhands compete in the same cascade, including specificity.
    if (!shorthand || css_declaration_cascade_compare(shorthand, longhand) <= 0) return false;
    return true;
}

static void inherit_font_shorthand(LayoutContext* lycon, ViewSpan* span) {
    const FontProp* parent_font = lycon->font.style;
    if (!parent_font) return;
    span->ensure_font(lycon);

    if (parent_font->family) {
        radiant_retain_font_family(span->font, lam::PoolPtr<char>(parent_font->family));
    } else {
        radiant_clear_font_family(span->font);
    }
    span->font->font_size = parent_font->font_size;
    span->font->font_size_from_medium = parent_font->font_size_from_medium;
    span->font->font_weight = parent_font->font_weight;
    span->font->font_weight_numeric = parent_font->font_weight_numeric;
    span->font->font_style = parent_font->font_style;
    span->font->font_variant = parent_font->font_variant;

    DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    DomElement* parent = current ? dom_parent_element(current) : nullptr;
    ensure_span_block(lycon, span);
    span->blk->line_height = parent && parent->blk ? parent->blk->line_height : nullptr;
}

template <typename SlotType>
static bool resolve_keyword_slot(const CssValue* value, SlotType* slot) {
    if (value->type != CSS_VALUE_TYPE_KEYWORD || value->data.keyword <= 0) return false;
    *slot = value->data.keyword;
    return true;
}

static float resolve_transform_angle(const CssValue* value) {
    if (!value) return 0.0f;
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        return (float)value->data.number.value * (float)M_PI / 180.0f;
    }
    if (value->type != CSS_VALUE_TYPE_LENGTH && value->type != CSS_VALUE_TYPE_ANGLE) {
        return 0.0f;
    }
    // Axis-specific transforms must not lose grad/turn units handled by rotate().
    float angle = (float)value->data.length.value;
    switch (value->data.length.unit) {
        case CSS_UNIT_RAD: return angle;
        case CSS_UNIT_GRAD: return angle * (float)M_PI / 200.0f;
        case CSS_UNIT_TURN: return angle * 2.0f * (float)M_PI;
        default: return angle * (float)M_PI / 180.0f;
    }
}

static const char* counter_named_value(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_CUSTOM) return value->data.custom_property.name;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    return nullptr;
}

static void append_counter_text(StringBuf* buffer, const char* text) {
    if (!text) return;
    if (buffer->length > 0) stringbuf_append_char(buffer, ' ');
    stringbuf_append_str(buffer, text);
}

static void append_counter_value(StringBuf* buffer, const CssValue* value,
                                 bool allow_reversed) {
    const char* text = counter_named_value(value);
    if (text) {
        append_counter_text(buffer, text);
        return;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        if (buffer->length > 0) stringbuf_append_char(buffer, ' ');
        // INT_CAST_OK: CSS counter values are serialized as integer tokens.
        stringbuf_append_int(buffer, (int)value->data.number.value);
        return;
    }
    if (!allow_reversed || value->type != CSS_VALUE_TYPE_FUNCTION ||
        !value->data.function) return;

    CssFunction* function = value->data.function;
    if (function->name && strcmp(function->name, "reversed") == 0 &&
        function->arg_count >= 1) {
        append_counter_text(buffer, counter_named_value(function->args[0]));
    }
}

static void resolve_counter_property(LayoutContext* lycon, const CssValue* value,
                                     char** destination, const char* property_name,
                                     bool allow_reversed) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        *destination = (char*)alloc_prop(lycon, 5);
        str_copy(*destination, 5, "none", 4);
        return;
    }

    const char* direct_value =
        (value->type == CSS_VALUE_TYPE_CUSTOM || value->type == CSS_VALUE_TYPE_STRING)
            ? counter_named_value(value) : nullptr;
    if (direct_value) {
        size_t length = strlen(direct_value);
        *destination = (char*)alloc_prop(lycon, length + 1);
        str_copy(*destination, length + 1, direct_value, length);
        return;
    }

    bool is_list = value->type == CSS_VALUE_TYPE_LIST;
    bool is_reversed = allow_reversed && value->type == CSS_VALUE_TYPE_FUNCTION;
    if (!is_list && !is_reversed) return;

    StringBuf* buffer = stringbuf_new(lycon->doc->view_tree->prop_pool);
    if (!buffer) {
        log_error("[CSS] %s: stringbuf_new failed", property_name);
        return;
    }
    if (is_list) {
        for (int index = 0; index < value->data.list.count; index++) {
            append_counter_value(buffer, value->data.list.values[index], allow_reversed);
        }
    } else {
        append_counter_value(buffer, value, allow_reversed);
    }
    if (buffer->length > 0) {
        *destination = (char*)alloc_prop(lycon, buffer->length + 1);
        str_copy(*destination, buffer->length + 1,
                 buffer->str->chars, buffer->length);
    }
    stringbuf_free(buffer);
}

static void resolve_background_position_axis(LayoutContext* lycon,
                                             CssPropertyCode property,
                                             const CssValue* value,
                                             BackgroundProp* background,
                                             bool horizontal) {
    float* position = horizontal
        ? &background->bg_position_x : &background->bg_position_y;
    bool is_percent = false;
    bool update_percent_flag = true;

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        *position = resolve_length_value(lycon, property, value);
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *position = value->data.percentage.value;
        is_percent = true;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_CENTER) {
            *position = 50.0f;
            is_percent = true;
        } else if ((horizontal && keyword == CSS_VALUE_LEFT) ||
                   (!horizontal && keyword == CSS_VALUE_TOP)) {
            *position = 0.0f;
            is_percent = true;
        } else if ((horizontal && keyword == CSS_VALUE_RIGHT) ||
                   (!horizontal && keyword == CSS_VALUE_BOTTOM)) {
            *position = 100.0f;
            is_percent = true;
        } else {
            update_percent_flag = false;
        }
    } else {
        return;
    }

    if (update_percent_flag) {
        if (horizontal) background->bg_position_x_is_percent = is_percent;
        else background->bg_position_y_is_percent = is_percent;
    }
    background->bg_position_set = 1;
}

static CssBoxSide css_physical_side(CssPropertyCode property) {
    switch (property) {
        // Position insets share the same physical-side mapping as box sides;
        // keeping them here prevents grouped inset dispatch from collapsing
        // right/bottom/left declarations onto the top slot.
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_PADDING_RIGHT:
        // border shorthands use the same physical-side contract as their
        // width/style/color longhands; omitting them silently routed every
        // horizontal shorthand to the top edge.
        case CSS_PROPERTY_BORDER_RIGHT:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_STYLE:
        case CSS_PROPERTY_BORDER_RIGHT_COLOR:
            return CSS_BOX_SIDE_RIGHT;
        case CSS_PROPERTY_BOTTOM:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_BORDER_BOTTOM:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_STYLE:
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
            return CSS_BOX_SIDE_BOTTOM;
        case CSS_PROPERTY_LEFT:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_PADDING_LEFT:
        case CSS_PROPERTY_BORDER_LEFT:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_STYLE:
        case CSS_PROPERTY_BORDER_LEFT_COLOR:
            return CSS_BOX_SIDE_LEFT;
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_BORDER_TOP:
        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_TOP_STYLE:
        case CSS_PROPERTY_BORDER_TOP_COLOR:
        default:
            return CSS_BOX_SIDE_TOP;
    }
}

static CssPropertyCode border_side_width_property(CssBoxSide side) {
    switch (side) {
        case CSS_BOX_SIDE_TOP: return CSS_PROPERTY_BORDER_TOP_WIDTH;
        case CSS_BOX_SIDE_RIGHT: return CSS_PROPERTY_BORDER_RIGHT_WIDTH;
        case CSS_BOX_SIDE_BOTTOM: return CSS_PROPERTY_BORDER_BOTTOM_WIDTH;
        case CSS_BOX_SIDE_LEFT: return CSS_PROPERTY_BORDER_LEFT_WIDTH;
    }
    return CSS_PROPERTY_BORDER_TOP_WIDTH;
}

static BorderProp* parent_border_prop(LayoutContext* lycon) {
    DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    if (!current || !current->parent || !current->parent->is_element()) return nullptr;
    DomElement* parent = lam::dom_require<DOM_NODE_ELEMENT>(current->parent);
    return (parent->bound && parent->boundary_mut()->border) ? parent->boundary_mut()->border : nullptr;
}

static void resolve_border_side_width(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                      CssPropertyCode prop_id, const CssValue* value, int64_t specificity) {
    BorderProp* border = ensure_span_border(lycon, span);
    RadiantBorderSide refs = radiant_border_side(border, side);
    float* width_slot = refs.width;
    int64_t* width_spec = refs.width_specificity;
    if (specificity < *width_spec) {
        return;
    }

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        BorderProp* parent_border = parent_border_prop(lycon);
        if (parent_border) {
            *width_slot = *radiant_border_side(parent_border, side).width;
            *width_spec = specificity;
        }
        return;
    }

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        float width = resolve_length_value(lycon, prop_id, value);
        *width_slot = width;
        *width_spec = specificity;
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        float width = value->data.number.value;
        if (width != 0.0f) {
            return;
        }
        *width_slot = 0.0f;
        *width_spec = specificity;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        float width = 3.0f;
        if (keyword == CSS_VALUE_THIN) width = 1.0f;
        else if (keyword == CSS_VALUE_THICK) width = 5.0f;
        *width_slot = width;
        *width_spec = specificity;
    }
}

static void resolve_border_side_style(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                      const CssValue* value, int64_t specificity) {
    BorderProp* border = ensure_span_border(lycon, span);
    if (value->type != CSS_VALUE_TYPE_KEYWORD) return;

    RadiantBorderSide refs = radiant_border_side(border, side);
    CssEnum val = value->data.keyword;
    *refs.style = val;
    if (val == CSS_VALUE_NONE || val == CSS_VALUE_HIDDEN) {
        *refs.width = 0;
        *refs.width_specificity = specificity;
    }
}

static void resolve_border_side_color(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                      const CssValue* value, int64_t specificity) {
    BorderProp* border = ensure_span_border(lycon, span);
    RadiantBorderSide refs = radiant_border_side(border, side);
    int64_t* color_spec = refs.color_specificity;
    if (specificity >= *color_spec) {
        *refs.color = resolve_color_value(lycon, value);
        *color_spec = specificity;
    }
}

static CssEnum css_value_axis_type(const CssValue* value) {
    if (!value) return CSS_VALUE__UNDEF;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) return value->data.keyword;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return CSS_VALUE__PERCENTAGE;
    return CSS_VALUE__UNDEF;
}

static bool css_expand_box_shorthand(const CssValue* value,
                                     const CssValue* sides[4]) {
    if (!value || !sides) return false;
    if (value->type != CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < 4; i++) sides[i] = value;
        return true;
    }
    int count = value->data.list.count;
    if (count < 1 || count > 4 || !value->data.list.values) return false;
    for (int i = 0; i < 4; i++) {
        sides[i] = css_box_shorthand_side_value(value, i);
        if (!sides[i]) return false;
    }
    return true;
}

static void set_spacing_side(Spacing* spacing, Margin* margin, CssBoxSide side,
                             float value, CssEnum type, int64_t specificity) {
    int64_t* side_spec = radiant_spacing_specificity(spacing, side);
    if (specificity < *side_spec) return;
    *radiant_spacing_value(spacing, side) = value;
    *side_spec = specificity;
    if (margin) *radiant_margin_type(margin, side) = type;
}

static void resolve_spacing_side(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                 CssPropertyCode prop_id, const CssValue* value,
                                 int64_t specificity, bool is_margin) {
    BoundaryProp* bound = ensure_span_bound(lycon, span);
    if (is_margin) {
        set_spacing_side(&bound->margin, &bound->margin, side,
                         resolve_margin_with_inherit(lycon, prop_id, value),
                         css_value_axis_type(value), specificity);
    } else {
        set_spacing_side(&bound->padding, nullptr, side,
                         resolve_padding_with_inherit(lycon, prop_id, value),
                         CSS_VALUE__UNDEF, specificity);
    }
}

static void resolve_spacing_pair(LayoutContext* lycon, ViewSpan* span, CssBoxSide first_side,
                                 CssBoxSide second_side, CssPropertyCode prop_id,
                                 const CssValue* value, int64_t specificity, bool is_margin) {
    BoundaryProp* bound = ensure_span_bound(lycon, span);
    if (is_margin) {
        float margin_value = resolve_margin_with_inherit(lycon, prop_id, value);
        CssEnum margin_type = css_value_axis_type(value);
        set_spacing_side(&bound->margin, &bound->margin, first_side,
                         margin_value, margin_type, specificity);
        set_spacing_side(&bound->margin, &bound->margin, second_side,
                         margin_value, margin_type, specificity);
    } else {
        float padding_value = resolve_padding_with_inherit(lycon, prop_id, value);
        set_spacing_side(&bound->padding, nullptr, first_side,
                         padding_value, CSS_VALUE__UNDEF, specificity);
        set_spacing_side(&bound->padding, nullptr, second_side,
                         padding_value, CSS_VALUE__UNDEF, specificity);
    }
}

static void resolve_logical_spacing_property(LayoutContext* lycon, ViewSpan* span,
                                             CssPropertyCode property,
                                             const CssValue* value, int64_t specificity,
                                             bool is_margin, bool inline_axis_is_vertical,
                                             bool vertical_block_start_is_right) {
    CssBoxSide inline_start = inline_axis_is_vertical ? CSS_BOX_SIDE_TOP : CSS_BOX_SIDE_LEFT;
    CssBoxSide inline_end = inline_axis_is_vertical ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_RIGHT;
    CssBoxSide block_start = inline_axis_is_vertical
        ? (vertical_block_start_is_right ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_LEFT)
        : CSS_BOX_SIDE_TOP;
    CssBoxSide block_end = inline_axis_is_vertical
        ? (vertical_block_start_is_right ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_RIGHT)
        : CSS_BOX_SIDE_BOTTOM;
    bool is_block_axis = property == (is_margin ? CSS_PROPERTY_MARGIN_BLOCK : CSS_PROPERTY_PADDING_BLOCK);
    bool is_block_property = is_block_axis ||
        property == (is_margin ? CSS_PROPERTY_MARGIN_BLOCK_START : CSS_PROPERTY_PADDING_BLOCK_START) ||
        property == (is_margin ? CSS_PROPERTY_MARGIN_BLOCK_END : CSS_PROPERTY_PADDING_BLOCK_END);
    bool pair = is_block_axis ||
                property == (is_margin ? CSS_PROPERTY_MARGIN_INLINE : CSS_PROPERTY_PADDING_INLINE);
    // Preserve the shorthand's physical expansion: vertical block pairs use
    // left/right order, while block-start/end honor vertical-rl direction.
    CssBoxSide block_pair_start = inline_axis_is_vertical ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_TOP;
    CssBoxSide block_pair_end = inline_axis_is_vertical ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_BOTTOM;
    CssBoxSide first = is_block_axis ? block_pair_start : inline_start;
    CssBoxSide second = is_block_axis ? block_pair_end : inline_end;
    if (pair) {
        resolve_spacing_pair(lycon, span, first, second, property, value, specificity, is_margin);
        return;
    }

    bool is_start = property == (is_margin ? CSS_PROPERTY_MARGIN_BLOCK_START
                                           : CSS_PROPERTY_PADDING_BLOCK_START) ||
                    property == (is_margin ? CSS_PROPERTY_MARGIN_INLINE_START
                                           : CSS_PROPERTY_PADDING_INLINE_START);
    CssBoxSide side = is_block_property
        ? (is_start ? block_start : block_end)
        : (is_start ? inline_start : inline_end);
    resolve_spacing_side(lycon, span, side, property, value, specificity, is_margin);
}

static PositionProp* ensure_span_position(LayoutContext* lycon, ViewSpan* span) {
    span->ensure_position(lycon);

    return span->position;
}

static PositionProp* parent_position_prop(LayoutContext* lycon) {
    DomElement* parent = lycon->elmt->parent ? lycon->elmt->parent->as_element() : nullptr;
    return parent ? parent->position : nullptr;
}

static void set_inset_side_auto(PositionProp* position, CssBoxSide side) {
    RadiantInsetSide refs = radiant_inset_side(position, side);
    *refs.has = false;
    *refs.percent = NAN;
}

static void set_inset_side_value(PositionProp* position, CssBoxSide side,
                                 float inset_value, float inset_percent, bool has_value) {
    RadiantInsetSide refs = radiant_inset_side(position, side);
    *refs.value = inset_value;
    *refs.percent = inset_percent;
    *refs.has = has_value;
}

static bool inherit_inset_side(LayoutContext* lycon, PositionProp* position, CssBoxSide side) {
    PositionProp* parent = parent_position_prop(lycon);
    RadiantInsetSide parent_refs = parent ? radiant_inset_side(parent, side)
                                          : RadiantInsetSide{nullptr, nullptr, nullptr};
    if (!parent || !*parent_refs.has) {
        set_inset_side_auto(position, side);
        return false;
    }

    set_inset_side_value(position, side,
                         *parent_refs.value,
                         *parent_refs.percent,
                         true);
    return true;
}

typedef struct ResolvedInsetValue {
    float value;
    float percent;
    bool has_value;
} ResolvedInsetValue;

static ResolvedInsetValue resolve_inset_value(LayoutContext* lycon,
                                              CssPropertyCode prop_id,
                                              const CssValue* value) {
    ResolvedInsetValue resolved = {
        resolve_length_value(lycon, prop_id, value), NAN, true
    };
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        resolved.percent = value->data.percentage.value;
        if (isnan(resolved.value)) resolved.value = 0.0f;
    } else if (isnan(resolved.value)) {
        // Raw percentages remain resolvable later; other NaN offsets mean auto.
        resolved.value = 0.0f;
        resolved.has_value = false;
    }
    return resolved;
}

static void resolve_inset_side(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                               CssPropertyCode prop_id, const CssValue* value,
                               bool inherit_from_parent) {
    PositionProp* position = ensure_span_position(lycon, span);
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_INHERIT && inherit_from_parent) {
            inherit_inset_side(lycon, position, side);
            return;
        }
        if (value->data.keyword != CSS_VALUE_INHERIT) {
            set_inset_side_auto(position, side);
            return;
        }
    }

    ResolvedInsetValue resolved = resolve_inset_value(lycon, prop_id, value);
    set_inset_side_value(position, side, resolved.value,
                         resolved.percent, resolved.has_value);
}

static void resolve_inset_pair(LayoutContext* lycon, ViewSpan* span, CssBoxSide first_side,
                               CssBoxSide second_side, CssPropertyCode prop_id,
                               const CssValue* value) {
    PositionProp* position = ensure_span_position(lycon, span);
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword != CSS_VALUE_INHERIT) {
        set_inset_side_auto(position, first_side);
        set_inset_side_auto(position, second_side);
        return;
    }

    ResolvedInsetValue resolved = resolve_inset_value(lycon, prop_id, value);
    set_inset_side_value(position, first_side, resolved.value,
                         resolved.percent, resolved.has_value);
    set_inset_side_value(position, second_side, resolved.value,
                         resolved.percent, resolved.has_value);
}

static void resolve_inset_shorthand(LayoutContext* lycon, ViewSpan* span, const CssValue* value) {
    PositionProp* position = ensure_span_position(lycon, span);
    const CssValue* values[4] = {value, value, value, value};
    int count = 1;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        count = value->data.list.count;
        if (count > 4) count = 4;
        for (int i = 0; i < count; i++) {
            values[i] = value->data.list.values[i];
        }
    }

    switch (count) {
        case 1:
            values[1] = values[0];
            values[2] = values[0];
            values[3] = values[0];
            break;
        case 2:
            values[2] = values[0];
            values[3] = values[1];
            break;
        case 3:
            values[3] = values[1];
            break;
    }

    CssBoxSide sides[4] = {
        CSS_BOX_SIDE_TOP,
        CSS_BOX_SIDE_RIGHT,
        CSS_BOX_SIDE_BOTTOM,
        CSS_BOX_SIDE_LEFT
    };
    for (int i = 0; i < 4; i++) {
        if (values[i]->type == CSS_VALUE_TYPE_KEYWORD) {
            set_inset_side_auto(position, sides[i]);
        } else {
            resolve_inset_side(lycon, span, sides[i], CSS_PROPERTY_INSET, values[i], false);
        }
    }
}

static bool resolve_logical_inset_property(LayoutContext* lycon, ViewSpan* span,
                                           CssPropertyCode property, const CssValue* value,
                                           bool inline_axis_is_vertical,
                                           bool vertical_block_start_is_right) {
    CssBoxSide first = CSS_BOX_SIDE_TOP;
    CssBoxSide second = CSS_BOX_SIDE_TOP;
    CssPropertyCode physical = CSS_PROPERTY_TOP;
    bool pair = false;
    switch (property) {
        case CSS_PROPERTY_INSET_INLINE:
            first = inline_axis_is_vertical ? CSS_BOX_SIDE_TOP : CSS_BOX_SIDE_LEFT;
            second = inline_axis_is_vertical ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_RIGHT;
            physical = inline_axis_is_vertical ? CSS_PROPERTY_TOP : CSS_PROPERTY_LEFT;
            pair = true;
            break;
        case CSS_PROPERTY_INSET_INLINE_START:
            first = inline_axis_is_vertical ? CSS_BOX_SIDE_TOP : CSS_BOX_SIDE_LEFT;
            physical = inline_axis_is_vertical ? CSS_PROPERTY_TOP : CSS_PROPERTY_LEFT;
            break;
        case CSS_PROPERTY_INSET_INLINE_END:
            first = inline_axis_is_vertical ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_RIGHT;
            physical = inline_axis_is_vertical ? CSS_PROPERTY_BOTTOM : CSS_PROPERTY_RIGHT;
            break;
        case CSS_PROPERTY_INSET_BLOCK:
            first = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_LEFT)
                : CSS_BOX_SIDE_TOP;
            second = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_RIGHT)
                : CSS_BOX_SIDE_BOTTOM;
            physical = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_PROPERTY_RIGHT : CSS_PROPERTY_LEFT)
                : CSS_PROPERTY_TOP;
            pair = true;
            break;
        case CSS_PROPERTY_INSET_BLOCK_START:
            first = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_BOX_SIDE_RIGHT : CSS_BOX_SIDE_LEFT)
                : CSS_BOX_SIDE_TOP;
            physical = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_PROPERTY_RIGHT : CSS_PROPERTY_LEFT)
                : CSS_PROPERTY_TOP;
            break;
        case CSS_PROPERTY_INSET_BLOCK_END:
            first = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_BOX_SIDE_LEFT : CSS_BOX_SIDE_RIGHT)
                : CSS_BOX_SIDE_BOTTOM;
            physical = inline_axis_is_vertical
                ? (vertical_block_start_is_right ? CSS_PROPERTY_LEFT : CSS_PROPERTY_RIGHT)
                : CSS_PROPERTY_BOTTOM;
            break;
        default:
            return false;
    }

    // Logical insets are resolved to physical storage only after writing mode
    // is known; this keeps vertical-rl block-start on the physical right edge.
    if (pair) resolve_inset_pair(lycon, span, first, second, physical, value);
    else resolve_inset_side(lycon, span, first, physical, value, false);
    return true;
}

static Color inherit_background_color(LayoutContext* lycon) {
    BackgroundProp* parent_bg = parent_computed_background(lycon);
    Color color = {};
    if (parent_bg) {
        color = parent_bg->color;
    }
    return color;
}

static bool css_custom_property_name_matches(const char* stored_name, const char* lookup_name) {
    if (!stored_name || !lookup_name) return false;
    if (strcmp(stored_name, lookup_name) == 0) return true;

    const char* stored_body = strncmp(stored_name, "--", 2) == 0 ? stored_name + 2 : stored_name;
    const char* lookup_body = strncmp(lookup_name, "--", 2) == 0 ? lookup_name + 2 : lookup_name;
    return strcmp(stored_body, lookup_body) == 0;
}

const char* css_font_family_name_from_value(const CssValue* value) {
    if (!value) return NULL;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        return value->data.custom_property.name;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : NULL;
    }
    return NULL;
}

static char* duplicate_view_pool_layout_string(LayoutContext* lycon, const char* value) {
    if (!value || !lycon || !lycon->doc || !lycon->doc->view_tree || !lycon->doc->view_tree->prop_pool) {
        return nullptr;
    }
    return pool_strdup(lycon->doc->view_tree->prop_pool, value);
}

static void replace_view_pool_layout_string(LayoutContext* lycon, char** target, const char* value) {
    if (!target) return;
    // Grid item props are view-pool objects and can be repurposed through the
    // item-prop union, so attached names must share the pool lifetime.
    *target = duplicate_view_pool_layout_string(lycon, value);
}

static void replace_view_pool_layout_const_string(LayoutContext* lycon, const char** target, const char* value) {
    if (!target) return;
    // Grid item props are view-pool objects and can be repurposed through the
    // item-prop union, so attached names must share the pool lifetime.
    *target = duplicate_view_pool_layout_string(lycon, value);
}

struct CssGridAxisSlots {
    int* start;
    int* end;
    const char** start_name;
    const char** end_name;
    bool* has_start;
    bool* has_end;
    bool* start_is_span;
    bool* end_is_span;
    const char* name;
};

static CssGridAxisSlots css_grid_axis_slots(GridItemProp* item, bool is_row) {
    if (is_row) {
        return {&item->grid_row_start, &item->grid_row_end,
                &item->grid_row_start_name, &item->grid_row_end_name,
                &item->has_explicit_grid_row_start, &item->has_explicit_grid_row_end,
                &item->grid_row_start_is_span, &item->grid_row_end_is_span, "row"};
    }
    return {&item->grid_column_start, &item->grid_column_end,
            &item->grid_column_start_name, &item->grid_column_end_name,
            &item->has_explicit_grid_column_start, &item->has_explicit_grid_column_end,
            &item->grid_column_start_is_span, &item->grid_column_end_is_span, "column"};
}

static const char* css_grid_identifier(const CssValue* value) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) {
        return value->data.custom_property.name;
    }
    return nullptr;
}

static bool css_grid_is_separator(const CssValue* value) {
    const char* text = value->type == CSS_VALUE_TYPE_STRING
        ? value->data.string : css_grid_identifier(value);
    return text && strcmp(text, "/") == 0;
}

static const char* css_grid_named_line(const CssValue* value) {
    const char* name = css_grid_identifier(value);
    if (!name || strcmp(name, "span") == 0 || strcmp(name, "/") == 0 ||
        name[0] == '[' || name[0] == ']' ||
        (value->type == CSS_VALUE_TYPE_KEYWORD && strcmp(name, "auto") == 0)) {
        return nullptr;
    }
    return name;
}

static void resolve_grid_axis_shorthand(LayoutContext* lycon, ViewSpan* span,
                                        const CssValue* value, bool is_row) {
    alloc_grid_item_prop(lycon, span);
    GridItemProp* item = span->gi;
    CssGridAxisSlots axis = css_grid_axis_slots(item, is_row);

    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        int line = (int)value->data.number.value; // INT_CAST_OK: grid lines are discrete indices.
        *axis.start = line;
        *axis.has_start = true;
        item->is_grid_auto_placed = false;
        return;
    }
    if (value->type != CSS_VALUE_TYPE_LIST || value->data.list.count == 0) return;

    size_t count = value->data.list.count;
    CssValue** values = value->data.list.values;
    bool has_separator = false;
    for (size_t i = 0; i < count; i++) {
        if (css_grid_is_separator(values[i])) {
            has_separator = true;
            break;
        }
    }

    if (!has_separator) {
        bool is_span = false;
        int span_value = 1;
        int line_value = 0;
        for (size_t i = 0; i < count; i++) {
            CssValue* part = values[i];
            const char* identifier = css_grid_identifier(part);
            if (identifier && strcmp(identifier, "span") == 0) {
                is_span = true;
            } else if (part->type == CSS_VALUE_TYPE_NUMBER) {
                int number = (int)part->data.number.value; // INT_CAST_OK: grid lines and spans are discrete indices.
                if (is_span) span_value = number;
                else line_value = number;
            }
        }
        if (is_span) {
            *axis.start = 0;
            *axis.end = -span_value;
            *axis.has_end = true;
            *axis.end_is_span = true;
        } else if (line_value != 0) {
            *axis.start = line_value;
            *axis.has_start = true;
        }
    } else {
        int value_index = 0;
        bool saw_span = false;
        for (size_t i = 0; i < count; i++) {
            CssValue* part = values[i];
            if (css_grid_is_separator(part)) {
                value_index = 1;
                saw_span = false;
                continue;
            }

            const char* identifier = css_grid_identifier(part);
            if (identifier && strcmp(identifier, "span") == 0) {
                saw_span = true;
                continue;
            }
            if (part->type == CSS_VALUE_TYPE_NUMBER) {
                int number = (int)part->data.number.value; // INT_CAST_OK: grid lines and spans are discrete indices.
                int* line = value_index == 0 ? axis.start : axis.end;
                bool* has_line = value_index == 0 ? axis.has_start : axis.has_end;
                bool* line_is_span = value_index == 0 ? axis.start_is_span : axis.end_is_span;
                *line = saw_span ? -number : number;
                *has_line = true;
                if (saw_span) *line_is_span = true;
                saw_span = false;
                continue;
            }

            const char* line_name = css_grid_named_line(part);
            if (line_name) {
                const char** name_slot = value_index == 0 ? axis.start_name : axis.end_name;
                bool* has_line = value_index == 0 ? axis.has_start : axis.has_end;
                replace_view_pool_layout_const_string(lycon, name_slot, line_name);
                *has_line = true;
            }
        }
    }

    item->is_grid_auto_placed = false;
}

static void resolve_grid_line_longhand(LayoutContext* lycon, ViewSpan* span,
                                       const CssValue* value, bool is_row,
                                       bool is_end) {
    alloc_grid_item_prop(lycon, span);
    GridItemProp* item = span->gi;
    CssGridAxisSlots axis = css_grid_axis_slots(item, is_row);
    int* line = is_end ? axis.end : axis.start;
    const char** line_name = is_end ? axis.end_name : axis.start_name;
    bool* has_line = is_end ? axis.has_end : axis.has_start;
    bool* line_is_span = is_end ? axis.end_is_span : axis.start_is_span;
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        // INT_CAST_OK: grid lines are discrete indices.
        *line = (int)value->data.number.value;
        *has_line = true;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
               value->data.keyword == CSS_VALUE_AUTO) {
        *line = 0;
        return;
    } else {
        const char* name = css_grid_named_line(value);
        if (name) {
            replace_view_pool_layout_const_string(lycon, line_name, name);
            *has_line = true;
        } else if (is_end && value->type == CSS_VALUE_TYPE_LIST) {
            bool saw_span = false;
            int span_value = 1;
            for (int index = 0; index < value->data.list.count; index++) {
                CssValue* part = value->data.list.values[index];
                const char* identifier = css_grid_identifier(part);
                if (identifier && strcmp(identifier, "span") == 0) {
                    saw_span = true;
                } else if (part->type == CSS_VALUE_TYPE_NUMBER) {
                    // INT_CAST_OK: grid spans are discrete track counts.
                    span_value = (int)part->data.number.value;
                }
            }
            if (saw_span) {
                *line = -span_value;
                *line_is_span = true;
                *has_line = true;
            }
        }
    }

    if (*has_line) {
        item->is_grid_auto_placed = false;
    }
}

static void resolve_grid_placement_property(LayoutContext* lycon, ViewSpan* span,
                                             CssPropertyCode property,
                                             const CssValue* value) {
    switch (property) {
        case CSS_PROPERTY_GRID_COLUMN_START:
            resolve_grid_line_longhand(lycon, span, value, false, false); break;
        case CSS_PROPERTY_GRID_COLUMN_END:
            resolve_grid_line_longhand(lycon, span, value, false, true); break;
        case CSS_PROPERTY_GRID_ROW_START:
            resolve_grid_line_longhand(lycon, span, value, true, false); break;
        case CSS_PROPERTY_GRID_ROW_END:
            resolve_grid_line_longhand(lycon, span, value, true, true); break;
        case CSS_PROPERTY_GRID_COLUMN:
            resolve_grid_axis_shorthand(lycon, span, value, false); break;
        case CSS_PROPERTY_GRID_ROW:
            resolve_grid_axis_shorthand(lycon, span, value, true); break;
        default: break;
    }
}

static void resolve_flex_grid_container_alignment(LayoutContext* lycon,
                                                   ViewBlock* block,
                                                   CssPropertyCode property,
                                                   const CssValue* value) {
    if (!block) {
        return;
    }

    if (property == CSS_PROPERTY_ALIGN_CONTENT) {
        CssEnum alignment = css_resolve_content_alignment_keyword(value);
        if (alignment == CSS_VALUE__UNDEF) return;
        ensure_span_block(lycon, block)->align_content = alignment;
        if (block->display.inner == CSS_VALUE_FLEX) {
            alloc_flex_prop(lycon, block);
            block->embedp()->flex->align_content = alignment;
        }
        if (block->display.inner == CSS_VALUE_GRID) {
            alloc_grid_prop(lycon, block);
            block->embedp()->grid->align_content = alignment;
        }
        return;
    }

    alloc_flex_prop(lycon, block);
    alloc_grid_prop(lycon, block);
    bool justify = property == CSS_PROPERTY_JUSTIFY_CONTENT;
    resolve_keyword_slot(value, justify ? &block->embedp()->flex->justify
                                        : &block->embedp()->flex->align_items);
    resolve_keyword_slot(value, justify ? &block->embedp()->grid->justify_content
                                        : &block->embedp()->grid->align_items);
}

static void resolve_grid_alignment_property(LayoutContext* lycon, ViewBlock* block,
                                            ViewSpan* span, CssPropertyCode property,
                                            const CssValue* value) {
    if (property == CSS_PROPERTY_ALIGN_SELF) {
        if (value->type != CSS_VALUE_TYPE_KEYWORD || value->data.keyword <= 0) return;
        CssEnum alignment = value->data.keyword;
        if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
            span->gi->align_self_grid = alignment;
        } else if (span->parent_item_kind() == DomElement::PARENT_ITEM_FLEX) {
            span->fi->align_self = alignment;
        } else {
            alloc_flex_item_prop(lycon, span);
            span->fi->align_self = alignment;
        }
        return;
    }

    bool self = property == CSS_PROPERTY_JUSTIFY_SELF;
    if (!self && !block) {
        return;
    }
    if (self) {
        alloc_grid_item_prop(lycon, span);
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            span->gi->justify_self = value->data.keyword;
        }
    } else {
        alloc_grid_prop(lycon, block);
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            block->embedp()->grid->justify_items = value->data.keyword;
        }
    }
}

static void resolve_flex_item_number(LayoutContext* lycon, ViewSpan* span,
                                     CssPropertyCode property, const CssValue* value) {
    if (value->type != CSS_VALUE_TYPE_NUMBER) return;
    if (property == CSS_PROPERTY_ORDER) {
        // CSS order is a discrete integer even though the parser stores numbers uniformly.
        int order = (int)value->data.number.value; // INT_CAST_OK: CSS order is an integer.
        if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
            span->gi->order = order;
        } else if (span->parent_item_kind() == DomElement::PARENT_ITEM_FLEX) {
            span->fi->order = order;
        } else {
            alloc_flex_item_prop(lycon, span);
            span->fi->order = order;
        }
        return;
    }

    alloc_flex_item_prop(lycon, span);
    if (!span->flex_item()) return;
    float number = (float)value->data.number.value;
    if (property == CSS_PROPERTY_FLEX_GROW) span->fi->flex_grow = number;
    else span->fi->flex_shrink = number;
}

static bool css_is_content_alignment_keyword(CssEnum value) {
    switch (value) {
        case CSS_VALUE_NORMAL:
        case CSS_VALUE_STRETCH:
        case CSS_VALUE_START:
        case CSS_VALUE_END:
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_CENTER:
        case CSS_VALUE_BASELINE:
        case CSS_VALUE_SPACE_BETWEEN:
        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            return true;
        default:
            return false;
    }
}

static CssEnum css_resolve_content_alignment_keyword(const CssValue* value) {
    if (!value) return CSS_VALUE__UNDEF;

    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        return css_is_content_alignment_keyword(keyword) ? keyword : CSS_VALUE__UNDEF;
    }

    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.values) {
        CssEnum resolved = CSS_VALUE__UNDEF;
        for (int i = 0; i < value->data.list.count; i++) {
            CssEnum candidate = css_resolve_content_alignment_keyword(value->data.list.values[i]);
            if (candidate != CSS_VALUE__UNDEF) {
                resolved = candidate;
            }
        }
        return resolved;
    }

    return CSS_VALUE__UNDEF;
}

const char* css_join_font_family_values(LayoutContext* lycon, const CssValue* list,
                                        size_t start, size_t end) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree ||
        !list || list->type != CSS_VALUE_TYPE_LIST || start >= end) {
        return NULL;
    }
    size_t list_count = (size_t)list->data.list.count;
    if (end > list_count) end = list_count;
    if (start >= end) return NULL;

    if (end == start + 1) {
        return css_font_family_name_from_value(list->data.list.values[start]);
    }

    size_t total_len = 0;
    size_t part_count = 0;
    for (size_t i = start; i < end; i++) {
        const char* part = css_font_family_name_from_value(list->data.list.values[i]);
        if (!part || !*part) continue;
        total_len += strlen(part);
        part_count++;
    }
    if (part_count == 0) return NULL;
    total_len += part_count - 1;

    char* combined = (char*)pool_alloc(lycon->doc->view_tree->prop_pool, total_len + 1);
    if (!combined) return NULL;
    combined[0] = '\0';

    size_t pos = 0;
    bool first = true;
    for (size_t i = start; i < end; i++) {
        const char* part = css_font_family_name_from_value(list->data.list.values[i]);
        if (!part || !*part) continue;
        if (!first) {
            pos = str_cat(combined, pos, total_len + 1, " ", 1);
        }
        pos = str_cat(combined, pos, total_len + 1, part, strlen(part));
        first = false;
    }
    return combined;
}

static const char* css_font_family_group_name(LayoutContext* lycon,
                                              const CssValue* value) {
    if (!value) return NULL;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        return css_join_font_family_values(
            lycon, value, 0, (size_t)value->data.list.count);
    }
    return css_font_family_name_from_value(value);
}

static const char* css_join_font_family_groups(LayoutContext* lycon,
                                               const CssValue* list,
                                               size_t start, size_t end) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree ||
        !list || list->type != CSS_VALUE_TYPE_LIST) return NULL;
    size_t count = (size_t)list->data.list.count;
    if (end > count) end = count;
    if (start >= end) return NULL;

    size_t total_len = 0;
    size_t part_count = 0;
    for (size_t i = start; i < end; i++) {
        const char* part = css_font_family_group_name(lycon, list->data.list.values[i]);
        if (!part || !*part) continue;
        total_len += strlen(part);
        part_count++;
    }
    if (part_count == 0) return NULL;
    total_len += (part_count - 1) * 2;

    char* combined = (char*)pool_alloc(lycon->doc->view_tree->prop_pool, total_len + 1);
    if (!combined) return NULL;
    combined[0] = '\0';
    size_t pos = 0;
    bool first = true;
    for (size_t i = start; i < end; i++) {
        const char* part = css_font_family_group_name(lycon, list->data.list.values[i]);
        if (!part || !*part) continue;
        if (!first) pos = str_cat(combined, pos, total_len + 1, ", ", 2);
        pos = str_cat(combined, pos, total_len + 1, part, strlen(part));
        first = false;
    }
    return combined;
}

const char* css_select_font_family(LayoutContext* lycon, const CssValue* value,
                                   bool require_loadable_face_source) {
    if (!value) return NULL;
    if (value->type != CSS_VALUE_TYPE_LIST) return css_font_family_name_from_value(value);
    (void)require_loadable_face_source;
    // Glyph fallback happens per character, so computed style must retain the
    // authored family order instead of collapsing it to the first loadable face.
    return css_join_font_family_groups(
        lycon, value, 0, (size_t)value->data.list.count);
}

const char* css_select_font_shorthand_family(LayoutContext* lycon,
                                             const CssValue* shorthand_value,
                                             const CssValue* main_group,
                                             size_t family_start_index,
                                             bool require_loadable_face_source) {
    (void)require_loadable_face_source;
    const char* first = main_group && main_group->type == CSS_VALUE_TYPE_LIST
        ? css_join_font_family_values(
            lycon, main_group, family_start_index, main_group->data.list.count)
        : NULL;
    // A flat shorthand list also contains size and line-height tokens; only a
    // nested outer list represents additional comma-separated family groups.
    if (shorthand_value == main_group) return first;
    if (!shorthand_value || shorthand_value->type != CSS_VALUE_TYPE_LIST ||
        shorthand_value->data.list.count < 2) return first;

    size_t total_len = first ? strlen(first) : 0;
    size_t part_count = first ? 1 : 0;
    size_t shorthand_count = (size_t)shorthand_value->data.list.count;
    for (size_t i = 1; i < shorthand_count; i++) {
        const char* part = css_font_family_group_name(
            lycon, shorthand_value->data.list.values[i]);
        if (!part || !*part) continue;
        total_len += strlen(part);
        part_count++;
    }
    if (part_count == 0) return NULL;
    if (part_count == 1) {
        if (first) return first;
        for (size_t i = 1; i < shorthand_count; i++) {
            const char* part = css_font_family_group_name(
                lycon, shorthand_value->data.list.values[i]);
            if (part && *part) return part;
        }
    }
    total_len += (part_count - 1) * 2;

    char* combined = (char*)pool_alloc(lycon->doc->view_tree->prop_pool, total_len + 1);
    if (!combined) return first;
    combined[0] = '\0';
    size_t pos = 0;
    bool has_part = false;
    if (first) {
        pos = str_cat(combined, pos, total_len + 1, first, strlen(first));
        has_part = true;
    }
    for (size_t i = 1; i < shorthand_count; i++) {
        const char* part = css_font_family_group_name(
            lycon, shorthand_value->data.list.values[i]);
        if (!part || !*part) continue;
        if (has_part) pos = str_cat(combined, pos, total_len + 1, ", ", 2);
        pos = str_cat(combined, pos, total_len + 1, part, strlen(part));
        has_part = true;
    }
    return combined;
}

/**
 * Look up a CSS custom property (variable) value
 * Searches current element and ancestors (CSS variables inherit)
 * @param lycon Layout context containing current element
 * @param var_name Variable name (e.g., "--primary-color")
 * @return CssValue* if found, nullptr otherwise
 */
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name) {
    if (!lycon || !lycon->view || !var_name) return nullptr;

    DomNode* current = lycon->view;
    while (current && !current->is_element()) {
        current = current->parent;
    }
    // Custom properties inherit through elements; layout can resolve var()
    // while the active view is a text node, so start from its nearest element.
    DomElement* element = current ? lam::dom_require<DOM_NODE_ELEMENT>(current) : nullptr;

    // Search up the DOM tree (CSS variables inherit)
    while (element) {
        // Check if this element has CSS variables
        if (element->css_variables) {
            CssCustomProp* var = element->css_variables;
            while (var) {
                if (css_custom_property_name_matches(var->name, var_name)) {
                    return var->value;
                }
                var = var->next;
            }
        }

        // Move to parent element
        if (element->parent && element->parent->is_element()) {
            element = lam::dom_require<DOM_NODE_ELEMENT>(element->parent);
        } else {
            break;
        }
    }

    return nullptr;
}

static bool is_border_radius_slash(const CssValue* value) {
    return value && value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name &&
           strcmp(value->data.custom_property.name, "/") == 0;
}

static const char* css_value_identifier_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        return value->data.custom_property.name;
    }
    if (value->type == CSS_VALUE_TYPE_STRING) {
        return value->data.string;
    }
    return nullptr;
}

static bool css_value_identifier_is(const CssValue* value, const char* name) {
    const char* ident = css_value_identifier_name(value);
    return ident && name && strcasecmp(ident, name) == 0;
}

static bool resolve_nonnegative_css_length(LayoutContext* lycon, uintptr_t property,
                                           const CssValue* value, float* out_length) {
    if (!value || !out_length) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
        float length = resolve_length_value(lycon, property, value);
        if (isnan(length)) return false;
        *out_length = max(length, 0.0f);
        return true;
    }
    return false;
}

static void css_distribute_missing_gradient_positions(GradientStop* stops,
                                                      int stop_count) {
    if (!stops || stop_count <= 0) return;
    for (int i = 0; i < stop_count; i++) {
        if (stops[i].position >= 0.0f) continue;
        // A single parsed stop has no interval denominator and starts at zero.
        stops[i].position = stop_count > 1
            ? (float)i / (float)(stop_count - 1) : 0.0f;
    }
}

static bool resolve_linear_gradient_value(LayoutContext* lycon, const CssValue* value,
                                          LinearGradient** out_gradient) {
    if (out_gradient) *out_gradient = nullptr;
    if (!lycon || !value || !out_gradient ||
        value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name) {
        return false;
    }

    CssFunction* func = value->data.function;
    bool repeating = strcmp(func->name, "repeating-linear-gradient") == 0;
    if (strcmp(func->name, "linear-gradient") != 0 && !repeating) {
        return false;
    }

    LinearGradient* lg = (LinearGradient*)alloc_prop(lycon, sizeof(LinearGradient));
    if (!lg) return false;
    lg->is_repeating = repeating;

    int arg_idx = 0;
    float angle = 180.0f;
    if (func->arg_count > 0 && func->args[0]) {
        CssValue* first_arg = func->args[0];
        if (first_arg->type == CSS_VALUE_TYPE_ANGLE ||
            first_arg->type == CSS_VALUE_TYPE_NUMBER ||
            first_arg->type == CSS_VALUE_TYPE_LENGTH) {
            angle = first_arg->type == CSS_VALUE_TYPE_NUMBER
                ? first_arg->data.number.value
                : first_arg->data.length.value;
            arg_idx = 1;
        } else if (first_arg->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum kw = first_arg->data.keyword;
            if (kw == CSS_VALUE_TOP) angle = 0.0f;
            else if (kw == CSS_VALUE_RIGHT) angle = 90.0f;
            else if (kw == CSS_VALUE_BOTTOM) angle = 180.0f;
            else if (kw == CSS_VALUE_LEFT) angle = 270.0f;
            arg_idx = 1;
        } else if (first_arg->type == CSS_VALUE_TYPE_LIST) {
            bool has_top = false, has_bottom = false;
            bool has_left = false, has_right = false;
            for (int li = 0; li < first_arg->data.list.count; li++) {
                CssValue* lv = first_arg->data.list.values[li];
                if (!lv || lv->type != CSS_VALUE_TYPE_KEYWORD) continue;
                CssEnum kw = lv->data.keyword;
                if (kw == CSS_VALUE_TOP) has_top = true;
                else if (kw == CSS_VALUE_BOTTOM) has_bottom = true;
                else if (kw == CSS_VALUE_LEFT) has_left = true;
                else if (kw == CSS_VALUE_RIGHT) has_right = true;
            }
            if (has_top && has_right) angle = 45.0f;
            else if (has_top && has_left) angle = 315.0f;
            else if (has_bottom && has_right) angle = 135.0f;
            else if (has_bottom && has_left) angle = 225.0f;
            else if (has_top) angle = 0.0f;
            else if (has_right) angle = 90.0f;
            else if (has_bottom) angle = 180.0f;
            else if (has_left) angle = 270.0f;
            arg_idx = 1;
        }
    }
    lg->angle = angle;

    int color_count = func->arg_count - arg_idx;
    if (color_count < 2) return false;
    lg->stop_count = color_count * 2;
    lg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * lg->stop_count);
    if (!lg->stops) return false;

    int stop_idx = 0;
    for (int i = arg_idx; i < func->arg_count && stop_idx < lg->stop_count; i++) {
        CssValue* arg = func->args[i];
        if (!arg) continue;
        CssValue* color_value = arg;
        CssValue* first_pos = nullptr;
        CssValue* second_pos = nullptr;
        if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
            color_value = arg->data.list.values[0];
            if (arg->data.list.count >= 2) first_pos = arg->data.list.values[1];
            if (arg->data.list.count >= 3) second_pos = arg->data.list.values[2];
        }

        if (!css_value_is_background_color_candidate(color_value)) continue;
        lg->stops[stop_idx].color = resolve_color_value(lycon, color_value);
        lg->stops[stop_idx].position = -1.0f;
        if (first_pos) {
            if (first_pos->type == CSS_VALUE_TYPE_PERCENTAGE) {
                lg->stops[stop_idx].position = first_pos->data.percentage.value / 100.0f;
            } else if (first_pos->type == CSS_VALUE_TYPE_NUMBER) {
                lg->stops[stop_idx].position = first_pos->data.number.value / 100.0f;
            } else if (first_pos->type == CSS_VALUE_TYPE_LENGTH) {
                lg->stops[stop_idx].position = first_pos->data.length.value;
                lg->stops_in_px = true;
            }
        }
        stop_idx++;

        if (second_pos && stop_idx < lg->stop_count) {
            lg->stops[stop_idx].color = resolve_color_value(lycon, color_value);
            lg->stops[stop_idx].position = -1.0f;
            if (second_pos->type == CSS_VALUE_TYPE_PERCENTAGE) {
                lg->stops[stop_idx].position = second_pos->data.percentage.value / 100.0f;
            } else if (second_pos->type == CSS_VALUE_TYPE_NUMBER) {
                lg->stops[stop_idx].position = second_pos->data.number.value / 100.0f;
            } else if (second_pos->type == CSS_VALUE_TYPE_LENGTH) {
                lg->stops[stop_idx].position = second_pos->data.length.value;
                lg->stops_in_px = true;
            }
            stop_idx++;
        }
    }
    lg->stop_count = stop_idx;
    if (lg->stop_count < 2) return false;

    if (!lg->stops_in_px) {
        css_distribute_missing_gradient_positions(lg->stops, lg->stop_count);
    }

    for (int i = 0; i < lg->stop_count; i++) {
        GradientStop* s = &lg->stops[i];
        if (s->color.a == 0 && s->color.r == 0 && s->color.g == 0 && s->color.b == 0) {
            GradientStop* neighbor = nullptr;
            if (i > 0 && lg->stops[i - 1].color.a > 0) neighbor = &lg->stops[i - 1];
            else if (i + 1 < lg->stop_count && lg->stops[i + 1].color.a > 0) neighbor = &lg->stops[i + 1];
            if (neighbor) {
                s->color.r = neighbor->color.r;
                s->color.g = neighbor->color.g;
                s->color.b = neighbor->color.b;
            }
        }
    }

    *out_gradient = lg;
    return true;
}

static bool resolve_contain_intrinsic_length(LayoutContext* lycon, uintptr_t property,
                                             const CssValue* value, float* out_length) {
    if (!value || !out_length) return false;
    if (resolve_nonnegative_css_length(lycon, property, value, out_length)) return true;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        bool found = false;
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values ? value->data.list.values[i] : nullptr;
            float length = 0.0f;
            if (resolve_nonnegative_css_length(lycon, property, item, &length)) {
                *out_length = length;
                found = true;
            }
        }
        return found;
    }
    return false;
}

static bool css_value_has_identifier(const CssValue* value, const char* identifier) {
    if (css_value_identifier_is(value, identifier)) {
        return true;
    }
    if (!value || value->type != CSS_VALUE_TYPE_LIST) return false;
    for (int i = 0; i < value->data.list.count; i++) {
        CssValue* item = value->data.list.values ? value->data.list.values[i] : nullptr;
        if (css_value_identifier_is(item, identifier)) {
            return true;
        }
    }
    return false;
}

static bool css_contain_value_has_size(const CssValue* value) {
    return css_value_has_identifier(value, "size") ||
        css_value_has_identifier(value, "strict");
}

static bool css_contain_value_has_inline_size(const CssValue* value) {
    return css_value_has_identifier(value, "inline-size");
}

static void resolve_contain_intrinsic_axis(LayoutContext* lycon, ViewBlock* block,
                                           CssPropertyCode property,
                                           const CssValue* value, bool horizontal) {
    if (!block || !value) return;
    float length = -1.0f;
    if (!resolve_contain_intrinsic_length(lycon, property, value, &length)) return;
    ensure_span_block(lycon, block);
    bool is_auto = css_value_has_identifier(value, "auto");
    if (horizontal) {
        block->blk->contain_intrinsic_width = length;
        block->blk->contain_intrinsic_width_auto = is_auto;
    } else {
        block->blk->contain_intrinsic_height = length;
        block->blk->contain_intrinsic_height_auto = is_auto;
    }
}

static void resolve_contain_intrinsic_logical_axis(LayoutContext* lycon,
                                                   ViewBlock* block,
                                                   CssPropertyCode property,
                                                   const CssValue* value,
                                                   bool inline_axis) {
    if (!block || !value) return;
    bool vertical = layout_element_inline_axis_is_vertical(block->as_element());
    resolve_contain_intrinsic_axis(lycon, block, property, value,
                                   inline_axis ? !vertical : vertical);
}

static bool css_content_visibility_value_is_hidden(const CssValue* value) {
    return value && value->type == CSS_VALUE_TYPE_KEYWORD &&
        value->data.keyword == CSS_VALUE_HIDDEN;
}

CssEnum layout_element_css_writing_mode(DomElement* element) {
    for (DomNode* node = element; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* ancestor = node->as_element();
        CssDeclaration* declaration = ancestor && ancestor->specified_style
            ? style_tree_get_declaration(ancestor->specified_style, CSS_PROPERTY_WRITING_MODE)
            : nullptr;
        if (!declaration || !declaration->value ||
            declaration->value->type != CSS_VALUE_TYPE_KEYWORD) {
            continue;
        }
        return declaration->value->data.keyword;
    }
    return CSS_VALUE_HORIZONTAL_TB;
}

WritingMode layout_element_writing_mode(DomElement* element) {
    return layout_writing_mode_from_css(layout_element_css_writing_mode(element));
}

bool layout_element_inline_axis_is_vertical(DomElement* element) {
    WritingMode mode = layout_element_writing_mode(element);
    return mode == WM_VERTICAL_LR || mode == WM_VERTICAL_RL;
}

bool layout_inline_element_is_orthogonal(DomElement* element) {
    if (!element || !element->parent || !element->parent->is_element()) return false;
    WritingMode element_mode = layout_element_writing_mode(element);
    WritingMode parent_mode = layout_element_writing_mode(element->parent->as_element());
    bool element_vertical = element_mode == WM_VERTICAL_LR || element_mode == WM_VERTICAL_RL;
    bool parent_vertical = parent_mode == WM_VERTICAL_LR || parent_mode == WM_VERTICAL_RL;
    return element_vertical != parent_vertical;
}

static CssDeclaration* layout_select_physical_size_alias(DomElement* element,
                                                          CssPropertyCode physical_property,
                                                          CssPropertyCode logical_property) {
    if (!element || !element->specified_style) return nullptr;
    CssDeclaration* physical = style_tree_get_declaration(
        element->specified_style, physical_property);
    CssDeclaration* logical = style_tree_get_declaration(
        element->specified_style, logical_property);
    if (!physical) return logical;
    if (!logical) return physical;

    // Logical and physical aliases address one used axis, so their cascade
    // priorities—not property storage order—select the specified size.
    return css_declaration_cascade_compare(logical, physical) > 0 ? logical : physical;
}

CssDeclaration* layout_specified_physical_size_declaration(DomElement* element,
                                                            bool horizontal) {
    bool inline_axis_is_vertical = layout_element_inline_axis_is_vertical(element);
    CssPropertyCode physical_property = horizontal ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
    CssPropertyCode logical_property = (horizontal != inline_axis_is_vertical)
        ? CSS_PROPERTY_INLINE_SIZE : CSS_PROPERTY_BLOCK_SIZE;
    return layout_select_physical_size_alias(element, physical_property, logical_property);
}

CssDeclaration* layout_specified_physical_minmax_size_declaration(DomElement* element,
                                                                   bool horizontal,
                                                                   bool minimum) {
    bool inline_axis_is_vertical = layout_element_inline_axis_is_vertical(element);
    CssPropertyCode physical_property;
    CssPropertyCode logical_property;
    if (minimum) {
        physical_property = horizontal ? CSS_PROPERTY_MIN_WIDTH : CSS_PROPERTY_MIN_HEIGHT;
        logical_property = (horizontal != inline_axis_is_vertical)
            ? CSS_PROPERTY_MIN_INLINE_SIZE : CSS_PROPERTY_MIN_BLOCK_SIZE;
    } else {
        physical_property = horizontal ? CSS_PROPERTY_MAX_WIDTH : CSS_PROPERTY_MAX_HEIGHT;
        logical_property = (horizontal != inline_axis_is_vertical)
            ? CSS_PROPERTY_MAX_INLINE_SIZE : CSS_PROPERTY_MAX_BLOCK_SIZE;
    }
    return layout_select_physical_size_alias(element, physical_property, logical_property);
}

static bool resolve_contain_intrinsic_size_value(LayoutContext* lycon, const CssValue* value,
                                                 float* out_width, float* out_height,
                                                 bool* out_auto_width, bool* out_auto_height) {
    if (!value || !out_width || !out_height || !out_auto_width || !out_auto_height) return false;
    *out_width = -1.0f;
    *out_height = -1.0f;
    *out_auto_width = false;
    *out_auto_height = false;

    if (value->type != CSS_VALUE_TYPE_LIST) {
        resolve_contain_intrinsic_length(lycon, CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE,
                                         value, out_width);
        *out_height = *out_width;
        return *out_width >= 0.0f || *out_height >= 0.0f;
    }

    int count = value->data.list.count;
    CssValue** values = value->data.list.values;
    if (count <= 0 || !values) return false;

    float component_size[2] = {-1.0f, -1.0f};
    bool component_auto[2] = {false, false};
    int component_count = 0;
    int index = 0;
    while (index < count && component_count < 2) {
        bool is_auto = false;
        CssValue* item = values[index];
        if (css_value_identifier_is(item, "auto")) {
            is_auto = true;
            index++;
            if (index >= count) break;
            item = values[index];
        }

        if (css_value_identifier_is(item, "none")) {
            // `none` still occupies its grammar slot, so the next fallback
            // belongs to the other axis rather than replacing this component.
            component_count++;
            index++;
            continue;
        }

        float length = -1.0f;
        if (!resolve_contain_intrinsic_length(lycon, CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE,
                                              item, &length)) {
            index++;
            continue;
        }
        component_size[component_count] = length;
        component_auto[component_count] = is_auto;
        component_count++;
        index++;
    }

    if (component_count > 0) {
        *out_width = component_size[0];
        *out_auto_width = component_auto[0] && *out_width >= 0.0f;
    }
    if (component_count > 1) {
        *out_height = component_size[1];
        *out_auto_height = component_auto[1] && *out_height >= 0.0f;
    } else if (component_count == 1) {
        *out_height = *out_width;
        *out_auto_height = *out_auto_width;
    }
    return *out_width >= 0.0f || *out_height >= 0.0f;
}

bool layout_resolve_contain_intrinsic_size(LayoutContext* lycon, DomElement* element,
                                           float* out_width, float* out_height) {
    if (!out_width || !out_height) return false;
    *out_width = -1.0f;
    *out_height = -1.0f;
    if (!lycon || !element || !element->specified_style) return false;

    ViewBlock* block = lam::unsafe_view_block_element_storage(element);
    CssDeclaration* content_visibility_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_CONTENT_VISIBILITY);
    bool content_visibility_hidden = (block && block->blk &&
        block->block()->content_visibility_hidden) ||
        (content_visibility_decl &&
         css_content_visibility_value_is_hidden(content_visibility_decl->value));
    CssDeclaration* contain_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_CONTAIN);
    bool contains_size = content_visibility_hidden ||
        (contain_decl && css_contain_value_has_size(contain_decl->value));
    bool contains_inline_size = contain_decl &&
        css_contain_value_has_inline_size(contain_decl->value);
    if (!contains_size && !contains_inline_size) return false;

    CssDeclaration* size_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE);
    if (size_decl) {
        bool auto_width = false;
        bool auto_height = false;
        resolve_contain_intrinsic_size_value(lycon, size_decl->value,
                                             out_width, out_height,
                                             &auto_width, &auto_height);
    }

    CssDeclaration* width_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH);
    if (width_decl && (!size_decl || width_decl->source_order > size_decl->source_order)) {
        resolve_contain_intrinsic_length(lycon, CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH,
                                         width_decl->value, out_width);
    }
    CssDeclaration* height_decl = style_tree_get_declaration(
        element->specified_style, CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT);
    if (height_decl && (!size_decl || height_decl->source_order > size_decl->source_order)) {
        resolve_contain_intrinsic_length(lycon, CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT,
                                         height_decl->value, out_height);
    }
    if (!contains_size) {
        if (layout_element_inline_axis_is_vertical(element)) {
            *out_width = -1.0f;
        } else {
            *out_height = -1.0f;
        }
    }
    return *out_width >= 0.0f || *out_height >= 0.0f;
}

static bool parse_object_position_component(LayoutContext* lycon, const CssValue* value,
                                            float* out_value, bool* out_is_percent,
                                            int* out_axis) {
    if (!value || !out_value || !out_is_percent || !out_axis) return false;
    *out_axis = 0;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *out_value = (float)value->data.percentage.value;
        *out_is_percent = true;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
        float length = resolve_length_value(lycon, CSS_PROPERTY_OBJECT_POSITION, value);
        if (isnan(length)) return false;
        *out_value = length;
        *out_is_percent = false;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum kw = value->data.keyword;
        if (kw == CSS_VALUE_LEFT) {
            *out_value = 0.0f; *out_is_percent = true; *out_axis = 1; return true;
        }
        if (kw == CSS_VALUE_RIGHT) {
            *out_value = 100.0f; *out_is_percent = true; *out_axis = 1; return true;
        }
        if (kw == CSS_VALUE_TOP) {
            *out_value = 0.0f; *out_is_percent = true; *out_axis = 2; return true;
        }
        if (kw == CSS_VALUE_BOTTOM) {
            *out_value = 100.0f; *out_is_percent = true; *out_axis = 2; return true;
        }
        if (kw == CSS_VALUE_CENTER) {
            *out_value = 50.0f; *out_is_percent = true; *out_axis = 0; return true;
        }
    }
    return false;
}

static bool css_text_has_top_level_comma(const char* text, size_t len) {
    if (!text) return false;

    int paren_depth = 0;
    char quote = '\0';
    bool escaping = false;
    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (quote) {
            if (escaping) {
                escaping = false;
            } else if (ch == '\\') {
                escaping = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '(') {
            paren_depth++;
        } else if (ch == ')') {
            if (paren_depth > 0) paren_depth--;
        } else if (ch == ',' && paren_depth == 0) {
            return true;
        }
    }
    return false;
}

static void resolve_background_url_function(LayoutContext* lycon, const CssDeclaration* decl, const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name || !str_ieq_const(value->data.function->name, strlen(value->data.function->name), "url")) {
        return;
    }

    lam::CssTempDecl img_decl(decl, CSS_PROPERTY_BACKGROUND_IMAGE, (CssValue*)value);
    img_decl.resolve(lycon);
}

static const CssValue* css_find_background_function(const CssValue* value,
                                                    const char* const* names,
                                                    int name_count) {
    if (!value || !names || name_count <= 0) return nullptr;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name) {
        const char* fn = value->data.function->name;
        size_t fn_len = strlen(fn);
        for (int i = 0; i < name_count; i++) {
            if (names[i] && str_ieq_const(fn, fn_len, names[i])) {
                return value;
            }
        }
    }
    if (value->type != CSS_VALUE_TYPE_LIST || !value->data.list.values) return nullptr;
    for (int i = 0; i < value->data.list.count; i++) {
        const CssValue* found = css_find_background_function(value->data.list.values[i],
                                                            names, name_count);
        if (found) return found;
    }
    return nullptr;
}

static const CssValue* css_find_background_url_layer(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_STRING) {
        return value;
    }
    static const char* const names[] = {"url"};
    const CssValue* found = css_find_background_function(value, names, 1);
    if (found || value->type != CSS_VALUE_TYPE_LIST || !value->data.list.values) {
        return found;
    }
    for (int i = 0; i < value->data.list.count; i++) {
        found = css_find_background_url_layer(value->data.list.values[i]);
        if (found) return found;
    }
    return nullptr;
}

static const CssValue* css_find_background_linear_gradient_layer(const CssValue* value) {
    static const char* const names[] = {"linear-gradient", "repeating-linear-gradient"};
    return css_find_background_function(value, names, 2);
}

static const CssValue* css_find_background_radial_gradient_layer(const CssValue* value) {
    static const char* const names[] = {"radial-gradient", "repeating-radial-gradient"};
    return css_find_background_function(value, names, 2);
}

static const CssValue* css_find_background_conic_gradient_layer(const CssValue* value) {
    static const char* const names[] = {"conic-gradient", "repeating-conic-gradient"};
    return css_find_background_function(value, names, 2);
}

static bool css_mask_value_length(const CssValue* value, float* out, bool* is_percent) {
    if (!value || !out || !is_percent) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        *out = (float)value->data.length.value;
        *is_percent = false;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *out = (float)(value->data.percentage.value / 100.0);
        *is_percent = true;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        *out = (float)value->data.number.value;
        *is_percent = false;
        return true;
    }
    return false;
}

static bool css_mask_stop_radius(const CssValue* value, float* out, bool* is_percent) {
    if (!value || !out || !is_percent) return false;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = value->data.list.count - 1; i >= 1; i--) {
            if (css_mask_value_length(value->data.list.values[i], out, is_percent)) {
                return true;
            }
        }
        return false;
    }
    return css_mask_value_length(value, out, is_percent);
}

static void resolve_css_mask_image(LayoutContext* lycon, ViewSpan* span,
                                   const CssValue* value) {
    if (!lycon || !span || !value) return;
    ensure_span_bound(lycon, span);
    if (!span->boundary()->mask) {
        span->bound->mask = (MaskProp*)alloc_prop(lycon, sizeof(MaskProp));
    }
    MaskProp* mask = span->boundary()->mask;
    memset(mask, 0, sizeof(MaskProp));

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        return;
    }
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name ||
        strcmp(value->data.function->name, "radial-gradient") != 0) {
        return;
    }

    CssFunction* func = value->data.function;
    mask->has_radial_gradient = true;
    mask->cx = 0.5f;
    mask->cy = 0.5f;
    mask->radius = 0.5f;
    mask->radius_is_percent = true;

    int arg_idx = 0;
    if (func->arg_count > 0 && func->args[0] &&
        func->args[0]->type == CSS_VALUE_TYPE_LIST) {
        CssValue* first = func->args[0];
        int at_idx = -1;
        for (int i = 0; i < first->data.list.count; i++) {
            CssValue* item = first->data.list.values[i];
            if (!item) continue;
            if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                if (item->data.keyword == CSS_VALUE_LEFT) mask->cx = 0.0f;
                else if (item->data.keyword == CSS_VALUE_RIGHT) mask->cx = 1.0f;
                else if (item->data.keyword == CSS_VALUE_TOP) mask->cy = 0.0f;
                else if (item->data.keyword == CSS_VALUE_BOTTOM) mask->cy = 1.0f;
                else if (item->data.keyword == CSS_VALUE_CENTER) {
                    // center is the default for both axes.
                }

                const CssEnumInfo* info = css_enum_info(item->data.keyword);
                if (info && info->name && strcmp(info->name, "at") == 0) {
                    at_idx = i;
                }
            } else if (at_idx >= 0 && item->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (i == at_idx + 1) mask->cx = (float)(item->data.percentage.value / 100.0);
                else if (i == at_idx + 2) mask->cy = (float)(item->data.percentage.value / 100.0);
            } else if (at_idx >= 0 && item->type == CSS_VALUE_TYPE_LENGTH) {
                // length positions need layout dimensions; keep center for now.
            }
        }
        arg_idx = 1;
    }

    float last_opaque = -1.0f;
    float first_transparent = -1.0f;
    bool last_opaque_pct = false;
    bool first_transparent_pct = false;
    for (int i = arg_idx; i < func->arg_count; i++) {
        CssValue* arg = func->args[i];
        if (!arg) continue;
        Color c = Color{};
        bool has_color = false;
        if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count > 0) {
            c = resolve_color_value(lycon, arg->data.list.values[0]);
            has_color = true;
        } else if (arg->type == CSS_VALUE_TYPE_COLOR || arg->type == CSS_VALUE_TYPE_KEYWORD ||
                   arg->type == CSS_VALUE_TYPE_FUNCTION) {
            c = resolve_color_value(lycon, arg);
            has_color = true;
        }
        if (!has_color) continue;

        float pos = 0.0f;
        bool pos_pct = false;
        if (!css_mask_stop_radius(arg, &pos, &pos_pct)) continue;
        if (c.a > 0) {
            last_opaque = pos;
            last_opaque_pct = pos_pct;
        } else if (first_transparent < 0.0f) {
            first_transparent = pos;
            first_transparent_pct = pos_pct;
        }
    }

    if (last_opaque >= 0.0f && first_transparent >= 0.0f &&
        last_opaque_pct == first_transparent_pct) {
        mask->radius = (last_opaque + first_transparent) * 0.5f;
        mask->radius_is_percent = last_opaque_pct;
    } else if (last_opaque >= 0.0f) {
        mask->radius = last_opaque;
        mask->radius_is_percent = last_opaque_pct;
    }
}

static bool css_value_is_background_color_candidate(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_COLOR) return true;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
        const char* name = value->data.function->name;
        size_t name_len = strlen(name);
        return str_ieq_const(name, name_len, "rgb") || str_ieq_const(name, name_len, "rgba") ||
               str_ieq_const(name, name_len, "hsl") || str_ieq_const(name, name_len, "hsla");
    }
    if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;

    const CssEnumInfo* info = css_enum_info(value->data.keyword);
    return info && info->group == CSS_VALUE_GROUP_COLOR;
}

static bool css_background_layer_has_plain_color(const CssValue* value) {
    if (!value) return false;
    if (css_value_is_background_color_candidate(value)) return true;
    if (value->type != CSS_VALUE_TYPE_LIST) return false;

    for (int i = 0; i < value->data.list.count; i++) {
        CssValue* item = value->data.list.values[i];
        if (css_background_layer_has_plain_color(item)) return true;
    }
    return false;
}

static bool css_value_is_background_position_candidate(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) return true;
    if (value->type != CSS_VALUE_TYPE_KEYWORD) return false;
    CssEnum keyword = value->data.keyword;
    return keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT || keyword == CSS_VALUE_TOP ||
           keyword == CSS_VALUE_BOTTOM || keyword == CSS_VALUE_CENTER;
}

static bool css_url_has_scheme(const char* url) {
    if (!url) return false;
    const char* p = url;
    while (*p) {
        if (*p == ':') return p != url;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.')) {
            return false;
        }
        p++;
    }
    return false;
}

char* resolve_css_resource_url(LayoutContext* lycon, const CssDeclaration* decl, const char* url) {
    if (!lycon || !url) return nullptr;

    size_t url_len = strlen(url);
    const char* source_file = decl ? decl->source_file : nullptr;
    bool already_resolved = url[0] == '/' || (url[0] == '/' && url[1] == '/') ||
        strncmp(url, "data:", 5) == 0 || css_url_has_scheme(url);
    bool has_stylesheet_base = source_file && source_file[0] && strcmp(source_file, "<inline-style>") != 0;

    if (!already_resolved && has_stylesheet_base) {
        const char* slash = strrchr(source_file, '/');
        if (slash) {
            size_t dir_len = slash - source_file + 1;
            char* resolved = (char*)alloc_prop(lycon, dir_len + url_len + 1);
            if (!resolved) return nullptr;
            memcpy(resolved, source_file, dir_len);
            memcpy(resolved + dir_len, url, url_len);
            resolved[dir_len + url_len] = '\0';
            return resolved;
        }
    }

    char* copy = (char*)alloc_prop(lycon, url_len + 1);
    if (!copy) return nullptr;
    str_copy(copy, url_len + 1, url, url_len);
    return copy;
}

static bool css_line_decoration_style(CssEnum keyword) {
    return keyword == CSS_VALUE_SOLID || keyword == CSS_VALUE_DOTTED ||
        keyword == CSS_VALUE_DASHED || keyword == CSS_VALUE_DOUBLE ||
        keyword == CSS_VALUE_GROOVE || keyword == CSS_VALUE_RIDGE ||
        keyword == CSS_VALUE_INSET || keyword == CSS_VALUE_OUTSET ||
        keyword == CSS_VALUE_NONE;
}

static void resolve_css_line_decoration_component(LayoutContext* lycon,
                                                  CssPropertyCode prop_id,
                                                  const CssValue* value,
                                                  float* width,
                                                  CssEnum* style,
                                                  Color* color) {
    if (!value || !width || !style || !color) return;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (css_line_decoration_style(keyword)) {
            *style = keyword;
        // width keywords must be classified before named colors in every line-decoration shorthand.
        } else if (keyword == CSS_VALUE_THIN || keyword == CSS_VALUE_MEDIUM ||
                   keyword == CSS_VALUE_THICK) {
            *width = keyword == CSS_VALUE_THIN ? 1.0f :
                (keyword == CSS_VALUE_MEDIUM ? 3.0f : 5.0f);
        } else {
            *color = color_name_to_rgb(keyword);
        }
    } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
               value->type == CSS_VALUE_TYPE_NUMBER) {
        *width = resolve_length_value(lycon, prop_id, value);
    } else if (value->type == CSS_VALUE_TYPE_COLOR ||
               value->type == CSS_VALUE_TYPE_FUNCTION) {
        *color = resolve_color_value(lycon, value);
    }
}

static bool parse_border_radius_component(LayoutContext* lycon, int prop_id, const CssValue* value,
                                          float* out_radius, bool* out_percent) {
    if (!value || !out_radius || !out_percent) return false;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *out_radius = (float)value->data.percentage.value;
        *out_percent = true;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
        *out_radius = resolve_length_value(lycon, prop_id, value);
        *out_percent = false;
        return true;
    }
    return false;
}

static bool expand_border_radius_values(LayoutContext* lycon, int prop_id, CssValue** values, int count,
                                        float out_radius[4], bool out_percent[4]) {
    if (!values || count <= 0 || count > 4) return false;

    float parsed[4] = {0, 0, 0, 0};
    bool parsed_percent[4] = {false, false, false, false};
    for (int i = 0; i < count; i++) {
        if (!parse_border_radius_component(lycon, prop_id, values[i], &parsed[i], &parsed_percent[i])) {
            return false;
        }
    }

    static const uint8_t expansion[4][4] = {
        {0, 0, 0, 0},
        {0, 1, 0, 1},
        {0, 1, 2, 1},
        {0, 1, 2, 3},
    };
    for (int corner = 0; corner < 4; corner++) {
        uint8_t source = expansion[count - 1][corner];
        out_radius[corner] = parsed[source];
        out_percent[corner] = parsed_percent[source];
    }
    return true;
}

static void set_corner_radius_values(Corner* radius, int corner_index,
                                     float radius_x, bool percent_x,
                                     float radius_y, bool percent_y,
                                     int64_t specificity) {
    switch (corner_index) {
        case 0:
            radius->top_left = radius_x;
            radius->top_left_y = radius_y;
            radius->tl_percent = percent_x;
            radius->tl_percent_y = percent_y;
            radius->tl_specificity = specificity;
            break;
        case 1:
            radius->top_right = radius_x;
            radius->top_right_y = radius_y;
            radius->tr_percent = percent_x;
            radius->tr_percent_y = percent_y;
            radius->tr_specificity = specificity;
            break;
        case 2:
            radius->bottom_right = radius_x;
            radius->bottom_right_y = radius_y;
            radius->br_percent = percent_x;
            radius->br_percent_y = percent_y;
            radius->br_specificity = specificity;
            break;
        case 3:
            radius->bottom_left = radius_x;
            radius->bottom_left_y = radius_y;
            radius->bl_percent = percent_x;
            radius->bl_percent_y = percent_y;
            radius->bl_specificity = specificity;
            break;
    }
}

static bool apply_border_radius_shorthand(LayoutContext* lycon, int prop_id, Corner* radius,
                                          const CssValue* value, int64_t specificity) {
    if (!value || !radius) return false;

    CssValue* horiz_values[4] = {nullptr, nullptr, nullptr, nullptr};
    CssValue* vert_values[4] = {nullptr, nullptr, nullptr, nullptr};
    int horiz_count = 0;
    int vert_count = 0;

    if (value->type == CSS_VALUE_TYPE_LIST) {
        bool seen_slash = false;
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item) continue;
            if (is_border_radius_slash(item)) {
                if (seen_slash) return false;
                seen_slash = true;
                continue;
            }
            if (!seen_slash) {
                if (horiz_count >= 4) return false;
                horiz_values[horiz_count++] = item;
            } else {
                if (vert_count >= 4) return false;
                vert_values[vert_count++] = item;
            }
        }
        if (horiz_count == 0) return false;
        if (vert_count == 0) {
            vert_count = horiz_count;
            for (int i = 0; i < horiz_count; i++) vert_values[i] = horiz_values[i];
        }
    } else {
        horiz_values[0] = (CssValue*)value;
        vert_values[0] = (CssValue*)value;
        horiz_count = 1;
        vert_count = 1;
    }

    float radius_x[4], radius_y[4];
    bool percent_x[4], percent_y[4];
    if (!expand_border_radius_values(lycon, prop_id, horiz_values, horiz_count, radius_x, percent_x)) return false;
    if (!expand_border_radius_values(lycon, prop_id, vert_values, vert_count, radius_y, percent_y)) return false;

    int64_t current_specificity[4] = {
        radius->tl_specificity,
        radius->tr_specificity,
        radius->br_specificity,
        radius->bl_specificity
    };
    for (int i = 0; i < 4; i++) {
        if (specificity >= current_specificity[i]) {
            set_corner_radius_values(radius, i, radius_x[i], percent_x[i], radius_y[i], percent_y[i], specificity);
        }
    }
    return true;
}

static bool apply_corner_radius_value(LayoutContext* lycon, int prop_id, Corner* radius,
                                      int corner_index, const CssValue* value, int64_t specificity) {
    CssValue* values[2] = {nullptr, nullptr};
    int count = 0;

    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item || is_border_radius_slash(item)) continue;
            if (count >= 2) return false;
            values[count++] = item;
        }
    } else {
        values[count++] = (CssValue*)value;
    }
    if (count <= 0) return false;

    float radius_x = 0, radius_y = 0;
    bool percent_x = false, percent_y = false;
    if (!parse_border_radius_component(lycon, prop_id, values[0], &radius_x, &percent_x)) return false;
    if (count == 2) {
        if (!parse_border_radius_component(lycon, prop_id, values[1], &radius_y, &percent_y)) return false;
    } else {
        radius_y = radius_x;
        percent_y = percent_x;
    }

    int64_t current_specificity = 0;
    switch (corner_index) {
        case 0: current_specificity = radius->tl_specificity; break;
        case 1: current_specificity = radius->tr_specificity; break;
        case 2: current_specificity = radius->br_specificity; break;
        case 3: current_specificity = radius->bl_specificity; break;
    }
    if (specificity >= current_specificity) {
        set_corner_radius_values(radius, corner_index, radius_x, percent_x, radius_y, percent_y, specificity);
    }
    return true;
}

static bool css_var_stack_contains(const char** var_stack, int stack_count, const char* var_name) {
    if (!var_stack || !var_name) return false;
    for (int i = 0; i < stack_count; i++) {
        if (var_stack[i] && strcmp(var_stack[i], var_name) == 0) return true;
    }
    return false;
}

static const char* css_var_function_name(const CssFunction* func) {
    if (!func || !func->args || func->arg_count < 1 || !func->args[0]) return nullptr;
    CssValue* first_arg = func->args[0];
    if (first_arg->type == CSS_VALUE_TYPE_CUSTOM) {
        return first_arg->data.custom_property.name;
    }
    return first_arg->type == CSS_VALUE_TYPE_STRING ? first_arg->data.string : nullptr;
}

static const CssValue* resolve_var_function_inner(LayoutContext* lycon, const CssValue* value,
                                                  const char** var_stack, int stack_count) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION) {
        return value;  // Not a function, return as-is
    }

    const CssFunction* func = value->data.function;
    if (!func || !func->name || strcmp(func->name, "var") != 0) {
        return value;  // Not a var() function, return as-is
    }
    auto resolve_fallback = [&]() -> const CssValue* {
        return func->arg_count >= 2 && func->args[1]
            ? resolve_var_function_inner(lycon, func->args[1], var_stack, stack_count)
            : nullptr;
    };

    const char* var_name = css_var_function_name(func);

    if (!var_name) {
        return resolve_fallback();
    }

    // Custom properties can legally form cycles on real pages; a cycle makes
    // the substituted value invalid, but Radiant must not recurse forever.
    if (stack_count >= 32 || css_var_stack_contains(var_stack, stack_count, var_name)) {
        return resolve_fallback();
    }

    // Look up the variable
    const CssValue* var_value = lookup_css_variable(lycon, var_name);
    if (var_value) {
        // Recursively resolve in case the variable value is also a var()
        const char* next_stack[32];
        for (int i = 0; i < stack_count; i++) next_stack[i] = var_stack[i];
        next_stack[stack_count] = var_name;
        const CssValue* resolved = resolve_var_function_inner(lycon, var_value, next_stack, stack_count + 1);
        if (resolved) return resolved;
        return resolve_fallback();
    }

    return resolve_fallback();
}

// Helper: resolve var() function to get the actual CSS value
// Returns the resolved value, or the original value if not a var() function
// Recursively resolves nested var() calls
const CssValue* resolve_var_function(LayoutContext* lycon, const CssValue* value) {
    const char* var_stack[32];
    return resolve_var_function_inner(lycon, value, var_stack, 0);
}

// Helper: extract a numeric value from a CssValue (number, percentage, length)
// For colors, percentages are relative to 255 (for RGB) or 1.0 (for alpha)
static double resolve_color_component(const CssValue* v, bool is_alpha = false) {
    if (!v) return 0.0;
    switch (v->type) {
    case CSS_VALUE_TYPE_NUMBER:
        return v->data.number.value;
    case CSS_VALUE_TYPE_PERCENTAGE:
        // For alpha, 100% = 1.0; for RGB, 100% = 255
        return is_alpha ? v->data.percentage.value / 100.0 : (v->data.percentage.value * 255.0 / 100.0);
    case CSS_VALUE_TYPE_LENGTH:
        return v->data.length.value;
    default:
        return 0.0;
    }
}

static uint8_t css_color_byte(double value) {
    return (uint8_t)(value < 0.0 ? 0.0 : (value > 255.0 ? 255.0 : value));
}

// CSS Color Level 4 §4.2.4: Convert HSL to RGB
// h: hue in degrees [0,360), s: saturation [0,1], l: lightness [0,1]
static Color hsl_to_rgb(float h, float s, float l, float a) {
    // normalize hue to [0,360)
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;

    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;

    float r1, g1, b1;
    if (h < 60)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }

    Color result;
    result.r = (uint8_t)((r1 + m) * 255.0f + 0.5f);
    result.g = (uint8_t)((g1 + m) * 255.0f + 0.5f);
    result.b = (uint8_t)((b1 + m) * 255.0f + 0.5f);
    result.a = (uint8_t)(a * 255.0f + 0.5f);
    return result;
}

static Color get_current_color(LayoutContext* lycon);

Color resolve_color_value(LayoutContext* lycon, const CssValue* value) {
    Color result;
    result.r = 0;
    result.g = 0;
    result.b = 0;
    result.a = 255; // default black, opaque

    if (!value) return result;

    // Resolve var() if present
    value = resolve_var_function(lycon, value);
    if (!value) return result;

    switch (value->type) {
    case CSS_VALUE_TYPE_COLOR: {
        // Access color data from CssValue anonymous struct
        switch (value->data.color.type) {
        case CSS_COLOR_HEX:  case CSS_COLOR_RGB:
            result = value->data.color.data.color;
            break;
        case CSS_COLOR_HSL: {
            // HSL stored as color with h in r (scaled), s in g, l in b
            Color hsl = value->data.color.data.color;
            result = hsl_to_rgb((float)hsl.r * 360.0f / 255.0f,
                                (float)hsl.g / 255.0f,
                                (float)hsl.b / 255.0f,
                                (float)hsl.a / 255.0f);
            break;
        }
        case CSS_COLOR_CURRENTCOLOR:
            result = get_current_color(lycon);
            break;
        case CSS_COLOR_TRANSPARENT:
            result = (Color){ .r = 0, .g = 0, .b = 0, .a = 0 };
            break;
        default:
            break;
        }
        break;
    }
    case CSS_VALUE_TYPE_KEYWORD: {
        if (value->data.keyword == CSS_VALUE_CURRENTCOLOR) {
            result = get_current_color(lycon);
        } else {
            result = color_name_to_rgb(value->data.keyword);
        }
        break;
    }
    case CSS_VALUE_TYPE_FUNCTION: {
        // Handle rgb(), rgba(), hsl(), hsla() color functions
        const CssFunction* func = value->data.function;
        if (!func || !func->name) break;


        // rgb() and rgba() functions
        // Modern syntax: rgb(r g b) or rgb(r g b / alpha) - parsed as 1 arg (list)
        // Legacy syntax: rgb(r, g, b) or rgba(r, g, b, a) - parsed as 3-4 args
        if (str_ieq_const(func->name, strlen(func->name), "rgb") || str_ieq_const(func->name, strlen(func->name), "rgba")) {
            // Check for modern syntax: single list argument with space-separated values
            if (func->arg_count == 1 && func->args[0] && func->args[0]->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = func->args[0];
                // Modern syntax: list contains R, G, B [, '/', alpha]
                // Find numeric values (skip '/' delimiter if present)
                double r = 0, g = 0, b = 0, a = 255;
                int num_idx = 0;
                bool found_slash = false;

                for (int i = 0; i < list->data.list.count && num_idx < 4; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;

                    // Check for '/' delimiter (CUSTOM type with "/" or DELIM)
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        found_slash = true;
                        continue;
                    }
                    // Skip var() functions (for opacity like var(--tw-text-opacity))
                    if (v->type == CSS_VALUE_TYPE_FUNCTION || v->type == CSS_VALUE_TYPE_VAR) {
                        // If we've seen slash, this is alpha - default to 1 (fully opaque)
                        if (found_slash && num_idx == 3) {
                            a = 255;  // default opaque
                        }
                        continue;
                    }

                    double val = resolve_color_component(v, found_slash);
                    if (num_idx == 0) r = val;
                    else if (num_idx == 1) g = val;
                    else if (num_idx == 2) b = val;
                    else if (num_idx == 3) {
                        // Alpha value
                        if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            a = val * 255.0;  // 0-1 scale
                        } else {
                            a = val;  // percentage already converted
                        }
                    }
                    num_idx++;
                }

                // Clamp to valid range
                result.r = css_color_byte(r);
                result.g = css_color_byte(g);
                result.b = css_color_byte(b);
                result.a = css_color_byte(a);

            }
            else if (func->arg_count >= 3) {
                // Legacy syntax: separate arguments
                double r = resolve_color_component(func->args[0]);
                double g = resolve_color_component(func->args[1]);
                double b = resolve_color_component(func->args[2]);

                // Clamp to valid range
                result.r = css_color_byte(r);
                result.g = css_color_byte(g);
                result.b = css_color_byte(b);

                // Check for alpha value
                if (func->arg_count >= 4) {
                    double a = resolve_color_component(func->args[3], true);
                    // Alpha can be a number (0-1) or percentage
                    if (func->args[3] && func->args[3]->type == CSS_VALUE_TYPE_NUMBER) {
                        // Number format: 0 to 1
                        a = a * 255.0;
                    }
                    result.a = css_color_byte(a);
                }

            }
        }
        // hsl() and hsla() functions
        else if (str_ieq_const(func->name, strlen(func->name), "hsl") || str_ieq_const(func->name, strlen(func->name), "hsla")) {
            // hsl(h, s%, l%) or hsl(h s% l% / alpha) — same modern/legacy pattern as rgb()
            double h = 0, s = 0, l = 0, a = 1.0;

            if (func->arg_count == 1 && func->args[0] && func->args[0]->type == CSS_VALUE_TYPE_LIST) {
                // modern space-separated syntax
                const CssValue* list = func->args[0];
                int num_idx = 0;

                for (int i = 0; i < list->data.list.count && num_idx < 4; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        continue;
                    }
                    if (v->type == CSS_VALUE_TYPE_FUNCTION || v->type == CSS_VALUE_TYPE_VAR) continue;

                    double val = 0;
                    if (v->type == CSS_VALUE_TYPE_NUMBER) val = v->data.number.value;
                    else if (v->type == CSS_VALUE_TYPE_PERCENTAGE) val = v->data.percentage.value;
                    else if (v->type == CSS_VALUE_TYPE_LENGTH) val = v->data.length.value;

                    if (num_idx == 0) h = val;                    // hue in degrees
                    else if (num_idx == 1) s = val / 100.0;       // saturation percentage
                    else if (num_idx == 2) l = val / 100.0;       // lightness percentage
                    else if (num_idx == 3) {
                        a = (v->type == CSS_VALUE_TYPE_PERCENTAGE) ? val / 100.0 : val;
                    }
                    num_idx++;
                }
            } else if (func->arg_count >= 3) {
                // legacy comma-separated syntax: hsl(h, s%, l%) or hsla(h, s%, l%, a)
                if (func->args[0]) {
                    if (func->args[0]->type == CSS_VALUE_TYPE_NUMBER) h = func->args[0]->data.number.value;
                    else if (func->args[0]->type == CSS_VALUE_TYPE_LENGTH) h = func->args[0]->data.length.value;
                }
                if (func->args[1] && func->args[1]->type == CSS_VALUE_TYPE_PERCENTAGE)
                    s = func->args[1]->data.percentage.value / 100.0;
                if (func->args[2] && func->args[2]->type == CSS_VALUE_TYPE_PERCENTAGE)
                    l = func->args[2]->data.percentage.value / 100.0;
                if (func->arg_count >= 4 && func->args[3]) {
                    if (func->args[3]->type == CSS_VALUE_TYPE_NUMBER)
                        a = func->args[3]->data.number.value;
                    else if (func->args[3]->type == CSS_VALUE_TYPE_PERCENTAGE)
                        a = func->args[3]->data.percentage.value / 100.0;
                }
            }

            // clamp
            if (s < 0) s = 0; if (s > 1) s = 1;
            if (l < 0) l = 0; if (l > 1) l = 1;
            if (a < 0) a = 0; if (a > 1) a = 1;

            result = hsl_to_rgb((float)h, (float)s, (float)l, (float)a);
        }
        break;
    }
    default:
        break;
    }
    return result;
}

// Get the CSS currentColor value for the element being styled.
// Since 'color' may not be resolved yet on the current element (border properties
// are processed before color in AVL tree order), walk up the parent chain.
static Color get_current_color(LayoutContext* lycon) {
    ViewSpan* span = lam::view_as_element(lycon->view);
    if (span && span->in_line && span->inl()->has_color) {
        return span->inl()->color;
    }
    DomNode* p = span ? span->parent : nullptr;
    while (p) {
        if (p->is_element()) {
            DomElement* pe = lam::dom_require<DOM_NODE_ELEMENT>(p);
            if (pe->in_line && pe->inl()->has_color) {
                return pe->inl()->color;
            }
        }
        p = p->parent;
    }
    return Color{0xFF000000};
}

// ============================================================================
// Keyword Mapping Functions
// ============================================================================

// CSS4 has a total of 148 colors
Color color_name_to_rgb(CssEnum color_name) {
    // Handle transparent specially - it has alpha = 0
    if (color_name == CSS_VALUE_TRANSPARENT) {
        return (Color){ .r = 0, .g = 0, .b = 0, .a = 0 };
    }

    uint32_t c;
    switch (color_name) {
        case CSS_VALUE_ALICEBLUE: c = 0xF0F8FF;  break;
        case CSS_VALUE_ANTIQUEWHITE: c = 0xFAEBD7;  break;
        case CSS_VALUE_AQUA: c = 0x00FFFF;  break;
        case CSS_VALUE_AQUAMARINE: c = 0x7FFFD4;  break;
        case CSS_VALUE_AZURE: c = 0xF0FFFF;  break;
        case CSS_VALUE_BEIGE: c = 0xF5F5DC;  break;
        case CSS_VALUE_BISQUE: c = 0xFFE4C4;  break;
        case CSS_VALUE_BLACK: c = 0x000000;  break;
        case CSS_VALUE_BLANCHEDALMOND: c = 0xFFEBCD;  break;
        case CSS_VALUE_BLUE: c = 0x0000FF;  break;
        case CSS_VALUE_BLUEVIOLET: c = 0x8A2BE2;  break;
        case CSS_VALUE_BROWN: c = 0xA52A2A;  break;
        case CSS_VALUE_BURLYWOOD: c = 0xDEB887;  break;
        case CSS_VALUE_CADETBLUE: c = 0x5F9EA0;  break;
        case CSS_VALUE_CHARTREUSE: c = 0x7FFF00;  break;
        case CSS_VALUE_CHOCOLATE: c = 0xD2691E;  break;
        case CSS_VALUE_CORAL: c = 0xFF7F50;  break;
        case CSS_VALUE_CORNFLOWERBLUE: c = 0x6495ED;  break;
        case CSS_VALUE_CORNSILK: c = 0xFFF8DC;  break;
        case CSS_VALUE_CRIMSON: c = 0xDC143C;  break;
        case CSS_VALUE_CYAN: c = 0x00FFFF;  break;
        case CSS_VALUE_DARKBLUE: c = 0x00008B;  break;
        case CSS_VALUE_DARKCYAN: c = 0x008B8B;  break;
        case CSS_VALUE_DARKGOLDENROD: c = 0xB8860B;  break;
        case CSS_VALUE_DARKGRAY: c = 0xA9A9A9;  break;
        case CSS_VALUE_DARKGREEN: c = 0x006400;  break;
        case CSS_VALUE_DARKGREY: c = 0xA9A9A9;  break;
        case CSS_VALUE_DARKKHAKI: c = 0xBDB76B;  break;
        case CSS_VALUE_DARKMAGENTA: c = 0x8B008B;  break;
        case CSS_VALUE_DARKOLIVEGREEN: c = 0x556B2F;  break;
        case CSS_VALUE_DARKORANGE: c = 0xFF8C00;  break;
        case CSS_VALUE_DARKORCHID: c = 0x9932CC;  break;
        case CSS_VALUE_DARKRED: c = 0x8B0000;  break;
        case CSS_VALUE_DARKSALMON: c = 0xE9967A;  break;
        case CSS_VALUE_DARKSEAGREEN: c = 0x8FBC8F;  break;
        case CSS_VALUE_DARKSLATEBLUE: c = 0x483D8B;  break;
        case CSS_VALUE_DARKSLATEGRAY: c = 0x2F4F4F;  break;
        case CSS_VALUE_DARKSLATEGREY: c = 0x2F4F4F;  break;
        case CSS_VALUE_DARKTURQUOISE: c = 0x00CED1;  break;
        case CSS_VALUE_DARKVIOLET: c = 0x9400D3;  break;
        case CSS_VALUE_DEEPPINK: c = 0xFF1493;  break;
        case CSS_VALUE_DEEPSKYBLUE: c = 0x00BFFF;  break;
        case CSS_VALUE_DIMGRAY: c = 0x696969;  break;
        case CSS_VALUE_DIMGREY: c = 0x696969;  break;
        case CSS_VALUE_DODGERBLUE: c = 0x1E90FF;  break;
        case CSS_VALUE_FIREBRICK: c = 0xB22222;  break;
        case CSS_VALUE_FLORALWHITE: c = 0xFFFAF0;  break;
        case CSS_VALUE_FORESTGREEN: c = 0x228B22;  break;
        case CSS_VALUE_FUCHSIA: c = 0xFF00FF;  break;
        case CSS_VALUE_GAINSBORO: c = 0xDCDCDC;  break;
        case CSS_VALUE_GHOSTWHITE: c = 0xF8F8FF;  break;
        case CSS_VALUE_GOLD: c = 0xFFD700;  break;
        case CSS_VALUE_GOLDENROD: c = 0xDAA520;  break;
        case CSS_VALUE_GRAY: c = 0x808080;  break;
        case CSS_VALUE_GREEN: c = 0x008000;  break;
        case CSS_VALUE_GREENYELLOW: c = 0xADFF2F;  break;
        case CSS_VALUE_GREY: c = 0x808080;  break;
        case CSS_VALUE_HONEYDEW: c = 0xF0FFF0;  break;
        case CSS_VALUE_HOTPINK: c = 0xFF69B4;  break;
        case CSS_VALUE_INDIANRED: c = 0xCD5C5C;  break;
        case CSS_VALUE_INDIGO: c = 0x4B0082;  break;
        case CSS_VALUE_IVORY: c = 0xFFFFF0;  break;
        case CSS_VALUE_KHAKI: c = 0xF0E68C;  break;
        case CSS_VALUE_LAVENDER: c = 0xE6E6FA;  break;
        case CSS_VALUE_LAVENDERBLUSH: c = 0xFFF0F5;  break;
        case CSS_VALUE_LAWNGREEN: c = 0x7CFC00;  break;
        case CSS_VALUE_LEMONCHIFFON: c = 0xFFFACD;  break;
        case CSS_VALUE_LIGHTBLUE: c = 0xADD8E6;  break;
        case CSS_VALUE_LIGHTCORAL: c = 0xF08080;  break;
        case CSS_VALUE_LIGHTCYAN: c = 0xE0FFFF;  break;
        case CSS_VALUE_LIGHTGOLDENRODYELLOW: c = 0xFAFAD2;  break;
        case CSS_VALUE_LIGHTGRAY: c = 0xD3D3D3;  break;
        case CSS_VALUE_LIGHTGREEN: c = 0x90EE90;  break;
        case CSS_VALUE_LIGHTGREY: c = 0xD3D3D3;  break;
        case CSS_VALUE_LIGHTPINK: c = 0xFFB6C1;  break;
        case CSS_VALUE_LIGHTSALMON: c = 0xFFA07A;  break;
        case CSS_VALUE_LIGHTSEAGREEN: c = 0x20B2AA;  break;
        case CSS_VALUE_LIGHTSKYBLUE: c = 0x87CEFA;  break;
        case CSS_VALUE_LIGHTSLATEGRAY: c = 0x778899;  break;
        case CSS_VALUE_LIGHTSLATEGREY: c = 0x778899;  break;
        case CSS_VALUE_LIGHTSTEELBLUE: c = 0xB0C4DE;  break;
        case CSS_VALUE_LIGHTYELLOW: c = 0xFFFFE0;  break;
        case CSS_VALUE_LIME: c = 0x00FF00;  break;
        case CSS_VALUE_LIMEGREEN: c = 0x32CD32;  break;
        case CSS_VALUE_LINEN: c = 0xFAF0E6;  break;
        case CSS_VALUE_MAGENTA: c = 0xFF00FF;  break;
        case CSS_VALUE_MAROON: c = 0x800000;  break;
        case CSS_VALUE_MEDIUMAQUAMARINE: c = 0x66CDAA;  break;
        case CSS_VALUE_MEDIUMBLUE: c = 0x0000CD;  break;
        case CSS_VALUE_MEDIUMORCHID: c = 0xBA55D3;  break;
        case CSS_VALUE_MEDIUMPURPLE: c = 0x9370DB;  break;
        case CSS_VALUE_MEDIUMSEAGREEN: c = 0x3CB371;  break;
        case CSS_VALUE_MEDIUMSLATEBLUE: c = 0x7B68EE;  break;
        case CSS_VALUE_MEDIUMSPRINGGREEN: c = 0x00FA9A;  break;
        case CSS_VALUE_MEDIUMTURQUOISE: c = 0x48D1CC;  break;
        case CSS_VALUE_MEDIUMVIOLETRED: c = 0xC71585;  break;
        case CSS_VALUE_MIDNIGHTBLUE: c = 0x191970;  break;
        case CSS_VALUE_MINTCREAM: c = 0xF5FFFA;  break;
        case CSS_VALUE_MISTYROSE: c = 0xFFE4E1;  break;
        case CSS_VALUE_MOCCASIN: c = 0xFFE4B5;  break;
        case CSS_VALUE_NAVAJOWHITE: c = 0xFFDEAD;  break;
        case CSS_VALUE_NAVY: c = 0x000080;  break;
        case CSS_VALUE_OLDLACE: c = 0xFDF5E6;  break;
        case CSS_VALUE_OLIVE: c = 0x808000;  break;
        case CSS_VALUE_OLIVEDRAB: c = 0x6B8E23;  break;
        case CSS_VALUE_ORANGE: c = 0xFFA500;  break;
        case CSS_VALUE_ORANGERED: c = 0xFF4500;  break;
        case CSS_VALUE_ORCHID: c = 0xDA70D6;  break;
        case CSS_VALUE_PALEGOLDENROD: c = 0xEEE8AA;  break;
        case CSS_VALUE_PALEGREEN: c = 0x98FB98;  break;
        case CSS_VALUE_PALETURQUOISE: c = 0xAFEEEE;  break;
        case CSS_VALUE_PALEVIOLETRED: c = 0xDB7093;  break;
        case CSS_VALUE_PAPAYAWHIP: c = 0xFFEFD5;  break;
        case CSS_VALUE_PEACHPUFF: c = 0xFFDAB9;  break;
        case CSS_VALUE_PERU: c = 0xCD853F;  break;
        case CSS_VALUE_PINK: c = 0xFFC0CB;  break;
        case CSS_VALUE_PLUM: c = 0xDDA0DD;  break;
        case CSS_VALUE_POWDERBLUE: c = 0xB0E0E6;  break;
        case CSS_VALUE_PURPLE: c = 0x800080;  break;
        case CSS_VALUE_REBECCAPURPLE: c = 0x663399;  break;
        case CSS_VALUE_RED: c = 0xFF0000;  break;
        case CSS_VALUE_ROSYBROWN: c = 0xBC8F8F;  break;
        case CSS_VALUE_ROYALBLUE: c = 0x4169E1;  break;
        case CSS_VALUE_SADDLEBROWN: c = 0x8B4513;  break;
        case CSS_VALUE_SALMON: c = 0xFA8072;  break;
        case CSS_VALUE_SANDYBROWN: c = 0xF4A460;  break;
        case CSS_VALUE_SEAGREEN: c = 0x2E8B57;  break;
        case CSS_VALUE_SEASHELL: c = 0xFFF5EE;  break;
        case CSS_VALUE_SIENNA: c = 0xA0522D;  break;
        case CSS_VALUE_SILVER: c = 0xC0C0C0;  break;
        case CSS_VALUE_SKYBLUE: c = 0x87CEEB;  break;
        case CSS_VALUE_SLATEBLUE: c = 0x6A5ACD;  break;
        case CSS_VALUE_SLATEGRAY: c = 0x708090;  break;
        case CSS_VALUE_SLATEGREY: c = 0x708090;  break;
        case CSS_VALUE_SNOW: c = 0xFFFAFA;  break;
        case CSS_VALUE_SPRINGGREEN: c = 0x00FF7F;  break;
        case CSS_VALUE_STEELBLUE: c = 0x4682B4;  break;
        case CSS_VALUE_TAN: c = 0xD2B48C;  break;
        case CSS_VALUE_TEAL: c = 0x008080;  break;
        case CSS_VALUE_THISTLE: c = 0xD8BFD8;  break;
        case CSS_VALUE_TOMATO: c = 0xFF6347;  break;
        case CSS_VALUE_TURQUOISE: c = 0x40E0D0;  break;
        case CSS_VALUE_VIOLET: c = 0xEE82EE;  break;
        case CSS_VALUE_WHEAT: c = 0xF5DEB3;  break;
        case CSS_VALUE_WHITE: c = 0xFFFFFF;  break;
        case CSS_VALUE_WHITESMOKE: c = 0xF5F5F5;  break;
        case CSS_VALUE_YELLOW: c = 0xFFFF00;  break;
        case CSS_VALUE_YELLOWGREEN: c = 0x9ACD32;  break;
        default: c = 0x000000;  break;
    }
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (Color){ 0xFF000000 | (b << 16) | (g << 8) | r };
}

float map_lambda_font_size_keyword(CssEnum keyword_enum) {
    // Map font-size keywords to pixel values
    switch (keyword_enum) {
        case CSS_VALUE_XX_SMALL: return 9.0f;
        case CSS_VALUE_X_SMALL: return 10.0f;
        case CSS_VALUE_SMALL: return 13.0f;
        case CSS_VALUE_MEDIUM: return 16.0f;
        case CSS_VALUE_LARGE: return 18.0f;
        case CSS_VALUE_X_LARGE: return 24.0f;
        case CSS_VALUE_XX_LARGE: return 32.0f;
        case CSS_VALUE_SMALLER: return -1.0f;  // relative to parent
        case CSS_VALUE_LARGER: return -1.0f;   // relative to parent
        default: return 16.0f; // default medium size
    }
}

// map CSS font-weight keywords/numbers to PropValue enum
CssEnum map_font_weight(const CssValue* value) {
    if (!value) return CSS_VALUE_NORMAL;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        // The keyword is already an enum, just return appropriate values
        switch (keyword) {
            case CSS_VALUE_NORMAL: return CSS_VALUE_NORMAL;
            case CSS_VALUE_BOLD: return CSS_VALUE_BOLD;
            case CSS_VALUE_BOLDER: return CSS_VALUE_BOLDER;
            case CSS_VALUE_LIGHTER: return CSS_VALUE_LIGHTER;
            default: return CSS_VALUE_NORMAL;
        }
    }
    else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        // CSS 2.1 §15.6: numeric weights map to nearest keyword
        int weight = (int)value->data.number.value;
        if (weight <= 400) return CSS_VALUE_NORMAL;
        if (weight <= 500) return CSS_VALUE_NORMAL;  // 500 maps to normal
        return CSS_VALUE_BOLD;  // 600-900 maps to bold
    }
    return CSS_VALUE_NORMAL; // default
}

// Extract numeric weight (100-900) from CSS value for precise font matching
static int16_t map_font_weight_numeric(const CssValue* value) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        int weight = (int)value->data.number.value;
        if (weight >= 100 && weight <= 900) return (int16_t)weight;
        return 0;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        switch (value->data.keyword) {
            case CSS_VALUE_NORMAL: return 400;
            case CSS_VALUE_BOLD: return 700;
            default: return 0; // bolder/lighter need inheritance computation
        }
    }
    return 0;
}

// Cascade Priority Calculation
// Encodes full CSS cascade ordering into a single int64_t for correct
// shorthand vs longhand resolution. Per CSS Cascading Level 4:
//   cascade_level > CSS specificity > source_order
// Bits 56-63: cascade level (1-8)
// Bits 32-55: CSS specificity (ids<<16 | classes<<8 | elements, plus inline flag)
// Bits 0-31:  source_order (higher = later declaration = wins ties)
int64_t get_cascade_priority(const CssDeclaration* decl) {
    if (!decl) {
        return 0;
    }

    // cascade level per CSS Cascading and Inheritance Level 4
    int level;
    if (decl->specificity.important) {
        switch (decl->origin) {
            case CSS_ORIGIN_USER_AGENT: level = 8; break;
            case CSS_ORIGIN_USER:       level = 7; break;
            default:                    level = 6; break; // author !important
        }
    } else {
        if (decl->origin == CSS_ORIGIN_AUTHOR && decl->specificity.inline_style) {
            level = 4; // inline styles
        } else {
            switch (decl->origin) {
                case CSS_ORIGIN_USER_AGENT: level = 1; break;
                case CSS_ORIGIN_USER:       level = 2; break;
                case CSS_ORIGIN_ANIMATION:
                case CSS_ORIGIN_TRANSITION: level = 5; break;
                default:                    level = 3; break; // author normal
            }
        }
    }

    int32_t css_specificity = (decl->specificity.inline_style << 24) |
                              (decl->specificity.ids << 16) |
                              (decl->specificity.classes << 8) |
                              decl->specificity.elements;

    int64_t priority = ((int64_t)level << 56) |
                       ((int64_t)(css_specificity & 0xFFFFFF) << 32) |
                       (int64_t)decl->source_order;

    return priority;
}

// CSS 2.1 §9.7: Apply blockification for floated or absolutely positioned elements
// Converts internal table display values to 'block'
DisplayValue blockify_display(DisplayValue display) {
    // Table internal display values that get blockified
    if (display.inner == CSS_VALUE_TABLE_ROW ||
        display.inner == CSS_VALUE_TABLE_ROW_GROUP ||
        display.inner == CSS_VALUE_TABLE_HEADER_GROUP ||
        display.inner == CSS_VALUE_TABLE_FOOTER_GROUP ||
        display.inner == CSS_VALUE_TABLE_COLUMN ||
        display.inner == CSS_VALUE_TABLE_COLUMN_GROUP ||
        display.inner == CSS_VALUE_TABLE_CELL ||
        display.inner == CSS_VALUE_TABLE_CAPTION) {
        return DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    // inline becomes block
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_FLOW) {
        return DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    // inline-block, inline-table, inline-flex, inline-grid stay as block-level equivalents
    if (display.outer == CSS_VALUE_INLINE_BLOCK) {
        display.outer = CSS_VALUE_BLOCK;
    }
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_TABLE) {
        display.outer = CSS_VALUE_BLOCK;  // inline-table -> table
    }
    return display;
}

static bool css_content_value_has_image_url(const CssValue* value) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_URL) return true;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name) {
        const char* fn = value->data.function->name;
        size_t fn_len = strlen(fn);
        if (str_ieq_const(fn, fn_len, "url")) return true;
    }
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            if (css_content_value_has_image_url(value->data.list.values[i])) return true;
        }
    }
    return false;
}

DisplayValue resolve_display_value(void* child) {
    // Resolve display value for a DOM node
    DisplayValue display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};

    DomNode* node = static_cast<DomNode*>(child);
    if (node && node->is_element()) {
        // resolve display from CSS if available
        DomElement* dom_elem = node->as_element();
        NameId tag_id = dom_elem ? dom_elem->tag_id : NAME_ID_NONE;


        // CSS 2.1 §9.7: Check for float and position - floated or absolutely positioned elements get blockified
        CssEnum float_value = layout_specified_keyword(
            dom_elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
        bool is_floated = (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT);
        CssEnum position_value = layout_specified_keyword(
            dom_elem, CSS_PROPERTY_POSITION, CSS_VALUE_NONE);
        bool is_abspos = (position_value == CSS_VALUE_ABSOLUTE || position_value == CSS_VALUE_FIXED);
        // CSS Flexbox §4 / CSS Grid §6: Children of flex/grid containers have their
        // display blockified. E.g. <tr> inside a display:flex <table> becomes block.
        bool is_flex_or_grid_child = false;
        if (node->parent && node->parent->is_element()) {
            DomElement* parent_elem = node->parent->as_element();
            if (parent_elem && parent_elem->display.inner != 0 && parent_elem->styles_resolved()) {
                is_flex_or_grid_child = (parent_elem->display.inner == CSS_VALUE_FLEX ||
                                         parent_elem->display.inner == CSS_VALUE_GRID);
            }
        }
        // CSS 2.1 §9.7 rule 2: absolute/fixed position also triggers blockification
        bool needs_blockify = is_floated || is_abspos || is_flex_or_grid_child;

        // HTML spec §14.3.1: The hidden attribute (UA stylesheet: [hidden] { display: none })
        // Must check before CSS cascade since it's a presentational hint
        if (dom_elem && dom_elem->has_attribute("hidden")) {
            DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
            return none_display;
        }

        // closed popovers are removed from layout by the HTML UA rule, before author display resolution.
        if (dom_elem && dom_elem->has_attribute("popover") && !dom_elem->is_popover_open()) {
            DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
            return none_display;
        }

        // HTML spec §4.11.1: Non-summary children of closed <details> are hidden.
        // This must be checked here (not just in layout_flow_node) to cover all
        // layout paths including flex and grid when CSS overrides display.
        if (node->parent && node->parent->is_element()) {
            DomElement* parent_elem = node->parent->as_element();
            if (parent_elem->tag() == MARKUP_NAME_DETAILS && !parent_elem->has_attribute(MARKUP_NAME_OPEN)) {
                if (tag_id != MARKUP_NAME_SUMMARY) {
                    DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
                    return none_display;
                }
            }
        }

        // Check if element already has display set directly (anonymous elements, pre-resolved)
        // This handles CSS 2.1 anonymous table objects created by layout
        // when display:none is set by UA defaults for hidden inputs, respect it
        if (dom_elem && tag_id == MARKUP_NAME_INPUT) {
            const char* type_attr = dom_elem->get_attribute("type");
            if (type_attr && (strcmp(type_attr, "hidden") == 0)) {
                DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
                return none_display;
            }
        }
        bool has_specified_display = false;
        if (dom_elem && dom_elem->specified_style && dom_elem->specified_style->tree) {
            has_specified_display =
                avl_tree_search(dom_elem->specified_style->tree, CSS_PROPERTY_DISPLAY) != nullptr;
        }
        if (dom_elem && !has_specified_display &&
            dom_elem->display.inner != CSS_VALUE_NONE &&
            dom_elem->display.inner != 0 && dom_elem->styles_resolved()) {
            // CSS 2.1 §9.7: Even pre-resolved elements must be blockified when
            // floated or absolutely positioned (takes precedence)
            return needs_blockify ? blockify_display(dom_elem->display) : dom_elem->display;
        }

        // Determine if this is a replaced element (img, video, iframe, svg, etc.)
        // Replaced elements always have inner display of RDT_DISPLAY_REPLACED
        // HTML §4.8.7: <object> is replaced only when it has a data attribute
        // HTML §4.8.9: <audio> is replaced only when it has a controls attribute
        // Note: <button> is NOT replaced — it contains flow content (text, spans, etc.)
        // per HTML spec. Its children are laid out normally via CSS_VALUE_FLOW.
        bool is_replaced = (tag_id == MARKUP_NAME_IMG || tag_id == MARKUP_NAME_VIDEO ||
                            tag_id == MARKUP_NAME_INPUT || tag_id == MARKUP_NAME_SELECT ||
                            tag_id == MARKUP_NAME_TEXTAREA ||
                            tag_id == MARKUP_NAME_IFRAME || tag_id == MARKUP_NAME_HR ||
                            tag_id == MARKUP_NAME_SVG || tag_id == MARKUP_NAME_METER ||
                            tag_id == MARKUP_NAME_PROGRESS || tag_id == MARKUP_NAME_CANVAS ||
                            tag_id == MARKUP_NAME_WEBVIEW ||
                            (tag_id == MARKUP_NAME_OBJECT && dom_elem && dom_elem->get_attribute(MARKUP_NAME_DATA)) ||
                            (tag_id == MARKUP_NAME_AUDIO && dom_elem && dom_elem->has_attribute(MARKUP_NAME_CONTROLS)) ||
                            tag_id == MARKUP_NAME_EMBED);
        if (dom_elem && dom_elem->specified_style) {
            CssDeclaration* content_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_CONTENT);
            if (content_decl && css_content_value_has_image_url(content_decl->value)) {
                // content:url() exposes a replaced object box in this layout path;
                // image-set() must not assign its intrinsic size to the DOM element.
                is_replaced = true;
            }
        }

        // first, try to get display from CSS
        if (dom_elem && dom_elem->specified_style) {
            StyleTree* style_tree = dom_elem->specified_style;
            if (style_tree->tree) {
                // look for display property in the AVL tree
                AvlNode* node = avl_tree_search(style_tree->tree, CSS_PROPERTY_DISPLAY);
                if (node) {
                    StyleNode* style_node = (StyleNode*)node->declaration;
                    if (style_node && style_node->winning_decl) {
                        CssDeclaration* decl = style_node->winning_decl;
                        const char* custom_layout_name = custom_layout_name_from_css_value(decl->value);
                        if (custom_layout_name && custom_layout_name[0] != '\0') {
                            display.outer = CSS_VALUE_BLOCK;
                            display.inner = CSS_VALUE_FLOW;
                            return needs_blockify ? blockify_display(display) : display;
                        }
                        if (decl->value && decl->value->type == CSS_VALUE_TYPE_CUSTOM &&
                            decl->value->data.custom_property.name &&
                            strcmp(decl->value->data.custom_property.name, "-webkit-inline-box") == 0) {
                            // legacy WebKit inline boxes resolve to an inline-level box;
                            // treating the unknown vendor value as block changes its
                            // shrink-to-fit and baseline participation before layout.
                            display.outer = CSS_VALUE_INLINE_BLOCK;
                            display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                            return needs_blockify ? blockify_display(display) : display;
                        } else if (decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum keyword = decl->value->data.keyword;
                            // Map keyword to display values
                            if (keyword == CSS_VALUE_FLEX) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_FLEX) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = CSS_VALUE_FLEX;
                                return display;
                            } else if (keyword == CSS_VALUE_GRID) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_GRID;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_GRID) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = CSS_VALUE_GRID;
                                return display;
                            } else if (keyword == CSS_VALUE_BLOCK) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                // CSS 2.1 §9.7: Floated/absolutely positioned elements become block
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_INLINE_BLOCK) {
                                display.outer = CSS_VALUE_INLINE_BLOCK;
                                display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                // CSS 2.1 §9.7: Floated elements become block
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_LIST_ITEM) {
                                display.outer = CSS_VALUE_LIST_ITEM;
                                display.inner = CSS_VALUE_FLOW;
                                display.list_item = true;
                                return display;
                            } else if (keyword == CSS_VALUE_NONE) {
                                display.outer = CSS_VALUE_NONE;
                                display.inner = CSS_VALUE_NONE;
                                return display;
                            } else if (keyword == CSS_VALUE_CONTENTS) {
                                // CSS Display Level 3: display: contents
                                // Element does not generate any box, but children are laid out
                                // as if they were children of the element's parent
                                display.outer = CSS_VALUE_CONTENTS;
                                display.inner = CSS_VALUE_CONTENTS;
                                return display;
                            } else if (keyword == CSS_VALUE_INHERIT) {
                                // CSS 2.1 §9.2.4: display: inherit — use parent's computed display
                                DomElement* parent_elem = dom_elem->parent_element();
                                if (parent_elem) {
                                    DisplayValue parent_display = resolve_display_value((void*)parent_elem);
                                    return needs_blockify ? blockify_display(parent_display) : parent_display;
                                }
                                // no parent (root element) — fall through to tag-based default
                            } else if (keyword == CSS_VALUE_RUN_IN) {
                                // Chrome treats run-in as tag default (dropped CSS 2.1 run-in support)
                                // Our test references are Chrome-based, so matching Chrome
                                // avoids false failures. The whitespace handling fix in
                                // should_collapse_inter_element_whitespace still correctly
                                // preserves pre whitespace between block siblings.
                                // Fall through to tag-based defaults below
                            } else if (keyword == CSS_VALUE_FLOW_ROOT) {
                                // CSS Display Level 3: display:flow-root establishes a BFC
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_FLOW_ROOT;
                                return display;
                            } else if (keyword == CSS_VALUE_TABLE) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE;
                                return display;
                            } else if (keyword == CSS_VALUE_INLINE_TABLE) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_TABLE;
                                return display;
                            } else if (keyword == CSS_VALUE_RUBY) {
                                display.outer = CSS_VALUE_INLINE;
                                display.inner = CSS_VALUE_RUBY;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_ROW) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_ROW;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_CELL) {
                                display.outer = CSS_VALUE_TABLE_CELL;
                                display.inner = CSS_VALUE_TABLE_CELL;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_ROW_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_ROW_GROUP;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_HEADER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_HEADER_GROUP;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_FOOTER_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_FOOTER_GROUP;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_COLUMN_GROUP) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
                                return needs_blockify ? blockify_display(display) : display;
                            } else if (keyword == CSS_VALUE_TABLE_CAPTION) {
                                display.outer = CSS_VALUE_BLOCK;
                                display.inner = CSS_VALUE_TABLE_CAPTION;
                                return needs_blockify ? blockify_display(display) : display;
                            }
                        } else if (decl->value->type == CSS_VALUE_TYPE_LIST) {
                            // Handle CSS Display Level 3 multi-value syntax
                            // e.g., "display: block flow", "display: inline list-item",
                            // "display: inline flow-root list-item"
                            CssValue** values = decl->value->data.list.values;
                            int count = decl->value->data.list.count;

                            // Scan all keywords: separate list-item flag from outer/inner
                            bool has_list_item = false;
                            CssEnum outer_kw = (CssEnum)0;
                            CssEnum inner_kw = (CssEnum)0;
                            bool has_outer = false;
                            bool has_inner = false;

                            for (int i = 0; i < count; i++) {
                                if (!values[i] || values[i]->type != CSS_VALUE_TYPE_KEYWORD) continue;
                                CssEnum kw = values[i]->data.keyword;

                                if (kw == CSS_VALUE_LIST_ITEM) {
                                    has_list_item = true;
                                } else if (kw == CSS_VALUE_BLOCK || kw == CSS_VALUE_INLINE || kw == CSS_VALUE_RUN_IN) {
                                    outer_kw = kw;
                                    has_outer = true;
                                } else if (kw == CSS_VALUE_FLOW || kw == CSS_VALUE_FLOW_ROOT ||
                                           kw == CSS_VALUE_FLEX || kw == CSS_VALUE_GRID ||
                                           kw == CSS_VALUE_TABLE || kw == CSS_VALUE_RUBY) {
                                    inner_kw = kw;
                                    has_inner = true;
                                }
                            }

                            if (has_list_item) {
                                display.list_item = true;
                                // Map outer: inline list-item → inline-block (for block layout + inline flow)
                                // block list-item or unspecified → CSS_VALUE_LIST_ITEM (existing behavior)
                                if (has_outer && outer_kw == CSS_VALUE_INLINE) {
                                    display.outer = CSS_VALUE_INLINE_BLOCK;
                                } else {
                                    display.outer = CSS_VALUE_LIST_ITEM;
                                }
                                // Map inner display
                                if (has_inner) {
                                    if (inner_kw == CSS_VALUE_FLOW) {
                                        display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                    } else if (inner_kw == CSS_VALUE_FLOW_ROOT) {
                                        display.inner = CSS_VALUE_FLOW_ROOT;
                                    } else {
                                        display.inner = CSS_VALUE_FLOW;
                                    }
                                } else {
                                    display.inner = CSS_VALUE_FLOW;
                                }
                                return display;
                            }

                            if (count >= 2 && has_outer && has_inner) {
                                // Standard two-value display (no list-item)

                                // Map outer display keyword
                                if (outer_kw == CSS_VALUE_BLOCK) {
                                    display.outer = CSS_VALUE_BLOCK;
                                } else if (outer_kw == CSS_VALUE_INLINE) {
                                    display.outer = CSS_VALUE_INLINE;
                                } else if (outer_kw == CSS_VALUE_RUN_IN) {
                                    // run-in unsupported — don't set
                                } else {
                                    display.outer = CSS_VALUE_BLOCK;
                                }

                                // Map inner display keyword
                                if (inner_kw == CSS_VALUE_FLOW) {
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                } else if (inner_kw == CSS_VALUE_FLOW_ROOT) {
                                    display.inner = CSS_VALUE_FLOW_ROOT;
                                } else if (inner_kw == CSS_VALUE_FLEX) {
                                    display.inner = CSS_VALUE_FLEX;
                                } else if (inner_kw == CSS_VALUE_GRID) {
                                    display.inner = CSS_VALUE_GRID;
                                } else if (inner_kw == CSS_VALUE_TABLE) {
                                    display.inner = CSS_VALUE_TABLE;
                                } else if (inner_kw == CSS_VALUE_RUBY) {
                                    display.inner = CSS_VALUE_RUBY;
                                } else {
                                    display.inner = CSS_VALUE_FLOW;
                                }

                                return display;
                            } else if (count == 1 && values[0] &&
                                       values[0]->type == CSS_VALUE_TYPE_KEYWORD) {
                                // Single keyword in list (edge case)
                                CssEnum keyword = values[0]->data.keyword;
                                // Handle same as single keyword (fall through to regular logic won't work here)
                                // Re-use the single keyword logic
                                if (keyword == CSS_VALUE_BLOCK) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                    return display;
                                } else if (keyword == CSS_VALUE_INLINE) {
                                    display.outer = CSS_VALUE_INLINE;
                                    display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                                    return display;
                                } else if (keyword == CSS_VALUE_FLEX) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = CSS_VALUE_FLEX;
                                    return display;
                                } else if (keyword == CSS_VALUE_GRID) {
                                    display.outer = CSS_VALUE_BLOCK;
                                    display.inner = CSS_VALUE_GRID;
                                    return display;
                                } else if (keyword == CSS_VALUE_NONE) {
                                    display.outer = CSS_VALUE_NONE;
                                    display.inner = CSS_VALUE_NONE;
                                    return display;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (custom_layout_name_for_element(dom_elem)) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
            return needs_blockify ? blockify_display(display) : display;
        }

        // Fall back to default display values based on tag ID
        if (tag_id == MARKUP_NAME_HTML || tag_id == MARKUP_NAME_BODY || tag_id == MARKUP_NAME_H1 ||
            tag_id == MARKUP_NAME_H2 || tag_id == MARKUP_NAME_H3 ||
            tag_id == MARKUP_NAME_H4 || tag_id == MARKUP_NAME_H5 ||
            tag_id == MARKUP_NAME_H6 || tag_id == MARKUP_NAME_P ||
            tag_id == MARKUP_NAME_DIV || tag_id == MARKUP_NAME_CENTER ||
            tag_id == MARKUP_NAME_UL || tag_id == MARKUP_NAME_OL ||
            tag_id == MARKUP_NAME_DL || tag_id == MARKUP_NAME_DT || tag_id == MARKUP_NAME_DD ||
            tag_id == MARKUP_NAME_HEADER || tag_id == MARKUP_NAME_MAIN ||
            tag_id == MARKUP_NAME_SECTION || tag_id == MARKUP_NAME_FOOTER ||
            tag_id == MARKUP_NAME_ARTICLE || tag_id == MARKUP_NAME_ASIDE ||
            tag_id == MARKUP_NAME_NAV || tag_id == MARKUP_NAME_ADDRESS ||
            tag_id == MARKUP_NAME_BLOCKQUOTE || tag_id == MARKUP_NAME_DETAILS ||
            tag_id == MARKUP_NAME_DIALOG || tag_id == MARKUP_NAME_FIGURE ||
            tag_id == MARKUP_NAME_FIGCAPTION || tag_id == MARKUP_NAME_HGROUP ||
            tag_id == MARKUP_NAME_PRE || tag_id == MARKUP_NAME_FIELDSET ||
            tag_id == MARKUP_NAME_LEGEND || tag_id == MARKUP_NAME_FORM ||
            tag_id == MARKUP_NAME_MENU || tag_id == MARKUP_NAME_FRAMESET) {
            // HTML framesets generate a block container even when created
            // dynamically; treating them as unknown inline content loses the
            // viewport-sized legacy layout box.
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == MARKUP_NAME_LI || tag_id == MARKUP_NAME_SUMMARY) {
            display.outer = CSS_VALUE_LIST_ITEM;
            display.inner = CSS_VALUE_FLOW;
            display.list_item = true;
        } else if (tag_id == MARKUP_NAME_IMG || tag_id == MARKUP_NAME_VIDEO ||
            tag_id == MARKUP_NAME_INPUT || tag_id == MARKUP_NAME_SELECT ||
            tag_id == MARKUP_NAME_TEXTAREA ||
            tag_id == MARKUP_NAME_IFRAME || tag_id == MARKUP_NAME_METER ||
            tag_id == MARKUP_NAME_PROGRESS || tag_id == MARKUP_NAME_CANVAS ||
            tag_id == MARKUP_NAME_WEBVIEW ||
            (tag_id == MARKUP_NAME_OBJECT && dom_elem && dom_elem->get_attribute(MARKUP_NAME_DATA)) ||
            (tag_id == MARKUP_NAME_AUDIO && dom_elem && dom_elem->has_attribute(MARKUP_NAME_CONTROLS)) ||
            tag_id == MARKUP_NAME_EMBED) {
            display.outer = CSS_VALUE_INLINE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == MARKUP_NAME_BUTTON) {
            // <button> is inline-block with flow children (not replaced)
            display.outer = CSS_VALUE_INLINE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == MARKUP_NAME_OBJECT) {
            // <object> without data attribute: inline flow (renders fallback children)
            display.outer = CSS_VALUE_INLINE;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == MARKUP_NAME_HR) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == MARKUP_NAME_RUBY) {
            // HTML ruby establishes an inline ruby formatting context, not a
            // regular inline flow box containing sequential base and <rt> text.
            display.outer = CSS_VALUE_INLINE;
            display.inner = CSS_VALUE_RUBY;
        } else if (tag_id == MARKUP_NAME_SVG) {
            // SVG elements are inline replaced elements by default
            display.outer = CSS_VALUE_INLINE;
            display.inner = RDT_DISPLAY_REPLACED;
        } else if (tag_id == MARKUP_NAME_SCRIPT || tag_id == MARKUP_NAME_STYLE ||
            tag_id == MARKUP_NAME_HEAD || tag_id == MARKUP_NAME_TITLE || tag_id == MARKUP_NAME_META ||
            tag_id == MARKUP_NAME_LINK || tag_id == MARKUP_NAME_BASE || tag_id == MARKUP_NAME_NOSCRIPT ||
            tag_id == MARKUP_NAME_TEMPLATE || tag_id == MARKUP_NAME_MAP || tag_id == MARKUP_NAME_AREA ||
            tag_id == MARKUP_NAME_RP ||
            tag_id == MARKUP_NAME_DATALIST) {
            display.outer = CSS_VALUE_NONE;
            display.inner = CSS_VALUE_NONE;
        } else if (tag_id == MARKUP_NAME_OPTION || tag_id == MARKUP_NAME_OPTGROUP) {
            // Option/optgroup inside select/datalist: block 0x0 (browsers report 0x0)
            // Outside select/datalist: normal block flow (shows text content)
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == MARKUP_NAME_TABLE) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE;
        } else if (tag_id == MARKUP_NAME_CAPTION) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_FLOW;
        } else if (tag_id == MARKUP_NAME_THEAD || tag_id == MARKUP_NAME_TBODY || tag_id == MARKUP_NAME_TFOOT) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW_GROUP;
        } else if (tag_id == MARKUP_NAME_TR) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_ROW;
        } else if (tag_id == MARKUP_NAME_TH || tag_id == MARKUP_NAME_TD) {
            display.outer = CSS_VALUE_TABLE_CELL;
            display.inner = CSS_VALUE_TABLE_CELL;
        } else if (tag_id == MARKUP_NAME_COLGROUP) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
        } else if (tag_id == MARKUP_NAME_COL) {
            display.outer = CSS_VALUE_BLOCK;
            display.inner = CSS_VALUE_TABLE_COLUMN;
        } else {
            // Fall back to tag name string comparison for elements without tag_id
            // This handles markdown/Lambda-generated HTML that doesn't go through HTML5 parser
            const char* tag_name = node->node_name();
            if (tag_name) {
                if (strcmp(tag_name, "table") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE;
                } else if (strcmp(tag_name, "thead") == 0 || strcmp(tag_name, "tbody") == 0 || strcmp(tag_name, "tfoot") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_ROW_GROUP;
                } else if (strcmp(tag_name, "tr") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_ROW;
                } else if (strcmp(tag_name, "th") == 0 || strcmp(tag_name, "td") == 0) {
                    display.outer = CSS_VALUE_TABLE_CELL;
                    display.inner = CSS_VALUE_TABLE_CELL;
                } else if (strcmp(tag_name, "caption") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_FLOW;
                } else if (strcmp(tag_name, "colgroup") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_COLUMN_GROUP;
                } else if (strcmp(tag_name, "col") == 0) {
                    display.outer = CSS_VALUE_BLOCK;
                    display.inner = CSS_VALUE_TABLE_COLUMN;
                } else {
                    // Default for truly unknown elements (inline)
                    display.outer = CSS_VALUE_INLINE;
                    display.inner = CSS_VALUE_FLOW;
                }
            } else {
                // No tag name available, default to inline
                display.outer = CSS_VALUE_INLINE;
                display.inner = CSS_VALUE_FLOW;
            }
        }
        // CSS 2.1 §9.7: Apply blockification to tag-based defaults too.
        // Floated or absolutely positioned elements become block-level
        // regardless of how their display value was determined.
        if (is_replaced && display.outer != CSS_VALUE_NONE && display.inner == CSS_VALUE_FLOW) {
            // content:url() keeps the element's outer display but uses replaced sizing.
            display.inner = RDT_DISPLAY_REPLACED;
        }
        return needs_blockify ? blockify_display(display) : display;
    }
    return display;
}

/**
 * Helper function to resolve font size for Lambda CSS
 * Used internally by resolve_length_value for em/rem calculations
 */
static void resolve_font_size(LayoutContext* lycon, const CssDeclaration* decl) {

    if (!decl && lycon->view) {
        // Try to get font-size from the view's font property
        // IMPORTANT: Must check node type before accessing font field.
        // DomElement::font and DomText::font are at different struct offsets,
        // so treating a DomText* as a DomElement reads garbage memory.
        FontProp* fp = nullptr;
        if (lycon->view->is_element()) {
            fp = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view)->font;
        } else if (lycon->view->is_text()) {
            fp = lam::dom_require<DOM_NODE_TEXT>(lycon->view)->font;
        }
        if (fp && fp->font_size > 0) {
            lycon->font.current_font_size = fp->font_size;
            return;
        }
    }

    if (decl && decl->value) {
        // resolve font size from declaration
        const CssValue* value = decl->value;

        // Resolve var() if present
        value = resolve_var_function(lycon, value);
        if (!value) {
            // var() couldn't be resolved, use fallback
            if (lycon->font.style && lycon->font.style->font_size > 0) {
                lycon->font.current_font_size = lycon->font.style->font_size;
            } else {
                lycon->font.current_font_size = 16.0f;
            }
            return;
        }

        if (value->type == CSS_VALUE_TYPE_LENGTH) {
            // Direct length value
            lycon->font.current_font_size = resolve_length_value(lycon,
                CSS_PROPERTY_FONT_SIZE, value);
            return;
        } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum kw = value->data.keyword;
            // CSS 2.1 §15.7: 'larger' and 'smaller' are relative to parent font size
            // Scale factor between adjacent absolute-size keywords is ~1.2
            if (kw == CSS_VALUE_LARGER || kw == CSS_VALUE_SMALLER) {
                float parent_size = (lycon->font.style && lycon->font.style->font_size > 0)
                                    ? lycon->font.style->font_size : 16.0f;
                float scale = (kw == CSS_VALUE_LARGER) ? 1.2f : (1.0f / 1.2f);
                lycon->font.current_font_size = parent_size * scale;
                return;
            }
            // Absolute keyword font size
            float size = map_lambda_font_size_keyword(kw);
            if (size > 0) {
                lycon->font.current_font_size = size;
                return;
            }
        }
    }

    // fallback: use font size from style context
    if (lycon->font.style && lycon->font.style->font_size > 0) {
        lycon->font.current_font_size = lycon->font.style->font_size;
    } else {
        // ultimate fallback: use default
        lycon->font.current_font_size = 16.0f;
    }
}

/**
 * Evaluate a calc() expression list with operator precedence and parentheses.
 * Items are: values (LENGTH, NUMBER, PERCENTAGE...), operators (CUSTOM with +, -, *, /),
 * and parentheses (KEYWORD with keyword=0, produced by CSS_TOKEN_LEFT/RIGHT_PAREN).
 * Uses two accumulators for * / precedence over + -, and recursion for parentheses.
 * @param pos  Current index in the list; updated on return to point past consumed items.
 * @param depth  Recursion depth for parenthesized sub-expressions.
 */
static float evaluate_calc_expression(LayoutContext* lycon, uintptr_t raw_prop,
                                      CssValue** items, int count, int* pos, int depth) {
    // Bound parenthesis nesting to prevent stack overflow on adversarial
    // stylesheets (Radiant audit finding #13).
    constexpr int kMaxCalcDepth = 32;
    if (depth > kMaxCalcDepth) return 0.0f;

    float result_sum = 0;
    float term = 0;
    int term_sign = 1;
    bool first_value = true;
    char pending_op = '+';
    bool expect_value = true;  // true when next non-operator token should be a value (or LPAREN)

    while (*pos < count) {
        CssValue* item = items[*pos];
        if (!item) { (*pos)++; continue; }

        // classify item as: arithmetic operator, no-op keyword (LPAREN/RPAREN), or value
        bool is_arith_op = false;
        bool is_noop_keyword = false;
        char op_char = 0;

        if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            const CssEnumInfo* op_info = css_enum_info(item->data.keyword);
            const char* name = op_info ? op_info->name : "";
            if (name[0] == '+' || name[0] == '-' || name[0] == '*' || name[0] == '/') {
                is_arith_op = true;
                op_char = name[0];
            } else {
                is_noop_keyword = true;
            }
        } else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
            const char* name = item->data.custom_property.name;
            if (strlen(name) == 1 && (name[0] == '+' || name[0] == '-' ||
                                      name[0] == '*' || name[0] == '/')) {
                is_arith_op = true;
                op_char = name[0];
            } else {
                is_noop_keyword = true;
            }
        }

        if (is_arith_op) {
            pending_op = op_char;
            expect_value = true;
            (*pos)++;
        } else if (is_noop_keyword) {
            if (expect_value) {
                // left parenthesis — recursively evaluate the sub-expression
                (*pos)++;
                float sub = evaluate_calc_expression(lycon, raw_prop, items, count, pos, depth + 1);
                // apply sub-expression result as a value
                if (first_value) {
                    term = sub; first_value = false;
                } else if (pending_op == '*') {
                    term *= sub;
                } else if (pending_op == '/') {
                    if (sub != 0) term /= sub;
                } else {
                    result_sum += term_sign * term;
                    term = sub;
                    term_sign = (pending_op == '-') ? -1 : 1;
                }
                expect_value = false;
            } else {
                // right parenthesis — end this sub-expression
                (*pos)++;
                break;
            }
        } else {
            // regular value
            float val = resolve_length_value(lycon, raw_prop, item);
            (*pos)++;
            if (!isnan(val)) {
                if (first_value) {
                    term = val; first_value = false;
                } else if (pending_op == '*') {
                    term *= val;
                } else if (pending_op == '/') {
                    if (val != 0) term /= val;
                } else {
                    result_sum += term_sign * term;
                    term = val;
                    term_sign = (pending_op == '-') ? -1 : 1;
                }
                expect_value = false;
            }
        }
    }
    if (!first_value) {
        result_sum += term_sign * term;
    }
    return result_sum;
}

static bool evaluate_simple_calc_operator(const char* op_name, float left,
                                          float right, float* result) {
    if (!op_name || !result) return false;
    if (strcmp(op_name, "+") == 0) *result = left + right;
    else if (strcmp(op_name, "-") == 0) *result = left - right;
    else if (strcmp(op_name, "*") == 0) *result = left * right;
    else if (strcmp(op_name, "/") == 0) *result = right != 0.0f ? left / right : 0.0f;
    else return false;
    return true;
}

static bool css_percentage_uses_containing_inline_size(uintptr_t property) {
    switch ((CssPropertyCode)property) {
        case CSS_PROPERTY_MARGIN:
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_MARGIN_BLOCK:
        case CSS_PROPERTY_MARGIN_BLOCK_START:
        case CSS_PROPERTY_MARGIN_BLOCK_END:
        case CSS_PROPERTY_MARGIN_INLINE:
        case CSS_PROPERTY_MARGIN_INLINE_START:
        case CSS_PROPERTY_MARGIN_INLINE_END:
        case CSS_PROPERTY_PADDING:
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT:
        case CSS_PROPERTY_PADDING_BLOCK:
        case CSS_PROPERTY_PADDING_BLOCK_START:
        case CSS_PROPERTY_PADDING_BLOCK_END:
        case CSS_PROPERTY_PADDING_INLINE:
        case CSS_PROPERTY_PADDING_INLINE_START:
        case CSS_PROPERTY_PADDING_INLINE_END:
            return true;
        default:
            return false;
    }
}

static float css_containing_inline_percentage_base(LayoutContext* lycon) {
    if (!lycon || !lycon->block.parent) return -1.0f;

    DomElement* current = lycon->elmt && lycon->elmt->is_element()
        ? lam::dom_require_element(lycon->elmt) : nullptr;
    DomElement* parent_element = current ? dom_parent_element(current) : nullptr;
    ViewBlock* parent_block = parent_element ? lam::view_as_block(parent_element) : nullptr;
    if (parent_block && layout_block_inline_axis_is_vertical(parent_block)) {
        bool parent_height_is_auto = !parent_block->blk ||
            parent_block->block()->given_height < 0.0f;
        ViewBlock* current_block = current ? lam::view_as_block(current) : nullptr;
        if (parent_height_is_auto && current_block && current_block->blk &&
            current_block->block()->given_height >= 0.0f) {
            // An orthogonal auto inline-size is established by the current
            // child's definite inline contribution before the parent is final.
            return current_block->block()->given_height;
        }
        if (lycon->block.parent->content_height > 0.0f) {
            return lycon->block.parent->content_height;
        }
        if (lycon->block.parent->given_height > 0.0f) {
            return lycon->block.parent->given_height;
        }
    }
    return lycon->block.parent->content_width;
}

/**
 * Resolve length/percentage value to pixels using Lambda CSS value structures
 *
 * @param lycon Layout context for font size, viewport, and parent dimensions
 * @param property CSS property ID for context-specific resolution.
 *                 Use negative value to suppress line-height NUMBER multiplication (for calc() operands)
 *                 while still using absolute value for percentage base selection.
 * @param value Lambda CssValue pointer (CSS_VALUE_LENGTH, CSS_VALUE_PERCENTAGE, or CSS_VALUE_NUMBER)
 * @return Resolved value in pixels
 */
float resolve_length_value(LayoutContext* lycon, uintptr_t property, const CssValue* value) {
    if (!value) { log_debug("resolve_length_value: null value");  return 0.0f; }

    static thread_local int length_resolve_depth = 0;
    if (length_resolve_depth > 64) {
        // Cyclic or extremely deep var()/calc() chains from live CSS must fail
        // as unresolved values instead of recursing until the process stack dies.
        log_warn("resolve_length_value: exceeded CSS variable/function recursion limit");
        return NAN;
    }
    length_resolve_depth++;

    // Check if we're in "raw mode" (negative property) - used for calc() operands
    // In raw mode, NUMBER values are not multiplied by font-size for line-height
    bool raw_number_mode = (intptr_t)property < 0;
    uintptr_t effective_property = raw_number_mode ? (uintptr_t)(-(intptr_t)property) : property;

    float result = 0.0f;
    switch (value->type) {
    case CSS_VALUE_TYPE_NUMBER:
        // unitless number
        if (!raw_number_mode && effective_property == CSS_PROPERTY_LINE_HEIGHT) {
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            result = value->data.number.value * lycon->font.current_font_size;
        } else {
            // treat as pixels for most properties (or in raw mode for calc operands)
            result = (float)value->data.number.value;
        }
        break;

    case CSS_VALUE_TYPE_LENGTH: {
        double num = value->data.length.value;
        CssUnit unit = value->data.length.unit;
        switch (unit) {
        // absolute units (all in CSS logical pixels, 96 dpi reference)
        case CSS_UNIT_Q:  // 1Q = 1cm / 40
            result = num * (96 / 2.54 / 40);
            break;
        case CSS_UNIT_CM:  // 96px / 2.54
            result = num * (96 / 2.54);
            break;
        case CSS_UNIT_IN:  // 96px
            result = num * 96;
            break;
        case CSS_UNIT_MM:  // 1mm = 1cm / 10
            result = num * (96 / 25.4);
            break;
        case CSS_UNIT_PC:  // 1pc = 12pt = 1in / 6
            result = num * 16;
            break;
        case CSS_UNIT_PT:  // 1pt = 1in / 72
            result = num * 4 / 3;
            break;
        case CSS_UNIT_PX:
            result = num;  // CSS logical pixels
            break;

        // relative units
        case CSS_UNIT_REM:
            if (lycon->root_font_size < 0) {
                resolve_font_size(lycon, NULL);
                lycon->root_font_size = lycon->font.current_font_size < 0 ?
                    lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
            }
            result = num * lycon->root_font_size;
            break;
        case CSS_UNIT_EM:
            if (effective_property == CSS_PROPERTY_FONT_SIZE) {
                result = num * lycon->font.style->font_size;
            } else {
                if (lycon->font.current_font_size < 0) {
                    resolve_font_size(lycon, NULL);
                }
                result = num * lycon->font.current_font_size;
            }
            break;
        case CSS_UNIT_VW:
            // viewport width percentage (result in CSS logical pixels)
            if (lycon && lycon->width > 0) {
                result = (num / 100.0) * lycon->width;
            }
            break;
        case CSS_UNIT_VH:
            // viewport height percentage (result in CSS logical pixels)
            if (lycon && lycon->height > 0) {
                result = (num / 100.0) * lycon->height;
            }
            break;
        case CSS_UNIT_VMIN: {
            float vmin = (lycon->width < lycon->height) ? lycon->width : lycon->height;
            result = (num / 100.0) * vmin;
            break;
        }
        case CSS_UNIT_VMAX: {
            float vmax = (lycon->width > lycon->height) ? lycon->width : lycon->height;
            result = (num / 100.0) * vmax;
            break;
        }
        case CSS_UNIT_EX: {
            // relative to x-height of the font
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            float x_height_ratio = font_get_x_height_ratio(lycon->font.font_handle);
            result = num * lycon->font.current_font_size * x_height_ratio;
            break;
        }
        case CSS_UNIT_CH: {
            // CSS Values 4 §6.1.1: equal to the advance width of the "0" (zero) glyph
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            if (lycon->font.font_handle) {
                FontStyleDesc style = font_style_desc_from_prop(lycon->font.style);
                LoadedGlyph* zero_glyph = font_load_glyph(lycon->font.font_handle, &style, (uint32_t)'0', false);
                if (zero_glyph && zero_glyph->advance_x > 0.0f) {
                    float pixel_ratio = (lycon->ui_context && lycon->ui_context->pixel_ratio > 0.0f)
                        ? lycon->ui_context->pixel_ratio : 1.0f;
                    float advance = zero_glyph->advance_x / pixel_ratio;
                    if (lycon->font.style && lycon->font.style->font_size > 0.0f &&
                        lycon->font.current_font_size > 0.0f &&
                        lycon->font.style->font_size != lycon->font.current_font_size) {
                        advance *= lycon->font.current_font_size / lycon->font.style->font_size;
                    }
                    result = num * advance;
                } else {
                    result = num * lycon->font.current_font_size * 0.5f;
                }
            } else {
                result = num * lycon->font.current_font_size * 0.5f;
            }
            break;
        }

        default:
            result = num;  // fallback: assume pixels for unknown units
            break;
        }
        break;
    }
    case CSS_VALUE_TYPE_PERCENTAGE: {
        double percentage = value->data.percentage.value;
        if (effective_property == CSS_PROPERTY_FONT_SIZE || effective_property == CSS_PROPERTY_LINE_HEIGHT || effective_property == CSS_PROPERTY_VERTICAL_ALIGN) {
            // font-size percentage is relative to parent font size
            result = percentage * lycon->font.style->font_size / 100.0;
        } else if (effective_property == CSS_PROPERTY_LETTER_SPACING) {
            // CSS Text 4 defines spacing percentages against the current font
            // size; the generic percentage path would incorrectly use the parent
            // box width and make computed spacing depend on layout geometry.
            if (lycon->font.current_font_size < 0) {
                resolve_font_size(lycon, NULL);
            }
            result = percentage * lycon->font.current_font_size / 100.0;
        } else if (effective_property == CSS_PROPERTY_HEIGHT || effective_property == CSS_PROPERTY_MIN_HEIGHT ||
                   effective_property == CSS_PROPERTY_MAX_HEIGHT || effective_property == CSS_PROPERTY_TOP ||
                   effective_property == CSS_PROPERTY_BOTTOM) {
            // CSS Position 3 §3.4: For top/bottom position insets, if the containing
            // block height is indefinite (auto), any percentage makes the entire
            // value auto. Return NAN so calc() expressions also evaluate to NAN.
            bool is_position_inset = (effective_property == CSS_PROPERTY_TOP || effective_property == CSS_PROPERTY_BOTTOM);
            if (is_position_inset && lycon->block.parent && lycon->block.parent->given_height < 0) {
                result = NAN;
            } else if (lycon->block.parent && lycon->block.parent->content_height > 0) {
                result = percentage * lycon->block.parent->content_height / 100.0;
            } else if (lycon->block.parent && lycon->block.parent->given_height > 0) {
                // Parent has given height but content_height not yet calculated
                // This handles flex items with percentage heights where parent has definite height
                result = percentage * lycon->block.parent->given_height / 100.0;
            } else if (!lycon->block.parent && lycon && lycon->height > 0) {
                // No parent context (root html element) - use viewport height
                // This handles html element with height: 100%
                // Layout now uses CSS pixels, so use lycon->height directly (no pixel_ratio scaling)
                result = percentage * lycon->height / 100.0;
            } else {
                // Parent exists but has no definite height - percentage resolves differently:
                // Per CSS 2.1 §10.7: max-height percentage → 'none', min-height percentage → '0'
                // Per CSS 2.1 §10.5: height percentage → 'auto'
                if (effective_property == CSS_PROPERTY_MAX_HEIGHT) {
                    result = NAN;  // NAN → treated as 'none' (-1) by max-height handler
                } else if (effective_property == CSS_PROPERTY_HEIGHT) {
                    result = NAN;  // NAN → treated as -1 (auto) by height handler
                } else {
                    result = 0.0f;
                }
            }
        } else if (css_percentage_uses_containing_inline_size(effective_property)) {
            // CSS Box resolves margin and padding percentages against the
            // containing block's logical inline size, which is physical height
            // for vertical writing modes rather than always physical width.
            float inline_base = css_containing_inline_percentage_base(lycon);
            if (inline_base > 0.0f) {
                result = percentage * inline_base / 100.0;
            } else {
                result = 0.0f;
            }
        } else {
            // width-related and other properties: percentage relative to parent width
            if (lycon->block.parent && lycon->block.parent->content_width > 0) {
                result = percentage * lycon->block.parent->content_width / 100.0;
            } else if (!lycon->block.parent && lycon && lycon->width > 0) {
                // No parent context (root html element) - use viewport width
                // CSS 2.1 §10.3: percentage widths on the root resolve against the
                // initial containing block (viewport), same as percentage heights
                result = percentage * lycon->width / 100.0;
            } else {
                result = 0.0f;
            }
        }
        break;
    }
    case CSS_VALUE_TYPE_KEYWORD: {
        // handle special keywords like 'auto'
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_AUTO) {
            log_info("length value: auto");
            result = 0.0f;  // auto represented as 0, caller should check keyword separately
        } else if (keyword == CSS_VALUE_THIN) {
            // CSS 2.1 §8.5.1: border-width keyword 'thin' → 1px
            result = 1.0f;
        } else if (keyword == CSS_VALUE_MEDIUM) {
            // CSS 2.1 §8.5.1: border-width keyword 'medium' → 3px
            result = 3.0f;
        } else if (keyword == CSS_VALUE_THICK) {
            // CSS 2.1 §8.5.1: border-width keyword 'thick' → 5px
            result = 5.0f;
        } else {
            result = 0.0f;
        }
        break;
    }
    case CSS_VALUE_TYPE_FUNCTION: {
        // handle calc() and other CSS functions that return length values
        CssFunction* func = value->data.function;
        if (!func || !func->name) {
            log_warn("function value with no name");
            result = NAN;  // Use NAN to indicate unresolvable value
            break;
        }

        if (strcmp(func->name, "calc") == 0) {
            // calc() expression - evaluate the expression
            // For now, handle simple cases like "calc(100% - 2rem)"

            // Use negative property to enable raw number mode (no line-height multiplication)
            // while preserving the property ID for correct percentage base selection
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);

            if (func->arg_count >= 1 && func->args && func->args[0]) {
                // Check for simple binary operations in a list value
                CssValue* arg = func->args[0];
                if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count == 3) {
                    // Expect: <value1> <operator> <value2>
                    CssValue* val1 = arg->data.list.values[0];
                    CssValue* op = arg->data.list.values[1];
                    CssValue* val2 = arg->data.list.values[2];

                    if (op && op->type == CSS_VALUE_TYPE_KEYWORD) {
                        // inside calc(), resolve operands without line-height special behavior
                        // unitless numbers inside calc() stay raw, not multiplied by font-size
                        float left = resolve_length_value(lycon, raw_prop, val1);
                        float right = resolve_length_value(lycon, raw_prop, val2);
                        const CssEnumInfo* op_info = css_enum_info(op->data.keyword);
                        const char* op_name = op_info ? op_info->name : "";


                        if (!evaluate_simple_calc_operator(op_name, left, right, &result)) {
                            log_warn("calc: unknown operator '%s'", op_name);
                            result = NAN;
                        }
                    } else if (op && op->type == CSS_VALUE_TYPE_CUSTOM && op->data.custom_property.name) {
                        // Operator stored as custom property (e.g. "-" parsed as CSS_TOKEN_DELIM)
                        // inside calc(), resolve operands without line-height special behavior
                        float left = resolve_length_value(lycon, raw_prop, val1);
                        float right = resolve_length_value(lycon, raw_prop, val2);
                        const char* op_name = op->data.custom_property.name;


                        if (!evaluate_simple_calc_operator(op_name, left, right, &result)) {
                            log_warn("calc: unknown operator '%s'", op_name);
                            result = NAN;
                        }
                    } else {
                        log_warn("calc: operator is not a keyword or custom (type=%d)", op ? op->type : -1);
                        result = NAN;
                    }
                } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                    // Evaluate calc() list with operator precedence (* / before + -)
                    // and parenthesized sub-expressions (CSS parser flattens parens to
                    // KEYWORD(0) items which we handle via recursive evaluation).
                    int pos = 0;
                    result = evaluate_calc_expression(lycon, raw_prop,
                                arg->data.list.values, arg->data.list.count, &pos, 0);
                } else {
                    // Single value in calc - resolve with raw_prop for correct percentage base
                    result = resolve_length_value(lycon, raw_prop, arg);
                }
            } else {
                log_warn("calc() with no arguments");
                result = NAN;
            }

            // Note: We do NOT apply line-height unitless multiplier here because:
            // 1. calc() results lose type information - we can't distinguish calc(1.2) from calc(10px + 8px)
            // 2. The heuristic (< 10 means unitless) is too fragile for complex CSS with variables
            // 3. If the result is truly unitless for line-height, the caller should handle it
            // This matches browser behavior where calc(1.5) returns a dimensionless value,
            // and calc(10px + 8px) returns a length value.
        } else if (strcmp(func->name, "min") == 0 && func->args && func->arg_count >= 1) {
            // min(a, b, ...) — return the smallest resolved value
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            result = INFINITY;
            for (int i = 0; i < func->arg_count; i++) {
                if (!func->args[i]) continue;
                float val = resolve_length_value(lycon, raw_prop, func->args[i]);
                if (!isnan(val) && val < result) result = val;
            }
            if (isinf(result)) result = NAN;
        } else if (strcmp(func->name, "max") == 0 && func->args && func->arg_count >= 1) {
            // max(a, b, ...) — return the largest resolved value
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            result = -INFINITY;
            for (int i = 0; i < func->arg_count; i++) {
                if (!func->args[i]) continue;
                float val = resolve_length_value(lycon, raw_prop, func->args[i]);
                if (!isnan(val) && val > result) result = val;
            }
            if (isinf(result)) result = NAN;
        } else if (strcmp(func->name, "clamp") == 0 && func->args && func->arg_count >= 3) {
            // clamp(min, val, max) = max(min, min(val, max))
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            float cmin = resolve_length_value(lycon, raw_prop, func->args[0]);
            float cval = resolve_length_value(lycon, raw_prop, func->args[1]);
            float cmax = resolve_length_value(lycon, raw_prop, func->args[2]);
            if (!isnan(cmin) && !isnan(cval) && !isnan(cmax)) {
                result = fmaxf(cmin, fminf(cval, cmax));
            } else {
                result = NAN;
            }
        } else if (strcmp(func->name, "min") == 0 || strcmp(func->name, "max") == 0 ||
                   strcmp(func->name, "clamp") == 0) {
            // insufficient arguments
            result = NAN;
        } else if (strcmp(func->name, "var") == 0) {
            // var(--custom-property-name) or var(--custom-property-name, fallback)
            const char* var_name = css_var_function_name(func);

            if (var_name) {
                // Look up the variable value
                const CssValue* var_value = lookup_css_variable(lycon, var_name);
                if (var_value) {
                    // Recursively resolve the variable value
                    result = resolve_length_value(lycon, property, var_value);
                } else {
                    // Variable not found, use fallback if available
                    if (func->arg_count >= 2 && func->args[1]) {
                        result = resolve_length_value(lycon, property, func->args[1]);
                    } else {
                        result = 0.0f;
                    }
                }
            } else {
                result = 0.0f;
            }
        } else {
            log_warn("unknown CSS function: %s(), using 0 instead of NaN", func->name);
            result = 0.0f;  // Use 0 instead of NAN to prevent crash
        }
        break;
    }
    case CSS_VALUE_TYPE_LIST:
        // List of values - typically used in shorthand properties or multi-value contexts
        // For length values, just take the first value if available
        if (value->data.list.count > 0 && value->data.list.values[0]) {
            result = resolve_length_value(lycon, property, value->data.list.values[0]);
        } else {
            result = 0.0f;
        }
        break;
    case CSS_VALUE_TYPE_CUSTOM:
        // Custom property value (e.g., --main-color: red;)
        // This should not be resolved directly - it should be stored and retrieved via var()
        result = 0.0f;
        break;
    case CSS_VALUE_TYPE_VAR:
        // var() reference that wasn't handled in function case
        // This might be a standalone var reference without being wrapped in a function
        result = 0.0f;
        break;
    default:
        log_warn("unknown length value type: %d", value->type);
        result = NAN;  // Use NAN instead of 0 to indicate unresolvable value
        break;
    }

    if (value->type == CSS_VALUE_TYPE_LENGTH && !isnan(result)) {
        // CSS Viewport 1 applies effective zoom to every resolved CSS length,
        // including lengths nested inside calc().
        result *= layout_effective_zoom(lycon->view);
    }
    if (length_resolve_depth == 1 && !isnan(result)) {
        // percentages remain percentage values and use the scaled CB; only
        // the final used length is limited by the layout coordinate range.
        // css Values 4 permits approximating an actual value that cannot be
        // represented by the layout coordinate range; apply that invariant to
        // every resolved used length, including margins and calc() results.
        result = layout_clamp_dimension(result);
    }
    length_resolve_depth--;
    return result;
}

static float* inherited_spacing_slot(BoundaryProp* bound, CssPropertyCode prop_id) {
    if (!bound) return nullptr;
    switch (prop_id) {
        case CSS_PROPERTY_MARGIN_TOP: return &bound->margin.top;
        case CSS_PROPERTY_MARGIN_RIGHT: return &bound->margin.right;
        case CSS_PROPERTY_MARGIN_BOTTOM: return &bound->margin.bottom;
        case CSS_PROPERTY_MARGIN_LEFT: return &bound->margin.left;
        case CSS_PROPERTY_PADDING_TOP: return &bound->padding.top;
        case CSS_PROPERTY_PADDING_RIGHT: return &bound->padding.right;
        case CSS_PROPERTY_PADDING_BOTTOM: return &bound->padding.bottom;
        case CSS_PROPERTY_PADDING_LEFT: return &bound->padding.left;
        default: return nullptr;
    }
}

static float resolve_spacing_with_inherit(LayoutContext* lycon,
                                          CssPropertyCode prop_id,
                                          const CssValue* value) {
    if (value->type != CSS_VALUE_TYPE_KEYWORD || value->data.keyword != CSS_VALUE_INHERIT) {
        return resolve_length_value(lycon, prop_id, value);
    }
    DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    DomElement* parent = current ? dom_parent_element(current) : nullptr;
    float* inherited = parent ? inherited_spacing_slot(parent->bound, prop_id) : nullptr;
    if (inherited) {
        return *inherited;
    }
    return 0.0f;
}

static float resolve_margin_with_inherit(LayoutContext* lycon, CssPropertyCode prop_id,
                                         const CssValue* value) {
    return resolve_spacing_with_inherit(lycon, prop_id, value);
}

static bool copy_border_side_inherit(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                     int64_t specificity) {
    BorderProp* parent = parent_border_prop(lycon);
    if (!parent) return false;

    BorderProp* border = ensure_span_border(lycon, span);
    RadiantBorderSide target = radiant_border_side(border, side);
    RadiantBorderSide source = radiant_border_side(parent, side);
    *target.width = *source.width;
    *target.width_specificity = specificity;
    *target.style = *source.style;
    *target.style_specificity = specificity;
    *target.color = *source.color;
    *target.color_specificity = specificity;
    return true;
}

static float resolve_padding_with_inherit(LayoutContext* lycon, CssPropertyCode prop_id,
                                          const CssValue* value) {
    return resolve_spacing_with_inherit(lycon, prop_id, value);
}

static CssEnum resolve_box_sizing_inherit(LayoutContext* lycon) {
    DomElement* current = lycon && lycon->view
        ? lam::dom_require<DOM_NODE_ELEMENT>(lycon->view) : nullptr;
    DomElement* parent = current ? dom_parent_element(current) : nullptr;
    while (parent) {
        ViewBlock* parent_block = lam::view_as_block(parent);
        if (parent_block && parent_block->blk &&
            parent_block->block()->box_sizing != CSS_VALUE_INHERIT &&
            parent_block->block()->box_sizing != CSS_VALUE__UNDEF) {
            return parent_block->block()->box_sizing;
        }
        parent = dom_parent_element(parent);
    }
    return CSS_VALUE_CONTENT_BOX;
}

static void copy_spacing_side(Spacing* target, const Spacing* source,
                              Margin* target_margin, const Margin* source_margin,
                              CssBoxSide side, int64_t specificity) {
    float* target_value = radiant_spacing_value(target, side);
    int64_t* target_specificity = radiant_spacing_specificity(target, side);
    if (!target_value || !target_specificity || specificity < *target_specificity) return;
    *target_value = *radiant_spacing_value((Spacing*)source, side);
    *target_specificity = specificity;
    if (target_margin) {
        CssEnum* target_type = radiant_margin_type(target_margin, side);
        CssEnum* source_type = source_margin
            ? radiant_margin_type((Margin*)source_margin, side) : nullptr;
        if (target_type) *target_type = source_type ? *source_type : CSS_VALUE__UNDEF;
    }
}

// resolve property 'margin', 'padding', etc.; all four sides use the same
// shorthand expansion and cascade gate, so side-specific cases stay in one loop.
void resolve_spacing_prop(LayoutContext* lycon, uintptr_t property,
    const CssValue* src_space, int64_t specificity, Spacing* trg_spacing) {
    if (!lycon || !src_space || !trg_spacing) return;
    bool is_margin = property == CSS_PROPERTY_MARGIN;
    bool is_padding = property == CSS_PROPERTY_PADDING;
    if (src_space->type == CSS_VALUE_TYPE_KEYWORD &&
        src_space->data.keyword == CSS_VALUE_INHERIT) {
        DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
        DomElement* parent = current ? dom_parent_element(current) : nullptr;
        Spacing* source = nullptr;
        if (parent && parent->bound) {
            source = is_margin ? &parent->boundary_mut()->margin :
                is_padding ? &parent->boundary_mut()->padding :
                (parent->boundary()->border ? &parent->boundary_mut()->border->width : nullptr);
        }
        if (!source) {
            return;
        }
        Margin* target_margin = is_margin ? (Margin*)trg_spacing : nullptr;
        Margin* source_margin = is_margin ? (Margin*)source : nullptr;
        for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
            copy_spacing_side(trg_spacing, source, target_margin, source_margin,
                              (CssBoxSide)side, specificity);
        }
        return;
    }

    int value_count = src_space->type == CSS_VALUE_TYPE_LIST
        ? src_space->data.list.count : 1;
    if (value_count < 1 || value_count > 4) {
        log_warn("unexpected spacing value count: %d", value_count);
        return;
    }

    Margin parsed = {};
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        const CssValue* value = src_space->type == CSS_VALUE_TYPE_LIST
            ? css_box_shorthand_side_value(src_space, side) : src_space;
        if (!value) continue;
        float* parsed_value = radiant_spacing_value(&parsed, (CssBoxSide)side);
        *parsed_value = resolve_length_value(lycon, property, value);
        *radiant_margin_type(&parsed, (CssBoxSide)side) = css_value_axis_type(value);
    }

    Margin* target_margin = is_margin ? (Margin*)trg_spacing : nullptr;
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        copy_spacing_side(trg_spacing, &parsed, target_margin, is_margin ? &parsed : nullptr,
                          (CssBoxSide)side, specificity);
    }
}

// ============================================================================
// Grid Track Parsing Helpers
// ============================================================================

// Parse a single CssValue into a GridTrackSize
// Returns NULL if the value cannot be converted to a track size
// Forward declaration for recursive parsing
static GridTrackSize* parse_css_value_to_track_size(const CssValue* val);

// Parse minmax() function to GridTrackSize
static GridTrackSize* parse_minmax_function(const CssValue* val) {
    if (!val || val->type != CSS_VALUE_TYPE_FUNCTION) return NULL;
    if (!val->data.function->name || strcmp(val->data.function->name, "minmax") != 0) return NULL;
    if (val->data.function->arg_count < 2) return NULL;

    GridTrackSize* min_size = parse_css_value_to_track_size(val->data.function->args[0]);
    GridTrackSize* max_size = parse_css_value_to_track_size(val->data.function->args[1]);

    if (!min_size && !max_size) return NULL;

    GridTrackSize* track_size = create_grid_track_size(GRID_TRACK_SIZE_MINMAX, 0);
    if (track_size) {
        track_size->min_size = min_size;
        track_size->max_size = max_size;
    }
    return track_size;
}

// Parse repeat() function including auto-fill/auto-fit
static GridTrackSize* parse_repeat_function(const CssValue* val) {
    if (!val || val->type != CSS_VALUE_TYPE_FUNCTION) return NULL;
    if (!val->data.function->name || strcmp(val->data.function->name, "repeat") != 0) return NULL;
    if (val->data.function->arg_count < 2) return NULL;

    CssValue* count_val = val->data.function->args[0];
    bool is_auto_fill = false;
    bool is_auto_fit = false;
    int repeat_count = 0;

    // Check if first arg is auto-fill, auto-fit, or a number
    if (count_val->type == CSS_VALUE_TYPE_KEYWORD) {
        if (count_val->data.keyword == CSS_VALUE_AUTO_FILL) {
            is_auto_fill = true;
        } else if (count_val->data.keyword == CSS_VALUE_AUTO_FIT) {
            is_auto_fit = true;
        }
    } else if (count_val->type == CSS_VALUE_TYPE_NUMBER) {
        repeat_count = (int)count_val->data.number.value;
        if (repeat_count > MAX_GRID_SPAN) {
            repeat_count = MAX_GRID_SPAN;
        }
    }

    if (!is_auto_fill && !is_auto_fit && repeat_count <= 0) {
        return NULL;
    }

    // Parse the track sizes in the repeat pattern
    int track_count = val->data.function->arg_count - 1;
    GridTrackSize** repeat_tracks = (GridTrackSize**)mem_calloc(track_count, sizeof(GridTrackSize*), MEM_CAT_LAYOUT);
    if (!repeat_tracks) return NULL;

    int actual_track_count = 0;
    for (int i = 1; i < val->data.function->arg_count && actual_track_count < track_count; i++) {
        GridTrackSize* ts = parse_css_value_to_track_size(val->data.function->args[i]);
        if (ts) {
            repeat_tracks[actual_track_count++] = ts;
        }
    }

    if (actual_track_count == 0) {
        mem_free(repeat_tracks);
        return NULL;
    }

    // Create the repeat track size
    GridTrackSize* track_size = (GridTrackSize*)mem_calloc(1, sizeof(GridTrackSize), MEM_CAT_LAYOUT);
    if (!track_size) {
        mem_free(repeat_tracks);
        return NULL;
    }

    track_size->type = GRID_TRACK_SIZE_REPEAT;
    track_size->repeat_count = repeat_count;
    track_size->repeat_tracks = repeat_tracks;
    track_size->repeat_track_count = actual_track_count;
    track_size->is_auto_fill = is_auto_fill;
    track_size->is_auto_fit = is_auto_fit;


    return track_size;
}

static GridTrackSize* parse_css_value_to_track_size(const CssValue* val) {
    if (!val) return NULL;

    GridTrackSize* track_size = NULL;

    if (val->type == CSS_VALUE_TYPE_LENGTH) {
        if (val->data.length.unit == CSS_UNIT_FR) {
            // Fractional unit - store as int * 100 for precision
            int fr_value = (int)(val->data.length.value * 100);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_FR, fr_value);
        } else {
            // Regular length (px, em, etc.)
            int px_value = (int)val->data.length.value;
            track_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, px_value);
        }
    } else if (val->type == CSS_VALUE_TYPE_PERCENTAGE) {
        int percent = (int)val->data.percentage.value;
        track_size = create_grid_track_size(GRID_TRACK_SIZE_PERCENTAGE, percent);
        track_size->is_percentage = true;
    } else if (val->type == CSS_VALUE_TYPE_NUMBER) {
        // CSS spec: unitless 0 is valid as a <length> value
        int px_value = (int)val->data.number.value;
        track_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, px_value);
    } else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
        if (val->data.keyword == CSS_VALUE_AUTO) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
        } else if (val->data.keyword == CSS_VALUE_MIN_CONTENT) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MIN_CONTENT, 0);
        } else if (val->data.keyword == CSS_VALUE_MAX_CONTENT) {
            track_size = create_grid_track_size(GRID_TRACK_SIZE_MAX_CONTENT, 0);
        }
    } else if (val->type == CSS_VALUE_TYPE_FUNCTION) {
        // Handle function types: minmax(), repeat(), fit-content()
        const char* func_name = val->data.function->name;
        if (func_name) {
            if (strcmp(func_name, "minmax") == 0) {
                track_size = parse_minmax_function(val);
            } else if (strcmp(func_name, "repeat") == 0) {
                track_size = parse_repeat_function(val);
            } else if (strcmp(func_name, "fit-content") == 0) {
                // fit-content(<length-percentage>)
                track_size = create_grid_track_size(GRID_TRACK_SIZE_FIT_CONTENT, 0);
                if (track_size && val->data.function->arg_count > 0) {
                    CssValue* arg = val->data.function->args[0];
                    if (arg->type == CSS_VALUE_TYPE_LENGTH) {
                        track_size->fit_content_limit = (int)arg->data.length.value;
                        track_size->is_percentage = false;
                    } else if (arg->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        track_size->fit_content_limit = (int)arg->data.percentage.value;
                        track_size->is_percentage = true;
                    }
                }
            }
        }
    }

    return track_size;
}

static GridTrackList* replace_grid_track_list(GridTrackList** track_list_ptr, int initial_capacity) {
    if (!track_list_ptr) return NULL;
    if (*track_list_ptr) {
        destroy_grid_track_list(*track_list_ptr);
    }
    *track_list_ptr = create_grid_track_list(initial_capacity);
    return *track_list_ptr;
}

static bool css_value_can_be_grid_track_size(const CssValue* val) {
    return val && (val->type == CSS_VALUE_TYPE_LENGTH ||
                   val->type == CSS_VALUE_TYPE_PERCENTAGE ||
                   val->type == CSS_VALUE_TYPE_KEYWORD ||
                   val->type == CSS_VALUE_TYPE_NUMBER ||
                   val->type == CSS_VALUE_TYPE_FUNCTION);
}

static void append_grid_track_size(GridTrackList* track_list, GridTrackSize* track_size) {
    if (!track_size) return;
    // The first pass estimates capacity from parsed CSS values; keep this guard
    // so future valid track forms degrade without writing past the track array.
    if (!track_list || track_list->track_count >= track_list->allocated_tracks) {
        destroy_grid_track_size(track_size);
        return;
    }
    track_list->tracks[track_list->track_count++] = track_size;
}

// Parse grid track list from CSS value list, handling repeat() functions
// The list may contain: lengths, percentages, keywords, or repeat(count, track-size)
// Parse grid track list from CSS value list
// Now supports: lengths, percentages, keywords, minmax(), repeat() with auto-fill/auto-fit
static void parse_grid_track_list(const CssValue* value, GridTrackList** track_list_ptr) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST || !track_list_ptr) return;

    int count = value->data.list.count;
    CssValue** values = value->data.list.values;


    // First pass: count tracks (auto-fill/auto-fit get 1 track as placeholder)
    int total_tracks = 0;
    for (int i = 0; i < count; i++) {
        CssValue* val = values[i];
        if (!val) continue;

        if (val->type == CSS_VALUE_TYPE_FUNCTION) {
            const char* func_name = val->data.function->name;
            if (func_name && strcmp(func_name, "repeat") == 0) {
                CssValue* count_val = val->data.function->arg_count > 0 ? val->data.function->args[0] : NULL;
                if (count_val && count_val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Fixed repeat count - expand
                    int repeat_count = (int)count_val->data.number.value;
                    if (repeat_count > MAX_GRID_SPAN) repeat_count = MAX_GRID_SPAN;
                    int track_vals = val->data.function->arg_count - 1;
                    total_tracks += repeat_count * (track_vals > 0 ? track_vals : 1);
                } else {
                    // auto-fill or auto-fit - just count as 1 (will be expanded at layout time)
                    total_tracks += 1;
                }
            } else {
                // minmax(), fit-content() - count as 1 track
                total_tracks += 1;
            }
        } else if (css_value_can_be_grid_track_size(val)) {
            total_tracks++;
        }
        // Legacy CUSTOM handling for old-style parsing
        else if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;
            if (strncmp(name, "repeat(", 7) == 0 || strcmp(name, "repeat") == 0) {
                if (i + 1 < count && values[i + 1] && values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    int repeat_count = (int)values[i + 1]->data.number.value;
                    if (repeat_count > MAX_GRID_SPAN) repeat_count = MAX_GRID_SPAN;
                    int track_values = 0;
                    for (int j = i + 2; j < count; j++) {
                        CssValue* tv = values[j];
                        if (!tv || tv->type == CSS_VALUE_TYPE_CUSTOM) break;
                        if (css_value_can_be_grid_track_size(tv)) {
                            track_values++;
                        }
                    }
                    total_tracks += repeat_count * (track_values > 0 ? track_values : 1);
                }
            }
        }
    }

    if (total_tracks == 0) {
        return;
    }

    // Replace previous tracks. CSS can be resolved repeatedly for the same DOM
    // during GUI reflow, so resetting track_count would leak old track objects.
    *track_list_ptr = replace_grid_track_list(track_list_ptr, total_tracks);
    GridTrackList* track_list = *track_list_ptr;
    if (!track_list) {
        return;
    }


    // Second pass: parse values
    int i = 0;
    while (i < count) {
        CssValue* val = values[i];
        if (!val) { i++; continue; }

        // Handle FUNCTION type (modern parsing)
        if (val->type == CSS_VALUE_TYPE_FUNCTION) {
            const char* func_name = val->data.function->name;
            if (func_name && strcmp(func_name, "repeat") == 0) {
                // Check if auto-fill/auto-fit or fixed count
                CssValue* count_val = val->data.function->arg_count > 0 ? val->data.function->args[0] : NULL;
                bool is_auto = count_val && count_val->type == CSS_VALUE_TYPE_KEYWORD &&
                               (count_val->data.keyword == CSS_VALUE_AUTO_FILL ||
                                count_val->data.keyword == CSS_VALUE_AUTO_FIT);

                if (is_auto) {
                    // Keep repeat() as a single track - will be expanded at layout time
                    GridTrackSize* ts = parse_repeat_function(val);
                    if (ts) {
                        append_grid_track_size(track_list, ts);
                        track_list->is_repeat = true;
                    }
                } else if (count_val && count_val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Fixed repeat count - expand inline
                    int repeat_count = (int)count_val->data.number.value;
                    if (repeat_count > MAX_GRID_SPAN) repeat_count = MAX_GRID_SPAN;
                    for (int r = 0; r < repeat_count && track_list->track_count < track_list->allocated_tracks; r++) {
                        for (int a = 1; a < val->data.function->arg_count && track_list->track_count < track_list->allocated_tracks; a++) {
                            GridTrackSize* ts = parse_css_value_to_track_size(val->data.function->args[a]);
                            if (ts) {
                                append_grid_track_size(track_list, ts);
                            }
                        }
                    }
                }
            } else {
                // minmax() or other function
                GridTrackSize* ts = parse_css_value_to_track_size(val);
                if (ts) {
                    append_grid_track_size(track_list, ts);
                }
            }
            i++;
            continue;
        }

        // Legacy CUSTOM handling for old-style parsing
        if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;

            // Named line group: [ <ident>* ] — capture names into track_list->line_names
            if (strcmp(name, "[") == 0) {
                int line_idx = track_list->track_count; // this name belongs at the current line position
                while (++i < count) {
                    CssValue* nv = values[i];
                    if (!nv) continue;
                    // Closing bracket
                    if (nv->type == CSS_VALUE_TYPE_CUSTOM && nv->data.custom_property.name &&
                            strcmp(nv->data.custom_property.name, "]") == 0) {
                        i++; // advance past "]"
                        break;
                    }
                    // Keyword ident used as line name (e.g. "start", "end", "top", "center")
                    if (nv->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* ki = css_enum_info(nv->data.keyword);
                        if (ki && ki->name && line_idx <= track_list->allocated_tracks && !track_list->line_names[line_idx]) {
                            track_list->line_names[line_idx] = mem_strdup(ki->name, MEM_CAT_LAYOUT);
                            track_list->line_name_count++;
                        }
                        continue;
                    }
                    // Custom ident used as line name (e.g. "middle", user-defined names)
                    if (nv->type == CSS_VALUE_TYPE_CUSTOM && nv->data.custom_property.name) {
                        const char* nn = nv->data.custom_property.name;
                        if (strcmp(nn, "[") == 0) break; // malformed nested bracket
                        if (line_idx <= track_list->allocated_tracks && !track_list->line_names[line_idx]) {
                            track_list->line_names[line_idx] = mem_strdup(nn, MEM_CAT_LAYOUT);
                            track_list->line_name_count++;
                        }
                    }
                }
                continue;
            }
            if (strcmp(name, "]") == 0) { i++; continue; } // stray closing bracket

            if (strncmp(name, "repeat(", 7) == 0 || strcmp(name, "repeat") == 0) {
                i++; // Move past "repeat("
                if (i >= count || !values[i] || values[i]->type != CSS_VALUE_TYPE_NUMBER) {
                    continue;
                }
                int repeat_count = (int)values[i]->data.number.value;
                if (repeat_count > MAX_GRID_SPAN) repeat_count = MAX_GRID_SPAN;
                i++; // Move past count

                const CssValue* repeat_tracks[16];
                int repeat_track_count = 0;
                while (i < count && repeat_track_count < 16) {
                    CssValue* tv = values[i];
                    if (!tv) break;
                    if (tv->type == CSS_VALUE_TYPE_CUSTOM) { i++; break; }
                    if (css_value_can_be_grid_track_size(tv)) {
                        repeat_tracks[repeat_track_count++] = tv;
                    }
                    i++;
                }

                for (int r = 0; r < repeat_count && track_list->track_count < track_list->allocated_tracks; r++) {
                    for (int t = 0; t < repeat_track_count && track_list->track_count < track_list->allocated_tracks; t++) {
                        GridTrackSize* ts = parse_css_value_to_track_size(repeat_tracks[t]);
                        if (ts) {
                            append_grid_track_size(track_list, ts);
                        }
                    }
                }
                continue;
            }
            i++;
            continue;
        }

        // Regular track value
        GridTrackSize* ts = parse_css_value_to_track_size(val);
        if (ts) {
            append_grid_track_size(track_list, ts);
        }
        i++;
    }

}

static void apply_grid_template_track_value(const CssValue* value,
                                            GridTrackList** track_list_ptr,
                                            const char* property_name) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        if (*track_list_ptr) {
            destroy_grid_track_list(*track_list_ptr);
            *track_list_ptr = NULL;
        }
        return;
    }

    if (value->type == CSS_VALUE_TYPE_LIST) {
        parse_grid_track_list(value, track_list_ptr);
        return;
    }

    if (value->type != CSS_VALUE_TYPE_KEYWORD &&
        value->type != CSS_VALUE_TYPE_FUNCTION &&
        value->type != CSS_VALUE_TYPE_LENGTH &&
        value->type != CSS_VALUE_TYPE_PERCENTAGE) {
        return;
    }

    GridTrackSize* track_size = parse_css_value_to_track_size(value);
    if (!track_size) return;

    bool expand_repeat = track_size->type == GRID_TRACK_SIZE_REPEAT &&
        !track_size->is_auto_fill && !track_size->is_auto_fit &&
        track_size->repeat_count > 0;
    if (expand_repeat) {
        int total = track_size->repeat_count * track_size->repeat_track_count;
        *track_list_ptr = replace_grid_track_list(track_list_ptr, total);
        if (!*track_list_ptr) {
            destroy_grid_track_size(track_size);
            return;
        }
        for (int repeat = 0; repeat < track_size->repeat_count; repeat++) {
            for (int track = 0; track < track_size->repeat_track_count; track++) {
                GridTrackSize* clone = clone_grid_track_size(track_size->repeat_tracks[track]);
                if (clone) {
                    (*track_list_ptr)->tracks[(*track_list_ptr)->track_count++] = clone;
                }
            }
        }
        destroy_grid_track_size(track_size);
    } else {
        *track_list_ptr = replace_grid_track_list(track_list_ptr, 1);
        if (!*track_list_ptr) {
            destroy_grid_track_size(track_size);
            return;
        }
        (*track_list_ptr)->tracks[0] = track_size;
        (*track_list_ptr)->track_count = 1;
        if (track_size->type == GRID_TRACK_SIZE_REPEAT) {
            (*track_list_ptr)->is_repeat = true;
        }
    }

}

static bool grid_template_track_slice_is_supported(CssValue** values, int count) {
    if (!values || count <= 0) return false;

    bool has_track_size = false;
    for (int i = 0; i < count; i++) {
        CssValue* value = values[i];
        if (!value) return false;
        if (css_value_can_be_grid_track_size(value)) {
            has_track_size = true;
            continue;
        }
        // parse_grid_track_list owns the existing line-name token handling.
        if (value->type != CSS_VALUE_TYPE_CUSTOM) return false;
    }
    return has_track_size;
}

static void clear_grid_template_track_list(GridTrackList** track_list_ptr) {
    if (!track_list_ptr || !*track_list_ptr) return;
    destroy_grid_track_list(*track_list_ptr);
    *track_list_ptr = nullptr;
}

static bool apply_grid_template_shorthand(const CssValue* value, GridProp* grid) {
    if (!value || !grid) return false;

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        clear_grid_template_track_list(&grid->grid_template_rows);
        clear_grid_template_track_list(&grid->grid_template_columns);
        clear_grid_template_areas(grid);
        return true;
    }
    if (value->type != CSS_VALUE_TYPE_LIST || value->data.list.comma_separated) return false;

    CssValue** values = value->data.list.values;
    int value_count = value->data.list.count;
    int separator_index = -1;
    for (int i = 0; i < value_count; i++) {
        if (!css_grid_is_separator(values[i])) continue;
        if (separator_index >= 0) return false;
        separator_index = i;
    }
    if (separator_index <= 0 || separator_index >= value_count - 1 ||
        !grid_template_track_slice_is_supported(values, separator_index) ||
        !grid_template_track_slice_is_supported(values + separator_index + 1,
                                                value_count - separator_index - 1)) {
        return false;
    }

    CssValue rows = {};
    rows.type = CSS_VALUE_TYPE_LIST;
    rows.data.list.values = values;
    rows.data.list.count = separator_index;

    CssValue columns = {};
    columns.type = CSS_VALUE_TYPE_LIST;
    columns.data.list.values = values + separator_index + 1;
    columns.data.list.count = value_count - separator_index - 1;

    // CSS Grid §7.2: the slash form assigns row then column tracks and resets
    // grid-template-areas; leaving this shorthand unresolved left explicit tracks absent.
    apply_grid_template_track_value(&rows, &grid->grid_template_rows, "grid-template rows");
    apply_grid_template_track_value(&columns, &grid->grid_template_columns, "grid-template columns");
    clear_grid_template_areas(grid);
    return true;
}

static bool apply_grid_shorthand(const CssValue* value, GridProp* grid) {
    if (!apply_grid_template_shorthand(value, grid)) return false;

    // The `<grid-template>` branch of `grid` resets the implicit grid; otherwise
    // `grid: auto / 0` leaves an old auto column that can grow to item content.
    clear_grid_template_track_list(&grid->grid_auto_rows);
    clear_grid_template_track_list(&grid->grid_auto_columns);
    grid->grid_auto_flow = CSS_VALUE_ROW;
    grid->is_dense_packing = false;
    return true;
}

// ============================================================================
// Main Style Resolution
// ============================================================================

static bool css_property_is_font(CssPropertyCode property) {
    switch (property) {
        case CSS_PROPERTY_FONT:
        case CSS_PROPERTY_FONT_SIZE:
        case CSS_PROPERTY_FONT_FAMILY:
        case CSS_PROPERTY_FONT_WEIGHT:
        case CSS_PROPERTY_FONT_STYLE:
        case CSS_PROPERTY_FONT_VARIANT:
        case CSS_PROPERTY_LINE_HEIGHT:
            return true;
        default:
            return false;
    }
}

// Callback for AVL tree traversal - first pass (font properties only)
static bool resolve_font_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;
    StyleNode* style_node = (StyleNode*)node->declaration;
    CssPropertyCode prop_id = (CssPropertyCode)node->property_id;

    // Only process font-related properties in first pass
    // These must be resolved before width/height/etc. which may use em/ex units
    if (!css_property_is_font(prop_id)) {
        return true; // skip, will process in second pass
    }

    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) return true;

    resolve_css_property(prop_id, decl, lycon);
    return true;
}

// Callback for AVL tree traversal - second pass (non-font properties)
static bool resolve_non_font_property_callback(AvlNode* node, void* context) {
    LayoutContext* lycon = (LayoutContext*)context;
    StyleNode* style_node = (StyleNode*)node->declaration;
    CssPropertyCode prop_id = (CssPropertyCode)node->property_id;

    // Skip font properties (already processed in first pass)
    if (css_property_is_font(prop_id)) {
        return true; // already processed
    }

    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl) return true;

    resolve_css_property(prop_id, decl, lycon);
    return true;
}

static float resolve_placeholder_font_size(LayoutContext* lycon,
                                           FontProp* base_font,
                                           const CssValue* raw_value) {
    if (!lycon || !base_font || !raw_value) return -1.0f;
    const CssValue* value = resolve_var_function(lycon, raw_value);
    if (!value) return -1.0f;

    float parent_font_size = base_font->font_size > 0.0f ? base_font->font_size : 16.0f;
    float font_size = -1.0f;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        if (value->data.length.unit == CSS_UNIT_EM) {
            font_size = (float)value->data.length.value * parent_font_size;
        } else {
            font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, value);
        }
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        font_size = parent_font_size * (float)(value->data.percentage.value / 100.0);
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum kw = value->data.keyword;
        if (kw == CSS_VALUE_INHERIT) {
            font_size = parent_font_size;
        } else if (kw == CSS_VALUE_LARGER || kw == CSS_VALUE_SMALLER) {
            float scale = (kw == CSS_VALUE_LARGER) ? 1.2f : (1.0f / 1.2f);
            font_size = parent_font_size * scale;
        } else {
            font_size = map_lambda_font_size_keyword(kw);
        }
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        if (value->data.number.value == 0.0) font_size = 0.0f;
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, value);
    }
    return (!isnan(font_size) && font_size >= 0.0f) ? font_size : -1.0f;
}

static const char* placeholder_font_family_from_value(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_STRING) {
        return value->data.string;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        return value->data.custom_property.name;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    return nullptr;
}

static void apply_placeholder_font_family(FontProp* font, const CssValue* raw_value) {
    if (!font || !raw_value) return;
    const CssValue* value = raw_value;
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        for (int i = 0; i < value->data.list.count; i++) {
            const char* family = placeholder_font_family_from_value(value->data.list.values[i]);
            if (family && *family) {
                radiant_retain_font_family(font, lam::PoolPtr<char>((char*)family));
                return;
            }
        }
        return;
    }

    const char* family = placeholder_font_family_from_value(value);
    if (family && *family) {
        radiant_retain_font_family(font, lam::PoolPtr<char>((char*)family));
    }
}

static FontProp* ensure_placeholder_font(LayoutContext* lycon,
                                         FormControlProp* form,
                                         FontProp* base_font) {
    if (!lycon || !form || !base_font) return nullptr;
    if (!form->placeholder_font) {
        form->placeholder_font = alloc_font_prop(lycon);
    }
    return form->placeholder_font;
}

static void resolve_placeholder_pseudo_style(DomElement* dom_elem, LayoutContext* lycon) {
    if (!dom_elem || !lycon || dom_elem->role_kind() != DomElement::ROLE_FORM ||
        !dom_elem->form) {
        return;
    }

    FormControlProp* form = dom_elem->form;
    form->placeholder_color_r = 0;
    form->placeholder_color_g = 0;
    form->placeholder_color_b = 0;
    form->placeholder_color_a = 0;
    form->placeholder_opacity = 1.0f;
    form->placeholder_has_color = 0;
    form->placeholder_has_opacity = 0;

    if (!dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER) || !dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER)->tree) {
        form->placeholder_font = nullptr;
        return;
    }

    FontProp* base_font = dom_elem->font ? dom_elem->font : lycon->font.style;
    bool has_placeholder_font_prop =
        style_tree_get_declaration(dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_SIZE) ||
        style_tree_get_declaration(dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_WEIGHT) ||
        style_tree_get_declaration(dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_STYLE) ||
        style_tree_get_declaration(dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_FAMILY);
    if (has_placeholder_font_prop && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            *placeholder_font = *base_font;
            placeholder_font->owns_font_handle = false;
        }
    } else {
        form->placeholder_font = nullptr;
    }

    CssDeclaration* color_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_COLOR);
    if (color_decl && color_decl->value) {
        Color color = resolve_color_value(lycon, color_decl->value);
        form->placeholder_color_r = color.r;
        form->placeholder_color_g = color.g;
        form->placeholder_color_b = color.b;
        form->placeholder_color_a = color.a;
        form->placeholder_has_color = 1;
    }

    CssDeclaration* opacity_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_OPACITY);
    if (opacity_decl && opacity_decl->value) {
        const CssValue* value = resolve_var_function(lycon, opacity_decl->value);
        float opacity = 1.0f;
        if (value && value->type == CSS_VALUE_TYPE_NUMBER) {
            opacity = (float)value->data.number.value;
        } else if (value && value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            opacity = (float)(value->data.percentage.value / 100.0);
        }
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        form->placeholder_opacity = opacity;
        form->placeholder_has_opacity = 1;
    }

    CssDeclaration* font_size_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_SIZE);
    if (font_size_decl && font_size_decl->value && base_font) {
        float font_size = resolve_placeholder_font_size(lycon, base_font,
                                                        font_size_decl->value);
        if (font_size >= 0.0f) {
            FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
            if (placeholder_font) {
                placeholder_font->font_size = font_size;
                placeholder_font->font_size_from_medium = false;
            }
        }
    }

    CssDeclaration* font_weight_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_WEIGHT);
    if (font_weight_decl && font_weight_decl->value && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            placeholder_font->font_weight = map_font_weight(font_weight_decl->value);
            placeholder_font->font_weight_numeric = map_font_weight_numeric(font_weight_decl->value);
        }
    }

    CssDeclaration* font_style_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_STYLE);
    if (font_style_decl && font_style_decl->value &&
        font_style_decl->value->type == CSS_VALUE_TYPE_KEYWORD && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            placeholder_font->font_style = font_style_decl->value->data.keyword;
        }
    }

    CssDeclaration* font_family_decl = style_tree_get_declaration(
        dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER), CSS_PROPERTY_FONT_FAMILY);
    if (font_family_decl && font_family_decl->value && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            apply_placeholder_font_family(placeholder_font, font_family_decl->value);
        }
    }
}

static bool apply_chromium_monospace_font_size_quirk(StyleTree* style_tree,
                                                     FontProp* parent_font_style,
                                                     ViewSpan* span,
                                                     LayoutContext* lycon) {
    if (!style_tree || !style_tree->tree || !span || !span->font ||
        !span->fontp()->family || span->fontp()->font_size <= 0) {
        return false;
    }

    bool has_author_font_family =
        style_tree_get_declaration(style_tree, CSS_PROPERTY_FONT_FAMILY) != nullptr ||
        style_tree_get_declaration(style_tree, CSS_PROPERTY_FONT) != nullptr;
    bool current_is_mono =
        str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace");
    bool parent_is_mono = parent_font_style && parent_font_style->family &&
        str_ieq_const(parent_font_style->family, strlen(parent_font_style->family), "monospace");

    if (!has_author_font_family || !current_is_mono || parent_is_mono ||
        !span->fontp()->font_size_from_medium) {
        return false;
    }

    // Chromium CheckForGenericFamilyChange quirk: when author CSS changes to
    // generic monospace from the initial medium-sized family chain, Chrome
    // scales by defaultFixedFontSize/defaultFontSize (13/16). This must run
    // before non-font properties so em widths use the adjusted computed size.
    float original_size = span->fontp()->font_size;
    span->font->font_size = original_size * 13.0f / 16.0f;
    span->font->font_size_from_medium = false;
    if (lycon) {
        lycon->font.style = span->font;
        lycon->font.current_font_size = span->fontp()->font_size;
    }
    return true;
}

static void apply_initial_letter_used_font_size(DomElement* dom_elem,
                                                LayoutContext* lycon,
                                                const FontBox& parent_font) {
    if (!dom_elem || !lycon || !dom_elem->tag_name ||
        strcmp(dom_elem->tag_name, "::first-letter") != 0) {
        return;
    }

    InitialLetterInfo initial = {};
    if (!layout_get_initial_letter_info(dom_elem, &initial) ||
        !parent_font.style || !parent_font.font_handle ||
        lycon->block.line_height <= 0.0f) {
        return;
    }

    ViewSpan* span = lam::view_require_element(lycon->view);
    if (!span) return;
    span->ensure_font(lycon);
    if (!span->font || span->fontp()->font_size <= 0.0f) return;

    FontBox computed_font = {};
    setup_font(lycon->ui_context, &computed_font, span->font);
    const FontMetrics* parent_metrics = font_get_metrics(parent_font.font_handle);
    const FontMetrics* initial_metrics = font_get_metrics(computed_font.font_handle);
    if (!parent_metrics || !initial_metrics || parent_metrics->cap_height <= 0.0f ||
        initial_metrics->cap_height <= 0.0f) {
        return;
    }

    float computed_size = span->fontp()->font_size;
    float initial_cap_ratio = initial_metrics->cap_height / computed_size;
    if (initial_cap_ratio <= 0.0f) return;

    float target_cap_height = (initial.size - 1.0f) * lycon->block.line_height +
        parent_metrics->cap_height;
    float used_size = target_cap_height / initial_cap_ratio;
    if (used_size <= 0.0f || isnan(used_size)) return;

    // Initial letters ignore specified font-size and use their requested line span.
    // Preserve the computed size for em units while using the derived glyph size.
    span->font->initial_letter_computed_font_size = computed_size;
    span->font->font_size = used_size;
    span->font->font_size_from_medium = false;

    FontBox used_font = {};
    setup_font(lycon->ui_context, &used_font, span->font);
}

static CssValue* resolve_inherited_line_height_value(LayoutContext* lycon,
                                                     DomElement* ancestor,
                                                     const CssValue* value,
                                                     bool resolve_rem) {
    if (!lycon || !ancestor || !value) return nullptr;
    float ancestor_size = ancestor->font ? ancestor->fontp()->font_size : 0.0f;
    bool needs_compute = value->type == CSS_VALUE_TYPE_PERCENTAGE
        ? ancestor_size > 0.0f : false;
    CssUnit unit = CSS_UNIT_PX;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        unit = value->data.length.unit;
        needs_compute = (unit == CSS_UNIT_EM || unit == CSS_UNIT_EX ||
                         unit == CSS_UNIT_CH) && ancestor_size > 0.0f;
        if (resolve_rem && unit == CSS_UNIT_REM) needs_compute = true;
    }
    if (!needs_compute) return nullptr;

    CssValue* computed = (CssValue*)alloc_prop(lycon, sizeof(CssValue));
    computed->type = CSS_VALUE_TYPE_LENGTH;
    float pixels = 0.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        pixels = (float)(value->data.percentage.value * ancestor_size / 100.0);
    } else if (unit == CSS_UNIT_REM) {
        pixels = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, value);
    } else {
        float amount = (float)value->data.length.value;
        pixels = amount * ancestor_size * (unit == CSS_UNIT_EM ? 1.0f : 0.5f);
    }
    computed->data.length.value = pixels;
    computed->data.length.unit = CSS_UNIT_PX;
    return computed;
}

void resolve_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    assert(dom_elem);

    // iterate through specified_style AVL tree
    StyleTree* style_tree = dom_elem->specified_style;
    if (!style_tree || !style_tree->tree) {
        return;
    }

    // Two-pass resolution:
    // 1. First pass: Resolve font properties (font, font-size, font-family, etc.)
    //    This ensures font metrics are available for em/ex unit calculations
    // 2. Second pass: Resolve all other properties

    // Font5 §4.4: skip first-pass AVL traversal if no font properties exist.
    // Most elements (especially in markdown) inherit all font properties from
    // their parent and have zero font-related CSS declarations.
    bool has_any_font_prop = (avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_SIZE) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_FAMILY) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_WEIGHT) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_STYLE) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_VARIANT) != nullptr ||
                              avl_tree_search(style_tree->tree, CSS_PROPERTY_LINE_HEIGHT) != nullptr);

    FontProp* parent_font_style = lycon->font.style;
    FontBox parent_font = lycon->font;
    if (has_any_font_prop) {
        avl_tree_foreach_inorder(style_tree->tree, resolve_font_property_callback, lycon);
    }

    if (has_any_font_prop) {
        ViewSpan* span = lam::view_require_element(lycon->view);
        apply_chromium_monospace_font_size_quirk(style_tree, parent_font_style, span, lycon);
        if (span && span->font && span->fontp()->font_size > 0) {
            lycon->font.style = span->font;
            lycon->font.current_font_size = span->fontp()->font_size;
        }
    } else if (dom_elem->tag() == MARKUP_NAME_TEXTAREA) {
        ViewSpan* span = lam::view_require_element(lycon->view);
        if (span && span->font && span->fontp()->font_size > 0) {
            lycon->font.style = span->font;
            lycon->font.current_font_size = span->fontp()->font_size;
            if (span->fontp()->family && lycon->ui_context) {
                setup_font(lycon->ui_context, &lycon->font, span->font);
            }
        }
    }

    // Set up font face if a font-family was specified for this element
    // This ensures ex/ch units use the correct font metrics
    if (has_any_font_prop) {
        ViewSpan* span = lam::view_require_element(lycon->view);
        // Check if any property that affects glyph metrics was explicitly set on this element.
        // Font-relative units such as ch/ex need the current element's resolved face/size even
        // when only font-size changes and the family is inherited.
        bool has_font = avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_FAMILY) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_SIZE) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_WEIGHT) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_STYLE) != nullptr ||
                       avl_tree_search(style_tree->tree, CSS_PROPERTY_FONT_VARIANT) != nullptr;
        if (has_font && span && span->font && span->fontp()->family && lycon->ui_context) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
    }

    {
        ViewSpan* span = lam::view_require_element(lycon->view);
        if (span && span->font && span->fontp()->font_size > 0.0f) {
            // Non-font properties resolve em/ex/ch against the element's computed font;
            // UA/default font sizes may be present even when this style tree has no font declarations.
            lycon->font.style = span->font;
            lycon->font.current_font_size = span->fontp()->font_size;
        }
    }

    // Pre-resolve 'color' before the second pass so that currentColor references
    // (e.g. border-color: currentColor on <a>.p-btn) see the cascaded color of
    // the current element instead of falling back to a parent or UA default.
    // Border properties (and others) come before 'color' in AVL property-id order,
    // so without this they would resolve currentColor against a stale value.
    {
        AvlNode* color_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_COLOR);
        if (color_node) {
            StyleNode* style_node = (StyleNode*)color_node->declaration;
            CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
            if (decl) {
                resolve_css_property(CSS_PROPERTY_COLOR, decl, lycon);
            }
        }
    }

    avl_tree_foreach_inorder(style_tree->tree, resolve_non_font_property_callback, lycon);

    // Handle CSS inheritance for inheritable properties not explicitly set
    // Important inherited properties: font-family, font-size, font-weight, color, etc.
    static const CssPropertyCode inheritable_props[] = {
        CSS_PROPERTY_FONT_FAMILY,
        CSS_PROPERTY_FONT_SIZE,
        CSS_PROPERTY_FONT_WEIGHT,
        CSS_PROPERTY_FONT_STYLE,
        CSS_PROPERTY_FONT_VARIANT,
        CSS_PROPERTY_COLOR,
        CSS_PROPERTY_LINE_HEIGHT,
        CSS_PROPERTY_TEXT_ALIGN,
        CSS_PROPERTY_TEXT_DECORATION,
        CSS_PROPERTY_TEXT_TRANSFORM,
        CSS_PROPERTY_TEXT_INDENT,
        CSS_PROPERTY_TEXT_SPACING_TRIM,
        CSS_PROPERTY_DOMINANT_BASELINE,
        CSS_PROPERTY_LETTER_SPACING,
        CSS_PROPERTY_WORD_SPACING,
        CSS_PROPERTY_WHITE_SPACE,
        CSS_PROPERTY_FILL,
        CSS_PROPERTY_STROKE,
        CSS_PROPERTY_STROKE_WIDTH,
        CSS_PROPERTY_ACCENT_COLOR,
        CSS_PROPERTY_VISIBILITY,
        CSS_PROPERTY_EMPTY_CELLS,
        CSS_PROPERTY_DIRECTION,
        CSS_PROPERTY_LIST_STYLE_POSITION,
        CSS_PROPERTY_LIST_STYLE_TYPE,
        CSS_PROPERTY_LIST_STYLE,
        CSS_PROPERTY_RUBY_POSITION,
    };
    static const size_t num_inheritable = sizeof(inheritable_props) / sizeof(inheritable_props[0]);

    // Get parent's style tree for inheritance
    DomElement* parent = dom_parent_element(dom_elem);
    StyleTree* parent_tree = (parent && parent->specified_style)
                             ? parent->specified_style : NULL;

    // Run inheritance check if parent has either specified_style or computed font
    // This handles anonymous table elements that have font but no specified_style
    if (parent_tree || (parent && parent->font)) {

        for (size_t i = 0; i < num_inheritable; i++) {
            CssPropertyCode prop_id = inheritable_props[i];

            // Check if this property is already set on the element
            CssDeclaration* existing = style_tree_get_declaration(style_tree, prop_id);
            if (existing) {
                // Property is explicitly set, don't inherit
                continue;
            }

            if (prop_id == CSS_PROPERTY_WHITE_SPACE) {
                NameId tag = dom_elem->tag();
                ViewSpan* span = lam::view_require_element(lycon->view);
                if ((tag == MARKUP_NAME_PRE || tag == MARKUP_NAME_LISTING || tag == MARKUP_NAME_XMP) &&
                    span->blk && span->block_mut()->white_space == CSS_VALUE_PRE) {
                    // The UA preformatted declaration applies on this element,
                    // so it wins over an inherited author value from its parent.
                    continue;
                }
            }

            // HTML spec: <th> uses "-internal-center-or-inherit" UA rule.
            // This means: use center if the inherited value is the initial value (start),
            // otherwise use the inherited value. E.g.:
            //   table { text-align: start } -> th: center (start is initial, UA wins)
            //   table { text-align: end }   -> th: end (non-initial, inherit wins)
            // Explicit author rules on <th> itself are handled above by the 'existing' check.
            if (prop_id == CSS_PROPERTY_TEXT_ALIGN) {
                DomElement* cur_elem = lam::dom_require_element(lycon->view);
                if (cur_elem && cur_elem->tag_name && strcmp(cur_elem->tag_name, "th") == 0) {
                    // Peek at what ancestor would be inherited
                    bool inherited_is_noninitial = false;
                    for (DomElement* p = dom_parent_element(dom_elem);
                         p; p = dom_parent_element(p)) {
                        // Check computed value first
                        if (p->blk && p->block_mut()->text_align != CSS_VALUE__UNDEF &&
                            p->block()->text_align != CSS_VALUE_INHERIT &&
                            p->block()->text_align != CSS_VALUE_CENTER) {
                            inherited_is_noninitial = (p->block()->text_align != CSS_VALUE_START);
                            break;
                        }
                        // Fall back to specified style
                        if (p->specified_style) {
                            CssDeclaration* pd = style_tree_get_declaration(p->specified_style, CSS_PROPERTY_TEXT_ALIGN);
                            if (pd && pd->value && pd->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum v = pd->value->data.keyword;
                                if (v != CSS_VALUE__UNDEF && v != CSS_VALUE_INHERIT) {
                                    inherited_is_noninitial = (v != CSS_VALUE_START);
                                    break;
                                }
                            }
                        }
                    }
                    if (!inherited_is_noninitial) {
                        // Inherited value is initial (start) → keep UA center
                        continue;
                    }
                }
            }

            // Special case: font shorthand sets font-family directly on span->font
            // without creating a CssDeclaration, so also check if font->family is set
            if (prop_id == CSS_PROPERTY_FONT_FAMILY) {
                ViewSpan* span = lam::view_require_element(lycon->view);
                if (span->font && span->fontp()->family) {
                    continue;
                }
            }

            // Special case: font shorthand sets font-size directly on span->font
            // without creating a CssDeclaration, so also check if font_size is set
            if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                CssDeclaration* font_sh = style_tree_get_declaration(style_tree, CSS_PROPERTY_FONT);
                if (font_sh) {
                    continue;
                }
                CssDeclaration* font_family_decl = style_tree_get_declaration(style_tree, CSS_PROPERTY_FONT_FAMILY);
                ViewSpan* span = lam::view_require_element(lycon->view);
                bool has_author_monospace_family = font_family_decl && span->font &&
                    span->fontp()->family &&
                    str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace");
                if (has_author_monospace_family) {
                    continue;
                }
            }

            // Special case: font shorthand sets line-height directly on span->blk
            // without creating a CssDeclaration, so also check if line_height is set
            if (prop_id == CSS_PROPERTY_LINE_HEIGHT) {
                ViewSpan* span = lam::view_require_element(lycon->view);
                if (span->blk && span->block_mut()->line_height) {
                    continue;
                }
            }

            // heading font-size comes from the HTML UA stylesheet. It is already
            // resolved by apply_element_default_style() against the parent font
            // size, so inheritance must not overwrite it with the parent's
            // computed font-size unless author CSS explicitly set font-size.
            if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                NameId tag = dom_elem->tag();
                if (tag == MARKUP_NAME_TABLE && lycon->doc && lycon->doc->view_tree &&
                    is_quirks_mode(lycon->doc->view_tree->html_version)) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    span->ensure_font(lycon);

                    span->font->font_size = 16.0f;
                    span->font->font_size_from_medium = true;
                    continue;
                }
                if (tag == MARKUP_NAME_CODE || tag == MARKUP_NAME_KBD ||
                    tag == MARKUP_NAME_SAMP || tag == MARKUP_NAME_TT) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    bool has_ua_monospace_size = span->font && span->fontp()->family &&
                        str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace") &&
                        span->fontp()->font_size > 0 && span->fontp()->font_size_from_medium;
                    if (has_ua_monospace_size) {
                        continue;
                    }
                }
                if (tag >= MARKUP_NAME_H1 && tag <= MARKUP_NAME_H6) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    if (span->font && span->fontp()->font_size > 0) {
                        continue;
                    }
                }
                if (tag == MARKUP_NAME_SMALL || tag == MARKUP_NAME_BIG ||
                    tag == MARKUP_NAME_SUB || tag == MARKUP_NAME_SUP) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    if (span->font && span->fontp()->font_size > 0 &&
                        !span->fontp()->font_size_from_medium) {
                        continue;
                    }
                }
                if (tag == MARKUP_NAME_INPUT || tag == MARKUP_NAME_BUTTON ||
                    tag == MARKUP_NAME_SELECT || tag == MARKUP_NAME_TEXTAREA) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    bool is_textarea_ua_medium = tag == MARKUP_NAME_TEXTAREA &&
                        span->font && span->fontp()->family &&
                        str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace") &&
                        span->fontp()->font_size > 0 && span->fontp()->font_size_from_medium;
                    if (span->font && span->fontp()->font_size > 0 &&
                        (!span->fontp()->font_size_from_medium || is_textarea_ua_medium)) {
                        continue;
                    }
                }
            }

            // Property not set, check parent chain for inherited declaration
            // Walk up the parent chain until we find a declaration
            DomElement* ancestor = dom_parent_element(dom_elem);
            CssDeclaration* inherited_decl = NULL;

            // Special handling for font-family: also check ancestor's computed font->family
            // This handles cases where font shorthand was used (sets font->family without
            // creating a CSS_PROPERTY_FONT_FAMILY declaration)
            // Apply for any parent with computed font->family (handles font shorthand case)
            if (prop_id == CSS_PROPERTY_FONT_FAMILY && ancestor && ancestor->font && ancestor->fontp()->family) {
                if (dom_elem->tag() == MARKUP_NAME_TEXTAREA) {
                    CssDeclaration* own_font_family = style_tree_get_declaration(
                        style_tree, CSS_PROPERTY_FONT_FAMILY);
                    CssDeclaration* own_font = style_tree_get_declaration(
                        style_tree, CSS_PROPERTY_FONT);
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    bool has_ua_textarea_family = span && span->font && span->fontp()->family &&
                        str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace");
                    if (!own_font_family && !own_font && has_ua_textarea_family) {
                        continue;
                    }
                }
                ViewSpan* span = lam::view_require_element(lycon->view);
                span->ensure_font(lycon);

                // Copy font-family from parent's computed font
                radiant_retain_font_family(span->font, lam::PoolPtr<char>(ancestor->fontp()->family));
                continue;  // Move to next property
            }

            if ((prop_id == CSS_PROPERTY_LETTER_SPACING ||
                 prop_id == CSS_PROPERTY_WORD_SPACING) &&
                ancestor && ancestor->font) {
                ViewSpan* span = lam::view_require_element(lycon->view);
                span->ensure_font(lycon);
                if (prop_id == CSS_PROPERTY_LETTER_SPACING) {
                    span->font->letter_spacing = ancestor->font->letter_spacing;
                    span->font->letter_spacing_percent = ancestor->font->letter_spacing_percent;
                    span->font->letter_spacing_is_percent = ancestor->font->letter_spacing_is_percent;
                } else {
                    span->font->word_spacing = ancestor->font->word_spacing;
                    span->font->word_spacing_percent = ancestor->font->word_spacing_percent;
                    span->font->word_spacing_is_percent = ancestor->font->word_spacing_is_percent;
                }
                if (span->font->word_spacing_is_percent && prop_id == CSS_PROPERTY_WORD_SPACING) {
                    span->font->word_spacing = span->font->word_spacing_percent *
                        span->font->font_size / 100.0f;
                }
                if (span->font->letter_spacing_is_percent && prop_id == CSS_PROPERTY_LETTER_SPACING) {
                    span->font->letter_spacing = span->font->letter_spacing_percent *
                        span->font->font_size / 100.0f;
                }
                continue;
            }

            // Special handling for line-height: also check ancestor's computed blk->line_height
            // This handles cases where font shorthand was used (sets blk->line_height without
            // creating a CSS_PROPERTY_LINE_HEIGHT declaration)
            if (prop_id == CSS_PROPERTY_LINE_HEIGHT && ancestor && ancestor->blk && ancestor->block_mut()->line_height) {
                ViewSpan* span = lam::view_require_element(lycon->view);
                ensure_span_block(lycon, span);
                const CssValue* alh = ancestor->block()->line_height;
                // CSS 2.1 §10.8.1: <length> and <percentage> line-height values
                // are computed at the declaring element and inherited as computed
                // px. Only unitless <number> inherits the multiplier.
                // Font-relative units (em, ex, ch) and percentages must be resolved
                // against the declaring ancestor's font-size, not the child's.
                CssValue* computed = resolve_inherited_line_height_value(
                    lycon, ancestor, alh, true);
                span->blk->line_height = computed ? computed : ancestor->blk->line_height;
                continue;
            }

            // CSS 2.1 §6.1.1/§6.2.1: inherited properties inherit the
            // parent's computed value, not the parent's winning declaration.
            // Prefer the immediate parent's computed font-size when it is
            // available; declaration fallback below is for unresolved parents.
            if (prop_id == CSS_PROPERTY_FONT_SIZE && ancestor &&
                ancestor->font && ancestor->fontp()->font_size > 0) {
                ViewSpan* span = lam::view_require_element(lycon->view);
                if (span->font && span->fontp()->font_size > 0.0f &&
                    !span->fontp()->font_size_from_medium) {
                    continue;
                }
                span->ensure_font(lycon);

                // Copy font-size from parent's computed font
                span->font->font_size = ancestor->font->font_size;
                span->font->font_size_from_medium = ancestor->font->font_size_from_medium;
                continue;  // Move to next property
            }

            while (ancestor && !inherited_decl) {
                if (ancestor->specified_style) {
                    inherited_decl = style_tree_get_declaration(ancestor->specified_style, prop_id);
                    if (inherited_decl && inherited_decl->value) {
                        break; // Found it!
                    }
                }
                // BUG FIX: Was using dom_elem->parent instead of ancestor->parent!
                ancestor = dom_parent_element(ancestor);
            }

            if (inherited_decl && inherited_decl->value) {

                // CRITICAL FIX: For font-size, do NOT re-resolve the specified value
                // because em/percentage values would compound incorrectly.
                // Instead, copy the computed font-size from lycon->font.style
                // which already has the parent's computed font-size.
                if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                    ViewSpan* span = lam::view_require_element(lycon->view);
                    span->ensure_font(lycon);

                    // font is already correctly set via alloc_font_prop copying lycon->font.style
                    continue;
                }

                // CSS 2.1 §10.8.1: line-height <length>/<percentage> inherit as
                // computed px. Font-relative units must resolve against the
                // declaring ancestor's font-size, not the inheriting element's.
                if (prop_id == CSS_PROPERTY_LINE_HEIGHT && ancestor && ancestor->font
                    && inherited_decl->value) {
                    const CssValue* v = inherited_decl->value;
                    CssValue* computed = resolve_inherited_line_height_value(
                        lycon, ancestor, v, false);
                    if (computed) {
                        ViewSpan* span = lam::view_require_element(lycon->view);
                        ensure_span_block(lycon, span);
                        span->blk->line_height = computed;
                        continue;
                    }
                }

                // Apply the inherited property using the ancestor's declaration
                resolve_css_property(prop_id, inherited_decl, lycon);
            }
        }
    }

    // Finalize border widths: per CSS spec, border-width computes to 0
    // when border-style is 'none' or 'hidden' (or unset, which defaults to 'none')
    ViewSpan* span = lam::view_require_element(lycon->view);
    if (span->bound && span->boundary_mut()->border) {
        BorderProp* border = span->boundary()->border;
        // CSS 2.1 §8.5.1: initial value of border-width is 'medium' (3px).
        // When border-style is visible (not none/hidden) but border-width was never
        // explicitly set (specificity == 0, width == 0), default to medium (3px).
        // An explicit border-width:0 would have specificity >= 1, so this is safe.
        for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
            CssBoxSide box_side = (CssBoxSide)side;
            RadiantBorderSide refs = radiant_border_side(border, box_side);
            bool hidden = *refs.style == CSS_VALUE_NONE || *refs.style == CSS_VALUE_HIDDEN ||
                          *refs.style == CSS_VALUE__UNDEF;
            if (hidden) {
                if (*refs.width != 0.0f) *refs.width = 0.0f;
            } else if (*refs.width == 0.0f && *refs.width_specificity == 0) {
                *refs.width = 3.0f;
            }
        }
    }

    // CSS 2.1 §8.3, §8.4, §17.5: Certain box-model properties do not apply to
    // table-internal display types. Use the element's computed display value
    // (not view_type, which may not be set yet during CSS resolution).
    DisplayValue display = resolve_display_value((void*)dom_elem);
    CssEnum di = display.inner;
    bool is_row_or_rowgroup = (di == CSS_VALUE_TABLE_ROW ||
                               di == CSS_VALUE_TABLE_ROW_GROUP ||
                               di == CSS_VALUE_TABLE_HEADER_GROUP ||
                               di == CSS_VALUE_TABLE_FOOTER_GROUP);
    bool is_column = (di == CSS_VALUE_TABLE_COLUMN ||
                      di == CSS_VALUE_TABLE_COLUMN_GROUP);
    bool is_cell = (di == CSS_VALUE_TABLE_CELL);

    if (span->bound && (is_row_or_rowgroup || is_column || is_cell)) {
        // CSS 2.1 §8.3: Margin does not apply to table-row, table-row-group,
        // table-header-group, table-footer-group, table-column, table-column-group,
        // and table-cell.
        bool has_margin = false;
        for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
            if (*radiant_spacing_value(&span->boundary_mut()->margin,
                                       (CssBoxSide)side) != 0.0f) {
                has_margin = true;
                break;
            }
        }
        if (has_margin) {
            for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                *radiant_spacing_value(&span->boundary_mut()->margin,
                                       (CssBoxSide)side) = 0.0f;
            }
        }

        // CSS 2.1 §8.4: Padding does not apply to table-row, table-row-group,
        // table-header-group, table-footer-group, table-column, table-column-group.
        // Note: Padding DOES apply to table-cell.
        if (!is_cell) {
            bool has_padding = false;
            for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                if (*radiant_spacing_value(&span->boundary_mut()->padding,
                                           (CssBoxSide)side) != 0.0f) {
                    has_padding = true;
                    break;
                }
            }
            if (has_padding) {
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    *radiant_spacing_value(&span->boundary_mut()->padding,
                                           (CssBoxSide)side) = 0.0f;
                }
            }
        }

        // CSS 2.1 §17.5: Border handling for table-internal elements depends on
        // the border model (separated vs collapsed), which is a property of the
        // ancestor table element. In the collapsed model, borders on rows,
        // row-groups, columns contribute to conflict resolution. In the separated
        // model, borders don't apply to these elements. Since we can't determine
        // the border model here, leave borders as-is for table layout to handle.

        // CSS 2.1 §10.3, §17.5.3: 'width' does not apply to table-row,
        // table-row-group, table-header-group, table-footer-group,
        // table-column, or table-column-group elements.
        // CSS 2.1 §10.5, §17.5.3: 'height' does not apply to table-column
        // or table-column-group elements.
        if (is_row_or_rowgroup || is_column) {
            ViewBlock* block = lam::view_as_block(span);
            if (block && block->blk) {
                if (block->block()->given_width >= 0) {
                    block->blk->given_width = -1;
                    block->blk->given_width_type = CSS_VALUE_AUTO;
                    block->blk->given_width_percent = NAN;
                }
                if (is_column && block->block_mut()->given_height >= 0) {
                    block->blk->given_height = -1;
                    block->blk->given_height_type = CSS_VALUE_AUTO;
                    block->blk->given_height_percent = NAN;
                }
            }
        }
    }

    // CSS 2.1 §10.3.1, §10.6.2: 'width' and 'height' do not apply to
    // non-replaced inline elements. However, the computed values are still
    // preserved on blk->given_width/given_height so that children can
    // inherit them (CSS 2.1 §6.2.1). The actual enforcement happens in
    // intrinsic_sizing.cpp (measure_element_intrinsic_widths skips the
    // explicit width shortcut) and in layout (inline elements don't use
    // given_width for their own sizing).

    // HTML UA stylesheet: <table> elements default to box-sizing: border-box.
    // CSS Tables 3 §5: The table grid box uses border-box sizing by default.
    // Only apply to actual <table> HTML elements, not to other elements with display:table.
    // Only apply if not explicitly set by author CSS.
    bool is_html_table = (dom_elem->tag_id == MARKUP_NAME_TABLE) ||
        (dom_elem->tag_name && strcmp(dom_elem->tag_name, "table") == 0);
    if (is_html_table && di == CSS_VALUE_TABLE) {
        ViewBlock* block = lam::view_as_block(span);
        if (block && block->blk) {
            CssDeclaration* box_sizing_decl = style_tree ? style_tree_get_declaration(style_tree, CSS_PROPERTY_BOX_SIZING) : nullptr;
            if (!box_sizing_decl) {
                block->blk->box_sizing = CSS_VALUE_BORDER_BOX;
            }
        }
    }

    apply_initial_letter_used_font_size(dom_elem, lycon, parent_font);
    resolve_placeholder_pseudo_style(dom_elem, lycon);
}

struct MultiValue {
    const CssValue* length;
    const CssValue* color;
    const CssValue* style;
};

void set_multi_value(MultiValue* mv, const CssValue* value) {
    if (!mv || !value) return;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE || value->type == CSS_VALUE_TYPE_NUMBER) {
        mv->length = (CssValue*)value;
    } else if (value->type == CSS_VALUE_TYPE_COLOR) {
        mv->color = (CssValue*)value;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        if (info) {
            if (value->data.keyword == CSS_VALUE_THIN ||
                value->data.keyword == CSS_VALUE_MEDIUM ||
                value->data.keyword == CSS_VALUE_THICK) {
                mv->length = value;
                return;
            }
            switch (info->group) {
                case CSS_VALUE_GROUP_BORDER_STYLE:
                    mv->style = value;
                    break;
                case CSS_VALUE_GROUP_COLOR:
                    mv->color = value;
                    break;
                default:
                    // could be other keyword types
                    break;
            }
        }
    }
    else if (value->type == CSS_VALUE_TYPE_LIST) {
        // handle list of values
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            set_multi_value(mv, item);
        }
    }
}

static void apply_border_side_shorthand(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                        const CssValue* value, int64_t specificity) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        copy_border_side_inherit(lycon, span, side, specificity);
        return;
    }

    BorderProp* border = ensure_span_border(lycon, span);
    RadiantBorderSide refs = radiant_border_side(border, side);
    float* width = refs.width;
    int64_t* width_specificity = refs.width_specificity;
    CssEnum* style = refs.style;
    int64_t* style_specificity = refs.style_specificity;
    Color* color = refs.color;
    int64_t* color_specificity = refs.color_specificity;
    MultiValue parts = {0};
    set_multi_value(&parts, value);

    // Physical and logical aliases must share cascade and none/hidden width semantics.
    bool style_applied = parts.style && specificity >= *style_specificity;
    bool hidden_style = false;
    if (style_applied) {
        *style = parts.style->data.keyword;
        *style_specificity = specificity;
        hidden_style = *style == CSS_VALUE_NONE || *style == CSS_VALUE_HIDDEN;
        if (specificity >= *width_specificity && (hidden_style || !parts.length)) {
            *width = hidden_style ? 0.0f : 3.0f;
            *width_specificity = specificity;
        }
    }
    if (parts.length && !hidden_style && specificity >= *width_specificity) {
        *width = resolve_length_value(lycon, border_side_width_property(side), parts.length);
        *width_specificity = specificity;
    }
    if (parts.color && specificity >= *color_specificity) {
        *color = resolve_color_value(lycon, parts.color);
        *color_specificity = specificity;
    } else if (style_applied && *style != CSS_VALUE_NONE && *style != CSS_VALUE_HIDDEN &&
               specificity >= *color_specificity) {
        *color = get_current_color(lycon);
        *color_specificity = specificity;
    }
}

static void apply_dimension_constraint(LayoutContext* lycon, ViewBlock* block,
                                       CssPropertyCode prop_id, const CssValue* value) {
    BlockProp* props = ensure_span_block(lycon, block);
    DomElement* parent = (lycon->elmt && lycon->elmt->parent)
        ? lycon->elmt->parent->as_element() : nullptr;
    ViewBlock* parent_block = lam::view_as_block(parent);
    BlockProp* parent_props = parent_block ? parent_block->blk : nullptr;
    bool horizontal = prop_id == CSS_PROPERTY_MIN_WIDTH ||
        prop_id == CSS_PROPERTY_MAX_WIDTH;
    bool is_maximum = prop_id == CSS_PROPERTY_MAX_WIDTH ||
        prop_id == CSS_PROPERTY_MAX_HEIGHT;
    if (prop_id != CSS_PROPERTY_MIN_WIDTH && prop_id != CSS_PROPERTY_MAX_WIDTH &&
        prop_id != CSS_PROPERTY_MIN_HEIGHT && prop_id != CSS_PROPERTY_MAX_HEIGHT) return;
    LayoutAxisConstraintRefs axis(props, horizontal);
    LayoutAxisConstraintRefs parent_axis(parent_props, horizontal);
    float* constraint = is_maximum ? axis.maximum : axis.minimum;
    float* percentage = is_maximum ? axis.maximum_percent : axis.minimum_percent;
    CssEnum* constraint_type = is_maximum ? axis.maximum_type : axis.minimum_type;
    float parent_constraint = parent_props
        ? (is_maximum ? *parent_axis.maximum : *parent_axis.minimum) : 0.0f;
    CssEnum parent_constraint_type = parent_props
        ? (is_maximum ? *parent_axis.maximum_type : *parent_axis.minimum_type)
        : CSS_VALUE_AUTO;

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        if (parent_props) {
            *constraint = parent_constraint;
            *constraint_type = parent_constraint_type;
        } else if (is_maximum) {
            *constraint = -1.0f;
            *constraint_type = CSS_VALUE_NONE;
        }
        return;
    }
    if (is_maximum && value->type == CSS_VALUE_TYPE_KEYWORD &&
        value->data.keyword == CSS_VALUE_NONE) {
        *constraint = -1.0f;
        *constraint_type = CSS_VALUE_NONE;
        return;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_INITIAL ||
         value->data.keyword == CSS_VALUE_UNSET ||
         value->data.keyword == CSS_VALUE_REVERT)) {
        // CSS-wide values use the property's initial constraint: min-* is
        // auto/zero, while max-* is none; resolving `initial` as 0 would
        // incorrectly clamp an abspos box to its border and padding.
        *constraint = is_maximum ? -1.0f : 0.0f;
        *constraint_type = is_maximum ? CSS_VALUE_NONE : CSS_VALUE_AUTO;
        *percentage = NAN;
        return;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD &&
        (value->data.keyword == CSS_VALUE_MIN_CONTENT ||
         value->data.keyword == CSS_VALUE_MAX_CONTENT ||
         value->data.keyword == CSS_VALUE_FIT_CONTENT)) {
        // Intrinsic constraints resolve only after their content contribution is known.
        *constraint = -1.0f;
        *constraint_type = value->data.keyword;
        *percentage = NAN;
        return;
    }
    if (is_maximum && prop_id == CSS_PROPERTY_MAX_WIDTH &&
        value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name &&
        strcmp(value->data.function->name, "fit-content") == 0 &&
        value->data.function->arg_count >= 1 && value->data.function->args[0]) {
        // Keep the fit-content clamp until intrinsic contributions are known;
        // resolving the function as a plain length would clamp to zero first.
        const CssValue* limit = value->data.function->args[0];
        float resolved = resolve_length_value(lycon, prop_id, limit);
        *constraint = isnan(resolved) ? (is_maximum ? -1.0f : 0.0f) : resolved;
        *constraint_type = CSS_VALUE_FIT_CONTENT;
        *percentage = limit->type == CSS_VALUE_TYPE_PERCENTAGE
            ? limit->data.percentage.value : NAN;
        return;
    }
    if (prop_id == CSS_PROPERTY_MAX_WIDTH &&
        value->type == CSS_VALUE_TYPE_PERCENTAGE && lycon->block.parent &&
        lycon->block.parent->given_width < 0.0f &&
        lycon->block.parent->content_width <= 0.0f) {
        *constraint = -1.0f;
    } else {
        float resolved = resolve_length_value(lycon, prop_id, value);
        *constraint = isnan(resolved) ? (is_maximum ? -1.0f : 0.0f) : resolved;
        *constraint_type = value->type == CSS_VALUE_TYPE_KEYWORD
            ? value->data.keyword : CSS_VALUE__UNDEF;
    }
    *percentage = value->type == CSS_VALUE_TYPE_PERCENTAGE
        ? value->data.percentage.value : NAN;
}

static void css_resolve_keyword_pair(const CssValue* value, CssEnum initial,
                                     CssEnum* first, CssEnum* second) {
    *first = initial;
    *second = initial;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        *first = value->data.keyword;
        *second = value->data.keyword;
        return;
    }
    if (value->type != CSS_VALUE_TYPE_LIST || value->data.list.count < 1) return;
    CssValue* first_value = value->data.list.values[0];
    if (first_value && first_value->type == CSS_VALUE_TYPE_KEYWORD) {
        *first = first_value->data.keyword;
    }
    CssValue* second_value = value->data.list.count >= 2
        ? value->data.list.values[1] : nullptr;
    *second = second_value && second_value->type == CSS_VALUE_TYPE_KEYWORD
        ? second_value->data.keyword : *first;
}

static void css_set_flex_item_values(DomElement* span,
                                     float grow, float shrink, float basis,
                                     bool basis_is_percent) {
    // Parent-item state is independent of the role union; form controls use
    // the same FlexItemProp as every other flex item.
    span->fi->flex_grow = grow;
    span->fi->flex_shrink = shrink;
    span->fi->flex_basis = basis;
    span->fi->flex_basis_is_percent = basis_is_percent;
    span->fi->flex_basis_is_content = false;
    span->fi->flex_basis_is_stretch = false;
}

static void css_set_flex_basis_value(DomElement* span, float basis,
                                     bool is_percent, bool is_content, bool is_stretch) {
    span->fi->flex_basis = basis;
    span->fi->flex_basis_is_percent = is_percent;
    span->fi->flex_basis_is_content = is_content;
    span->fi->flex_basis_is_stretch = is_stretch;
}

static void css_apply_list_style_keyword(LayoutContext* lycon, ViewSpan* span,
                                         CssEnum keyword, bool list_member) {
    const CssEnumInfo* info = css_enum_info(keyword);
    if (info && info->name &&
        (strcmp(info->name, "inside") == 0 || strcmp(info->name, "outside") == 0)) {
        span->blk->list_style_position = keyword;
        return;
    }
    if (keyword >= CSS_VALUE_DISC && keyword <= 0x0190) {
        span->blk->list_style_type = keyword;
        return;
    }
    if (keyword != CSS_VALUE_NONE) {
        return;
    }

    bool type_already_set = list_member && span->block_mut()->list_style_type != 0 &&
        span->block()->list_style_type != CSS_VALUE_NONE;
    if (!type_already_set) {
        span->blk->list_style_type = CSS_VALUE_NONE;
    }
    if (!list_member || type_already_set) {
        span->blk->list_style_image = (char*)alloc_prop(lycon, 5);
        str_copy(span->block()->list_style_image, 5, "none", 4);
    }
}

static bool css_list_style_custom_position(const char* name, CssEnum* out_position) {
    if (!name || !out_position) return false;
    if (strcmp(name, "inside") == 0) *out_position = (CssEnum)1;
    else if (strcmp(name, "outside") == 0) *out_position = (CssEnum)2;
    else return false;
    return true;
}

static void css_store_list_style_type_string(LayoutContext* lycon, ViewSpan* span,
                                             const char* marker) {
    if (!marker) return;
    size_t length = strlen(marker);
    span->blk->list_style_type_string = (char*)alloc_prop(lycon, length + 1);
    str_copy(span->block()->list_style_type_string, length + 1, marker, length);
    span->blk->list_style_type = CSS_VALUE_NONE;
}

static const char* css_list_style_image_url(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_URL) return value->data.url;
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name || strcmp(value->data.function->name, "url") != 0 ||
        value->data.function->arg_count <= 0 || !value->data.function->args[0]) return nullptr;
    CssValue* argument = value->data.function->args[0];
    return argument->type == CSS_VALUE_TYPE_STRING ? argument->data.string
        : argument->type == CSS_VALUE_TYPE_URL ? argument->data.url : nullptr;
}

static bool css_store_list_style_image(LayoutContext* lycon, ViewSpan* span,
                                       const CssValue* value) {
    const char* url = css_list_style_image_url(value);
    if (!url) return false;
    size_t length = strlen(url);
    span->blk->list_style_image = (char*)alloc_prop(lycon, length + 1);
    str_copy(span->block()->list_style_image, length + 1, url, length);
    return true;
}

static const CssValue* css_fit_content_function_limit(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name ||
        strcmp(value->data.function->name, "fit-content") != 0 ||
        value->data.function->arg_count < 1) {
        return nullptr;
    }
    return value->data.function->args[0];
}

static void resolve_grid_auto_track(LayoutContext* lycon, ViewBlock* block,
                                    const CssValue* value, bool rows) {
    if (!block) return;
    alloc_grid_prop(lycon, block);
    GridProp* grid = block->embedp()->grid;
    GridTrackList** tracks = rows ? &grid->grid_auto_rows : &grid->grid_auto_columns;

    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        if (*tracks) {
            destroy_grid_track_list(*tracks);
            *tracks = nullptr;
        }
        return;
    }

    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        GridTrackSize* track = parse_css_value_to_track_size(value);
        if (!track) return;
        *tracks = replace_grid_track_list(tracks, 1);
        if (!*tracks) {
            destroy_grid_track_size(track);
            return;
        }
        (*tracks)->tracks[0] = track;
        (*tracks)->track_count = 1;
        return;
    }

    if (value->type == CSS_VALUE_TYPE_LIST) {
        parse_grid_track_list(value, tracks);
    }
}

static void resolve_css_axis_size(LayoutContext* lycon, ViewBlock* block,
                                  const CssValue* value,
                                  bool horizontal) {
    const CssValue* fit_limit = css_fit_content_function_limit(value);
    CssPropertyCode axis_property = horizontal ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
    float size = -1.0f;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        DomElement* current = lycon->elmt && lycon->elmt->is_element()
            ? lycon->elmt->as_element() : nullptr;
        DomElement* parent = current ? dom_parent_element(current) : nullptr;
        ViewBlock* parent_block = parent ? lam::view_as_block(parent) : nullptr;
        float inherited = parent_block && parent_block->blk
            ? layout_axis_given_size(parent_block->block(),
                horizontal ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y) : -1.0f;
        size = inherited >= 0.0f ? inherited : -1.0f;
    } else if (fit_limit || (value->type == CSS_VALUE_TYPE_KEYWORD &&
               (value->data.keyword == CSS_VALUE_AUTO ||
                value->data.keyword == CSS_VALUE_MAX_CONTENT ||
                value->data.keyword == CSS_VALUE_MIN_CONTENT ||
                value->data.keyword == CSS_VALUE_FIT_CONTENT ||
                value->data.keyword == CSS_VALUE_STRETCH))) {
        size = -1.0f;
    } else {
        size = resolve_length_value(lycon, axis_property, value);
        size = isnan(size) ? -1.0f : max(size, 0.0f);
    }

    if (horizontal) lycon->block.given_width = size;
    else lycon->block.given_height = size;
    if (!block) return;
    ensure_span_block(lycon, block);
    LayoutAxisConstraintRefs refs(block->block_mut(), horizontal);
    *refs.given = size;
    *refs.given_type = fit_limit ? CSS_VALUE_FIT_CONTENT
        : value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
    float* fit_size = horizontal ? &block->blk->given_width_fit_content_limit
                                 : &block->blk->given_height_fit_content_limit;
    float* fit_percent = horizontal ? &block->blk->given_width_fit_content_percent
                                    : &block->blk->given_height_fit_content_percent;
    *fit_size = -1.0f;
    *fit_percent = NAN;
    if (fit_limit) {
        if (fit_limit->type == CSS_VALUE_TYPE_PERCENTAGE) {
            *fit_percent = fit_limit->data.percentage.value;
        } else {
            float resolved = resolve_length_value(lycon, axis_property, fit_limit);
            if (!isnan(resolved)) *fit_size = max(resolved, 0.0f);
        }
    }
    *refs.given_percent = value->type == CSS_VALUE_TYPE_PERCENTAGE
        ? value->data.percentage.value : NAN;
}

static void resolve_gap_property(LayoutContext* lycon, ViewBlock* block,
                                 CssPropertyCode property, const CssValue* value,
                                 bool row) {
    if (!block) return;
    float gap = 0.0f;
    bool percent = false;
    bool normal = !row && value->type == CSS_VALUE_TYPE_KEYWORD &&
        value->data.keyword == CSS_VALUE_NORMAL;
    if (normal) {
        gap = 16.0f;
    } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
               value->type == CSS_VALUE_TYPE_NUMBER) {
        gap = resolve_length_value(lycon, property, value);
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        gap = value->data.percentage.value;
        percent = true;
    }

    alloc_flex_prop(lycon, block);
    alloc_grid_prop(lycon, block);
    if (row) {
        block->embedp()->flex->row_gap = gap;
        block->embedp()->flex->row_gap_is_percent = percent;
        block->embedp()->grid->row_gap = gap;
        block->embedp()->grid->row_gap_is_percent = percent;
    } else {
        block->embedp()->flex->column_gap = gap;
        block->embedp()->flex->column_gap_is_percent = percent;
        block->embedp()->grid->column_gap = gap;
        block->embedp()->grid->column_gap_is_percent = percent;
        ensure_multicol_prop(lycon, block);
        block->multicol_prop()->column_gap = gap;
        block->multicol_prop()->column_gap_is_normal = normal;
    }
}

static MultiColumnProp* resolve_multicol_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return nullptr;
    ensure_multicol_prop(lycon, block);
    return block->multicol_prop();
}

static void resolve_multicol_dimension(LayoutContext* lycon, ViewBlock* block,
                                       const CssValue* value, CssPropertyCode property,
                                       bool height, bool allow_number,
                                       const char* name) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value) return;
    float* target = height ? &multicol->column_height : &multicol->column_width;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        *target = 0.0f;
    } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
               (allow_number && value->type == CSS_VALUE_TYPE_NUMBER)) {
        float size = resolve_length_value(lycon, property, value);
        if (size > 0.0f) {
            *target = size;
        }
    }
}

static void resolve_multicol_count(LayoutContext* lycon, ViewBlock* block,
                                   const CssValue* value) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value) return;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        multicol->column_count = 0;
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        int count = (int)value->data.number.value; // INT_CAST_OK: column count
        if (count > 0) multicol->column_count = count;
    }
}

static void resolve_multicol_rule_width(LayoutContext* lycon, ViewBlock* block,
                                        const CssValue* value) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value) return;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        multicol->rule_width = resolve_length_value(
            lycon, CSS_PROPERTY_COLUMN_RULE_WIDTH, value);
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        switch (value->data.keyword) {
            case CSS_VALUE_THIN: multicol->rule_width = 1.0f; break;
            case CSS_VALUE_MEDIUM: multicol->rule_width = 3.0f; break;
            case CSS_VALUE_THICK: multicol->rule_width = 5.0f; break;
            default: return;
        }
    } else {
        return;
    }
}

static void resolve_multicol_rule_style(LayoutContext* lycon, ViewBlock* block,
                                        const CssValue* value) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value || value->type != CSS_VALUE_TYPE_KEYWORD) return;
    multicol->rule_style = value->data.keyword;
}

static void resolve_multicol_rule_color(LayoutContext* lycon, ViewBlock* block,
                                        const CssValue* value) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value || value->type != CSS_VALUE_TYPE_COLOR) return;
    multicol->rule_color = resolve_color_value(lycon, value);
}

static void resolve_break_value(LayoutContext* lycon, ViewBlock* block,
                               const CssValue* value, CssEnum* target,
                               const char* name) {
    if (!block || !target || !value || value->type != CSS_VALUE_TYPE_KEYWORD) return;
    ensure_span_block(lycon, block);
    *target = value->data.keyword;
}

static void resolve_line_count_value(LayoutContext* lycon, ViewBlock* block,
                                     const CssValue* value, int* target,
                                     const char* name) {
    if (!block || !target || !value || value->type != CSS_VALUE_TYPE_NUMBER) return;
    ensure_span_block(lycon, block);
    int count = (int)value->data.number.value; // INT_CAST_OK: line count
    if (count > 0) {
        *target = count;
    }
}

static void resolve_flow_break_property(LayoutContext* lycon, ViewBlock* block,
                                        CssPropertyCode property, const CssValue* value) {
    if (!block) return;
    ensure_span_block(lycon, block);
    if (property == CSS_PROPERTY_BREAK_BEFORE || property == CSS_PROPERTY_PAGE_BREAK_BEFORE) {
        resolve_break_value(lycon, block, value, &block->blk->break_before, "break-before");
    } else if (property == CSS_PROPERTY_BREAK_AFTER || property == CSS_PROPERTY_PAGE_BREAK_AFTER) {
        resolve_break_value(lycon, block, value, &block->blk->break_after, "break-after");
    } else if (property == CSS_PROPERTY_ORPHANS) {
        resolve_line_count_value(lycon, block, value, &block->blk->orphans, "orphans");
    } else {
        resolve_line_count_value(lycon, block, value, &block->blk->widows, "widows");
    }
}

static bool css_text_decoration_style_keyword(CssEnum keyword) {
    const CssEnumInfo* info = css_enum_info(keyword);
    return info && (info->group == CSS_VALUE_GROUP_TEXT_DECO_STYLE ||
        (info->group == CSS_VALUE_GROUP_BORDER_STYLE &&
         (keyword == CSS_VALUE_SOLID || keyword == CSS_VALUE_DOUBLE ||
          keyword == CSS_VALUE_DOTTED || keyword == CSS_VALUE_DASHED)));
}

static void resolve_text_decoration_property(LayoutContext* lycon, ViewSpan* span,
                                             CssPropertyCode property,
                                             const CssValue* value) {
    span->ensure_font(lycon);
    FontProp* font = span->font;
    if (property == CSS_PROPERTY_TEXT_DECORATION) {
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            CssEnum keyword = value->data.keyword;
            if (keyword != CSS_VALUE__UNDEF) font->text_deco = keyword;
        } else if (value->type == CSS_VALUE_TYPE_LIST) {
            for (int i = 0; i < value->data.list.count; i++) {
                CssValue* item = value->data.list.values[i];
                if (!item) continue;
                if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                    CssEnum keyword = item->data.keyword;
                    const CssEnumInfo* info = css_enum_info(keyword);
                    if (!info) continue;
                    if (info->group == CSS_VALUE_GROUP_TEXT_DECO_LINE) {
                        font->text_deco = keyword;
                    } else if (css_text_decoration_style_keyword(keyword)) {
                        font->text_deco_style = keyword;
                    } else if (info->group == CSS_VALUE_GROUP_COLOR ||
                               info->group == CSS_VALUE_GROUP_SYSTEM_COLOR) {
                        font->text_deco_color = color_name_to_rgb(keyword);
                    }
                } else if (item->type == CSS_VALUE_TYPE_COLOR ||
                           item->type == CSS_VALUE_TYPE_FUNCTION) {
                    font->text_deco_color = resolve_color_value(lycon, item);
                }
            }
        }
        return;
    }

    if (property == CSS_PROPERTY_TEXT_DECORATION_COLOR) {
        Color color = resolve_color_value(lycon, value);
        if (color.a > 0) font->text_deco_color = color;
    } else if (property == CSS_PROPERTY_TEXT_DECORATION_THICKNESS) {
        if (value->type == CSS_VALUE_TYPE_LENGTH) {
            float thickness = resolve_length_value(lycon, property, value);
            if (!isnan(thickness) && thickness > 0.0f) font->text_deco_thickness = thickness;
        }
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE__UNDEF) return;
        if (property == CSS_PROPERTY_TEXT_DECORATION_LINE) font->text_deco = keyword;
        else if (property == CSS_PROPERTY_TEXT_DECORATION_STYLE) font->text_deco_style = keyword;
    }
}

static void resolve_float_clear_property(LayoutContext* lycon, ViewBlock* block,
                                         CssPropertyCode property,
                                         const CssValue* value) {
    if (!block || value->type != CSS_VALUE_TYPE_KEYWORD) return;
    block->ensure_position(lycon);
    CssEnum* slot = property == CSS_PROPERTY_FLOAT
        ? &block->position->float_prop : &block->position->clear;
    CssEnum keyword = value->data.keyword;
    if (keyword == CSS_VALUE_INHERIT) {
        DomElement* parent = lycon->elmt->parent ? lycon->elmt->parent->as_element() : nullptr;
        if (parent && parent->position) keyword = property == CSS_PROPERTY_FLOAT
            ? parent->positionp()->float_prop : parent->positionp()->clear;
        else keyword = CSS_VALUE_NONE;
    }
    if (keyword > 0 && keyword != CSS_VALUE_INHERIT) *slot = keyword;
}

static void resolve_font_spacing_property(LayoutContext* lycon, ViewSpan* span,
                                          CssPropertyCode property,
                                          const CssValue* value) {
    bool letter = property == CSS_PROPERTY_LETTER_SPACING;
    span->ensure_font(lycon);
    bool length_value = value->type == CSS_VALUE_TYPE_LENGTH ||
        (letter && (value->type == CSS_VALUE_TYPE_PERCENTAGE ||
                    value->type == CSS_VALUE_TYPE_FUNCTION));
    if (length_value) {
        float spacing = resolve_length_value(lycon, property, value);
        if (letter) {
            span->font->letter_spacing = spacing;
            span->font->letter_spacing_is_percent = value->type == CSS_VALUE_TYPE_PERCENTAGE;
            span->font->letter_spacing_percent = span->font->letter_spacing_is_percent
                ? (float)value->data.percentage.value : 0.0f;
        } else {
            span->font->word_spacing = spacing;
        }
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
               value->data.keyword == CSS_VALUE_NORMAL) {
        if (letter) {
            span->font->letter_spacing = 0.0f;
            span->font->letter_spacing_is_percent = false;
            span->font->letter_spacing_percent = 0.0f;
        } else {
            span->font->word_spacing = 0.0f;
        }
    }
}

static void resolve_simple_keyword_property(LayoutContext* lycon, ViewSpan* span,
                                            ViewBlock* block, CssPropertyCode property,
                                            const CssValue* value) {
    if (property == CSS_PROPERTY_FONT_KERNING) {
        span->ensure_font(lycon);
        if (value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == CSS_VALUE_NONE ||
             value->data.keyword == CSS_VALUE_NORMAL ||
             value->data.keyword == CSS_VALUE_AUTO)) {
            span->font->font_kerning = value->data.keyword;
        }
        return;
    }
    if (property == CSS_PROPERTY_CURSOR) {
        span->ensure_inline(lycon);
        resolve_keyword_slot(value, &span->in_line->cursor);
        return;
    }
    if (property == CSS_PROPERTY_CARET_SHAPE) {
        span->ensure_inline(lycon);
        if (value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == CSS_VALUE_AUTO || value->data.keyword == CSS_VALUE_BAR ||
             value->data.keyword == CSS_VALUE_BLOCK || value->data.keyword == CSS_VALUE_UNDERSCORE)) {
            span->in_line->caret_shape = value->data.keyword;
        }
        return;
    }
    if (property == CSS_PROPERTY_TEXT_ALIGN_LAST) {
        if (!block) return;
        ensure_span_block(lycon, block);
        if (value->type == CSS_VALUE_TYPE_KEYWORD &&
            value->data.keyword != CSS_VALUE_INHERIT &&
            value->data.keyword != CSS_VALUE__UNDEF) {
            block->blk->text_align_last = value->data.keyword;
        }
        return;
    }
    if (property == CSS_PROPERTY_BASELINE_SOURCE) {
        if (!block) return;
        ensure_span_block(lycon, block);
        if (value->type == CSS_VALUE_TYPE_KEYWORD &&
            (value->data.keyword == CSS_VALUE_AUTO || value->data.keyword == CSS_VALUE_FIRST ||
             value->data.keyword == CSS_VALUE_LAST)) {
            block->blk->baseline_source = value->data.keyword;
        }
        return;
    }
    if (property == CSS_PROPERTY_DOMINANT_BASELINE) {
        if (!block) return;
        ensure_span_block(lycon, block);
        if (value->type == CSS_VALUE_TYPE_KEYWORD) block->blk->dominant_baseline = value->data.keyword;
        return;
    }
    if (property == CSS_PROPERTY_MIX_BLEND_MODE) {
        span->ensure_inline(lycon);
        resolve_keyword_slot(value, &span->in_line->mix_blend_mode);
        return;
    }
    if (property == CSS_PROPERTY_TEXT_OVERFLOW && !block) return;
    BlockProp* props = ensure_span_block(lycon, span);
    CssEnum* slot = nullptr;
    switch (property) {
        case CSS_PROPERTY_TEXT_TRANSFORM: slot = &props->text_transform; break;
        case CSS_PROPERTY_TEXT_WRAP_STYLE: slot = &props->text_wrap_style; break;
        case CSS_PROPERTY_TEXT_OVERFLOW: slot = &props->text_overflow; break;
        case CSS_PROPERTY_WORD_BREAK: slot = &props->word_break; break;
        case CSS_PROPERTY_LINE_BREAK: slot = &props->line_break; break;
        case CSS_PROPERTY_WORD_WRAP:
        case CSS_PROPERTY_OVERFLOW_WRAP: slot = &props->overflow_wrap; break;
        case CSS_PROPERTY_WHITE_SPACE: slot = &props->white_space; break;
        case CSS_PROPERTY_TEXT_SPACING_TRIM: slot = &props->text_spacing_trim; break;
        default: return;
    }
    resolve_keyword_slot(value, slot);
}

static void resolve_background_keyword_property(LayoutContext* lycon, ViewSpan* span,
                                                CssPropertyCode property,
                                                const CssValue* value) {
    ensure_span_background(lycon, span);
    BackgroundProp* background = span->boundary()->background;
    CssEnum* slot = nullptr;
    switch (property) {
        case CSS_PROPERTY_BACKGROUND_ATTACHMENT: slot = &background->bg_attachment; break;
        case CSS_PROPERTY_BACKGROUND_ORIGIN: slot = &background->bg_origin; break;
        case CSS_PROPERTY_BACKGROUND_CLIP: slot = &background->bg_clip; break;
        case CSS_PROPERTY_BACKGROUND_BLEND_MODE: slot = &background->blend_mode; break;
        default: return;
    }
    resolve_keyword_slot(value, slot);
}

static void resolve_outline_longhand(LayoutContext* lycon, ViewSpan* span,
                                     CssPropertyCode property, const CssValue* value) {
    ensure_span_outline(lycon, span);
    OutlineProp* outline = span->boundary()->outline;
    switch (property) {
        case CSS_PROPERTY_OUTLINE_STYLE:
            if (value->type == CSS_VALUE_TYPE_KEYWORD) outline->style = value->data.keyword;
            break;
        case CSS_PROPERTY_OUTLINE_WIDTH:
            outline->width = resolve_length_value(lycon, property, value);
            break;
        case CSS_PROPERTY_OUTLINE_COLOR:
            outline->color = resolve_color_value(lycon, value);
            break;
        case CSS_PROPERTY_OUTLINE_OFFSET:
            outline->offset = resolve_length_value(lycon, property, value);
            break;
        default: break;
    }
}

static void resolve_inline_color_property(LayoutContext* lycon, ViewSpan* span,
                                          CssPropertyCode property,
                                          const CssValue* value) {
    span->ensure_inline(lycon);
    if (property == CSS_PROPERTY_COLOR) {
        span->in_line->color = resolve_color_value(lycon, value);
        span->in_line->has_color = true;
    } else if (property == CSS_PROPERTY_ACCENT_COLOR) {
        span->in_line->has_accent_color =
            !(value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO);
        if (span->in_line->has_accent_color) {
            span->in_line->accent_color = resolve_color_value(lycon, value);
        }
    } else {
        bool fill = property == CSS_PROPERTY_FILL;
        bool none = value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE;
        if (fill) {
            span->in_line->has_svg_fill = true;
            span->in_line->svg_fill_none = none;
            if (!none) span->in_line->svg_fill_color = resolve_color_value(lycon, value);
        } else {
            span->in_line->has_svg_stroke = true;
            span->in_line->svg_stroke_none = none;
            if (!none) span->in_line->svg_stroke_color = resolve_color_value(lycon, value);
        }
    }
}

static void resolve_inline_visibility_opacity(LayoutContext* lycon, ViewSpan* span,
                                              CssPropertyCode property,
                                              const CssValue* value) {
    span->ensure_inline(lycon);
    if (property == CSS_PROPERTY_VISIBILITY) {
        if (value->type != CSS_VALUE_TYPE_KEYWORD) return;
        span->in_line->visibility = value->data.keyword == CSS_VALUE_HIDDEN ? VIS_HIDDEN
            : value->data.keyword == CSS_VALUE_COLLAPSE ? VIS_COLLAPSE : VIS_VISIBLE;
        return;
    }
    float opacity;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) opacity =
        (float)value->data.percentage.value / 100.0f;
    else if (value->type == CSS_VALUE_TYPE_NUMBER) opacity =
        (float)value->data.number.value;
    else return;
    span->in_line->opacity = opacity < 0.0f ? 0.0f : opacity > 1.0f ? 1.0f : opacity;
}

static void resolve_line_count_property(LayoutContext* lycon, ViewBlock* block,
                                        CssPropertyCode property, const CssValue* value) {
    if (!block) return;
    ensure_span_block(lycon, block);
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        block->blk->line_clamp = 0;
        return;
    }
    if (value->type != CSS_VALUE_TYPE_NUMBER && value->type != CSS_VALUE_TYPE_LENGTH) return;
    float raw = value->type == CSS_VALUE_TYPE_NUMBER
        ? (float)value->data.number.value : (float)value->data.length.value;
    if (raw > 0.0f) block->blk->line_clamp = (int)raw; // INT_CAST_OK: line count.
}

struct CssBackgroundComponent {
    float value;
    bool is_percent;
    bool is_auto;
};

static CssBackgroundComponent resolve_background_size_component(
    LayoutContext* lycon, CssPropertyCode property, const CssValue* value,
    float initial_value, bool initial_percent, bool initial_auto) {
    CssBackgroundComponent result = {initial_value, initial_percent, initial_auto};
    if (!value) return result;
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        result.is_auto = true;
    } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
        result.value = resolve_length_value(lycon, property, value);
        result.is_percent = false;
        result.is_auto = false;
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        result.value = value->data.percentage.value;
        result.is_percent = true;
        result.is_auto = false;
    }
    return result;
}

static void resolve_background_size(LayoutContext* lycon, ViewSpan* span,
                                    const CssValue* value, CssPropertyCode property) {
    ensure_span_background(lycon, span);
    BackgroundProp* background = span->boundary()->background;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_COVER || keyword == CSS_VALUE_CONTAIN) {
            background->bg_size_type = keyword;
        } else if (keyword == CSS_VALUE_AUTO) {
            background->bg_size_type = CSS_VALUE_AUTO;
            background->bg_size_width_auto = true;
            background->bg_size_height_auto = true;
        }
    } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
               value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        background->bg_size_type = (CssEnum)0;
        CssBackgroundComponent width = resolve_background_size_component(
            lycon, property, value, background->bg_size_width,
            background->bg_size_width_is_percent, background->bg_size_width_auto);
        background->bg_size_width = width.value;
        background->bg_size_width_is_percent = width.is_percent;
        background->bg_size_width_auto = width.is_auto;
        background->bg_size_height_auto = true;
    } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
        background->bg_size_type = (CssEnum)0;
        CssBackgroundComponent width = resolve_background_size_component(
            lycon, property, value->data.list.values[0], background->bg_size_width,
            background->bg_size_width_is_percent, background->bg_size_width_auto);
        CssBackgroundComponent height = resolve_background_size_component(
            lycon, property, value->data.list.values[1], background->bg_size_height,
            background->bg_size_height_is_percent, background->bg_size_height_auto);
        background->bg_size_width = width.value;
        background->bg_size_width_is_percent = width.is_percent;
        background->bg_size_width_auto = width.is_auto;
        background->bg_size_height = height.value;
        background->bg_size_height_is_percent = height.is_percent;
        background->bg_size_height_auto = height.is_auto;
    }
}

static float background_position_keyword(CssEnum keyword, bool horizontal) {
    if (keyword == CSS_VALUE_CENTER) return 50.0f;
    if (horizontal) return keyword == CSS_VALUE_RIGHT ? 100.0f : 0.0f;
    return keyword == CSS_VALUE_BOTTOM ? 100.0f : 0.0f;
}

static CssBackgroundComponent resolve_background_position_component(
    LayoutContext* lycon, CssPropertyCode property, const CssValue* value,
    float initial_value, bool initial_percent, bool horizontal) {
    CssBackgroundComponent result = {initial_value, initial_percent, false};
    if (!value) return result;
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        result.value = resolve_length_value(lycon, property, value);
        result.is_percent = false;
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        result.value = value->data.percentage.value;
        result.is_percent = true;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        result.value = background_position_keyword(value->data.keyword, horizontal);
        result.is_percent = true;
    }
    return result;
}

static void resolve_background_position(LayoutContext* lycon, ViewSpan* span,
                                        const CssValue* value, CssPropertyCode property) {
    ensure_span_background(lycon, span);
    BackgroundProp* background = span->boundary()->background;
    background->bg_position_set = true;
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
        CssBackgroundComponent x = resolve_background_position_component(
            lycon, property, value->data.list.values[0], background->bg_position_x,
            background->bg_position_x_is_percent, true);
        CssBackgroundComponent y = resolve_background_position_component(
            lycon, property, value->data.list.values[1], background->bg_position_y,
            background->bg_position_y_is_percent, false);
        background->bg_position_x = x.value;
        background->bg_position_x_is_percent = x.is_percent;
        background->bg_position_y = y.value;
        background->bg_position_y_is_percent = y.is_percent;
        return;
    }

    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        CssBackgroundComponent x = resolve_background_position_component(
            lycon, property, value, background->bg_position_x,
            background->bg_position_x_is_percent, true);
        background->bg_position_x = x.value;
        background->bg_position_x_is_percent = x.is_percent;
        background->bg_position_y = 50.0f;
        background->bg_position_y_is_percent = true;
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM) {
            background->bg_position_x = 50.0f;
            background->bg_position_x_is_percent = true;
            CssBackgroundComponent y = resolve_background_position_component(
                lycon, property, value, background->bg_position_y,
                background->bg_position_y_is_percent, false);
            background->bg_position_y = y.value;
            background->bg_position_y_is_percent = y.is_percent;
        } else {
            CssBackgroundComponent x = resolve_background_position_component(
                lycon, property, value, background->bg_position_x,
                background->bg_position_x_is_percent, true);
            background->bg_position_x = x.value;
            background->bg_position_x_is_percent = x.is_percent;
            background->bg_position_y = 50.0f;
            background->bg_position_y_is_percent = true;
        }
    }
}

void resolve_css_property(CssPropertyCode prop_id, const CssDeclaration* decl, LayoutContext* lycon) {
#ifdef RADIANT_TRACE_CSS_PROPERTIES
    // Property-level tracing is opt-in; cascade runs for every matched
    // declaration and otherwise overwhelms large online registry pages.
#endif
    if (!decl || !lycon || !lycon->view) {
        return;
    }
    const CssValue* value = decl->value;
    if (!value) { log_debug("No value in declaration");  return; }
#ifdef RADIANT_TRACE_CSS_PROPERTIES
#endif
    int64_t specificity = get_cascade_priority(decl);
#ifdef RADIANT_TRACE_CSS_PROPERTIES
#endif

    // Handle CSS custom properties (--variable-name: value)
    if (decl->property_name && decl->property_name[0] == '-' && decl->property_name[1] == '-') {
        // This is a CSS custom property, store it
        DomElement* element = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);

        CssCustomProp* new_var = element->css_variables;
        while (new_var && strcmp(new_var->name, decl->property_name) != 0) {
            new_var = new_var->next;
        }
        if (!new_var) {
            // CSS variables are retained DOM state, not a view-generation prop;
            // document-pool ownership keeps retained reset from dangling this list.
            new_var = (CssCustomProp*)pool_calloc(
                lycon->doc->document_pool, sizeof(CssCustomProp));
            if (new_var) {
                new_var->name = decl->property_name;
                new_var->next = element->css_variables;
                element->css_variables = new_var;
            }
        }
        if (new_var) {
            new_var->value = value;
        }
        return;  // Custom properties don't have standard processing
    }

    // Map logical dimensions after reading the specified writing mode, because
    // `inline-size` is physical height in vertical writing modes.
    DomElement* current_element = lycon->elmt && lycon->elmt->is_element()
        ? lycon->elmt->as_element() : nullptr;
    bool inline_axis_is_vertical = layout_element_inline_axis_is_vertical(current_element);
    WritingMode current_writing_mode = layout_element_writing_mode(current_element);
    bool vertical_block_start_is_right = current_writing_mode == WM_VERTICAL_RL;
    switch (prop_id) {
        case CSS_PROPERTY_INLINE_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_HEIGHT : CSS_PROPERTY_WIDTH;
            break;
        case CSS_PROPERTY_BLOCK_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_WIDTH : CSS_PROPERTY_HEIGHT;
            break;
        case CSS_PROPERTY_MIN_INLINE_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_MIN_HEIGHT : CSS_PROPERTY_MIN_WIDTH;
            break;
        case CSS_PROPERTY_MAX_INLINE_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_MAX_HEIGHT : CSS_PROPERTY_MAX_WIDTH;
            break;
        case CSS_PROPERTY_MIN_BLOCK_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_MIN_WIDTH : CSS_PROPERTY_MIN_HEIGHT;
            break;
        case CSS_PROPERTY_MAX_BLOCK_SIZE:
            prop_id = inline_axis_is_vertical ? CSS_PROPERTY_MAX_WIDTH : CSS_PROPERTY_MAX_HEIGHT;
            break;
        default: break;
    }

    // centralized entry tracing keeps ordinary debug builds informative while cases log only extra context.

    // Dispatch based on property ID
    // Parallel implementation to resolve_element_style() in resolve_style.cpp
    ViewSpan* span = lam::view_require_element(lycon->view);
    ViewBlock* block = lam::view_as_block(span);

    switch (prop_id) {
        // ===== GROUP 1: Core Typography & Color =====
        case CSS_PROPERTY_COLOR:
        case CSS_PROPERTY_ACCENT_COLOR:
        case CSS_PROPERTY_FILL:
        case CSS_PROPERTY_STROKE:
            resolve_inline_color_property(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_STROKE_WIDTH: {
            span->ensure_inline(lycon);

            float width = resolve_length_value(lycon, CSS_PROPERTY_STROKE_WIDTH, value);
            if (!isnan(width)) {
                span->in_line->svg_stroke_width = max(width, 0.0f);
                span->in_line->has_svg_stroke_width = true;
            }
            break;
        }

        // ===== Font Shorthand (must be before individual font properties) =====
        case CSS_PROPERTY_FONT: {
            span->ensure_font(lycon);

            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                // UA-styled controls already own font state, so inherit must replace every shorthand component.
                inherit_font_shorthand(lycon, span);
                break;
            }

            // CSS 2.1 §15.8: font shorthand resets omitted properties to initial values.
            // Pre-reset font-variant before scanning; if small-caps is found, the loop sets it.
            span->font->font_variant = CSS_VALUE_NORMAL;

            // CSS 2.1 §15.8: Handle system font keywords (caption, icon, menu,
            // message-box, small-caption, status-bar) as sole value.
            // These set ALL font sub-properties to the system font values.
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_SYSTEM_FONT) {
                    // CSS 2.1 §15.8: System font keywords set all font sub-properties.
                    // Map to platform system font. On macOS/Linux, system UI fonts
                    // typically resolve to a sans-serif family.
                    radiant_retain_font_family(span->font, lam::GcPtr<char>((char*)"Arial"));
                    span->font->font_size = 13.333f;  // typical system font size (browser default)
                    span->font->font_size_from_medium = false;
                    span->font->font_weight = CSS_VALUE_NORMAL;
                    span->font->font_weight_numeric = 400;
                    span->font->font_style = CSS_VALUE_NORMAL;
                    span->font->font_variant = CSS_VALUE_NORMAL;
                    ensure_span_block(lycon, span);
                    // line-height: normal for system fonts
                    span->blk->line_height = nullptr;
                    break;
                }
                break;
            }

            // Font shorthand format: [font-style] [font-variant] [font-weight] [font-stretch] font-size[/line-height] font-family
            // The last value (or values) is always font-family
            // font-size is required and comes before font-family

            // Handle list of values (common case for shorthand)
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
                // The CSS parser may produce comma-separated groups for font shorthand
                // (e.g. "bold 10px Arial, Helvetica, sans-serif" → 3 groups).
                // Detect this: if the first child is itself a list, we have nested
                // comma groups.  Flatten the first group (which has style/weight/size/
                // first-family) and collect remaining groups as extra font-family names.
                const CssValue* effective_value = value;
                if (value->data.list.count >= 2 &&
                    value->data.list.values[0] &&
                    value->data.list.values[0]->type == CSS_VALUE_TYPE_LIST) {
                    // First group is the main shorthand; use it as the value to parse
                    effective_value = value->data.list.values[0];
                }
                size_t count = effective_value->data.list.count;

                // CSS 2.1 §15.8: If 'inherit' appears mixed with other values
                // in the font shorthand, the entire declaration is invalid.
                // System font keywords (caption, icon, etc.) are NOT rejected here
                // because when mixed with other values, they act as font-family names.
                {
                    bool has_inherit = false;
                    for (size_t i = 0; i < count; i++) {
                        const CssValue* v = effective_value->data.list.values[i];
                        if (v && v->type == CSS_VALUE_TYPE_KEYWORD) {
                            const CssEnumInfo* ki = css_enum_info(v->data.keyword);
                            if (ki && ki->group == CSS_VALUE_GROUP_GLOBAL) {
                                has_inherit = true;
                                break;
                            }
                        }
                    }
                    if (has_inherit) break;  // break from CSS_PROPERTY_FONT case
                }

                // Last value(s) are font-family - find the font-size value first
                // Scan backwards: last is family, find size
                const CssValue* size_value = nullptr;
                const CssValue* line_height_value = nullptr;
                const CssValue* weight_value = nullptr;
                const CssValue* style_value = nullptr;
                size_t family_start_index = count; // Index where font-family starts

                for (size_t i = 0; i < count; i++) {
                    const CssValue* v = effective_value->data.list.values[i];
                    if (!v) continue;


                    if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        if (!size_value) {
                            // First length is font-size
                            size_value = v;

                            // Check for /line-height syntax: next values might be "/" and line-height
                            size_t next_idx = i + 1;

                            // Skip "/" delimiter if present
                            if (next_idx < count) {
                                const CssValue* next = effective_value->data.list.values[next_idx];
                                // Check if next is "/" (could be CUSTOM type with name "/")
                                if (next && next->type == CSS_VALUE_TYPE_CUSTOM &&
                                    next->data.custom_property.name &&
                                    strcmp(next->data.custom_property.name, "/") == 0) {
                                    next_idx++;

                                    // Next should be line-height
                                    // CSS 2.1 §15.7: line-height accepts: normal | <number> | <length> | <percentage> | inherit
                                    if (next_idx < count) {
                                        const CssValue* lh = effective_value->data.list.values[next_idx];
                                        if (lh && (lh->type == CSS_VALUE_TYPE_LENGTH ||
                                                   lh->type == CSS_VALUE_TYPE_PERCENTAGE ||
                                                   lh->type == CSS_VALUE_TYPE_NUMBER ||
                                                   (lh->type == CSS_VALUE_TYPE_KEYWORD &&
                                                    (lh->data.keyword == CSS_VALUE_NORMAL ||
                                                     lh->data.keyword == CSS_VALUE_INHERIT)))) {
                                            line_height_value = lh;
                                            next_idx++;
                                        }
                                    }
                                }
                            }

                            // Everything from next_idx onwards is font-family
                            family_start_index = next_idx;
                            break;  // Found size, done scanning for size
                        }
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* info = css_enum_info(v->data.keyword);
                        if (info) {
                            if (info->group == CSS_VALUE_GROUP_FONT_WEIGHT) {
                                weight_value = v;
                            } else if (info->group == CSS_VALUE_GROUP_FONT_STYLE) {
                                style_value = v;
                            } else if (v->data.keyword == CSS_VALUE_SMALL_CAPS) {
                                // CSS 2.1 §15.8: font shorthand includes font-variant
                                span->font->font_variant = CSS_VALUE_SMALL_CAPS;
                            } else if (info->group == CSS_VALUE_GROUP_FONT_SIZE) {
                                // CSS 2.1 §15.7: named font-size keywords (xx-small..xx-large)
                                // These can appear in font shorthand as the font-size component
                                size_value = v;
                                // Check if next value is "/line-height"
                                if (i + 2 < count) {
                                    CssValue* maybe_slash = effective_value->data.list.values[i + 1];
                                    if (maybe_slash && maybe_slash->type == CSS_VALUE_TYPE_CUSTOM &&
                                        maybe_slash->data.custom_property.name &&
                                        strcmp(maybe_slash->data.custom_property.name, "/") == 0 &&
                                        i + 2 < count) {
                                        line_height_value = effective_value->data.list.values[i + 2];
                                        int next_idx = i + 3;
                                        family_start_index = next_idx;
                                        break;
                                    }
                                }
                                // Everything after size is font-family
                                family_start_index = i + 1;
                                break;
                            }
                        }
                    } else if (v->type == CSS_VALUE_TYPE_NUMBER) {
                        // CSS 2.1 §15.6: numeric font-weight values (100, 200, ..., 900)
                        int val = (int)v->data.number.value;
                        if (val >= 1 && val <= 1000) {
                            weight_value = v;
                        }
                    }
                }

                const char* font_family_name = css_select_font_shorthand_family(
                    lycon, value, effective_value, family_start_index, true);

                // Apply font-size
                if (size_value) {
                    float font_size = resolve_length_value(lycon, CSS_PROPERTY_FONT_SIZE, size_value);
                    if (font_size > 0) {
                        span->font->font_size = font_size;
                        // Chromium CheckForGenericFamilyChange: track whether font-size
                        // derives from the 'medium' initial value through the inheritance chain.
                        // Relative units (em, %) propagate from parent; absolute units break the chain.
                        bool parent_from_medium = lycon->font.style && lycon->font.style->font_size_from_medium;
                        if (size_value->type == CSS_VALUE_TYPE_KEYWORD) {
                            span->font->font_size_from_medium = true;
                        } else if (size_value->type == CSS_VALUE_TYPE_PERCENTAGE ||
                                   (size_value->type == CSS_VALUE_TYPE_LENGTH &&
                                    size_value->data.length.unit == CSS_UNIT_EM)) {
                            span->font->font_size_from_medium = parent_from_medium;
                        } else {
                            span->font->font_size_from_medium = false;
                        }
                    }
                }

                // a valid font shorthand resets an omitted line-height to normal;
                // retaining the inherited value changes intrinsic form-control rows.
                if (size_value && font_family_name) {
                    ensure_span_block(lycon, span);
                    span->blk->line_height = line_height_value
                        ? line_height_value
                        : css_value_create_keyword(lycon->doc->view_tree->prop_pool, "normal");
                }

                // Apply font-family
                if (font_family_name) {
                    radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)font_family_name));
                }

                // CSS 2.1 §15.8: Properties omitted from the font shorthand
                // are reset to their initial values.
                // Reset font-weight to initial (normal/400) then apply if specified
                if (weight_value) {
                    span->font->font_weight = map_font_weight(weight_value);
                    span->font->font_weight_numeric = map_font_weight_numeric(weight_value);
                } else {
                    // reset to initial: font-weight: normal (400)
                    span->font->font_weight = CSS_VALUE_NORMAL;
                    span->font->font_weight_numeric = 400;
                }

                // Reset font-style to initial (normal) then apply if specified
                if (style_value) {
                    span->font->font_style = style_value->data.keyword;
                } else {
                    // reset to initial: font-style: normal
                    span->font->font_style = CSS_VALUE_NORMAL;
                }

                // Reset font-variant if not set via shorthand — already done with
                // pre-reset to CSS_VALUE_NORMAL above (before scanning loop).
                // If small-caps was found in the shorthand, the loop already set
                // font_variant = CSS_VALUE_SMALL_CAPS.
            }
            break;
        }

        case CSS_PROPERTY_FONT_SIZE: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "font-size")) break;
            span->ensure_font(lycon);

            float font_size = 0.0f;  bool valid = false;
            // For font-size, em/percentage are relative to PARENT font size, not element's current
            // lycon->font.style->font_size holds the inherited/parent font size
            float parent_font_size = lycon->font.style && lycon->font.style->font_size > 0
                ? lycon->font.style->font_size : 16.0f;
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Special handling for em units: em is relative to parent font size for font-size property
                if (value->data.length.unit == CSS_UNIT_EM) {
                    font_size = value->data.length.value * parent_font_size;
                } else {
                    font_size = resolve_length_value(lycon, prop_id, value);
                }
                // Per CSS spec, negative font-size values are invalid, but 0 is valid
                if (font_size >= 0) valid = true;
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Percentage of parent font size
                font_size = parent_font_size * (value->data.percentage.value / 100.0f);
                if (font_size >= 0) valid = true;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Named font sizes: small, medium, large, etc.
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_INHERIT) {
                    // CSS 2.1 §6.2.1: inherit from parent's computed font-size
                    font_size = parent_font_size;
                    valid = true;
                } else if (kw == CSS_VALUE_LARGER || kw == CSS_VALUE_SMALLER) {
                    // CSS 2.1 §15.7: relative to parent font size
                    float scale = (kw == CSS_VALUE_LARGER) ? 1.2f : (1.0f / 1.2f);
                    font_size = parent_font_size * scale;
                    valid = true;
                } else {
                    font_size = map_lambda_font_size_keyword(kw);
                    if (font_size > 0) {
                        valid = true;
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Per CSS spec, unitless zero is valid and treated as 0px
                // Other unitless numbers are invalid for font-size
                font_size = value->data.number.value;
                if (font_size == 0.0f) valid = true;
            } else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                font_size = resolve_length_value(lycon, prop_id, value);
                if (!isnan(font_size) && font_size >= 0.0f) valid = true;
            }

            if (valid) {
                span->font->font_size = font_size;
                // Chromium CheckForGenericFamilyChange: track whether font-size
                // derives from the 'medium' initial value through the inheritance chain.
                // Keywords (medium, large, etc.) are all based on the medium baseline.
                // Relative units (em, %) propagate from parent; absolute units break the chain.
                bool parent_from_medium = lycon->font.style && lycon->font.style->font_size_from_medium;
                if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                    span->font->font_size_from_medium = true;
                } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE ||
                           (value->type == CSS_VALUE_TYPE_LENGTH &&
                            value->data.length.unit == CSS_UNIT_EM)) {
                    span->font->font_size_from_medium = parent_from_medium;
                } else {
                    span->font->font_size_from_medium = false;
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_WEIGHT: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "font-weight")) break;
            span->ensure_font(lycon);
            // map CSS font weight to enum and preserve numeric value
            span->font->font_weight = map_font_weight(value);
            span->font->font_weight_numeric = map_font_weight_numeric(value);
            break;
        }

        case CSS_PROPERTY_FONT_FAMILY: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "font-family")) break;
            span->ensure_font(lycon);


            if (value->type == CSS_VALUE_TYPE_STRING) {
                // Font family name as string (quotes already stripped during parsing)
                radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)value->data.string));
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Custom identifier font family (e.g., "ahem" without quotes)
                radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)value->data.custom_property.name));
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_INHERIT) {
                    const FontProp* parent_font = lycon->font.style;
                    // CSS-wide keywords resolve to computed values; "inherit" is never a family name.
                    if (parent_font && parent_font->family) {
                        radiant_retain_font_family(span->font, lam::PoolPtr<char>(parent_font->family));
                    } else {
                        radiant_clear_font_family(span->font);
                    }
                } else {
                    // Keyword font family - check if generic or specific
                    const CssEnumInfo* info = css_enum_info(value->data.keyword);
                    if (info) radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)info->name));
                    else radiant_clear_font_family(span->font);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                const char* family = css_select_font_family(lycon, value, true);
                if (family) {
                    radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)family));
                }
            }
            break;
        }

        case CSS_PROPERTY_LINE_HEIGHT: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "line-height")) break;
            ensure_span_block(lycon, span);
            span->blk->line_height = value;  // Store CssValue*, resolve during layout
            break;
        }

        // ===== GROUP 5: Text Properties =====
        case CSS_PROPERTY_TEXT_ALIGN: {
            if (!block) break;
            ensure_span_block(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align_value = value->data.keyword;

                // Handle explicit 'inherit' keyword
                if (align_value == CSS_VALUE_INHERIT) {
                    // Find parent's computed text-align value
                    // Check computed blk->text_align first (handles HTML align attribute and
                    // CSS values set through any path), then fall back to specified_style
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    DomElement* parent = dom_parent_element(dom_elem);
                    bool resolved = false;

                    while (parent) {
                        // Prefer computed value (covers HTML align attr, CSS, and inherited values)
                        if (parent->blk && parent->block_mut()->text_align != CSS_VALUE__UNDEF &&
                            parent->block()->text_align != CSS_VALUE_INHERIT) {
                            block->blk->text_align = parent->blk->text_align;
                            resolved = true;
                            break;
                        }
                        // Fall back to specified style
                        if (parent->specified_style) {
                            CssDeclaration* parent_decl = style_tree_get_declaration(
                                parent->specified_style, CSS_PROPERTY_TEXT_ALIGN);
                            if (parent_decl && parent_decl->value &&
                                parent_decl->value->type == CSS_VALUE_TYPE_KEYWORD) {
                                CssEnum parent_align = parent_decl->value->data.keyword;
                                if (parent_align != CSS_VALUE_INHERIT && parent_align != CSS_VALUE__UNDEF) {
                                    block->blk->text_align = parent_align;
                                    resolved = true;
                                    break;
                                }
                            }
                        }
                        parent = dom_parent_element(parent);
                    }

                    if (!resolved) {
                        block->blk->text_align = CSS_VALUE_START;
                    }
                }
                else if (align_value == CSS_VALUE_MATCH_PARENT) {
                    // match-parent: inherit parent's text-align value as-is.
                    // CSS Text 3 §7.1: Logical keywords (start/end) are inherited
                    // without resolution — the layout code resolves them against
                    // the element's own direction at layout time (line_align()).
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    DomElement* parent = dom_parent_element(dom_elem);
                    CssEnum inherited_align = CSS_VALUE_START;

                    // Find parent's computed text-align
                    for (DomElement* p = parent; p; p = dom_parent_element(p)) {
                        if (p->blk && p->block_mut()->text_align != CSS_VALUE__UNDEF &&
                            p->block()->text_align != CSS_VALUE_INHERIT &&
                            p->block()->text_align != CSS_VALUE_MATCH_PARENT) {
                            inherited_align = p->block()->text_align;
                            break;
                        }
                    }

                    block->blk->text_align = inherited_align;
                }
                else if (align_value != CSS_VALUE__UNDEF) {
                    block->blk->text_align = align_value;
                }
            }
            break;
        }

        case CSS_PROPERTY_DIRECTION: {
            if (!block) {
                // direction also applies to inline elements (span) for bidi
                ViewSpan* span = lycon->view->is_element() ? lam::view_require_element(lycon->view) : nullptr;
                if (span && span->blk) {
                    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum dir_value = value->data.keyword;
                        if (dir_value == CSS_VALUE_LTR || dir_value == CSS_VALUE_RTL) {
                            span->blk->direction = dir_value;
                        }
                    }
                }
                break;
            }
            ensure_span_block(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dir_value = value->data.keyword;

                // Handle 'inherit' keyword
                if (dir_value == CSS_VALUE_INHERIT) {
                    // Walk up parents to find computed direction
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    DomElement* parent = dom_parent_element(dom_elem);
                    bool resolved = false;
                    while (parent) {
                        if (parent->blk && parent->block_mut()->direction != CSS_VALUE__UNDEF &&
                            parent->block()->direction != CSS_VALUE_INHERIT) {
                            block->blk->direction = parent->blk->direction;
                            resolved = true;
                            break;
                        }
                        parent = dom_parent_element(parent);
                    }
                    if (!resolved) {
                        block->blk->direction = CSS_VALUE_LTR;
                    }
                }
                else if (dir_value == CSS_VALUE_LTR || dir_value == CSS_VALUE_RTL) {
                    block->blk->direction = dir_value;
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_ALIGN_LAST:
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;

        case CSS_PROPERTY_TEXT_INDENT: {
            if (!block) break;
            ensure_span_block(lycon, block);
            // text-indent can be a length or percentage
            // CSS 2.1: text-indent applies to the first line of a block container
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, value);
                // Clamp to browser-compatible range to prevent integer overflow
                // in layout (browsers typically clamp at ±33554432 = 2^25)
                if (indent > MAX_LAYOUT_DIMENSION) indent = MAX_LAYOUT_DIMENSION;
                else if (indent < -MAX_LAYOUT_DIMENSION) indent = -MAX_LAYOUT_DIMENSION;
                block->blk->text_indent = indent;
                block->blk->text_indent_percent = NAN;  // not percentage
            }
            else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // Percentage is relative to containing block's width
                // Store percentage for deferred resolution during layout
                float percent = value->data.percentage.value;
                block->blk->text_indent = 0;  // will be computed during layout
                block->blk->text_indent_percent = percent;  // store for layout resolution
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                // Handle inherit - get parent's text-indent
                DomElement* dom_elem = lam::dom_require_element(lycon->view);
                DomElement* parent = dom_parent_element(dom_elem);
                if (parent && parent->blk) {
                    block->blk->text_indent = parent->blk->text_indent;
                    block->blk->text_indent_percent = parent->blk->text_indent_percent;
                    block->blk->text_indent_calc = parent->blk->text_indent_calc;
                }
            }
            else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // calc() expression - may contain percentages that need deferred resolution
                // Store the CssValue for resolution at layout time when content_width is known
                block->blk->text_indent = 0;
                block->blk->text_indent_percent = NAN;
                block->blk->text_indent_calc = value;
            }
            break;
        }

        case CSS_PROPERTY_TEXT_DECORATION:
        case CSS_PROPERTY_TEXT_DECORATION_LINE:
        case CSS_PROPERTY_TEXT_DECORATION_STYLE:
        case CSS_PROPERTY_TEXT_DECORATION_COLOR:
        case CSS_PROPERTY_TEXT_DECORATION_THICKNESS:
            resolve_text_decoration_property(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_VERTICAL_ALIGN: {
            span->ensure_inline(lycon);


            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum valign_value = value->data.keyword;
                if (valign_value == CSS_VALUE_INHERIT) {
                    // CSS 2.1 §6.2.1: vertical-align is not inherited, but 'inherit'
                    // forces use of parent's computed value
                    DomElement* parent = lycon->elmt->parent ? lycon->elmt->parent->as_element() : nullptr;
                    ViewBlock* parent_view = lam::view_as_block(parent);
                    if (parent_view && parent_view->in_line) {
                        span->in_line->vertical_align = parent_view->in_line->vertical_align;
                        span->in_line->vertical_align_offset = parent_view->in_line->vertical_align_offset;
                    } else {
                        // no parent inline prop — use initial value (baseline)
                        span->in_line->vertical_align = CSS_VALUE_BASELINE;
                        span->in_line->vertical_align_offset = 0;
                    }
                } else if (valign_value != CSS_VALUE__UNDEF) {
                    span->in_line->vertical_align = valign_value;
                    span->in_line->vertical_align_offset = 0;
                }
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                // Length values for vertical-align (e.g., 10px, -5px)
                // Baseline alignment + shift by the specified amount (positive = raise)
                float offset = resolve_length_value(lycon, CSS_PROPERTY_VERTICAL_ALIGN, value);
                span->in_line->vertical_align = CSS_VALUE_BASELINE;
                span->in_line->vertical_align_offset = offset;
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // CSS 2.1 §10.8.1: Percentage values refer to the element's OWN computed
                // line-height, not the parent's. Look up the element's specified line-height
                // first; fall back to the block context's line-height (which is the
                // element's inherited value if no explicit declaration exists).
                float line_height = 0;
                // Check if the element already has a resolved line-height on span->blk
                if (span->blk && span->block_mut()->line_height) {
                    line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, span->block()->line_height);
                }
                // If not yet resolved, look up from the element's specified style
                if (line_height <= 0) {
                    DomElement* elem = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
                    if (elem && elem->specified_style) {
                        CssDeclaration* lh_decl = style_tree_get_declaration(
                            elem->specified_style, CSS_PROPERTY_LINE_HEIGHT);
                        if (lh_decl && lh_decl->value) {
                            line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, lh_decl->value);
                        }
                    }
                }
                // Fall back to the block context line-height (inherited value)
                if (line_height <= 0) {
                    line_height = lycon->block.line_height > 0 ? lycon->block.line_height : lycon->font.current_font_size;
                }
                float offset = value->data.percentage.value * line_height / 100.0f;
                span->in_line->vertical_align = CSS_VALUE_BASELINE;
                span->in_line->vertical_align_offset = offset;
                }
            break;
        }

        case CSS_PROPERTY_RUBY_POSITION: {
            span->ensure_inline(lycon);
            if (value->type != CSS_VALUE_TYPE_KEYWORD) break;

            CssEnum ruby_position = value->data.keyword;
            if (ruby_position == CSS_VALUE_INHERIT || ruby_position == CSS_VALUE_UNSET) {
                DomElement* parent = dom_parent_element(
                    lam::dom_require<DOM_NODE_ELEMENT>(lycon->view));
                span->in_line->ruby_position = parent && parent->in_line
                    ? parent->inl()->ruby_position : CSS_VALUE_ALTERNATE;
            } else if (ruby_position == CSS_VALUE_INITIAL) {
                span->in_line->ruby_position = CSS_VALUE_ALTERNATE;
            } else if (ruby_position == CSS_VALUE_ALTERNATE ||
                       ruby_position == CSS_VALUE_OVER ||
                       ruby_position == CSS_VALUE_UNDER ||
                       ruby_position == CSS_VALUE_INTER_CHARACTER) {
                span->in_line->ruby_position = ruby_position;
            } else {
                break;
            }
            break;
        }

        case CSS_PROPERTY_CURSOR:
        case CSS_PROPERTY_CARET_SHAPE:
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;

        // ===== GROUP 2: Box Model Basics =====

        case CSS_PROPERTY_ZOOM: {
            if (!block) break;
            ensure_span_block(lycon, block);
            float zoom = 1.0f;
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                zoom = (float)value->data.number.value;
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                zoom = (float)value->data.percentage.value / 100.0f;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                       value->data.keyword == CSS_VALUE_INHERIT) {
                DomElement* parent = dom_parent_element(lam::dom_require_element(lycon->view));
                ViewBlock* parent_block = parent ? lam::view_as_block(parent) : nullptr;
                if (parent_block && parent_block->blk) zoom = parent_block->block()->zoom;
            }
            // CSS Viewport 1 preserves the web-compatibility behavior that zero
            // computes as 1; negative values are rejected by the CSS parser.
            block->blk->zoom = zoom > 0.0f ? zoom : 1.0f;
            break;
        }

        case CSS_PROPERTY_WIDTH:
            resolve_css_axis_size(lycon, block, value, true);
            break;
        case CSS_PROPERTY_HEIGHT:
            resolve_css_axis_size(lycon, block, value, false);
            break;

        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT: {
            if (block) apply_dimension_constraint(lycon, block, prop_id, value);
            break;
        }

        case CSS_PROPERTY_MARGIN: {
            ensure_span_bound(lycon, span);
            resolve_spacing_prop(lycon, CSS_PROPERTY_MARGIN, value, specificity, &span->boundary_mut()->margin);
            break;
        }

        case CSS_PROPERTY_PADDING: {
            ensure_span_bound(lycon, span);
            resolve_spacing_prop(lycon, CSS_PROPERTY_PADDING, value, specificity, &span->boundary_mut()->padding);
            break;
        }

        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT: {
            CssBoxSide side = css_physical_side(prop_id);
            bool is_margin_side = prop_id == CSS_PROPERTY_MARGIN_TOP ||
                prop_id == CSS_PROPERTY_MARGIN_RIGHT ||
                prop_id == CSS_PROPERTY_MARGIN_BOTTOM ||
                prop_id == CSS_PROPERTY_MARGIN_LEFT;
            resolve_spacing_side(lycon, span, side, prop_id, value, specificity, is_margin_side);
            break;
        }

        case CSS_PROPERTY_MARGIN_BLOCK:
        case CSS_PROPERTY_MARGIN_INLINE:
        case CSS_PROPERTY_MARGIN_INLINE_START:
        case CSS_PROPERTY_MARGIN_INLINE_END:
        case CSS_PROPERTY_MARGIN_BLOCK_START:
        case CSS_PROPERTY_MARGIN_BLOCK_END:
            resolve_logical_spacing_property(lycon, span, prop_id, value, specificity,
                                             true, inline_axis_is_vertical,
                                             vertical_block_start_is_right);
            break;

        case CSS_PROPERTY_MARGIN_TRIM: {
            // CSS Box 4 §3.1: margin-trim is spec-correct but Chrome does not
            // support it yet. Disabled to match browser reference output.
            // Re-enable when browser support catches up and references are
            // regenerated.
            break;
        }

        case CSS_PROPERTY_TEXT_BOX:
        case CSS_PROPERTY_TEXT_BOX_TRIM: {
            if (!block) break;
            ensure_span_block(lycon, block);
            if (prop_id == CSS_PROPERTY_TEXT_BOX) {
                uint8_t trim = 0;
                CssEnum over_edge = CSS_VALUE_TEXT;
                CssEnum under_edge = CSS_VALUE_TEXT;
                bool has_trim = false;
                bool has_edge = false;

                CssValue* values[4];
                int value_count = 0;
                if (value->type == CSS_VALUE_TYPE_LIST) {
                    value_count = value->data.list.count > 4 ? 4 : value->data.list.count;
                    for (int i = 0; i < value_count; i++) values[i] = value->data.list.values[i];
                } else {
                    values[0] = (CssValue*)value;
                    value_count = 1;
                }

                for (int i = 0; i < value_count; i++) {
                    CssValue* item = values[i];
                    if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) continue;

                    CssEnum val = item->data.keyword;
                    if (val == CSS_VALUE_NONE) {
                        trim = 0;
                        has_trim = true;
                    } else if (val == CSS_VALUE_TRIM_START) {
                        trim = TEXT_BOX_TRIM_START;
                        has_trim = true;
                    } else if (val == CSS_VALUE_TRIM_END) {
                        trim = TEXT_BOX_TRIM_END;
                        has_trim = true;
                    } else if (val == CSS_VALUE_TRIM_BOTH || val == CSS_VALUE_BOTH) {
                        trim = TEXT_BOX_TRIM_START | TEXT_BOX_TRIM_END;
                        has_trim = true;
                    } else if (val == CSS_VALUE_AUTO || val == CSS_VALUE_TEXT) {
                        if (!has_edge) {
                            over_edge = CSS_VALUE_TEXT;
                            under_edge = CSS_VALUE_TEXT;
                            has_edge = true;
                        } else {
                            under_edge = CSS_VALUE_TEXT;
                        }
                    } else if (val == CSS_VALUE_CAP || val == CSS_VALUE_EX) {
                        if (!has_edge) {
                            over_edge = val;
                            under_edge = CSS_VALUE_TEXT;
                            has_edge = true;
                        } else {
                            under_edge = val;
                        }
                    } else if (val == CSS_VALUE_ALPHABETIC || val == CSS_VALUE_IDEOGRAPHIC) {
                        if (!has_edge) {
                            over_edge = CSS_VALUE_TEXT;
                            under_edge = val;
                            has_edge = true;
                        } else {
                            under_edge = val;
                        }
                    }
                }

                if (has_trim) {
                    block->blk->text_box_trim = trim;
                }
                if (has_edge) {
                    block->blk->text_box_over_edge = over_edge;
                    block->blk->text_box_under_edge = under_edge;
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_NONE) {
                    block->blk->text_box_trim = 0;
                } else if (val == CSS_VALUE_TRIM_START) {
                    block->blk->text_box_trim = TEXT_BOX_TRIM_START;
                } else if (val == CSS_VALUE_TRIM_END) {
                    block->blk->text_box_trim = TEXT_BOX_TRIM_END;
                } else if (val == CSS_VALUE_TRIM_BOTH || val == CSS_VALUE_BOTH) {
                    block->blk->text_box_trim = TEXT_BOX_TRIM_START | TEXT_BOX_TRIM_END;
                }
            }
            break;
        }

        case CSS_PROPERTY_TEXT_SPACING_TRIM: {
            if (!block) break;
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;
        }

        case CSS_PROPERTY_TEXT_BOX_EDGE: {
            // CSS Inline Level 3 §5.1: text-box-edge defines over/under edge metrics
            // Single value: auto | text → both edges use same metric
            // Two values: <over-edge> <under-edge> (e.g., "text alphabetic", "cap text", "ex text")
            if (!block) break;
            ensure_span_block(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_AUTO || val == CSS_VALUE_TEXT) {
                    // text-box-edge: auto → both edges = text (for horizontal-tb)
                    // text-box-edge: text → both edges = text
                    block->blk->text_box_over_edge = CSS_VALUE_TEXT;
                    block->blk->text_box_under_edge = CSS_VALUE_TEXT;
                } else if (val == CSS_VALUE_CAP) {
                    block->blk->text_box_over_edge = CSS_VALUE_CAP;
                    block->blk->text_box_under_edge = CSS_VALUE_TEXT;
                } else if (val == CSS_VALUE_EX) {
                    block->blk->text_box_over_edge = CSS_VALUE_EX;
                    block->blk->text_box_under_edge = CSS_VALUE_TEXT;
                } else if (val == CSS_VALUE_ALPHABETIC) {
                    block->blk->text_box_over_edge = CSS_VALUE_TEXT;
                    block->blk->text_box_under_edge = CSS_VALUE_ALPHABETIC;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
                // Two-value form: <over-edge> <under-edge>
                CssValue* v0 = value->data.list.values[0];
                CssValue* v1 = value->data.list.values[1];
                block->blk->text_box_over_edge = (v0->type == CSS_VALUE_TYPE_KEYWORD) ? v0->data.keyword : CSS_VALUE_TEXT;
                block->blk->text_box_under_edge = (v1->type == CSS_VALUE_TYPE_KEYWORD) ? v1->data.keyword : CSS_VALUE_TEXT;
            }
            break;
        }

        case CSS_PROPERTY_BASELINE_SOURCE:
        case CSS_PROPERTY_DOMINANT_BASELINE:
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;

        case CSS_PROPERTY_PADDING_INLINE:
        case CSS_PROPERTY_PADDING_INLINE_START:
        case CSS_PROPERTY_PADDING_INLINE_END:
        case CSS_PROPERTY_PADDING_BLOCK:
        case CSS_PROPERTY_PADDING_BLOCK_START:
        case CSS_PROPERTY_PADDING_BLOCK_END:
            resolve_logical_spacing_property(lycon, span, prop_id, value, specificity,
                                             false, inline_axis_is_vertical,
                                             vertical_block_start_is_right);
            break;

        case CSS_PROPERTY_BACKGROUND_COLOR: {
            // The property tree resolves background before background-color;
            // preserve the shorthand's higher cascade priority across that boundary.
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_BACKGROUND, decl, "background-color")) break;
            ensure_span_background(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                span->boundary_mut()->background->color = inherit_background_color(lycon);
                break;
            }
            span->boundary_mut()->background->color = resolve_color_value(lycon, value);
            break;
        }

        case CSS_PROPERTY_BACKGROUND_IMAGE: {
            ViewSpan* span = lam::view_require_element(lycon->view);
            ensure_span_background(lycon, span);

            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // url() is parsed as a function
                CssFunction* func = value->data.function;
                if (func && func->name && strcmp(func->name, "url") == 0) {
                    // Get the first argument of url() function
                    if (func->args && func->arg_count > 0) {
                        CssValue* arg = func->args[0];
                        const char* url = (arg->type == CSS_VALUE_TYPE_STRING) ? arg->data.string :
                                         (arg->type == CSS_VALUE_TYPE_URL) ? arg->data.url : nullptr;
                        if (url) {
                            char* image_path = resolve_css_resource_url(lycon, decl, url);
                            if (image_path) {
                                radiant_retain_background_image(span->boundary()->background, lam::PoolPtr<char>(image_path));
                            }
                        }
                    }
                } else if (func && func->name &&
                           (strcmp(func->name, "linear-gradient") == 0 ||
                            strcmp(func->name, "repeating-linear-gradient") == 0 ||
                            strcmp(func->name, "radial-gradient") == 0 ||
                            strcmp(func->name, "repeating-radial-gradient") == 0 ||
                            strcmp(func->name, "conic-gradient") == 0)) {
                    // Delegate gradient functions to the background shorthand handler
                    resolve_css_property(CSS_PROPERTY_BACKGROUND, decl, lycon);
                }
            } else if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_STRING) {
                // Direct URL/string value (non-function form)
                const char* url = (value->type == CSS_VALUE_TYPE_URL) ? value->data.url : value->data.string;
                if (url) {
                    char* image_path = resolve_css_resource_url(lycon, decl, url);
                    if (image_path) {
                        radiant_retain_background_image(span->boundary()->background, lam::PoolPtr<char>(image_path));
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                radiant_clear_background_image(span->boundary()->background);
            }
            break;
        }

        case CSS_PROPERTY_MASK_IMAGE: {
            resolve_css_mask_image(lycon, span, value);
            break;
        }

        // ===== GROUP 16: Background Advanced Properties =====
        case CSS_PROPERTY_BACKGROUND_ATTACHMENT:
        case CSS_PROPERTY_BACKGROUND_ORIGIN:
        case CSS_PROPERTY_BACKGROUND_CLIP:
        case CSS_PROPERTY_BACKGROUND_BLEND_MODE:
            resolve_background_keyword_property(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_BACKGROUND_POSITION_X:
        case CSS_PROPERTY_BACKGROUND_POSITION_Y: {
            ensure_span_background(lycon, span);
            resolve_background_position_axis(
                lycon, prop_id, value, span->boundary()->background,
                prop_id == CSS_PROPERTY_BACKGROUND_POSITION_X);
            break;
        }

        case CSS_PROPERTY_MIX_BLEND_MODE: {
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;
        }

        case CSS_PROPERTY_BACKGROUND_SIZE: {
            resolve_background_size(lycon, span, value, prop_id);
            break;
        }

        case CSS_PROPERTY_BACKGROUND_REPEAT: {
            ensure_span_background(lycon, span);
            BackgroundProp* bg = span->boundary()->background;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_REPEAT || kw == CSS_VALUE_NO_REPEAT ||
                    kw == CSS_VALUE_ROUND || kw == CSS_VALUE_SPACE) {
                    bg->bg_repeat_x = kw;
                    bg->bg_repeat_y = kw;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Two-value form: <repeat-x> <repeat-y>
                if (value->data.list.count >= 2) {
                    if (value->data.list.values[0]->type == CSS_VALUE_TYPE_KEYWORD)
                        bg->bg_repeat_x = value->data.list.values[0]->data.keyword;
                    if (value->data.list.values[1]->type == CSS_VALUE_TYPE_KEYWORD)
                        bg->bg_repeat_y = value->data.list.values[1]->data.keyword;
                }
            }
            break;
        }

        case CSS_PROPERTY_BACKGROUND_POSITION: {
            resolve_background_position(lycon, span, value, prop_id);
            break;
        }

        case CSS_PROPERTY_BOX_SHADOW: {
            ensure_span_bound(lycon, span);

            // Handle 'none' keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->bound->box_shadow = nullptr;
                break;
            }

            // Box-shadow can be a list of shadows (comma-separated)
            // Each shadow: [inset] <offset-x> <offset-y> [blur-radius] [spread-radius] [color]
            BoxShadow* shadow_list_head = nullptr;
            BoxShadow* shadow_list_tail = nullptr;

            // Helper lambda to parse a single shadow from a value list
            auto parse_single_shadow = [&](const CssValue* shadow_value) -> BoxShadow* {
                BoxShadow* shadow = (BoxShadow*)alloc_prop(lycon, sizeof(BoxShadow));
                memset(shadow, 0, sizeof(BoxShadow));
                // Default color: black with full opacity
                shadow->color.r = 0;
                shadow->color.g = 0;
                shadow->color.b = 0;
                shadow->color.a = 255;

                if (shadow_value->type == CSS_VALUE_TYPE_LIST) {
                    const CssValue* list = shadow_value;
                    int length_count = 0;
                    for (int i = 0; i < list->data.list.count; i++) {
                        const CssValue* v = list->data.list.values[i];
                        if (!v) continue;

                        if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            if (v->data.keyword == CSS_VALUE_INSET) {
                                shadow->inset = true;
                            } else {
                                // Could be a color keyword
                                shadow->color = color_name_to_rgb(v->data.keyword);
                            }
                        } else if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_NUMBER) {
                            float val = (v->type == CSS_VALUE_TYPE_LENGTH)
                                ? resolve_length_value(lycon, prop_id, v)
                                : v->data.number.value;
                            switch (length_count) {
                                case 0: shadow->offset_x = val; break;
                                case 1: shadow->offset_y = val; break;
                                case 2: shadow->blur_radius = val; break;
                                case 3: shadow->spread_radius = val; break;
                            }
                            length_count++;
                        } else if (v->type == CSS_VALUE_TYPE_COLOR || v->type == CSS_VALUE_TYPE_FUNCTION) {
                            shadow->color = resolve_color_value(lycon, v);
                        }
                    }
                } else if (shadow_value->type == CSS_VALUE_TYPE_LENGTH || shadow_value->type == CSS_VALUE_TYPE_NUMBER) {
                    // Single length value - just offset-x (unlikely but valid syntax)
                    shadow->offset_x = (shadow_value->type == CSS_VALUE_TYPE_LENGTH)
                        ? resolve_length_value(lycon, prop_id, shadow_value)
                        : shadow_value->data.number.value;
                }
                return shadow;
            };

            // Check if this is a list of shadows (comma-separated)
            if (value->type == CSS_VALUE_TYPE_LIST) {
                // Could be a single shadow's components OR multiple shadows
                // Look for nested lists (multiple shadows) vs flat list (single shadow)
                const CssValue* list = value;
                bool is_multi_shadow = false;

                // Check if any child is itself a list (indicates multiple shadows)
                for (int i = 0; i < list->data.list.count && !is_multi_shadow; i++) {
                    if (list->data.list.values[i] &&
                        list->data.list.values[i]->type == CSS_VALUE_TYPE_LIST) {
                        is_multi_shadow = true;
                    }
                }

                if (is_multi_shadow) {
                    // Multiple shadows - each child is a shadow
                    for (int i = 0; i < list->data.list.count; i++) {
                        const CssValue* shadow_val = list->data.list.values[i];
                        if (!shadow_val) continue;
                        BoxShadow* shadow = parse_single_shadow(shadow_val);
                        if (shadow) {
                            if (!shadow_list_head) {
                                shadow_list_head = shadow;
                                shadow_list_tail = shadow;
                            } else {
                                shadow_list_tail->next = shadow;
                                shadow_list_tail = shadow;
                            }
                        }
                    }
                } else {
                    // Single shadow - parse the flat list directly
                    BoxShadow* shadow = parse_single_shadow(value);
                    if (shadow) {
                        shadow_list_head = shadow;
                    }
                }
            }

            span->bound->box_shadow = shadow_list_head;
            break;
        }

        // ============================================================================
        // CSS Transforms
        // ============================================================================
        case CSS_PROPERTY_TRANSFORM: {

            // Handle 'none' keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->transform = nullptr;
                break;
            }

            ensure_transform_prop(lycon, span);

            TransformFunction* func_list_head = nullptr;
            TransformFunction* func_list_tail = nullptr;

            // Helper lambda to parse a single transform function
            auto parse_transform_function = [&](const CssValue* func_value) -> TransformFunction* {
                if (func_value->type != CSS_VALUE_TYPE_FUNCTION) return nullptr;

                const CssFunction* func = func_value->data.function;
                if (!func || !func->name) return nullptr;

                TransformFunction* tf = (TransformFunction*)alloc_prop(lycon, sizeof(TransformFunction));
                memset(tf, 0, sizeof(TransformFunction));
                // Initialize percentage fields to NaN (not percentage)
                tf->translate_x_percent = NAN;
                tf->translate_y_percent = NAN;

                // Parse function name and arguments
                if (str_ieq_const(func->name, strlen(func->name), "translate")) {
                    tf->type = TRANSFORM_TRANSLATE;
                    if (func->arg_count >= 1 && func->args[0]) {
                        // Check if X is a percentage (needs deferred resolution)
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_x_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.x = 0;  // Will be resolved later
                        } else {
                            tf->params.translate.x = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        // Check if Y is a percentage
                        if (func->args[1]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_y_percent = func->args[1]->data.percentage.value;
                            tf->params.translate.y = 0;  // Will be resolved later
                        } else {
                            tf->params.translate.y = resolve_length_value(lycon, prop_id, func->args[1]);
                        }
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateX")) {
                    tf->type = TRANSFORM_TRANSLATEX;
                    if (func->arg_count >= 1 && func->args[0]) {
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_x_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.x = 0;
                        } else {
                            tf->params.translate.x = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateY")) {
                    tf->type = TRANSFORM_TRANSLATEY;
                    if (func->arg_count >= 1 && func->args[0]) {
                        if (func->args[0]->type == CSS_VALUE_TYPE_PERCENTAGE) {
                            tf->translate_y_percent = func->args[0]->data.percentage.value;
                            tf->params.translate.y = 0;
                        } else {
                            tf->params.translate.y = resolve_length_value(lycon, prop_id, func->args[0]);
                        }
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scale")) {
                    tf->type = TRANSFORM_SCALE;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.x = func->args[0]->data.number.value;
                        tf->params.scale.y = tf->params.scale.x; // default to uniform scale
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        tf->params.scale.y = func->args[1]->data.number.value;
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scaleX")) {
                    tf->type = TRANSFORM_SCALEX;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.x = func->args[0]->data.number.value;
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "scaleY")) {
                    tf->type = TRANSFORM_SCALEY;
                    tf->params.scale.x = 1.0f;
                    tf->params.scale.y = 1.0f;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.scale.y = func->args[0]->data.number.value;
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "rotate") ||
                         str_ieq_const(func->name, strlen(func->name), "skewX") ||
                         str_ieq_const(func->name, strlen(func->name), "skewY") ||
                         str_ieq_const(func->name, strlen(func->name), "rotateX") ||
                         str_ieq_const(func->name, strlen(func->name), "rotateY") ||
                         str_ieq_const(func->name, strlen(func->name), "rotateZ")) {
                    if (str_ieq_const(func->name, strlen(func->name), "rotate")) tf->type = TRANSFORM_ROTATE;
                    else if (str_ieq_const(func->name, strlen(func->name), "skewX")) tf->type = TRANSFORM_SKEWX;
                    else if (str_ieq_const(func->name, strlen(func->name), "skewY")) tf->type = TRANSFORM_SKEWY;
                    else if (str_ieq_const(func->name, strlen(func->name), "rotateX")) tf->type = TRANSFORM_ROTATEX;
                    else if (str_ieq_const(func->name, strlen(func->name), "rotateY")) tf->type = TRANSFORM_ROTATEY;
                    else tf->type = TRANSFORM_ROTATEZ;
                    if (func->arg_count >= 1 && func->args[0]) tf->params.angle = resolve_transform_angle(func->args[0]);
                }
                else if (str_ieq_const(func->name, strlen(func->name), "skew")) {
                    tf->type = TRANSFORM_SKEW;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.skew.x = resolve_transform_angle(func->args[0]);
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        tf->params.skew.y = resolve_transform_angle(func->args[1]);
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "matrix")) {
                    tf->type = TRANSFORM_MATRIX;
                    // matrix(a, b, c, d, e, f) = [a c e; b d f; 0 0 1]
                    // Default to identity
                    tf->params.matrix.a = 1; tf->params.matrix.b = 0;
                    tf->params.matrix.c = 0; tf->params.matrix.d = 1;
                    tf->params.matrix.e = 0; tf->params.matrix.f = 0;
                    if (func->arg_count >= 6 &&
                        func->args[0] && func->args[1] && func->args[2] &&
                        func->args[3] && func->args[4] && func->args[5]) {
                        tf->params.matrix.a = func->args[0]->data.number.value;
                        tf->params.matrix.b = func->args[1]->data.number.value;
                        tf->params.matrix.c = func->args[2]->data.number.value;
                        tf->params.matrix.d = func->args[3]->data.number.value;
                        tf->params.matrix.e = func->args[4]->data.number.value;
                        tf->params.matrix.f = func->args[5]->data.number.value;
                    }
                }
                // 3D transforms
                else if (str_ieq_const(func->name, strlen(func->name), "translate3d")) {
                    tf->type = TRANSFORM_TRANSLATE3D;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.translate3d.x = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                    if (func->arg_count >= 2 && func->args[1]) {
                        tf->params.translate3d.y = resolve_length_value(lycon, prop_id, func->args[1]);
                    }
                    if (func->arg_count >= 3 && func->args[2]) {
                        tf->params.translate3d.z = resolve_length_value(lycon, prop_id, func->args[2]);
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "translateZ")) {
                    tf->type = TRANSFORM_TRANSLATEZ;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.translate3d.z = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                }
                else if (str_ieq_const(func->name, strlen(func->name), "perspective")) {
                    tf->type = TRANSFORM_PERSPECTIVE;
                    if (func->arg_count >= 1 && func->args[0]) {
                        tf->params.perspective = resolve_length_value(lycon, prop_id, func->args[0]);
                    }
                }
                else {
                    return nullptr;
                }

                return tf;
            };

            // Parse transform functions from value
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // Single transform function
                TransformFunction* tf = parse_transform_function(value);
                if (tf) {
                    func_list_head = tf;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple transform functions
                const CssValue* list = value;
                for (int i = 0; i < list->data.list.count; i++) {
                    const CssValue* item = list->data.list.values[i];
                    if (!item) continue;

                    TransformFunction* tf = parse_transform_function(item);
                    if (tf) {
                        if (!func_list_head) {
                            func_list_head = tf;
                            func_list_tail = tf;
                        } else {
                            func_list_tail->next = tf;
                            func_list_tail = tf;
                        }
                    }
                }
            }

            span->transform->functions = func_list_head;
            break;
        }

        case CSS_PROPERTY_TRANSFORM_ORIGIN: {

            ensure_transform_prop(lycon, span);

            // Parse transform-origin: can be keywords (left, center, right, top, bottom)
            // or length/percentage values
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_LEFT) {
                    span->transform->origin_x = 0;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_CENTER) {
                    span->transform->origin_x = 50.0f;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_RIGHT) {
                    span->transform->origin_x = 100.0f;
                    span->transform->origin_x_percent = true;
                } else if (kw == CSS_VALUE_TOP) {
                    span->transform->origin_y = 0;
                    span->transform->origin_y_percent = true;
                } else if (kw == CSS_VALUE_BOTTOM) {
                    span->transform->origin_y = 100.0f;
                    span->transform->origin_y_percent = true;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = value;
                // First value is X, second is Y (optional third is Z)
                for (int i = 0; i < list->data.list.count && i < 3; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;

                    if (v->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        float pct = (float)v->data.percentage.value;
                        if (i == 0) {
                            span->transform->origin_x = pct;
                            span->transform->origin_x_percent = true;
                        } else if (i == 1) {
                            span->transform->origin_y = pct;
                            span->transform->origin_y_percent = true;
                        } else {
                            // Z cannot be percentage
                        }
                    } else if (v->type == CSS_VALUE_TYPE_LENGTH) {
                        float len = resolve_length_value(lycon, prop_id, v);
                        if (i == 0) {
                            span->transform->origin_x = len;
                            span->transform->origin_x_percent = false;
                        } else if (i == 1) {
                            span->transform->origin_y = len;
                            span->transform->origin_y_percent = false;
                        } else {
                            span->transform->origin_z = len;
                        }
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum kw = v->data.keyword;
                        // Keywords can be in any order for X/Y
                        if (kw == CSS_VALUE_LEFT || kw == CSS_VALUE_RIGHT) {
                            span->transform->origin_x = (kw == CSS_VALUE_LEFT) ? 0 : 100.0f;
                            span->transform->origin_x_percent = true;
                        } else if (kw == CSS_VALUE_TOP || kw == CSS_VALUE_BOTTOM) {
                            span->transform->origin_y = (kw == CSS_VALUE_TOP) ? 0 : 100.0f;
                            span->transform->origin_y_percent = true;
                        } else if (kw == CSS_VALUE_CENTER) {
                            if (i == 0) {
                                span->transform->origin_x = 50.0f;
                                span->transform->origin_x_percent = true;
                            } else {
                                span->transform->origin_y = 50.0f;
                                span->transform->origin_y_percent = true;
                            }
                        }
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                span->transform->origin_x = (float)value->data.percentage.value;
                span->transform->origin_x_percent = true;
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                span->transform->origin_x = resolve_length_value(lycon, prop_id, value);
                span->transform->origin_x_percent = false;
            }

            break;
        }

        case CSS_PROPERTY_PERSPECTIVE: {
            ensure_transform_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->transform->perspective = 0.0f;
            } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
                       value->type == CSS_VALUE_TYPE_NUMBER) {
                float perspective = value->type == CSS_VALUE_TYPE_NUMBER
                    ? value->data.number.value
                    : resolve_length_value(lycon, CSS_PROPERTY_PERSPECTIVE, value);
                span->transform->perspective = max(0.0f, perspective);
            }
            break;
        }

        case CSS_PROPERTY_PERSPECTIVE_ORIGIN: {
            ensure_transform_prop(lycon, span);
            if (value->type == CSS_VALUE_TYPE_LIST) {
                for (int i = 0; i < value->data.list.count && i < 2; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;
                    if (item->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        float pct = (float)item->data.percentage.value;
                        if (i == 0) span->transform->perspective_origin_x = pct;
                        else span->transform->perspective_origin_y = pct;
                    } else if (item->type == CSS_VALUE_TYPE_LENGTH ||
                               item->type == CSS_VALUE_TYPE_NUMBER) {
                        float len = item->type == CSS_VALUE_TYPE_NUMBER
                            ? item->data.number.value
                            : resolve_length_value(lycon, CSS_PROPERTY_PERSPECTIVE_ORIGIN, item);
                        if (i == 0) span->transform->perspective_origin_x = len;
                        else span->transform->perspective_origin_y = len;
                    } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum kw = item->data.keyword;
                        if (kw == CSS_VALUE_LEFT) span->transform->perspective_origin_x = 0.0f;
                        else if (kw == CSS_VALUE_RIGHT) span->transform->perspective_origin_x = 100.0f;
                        else if (kw == CSS_VALUE_TOP) span->transform->perspective_origin_y = 0.0f;
                        else if (kw == CSS_VALUE_BOTTOM) span->transform->perspective_origin_y = 100.0f;
                        else if (kw == CSS_VALUE_CENTER) {
                            if (i == 0) span->transform->perspective_origin_x = 50.0f;
                            else span->transform->perspective_origin_y = 50.0f;
                        }
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                span->transform->perspective_origin_x = (float)value->data.percentage.value;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_CENTER) {
                span->transform->perspective_origin_x = 50.0f;
            }
            break;
        }

        case CSS_PROPERTY_TRANSFORM_STYLE:
        case CSS_PROPERTY_BACKFACE_VISIBILITY:
            break;

        case CSS_PROPERTY_FILTER:
        case CSS_PROPERTY_BACKDROP_FILTER: {
            bool is_backdrop_filter = prop_id == CSS_PROPERTY_BACKDROP_FILTER;
            FilterProp** target_filter = is_backdrop_filter
                ? span->backdrop_filter_slot()
                : &span->filter;

            // Helper lambda to parse a single filter function
            auto parse_filter_func = [&](CssFunction* func) -> FilterFunction* {
                if (!func || !func->name || func->arg_count == 0) return nullptr;

                FilterFunction* filter = (FilterFunction*)alloc_prop(lycon, sizeof(FilterFunction));
                filter->next = nullptr;

                const char* name = func->name;
                CssValue* arg = func->args[0];  // First argument

                if (strcmp(name, "blur") == 0) {
                    filter->type = FILTER_BLUR;
                    if (arg && arg->type == CSS_VALUE_TYPE_LENGTH) {
                        filter->params.blur_radius = resolve_length_value(lycon, prop_id, arg);
                    } else {
                        filter->params.blur_radius = 0;
                    }
                }
                else if (const FilterAmountSpec* spec = find_filter_amount_spec(name)) {
                    filter->type = spec->type;
                    filter->params.amount = resolve_filter_amount(arg, spec->clamp_unit_interval);
                }
                else if (strcmp(name, "hue-rotate") == 0) {
                    filter->type = FILTER_HUE_ROTATE;
                    if (arg && (arg->type == CSS_VALUE_TYPE_ANGLE || arg->type == CSS_VALUE_TYPE_LENGTH)) {
                        // Angles are stored in length.value (degrees)
                        float degrees = (float)arg->data.length.value;
                        filter->params.angle = degrees * ((float)M_PI / 180.0f);
                    } else if (arg && arg->type == CSS_VALUE_TYPE_NUMBER) {
                        // Unitless number treated as degrees
                        filter->params.angle = (float)arg->data.number.value * ((float)M_PI / 180.0f);
                    } else {
                        filter->params.angle = 0;
                    }
                }
                else if (strcmp(name, "drop-shadow") == 0) {
                    filter->type = FILTER_DROP_SHADOW;
                    filter->params.drop_shadow.offset_x = 0;
                    filter->params.drop_shadow.offset_y = 0;
                    filter->params.drop_shadow.blur_radius = 0;
                    filter->params.drop_shadow.color.r = 0;
                    filter->params.drop_shadow.color.g = 0;
                    filter->params.drop_shadow.color.b = 0;
                    filter->params.drop_shadow.color.a = 255;

                    // Parse drop-shadow arguments: <offset-x> <offset-y> [<blur-radius>] [<color>]
                    // drop-shadow() uses space-separated args, so the parser may pack
                    // them into a single CSS_VALUE_TYPE_LIST — unwrap if needed
                    int ds_count = func->arg_count;
                    CssValue** ds_values = func->args;
                    if (ds_count == 1 && ds_values[0] && ds_values[0]->type == CSS_VALUE_TYPE_LIST) {
                        ds_count = ds_values[0]->data.list.count;
                        ds_values = ds_values[0]->data.list.values;
                    }
                    int len_idx = 0;
                    for (int i = 0; i < ds_count; i++) {
                        CssValue* a = ds_values[i];
                        if (!a) continue;
                        if (a->type == CSS_VALUE_TYPE_LENGTH) {
                            float val = resolve_length_value(lycon, prop_id, a);
                            if (len_idx == 0) filter->params.drop_shadow.offset_x = val;
                            else if (len_idx == 1) filter->params.drop_shadow.offset_y = val;
                            else if (len_idx == 2) filter->params.drop_shadow.blur_radius = val;
                            len_idx++;
                        } else if (a->type == CSS_VALUE_TYPE_COLOR) {
                            filter->params.drop_shadow.color.r = a->data.color.data.rgba.r;
                            filter->params.drop_shadow.color.g = a->data.color.data.rgba.g;
                            filter->params.drop_shadow.color.b = a->data.color.data.rgba.b;
                            filter->params.drop_shadow.color.a = a->data.color.data.rgba.a;
                        } else if (a->type == CSS_VALUE_TYPE_FUNCTION && a->data.function) {
                            // Nested color function like rgba(0,0,0,0.5) — resolve it
                            Color c = resolve_color_value(lycon, a);
                            filter->params.drop_shadow.color = c;
                        }
                    }
                }
                else {
                    return nullptr;
                }

                return filter;
            };

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                *target_filter = nullptr;
                break;
            }

            // Handle single filter function
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                *target_filter = is_backdrop_filter
                    ? (FilterProp*)alloc_prop(lycon, sizeof(FilterProp))
                    : span->ensure_filter(lycon);
                (*target_filter)->functions = parse_filter_func(value->data.function);
                break;
            }

            // Handle list of filter functions
            if (value->type == CSS_VALUE_TYPE_LIST) {
                *target_filter = is_backdrop_filter
                    ? (FilterProp*)alloc_prop(lycon, sizeof(FilterProp))
                    : span->ensure_filter(lycon);
                (*target_filter)->functions = nullptr;
                FilterFunction* tail = nullptr;

                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (item && item->type == CSS_VALUE_TYPE_FUNCTION) {
                        FilterFunction* f = parse_filter_func(item->data.function);
                        if (f) {
                            if (!(*target_filter)->functions) {
                                (*target_filter)->functions = f;
                            } else {
                                tail->next = f;
                            }
                            tail = f;
                        }
                    }
                }
            }
            break;
        }

        // ========================================================================
        // Multi-column Layout Properties
        // ========================================================================

        case CSS_PROPERTY_COLUMN_COUNT: {
            resolve_multicol_count(lycon, block, value);
            break;
        }

        case CSS_PROPERTY_COLUMN_WIDTH: {
            if (block) resolve_multicol_dimension(lycon, block, value, prop_id,
                false, false, "column-width");
            break;
        }

        // CSS Multicol §3.3: The 'columns' shorthand
        // Syntax: columns = <'column-width'> || <'column-count'>
        // A single integer → column-count; a single length → column-width;
        // 'auto' resets both to auto.
        case CSS_PROPERTY_COLUMNS: {
            if (!block) {
                break;
            }

            ensure_multicol_prop(lycon, block);

            // Reset both longhands to initial (auto) per shorthand rules
            block->multicol_prop()->column_count = 0;
            block->multicol_prop()->column_width = 0;

            // Process value(s) — can be single value or list of two
            int val_count = 1;
            const CssValue* vals[2] = { value, nullptr };
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 1) {
                val_count = value->data.list.count < 2 ? value->data.list.count : 2;
                vals[0] = value->data.list.values[0];
                if (val_count > 1) vals[1] = value->data.list.values[1];
            }

            for (int vi = 0; vi < val_count; vi++) {
                const CssValue* v = vals[vi];
                if (!v) continue;
                if (v->type == CSS_VALUE_TYPE_KEYWORD && v->data.keyword == CSS_VALUE_AUTO) {
                    // 'auto' — already reset above
                } else if (v->type == CSS_VALUE_TYPE_NUMBER && v->data.number.is_integer) {
                    int count = (int)v->data.number.value;
                    if (count > 0) {
                        block->multicol_prop()->column_count = count;
                    }
                } else if (v->type == CSS_VALUE_TYPE_LENGTH) {
                    float width = resolve_length_value(lycon, prop_id, v);
                    if (width > 0) {
                        block->multicol_prop()->column_width = width;
                    }
                } else if (v->type == CSS_VALUE_TYPE_NUMBER && !v->data.number.is_integer) {
                    // Non-integer number: could be column-width in px (unitless)
                    // CSS Multicol spec says column-width must be a length, but
                    // some parsers may emit unitless numbers. Treat as count if integer-valued.
                    int count = (int)v->data.number.value;
                    if (v->data.number.value == (double)count && count > 0) {
                        block->multicol_prop()->column_count = count;
                    }
                }
            }

            break;
        }

        // column-rule shorthand: <column-rule-width> || <column-rule-style> || <column-rule-color>
        case CSS_PROPERTY_COLUMN_RULE: {
            if (!block) break;

            ensure_multicol_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_LIST) {
                for (int i = 0; i < value->data.list.count; i++) {
                    const CssValue* v = value->data.list.values[i];
                    resolve_css_line_decoration_component(
                        lycon, prop_id, v, &block->multicol_prop()->rule_width,
                        &block->multicol_prop()->rule_style, &block->multicol_prop()->rule_color);
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                block->multicol_prop()->rule_style = CSS_VALUE_NONE;
                block->multicol_prop()->rule_width = 0;
            }

            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_WIDTH: {
            resolve_multicol_rule_width(lycon, block, value);
            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_STYLE: {
            resolve_multicol_rule_style(lycon, block, value);
            break;
        }

        case CSS_PROPERTY_COLUMN_RULE_COLOR: {
            resolve_multicol_rule_color(lycon, block, value);
            break;
        }

        case CSS_PROPERTY_COLUMN_SPAN: {
            if (!block) break;

            ensure_multicol_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_ALL) {
                    block->multicol_prop()->span = COLUMN_SPAN_ALL;
                } else {
                    block->multicol_prop()->span = COLUMN_SPAN_NONE;
                }
            }
            break;
        }

        case CSS_PROPERTY_BREAK_BEFORE:
        case CSS_PROPERTY_PAGE_BREAK_BEFORE:
        case CSS_PROPERTY_BREAK_AFTER:
        case CSS_PROPERTY_PAGE_BREAK_AFTER:
        case CSS_PROPERTY_ORPHANS:
        case CSS_PROPERTY_WIDOWS:
            resolve_flow_break_property(lycon, block, prop_id, value);
            break;

        case CSS_PROPERTY_BOX_DECORATION_BREAK: {
            if (!block) break;
            ensure_span_block(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                block->blk->box_decoration_break =
                    (kw == CSS_VALUE_CLONE) ? CSS_VALUE_CLONE : CSS_VALUE_SLICE;
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_FILL: {
            if (!block) break;

            ensure_multicol_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                if (kw == CSS_VALUE_AUTO) {
                    block->multicol_prop()->fill = COLUMN_FILL_AUTO;
                } else {
                    block->multicol_prop()->fill = COLUMN_FILL_BALANCE;
                }
            }
            break;
        }

        case CSS_PROPERTY_COLUMN_HEIGHT: {
            if (block) resolve_multicol_dimension(lycon, block, value, prop_id,
                true, true, "column-height");
            break;
        }

        case CSS_PROPERTY_COLUMN_WRAP: {
            if (!block) break;

            ensure_multicol_prop(lycon, block);

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_WRAP) {
                    block->multicol_prop()->wrap = COLUMN_WRAP_WRAP;
                } else if (value->data.keyword == CSS_VALUE_AUTO) {
                    block->multicol_prop()->wrap = COLUMN_WRAP_AUTO;
                } else {
                    block->multicol_prop()->wrap = COLUMN_WRAP_NOWRAP;
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH: {
            resolve_border_side_width(lycon, span, css_physical_side(prop_id), prop_id,
                                      value, specificity);
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_STYLE:
        case CSS_PROPERTY_BORDER_RIGHT_STYLE:
        case CSS_PROPERTY_BORDER_BOTTOM_STYLE:
        case CSS_PROPERTY_BORDER_LEFT_STYLE: {
            resolve_border_side_style(lycon, span, css_physical_side(prop_id),
                                      value, specificity);
            break;
        }

        case CSS_PROPERTY_BORDER_TOP_COLOR:
        case CSS_PROPERTY_BORDER_RIGHT_COLOR:
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
        case CSS_PROPERTY_BORDER_LEFT_COLOR: {
            resolve_border_side_color(lycon, span, css_physical_side(prop_id),
                                      value, specificity);
            break;
        }

        case CSS_PROPERTY_BORDER_IMAGE_SOURCE: {
            ensure_span_border(lycon, span);

            LinearGradient* gradient = nullptr;
            if (resolve_linear_gradient_value(lycon, value, &gradient)) {
                span->boundary_mut()->border->border_image_type = GRADIENT_LINEAR;
                span->boundary_mut()->border->border_image_linear_gradient = gradient;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->boundary_mut()->border->border_image_type = GRADIENT_NONE;
                span->boundary_mut()->border->border_image_linear_gradient = nullptr;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_IMAGE_WIDTH: {
            ensure_span_border(lycon, span);
            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                float width = value->type == CSS_VALUE_TYPE_NUMBER
                    ? value->data.number.value
                    : resolve_length_value(lycon, CSS_PROPERTY_BORDER_IMAGE_WIDTH, value);
                if (!isnan(width) && width >= 0.0f) {
                    span->boundary_mut()->border->border_image_width = width;
                    span->boundary_mut()->border->has_border_image_width = true;
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_IMAGE_REPEAT: {
            ensure_span_border(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                span->boundary_mut()->border->border_image_repeat = value->data.keyword;
            }
            break;
        }

        case CSS_PROPERTY_BORDER_IMAGE_SLICE:
        case CSS_PROPERTY_BORDER_IMAGE_OUTSET:
        case CSS_PROPERTY_BORDER_IMAGE: {
            break;
        }

        case CSS_PROPERTY_BORDER: {
            ensure_span_border(lycon, span);

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    copy_border_side_inherit(lycon, span, (CssBoxSide)side, specificity);
                }
            } else {
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    apply_border_side_shorthand(lycon, span, (CssBoxSide)side,
                                                value, specificity);
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_TOP:
        case CSS_PROPERTY_BORDER_RIGHT:
        case CSS_PROPERTY_BORDER_BOTTOM:
        case CSS_PROPERTY_BORDER_LEFT:
            apply_border_side_shorthand(lycon, span, css_physical_side(prop_id), value, specificity);
            break;

        // logical shorthands share the same physical side applicator; their
        // horizontal-writing-mode mapping is the existing Radiant contract.
        case CSS_PROPERTY_BORDER_INLINE:
        case CSS_PROPERTY_BORDER_BLOCK:
        case CSS_PROPERTY_BORDER_INLINE_START:
        case CSS_PROPERTY_BORDER_INLINE_END:
        case CSS_PROPERTY_BORDER_BLOCK_START:
        case CSS_PROPERTY_BORDER_BLOCK_END: {
            CssBoxSide first = CSS_BOX_SIDE_TOP;
            CssBoxSide second = CSS_BOX_SIDE_TOP;
            bool has_second = false;
            switch (prop_id) {
                case CSS_PROPERTY_BORDER_INLINE:
                    first = CSS_BOX_SIDE_RIGHT; second = CSS_BOX_SIDE_LEFT; has_second = true; break;
                case CSS_PROPERTY_BORDER_BLOCK:
                    first = CSS_BOX_SIDE_TOP; second = CSS_BOX_SIDE_BOTTOM; has_second = true; break;
                case CSS_PROPERTY_BORDER_INLINE_START: first = CSS_BOX_SIDE_LEFT; break;
                case CSS_PROPERTY_BORDER_INLINE_END: first = CSS_BOX_SIDE_RIGHT; break;
                case CSS_PROPERTY_BORDER_BLOCK_START: first = CSS_BOX_SIDE_TOP; break;
                case CSS_PROPERTY_BORDER_BLOCK_END: first = CSS_BOX_SIDE_BOTTOM; break;
                default: break;
            }
            apply_border_side_shorthand(lycon, span, first, value, specificity);
            if (has_second) {
                apply_border_side_shorthand(lycon, span, second, value, specificity);
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BLOCK_END_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_START_COLOR: {
            CssBoxSide side = prop_id == CSS_PROPERTY_BORDER_BLOCK_END_COLOR
                ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_TOP;
            resolve_border_side_color(lycon, span, side, value, specificity);
            break;
        }
        case CSS_PROPERTY_BORDER_BLOCK_END_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_START_WIDTH: {
            CssBoxSide side = prop_id == CSS_PROPERTY_BORDER_BLOCK_END_WIDTH
                ? CSS_BOX_SIDE_BOTTOM : CSS_BOX_SIDE_TOP;
            resolve_border_side_width(lycon, span, side,
                border_side_width_property(side), value, specificity);
            break;
        }
        case CSS_PROPERTY_BORDER_BLOCK_WIDTH: {
            resolve_border_side_width(lycon, span, CSS_BOX_SIDE_TOP,
                CSS_PROPERTY_BORDER_TOP_WIDTH, value, specificity);
            resolve_border_side_width(lycon, span, CSS_BOX_SIDE_BOTTOM,
                CSS_PROPERTY_BORDER_BOTTOM_WIDTH, value, specificity);
            break;
        }
        case CSS_PROPERTY_BORDER_BLOCK_COLOR: {
            resolve_border_side_color(lycon, span, CSS_BOX_SIDE_TOP, value, specificity);
            resolve_border_side_color(lycon, span, CSS_BOX_SIDE_BOTTOM, value, specificity);
            break;
        }

        case CSS_PROPERTY_BORDER_STYLE: {
            ensure_span_border(lycon, span);
            const CssValue* sides[4];
            if (css_expand_box_shorthand(value, sides)) {
                BorderProp* border = span->boundary_mut()->border;
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    if (sides[side]->type != CSS_VALUE_TYPE_KEYWORD) continue;
                    RadiantBorderSide refs = radiant_border_side(
                        border, (CssBoxSide)side);
                    *refs.style = sides[side]->data.keyword;
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_WIDTH: {
            ensure_span_border(lycon, span);
            resolve_spacing_prop(lycon, CSS_PROPERTY_BORDER_WIDTH, value, specificity, &span->boundary_mut()->border->width);
            break;

        }

        case CSS_PROPERTY_BORDER_COLOR: {
            ensure_span_border(lycon, span);
            const CssValue* sides[4];
            if (css_expand_box_shorthand(value, sides)) {
                BorderProp* border = span->boundary_mut()->border;
                for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
                    RadiantBorderSide refs = radiant_border_side(
                        border, (CssBoxSide)side);
                    if (specificity < *refs.color_specificity) continue;
                    *refs.color = resolve_color_value(lycon, sides[side]);
                    *refs.color_specificity = specificity;
                }
            }
            break;
        }

        case CSS_PROPERTY_BORDER_RADIUS: {
            ensure_span_border(lycon, span);
            apply_border_radius_shorthand(lycon, prop_id, &span->boundary_mut()->border->radius, value, specificity);
            break;
        }

        // ===== GROUP 15: Additional Border Properties =====
        case CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS:
        case CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS:
        case CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS:
        case CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS: {
            ensure_span_border(lycon, span);
            int corner = prop_id == CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS ? 0
                : prop_id == CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS ? 1
                : prop_id == CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS ? 2 : 3;
            apply_corner_radius_value(lycon, prop_id,
                                      &span->boundary_mut()->border->radius,
                                      corner, value, specificity);
            break;
        }

        // ===== GROUP 4: Layout Properties =====
        case CSS_PROPERTY_DISPLAY: {
            // nothing to do here
            break;
        }

        case CSS_PROPERTY_POSITION: {
            ensure_span_position(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_INHERIT) {
                    // CSS 2.1 §6.2.1: inherit from parent's computed value
                    DomElement* parent = lycon->elmt->parent ? lycon->elmt->parent->as_element() : nullptr;
                    if (parent && parent->position) {
                        span->position->position = parent->position->position;
                    } else {
                        span->position->position = CSS_VALUE_STATIC;
                    }
                } else {
                    span->position->position = val;
                }
            }
            break;
        }

        case CSS_PROPERTY_INSET: {
            resolve_inset_shorthand(lycon, span, value);
            break;
        }

        case CSS_PROPERTY_INSET_INLINE:
        case CSS_PROPERTY_INSET_INLINE_START:
        case CSS_PROPERTY_INSET_INLINE_END:
        case CSS_PROPERTY_INSET_BLOCK:
        case CSS_PROPERTY_INSET_BLOCK_START:
        case CSS_PROPERTY_INSET_BLOCK_END: {
            resolve_logical_inset_property(lycon, span, prop_id, value,
                                           inline_axis_is_vertical,
                                           vertical_block_start_is_right);
            break;
        }

        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_LEFT:
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_BOTTOM:
            resolve_inset_side(lycon, span, css_physical_side(prop_id), prop_id, value, true);
            break;

        case CSS_PROPERTY_Z_INDEX: {
            ensure_span_position(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int z = (int)value->data.number.value;
                span->position->z_index = z;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // 'auto' keyword - typically means z-index = 0
                span->position->z_index = 0;
            }
            break;
        }

        // ===== GROUP 7: Float and Clear =====

        case CSS_PROPERTY_FLOAT:
        case CSS_PROPERTY_CLEAR:
            resolve_float_clear_property(lycon, block, prop_id, value);
            break;

        // ===== GROUP 8: Overflow Properties =====

        case CSS_PROPERTY_OVERFLOW:
        case CSS_PROPERTY_OVERFLOW_X:
        case CSS_PROPERTY_OVERFLOW_Y: {
            if (!block) break;
            block->ensure_scroll(lycon);

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword > 0) {
                CssEnum overflow_value = value->data.keyword;
                if (prop_id != CSS_PROPERTY_OVERFLOW_Y) {
                    block->scroller->overflow_x = overflow_value;
                }
                if (prop_id != CSS_PROPERTY_OVERFLOW_X) {
                    block->scroller->overflow_y = overflow_value;
                }
            }
            break;
        }

        case CSS_PROPERTY_SCROLLBAR_GUTTER: {
            if (!block || !value) break;
            block->ensure_scroll(lycon);
            block->scroller->scrollbar_gutter_stable = css_value_has_identifier(value, "stable");
            block->scroller->scrollbar_gutter_both_edges =
                css_value_has_identifier(value, "both-edges");
            // A gutter only reserves layout space in the stable mode; `auto`
            // remains dependent on whether a scrollbar is later needed.
            if (!block->scroller->scrollbar_gutter_stable) {
                block->scroller->scrollbar_gutter_both_edges = false;
            }
            break;
        }

        case CSS_PROPERTY_APPEARANCE: {
            if (!block || !block->form || !value ||
                value->type != CSS_VALUE_TYPE_KEYWORD) {
                break;
            }
            CssEnum appearance = value->data.keyword;
            // Form intrinsic metrics include UA chrome only while the control
            // retains its native appearance; this also covers the webkit alias.
            block->form->appearance_none = appearance == CSS_VALUE_NONE;
            block->form->appearance_base_select = appearance == CSS_VALUE_BASE_SELECT;
            break;
        }

        // ===== GROUP 9: White-space Property =====

        case CSS_PROPERTY_WHITE_SPACE: {
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;
        }

        // ===== GROUP 10: Visibility and Opacity =====
        case CSS_PROPERTY_VISIBILITY:
        case CSS_PROPERTY_OPACITY:
            resolve_inline_visibility_opacity(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_CLIP: {
            if (!block) break;
            block->ensure_scroll(lycon);


            // CSS clip property uses rect(top, right, bottom, left) syntax
            // TODO: Parse rect() values and set block->scroll()->clip bounds
            // For now, clip bounds will be set during layout based on block dimensions
            break;
        }

        // ===== GROUP 11: Box Sizing =====
        case CSS_PROPERTY_BOX_SIZING: {
            if (!block) break;
            ensure_span_block(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum boxsizing_value = value->data.keyword;
                if (boxsizing_value == CSS_VALUE_INHERIT) {
                    boxsizing_value = resolve_box_sizing_inherit(lycon);
                    block->blk->box_sizing = boxsizing_value;
                } else if (boxsizing_value == CSS_VALUE_INITIAL ||
                           boxsizing_value == CSS_VALUE_UNSET ||
                           boxsizing_value == CSS_VALUE_REVERT) {
                    block->blk->box_sizing = CSS_VALUE_CONTENT_BOX;
                } else if (boxsizing_value == CSS_VALUE_CONTENT_BOX ||
                           boxsizing_value == CSS_VALUE_BORDER_BOX) {
                    block->blk->box_sizing = boxsizing_value;
                }
            }
            break;
        }

        case CSS_PROPERTY_ASPECT_RATIO: {
            // aspect-ratio can apply to block-level and flex/grid items
            // For grid items, aspect-ratio is read from specified_style during layout
            // (fi and gi are in a union, so we can't store aspect_ratio in fi for grid items)
            if (!span) break;

            // Don't allocate fi for grid items - it would overwrite gi in the union!
            // Grid layout reads aspect-ratio from specified_style instead
            if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
                break;
            }

            if (!span->fi) { alloc_flex_item_prop(lycon, span); }
            if (!span->fi) break;

            // aspect-ratio values: auto | <ratio> | auto && <ratio>
            // <ratio> is expressed as "width / height" (e.g., "16 / 9") or just a number (e.g., "2")
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // "auto" means no aspect ratio enforced
                span->fi->aspect_ratio = 0;
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single number means ratio = number (e.g., aspect-ratio: 2 means 2/1)
                span->fi->aspect_ratio = (float)value->data.number.value;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
                // List format: [number, "/", number] for "16 / 9" syntax
                // Find two numbers in the list
                double numerator = 0, denominator = 0;
                bool got_numerator = false, got_denominator = false;
                for (int i = 0; i < value->data.list.count && !got_denominator; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (item && item->type == CSS_VALUE_TYPE_NUMBER) {
                        if (!got_numerator) {
                            numerator = item->data.number.value;
                            got_numerator = true;
                        } else {
                            denominator = item->data.number.value;
                            got_denominator = true;
                        }
                    }
                }
                if (got_numerator && got_denominator) {
                    if (denominator > 0) {
                        span->fi->aspect_ratio = (float)(numerator / denominator);
                    } else {
                        // A zero denominator makes the ratio invalid; do not
                        // reinterpret the numerator as a single-number ratio.
                        span->fi->aspect_ratio = 0;
                    }
                } else if (got_numerator) {
                    // Just one number in list means ratio = number
                    span->fi->aspect_ratio = (float)numerator;
                }
            }
            break;
        }

        // ===== GROUP 12: Advanced Typography Properties =====

        case CSS_PROPERTY_FONT_STYLE: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "font-style")) break;
            span->ensure_font(lycon);
            resolve_keyword_slot(value, &span->font_mut()->font_style);
            break;
        }

        case CSS_PROPERTY_TEXT_TRANSFORM:
        case CSS_PROPERTY_TEXT_WRAP_STYLE:
        case CSS_PROPERTY_TEXT_OVERFLOW:
        case CSS_PROPERTY_WORD_BREAK:
        case CSS_PROPERTY_LINE_BREAK:
        case CSS_PROPERTY_WORD_WRAP:
        case CSS_PROPERTY_OVERFLOW_WRAP:
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;

        case CSS_PROPERTY_LINE_CLAMP:
        case CSS_PROPERTY_WEBKIT_LINE_CLAMP:
            resolve_line_count_property(lycon, block, prop_id, value);
            break;

        case CSS_PROPERTY_TAB_SIZE: {
            ensure_span_block(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value >= 0.0) {
                span->blk->tab_size = (int)value->data.number.value; // INT_CAST_OK: tab-size is a count.
            }
            break;
        }

        case CSS_PROPERTY_FONT_VARIANT: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl, "font-variant")) break;
            span->ensure_font(lycon);


            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_INHERIT) {
                    // inherit from parent
                    DomElement* ancestor = lam::dom_require_element(lycon->view);
                    if (ancestor && ancestor->font) {
                        span->font->font_variant = ancestor->font->font_variant;
                    }
                } else if (val > 0) {
                    span->font->font_variant = val;
                }
            } else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Handle unregistered keywords (e.g., "small-caps" parsed as custom)
                CssEnum val = css_enum_by_name(value->data.custom_property.name);
                if (val != CSS_VALUE__UNDEF) {
                    span->font->font_variant = val;
                }
            }
            break;
        }

        case CSS_PROPERTY_FONT_KERNING:
            resolve_simple_keyword_property(lycon, span, block, prop_id, value);
            break;

        case CSS_PROPERTY_LETTER_SPACING:
        case CSS_PROPERTY_WORD_SPACING:
            resolve_font_spacing_property(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_TEXT_SHADOW: {
            if (!span->font) {
                break;
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->font->text_shadow = nullptr;
                break;
            }

            // text-shadow: offset-x offset-y [blur-radius] [color] [, ...]
            auto parse_single_text_shadow = [&](const CssValue* sv) -> TextShadow* {
                TextShadow* ts = (TextShadow*)alloc_prop(lycon, sizeof(TextShadow));
                memset(ts, 0, sizeof(TextShadow));
                ts->color.a = 255;  // default opaque black

                if (sv->type == CSS_VALUE_TYPE_LIST) {
                    int length_count = 0;
                    for (int i = 0; i < sv->data.list.count; i++) {
                        const CssValue* v = sv->data.list.values[i];
                        if (!v) continue;
                        if (v->type == CSS_VALUE_TYPE_KEYWORD) {
                            ts->color = color_name_to_rgb(v->data.keyword);
                        } else if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_NUMBER) {
                            float val = (v->type == CSS_VALUE_TYPE_LENGTH)
                                ? resolve_length_value(lycon, prop_id, v)
                                : v->data.number.value;
                            switch (length_count) {
                                case 0: ts->offset_x = val; break;
                                case 1: ts->offset_y = val; break;
                                case 2: ts->blur_radius = val; break;
                            }
                            length_count++;
                        } else if (v->type == CSS_VALUE_TYPE_COLOR || v->type == CSS_VALUE_TYPE_FUNCTION) {
                            ts->color = resolve_color_value(lycon, v);
                        }
                    }
                }
                return ts;
            };

            TextShadow* ts_head = nullptr;
            TextShadow* ts_tail = nullptr;

            if (value->type == CSS_VALUE_TYPE_LIST) {
                bool is_multi = false;
                for (int i = 0; i < value->data.list.count && !is_multi; i++) {
                    if (value->data.list.values[i] &&
                        value->data.list.values[i]->type == CSS_VALUE_TYPE_LIST) {
                        is_multi = true;
                    }
                }
                if (is_multi) {
                    for (int i = 0; i < value->data.list.count; i++) {
                        const CssValue* sv = value->data.list.values[i];
                        if (!sv) continue;
                        TextShadow* ts = parse_single_text_shadow(sv);
                        if (ts) {
                            if (!ts_head) { ts_head = ts; ts_tail = ts; }
                            else { ts_tail->next = ts; ts_tail = ts; }
                        }
                    }
                } else {
                    ts_head = parse_single_text_shadow(value);
                }
            }

            span->font->text_shadow = ts_head;
            break;
        }

        // ===== GROUP 13: Flexbox Properties =====

        case CSS_PROPERTY_FLEX_DIRECTION:
        case CSS_PROPERTY_FLEX_WRAP: {
            if (!block) {
                break;
            }
            alloc_flex_prop(lycon, block);
            auto* slot = prop_id == CSS_PROPERTY_FLEX_DIRECTION
                ? &block->embedp()->flex->direction : &block->embedp()->flex->wrap;
            resolve_keyword_slot(value, slot);
            break;
        }

        case CSS_PROPERTY_JUSTIFY_CONTENT:
        case CSS_PROPERTY_ALIGN_ITEMS:
            resolve_flex_grid_container_alignment(lycon, block, prop_id, value);
            break;

        case CSS_PROPERTY_ALIGN_CONTENT:
            resolve_flex_grid_container_alignment(lycon, block, prop_id, value);
            break;

        // grid-row-gap is the legacy name for row-gap (CSS Grid Level 1)
        case CSS_PROPERTY_GRID_ROW_GAP:
        case CSS_PROPERTY_ROW_GAP:
            resolve_gap_property(lycon, block, prop_id, value, true);
            break;

        // grid-column-gap is the legacy name for column-gap (CSS Grid Level 1)
        case CSS_PROPERTY_GRID_COLUMN_GAP:
        case CSS_PROPERTY_COLUMN_GAP:
            resolve_gap_property(lycon, block, prop_id, value, false);
            break;

        case CSS_PROPERTY_WRITING_MODE: {
            if (!block) break;
            BlockProp* block_prop = ensure_span_block(lycon, block);
            if (!block_prop) break;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                WritingMode mode = layout_writing_mode_from_css(val);
                block_prop->writing_mode = mode;
                // Flex layout still consumes its own axis field, but ordinary
                // blocks must not acquire FlexProp merely to store writing-mode.
                if (block->embed && block->embedp()->flex) {
                    block->embedp()->flex->writing_mode = mode;
                }
            }
            break;
        }

        // Grid Template Properties
        case CSS_PROPERTY_GRID_TEMPLATE_COLUMNS:
        case CSS_PROPERTY_GRID_TEMPLATE_ROWS: {
            bool columns = prop_id == CSS_PROPERTY_GRID_TEMPLATE_COLUMNS;
            const char* property_name = columns ?
                "grid-template-columns" : "grid-template-rows";
            if (!block) {
                break;
            }

            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embedp()->grid;
            GridTrackList** track_list_ptr = columns ?
                &grid->grid_template_columns : &grid->grid_template_rows;
            apply_grid_template_track_value(value, track_list_ptr, property_name);
            break;
        }

        case CSS_PROPERTY_GRID_TEMPLATE: {
            if (!block) {
                break;
            }
            alloc_grid_prop(lycon, block);
            apply_grid_template_shorthand(value, block->embedp()->grid);
            break;
        }

        case CSS_PROPERTY_GRID: {
            if (!block) {
                break;
            }
            alloc_grid_prop(lycon, block);
            apply_grid_shorthand(value, block->embedp()->grid);
            break;
        }

        case CSS_PROPERTY_GRID_TEMPLATE_AREAS: {
            if (!block) {
                break;
            }

            alloc_grid_prop(lycon, block);
            GridProp* grid = block->embedp()->grid;

            // Handle "none" keyword
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                clear_grid_template_areas(grid);
                break;
            }

            // Handle string value containing area definitions
            // CSS format: "header header header" "sidebar main aside" "footer footer footer"
            if (value->type == CSS_VALUE_TYPE_STRING) {
                parse_grid_template_areas(grid, value->data.string, &lycon->scratch);
            }
            // Handle list of strings (each row is a separate string)
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Concatenate all strings with quotes to form complete areas string
                // Each string needs to be wrapped in quotes for the parser
                size_t total_len = 0;
                for (int i = 0; i < value->data.list.count; i++) {
                    if (value->data.list.values[i]->type == CSS_VALUE_TYPE_STRING) {
                        // +3 for: quote, space/null, quote
                        total_len += strlen(value->data.list.values[i]->data.string) + 4;
                    }
                }
                if (total_len > 0) {
                    char* combined = (char*)scratch_alloc(&lycon->scratch, total_len + 1);
                    combined[0] = '\0';
                    size_t combined_len = 0;
                    for (int i = 0; i < value->data.list.count; i++) {
                        if (value->data.list.values[i]->type == CSS_VALUE_TYPE_STRING) {
                            if (combined_len > 0) combined_len = str_cat(combined, combined_len, total_len + 1, " ", 1);
                            // Wrap each row in quotes
                            combined_len = str_cat(combined, combined_len, total_len + 1, "\"", 1);
                            combined_len = str_cat(combined, combined_len, total_len + 1, value->data.list.values[i]->data.string, strlen(value->data.list.values[i]->data.string));
                            combined_len = str_cat(combined, combined_len, total_len + 1, "\"", 1);
                        }
                    }
                    parse_grid_template_areas(grid, combined, &lycon->scratch);
                    scratch_free(&lycon->scratch, combined);
                }
            }
            break;
        }

        case CSS_PROPERTY_GRID_AREA: {
            alloc_grid_item_prop(lycon, span);

            // grid-area can be:
            // 1. A named area: grid-area: header
            // 2. A shorthand for row-start / column-start / row-end / column-end
            if (value->type == CSS_VALUE_TYPE_STRING) {
                // Named area (quoted string)
                replace_view_pool_layout_string(lycon, &span->gi->grid_area, value->data.string);
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                // Named area (unquoted identifier like "header")
                // Stored as custom property reference when not a known keyword
                if (value->data.custom_property.name) {
                    replace_view_pool_layout_string(lycon, &span->gi->grid_area, value->data.custom_property.name);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Can be "auto" or an identifier (area name)
                const char* name = css_enum_info(value->data.keyword)->name;
                if (value->data.keyword != CSS_VALUE_AUTO) {
                    replace_view_pool_layout_string(lycon, &span->gi->grid_area, name);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Shorthand: row-start / column-start / row-end / column-end
                // Values separated by /
                int count = value->data.list.count;

                auto parse_line = [](const CssValue* v, int* line, bool* has_explicit, bool* is_span) {
                    *has_explicit = false;
                    *is_span = false;
                    if (v->type == CSS_VALUE_TYPE_NUMBER) {
                        *line = (int)v->data.number.value;
                        *has_explicit = true;
                    } else if (v->type == CSS_VALUE_TYPE_KEYWORD && v->data.keyword == CSS_VALUE_AUTO) {
                        *line = 0;
                        *has_explicit = false;
                    } else if (v->type == CSS_VALUE_TYPE_FUNCTION && v->data.function) {
                        // span N - function named "span"
                        CssFunction* func = v->data.function;
                        if (strcmp(func->name, "span") == 0 && func->arg_count > 0 &&
                            func->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
                            int span_val = (int)func->args[0]->data.number.value;
                            if (span_val > MAX_GRID_SPAN) {
                                span_val = MAX_GRID_SPAN;
                            }
                            *line = -span_val;
                            *has_explicit = true;
                            *is_span = true;
                        }
                    }
                };

                // CSS Grid grammar separates components with slash tokens;
                // those tokens are list entries but are not shorthand values.
                CssValue* components[4] = {};
                int component_count = 0;
                for (int i = 0; i < count && component_count < 4; i++) {
                    if (!css_grid_is_separator(value->data.list.values[i])) {
                        components[component_count++] = value->data.list.values[i];
                    }
                }

                if (component_count >= 1) {
                    parse_line(components[0], &span->gi->grid_row_start,
                              &span->gi->has_explicit_grid_row_start, &span->gi->grid_row_start_is_span);
                }
                if (component_count >= 2) {
                    parse_line(components[1], &span->gi->grid_column_start,
                              &span->gi->has_explicit_grid_column_start, &span->gi->grid_column_start_is_span);
                }
                if (component_count >= 3) {
                    parse_line(components[2], &span->gi->grid_row_end,
                              &span->gi->has_explicit_grid_row_end, &span->gi->grid_row_end_is_span);
                }
                if (component_count >= 4) {
                    parse_line(components[3], &span->gi->grid_column_end,
                              &span->gi->has_explicit_grid_column_end, &span->gi->grid_column_end_is_span);
                }
            }
            break;
        }

        // Grid Item Placement Properties
        case CSS_PROPERTY_GRID_COLUMN_START:
        case CSS_PROPERTY_GRID_COLUMN_END:
        case CSS_PROPERTY_GRID_ROW_START:
        case CSS_PROPERTY_GRID_ROW_END:
        case CSS_PROPERTY_GRID_COLUMN:
        case CSS_PROPERTY_GRID_ROW:
            resolve_grid_placement_property(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_GRID_AUTO_FLOW: {
            if (!block) {
                break;
            }
            alloc_grid_prop(lycon, block);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum flow = value->data.keyword;
                block->embedp()->grid->grid_auto_flow = flow;
            }
            break;
        }

        case CSS_PROPERTY_GRID_AUTO_ROWS:
            resolve_grid_auto_track(lycon, block, value, true);
            break;
        case CSS_PROPERTY_GRID_AUTO_COLUMNS:
            resolve_grid_auto_track(lycon, block, value, false);
            break;

        case CSS_PROPERTY_JUSTIFY_ITEMS:
        case CSS_PROPERTY_JUSTIFY_SELF:
            resolve_grid_alignment_property(lycon, block, span, prop_id, value);
            break;

        case CSS_PROPERTY_PLACE_ITEMS: {
            // place-items is a shorthand for align-items and justify-items
            // Syntax: place-items: <align-items> <justify-items>?
            // If only one value, it applies to both
            if (!block) {
                break;
            }

            alloc_grid_prop(lycon, block);
            alloc_flex_prop(lycon, block);

            CssEnum align_val = CSS_VALUE_STRETCH;
            CssEnum justify_val = CSS_VALUE_STRETCH;
            css_resolve_keyword_pair(value, CSS_VALUE_STRETCH,
                                     &align_val, &justify_val);

            // Apply to grid
            block->embedp()->grid->align_items = align_val;
            block->embedp()->grid->justify_items = justify_val;
            // Also apply to flex
            block->embedp()->flex->align_items = align_val;

            break;
        }

        case CSS_PROPERTY_PLACE_SELF: {
            // place-self is a shorthand for align-self and justify-self
            // Syntax: place-self: <align-self> <justify-self>?
            // If only one value, it applies to both

            CssEnum align_val = CSS_VALUE_AUTO;
            CssEnum justify_val = CSS_VALUE_AUTO;
            css_resolve_keyword_pair(value, CSS_VALUE_AUTO,
                                     &align_val, &justify_val);

            // Set align-self based on item type
            if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
                span->gi->align_self_grid = align_val;
                span->gi->justify_self = justify_val;
            } else if (span->parent_item_kind() == DomElement::PARENT_ITEM_FLEX) {
                span->fi->align_self = align_val;
                // Note: justify-self doesn't apply to flex items in the main axis
            } else {
                // Neither allocated yet - allocate grid prop (for grid items)
                // or flex prop (for flex items). Default to grid since place-self
                // is primarily used with grid.
                alloc_grid_item_prop(lycon, span);
                span->gi->align_self_grid = align_val;
                span->gi->justify_self = justify_val;
            }

            break;
        }

        case CSS_PROPERTY_FLEX_GROW:
        case CSS_PROPERTY_FLEX_SHRINK:
        case CSS_PROPERTY_ORDER:
            resolve_flex_item_number(lycon, span, prop_id, value);
            break;

        case CSS_PROPERTY_FLEX_BASIS: {
            alloc_flex_item_prop(lycon, span);
            if (!span->flex_item()) break;
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
                css_set_flex_basis_value(span, -1.0f, false, false, false);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                       value->data.keyword == CSS_VALUE_CONTENT) {
                css_set_flex_basis_value(span, -1.0f, false, true, false);
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                       value->data.keyword == CSS_VALUE_STRETCH) {
                css_set_flex_basis_value(span, -1.0f, false, false, true);
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float basis_value = resolve_length_value(lycon, prop_id, value);
                css_set_flex_basis_value(span, basis_value, false, false, false);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                css_set_flex_basis_value(span, (float)value->data.percentage.value,
                                         true, false, false);
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // CSS allows unitless 0 as a valid zero length (e.g. flex-basis: 0)
                float basis_value = (float)value->data.number.value;
                css_set_flex_basis_value(span, basis_value, false, false, false);
            }
            break;
        }

        case CSS_PROPERTY_ALIGN_SELF:
            resolve_grid_alignment_property(lycon, block, span, prop_id, value);
            break;

        case CSS_PROPERTY_FLEX_FLOW: {
            if (!block) {
                break;
            }
            alloc_flex_prop(lycon, block);

            // flex-flow is a shorthand for flex-direction and flex-wrap
            // Values can appear in any order: "column wrap", "wrap column", "row-reverse", etc.
            auto is_direction = [](CssEnum val) -> bool {
                return val == CSS_VALUE_ROW || val == CSS_VALUE_ROW_REVERSE ||
                       val == CSS_VALUE_COLUMN || val == CSS_VALUE_COLUMN_REVERSE;
            };
            auto is_wrap = [](CssEnum val) -> bool {
                return val == CSS_VALUE_NOWRAP || val == CSS_VALUE_WRAP || val == CSS_VALUE_WRAP_REVERSE;
            };

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                // Single keyword: either direction or wrap
                CssEnum val = value->data.keyword;
                if (is_direction(val)) {
                    block->embedp()->flex->direction = val;
                } else if (is_wrap(val)) {
                    block->embedp()->flex->wrap = val;
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                for (size_t i = 0; i < count && i < 2; i++) {
                    if (values[i]->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum val = values[i]->data.keyword;
                        if (is_direction(val)) {
                            block->embedp()->flex->direction = val;
                        } else if (is_wrap(val)) {
                            block->embedp()->flex->wrap = val;
                        }
                    }
                }
            }
            break;
        }

        case CSS_PROPERTY_FLEX: {
            alloc_flex_item_prop(lycon, span);
            if (!span->flex_item()) break;
            // flex is a shorthand for flex-grow, flex-shrink, and flex-basis
            // Syntax: none | [ <'flex-grow'> <'flex-shrink'>? || <'flex-basis'> ]

            ViewSpan* span = lam::view_require_element(lycon->view);
            // Initialize with defaults
            float flex_grow = 1.0f;      // default when using shorthand
            float flex_shrink = 1.0f;    // default
            float flex_basis = -1.0f;    // auto
            bool flex_basis_is_percent = false;
            bool flex_basis_is_stretch = false;

            // Handle single keyword values
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    flex_grow = 0;
                    flex_shrink = 0;
                    flex_basis = -1;  // auto
                } else if (value->data.keyword == CSS_VALUE_AUTO) {
                    flex_grow = 1;
                    flex_shrink = 1;
                    flex_basis = -1;  // auto
                } else if (value->data.keyword == CSS_VALUE_INITIAL) {
                    flex_grow = 0;
                    flex_shrink = 1;
                    flex_basis = -1;  // auto
                }

                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         flex_basis, flex_basis_is_percent);
                break;
            }

            // Parse multi-value flex shorthand (e.g., "1 0 100px" or "2 1 50px")
            if (value->type == CSS_VALUE_TYPE_LIST) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;

                int value_index = 0;
                bool found_basis = false;

                // Parse up to 3 values: grow, shrink, basis
                for (size_t i = 0; i < count && i < 3; i++) {
                    CssValue* val = values[i];

                    if (val->type == CSS_VALUE_TYPE_NUMBER) {
                        // Numbers are grow and shrink (unitless), except:
                        // - Third number is flex-basis (unitless 0 is valid for lengths)
                        // - If the number is 0 and we already have grow+shrink, it's basis=0
                        if (value_index == 0) {
                            flex_grow = (float)val->data.number.value;
                            value_index++;
                        } else if (value_index == 1) {
                            flex_shrink = (float)val->data.number.value;
                            value_index++;
                        } else if (value_index == 2 && val->data.number.value == 0) {
                            // Third value is unitless 0 -> flex-basis: 0
                            // CSS allows unitless 0 for any length value
                            flex_basis = 0;
                            flex_basis_is_percent = false;
                            found_basis = true;
                        }
                    } else if (val->type == CSS_VALUE_TYPE_LENGTH) {
                        // Length is basis
                        flex_basis = val->data.length.value;
                        flex_basis_is_percent = false;
                        found_basis = true;
                    } else if (val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                        // Percentage is basis
                        flex_basis = val->data.percentage.value;
                        flex_basis_is_percent = true;
                        found_basis = true;
                    } else if (val->type == CSS_VALUE_TYPE_KEYWORD) {
                        if (val->data.keyword == CSS_VALUE_AUTO) {
                            flex_basis = -1;  // auto
                            flex_basis_is_percent = false;
                            found_basis = true;
                        } else if (val->data.keyword == CSS_VALUE_STRETCH) {
                            flex_basis = -1;
                            flex_basis_is_percent = false;
                            flex_basis_is_stretch = true;
                            found_basis = true;
                        }
                    }
                }

                // If only one number was provided, it's grow with implicit 1 0
                if (count == 1 && value_index == 1 && !found_basis) {
                    flex_shrink = 1.0f;
                    flex_basis = 0;  // 0px basis when single number
                }

                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         flex_basis, flex_basis_is_percent);
                span->fi->flex_basis_is_stretch = flex_basis_is_stretch;

            }
            else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                // Single number without list wrapper: just flex-grow
                flex_grow = (float)value->data.number.value;
                flex_shrink = 1.0f;
                flex_basis = 0;  // 0px when single unitless number

                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         flex_basis, false);
            }
            break;
        }

        // Animation declarations are consumed by css_animation.cpp; this
        // layout resolver intentionally has no second, logging-only path.
        case CSS_PROPERTY_ANIMATION:
        case CSS_PROPERTY_ANIMATION_NAME:
        case CSS_PROPERTY_ANIMATION_DURATION:
        case CSS_PROPERTY_ANIMATION_TIMING_FUNCTION:
        case CSS_PROPERTY_ANIMATION_DELAY:
        case CSS_PROPERTY_ANIMATION_ITERATION_COUNT:
        case CSS_PROPERTY_ANIMATION_DIRECTION:
        case CSS_PROPERTY_ANIMATION_FILL_MODE:
        case CSS_PROPERTY_ANIMATION_PLAY_STATE:
            break;

        // List Properties (Group 18)
        case CSS_PROPERTY_LIST_STYLE_TYPE: {
            ensure_span_block(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum type = value->data.keyword;
                span->blk->list_style_type = type;
                span->blk->list_style_type_string = nullptr;
            } else if (value->type == CSS_VALUE_TYPE_STRING) {
                // CSS Lists 3 §4.1: list-style-type can be a <string>
                // The string is used as the marker content directly
                css_store_list_style_type_string(lycon, span, value->data.string);
                }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_POSITION: {
            ensure_span_block(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum position = value->data.keyword;
                span->blk->list_style_position = position;
            }
            // CSS 2.1 §12.5.1: "inside" and "outside" may arrive as CSS_VALUE_TYPE_CUSTOM
            // when not in the CssEnum table. Handle them by name comparison.
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                CssEnum position;
                if (css_list_style_custom_position(value->data.custom_property.name, &position)) {
                    span->blk->list_style_position = position;
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE_IMAGE: {
            ensure_span_block(lycon, span);
            // Extract URL from either CSS_VALUE_TYPE_URL or CSS_VALUE_TYPE_FUNCTION(url)
            if (!css_store_list_style_image(lycon, span, value)) {
                if (value->type != CSS_VALUE_TYPE_KEYWORD) break;
                if (value->data.keyword == CSS_VALUE_NONE) {
                    span->blk->list_style_image = (char*)alloc_prop(lycon, 5);
                    str_copy(span->block()->list_style_image, 5, "none", 4);
                }
            }
            break;
        }

        case CSS_PROPERTY_LIST_STYLE: {
            // CSS 2.1 Section 12.5.1: list-style shorthand
            // Syntax: list-style: [ <list-style-type> || <list-style-position> || <list-style-image> ] | inherit

            ensure_span_block(lycon, span);

            // Handle 'inherit' keyword: copy all three list-style sub-properties from parent
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                DomElement* dom_elem = lam::dom_require_element(lycon->view);
                DomElement* parent = dom_elem->parent ? dom_elem->parent->as_element() : nullptr;
                while (parent) {
                    if (parent->blk) {
                        if (parent->block()->list_style_type) {
                            span->blk->list_style_type = parent->blk->list_style_type;
                        }
                        if (parent->block()->list_style_position) {
                            span->blk->list_style_position = parent->blk->list_style_position;
                        }
                        if (parent->block()->list_style_image) {
                            span->blk->list_style_image = parent->blk->list_style_image;
                        }
                        break;
                    }
                    parent = parent->parent ? parent->parent->as_element() : nullptr;
                }
                break;
            }

            // Handle single keyword value (most common case)
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = value->data.keyword;
                css_apply_list_style_keyword(lycon, span, keyword, false);
            }
            // Handle string value for list-style-type (CSS Lists 3 §4.1)
            else if (value->type == CSS_VALUE_TYPE_STRING && value->data.string) {
                css_store_list_style_type_string(lycon, span, value->data.string);
            }
            // Handle custom property reference (which might be misidentified keywords like "inside")
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                // Check if it's actually a keyword like "inside" or "outside"
                const char* name = value->data.custom_property.name;

                CssEnum position;
                if (css_list_style_custom_position(name, &position)) {
                    // "inside" keyword - set position to inside
                    span->blk->list_style_position = position;
                    // CSS 2.1: Initial value for list-style-type is 'disc'
                    // If only position is specified, use default disc marker
                    if (span->block()->list_style_type == 0) {
                        span->blk->list_style_type = CSS_VALUE_DISC;
                    }
                }
            }
            // Handle URL for list-style-image
            else if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_FUNCTION) {
                css_store_list_style_image(lycon, span, value);
            }
            // Handle multiple values (e.g., "square inside", "disc outside")
            else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {

                // Iterate through all values in the list
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;

                    if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum keyword = item->data.keyword;
                        css_apply_list_style_keyword(lycon, span, keyword, true);
                    }
                    else if (item->type == CSS_VALUE_TYPE_CUSTOM && item->data.custom_property.name) {
                        // Handle "inside"/"outside" that might be parsed as custom
                        const char* name = item->data.custom_property.name;
                        CssEnum position;
                        if (css_list_style_custom_position(name, &position)) {
                            span->blk->list_style_position = position;
                        }
                    }
                    else if (item->type == CSS_VALUE_TYPE_STRING && item->data.string) {
                        // CSS Lists 3 §4.1: string value for list-style-type in shorthand
                        css_store_list_style_type_string(lycon, span, item->data.string);
                    }
                    else if (item->type == CSS_VALUE_TYPE_URL || item->type == CSS_VALUE_TYPE_FUNCTION) {
                        css_store_list_style_image(lycon, span, item);
                    }
                }
            }

            // CSS 2.1 §12.5.1: If list-style shorthand didn't explicitly set
            // list-style-type, it defaults to 'disc' (the initial value).
            // Without this, marker generation is skipped because list_style_type==0.
            if (span->block()->list_style_type == 0) {
                span->blk->list_style_type = CSS_VALUE_DISC;
            }
            break;
        }

        case CSS_PROPERTY_COUNTER_RESET:
        case CSS_PROPERTY_COUNTER_INCREMENT:
        case CSS_PROPERTY_COUNTER_SET: {
            ensure_span_block(lycon, span);
            char** target = prop_id == CSS_PROPERTY_COUNTER_RESET
                ? &span->block_mut()->counter_reset
                : prop_id == CSS_PROPERTY_COUNTER_INCREMENT
                    ? &span->block_mut()->counter_increment
                    : &span->block_mut()->counter_set;
            const char* name = prop_id == CSS_PROPERTY_COUNTER_RESET ? "counter-reset"
                : prop_id == CSS_PROPERTY_COUNTER_INCREMENT ? "counter-increment" : "counter-set";
            resolve_counter_property(lycon, value, target, name,
                                     prop_id == CSS_PROPERTY_COUNTER_RESET);
            break;
        }

        case CSS_PROPERTY_CONTENT: {
            // CSS 2.1 Section 12.2: content property for ::before and ::after

            if (!span->pseudo) {
                span->pseudo = (PseudoContentProp*)alloc_prop(lycon, sizeof(PseudoContentProp));
                memset(span->pseudo, 0, sizeof(PseudoContentProp));
            }

            // Determine if this is ::before or ::after based on decl context
            // For now, we'll check the selector context (TODO: improve this)
            bool is_before = false;  // Will be determined by selector parsing
            bool is_after = false;

            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE ||
                    value->data.keyword == CSS_VALUE_NORMAL) {
                    // No content generated
                    if (is_before) {
                        span->pseudo->before_content_type = CONTENT_TYPE_NONE;
                    } else if (is_after) {
                        span->pseudo->after_content_type = CONTENT_TYPE_NONE;
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_STRING) {
                // String literal content
                const char* str = value->data.string;

                if (str) {
                    // Allocate and store content string
                    size_t len = strlen(str);
                    char* content_copy = (char*)alloc_prop(lycon, len + 1);
                    str_copy(content_copy, len + 1, str, len);

                    if (is_before) {
                        span->pseudo->before_content = content_copy;
                        span->pseudo->before_content_type = CONTENT_TYPE_STRING;
                    } else if (is_after) {
                        span->pseudo->after_content = content_copy;
                        span->pseudo->after_content_type = CONTENT_TYPE_STRING;
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                // Handle counter(), counters(), attr(), url()
                CssFunction* func = value->data.function;
                if (func && func->name) {

                    if (strcmp(func->name, "counter") == 0) {
                        // counter(name) or counter(name, style)
                        span->pseudo->before_content_type = is_before ? CONTENT_TYPE_COUNTER : span->pseudo->before_content_type;
                        span->pseudo->after_content_type = is_after ? CONTENT_TYPE_COUNTER : span->pseudo->after_content_type;
                        // TODO: Parse counter name and style
                    } else if (strcmp(func->name, "counters") == 0) {
                        // counters(name, separator) or counters(name, separator, style)
                        span->pseudo->before_content_type = is_before ? CONTENT_TYPE_COUNTERS : span->pseudo->before_content_type;
                        span->pseudo->after_content_type = is_after ? CONTENT_TYPE_COUNTERS : span->pseudo->after_content_type;
                        // TODO: Parse counter name, separator, and style
                    } else if (strcmp(func->name, "attr") == 0) {
                        // attr(attribute-name)
                        span->pseudo->before_content_type = is_before ? CONTENT_TYPE_ATTR : span->pseudo->before_content_type;
                        span->pseudo->after_content_type = is_after ? CONTENT_TYPE_ATTR : span->pseudo->after_content_type;
                        // TODO: Parse attribute name
                    } else if (strcmp(func->name, "url") == 0) {
                        // url(image)
                        span->pseudo->before_content_type = is_before ? CONTENT_TYPE_URI : span->pseudo->before_content_type;
                        span->pseudo->after_content_type = is_after ? CONTENT_TYPE_URI : span->pseudo->after_content_type;
                        // TODO: Parse URL
                    }
                }
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                // Multiple content values (concatenated)
                // TODO: Handle concatenated content values
            }
            break;
        }

       case CSS_PROPERTY_BACKGROUND: {
            // background shorthand can set background-color, background-image, etc.

            // Resolve var() before routing through background shorthand logic
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name && strcmp(value->data.function->name, "var") == 0) {
                const CssValue* resolved = resolve_var_function(lycon, value);
                if (resolved && resolved != value) {
                    lam::CssTempDecl resolved_decl(decl, CSS_PROPERTY_BACKGROUND, const_cast<CssValue*>(resolved));
                    resolved_decl.resolve(lycon);
                    return;
                }
                // var() didn't resolve — fall through (will be logged as unimplemented)
            }

            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                ensure_span_background(lycon, span);
                BackgroundProp* parent_bg = parent_computed_background(lycon);
                if (parent_bg) {
                    *span->bound->background = *parent_bg;
                } else {
                    memset(span->boundary()->background, 0, sizeof(BackgroundProp));
                }
                return;
            }

            // Handle 'background: none' → transparent background (CSS spec: background-image: none)
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                (value->data.keyword == CSS_VALUE_NONE || value->data.keyword == CSS_VALUE_TRANSPARENT)) {
                ensure_span_background(lycon, span);
                span->boundary_mut()->background->color.r = 0;
                span->boundary_mut()->background->color.g = 0;
                span->boundary_mut()->background->color.b = 0;
                span->boundary_mut()->background->color.a = 0;  // fully transparent
                return;
            }

            bool has_top_level_comma = decl && decl->value_text &&
                css_text_has_top_level_comma(decl->value_text, decl->value_text_len);

            // Handle multiple background layers (comma-separated list)
            // CSS stacks backgrounds bottom-to-top, so last item is base layer
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 1 && has_top_level_comma) {
                CssValue** layers = value->data.list.values;
                int count = value->data.list.count;

                // Per CSS Backgrounds, background-color is allowed only in the
                // final background layer. A comma-separated list of plain colors
                // such as "background: red, white" is invalid as a whole and
                // must not paint the last color over children.
                for (int i = 0; i < count - 1; i++) {
                    if (css_background_layer_has_plain_color(layers[i])) {
                        return;
                    }
                }

                // Ensure background prop exists
                ensure_span_background(lycon, span);
                BackgroundProp* bg = span->boundary()->background;

                // First, look for a solid color in the last layer (base background)
                CssValue* last_layer = layers[count - 1];
                if (last_layer) {
                    if (last_layer->type == CSS_VALUE_TYPE_COLOR ||
                        last_layer->type == CSS_VALUE_TYPE_KEYWORD ||
                        (last_layer->type == CSS_VALUE_TYPE_FUNCTION && last_layer->data.function &&
                         last_layer->data.function->name &&
                         (str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgb") ||
                          str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgba")))) {
                        // Set base background color
                        bg->color = resolve_color_value(lycon, last_layer);
                    }
                }

                // Count gradient layers by type
                int radial_count = 0;
                int linear_count = 0;
                for (int i = 0; i < count - 1; i++) {  // exclude last layer (base color)
                    CssValue* layer = layers[i];
                    if (css_find_background_radial_gradient_layer(layer)) radial_count++;
                    else if (css_find_background_linear_gradient_layer(layer)) linear_count++;
                }

                // Also check if the last layer is a gradient (not a solid color)
                if (css_find_background_linear_gradient_layer(last_layer)) linear_count++;
                else if (css_find_background_radial_gradient_layer(last_layer)) radial_count++;

                // Allocate arrays for gradient layers
                if (radial_count > 0) {
                    bg->radial_layers = (RadialGradient**)alloc_prop(lycon, sizeof(RadialGradient*) * radial_count);
                    bg->radial_layer_count = 0;
                }
                if (linear_count > 0) {
                    bg->linear_layers = (LinearGradient**)alloc_prop(lycon, sizeof(LinearGradient*) * linear_count);
                    bg->linear_layer_count = 0;
                }

                // Process all gradient layers (from bottom to top visually, i.e., last-to-first in CSS)
                for (int i = count - 1; i >= 0; i--) {
                    CssValue* layer = layers[i];
                    const CssValue* radial_layer = css_find_background_radial_gradient_layer(layer);
                    const CssValue* linear_layer = css_find_background_linear_gradient_layer(layer);
                    const CssValue* conic_layer = css_find_background_conic_gradient_layer(layer);
                    const CssValue* url_layer = css_find_background_url_layer(layer);

                    if (radial_layer) {
                        lam::CssTempDecl gradient_decl(decl, CSS_PROPERTY_BACKGROUND, (CssValue*)radial_layer);
                        gradient_decl.resolve(lycon);

                        if (bg->radial_gradient && bg->radial_layer_count < radial_count) {
                            bg->radial_layers[bg->radial_layer_count++] = bg->radial_gradient;
                            bg->radial_gradient = nullptr;
                        }
                    } else if (linear_layer) {
                        lam::CssTempDecl gradient_decl(decl, CSS_PROPERTY_BACKGROUND, (CssValue*)linear_layer);
                        gradient_decl.resolve(lycon);

                        if (bg->linear_gradient && bg->linear_layer_count < linear_count) {
                            bg->linear_layers[bg->linear_layer_count++] = bg->linear_gradient;
                            bg->linear_gradient = nullptr;
                            bg->gradient_type = GRADIENT_NONE;
                        }
                    } else if (conic_layer) {
                        if (!bg->conic_gradient) {
                            lam::CssTempDecl gradient_decl(decl, CSS_PROPERTY_BACKGROUND, (CssValue*)conic_layer);
                            gradient_decl.resolve(lycon);
                        }
                    } else if (url_layer) {
                        // url() image layer — route to background-image handler.
                        // Currently we only retain the topmost url() (single image slot).
                        if (!bg->image) {
                            lam::CssTempDecl img_decl(decl, CSS_PROPERTY_BACKGROUND_IMAGE, (CssValue*)url_layer);
                            img_decl.resolve(lycon);
                        }
                    }
                }

                return;
            }

            // Space-separated background shorthand components (e.g.
            // background: 50% / 100% 100% no-repeat; or background: url(...);).
            // This is not a color layer list. Route only recognized components and
            // leave other components for their longhand rules/defaults.
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;

                    if (is_border_radius_slash(item) && i + 1 < value->data.list.count) {
                        CssValue* size_values[2] = {};
                        int size_count = 0;
                        int j = i + 1;
                        while (j < value->data.list.count && size_count < 2) {
                            CssValue* size_item = value->data.list.values[j];
                            if (!size_item || is_border_radius_slash(size_item)) break;
                            if (size_item->type == CSS_VALUE_TYPE_LENGTH ||
                                size_item->type == CSS_VALUE_TYPE_PERCENTAGE ||
                                (size_item->type == CSS_VALUE_TYPE_KEYWORD &&
                                 (size_item->data.keyword == CSS_VALUE_AUTO ||
                                  size_item->data.keyword == CSS_VALUE_COVER ||
                                  size_item->data.keyword == CSS_VALUE_CONTAIN))) {
                                size_values[size_count++] = size_item;
                                j++;
                                continue;
                            }
                            break;
                        }
                        if (size_count > 0) {
                            lam::CssTempListDecl<2> size_decl(decl, CSS_PROPERTY_BACKGROUND_SIZE);
                            for (int k = 0; k < size_count; k++) size_decl.append(size_values[k]);
                            size_decl.resolve(lycon);
                            i = j - 1;
                        }
                        continue;
                    }

                    if (css_value_is_background_position_candidate(item)) {
                        lam::CssTempListDecl<2> position_decl(decl, CSS_PROPERTY_BACKGROUND_POSITION);
                        position_decl.append(item);
                        if (i + 1 < value->data.list.count) {
                            CssValue* next_item = value->data.list.values[i + 1];
                            if (css_value_is_background_position_candidate(next_item)) {
                                position_decl.append(next_item);
                                i++;
                            }
                        }
                        position_decl.resolve(lycon);
                        continue;
                    }

                    if (item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function && item->data.function->name) {
                        const char* func_name = item->data.function->name;
                        size_t func_len = strlen(func_name);
                        if (str_ieq_const(func_name, func_len, "url")) {
                            resolve_background_url_function(lycon, decl, item);
                        } else if (str_ieq_const(func_name, func_len, "linear-gradient") ||
                                   str_ieq_const(func_name, func_len, "repeating-linear-gradient") ||
                                   str_ieq_const(func_name, func_len, "radial-gradient") ||
                                   str_ieq_const(func_name, func_len, "repeating-radial-gradient") ||
                                   str_ieq_const(func_name, func_len, "conic-gradient")) {
                            lam::CssTempDecl gradient_decl(decl, CSS_PROPERTY_BACKGROUND, item);
                            gradient_decl.resolve(lycon);
                        } else if (css_value_is_background_color_candidate(item)) {
                            lam::CssTempDecl color_decl(decl, CSS_PROPERTY_BACKGROUND_COLOR, item);
                            color_decl.resolve(lycon);
                        }
                    } else if (css_value_is_background_color_candidate(item)) {
                        lam::CssTempDecl color_decl(decl, CSS_PROPERTY_BACKGROUND_COLOR, item);
                        color_decl.resolve(lycon);
                    } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
                        CssEnum keyword = item->data.keyword;
                        if (keyword == CSS_VALUE_REPEAT || keyword == CSS_VALUE_NO_REPEAT ||
                            keyword == CSS_VALUE_ROUND || keyword == CSS_VALUE_SPACE) {
                            lam::CssTempDecl repeat_decl(decl, CSS_PROPERTY_BACKGROUND_REPEAT, item);
                            repeat_decl.resolve(lycon);
                        } else if (keyword == CSS_VALUE_COVER || keyword == CSS_VALUE_CONTAIN) {
                            lam::CssTempDecl size_decl(decl, CSS_PROPERTY_BACKGROUND_SIZE, item);
                            size_decl.resolve(lycon);
                        }
                    }
                }
                return;
            }

            // simple case: single color value (e.g., "background: green;")
            if (css_value_is_background_color_candidate(value)) {
                lam::CssTempDecl color_decl(decl, CSS_PROPERTY_BACKGROUND_COLOR, value);
                color_decl.resolve(lycon);
                return;
            }
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name &&
                str_ieq_const(value->data.function->name, strlen(value->data.function->name), "url")) {
                resolve_background_url_function(lycon, decl, value);
                return;
            }
            // Handle color functions like rgb(), rgba() as background color
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
                const char* func_name = value->data.function->name;
                if (str_ieq_const(func_name, strlen(func_name), "rgb") || str_ieq_const(func_name, strlen(func_name), "rgba") ||
                    str_ieq_const(func_name, strlen(func_name), "hsl") || str_ieq_const(func_name, strlen(func_name), "hsla")) {
                    // Color function - treat as background-color
                    ensure_span_background(lycon, span);
                    span->boundary_mut()->background->color = resolve_color_value(lycon, value);
                    return;
                }
            }
            // Handle gradient functions (linear-gradient, radial-gradient, etc.)
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
                const char* func_name = value->data.function->name;
                if (strcmp(func_name, "linear-gradient") == 0 ||
                    strcmp(func_name, "repeating-linear-gradient") == 0) {
                    LinearGradient* gradient = nullptr;
                    if (resolve_linear_gradient_value(lycon, value, &gradient)) {
                        ensure_span_background(lycon, span);
                        span->boundary_mut()->background->gradient_type = GRADIENT_LINEAR;
                        span->boundary_mut()->background->linear_gradient = gradient;
                    }
                    return;
                }
                // Handle radial-gradient
                else if (strcmp(func_name, "radial-gradient") == 0 ||
                         strcmp(func_name, "repeating-radial-gradient") == 0) {
                    // Parse radial-gradient(shape size at position, color-stops...)
                    ensure_span_background(lycon, span);
                    span->boundary_mut()->background->gradient_type = GRADIENT_RADIAL;

                    // Allocate RadialGradient
                    RadialGradient* rg = (RadialGradient*)alloc_prop(lycon, sizeof(RadialGradient));
                    span->boundary_mut()->background->radial_gradient = rg;

                    // Defaults
                    rg->shape = RADIAL_SHAPE_ELLIPSE;
                    rg->size = RADIAL_SIZE_FARTHEST_CORNER;
                    rg->cx = 0.5f;
                    rg->cy = 0.5f;
                    rg->cx_set = false;
                    rg->cy_set = false;

                    CssFunction* func = value->data.function;
                    int arg_idx = 0;

                    // Parse shape/size/position from first argument
                    // Format can be: "circle", "circle at top", "circle at top left", etc.
                    if (func->arg_count > 0 && func->args[0]) {
                        CssValue* first_arg = func->args[0];

                        // Check for keyword indicating shape/position
                        if (first_arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            CssEnum kw = first_arg->data.keyword;
                            const CssEnumInfo* info = css_enum_info(kw);
                            const char* kw_name = info ? info->name : nullptr;
                            if (kw_name) {
                                if (strcmp(kw_name, "circle") == 0) {
                                    rg->shape = RADIAL_SHAPE_CIRCLE;
                                    arg_idx = 1;
                                } else if (strcmp(kw_name, "ellipse") == 0) {
                                    rg->shape = RADIAL_SHAPE_ELLIPSE;
                                    arg_idx = 1;
                                }
                            }
                        }
                        // Check for list containing shape and position keywords
                        else if (first_arg->type == CSS_VALUE_TYPE_LIST) {
                            CssValue** items = first_arg->data.list.values;
                            int count = first_arg->data.list.count;
                            int at_idx = -1;

                            for (int i = 0; i < count; i++) {
                                if (!items[i]) continue;

                                // Get keyword name from keyword or custom type
                                const char* kw_name = nullptr;
                                if (items[i]->type == CSS_VALUE_TYPE_KEYWORD) {
                                    const CssEnumInfo* kw_info = css_enum_info(items[i]->data.keyword);
                                    kw_name = kw_info ? kw_info->name : nullptr;
                                } else if (items[i]->type == CSS_VALUE_TYPE_CUSTOM) {
                                    kw_name = items[i]->data.custom_property.name;
                                }

                                if (kw_name) {

                                    if (strcmp(kw_name, "circle") == 0) {
                                        rg->shape = RADIAL_SHAPE_CIRCLE;
                                    } else if (strcmp(kw_name, "ellipse") == 0) {
                                        rg->shape = RADIAL_SHAPE_ELLIPSE;
                                    } else if (strcmp(kw_name, "at") == 0) {
                                        at_idx = i;
                                    } else if (at_idx >= 0) {
                                        // Position keyword after "at"
                                        if (strcmp(kw_name, "top") == 0) {
                                            rg->cy = 0.0f; rg->cy_set = true;
                                        } else if (strcmp(kw_name, "bottom") == 0) {
                                            rg->cy = 1.0f; rg->cy_set = true;
                                        } else if (strcmp(kw_name, "left") == 0) {
                                            rg->cx = 0.0f; rg->cx_set = true;
                                        } else if (strcmp(kw_name, "right") == 0) {
                                            rg->cx = 1.0f; rg->cx_set = true;
                                        } else if (strcmp(kw_name, "center") == 0) {
                                            // center is default, do nothing special
                                        }
                                    }
                                }
                            }
                            arg_idx = 1;
                        }
                    }

                    // Count color stops
                    int color_count = func->arg_count - arg_idx;
                    rg->stop_count = color_count > 0 ? color_count : 2;
                    rg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * rg->stop_count);

                    // Parse color stops (same logic as linear gradient)
                    int stop_idx = 0;
                    for (int i = arg_idx; i < func->arg_count && stop_idx < rg->stop_count; i++) {
                        CssValue* arg = func->args[i];
                        if (!arg) continue;

                        if (arg->type == CSS_VALUE_TYPE_COLOR) {
                            rg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_FUNCTION) {
                            rg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            // Color keyword like "transparent"
                            Color c = resolve_color_value(lycon, arg);
                            rg->stops[stop_idx].color = c;
                            rg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                            CssValue** items = arg->data.list.values;
                            Color c = resolve_color_value(lycon, items[0]);
                            rg->stops[stop_idx].color = c;
                            rg->stops[stop_idx].position = -1;

                            if (arg->data.list.count >= 2 && items[1]) {
                                CssValue* pos_val = items[1];
                                if (pos_val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                    rg->stops[stop_idx].position = pos_val->data.percentage.value / 100.0f;
                                } else if (pos_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    rg->stops[stop_idx].position = pos_val->data.number.value / 100.0f;
                                }
                            }
                            stop_idx++;
                        }
                    }
                    rg->stop_count = stop_idx;

                    // Auto-distribute positions
                    css_distribute_missing_gradient_positions(
                        rg->stops, rg->stop_count);

                    // CSS Color Level 4: fix transparent stops (same as linear gradient)
                    for (int i = 0; i < rg->stop_count; i++) {
                        GradientStop* s = &rg->stops[i];
                        if (s->color.a == 0 && s->color.r == 0 && s->color.g == 0 && s->color.b == 0) {
                            GradientStop* neighbor = nullptr;
                            if (i > 0 && rg->stops[i - 1].color.a > 0) {
                                neighbor = &rg->stops[i - 1];
                            } else if (i + 1 < rg->stop_count && rg->stops[i + 1].color.a > 0) {
                                neighbor = &rg->stops[i + 1];
                            }
                            if (neighbor) {
                                s->color.r = neighbor->color.r;
                                s->color.g = neighbor->color.g;
                                s->color.b = neighbor->color.b;
                            }
                        }
                    }

                    return;
                }
                // Handle conic-gradient
                else if (strcmp(func_name, "conic-gradient") == 0 ||
                         strcmp(func_name, "repeating-conic-gradient") == 0) {
                    // Parse conic-gradient(from angle at position, color-stops...)
                    ensure_span_background(lycon, span);
                    span->boundary_mut()->background->gradient_type = GRADIENT_CONIC;

                    // Allocate ConicGradient
                    ConicGradient* cg = (ConicGradient*)alloc_prop(lycon, sizeof(ConicGradient));
                    span->boundary_mut()->background->conic_gradient = cg;

                    // Defaults
                    cg->from_angle = 0.0f;
                    cg->cx = 0.5f;
                    cg->cy = 0.5f;
                    cg->cx_set = false;
                    cg->cy_set = false;

                    CssFunction* func = value->data.function;
                    int arg_idx = 0;

                    // Parse "from Xdeg" from first argument
                    if (func->arg_count > 0 && func->args[0]) {
                        CssValue* first_arg = func->args[0];

                        if (first_arg->type == CSS_VALUE_TYPE_LIST) {
                            CssValue** items = first_arg->data.list.values;
                            int count = first_arg->data.list.count;

                            for (int i = 0; i < count; i++) {
                                if (!items[i]) continue;

                                // Check for "from" keyword (may be keyword or custom type with name)
                                bool is_from_keyword = false;
                                if (items[i]->type == CSS_VALUE_TYPE_KEYWORD) {
                                    const CssEnumInfo* kw_info = css_enum_info(items[i]->data.keyword);
                                    const char* kw_name = kw_info ? kw_info->name : nullptr;
                                    is_from_keyword = (kw_name && strcmp(kw_name, "from") == 0);
                                } else if (items[i]->type == CSS_VALUE_TYPE_CUSTOM) {
                                    const char* custom_name = items[i]->data.custom_property.name;
                                    is_from_keyword = (custom_name && strcmp(custom_name, "from") == 0);
                                }

                                if (is_from_keyword) {
                                    // Next item should be angle
                                    if (i + 1 < count && items[i + 1]) {
                                        CssValue* angle_val = items[i + 1];
                                        if (angle_val->type == CSS_VALUE_TYPE_ANGLE) {
                                            cg->from_angle = angle_val->data.length.value;
                                        } else if (angle_val->type == CSS_VALUE_TYPE_NUMBER) {
                                            cg->from_angle = angle_val->data.number.value;
                                        } else if (angle_val->type == CSS_VALUE_TYPE_LENGTH) {
                                            cg->from_angle = angle_val->data.length.value;
                                        }
                                        i++; // Skip the angle value
                                    }
                                } else if (items[i]->type == CSS_VALUE_TYPE_ANGLE) {
                                    cg->from_angle = items[i]->data.length.value;
                                } else if (items[i]->type == CSS_VALUE_TYPE_LENGTH) {
                                    // Angle stored as length with deg unit
                                    cg->from_angle = items[i]->data.length.value;
                                }
                            }
                            arg_idx = 1;
                        } else if (first_arg->type == CSS_VALUE_TYPE_ANGLE) {
                            cg->from_angle = first_arg->data.length.value;
                            arg_idx = 1;
                        }
                    }

                    // Count and parse color stops
                    int color_count = func->arg_count - arg_idx;
                    cg->stop_count = color_count > 0 ? color_count : 2;
                    cg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * cg->stop_count);

                    int stop_idx = 0;
                    for (int i = arg_idx; i < func->arg_count && stop_idx < cg->stop_count; i++) {
                        CssValue* arg = func->args[i];
                        if (!arg) continue;


                        if (arg->type == CSS_VALUE_TYPE_COLOR ||
                            arg->type == CSS_VALUE_TYPE_FUNCTION ||
                            arg->type == CSS_VALUE_TYPE_KEYWORD) {
                            cg->stops[stop_idx].color = resolve_color_value(lycon, arg);
                            cg->stops[stop_idx].position = -1;
                            stop_idx++;
                        } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                            CssValue** items = arg->data.list.values;
                            Color c = resolve_color_value(lycon, items[0]);
                            cg->stops[stop_idx].color = c;
                            cg->stops[stop_idx].position = -1;

                            if (arg->data.list.count >= 2 && items[1]) {
                                CssValue* pos_val = items[1];
                                if (pos_val->type == CSS_VALUE_TYPE_PERCENTAGE) {
                                    cg->stops[stop_idx].position = pos_val->data.percentage.value / 100.0f;
                                } else if (pos_val->type == CSS_VALUE_TYPE_NUMBER) {
                                    cg->stops[stop_idx].position = pos_val->data.number.value / 100.0f;
                                }
                            }
                            stop_idx++;
                        }
                    }
                    cg->stop_count = stop_idx;

                    // Auto-distribute positions (for conic, positions are angles 0-1 mapping to 0-360deg)
                    css_distribute_missing_gradient_positions(
                        cg->stops, cg->stop_count);

                    return;
                }
            }
            return;
        }

        // grid-gap is the legacy name for gap (CSS Grid Level 1)
        case CSS_PROPERTY_GRID_GAP:
        case CSS_PROPERTY_GAP: {
            // gap shorthand: 1-2 values (row-gap column-gap)
            // If only one value is specified, it's used for both row and column gap

            if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER ||
                value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                // single value - use for both row-gap and column-gap
                lam::CssTempDecl row_gap_decl(decl, CSS_PROPERTY_ROW_GAP, decl->value);
                row_gap_decl.resolve(lycon);
                lam::CssTempDecl col_gap_decl(decl, CSS_PROPERTY_COLUMN_GAP, decl->value);
                col_gap_decl.resolve(lycon);
                return;
            } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count == 2) {
                // two values: row-gap column-gap
                CssValue** values = value->data.list.values;

                lam::CssTempDecl row_gap_decl(decl, CSS_PROPERTY_ROW_GAP, values[0]);
                row_gap_decl.resolve(lycon);

                lam::CssTempDecl col_gap_decl(decl, CSS_PROPERTY_COLUMN_GAP, values[1]);
                col_gap_decl.resolve(lycon);
                return;
            }
            return;
        }

        case CSS_PROPERTY_CONTAIN: {
            if (!block || !value) break;
            ensure_span_block(lycon, block);
            bool contains_size = css_contain_value_has_size(value);
            bool contains_inline_size = css_contain_value_has_inline_size(value);
            block->blk->contain_size = contains_size;
            block->blk->contain_inline_size = contains_inline_size;
            break;
        }

        case CSS_PROPERTY_CONTENT_VISIBILITY: {
            if (!block || !value) break;
            ensure_span_block(lycon, block);
            block->blk->content_visibility_hidden =
                css_content_visibility_value_is_hidden(value);
            // CSS Containment 2: `hidden` applies size containment while its
            // contents are skipped; `visible` and `auto` keep normal layout.
            break;
        }

        case CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH:
        case CSS_PROPERTY_CONTAIN_INTRINSIC_HEIGHT:
            resolve_contain_intrinsic_axis(lycon, block, prop_id, value,
                prop_id == CSS_PROPERTY_CONTAIN_INTRINSIC_WIDTH);
            break;

        case CSS_PROPERTY_CONTAIN_INTRINSIC_INLINE_SIZE:
        case CSS_PROPERTY_CONTAIN_INTRINSIC_BLOCK_SIZE:
            resolve_contain_intrinsic_logical_axis(
                lycon, block, prop_id, value,
                prop_id == CSS_PROPERTY_CONTAIN_INTRINSIC_INLINE_SIZE);
            break;

        case CSS_PROPERTY_CONTAIN_INTRINSIC_SIZE: {
            if (!block || !value) break;
            float first = -1.0f;
            float second = -1.0f;
            bool auto_first = false;
            bool auto_second = false;
            resolve_contain_intrinsic_size_value(lycon, value, &first, &second,
                                                 &auto_first, &auto_second);
            if (first >= 0.0f || second >= 0.0f) {
                // A `none <length>` shorthand overrides only the block axis;
                // do not discard that valid one-axis intrinsic-size fallback.
                ensure_span_block(lycon, block);
                block->blk->contain_intrinsic_width = first;
                block->blk->contain_intrinsic_height = second;
                block->blk->contain_intrinsic_width_auto = auto_first;
                block->blk->contain_intrinsic_height_auto = auto_second;
            }
            break;
        }

        case CSS_PROPERTY_OBJECT_FIT: {
            if (!block) break;
            if (!block->embed) {
                block->ensure_embed(lycon);
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_FILL || val == CSS_VALUE_CONTAIN ||
                    val == CSS_VALUE_COVER || val == CSS_VALUE_NONE ||
                    val == CSS_VALUE_SCALE_DOWN) {
                    block->embed->object_fit = val;
                }
            }
            break;
        }

        case CSS_PROPERTY_OBJECT_POSITION: {
            if (!block) break;
            if (!block->embed) {
                block->ensure_embed(lycon);
            }

            float x = 50.0f, y = 50.0f;
            bool x_is_percent = true, y_is_percent = true;
            bool parsed = false;

            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                float values[2] = {50.0f, 50.0f};
                bool is_percent[2] = {true, true};
                int axes[2] = {0, 0};
                int count = 0;
                for (int i = 0; i < value->data.list.count && count < 2; i++) {
                    CssValue* item = value->data.list.values ? value->data.list.values[i] : nullptr;
                    if (parse_object_position_component(lycon, item,
                            &values[count], &is_percent[count], &axes[count])) {
                        count++;
                    }
                }
                if (count == 1) {
                    if (axes[0] == 2) {
                        y = values[0]; y_is_percent = is_percent[0];
                    } else {
                        x = values[0]; x_is_percent = is_percent[0];
                    }
                    parsed = true;
                } else if (count >= 2) {
                    if (axes[0] == 2 && axes[1] != 2) {
                        y = values[0]; y_is_percent = is_percent[0];
                        x = values[1]; x_is_percent = is_percent[1];
                    } else {
                        x = values[0]; x_is_percent = is_percent[0];
                        y = values[1]; y_is_percent = is_percent[1];
                    }
                    parsed = true;
                }
            } else {
                int axis = 0;
                float component = 50.0f;
                bool is_percent = true;
                if (parse_object_position_component(lycon, value, &component, &is_percent, &axis)) {
                    if (axis == 2) {
                        y = component; y_is_percent = is_percent;
                    } else {
                        x = component; x_is_percent = is_percent;
                    }
                    parsed = true;
                }
            }

            if (parsed) {
                block->embed->object_position_x = x;
                block->embed->object_position_y = y;
                block->embed->object_position_x_is_percent = x_is_percent;
                block->embed->object_position_y_is_percent = y_is_percent;
                block->embed->object_position_set = true;
            }
            break;
        }

        // ===== Outline Properties =====
        case CSS_PROPERTY_OUTLINE_STYLE:
        case CSS_PROPERTY_OUTLINE_WIDTH:
        case CSS_PROPERTY_OUTLINE_COLOR:
        case CSS_PROPERTY_OUTLINE_OFFSET:
            resolve_outline_longhand(lycon, span, prop_id, value);
            break;
        case CSS_PROPERTY_OUTLINE: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                if (span->bound && span->boundary_mut()->outline) {
                    span->boundary_mut()->outline->style = CSS_VALUE_NONE;
                    span->boundary_mut()->outline->width = 0;
                }
                break;
            }
            ensure_span_outline(lycon, span);
            if (value->type == CSS_VALUE_TYPE_LIST) {
                for (int i = 0; i < value->data.list.count; i++) {
                    const CssValue* v = value->data.list.values[i];
                    resolve_css_line_decoration_component(
                        lycon, prop_id, v, &span->boundary_mut()->outline->width,
                        &span->boundary_mut()->outline->style, &span->boundary_mut()->outline->color);
                }
            } else if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_NUMBER) {
                span->boundary_mut()->outline->width = resolve_length_value(lycon, prop_id, value);
                span->boundary_mut()->outline->style = CSS_VALUE_SOLID;
            }
            break;
        }

        default:
            // Unknown or unimplemented property
            break;
    }
}
