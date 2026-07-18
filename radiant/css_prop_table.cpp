#include "view.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lib/log.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" bool js_dom_force_layout_for_geometry(void* dom_doc);

static bool copy_text(char* out, size_t out_size, const char* text) {
    if (!out || out_size == 0) return false;
    const char* value = text ? text : "";
    size_t len = strlen(value);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, value, len);
    out[len] = '\0';
    return true;
}

static bool format_number(char* out, size_t out_size, double value, const char* unit) {
    if (!out || out_size == 0) return false;
    snprintf(out, out_size, "%.6g%s", value, unit ? unit : "");
    return true;
}

static bool format_color(char* out, size_t out_size, Color color) {
    if (color.a == 255) {
        snprintf(out, out_size, "rgb(%u, %u, %u)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b);
    } else {
        snprintf(out, out_size, "rgba(%u, %u, %u, %.3g)",
                 (unsigned)color.r, (unsigned)color.g, (unsigned)color.b,
                 color.a / 255.0);
    }
    return true;
}

static Color rgba_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

static const void* prop_group_base(const DomElement* element, PropGroupKind group) {
    if (!element) return nullptr;
    switch (group) {
        case PROP_GROUP_BLOCK: return element->block();
        case PROP_GROUP_BOUNDARY: return element->boundary();
        case PROP_GROUP_FONT: return element->fontp();
        case PROP_GROUP_INLINE: return element->inl();
        case PROP_GROUP_SCROLL: return element->scroll();
        case PROP_GROUP_POSITION: return element->positionp();
        case PROP_GROUP_FLEX_ITEM:
            return element->flex_item() ? element->flex_item() : &FLEX_ITEM_PROP_DEFAULT;
        case PROP_GROUP_GRID_ITEM:
            return element->grid_item() ? element->grid_item() : &GRID_ITEM_PROP_DEFAULT;
        case PROP_GROUP_NONE:
        default: return nullptr;
    }
}

static const char* property_initial(CssPropertyId id) {
    const CssProperty* property = css_property_get_by_id(id);
    return property && property->initial_value ? property->initial_value : "";
}

static bool serialize_direct(const CssPropAccessor* accessor, DomElement* element,
                             int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const uint8_t* base = (const uint8_t*)prop_group_base(element, accessor->group_kind);
    if (!base) return false;
    const void* field = base + accessor->offset;
    switch (accessor->value_kind) {
        case CSS_PROP_VALUE_ENUM: {
            CssEnum value = *(const CssEnum*)field;
            const CssEnumInfo* info = css_enum_info(value);
            return copy_text(out, out_size,
                info && info->name ? info->name : property_initial(accessor->id));
        }
        case CSS_PROP_VALUE_PX:
            return format_number(out, out_size, *(const float*)field, "px");
        case CSS_PROP_VALUE_NUMBER:
            return format_number(out, out_size, *(const float*)field, "");
        case CSS_PROP_VALUE_INTEGER:
            snprintf(out, out_size, "%d", *(const int*)field);
            return true;
        case CSS_PROP_VALUE_COLOR:
            return format_color(out, out_size, *(const Color*)field);
        case CSS_PROP_VALUE_STRING: {
            const char* value = *(char* const*)field;
            return copy_text(out, out_size, value ? value : property_initial(accessor->id));
        }
        case CSS_PROP_VALUE_SPECIAL:
        default:
            return false;
    }
}

static CssDeclaration* computed_decl(DomElement* element, CssPropertyId id, int pseudo_type) {
    if (!element) return nullptr;
    if (pseudo_type == 1 || pseudo_type == 2) {
        return dom_element_get_pseudo_element_value(element, id, pseudo_type);
    }
    CssDeclaration* declaration = dom_element_get_specified_value(element, id);
    if (!declaration && (id == CSS_PROPERTY_OVERFLOW_X || id == CSS_PROPERTY_OVERFLOW_Y)) {
        declaration = dom_element_get_specified_value(element, CSS_PROPERTY_OVERFLOW);
    }
    return declaration;
}

static bool format_decl_color(DomElement* element, const CssValue* value,
                              char* out, size_t out_size) {
    if (!value) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        uint8_t r, g, b, a;
        if (css_named_color_to_rgba(value->data.keyword, &r, &g, &b, &a)) {
            return format_color(out, out_size, rgba_color(r, g, b, a));
        }
        if (value->data.keyword == CSS_VALUE_CURRENTCOLOR && element) {
            return format_color(out, out_size, element->inl()->color);
        }
    }
    if (value->type != CSS_VALUE_TYPE_COLOR) return false;
    switch (value->data.color.type) {
        case CSS_COLOR_HEX:
        case CSS_COLOR_RGB:
            return format_color(out, out_size, rgba_color(
                value->data.color.data.rgba.r, value->data.color.data.rgba.g,
                value->data.color.data.rgba.b, value->data.color.data.rgba.a));
        case CSS_COLOR_TRANSPARENT:
            return format_color(out, out_size, rgba_color(0, 0, 0, 0));
        case CSS_COLOR_CURRENT:
        case CSS_COLOR_CURRENTCOLOR:
            return element
                ? format_color(out, out_size, element->inl()->color)
                : false;
        default:
            return false;
    }
}

