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

#define MAX_GRID_SPAN 1000

// Forward declaration for CSS variable lookup
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name);
Color resolve_color_value(LayoutContext* lycon, const CssValue* value);
static bool css_value_is_background_color_candidate(const CssValue* value);
static CssEnum find_inherited_block_keyword(DomElement* element,
                                            CssPropertyCode property,
                                            bool check_specified,
                                            bool reject_match_parent,
                                            CssEnum fallback);
static float resolve_spacing_with_inherit(LayoutContext* lycon,
                                          CssPropertyCode prop_id,
                                          const CssValue* value);
void resolve_spacing_prop(LayoutContext* lycon, uintptr_t property,
                          const CssValue* src_space, int64_t specificity,
                          Spacing* trg_spacing);

static DomElement* dom_parent_element(DomElement* element) {
    if (!element || !element->parent) return nullptr;
    DomElement* parent = lam::dom_require_element(element->parent);
    if (parent && parent->tag_name &&
        strcmp(parent->tag_name, "#document-fragment") == 0 &&
        parent->shadow_host_element()) {
        // CSS Shadow DOM: a shadow fragment inherits the host's computed style;
        // projected light-DOM nodes must not fall back to UA defaults.
        return parent->shadow_host_element();
    }
    return parent;
}

static BackgroundProp* parent_computed_background(LayoutContext* lycon) {
    if (!lycon || !lycon->view || !lycon->view->is_element()) return nullptr;
    DomElement* element = lam::dom_require_element(lycon->view);
    DomElement* parent = dom_parent_element(element);
    return (parent && parent->bound) ? parent->boundary()->background : nullptr;
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

static float resolve_filter_hue_angle(const CssValue* value) {
    if (!value) return 0.0f;
    float degrees = 0.0f;
    if (value->type == CSS_VALUE_TYPE_ANGLE || value->type == CSS_VALUE_TYPE_LENGTH) {
        // Filter angle values are normalized by the CSS parser before this resolver.
        degrees = (float)value->data.length.value;
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        degrees = (float)value->data.number.value;
    }
    return degrees * ((float)M_PI / 180.0f);
}

static FilterFunction* resolve_filter_function(LayoutContext* lycon,
                                               CssPropertyCode prop_id,
                                               CssFunction* func) {
    if (!func || !func->name || func->arg_count == 0) return nullptr;
    FilterFunction* filter = (FilterFunction*)alloc_prop(lycon, sizeof(FilterFunction));
    memset(filter, 0, sizeof(FilterFunction));
    const char* name = func->name;
    const CssValue* arg = func->args[0];
    if (strcmp(name, "blur") == 0) {
        filter->type = FILTER_BLUR;
        filter->params.blur_radius = arg && arg->type == CSS_VALUE_TYPE_LENGTH
            ? resolve_length_value(lycon, prop_id, arg) : 0.0f;
    } else if (const FilterAmountSpec* spec = find_filter_amount_spec(name)) {
        filter->type = spec->type;
        filter->params.amount = resolve_filter_amount(arg, spec->clamp_unit_interval);
    } else if (strcmp(name, "hue-rotate") == 0) {
        filter->type = FILTER_HUE_ROTATE;
        filter->params.angle = resolve_filter_hue_angle(arg);
    } else if (strcmp(name, "drop-shadow") == 0) {
        filter->type = FILTER_DROP_SHADOW;
        filter->params.drop_shadow.color.a = 255;
        // drop-shadow() arguments can be packed in one list by the CSS parser.
        int value_count = func->arg_count;
        CssValue** values = func->args;
        if (value_count == 1 && values[0] && values[0]->type == CSS_VALUE_TYPE_LIST) {
            value_count = values[0]->data.list.count;
            values = values[0]->data.list.values;
        }
        int length_index = 0;
        for (int i = 0; i < value_count; i++) {
            CssValue* value = values[i];
            if (!value) continue;
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float length = resolve_length_value(lycon, prop_id, value);
                if (length_index == 0) filter->params.drop_shadow.offset_x = length;
                else if (length_index == 1) filter->params.drop_shadow.offset_y = length;
                else if (length_index == 2) filter->params.drop_shadow.blur_radius = length;
                length_index++;
            } else if (value->type == CSS_VALUE_TYPE_COLOR) {
                filter->params.drop_shadow.color.r = value->data.color.data.rgba.r;
                filter->params.drop_shadow.color.g = value->data.color.data.rgba.g;
                filter->params.drop_shadow.color.b = value->data.color.data.rgba.b;
                filter->params.drop_shadow.color.a = value->data.color.data.rgba.a;
            } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
                filter->params.drop_shadow.color = resolve_color_value(lycon, value);
            }
        }
    } else {
        return nullptr;
    }
    return filter;
}

static void append_filter_function(FilterFunction** head, FilterFunction** tail,
                                   FilterFunction* function) {
    if (!function) return;
    if (!*head) *head = function;
    else (*tail)->next = function;
    *tail = function;
}

static bool shorthand_overrides_longhand(LayoutContext* lycon,
                                         CssPropertyCode shorthand_id,
                                         const CssDeclaration* longhand) {
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
    radiant_copy_font_values(span->font, parent_font);
    DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    DomElement* parent = current ? dom_parent_element(current) : nullptr;
    span->ensure_block(lycon);
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

typedef struct TransformFunctionSpec {
    const char* name;
    TransformFunctionType type;
} TransformFunctionSpec;

static const TransformFunctionSpec TRANSFORM_FUNCTION_SPECS[] = {
    {"translate", TRANSFORM_TRANSLATE},
    {"translateX", TRANSFORM_TRANSLATEX},
    {"translateY", TRANSFORM_TRANSLATEY},
    {"scale", TRANSFORM_SCALE},
    {"scaleX", TRANSFORM_SCALEX},
    {"scaleY", TRANSFORM_SCALEY},
    {"rotate", TRANSFORM_ROTATE},
    {"skew", TRANSFORM_SKEW},
    {"skewX", TRANSFORM_SKEWX},
    {"skewY", TRANSFORM_SKEWY},
    {"matrix", TRANSFORM_MATRIX},
    {"translate3d", TRANSFORM_TRANSLATE3D},
    {"translateZ", TRANSFORM_TRANSLATEZ},
    {"rotateX", TRANSFORM_ROTATEX},
    {"rotateY", TRANSFORM_ROTATEY},
    {"rotateZ", TRANSFORM_ROTATEZ},
    {"perspective", TRANSFORM_PERSPECTIVE},
};

static TransformFunctionType transform_function_type(const char* name) {
    if (!name) return TRANSFORM_NONE;
    for (const TransformFunctionSpec& spec : TRANSFORM_FUNCTION_SPECS) {
        if (str_ieq_const(name, strlen(name), spec.name)) return spec.type;
    }
    return TRANSFORM_NONE;
}

static float transform_number_value(const CssValue* value, float fallback = 0.0f) {
    return value && value->type == CSS_VALUE_TYPE_NUMBER
        ? (float)value->data.number.value : fallback;
}

static float transform_length_value(LayoutContext* lycon, CssPropertyCode prop_id,
                                    const CssValue* value) {
    return value ? resolve_length_value(lycon, prop_id, value) : 0.0f;
}

static void resolve_transform_translate_arg(LayoutContext* lycon, CssPropertyCode prop_id,
                                            const CssValue* value, float* length,
                                            float* percent) {
    if (!value) return;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        *percent = (float)value->data.percentage.value;
        *length = 0.0f;
    } else {
        *length = transform_length_value(lycon, prop_id, value);
    }
}

static TransformFunction* resolve_transform_function(LayoutContext* lycon,
                                                     CssPropertyCode prop_id,
                                                     const CssValue* func_value) {
    if (!func_value || func_value->type != CSS_VALUE_TYPE_FUNCTION) return nullptr;
    const CssFunction* func = func_value->data.function;
    TransformFunctionType type = transform_function_type(func ? func->name : nullptr);
    if (type == TRANSFORM_NONE) return nullptr;
    TransformFunction* tf = (TransformFunction*)alloc_prop(lycon, sizeof(TransformFunction));
    memset(tf, 0, sizeof(TransformFunction));
    tf->type = type;
    tf->translate_x_percent = NAN;
    tf->translate_y_percent = NAN;
    const CssValue* arg0 = func->arg_count >= 1 ? func->args[0] : nullptr;
    const CssValue* arg1 = func->arg_count >= 2 ? func->args[1] : nullptr;
    const CssValue* arg2 = func->arg_count >= 3 ? func->args[2] : nullptr;
    switch (type) {
        case TRANSFORM_TRANSLATE:
            resolve_transform_translate_arg(lycon, prop_id, arg0,
                                            &tf->params.translate.x,
                                            &tf->translate_x_percent);
            resolve_transform_translate_arg(lycon, prop_id, arg1,
                                            &tf->params.translate.y,
                                            &tf->translate_y_percent);
            break;
        case TRANSFORM_TRANSLATEX:
            resolve_transform_translate_arg(lycon, prop_id, arg0,
                                            &tf->params.translate.x,
                                            &tf->translate_x_percent);
            break;
        case TRANSFORM_TRANSLATEY:
            resolve_transform_translate_arg(lycon, prop_id, arg0,
                                            &tf->params.translate.y,
                                            &tf->translate_y_percent);
            break;
        case TRANSFORM_SCALE:
            tf->params.scale.x = transform_number_value(arg0, 1.0f);
            tf->params.scale.y = arg1 ? transform_number_value(arg1, 1.0f)
                                      : tf->params.scale.x;
            break;
        case TRANSFORM_SCALEX:
            tf->params.scale.x = transform_number_value(arg0, 1.0f);
            tf->params.scale.y = 1.0f;
            break;
        case TRANSFORM_SCALEY:
            tf->params.scale.x = 1.0f;
            tf->params.scale.y = transform_number_value(arg0, 1.0f);
            break;
        case TRANSFORM_ROTATE:
        case TRANSFORM_SKEWX:
        case TRANSFORM_SKEWY:
        case TRANSFORM_ROTATEX:
        case TRANSFORM_ROTATEY:
        case TRANSFORM_ROTATEZ:
            tf->params.angle = resolve_transform_angle(arg0);
            break;
        case TRANSFORM_SKEW:
            tf->params.skew.x = resolve_transform_angle(arg0);
            tf->params.skew.y = resolve_transform_angle(arg1);
            break;
        case TRANSFORM_MATRIX:
            tf->params.matrix.a = 1.0f;
            tf->params.matrix.d = 1.0f;
            if (func->arg_count >= 6 && func->args[0] && func->args[1] &&
                func->args[2] && func->args[3] && func->args[4] && func->args[5]) {
                tf->params.matrix.a = transform_number_value(func->args[0]);
                tf->params.matrix.b = transform_number_value(func->args[1]);
                tf->params.matrix.c = transform_number_value(func->args[2]);
                tf->params.matrix.d = transform_number_value(func->args[3]);
                tf->params.matrix.e = transform_number_value(func->args[4]);
                tf->params.matrix.f = transform_number_value(func->args[5]);
            }
            break;
        case TRANSFORM_TRANSLATE3D:
            tf->params.translate3d.x = transform_length_value(lycon, prop_id, arg0);
            tf->params.translate3d.y = transform_length_value(lycon, prop_id, arg1);
            tf->params.translate3d.z = transform_length_value(lycon, prop_id, arg2);
            break;
        case TRANSFORM_TRANSLATEZ:
            tf->params.translate3d.z = transform_length_value(lycon, prop_id, arg0);
            break;
        case TRANSFORM_PERSPECTIVE:
            tf->params.perspective = transform_length_value(lycon, prop_id, arg0);
            break;
        default:
            break;
    }
    return tf;
}

static void append_transform_function(TransformFunction** head,
                                       TransformFunction** tail,
                                       TransformFunction* function) {
    if (!function) return;
    if (!*head) *head = function;
    else (*tail)->next = function;
    *tail = function;
}

static void resolve_origin_keyword(CssEnum keyword, int index,
                                   float* x, bool* x_percent,
                                   float* y, bool* y_percent) {
    if (!x || !y) return;
    if (keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT) {
        *x = keyword == CSS_VALUE_LEFT ? 0.0f : 100.0f;
        if (x_percent) *x_percent = true;
    } else if (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM) {
        *y = keyword == CSS_VALUE_TOP ? 0.0f : 100.0f;
        if (y_percent) *y_percent = true;
    } else if (keyword == CSS_VALUE_CENTER) {
        if (index == 0) {
            *x = 50.0f;
            if (x_percent) *x_percent = true;
        } else {
            *y = 50.0f;
            if (y_percent) *y_percent = true;
        }
    }
}

static void resolve_origin_list(LayoutContext* lycon, CssPropertyCode property,
                                const CssValue* value, bool allow_number,
                                bool include_z, float* x, bool* x_percent,
                                float* y, bool* y_percent, float* z) {
    if (!lycon || !value || value->type != CSS_VALUE_TYPE_LIST || !x || !y) return;
    int limit = include_z ? 3 : 2;
    int count = value->data.list.count < limit ? value->data.list.count : limit;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->data.list.values[i];
        if (!item) continue;
        if (item->type == CSS_VALUE_TYPE_PERCENTAGE) {
            float percent = (float)item->data.percentage.value;
            if (i == 0) {
                *x = percent;
                if (x_percent) *x_percent = true;
            } else if (i == 1) {
                *y = percent;
                if (y_percent) *y_percent = true;
            }
        } else if (item->type == CSS_VALUE_TYPE_LENGTH ||
                   (allow_number && item->type == CSS_VALUE_TYPE_NUMBER)) {
            float length = item->type == CSS_VALUE_TYPE_NUMBER
                ? (float)item->data.number.value
                : resolve_length_value(lycon, property, item);
            if (i == 0) {
                *x = length;
                if (x_percent) *x_percent = false;
            } else if (i == 1) {
                *y = length;
                if (y_percent) *y_percent = false;
            } else if (include_z && z) {
                *z = length;
            }
        } else if (item->type == CSS_VALUE_TYPE_KEYWORD) {
            resolve_origin_keyword(item->data.keyword, i, x, x_percent, y, y_percent);
        }
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

struct CssBackgroundComponent {
    float value;
    bool is_percent;
    bool is_auto;
};

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

static void resolve_background_position_axis(LayoutContext* lycon,
                                             CssPropertyCode property,
                                             const CssValue* value,
                                             BackgroundProp* background,
                                             bool horizontal) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        bool valid = keyword == CSS_VALUE_CENTER ||
            (horizontal && (keyword == CSS_VALUE_LEFT || keyword == CSS_VALUE_RIGHT)) ||
            (!horizontal && (keyword == CSS_VALUE_TOP || keyword == CSS_VALUE_BOTTOM));
        if (!valid) return;
    }
    float initial = horizontal ? background->bg_position_x : background->bg_position_y;
    bool initial_percent = horizontal ? background->bg_position_x_is_percent
                                      : background->bg_position_y_is_percent;
    CssBackgroundComponent result = resolve_background_position_component(
        lycon, property, value, initial, initial_percent, horizontal);
    if (value->type != CSS_VALUE_TYPE_LENGTH && value->type != CSS_VALUE_TYPE_PERCENTAGE &&
        value->type != CSS_VALUE_TYPE_KEYWORD) return;
    if (horizontal) {
        background->bg_position_x = result.value;
        background->bg_position_x_is_percent = result.is_percent;
    } else {
        background->bg_position_y = result.value;
        background->bg_position_y_is_percent = result.is_percent;
    }
    background->bg_position_set = true;
}

static BorderProp* parent_border_prop(LayoutContext* lycon) {
    DomElement* current = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view);
    if (!current || !current->parent || !current->parent->is_element()) return nullptr;
    DomElement* parent = lam::dom_require<DOM_NODE_ELEMENT>(current->parent);
    return (parent->bound && parent->boundary_mut()->border) ? parent->boundary_mut()->border : nullptr;
}

enum CssBorderSidePart : uint8_t {
    CSS_BORDER_SIDE_WIDTH,
    CSS_BORDER_SIDE_STYLE,
    CSS_BORDER_SIDE_COLOR,
};

static void resolve_border_side_part(LayoutContext* lycon, ViewSpan* span,
                                     CssPropertyCode property, const CssValue* value,
                                     int64_t specificity, CssBorderSidePart part) {
    CssBoxSide side = radiant_css_box_side(property);
    BorderProp* border = layout_ensure_border(lycon, span);
    RadiantBorderSide refs = radiant_border_side(border, side);
    if (part == CSS_BORDER_SIDE_WIDTH) {
        if (specificity < *refs.width_specificity) return;
        if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
            BorderProp* parent = parent_border_prop(lycon);
            if (parent) {
                *refs.width = *radiant_border_side(parent, side).width;
                *refs.width_specificity = specificity;
            }
        } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
            *refs.width = resolve_length_value(lycon, radiant_border_width_property(side), value);
            *refs.width_specificity = specificity;
        } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
            if (value->data.number.value == 0.0f) {
                *refs.width = 0.0f;
                *refs.width_specificity = specificity;
            }
        } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            *refs.width = layout_css_border_width_keyword(value->data.keyword);
            *refs.width_specificity = specificity;
        }
    } else if (part == CSS_BORDER_SIDE_STYLE) {
        if (value->type != CSS_VALUE_TYPE_KEYWORD) return;
        *refs.style = value->data.keyword;
        if (*refs.style == CSS_VALUE_NONE || *refs.style == CSS_VALUE_HIDDEN) {
            *refs.width = 0.0f;
            *refs.width_specificity = specificity;
        }
    } else if (specificity >= *refs.color_specificity) {
        *refs.color = resolve_color_value(lycon, value);
        *refs.color_specificity = specificity;
    }
}

static void resolve_border_physical_longhand(LayoutContext* lycon, ViewSpan* span,
                                             CssPropertyCode property,
                                             const CssValue* value, int64_t specificity) {
    CssBorderSidePart part = property >= CSS_PROPERTY_BORDER_TOP_WIDTH &&
            property <= CSS_PROPERTY_BORDER_LEFT_WIDTH
        ? CSS_BORDER_SIDE_WIDTH
        : property >= CSS_PROPERTY_BORDER_TOP_STYLE &&
            property <= CSS_PROPERTY_BORDER_LEFT_STYLE
            ? CSS_BORDER_SIDE_STYLE : CSS_BORDER_SIDE_COLOR;
    resolve_border_side_part(lycon, span, property, value, specificity, part);
}

static CssEnum css_value_axis_type(const CssValue* value) {
    if (!value) return CSS_VALUE__UNDEF;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) return value->data.keyword;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) return CSS_VALUE__PERCENTAGE;
    return CSS_VALUE__UNDEF;
}

static void resolve_border_box_part(LayoutContext* lycon, ViewSpan* span,
                                    const CssValue* value, int64_t specificity,
                                    CssBorderSidePart part) {
    if (!lycon || !span || !value) return;
    CssQuadValues values;
    if (!values.expand(value)) return;
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        resolve_border_side_part(lycon, span,
            radiant_inset_property((CssBoxSide)side), values.side[side], specificity, part);
    }
}

static void set_spacing_side(Spacing* spacing, Margin* margin, CssBoxSide side,
                             float value, CssEnum type, int64_t specificity) {
    int64_t* side_spec = radiant_spacing_specificity(spacing, side);
    if (specificity < *side_spec) return;
    *radiant_spacing_value(spacing, side) = value;
    *side_spec = specificity;
    if (margin) *radiant_margin_type(margin, side) = type;
}

static void resolve_spacing_sides(LayoutContext* lycon, ViewSpan* span, CssBoxSide first_side,
                                  CssBoxSide second_side, CssPropertyCode prop_id,
                                  const CssValue* value, int64_t specificity, bool is_margin) {
    BoundaryProp* bound = span->ensure_boundary(lycon);
    float resolved = resolve_spacing_with_inherit(lycon, prop_id, value);
    Spacing* spacing = is_margin ? &bound->margin : &bound->padding;
    Margin* margin = is_margin ? &bound->margin : nullptr;
    CssEnum type = is_margin ? css_value_axis_type(value) : CSS_VALUE__UNDEF;
    set_spacing_side(spacing, margin, first_side, resolved, type, specificity);
    if (second_side != first_side) {
        set_spacing_side(spacing, margin, second_side, resolved, type, specificity);
    }
}

static void resolve_logical_spacing_property(LayoutContext* lycon, ViewSpan* span,
                                             CssPropertyCode property,
                                             const CssValue* value, int64_t specificity,
                                             bool is_margin, bool inline_axis_is_vertical,
                                             bool vertical_block_start_is_right,
                                             bool inline_direction_rtl) {
    LayoutLogicalProperty logical = layout_logical_property(property);
    if (!logical.valid) return;
    LayoutLogicalSides sides = layout_logical_sides(
        inline_axis_is_vertical, vertical_block_start_is_right, inline_direction_rtl);
    LayoutPhysicalSides physical = layout_logical_physical_sides(logical, sides);
    resolve_spacing_sides(lycon, span, physical.first,
                          physical.pair ? physical.second : physical.first,
                          property, value, specificity, is_margin);
}

static bool resolve_spacing_property(LayoutContext* lycon, ViewSpan* span,
                                     CssPropertyCode property, const CssValue* value,
                                     int64_t specificity, bool inline_axis_is_vertical,
                                     bool vertical_block_start_is_right,
                                     bool inline_direction_rtl) {
    if (property == CSS_PROPERTY_MARGIN || property == CSS_PROPERTY_PADDING) {
        bool is_margin = property == CSS_PROPERTY_MARGIN;
        span->ensure_boundary(lycon);
        resolve_spacing_prop(lycon, property, value, specificity,
            is_margin ? static_cast<Spacing*>(&span->boundary_mut()->margin)
                      : &span->boundary_mut()->padding);
        return true;
    }

    bool is_margin_side = property >= CSS_PROPERTY_MARGIN_TOP &&
        property <= CSS_PROPERTY_MARGIN_LEFT;
    bool is_padding_side = property >= CSS_PROPERTY_PADDING_TOP &&
        property <= CSS_PROPERTY_PADDING_LEFT;
    if (is_margin_side || is_padding_side) {
        CssBoxSide side = radiant_css_box_side(property);
        resolve_spacing_sides(lycon, span, side, side, property, value,
                              specificity, is_margin_side);
        return true;
    }

    if ((property >= CSS_PROPERTY_MARGIN_BLOCK &&
         property <= CSS_PROPERTY_MARGIN_INLINE_END) ||
        (property >= CSS_PROPERTY_PADDING_BLOCK &&
         property <= CSS_PROPERTY_PADDING_INLINE_END)) {
        bool is_margin = property <= CSS_PROPERTY_MARGIN_INLINE_END;
        resolve_logical_spacing_property(lycon, span, property, value, specificity,
                                         is_margin, inline_axis_is_vertical,
                                         vertical_block_start_is_right,
                                         inline_direction_rtl);
        return true;
    }
    return false;
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
        resolved.value = 0.0f;
        resolved.has_value = false;
    }
    return resolved;
}

static void resolve_inset_sides(LayoutContext* lycon, ViewSpan* span, CssBoxSide first_side,
                                CssBoxSide second_side, CssPropertyCode prop_id,
                                const CssValue* value, bool inherit_from_parent) {
    PositionProp* position = ensure_span_position(lycon, span);
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (first_side == second_side && value->data.keyword == CSS_VALUE_INHERIT &&
            inherit_from_parent) {
            inherit_inset_side(lycon, position, first_side);
            return;
        }
        if (value->data.keyword != CSS_VALUE_INHERIT) {
            set_inset_side_auto(position, first_side);
            if (second_side != first_side) set_inset_side_auto(position, second_side);
            return;
        }
    }
    ResolvedInsetValue resolved = resolve_inset_value(lycon, prop_id, value);
    set_inset_side_value(position, first_side, resolved.value,
                         resolved.percent, resolved.has_value);
    if (second_side != first_side) {
        set_inset_side_value(position, second_side, resolved.value,
                             resolved.percent, resolved.has_value);
    }
}

static void resolve_inset_shorthand(LayoutContext* lycon, ViewSpan* span, const CssValue* value) {
    PositionProp* position = ensure_span_position(lycon, span);
    CssQuadValues values;
    if (!values.expand(value)) return;
    for (int i = 0; i < 4; i++) {
        CssBoxSide side = (CssBoxSide)i;
        if (values.side[i]->type == CSS_VALUE_TYPE_KEYWORD) {
            set_inset_side_auto(position, side);
        } else {
            resolve_inset_sides(lycon, span, side, side, CSS_PROPERTY_INSET,
                                values.side[i], false);
        }
    }
}

static bool resolve_logical_inset_property(LayoutContext* lycon, ViewSpan* span,
                                           CssPropertyCode property, const CssValue* value,
                                           bool inline_axis_is_vertical,
                                           bool vertical_block_start_is_right,
                                           bool inline_direction_rtl) {
    LayoutLogicalProperty logical = layout_logical_property(property);
    if (!logical.valid) return false;
    LayoutLogicalSides sides = layout_logical_sides(
        inline_axis_is_vertical, vertical_block_start_is_right, inline_direction_rtl);
    LayoutPhysicalSides physical_sides = layout_logical_physical_sides(
        logical, sides, true);
    CssBoxSide first = physical_sides.first;
    CssPropertyCode physical = radiant_inset_property(first);
    // Logical insets are resolved to physical storage only after writing mode
    resolve_inset_sides(lycon, span, first,
                        physical_sides.pair ? physical_sides.second : first,
                        physical, value, false);
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
    // item-prop union, so attached names must share the pool lifetime.
    *target = duplicate_view_pool_layout_string(lycon, value);
}

static void replace_view_pool_layout_const_string(LayoutContext* lycon, const char** target, const char* value) {
    if (!target) return;
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
};

static CssGridAxisSlots css_grid_axis_slots(GridItemProp* item, bool is_row) {
    if (is_row) {
        return {&item->grid_row_start, &item->grid_row_end,
                &item->grid_row_start_name, &item->grid_row_end_name,
                &item->has_explicit_grid_row_start, &item->has_explicit_grid_row_end,
                &item->grid_row_start_is_span, &item->grid_row_end_is_span};
    }
    return {&item->grid_column_start, &item->grid_column_end,
            &item->grid_column_start_name, &item->grid_column_end_name,
            &item->has_explicit_grid_column_start, &item->has_explicit_grid_column_end,
            &item->grid_column_start_is_span, &item->grid_column_end_is_span};
}

