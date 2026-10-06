#include <stdlib.h>
#include <string.h>

extern "C" {
#include "../../../lib/log.h"
#include "../../../lib/str.h"
#include "../../../lib/mempool.h"
#include "../../../lib/hashmap_helpers.h"
}
#include "css_value.hpp"
#include "css_style.hpp"
#include "../../../lib/color.h"
#include "../../../lib/strbuf.h"
#include <math.h>

static double css_color_clamp(double value, double maximum = 1.0) {
    return isnan(value) ? 0.0 : fmax(0.0, fmin(maximum, value));
}

static bool css_color_component(const CssValue* value, bool hue, bool percentage_only,
    bool allow_missing, bool clamp_component, double percentage_scale, double number_scale, double* result,
    bool* missing, CssMathType* type) {
    if (!value) return false;
    *missing = value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE;
    if (*missing) {*result = 0.0; *type = CSS_MATH_NUMBER; return allow_missing;}
    if (value->type != CSS_VALUE_TYPE_NUMBER && value->type != CSS_VALUE_TYPE_PERCENTAGE &&
        value->type != CSS_VALUE_TYPE_LENGTH && value->type != CSS_VALUE_TYPE_ANGLE &&
        value->type != CSS_VALUE_TYPE_FUNCTION) return false;
    CssMathEvaluationContext context = {nullptr, nullptr, 1.0, true};
    CssMathResult computed = css_math_evaluate(value, &context);
    *type = computed.type;
    if (!computed.resolved) return false;
    if (hue) {
        if (*type != CSS_MATH_NUMBER && *type != CSS_MATH_ANGLE) return false;
        *result = isfinite(computed.value) ? fmod(computed.value, 360.0) : 0.0;
        if (*result < 0.0) *result += 360.0;
    } else {
        if (*type != CSS_MATH_PERCENT && (*type != CSS_MATH_NUMBER || percentage_only)) return false;
        double amount = *type == CSS_MATH_PERCENT
            ? computed.percentage * percentage_scale : computed.value * number_scale;
        *result = clamp_component ? css_color_clamp(amount) : amount;
    }
    return true;
}

static bool css_color_function_compute(const CssFunction* function, CssComputedColor* result) {
    if (!function || !function->name || !function->args) return false;
    bool rgb = str_ieq_cstr(function->name, "rgb") || str_ieq_cstr(function->name, "rgba");
    bool hsl = str_ieq_cstr(function->name, "hsl") || str_ieq_cstr(function->name, "hsla");
    bool hwb = str_ieq_cstr(function->name, "hwb");
    bool predefined = str_ieq_cstr(function->name, "color");
    if (!rgb && !hsl && !hwb && !predefined) return false;
    const CssValue* channels[4] = {};
    bool modern = function->arg_count == 1;
    int count = function->arg_count;
    if (modern) {
        const CssValue* list = function->args[0];
        if (!list || list->type != CSS_VALUE_TYPE_LIST || list->data.list.comma_separated ||
            !list->data.list.values) return false;
        count = list->data.list.count;
        int offset = predefined ? 1 : 0;
        if (predefined) {
            if (count < 1) return false;
            const char* space = css_value_identifier_name(list->data.list.values[0]);
            if (!space || !str_ieq_cstr(space, "srgb")) return false;
            result->color_space = "srgb";
            count--;
        }
        if (count != 3 && count != 5) return false;
        for (int i = 0; i < 3; i++) channels[i] = list->data.list.values[i + offset];
        if (count == 5) {
            const char* slash = css_math_token_name(list->data.list.values[3 + offset]);
            if (!slash || strcmp(slash, "/") != 0) return false;
            channels[3] = list->data.list.values[4 + offset];
        }
    } else {
        if (hwb || predefined || count < 3 || count > 4) return false;
        for (int i = 0; i < count; i++) channels[i] = function->args[i];
    }
    result->type = predefined ? CSS_COLOR_COLOR : rgb ? CSS_COLOR_RGB : hsl ? CSS_COLOR_HSL : CSS_COLOR_HWB;
    result->components[3] = 1.0;
    CssMathType first_type = CSS_MATH_INVALID;
    for (int i = 0; i < 4; i++) {
        if (i == 3 && !channels[i]) continue;
        bool missing = false;
        CssMathType type;
        bool hue = (hsl || hwb) && i == 0;
        if (!css_color_component(channels[i], hue, !modern && !rgb && i > 0 && i < 3,
            modern, !predefined || i == 3, 0.01,
            rgb && i < 3 ? 1.0 / 255.0 : (hsl || hwb) && i > 0 && i < 3 ? 0.01 : 1.0,
            &result->components[i], &missing, &type)) return false;
        if (missing) result->missing |= (uint8_t)(1u << i);
        if (rgb && !modern && i < 3) {
            if (i == 0) first_type = type;
            else if (first_type != type) return false;
        }
    }
    if ((hsl || hwb) && !result->missing) {
        // HSL/HWB become sRGB; missing channels retain their source model for interpolation.
        double red, green, blue;
        if (hsl) color_hsl_to_rgb(result->components[0], result->components[1], result->components[2],
            &red, &green, &blue);
        else color_hwb_to_rgb(result->components[0], result->components[1], result->components[2],
            &red, &green, &blue);
        result->type = CSS_COLOR_RGB;
        result->components[0] = red; result->components[1] = green; result->components[2] = blue;
    }
    return true;
}

bool css_color_compute(const CssValue* value, CssComputedColor* result) {
    if (!value || !result) return false;
    *result = {};
    if (value->type == CSS_VALUE_TYPE_FUNCTION)
        return css_color_function_compute(value->data.function, result);
    CssEnum keyword = CSS_VALUE__UNDEF;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) keyword = value->data.keyword;
    else if (value->type == CSS_VALUE_TYPE_COLOR) {
        CssColorType type = value->data.color.type;
        if (type == CSS_COLOR_CURRENT || type == CSS_COLOR_CURRENTCOLOR) keyword = CSS_VALUE_CURRENTCOLOR;
        else if (type == CSS_COLOR_KEYWORD || type == CSS_COLOR_SYSTEM)
            keyword = css_enum_by_name(value->data.color.data.keyword);
        else if (type == CSS_COLOR_TRANSPARENT) keyword = CSS_VALUE_TRANSPARENT;
        else if (type == CSS_COLOR_HEX || type == CSS_COLOR_RGB) {
            result->type = CSS_COLOR_RGB;
            result->alpha_is_byte = true;
            result->components[0] = value->data.color.data.rgba.r / 255.0;
            result->components[1] = value->data.color.data.rgba.g / 255.0;
            result->components[2] = value->data.color.data.rgba.b / 255.0;
            result->components[3] = value->data.color.data.rgba.a / 255.0;
            return true;
        } else if ((type == CSS_COLOR_HSL || type == CSS_COLOR_HWB) && value->data.color.data.components) {
            const CssColorComponents* components = value->data.color.data.components;
            result->type = CSS_COLOR_RGB;
            if (type == CSS_COLOR_HSL) color_hsl_to_rgb(components->component1, components->component2,
                components->component3, &result->components[0], &result->components[1], &result->components[2]);
            else color_hwb_to_rgb(components->component1, components->component2,
                components->component3, &result->components[0], &result->components[1], &result->components[2]);
            result->components[3] = css_color_clamp(components->component4);
            return true;
        } else return false;
    } else return false;
    result->keyword = keyword;
    if (keyword == CSS_VALUE_CURRENTCOLOR) {result->type = CSS_COLOR_CURRENTCOLOR; return true;}
    const CssEnumInfo* info = css_enum_info(keyword);
    if (info && info->group == CSS_VALUE_GROUP_SYSTEM_COLOR) {result->type = CSS_COLOR_SYSTEM; return true;}
    uint8_t r, g, b, a;
    if (!css_named_color_to_rgba(keyword, &r, &g, &b, &a)) return false;
    result->type = CSS_COLOR_RGB;
    result->alpha_is_byte = true;
    result->components[0] = r / 255.0; result->components[1] = g / 255.0;
    result->components[2] = b / 255.0; result->components[3] = a / 255.0;
    return true;
}

const CssValue* css_background_color_component(const CssValue* value) {
    if (!value) return nullptr;
    CssComputedColor color;
    if (css_color_compute(value, &color)) return value;
    if (value->type != CSS_VALUE_TYPE_LIST || !value->data.list.values) return nullptr;
    for (int i = value->data.list.count - 1; i >= 0; i--) {
        const CssValue* found = css_background_color_component(value->data.list.values[i]);
        if (found) return found;
    }
    return nullptr;
}

double css_color_legacy_alpha(uint8_t alpha) {
    // CSS Color 4 §16.1.1; the rational form keeps the 50% half tie exact.
    double percent = floor(alpha * 100.0 / 255.0 + 0.5);
    return floor(percent * 255.0 / 100.0 + 0.5) == alpha ? percent / 100.0
        : floor(alpha / 0.255 + 0.5) / 1000.0;
}

bool css_color_to_rgba(const CssComputedColor* color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (!color || !r || !g || !b || !a) return false;
    double red = color->components[0], green = color->components[1], blue = color->components[2];
    if (color->type == CSS_COLOR_HSL) color_hsl_to_rgb(red, green, blue, &red, &green, &blue);
    else if (color->type == CSS_COLOR_HWB) color_hwb_to_rgb(red, green, blue, &red, &green, &blue);
    else if (color->type != CSS_COLOR_RGB && !(color->type == CSS_COLOR_COLOR &&
        color->color_space && strcmp(color->color_space, "srgb") == 0)) return false;
    *r = (uint8_t)floor(css_color_clamp(red) * 255.0 + 0.5);
    *g = (uint8_t)floor(css_color_clamp(green) * 255.0 + 0.5);
    *b = (uint8_t)floor(css_color_clamp(blue) * 255.0 + 0.5);
    *a = (uint8_t)floor(css_color_clamp(color->components[3]) * 255.0 + 0.5);
    return true;
}

CssValue* css_value_create_function(Pool* pool, const char* name, CssValue** args, int count) {
    if (!pool || !name || count < 0 || (count && !args)) return nullptr;
    CssValue* value = (CssValue*)pool_calloc(pool, sizeof(CssValue));
    CssFunction* function = (CssFunction*)pool_calloc(pool, sizeof(CssFunction));
    if (!value || !function) return nullptr;
    function->name = pool_strdup(pool, name);
    if (!function->name) return nullptr;
    function->args = args;
    function->arg_count = count;
    value->type = CSS_VALUE_TYPE_FUNCTION;
    value->data.function = function;
    return value;
}

CssRuleChildList css_rule_child_list(CssRule* rule) {
    if (!rule) return {};
    if (rule->type == CSS_RULE_STYLE)
        return {&rule->data.style_rule.nested_rules, &rule->data.style_rule.nested_rule_count};
    if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
        rule->type == CSS_RULE_CONTAINER || rule->type == CSS_RULE_SCOPE ||
        (rule->type == CSS_RULE_LAYER && !rule->data.conditional_rule.layer_statement))
        return {&rule->data.conditional_rule.rules, &rule->data.conditional_rule.rule_count};
    return {};
}

void css_rule_attach(CssRule* rule, CssRule* parent, CssStylesheet* stylesheet) {
    if (!rule) return;
    rule->parent = parent;
    rule->stylesheet = stylesheet;
    // descriptor declaration wrappers retain the existing lazy style-rule cache.
    if ((rule->type == CSS_RULE_FONT_FACE || rule->type == CSS_RULE_PAGE) &&
        rule->property_count && rule->property_names && rule->property_values)
        css_rule_attach((CssRule*)rule->property_values, rule, stylesheet);
    CssRuleChildList children = css_rule_child_list(rule);
    if (children.count) for (size_t i = 0; i < *children.count; i++)
        css_rule_attach((*children.rules)[i], rule, stylesheet);
}