static bool format_css_value(DomElement* element, CssPropertyId id,
                             const CssValue* value, char* out, size_t out_size) {
    if (!value) return copy_text(out, out_size, property_initial(id));
    if (id == CSS_PROPERTY_COLOR || id == CSS_PROPERTY_BACKGROUND_COLOR ||
        id == CSS_PROPERTY_BORDER_TOP_COLOR || id == CSS_PROPERTY_BORDER_RIGHT_COLOR ||
        id == CSS_PROPERTY_BORDER_BOTTOM_COLOR || id == CSS_PROPERTY_BORDER_LEFT_COLOR) {
        // Computed-style serialization has no live LayoutContext; use the
        // element's already-resolved currentColor instead of calling the
        // cascade resolver with an invalid context.
        if (format_decl_color(element, value, out, out_size)) return true;
    }
    Pool* pool = element && element->doc ? element->doc->document_pool : nullptr;
    if (!pool) return false;
    CssFormatter* formatter = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!formatter) return false;
    css_format_value(formatter, (CssValue*)value);
    String* result = stringbuf_to_string(formatter->output);
    return copy_text(out, out_size, result ? result->chars : "");
}

static DomElement* parent_element(DomElement* element) {
    return element && element->parent && element->parent->is_element()
        ? static_cast<DomElement*>(element->parent) : nullptr;
}

static bool serialize_decl_recursive(DomElement* element, CssPropertyId id,
                                     int pseudo_type, int depth,
                                     char* out, size_t out_size) {
    if (!element || depth > 16) return false;
    CssDeclaration* declaration = computed_decl(element, id, pseudo_type);
    const CssValue* value = declaration ? declaration->value : nullptr;
    if (value && value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT ||
            (keyword == CSS_VALUE_UNSET && css_property_is_inherited(id))) {
            DomElement* parent = parent_element(element);
            return parent ? serialize_decl_recursive(parent, id, 0, depth + 1, out, out_size)
                          : copy_text(out, out_size, property_initial(id));
        }
        if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT ||
            keyword == CSS_VALUE_UNSET) {
            return copy_text(out, out_size, property_initial(id));
        }
    }
    if (!value && css_property_is_inherited(id)) {
        DomElement* parent = parent_element(element);
        if (parent) return serialize_decl_recursive(parent, id, 0, depth + 1, out, out_size);
    }
    if (id == CSS_PROPERTY_CONTENT && pseudo_type != 0 &&
        (!value || (value->type == CSS_VALUE_TYPE_KEYWORD &&
                    value->data.keyword == CSS_VALUE_NORMAL))) {
        return copy_text(out, out_size, "none");
    }
    return value ? format_css_value(element, id, value, out, out_size)
                 : copy_text(out, out_size, property_initial(id));
}