static const char* css_grid_identifier(const CssValue* value) {
    if (!value) return nullptr;
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

static bool css_grid_span_value(const CssValue* value, int* span_value) {
    if (!value || !span_value) return false;
    if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
        value->data.function->name &&
        strcmp(value->data.function->name, "span") == 0 &&
        value->data.function->arg_count > 0 && value->data.function->args[0] &&
        value->data.function->args[0]->type == CSS_VALUE_TYPE_NUMBER) {
        *span_value = (int)value->data.function->args[0]->data.number.value;
        return true;
    }
    if (value->type != CSS_VALUE_TYPE_LIST) return false;
    bool saw_span = false;
    int parsed = 1;
    for (int i = 0; i < value->data.list.count; i++) {
        CssValue* part = value->data.list.values[i];
        if (css_grid_is_separator(part)) return false;
        const char* identifier = css_grid_identifier(part);
        if (identifier && strcmp(identifier, "span") == 0) {
            saw_span = true;
        } else if (part && part->type == CSS_VALUE_TYPE_NUMBER) {
            parsed = (int)part->data.number.value; // INT_CAST_OK: grid span is discrete.
        }
    }
    if (!saw_span) return false;
    *span_value = parsed;
    return true;
}

static bool css_grid_line_value(const CssValue* value, int* line,
                                bool* has_explicit, bool* is_span) {
    if (!line || !has_explicit || !is_span || !value) return false;
    *line = 0;
    *has_explicit = false;
    *is_span = false;
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        *line = (int)value->data.number.value; // INT_CAST_OK: grid line is discrete.
        *has_explicit = true;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_AUTO) {
        return true;
    }
    int span_value = 0;
    if (!css_grid_span_value(value, &span_value)) return false;
    if (span_value > MAX_GRID_SPAN) span_value = MAX_GRID_SPAN;
    *line = -span_value;
    *has_explicit = true;
    *is_span = true;
    return true;
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
        int span_value = 1;
        int line_value = 0;
        bool is_span = css_grid_span_value(value, &span_value);
        if (!is_span) {
            for (size_t i = 0; i < count; i++) {
                CssValue* part = values[i];
                if (part && part->type == CSS_VALUE_TYPE_NUMBER) {
                    line_value = (int)part->data.number.value; // INT_CAST_OK: grid line is discrete.
                }
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
    if (css_grid_line_value(value, line, has_line, line_is_span)) {
        if (!*has_line) return;
        item->is_grid_auto_placed = false;
    } else {
        const char* name = css_grid_named_line(value);
        if (name) {
            replace_view_pool_layout_const_string(lycon, line_name, name);
            *has_line = true;
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

static void css_store_content_alignment(LayoutContext* lycon, ViewBlock* block,
                                        bool justify, CssSelfAlignment alignment) {
    if (!block) return;
    if (!justify) {
        BlockProp* block_prop = block->ensure_block(lycon);
        block_prop->align_content = alignment.value;
        block_prop->align_content_detail = alignment;
        if (block->display.inner == CSS_VALUE_FLEX) {
            alloc_flex_prop(lycon, block);
            block->embedp()->flex->align_content = alignment.value;
        }
        if (block->display.inner == CSS_VALUE_GRID) {
            alloc_grid_prop(lycon, block);
            block->embedp()->grid->align_content = alignment.value;
        }
        return;
    }
    alloc_flex_prop(lycon, block);
    alloc_grid_prop(lycon, block);
    block->embedp()->flex->justify = alignment.value;
    block->embedp()->grid->justify_content = alignment.value;
}

static void resolve_flex_grid_container_alignment(LayoutContext* lycon,
                                                   ViewBlock* block,
                                                   CssPropertyCode property,
                                                   const CssValue* value) {
    if (!block) return;
    if (property == CSS_PROPERTY_ALIGN_CONTENT ||
        property == CSS_PROPERTY_JUSTIFY_CONTENT) {
        bool justify = property == CSS_PROPERTY_JUSTIFY_CONTENT;
        CssSelfAlignment alignment = css_parse_content_alignment_value(value, justify);
        if (alignment.value != CSS_VALUE__UNDEF) {
            css_store_content_alignment(lycon, block, justify, alignment);
        }
        return;
    }
    alloc_flex_prop(lycon, block);
    alloc_grid_prop(lycon, block);
    resolve_keyword_slot(value, &block->embedp()->flex->align_items);
    resolve_keyword_slot(value, &block->embedp()->grid->align_items);
}

static void css_store_self_alignment(LayoutContext* lycon, ViewSpan* span,
                                     bool justify, CssSelfAlignment value);

static CssSelfAlignment css_parse_legacy_justify_items(const CssValue* value) {
    CssSelfAlignment result = {};
    if (!value || value->type != CSS_VALUE_TYPE_LIST || value->data.list.count != 2) {
        return result;
    }

    bool has_legacy = false;
    CssEnum position = CSS_VALUE__UNDEF;
    for (int i = 0; i < value->data.list.count; i++) {
        const CssValue* part = value->data.list.values[i];
        if (part && part->type == CSS_VALUE_TYPE_CUSTOM &&
            part->data.custom_property.name &&
            str_ieq_const(part->data.custom_property.name,
                          strlen(part->data.custom_property.name), "legacy")) {
            if (has_legacy) return result;
            has_legacy = true;
            continue;
        }
        if (part && part->type == CSS_VALUE_TYPE_KEYWORD &&
            (part->data.keyword == CSS_VALUE_LEFT ||
             part->data.keyword == CSS_VALUE_RIGHT ||
             part->data.keyword == CSS_VALUE_CENTER)) {
            if (position != CSS_VALUE__UNDEF) return result;
            position = part->data.keyword;
            continue;
        }
        return result;
    }
    if (!has_legacy) return result;
    result.value = position;
    result.legacy = true;
    return result;
}

static CssSelfAlignment css_parse_justify_items(const CssValue* value) {
    CssSelfAlignment result = css_parse_self_alignment_value(value, true);
    if (result.value != CSS_VALUE__UNDEF) return result;

    // CSS Align 3 §7.1: legacy is a separate grammar branch for this property.
    return css_parse_legacy_justify_items(value);
}

static void css_store_grid_justify_items(LayoutContext* lycon, ViewBlock* block,
                                         CssSelfAlignment value) {
    if (!block || value.value == CSS_VALUE__UNDEF) return;
    alloc_grid_prop(lycon, block);
    block->embedp()->grid->justify_items = value.value;
    block->embedp()->grid->justify_items_detail = value;
}

static void resolve_grid_alignment_property(LayoutContext* lycon, ViewBlock* block,
                                            ViewSpan* span, CssPropertyCode property,
                                            const CssValue* value) {
    if (property == CSS_PROPERTY_ALIGN_SELF) {
        CssSelfAlignment self = css_parse_self_alignment_value(value, false);
        if (self.value == CSS_VALUE__UNDEF) return;
        css_store_self_alignment(lycon, span, false, self);
        CssEnum alignment = self.value;
        if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
            span->gi->align_self_grid = alignment;
        } else if (span->parent_item_kind() == DomElement::PARENT_ITEM_FLEX) {
            span->fi->align_self = alignment;
        }
        return;
    }
    bool self = property == CSS_PROPERTY_JUSTIFY_SELF;
    if (!self && !block) {
        return;
    }
    if (self) {
        CssSelfAlignment alignment = css_parse_self_alignment_value(value, true);
        if (alignment.value == CSS_VALUE__UNDEF) return;
        css_store_self_alignment(lycon, span, true, alignment);
        if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
            span->gi->justify_self = alignment.value;
        }
    } else {
        css_store_grid_justify_items(lycon, block, css_parse_justify_items(value));
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

static const char* css_join_font_family_parts(LayoutContext* lycon,
                                              const CssValue* list,
                                              size_t start, size_t end,
                                              bool grouped,
                                              const char* separator,
                                              size_t separator_length,
                                              const char* prefix);

static const char* css_font_family_group_name(LayoutContext* lycon,
                                              const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        return css_join_font_family_parts(
            lycon, value, 0, (size_t)value->data.list.count,
            false, " ", 1, nullptr);
    }
    return css_font_family_name_from_value(value);
}

static const char* css_join_font_family_parts(LayoutContext* lycon,
                                              const CssValue* list,
                                              size_t start, size_t end,
                                              bool grouped,
                                              const char* separator,
                                              size_t separator_length,
    const char* prefix) {
    if (!lycon || !lycon->doc || !lycon->doc->view_tree ||
        !list || list->type != CSS_VALUE_TYPE_LIST) return nullptr;
    size_t count = (size_t)list->data.list.count;
    if (end > count) end = count;
    if (start >= end) return prefix;
    auto part_name = [&](size_t index) -> const char* {
        return grouped ? css_font_family_group_name(
            lycon, list->data.list.values[index]) :
            css_font_family_name_from_value(list->data.list.values[index]);
    };
    if (!prefix && end == start + 1) return part_name(start);
    size_t total_len = prefix ? strlen(prefix) : 0;
    size_t part_count = prefix ? 1 : 0;
    for (size_t i = start; i < end; i++) {
        const char* part = part_name(i);
        if (!part || !*part) continue;
        total_len += strlen(part);
        part_count++;
    }
    if (part_count == 0) return nullptr;
    total_len += (part_count - 1) * separator_length;
    char* combined = (char*)pool_alloc(lycon->doc->view_tree->prop_pool, total_len + 1);
    if (!combined) return nullptr;
    combined[0] = '\0';
    size_t pos = 0;
    bool first = true;
    if (prefix) {
        pos = str_cat(combined, pos, total_len + 1, prefix, strlen(prefix));
        first = false;
    }
    for (size_t i = start; i < end; i++) {
        const char* part = part_name(i);
        if (!part || !*part) continue;
        if (!first) {
            pos = str_cat(combined, pos, total_len + 1,
                         separator, separator_length);
        }
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
    return css_join_font_family_parts(
        lycon, value, 0, (size_t)value->data.list.count,
        true, ", ", 2, nullptr);
}

const char* css_select_font_shorthand_family(LayoutContext* lycon,
                                             const CssValue* shorthand_value,
                                             const CssValue* main_group,
                                             size_t family_start_index,
                                             bool require_loadable_face_source) {
    (void)require_loadable_face_source;
    const char* first = main_group && main_group->type == CSS_VALUE_TYPE_LIST
        ? css_join_font_family_parts(
            lycon, main_group, family_start_index,
            main_group->data.list.count, false, " ", 1, nullptr)
        : NULL;
    // A flat shorthand list also contains size and line-height tokens; only a
    if (shorthand_value == main_group) return first;
    if (!shorthand_value || shorthand_value->type != CSS_VALUE_TYPE_LIST ||
        shorthand_value->data.list.count < 2) return first;
    size_t shorthand_count = (size_t)shorthand_value->data.list.count;
    const char* combined = css_join_font_family_parts(
        lycon, shorthand_value, 1, shorthand_count,
        true, ", ", 2, first);
    return combined ? combined : first;
}

// look up an inherited CSS custom property.
static const CssValue* lookup_css_variable(LayoutContext* lycon, const char* var_name) {
    if (!lycon || !lycon->view || !var_name) return nullptr;
    DomNode* current = lycon->view;
    while (current && !current->is_element()) {
        current = current->parent;
    }
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
        if (element->parent && element->parent->is_element()) {
            element = lam::dom_require<DOM_NODE_ELEMENT>(element->parent);
        } else {
            break;
        }
    }
    return nullptr;
}

static bool css_value_is_slash(const CssValue* value) {
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
        stops[i].position = stop_count > 1
            ? (float)i / (float)(stop_count - 1) : 0.0f;
    }
}

static void css_normalize_gradient_stops(GradientStop* stops, int stop_count,
                                         bool distribute);
static int resolve_gradient_stops(LayoutContext* lycon, CssFunction* func,
                                  int first_stop, GradientStop* stops, int capacity,
                                  bool strict_color, bool allow_second_position,
                                  bool* stops_in_px);

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
            // A named color is also a keyword; only consume the first
            // argument when it is actually a gradient direction.
            if (kw == CSS_VALUE_TOP) { angle = 0.0f; arg_idx = 1; }
            else if (kw == CSS_VALUE_RIGHT) { angle = 90.0f; arg_idx = 1; }
            else if (kw == CSS_VALUE_BOTTOM) { angle = 180.0f; arg_idx = 1; }
            else if (kw == CSS_VALUE_LEFT) { angle = 270.0f; arg_idx = 1; }
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
            if (has_top || has_bottom || has_left || has_right) arg_idx = 1;
        }
    }
    lg->angle = angle;
    int color_count = func->arg_count - arg_idx;
    if (color_count < 2) return false;
    lg->stop_count = color_count * 2;
    lg->stops = (GradientStop*)alloc_prop(lycon, sizeof(GradientStop) * lg->stop_count);
    if (!lg->stops) return false;
    bool stops_in_px = false;
    lg->stop_count = resolve_gradient_stops(
        lycon, func, arg_idx, lg->stops, lg->stop_count, true, true,
        &stops_in_px);
    lg->stops_in_px = stops_in_px;
    if (lg->stop_count < 2) return false;
    css_normalize_gradient_stops(lg->stops, lg->stop_count, !lg->stops_in_px);
    *out_gradient = lg;
    return true;
}

static float css_gradient_stop_position(const CssValue* value, bool* is_px) {
    if (!value) return -1.0f;
    if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        return (float)value->data.percentage.value / 100.0f;
    }
    if (value->type == CSS_VALUE_TYPE_NUMBER) {
        return (float)value->data.number.value / 100.0f;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        if (is_px) *is_px = true;
        return (float)value->data.length.value;
    }
    return -1.0f;
}

static int resolve_gradient_stops(LayoutContext* lycon, CssFunction* func,
                                  int first_stop, GradientStop* stops, int capacity,
                                  bool strict_color, bool allow_second_position,
                                  bool* stops_in_px) {
    if (!lycon || !func || !stops || capacity <= 0) return 0;
    int stop_count = 0;
    for (int i = first_stop; i < func->arg_count && stop_count < capacity; i++) {
        CssValue* arg = func->args ? func->args[i] : nullptr;
        if (!arg) continue;

        CssValue* color = arg;
        CssValue* position = nullptr;
        if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
            color = arg->data.list.values[0];
            if (arg->data.list.count >= 2) position = arg->data.list.values[1];
        } else if (strict_color && !css_value_is_background_color_candidate(arg)) {
            continue;
        }
        if (!color || (strict_color && !css_value_is_background_color_candidate(color))) {
            continue;
        }

        stops[stop_count].color = resolve_color_value(lycon, color);
        stops[stop_count].position = css_gradient_stop_position(position, stops_in_px);
        stop_count++;
        if (allow_second_position && arg->type == CSS_VALUE_TYPE_LIST &&
            arg->data.list.count >= 3 && stop_count < capacity) {
            bool second_is_px = false;
            float second_position = css_gradient_stop_position(
                arg->data.list.values[2], &second_is_px);
            stops[stop_count].color = stops[stop_count - 1].color;
            stops[stop_count].position = second_position;
            if (second_is_px && stops_in_px) *stops_in_px = true;
            stop_count++;
        }
    }
    return stop_count;
}

static void css_normalize_gradient_stops(GradientStop* stops, int stop_count,
                                         bool distribute) {
    if (!stops || stop_count <= 0) return;
    if (distribute) css_distribute_missing_gradient_positions(stops, stop_count);
    for (int i = 0; i < stop_count; i++) {
        GradientStop* stop = &stops[i];
        if (stop->color.a != 0 || stop->color.r != 0 ||
            stop->color.g != 0 || stop->color.b != 0) continue;
        GradientStop* neighbor = nullptr;
        if (i > 0 && stops[i - 1].color.a > 0) neighbor = &stops[i - 1];
        else if (i + 1 < stop_count && stops[i + 1].color.a > 0) neighbor = &stops[i + 1];
        if (neighbor) {
            stop->color.r = neighbor->color.r;
            stop->color.g = neighbor->color.g;
            stop->color.b = neighbor->color.b;
        }
    }
}

static const char* css_gradient_component_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    return value->type == CSS_VALUE_TYPE_CUSTOM
        ? value->data.custom_property.name : nullptr;
}

static bool resolve_radial_gradient_value(LayoutContext* lycon, const CssValue* value,
                                          RadialGradient** out_gradient) {
    if (out_gradient) *out_gradient = nullptr;
    if (!lycon || !value || !out_gradient ||
        value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name) return false;
    CssFunction* func = value->data.function;
    bool repeating = strcmp(func->name, "repeating-radial-gradient") == 0;
    if (strcmp(func->name, "radial-gradient") != 0 && !repeating) return false;

    RadialGradient* gradient = (RadialGradient*)alloc_prop(lycon, sizeof(RadialGradient));
    if (!gradient) return false;
    gradient->shape = RADIAL_SHAPE_ELLIPSE;
    gradient->size = RADIAL_SIZE_FARTHEST_CORNER;
    gradient->cx = 0.5f;
    gradient->cy = 0.5f;
    gradient->cx_set = false;
    gradient->cy_set = false;

    int first_stop = 0;
    CssValue* first = func->arg_count > 0 && func->args ? func->args[0] : nullptr;
    if (first && first->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(first->data.keyword);
        const char* name = info ? info->name : nullptr;
        if (name && (strcmp(name, "circle") == 0 || strcmp(name, "ellipse") == 0)) {
            gradient->shape = strcmp(name, "circle") == 0
                ? RADIAL_SHAPE_CIRCLE : RADIAL_SHAPE_ELLIPSE;
            first_stop = 1;
        }
    } else if (first && first->type == CSS_VALUE_TYPE_LIST) {
        int at_index = -1;
        for (int i = 0; i < first->data.list.count; i++) {
            CssValue* item = first->data.list.values[i];
            if (!item) continue;
            const char* name = css_gradient_component_name(item);
            if (!name) continue;
            if (strcmp(name, "circle") == 0) gradient->shape = RADIAL_SHAPE_CIRCLE;
            else if (strcmp(name, "ellipse") == 0) gradient->shape = RADIAL_SHAPE_ELLIPSE;
            else if (strcmp(name, "at") == 0) at_index = i;
            else if (at_index >= 0) {
                if (strcmp(name, "top") == 0) { gradient->cy = 0.0f; gradient->cy_set = true; }
                else if (strcmp(name, "bottom") == 0) { gradient->cy = 1.0f; gradient->cy_set = true; }
                else if (strcmp(name, "left") == 0) { gradient->cx = 0.0f; gradient->cx_set = true; }
                else if (strcmp(name, "right") == 0) { gradient->cx = 1.0f; gradient->cx_set = true; }
            }
        }
        first_stop = 1;
    }

    int capacity = func->arg_count - first_stop;
    if (capacity < 2) capacity = 2;
    gradient->stops = (GradientStop*)alloc_prop(
        lycon, sizeof(GradientStop) * capacity);
    if (!gradient->stops) return false;
    gradient->stop_count = resolve_gradient_stops(
        lycon, func, first_stop, gradient->stops, capacity, false, false, nullptr);
    css_normalize_gradient_stops(gradient->stops, gradient->stop_count, true);
    *out_gradient = gradient;
    return true;
}

static bool resolve_conic_gradient_value(LayoutContext* lycon, const CssValue* value,
                                         ConicGradient** out_gradient) {
    if (out_gradient) *out_gradient = nullptr;
    if (!lycon || !value || !out_gradient ||
        value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name) return false;
    CssFunction* func = value->data.function;
    bool repeating = strcmp(func->name, "repeating-conic-gradient") == 0;
    if (strcmp(func->name, "conic-gradient") != 0 && !repeating) return false;

    ConicGradient* gradient = (ConicGradient*)alloc_prop(lycon, sizeof(ConicGradient));
    if (!gradient) return false;
    gradient->from_angle = 0.0f;
    gradient->cx = 0.5f;
    gradient->cy = 0.5f;
    gradient->cx_set = false;
    gradient->cy_set = false;

    int first_stop = 0;
    CssValue* first = func->arg_count > 0 && func->args ? func->args[0] : nullptr;
    if (first && first->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < first->data.list.count; i++) {
            CssValue* item = first->data.list.values[i];
            if (!item) continue;
            const char* name = css_gradient_component_name(item);
            if (name && strcmp(name, "from") == 0 && i + 1 < first->data.list.count) {
                CssValue* angle = first->data.list.values[++i];
                if (angle && (angle->type == CSS_VALUE_TYPE_ANGLE ||
                              angle->type == CSS_VALUE_TYPE_LENGTH ||
                              angle->type == CSS_VALUE_TYPE_NUMBER)) {
                    gradient->from_angle = angle->type == CSS_VALUE_TYPE_NUMBER
                        ? (float)angle->data.number.value
                        : (float)angle->data.length.value;
                }
            } else if (item->type == CSS_VALUE_TYPE_ANGLE ||
                       item->type == CSS_VALUE_TYPE_LENGTH) {
                gradient->from_angle = (float)item->data.length.value;
            }
        }
        first_stop = 1;
    } else if (first && first->type == CSS_VALUE_TYPE_ANGLE) {
        gradient->from_angle = (float)first->data.length.value;
        first_stop = 1;
    }

    int capacity = func->arg_count - first_stop;
    if (capacity < 2) capacity = 2;
    gradient->stops = (GradientStop*)alloc_prop(
        lycon, sizeof(GradientStop) * capacity);
    if (!gradient->stops) return false;
    gradient->stop_count = resolve_gradient_stops(
        lycon, func, first_stop, gradient->stops, capacity, false, false, nullptr);
    css_distribute_missing_gradient_positions(gradient->stops, gradient->stop_count);
    *out_gradient = gradient;
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

static bool css_contain_value_establishes_positioning_cb(const CssValue* value) {
    // CSS Containment: layout and paint containment establish absolute/fixed CBs;
    // strict and content expand to those containment modes.
    return css_value_has_identifier(value, "layout") ||
        css_value_has_identifier(value, "paint") ||
        css_value_has_identifier(value, "strict") ||
        css_value_has_identifier(value, "content");
}

static void resolve_contain_intrinsic_axis(LayoutContext* lycon, ViewBlock* block,
                                           CssPropertyCode property,
                                           const CssValue* value, bool horizontal) {
    if (!block || !value) return;
    float length = -1.0f;
    if (!resolve_contain_intrinsic_length(lycon, property, value, &length)) return;
    block->ensure_block(lycon);
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

static const CssValue* css_find_background_gradient_layer(const CssValue* value,
                                                          GradientType type) {
    static const char* const linear_names[] = {
        "linear-gradient", "repeating-linear-gradient"};
    static const char* const radial_names[] = {
        "radial-gradient", "repeating-radial-gradient"};
    static const char* const conic_names[] = {
        "conic-gradient", "repeating-conic-gradient"};
    const char* const* names = type == GRADIENT_LINEAR ? linear_names
        : type == GRADIENT_RADIAL ? radial_names : conic_names;
    return css_find_background_function(value, names, 2);
}

static GradientType css_background_gradient_type(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name) return GRADIENT_NONE;
    const char* name = value->data.function->name;
    if (strcmp(name, "linear-gradient") == 0 ||
        strcmp(name, "repeating-linear-gradient") == 0) return GRADIENT_LINEAR;
    if (strcmp(name, "radial-gradient") == 0 ||
        strcmp(name, "repeating-radial-gradient") == 0) return GRADIENT_RADIAL;
    if (strcmp(name, "conic-gradient") == 0 ||
        strcmp(name, "repeating-conic-gradient") == 0) return GRADIENT_CONIC;
    return GRADIENT_NONE;
}

static bool resolve_background_gradient_value(LayoutContext* lycon, ViewSpan* span,
                                              const CssValue* value) {
    GradientType type = css_background_gradient_type(value);
    if (type == GRADIENT_NONE) return false;

    LinearGradient* linear = nullptr;
    RadialGradient* radial = nullptr;
    ConicGradient* conic = nullptr;
    bool resolved = type == GRADIENT_LINEAR
        ? resolve_linear_gradient_value(lycon, value, &linear)
        : type == GRADIENT_RADIAL
            ? resolve_radial_gradient_value(lycon, value, &radial)
            : resolve_conic_gradient_value(lycon, value, &conic);
    if (!resolved) return true;

    layout_ensure_background(lycon, span);
    BackgroundProp* background = span->boundary_mut()->background;
    background->gradient_type = type;
    if (type == GRADIENT_LINEAR) background->linear_gradient = linear;
    else if (type == GRADIENT_RADIAL) background->radial_gradient = radial;
    else background->conic_gradient = conic;
    return true;
}