bool css_value_is_global_keyword(const CssValue* value) {
    const CssEnumInfo* info = value && value->type == CSS_VALUE_TYPE_KEYWORD
        ? css_enum_info(value->data.keyword) : nullptr;
    return info && info->group == CSS_VALUE_GROUP_GLOBAL;
}

const char* css_value_identifier_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->has_keyword_spelling) return value->data.keyword_token.spelling;
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) return value->data.custom_property.name;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_URL) return value->data.url;
    return nullptr;
}

bool css_value_keyword_equals(const CssValue* value, CssEnum keyword) {
    return value && value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == keyword;
}

bool css_function_name_is(const CssFunction* function, const char* name) {
    return function && function->name && name && str_ieq_cstr(function->name, name);
}

bool css_value_is_inherit(const CssValue* value) { return css_value_keyword_equals(value, CSS_VALUE_INHERIT); }
bool css_value_is_initial(const CssValue* value) { return css_value_keyword_equals(value, CSS_VALUE_INITIAL); }
bool css_value_is_unset(const CssValue* value) { return css_value_keyword_equals(value, CSS_VALUE_UNSET); }
bool css_value_is_auto(const CssValue* value) { return css_value_keyword_equals(value, CSS_VALUE_AUTO); }
bool css_value_is_none(const CssValue* value) { return css_value_keyword_equals(value, CSS_VALUE_NONE); }

int css_content_quote_type(const CssValue* value) {
    if (!value || (value->type != CSS_VALUE_TYPE_CUSTOM && value->type != CSS_VALUE_TYPE_KEYWORD)) return 0;
    const char* name = css_value_identifier_name(value);
    const char* names[] = {"open-quote", "close-quote", "no-open-quote", "no-close-quote"};
    for (int i = 0; name && i < 4; i++) if (str_ieq_cstr(name, names[i])) return i + 1;
    return 0;
}

const char* css_content_quote_char(const CssValue* quotes, bool open, int depth) {
    if (depth < 0) depth = 0;
    if (css_value_is_none(quotes)) return "";
    if (quotes && quotes->type == CSS_VALUE_TYPE_LIST && quotes->data.list.count >= 2) {
        int pairs = quotes->data.list.count / 2;
        int pair = depth < pairs ? depth : pairs - 1;
        const CssValue* item = quotes->data.list.values[pair * 2 + (open ? 0 : 1)];
        if (item && item->type == CSS_VALUE_TYPE_STRING) return item->data.string;
    }
    if (quotes && quotes->type == CSS_VALUE_TYPE_STRING) return quotes->data.string;
    return open ? "\xe2\x80\x9c" : "\xe2\x80\x9d";
}

const char* css_content_attribute_name(const CssValue* value, const char** type) {
    if (type) *type = nullptr;
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_ATTR && value->data.attr_ref) {
        if (type) *type = value->data.attr_ref->type_or_unit;
        return value->data.attr_ref->name;
    }
    const CssFunction* function = value->type == CSS_VALUE_TYPE_FUNCTION ? value->data.function : nullptr;
    if (!css_function_name_is(function, "attr") || function->arg_count < 1 || function->arg_count > 2) return nullptr;
    const CssValue* name = function->args[0];
    // attr(name type) is one space-separated argument, followed by an optional comma fallback.
    if (name && name->type == CSS_VALUE_TYPE_LIST) {
        if (name->data.list.comma_separated || name->data.list.count < 1 || name->data.list.count > 2) return nullptr;
        if (name->data.list.count == 2) {
            const char* hint = css_value_identifier_name(name->data.list.values[1]);
            if (!hint) return nullptr;
            if (type) *type = hint;
        }
        name = name->data.list.values[0];
    }
    return css_value_identifier_name(name);
}

bool css_content_append(const CssValue* value, const CssContentBindings* bindings,
                        int* quote_depth, StrBuf* text, size_t depth) {
    if (!value || !bindings || !quote_depth || !text || depth > 64) return false;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        for (int i = 0; i < value->data.list.count; i++)
            if (!css_content_append(value->data.list.values[i], bindings, quote_depth, text, depth + 1)) return false;
        return true;
    }
    if (value->type == CSS_VALUE_TYPE_STRING) {
        if (value->data.string) strbuf_append_str(text, value->data.string);
        return true;
    }
    const char* attribute = css_content_attribute_name(value);
    const CssFunction* function = value->type == CSS_VALUE_TYPE_FUNCTION ? value->data.function : nullptr;
    if (attribute) {
        const char* result = bindings->attribute ? bindings->attribute(bindings->context, attribute) : nullptr;
        if (result) strbuf_append_str(text, result);
        else if (value->type == CSS_VALUE_TYPE_ATTR && value->data.attr_ref->fallback)
            return css_content_append(value->data.attr_ref->fallback, bindings, quote_depth, text, depth + 1);
        return true;
    }
    int quote = css_content_quote_type(value);
    if (quote) {
        if (quote == 2 || quote == 4) { if (*quote_depth > 0) (*quote_depth)--; }
        if ((quote == 1 || quote == 2) && bindings->quote) {
            const char* result = bindings->quote(bindings->context, quote == 1, *quote_depth);
            if (result) strbuf_append_str(text, result);
        }
        if (quote == 1 || quote == 3) (*quote_depth)++;
        return true;
    }
    if (css_value_is_none(value) || (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NORMAL)) return true;
    return function && function->name && bindings->function && bindings->function(bindings->context, function, text);
}

bool css_absolute_length_to_px(CssUnit unit, double value, double* pixels) {
    static const double scales[] = {
        1.0, 96.0 / 2.54, 96.0 / 25.4, 96.0, 4.0 / 3.0, 16.0,
        96.0 / 2.54 / 40.0,
    };
    if (!pixels || unit < CSS_UNIT_PX || unit > CSS_UNIT_Q) return false;
    *pixels = value * scales[unit - CSS_UNIT_PX];
    return true;
}

bool css_dimension_to_canonical(CssUnit unit, double value, CssUnit* canonical_unit,
                                double* canonical_value) {
    if (!canonical_unit || !canonical_value) return false;
    double converted;
    if (css_absolute_length_to_px(unit, value, &converted)) {
        *canonical_unit = CSS_UNIT_PX;
        *canonical_value = converted;
        return true;
    }
    double factor;
    switch (unit) {
        case CSS_UNIT_DEG: factor = 1.0; *canonical_unit = CSS_UNIT_DEG; break;
        case CSS_UNIT_GRAD: factor = 0.9; *canonical_unit = CSS_UNIT_DEG; break;
        case CSS_UNIT_RAD: factor = 180.0 / acos(-1.0); *canonical_unit = CSS_UNIT_DEG; break;
        case CSS_UNIT_TURN: factor = 360.0; *canonical_unit = CSS_UNIT_DEG; break;
        case CSS_UNIT_S: factor = 1.0; *canonical_unit = CSS_UNIT_S; break;
        case CSS_UNIT_MS: factor = 0.001; *canonical_unit = CSS_UNIT_S; break;
        case CSS_UNIT_DPI: factor = 1.0 / 96.0; *canonical_unit = CSS_UNIT_DPPX; break;
        case CSS_UNIT_DPCM: factor = 2.54 / 96.0; *canonical_unit = CSS_UNIT_DPPX; break;
        case CSS_UNIT_DPPX: factor = 1.0; *canonical_unit = CSS_UNIT_DPPX; break;
        default: return false;
    }
    *canonical_value = value * factor;
    return true;
}

bool css_viewport_length_to_px(CssUnit unit, double value, double width,
                               double height, bool vertical_inline_axis,
                               double* pixels) {
    if (!pixels) return false;
    double base = 0.0;
    switch (unit) {
        case CSS_UNIT_VW: case CSS_UNIT_SVW: case CSS_UNIT_LVW: case CSS_UNIT_DVW:
            base = width; break;
        case CSS_UNIT_VH: case CSS_UNIT_SVH: case CSS_UNIT_LVH: case CSS_UNIT_DVH:
            base = height; break;
        case CSS_UNIT_VI: case CSS_UNIT_SVI: case CSS_UNIT_LVI: case CSS_UNIT_DVI:
            base = vertical_inline_axis ? height : width; break;
        case CSS_UNIT_VB: case CSS_UNIT_SVB: case CSS_UNIT_LVB: case CSS_UNIT_DVB:
            base = vertical_inline_axis ? width : height; break;
        case CSS_UNIT_VMIN: case CSS_UNIT_SVMIN: case CSS_UNIT_LVMIN: case CSS_UNIT_DVMIN:
            base = width < height ? width : height; break;
        case CSS_UNIT_VMAX: case CSS_UNIT_SVMAX: case CSS_UNIT_LVMAX: case CSS_UNIT_DVMAX:
            base = width > height ? width : height; break;
        default: return false;
    }
    // Radiant has no retractable viewport UI; all three variants share its
    // current viewport dimensions until such UI contributes separate sizes.
    *pixels = value * base / 100.0;
    return true;
}

const char* css_math_token_name(const CssValue* value) {
    if (!value) return NULL;
    if (value->type == CSS_VALUE_TYPE_CUSTOM)
        return value->data.custom_property.name;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : NULL;
    }
    return NULL;
}

static bool css_value_contains_var_reference_inner(const CssValue* value,
                                                   int depth, bool include_pending) {
    if (!value || depth > 32) return false;
    if (value->type == CSS_VALUE_TYPE_VAR) return true;
    if (include_pending && (value->type == CSS_VALUE_TYPE_ENV || value->type == CSS_VALUE_TYPE_ATTR)) return true;
    if (value->type == CSS_VALUE_TYPE_LIST) {
        if (!value->data.list.values) return false;
        for (int i = 0; i < value->data.list.count; i++) {
            if (css_value_contains_var_reference_inner(
                    value->data.list.values[i], depth + 1, include_pending)) return true;
        }
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION &&
               value->data.function && value->data.function->name) {
        if (strcmp(value->data.function->name, "var") == 0) return true;
        if (include_pending && (strcmp(value->data.function->name, "env") == 0 ||
            strcmp(value->data.function->name, "attr") == 0)) return true;
        for (int i = 0; value->data.function->args &&
             i < value->data.function->arg_count; i++) {
            if (css_value_contains_var_reference_inner(
                    value->data.function->args[i], depth + 1, include_pending)) return true;
        }
    }
    return false;
}

bool css_value_contains_var_reference(const CssValue* value) {
    return css_value_contains_var_reference_inner(value, 0, false);
}

bool css_value_contains_pending_substitution(const CssValue* value) {
    return css_value_contains_var_reference_inner(value, 0, true);
}

float css_font_size_keyword_px(CssEnum keyword) {
    switch (keyword) {
        case CSS_VALUE_XX_SMALL: return 9.0f;
        case CSS_VALUE_X_SMALL: return 10.0f;
        case CSS_VALUE_SMALL: return 13.0f;
        case CSS_VALUE_MEDIUM: return 16.0f;
        case CSS_VALUE_LARGE: return 18.0f;
        case CSS_VALUE_X_LARGE: return 24.0f;
        case CSS_VALUE_XX_LARGE: return 32.0f;
        case CSS_VALUE_XXX_LARGE: return 48.0f;
        default: return 16.0f;
    }
}