static bool serialize_decl(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, char* out, size_t out_size) {
    return accessor && serialize_decl_recursive(element, accessor->id, pseudo_type, 0,
                                                out, out_size);
}

static bool serialize_display(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    if (element->display.outer == CSS_VALUE_NONE) return copy_text(out, out_size, "none");
    if (element->display.inner == CSS_VALUE_FLEX) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-flex" : "flex");
    }
    if (element->display.inner == CSS_VALUE_GRID) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-grid" : "grid");
    }
    if (element->display.inner == CSS_VALUE_TABLE) {
        return copy_text(out, out_size,
            element->display.outer == CSS_VALUE_INLINE ? "inline-table" : "table");
    }
    const CssEnumInfo* info = css_enum_info(element->display.outer);
    return copy_text(out, out_size, info && info->name ? info->name : "block");
}

static bool serialize_visibility(const CssPropAccessor*, DomElement* element,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* in_line = element->inl();
    Visibility visibility = in_line
        ? (Visibility)in_line->visibility : VIS_VISIBLE;
    // Visibility is a compact render enum, not a CssEnum; indexing the CSS
    // keyword table with it serialized VIS_HIDDEN as the unrelated "_length".
    switch (visibility) {
        case VIS_HIDDEN: return copy_text(out, out_size, "hidden");
        case VIS_COLLAPSE: return copy_text(out, out_size, "collapse");
        case VIS_VISIBLE:
        default: return copy_text(out, out_size, "visible");
    }
}

static bool serialize_used_size(const CssPropAccessor* accessor, DomElement* element,
                                int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    float value = accessor->id == CSS_PROPERTY_WIDTH ? element->width : element->height;
    if (!element->doc || !element->doc->view_tree || !element->doc->view_tree->root) {
        const BlockProp* block = element->block();
        float specified = accessor->id == CSS_PROPERTY_WIDTH
            ? block->given_width : block->given_height;
        // Load-time scripts run after cascade but before the first ViewTree;
        // the resolved CSS size is the only valid computed value at that seam.
        if (specified >= 0.0f) value = specified;
    }
    return format_number(out, out_size, value, "px");
}

static bool serialize_edge(const CssPropAccessor* accessor, DomElement* element,
                           int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    float value = 0.0f;
    CssEnum type = CSS_VALUE__UNDEF;
    switch (accessor->id) {
        case CSS_PROPERTY_MARGIN_TOP: value = boundary->margin.top; type = boundary->margin.top_type; break;
        case CSS_PROPERTY_MARGIN_RIGHT: value = boundary->margin.right; type = boundary->margin.right_type; break;
        case CSS_PROPERTY_MARGIN_BOTTOM: value = boundary->margin.bottom; type = boundary->margin.bottom_type; break;
        case CSS_PROPERTY_MARGIN_LEFT: value = boundary->margin.left; type = boundary->margin.left_type; break;
        case CSS_PROPERTY_PADDING_TOP: value = boundary->padding.top; break;
        case CSS_PROPERTY_PADDING_RIGHT: value = boundary->padding.right; break;
        case CSS_PROPERTY_PADDING_BOTTOM: value = boundary->padding.bottom; break;
        case CSS_PROPERTY_PADDING_LEFT: value = boundary->padding.left; break;
        default: return false;
    }
    if (type == CSS_VALUE_AUTO) return copy_text(out, out_size, "auto");
    return format_number(out, out_size, value, "px");
}

static bool serialize_inset(const CssPropAccessor* accessor, DomElement* element,
                            int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const PositionProp* position = element->positionp();
    bool present = false;
    float value = 0.0f;
    switch (accessor->id) {
        case CSS_PROPERTY_TOP: present = position->has_top; value = position->top; break;
        case CSS_PROPERTY_RIGHT: present = position->has_right; value = position->right; break;
        case CSS_PROPERTY_BOTTOM: present = position->has_bottom; value = position->bottom; break;
        case CSS_PROPERTY_LEFT: present = position->has_left; value = position->left; break;
        default: return false;
    }
    return present ? format_number(out, out_size, value, "px")
                   : copy_text(out, out_size, "auto");
}