static const char* css_background_url_value(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_STRING) {
        return value->type == CSS_VALUE_TYPE_URL ? value->data.url : value->data.string;
    }
    if (value->type != CSS_VALUE_TYPE_FUNCTION || !value->data.function ||
        !value->data.function->name ||
        strcmp(value->data.function->name, "url") != 0 ||
        value->data.function->arg_count <= 0) return nullptr;
    const CssValue* arg = value->data.function->args[0];
    return arg && (arg->type == CSS_VALUE_TYPE_URL || arg->type == CSS_VALUE_TYPE_STRING)
        ? (arg->type == CSS_VALUE_TYPE_URL ? arg->data.url : arg->data.string) : nullptr;
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
    span->ensure_boundary(lycon);
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
                const CssEnumInfo* info = css_enum_info(item->data.keyword);
                if (info && info->name && strcmp(info->name, "at") == 0) {
                    at_idx = i;
                }
            } else if (at_idx >= 0 && item->type == CSS_VALUE_TYPE_PERCENTAGE) {
                if (i == at_idx + 1) mask->cx = (float)(item->data.percentage.value / 100.0);
                else if (i == at_idx + 2) mask->cy = (float)(item->data.percentage.value / 100.0);
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

static void resolve_background_layer_component(LayoutContext* lycon,
                                               const CssDeclaration* decl,
                                               CssValue* item) {
    if (!lycon || !decl || !item) return;
    if (item->type == CSS_VALUE_TYPE_FUNCTION && item->data.function &&
        item->data.function->name) {
        const char* name = item->data.function->name;
        size_t length = strlen(name);
        if (str_ieq_const(name, length, "url")) {
            resolve_background_url_function(lycon, decl, item);
        } else if (css_background_gradient_type(item) != GRADIENT_NONE) {
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
            *width = layout_css_border_width_keyword(keyword);
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
    if (!radius || corner_index < 0 || corner_index >= 4) return;
    radius->horizontal[corner_index] = radius_x;
    radius->vertical[corner_index] = radius_y;
    radius->horizontal_percent[corner_index] = percent_x;
    radius->vertical_percent[corner_index] = percent_y;
    radius->specificities[corner_index] = specificity;
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
            if (css_value_is_slash(item)) {
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
    for (int i = 0; i < 4; i++) {
        if (specificity >= radius->specificities[i]) {
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
            if (!item || css_value_is_slash(item)) continue;
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
    if (corner_index < 0 || corner_index >= 4) return false;
    int64_t current_specificity = radius->specificities[corner_index];
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
    // the substituted value invalid, but Radiant must not recurse forever.
    if (stack_count >= 32 || css_var_stack_contains(var_stack, stack_count, var_name)) {
        return resolve_fallback();
    }
    const CssValue* var_value = lookup_css_variable(lycon, var_name);
    if (var_value) {
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
const CssValue* resolve_var_function(LayoutContext* lycon, const CssValue* value) {
    const char* var_stack[32];
    return resolve_var_function_inner(lycon, value, var_stack, 0);
}

// Helper: extract a numeric value from a CssValue (number, percentage, length)
static double resolve_color_component(const CssValue* v, bool is_alpha = false) {
    if (!v) return 0.0;
    switch (v->type) {
    case CSS_VALUE_TYPE_NUMBER:
        return v->data.number.value;
    case CSS_VALUE_TYPE_PERCENTAGE:
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
static Color hsl_to_rgb(float h, float s, float l, float a) {
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
        const CssFunction* func = value->data.function;
        if (!func || !func->name) break;


        if (str_ieq_const(func->name, strlen(func->name), "rgb") || str_ieq_const(func->name, strlen(func->name), "rgba")) {
            if (func->arg_count == 1 && func->args[0] && func->args[0]->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = func->args[0];
                double r = 0, g = 0, b = 0, a = 255;
                int num_idx = 0;
                bool found_slash = false;
                for (int i = 0; i < list->data.list.count && num_idx < 4; i++) {
                    const CssValue* v = list->data.list.values[i];
                    if (!v) continue;
                    if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name &&
                        strcmp(v->data.custom_property.name, "/") == 0) {
                        found_slash = true;
                        continue;
                    }
                    if (v->type == CSS_VALUE_TYPE_FUNCTION || v->type == CSS_VALUE_TYPE_VAR) {
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
                        if (v->type == CSS_VALUE_TYPE_NUMBER) {
                            a = val * 255.0;  // 0-1 scale
                        } else {
                            a = val;  // percentage already converted
                        }
                    }
                    num_idx++;
                }
                result.r = css_color_byte(r);
                result.g = css_color_byte(g);
                result.b = css_color_byte(b);
                result.a = css_color_byte(a);
            }
            else if (func->arg_count >= 3) {
                double r = resolve_color_component(func->args[0]);
                double g = resolve_color_component(func->args[1]);
                double b = resolve_color_component(func->args[2]);
                result.r = css_color_byte(r);
                result.g = css_color_byte(g);
                result.b = css_color_byte(b);
                if (func->arg_count >= 4) {
                    double a = resolve_color_component(func->args[3], true);
                    if (func->args[3] && func->args[3]->type == CSS_VALUE_TYPE_NUMBER) {
                        a = a * 255.0;
                    }
                    result.a = css_color_byte(a);
                }
            }
        }
        else if (str_ieq_const(func->name, strlen(func->name), "hsl") || str_ieq_const(func->name, strlen(func->name), "hsla")) {
            double h = 0, s = 0, l = 0, a = 1.0;
            if (func->arg_count == 1 && func->args[0] && func->args[0]->type == CSS_VALUE_TYPE_LIST) {
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


// CSS4 has a total of 148 colors
Color color_name_to_rgb(CssEnum color_name) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    if (!css_named_color_to_rgba(color_name, &r, &g, &b, &a)) {
        return (Color){ 0xFF000000 };
    }
    return (Color){ .r = r, .g = g, .b = b, .a = a };
}

float map_lambda_font_size_keyword(CssEnum keyword_enum) {
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
int16_t map_font_weight_numeric(const CssValue* value) {
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

// Encodes full CSS cascade ordering into a single int64_t for correct
// shorthand vs longhand resolution. Per CSS Cascading Level 4:
//   cascade_level > CSS specificity > source_order
// Bits 32-55: CSS specificity (ids<<16 | classes<<8 | elements, plus inline flag)
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
DisplayValue blockify_display(DisplayValue display) {
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
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_FLOW) {
        return DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    if (display.outer == CSS_VALUE_INLINE_BLOCK) {
        display.outer = CSS_VALUE_BLOCK;
    }
    if (display.outer == CSS_VALUE_INLINE && display.inner == CSS_VALUE_TABLE) {
        display.outer = CSS_VALUE_BLOCK;  // inline-table -> table
    }
    return display;
}

static DisplayValue css_default_display_for_element(DomElement* dom_elem, DomNode* node) {
    NameId tag_id = dom_elem ? dom_elem->tag_id : NAME_ID_NONE;
    if (css_is_mathml_element(dom_elem)) {
        return (dom_elem->tag_name && strcmp(dom_elem->tag_name, "math") == 0)
            ? DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_MATH}
            : DisplayValue{CSS_VALUE_BLOCK, CSS_VALUE_MATH};
    }
    static const NameId block_tags[] = {
        MARKUP_NAME_HTML, MARKUP_NAME_BODY, MARKUP_NAME_H1, MARKUP_NAME_H2,
        MARKUP_NAME_H3, MARKUP_NAME_H4, MARKUP_NAME_H5, MARKUP_NAME_H6,
        MARKUP_NAME_P, MARKUP_NAME_DIV, MARKUP_NAME_CENTER, MARKUP_NAME_UL,
        MARKUP_NAME_OL, MARKUP_NAME_DL, MARKUP_NAME_DT, MARKUP_NAME_DD,
        MARKUP_NAME_HEADER, MARKUP_NAME_MAIN, MARKUP_NAME_SECTION,
        MARKUP_NAME_FOOTER, MARKUP_NAME_ARTICLE, MARKUP_NAME_ASIDE,
        MARKUP_NAME_NAV, MARKUP_NAME_ADDRESS, MARKUP_NAME_BLOCKQUOTE,
        MARKUP_NAME_DETAILS, MARKUP_NAME_DIALOG, MARKUP_NAME_FIGURE,
        MARKUP_NAME_FIGCAPTION, MARKUP_NAME_HGROUP, MARKUP_NAME_PRE,
        MARKUP_NAME_FIELDSET, MARKUP_NAME_LEGEND, MARKUP_NAME_FORM,
        MARKUP_NAME_MENU, MARKUP_NAME_FRAMESET};
    static const NameId replaced_tags[] = {
        MARKUP_NAME_IMG, MARKUP_NAME_VIDEO, MARKUP_NAME_INPUT, MARKUP_NAME_SELECT,
        MARKUP_NAME_TEXTAREA, MARKUP_NAME_IFRAME, MARKUP_NAME_METER,
        MARKUP_NAME_PROGRESS, MARKUP_NAME_CANVAS, MARKUP_NAME_WEBVIEW,
        MARKUP_NAME_EMBED};
    static const NameId hidden_tags[] = {
        MARKUP_NAME_SCRIPT, MARKUP_NAME_STYLE, MARKUP_NAME_HEAD, MARKUP_NAME_TITLE,
        MARKUP_NAME_META, MARKUP_NAME_LINK, MARKUP_NAME_BASE,
        MARKUP_NAME_TEMPLATE, MARKUP_NAME_MAP, MARKUP_NAME_AREA, MARKUP_NAME_RP,
        MARKUP_NAME_DATALIST};
    static const NameId flow_block_tags[] = {
        MARKUP_NAME_OPTION, MARKUP_NAME_OPTGROUP, MARKUP_NAME_CAPTION};
    if (layout_tag_in_list(tag_id, block_tags, sizeof(block_tags) / sizeof(*block_tags)) ||
        layout_tag_in_list(tag_id, flow_block_tags,
                           sizeof(flow_block_tags) / sizeof(*flow_block_tags))) {
        return {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    }
    if (layout_tag_in_list(tag_id, replaced_tags,
                           sizeof(replaced_tags) / sizeof(*replaced_tags))) {
        return {CSS_VALUE_INLINE_BLOCK, RDT_DISPLAY_REPLACED};
    }
    if (layout_tag_in_list(tag_id, hidden_tags,
                           sizeof(hidden_tags) / sizeof(*hidden_tags))) {
        return {CSS_VALUE_NONE, CSS_VALUE_NONE};
    }
    if (layout_noscript_content_suppressed(dom_elem)) {
        return {CSS_VALUE_INLINE, CSS_VALUE_FLOW};
    }
    if (tag_id == MARKUP_NAME_LI) {
        DisplayValue display = {CSS_VALUE_LIST_ITEM, CSS_VALUE_FLOW};
        display.list_item = true;
        return display;
    }
    if (tag_id == MARKUP_NAME_SUMMARY) {
        // HTML details rendering gives the disclosure marker only to a direct
        // summary child; nested summaries remain ordinary block flow content.
        bool direct_details_child = node && node->parent && node->parent->is_element() &&
            node->parent->as_element()->tag() == MARKUP_NAME_DETAILS;
        if (!direct_details_child) return {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
        DisplayValue display = {CSS_VALUE_LIST_ITEM, CSS_VALUE_FLOW};
        display.list_item = true;
        return display;
    }
    if (tag_id == MARKUP_NAME_OBJECT) {
        return dom_elem && dom_elem->get_attribute(MARKUP_NAME_DATA)
            ? DisplayValue{CSS_VALUE_INLINE_BLOCK, RDT_DISPLAY_REPLACED}
            : DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW};
    }
    if (tag_id == MARKUP_NAME_AUDIO) {
        return dom_elem && dom_elem->has_attribute(MARKUP_NAME_CONTROLS)
            ? DisplayValue{CSS_VALUE_INLINE_BLOCK, RDT_DISPLAY_REPLACED}
            : DisplayValue{CSS_VALUE_INLINE, CSS_VALUE_FLOW};
    }
    if (tag_id == MARKUP_NAME_BUTTON) return {CSS_VALUE_INLINE_BLOCK, CSS_VALUE_FLOW};
    if (tag_id == MARKUP_NAME_HR) return {CSS_VALUE_BLOCK, RDT_DISPLAY_REPLACED};
    if (tag_id == MARKUP_NAME_RUBY) return {CSS_VALUE_INLINE, CSS_VALUE_RUBY};
    if (tag_id == MARKUP_NAME_RT) return {CSS_VALUE_INLINE, CSS_VALUE_RUBY_TEXT};
    if (tag_id == MARKUP_NAME_SVG) return {CSS_VALUE_INLINE, RDT_DISPLAY_REPLACED};
    if (tag_id == MARKUP_NAME_TABLE) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE};
    if (tag_id == MARKUP_NAME_THEAD || tag_id == MARKUP_NAME_TBODY ||
        tag_id == MARKUP_NAME_TFOOT) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW_GROUP};
    if (tag_id == MARKUP_NAME_TR) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW};
    if (tag_id == MARKUP_NAME_TH || tag_id == MARKUP_NAME_TD) {
        return {CSS_VALUE_TABLE_CELL, CSS_VALUE_TABLE_CELL};
    }
    if (tag_id == MARKUP_NAME_COLGROUP) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN_GROUP};
    if (tag_id == MARKUP_NAME_COL) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN};

    const char* tag_name = node ? node->node_name() : nullptr;
    if (tag_name) {
        if (strcmp(tag_name, "table") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE};
        if (strcmp(tag_name, "thead") == 0 || strcmp(tag_name, "tbody") == 0 ||
            strcmp(tag_name, "tfoot") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW_GROUP};
        if (strcmp(tag_name, "tr") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_ROW};
        if (strcmp(tag_name, "th") == 0 || strcmp(tag_name, "td") == 0) {
            return {CSS_VALUE_TABLE_CELL, CSS_VALUE_TABLE_CELL};
        }
        if (strcmp(tag_name, "caption") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
        if (strcmp(tag_name, "colgroup") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN_GROUP};
        if (strcmp(tag_name, "col") == 0) return {CSS_VALUE_BLOCK, CSS_VALUE_TABLE_COLUMN};
    }
    return {CSS_VALUE_INLINE, CSS_VALUE_FLOW};
}

static DisplayValue resolve_display_value_raw(void* child,
                                               bool skip_flex_grid_child_check = false);

static bool display_contents_child_of_flex_or_grid(DomNode* node) {
    for (DomNode* ancestor = node ? node->parent : nullptr;
         ancestor && ancestor->is_element(); ancestor = ancestor->parent) {
        // The ancestor probe only needs its own display value. Avoid asking
        // each ancestor to rescan its ancestors, which made deep trees O(n^3).
        DisplayValue ancestor_display = resolve_display_value_raw(ancestor, true);
        if (ancestor_display.outer == CSS_VALUE_NONE) return false;
        if (ancestor_display.outer == CSS_VALUE_CONTENTS) continue;
        return ancestor_display.inner == CSS_VALUE_FLEX ||
            ancestor_display.inner == CSS_VALUE_GRID;
    }
    return false;
}

static bool css_display_is_ruby_internal(CssEnum display) {
    return display == CSS_VALUE_RUBY_BASE ||
        display == CSS_VALUE_RUBY_TEXT ||
        display == CSS_VALUE_RUBY_BASE_CONTAINER ||
        display == CSS_VALUE_RUBY_TEXT_CONTAINER;
}

static DisplayValue resolve_display_value_raw(void* child,
                                              bool skip_flex_grid_child_check) {
    DisplayValue display = {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
    DomNode* node = static_cast<DomNode*>(child);
    if (node && node->is_element()) {
        // resolve display from CSS if available
        DomElement* dom_elem = node->as_element();
        NameId tag_id = dom_elem ? dom_elem->tag_id : NAME_ID_NONE;
        bool is_mathml = css_is_mathml_element(dom_elem);

        if (dom_elem && dom_elem->tag_name &&
            strcmp(dom_elem->tag_name, "::first-letter") == 0) {
            // CSS Pseudo §4.2: first-letter ignores authored display; float
            // blockifies the pseudo, while an unfloated first letter is inline.
            CssEnum first_letter_float = layout_specified_keyword(
                dom_elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
            if (first_letter_float == CSS_VALUE_LEFT ||
                first_letter_float == CSS_VALUE_RIGHT) {
                return {CSS_VALUE_BLOCK, CSS_VALUE_FLOW};
            }
            return {CSS_VALUE_INLINE, CSS_VALUE_FLOW};
        }

        // CSS 2.1 §9.7: Check for float and position - floated or absolutely positioned elements get blockified
        CssEnum float_value = layout_specified_keyword(
            dom_elem, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
        bool is_floated = (float_value == CSS_VALUE_LEFT || float_value == CSS_VALUE_RIGHT);
        CssEnum position_value = layout_specified_keyword(
            dom_elem, CSS_PROPERTY_POSITION, CSS_VALUE_NONE);
        bool is_abspos = (position_value == CSS_VALUE_ABSOLUTE || position_value == CSS_VALUE_FIXED);
        // CSS Flexbox §4 / CSS Grid §6: Children of flex/grid containers have their
        // CSS Display 3 §2.5 / Flexbox §4 / Grid §9: display:contents
        // removes the wrapper from the box tree, so blockification follows the
        // flattened-tree ancestor rather than only the DOM parent.
        bool is_flex_or_grid_child = !skip_flex_grid_child_check &&
            display_contents_child_of_flex_or_grid(node);
        // CSS 2.1 §9.7 rule 2: absolute/fixed position also triggers blockification
        bool needs_blockify = is_floated || is_abspos || is_flex_or_grid_child;
        CssDeclaration* specified_display_decl = nullptr;
        if (dom_elem && dom_elem->specified_style && dom_elem->specified_style->tree) {
            specified_display_decl = style_tree_get_declaration(
                dom_elem->specified_style, CSS_PROPERTY_DISPLAY);
        }
        bool has_specified_display = specified_display_decl != nullptr;
        bool specified_display_contents = specified_display_decl &&
            specified_display_decl->value &&
            specified_display_decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
            specified_display_decl->value->data.keyword == CSS_VALUE_CONTENTS;
        // HTML spec §14.3.1: The hidden attribute (UA stylesheet: [hidden] { display: none })
        // Must check before CSS cascade since it's a presentational hint
        if (dom_elem && dom_elem->has_attribute("hidden")) {
            DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
            return none_display;
        }
        // HTML spec §4.11.1: Non-summary children of closed <details> are hidden.
        // This must be checked here (not just in layout_flow_node) to cover all
        // layout paths including flex and grid when CSS overrides display.
        if (node->parent && node->parent->is_element()) {
            DomElement* parent_elem = node->parent->as_element();
            if (parent_elem->tag() == MARKUP_NAME_DETAILS && !parent_elem->has_attribute(MARKUP_NAME_OPEN)) {
                // CSS Display 3: contents exposes descendants before the closed-details
                // suppression boundary is applied to the flattened children.
                if (tag_id != MARKUP_NAME_SUMMARY && !specified_display_contents) {
                    DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
                    return none_display;
                }
            }
        }
        // This handles CSS 2.1 anonymous table objects created by layout
        // when display:none is set by UA defaults for hidden inputs, respect it
        if (dom_elem && tag_id == MARKUP_NAME_INPUT) {
            const char* type_attr = dom_elem->get_attribute("type");
            if (type_attr && (strcmp(type_attr, "hidden") == 0)) {
                DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
                return none_display;
            }
        }
        if (dom_elem && dom_elem->has_attribute("popover") &&
            !dom_elem->is_popover_open() && !has_specified_display) {
            // the closed-popover UA display rule must yield to author display declarations
            DisplayValue none_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
            return none_display;
        }
        if (dom_elem && dom_elem->has_attribute("popover") &&
            dom_elem->is_popover_open() && !has_specified_display) {
            // the HTML popover UA rule makes an open popover block-level before fit-content sizing
            bool object_fallback = dom_elem->tag_id == MARKUP_NAME_OBJECT &&
                !dom_elem->get_attribute(MARKUP_NAME_DATA);
            DisplayValue popover_display = {CSS_VALUE_BLOCK,
                object_fallback ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW};
            return needs_blockify ? blockify_display(popover_display) : popover_display;
        }
        if (dom_elem && !has_specified_display && !is_mathml &&
            dom_elem->display.inner != CSS_VALUE_NONE &&
            dom_elem->display.inner != 0 && dom_elem->styles_resolved()) {
            // CSS 2.1 §9.7: Even pre-resolved elements must be blockified when
            return needs_blockify ? blockify_display(dom_elem->display) : dom_elem->display;
        }
        bool is_replaced = css_display_element_is_replaced(dom_elem);
        if (dom_elem && dom_elem->has_animated_display()) {
            DisplayValue animated = dom_elem->animated_display();
            if (animated.outer == CSS_VALUE_CONTENTS &&
                css_display_contents_suppresses_element(dom_elem)) {
                return {CSS_VALUE_NONE, CSS_VALUE_NONE};
            }
            return needs_blockify ? blockify_display(animated) : animated;
        }
        // first, try to get display from CSS
        if (dom_elem && dom_elem->specified_style) {
            StyleTree* style_tree = dom_elem->specified_style;
            if (style_tree->tree) {
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
                            display.outer = CSS_VALUE_INLINE_BLOCK;
                            display.inner = is_replaced ? RDT_DISPLAY_REPLACED : CSS_VALUE_FLOW;
                            return needs_blockify ? blockify_display(display) : display;
                        } else if (decl->value && decl->value->type == CSS_VALUE_TYPE_KEYWORD &&
                                   decl->value->data.keyword == CSS_VALUE_INHERIT) {
                            // CSS 2.1 §9.2.4: inherit from the parent's computed display.
                            DomElement* parent_elem = dom_elem->parent_element();
                            if (parent_elem) {
                                DisplayValue parent_display = resolve_display_value((void*)parent_elem);
                                return needs_blockify ? blockify_display(parent_display) : parent_display;
                            }
                        } else if (css_resolve_display_css_value(
                                       dom_elem, decl->value, &display)) {
                            if (display.outer == CSS_VALUE_CONTENTS &&
                                css_display_contents_suppresses_element(dom_elem)) {
                                return {CSS_VALUE_NONE, CSS_VALUE_NONE};
                            }
                            // CSS Display 3: a parsed display value is blockified
                            // only when its outer box participates in that rule.
                            return needs_blockify ? blockify_display(display) : display;
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
        if (!has_specified_display && is_mathml) {
            display = css_default_display_for_element(dom_elem, node);
            return needs_blockify ? blockify_display(display) : display;
        }
        display = css_default_display_for_element(dom_elem, node);
        // CSS 2.1 §9.7: Apply blockification to tag-based defaults too.
        if (is_replaced && display.outer != CSS_VALUE_NONE && display.inner == CSS_VALUE_FLOW) {
            display.inner = RDT_DISPLAY_REPLACED;
        }
        return needs_blockify ? blockify_display(display) : display;
    }
    return display;
}

DisplayValue resolve_display_value(void* child) {
    DisplayValue display = resolve_display_value_raw(child);
    DomNode* node = static_cast<DomNode*>(child);
    if (!node || !node->is_element() || !node->parent ||
        !node->parent->is_element()) {
        return display;
    }

    // CSS Ruby 1 §2.2: in-flow block-level children of ruby boxes are
    // inlinified, so a block child of ruby-text computes to inline-block.
    DomElement* element = node->as_element();
    CssEnum float_value = layout_specified_keyword(
        element, CSS_PROPERTY_FLOAT, CSS_VALUE_NONE);
    CssEnum position_value = layout_specified_keyword(
        element, CSS_PROPERTY_POSITION, CSS_VALUE_NONE);
    bool out_of_flow = float_value == CSS_VALUE_LEFT ||
        float_value == CSS_VALUE_RIGHT ||
        position_value == CSS_VALUE_ABSOLUTE ||
        position_value == CSS_VALUE_FIXED;
    if (out_of_flow) return display;

    DomElement* parent = node->parent->as_element();
    DisplayValue parent_display = resolve_display_value_raw(parent, true);
    if (!css_display_is_ruby_internal(parent_display.inner)) return display;

    bool block_level = display.outer == CSS_VALUE_BLOCK ||
        display.outer == CSS_VALUE_LIST_ITEM ||
        display.outer == CSS_VALUE_TABLE;
    if (block_level) display.outer = CSS_VALUE_INLINE_BLOCK;
    return display;
}

static void resolve_current_font_size(LayoutContext* lycon) {
    if (!lycon) return;
    FontProp* font = nullptr;
    if (lycon->view && lycon->view->is_element()) {
        font = lam::dom_require<DOM_NODE_ELEMENT>(lycon->view)->font;
    } else if (lycon->view && lycon->view->is_text()) {
        font = lam::dom_require<DOM_NODE_TEXT>(lycon->view)->font;
    }
    if (font && font->font_size > 0.0f) {
        // CSS Viewport 1 applies zoom to used font metrics; line boxes must not
        // fall back to the unzoomed computed size after the font is resolved.
        lycon->font.current_font_size = font_prop_used_size(font);
        return;
    }
    lycon->font.current_font_size = lycon->font.style &&
        lycon->font.style->font_size > 0.0f
        ? font_prop_used_size(lycon->font.style) : 16.0f;
}

// evaluate calc() terms with precedence and nested parentheses.
static float evaluate_calc_expression(LayoutContext* lycon, uintptr_t raw_prop,
                                      CssValue** items, int count, int* pos, int depth) {
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
                (*pos)++;
                float sub = evaluate_calc_expression(lycon, raw_prop, items, count, pos, depth + 1);
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
                (*pos)++;
                break;
            }
        } else {
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

static bool resolve_calc_binary_operator(LayoutContext* lycon, uintptr_t property,
                                         const CssValue* left_value,
                                         const CssValue* operator_value,
                                         const CssValue* right_value,
                                         float* result) {
    if (!operator_value || !result) return false;
    float left = resolve_length_value(lycon, property, left_value);
    float right = resolve_length_value(lycon, property, right_value);
    const char* op_name = nullptr;
    if (operator_value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* op_info = css_enum_info(operator_value->data.keyword);
        op_name = op_info ? op_info->name : "";
    } else if (operator_value->type == CSS_VALUE_TYPE_CUSTOM &&
               operator_value->data.custom_property.name) {
        // CSS delimiters such as '-' are stored as custom names by the parser.
        op_name = operator_value->data.custom_property.name;
    } else {
        return false;
    }
    if (evaluate_simple_calc_operator(op_name, left, right, result)) return true;
    log_warn("calc: unknown operator '%s'", op_name);
    return false;
}

static bool css_percentage_uses_containing_inline_size(uintptr_t property) {
    CssPropertyCode code = (CssPropertyCode)property;
    return code == CSS_PROPERTY_MARGIN || code == CSS_PROPERTY_PADDING ||
        (code >= CSS_PROPERTY_MARGIN_TOP && code <= CSS_PROPERTY_MARGIN_LEFT) ||
        (code >= CSS_PROPERTY_MARGIN_BLOCK && code <= CSS_PROPERTY_MARGIN_INLINE_END) ||
        (code >= CSS_PROPERTY_PADDING_TOP && code <= CSS_PROPERTY_PADDING_LEFT) ||
        (code >= CSS_PROPERTY_PADDING_BLOCK && code <= CSS_PROPERTY_PADDING_INLINE_END);
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

static bool css_absolute_unit_scale(CssUnit unit, double* scale) {
    static const double scales[] = {
        1.0, 96.0 / 2.54, 96.0 / 25.4, 96.0, 4.0 / 3.0, 16.0,
        96.0 / 2.54 / 40.0,
    };
    if (unit < CSS_UNIT_PX || unit > CSS_UNIT_Q || !scale) return false;
    *scale = scales[unit - CSS_UNIT_PX];
    return true;
}

// resolve a CSS length, percentage, or number to pixels.
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
    // In raw mode, NUMBER values are not multiplied by font-size for line-height
    bool raw_number_mode = (intptr_t)property < 0;
    uintptr_t effective_property = raw_number_mode ? (uintptr_t)(-(intptr_t)property) : property;
    float result = 0.0f;
    switch (value->type) {
    case CSS_VALUE_TYPE_NUMBER:
        if (!raw_number_mode && effective_property == CSS_PROPERTY_LINE_HEIGHT) {
            if (lycon->font.current_font_size < 0) {
                resolve_current_font_size(lycon);
            }
            result = value->data.number.value * lycon->font.current_font_size;
        } else {
            result = (float)value->data.number.value;
        }
        break;
    case CSS_VALUE_TYPE_LENGTH: {
        double num = value->data.length.value;
        CssUnit unit = value->data.length.unit;
        double absolute_scale = 0.0;
        if (css_absolute_unit_scale(unit, &absolute_scale)) {
            result = num * absolute_scale;
            break;
        }
        switch (unit) {
        case CSS_UNIT_REM:
            if (lycon->root_font_size < 0) {
                resolve_current_font_size(lycon);
                lycon->root_font_size = lycon->font.current_font_size < 0 ?
                    lycon->ui_context->default_font.font_size : lycon->font.current_font_size;
            }
            result = num * lycon->root_font_size;
            break;
        case CSS_UNIT_EM:
            // Font-relative lengths resolve against the computed font size;
            // zoom is applied once below at used-value time.
            result = num * (lycon->font.style && lycon->font.style->font_size > 0.0f
                ? lycon->font.style->font_size : lycon->font.current_font_size);
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
            float x_height_ratio = font_get_x_height_ratio(font_box_handle(&lycon->font));
            float font_size = lycon->font.style && lycon->font.style->font_size > 0.0f
                ? lycon->font.style->font_size : lycon->font.current_font_size;
            result = num * font_size * x_height_ratio;
            break;
        }
        case CSS_UNIT_CH: {
            // CSS Values 4 §6.1.1: equal to the advance width of the "0" (zero) glyph
            float font_size = lycon->font.style && lycon->font.style->font_size > 0.0f
                ? lycon->font.style->font_size : lycon->font.current_font_size;
            if (font_box_handle(&lycon->font)) {
                FontStyleDesc style = font_style_desc_from_prop(lycon->font.style);
                LoadedGlyph* zero_glyph = font_load_glyph(font_box_handle(&lycon->font), &style, (uint32_t)'0', false);
                if (zero_glyph && zero_glyph->advance_x > 0.0f) {
                    float raster_scale = ui_context_raster_scale(lycon->ui_context);
                    float advance = zero_glyph->advance_x / raster_scale;
                    float zoom = layout_effective_zoom(lycon->view);
                    if (zoom > 0.0f) advance /= zoom;
                    if (lycon->font.style && lycon->font.style->font_size > 0.0f &&
                        lycon->font.current_font_size > 0.0f &&
                        lycon->font.style->font_size != lycon->font.current_font_size) {
                        advance *= lycon->font.current_font_size / lycon->font.style->font_size;
                    }
                    result = num * advance;
                } else {
                    result = num * font_size * 0.5f;
                }
            } else {
                result = num * font_size * 0.5f;
            }
            break;
        }
        case CSS_UNIT_LH: {
            // CSS Values 4 §6.1.1: `lh` is the computed line-height of the
            // element whose length is being resolved.
            DomElement* owner = lycon->view && lycon->view->is_element()
                ? lycon->view->as_element() : nullptr;
            const CssValue* line_height = owner && owner->blk
                ? owner->block()->line_height : nullptr;
            float target_font_size = lycon->font.style &&
                lycon->font.style->font_size > 0.0f
                ? lycon->font.style->font_size : lycon->font.current_font_size;
            if (target_font_size <= 0.0f) target_font_size = 16.0f;
            if (line_height) {
                result = num * layout_resolve_line_height_value(
                    lycon, line_height, owner, target_font_size);
            } else {
                CssValue normal_value = {};
                normal_value.type = CSS_VALUE_TYPE_KEYWORD;
                normal_value.data.keyword = CSS_VALUE_NORMAL;
                result = num * layout_resolve_line_height_value(
                    lycon, &normal_value, owner, target_font_size);
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
            result = percentage * lycon->font.style->font_size / 100.0;
            if (effective_property == CSS_PROPERTY_LINE_HEIGHT) {
                result *= layout_effective_zoom(lycon->view);
            }
        } else if (effective_property == CSS_PROPERTY_LETTER_SPACING) {
            // CSS Text 4 defines spacing percentages against the current font
            if (lycon->font.current_font_size < 0) {
                resolve_current_font_size(lycon);
            }
            result = percentage * lycon->font.current_font_size / 100.0;
        } else if (effective_property == CSS_PROPERTY_HEIGHT || effective_property == CSS_PROPERTY_MIN_HEIGHT ||
                   effective_property == CSS_PROPERTY_MAX_HEIGHT || effective_property == CSS_PROPERTY_TOP ||
                   effective_property == CSS_PROPERTY_BOTTOM) {
            // CSS Position 3 §3.4: For top/bottom position insets, if the containing
            bool is_position_inset = (effective_property == CSS_PROPERTY_TOP || effective_property == CSS_PROPERTY_BOTTOM);
            if (is_position_inset && lycon->block.parent && lycon->block.parent->given_height < 0) {
                result = NAN;
            } else if (lycon->block.parent && lycon->block.parent->content_height > 0) {
                result = percentage * lycon->block.parent->content_height / 100.0;
            } else if (lycon->block.parent && lycon->block.parent->given_height > 0) {
                // Parent has given height but content_height not yet calculated
                result = percentage * lycon->block.parent->given_height / 100.0;
            } else if (!lycon->block.parent && lycon && lycon->height > 0) {
                // No parent context (root html element) - use viewport height
                // Layout uses logical pixels, so use lycon->height without raster scaling.
                result = percentage * lycon->height / 100.0;
            } else {
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
            float inline_base = css_containing_inline_percentage_base(lycon);
            if (inline_base > 0.0f) {
                result = percentage * inline_base / 100.0;
            } else {
                result = 0.0f;
            }
        } else {
            if (lycon->block.parent && lycon->block.parent->content_width > 0) {
                result = percentage * lycon->block.parent->content_width / 100.0;
            } else if (!lycon->block.parent && lycon && lycon->width > 0) {
                // No parent context (root html element) - use viewport width
                // CSS 2.1 §10.3: percentage widths on the root resolve against the
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
        } else if (keyword == CSS_VALUE_THIN || keyword == CSS_VALUE_MEDIUM ||
                   keyword == CSS_VALUE_THICK) {
            // CSS 2.1 §8.5.1 maps the three border-width keywords to fixed pixels.
            result = layout_css_border_width_keyword(keyword);
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
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            if (func->arg_count >= 1 && func->args && func->args[0]) {
                CssValue* arg = func->args[0];
                if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count == 3) {
                    CssValue* val1 = arg->data.list.values[0];
                    CssValue* op = arg->data.list.values[1];
                    CssValue* val2 = arg->data.list.values[2];
                    if (op && (op->type == CSS_VALUE_TYPE_KEYWORD ||
                               (op->type == CSS_VALUE_TYPE_CUSTOM &&
                                op->data.custom_property.name))) {
                        // raw mode keeps unitless calc operands from inheriting line-height scaling.
                        if (!resolve_calc_binary_operator(lycon, raw_prop, val1, op, val2, &result)) {
                            result = NAN;
                        }
                    } else {
                        log_warn("calc: operator is not a keyword or custom (type=%d)", op ? op->type : -1);
                        result = NAN;
                    }
                } else if (arg->type == CSS_VALUE_TYPE_LIST && arg->data.list.count >= 1) {
                    // and parenthesized sub-expressions (CSS parser flattens parens to
                    int pos = 0;
                    result = evaluate_calc_expression(lycon, raw_prop,
                                arg->data.list.values, arg->data.list.count, &pos, 0);
                } else {
                    result = resolve_length_value(lycon, raw_prop, arg);
                }
            } else {
                log_warn("calc() with no arguments");
                result = NAN;
            }
            // Note: We do NOT apply line-height unitless multiplier here because:
            // 2. The heuristic (< 10 means unitless) is too fragile for complex CSS with variables
            // 3. If the result is truly unitless for line-height, the caller should handle it
        } else if (strcmp(func->name, "min") == 0 && func->args && func->arg_count >= 1) {
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            result = INFINITY;
            for (int i = 0; i < func->arg_count; i++) {
                if (!func->args[i]) continue;
                float val = resolve_length_value(lycon, raw_prop, func->args[i]);
                if (!isnan(val) && val < result) result = val;
            }
            if (isinf(result)) result = NAN;
        } else if (strcmp(func->name, "max") == 0 && func->args && func->arg_count >= 1) {
            uintptr_t raw_prop = (uintptr_t)(-(intptr_t)property);
            result = -INFINITY;
            for (int i = 0; i < func->arg_count; i++) {
                if (!func->args[i]) continue;
                float val = resolve_length_value(lycon, raw_prop, func->args[i]);
                if (!isnan(val) && val > result) result = val;
            }
            if (isinf(result)) result = NAN;
        } else if (strcmp(func->name, "clamp") == 0 && func->args && func->arg_count >= 3) {
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
            result = NAN;
        } else if (strcmp(func->name, "var") == 0) {
            const char* var_name = css_var_function_name(func);
            if (var_name) {
                const CssValue* var_value = lookup_css_variable(lycon, var_name);
                if (var_value) {
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
        if (value->data.list.count > 0 && value->data.list.values[0]) {
            result = resolve_length_value(lycon, property, value->data.list.values[0]);
        } else {
            result = 0.0f;
        }
        break;
    case CSS_VALUE_TYPE_CUSTOM:
        // This should not be resolved directly - it should be stored and retrieved via var()
        result = 0.0f;
        break;
    case CSS_VALUE_TYPE_VAR:
        result = 0.0f;
        break;
    default:
        log_warn("unknown length value type: %d", value->type);
        result = NAN;  // Use NAN instead of 0 to indicate unresolvable value
        break;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH && !isnan(result) &&
        effective_property != CSS_PROPERTY_FONT_SIZE) {
        // CSS Viewport 1 applies effective zoom to every resolved CSS length,
        result *= layout_effective_zoom(lycon->view);
    }
    if (length_resolve_depth == 1 && !isnan(result)) {
        // percentages remain percentage values and use the scaled CB; only
        // css Values 4 permits approximating an actual value that cannot be
        // represented by the layout coordinate range; apply that invariant to
        result = layout_clamp_dimension(result);
    }
    length_resolve_depth--;
    return result;
}

static float* inherited_spacing_slot(BoundaryProp* bound, CssPropertyCode prop_id) {
    if (!bound) return nullptr;
    CssBoxSide side = radiant_css_box_side(prop_id);
    if (prop_id >= CSS_PROPERTY_MARGIN_TOP && prop_id <= CSS_PROPERTY_MARGIN_LEFT) {
        return radiant_spacing_value(&bound->margin, side);
    }
    if (prop_id >= CSS_PROPERTY_PADDING_TOP && prop_id <= CSS_PROPERTY_PADDING_LEFT) {
        return radiant_spacing_value(&bound->padding, side);
    }
    return nullptr;
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

static bool copy_border_side_inherit(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                     int64_t specificity) {
    BorderProp* parent = parent_border_prop(lycon);
    if (!parent) return false;
    BorderProp* border = layout_ensure_border(lycon, span);
    radiant_border_side_copy(radiant_border_side(border, side),
                             radiant_border_side(parent, side), specificity);
    return true;
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
    CssQuadValues values;
    if (!values.expand(src_space)) {
        log_warn("unexpected spacing shorthand value count");
        return;
    }
    Margin parsed = {};
    for (int side = CSS_BOX_SIDE_TOP; side <= CSS_BOX_SIDE_LEFT; side++) {
        const CssValue* value = values.side[side];
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


// Parse a single CssValue into a GridTrackSize
// Returns NULL if the value cannot be converted to a track size
static GridTrackSize* parse_css_value_to_track_size(const CssValue* val);

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

static GridTrackSize* parse_repeat_function(const CssValue* val) {
    if (!val || val->type != CSS_VALUE_TYPE_FUNCTION) return NULL;
    if (!val->data.function->name || strcmp(val->data.function->name, "repeat") != 0) return NULL;
    if (val->data.function->arg_count < 2) return NULL;
    CssValue* count_val = val->data.function->args[0];
    bool is_auto_fill = false;
    bool is_auto_fit = false;
    int repeat_count = 0;
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
            int fr_value = (int)(val->data.length.value * 100);
            track_size = create_grid_track_size(GRID_TRACK_SIZE_FR, fr_value);
        } else {
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
        const char* func_name = val->data.function->name;
        if (func_name) {
            if (strcmp(func_name, "minmax") == 0) {
                track_size = parse_minmax_function(val);
            } else if (strcmp(func_name, "repeat") == 0) {
                track_size = parse_repeat_function(val);
            } else if (strcmp(func_name, "fit-content") == 0) {
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
    if (!track_list || track_list->track_count >= track_list->allocated_tracks) {
        destroy_grid_track_size(track_size);
        return;
    }
    track_list->tracks[track_list->track_count++] = track_size;
}

static int grid_fixed_repeat_count(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_NUMBER) return 0;
    int count = (int)value->data.number.value; // INT_CAST_OK: CSS repeat count.
    return count > MAX_GRID_SPAN ? MAX_GRID_SPAN : count;
}

static void append_grid_repeated_track_values(GridTrackList* track_list,
                                              const CssValue* const* values,
                                              int value_count, int repeat_count) {
    if (!track_list || !values || value_count <= 0 || repeat_count <= 0) return;
    for (int repeat = 0; repeat < repeat_count; repeat++) {
        for (int value = 0; value < value_count; value++) {
            if (track_list->track_count >= track_list->allocated_tracks) return;
            GridTrackSize* track = parse_css_value_to_track_size(values[value]);
            if (track) append_grid_track_size(track_list, track);
        }
    }
}

// Parse grid track list from CSS value list, handling repeat() functions
// Parse grid track list from CSS value list
static void parse_grid_track_list(const CssValue* value, GridTrackList** track_list_ptr) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST || !track_list_ptr) return;
    int count = value->data.list.count;
    CssValue** values = value->data.list.values;


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
                    int repeat_count = grid_fixed_repeat_count(count_val);
                    int track_vals = val->data.function->arg_count - 1;
                    total_tracks += repeat_count * (track_vals > 0 ? track_vals : 1);
                } else {
                    total_tracks += 1;
                }
            } else {
                total_tracks += 1;
            }
        } else if (css_value_can_be_grid_track_size(val)) {
            total_tracks++;
        }
        else if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;
            if (strncmp(name, "repeat(", 7) == 0 || strcmp(name, "repeat") == 0) {
                if (i + 1 < count && values[i + 1] && values[i + 1]->type == CSS_VALUE_TYPE_NUMBER) {
                    int repeat_count = grid_fixed_repeat_count(values[i + 1]);
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
    *track_list_ptr = replace_grid_track_list(track_list_ptr, total_tracks);
    GridTrackList* track_list = *track_list_ptr;
    if (!track_list) {
        return;
    }


    int i = 0;
    while (i < count) {
        CssValue* val = values[i];
        if (!val) { i++; continue; }
        if (val->type == CSS_VALUE_TYPE_FUNCTION) {
            const char* func_name = val->data.function->name;
            if (func_name && strcmp(func_name, "repeat") == 0) {
                // Check if auto-fill/auto-fit or fixed count
                CssValue* count_val = val->data.function->arg_count > 0 ? val->data.function->args[0] : NULL;
                bool is_auto = count_val && count_val->type == CSS_VALUE_TYPE_KEYWORD &&
                               (count_val->data.keyword == CSS_VALUE_AUTO_FILL ||
                                count_val->data.keyword == CSS_VALUE_AUTO_FIT);
                if (is_auto) {
                    GridTrackSize* ts = parse_repeat_function(val);
                    if (ts) {
                        append_grid_track_size(track_list, ts);
                        track_list->is_repeat = true;
                    }
                } else if (count_val && count_val->type == CSS_VALUE_TYPE_NUMBER) {
                    // Fixed repeat count - expand inline
                    append_grid_repeated_track_values(
                        track_list, val->data.function->args + 1,
                        val->data.function->arg_count - 1,
                        grid_fixed_repeat_count(count_val));
                }
            } else {
                GridTrackSize* ts = parse_css_value_to_track_size(val);
                if (ts) {
                    append_grid_track_size(track_list, ts);
                }
            }
            i++;
            continue;
        }
        if (val->type == CSS_VALUE_TYPE_CUSTOM && val->data.custom_property.name) {
            const char* name = val->data.custom_property.name;
            if (strcmp(name, "[") == 0) {
                int line_idx = track_list->track_count; // this name belongs at the current line position
                while (++i < count) {
                    CssValue* nv = values[i];
                    if (!nv) continue;
                    if (nv->type == CSS_VALUE_TYPE_CUSTOM && nv->data.custom_property.name &&
                            strcmp(nv->data.custom_property.name, "]") == 0) {
                        i++; // advance past "]"
                        break;
                    }
                    if (nv->type == CSS_VALUE_TYPE_KEYWORD) {
                        const CssEnumInfo* ki = css_enum_info(nv->data.keyword);
                        if (ki && ki->name && line_idx <= track_list->allocated_tracks && !track_list->line_names[line_idx]) {
                            track_list->line_names[line_idx] = mem_strdup(ki->name, MEM_CAT_LAYOUT);
                            track_list->line_name_count++;
                        }
                        continue;
                    }
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
                int repeat_count = grid_fixed_repeat_count(values[i]);
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
                append_grid_repeated_track_values(
                    track_list, repeat_tracks, repeat_track_count, repeat_count);
                continue;
            }
            i++;
            continue;
        }
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
    apply_grid_template_track_value(&rows, &grid->grid_template_rows, "grid-template rows");
    apply_grid_template_track_value(&columns, &grid->grid_template_columns, "grid-template columns");
    clear_grid_template_areas(grid);
    return true;
}

static bool apply_grid_shorthand(const CssValue* value, GridProp* grid) {
    if (!apply_grid_template_shorthand(value, grid)) return false;
    // The `<grid-template>` branch of `grid` resets the implicit grid; otherwise
    clear_grid_template_track_list(&grid->grid_auto_rows);
    clear_grid_template_track_list(&grid->grid_auto_columns);
    grid->grid_auto_flow = CSS_VALUE_ROW;
    grid->is_dense_packing = false;
    return true;
}


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

static bool resolve_property_callback(AvlNode* node, void* context, bool font_pass) {
    LayoutContext* lycon = (LayoutContext*)context;
    StyleNode* style_node = (StyleNode*)node->declaration;
    CssPropertyCode prop_id = (CssPropertyCode)node->property_id;
    if (css_property_is_font(prop_id) != font_pass) return true;
    CssDeclaration* decl = style_node ? style_node->winning_decl : nullptr;
    if (decl) resolve_css_property(prop_id, decl, lycon);
    return true;
}

// Font declarations must resolve before em/ex-dependent properties.
static bool resolve_font_property_callback(AvlNode* node, void* context) {
    return resolve_property_callback(node, context, true);
}

static bool resolve_non_font_property_callback(AvlNode* node, void* context) {
    return resolve_property_callback(node, context, false);
}

static const CssPropertyCode kFontProperties[] = {
    CSS_PROPERTY_FONT,
    CSS_PROPERTY_FONT_SIZE,
    CSS_PROPERTY_FONT_FAMILY,
    CSS_PROPERTY_FONT_WEIGHT,
    CSS_PROPERTY_FONT_STYLE,
    CSS_PROPERTY_FONT_VARIANT,
    CSS_PROPERTY_LINE_HEIGHT,
};

static bool css_style_tree_has_font_property(StyleTree* style_tree,
                                             bool include_line_height) {
    if (!style_tree || !style_tree->tree) return false;
    size_t count = sizeof(kFontProperties) / sizeof(kFontProperties[0]);
    for (size_t i = 0; i < count; i++) {
        if (!include_line_height && kFontProperties[i] == CSS_PROPERTY_LINE_HEIGHT) continue;
        if (avl_tree_search(style_tree->tree, kFontProperties[i])) return true;
    }
    return false;
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

static void apply_pseudo_font_family(LayoutContext* lycon, FontProp* font,
                                     const CssValue* raw_value) {
    if (!font || !raw_value) return;
    const CssValue* value = resolve_var_function(lycon, raw_value);
    if (!value) return;
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        const char* family = css_select_font_family(lycon, value, true);
        if (family && *family) {
            radiant_retain_font_family(font, lam::PoolPtr<char>((char*)family));
        }
        return;
    }
    const char* family = placeholder_font_family_from_value(value);
    if (family && *family) {
        radiant_retain_font_family(font, lam::PoolPtr<char>((char*)family));
    }
}

static bool pseudo_longhand_overridden_by_font(StyleTree* style,
                                               CssDeclaration* longhand) {
    if (!style || !longhand) return false;
    CssDeclaration* shorthand = style_tree_get_declaration(style, CSS_PROPERTY_FONT);
    return shorthand && css_declaration_cascade_compare(shorthand, longhand) > 0;
}

FontProp* layout_resolve_first_line_font(LayoutContext* lycon,
                                         DomElement* element,
                                         FontProp* base_font) {
    if (!lycon || !element || !base_font) return nullptr;
    StyleTree* style = element->pseudo_style(PSEUDO_STYLE_FIRST_LINE);
    if (!style || !style->tree || !css_style_tree_has_font_property(style, false)) {
        return nullptr;
    }

    FontProp* first_line_font = (FontProp*)alloc_prop(lycon, sizeof(FontProp));
    font_prop_copy(first_line_font, base_font);
    first_line_font->used_zoom = base_font->used_zoom;

    CssDeclaration* font_decl = style_tree_get_declaration(style, CSS_PROPERTY_FONT);
    if (font_decl && font_decl->value) {
        const CssValue* value = resolve_var_function(lycon, font_decl->value);
        LayoutFontShorthandParts parts;
        if (value && layout_parse_font_shorthand(value, &parts)) {
            first_line_font->font_variant = parts.small_caps
                ? CSS_VALUE_SMALL_CAPS : CSS_VALUE_NORMAL;
            if (parts.size) {
                LayoutFontSizeResult resolved = layout_resolve_font_size_value(
                    lycon, parts.size, base_font, true);
                if (!isnan(resolved.value) && resolved.value >= 0.0f) {
                    first_line_font->font_size = resolved.value;
                    first_line_font->font_size_from_medium = resolved.from_medium;
                }
            }
            if (parts.weight) {
                first_line_font->font_weight = map_font_weight(parts.weight);
                first_line_font->font_weight_numeric = map_font_weight_numeric(parts.weight);
            }
            if (parts.style && parts.style->type == CSS_VALUE_TYPE_KEYWORD) {
                first_line_font->font_style = parts.style->data.keyword;
            }
            const char* family = css_select_font_shorthand_family(
                lycon, value, parts.group, parts.family_start, true);
            if (family && *family) {
                radiant_retain_font_family(first_line_font,
                    lam::PoolPtr<char>((char*)family));
            }
        }
    }

    CssDeclaration* font_size = style_tree_get_declaration(style, CSS_PROPERTY_FONT_SIZE);
    if (font_size && font_size->value &&
        !pseudo_longhand_overridden_by_font(style, font_size)) {
        LayoutFontSizeResult resolved = layout_resolve_font_size_value(
            lycon, font_size->value, base_font, true);
        if (!isnan(resolved.value) && resolved.value >= 0.0f) {
            first_line_font->font_size = resolved.value;
            first_line_font->font_size_from_medium = resolved.from_medium;
        }
    }
    CssDeclaration* font_weight = style_tree_get_declaration(style, CSS_PROPERTY_FONT_WEIGHT);
    if (font_weight && font_weight->value &&
        !pseudo_longhand_overridden_by_font(style, font_weight)) {
        first_line_font->font_weight = map_font_weight(font_weight->value);
        first_line_font->font_weight_numeric = map_font_weight_numeric(font_weight->value);
    }
    CssDeclaration* font_style = style_tree_get_declaration(style, CSS_PROPERTY_FONT_STYLE);
    if (font_style && font_style->value &&
        !pseudo_longhand_overridden_by_font(style, font_style) &&
        font_style->value->type == CSS_VALUE_TYPE_KEYWORD) {
        first_line_font->font_style = font_style->value->data.keyword;
    }
    CssDeclaration* font_family = style_tree_get_declaration(style, CSS_PROPERTY_FONT_FAMILY);
    if (font_family && font_family->value &&
        !pseudo_longhand_overridden_by_font(style, font_family)) {
        apply_pseudo_font_family(lycon, first_line_font, font_family->value);
    }
    CssDeclaration* font_variant = style_tree_get_declaration(style, CSS_PROPERTY_FONT_VARIANT);
    if (font_variant && font_variant->value &&
        !pseudo_longhand_overridden_by_font(style, font_variant) &&
        font_variant->value->type == CSS_VALUE_TYPE_KEYWORD) {
        first_line_font->font_variant = font_variant->value->data.keyword;
    }
    return first_line_font;
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
    StyleTree* pseudo_style = dom_elem->pseudo_style(PSEUDO_STYLE_PLACEHOLDER);
    if (!pseudo_style || !pseudo_style->tree) {
        form->placeholder_font = nullptr;
        return;
    }
    FontProp* base_font = dom_elem->font ? dom_elem->font : lycon->font.style;
    bool has_placeholder_font_prop =
        style_tree_get_declaration(pseudo_style, CSS_PROPERTY_FONT_SIZE) ||
        style_tree_get_declaration(pseudo_style, CSS_PROPERTY_FONT_WEIGHT) ||
        style_tree_get_declaration(pseudo_style, CSS_PROPERTY_FONT_STYLE) ||
        style_tree_get_declaration(pseudo_style, CSS_PROPERTY_FONT_FAMILY);
    if (has_placeholder_font_prop && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            font_prop_copy(placeholder_font, base_font);
        }
    } else {
        form->placeholder_font = nullptr;
    }
    CssDeclaration* color_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_COLOR);
    if (color_decl && color_decl->value) {
        Color color = resolve_color_value(lycon, color_decl->value);
        form->placeholder_color_r = color.r;
        form->placeholder_color_g = color.g;
        form->placeholder_color_b = color.b;
        form->placeholder_color_a = color.a;
        form->placeholder_has_color = 1;
    }
    CssDeclaration* opacity_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_OPACITY);
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
        pseudo_style, CSS_PROPERTY_FONT_SIZE);
    if (font_size_decl && font_size_decl->value && base_font) {
        float font_size = layout_resolve_font_size(
            lycon, font_size_decl->value, base_font, true, nullptr);
        if (font_size >= 0.0f) {
            FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
            if (placeholder_font) {
                placeholder_font->font_size = font_size;
                placeholder_font->font_size_from_medium = false;
            }
        }
    }
    CssDeclaration* font_weight_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_FONT_WEIGHT);
    if (font_weight_decl && font_weight_decl->value && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            placeholder_font->font_weight = map_font_weight(font_weight_decl->value);
            placeholder_font->font_weight_numeric = map_font_weight_numeric(font_weight_decl->value);
        }
    }
    CssDeclaration* font_style_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_FONT_STYLE);
    if (font_style_decl && font_style_decl->value &&
        font_style_decl->value->type == CSS_VALUE_TYPE_KEYWORD && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
            placeholder_font->font_style = font_style_decl->value->data.keyword;
        }
    }
    CssDeclaration* font_family_decl = style_tree_get_declaration(
        pseudo_style, CSS_PROPERTY_FONT_FAMILY);
    if (font_family_decl && font_family_decl->value && base_font) {
        FontProp* placeholder_font = ensure_placeholder_font(lycon, form, base_font);
        if (placeholder_font) {
                apply_pseudo_font_family(lycon, placeholder_font, font_family_decl->value);
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
    // scales by defaultFixedFontSize/defaultFontSize (13/16). This must run
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
        !parent_font.style || !font_box_handle(&parent_font) ||
        lycon->block.line_height <= 0.0f) {
        return;
    }
    ViewSpan* span = lam::view_require_element(lycon->view);
    if (!span) return;
    span->ensure_font(lycon);
    if (!span->font || span->fontp()->font_size <= 0.0f) return;
    FontBox computed_font = {};
    setup_font(lycon->ui_context, &computed_font, span->font);
    const FontMetrics* parent_metrics = font_get_metrics(font_box_handle(&parent_font));
    const FontMetrics* initial_metrics = font_get_metrics(font_box_handle(&computed_font));
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

static bool preserve_html_ua_font_size(LayoutContext* lycon, DomElement* element,
                                       ViewSpan* span) {
    if (!lycon || !element || !span) return false;
    NameId tag = element->tag();
    if (tag == MARKUP_NAME_TABLE && lycon->doc && lycon->doc->view_tree &&
        is_quirks_mode(lycon->doc->view_tree->html_version)) {
        span->ensure_font(lycon);
        span->font->font_size = 16.0f;
        span->font->font_size_from_medium = true;
        return true;
    }
    if (tag == MARKUP_NAME_CODE || tag == MARKUP_NAME_KBD ||
        tag == MARKUP_NAME_SAMP || tag == MARKUP_NAME_TT) {
        return span->font && span->fontp()->family &&
            str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace") &&
            span->fontp()->font_size > 0 && span->fontp()->font_size_from_medium;
    }
    if (tag >= MARKUP_NAME_H1 && tag <= MARKUP_NAME_H6) {
        return span->font && span->fontp()->font_size > 0;
    }
    if (tag == MARKUP_NAME_SMALL || tag == MARKUP_NAME_BIG ||
        tag == MARKUP_NAME_SUB || tag == MARKUP_NAME_SUP) {
        return span->font && span->fontp()->font_size > 0 &&
            !span->fontp()->font_size_from_medium;
    }
    if (tag == MARKUP_NAME_INPUT || tag == MARKUP_NAME_BUTTON ||
        tag == MARKUP_NAME_SELECT || tag == MARKUP_NAME_TEXTAREA) {
        bool textarea_medium = tag == MARKUP_NAME_TEXTAREA && span->font &&
            span->fontp()->family &&
            str_ieq_const(span->fontp()->family, strlen(span->fontp()->family), "monospace") &&
            span->fontp()->font_size > 0 && span->fontp()->font_size_from_medium;
        return span->font && span->fontp()->font_size > 0 &&
            (!span->fontp()->font_size_from_medium || textarea_medium);
    }
    return false;
}

void resolve_css_styles(DomElement* dom_elem, LayoutContext* lycon) {
    assert(dom_elem);
    // iterate through specified_style AVL tree
    StyleTree* style_tree = dom_elem->specified_style;
    if (!style_tree || !style_tree->tree) {
        return;
    }
    // Font5 §4.4: skip first-pass AVL traversal if no font properties exist.
    // Most elements (especially in markdown) inherit all font properties from
    // their parent and have zero font-related CSS declarations.
    bool has_any_font_prop = css_style_tree_has_font_property(style_tree, true);
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
    if (has_any_font_prop) {
        ViewSpan* span = lam::view_require_element(lycon->view);
        // when only font-size changes and the family is inherited.
        bool has_font = css_style_tree_has_font_property(style_tree, false);
        if (has_font && span && span->font && span->fontp()->family && lycon->ui_context) {
            setup_font(lycon->ui_context, &lycon->font, span->font);
        }
    }
    {
        ViewSpan* span = lam::view_require_element(lycon->view);
        if (span && span->font && span->fontp()->font_size > 0.0f) {
            lycon->font.style = span->font;
            lycon->font.current_font_size = span->fontp()->font_size;
        }
    }
    // the current element instead of falling back to a parent or UA default.
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
        CSS_PROPERTY_HYPHENATE_CHARACTER,
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
    DomElement* parent = dom_parent_element(dom_elem);
    StyleTree* parent_tree = (parent && parent->specified_style)
                             ? parent->specified_style : NULL;
    // Run inheritance check if parent has either specified_style or computed font
    // This handles anonymous table elements that have font but no specified_style
    if (parent_tree || (parent && parent->font)) {
        ViewSpan* inheritance_span = lam::view_require_element(lycon->view);
        for (size_t i = 0; i < num_inheritable; i++) {
            CssPropertyCode prop_id = inheritable_props[i];
            CssDeclaration* existing = style_tree_get_declaration(style_tree, prop_id);
            if (existing) {
                continue;
            }
            if (prop_id == CSS_PROPERTY_WHITE_SPACE) {
                NameId tag = dom_elem->tag();
                if ((tag == MARKUP_NAME_PRE || tag == MARKUP_NAME_LISTING || tag == MARKUP_NAME_XMP) &&
                    inheritance_span->blk && inheritance_span->block_mut()->white_space == CSS_VALUE_PRE) {
                    continue;
                }
            }
            // HTML spec: <th> uses "-internal-center-or-inherit" UA rule.
            // otherwise use the inherited value. E.g.:
            if (prop_id == CSS_PROPERTY_TEXT_ALIGN) {
                DomElement* cur_elem = lam::dom_require_element(lycon->view);
                if (cur_elem && cur_elem->tag_name && strcmp(cur_elem->tag_name, "th") == 0) {
                    bool inherited_is_noninitial = false;
                    for (DomElement* p = dom_parent_element(dom_elem);
                         p; p = dom_parent_element(p)) {
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
                        continue;
                    }
                }
            }
            // Special case: font shorthand sets font-family directly on span->font
            // without creating a CssDeclaration, so also check if font->family is set
            if (prop_id == CSS_PROPERTY_FONT_FAMILY) {
                if (inheritance_span->font && inheritance_span->fontp()->family) {
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
                bool has_author_monospace_family = font_family_decl && inheritance_span->font &&
                    inheritance_span->fontp()->family &&
                    str_ieq_const(inheritance_span->fontp()->family, strlen(inheritance_span->fontp()->family), "monospace");
                if (has_author_monospace_family) {
                    continue;
                }
            }
            // Special case: font shorthand sets line-height directly on span->blk
            // without creating a CssDeclaration, so also check if line_height is set
            if (prop_id == CSS_PROPERTY_LINE_HEIGHT) {
                if (inheritance_span->blk && inheritance_span->block_mut()->line_height) {
                    continue;
                }
            }
            // size, so inheritance must not overwrite it with the parent's
            // computed font-size unless author CSS explicitly set font-size.
            if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                if (preserve_html_ua_font_size(
                        lycon, dom_elem, inheritance_span)) continue;
            }
            // Property not set, check parent chain for inherited declaration
            DomElement* ancestor = dom_parent_element(dom_elem);
            CssDeclaration* inherited_decl = NULL;
            // Special handling for font-family: also check ancestor's computed font->family
            // creating a CSS_PROPERTY_FONT_FAMILY declaration)
            if (prop_id == CSS_PROPERTY_FONT_FAMILY && ancestor && ancestor->font && ancestor->fontp()->family) {
                if (dom_elem->tag() == MARKUP_NAME_TEXTAREA) {
                    CssDeclaration* own_font_family = style_tree_get_declaration(
                        style_tree, CSS_PROPERTY_FONT_FAMILY);
                    CssDeclaration* own_font = style_tree_get_declaration(
                        style_tree, CSS_PROPERTY_FONT);
                    bool has_ua_textarea_family = inheritance_span && inheritance_span->font &&
                        inheritance_span->fontp()->family &&
                        str_ieq_const(inheritance_span->fontp()->family,
                                      strlen(inheritance_span->fontp()->family), "monospace");
                    if (!own_font_family && !own_font && has_ua_textarea_family) {
                        continue;
                    }
                }
                inheritance_span->ensure_font(lycon);
                radiant_retain_font_family(inheritance_span->font,
                                           lam::PoolPtr<char>(ancestor->fontp()->family));
                continue;  // Move to next property
            }
            if ((prop_id == CSS_PROPERTY_LETTER_SPACING ||
                 prop_id == CSS_PROPERTY_WORD_SPACING) &&
                ancestor && ancestor->font) {
                inheritance_span->ensure_font(lycon);
                if (prop_id == CSS_PROPERTY_LETTER_SPACING) {
                    inheritance_span->font->letter_spacing = ancestor->font->letter_spacing;
                    inheritance_span->font->letter_spacing_percent = ancestor->font->letter_spacing_percent;
                    inheritance_span->font->letter_spacing_is_percent = ancestor->font->letter_spacing_is_percent;
                } else {
                    inheritance_span->font->word_spacing = ancestor->font->word_spacing;
                    inheritance_span->font->word_spacing_percent = ancestor->font->word_spacing_percent;
                    inheritance_span->font->word_spacing_is_percent = ancestor->font->word_spacing_is_percent;
                }
                if (inheritance_span->font->word_spacing_is_percent && prop_id == CSS_PROPERTY_WORD_SPACING) {
                    inheritance_span->font->word_spacing = inheritance_span->font->word_spacing_percent *
                        inheritance_span->font->font_size / 100.0f;
                }
                if (inheritance_span->font->letter_spacing_is_percent && prop_id == CSS_PROPERTY_LETTER_SPACING) {
                    inheritance_span->font->letter_spacing = inheritance_span->font->letter_spacing_percent *
                        inheritance_span->font->font_size / 100.0f;
                }
                continue;
            }
            // Special handling for line-height: also check ancestor's computed blk->line_height
            // creating a CSS_PROPERTY_LINE_HEIGHT declaration)
            if (prop_id == CSS_PROPERTY_LINE_HEIGHT && ancestor && ancestor->blk && ancestor->block_mut()->line_height) {
                inheritance_span->ensure_block(lycon);
                const CssValue* alh = ancestor->block()->line_height;
                // CSS 2.1 §10.8.1: <length> and <percentage> line-height values
                // px. Only unitless <number> inherits the multiplier.
                // Font-relative units (em, ex, ch) and percentages must be resolved
                // against the declaring ancestor's font-size, not the child's.
                CssValue* computed = resolve_inherited_line_height_value(
                    lycon, ancestor, alh, true);
                inheritance_span->blk->line_height = computed ? computed : ancestor->blk->line_height;
                continue;
            }
            // CSS 2.1 §6.1.1/§6.2.1: inherited properties inherit the
            // parent's computed value, not the parent's winning declaration.
            if (prop_id == CSS_PROPERTY_FONT_SIZE && ancestor &&
                ancestor->font && ancestor->fontp()->font_size > 0) {
                if (inheritance_span->font && inheritance_span->fontp()->font_size > 0.0f &&
                    !inheritance_span->fontp()->font_size_from_medium) {
                    continue;
                }
                inheritance_span->ensure_font(lycon);
                inheritance_span->font->font_size = ancestor->font->font_size;
                inheritance_span->font->font_size_from_medium = ancestor->font->font_size_from_medium;
                continue;  // Move to next property
            }
            if (prop_id == CSS_PROPERTY_DIRECTION && !dom_elem->get_attribute("dir")) {
                // css writing modes: direction inherits from the computed parent value;
                // html dir can supply that value without a specified CSS declaration.
                CssEnum inherited_direction = find_inherited_block_keyword(
                    dom_elem, CSS_PROPERTY_DIRECTION, false, false, CSS_VALUE_LTR);
                // preserve the canonical LTR default without materializing a block prop
                // on ordinary elements; some layout paths use prop presence as state.
                if (inherited_direction != CSS_VALUE_LTR || inheritance_span->blk) {
                    inheritance_span->ensure_block(lycon);
                    inheritance_span->blk->direction = inherited_direction;
                }
                continue;
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
                if (prop_id == CSS_PROPERTY_FONT_SIZE) {
                    inheritance_span->ensure_font(lycon);
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
                        inheritance_span->ensure_block(lycon);
                        inheritance_span->blk->line_height = computed;
                        continue;
                    }
                }
                resolve_css_property(prop_id, inherited_decl, lycon);
            }
        }
    }
    // Finalize border widths: per CSS spec, border-width computes to 0
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
            if (radiant_border_side_is_hidden(refs)) {
                if (*refs.width != 0.0f) *refs.width = 0.0f;
            } else if (*refs.width == 0.0f && *refs.width_specificity == 0) {
                *refs.width = 3.0f;
            }
        }
    }
    // CSS 2.1 §8.3, §8.4, §17.5: Certain box-model properties do not apply to
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
        // CSS 2.1 §8.3/§8.4: these properties do not affect table-internal
        // geometry, but their computed values must survive for descendants'
        // `inherit` resolution; table layout ignores them at the box stage.
        // CSS 2.1 §17.5: Border handling for table-internal elements depends on
        // CSS 2.1 §10.3, §17.5.3: 'width' does not apply to table-row,
        // CSS 2.1 §10.5, §17.5.3: 'height' does not apply to table-column
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
    // preserved on blk->given_width/given_height so that children can
    // inherit them (CSS 2.1 §6.2.1). The actual enforcement happens in
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

void set_multi_value(LayoutContext* lycon, MultiValue* mv, const CssValue* value) {
    if (!mv || !value) return;
    value = resolve_var_function(lycon, value);
    if (!value) return;
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE || value->type == CSS_VALUE_TYPE_NUMBER) {
        mv->length = (CssValue*)value;
    // Border shorthand colors such as rgba() are function values; otherwise
    // they fall through to currentcolor and paint as an opaque black border.
    } else if (css_value_is_background_color_candidate(value)) {
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
                    break;
            }
        }
    }
    else if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            set_multi_value(lycon, mv, item);
        }
    }
}

static void apply_border_side_shorthand(LayoutContext* lycon, ViewSpan* span, CssBoxSide side,
                                        const CssValue* value, int64_t specificity) {
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
        copy_border_side_inherit(lycon, span, side, specificity);
        return;
    }
    BorderProp* border = layout_ensure_border(lycon, span);
    RadiantBorderSide refs = radiant_border_side(border, side);
    MultiValue parts = {0};
    set_multi_value(lycon, &parts, value);
    // Physical and logical aliases must share cascade and none/hidden width semantics.
    bool style_applied = parts.style && specificity >= *refs.style_specificity;
    bool hidden_style = false;
    if (style_applied) {
        *refs.style = parts.style->data.keyword;
        *refs.style_specificity = specificity;
        hidden_style = *refs.style == CSS_VALUE_NONE || *refs.style == CSS_VALUE_HIDDEN;
        if (specificity >= *refs.width_specificity && (hidden_style || !parts.length)) {
            *refs.width = hidden_style ? 0.0f : 3.0f;
            *refs.width_specificity = specificity;
        }
    }
    if (parts.length && !hidden_style && specificity >= *refs.width_specificity) {
        *refs.width = resolve_length_value(lycon, radiant_border_width_property(side), parts.length);
        *refs.width_specificity = specificity;
    }
    if (parts.color && specificity >= *refs.color_specificity) {
        *refs.color = resolve_color_value(lycon, parts.color);
        *refs.color_specificity = specificity;
    } else if (style_applied && *refs.style != CSS_VALUE_NONE &&
               *refs.style != CSS_VALUE_HIDDEN && specificity >= *refs.color_specificity) {
        *refs.color = get_current_color(lycon);
        *refs.color_specificity = specificity;
    }
}

static void apply_dimension_constraint(LayoutContext* lycon, ViewBlock* block,
                                       CssPropertyCode prop_id, const CssValue* value) {
    BlockProp* props = block->ensure_block(lycon);
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
    LayoutAxisRefs axis(props, horizontal);
    LayoutAxisRefs parent_axis(parent_props, horizontal);
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

static void css_store_self_alignment(LayoutContext* lycon, ViewSpan* span,
                                     bool justify, CssSelfAlignment value) {
    if (!span || value.value == CSS_VALUE__UNDEF) return;
    BlockProp* block = span->ensure_block(lycon);
    if (!block) return;
    if (justify) block->justify_self = value;
    else block->align_self = value;
}

static void css_set_flex_item_values(DomElement* span,
                                     float grow, float shrink, float basis,
                                     bool basis_is_percent) {
    span->fi->flex_grow = grow;
    span->fi->flex_shrink = shrink;
    span->fi->flex_basis = basis;
    span->fi->flex_basis_is_percent = basis_is_percent;
    span->fi->flex_basis_is_content = false;
    span->fi->flex_basis_is_stretch = false;
}

struct CssFlexBasisValue {
    float value = -1.0f;
    bool is_percent = false;
    bool is_content = false;
    bool is_stretch = false;
    bool valid = false;
};

static CssFlexBasisValue css_parse_flex_basis_value(LayoutContext* lycon,
                                                    CssPropertyCode property,
                                                    const CssValue* value,
                                                    bool resolve_length,
                                                    bool allow_content) {
    CssFlexBasisValue result;
    if (!value) return result;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        switch (value->data.keyword) {
            case CSS_VALUE_AUTO:
                result.valid = true;
                break;
            case CSS_VALUE_CONTENT:
                result.is_content = allow_content;
                result.valid = allow_content;
                break;
            case CSS_VALUE_STRETCH:
                result.is_stretch = true;
                result.valid = true;
                break;
            default:
                break;
        }
    } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
        result.value = resolve_length
            ? resolve_length_value(lycon, property, value)
            : (float)value->data.length.value;
        result.valid = true;
    } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        result.value = (float)value->data.percentage.value;
        result.is_percent = true;
        result.valid = true;
    } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
        result.value = (float)value->data.number.value;
        result.valid = true;
    }
    return result;
}

static void css_set_flex_basis_value(DomElement* span,
                                     const CssFlexBasisValue& basis) {
    span->fi->flex_basis = basis.value;
    span->fi->flex_basis_is_percent = basis.is_percent;
    span->fi->flex_basis_is_content = basis.is_content;
    span->fi->flex_basis_is_stretch = basis.is_stretch;
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
        span->blk->list_style_image = {};
        span->blk->list_style_image.url = (char*)alloc_prop(lycon, 5);
        str_copy(span->block()->list_style_image.url, 5, "none", 4);
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

static void css_store_hyphenate_character(LayoutContext* lycon, ViewSpan* span,
                                          const char* character) {
    if (!lycon || !span || !character) return;
    size_t length = strlen(character);
    span->blk->hyphenate_character = (char*)alloc_prop(lycon, length + 1);
    str_copy(span->blk->hyphenate_character, length + 1, character, length);
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
    if (url) {
        size_t length = strlen(url);
        span->blk->list_style_image = {};
        span->blk->list_style_image.url = (char*)alloc_prop(lycon, length + 1);
        str_copy(span->block()->list_style_image.url, length + 1, url, length);
        return true;
    }

    GradientType gradient_type = css_background_gradient_type(value);
    if (gradient_type == GRADIENT_NONE) return false;

    ListStyleImage image = {};
    image.gradient_type = gradient_type;
    bool resolved = gradient_type == GRADIENT_LINEAR
        ? resolve_linear_gradient_value(lycon, value, &image.linear_gradient)
        : gradient_type == GRADIENT_RADIAL
            ? resolve_radial_gradient_value(lycon, value, &image.radial_gradient)
            : resolve_conic_gradient_value(lycon, value, &image.conic_gradient);
    if (!resolved) return false;
    // CSS Images gradients have no intrinsic dimensions; retain the resolved
    // image so list markers can apply the image-marker default object size.
    span->blk->list_style_image = image;
    return true;
}

static void css_apply_list_style_component(LayoutContext* lycon, ViewSpan* span,
                                           const CssValue* value, bool list_member) {
    if (!value) return;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        css_apply_list_style_keyword(lycon, span, value->data.keyword, list_member);
    } else if (value->type == CSS_VALUE_TYPE_STRING && value->data.string) {
        css_store_list_style_type_string(lycon, span, value->data.string);
    } else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        CssEnum position;
        if (css_list_style_custom_position(value->data.custom_property.name, &position)) {
            span->blk->list_style_position = position;
        }
    } else if (value->type == CSS_VALUE_TYPE_URL || value->type == CSS_VALUE_TYPE_FUNCTION) {
        css_store_list_style_image(lycon, span, value);
    }
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
                                  LayoutAxis axis) {
    const CssValue* fit_limit = css_fit_content_function_limit(value);
    bool horizontal = axis == LAYOUT_AXIS_X;
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
    LayoutAxisRefs context(&lycon->block, axis);
    if (context.given) *context.given = size;
    if (!block) return;
    block->ensure_block(lycon);
    LayoutAxisRefs refs(block->block_mut(), axis);
    *refs.given = size;
    *refs.given_type = fit_limit ? CSS_VALUE_FIT_CONTENT
        : value->type == CSS_VALUE_TYPE_KEYWORD ? value->data.keyword : CSS_VALUE__UNDEF;
    float* fit_size = refs.given_fit_content_limit;
    float* fit_percent = refs.given_fit_content_percent;
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
        block->ensure_multicol(lycon);
        block->multicol_prop()->column_gap = gap;
        block->multicol_prop()->column_gap_is_normal = normal;
        block->multicol_prop()->column_gap_is_percent = percent;
    }
}

static bool css_background_repeat_keyword(CssEnum keyword) {
    return keyword == CSS_VALUE_REPEAT || keyword == CSS_VALUE_NO_REPEAT ||
           keyword == CSS_VALUE_ROUND || keyword == CSS_VALUE_SPACE;
}

static void resolve_background_repeat_property(ViewSpan* span, const CssValue* value) {
    if (!span || !value) return;
    BackgroundProp* background = span->boundary()->background;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (css_background_repeat_keyword(value->data.keyword)) {
            background->bg_repeat_x = background->bg_repeat_y = value->data.keyword;
        }
        return;
    }
    if (value->type != CSS_VALUE_TYPE_LIST || value->data.list.count < 2) return;
    CssValue* x = value->data.list.values[0];
    CssValue* y = value->data.list.values[1];
    if (x && x->type == CSS_VALUE_TYPE_KEYWORD &&
        css_background_repeat_keyword(x->data.keyword)) {
        background->bg_repeat_x = x->data.keyword;
    }
    if (y && y->type == CSS_VALUE_TYPE_KEYWORD &&
        css_background_repeat_keyword(y->data.keyword)) {
        background->bg_repeat_y = y->data.keyword;
    }
}

static MultiColumnProp* resolve_multicol_prop(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return nullptr;
    block->ensure_multicol(lycon);
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
        if (height) multicol->column_height_is_specified = false;
    } else if (value->type == CSS_VALUE_TYPE_LENGTH ||
               (allow_number && value->type == CSS_VALUE_TYPE_NUMBER)) {
        float size = resolve_length_value(lycon, property, value);
        if (size > 0.0f || (height && size == 0.0f)) {
            *target = size;
            if (height) multicol->column_height_is_specified = true;
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

static void resolve_multicol_rule_property(LayoutContext* lycon, ViewBlock* block,
                                           CssPropertyCode property,
                                           const CssValue* value) {
    MultiColumnProp* multicol = resolve_multicol_prop(lycon, block);
    if (!multicol || !value) return;
    if (property == CSS_PROPERTY_COLUMN_RULE_STYLE) {
        if (value->type == CSS_VALUE_TYPE_KEYWORD) multicol->rule_style = value->data.keyword;
        return;
    }
    if (property == CSS_PROPERTY_COLUMN_RULE_COLOR) {
        if (value->type == CSS_VALUE_TYPE_COLOR) {
            multicol->rule_color = resolve_color_value(lycon, value);
        }
        return;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH) {
        multicol->rule_width = resolve_length_value(
            lycon, CSS_PROPERTY_COLUMN_RULE_WIDTH, value);
    } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword != CSS_VALUE_THIN &&
            value->data.keyword != CSS_VALUE_MEDIUM &&
            value->data.keyword != CSS_VALUE_THICK) return;
        multicol->rule_width = layout_css_border_width_keyword(value->data.keyword);
    } else {
        return;
    }
}

static void resolve_break_value(LayoutContext* lycon, ViewBlock* block,
                               const CssValue* value, CssEnum* target) {
    if (!block || !target || !value || value->type != CSS_VALUE_TYPE_KEYWORD) return;
    block->ensure_block(lycon);
    *target = value->data.keyword;
}

static void resolve_line_count_value(LayoutContext* lycon, ViewBlock* block,
                                     const CssValue* value, int* target) {
    if (!block || !target || !value || value->type != CSS_VALUE_TYPE_NUMBER) return;
    block->ensure_block(lycon);
    int count = (int)value->data.number.value; // INT_CAST_OK: line count
    if (count > 0) {
        *target = count;
    }
}

static void resolve_flow_break_property(LayoutContext* lycon, ViewBlock* block,
                                        CssPropertyCode property, const CssValue* value) {
    if (!block) return;
    block->ensure_block(lycon);
    if (property == CSS_PROPERTY_BREAK_BEFORE || property == CSS_PROPERTY_PAGE_BREAK_BEFORE) {
        resolve_break_value(lycon, block, value, &block->blk->break_before);
    } else if (property == CSS_PROPERTY_BREAK_AFTER || property == CSS_PROPERTY_PAGE_BREAK_AFTER) {
        resolve_break_value(lycon, block, value, &block->blk->break_after);
    } else if (property == CSS_PROPERTY_BREAK_INSIDE ||
               property == CSS_PROPERTY_PAGE_BREAK_INSIDE) {
        resolve_break_value(lycon, block, value, &block->blk->break_inside);
    } else if (property == CSS_PROPERTY_ORPHANS) {
        resolve_line_count_value(lycon, block, value, &block->blk->orphans);
    } else {
        resolve_line_count_value(lycon, block, value, &block->blk->widows);
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

struct CssKeywordSlotSpec {
    CssPropertyCode property;
    size_t offset;
};

static CssEnum* css_keyword_slot(void* object, const CssKeywordSlotSpec* slots,
                                 size_t count, CssPropertyCode property) {
    if (!object) return nullptr;
    for (size_t i = 0; i < count; i++) {
        if (slots[i].property == property) {
            return (CssEnum*)((char*)object + slots[i].offset);
        }
    }
    return nullptr;
}

enum CssSimpleKeywordTarget : uint8_t {
    CSS_SIMPLE_KEYWORD_BLOCK,
    CSS_SIMPLE_KEYWORD_FONT,
    CSS_SIMPLE_KEYWORD_INLINE,
};

enum CssSimpleKeywordRule : uint8_t {
    CSS_SIMPLE_KEYWORD_POSITIVE,
    CSS_SIMPLE_KEYWORD_NO_INHERIT,
    CSS_SIMPLE_KEYWORD_ANY,
    CSS_SIMPLE_KEYWORD_FONT_KERNING,
    CSS_SIMPLE_KEYWORD_CARET_SHAPE,
    CSS_SIMPLE_KEYWORD_BASELINE_SOURCE,
};

struct CssSimpleKeywordSpec {
    CssPropertyCode property;
    CssSimpleKeywordTarget target;
    CssSimpleKeywordRule rule;
    size_t offset;
};

static const CssSimpleKeywordSpec* css_simple_keyword_spec(CssPropertyCode property) {
    static const CssSimpleKeywordSpec specs[] = {
        {CSS_PROPERTY_FONT_KERNING, CSS_SIMPLE_KEYWORD_FONT,
         CSS_SIMPLE_KEYWORD_FONT_KERNING, offsetof(FontProp, font_kerning)},
        {CSS_PROPERTY_CURSOR, CSS_SIMPLE_KEYWORD_INLINE,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(InlineProp, cursor)},
        {CSS_PROPERTY_CARET_SHAPE, CSS_SIMPLE_KEYWORD_INLINE,
         CSS_SIMPLE_KEYWORD_CARET_SHAPE, offsetof(InlineProp, caret_shape)},
        {CSS_PROPERTY_TEXT_ALIGN_LAST, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_NO_INHERIT, offsetof(BlockProp, text_align_last)},
        {CSS_PROPERTY_BASELINE_SOURCE, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_BASELINE_SOURCE, offsetof(BlockProp, baseline_source)},
        {CSS_PROPERTY_DOMINANT_BASELINE, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_ANY, offsetof(BlockProp, dominant_baseline)},
        {CSS_PROPERTY_MIX_BLEND_MODE, CSS_SIMPLE_KEYWORD_INLINE,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(InlineProp, mix_blend_mode)},
        {CSS_PROPERTY_TEXT_TRANSFORM, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, text_transform)},
        {CSS_PROPERTY_TEXT_WRAP_STYLE, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, text_wrap_style)},
        {CSS_PROPERTY_TEXT_OVERFLOW, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, text_overflow)},
        {CSS_PROPERTY_WORD_BREAK, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, word_break)},
        {CSS_PROPERTY_LINE_BREAK, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, line_break)},
        {CSS_PROPERTY_WORD_WRAP, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, overflow_wrap)},
        {CSS_PROPERTY_OVERFLOW_WRAP, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, overflow_wrap)},
        {CSS_PROPERTY_HYPHENS, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, hyphens)},
        {CSS_PROPERTY_WHITE_SPACE, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, white_space)},
        {CSS_PROPERTY_TEXT_SPACING_TRIM, CSS_SIMPLE_KEYWORD_BLOCK,
         CSS_SIMPLE_KEYWORD_POSITIVE, offsetof(BlockProp, text_spacing_trim)},
    };
    for (const CssSimpleKeywordSpec& spec : specs) {
        if (spec.property == property) return &spec;
    }
    return nullptr;
}