static const CssEnumInfo css_value_definitions[] = {
    {"_undef", 6, CSS_VALUE__UNDEF, CSS_VALUE_GROUP__UNDEF},
    {"_length", 7, CSS_VALUE__LENGTH, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_percentage", 11, CSS_VALUE__PERCENTAGE, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_number", 7, CSS_VALUE__NUMBER, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_integer", 8, CSS_VALUE__INTEGER, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"_angle", 6, CSS_VALUE__ANGLE, CSS_VALUE_GROUP_SPECIAL_TYPE},
    {"initial", 7, CSS_VALUE_INITIAL, CSS_VALUE_GROUP_GLOBAL},
    {"inherit", 7, CSS_VALUE_INHERIT, CSS_VALUE_GROUP_GLOBAL},
    {"unset", 5, CSS_VALUE_UNSET, CSS_VALUE_GROUP_GLOBAL},
    {"revert", 6, CSS_VALUE_REVERT, CSS_VALUE_GROUP_GLOBAL},
    {"revert-layer", 12, CSS_VALUE_REVERT_LAYER, CSS_VALUE_GROUP_GLOBAL},
    {"flex-start", 10, CSS_VALUE_FLEX_START, CSS_VALUE_GROUP_ALIGNMENT},
    {"flex-end", 8, CSS_VALUE_FLEX_END, CSS_VALUE_GROUP_ALIGNMENT},
    {"center", 6, CSS_VALUE_CENTER, CSS_VALUE_GROUP_ALIGNMENT},
    {"space-between", 13, CSS_VALUE_SPACE_BETWEEN, CSS_VALUE_GROUP_ALIGNMENT},
    {"space-around", 12, CSS_VALUE_SPACE_AROUND, CSS_VALUE_GROUP_ALIGNMENT},
    {"stretch", 7, CSS_VALUE_STRETCH, CSS_VALUE_GROUP_ALIGNMENT},
    {"baseline", 8, CSS_VALUE_BASELINE, CSS_VALUE_GROUP_ALIGNMENT},
    {"auto", 4, CSS_VALUE_AUTO, CSS_VALUE_GROUP_SIZE},
    {"avoid", 5, CSS_VALUE_AVOID, CSS_VALUE_GROUP_MISC},
    {"text-bottom", 11, CSS_VALUE_TEXT_BOTTOM, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"alphabetic", 10, CSS_VALUE_ALPHABETIC, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"ideographic", 11, CSS_VALUE_IDEOGRAPHIC, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"middle", 6, CSS_VALUE_MIDDLE, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"central", 7, CSS_VALUE_CENTRAL, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"mathematical", 12, CSS_VALUE_MATHEMATICAL, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"text-top", 8, CSS_VALUE_TEXT_TOP, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"sub", 3, CSS_VALUE_SUB, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"super", 5, CSS_VALUE_SUPER, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"top", 3, CSS_VALUE_TOP, CSS_VALUE_GROUP_VERTICAL_ALIGN},  // vertical-align: top (also for position side)
    {"bottom", 6, CSS_VALUE_BOTTOM, CSS_VALUE_GROUP_VERTICAL_ALIGN},  // vertical-align: bottom (also for position side)
    {"first", 5, CSS_VALUE_FIRST, CSS_VALUE_GROUP_POSITION_SIDE},
    {"last", 4, CSS_VALUE_LAST, CSS_VALUE_GROUP_POSITION_SIDE},
    {"thin", 4, CSS_VALUE_THIN, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"medium", 6, CSS_VALUE_MEDIUM, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"thick", 5, CSS_VALUE_THICK, CSS_VALUE_GROUP_BORDER_WIDTH},
    {"none", 4, CSS_VALUE_NONE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"hidden", 6, CSS_VALUE_HIDDEN, CSS_VALUE_GROUP_BORDER_STYLE},
    {"dotted", 6, CSS_VALUE_DOTTED, CSS_VALUE_GROUP_BORDER_STYLE},
    {"dashed", 6, CSS_VALUE_DASHED, CSS_VALUE_GROUP_BORDER_STYLE},
    {"solid", 5, CSS_VALUE_SOLID, CSS_VALUE_GROUP_BORDER_STYLE},
    {"double", 6, CSS_VALUE_DOUBLE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"groove", 6, CSS_VALUE_GROOVE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"ridge", 5, CSS_VALUE_RIDGE, CSS_VALUE_GROUP_BORDER_STYLE},
    {"inset", 5, CSS_VALUE_INSET, CSS_VALUE_GROUP_BORDER_STYLE},
    {"outset", 6, CSS_VALUE_OUTSET, CSS_VALUE_GROUP_BORDER_STYLE},
    {"content-box", 11, CSS_VALUE_CONTENT_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"border-box", 10, CSS_VALUE_BORDER_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"inline-start", 12, CSS_VALUE_INLINE_START, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"inline-end", 10, CSS_VALUE_INLINE_END, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"block-start", 11, CSS_VALUE_BLOCK_START, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"block-end", 9, CSS_VALUE_BLOCK_END, CSS_VALUE_GROUP_LOGICAL_SIDE},
    {"left", 4, CSS_VALUE_LEFT, CSS_VALUE_GROUP_POSITION_SIDE},
    {"right", 5, CSS_VALUE_RIGHT, CSS_VALUE_GROUP_POSITION_SIDE},
    {"currentcolor", 12, CSS_VALUE_CURRENTCOLOR, CSS_VALUE_GROUP_COLOR},
    {"transparent", 11, CSS_VALUE_TRANSPARENT, CSS_VALUE_GROUP_COLOR},
    {"hex", 3, CSS_VALUE_HEX, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"aliceblue", 9, CSS_VALUE_ALICEBLUE, CSS_VALUE_GROUP_COLOR},
    {"antiquewhite", 12, CSS_VALUE_ANTIQUEWHITE, CSS_VALUE_GROUP_COLOR},
    {"aqua", 4, CSS_VALUE_AQUA, CSS_VALUE_GROUP_COLOR},
    {"aquamarine", 10, CSS_VALUE_AQUAMARINE, CSS_VALUE_GROUP_COLOR},
    {"azure", 5, CSS_VALUE_AZURE, CSS_VALUE_GROUP_COLOR},
    {"beige", 5, CSS_VALUE_BEIGE, CSS_VALUE_GROUP_COLOR},
    {"bisque", 6, CSS_VALUE_BISQUE, CSS_VALUE_GROUP_COLOR},
    {"black", 5, CSS_VALUE_BLACK, CSS_VALUE_GROUP_COLOR},
    {"blanchedalmond", 14, CSS_VALUE_BLANCHEDALMOND, CSS_VALUE_GROUP_COLOR},
    {"blue", 4, CSS_VALUE_BLUE, CSS_VALUE_GROUP_COLOR},
    {"blueviolet", 10, CSS_VALUE_BLUEVIOLET, CSS_VALUE_GROUP_COLOR},
    {"brown", 5, CSS_VALUE_BROWN, CSS_VALUE_GROUP_COLOR},
    {"burlywood", 9, CSS_VALUE_BURLYWOOD, CSS_VALUE_GROUP_COLOR},
    {"cadetblue", 9, CSS_VALUE_CADETBLUE, CSS_VALUE_GROUP_COLOR},
    {"chartreuse", 10, CSS_VALUE_CHARTREUSE, CSS_VALUE_GROUP_COLOR},
    {"chocolate", 9, CSS_VALUE_CHOCOLATE, CSS_VALUE_GROUP_COLOR},
    {"coral", 5, CSS_VALUE_CORAL, CSS_VALUE_GROUP_COLOR},
    {"cornflowerblue", 14, CSS_VALUE_CORNFLOWERBLUE, CSS_VALUE_GROUP_COLOR},
    {"cornsilk", 8, CSS_VALUE_CORNSILK, CSS_VALUE_GROUP_COLOR},
    {"crimson", 7, CSS_VALUE_CRIMSON, CSS_VALUE_GROUP_COLOR},
    {"cyan", 4, CSS_VALUE_CYAN, CSS_VALUE_GROUP_COLOR},
    {"darkblue", 8, CSS_VALUE_DARKBLUE, CSS_VALUE_GROUP_COLOR},
    {"darkcyan", 8, CSS_VALUE_DARKCYAN, CSS_VALUE_GROUP_COLOR},
    {"darkgoldenrod", 13, CSS_VALUE_DARKGOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"darkgray", 8, CSS_VALUE_DARKGRAY, CSS_VALUE_GROUP_COLOR},
    {"darkgreen", 9, CSS_VALUE_DARKGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkgrey", 8, CSS_VALUE_DARKGREY, CSS_VALUE_GROUP_COLOR},
    {"darkkhaki", 9, CSS_VALUE_DARKKHAKI, CSS_VALUE_GROUP_COLOR},
    {"darkmagenta", 11, CSS_VALUE_DARKMAGENTA, CSS_VALUE_GROUP_COLOR},
    {"darkolivegreen", 14, CSS_VALUE_DARKOLIVEGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkorange", 10, CSS_VALUE_DARKORANGE, CSS_VALUE_GROUP_COLOR},
    {"darkorchid", 10, CSS_VALUE_DARKORCHID, CSS_VALUE_GROUP_COLOR},
    {"darkred", 7, CSS_VALUE_DARKRED, CSS_VALUE_GROUP_COLOR},
    {"darksalmon", 10, CSS_VALUE_DARKSALMON, CSS_VALUE_GROUP_COLOR},
    {"darkseagreen", 12, CSS_VALUE_DARKSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"darkslateblue", 13, CSS_VALUE_DARKSLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"darkslategray", 13, CSS_VALUE_DARKSLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"darkslategrey", 13, CSS_VALUE_DARKSLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"darkturquoise", 13, CSS_VALUE_DARKTURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"darkviolet", 10, CSS_VALUE_DARKVIOLET, CSS_VALUE_GROUP_COLOR},
    {"deeppink", 8, CSS_VALUE_DEEPPINK, CSS_VALUE_GROUP_COLOR},
    {"deepskyblue", 11, CSS_VALUE_DEEPSKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"dimgray", 7, CSS_VALUE_DIMGRAY, CSS_VALUE_GROUP_COLOR},
    {"dimgrey", 7, CSS_VALUE_DIMGREY, CSS_VALUE_GROUP_COLOR},
    {"dodgerblue", 10, CSS_VALUE_DODGERBLUE, CSS_VALUE_GROUP_COLOR},
    {"firebrick", 9, CSS_VALUE_FIREBRICK, CSS_VALUE_GROUP_COLOR},
    {"floralwhite", 11, CSS_VALUE_FLORALWHITE, CSS_VALUE_GROUP_COLOR},
    {"forestgreen", 11, CSS_VALUE_FORESTGREEN, CSS_VALUE_GROUP_COLOR},
    {"fuchsia", 7, CSS_VALUE_FUCHSIA, CSS_VALUE_GROUP_COLOR},
    {"gainsboro", 9, CSS_VALUE_GAINSBORO, CSS_VALUE_GROUP_COLOR},
    {"ghostwhite", 10, CSS_VALUE_GHOSTWHITE, CSS_VALUE_GROUP_COLOR},
    {"gold", 4, CSS_VALUE_GOLD, CSS_VALUE_GROUP_COLOR},
    {"goldenrod", 9, CSS_VALUE_GOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"gray", 4, CSS_VALUE_GRAY, CSS_VALUE_GROUP_COLOR},
    {"green", 5, CSS_VALUE_GREEN, CSS_VALUE_GROUP_COLOR},
    {"greenyellow", 11, CSS_VALUE_GREENYELLOW, CSS_VALUE_GROUP_COLOR},
    {"grey", 4, CSS_VALUE_GREY, CSS_VALUE_GROUP_COLOR},
    {"honeydew", 8, CSS_VALUE_HONEYDEW, CSS_VALUE_GROUP_COLOR},
    {"hotpink", 7, CSS_VALUE_HOTPINK, CSS_VALUE_GROUP_COLOR},
    {"indianred", 9, CSS_VALUE_INDIANRED, CSS_VALUE_GROUP_COLOR},
    {"indigo", 6, CSS_VALUE_INDIGO, CSS_VALUE_GROUP_COLOR},
    {"ivory", 5, CSS_VALUE_IVORY, CSS_VALUE_GROUP_COLOR},
    {"khaki", 5, CSS_VALUE_KHAKI, CSS_VALUE_GROUP_COLOR},
    {"lavender", 8, CSS_VALUE_LAVENDER, CSS_VALUE_GROUP_COLOR},
    {"lavenderblush", 13, CSS_VALUE_LAVENDERBLUSH, CSS_VALUE_GROUP_COLOR},
    {"lawngreen", 9, CSS_VALUE_LAWNGREEN, CSS_VALUE_GROUP_COLOR},
    {"lemonchiffon", 12, CSS_VALUE_LEMONCHIFFON, CSS_VALUE_GROUP_COLOR},
    {"lightblue", 9, CSS_VALUE_LIGHTBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightcoral", 10, CSS_VALUE_LIGHTCORAL, CSS_VALUE_GROUP_COLOR},
    {"lightcyan", 9, CSS_VALUE_LIGHTCYAN, CSS_VALUE_GROUP_COLOR},
    {"lightgoldenrodyellow", 20, CSS_VALUE_LIGHTGOLDENRODYELLOW, CSS_VALUE_GROUP_COLOR},
    {"lightgray", 9, CSS_VALUE_LIGHTGRAY, CSS_VALUE_GROUP_COLOR},
    {"lightgreen", 10, CSS_VALUE_LIGHTGREEN, CSS_VALUE_GROUP_COLOR},
    {"lightgrey", 9, CSS_VALUE_LIGHTGREY, CSS_VALUE_GROUP_COLOR},
    {"lightpink", 9, CSS_VALUE_LIGHTPINK, CSS_VALUE_GROUP_COLOR},
    {"lightsalmon", 11, CSS_VALUE_LIGHTSALMON, CSS_VALUE_GROUP_COLOR},
    {"lightseagreen", 13, CSS_VALUE_LIGHTSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"lightskyblue", 12, CSS_VALUE_LIGHTSKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightslategray", 14, CSS_VALUE_LIGHTSLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"lightslategrey", 14, CSS_VALUE_LIGHTSLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"lightsteelblue", 14, CSS_VALUE_LIGHTSTEELBLUE, CSS_VALUE_GROUP_COLOR},
    {"lightyellow", 11, CSS_VALUE_LIGHTYELLOW, CSS_VALUE_GROUP_COLOR},
    {"lime", 4, CSS_VALUE_LIME, CSS_VALUE_GROUP_COLOR},
    {"limegreen", 9, CSS_VALUE_LIMEGREEN, CSS_VALUE_GROUP_COLOR},
    {"linen", 5, CSS_VALUE_LINEN, CSS_VALUE_GROUP_COLOR},
    {"magenta", 7, CSS_VALUE_MAGENTA, CSS_VALUE_GROUP_COLOR},
    {"maroon", 6, CSS_VALUE_MAROON, CSS_VALUE_GROUP_COLOR},
    {"mediumaquamarine", 16, CSS_VALUE_MEDIUMAQUAMARINE, CSS_VALUE_GROUP_COLOR},
    {"mediumblue", 10, CSS_VALUE_MEDIUMBLUE, CSS_VALUE_GROUP_COLOR},
    {"mediumorchid", 12, CSS_VALUE_MEDIUMORCHID, CSS_VALUE_GROUP_COLOR},
    {"mediumpurple", 12, CSS_VALUE_MEDIUMPURPLE, CSS_VALUE_GROUP_COLOR},
    {"mediumseagreen", 14, CSS_VALUE_MEDIUMSEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"mediumslateblue", 15, CSS_VALUE_MEDIUMSLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"mediumspringgreen", 17, CSS_VALUE_MEDIUMSPRINGGREEN, CSS_VALUE_GROUP_COLOR},
    {"mediumturquoise", 15, CSS_VALUE_MEDIUMTURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"mediumvioletred", 15, CSS_VALUE_MEDIUMVIOLETRED, CSS_VALUE_GROUP_COLOR},
    {"midnightblue", 12, CSS_VALUE_MIDNIGHTBLUE, CSS_VALUE_GROUP_COLOR},
    {"mintcream", 9, CSS_VALUE_MINTCREAM, CSS_VALUE_GROUP_COLOR},
    {"mistyrose", 9, CSS_VALUE_MISTYROSE, CSS_VALUE_GROUP_COLOR},
    {"moccasin", 8, CSS_VALUE_MOCCASIN, CSS_VALUE_GROUP_COLOR},
    {"navajowhite", 11, CSS_VALUE_NAVAJOWHITE, CSS_VALUE_GROUP_COLOR},
    {"navy", 4, CSS_VALUE_NAVY, CSS_VALUE_GROUP_COLOR},
    {"oldlace", 7, CSS_VALUE_OLDLACE, CSS_VALUE_GROUP_COLOR},
    {"olive", 5, CSS_VALUE_OLIVE, CSS_VALUE_GROUP_COLOR},
    {"olivedrab", 9, CSS_VALUE_OLIVEDRAB, CSS_VALUE_GROUP_COLOR},
    {"orange", 6, CSS_VALUE_ORANGE, CSS_VALUE_GROUP_COLOR},
    {"orangered", 9, CSS_VALUE_ORANGERED, CSS_VALUE_GROUP_COLOR},
    {"orchid", 6, CSS_VALUE_ORCHID, CSS_VALUE_GROUP_COLOR},
    {"palegoldenrod", 13, CSS_VALUE_PALEGOLDENROD, CSS_VALUE_GROUP_COLOR},
    {"palegreen", 9, CSS_VALUE_PALEGREEN, CSS_VALUE_GROUP_COLOR},
    {"paleturquoise", 13, CSS_VALUE_PALETURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"palevioletred", 13, CSS_VALUE_PALEVIOLETRED, CSS_VALUE_GROUP_COLOR},
    {"papayawhip", 10, CSS_VALUE_PAPAYAWHIP, CSS_VALUE_GROUP_COLOR},
    {"peachpuff", 9, CSS_VALUE_PEACHPUFF, CSS_VALUE_GROUP_COLOR},
    {"peru", 4, CSS_VALUE_PERU, CSS_VALUE_GROUP_COLOR},
    {"pink", 4, CSS_VALUE_PINK, CSS_VALUE_GROUP_COLOR},
    {"plum", 4, CSS_VALUE_PLUM, CSS_VALUE_GROUP_COLOR},
    {"powderblue", 10, CSS_VALUE_POWDERBLUE, CSS_VALUE_GROUP_COLOR},
    {"purple", 6, CSS_VALUE_PURPLE, CSS_VALUE_GROUP_COLOR},
    {"rebeccapurple", 13, CSS_VALUE_REBECCAPURPLE, CSS_VALUE_GROUP_COLOR},
    {"red", 3, CSS_VALUE_RED, CSS_VALUE_GROUP_COLOR},
    {"rosybrown", 9, CSS_VALUE_ROSYBROWN, CSS_VALUE_GROUP_COLOR},
    {"royalblue", 9, CSS_VALUE_ROYALBLUE, CSS_VALUE_GROUP_COLOR},
    {"saddlebrown", 11, CSS_VALUE_SADDLEBROWN, CSS_VALUE_GROUP_COLOR},
    {"salmon", 6, CSS_VALUE_SALMON, CSS_VALUE_GROUP_COLOR},
    {"sandybrown", 10, CSS_VALUE_SANDYBROWN, CSS_VALUE_GROUP_COLOR},
    {"seagreen", 8, CSS_VALUE_SEAGREEN, CSS_VALUE_GROUP_COLOR},
    {"seashell", 8, CSS_VALUE_SEASHELL, CSS_VALUE_GROUP_COLOR},
    {"sienna", 6, CSS_VALUE_SIENNA, CSS_VALUE_GROUP_COLOR},
    {"silver", 6, CSS_VALUE_SILVER, CSS_VALUE_GROUP_COLOR},
    {"skyblue", 7, CSS_VALUE_SKYBLUE, CSS_VALUE_GROUP_COLOR},
    {"slateblue", 9, CSS_VALUE_SLATEBLUE, CSS_VALUE_GROUP_COLOR},
    {"slategray", 9, CSS_VALUE_SLATEGRAY, CSS_VALUE_GROUP_COLOR},
    {"slategrey", 9, CSS_VALUE_SLATEGREY, CSS_VALUE_GROUP_COLOR},
    {"snow", 4, CSS_VALUE_SNOW, CSS_VALUE_GROUP_COLOR},
    {"springgreen", 11, CSS_VALUE_SPRINGGREEN, CSS_VALUE_GROUP_COLOR},
    {"steelblue", 9, CSS_VALUE_STEELBLUE, CSS_VALUE_GROUP_COLOR},
    {"tan", 3, CSS_VALUE_TAN, CSS_VALUE_GROUP_COLOR},
    {"teal", 4, CSS_VALUE_TEAL, CSS_VALUE_GROUP_COLOR},
    {"thistle", 7, CSS_VALUE_THISTLE, CSS_VALUE_GROUP_COLOR},
    {"tomato", 6, CSS_VALUE_TOMATO, CSS_VALUE_GROUP_COLOR},
    {"turquoise", 9, CSS_VALUE_TURQUOISE, CSS_VALUE_GROUP_COLOR},
    {"violet", 6, CSS_VALUE_VIOLET, CSS_VALUE_GROUP_COLOR},
    {"wheat", 5, CSS_VALUE_WHEAT, CSS_VALUE_GROUP_COLOR},
    {"white", 5, CSS_VALUE_WHITE, CSS_VALUE_GROUP_COLOR},
    {"whitesmoke", 10, CSS_VALUE_WHITESMOKE, CSS_VALUE_GROUP_COLOR},
    {"yellow", 6, CSS_VALUE_YELLOW, CSS_VALUE_GROUP_COLOR},
    {"yellowgreen", 11, CSS_VALUE_YELLOWGREEN, CSS_VALUE_GROUP_COLOR},
    {"Canvas", 6, CSS_VALUE_CANVAS, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"CanvasText", 10, CSS_VALUE_CANVASTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"LinkText", 8, CSS_VALUE_LINKTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"VisitedText", 11, CSS_VALUE_VISITEDTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ActiveText", 10, CSS_VALUE_ACTIVETEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonFace", 10, CSS_VALUE_BUTTONFACE, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonText", 10, CSS_VALUE_BUTTONTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"ButtonBorder", 12, CSS_VALUE_BUTTONBORDER, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Field", 5, CSS_VALUE_FIELD, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"FieldText", 9, CSS_VALUE_FIELDTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Highlight", 9, CSS_VALUE_HIGHLIGHT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"HighlightText", 13, CSS_VALUE_HIGHLIGHTTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"SelectedItem", 12, CSS_VALUE_SELECTEDITEM, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"SelectedItemText", 16, CSS_VALUE_SELECTEDITEMTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"Mark", 4, CSS_VALUE_MARK, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"MarkText", 8, CSS_VALUE_MARKTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"GrayText", 8, CSS_VALUE_GRAYTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"AccentColor", 11, CSS_VALUE_ACCENTCOLOR, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"AccentColorText", 15, CSS_VALUE_ACCENTCOLORTEXT, CSS_VALUE_GROUP_SYSTEM_COLOR},
    {"rgb", 3, CSS_VALUE_RGB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"rgba", 4, CSS_VALUE_RGBA, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hsl", 3, CSS_VALUE_HSL, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hsla", 4, CSS_VALUE_HSLA, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hwb", 3, CSS_VALUE_HWB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"lab", 3, CSS_VALUE_LAB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"lch", 3, CSS_VALUE_LCH, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"oklab", 5, CSS_VALUE_OKLAB, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"oklch", 5, CSS_VALUE_OKLCH, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"color", 5, CSS_VALUE_COLOR, CSS_VALUE_GROUP_COLOR_FUNCTION},
    {"hand", 4, CSS_VALUE_HAND, CSS_VALUE_GROUP_CURSOR},
    {"pointer", 7, CSS_VALUE_POINTER, CSS_VALUE_GROUP_CURSOR},
    {"text", 4, CSS_VALUE_TEXT, CSS_VALUE_GROUP_CURSOR},
    {"wait", 4, CSS_VALUE_WAIT, CSS_VALUE_GROUP_CURSOR},
    {"progress", 8, CSS_VALUE_PROGRESS, CSS_VALUE_GROUP_CURSOR},
    {"grab", 4, CSS_VALUE_GRAB, CSS_VALUE_GROUP_CURSOR},
    {"grabbing", 8, CSS_VALUE_GRABBING, CSS_VALUE_GROUP_CURSOR},
    {"move", 4, CSS_VALUE_MOVE, CSS_VALUE_GROUP_CURSOR},
    {"bar", 3, CSS_VALUE_BAR, CSS_VALUE_GROUP_CARET_SHAPE},
    {"underscore", 10, CSS_VALUE_UNDERSCORE, CSS_VALUE_GROUP_CARET_SHAPE},
    {"ltr", 3, CSS_VALUE_LTR, CSS_VALUE_GROUP_DIRECTION},
    {"rtl", 3, CSS_VALUE_RTL, CSS_VALUE_GROUP_DIRECTION},
    {"block", 5, CSS_VALUE_BLOCK, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"inline", 6, CSS_VALUE_INLINE, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"run-in", 6, CSS_VALUE_RUN_IN, CSS_VALUE_GROUP_DISPLAY_OUTSIDE},
    {"flow", 4, CSS_VALUE_FLOW, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"flow-root", 9, CSS_VALUE_FLOW_ROOT, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"table", 5, CSS_VALUE_TABLE, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"flex", 4, CSS_VALUE_FLEX, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"grid", 4, CSS_VALUE_GRID, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"ruby", 4, CSS_VALUE_RUBY, CSS_VALUE_GROUP_DISPLAY_INSIDE},
    {"list-item", 9, CSS_VALUE_LIST_ITEM, CSS_VALUE_GROUP_DISPLAY_LISTITEM},
    {"table-row-group", 15, CSS_VALUE_TABLE_ROW_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-header-group", 18, CSS_VALUE_TABLE_HEADER_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-footer-group", 18, CSS_VALUE_TABLE_FOOTER_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-row", 9, CSS_VALUE_TABLE_ROW, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-cell", 10, CSS_VALUE_TABLE_CELL, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-column-group", 18, CSS_VALUE_TABLE_COLUMN_GROUP, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-column", 12, CSS_VALUE_TABLE_COLUMN, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"table-caption", 13, CSS_VALUE_TABLE_CAPTION, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-base", 9, CSS_VALUE_RUBY_BASE, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-text", 9, CSS_VALUE_RUBY_TEXT, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-base-container", 19, CSS_VALUE_RUBY_BASE_CONTAINER, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"ruby-text-container", 19, CSS_VALUE_RUBY_TEXT_CONTAINER, CSS_VALUE_GROUP_DISPLAY_INTERNAL},
    {"contents", 8, CSS_VALUE_CONTENTS, CSS_VALUE_GROUP_DISPLAY_BOX},
    {"inline-block", 12, CSS_VALUE_INLINE_BLOCK, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-table", 12, CSS_VALUE_INLINE_TABLE, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-flex", 11, CSS_VALUE_INLINE_FLEX, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"inline-grid", 11, CSS_VALUE_INLINE_GRID, CSS_VALUE_GROUP_DISPLAY_LEGACY},
    {"hanging", 7, CSS_VALUE_HANGING, CSS_VALUE_GROUP_VERTICAL_ALIGN},
    {"content", 7, CSS_VALUE_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"row", 3, CSS_VALUE_ROW, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"row-reverse", 11, CSS_VALUE_ROW_REVERSE, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"column", 6, CSS_VALUE_COLUMN, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"column-reverse", 14, CSS_VALUE_COLUMN_REVERSE, CSS_VALUE_GROUP_FLEX_DIRECTION},
    {"balance", 7, CSS_VALUE_BALANCE, CSS_VALUE_GROUP_MISC},
    {"nowrap", 6, CSS_VALUE_NOWRAP, CSS_VALUE_GROUP_FLEX_WRAP},
    {"wrap", 4, CSS_VALUE_WRAP, CSS_VALUE_GROUP_FLEX_WRAP},
    {"wrap-reverse", 12, CSS_VALUE_WRAP_REVERSE, CSS_VALUE_GROUP_FLEX_WRAP},
    {"snap-block", 10, CSS_VALUE_SNAP_BLOCK, CSS_VALUE_GROUP_MISC},
    {"start", 5, CSS_VALUE_START, CSS_VALUE_GROUP_ALIGNMENT},
    {"end", 3, CSS_VALUE_END, CSS_VALUE_GROUP_ALIGNMENT},
    {"anchor-center", 13, CSS_VALUE_ANCHOR_CENTER, CSS_VALUE_GROUP_ALIGNMENT},
    {"self-start", 10, CSS_VALUE_SELF_START, CSS_VALUE_GROUP_ALIGNMENT},
    {"self-end", 8, CSS_VALUE_SELF_END, CSS_VALUE_GROUP_ALIGNMENT},
    {"safe", 4, CSS_VALUE_SAFE, CSS_VALUE_GROUP_ALIGNMENT},
    {"unsafe", 6, CSS_VALUE_UNSAFE, CSS_VALUE_GROUP_ALIGNMENT},
    {"near", 4, CSS_VALUE_NEAR, CSS_VALUE_GROUP_MISC},
    {"snap-inline", 11, CSS_VALUE_SNAP_INLINE, CSS_VALUE_GROUP_MISC},
    {"region", 6, CSS_VALUE_REGION, CSS_VALUE_GROUP_MISC},
    {"page", 4, CSS_VALUE_PAGE, CSS_VALUE_GROUP_MISC},
    {"serif", 5, CSS_VALUE_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"sans-serif", 10, CSS_VALUE_SANS_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"cursive", 7, CSS_VALUE_CURSIVE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"fantasy", 7, CSS_VALUE_FANTASY, CSS_VALUE_GROUP_FONT_FAMILY},
    {"monospace", 9, CSS_VALUE_MONOSPACE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"system-ui", 9, CSS_VALUE_SYSTEM_UI, CSS_VALUE_GROUP_FONT_FAMILY},
    {"emoji", 5, CSS_VALUE_EMOJI, CSS_VALUE_GROUP_FONT_FAMILY},
    {"math", 4, CSS_VALUE_MATH, CSS_VALUE_GROUP_FONT_FAMILY},
    {"fangsong", 8, CSS_VALUE_FANGSONG, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-serif", 8, CSS_VALUE_UI_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-sans-serif", 13, CSS_VALUE_UI_SANS_SERIF, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-monospace", 12, CSS_VALUE_UI_MONOSPACE, CSS_VALUE_GROUP_FONT_FAMILY},
    {"ui-rounded", 10, CSS_VALUE_UI_ROUNDED, CSS_VALUE_GROUP_FONT_FAMILY},
    {"xx-small", 8, CSS_VALUE_XX_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"x-small", 7, CSS_VALUE_X_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"small", 5, CSS_VALUE_SMALL, CSS_VALUE_GROUP_FONT_SIZE},
    {"large", 5, CSS_VALUE_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"x-large", 7, CSS_VALUE_X_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"xx-large", 8, CSS_VALUE_XX_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"xxx-large", 9, CSS_VALUE_XXX_LARGE, CSS_VALUE_GROUP_FONT_SIZE},
    {"larger", 6, CSS_VALUE_LARGER, CSS_VALUE_GROUP_FONT_SIZE},
    {"smaller", 7, CSS_VALUE_SMALLER, CSS_VALUE_GROUP_FONT_SIZE},
    {"normal", 6, CSS_VALUE_NORMAL, CSS_VALUE_GROUP_FONT_STYLE},
    {"ultra-condensed", 15, CSS_VALUE_ULTRA_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"extra-condensed", 15, CSS_VALUE_EXTRA_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"condensed", 9, CSS_VALUE_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"semi-condensed", 14, CSS_VALUE_SEMI_CONDENSED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"semi-expanded", 13, CSS_VALUE_SEMI_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"expanded", 8, CSS_VALUE_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"extra-expanded", 14, CSS_VALUE_EXTRA_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"ultra-expanded", 14, CSS_VALUE_ULTRA_EXPANDED, CSS_VALUE_GROUP_FONT_STRETCH},
    {"italic", 6, CSS_VALUE_ITALIC, CSS_VALUE_GROUP_FONT_STYLE},
    {"oblique", 7, CSS_VALUE_OBLIQUE, CSS_VALUE_GROUP_FONT_STYLE},
    {"bold", 4, CSS_VALUE_BOLD, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"bolder", 6, CSS_VALUE_BOLDER, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"lighter", 7, CSS_VALUE_LIGHTER, CSS_VALUE_GROUP_FONT_WEIGHT},
    {"force-end", 9, CSS_VALUE_FORCE_END, CSS_VALUE_GROUP_MISC},
    {"allow-end", 9, CSS_VALUE_ALLOW_END, CSS_VALUE_GROUP_MISC},
    {"min-content", 11, CSS_VALUE_MIN_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"max-content", 11, CSS_VALUE_MAX_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"manual", 6, CSS_VALUE_MANUAL, CSS_VALUE_GROUP_LINE_BREAK},
    {"loose", 5, CSS_VALUE_LOOSE, CSS_VALUE_GROUP_LINE_BREAK},
    {"strict", 6, CSS_VALUE_STRICT, CSS_VALUE_GROUP_LINE_BREAK},
    {"anywhere", 8, CSS_VALUE_ANYWHERE, CSS_VALUE_GROUP_LINE_BREAK},
    {"visible", 7, CSS_VALUE_VISIBLE, CSS_VALUE_GROUP_OVERFLOW},
    {"clip", 4, CSS_VALUE_CLIP, CSS_VALUE_GROUP_OVERFLOW},
    {"scroll", 6, CSS_VALUE_SCROLL, CSS_VALUE_GROUP_OVERFLOW},
    {"break-word", 10, CSS_VALUE_BREAK_WORD, CSS_VALUE_GROUP_OVERFLOW_WRAP},
    {"static", 6, CSS_VALUE_STATIC, CSS_VALUE_GROUP_POSITION},
    {"relative", 8, CSS_VALUE_RELATIVE, CSS_VALUE_GROUP_POSITION},
    {"absolute", 8, CSS_VALUE_ABSOLUTE, CSS_VALUE_GROUP_POSITION},
    {"sticky", 6, CSS_VALUE_STICKY, CSS_VALUE_GROUP_POSITION},
    {"fixed", 5, CSS_VALUE_FIXED, CSS_VALUE_GROUP_POSITION},
    {"justify", 7, CSS_VALUE_JUSTIFY, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"match-parent", 12, CSS_VALUE_MATCH_PARENT, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"justify-all", 11, CSS_VALUE_JUSTIFY_ALL, CSS_VALUE_GROUP_TEXT_ALIGN},
    {"all", 3, CSS_VALUE_ALL, CSS_VALUE_GROUP_MISC},
    {"digits", 6, CSS_VALUE_DIGITS, CSS_VALUE_GROUP_MISC},
    {"underline", 9, CSS_VALUE_UNDERLINE, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"overline", 8, CSS_VALUE_OVERLINE, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"line-through", 12, CSS_VALUE_LINE_THROUGH, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"blink", 5, CSS_VALUE_BLINK, CSS_VALUE_GROUP_TEXT_DECO_LINE},
    {"wavy", 4, CSS_VALUE_WAVY, CSS_VALUE_GROUP_TEXT_DECO_STYLE},
    {"each-line", 9, CSS_VALUE_EACH_LINE, CSS_VALUE_GROUP_MISC},
    {"inter-word", 10, CSS_VALUE_INTER_WORD, CSS_VALUE_GROUP_TEXT_JUSTIFY},
    {"inter-character", 15, CSS_VALUE_INTER_CHARACTER, CSS_VALUE_GROUP_TEXT_JUSTIFY},
    {"mixed", 5, CSS_VALUE_MIXED, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"upright", 7, CSS_VALUE_UPRIGHT, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"sideways", 8, CSS_VALUE_SIDEWAYS, CSS_VALUE_GROUP_TEXT_ORIENTATION},
    {"ellipsis", 8, CSS_VALUE_ELLIPSIS, CSS_VALUE_GROUP_TEXT_OVERFLOW},
    {"capitalize", 10, CSS_VALUE_CAPITALIZE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"uppercase", 9, CSS_VALUE_UPPERCASE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"lowercase", 9, CSS_VALUE_LOWERCASE, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"full-width", 10, CSS_VALUE_FULL_WIDTH, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"full-size-kana", 14, CSS_VALUE_FULL_SIZE_KANA, CSS_VALUE_GROUP_TEXT_TRANSFORM},
    {"embed", 5, CSS_VALUE_EMBED, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"isolate", 7, CSS_VALUE_ISOLATE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"bidi-override", 13, CSS_VALUE_BIDI_OVERRIDE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"isolate-override", 16, CSS_VALUE_ISOLATE_OVERRIDE, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"plaintext", 9, CSS_VALUE_PLAINTEXT, CSS_VALUE_GROUP_UNICODE_BIDI},
    {"collapse", 8, CSS_VALUE_COLLAPSE, CSS_VALUE_GROUP_MISC},
    {"pre", 3, CSS_VALUE_PRE, CSS_VALUE_GROUP_WHITE_SPACE},
    {"pre-wrap", 8, CSS_VALUE_PRE_WRAP, CSS_VALUE_GROUP_WHITE_SPACE},
    {"break-spaces", 12, CSS_VALUE_BREAK_SPACES, CSS_VALUE_GROUP_WHITE_SPACE},
    {"pre-line", 8, CSS_VALUE_PRE_LINE, CSS_VALUE_GROUP_WHITE_SPACE},
    {"keep-all", 8, CSS_VALUE_KEEP_ALL, CSS_VALUE_GROUP_WORD_BREAK},
    {"break-all", 9, CSS_VALUE_BREAK_ALL, CSS_VALUE_GROUP_WORD_BREAK},
    {"both", 4, CSS_VALUE_BOTH, CSS_VALUE_GROUP_CLEAR},
    {"minimum", 7, CSS_VALUE_MINIMUM, CSS_VALUE_GROUP_MISC},
    {"maximum", 7, CSS_VALUE_MAXIMUM, CSS_VALUE_GROUP_MISC},
    {"clear", 5, CSS_VALUE_CLEAR, CSS_VALUE_GROUP_CLEAR},
    {"horizontal-tb", 13, CSS_VALUE_HORIZONTAL_TB, CSS_VALUE_GROUP_WRITING_MODE},
    {"vertical-rl", 11, CSS_VALUE_VERTICAL_RL, CSS_VALUE_GROUP_WRITING_MODE},
    {"vertical-lr", 11, CSS_VALUE_VERTICAL_LR, CSS_VALUE_GROUP_WRITING_MODE},
    {"sideways-rl", 11, CSS_VALUE_SIDEWAYS_RL, CSS_VALUE_GROUP_WRITING_MODE},
    {"sideways-lr", 11, CSS_VALUE_SIDEWAYS_LR, CSS_VALUE_GROUP_WRITING_MODE},
    {"disc", 4, CSS_VALUE_DISC, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"circle", 6, CSS_VALUE_CIRCLE, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"square", 6, CSS_VALUE_SQUARE, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"decimal", 7, CSS_VALUE_DECIMAL, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-roman", 11, CSS_VALUE_LOWER_ROMAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"upper-roman", 11, CSS_VALUE_UPPER_ROMAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-alpha", 11, CSS_VALUE_LOWER_ALPHA, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"upper-alpha", 11, CSS_VALUE_UPPER_ALPHA, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-latin", 11, CSS_VALUE_LOWER_LATIN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"upper-latin", 11, CSS_VALUE_UPPER_LATIN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"decimal-leading-zero", 20, CSS_VALUE_DECIMAL_LEADING_ZERO, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"lower-greek", 11, CSS_VALUE_LOWER_GREEK, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"armenian", 8, CSS_VALUE_ARMENIAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"georgian", 8, CSS_VALUE_GEORGIAN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"disclosure-closed", 17, CSS_VALUE_DISCLOSURE_CLOSED, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"disclosure-open", 15, CSS_VALUE_DISCLOSURE_OPEN, CSS_VALUE_GROUP_LIST_STYLE_TYPE},
    {"space-evenly", 12, CSS_VALUE_SPACE_EVENLY, CSS_VALUE_GROUP_FLEX_JUSTIFY},
    {"contain", 7, CSS_VALUE_CONTAIN, CSS_VALUE_GROUP_BGROUND_SIZE},
    {"cover", 5, CSS_VALUE_COVER, CSS_VALUE_GROUP_BGROUND_SIZE},
    {"local", 5, CSS_VALUE_LOCAL, CSS_VALUE_GROUP_BGROUND_ATTACHMENT},
    {"padding-box", 11, CSS_VALUE_PADDING_BOX, CSS_VALUE_GROUP_BOX_MODEL},
    {"multiply", 8, CSS_VALUE_MULTIPLY, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"overlay", 7, CSS_VALUE_OVERLAY, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"screen", 6, CSS_VALUE_SCREEN, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"darken", 6, CSS_VALUE_DARKEN, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"lighten", 7, CSS_VALUE_LIGHTEN, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"color-dodge", 11, CSS_VALUE_COLOR_DODGE, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"color-burn", 10, CSS_VALUE_COLOR_BURN, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"hard-light", 10, CSS_VALUE_HARD_LIGHT, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"soft-light", 10, CSS_VALUE_SOFT_LIGHT, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"difference", 10, CSS_VALUE_DIFFERENCE, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"exclusion", 9, CSS_VALUE_EXCLUSION, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"hue", 3, CSS_VALUE_HUE_BLEND, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"saturation", 10, CSS_VALUE_SATURATION_BLEND, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"color", 5, CSS_VALUE_COLOR_BLEND, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"luminosity", 10, CSS_VALUE_LUMINOSITY_BLEND, CSS_VALUE_GROUP_BGROUND_BLEND},
    {"round", 5, CSS_VALUE_ROUND, CSS_VALUE_GROUP_BGROUND_REPEAT},
    {"space", 5, CSS_VALUE_SPACE, CSS_VALUE_GROUP_BGROUND_REPEAT},
    // Table properties (must match enum order: COLLAPSE_TABLE, SEPARATE, HIDE, SHOW)
    {"collapse-table", 14, CSS_VALUE_COLLAPSE_TABLE, CSS_VALUE_GROUP_BORDER_COLLAPSE},
    {"separate", 8, CSS_VALUE_SEPARATE, CSS_VALUE_GROUP_BORDER_COLLAPSE},
    {"hide", 4, CSS_VALUE_HIDE, CSS_VALUE_GROUP_EMPTY_CELLS},
    {"show", 4, CSS_VALUE_SHOW, CSS_VALUE_GROUP_EMPTY_CELLS},
    // Grid layout
    {"fit-content", 11, CSS_VALUE_FIT_CONTENT, CSS_VALUE_GROUP_SIZE},
    {"fr", 2, CSS_VALUE_FR, CSS_VALUE_GROUP_MISC},
    {"dense", 5, CSS_VALUE_DENSE, CSS_VALUE_GROUP_GRID_AUTO_FLOW},
    {"auto-fill", 9, CSS_VALUE_AUTO_FILL, CSS_VALUE_GROUP_GRID_AUTO_FLOW},
    {"auto-fit", 8, CSS_VALUE_AUTO_FIT, CSS_VALUE_GROUP_GRID_AUTO_FLOW},
    // font-variant values
    {"small-caps", 10, CSS_VALUE_SMALL_CAPS, CSS_VALUE_GROUP_MISC},
    // object-fit values (must match enum order: FILL, SCALE_DOWN after SMALL_CAPS)
    {"fill", 4, CSS_VALUE_FILL, CSS_VALUE_GROUP_OBJECT_FIT},
    {"scale-down", 10, CSS_VALUE_SCALE_DOWN, CSS_VALUE_GROUP_OBJECT_FIT},
    {"smooth", 6, CSS_VALUE_SMOOTH, CSS_VALUE_GROUP_MISC},
    {"high-quality", 12, CSS_VALUE_HIGH_QUALITY, CSS_VALUE_GROUP_MISC},
    {"pixelated", 9, CSS_VALUE_PIXELATED, CSS_VALUE_GROUP_MISC},
    {"crisp-edges", 11, CSS_VALUE_CRISP_EDGES, CSS_VALUE_GROUP_MISC},
    {"optimizespeed", 13, CSS_VALUE_OPTIMIZE_SPEED, CSS_VALUE_GROUP_MISC},
    {"optimizequality", 15, CSS_VALUE_OPTIMIZE_QUALITY, CSS_VALUE_GROUP_MISC},
    // text-box-trim values
    {"trim-start", 10, CSS_VALUE_TRIM_START, CSS_VALUE_GROUP_TEXT_BOX_TRIM},
    {"trim-end", 8, CSS_VALUE_TRIM_END, CSS_VALUE_GROUP_TEXT_BOX_TRIM},
    {"trim-both", 9, CSS_VALUE_TRIM_BOTH, CSS_VALUE_GROUP_TEXT_BOX_TRIM},
    {"space-all", 9, CSS_VALUE_SPACE_ALL, CSS_VALUE_GROUP_MISC},
    {"space-first", 11, CSS_VALUE_SPACE_FIRST, CSS_VALUE_GROUP_MISC},
    {"trim-all", 8, CSS_VALUE_TRIM_ALL, CSS_VALUE_GROUP_MISC},
    // text-box-edge values (auto, text, alphabetic, ideographic reuse existing enums)
    {"cap", 3, CSS_VALUE_CAP, CSS_VALUE_GROUP_TEXT_BOX_EDGE},
    {"ex", 2, CSS_VALUE_EX, CSS_VALUE_GROUP_TEXT_BOX_EDGE},
    {"slice", 5, CSS_VALUE_SLICE, CSS_VALUE_GROUP_MISC},
    {"clone", 5, CSS_VALUE_CLONE, CSS_VALUE_GROUP_MISC},
    // Background repeat values
    {"repeat", 6, CSS_VALUE_REPEAT, CSS_VALUE_GROUP_BGROUND_REPEAT},
    {"no-repeat", 9, CSS_VALUE_NO_REPEAT, CSS_VALUE_GROUP_BGROUND_REPEAT},
    // CSS 2.1 §15.8: System font keywords for font shorthand
    {"caption", 7, CSS_VALUE_CAPTION, CSS_VALUE_GROUP_SYSTEM_FONT},
    {"icon", 4, CSS_VALUE_ICON, CSS_VALUE_GROUP_SYSTEM_FONT},
    {"menu", 4, CSS_VALUE_MENU, CSS_VALUE_GROUP_SYSTEM_FONT},
    {"message-box", 11, CSS_VALUE_MESSAGE_BOX, CSS_VALUE_GROUP_SYSTEM_FONT},
    {"small-caption", 13, CSS_VALUE_SMALL_CAPTION, CSS_VALUE_GROUP_SYSTEM_FONT},
    {"status-bar", 10, CSS_VALUE_STATUS_BAR, CSS_VALUE_GROUP_SYSTEM_FONT},
    // Animation timing function keywords
    {"ease", 4, CSS_VALUE_EASE, CSS_VALUE_GROUP_ANIMATION},
    {"ease-in", 7, CSS_VALUE_EASE_IN, CSS_VALUE_GROUP_ANIMATION},
    {"ease-out", 8, CSS_VALUE_EASE_OUT, CSS_VALUE_GROUP_ANIMATION},
    {"ease-in-out", 11, CSS_VALUE_EASE_IN_OUT, CSS_VALUE_GROUP_ANIMATION},
    {"linear", 6, CSS_VALUE_LINEAR, CSS_VALUE_GROUP_ANIMATION},
    {"step-start", 10, CSS_VALUE_STEP_START, CSS_VALUE_GROUP_ANIMATION},
    {"step-end", 8, CSS_VALUE_STEP_END, CSS_VALUE_GROUP_ANIMATION},
    // Animation direction keywords
    {"reverse", 7, CSS_VALUE_REVERSE, CSS_VALUE_GROUP_ANIMATION},
    {"alternate", 9, CSS_VALUE_ALTERNATE, CSS_VALUE_GROUP_ANIMATION},
    {"alternate-reverse", 17, CSS_VALUE_ALTERNATE_REVERSE, CSS_VALUE_GROUP_ANIMATION},
    {"over", 4, CSS_VALUE_OVER, CSS_VALUE_GROUP_RUBY_POSITION},
    {"under", 5, CSS_VALUE_UNDER, CSS_VALUE_GROUP_RUBY_POSITION},
    // Animation fill-mode keywords
    {"forwards", 8, CSS_VALUE_FORWARDS, CSS_VALUE_GROUP_ANIMATION},
    {"backwards", 9, CSS_VALUE_BACKWARDS, CSS_VALUE_GROUP_ANIMATION},
    // Animation play-state keywords
    {"running", 7, CSS_VALUE_RUNNING, CSS_VALUE_GROUP_ANIMATION},
    {"paused", 6, CSS_VALUE_PAUSED, CSS_VALUE_GROUP_ANIMATION},
    // Animation iteration-count keyword
    {"infinite", 8, CSS_VALUE_INFINITE, CSS_VALUE_GROUP_ANIMATION},
    {"base-select", 11, CSS_VALUE_BASE_SELECT, CSS_VALUE_GROUP_MISC},
    {"no-autospace", 12, CSS_VALUE_NO_AUTOSPACE, CSS_VALUE_GROUP_MISC},
    {"ideograph-alpha", 15, CSS_VALUE_IDEOGRAPH_ALPHA, CSS_VALUE_GROUP_MISC},
    {"ideograph-numeric", 17, CSS_VALUE_IDEOGRAPH_NUMERIC, CSS_VALUE_GROUP_MISC},
    {"punctuation", 11, CSS_VALUE_PUNCTUATION, CSS_VALUE_GROUP_MISC},
    {"insert", 6, CSS_VALUE_INSERT, CSS_VALUE_GROUP_MISC},
    {"replace", 7, CSS_VALUE_REPLACE, CSS_VALUE_GROUP_MISC},
    {"flat", 4, CSS_VALUE_FLAT, CSS_VALUE_GROUP_MISC},
    {"preserve-3d", 11, CSS_VALUE_PRESERVE_3D, CSS_VALUE_GROUP_MISC},
    {"from-font", 9, CSS_VALUE_FROM_FONT, CSS_VALUE_GROUP_MISC},
    {"chain", 5, CSS_VALUE_CHAIN, CSS_VALUE_GROUP_MISC},
    {"always", 6, CSS_VALUE_ALWAYS, CSS_VALUE_GROUP_MISC},
    {"avoid-page", 10, CSS_VALUE_AVOID_PAGE, CSS_VALUE_GROUP_MISC},
    {"avoid-column", 12, CSS_VALUE_AVOID_COLUMN, CSS_VALUE_GROUP_MISC},
    {"avoid-region", 12, CSS_VALUE_AVOID_REGION, CSS_VALUE_GROUP_MISC},
    {"recto", 5, CSS_VALUE_RECTO, CSS_VALUE_GROUP_MISC},
    {"verso", 5, CSS_VALUE_VERSO, CSS_VALUE_GROUP_MISC},
    {"footnote", 8, CSS_VALUE_FOOTNOTE, CSS_VALUE_GROUP_MISC},
    {"_replaced", 9, CSS_VALUE__REPLACED, CSS_VALUE_GROUP_RADINT},
    {"col-resize", 10, CSS_VALUE_COL_RESIZE, CSS_VALUE_GROUP_CURSOR},
    {"row-resize", 10, CSS_VALUE_ROW_RESIZE, CSS_VALUE_GROUP_CURSOR},
};

static const size_t css_value_definitions_count = sizeof(css_value_definitions) / sizeof(css_value_definitions[0]);

const CssEnumInfo* css_enum_info(CssEnum id) {
    // Direct indexing - array is ordered to match enum values
    if (id >= 0 && id < (int)css_value_definitions_count) {
        return &css_value_definitions[id];
    }
    return &css_value_definitions[0]; // return _undef for unknown IDs
}

// hash function for CSS keyword strings (case-insensitive) uses the shared callback.

// comparison function for CSS keyword strings (case-insensitive) uses the shared callback.

// Look up CSS value by name (case-insensitive)
// Returns the LXB_CSS_VALUE enum, or CSS_VALUE__UNDEF if not found
CssEnum css_enum_by_name(const char* name) {
    if (!name) return CSS_VALUE__UNDEF;

    // the legacy WebKit spelling is a CSS Sizing `stretch` alias; leaving it
    // custom discards the sizing declaration before layout can resolve it.
    static const char webkit_fill_available[] = "-webkit-fill-available";
    if (str_icmp(name, strlen(name), webkit_fill_available,
                 sizeof(webkit_fill_available) - 1) == 0) {
        return CSS_VALUE_STRETCH;
    }

    static HashMap* keyword_cache = NULL;

    // initialize hashmap on first use
    if (!keyword_cache) {
        keyword_cache = hashmap_new(
            sizeof(const char*),  // key is pointer to string
            css_value_definitions_count,  // initial capacity
            0, 0,                 // seeds (0 means random)
            hashmap_hash_icstr_ptr,
            hashmap_compare_icstr_ptr,
            NULL,                 // no element free function
            (void*)css_value_definitions  // udata pointing to value table
        );

        // populate hashmap with all keywords
        for (size_t i = 0; i < css_value_definitions_count; i++) {
            const char* keyword = css_value_definitions[i].name;
            hashmap_set(keyword_cache, &keyword);
        }
    }

    // lookup in hashmap
    const char** result = (const char**)hashmap_get(keyword_cache, &name);
    if (result) {
        // find index in table to get the unique value
        const char* found_name = *result;
        for (size_t i = 0; i < css_value_definitions_count; i++) {
            if (css_value_definitions[i].name == found_name) {
                return css_value_definitions[i].enum_id;
            }
        }
    }

    return CSS_VALUE__UNDEF;
}

bool css_named_color_to_rgba(CssEnum keyword, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    uint32_t c;
    switch (keyword) {
        case CSS_VALUE_TRANSPARENT: *r=0; *g=0; *b=0; *a=0; return true;
        case CSS_VALUE_ALICEBLUE: c=0xF0F8FF; break;
        case CSS_VALUE_ANTIQUEWHITE: c=0xFAEBD7; break;
        case CSS_VALUE_AQUA: c=0x00FFFF; break;
        case CSS_VALUE_AQUAMARINE: c=0x7FFFD4; break;
        case CSS_VALUE_AZURE: c=0xF0FFFF; break;
        case CSS_VALUE_BEIGE: c=0xF5F5DC; break;
        case CSS_VALUE_BISQUE: c=0xFFE4C4; break;
        case CSS_VALUE_BLACK: c=0x000000; break;
        case CSS_VALUE_BLANCHEDALMOND: c=0xFFEBCD; break;
        case CSS_VALUE_BLUE: c=0x0000FF; break;
        case CSS_VALUE_BLUEVIOLET: c=0x8A2BE2; break;
        case CSS_VALUE_BROWN: c=0xA52A2A; break;
        case CSS_VALUE_BURLYWOOD: c=0xDEB887; break;
        case CSS_VALUE_CADETBLUE: c=0x5F9EA0; break;
        case CSS_VALUE_CHARTREUSE: c=0x7FFF00; break;
        case CSS_VALUE_CHOCOLATE: c=0xD2691E; break;
        case CSS_VALUE_CORAL: c=0xFF7F50; break;
        case CSS_VALUE_CORNFLOWERBLUE: c=0x6495ED; break;
        case CSS_VALUE_CORNSILK: c=0xFFF8DC; break;
        case CSS_VALUE_CRIMSON: c=0xDC143C; break;
        case CSS_VALUE_CYAN: c=0x00FFFF; break;
        case CSS_VALUE_DARKBLUE: c=0x00008B; break;
        case CSS_VALUE_DARKCYAN: c=0x008B8B; break;
        case CSS_VALUE_DARKGOLDENROD: c=0xB8860B; break;
        case CSS_VALUE_DARKGRAY: c=0xA9A9A9; break;
        case CSS_VALUE_DARKGREEN: c=0x006400; break;
        case CSS_VALUE_DARKGREY: c=0xA9A9A9; break;
        case CSS_VALUE_DARKKHAKI: c=0xBDB76B; break;
        case CSS_VALUE_DARKMAGENTA: c=0x8B008B; break;
        case CSS_VALUE_DARKOLIVEGREEN: c=0x556B2F; break;
        case CSS_VALUE_DARKORANGE: c=0xFF8C00; break;
        case CSS_VALUE_DARKORCHID: c=0x9932CC; break;
        case CSS_VALUE_DARKRED: c=0x8B0000; break;
        case CSS_VALUE_DARKSALMON: c=0xE9967A; break;
        case CSS_VALUE_DARKSEAGREEN: c=0x8FBC8F; break;
        case CSS_VALUE_DARKSLATEBLUE: c=0x483D8B; break;
        case CSS_VALUE_DARKSLATEGRAY: c=0x2F4F4F; break;
        case CSS_VALUE_DARKSLATEGREY: c=0x2F4F4F; break;
        case CSS_VALUE_DARKTURQUOISE: c=0x00CED1; break;
        case CSS_VALUE_DARKVIOLET: c=0x9400D3; break;
        case CSS_VALUE_DEEPPINK: c=0xFF1493; break;
        case CSS_VALUE_DEEPSKYBLUE: c=0x00BFFF; break;
        case CSS_VALUE_DIMGRAY: c=0x696969; break;
        case CSS_VALUE_DIMGREY: c=0x696969; break;
        case CSS_VALUE_DODGERBLUE: c=0x1E90FF; break;
        case CSS_VALUE_FIREBRICK: c=0xB22222; break;
        case CSS_VALUE_FLORALWHITE: c=0xFFFAF0; break;
        case CSS_VALUE_FORESTGREEN: c=0x228B22; break;
        case CSS_VALUE_FUCHSIA: c=0xFF00FF; break;
        case CSS_VALUE_GAINSBORO: c=0xDCDCDC; break;
        case CSS_VALUE_GHOSTWHITE: c=0xF8F8FF; break;
        case CSS_VALUE_GOLD: c=0xFFD700; break;
        case CSS_VALUE_GOLDENROD: c=0xDAA520; break;
        case CSS_VALUE_GRAY: c=0x808080; break;
        case CSS_VALUE_GREEN: c=0x008000; break;
        case CSS_VALUE_GREENYELLOW: c=0xADFF2F; break;
        case CSS_VALUE_GREY: c=0x808080; break;
        case CSS_VALUE_HONEYDEW: c=0xF0FFF0; break;
        case CSS_VALUE_HOTPINK: c=0xFF69B4; break;
        case CSS_VALUE_INDIANRED: c=0xCD5C5C; break;
        case CSS_VALUE_INDIGO: c=0x4B0082; break;
        case CSS_VALUE_IVORY: c=0xFFFFF0; break;
        case CSS_VALUE_KHAKI: c=0xF0E68C; break;
        case CSS_VALUE_LAVENDER: c=0xE6E6FA; break;
        case CSS_VALUE_LAVENDERBLUSH: c=0xFFF0F5; break;
        case CSS_VALUE_LAWNGREEN: c=0x7CFC00; break;
        case CSS_VALUE_LEMONCHIFFON: c=0xFFFACD; break;
        case CSS_VALUE_LIGHTBLUE: c=0xADD8E6; break;
        case CSS_VALUE_LIGHTCORAL: c=0xF08080; break;
        case CSS_VALUE_LIGHTCYAN: c=0xE0FFFF; break;
        case CSS_VALUE_LIGHTGOLDENRODYELLOW: c=0xFAFAD2; break;
        case CSS_VALUE_LIGHTGRAY: c=0xD3D3D3; break;
        case CSS_VALUE_LIGHTGREEN: c=0x90EE90; break;
        case CSS_VALUE_LIGHTGREY: c=0xD3D3D3; break;
        case CSS_VALUE_LIGHTPINK: c=0xFFB6C1; break;
        case CSS_VALUE_LIGHTSALMON: c=0xFFA07A; break;
        case CSS_VALUE_LIGHTSEAGREEN: c=0x20B2AA; break;
        case CSS_VALUE_LIGHTSKYBLUE: c=0x87CEFA; break;
        case CSS_VALUE_LIGHTSLATEGRAY: c=0x778899; break;
        case CSS_VALUE_LIGHTSLATEGREY: c=0x778899; break;
        case CSS_VALUE_LIGHTSTEELBLUE: c=0xB0C4DE; break;
        case CSS_VALUE_LIGHTYELLOW: c=0xFFFFE0; break;
        case CSS_VALUE_LIME: c=0x00FF00; break;
        case CSS_VALUE_LIMEGREEN: c=0x32CD32; break;
        case CSS_VALUE_LINEN: c=0xFAF0E6; break;
        case CSS_VALUE_MAGENTA: c=0xFF00FF; break;
        case CSS_VALUE_MAROON: c=0x800000; break;
        case CSS_VALUE_MEDIUMAQUAMARINE: c=0x66CDAA; break;
        case CSS_VALUE_MEDIUMBLUE: c=0x0000CD; break;
        case CSS_VALUE_MEDIUMORCHID: c=0xBA55D3; break;
        case CSS_VALUE_MEDIUMPURPLE: c=0x9370DB; break;
        case CSS_VALUE_MEDIUMSEAGREEN: c=0x3CB371; break;
        case CSS_VALUE_MEDIUMSLATEBLUE: c=0x7B68EE; break;
        case CSS_VALUE_MEDIUMSPRINGGREEN: c=0x00FA9A; break;
        case CSS_VALUE_MEDIUMTURQUOISE: c=0x48D1CC; break;
        case CSS_VALUE_MEDIUMVIOLETRED: c=0xC71585; break;
        case CSS_VALUE_MIDNIGHTBLUE: c=0x191970; break;
        case CSS_VALUE_MINTCREAM: c=0xF5FFFA; break;
        case CSS_VALUE_MISTYROSE: c=0xFFE4E1; break;
        case CSS_VALUE_MOCCASIN: c=0xFFE4B5; break;
        case CSS_VALUE_NAVAJOWHITE: c=0xFFDEAD; break;
        case CSS_VALUE_NAVY: c=0x000080; break;
        case CSS_VALUE_OLDLACE: c=0xFDF5E6; break;
        case CSS_VALUE_OLIVE: c=0x808000; break;
        case CSS_VALUE_OLIVEDRAB: c=0x6B8E23; break;
        case CSS_VALUE_ORANGE: c=0xFFA500; break;
        case CSS_VALUE_ORANGERED: c=0xFF4500; break;
        case CSS_VALUE_ORCHID: c=0xDA70D6; break;
        case CSS_VALUE_PALEGOLDENROD: c=0xEEE8AA; break;
        case CSS_VALUE_PALEGREEN: c=0x98FB98; break;
        case CSS_VALUE_PALETURQUOISE: c=0xAFEEEE; break;
        case CSS_VALUE_PALEVIOLETRED: c=0xDB7093; break;
        case CSS_VALUE_PAPAYAWHIP: c=0xFFEFD5; break;
        case CSS_VALUE_PEACHPUFF: c=0xFFDAB9; break;
        case CSS_VALUE_PERU: c=0xCD853F; break;
        case CSS_VALUE_PINK: c=0xFFC0CB; break;
        case CSS_VALUE_PLUM: c=0xDDA0DD; break;
        case CSS_VALUE_POWDERBLUE: c=0xB0E0E6; break;
        case CSS_VALUE_PURPLE: c=0x800080; break;
        case CSS_VALUE_REBECCAPURPLE: c=0x663399; break;
        case CSS_VALUE_RED: c=0xFF0000; break;
        case CSS_VALUE_ROSYBROWN: c=0xBC8F8F; break;
        case CSS_VALUE_ROYALBLUE: c=0x4169E1; break;
        case CSS_VALUE_SADDLEBROWN: c=0x8B4513; break;
        case CSS_VALUE_SALMON: c=0xFA8072; break;
        case CSS_VALUE_SANDYBROWN: c=0xF4A460; break;
        case CSS_VALUE_SEAGREEN: c=0x2E8B57; break;
        case CSS_VALUE_SEASHELL: c=0xFFF5EE; break;
        case CSS_VALUE_SIENNA: c=0xA0522D; break;
        case CSS_VALUE_SILVER: c=0xC0C0C0; break;
        case CSS_VALUE_SKYBLUE: c=0x87CEEB; break;
        case CSS_VALUE_SLATEBLUE: c=0x6A5ACD; break;
        case CSS_VALUE_SLATEGRAY: c=0x708090; break;
        case CSS_VALUE_SLATEGREY: c=0x708090; break;
        case CSS_VALUE_SNOW: c=0xFFFAFA; break;
        case CSS_VALUE_SPRINGGREEN: c=0x00FF7F; break;
        case CSS_VALUE_STEELBLUE: c=0x4682B4; break;
        case CSS_VALUE_TAN: c=0xD2B48C; break;
        case CSS_VALUE_TEAL: c=0x008080; break;
        case CSS_VALUE_THISTLE: c=0xD8BFD8; break;
        case CSS_VALUE_TOMATO: c=0xFF6347; break;
        case CSS_VALUE_TURQUOISE: c=0x40E0D0; break;
        case CSS_VALUE_VIOLET: c=0xEE82EE; break;
        case CSS_VALUE_WHEAT: c=0xF5DEB3; break;
        case CSS_VALUE_WHITE: c=0xFFFFFF; break;
        case CSS_VALUE_WHITESMOKE: c=0xF5F5F5; break;
        case CSS_VALUE_YELLOW: c=0xFFFF00; break;
        case CSS_VALUE_YELLOWGREEN: c=0x9ACD32; break;
        default: return false;
    }
    *r = (uint8_t)((c >> 16) & 0xFF);
    *g = (uint8_t)((c >> 8) & 0xFF);
    *b = (uint8_t)(c & 0xFF);
    *a = 255;
    return true;
}

// ============================================================================
// CSS Unit string <-> enum mapping
// ============================================================================

// unit-string table for bidirectional lookup
struct CssUnitEntry {
    const char* name;
    size_t      name_len;
    CssUnit     unit;
};

static const CssUnitEntry css_unit_table[] = {
    // absolute length units
    {"px",   2, CSS_UNIT_PX},
    {"cm",   2, CSS_UNIT_CM},
    {"mm",   2, CSS_UNIT_MM},
    {"in",   2, CSS_UNIT_IN},
    {"pt",   2, CSS_UNIT_PT},
    {"pc",   2, CSS_UNIT_PC},
    {"q",    1, CSS_UNIT_Q},
    // font-relative length units
    {"em",   2, CSS_UNIT_EM},
    {"ex",   2, CSS_UNIT_EX},
    {"cap",  3, CSS_UNIT_CAP},
    {"ch",   2, CSS_UNIT_CH},
    {"ic",   2, CSS_UNIT_IC},
    {"rem",  3, CSS_UNIT_REM},
    {"lh",   2, CSS_UNIT_LH},
    {"rlh",  3, CSS_UNIT_RLH},
    // viewport units
    {"vw",   2, CSS_UNIT_VW},
    {"vh",   2, CSS_UNIT_VH},
    {"vi",   2, CSS_UNIT_VI},
    {"vb",   2, CSS_UNIT_VB},
    {"vmin", 4, CSS_UNIT_VMIN},
    {"vmax", 4, CSS_UNIT_VMAX},
    // small/large/dynamic viewport units
    {"svw",  3, CSS_UNIT_SVW},
    {"svh",  3, CSS_UNIT_SVH},
    {"svi",  3, CSS_UNIT_SVI},
    {"svb",  3, CSS_UNIT_SVB},
    {"svmin",5, CSS_UNIT_SVMIN},
    {"svmax",5, CSS_UNIT_SVMAX},
    {"lvw",  3, CSS_UNIT_LVW},
    {"lvh",  3, CSS_UNIT_LVH},
    {"lvi",  3, CSS_UNIT_LVI},
    {"lvb",  3, CSS_UNIT_LVB},
    {"lvmin",5, CSS_UNIT_LVMIN},
    {"lvmax",5, CSS_UNIT_LVMAX},
    {"dvw",  3, CSS_UNIT_DVW},
    {"dvh",  3, CSS_UNIT_DVH},
    {"dvi",  3, CSS_UNIT_DVI},
    {"dvb",  3, CSS_UNIT_DVB},
    {"dvmin",5, CSS_UNIT_DVMIN},
    {"dvmax",5, CSS_UNIT_DVMAX},
    // container query units
    {"cqw",  3, CSS_UNIT_CQW},
    {"cqh",  3, CSS_UNIT_CQH},
    {"cqi",  3, CSS_UNIT_CQI},
    {"cqb",  3, CSS_UNIT_CQB},
    {"cqmin",5, CSS_UNIT_CQMIN},
    {"cqmax",5, CSS_UNIT_CQMAX},
    // angle units
    {"deg",  3, CSS_UNIT_DEG},
    {"grad", 4, CSS_UNIT_GRAD},
    {"rad",  3, CSS_UNIT_RAD},
    {"turn", 4, CSS_UNIT_TURN},
    // time units
    {"s",    1, CSS_UNIT_S},
    {"ms",   2, CSS_UNIT_MS},
    // frequency units
    {"hz",   2, CSS_UNIT_HZ},
    {"khz",  3, CSS_UNIT_KHZ},
    // resolution units
    {"dpi",  3, CSS_UNIT_DPI},
    {"dpcm", 4, CSS_UNIT_DPCM},
    {"dppx", 4, CSS_UNIT_DPPX},
    // flex units
    {"fr",   2, CSS_UNIT_FR},
    // percentage
    {"%",    1, CSS_UNIT_PERCENT},
};

static const size_t css_unit_table_count = sizeof(css_unit_table) / sizeof(css_unit_table[0]);

CssUnit css_unit_from_string(const char* unit_str, size_t length) {
    if (!unit_str || length == 0) return CSS_UNIT_NONE;
    for (size_t i = 0; i < css_unit_table_count; i++) {
        if (css_unit_table[i].name_len == length &&
            str_icmp(unit_str, length, css_unit_table[i].name, css_unit_table[i].name_len) == 0) {
            return css_unit_table[i].unit;
        }
    }
    return CSS_UNIT_NONE;
}

const char* css_unit_to_string(CssUnit unit) {
    for (size_t i = 0; i < css_unit_table_count; i++) {
        if (css_unit_table[i].unit == unit) {
            return css_unit_table[i].name;
        }
    }
    if (unit == CSS_UNIT_NONE) return "";
    return "unknown";
}

bool css_unit_is_length(CssUnit unit) {
    // CssUnit keeps every CSS <length> unit in one contiguous enum range.
    return unit >= CSS_UNIT_PX && unit <= CSS_UNIT_CQMAX;
}

bool css_unit_is_angle(CssUnit unit) {
    return unit >= CSS_UNIT_DEG && unit <= CSS_UNIT_TURN;
}