static bool serialize_font_weight(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                  char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const FontProp* font = element->fontp();
    if (font->font_weight_numeric > 0) {
        snprintf(out, out_size, "%d", (int)font->font_weight_numeric);
        return true;
    }
    const CssEnumInfo* info = css_enum_info(font->font_weight);
    return copy_text(out, out_size, info && info->name ? info->name : "normal");
}

static bool serialize_color_prop(const CssPropAccessor*, DomElement* element, int pseudo_type,
                                 char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const InlineProp* inl = element->inl();
    Color color = inl->color;
    if (!inl->has_color) { color.r = color.g = color.b = 0; color.a = 255; }
    return format_color(out, out_size, color);
}

static bool serialize_background_color(const CssPropAccessor*, DomElement* element,
                                       int pseudo_type, char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const BoundaryProp* boundary = element->boundary();
    if (boundary && boundary->background) {
        return format_color(out, out_size, boundary->background->color);
    }

    CssDeclaration* longhand = computed_decl(
        element, CSS_PROPERTY_BACKGROUND_COLOR, pseudo_type);
    CssDeclaration* shorthand = computed_decl(
        element, CSS_PROPERTY_BACKGROUND, pseudo_type);
    // A shorthand and its longhand occupy separate style-tree nodes; compare
    // them before layout so computed style cannot expose the losing longhand.
    if (shorthand && (!longhand ||
        css_declaration_cascade_compare(shorthand, longhand) > 0)) {
        if (format_decl_color(element, shorthand->value, out, out_size)) return true;
    }
    return longhand
        ? format_css_value(element, CSS_PROPERTY_BACKGROUND_COLOR,
                           longhand->value, out, out_size)
        : copy_text(out, out_size, "rgba(0, 0, 0, 0)");
}

static bool serialize_minmax(const CssPropAccessor* accessor, DomElement* element,
                             int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    const BlockProp* block = element->block();
    float value = -1.0f;
    bool maximum = false;
    switch (accessor->id) {
        case CSS_PROPERTY_MIN_WIDTH: value = block->given_min_width; break;
        case CSS_PROPERTY_MIN_HEIGHT: value = block->given_min_height; break;
        case CSS_PROPERTY_MAX_WIDTH: value = block->given_max_width; maximum = true; break;
        case CSS_PROPERTY_MAX_HEIGHT: value = block->given_max_height; maximum = true; break;
        default: return false;
    }
    if (value < 0.0f) return copy_text(out, out_size, maximum ? "none" : "0px");
    return format_number(out, out_size, value, "px");
}

static bool serialize_z_index(const CssPropAccessor*, DomElement* element, int pseudo_type,
                              char* out, size_t out_size) {
    if (!element || pseudo_type != 0) return false;
    const PositionProp* position = element->positionp();
    if (position->position == CSS_VALUE_STATIC && position->z_index == 0) {
        return copy_text(out, out_size, "auto");
    }
    snprintf(out, out_size, "%d", position->z_index);
    return true;
}