static bool css_simple_keyword_is_valid(CssEnum keyword, CssSimpleKeywordRule rule) {
    switch (rule) {
        case CSS_SIMPLE_KEYWORD_ANY: return true;
        case CSS_SIMPLE_KEYWORD_FONT_KERNING:
            return keyword == CSS_VALUE_NONE || keyword == CSS_VALUE_NORMAL ||
                keyword == CSS_VALUE_AUTO;
        case CSS_SIMPLE_KEYWORD_CARET_SHAPE:
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_BAR ||
                keyword == CSS_VALUE_BLOCK || keyword == CSS_VALUE_UNDERSCORE;
        case CSS_SIMPLE_KEYWORD_BASELINE_SOURCE:
            return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_FIRST ||
                keyword == CSS_VALUE_LAST;
        case CSS_SIMPLE_KEYWORD_POSITIVE:
            return keyword > 0;
        case CSS_SIMPLE_KEYWORD_NO_INHERIT:
            return keyword > 0 && keyword != CSS_VALUE_INHERIT;
    }
    return false;
}

static void resolve_simple_keyword_property(LayoutContext* lycon, ViewSpan* span,
                                            ViewBlock* block, CssPropertyCode property,
                                            const CssValue* value) {
    const CssSimpleKeywordSpec* spec = css_simple_keyword_spec(property);
    if (!spec || !value || value->type != CSS_VALUE_TYPE_KEYWORD ||
        !css_simple_keyword_is_valid(value->data.keyword, spec->rule)) return;
    if (property == CSS_PROPERTY_TEXT_OVERFLOW && !block) return;
    void* target = nullptr;
    if (spec->target == CSS_SIMPLE_KEYWORD_BLOCK) target = span->ensure_block(lycon);
    else if (spec->target == CSS_SIMPLE_KEYWORD_FONT) target = span->ensure_font(lycon);
    else target = span->ensure_inline(lycon);
    if (target) *(CssEnum*)((char*)target + spec->offset) = value->data.keyword;
}

static bool resolve_common_keyword_property(LayoutContext* lycon, ViewSpan* span,
                                            ViewBlock* block, CssPropertyCode property,
                                            const CssDeclaration* decl,
                                            const CssValue* value) {
    if (property == CSS_PROPERTY_FONT_STYLE || property == CSS_PROPERTY_FONT_VARIANT) {
        if (shorthand_overrides_longhand(lycon, CSS_PROPERTY_FONT, decl)) return true;
        span->ensure_font(lycon);
        if (property == CSS_PROPERTY_FONT_STYLE) {
            resolve_keyword_slot(value, &span->font_mut()->font_style);
            return true;
        }
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            if (value->data.keyword == CSS_VALUE_INHERIT) {
                DomElement* parent = dom_parent_element(
                    lam::dom_require<DOM_NODE_ELEMENT>(lycon->view));
                span->font->font_variant = parent && parent->font
                    ? parent->fontp()->font_variant : CSS_VALUE_NORMAL;
            } else if (value->data.keyword > 0) {
                span->font->font_variant = value->data.keyword;
            }
        } else if (value->type == CSS_VALUE_TYPE_CUSTOM &&
                   value->data.custom_property.name) {
            CssEnum variant = css_enum_by_name(value->data.custom_property.name);
            if (variant != CSS_VALUE__UNDEF) span->font->font_variant = variant;
        }
        return true;
    }
    if (css_simple_keyword_spec(property)) {
        resolve_simple_keyword_property(lycon, span, block, property, value);
        return true;
    }
    return false;
}