static bool serialize_transition(const CssPropAccessor* accessor, DomElement* element,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!accessor || !element || pseudo_type != 0) return false;
    CssTransitionProp transition = {};
    CssPropertyId properties[8];
    CssDeclaration* shorthand = computed_decl(element, CSS_PROPERTY_TRANSITION, 0);
    CssDeclaration* duration = computed_decl(element, CSS_PROPERTY_TRANSITION_DURATION, 0);
    CssDeclaration* delay = computed_decl(element, CSS_PROPERTY_TRANSITION_DELAY, 0);
    CssDeclaration* property = computed_decl(element, CSS_PROPERTY_TRANSITION_PROPERTY, 0);
    CssDeclaration* timing = computed_decl(
        element, CSS_PROPERTY_TRANSITION_TIMING_FUNCTION, 0);
    css_transition_resolve_values(
        shorthand ? shorthand->value : nullptr,
        duration ? duration->value : nullptr,
        delay ? delay->value : nullptr,
        property ? property->value : nullptr,
        timing ? timing->value : nullptr,
        &transition, properties, 8);
    if (accessor->id == CSS_PROPERTY_TRANSITION_DURATION ||
        accessor->id == CSS_PROPERTY_TRANSITION_DELAY) {
        float seconds = accessor->id == CSS_PROPERTY_TRANSITION_DURATION
            ? transition.duration : transition.delay;
        return format_number(out, out_size, seconds, "s");
    }
    if (transition.property_count < 0) return copy_text(out, out_size, "all");
    if (transition.property_count == 0) return copy_text(out, out_size, "none");
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < transition.property_count && used + 1 < out_size; i++) {
        const CssProperty* item = css_property_get_by_id(transition.properties[i]);
        if (!item || !item->name) continue;
        int count = snprintf(out + used, out_size - used, "%s%s",
                             used ? ", " : "", item->name);
        if (count < 0) return false;
        size_t written = (size_t)count;
        if (written >= out_size - used) {
            out[out_size - 1] = '\0';
            break;
        }
        used += written;
    }
    return true;
}

#define DIRECT_ROW(prop_id, group, type, field, kind, row_flags) \
    {prop_id, group, (uint16_t)offsetof(type, field), kind, row_flags, serialize_direct, nullptr}
#define DERIVED_ROW(prop_id, fn, row_flags) \
    {prop_id, PROP_GROUP_NONE, 0, CSS_PROP_VALUE_SPECIAL, row_flags, fn, fn}
#define DECL_ROW(prop_id) DERIVED_ROW(prop_id, serialize_decl, 0)