static bool css_property_is_ignored(CssPropertyCode property) {
    static const CssPropertyCode ignored[] = {
        CSS_PROPERTY_DISPLAY, CSS_PROPERTY_MARGIN_TRIM,
        CSS_PROPERTY_TRANSFORM_STYLE, CSS_PROPERTY_BACKFACE_VISIBILITY,
        CSS_PROPERTY_BORDER_IMAGE_SLICE, CSS_PROPERTY_BORDER_IMAGE_OUTSET,
        CSS_PROPERTY_BORDER_IMAGE, CSS_PROPERTY_CLIP,
        CSS_PROPERTY_ANIMATION, CSS_PROPERTY_ANIMATION_NAME,
        CSS_PROPERTY_ANIMATION_DURATION, CSS_PROPERTY_ANIMATION_TIMING_FUNCTION,
        CSS_PROPERTY_ANIMATION_DELAY, CSS_PROPERTY_ANIMATION_ITERATION_COUNT,
        CSS_PROPERTY_ANIMATION_DIRECTION, CSS_PROPERTY_ANIMATION_FILL_MODE,
        CSS_PROPERTY_ANIMATION_PLAY_STATE
    };
    for (CssPropertyCode ignored_property : ignored) {
        if (ignored_property == property) return true;
    }
    return false;
}

static void resolve_background_keyword_property(LayoutContext* lycon, ViewSpan* span,
                                                CssPropertyCode property,
                                                const CssValue* value) {
    layout_ensure_background(lycon, span);
    BackgroundProp* background = span->boundary()->background;
    const CssKeywordSlotSpec slots[] = {
        {CSS_PROPERTY_BACKGROUND_ATTACHMENT, offsetof(BackgroundProp, bg_attachment)},
        {CSS_PROPERTY_BACKGROUND_ORIGIN, offsetof(BackgroundProp, bg_origin)},
        {CSS_PROPERTY_BACKGROUND_CLIP, offsetof(BackgroundProp, bg_clip)},
        {CSS_PROPERTY_BACKGROUND_BLEND_MODE, offsetof(BackgroundProp, blend_mode)}
    };
    CssEnum* slot = css_keyword_slot(
        background, slots, sizeof(slots) / sizeof(*slots), property);
    if (slot) resolve_keyword_slot(value, slot);
}

static void resolve_outline_longhand(LayoutContext* lycon, ViewSpan* span,
                                     CssPropertyCode property, const CssValue* value) {
    layout_ensure_outline(lycon, span);
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
    block->ensure_block(lycon);
    if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
        block->blk->line_clamp = 0;
        return;
    }
    if (value->type != CSS_VALUE_TYPE_NUMBER && value->type != CSS_VALUE_TYPE_LENGTH) return;
    float raw = value->type == CSS_VALUE_TYPE_NUMBER
        ? (float)value->data.number.value : (float)value->data.length.value;
    if (raw > 0.0f) block->blk->line_clamp = (int)raw; // INT_CAST_OK: line count.
}

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

static void css_store_background_size_axis(BackgroundProp* background, bool horizontal,
                                           CssBackgroundComponent component) {
    if (horizontal) {
        background->bg_size_width = component.value;
        background->bg_size_width_is_percent = component.is_percent;
        background->bg_size_width_auto = component.is_auto;
    } else {
        background->bg_size_height = component.value;
        background->bg_size_height_is_percent = component.is_percent;
        background->bg_size_height_auto = component.is_auto;
    }
}

static void css_store_background_position_axis(BackgroundProp* background, bool horizontal,
                                               CssBackgroundComponent component) {
    if (horizontal) {
        background->bg_position_x = component.value;
        background->bg_position_x_is_percent = component.is_percent;
    } else {
        background->bg_position_y = component.value;
        background->bg_position_y_is_percent = component.is_percent;
    }
}

static void resolve_background_size(LayoutContext* lycon, ViewSpan* span,
                                    const CssValue* value, CssPropertyCode property) {
    layout_ensure_background(lycon, span);
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
        css_store_background_size_axis(background, true, width);
        background->bg_size_height_auto = true;
    } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
        background->bg_size_type = (CssEnum)0;
        CssBackgroundComponent width = resolve_background_size_component(
            lycon, property, value->data.list.values[0], background->bg_size_width,
            background->bg_size_width_is_percent, background->bg_size_width_auto);
        CssBackgroundComponent height = resolve_background_size_component(
            lycon, property, value->data.list.values[1], background->bg_size_height,
            background->bg_size_height_is_percent, background->bg_size_height_auto);
        css_store_background_size_axis(background, true, width);
        css_store_background_size_axis(background, false, height);
    }
}

static void resolve_background_position(LayoutContext* lycon, ViewSpan* span,
                                        const CssValue* value, CssPropertyCode property) {
    layout_ensure_background(lycon, span);
    BackgroundProp* background = span->boundary()->background;
    background->bg_position_set = true;
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
        CssBackgroundComponent x = resolve_background_position_component(
            lycon, property, value->data.list.values[0], background->bg_position_x,
            background->bg_position_x_is_percent, true);
        CssBackgroundComponent y = resolve_background_position_component(
            lycon, property, value->data.list.values[1], background->bg_position_y,
            background->bg_position_y_is_percent, false);
        css_store_background_position_axis(background, true, x);
        css_store_background_position_axis(background, false, y);
        return;
    }
    if (value->type == CSS_VALUE_TYPE_LENGTH || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
        CssBackgroundComponent x = resolve_background_position_component(
            lycon, property, value, background->bg_position_x,
            background->bg_position_x_is_percent, true);
        css_store_background_position_axis(background, true, x);
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
            css_store_background_position_axis(background, false, y);
        } else {
            CssBackgroundComponent x = resolve_background_position_component(
                lycon, property, value, background->bg_position_x,
                background->bg_position_x_is_percent, true);
            css_store_background_position_axis(background, true, x);
            background->bg_position_y = 50.0f;
            background->bg_position_y_is_percent = true;
        }
    }
}

static bool css_text_box_trim_value(CssEnum value, uint8_t* trim) {
    if (!trim) return false;
    switch (value) {
        case CSS_VALUE_NONE:
            *trim = 0;
            return true;
        case CSS_VALUE_TRIM_START:
            *trim = TEXT_BOX_TRIM_START;
            return true;
        case CSS_VALUE_TRIM_END:
            *trim = TEXT_BOX_TRIM_END;
            return true;
        case CSS_VALUE_TRIM_BOTH:
        case CSS_VALUE_BOTH:
            *trim = TEXT_BOX_TRIM_START | TEXT_BOX_TRIM_END;
            return true;
        default:
            return false;
    }
}

static bool css_text_box_edge_value(CssEnum value, CssEnum* over, CssEnum* under) {
    if (!over || !under) return false;
    if (value == CSS_VALUE_AUTO || value == CSS_VALUE_TEXT) {
        *over = CSS_VALUE_TEXT;
        *under = CSS_VALUE_TEXT;
        return true;
    }
    if (value == CSS_VALUE_CAP || value == CSS_VALUE_EX) {
        *over = value;
        *under = CSS_VALUE_TEXT;
        return true;
    }
    if (value == CSS_VALUE_ALPHABETIC || value == CSS_VALUE_IDEOGRAPHIC) {
        *over = CSS_VALUE_TEXT;
        *under = value;
        return true;
    }
    return false;
}

static CssEnum css_text_box_edge_keyword(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return CSS_VALUE_TEXT;
    CssEnum over = CSS_VALUE_TEXT;
    CssEnum under = CSS_VALUE_TEXT;
    return css_text_box_edge_value(value->data.keyword, &over, &under)
        ? value->data.keyword : CSS_VALUE_TEXT;
}

static void resolve_text_box_property(ViewBlock* block, const CssValue* value,
                                      bool shorthand) {
    if (!block || !value) return;
    if (!block->blk) return;
    if (!shorthand) {
        if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            uint8_t trim = 0;
            if (css_text_box_trim_value(value->data.keyword, &trim)) {
                block->blk->text_box_trim = trim;
            }
        }
        return;
    }
    uint8_t trim = 0;
    CssEnum over_edge = CSS_VALUE_TEXT;
    CssEnum under_edge = CSS_VALUE_TEXT;
    bool has_trim = false;
    bool has_edge = false;
    int value_count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (value_count > 4) value_count = 4;
    for (int i = 0; i < value_count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) continue;
        uint8_t item_trim = 0;
        if (css_text_box_trim_value(item->data.keyword, &item_trim)) {
            trim = item_trim;
            has_trim = true;
            continue;
        }
        CssEnum item_over = CSS_VALUE_TEXT;
        CssEnum item_under = CSS_VALUE_TEXT;
        if (!css_text_box_edge_value(item->data.keyword, &item_over, &item_under)) {
            continue;
        }
        if (!has_edge) {
            over_edge = item_over;
            under_edge = item_under;
            has_edge = true;
        } else {
            under_edge = item_under;
        }
    }
    if (has_trim) block->blk->text_box_trim = trim;
    if (has_edge) {
        block->blk->text_box_over_edge = over_edge;
        block->blk->text_box_under_edge = under_edge;
    }
}

static void resolve_text_box_edge_property(ViewBlock* block, const CssValue* value) {
    if (!block || !value || !block->blk) return;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum over = CSS_VALUE_TEXT;
        CssEnum under = CSS_VALUE_TEXT;
        if (css_text_box_edge_value(value->data.keyword, &over, &under)) {
            block->blk->text_box_over_edge = over;
            block->blk->text_box_under_edge = under;
        }
    } else if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count >= 2) {
        block->blk->text_box_over_edge = css_text_box_edge_keyword(value->data.list.values[0]);
        block->blk->text_box_under_edge = css_text_box_edge_keyword(value->data.list.values[1]);
    }
}

static bool text_wrap_style_keyword(CssEnum keyword) {
    return keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_BALANCE;
}

static bool resolve_text_wrap_mode_keyword(CssEnum keyword, CssEnum* mode) {
    if (!mode) return false;
    if (keyword == CSS_VALUE_WRAP || keyword == CSS_VALUE_NOWRAP) {
        *mode = keyword;
        return true;
    }
    return false;
}

static bool resolve_text_wrap_value(const CssValue* value, CssEnum* mode,
                                    CssEnum* style) {
    if (!value || !mode || !style) return false;
    *mode = CSS_VALUE_WRAP;
    *style = CSS_VALUE_AUTO;
    bool has_mode = false;
    bool has_style = false;
    int count = value->type == CSS_VALUE_TYPE_LIST ? value->data.list.count : 1;
    if (count < 1 || count > 2) return false;
    for (int i = 0; i < count; i++) {
        const CssValue* item = value->type == CSS_VALUE_TYPE_LIST
            ? value->data.list.values[i] : value;
        if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) return false;
        if (resolve_text_wrap_mode_keyword(item->data.keyword, mode)) {
            if (has_mode) return false;
            has_mode = true;
        } else if (text_wrap_style_keyword(item->data.keyword)) {
            if (has_style) return false;
            *style = item->data.keyword;
            has_style = true;
        } else {
            return false;
        }
    }
    return true;
}

static void resolve_text_wrap_property(LayoutContext* lycon, ViewSpan* span,
                                       CssPropertyCode property,
                                       const CssValue* value) {
    if (!lycon || !span || !value) return;
    BlockProp* target = span->ensure_block(lycon);
    if (!target) return;
    if (property == CSS_PROPERTY_TEXT_WRAP_MODE) {
        if (value->type != CSS_VALUE_TYPE_KEYWORD) return;
        CssEnum mode = value->data.keyword;
        if (mode == CSS_VALUE_INHERIT || mode == CSS_VALUE_UNSET) {
            target->text_wrap_mode = get_text_wrap_mode_value(
                dom_parent_element(lam::dom_require_element(lycon->view)));
        } else if (mode == CSS_VALUE_INITIAL || mode == CSS_VALUE_REVERT) {
            target->text_wrap_mode = CSS_VALUE_WRAP;
        } else if (!resolve_text_wrap_mode_keyword(mode, &target->text_wrap_mode)) {
            target->text_wrap_mode = (CssEnum)0;
        }
        return;
    }
    CssEnum mode = CSS_VALUE_WRAP;
    CssEnum style = CSS_VALUE_AUTO;
    if (!resolve_text_wrap_value(value, &mode, &style)) return;
    target->text_wrap_mode = mode;
    target->text_wrap_style = style;
}

static bool parse_text_autospace_value(const CssValue* value, uint8_t* flags) {
    if (!value || !flags) return false;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        switch (value->data.keyword) {
            case CSS_VALUE_NORMAL:
            case CSS_VALUE_AUTO:
                *flags = TEXT_AUTOSPACE_NORMAL;
                return true;
            case CSS_VALUE_NO_AUTOSPACE:
                *flags = 0;
                return true;
            case CSS_VALUE_IDEOGRAPH_ALPHA:
                *flags = TEXT_AUTOSPACE_IDEOGRAPH_ALPHA;
                return true;
            case CSS_VALUE_IDEOGRAPH_NUMERIC:
                *flags = TEXT_AUTOSPACE_IDEOGRAPH_NUMERIC;
                return true;
            case CSS_VALUE_PUNCTUATION:
                *flags = 0;
                return true;
            default:
                return false;
        }
    }
    if (value->type != CSS_VALUE_TYPE_LIST || value->data.list.count == 0) return false;
    uint8_t parsed = 0;
    bool has_spacing_class = false;
    for (int i = 0; i < value->data.list.count; i++) {
        const CssValue* item = value->data.list.values[i];
        if (!item || item->type != CSS_VALUE_TYPE_KEYWORD) return false;
        switch (item->data.keyword) {
            case CSS_VALUE_IDEOGRAPH_ALPHA:
                parsed |= TEXT_AUTOSPACE_IDEOGRAPH_ALPHA;
                has_spacing_class = true;
                break;
            case CSS_VALUE_IDEOGRAPH_NUMERIC:
                parsed |= TEXT_AUTOSPACE_IDEOGRAPH_NUMERIC;
                has_spacing_class = true;
                break;
            case CSS_VALUE_PUNCTUATION:
                // punctuation spacing is language-specific at this level.
                has_spacing_class = true;
                break;
            case CSS_VALUE_INSERT:
                parsed &= (uint8_t)~TEXT_AUTOSPACE_REPLACE;
                break;
            case CSS_VALUE_REPLACE:
                parsed |= TEXT_AUTOSPACE_REPLACE;
                break;
            default:
                return false;
        }
    }
    if (!has_spacing_class) return false;
    *flags = parsed;
    return true;
}

static uint8_t inherited_text_autospace(ViewSpan* span) {
    DomElement* element = span && span->is_element()
        ? lam::dom_require_element(span) : nullptr;
    DomElement* parent = element ? dom_parent_element(element) : nullptr;
    while (parent) {
        if (parent->blk && parent->block()->text_autospace_is_set) {
            return parent->block()->text_autospace;
        }
        parent = dom_parent_element(parent);
    }
    return TEXT_AUTOSPACE_NORMAL;
}

static void resolve_text_autospace_property(LayoutContext* lycon,
                                             ViewSpan* span,
                                             const CssValue* value) {
    if (!lycon || !span || !value) return;
    BlockProp* target = span->ensure_block(lycon);
    if (!target) return;
    uint8_t flags = TEXT_AUTOSPACE_NORMAL;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        CssEnum keyword = value->data.keyword;
        if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) {
            flags = inherited_text_autospace(span);
        } else if (keyword == CSS_VALUE_INITIAL || keyword == CSS_VALUE_REVERT) {
            flags = TEXT_AUTOSPACE_NORMAL;
        } else if (!parse_text_autospace_value(value, &flags)) {
            return;
        }
    } else if (!parse_text_autospace_value(value, &flags)) {
        return;
    }
    target->text_autospace = flags;
    target->text_autospace_is_set = true;
}

static LayoutShadowValue resolve_shadow_value(LayoutContext* lycon,
                                              CssPropertyCode property,
                                              const CssValue* value,
                                              bool allow_spread_and_inset) {
    LayoutShadowValue shadow = {};
    shadow.color.a = 255;
    int length_count = 0;
    auto apply_value = [&](const CssValue* component) {
        if (!component) return;
        if (component->type == CSS_VALUE_TYPE_KEYWORD) {
            if (allow_spread_and_inset && component->data.keyword == CSS_VALUE_INSET) {
                shadow.inset = true;
            } else {
                shadow.color = color_name_to_rgb(component->data.keyword);
            }
            return;
        }
        if (component->type == CSS_VALUE_TYPE_LENGTH ||
            component->type == CSS_VALUE_TYPE_NUMBER) {
            float resolved = component->type == CSS_VALUE_TYPE_LENGTH
                ? resolve_length_value(lycon, property, component)
                : (float)component->data.number.value;
            if (length_count == 0) shadow.offset_x = resolved;
            else if (length_count == 1) shadow.offset_y = resolved;
            else if (length_count == 2) shadow.blur_radius = resolved;
            else if (allow_spread_and_inset && length_count == 3) shadow.spread_radius = resolved;
            length_count++;
            return;
        }
        if (component->type == CSS_VALUE_TYPE_COLOR ||
            component->type == CSS_VALUE_TYPE_FUNCTION) {
            shadow.color = resolve_color_value(lycon, component);
        }
    };
    if (value && value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++) {
            apply_value(value->data.list.values[i]);
        }
    } else if (allow_spread_and_inset && value &&
               (value->type == CSS_VALUE_TYPE_LENGTH ||
                value->type == CSS_VALUE_TYPE_NUMBER)) {
        apply_value(value);
    }
    return shadow;
}

template <typename ShadowType>
static void append_shadow_value(ShadowType** head, ShadowType** tail, ShadowType* shadow) {
    if (!head || !tail || !shadow) return;
    if (!*head) *head = shadow;
    else (*tail)->next = shadow;
    *tail = shadow;
}

template <typename ShadowType, typename ParseFn>
static ShadowType* resolve_shadow_list(const CssValue* value, ParseFn parse) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST) return nullptr;
    ShadowType* head = nullptr;
    ShadowType* tail = nullptr;
    bool nested = false;
    for (int i = 0; i < value->data.list.count; i++) {
        const CssValue* item = value->data.list.values[i];
        if (item && item->type == CSS_VALUE_TYPE_LIST) {
            nested = true;
            break;
        }
    }
    if (!nested) {
        append_shadow_value(&head, &tail, parse(value));
        return head;
    }
    for (int i = 0; i < value->data.list.count; i++) {
        const CssValue* item = value->data.list.values[i];
        if (item) append_shadow_value(&head, &tail, parse(item));
    }
    return head;
}

static CssEnum find_inherited_block_keyword(DomElement* element,
                                            CssPropertyCode property,
                                            bool check_specified,
                                            bool reject_match_parent,
                                            CssEnum fallback) {
    for (DomElement* parent = dom_parent_element(element); parent;
         parent = dom_parent_element(parent)) {
        CssEnum computed = property == CSS_PROPERTY_TEXT_ALIGN
            ? parent->blk ? parent->block()->text_align : CSS_VALUE__UNDEF
            : parent->blk ? parent->block()->direction : CSS_VALUE__UNDEF;
        if (computed != CSS_VALUE__UNDEF && computed != CSS_VALUE_INHERIT &&
            (!reject_match_parent || computed != CSS_VALUE_MATCH_PARENT)) {
            return computed;
        }
        if (check_specified && parent->specified_style) {
            CssDeclaration* declaration = style_tree_get_declaration(
                parent->specified_style, property);
            if (declaration && declaration->value &&
                declaration->value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum specified = declaration->value->data.keyword;
                if (specified != CSS_VALUE__UNDEF && specified != CSS_VALUE_INHERIT) {
                    return specified;
                }
            }
        }
    }
    return fallback;
}

static CssEnum css_parse_webkit_text_align(const CssValue* value) {
    if (!value || value->type != CSS_VALUE_TYPE_CUSTOM ||
        !value->data.custom_property.name) {
        return CSS_VALUE__UNDEF;
    }
    const char* name = value->data.custom_property.name;
    size_t length = strlen(name);
    if (str_ieq_const(name, length, "-webkit-left")) return CSS_VALUE_LEFT;
    if (str_ieq_const(name, length, "-webkit-center")) return CSS_VALUE_CENTER;
    if (str_ieq_const(name, length, "-webkit-right")) return CSS_VALUE_RIGHT;
    return CSS_VALUE__UNDEF;
}

static CssEnum logical_inline_direction(DomElement* element) {
    for (DomElement* parent = dom_parent_element(element); parent;
         parent = dom_parent_element(parent)) {
        CssEnum specified = layout_specified_keyword(
            parent, CSS_PROPERTY_DIRECTION, CSS_VALUE__UNDEF);
        if (specified == CSS_VALUE_LTR || specified == CSS_VALUE_RTL) return specified;
        if (parent->blk && (parent->block()->direction == CSS_VALUE_LTR ||
                            parent->block()->direction == CSS_VALUE_RTL)) {
            return parent->block()->direction;
        }
    }
    return CSS_VALUE_LTR;
}

static CssPropertyCode css_physical_size_alias(CssPropertyCode property,
                                                bool vertical_inline_axis) {
    static const CssPropertyCode width[] = {
        CSS_PROPERTY_WIDTH, CSS_PROPERTY_MIN_WIDTH, CSS_PROPERTY_MAX_WIDTH
    };
    static const CssPropertyCode height[] = {
        CSS_PROPERTY_HEIGHT, CSS_PROPERTY_MIN_HEIGHT, CSS_PROPERTY_MAX_HEIGHT
    };
    switch (property) {
        case CSS_PROPERTY_INLINE_SIZE: return vertical_inline_axis ? height[0] : width[0];
        case CSS_PROPERTY_BLOCK_SIZE: return vertical_inline_axis ? width[0] : height[0];
        case CSS_PROPERTY_MIN_INLINE_SIZE: return vertical_inline_axis ? height[1] : width[1];
        case CSS_PROPERTY_MAX_INLINE_SIZE: return vertical_inline_axis ? height[2] : width[2];
        case CSS_PROPERTY_MIN_BLOCK_SIZE: return vertical_inline_axis ? width[1] : height[1];
        case CSS_PROPERTY_MAX_BLOCK_SIZE: return vertical_inline_axis ? width[2] : height[2];
        default: return property;
    }
}