static const CssPropAccessor CSS_PROP_ROWS[] = {
    DERIVED_ROW(CSS_PROPERTY_DISPLAY, serialize_display, 0),
    DIRECT_ROW(CSS_PROPERTY_POSITION, PROP_GROUP_POSITION, PositionProp, position, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_TOP, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_RIGHT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_BOTTOM, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_LEFT, serialize_inset, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_Z_INDEX, serialize_z_index, 0),
    DIRECT_ROW(CSS_PROPERTY_FLOAT, PROP_GROUP_POSITION, PositionProp, float_prop, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_CLEAR, PROP_GROUP_POSITION, PositionProp, clear, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERFLOW_X, PROP_GROUP_SCROLL, ScrollProp, overflow_x, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_OVERFLOW_Y, PROP_GROUP_SCROLL, ScrollProp, overflow_y, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_VISIBILITY, serialize_visibility, 0),
    DERIVED_ROW(CSS_PROPERTY_WIDTH, serialize_used_size, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_HEIGHT, serialize_used_size, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MIN_WIDTH, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MIN_HEIGHT, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MAX_WIDTH, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MAX_HEIGHT, serialize_minmax, CSS_PROP_ACCESSOR_USED_VALUE),
    DIRECT_ROW(CSS_PROPERTY_BOX_SIZING, PROP_GROUP_BLOCK, BlockProp, box_sizing, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_TOP, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_RIGHT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_BOTTOM, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_MARGIN_LEFT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_TOP, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_RIGHT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_BOTTOM, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DERIVED_ROW(CSS_PROPERTY_PADDING_LEFT, serialize_edge, CSS_PROP_ACCESSOR_USED_VALUE),
    DIRECT_ROW(CSS_PROPERTY_FONT_FAMILY, PROP_GROUP_FONT, FontProp, family, CSS_PROP_VALUE_STRING, 0),
    DIRECT_ROW(CSS_PROPERTY_FONT_SIZE, PROP_GROUP_FONT, FontProp, font_size, CSS_PROP_VALUE_PX, 0),
    DERIVED_ROW(CSS_PROPERTY_FONT_WEIGHT, serialize_font_weight, 0),
    DIRECT_ROW(CSS_PROPERTY_FONT_STYLE, PROP_GROUP_FONT, FontProp, font_style, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_FONT_VARIANT, PROP_GROUP_FONT, FontProp, font_variant, CSS_PROP_VALUE_ENUM, 0),
    DECL_ROW(CSS_PROPERTY_LINE_HEIGHT),
    DIRECT_ROW(CSS_PROPERTY_LETTER_SPACING, PROP_GROUP_FONT, FontProp, letter_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_WORD_SPACING, PROP_GROUP_FONT, FontProp, word_spacing, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_ALIGN, PROP_GROUP_BLOCK, BlockProp, text_align, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_WHITE_SPACE, PROP_GROUP_BLOCK, BlockProp, white_space, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_TEXT_INDENT, PROP_GROUP_BLOCK, BlockProp, text_indent, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_VERTICAL_ALIGN, PROP_GROUP_INLINE, InlineProp, vertical_align, CSS_PROP_VALUE_ENUM, 0),
    DERIVED_ROW(CSS_PROPERTY_COLOR, serialize_color_prop, 0),
    DIRECT_ROW(CSS_PROPERTY_OPACITY, PROP_GROUP_INLINE, InlineProp, opacity, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_CURSOR, PROP_GROUP_INLINE, InlineProp, cursor, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_ALIGN_SELF, PROP_GROUP_FLEX_ITEM, FlexItemProp, align_self, CSS_PROP_VALUE_ENUM, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_GROW, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_grow, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_SHRINK, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_shrink, CSS_PROP_VALUE_NUMBER, 0),
    DIRECT_ROW(CSS_PROPERTY_FLEX_BASIS, PROP_GROUP_FLEX_ITEM, FlexItemProp, flex_basis, CSS_PROP_VALUE_PX, 0),
    DIRECT_ROW(CSS_PROPERTY_ORDER, PROP_GROUP_FLEX_ITEM, FlexItemProp, order, CSS_PROP_VALUE_INTEGER, 0),
    DECL_ROW(CSS_PROPERTY_FLEX_DIRECTION),
    DECL_ROW(CSS_PROPERTY_FLEX_WRAP),
    DECL_ROW(CSS_PROPERTY_JUSTIFY_CONTENT),
    DECL_ROW(CSS_PROPERTY_ALIGN_ITEMS),
    DECL_ROW(CSS_PROPERTY_ALIGN_CONTENT),
    DECL_ROW(CSS_PROPERTY_GAP),
    DECL_ROW(CSS_PROPERTY_ROW_GAP),
    DECL_ROW(CSS_PROPERTY_COLUMN_GAP),
    DERIVED_ROW(CSS_PROPERTY_BACKGROUND_COLOR, serialize_background_color, 0),
    DECL_ROW(CSS_PROPERTY_BORDER_TOP_WIDTH),
    DECL_ROW(CSS_PROPERTY_BORDER_RIGHT_WIDTH),
    DECL_ROW(CSS_PROPERTY_BORDER_BOTTOM_WIDTH),
    DECL_ROW(CSS_PROPERTY_BORDER_LEFT_WIDTH),
    DECL_ROW(CSS_PROPERTY_BORDER_TOP_STYLE),
    DECL_ROW(CSS_PROPERTY_BORDER_RIGHT_STYLE),
    DECL_ROW(CSS_PROPERTY_BORDER_BOTTOM_STYLE),
    DECL_ROW(CSS_PROPERTY_BORDER_LEFT_STYLE),
    DECL_ROW(CSS_PROPERTY_BORDER_TOP_COLOR),
    DECL_ROW(CSS_PROPERTY_BORDER_RIGHT_COLOR),
    DECL_ROW(CSS_PROPERTY_BORDER_BOTTOM_COLOR),
    DECL_ROW(CSS_PROPERTY_BORDER_LEFT_COLOR),
    DECL_ROW(CSS_PROPERTY_TRANSFORM),
    DECL_ROW(CSS_PROPERTY_FILTER),
    DECL_ROW(CSS_PROPERTY_ANIMATION_DURATION),
    DECL_ROW(CSS_PROPERTY_ANIMATION_DELAY),
    DERIVED_ROW(CSS_PROPERTY_TRANSITION_DURATION, serialize_transition, 0),
    DERIVED_ROW(CSS_PROPERTY_TRANSITION_DELAY, serialize_transition, 0),
    DERIVED_ROW(CSS_PROPERTY_TRANSITION_PROPERTY, serialize_transition, 0),
    DECL_ROW(CSS_PROPERTY_TRANSITION_TIMING_FUNCTION),
    DECL_ROW(CSS_PROPERTY_DIRECTION),
    DECL_ROW(CSS_PROPERTY_CONTENT),
};

#undef DECL_ROW
#undef DERIVED_ROW
#undef DIRECT_ROW

struct CssPropIndex {
    const CssPropAccessor* rows[CSS_PROPERTY_COUNT];
    CssPropIndex() : rows{} {
        size_t count = sizeof(CSS_PROP_ROWS) / sizeof(CSS_PROP_ROWS[0]);
        for (size_t i = 0; i < count; i++) {
            CssPropertyId id = CSS_PROP_ROWS[i].id;
            if (id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT) rows[id] = &CSS_PROP_ROWS[i];
        }
    }
};

const CssPropAccessor* css_prop_accessor(CssPropertyId id) {
    static const CssPropIndex index;
    return id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT ? index.rows[id] : nullptr;
}

const CssPropAccessor* css_prop_accessors(size_t* count) {
    if (count) *count = sizeof(CSS_PROP_ROWS) / sizeof(CSS_PROP_ROWS[0]);
    return CSS_PROP_ROWS;
}

bool dom_ensure_computed(DomElement* element, bool needs_used_value) {
    if (!element || !element->doc) return false;
    bool dirty = needs_used_value || element->layout_dirty;
    for (DomNode* node = element; node && !dirty; node = node->parent) {
        if (node->layout_dirty || (node->is_element() &&
            (!node->as_element()->styles_resolved() ||
             node->as_element()->needs_style_recompute()))) dirty = true;
    }
    dirty = dirty || element->doc->js.mutation_count > 0;
    if (!dirty) return true;

    static thread_local bool computed_flush_active = false;
    // A computed-style flush cannot recursively enter layout; doing so would
    // expose partially regenerated tier-2 groups as authoritative style.
    assert(!computed_flush_active);
    if (computed_flush_active) return false;
    computed_flush_active = true;
    bool result = js_dom_force_layout_for_geometry(element->doc);
    computed_flush_active = false;
    if (result) return true;
    // The loader cascades before executing load-time scripts, while no UiContext
    // exists yet to commit layout. Clean resolved groups remain authoritative;
    // mutations and unresolved styles must still fail rather than return stale data.
    return element->styles_resolved() && !element->needs_style_recompute() &&
        element->doc->js.mutation_count == 0;
}

bool css_prop_serialize_computed(DomElement* element, CssPropertyId id,
                                 int pseudo_type, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    const CssPropAccessor* accessor = css_prop_accessor(id);
    if (!accessor) {
        static bool logged[CSS_PROPERTY_COUNT] = {};
        if (id > CSS_PROPERTY_UNKNOWN && id < CSS_PROPERTY_COUNT && !logged[id]) {
            logged[id] = true;
            const CssProperty* property = css_property_get_by_id(id);
            log_debug("computed-style table: unsupported property '%s'",
                      property && property->name ? property->name : "?");
            (void)property;
        }
        return false;
    }
    if (!dom_ensure_computed(element,
            (accessor->flags & CSS_PROP_ACCESSOR_USED_VALUE) != 0)) {
        // Before the first UiContext exists, loader scripts can only observe
        // the already-cascaded declaration tree; keep this compatibility seam
        // inside the table instead of reviving a second JS serializer.
        return serialize_decl(accessor, element, pseudo_type, out, out_size);
    }
    if (pseudo_type != 0) return serialize_decl(accessor, element, pseudo_type, out, out_size);
    return accessor->serialize && accessor->serialize(accessor, element, pseudo_type,
                                                      out, out_size);
}