void resolve_css_property(CssPropertyCode prop_id, const CssDeclaration* decl, LayoutContext* lycon) {
    if (!decl || !lycon || !lycon->view) {
        return;
    }
    const CssValue* value = decl->value;
    if (!value) { log_debug("No value in declaration");  return; }
    int64_t specificity = get_cascade_priority(decl);
    if (decl->property_name && decl->property_name[0] == '-' && decl->property_name[1] == '-') {
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
    DomElement* current_element = lycon->view && lycon->view->is_element()
        ? lycon->view->as_element() : nullptr;
    bool inline_axis_is_vertical = layout_element_inline_axis_is_vertical(current_element);
    WritingMode current_writing_mode = layout_element_writing_mode(current_element);
    bool vertical_block_start_is_right = current_writing_mode == WM_VERTICAL_RL;
    prop_id = css_physical_size_alias(prop_id, inline_axis_is_vertical);
    ViewSpan* span = lam::view_require_element(lycon->view);
    ViewBlock* block = lam::view_as_block(span);
    CssEnum specified_direction = layout_specified_keyword(
        current_element, CSS_PROPERTY_DIRECTION, CSS_VALUE__UNDEF);
    bool inline_direction_rtl = specified_direction == CSS_VALUE_RTL ||
        (specified_direction != CSS_VALUE_LTR &&
         logical_inline_direction(current_element) == CSS_VALUE_RTL);
    if (css_property_is_ignored(prop_id) ||
        resolve_common_keyword_property(lycon, span, block, prop_id, decl, value)) {
        return;
    }
    if (prop_id == CSS_PROPERTY_TEXT_AUTOSPACE) {
        resolve_text_autospace_property(lycon, span, value);
        return;
    }
    if (prop_id >= CSS_PROPERTY_BORDER_TOP_WIDTH &&
        prop_id <= CSS_PROPERTY_BORDER_LEFT_COLOR) {
        resolve_border_physical_longhand(lycon, span, prop_id, value, specificity);
        return;
    }
    if (resolve_spacing_property(lycon, span, prop_id, value, specificity,
                                 inline_axis_is_vertical,
                                 vertical_block_start_is_right,
                                 inline_direction_rtl)) {
        return;
    }
    switch (prop_id) {
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
        case CSS_PROPERTY_FONT: {
            span->ensure_font(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                inherit_font_shorthand(lycon, span);
                break;
            }
            span->font->font_variant = CSS_VALUE_NORMAL;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const CssEnumInfo* info = css_enum_info(value->data.keyword);
                if (info && info->group == CSS_VALUE_GROUP_SYSTEM_FONT) {
                    radiant_retain_font_family(span->font, lam::GcPtr<char>((char*)"Arial"));
                    span->font->font_size = 13.333f;
                    span->font->font_size_from_medium = false;
                    span->font->font_weight = CSS_VALUE_NORMAL;
                    span->font->font_weight_numeric = 400;
                    span->font->font_style = CSS_VALUE_NORMAL;
                    span->font->font_variant = CSS_VALUE_NORMAL;
                    span->ensure_block(lycon);
                    span->blk->line_height = nullptr;
                    break;
                }
                break;
            }
            LayoutFontShorthandParts parts;
            if (layout_parse_font_shorthand(value, &parts)) {
                span->font->font_variant = parts.small_caps
                    ? CSS_VALUE_SMALL_CAPS : CSS_VALUE_NORMAL;
                const char* font_family_name = css_select_font_shorthand_family(
                    lycon, value, parts.group, parts.family_start, true);
                if (parts.size) {
                    LayoutFontSizeResult resolved = layout_resolve_font_size_value(
                        lycon, parts.size, lycon->font.style, true);
                    if (!isnan(resolved.value) && resolved.value > 0.0f) {
                        span->font->font_size = resolved.value;
                        span->font->font_size_from_medium = resolved.from_medium;
                    }
                }
                if (parts.size && font_family_name) {
                    span->ensure_block(lycon);
                    span->blk->line_height = parts.line_height
                        ? parts.line_height
                        : css_value_create_keyword(lycon->doc->view_tree->prop_pool, "normal");
                }
                if (font_family_name) {
                    radiant_retain_font_family(span->font,
                        lam::PoolPtr<char>((char*)font_family_name));
                }
                span->font->font_weight = parts.weight
                    ? map_font_weight(parts.weight) : CSS_VALUE_NORMAL;
                span->font->font_weight_numeric = parts.weight
                    ? map_font_weight_numeric(parts.weight) : 400;
                span->font->font_style = parts.style
                    ? parts.style->data.keyword : CSS_VALUE_NORMAL;
            }
            break;
        }
        case CSS_PROPERTY_FONT_SIZE: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl)) break;
            span->ensure_font(lycon);
            LayoutFontSizeResult resolved = layout_resolve_font_size_value(
                lycon, value, lycon->font.style, true);
            float font_size = resolved.value;
            bool valid = !isnan(font_size) && font_size >= 0.0f;
            if (valid) {
                span->font->font_size = font_size;
                span->font->font_size_from_medium = resolved.from_medium;
            }
            break;
        }
        case CSS_PROPERTY_FONT_WEIGHT: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl)) break;
            span->ensure_font(lycon);
            span->font->font_weight = map_font_weight(value);
            span->font->font_weight_numeric = map_font_weight_numeric(value);
            break;
        }
        case CSS_PROPERTY_FONT_FAMILY: {
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_FONT, decl)) break;
            span->ensure_font(lycon);


            if (value->type == CSS_VALUE_TYPE_STRING) {
                radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)value->data.string));
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                radiant_retain_font_family(span->font, lam::PoolPtr<char>((char*)value->data.custom_property.name));
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_INHERIT) {
                    const FontProp* parent_font = lycon->font.style;
                    if (parent_font && parent_font->family) {
                        radiant_retain_font_family(span->font, lam::PoolPtr<char>(parent_font->family));
                    } else {
                        radiant_clear_font_family(span->font);
                    }
                } else {
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
                    lycon, CSS_PROPERTY_FONT, decl)) break;
            span->ensure_block(lycon);
            span->blk->line_height = value;
            break;
        }
        case CSS_PROPERTY_TEXT_ALIGN: {
            if (!block) break;
            block->ensure_block(lycon);
            CssEnum webkit_legacy_align = css_parse_webkit_text_align(value);
            if (webkit_legacy_align != CSS_VALUE__UNDEF) {
                // WebKit compatibility values align block descendants unless a
                // child supplies an explicit non-normal justify-self value.
                block->blk->text_align = webkit_legacy_align;
                block->blk->legacy_block_align = webkit_legacy_align;
                block->blk->legacy_align_center_blocks =
                    webkit_legacy_align == CSS_VALUE_CENTER;
                break;
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum align_value = value->data.keyword;
                if (align_value == CSS_VALUE_INHERIT) {
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    block->blk->text_align = find_inherited_block_keyword(
                        dom_elem, CSS_PROPERTY_TEXT_ALIGN, true, false, CSS_VALUE_START);
                }
                else if (align_value == CSS_VALUE_MATCH_PARENT) {
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    block->blk->text_align = find_inherited_block_keyword(
                        dom_elem, CSS_PROPERTY_TEXT_ALIGN, false, true, CSS_VALUE_START);
                }
                else if (align_value != CSS_VALUE__UNDEF) {
                    block->blk->text_align = align_value;
                }
            }
            break;
        }
        case CSS_PROPERTY_TEXT_WRAP:
        case CSS_PROPERTY_TEXT_WRAP_MODE:
            resolve_text_wrap_property(lycon, span, prop_id, value);
            break;
        case CSS_PROPERTY_DIRECTION: {
            if (!block) {
                ViewSpan* span = lycon->view->is_element() ? lam::view_require_element(lycon->view) : nullptr;
                if (span) {
                    // CSS direction is inherited by inline boxes; allocate the
                    // shared block property before storing an explicit value.
                    span->ensure_block(lycon);
                }
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
            block->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum dir_value = value->data.keyword;
                if (dir_value == CSS_VALUE_INHERIT) {
                    DomElement* dom_elem = lam::dom_require_element(lycon->view);
                    block->blk->direction = find_inherited_block_keyword(
                        dom_elem, CSS_PROPERTY_DIRECTION, false, false, CSS_VALUE_LTR);
                }
                else if (dir_value == CSS_VALUE_LTR || dir_value == CSS_VALUE_RTL) {
                    block->blk->direction = dir_value;
                }
            }
            break;
        }
        case CSS_PROPERTY_UNICODE_BIDI: {
            BlockProp* target = block ? block->ensure_block(lycon) : nullptr;
            if (!target && lycon->view->is_element()) {
                ViewSpan* inline_span = lam::view_require_element(lycon->view);
                target = inline_span ? inline_span->ensure_block(lycon) : nullptr;
            }
            if (!target || value->type != CSS_VALUE_TYPE_KEYWORD) break;
            CssEnum bidi_value = value->data.keyword;
            if (bidi_value == CSS_VALUE_INHERIT) {
                DomElement* dom_elem = lam::dom_require_element(lycon->view);
                target->unicode_bidi = find_inherited_block_keyword(
                    dom_elem, CSS_PROPERTY_UNICODE_BIDI, false, false,
                    CSS_VALUE_NORMAL);
            } else if (bidi_value == CSS_VALUE_NORMAL ||
                       bidi_value == CSS_VALUE_EMBED ||
                       bidi_value == CSS_VALUE_ISOLATE ||
                       bidi_value == CSS_VALUE_BIDI_OVERRIDE ||
                       bidi_value == CSS_VALUE_ISOLATE_OVERRIDE ||
                       bidi_value == CSS_VALUE_PLAINTEXT) {
                target->unicode_bidi = bidi_value;
            }
            break;
        }
        case CSS_PROPERTY_TEXT_INDENT: {
            if (!block) break;
            block->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float indent = resolve_length_value(lycon, CSS_PROPERTY_TEXT_INDENT, value);
                if (indent > MAX_LAYOUT_DIMENSION) indent = MAX_LAYOUT_DIMENSION;
                else if (indent < -MAX_LAYOUT_DIMENSION) indent = -MAX_LAYOUT_DIMENSION;
                block->blk->text_indent = indent;
                block->blk->text_indent_percent = NAN;
            }
            else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float percent = value->data.percentage.value;
                block->blk->text_indent = 0;
                block->blk->text_indent_percent = percent;
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_INHERIT) {
                DomElement* dom_elem = lam::dom_require_element(lycon->view);
                DomElement* parent = dom_parent_element(dom_elem);
                if (parent && parent->blk) {
                    block->blk->text_indent = parent->blk->text_indent;
                    block->blk->text_indent_percent = parent->blk->text_indent_percent;
                    block->blk->text_indent_calc = parent->blk->text_indent_calc;
                }
            }
            else if (value->type == CSS_VALUE_TYPE_FUNCTION) {
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
                    DomElement* parent = lycon->elmt->parent ? lycon->elmt->parent->as_element() : nullptr;
                    ViewBlock* parent_view = lam::view_as_block(parent);
                    if (parent_view && parent_view->in_line) {
                        span->in_line->vertical_align = parent_view->in_line->vertical_align;
                        span->in_line->vertical_align_offset = parent_view->in_line->vertical_align_offset;
                    } else {
                        span->in_line->vertical_align = CSS_VALUE_BASELINE;
                        span->in_line->vertical_align_offset = 0;
                    }
                } else if (valign_value != CSS_VALUE__UNDEF) {
                    span->in_line->vertical_align = valign_value;
                    span->in_line->vertical_align_offset = 0;
                }
            } else if (value->type == CSS_VALUE_TYPE_LENGTH) {
                float offset = resolve_length_value(lycon, CSS_PROPERTY_VERTICAL_ALIGN, value);
                span->in_line->vertical_align = CSS_VALUE_BASELINE;
                span->in_line->vertical_align_offset = offset;
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                float line_height = 0;
                if (span->blk && span->block_mut()->line_height) {
                    line_height = resolve_length_value(lycon, CSS_PROPERTY_LINE_HEIGHT, span->block()->line_height);
                }
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
        case CSS_PROPERTY_ZOOM: {
            if (!block) break;
            block->ensure_block(lycon);
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
        case CSS_PROPERTY_HEIGHT:
            resolve_css_axis_size(lycon, block, value,
                prop_id == CSS_PROPERTY_WIDTH ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
            break;
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT: {
            if (block) apply_dimension_constraint(lycon, block, prop_id, value);
            break;
        }
        case CSS_PROPERTY_TEXT_BOX:
        case CSS_PROPERTY_TEXT_BOX_TRIM: {
            if (!block) break;
            block->ensure_block(lycon);
            resolve_text_box_property(block, value, prop_id == CSS_PROPERTY_TEXT_BOX);
            break;
        }
        case CSS_PROPERTY_TEXT_BOX_EDGE: {
            if (!block) break;
            block->ensure_block(lycon);
            resolve_text_box_edge_property(block, value);
            break;
        }
        case CSS_PROPERTY_BACKGROUND_COLOR: {
            // preserve the shorthand's higher cascade priority across that boundary.
            if (shorthand_overrides_longhand(
                    lycon, CSS_PROPERTY_BACKGROUND, decl)) break;
            layout_ensure_background(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                span->boundary_mut()->background->color = inherit_background_color(lycon);
                break;
            }
            span->boundary_mut()->background->color = resolve_color_value(lycon, value);
            break;
        }
        case CSS_PROPERTY_BACKGROUND_IMAGE: {
            layout_ensure_background(lycon, span);
            static const char* const gradient_names[] = {
                "linear-gradient", "repeating-linear-gradient", "radial-gradient",
                "repeating-radial-gradient", "conic-gradient"
            };
            const CssValue* gradient = css_find_background_function(
                value, gradient_names, 5);
            const char* url = css_background_url_value(value);
            if (gradient) {
                resolve_css_property(CSS_PROPERTY_BACKGROUND, decl, lycon);
            } else if (url) {
                char* image_path = resolve_css_resource_url(lycon, decl, url);
                if (image_path) {
                    radiant_retain_background_image(
                        span->boundary()->background, lam::PoolPtr<char>(image_path));
                }
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                       value->data.keyword == CSS_VALUE_NONE) {
                radiant_clear_background_image(span->boundary()->background);
            }
            break;
        }
        case CSS_PROPERTY_MASK_IMAGE:
            resolve_css_mask_image(lycon, span, value);
            break;
        case CSS_PROPERTY_BACKGROUND_ATTACHMENT:
        case CSS_PROPERTY_BACKGROUND_ORIGIN:
        case CSS_PROPERTY_BACKGROUND_CLIP:
        case CSS_PROPERTY_BACKGROUND_BLEND_MODE:
            resolve_background_keyword_property(lycon, span, prop_id, value);
            break;
        case CSS_PROPERTY_BACKGROUND_POSITION_X:
        case CSS_PROPERTY_BACKGROUND_POSITION_Y: {
            layout_ensure_background(lycon, span);
            resolve_background_position_axis(
                lycon, prop_id, value, span->boundary()->background,
                prop_id == CSS_PROPERTY_BACKGROUND_POSITION_X);
            break;
        }
        case CSS_PROPERTY_BACKGROUND_SIZE:
            resolve_background_size(lycon, span, value, prop_id);
            break;
        case CSS_PROPERTY_BACKGROUND_REPEAT: {
            layout_ensure_background(lycon, span);
            resolve_background_repeat_property(span, value);
            break;
        }
        case CSS_PROPERTY_BACKGROUND_POSITION:
            resolve_background_position(lycon, span, value, prop_id);
            break;
        case CSS_PROPERTY_BOX_SHADOW: {
            span->ensure_boundary(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->bound->box_shadow = nullptr;
                break;
            }
            BoxShadow* shadow_list_head = nullptr;
            auto parse_single_shadow = [&](const CssValue* shadow_value) -> BoxShadow* {
                BoxShadow* shadow = (BoxShadow*)alloc_prop(lycon, sizeof(BoxShadow));
                LayoutShadowValue values = resolve_shadow_value(
                    lycon, prop_id, shadow_value, true);
                shadow->offset_x = values.offset_x;
                shadow->offset_y = values.offset_y;
                shadow->blur_radius = values.blur_radius;
                shadow->spread_radius = values.spread_radius;
                shadow->color = values.color;
                shadow->inset = values.inset;
                return shadow;
            };
            if (value->type == CSS_VALUE_TYPE_LIST) {
                shadow_list_head = resolve_shadow_list<BoxShadow>(value, parse_single_shadow);
            }
            span->bound->box_shadow = shadow_list_head;
            break;
        }
        case CSS_PROPERTY_TRANSFORM: {
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->transform = nullptr;
                break;
            }
            span->ensure_transform(lycon);
            TransformFunction* func_list_head = nullptr;
            TransformFunction* func_list_tail = nullptr;
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                append_transform_function(&func_list_head, &func_list_tail,
                                          resolve_transform_function(lycon, prop_id, value));
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                const CssValue* list = value;
                for (int i = 0; i < list->data.list.count; i++) {
                    const CssValue* item = list->data.list.values[i];
                    append_transform_function(&func_list_head, &func_list_tail,
                                              resolve_transform_function(lycon, prop_id, item));
                }
            }
            span->transform->functions = func_list_head;
            break;
        }
        case CSS_PROPERTY_TRANSFORM_ORIGIN: {
            span->ensure_transform(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                resolve_origin_keyword(value->data.keyword, 0,
                                       &span->transform->origin_x,
                                       &span->transform->origin_x_percent,
                                       &span->transform->origin_y,
                                       &span->transform->origin_y_percent);
            } else if (value->type == CSS_VALUE_TYPE_LIST) {
                resolve_origin_list(lycon, prop_id, value, false, true,
                                    &span->transform->origin_x,
                                    &span->transform->origin_x_percent,
                                    &span->transform->origin_y,
                                    &span->transform->origin_y_percent,
                                    &span->transform->origin_z);
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
            span->ensure_transform(lycon);
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
            span->ensure_transform(lycon);
            if (value->type == CSS_VALUE_TYPE_LIST) {
                resolve_origin_list(lycon, CSS_PROPERTY_PERSPECTIVE_ORIGIN, value,
                                    true, false,
                                    &span->transform->perspective_origin_x, nullptr,
                                    &span->transform->perspective_origin_y, nullptr,
                                    nullptr);
            } else if (value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                span->transform->perspective_origin_x = (float)value->data.percentage.value;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_CENTER) {
                span->transform->perspective_origin_x = 50.0f;
            }
            break;
        }
        case CSS_PROPERTY_FILTER:
        case CSS_PROPERTY_BACKDROP_FILTER: {
            bool is_backdrop_filter = prop_id == CSS_PROPERTY_BACKDROP_FILTER;
            FilterProp** target_filter = is_backdrop_filter
                ? span->backdrop_filter_slot()
                : &span->filter;
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                *target_filter = nullptr;
                break;
            }
            if (value->type == CSS_VALUE_TYPE_FUNCTION) {
                *target_filter = is_backdrop_filter
                    ? (FilterProp*)alloc_prop(lycon, sizeof(FilterProp))
                    : span->ensure_filter(lycon);
                (*target_filter)->functions = resolve_filter_function(lycon, prop_id,
                                                                       value->data.function);
                break;
            }
            if (value->type == CSS_VALUE_TYPE_LIST) {
                *target_filter = is_backdrop_filter
                    ? (FilterProp*)alloc_prop(lycon, sizeof(FilterProp))
                    : span->ensure_filter(lycon);
                (*target_filter)->functions = nullptr;
                FilterFunction* tail = nullptr;
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (item && item->type == CSS_VALUE_TYPE_FUNCTION)
                        append_filter_function(&(*target_filter)->functions, &tail,
                                               resolve_filter_function(lycon, prop_id,
                                                                       item->data.function));
                }
            }
            break;
        }
        case CSS_PROPERTY_COLUMN_COUNT:
            resolve_multicol_count(lycon, block, value);
            break;
        case CSS_PROPERTY_COLUMN_WIDTH: {
            if (block) resolve_multicol_dimension(lycon, block, value, prop_id,
                false, false, "column-width");
            break;
        }
        case CSS_PROPERTY_COLUMNS: {
            if (!block) {
                break;
            }
            block->ensure_multicol(lycon);
            MultiColumnProp* multicol = block->multicol_prop();
            multicol->column_count = 0;
            multicol->column_width = 0;
            multicol->column_height = 0;
            multicol->column_height_is_specified = false;
            multicol->wrap = COLUMN_WRAP_AUTO;

            const CssValue* values[8] = {};
            int value_count = 1;
            if (value->type == CSS_VALUE_TYPE_LIST) {
                value_count = min(value->data.list.count, 8);
                for (int vi = 0; vi < value_count; vi++) {
                    values[vi] = value->data.list.values[vi];
                }
            } else {
                values[0] = value;
            }

            int slash_index = -1;
            for (int vi = 0; vi < value_count; vi++) {
                if (css_value_is_slash(values[vi])) {
                    slash_index = vi;
                    break;
                }
            }
            int inline_value_count = slash_index >= 0 ? slash_index : value_count;
            if (inline_value_count > 2) inline_value_count = 2;
            for (int vi = 0; vi < inline_value_count; vi++) {
                const CssValue* v = values[vi];
                if (!v || (v->type == CSS_VALUE_TYPE_KEYWORD &&
                           v->data.keyword == CSS_VALUE_AUTO)) {
                    continue;
                }
                if (v->type == CSS_VALUE_TYPE_NUMBER) {
                    int count = (int)v->data.number.value; // INT_CAST_OK: column count
                    if (v->data.number.value == (double)count && count > 0) {
                        multicol->column_count = count;
                    }
                } else if (v->type == CSS_VALUE_TYPE_LENGTH) {
                    float width = resolve_length_value(lycon, prop_id, v);
                    if (width > 0.0f) multicol->column_width = width;
                }
            }
            if (slash_index >= 0 && slash_index + 1 < value_count) {
                const CssValue* height = values[slash_index + 1];
                if (height && height->type == CSS_VALUE_TYPE_KEYWORD &&
                    height->data.keyword == CSS_VALUE_AUTO) {
                    multicol->column_height_is_specified = false;
                } else if (height && (height->type == CSS_VALUE_TYPE_LENGTH ||
                                      (height->type == CSS_VALUE_TYPE_NUMBER &&
                                       height->data.number.value == 0.0))) {
                    float resolved_height = resolve_length_value(lycon, prop_id, height);
                    if (resolved_height >= 0.0f) {
                        multicol->column_height = resolved_height;
                        multicol->column_height_is_specified = true;
                    }
                }
            }
            break;
        }
        case CSS_PROPERTY_COLUMN_RULE: {
            if (!block) break;
            block->ensure_multicol(lycon);
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
        case CSS_PROPERTY_COLUMN_RULE_WIDTH:
        case CSS_PROPERTY_COLUMN_RULE_STYLE:
        case CSS_PROPERTY_COLUMN_RULE_COLOR:
            resolve_multicol_rule_property(lycon, block, prop_id, value);
            break;
        case CSS_PROPERTY_COLUMN_SPAN: {
            if (!block) break;
            block->ensure_multicol(lycon);
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
        case CSS_PROPERTY_BREAK_INSIDE:
        case CSS_PROPERTY_PAGE_BREAK_INSIDE:
        case CSS_PROPERTY_ORPHANS:
        case CSS_PROPERTY_WIDOWS:
            resolve_flow_break_property(lycon, block, prop_id, value);
            break;
        case CSS_PROPERTY_BOX_DECORATION_BREAK: {
            if (!block) break;
            block->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum kw = value->data.keyword;
                block->blk->box_decoration_break =
                    (kw == CSS_VALUE_CLONE) ? CSS_VALUE_CLONE : CSS_VALUE_SLICE;
            }
            break;
        }
        case CSS_PROPERTY_COLUMN_FILL: {
            if (!block) break;
            block->ensure_multicol(lycon);
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
            block->ensure_multicol(lycon);
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
        case CSS_PROPERTY_BORDER_IMAGE_SOURCE: {
            layout_ensure_border(lycon, span);
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
            layout_ensure_border(lycon, span);
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
            layout_ensure_border(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                span->boundary_mut()->border->border_image_repeat = value->data.keyword;
            }
            break;
        }
        case CSS_PROPERTY_BORDER: {
            layout_ensure_border(lycon, span);
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
            apply_border_side_shorthand(lycon, span, radiant_css_box_side(prop_id), value, specificity);
            break;
        case CSS_PROPERTY_BORDER_INLINE:
        case CSS_PROPERTY_BORDER_BLOCK:
        case CSS_PROPERTY_BORDER_INLINE_START:
        case CSS_PROPERTY_BORDER_INLINE_END:
        case CSS_PROPERTY_BORDER_BLOCK_START:
        case CSS_PROPERTY_BORDER_BLOCK_END: {
            LayoutLogicalProperty logical = layout_logical_property(prop_id);
            LayoutPhysicalSides physical = layout_logical_physical_sides(
                logical, layout_logical_sides(inline_axis_is_vertical,
                                              vertical_block_start_is_right,
                                              inline_direction_rtl), true);
            apply_border_side_shorthand(lycon, span, physical.first, value, specificity);
            if (physical.pair) {
                apply_border_side_shorthand(lycon, span, physical.second, value, specificity);
            }
            break;
        }
        case CSS_PROPERTY_BORDER_BLOCK_END_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_START_COLOR:
        case CSS_PROPERTY_BORDER_BLOCK_END_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_START_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_WIDTH:
        case CSS_PROPERTY_BORDER_BLOCK_COLOR: {
            LayoutLogicalProperty logical = layout_logical_property(prop_id);
            LayoutPhysicalSides physical = layout_logical_physical_sides(
                logical, layout_logical_sides(inline_axis_is_vertical,
                                              vertical_block_start_is_right,
                                              inline_direction_rtl), true);
            CssBorderSidePart part = prop_id == CSS_PROPERTY_BORDER_BLOCK_COLOR ||
                prop_id == CSS_PROPERTY_BORDER_BLOCK_START_COLOR ||
                prop_id == CSS_PROPERTY_BORDER_BLOCK_END_COLOR
                ? CSS_BORDER_SIDE_COLOR : CSS_BORDER_SIDE_WIDTH;
            resolve_border_side_part(lycon, span,
                radiant_inset_property(physical.first), value, specificity, part);
            if (physical.pair) {
                resolve_border_side_part(lycon, span,
                    radiant_inset_property(physical.second), value, specificity, part);
            }
            break;
        }
        case CSS_PROPERTY_BORDER_STYLE:
            resolve_border_box_part(lycon, span, value, specificity, CSS_BORDER_SIDE_STYLE);
            break;
        case CSS_PROPERTY_BORDER_WIDTH: {
            layout_ensure_border(lycon, span);
            resolve_spacing_prop(lycon, CSS_PROPERTY_BORDER_WIDTH, value, specificity, &span->boundary_mut()->border->width);
            break;
        }
        case CSS_PROPERTY_BORDER_COLOR:
            resolve_border_box_part(lycon, span, value, specificity, CSS_BORDER_SIDE_COLOR);
            break;
        case CSS_PROPERTY_BORDER_RADIUS: {
            layout_ensure_border(lycon, span);
            apply_border_radius_shorthand(lycon, prop_id, &span->boundary_mut()->border->radius, value, specificity);
            break;
        }
        case CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS:
        case CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS:
        case CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS:
        case CSS_PROPERTY_BORDER_BOTTOM_LEFT_RADIUS: {
            layout_ensure_border(lycon, span);
            int corner = prop_id == CSS_PROPERTY_BORDER_TOP_LEFT_RADIUS ? 0
                : prop_id == CSS_PROPERTY_BORDER_TOP_RIGHT_RADIUS ? 1
                : prop_id == CSS_PROPERTY_BORDER_BOTTOM_RIGHT_RADIUS ? 2 : 3;
            apply_corner_radius_value(lycon, prop_id,
                                      &span->boundary_mut()->border->radius,
                                      corner, value, specificity);
            break;
        }
        case CSS_PROPERTY_POSITION: {
            ensure_span_position(lycon, span);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                if (val == CSS_VALUE_INHERIT) {
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
        case CSS_PROPERTY_INSET:
            resolve_inset_shorthand(lycon, span, value);
            break;
        case CSS_PROPERTY_INSET_INLINE:
        case CSS_PROPERTY_INSET_INLINE_START:
        case CSS_PROPERTY_INSET_INLINE_END:
        case CSS_PROPERTY_INSET_BLOCK:
        case CSS_PROPERTY_INSET_BLOCK_START:
        case CSS_PROPERTY_INSET_BLOCK_END: {
            resolve_logical_inset_property(lycon, span, prop_id, value,
                                           inline_axis_is_vertical,
                                           vertical_block_start_is_right,
                                           inline_direction_rtl);
            break;
        }
        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_LEFT:
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_BOTTOM:
            resolve_inset_sides(lycon, span, radiant_css_box_side(prop_id), radiant_css_box_side(prop_id),
                                prop_id, value, true);
            break;
        case CSS_PROPERTY_Z_INDEX: {
            ensure_span_position(lycon, span);
            if (value->type == CSS_VALUE_TYPE_NUMBER) {
                int z = (int)value->data.number.value;
                span->position->z_index = z;
            } else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
            span->position->z_index = 0;
            }
            break;
        }
        case CSS_PROPERTY_FLOAT:
        case CSS_PROPERTY_CLEAR:
            resolve_float_clear_property(lycon, block, prop_id, value);
            break;
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
            if (!block->scroller->scrollbar_gutter_stable) {
                block->scroller->scrollbar_gutter_both_edges = false;
            }
            break;
        }
        case CSS_PROPERTY_APPEARANCE: {
            // role-tagged property storage is shared by tables and form controls;
            // reading the raw union would let table appearance write past TableProp.
            FormControlProp* form = block ? block->form_control() : nullptr;
            if (!form || !value ||
                value->type != CSS_VALUE_TYPE_KEYWORD) {
                break;
            }
            CssEnum appearance = value->data.keyword;
            form->appearance_none = appearance == CSS_VALUE_NONE;
            form->appearance_base_select = appearance == CSS_VALUE_BASE_SELECT;
            break;
        }
        case CSS_PROPERTY_VISIBILITY:
        case CSS_PROPERTY_OPACITY:
            resolve_inline_visibility_opacity(lycon, span, prop_id, value);
            break;
        case CSS_PROPERTY_BOX_SIZING: {
            if (!block) break;
            block->ensure_block(lycon);
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
            if (!span) break;
            if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
                break;
            }
            if (!span->fi) { alloc_flex_item_prop(lycon, span); }
            if (!span->fi) break;
            span->fi->aspect_ratio = layout_aspect_ratio_value(value);
            break;
        }
        case CSS_PROPERTY_LINE_CLAMP:
        case CSS_PROPERTY_WEBKIT_LINE_CLAMP:
            resolve_line_count_property(lycon, block, prop_id, value);
            break;
        case CSS_PROPERTY_TAB_SIZE: {
            span->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_NUMBER && value->data.number.value >= 0.0) {
                span->blk->tab_size = (int)value->data.number.value; // INT_CAST_OK: tab-size is a count.
            }
            break;
        }
        case CSS_PROPERTY_LETTER_SPACING:
        case CSS_PROPERTY_WORD_SPACING:
            resolve_font_spacing_property(lycon, span, prop_id, value);
            break;
        case CSS_PROPERTY_HYPHENATE_CHARACTER: {
            span->ensure_block(lycon);
            const CssValue* resolved = resolve_var_function(lycon, value);
            if (!resolved) break;
            if (resolved->type == CSS_VALUE_TYPE_STRING) {
                css_store_hyphenate_character(lycon, span, resolved->data.string);
            } else if (resolved->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum keyword = resolved->data.keyword;
                if (keyword == CSS_VALUE_AUTO || keyword == CSS_VALUE_INITIAL ||
                    keyword == CSS_VALUE_UNSET || keyword == CSS_VALUE_REVERT) {
                    span->blk->hyphenate_character = nullptr;
                } else if (keyword == CSS_VALUE_INHERIT) {
                    DomElement* parent = dom_parent_element(
                        lam::dom_require<DOM_NODE_ELEMENT>(lycon->view));
                    span->blk->hyphenate_character = parent && parent->blk
                        ? parent->block()->hyphenate_character : nullptr;
                }
            }
            break;
        }
        case CSS_PROPERTY_TEXT_SHADOW: {
            if (!span->font) {
                break;
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                span->font->text_shadow = nullptr;
                break;
            }
            auto parse_single_text_shadow = [&](const CssValue* sv) -> TextShadow* {
                TextShadow* ts = (TextShadow*)alloc_prop(lycon, sizeof(TextShadow));
                LayoutShadowValue values = resolve_shadow_value(
                    lycon, prop_id, sv, false);
                ts->offset_x = values.offset_x;
                ts->offset_y = values.offset_y;
                ts->blur_radius = values.blur_radius;
                ts->color = values.color;
                return ts;
            };
            TextShadow* ts_head = resolve_shadow_list<TextShadow>(value, parse_single_text_shadow);
            span->font->text_shadow = ts_head;
            break;
        }
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
        case CSS_PROPERTY_ALIGN_CONTENT:
            resolve_flex_grid_container_alignment(lycon, block, prop_id, value);
            break;
        case CSS_PROPERTY_PLACE_CONTENT: {
            CssSelfAlignment align_val = {};
            CssSelfAlignment justify_val = {};
            if (!css_parse_place_content_alignment(value, &align_val, &justify_val)) break;
            css_store_content_alignment(lycon, block, false, align_val);
            css_store_content_alignment(lycon, block, true, justify_val);
            break;
        }
        case CSS_PROPERTY_GRID_ROW_GAP:
        case CSS_PROPERTY_ROW_GAP:
            resolve_gap_property(lycon, block, prop_id, value, true);
            break;
        case CSS_PROPERTY_GRID_COLUMN_GAP:
        case CSS_PROPERTY_COLUMN_GAP:
            resolve_gap_property(lycon, block, prop_id, value, false);
            break;
        case CSS_PROPERTY_WRITING_MODE: {
            if (!block) break;
            BlockProp* block_prop = block->ensure_block(lycon);
            if (!block_prop) break;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum val = value->data.keyword;
                WritingMode mode = layout_writing_mode_from_css(val);
                block_prop->writing_mode = mode;
                if (block->embed && block->embedp()->flex) {
                    block->embedp()->flex->writing_mode = mode;
                }
            }
            break;
        }
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
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) {
                clear_grid_template_areas(grid);
                break;
            }
            if (value->type == CSS_VALUE_TYPE_STRING) {
                parse_grid_template_areas(grid, value->data.string, &lycon->scratch);
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                size_t total_len = 0;
                for (int i = 0; i < value->data.list.count; i++) {
                    if (value->data.list.values[i]->type == CSS_VALUE_TYPE_STRING) {
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
            if (value->type == CSS_VALUE_TYPE_STRING) {
                replace_view_pool_layout_string(lycon, &span->gi->grid_area, value->data.string);
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM) {
                if (value->data.custom_property.name) {
                    replace_view_pool_layout_string(lycon, &span->gi->grid_area, value->data.custom_property.name);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                const char* name = css_enum_info(value->data.keyword)->name;
                if (value->data.keyword != CSS_VALUE_AUTO) {
                    replace_view_pool_layout_string(lycon, &span->gi->grid_area, name);
                }
            }
            else if (value->type == CSS_VALUE_TYPE_LIST) {
                int count = value->data.list.count;
                CssValue* components[4] = {};
                int component_count = 0;
                for (int i = 0; i < count && component_count < 4; i++) {
                    if (!css_grid_is_separator(value->data.list.values[i])) {
                        components[component_count++] = value->data.list.values[i];
                    }
                }
                if (component_count >= 1) {
                    css_grid_line_value(components[0], &span->gi->grid_row_start,
                              &span->gi->has_explicit_grid_row_start, &span->gi->grid_row_start_is_span);
                }
                if (component_count >= 2) {
                    css_grid_line_value(components[1], &span->gi->grid_column_start,
                              &span->gi->has_explicit_grid_column_start, &span->gi->grid_column_start_is_span);
                }
                if (component_count >= 3) {
                    css_grid_line_value(components[2], &span->gi->grid_row_end,
                              &span->gi->has_explicit_grid_row_end, &span->gi->grid_row_end_is_span);
                }
                if (component_count >= 4) {
                    css_grid_line_value(components[3], &span->gi->grid_column_end,
                              &span->gi->has_explicit_grid_column_end, &span->gi->grid_column_end_is_span);
                }
            }
            break;
        }
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
            if (!block) {
                break;
            }
            alloc_grid_prop(lycon, block);
            alloc_flex_prop(lycon, block);
            CssEnum align_val = CSS_VALUE_STRETCH;
            CssEnum justify_val = CSS_VALUE_STRETCH;
            css_resolve_keyword_pair(value, CSS_VALUE_STRETCH,
                                     &align_val, &justify_val);
            block->embedp()->grid->align_items = align_val;
            CssSelfAlignment justify_detail = {};
            justify_detail.value = justify_val;
            css_store_grid_justify_items(lycon, block, justify_detail);
            block->embedp()->flex->align_items = align_val;
            break;
        }
        case CSS_PROPERTY_PLACE_SELF: {
            CssSelfAlignment align_val = {};
            CssSelfAlignment justify_val = {};
            if (!css_parse_place_self_alignment(value, &align_val, &justify_val)) break;
            css_store_self_alignment(lycon, span, false, align_val);
            css_store_self_alignment(lycon, span, true, justify_val);
            if (span->parent_item_kind() == DomElement::PARENT_ITEM_GRID) {
                span->gi->align_self_grid = align_val.value;
                span->gi->justify_self = justify_val.value;
            } else if (span->parent_item_kind() == DomElement::PARENT_ITEM_FLEX) {
                span->fi->align_self = align_val.value;
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
            CssFlexBasisValue basis = css_parse_flex_basis_value(
                lycon, prop_id, value, true, true);
            if (basis.valid) css_set_flex_basis_value(span, basis);
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
            auto is_direction = [](CssEnum val) -> bool {
                return val == CSS_VALUE_ROW || val == CSS_VALUE_ROW_REVERSE ||
                       val == CSS_VALUE_COLUMN || val == CSS_VALUE_COLUMN_REVERSE;
            };
            auto is_wrap = [](CssEnum val) -> bool {
                return val == CSS_VALUE_NOWRAP || val == CSS_VALUE_WRAP || val == CSS_VALUE_WRAP_REVERSE;
            };
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
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
            ViewSpan* span = lam::view_require_element(lycon->view);
            float flex_grow = 1.0f;
            float flex_shrink = 1.0f;
            CssFlexBasisValue basis;
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                if (value->data.keyword == CSS_VALUE_NONE) {
                    flex_grow = 0;
                    flex_shrink = 0;
                    basis.value = -1;
                } else if (value->data.keyword == CSS_VALUE_AUTO) {
                    flex_grow = 1;
                    flex_shrink = 1;
                    basis.value = -1;
                } else if (value->data.keyword == CSS_VALUE_INITIAL) {
                    flex_grow = 0;
                    flex_shrink = 1;
                    basis.value = -1;
                }
                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         basis.value, basis.is_percent);
                break;
            }
            if (value->type == CSS_VALUE_TYPE_LIST) {
                size_t count = value->data.list.count;
                CssValue** values = value->data.list.values;
                int value_index = 0;
                bool found_basis = false;
                for (size_t i = 0; i < count && i < 3; i++) {
                    CssValue* val = values[i];
                    if (val->type == CSS_VALUE_TYPE_NUMBER) {
                        if (value_index == 0) {
                            flex_grow = (float)val->data.number.value;
                            value_index++;
                        } else if (value_index == 1) {
                            flex_shrink = (float)val->data.number.value;
                            value_index++;
                        } else if (value_index == 2 && val->data.number.value == 0) {
                            basis.value = 0;
                            basis.is_percent = false;
                            found_basis = true;
                        }
                    } else {
                        CssFlexBasisValue parsed = css_parse_flex_basis_value(
                            lycon, prop_id, val, false, false);
                        if (parsed.valid) {
                            basis = parsed;
                            found_basis = true;
                        }
                    }
                }
                if (count == 1 && value_index == 1 && !found_basis) {
                    flex_shrink = 1.0f;
                    basis.value = 0;
                }
                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         basis.value, basis.is_percent);
                span->fi->flex_basis_is_stretch = basis.is_stretch;
            }
            else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                flex_grow = (float)value->data.number.value;
                flex_shrink = 1.0f;
                basis.value = 0;
                css_set_flex_item_values(span, flex_grow, flex_shrink,
                                         basis.value, false);
            }
            break;
        }
        case CSS_PROPERTY_LIST_STYLE_TYPE: {
            span->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum type = value->data.keyword;
                span->blk->list_style_type = type;
                span->blk->list_style_type_string = nullptr;
            } else if (value->type == CSS_VALUE_TYPE_STRING) {
                css_store_list_style_type_string(lycon, span, value->data.string);
                }
            break;
        }
        case CSS_PROPERTY_LIST_STYLE_POSITION: {
            span->ensure_block(lycon);
            if (value->type == CSS_VALUE_TYPE_KEYWORD) {
                CssEnum position = value->data.keyword;
                span->blk->list_style_position = position;
            }
            else if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
                CssEnum position;
                if (css_list_style_custom_position(value->data.custom_property.name, &position)) {
                    span->blk->list_style_position = position;
                }
            }
            break;
        }
        case CSS_PROPERTY_LIST_STYLE_IMAGE: {
            span->ensure_block(lycon);
            if (!css_store_list_style_image(lycon, span, value)) {
                if (value->type != CSS_VALUE_TYPE_KEYWORD) break;
                if (value->data.keyword == CSS_VALUE_NONE) {
                    span->blk->list_style_image = {};
                    span->blk->list_style_image.url = (char*)alloc_prop(lycon, 5);
                    str_copy(span->block()->list_style_image.url, 5, "none", 4);
                }
            }
            break;
        }
        case CSS_PROPERTY_LIST_STYLE: {
            span->ensure_block(lycon);
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
                        if (parent->block()->list_style_image.url ||
                            parent->block()->list_style_image.gradient_type != GRADIENT_NONE) {
                            span->blk->list_style_image = parent->blk->list_style_image;
                        }
                        break;
                    }
                    parent = parent->parent ? parent->parent->as_element() : nullptr;
                }
                break;
            }
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                for (int i = 0; i < value->data.list.count; i++) {
                    css_apply_list_style_component(lycon, span, value->data.list.values[i], true);
                }
            } else {
                css_apply_list_style_component(lycon, span, value, false);
            }
            if (span->block()->list_style_type == 0) {
                span->blk->list_style_type = CSS_VALUE_DISC;
            }
            break;
        }
        case CSS_PROPERTY_COUNTER_RESET:
        case CSS_PROPERTY_COUNTER_INCREMENT:
        case CSS_PROPERTY_COUNTER_SET: {
            span->ensure_block(lycon);
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
       case CSS_PROPERTY_BACKGROUND: {
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function &&
                value->data.function->name && strcmp(value->data.function->name, "var") == 0) {
                const CssValue* resolved = resolve_var_function(lycon, value);
                if (resolved && resolved != value) {
                    lam::CssTempDecl resolved_decl(decl, CSS_PROPERTY_BACKGROUND, const_cast<CssValue*>(resolved));
                    resolved_decl.resolve(lycon);
                    return;
                }
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword == CSS_VALUE_INHERIT) {
                layout_ensure_background(lycon, span);
                BackgroundProp* parent_bg = parent_computed_background(lycon);
                if (parent_bg) {
                    *span->bound->background = *parent_bg;
                } else {
                    memset(span->boundary()->background, 0, sizeof(BackgroundProp));
                }
                return;
            }
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                (value->data.keyword == CSS_VALUE_NONE || value->data.keyword == CSS_VALUE_TRANSPARENT)) {
                layout_ensure_background(lycon, span);
                span->boundary_mut()->background->color.r = 0;
                span->boundary_mut()->background->color.g = 0;
                span->boundary_mut()->background->color.b = 0;
                span->boundary_mut()->background->color.a = 0;  // fully transparent
                return;
            }
            bool has_top_level_comma = decl && decl->value_text &&
                css_text_has_top_level_comma(decl->value_text, decl->value_text_len);
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 1 && has_top_level_comma) {
                CssValue** layers = value->data.list.values;
                int count = value->data.list.count;
                // Per CSS Backgrounds, background-color is allowed only in the
                // must not paint the last color over children.
                for (int i = 0; i < count - 1; i++) {
                    if (css_background_layer_has_plain_color(layers[i])) {
                        return;
                    }
                }
                layout_ensure_background(lycon, span);
                BackgroundProp* bg = span->boundary()->background;
                CssValue* last_layer = layers[count - 1];
                if (last_layer) {
                    if (last_layer->type == CSS_VALUE_TYPE_COLOR ||
                        last_layer->type == CSS_VALUE_TYPE_KEYWORD ||
                        (last_layer->type == CSS_VALUE_TYPE_FUNCTION && last_layer->data.function &&
                         last_layer->data.function->name &&
                         (str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgb") ||
                          str_ieq_const(last_layer->data.function->name, strlen(last_layer->data.function->name), "rgba")))) {
                        bg->color = resolve_color_value(lycon, last_layer);
                    }
                }
                int radial_count = 0;
                int linear_count = 0;
                for (int i = 0; i < count - 1; i++) {  // exclude last layer (base color)
                    CssValue* layer = layers[i];
                    if (css_find_background_gradient_layer(layer, GRADIENT_RADIAL)) radial_count++;
                    else if (css_find_background_gradient_layer(layer, GRADIENT_LINEAR)) linear_count++;
                }
                if (css_find_background_gradient_layer(last_layer, GRADIENT_LINEAR)) linear_count++;
                else if (css_find_background_gradient_layer(last_layer, GRADIENT_RADIAL)) radial_count++;
                if (radial_count > 0) {
                    bg->radial_layers = (RadialGradient**)alloc_prop(lycon, sizeof(RadialGradient*) * radial_count);
                    bg->radial_layer_count = 0;
                }
                if (linear_count > 0) {
                    bg->linear_layers = (LinearGradient**)alloc_prop(lycon, sizeof(LinearGradient*) * linear_count);
                    bg->linear_layer_count = 0;
                }
                for (int i = count - 1; i >= 0; i--) {
                    CssValue* layer = layers[i];
                    const CssValue* radial_layer = css_find_background_gradient_layer(layer, GRADIENT_RADIAL);
                    const CssValue* linear_layer = css_find_background_gradient_layer(layer, GRADIENT_LINEAR);
                    const CssValue* conic_layer = css_find_background_gradient_layer(layer, GRADIENT_CONIC);
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
                        if (!bg->image) {
                            lam::CssTempDecl img_decl(decl, CSS_PROPERTY_BACKGROUND_IMAGE, (CssValue*)url_layer);
                            img_decl.resolve(lycon);
                        }
                    }
                }
                return;
            }
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
                for (int i = 0; i < value->data.list.count; i++) {
                    CssValue* item = value->data.list.values[i];
                    if (!item) continue;
                    if (css_value_is_slash(item) && i + 1 < value->data.list.count) {
                        CssValue* size_values[2] = {};
                        int size_count = 0;
                        int j = i + 1;
                        while (j < value->data.list.count && size_count < 2) {
                            CssValue* size_item = value->data.list.values[j];
                            if (!size_item || css_value_is_slash(size_item)) break;
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
                    resolve_background_layer_component(lycon, decl, item);
                }
                return;
            }
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
            if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function && value->data.function->name) {
                const char* func_name = value->data.function->name;
                if (str_ieq_const(func_name, strlen(func_name), "rgb") || str_ieq_const(func_name, strlen(func_name), "rgba") ||
                    str_ieq_const(func_name, strlen(func_name), "hsl") || str_ieq_const(func_name, strlen(func_name), "hsla")) {
                    layout_ensure_background(lycon, span);
                    span->boundary_mut()->background->color = resolve_color_value(lycon, value);
                    return;
                }
            }
            if (resolve_background_gradient_value(lycon, span, value)) return;
            return;
        }
        case CSS_PROPERTY_GRID_GAP:
        case CSS_PROPERTY_GAP: {
            const CssValue* row_value = value;
            const CssValue* column_value = value;
            if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count == 2) {
                row_value = value->data.list.values[0];
                column_value = value->data.list.values[1];
            }
            bool scalar = value->type == CSS_VALUE_TYPE_LENGTH ||
                value->type == CSS_VALUE_TYPE_NUMBER ||
                value->type == CSS_VALUE_TYPE_PERCENTAGE;
            if (scalar || (value->type == CSS_VALUE_TYPE_LIST &&
                           value->data.list.count == 2)) {
                resolve_gap_property(lycon, block, CSS_PROPERTY_ROW_GAP, row_value, true);
                resolve_gap_property(lycon, block, CSS_PROPERTY_COLUMN_GAP, column_value, false);
            }
            return;
        }
        case CSS_PROPERTY_CONTAIN: {
            if (!block || !value) break;
            block->ensure_block(lycon);
            bool contains_size = css_contain_value_has_size(value);
            bool contains_inline_size = css_contain_value_has_inline_size(value);
            block->blk->contain_size = contains_size;
            block->blk->contain_inline_size = contains_inline_size;
            block->blk->contain_positioning =
                css_contain_value_establishes_positioning_cb(value);
            break;
        }
        case CSS_PROPERTY_CONTAINER_TYPE: {
            if (!block || !value) break;
            block->ensure_block(lycon);
            // CSS Containment: container-type:size maps to size containment;
            // otherwise the contained box's auto size still grows from content.
            block->blk->contain_size = css_value_has_identifier(value, "size");
            block->blk->contain_inline_size = css_value_has_identifier(
                value, "inline-size");
            break;
        }
        case CSS_PROPERTY_CONTENT_VISIBILITY: {
            if (!block || !value) break;
            block->ensure_block(lycon);
            block->blk->content_visibility_hidden =
                css_content_visibility_value_is_hidden(value);
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
                block->ensure_block(lycon);
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
            layout_ensure_outline(lycon, span);
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
            break;
    }
}
