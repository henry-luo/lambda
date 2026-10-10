#include "view.hpp"
#include "layout.hpp"
#include "event.hpp"
#include "render.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/color.h"
#include "../lib/avl_tree.h"
}

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lambda/dom/dom.h"
#include "../lib/tagged.hpp"
#include "../lib/mem_grow.hpp"
#include "../lib/mem_factory.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

// ============================================================================
// Property Interpolation
// ============================================================================

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return clamp_byte_round(v);
}

Color css_interpolate_color(Color a, Color b, float t) {
    Color result = {};
    // transparent endpoints contribute no color in premultiplied sRGB.
    float alpha = css_interpolate_float((float)a.a, (float)b.a, t);
    if (alpha > 0.0f) {
        result.r = clamp_byte_round(css_interpolate_float((float)a.r * a.a,
            (float)b.r * b.a, t) / alpha);
        result.g = clamp_byte_round(css_interpolate_float((float)a.g * a.a,
            (float)b.g * b.a, t) / alpha);
        result.b = clamp_byte_round(css_interpolate_float((float)a.b * a.a,
            (float)b.b * b.a, t) / alpha);
    }
    result.a = lerp_u8(a.a, b.a, t);
    return result;
}

// ============================================================================
// Keyframe Content Parsing
// ============================================================================

static const char* skip_css_balanced_block(const char* source) {
    if (!source) return source;
    return str_scan_balanced(str_scan_until_char(source, '{'), '{', '}', false, NULL);
}

static CssAnimValueType property_value_type(CssPropertyCode id) {
    const CssPropertyRuntimeMetadata* metadata = css_property_runtime_metadata(id);
    return metadata ? metadata->animation_type : ANIM_VAL_NONE;
}

static int animation_transform_slot(CssPropertyCode property) {
    return property == CSS_PROPERTY_TRANSFORM ? 0 : css_individual_transform_index(property) + 1;
}

static bool transform_value_is_context_free(const CssValue* value) {
    if (!value) return false;
    switch (value->type) {
        case CSS_VALUE_TYPE_NUMBER:
        case CSS_VALUE_TYPE_PERCENTAGE:
        case CSS_VALUE_TYPE_ANGLE: return true;
        case CSS_VALUE_TYPE_LENGTH: {
            double pixels = 0.0;
            return css_absolute_length_to_px(value->data.length.unit, value->data.length.value, &pixels) ||
                (value->data.length.unit >= CSS_UNIT_DEG && value->data.length.unit <= CSS_UNIT_TURN);
        }
        case CSS_VALUE_TYPE_FUNCTION: {
            const CssFunction* function = value->data.function;
            if (!function || !css_transform_function_info(function->name)) return false;
            for (int index = 0; index < function->arg_count; index++)
                if (!transform_value_is_context_free(function->args[index])) return false;
            return true;
        }
        case CSS_VALUE_TYPE_LIST:
            for (int index = 0; index < value->data.list.count; index++)
                if (!transform_value_is_context_free(value->data.list.values[index])) return false;
            return true;
        default: return false;
    }
}

static bool parse_aspect_ratio_value(const char* val, CssAnimatedProp* out) {
    if (!val || !out) return false;

    const char* p = str_skip_ascii_space(val);
    bool is_auto = str_istarts_with_cstr(p, "auto") &&
        !isalnum((unsigned char)p[4]);
    if (is_auto) p += 4;

    float numerator = -1.0f;
    float denominator = -1.0f;
    while (*p) {
        if (isdigit((unsigned char)*p) || *p == '.' || *p == '+' || *p == '-') {
            char* end = nullptr;
            float number = strtof(p, &end);
            if (end != p) {
                if (numerator < 0.0f) numerator = number;
                else {
                    denominator = number;
                    break;
                }
                p = end;
                continue;
            }
        }
        p++;
    }

    // A ratio with no numeric component is valid only as the `auto` keyword;
    // zero or negative components are degenerate and cannot interpolate.
    if (numerator < 0.0f) {
        out->value.aspect_ratio.value = 0.0f;
        out->value.aspect_ratio.is_auto = is_auto;
        return is_auto;
    }
    if (numerator <= 0.0f || denominator == 0.0f) return false;

    out->value.aspect_ratio.value = denominator > 0.0f
        ? numerator / denominator : numerator;
    out->value.aspect_ratio.is_auto = is_auto;
    return out->value.aspect_ratio.value > 0.0f &&
        isfinite(out->value.aspect_ratio.value);
}

// Parse a property value into CssAnimatedProp
static bool parse_property_value(CssPropertyCode prop_id, const char* val,
                                  CssAnimatedProp* out, Pool* pool) {
    memset(out, 0, sizeof(*out));
    out->property_code = prop_id;
    out->value_type = property_value_type(prop_id);
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;

    const CssProperty* property = css_property_get_by_code(prop_id);
    CssDeclaration* declaration = property ? css_parse_property_declaration(
        property->name, strlen(property->name), val, strlen(val), pool) : nullptr;
    // keyframe declarations never participate in the important cascade.
    if (!declaration || !declaration->value || declaration->important) return false;

    switch (out->value_type) {
        case ANIM_VAL_FLOAT: {
            const CssValue* value = declaration->value;
            if (value->type == CSS_VALUE_TYPE_NUMBER || value->type == CSS_VALUE_TYPE_PERCENTAGE) {
                LayoutContext context = {};
                context.pool = lam::up(pool);
                out->value.f = resolve_css_opacity_value(&context, value);
            } else out->expression = value;
            return true;
        }
        case ANIM_VAL_COLOR: {
            const CssValue* value = declaration->value;
            const CssEnumInfo* keyword = value->type == CSS_VALUE_TYPE_KEYWORD
                ? css_enum_info(value->data.keyword) : nullptr;
            if (css_value_contains_var_reference(value) ||
                (keyword && (keyword->group == CSS_VALUE_GROUP_GLOBAL ||
                             value->data.keyword == CSS_VALUE_CURRENTCOLOR)) ||
                (value->type == CSS_VALUE_TYPE_COLOR && value->data.color.type == CSS_COLOR_CURRENTCOLOR)) {
                out->expression = value;
            } else {
                LayoutContext context = {};
                context.pool = lam::up(pool);
                out->value.color = resolve_color_value(&context, value);
            }
            return true;
        }
        case ANIM_VAL_LENGTH: {
            const CssValue* value = declaration->value;
            // keep units and variable/math trees until the element's style context exists.
            out->expression = value;
            out->value.length.is_percent = value->type == CSS_VALUE_TYPE_PERCENTAGE;
            out->value.length.keyword = CSS_VALUE__UNDEF;
            out->value.length.value = NAN;
            double pixels = 0.0;
            if (value->type == CSS_VALUE_TYPE_LENGTH &&
                css_absolute_length_to_px(value->data.length.unit, value->data.length.value, &pixels)) {
                out->value.length.value = (float)pixels;
            } else if (value->type == CSS_VALUE_TYPE_NUMBER) {
                out->value.length.value = (float)value->data.number.value;
            }
            return true;
        }
        case ANIM_VAL_BACKGROUND_POSITION:
        case ANIM_VAL_IMAGE:
        case ANIM_VAL_FILTER:
            out->expression = declaration->value;
            return true;
        case ANIM_VAL_ASPECT_RATIO:
            return parse_aspect_ratio_value(val, out);
        case ANIM_VAL_TRANSFORM: {
            if (prop_id != CSS_PROPERTY_TRANSFORM) {
                out->expression = declaration->value;
                return true;
            }
            if (declaration->value->type == CSS_VALUE_TYPE_KEYWORD &&
                declaration->value->data.keyword == CSS_VALUE_NONE) {
                out->value.transform = nullptr;
                return true;
            }
            if (transform_value_is_context_free(declaration->value))
                out->value.transform = resolve_transform_value(nullptr, declaration->value, pool);
            else out->expression = declaration->value;
            return true;
        }
        case ANIM_VAL_DISPLAY: {
            out->value.display.value = declaration->value;
            out->value.display.has_used = false;
            return true;
        }
        default:
            return false;
    }
}

bool css_animation_parse_property_value(CssPropertyCode property,
                                        const char* value,
                                        CssAnimatedProp* out,
                                        Pool* pool) {
    if (!value || !out) return false;
    return parse_property_value(property, value, out, pool);
}

static bool parse_animation_composition(const char* value, Pool* pool,
                                        CssAnimComposite* result) {
    CssDeclaration* declaration = value ? css_parse_property_declaration(
        "animation-composition", 21, value, strlen(value), pool) : nullptr;
    if (!declaration || declaration->important) return false;
    const char* name = css_math_token_name(declaration->value);
    if (!name) return false;
    if (str_icmp_cstr(name, "replace") == 0) *result = CSS_ANIM_COMPOSITE_REPLACE;
    else if (str_icmp_cstr(name, "add") == 0) *result = CSS_ANIM_COMPOSITE_ADD;
    else if (str_icmp_cstr(name, "accumulate") == 0) *result = CSS_ANIM_COMPOSITE_ACCUMULATE;
    else return false;
    return true;
}

static bool animation_property_matches(const CssAnimatedProp* property, CssPropertyCode id, const char* name) {
    return property->property_code == id && (id != CSS_PROPERTY_CUSTOM ||
        (property->custom_name && name && strcmp(property->custom_name, name) == 0));
}

static CssAnimatedProp* find_prop_in_stop(CssKeyframeStop* stop, CssPropertyCode id, const char* name = nullptr) {
    for (int i = 0; i < stop->property_count; i++) {
        if (animation_property_matches(&stop->properties[i], id, name)) return &stop->properties[i];
    }
    return nullptr;
}

// Parse the content of a @keyframes rule into structured CssKeyframes
// Content format: "animName { from { prop: val; } 50% { prop: val; } to { prop: val; } }"
static CssKeyframes* parse_keyframes_content(const char* content, Pool* pool) {
    if (!content || !pool) return NULL;

    // token boundaries preserve quoted/escaped names, including a quoted brace.
    Pool* scratch = pool_create();
    if (!scratch) return nullptr;
    size_t token_count = 0;
    CssToken* tokens = css_tokenize(content, strlen(content), scratch, &token_count);
    const char* brace = nullptr;
    for (size_t i = 0; tokens && i < token_count; i++) {
        if (tokens[i].type == CSS_TOKEN_LEFT_BRACE) {
            brace = tokens[i].start;
            break;
        }
    }
    CssDeclaration* name_declaration = brace ? css_parse_property_declaration(
        "animation-name", 14, content, brace - content, pool) : nullptr;
    pool_destroy(scratch);
    const CssValue* name_value = name_declaration ? name_declaration->value : nullptr;
    const CssEnumInfo* keyword = name_value && name_value->type == CSS_VALUE_TYPE_KEYWORD
        ? css_enum_info(name_value->data.keyword) : nullptr;
    if (!name_value || name_value->type == CSS_VALUE_TYPE_LIST ||
        (keyword && (keyword->group == CSS_VALUE_GROUP_GLOBAL ||
         name_value->data.keyword == CSS_VALUE_NONE))) return nullptr;
    const char* name = name_value->type == CSS_VALUE_TYPE_STRING
        ? name_value->data.string : css_math_token_name(name_value);
    if (!name) return nullptr;
    const char* p = brace + 1;

    // parse keyframe stops — temporary storage
    lam::OwnArr<CssKeyframeStop> temp_stops = {};
    int stop_count = 0, stop_capacity = 0;

    // temporary property storage per stop
    lam::OwnArr<CssAnimatedProp> temp_props = {};
    int prop_capacity = 0;
    lam::OwnArr<float> offsets = {};
    int offset_capacity = 0;

    while (*p) {
        p = str_skip_ascii_space(p);
        if (*p == '}') break; // end of @keyframes

        // one declaration block contributes to every selector in its comma list.
        int offset_count = 0;
        bool valid_selectors = true;
        do {
            p = str_skip_ascii_space(p);
            float offset = -1.0f;
            if (str_icmp(p, 4, "from", 4) == 0 && !isalnum((unsigned char)p[4])) {
                offset = 0.0f;
                p += 4;
            } else if (str_icmp(p, 2, "to", 2) == 0 && !isalnum((unsigned char)p[2])) {
                offset = 1.0f;
                p += 2;
            } else if (isdigit((unsigned char)*p) || *p == '.' || *p == '+' || *p == '-') {
                char* end = nullptr;
                offset = strtof(p, &end) / 100.0f;
                valid_selectors = end != p && *end == '%';
                p = valid_selectors ? end + 1 : p;
            } else valid_selectors = false;
            if (!valid_selectors || !isfinite(offset) || offset < 0.0f || offset > 1.0f) {
                valid_selectors = false;
                break;
            }
            if (!lam::pool_grow_array(pool, &offsets, &offset_capacity, offset_count + 1, 4)) return nullptr;
            offsets[offset_count++] = offset;
            p = str_skip_ascii_space(p);
            if (*p != ',') break;
            p++;
        } while (*p);
        if (!valid_selectors || !offset_count || *p != '{') {
            p = skip_css_balanced_block(p);
            continue;
        }
        p++; // skip '{'

        // parse declarations inside keyframe stop
        int prop_count = 0;
        CssAnimComposite stop_composite = CSS_ANIM_COMPOSITE_REPLACE;
        TimingFunction stop_timing = {};
        bool has_timing = false;

        while (*p && *p != '}') {
            p = str_skip_ascii_space(p);
            if (*p == '}') break;

            // parse property name
            const char* prop_start = p;
            while (*p && *p != ':' && *p != '}') p++;
            if (*p != ':') break;

            const char* prop_end = p;
            while (prop_end > prop_start && isspace((unsigned char)*(prop_end - 1))) prop_end--;

            size_t plen = prop_end - prop_start;
            const char* prop_name = pool_dup_n(pool, prop_start, plen);
            if (!prop_name) return nullptr;

            p++; // skip ':'
            p = str_skip_ascii_space(p);

            // parse property value (up to ';' or '}')
            const char* val_start = p;
            p = str_scan_top_level(p, ";}", '(', ')', "\"'", true);
            size_t val_len = (size_t)(p - val_start);
            str_rtrim(&val_start, &val_len);
            const char* val_end = val_start + val_len;

            size_t vlen = val_end - val_start;
            const char* val_buf = pool_dup_n(pool, val_start, vlen);
            if (!val_buf) return nullptr;

            if (*p == ';') p++;

            if (str_icmp_cstr(prop_name, "animation-composition") == 0) {
                parse_animation_composition(val_buf, pool, &stop_composite);
                continue;
            }
            if (str_icmp_cstr(prop_name, "animation-timing-function") == 0) {
                TimingFunction timing = {};
                if (css_animation_parse_timing_function_text(val_buf, &timing)) {
                    stop_timing = timing;
                    has_timing = true;
                }
                continue;
            }

            // resolve property and parse value
            CssPropertyCode prop_id = (CssPropertyCode)css_property_code_from_name(prop_name);
            bool custom = prop_name[0] == '-' && prop_name[1] == '-' && prop_name[2];
            if (custom || prop_id != (CssPropertyCode)0) {
                CssAnimatedProp parsed = {};
                CssDeclaration* custom_decl = custom ? css_parse_property_declaration(prop_name, plen, val_buf, vlen, pool) : nullptr;
                bool accepted = custom ? custom_decl && custom_decl->value && !custom_decl->important
                    : parse_property_value(prop_id, val_buf, &parsed, pool);
                if (custom && accepted) {
                    parsed.property_code = CSS_PROPERTY_CUSTOM;
                    parsed.custom_name = prop_name;
                    parsed.value_type = ANIM_VAL_CUSTOM;
                    parsed.expression = custom_decl->value;
                }
                if (accepted) {
                    // invalid later declarations leave the preceding valid endpoint eligible.
                    CssKeyframeStop pending = {offsets[0], temp_props, prop_count, nullptr};
                    CssAnimatedProp* previous = find_prop_in_stop(&pending, parsed.property_code, parsed.custom_name);
                    if (previous) *previous = parsed;
                    else {
                        if (!lam::pool_grow_array(pool, &temp_props, &prop_capacity, prop_count + 1, 8)) return nullptr;
                        temp_props[prop_count++] = parsed;
                    }
                }
            }
        }

        if (*p == '}') p++; // skip closing brace of keyframe stop

        for (int selector = 0; selector < offset_count && (prop_count > 0 || has_timing); selector++) {
            if (!lam::pool_grow_array(pool, &temp_stops, &stop_capacity, stop_count + 1, 8)) return nullptr;
            for (int i = 0; i < prop_count; i++) {
                temp_props[i].composite = stop_composite;
            }
            CssKeyframeStop* stop = &temp_stops[stop_count];
            stop->offset = offsets[selector];
            stop->timing = has_timing ? (TimingFunction*)pool_alloc(pool, sizeof(TimingFunction)) : nullptr;
            if (stop->timing) *stop->timing = stop_timing;
            stop->property_count = prop_count;
            stop->properties = prop_count > 0 ? lam::own_arr((CssAnimatedProp*)pool_alloc(
                pool, sizeof(CssAnimatedProp) * prop_count)) : nullptr;
            if (prop_count > 0) memcpy(stop->properties, temp_props, sizeof(CssAnimatedProp) * prop_count);
            stop_count++;
        }
    }
    if (offsets) pool_free(pool, offsets);

    if (stop_count == 0) {
        if (temp_props) pool_free(pool, temp_props);
        if (temp_stops) pool_free(pool, temp_stops);
        log_debug("css-anim: @keyframes '%s' has no valid stops", name);
        return NULL;
    }

    // sort stops by offset (simple insertion sort, small N)
    for (int i = 1; i < stop_count; i++) {
        CssKeyframeStop key = temp_stops[i];
        int j = i - 1;
        while (j >= 0 && temp_stops[j].offset > key.offset) {
            temp_stops[j + 1] = temp_stops[j];
            j--;
        }
        temp_stops[j + 1] = key;
    }

    CssKeyframes* kf = (CssKeyframes*)pool_calloc(pool, sizeof(CssKeyframes));
    if (!kf) return nullptr;
    kf->name = lam::up(name);
    kf->stop_count = stop_count;
    kf->stops = temp_stops;
    if (temp_props) pool_free(pool, temp_props);

    log_debug("css-anim: parsed @keyframes '%s' with %d stops", name, stop_count);
    return kf;
}

// ============================================================================
// Keyframe Registry
// ============================================================================

static void keyframe_registry_scan(KeyframeRegistry* registry,
                                   CssStylesheet** sheets, int count,
                                   Pool* pool) {
    for (int si = 0; si < count; si++) {
        CssStylesheet* sheet = sheets[si];
        if (!sheet || sheet->disabled) continue;
        for (size_t ri = 0; ri < sheet->rule_count; ri++) {
            CssRule* rule = sheet->rules[ri];
            if (!rule || rule->type != CSS_RULE_KEYFRAMES ||
                !rule->data.generic_rule.content) continue;
            CssKeyframes* keyframes = parse_keyframes_content(
                rule->data.generic_rule.content, pool);
            const char* source_file = sheet->origin_url ? sheet->origin_url : sheet->href;
            if (keyframes && source_file) keyframes->source_file = pool_strdup(pool, source_file);
            if (!keyframes || !lam::pool_grow_array(pool, &registry->entries,
                    &registry->capacity, registry->count + 1, 16)) continue;
            registry->entries[registry->count++] = keyframes;
        }
    }
}

KeyframeRegistry* keyframe_registry_create(DomDocument* doc, Pool* pool) {
    if (!doc || !pool) return NULL;

    // keyframe parsing resolves property names; standalone animation paths may
    // reach here before the layout engine initializes the CSS property table.
    if (!css_property_system_init(pool)) return NULL;

    KeyframeRegistry* registry = (KeyframeRegistry*)pool_calloc(pool, sizeof(KeyframeRegistry));
    registry->pool = lam::up(pool);
    registry->capacity = 0;
    registry->entries = nullptr;
    registry->count = 0;

    keyframe_registry_scan(registry, doc->stylesheets, doc->stylesheet_count, pool);
    keyframe_registry_scan(registry, doc->cached_inline_sheets,
                           doc->cached_inline_sheet_count, pool);

    log_debug("css-anim: keyframe registry created with %d @keyframes rules", registry->count);
    return registry;
}

CssKeyframes* keyframe_registry_find(KeyframeRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    for (int i = registry->count - 1; i >= 0; i--) {
        if (strcmp(registry->entries[i]->name, name) == 0) {
            return registry->entries[i];
        }
    }
    return NULL;
}

// ============================================================================
// CSS Animation Tick
// ============================================================================

static CssAnimatedProp* find_underlying_prop(CssAnimState* state, CssPropertyCode id, const char* name = nullptr) {
    if (!state) return NULL;
    for (int i = 0; i < state->underlying_count; i++) {
        if (animation_property_matches(&state->underlying[i], id, name)) return &state->underlying[i];
    }
    return NULL;
}

static bool capture_underlying_length(DomElement* element, CssPropertyCode id,
                                      CssAnimatedProp* out);

static bool css_animation_property_is_important(CssAnimState* state, CssPropertyCode property) {
    for (int index = 0; index < state->important_property_count; index++) {
        if (state->important_properties[index] == property) return true;
    }
    return false;
}

static void css_animation_refresh_priority(CssAnimState* state) {
    if (!state || !state->keyframes) return;
    state->important_property_count = 0;
    for (int stop_index = 0; stop_index < state->keyframes->stop_count; stop_index++) {
        CssKeyframeStop* stop = &state->keyframes->stops[stop_index];
        for (int index = 0; index < stop->property_count; index++) {
            CssPropertyCode property = stop->properties[index].property_code;
            if (property == CSS_PROPERTY_CUSTOM) continue;
            if (css_animation_property_is_important(state, property)) continue;
            CssDeclaration* winner = layout_cascaded_physical_declaration(state->element, property);
            if (!winner || !(winner->important || winner->specificity.important)) continue;
            if (!lam::pool_grow_array(state->pool, &state->important_properties,
                    &state->important_property_capacity, state->important_property_count + 1, 8)) {
                log_error("css-anim: important property cache allocation failed");
                return;
            }
            state->important_properties[state->important_property_count++] = property;
        }
    }
}

static bool capture_underlying_value(DomElement* element, CssPropertyCode property,
                                     CssAnimatedProp* out);


static FilterFunction filter_identity(FilterFunctionType type) {
    FilterFunction result = {};
    result.type = type;
    if (type == FILTER_BRIGHTNESS || type == FILTER_CONTRAST ||
        type == FILTER_OPACITY || type == FILTER_SATURATE) result.params.amount = 1.0f;
    return result;
}

static FilterFunction* interpolate_filter_list(Pool* pool, const FilterFunction* from,
                                               const FilterFunction* to, float progress) {
    // Filter Effects 1 §14.1: mismatched lists are discrete; missing tails use identity filters.
    for (const FilterFunction *a = from, *b = to; a || b;
         a = a ? a->next.get() : nullptr, b = b ? b->next.get() : nullptr) {
        if ((a && a->type == FILTER_URL) || (b && b->type == FILTER_URL) ||
            (a && b && a->type != b->type)) return radiant::clone_filter_list(pool, progress < .5f ? from : to);
    }
    FilterFunction* result = radiant::clone_filter_list(pool, from ? from : to);
    FilterFunction* tail = result;
    while (tail && tail->next) tail = tail->next;
    const FilterFunction* a = from;
    const FilterFunction* b = to;
    FilterFunction* node = result;
    while (a || b) {
        FilterFunction identity = filter_identity(a ? a->type : b->type);
        const FilterFunction* left = a ? a : &identity;
        const FilterFunction* right = b ? b : &identity;
        if (!node) {
            node = (FilterFunction*)pool_calloc(pool, sizeof(FilterFunction));
            if (!node) { auto owned = lam::own(result); lam::free_owned_list(pool, owned); return nullptr; }
            if (tail) tail->next = lam::own(node);
            else result = node;
            tail = node;
        }
        node->type = left->type;
        if (left->type == FILTER_DROP_SHADOW) {
            node->params.drop_shadow.offset_x = css_interpolate_float(left->params.drop_shadow.offset_x, right->params.drop_shadow.offset_x, progress);
            node->params.drop_shadow.offset_y = css_interpolate_float(left->params.drop_shadow.offset_y, right->params.drop_shadow.offset_y, progress);
            node->params.drop_shadow.blur_radius = max(0.0f, css_interpolate_float(left->params.drop_shadow.blur_radius, right->params.drop_shadow.blur_radius, progress));
            node->params.drop_shadow.color = css_interpolate_color(left->params.drop_shadow.color, right->params.drop_shadow.color, progress);
        } else {
            // all single-component functions share their scalar union storage; hue angles stay unbounded.
            float value = css_interpolate_float(left->params.amount, right->params.amount, progress);
            node->params.amount = left->type == FILTER_HUE_ROTATE ? value : max(0.0f, value);
        }
        a = a ? a->next.get() : nullptr;
        b = b ? b->next.get() : nullptr;
        node = node->next;
    }
    return result;
}


static void css_animation_clear_value_samples(CssAnimState* state) {
    for (int index = 0; index < state->value_sample_count; index++) {
        CssAnimValueSample* sample = &state->value_samples[index];
        if (sample->computed.value_type == ANIM_VAL_TRANSFORM)
            radiant::destroy_transform_list(state->pool, sample->computed.value.transform);
        else if (sample->computed.value_type == ANIM_VAL_IMAGE && sample->computed.value.image)
            pool_free(state->pool, sample->computed.value.image);
        else if (sample->computed.value_type == ANIM_VAL_FILTER) {
            radiant::destroy_filter_list(state->pool, sample->computed.value.filter);
            sample->computed.value.filter = nullptr;
        }
    }
    state->value_sample_count = 0;
    if (state->custom_value_pool) mem_pool_destroy(state->custom_value_pool);
    state->custom_value_pool = nullptr;
}

static bool css_animation_compute_property(CssAnimState* state, LayoutContext* lycon,
                                           const CssAnimatedProp* property,
                                           CssAnimatedProp* computed) {
    *computed = *property;
    computed->expression = nullptr;
    if (property->value_type == ANIM_VAL_CUSTOM) {
        if (!state->custom_value_pool) state->custom_value_pool = mem_pool_create(nullptr, MEM_ROLE_CSS, "css.animation.custom-values");
        if (!state->custom_value_pool) return false;
        computed->value.custom = css_compute_custom_property_value(state->custom_value_pool,
            state->element, property->custom_name, property->expression);
        return true;
    }
    const CssValue* value = resolve_var_function(lycon, property->expression);
    const CssEnumInfo* keyword = value && value->type == CSS_VALUE_TYPE_KEYWORD
        ? css_enum_info(value->data.keyword) : nullptr;
    bool invalid = !value || !css_property_validate_value(property->property_code, value);
    bool global = keyword && keyword->group == CSS_VALUE_GROUP_GLOBAL;
    if (invalid || global) {
        bool inherit = global && value->data.keyword == CSS_VALUE_INHERIT;
        if (invalid || (global && value->data.keyword == CSS_VALUE_UNSET))
            inherit = css_property_runtime_inherited(property->property_code);
        CssAnimatedProp inherited = {};
        if (inherit && capture_underlying_value(dom_parent_element(state->element),
                property->property_code, &inherited)) {
            computed->value = inherited.value;
            // computed transform caches own copies, never a parent's mutable view-pool chain (D4.5.1v4).
            if (property->value_type == ANIM_VAL_TRANSFORM)
                computed->value.transform = radiant::clone_transform_list(
                    state->pool, inherited.value.transform);
            else if (property->value_type == ANIM_VAL_IMAGE)
                computed->value.image = inherited.value.image ? pool_strdup(state->pool, inherited.value.image) : nullptr;
            else if (property->value_type == ANIM_VAL_FILTER)
                computed->value.filter = radiant::clone_filter_list(state->pool, inherited.value.filter);
            return true;
        }
        // invalid substitution defaults just like unset; noninherited values use initial.
        const CssProperty* metadata = css_property_get_by_code(property->property_code);
        CssDeclaration* initial = metadata ? css_parse_property_declaration(
            metadata->name, strlen(metadata->name), metadata->initial_value,
            strlen(metadata->initial_value), lycon->pool) : nullptr;
        value = initial ? initial->value : nullptr;
        if (!value) return false;
    }
    switch (property->value_type) {
        case ANIM_VAL_FLOAT:
            computed->value.f = resolve_css_opacity_value(lycon, value);
            return isfinite(computed->value.f);
        case ANIM_VAL_COLOR:
            computed->value.color = property->property_code == CSS_PROPERTY_COLOR
                ? resolve_text_color_value(lycon, value) : resolve_color_value(lycon, value);
            return true;
        case ANIM_VAL_TRANSFORM:
            if (property->property_code == CSS_PROPERTY_TRANSFORM)
                computed->value.transform = resolve_transform_value(lycon, value, state->pool);
            else {
                computed->value.transform = nullptr;
                if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) return true;
                TransformFunction function = {};
                if (!resolve_individual_transform_value(lycon, property->property_code, value, &function)) return false;
                computed->value.transform = (TransformFunction*)pool_calloc(state->pool, sizeof(TransformFunction));
                if (!computed->value.transform) return false;
                *computed->value.transform = function;
            }
            return true;
        case ANIM_VAL_LENGTH:
            computed->value.length.is_percent = false;
            computed->value.length.keyword = CSS_VALUE__UNDEF;
            if (value->type == CSS_VALUE_TYPE_KEYWORD &&
                value->data.keyword != CSS_VALUE_THIN && value->data.keyword != CSS_VALUE_MEDIUM &&
                value->data.keyword != CSS_VALUE_THICK) {
                computed->value.length.keyword = value->data.keyword;
                computed->value.length.value = 0.0f;
                return true;
            }
            computed->value.length.value = resolve_length_value(lycon, property->property_code, value);
            return isfinite(computed->value.length.value);
        case ANIM_VAL_BACKGROUND_POSITION:
            computed->value.background_position = {};
            if (value->type == CSS_VALUE_TYPE_PERCENTAGE)
                computed->value.background_position.percent = (float)value->data.percentage.value;
            else computed->value.background_position.pixels = resolve_length_value(
                lycon, property->property_code, value);
            return isfinite(computed->value.background_position.pixels) &&
                isfinite(computed->value.background_position.percent);
        case ANIM_VAL_IMAGE: {
            computed->value.image = nullptr;
            if (value->type == CSS_VALUE_TYPE_KEYWORD && value->data.keyword == CSS_VALUE_NONE) return true;
            const char* url = css_background_url_value(value);
            CssDeclaration declaration = {};
            declaration.source_file = state->keyframes ? state->keyframes->source_file : nullptr;
            char* path = url ? resolve_css_resource_url(lycon, &declaration, url) : nullptr;
            if (!path) return false;
            computed->value.image = pool_strdup(state->pool, path);
            pool_free(layout_prop_pool(lycon), path);
            return computed->value.image != nullptr;
        }
        case ANIM_VAL_FILTER:
            computed->value.filter = resolve_filter_value(lycon, property->property_code, value, state->pool);
            return true;
        default: return false;
    }
}

static void css_animation_refresh_value_samples(CssAnimState* state, LayoutContext* lycon) {
    if (!state || !state->keyframes || !lycon) return;
    css_animation_clear_value_samples(state);
    int count = 0;
    for (int stop_index = 0; stop_index < state->keyframes->stop_count; stop_index++) {
        CssKeyframeStop* stop = &state->keyframes->stops[stop_index];
        for (int index = 0; index < stop->property_count; index++)
            if (stop->properties[index].expression) count++;
    }
    if (count == 0) return;
    if (!lam::pool_grow_array(state->pool, &state->value_samples,
            &state->value_sample_capacity, count, 8)) {
        log_error("css-anim: computed value sample allocation failed (count=%d)", count);
        return;
    }
    for (int stop_index = 0; stop_index < state->keyframes->stop_count; stop_index++) {
        CssKeyframeStop* stop = &state->keyframes->stops[stop_index];
        for (int index = 0; index < stop->property_count; index++) {
            CssAnimatedProp* property = &stop->properties[index];
            if (!property->expression) continue;
            CssAnimValueSample* sample = &state->value_samples[state->value_sample_count++];
            sample->source = property;
            sample->valid = css_animation_compute_property(state, lycon, property, &sample->computed);
        }
    }
}

static bool css_animation_resolved_property(CssAnimState* state,
                                            const CssAnimatedProp* source,
                                            CssAnimatedProp* resolved) {
    // CSS and Web effects share animation-origin priority; transitions sample separately.
    if (source->custom_name) {
        const CssCustomProp* winner = dom_element_lookup_own_custom_property_entry(state->element, source->custom_name);
        if (winner && winner->declaration && (winner->declaration->important || winner->declaration->specificity.important)) return false;
    } else if (css_animation_property_is_important(state, source->property_code)) return false;
    if (source->expression) {
        for (int index = 0; index < state->value_sample_count; index++) {
            if (state->value_samples[index].source == source) {
                if (!state->value_samples[index].valid) return false;
                *resolved = state->value_samples[index].computed;
                return true;
            }
        }
        return false;
    }
    *resolved = *source;
    return true;
}

static void css_animation_interpolate_length(const CssAnimatedProp* from,
                                              const CssAnimatedProp* to, float progress,
                                              CssAnimatedProp* result) {
    if (from->value.length.keyword != CSS_VALUE__UNDEF ||
        to->value.length.keyword != CSS_VALUE__UNDEF) {
        result->value.length = progress < .5f ? from->value.length : to->value.length;
    } else {
        result->value.length.value = css_interpolate_float(
            from->value.length.value, to->value.length.value, progress);
        result->value.length.keyword = CSS_VALUE__UNDEF;
        result->expression = nullptr;
        result->value.length.is_percent = false;
    }
}

struct CssAnimationLengthSlot {
    float* value = nullptr;
    CssEnum* type = nullptr;
    float* percent = nullptr;
    bool* present = nullptr;
    float* flow_value = nullptr;
    CssEnum* flow_type = nullptr;
    bool allow_negative = false;
};

static CssAnimationLengthSlot css_animation_length_slot(DomElement* element,
                                                        CssPropertyCode property,
                                                        bool writable = false) {
    if (!element) return {};
    ViewTree* tree = element->doc ? element->doc->view_tree : nullptr;
    BlockProp* block = element->blk;
    BoundaryProp* boundary = element->bound;
    CssBoxSide side = radiant_css_box_side(property);
    switch (property) {
        case CSS_PROPERTY_WIDTH:
            if (!block) return {};
            return {&block->given_width, &block->given_width_type,
                    &block->given_width_percent};
        case CSS_PROPERTY_HEIGHT:
            if (!block) return {};
            return {&block->given_height, &block->given_height_type,
                    &block->given_height_percent};
        case CSS_PROPERTY_MIN_WIDTH:
            return block ? CssAnimationLengthSlot{&block->given_min_width,
                &block->given_min_width_type, &block->given_min_width_percent} : CssAnimationLengthSlot{};
        case CSS_PROPERTY_MAX_WIDTH:
            return block ? CssAnimationLengthSlot{&block->given_max_width,
                &block->given_max_width_type, &block->given_max_width_percent} : CssAnimationLengthSlot{};
        case CSS_PROPERTY_MIN_HEIGHT:
            return block ? CssAnimationLengthSlot{&block->given_min_height,
                &block->given_min_height_type, &block->given_min_height_percent} : CssAnimationLengthSlot{};
        case CSS_PROPERTY_MAX_HEIGHT:
            return block ? CssAnimationLengthSlot{&block->given_max_height,
                &block->given_max_height_type, &block->given_max_height_percent} : CssAnimationLengthSlot{};
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT: {
            if (writable) boundary = element->ensure_boundary(tree);
            if (!boundary) return {};
            // collapse mutates margin; transitions capture the uncollapsed source.
            Margin* margin = !writable && boundary->has_flow_margin
                ? &boundary->flow_margin : &boundary->margin;
            return {radiant_spacing_value(margin, side), radiant_margin_type(margin, side),
                nullptr, nullptr,
                writable && boundary->has_flow_margin
                    ? radiant_spacing_value(&boundary->flow_margin, side) : nullptr,
                writable && boundary->has_flow_margin
                    ? radiant_margin_type(&boundary->flow_margin, side) : nullptr, true};
        }
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT:
            if (writable) boundary = element->ensure_boundary(tree);
            return boundary ? CssAnimationLengthSlot{
                radiant_spacing_value(&boundary->padding, side)} : CssAnimationLengthSlot{};
        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH: {
            if (writable) boundary = element->ensure_boundary(tree);
            BorderProp* border = writable
                ? radiant_ensure_border_prop(boundary, tree ? tree->prop_pool : nullptr)
                : boundary ? boundary->border.get() : nullptr;
            return border ? CssAnimationLengthSlot{
                radiant_border_side(border, side).width.get()} : CssAnimationLengthSlot{};
        }
        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_BOTTOM:
        case CSS_PROPERTY_LEFT: {
            PositionProp* position = writable ? element->ensure_position(tree) : element->position;
            if (!position) return {};
            RadiantInsetSide inset = radiant_inset_side(position, side);
            return {inset.value.get(), nullptr, inset.percent.get(), inset.has.get(),
                    nullptr, nullptr, true};
        }
        default: return {};
    }
}

static bool capture_underlying_length(DomElement* element, CssPropertyCode id,
                                      CssAnimatedProp* out) {
    if (!element || !out) return false;
    CssAnimationLengthSlot slot = css_animation_length_slot(element, id);
    if (!slot.value) return false;
    float value = *slot.value;
    CssEnum keyword = slot.type ? *slot.type : CSS_VALUE__UNDEF;
    if (slot.present && !*slot.present) keyword = CSS_VALUE_AUTO;
    if (keyword == CSS_VALUE__UNDEF &&
        (!isfinite(value) || (!slot.allow_negative && value < 0.0f))) return false;
    out->property_code = id;
    out->value_type = ANIM_VAL_LENGTH;
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    out->value.length.value = value;
    out->value.length.is_percent = false;
    out->expression = nullptr;
    out->value.length.keyword = keyword;
    return true;
}

static bool capture_underlying_aspect_ratio(DomElement* element,
                                            CssAnimatedProp* out) {
    if (!element || !out) return false;
    float ratio = element->fi ? element->fi->aspect_ratio : 0.0f;
    if (!isfinite(ratio)) return false;

    out->property_code = CSS_PROPERTY_ASPECT_RATIO;
    out->value_type = ANIM_VAL_ASPECT_RATIO;
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    out->value.aspect_ratio.value = ratio;
    out->value.aspect_ratio.is_auto = ratio <= 0.0f;
    return true;
}

static bool capture_underlying_display(DomElement* element,
                                       CssAnimatedProp* out) {
    if (!element || !out) return false;
    out->property_code = CSS_PROPERTY_DISPLAY;
    out->value_type = ANIM_VAL_DISPLAY;
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    out->value.display.value = NULL;
    out->value.display.used = element->display;
    out->value.display.has_used = true;
    return true;
}

static void animation_update_layout_bounds(AnimationInstance* animation, View* target) {
    RdtLogicalPoint origin = view_geometry_node_document_origin(target);
    animation->bounds[0] = origin.x;
    animation->bounds[1] = origin.y;
    animation->bounds[2] = target->width;
    animation->bounds[3] = target->height;
}

static TransformFunction css_animation_identity_transform(TransformFunctionType type) {
    TransformFunction identity = {};
    identity.type = type;
    identity.translate_x_percent = NAN;
    identity.translate_y_percent = NAN;
    if (type == TRANSFORM_SCALE || type == TRANSFORM_SCALEX || type == TRANSFORM_SCALEY) {
        identity.params.scale = {1.0f, 1.0f};
    } else if (type == TRANSFORM_SCALE3D || type == TRANSFORM_SCALEZ) {
        identity.params.scale3d = {1.0f, 1.0f, 1.0f};
    } else if (type == TRANSFORM_MATRIX) {
        identity.params.matrix.a = identity.params.matrix.d = 1.0f;
    } else if (type == TRANSFORM_MATRIX3D) {
        identity.params.matrix3d[0] = identity.params.matrix3d[5] =
            identity.params.matrix3d[10] = identity.params.matrix3d[15] = 1.0f;
    } else if (type == TRANSFORM_PERSPECTIVE) {
        identity.params.perspective = INFINITY;
    }
    return identity;
}

static TransformFunctionType transform_interpolation_primitive(TransformFunctionType type) {
    switch (type) {
        case TRANSFORM_TRANSLATE: case TRANSFORM_TRANSLATEX: case TRANSFORM_TRANSLATEY:
        case TRANSFORM_TRANSLATE3D: case TRANSFORM_TRANSLATEZ: return TRANSFORM_TRANSLATE3D;
        case TRANSFORM_SCALE: case TRANSFORM_SCALEX: case TRANSFORM_SCALEY:
        case TRANSFORM_SCALE3D: case TRANSFORM_SCALEZ: return TRANSFORM_SCALE3D;
        case TRANSFORM_ROTATE: case TRANSFORM_ROTATEZ: case TRANSFORM_ROTATEX:
        case TRANSFORM_ROTATEY: case TRANSFORM_ROTATE3D: return TRANSFORM_ROTATE3D;
        default: return type;
    }
}

static TransformFunction normalize_transform_primitive(const TransformFunction* source,
                                                       TransformFunctionType type) {
    TransformFunction result = *source;
    if (type == TRANSFORM_SCALE3D && source->type != TRANSFORM_SCALE3D && source->type != TRANSFORM_SCALEZ)
        result.params.scale3d.z = 1.0f;
    if (type == TRANSFORM_ROTATE3D) {
        if (source->type != TRANSFORM_ROTATE3D) {
            result.params.rotate3d = {source->type == TRANSFORM_ROTATEX ? 1.0f : 0.0f,
                source->type == TRANSFORM_ROTATEY ? 1.0f : 0.0f,
                source->type == TRANSFORM_ROTATE || source->type == TRANSFORM_ROTATEZ ? 1.0f : 0.0f,
                source->params.angle};
        }
        float axis[3] = {result.params.rotate3d.x, result.params.rotate3d.y, result.params.rotate3d.z};
        if (radiant::normalize_transform_vector3(axis) == 0.0f) result.params.rotate3d.angle = 0.0f;
        result.params.rotate3d.x = axis[0]; result.params.rotate3d.y = axis[1]; result.params.rotate3d.z = axis[2];
    }
    result.type = type;
    return result;
}

static bool transform_list_depends_on_reference_box(const TransformFunction* function) {
    for (; function; function = function->next) {
        if ((!isnan(function->translate_x_percent) && function->translate_x_percent != 0.0f) ||
            (!isnan(function->translate_y_percent) && function->translate_y_percent != 0.0f) ||
            function->translate_math[0] || function->translate_math[1] || function->matrix_interpolation) return true;
    }
    return false;
}

static TransformFunction* interpolate_transform_matrix_functions(TransformFunction* a, TransformFunction* b,
                                                                  float progress, CssAnimState* state,
                                                                  bool* decomposable) {
    Pool* pool = state->pool;
    RdtMatrix4 from = radiant::compute_transform_matrix_3d(a, state->element->width, state->element->height, 0.0f, 0.0f);
    RdtMatrix4 to = radiant::compute_transform_matrix_3d(b, state->element->width, state->element->height, 0.0f, 0.0f);
    RdtMatrix4 matrix;
    *decomposable = radiant::interpolate_transform_matrix(&from, &to, progress, &matrix);
    if (!*decomposable) return nullptr;
    TransformFunction* sampled = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
    if (!sampled) return nullptr;
    sampled->translate_x_percent = sampled->translate_y_percent = NAN;
    if (rdt_matrix4_is_2d(&matrix)) {
        sampled->type = TRANSFORM_MATRIX;
        sampled->params.matrix = {matrix.values[0], matrix.values[4], matrix.values[1],
            matrix.values[5], matrix.values[3], matrix.values[7]};
    } else {
        sampled->type = TRANSFORM_MATRIX3D;
        for (int row = 0; row < 4; row++)
            for (int column = 0; column < 4; column++) sampled->params.matrix3d[column * 4 + row] = matrix.values[row * 4 + column];
    }
    if (transform_list_depends_on_reference_box(a) || transform_list_depends_on_reference_box(b)) {
        // retain endpoint products until used-value matrix construction knows the sized box.
        sampled->type = TRANSFORM_MATRIX3D;
        sampled->matrix_interpolation = lam::own((TransformMatrixInterpolation*)pool_calloc(pool, sizeof(TransformMatrixInterpolation)));
        if (sampled->matrix_interpolation) {
            sampled->matrix_interpolation->progress = progress;
            sampled->matrix_interpolation->from = lam::own(radiant::clone_transform_list(pool, a));
            sampled->matrix_interpolation->to = lam::own(radiant::clone_transform_list(pool, b));
        }
        if (!sampled->matrix_interpolation ||
            (a && !sampled->matrix_interpolation->from) || (b && !sampled->matrix_interpolation->to)) {
            radiant::destroy_transform_list(pool, sampled);
            return nullptr;
        }
    }
    return sampled;
}

template <typename Vector>
static void interpolate_transform_components(Vector* result, const Vector* a, const Vector* b,
                                               float progress) {
    result->x = css_interpolate_float(a->x, b->x, progress);
    result->y = css_interpolate_float(a->y, b->y, progress);
}

// Interpolate a single transform function pair
static TransformFunction* interpolate_transform_func(TransformFunction* a, TransformFunction* b,
                                                       float t, CssAnimState* state, bool* decomposable) {
    Pool* pool = state->pool;
    if (!a && !b) return NULL;
    // `none` and shorter lists pad with matching identity functions.
    TransformFunction identity = css_animation_identity_transform(a ? a->type : b->type);
    if (!a) a = &identity;
    if (!b) b = &identity;

    TransformFunction normalized_a, normalized_b;
    if ((a->type != b->type || a->type == TRANSFORM_ROTATE3D) &&
        transform_interpolation_primitive(a->type) == transform_interpolation_primitive(b->type)) {
        TransformFunctionType primitive = transform_interpolation_primitive(a->type);
        if (primitive == TRANSFORM_ROTATE3D &&
            (a->type == TRANSFORM_ROTATE || a->type == TRANSFORM_ROTATEZ) &&
            (b->type == TRANSFORM_ROTATE || b->type == TRANSFORM_ROTATEZ)) primitive = TRANSFORM_ROTATE;
        normalized_a = normalize_transform_primitive(a, primitive);
        normalized_b = normalize_transform_primitive(b, primitive);
        a = &normalized_a; b = &normalized_b;
    }
    bool rotation_matrix = a->type == TRANSFORM_ROTATE3D && b->type == TRANSFORM_ROTATE3D &&
        a->params.rotate3d.angle != 0.0f && b->params.rotate3d.angle != 0.0f &&
        (a->params.rotate3d.x != b->params.rotate3d.x || a->params.rotate3d.y != b->params.rotate3d.y ||
         a->params.rotate3d.z != b->params.rotate3d.z);
    if (rotation_matrix || a->type == TRANSFORM_MATRIX || a->type == TRANSFORM_MATRIX3D || a->type == TRANSFORM_PERSPECTIVE) {
        // matching matrix/rotation pairs interpolate locally; subsequent functions retain their turns.
        TransformFunction local_a = *a, local_b = *b;
        local_a.next = local_b.next = nullptr;
        return interpolate_transform_matrix_functions(&local_a, &local_b, t, state, decomposable);
    }
    TransformFunction* result = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
    if (!result) return nullptr;
    result->translate_x_percent = NAN;
    result->translate_y_percent = NAN;

    if (a->type == b->type) {
        result->type = a->type;
        switch (a->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY:
            case TRANSFORM_TRANSLATE3D:
            case TRANSFORM_TRANSLATEZ:
                interpolate_transform_components(&result->params.translate3d, &a->params.translate3d,
                    &b->params.translate3d, t);
                if (a->type == TRANSFORM_TRANSLATE3D || a->type == TRANSFORM_TRANSLATEZ)
                    result->params.translate3d.z = css_interpolate_float(a->params.translate3d.z, b->params.translate3d.z, t);
                // a computed translation can contain both pixels and a reference-box percentage.
                if (!isnan(a->translate_x_percent) || !isnan(b->translate_x_percent))
                    result->translate_x_percent = css_interpolate_float(
                        isnan(a->translate_x_percent) ? 0.0f : a->translate_x_percent,
                        isnan(b->translate_x_percent) ? 0.0f : b->translate_x_percent, t);
                if (!isnan(a->translate_y_percent) || !isnan(b->translate_y_percent))
                    result->translate_y_percent = css_interpolate_float(
                        isnan(a->translate_y_percent) ? 0.0f : a->translate_y_percent,
                        isnan(b->translate_y_percent) ? 0.0f : b->translate_y_percent, t);
                for (int axis = 0; axis < 2; axis++) {
                    result->translate_math[axis] = lam::own(radiant::interpolate_transform_length_terms(
                        pool, a->translate_math[axis], b->translate_math[axis], t));
                    if ((a->translate_math[axis] || b->translate_math[axis]) && !result->translate_math[axis]) {
                        radiant::destroy_transform_list(pool, result);
                        return nullptr;
                    }
                }
                break;
            case TRANSFORM_SCALE:
            case TRANSFORM_SCALEX:
            case TRANSFORM_SCALEY:
            case TRANSFORM_SCALE3D:
            case TRANSFORM_SCALEZ:
                interpolate_transform_components(&result->params.scale3d, &a->params.scale3d,
                    &b->params.scale3d, t);
                if (a->type == TRANSFORM_SCALE3D || a->type == TRANSFORM_SCALEZ)
                    result->params.scale3d.z = css_interpolate_float(a->params.scale3d.z, b->params.scale3d.z, t);
                break;
            case TRANSFORM_ROTATE:
            case TRANSFORM_ROTATEX:
            case TRANSFORM_ROTATEY:
            case TRANSFORM_ROTATEZ:
            case TRANSFORM_SKEWX:
            case TRANSFORM_SKEWY:
                result->params.angle = css_interpolate_float(a->params.angle, b->params.angle, t);
                break;
            case TRANSFORM_SKEW:
                interpolate_transform_components(&result->params.skew, &a->params.skew,
                    &b->params.skew, t);
                break;
            case TRANSFORM_ROTATE3D: {
                const TransformFunction* direction = a->params.rotate3d.angle != 0.0f ? a : b;
                result->params.rotate3d = direction->params.rotate3d;
                if (a->params.rotate3d.angle == 0.0f && b->params.rotate3d.angle == 0.0f)
                    result->params.rotate3d = {0.0f, 0.0f, 1.0f, 0.0f};
                result->params.rotate3d.angle = css_interpolate_float(a->params.rotate3d.angle, b->params.rotate3d.angle, t);
                break;
            }
            default:
                // unsupported — use 'a' value
                pool_free(pool, result);
                return radiant::clone_transform_function(pool, a);
        }
    } else {
        pool_free(pool, result);
        return radiant::clone_transform_function(pool, a);
    }
    return result;
}

// at the first incompatible pair, interpolate the entire remaining matrix product.
static TransformFunction* interpolate_transform_list(TransformFunction* a, TransformFunction* b,
                                                       float t, CssAnimState* state) {
    Pool* pool = state->pool;
    TransformFunction* full_a = a;
    TransformFunction* full_b = b;
    TransformFunction* head = nullptr;
    TransformFunction* tail = nullptr;
    while (a || b) {
        TransformFunction* sampled = nullptr;
        bool decomposable = true;
        bool matrix_suffix = a && b && transform_interpolation_primitive(a->type) != transform_interpolation_primitive(b->type);
        if (matrix_suffix) {
            sampled = interpolate_transform_matrix_functions(a, b, t, state, &decomposable);
            a = b = nullptr;
        } else {
            sampled = interpolate_transform_func(a, b, t, state, &decomposable);
            if (a) a = a->next;
            if (b) b = b->next;
        }
        if (!decomposable) {
            // a singular endpoint makes the entire property, including any prefix, discrete.
            radiant::destroy_transform_list(pool, head);
            return radiant::clone_transform_list(pool, t < .5f ? full_a : full_b);
        }
        if (!sampled) { radiant::destroy_transform_list(pool, head); return nullptr; }
        if (sampled) {
            sampled->next = nullptr;
            if (tail) tail->next = lam::own(sampled);
            else head = sampled;
            tail = sampled;
        }
    }
    return head;
}

// Lazily ensure InlineProp exists on the span (needed for opacity/color animation
// when the element has no static opacity/color declaration)
static InlineProp* ensure_inline_prop(ViewSpan* span) {
    DomElement* el = lam::dom_require_element(span);
    if (el->doc && el->doc->view_tree) {
        // A present InlineProp may still be shared; ensure_inline performs the
        // copy before animation writes into the element's live style.
        return span->ensure_inline(el->doc->view_tree);
    }
    return span->inline_prop_shared() ? NULL : span->in_line;
}

// Lazily ensure BoundaryProp + BackgroundProp exist (needed for background-color
// animation when the element has no static background declaration)
static BackgroundProp* ensure_background_prop(ViewSpan* span) {
    DomElement* el = lam::dom_require_element(span);
    Pool* pool = (el->doc && el->doc->view_tree) ? el->doc->view_tree->prop_pool : NULL;
    if (!pool) return span->bound ? span->boundary()->background.get() : nullptr;
    if (!span->bound) span->ensure_boundary(el->doc->view_tree);
    if (span->bound && !span->boundary()->background) {
        span->bound->background = lam::own((BackgroundProp*)pool_calloc(pool, sizeof(BackgroundProp)));
    }
    return span->bound ? span->boundary()->background : NULL;
}

struct CssAnimationColorSlot {
    Color* value;
    bool* present;
};

static CssAnimationColorSlot css_animation_color_slot(DomElement* element,
                                                       CssPropertyCode property,
                                                       bool writable = false) {
    if (!element) return {};
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));
    switch (property) {
        case CSS_PROPERTY_COLOR: {
            InlineProp* style = writable ? ensure_inline_prop(span) : element->in_line;
            return style ? CssAnimationColorSlot{&style->color, &style->has_color}
                : CssAnimationColorSlot{};
        }
        case CSS_PROPERTY_BACKGROUND_COLOR: {
            BackgroundProp* background = writable ? ensure_background_prop(span)
                : element->bound ? element->boundary()->background.get() : nullptr;
            return background ? CssAnimationColorSlot{&background->color} : CssAnimationColorSlot{};
        }
        case CSS_PROPERTY_BORDER_TOP_COLOR:
        case CSS_PROPERTY_BORDER_RIGHT_COLOR:
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
        case CSS_PROPERTY_BORDER_LEFT_COLOR: {
            ViewTree* tree = element->doc ? element->doc->view_tree : nullptr;
            BoundaryProp* boundary = writable ? element->ensure_boundary(tree) : element->bound;
            BorderProp* border = writable
                ? radiant_ensure_border_prop(boundary, tree ? tree->prop_pool : nullptr)
                : boundary ? boundary->border.get() : nullptr;
            return border ? CssAnimationColorSlot{
                radiant_border_side(border, radiant_css_box_side(property)).color.get()}
                : CssAnimationColorSlot{};
        }
        default: return {};
    }
}

static bool capture_underlying_value(DomElement* element, CssPropertyCode property,
                                     CssAnimatedProp* out) {
    if (!element || !out) return false;
    out->property_code = property;
    out->value_type = property_value_type(property);
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    switch (out->value_type) {
        case ANIM_VAL_LENGTH:
            return capture_underlying_length(element, property, out);
        case ANIM_VAL_ASPECT_RATIO:
            return capture_underlying_aspect_ratio(element, out);
        case ANIM_VAL_DISPLAY:
            return capture_underlying_display(element, out);
        case ANIM_VAL_FLOAT:
            out->value.f = element->in_line ? element->in_line->opacity : 1.0f;
            return true;
        case ANIM_VAL_BACKGROUND_POSITION: {
            BackgroundProp* background = element->bound ? element->bound->background.get() : nullptr;
            out->value.background_position = {};
            if (background) {
                bool horizontal = property == CSS_PROPERTY_BACKGROUND_POSITION_X;
                float value = horizontal ? background->bg_position_x : background->bg_position_y;
                bool percent = horizontal ? background->bg_position_x_is_percent : background->bg_position_y_is_percent;
                out->value.background_position.pixels = horizontal ? background->bg_position_x_length : background->bg_position_y_length;
                if (percent) out->value.background_position.percent = value;
                else out->value.background_position.pixels += value;
            }
            return true;
        }
        case ANIM_VAL_IMAGE:
            out->value.image = element->bound && element->bound->background ? element->bound->background->image : nullptr;
            return true;
        case ANIM_VAL_FILTER:
            out->value.filter = element->filter_prop() ? element->filter_prop()->functions.get() : nullptr;
            return true;
        case ANIM_VAL_TRANSFORM:
            out->value.transform = nullptr;
            if (element->transform) {
                int slot = animation_transform_slot(property);
                if (slot == 0) out->value.transform = element->transform->functions.get();
                else if (element->transform->individual[slot - 1].type != TRANSFORM_NONE)
                    out->value.transform = &element->transform->individual[slot - 1];
            }
            return true;
        case ANIM_VAL_COLOR: {
            CssAnimationColorSlot slot = css_animation_color_slot(element, property);
            out->value.color = slot.value && (!slot.present || *slot.present)
                ? *slot.value : property == CSS_PROPERTY_BACKGROUND_COLOR ? Color{0}
                    : get_current_color_for_view(lam::view_require_element(static_cast<View*>(element)));
            return true;
        }
        default: return false;
    }
}

// Apply an interpolated property value to a DomElement
static bool apply_animated_value(DomElement* element, CssAnimatedProp* prop) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));
    bool layout_changed = false;

    if (prop->value_type == ANIM_VAL_LENGTH) {
        CssAnimationLengthSlot slot = css_animation_length_slot(element, prop->property_code, true);
        if (!slot.value) return false;
        if (prop->value.length.keyword != CSS_VALUE__UNDEF) {
            float before = *slot.value;
            CssEnum before_type = slot.type ? *slot.type : CSS_VALUE__UNDEF;
            bool before_present = slot.present && *slot.present;
            ViewBlock* block = lam::view_as_block(static_cast<View*>(element));
            if (block && (prop->property_code == CSS_PROPERTY_WIDTH ||
                          prop->property_code == CSS_PROPERTY_HEIGHT ||
                          prop->property_code == CSS_PROPERTY_MIN_WIDTH ||
                          prop->property_code == CSS_PROPERTY_MAX_WIDTH ||
                          prop->property_code == CSS_PROPERTY_MIN_HEIGHT ||
                          prop->property_code == CSS_PROPERTY_MAX_HEIGHT)) {
                LayoutContext context = {};
                context.doc = element->doc;
                context.view = lam::up(static_cast<View*>(element));
                context.elmt = lam::up(element);
                context.pool = lam::up(element->doc && element->doc->view_tree
                    ? element->doc->view_tree->prop_pool : nullptr);
                CssValue value = {};
                value.type = CSS_VALUE_TYPE_KEYWORD;
                value.data.keyword = prop->value.length.keyword;
                if (prop->property_code == CSS_PROPERTY_WIDTH || prop->property_code == CSS_PROPERTY_HEIGHT) {
                    resolve_css_axis_size(&context, block, &value,
                        prop->property_code == CSS_PROPERTY_WIDTH ? LAYOUT_AXIS_X : LAYOUT_AXIS_Y);
                } else resolve_css_dimension_constraint(&context, block, prop->property_code, &value);
            } else {
                *slot.value = 0.0f;
                if (slot.type) *slot.type = prop->value.length.keyword;
                if (slot.percent) *slot.percent = NAN;
                if (slot.present) *slot.present = false;
                if (slot.flow_value) *slot.flow_value = *slot.value;
                if (slot.flow_type) *slot.flow_type = prop->value.length.keyword;
            }
            return before != *slot.value ||
                (slot.type && before_type != *slot.type) ||
                (slot.present && before_present != *slot.present);
        }
        if (!isfinite(prop->value.length.value)) return false;
        // easing can extrapolate beyond valid endpoints; signed spacing remains legal.
        float value = slot.allow_negative ? prop->value.length.value
            : fmaxf(0.0f, prop->value.length.value);
        layout_changed = *slot.value != value ||
            (slot.present && !*slot.present) ||
            (slot.type && *slot.type != CSS_VALUE__UNDEF) ||
            (slot.percent && !isnan(*slot.percent));
        *slot.value = value;
        if (slot.type) *slot.type = CSS_VALUE__UNDEF;
        if (slot.percent) *slot.percent = NAN;
        if (slot.present) *slot.present = true;
        if (slot.flow_value) *slot.flow_value = *slot.value;
        if (slot.flow_type) *slot.flow_type = CSS_VALUE__UNDEF;
        return layout_changed;
    }
    if (prop->value_type == ANIM_VAL_COLOR) {
        CssAnimationColorSlot slot = css_animation_color_slot(element, prop->property_code, true);
        if (slot.value) *slot.value = prop->value.color;
        if (slot.present) *slot.present = true;
        return false;
    }
    if (prop->value_type == ANIM_VAL_BACKGROUND_POSITION) {
        LayoutContext layout = {};
        layout.doc = element->doc;
        layout.pool = lam::up(element->doc && element->doc->view_tree
            ? element->doc->view_tree->prop_pool.get() : nullptr);
        layout_ensure_background(&layout, span);
        BackgroundProp* background = span->boundary()->background;
        bool horizontal = prop->property_code == CSS_PROPERTY_BACKGROUND_POSITION_X;
        if (horizontal) {
            background->bg_position_x = prop->value.background_position.percent;
            background->bg_position_x_length = prop->value.background_position.pixels;
            background->bg_position_x_is_percent = true;
        } else {
            background->bg_position_y = prop->value.background_position.percent;
            background->bg_position_y_length = prop->value.background_position.pixels;
            background->bg_position_y_is_percent = true;
        }
        background->bg_position_set = true;
        return false;
    }
    if (prop->value_type == ANIM_VAL_IMAGE) {
        LayoutContext layout = {};
        layout.doc = element->doc;
        layout.pool = lam::up(element->doc && element->doc->view_tree ? element->doc->view_tree->prop_pool.get() : nullptr);
        BackgroundProp* background = layout_ensure_background(&layout, span);
        if (prop->value.image) radiant_retain_background_image(background, lam::PoolPtr<char>(prop->value.image));
        else radiant_clear_background_image(background);
        return false;
    }
    if (prop->value_type == ANIM_VAL_FILTER) {
        FilterProp* filter = element->filter_prop();
        if (!filter && element->doc && element->doc->view_tree) filter = element->ensure_filter(element->doc->view_tree);
        if (filter) {
            if (!filter->functions_borrowed && element->doc && element->doc->view_tree) {
                radiant::destroy_filter_list(element->doc->view_tree->prop_pool.get(), filter->functions);
                filter->functions = nullptr;
            }
            filter->functions = lam::own(prop->value.filter);
            filter->functions_borrowed = true;
        }
        return false;
    }

    switch (prop->property_code) {
        case CSS_PROPERTY_DISPLAY: {
            DisplayValue display;
            bool resolved = prop->value.display.has_used
                ? (display = prop->value.display.used, true)
                : css_resolve_display_css_value(
                    element, prop->value.display.value, &display);
            if (resolved) {
                layout_changed = element->display.outer != display.outer ||
                    element->display.inner != display.inner ||
                    element->display.list_item != display.list_item;
                element->display = display;
                element->set_animated_display(display);
            }
            break;
        }
        case CSS_PROPERTY_OPACITY: {
            InlineProp* il = ensure_inline_prop(span);
            if (il) il->opacity = clamp_unit(prop->value.f);
            break;
        }
        case CSS_PROPERTY_TRANSFORM:
        case CSS_PROPERTY_TRANSLATE:
        case CSS_PROPERTY_ROTATE:
        case CSS_PROPERTY_SCALE: {
            if (!span->transform) {
                if (element->doc && element->doc->view_tree)
                    span->ensure_transform(element->doc->view_tree);
            }
            if (span->transform) {
                int slot = animation_transform_slot(prop->property_code);
                if (slot > 0) {
                    span->transform->individual[slot - 1] = prop->value.transform ? *prop->value.transform : TransformFunction{};
                    span->transform->individual_sample[slot - 1] = prop->value.transform;
                    break;
                }
                if (span->transform->functions_owner == TRANSFORM_FUNCTIONS_VIEW_POOL &&
                    element->doc && element->doc->view_tree) {
                    radiant::destroy_transform_list(element->doc->view_tree->prop_pool,
                        span->transform->functions);
                }
                span->transform->functions = lam::shared(prop->value.transform);
                // Keyframe lists are document-owned and may be sampled again
                // after a retained view-pool reset.
                span->transform->functions_owner = TRANSFORM_FUNCTIONS_DOCUMENT_POOL;
            }
            break;
        }
        case CSS_PROPERTY_ASPECT_RATIO: {
            ViewBlock* block = lam::view_as_block(static_cast<View*>(element));
            if (!block || !block->fi) break;
            // Layout reads the resolved ratio from the flex-item property; the
            // keyframe value must update that same used-value source each tick.
            float ratio = prop->value.aspect_ratio.is_auto ? 0.0f : prop->value.aspect_ratio.value;
            layout_changed = block->fi->aspect_ratio != ratio;
            block->fi->aspect_ratio = ratio;
            break;
        }
        default:
            break;
    }
    return layout_changed;
}

static float css_animation_composite_length(CssAnimState* state,
                                            CssAnimatedProp* prop,
                                            float value) {
    if (!state || !prop || prop->composite == CSS_ANIM_COMPOSITE_REPLACE) {
        return value;
    }
    CssAnimatedProp* underlying = find_underlying_prop(state, prop->property_code);
    if (!underlying || underlying->value.length.keyword != CSS_VALUE__UNDEF ||
        prop->value.length.keyword != CSS_VALUE__UNDEF) return value;

    // Keyframe composition was previously discarded, so additive sizing effects
    // replaced the underlying used value instead of composing over it.
    return value + underlying->value.length.value;
}

static float css_interpolate_aspect_ratio(float from, float to, float t) {
    if (from <= 0.0f || to <= 0.0f || !isfinite(from) || !isfinite(to)) {
        return t < 0.5f ? from : to;
    }
    // CSS Values combines positive ratios in log space, so 1/2 -> 2/1 passes
    // through 1/1 at the midpoint instead of linearly producing 1.25/1.
    return expf(logf(from) + (logf(to) - logf(from)) * t);
}

static bool css_display_is_none(DisplayValue display) {
    return display.outer == CSS_VALUE_NONE;
}

static bool css_animation_resolve_display(DomElement* element,
                                          CssAnimatedProp* prop,
                                          DisplayValue* out_display) {
    if (!element || !prop || !out_display ||
        prop->value_type != ANIM_VAL_DISPLAY) return false;
    if (prop->value.display.has_used) {
        *out_display = prop->value.display.used;
        return true;
    }
    return css_resolve_display_css_value(
        element, prop->value.display.value, out_display);
}

static DisplayValue css_interpolate_display(DomElement* element,
                                             CssAnimatedProp* from,
                                             CssAnimatedProp* to, float t) {
    DisplayValue from_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
    DisplayValue to_display = {CSS_VALUE_NONE, CSS_VALUE_NONE};
    if (!css_animation_resolve_display(element, from, &from_display) ||
        !css_animation_resolve_display(element, to, &to_display)) {
        return t < 0.5f ? from_display : to_display;
    }
    // CSS Display 3: `none` stays at the endpoint of an appearance or
    // disappearance transition; other discrete values flip at 50 percent.
    if (css_display_is_none(from_display) && !css_display_is_none(to_display)) {
        return t <= 0.0f ? from_display : to_display;
    }
    if (!css_display_is_none(from_display) && css_display_is_none(to_display)) {
        return t < 1.0f ? from_display : to_display;
    }
    return t < 0.5f ? from_display : to_display;
}

struct CssAnimationPropertyEndpoint {
    CssAnimatedProp property;
    float offset;
    const TimingFunction* timing;
};

struct CssAnimationPropertyPair {
    CssAnimationPropertyEndpoint from, to;
};

static void css_animation_append_endpoint(CssAnimationPropertyEndpoint endpoint,
                                           float progress,
                                           CssAnimationPropertyEndpoint* previous,
                                           bool* has_previous, bool* has_pair,
                                           CssAnimationPropertyPair* pair) {
    if (*has_previous && (!*has_pair || progress >= previous->offset)) {
        pair->from = *previous;
        pair->to = endpoint;
        *has_pair = true;
    }
    *previous = endpoint;
    *has_previous = true;
}

static bool css_animation_property_pair(CssAnimState* state, const CssAnimatedProp* key,
                                         float progress, CssAnimationPropertyPair* pair) {
    CssAnimatedProp* underlying = find_underlying_prop(state, key->property_code, key->custom_name);
    CssAnimationPropertyEndpoint previous = {};
    bool has_previous = false, has_pair = false;
    for (int index = 0; index < state->keyframes->stop_count; ) {
        CssKeyframeStop* stop = &state->keyframes->stops[index];
        CssAnimationPropertyEndpoint endpoint = {};
        endpoint.offset = stop->offset;
        bool has_property = false;
        // equal-offset blocks cascade in source order, including their timing descriptor.
        do {
            stop = &state->keyframes->stops[index++];
            CssAnimatedProp* source = find_prop_in_stop(stop, key->property_code, key->custom_name);
            CssAnimatedProp resolved = {};
            if (source && css_animation_resolved_property(state, source, &resolved)) {
                endpoint.property = resolved;
                has_property = true;
            }
            if (stop->timing) endpoint.timing = stop->timing;
        } while (index < state->keyframes->stop_count &&
                 state->keyframes->stops[index].offset == endpoint.offset);
        if (!has_property) continue;
        if (!has_previous && endpoint.offset > 0.0f) {
            if (!underlying) return false;
            CssAnimationPropertyEndpoint neutral = {*underlying, 0.0f, nullptr};
            css_animation_append_endpoint(neutral, progress, &previous,
                &has_previous, &has_pair, pair);
        }
        css_animation_append_endpoint(endpoint, progress, &previous,
            &has_previous, &has_pair, pair);
    }
    if (has_previous && previous.offset < 1.0f) {
        if (!underlying) return false;
        CssAnimationPropertyEndpoint neutral = {*underlying, 1.0f, nullptr};
        css_animation_append_endpoint(neutral, progress, &previous,
            &has_previous, &has_pair, pair);
    }
    return has_pair;
}

static void css_animation_composite_endpoint(CssAnimState* state, CssAnimatedProp* property) {
    if (property->value_type == ANIM_VAL_LENGTH) {
        property->value.length.value = css_animation_composite_length(
            state, property, property->value.length.value);
    } else if (property->value_type == ANIM_VAL_ASPECT_RATIO &&
               property->composite != CSS_ANIM_COMPOSITE_REPLACE) {
        CssAnimatedProp* underlying = find_underlying_prop(state, property->property_code);
        if (underlying) property->value.aspect_ratio = underlying->value.aspect_ratio;
    }
}

static void css_animation_interpolate_property(CssAnimState* state,
                                               CssAnimatedProp* from, CssAnimatedProp* to,
                                               float progress, CssAnimatedProp* result) {
    switch (result->value_type) {
        case ANIM_VAL_FLOAT:
            result->value.f = css_interpolate_float(from->value.f, to->value.f, progress);
            break;
        case ANIM_VAL_COLOR:
            result->value.color = css_interpolate_color(from->value.color, to->value.color, progress);
            break;
        case ANIM_VAL_BACKGROUND_POSITION:
            result->value.background_position.pixels = css_interpolate_float(
                from->value.background_position.pixels, to->value.background_position.pixels, progress);
            result->value.background_position.percent = css_interpolate_float(
                from->value.background_position.percent, to->value.background_position.percent, progress);
            break;
        case ANIM_VAL_IMAGE:
            result->value.image = progress < 0.5f ? from->value.image : to->value.image;
            break;
        case ANIM_VAL_FILTER:
            result->value.filter = interpolate_filter_list(state->pool, from->value.filter, to->value.filter, progress);
            break;
        case ANIM_VAL_CUSTOM:
            result->value.custom = css_interpolate_custom_property_value(state->custom_tick_pool,
                state->element, result->custom_name, from->value.custom, to->value.custom, progress);
            break;
        case ANIM_VAL_LENGTH:
            css_animation_interpolate_length(from, to, progress, result);
            break;
        case ANIM_VAL_ASPECT_RATIO:
            if (from->value.aspect_ratio.is_auto || to->value.aspect_ratio.is_auto) {
                result->value.aspect_ratio = progress < .5f
                    ? from->value.aspect_ratio : to->value.aspect_ratio;
            } else {
                result->value.aspect_ratio.value = css_interpolate_aspect_ratio(
                    from->value.aspect_ratio.value, to->value.aspect_ratio.value, progress);
                result->value.aspect_ratio.is_auto = false;
            }
            break;
        case ANIM_VAL_TRANSFORM:
            result->value.transform = interpolate_transform_list(from->value.transform,
                to->value.transform, progress, state);
            break;
        case ANIM_VAL_DISPLAY:
            result->value.display.used = css_interpolate_display(state->element, from, to, progress);
            result->value.display.value = nullptr;
            result->value.display.has_used = true;
            break;
        default: break;
    }
}

static void css_animation_dispatch_start(AnimationInstance* anim, CssAnimState* state) {
    if (!state->suppress_events && !state->event_started && anim->active_time >= 0.0) {
        state->event_started = true;
        state->event_iteration = anim->current_iteration;
        double duration = anim->iteration_count >= 0.0
            ? anim->duration * anim->iteration_count : INFINITY;
        double elapsed = fmin(fmax(-anim->delay, 0.0), duration);
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationstart", "animationName", state->keyframes->name, elapsed);
    }
}

struct CssCustomPaintTarget { DomElement* element; CssPropertyCode property; };
struct CssCustomPaintUpdate {
    DomElement* owner;
    DomElement* consumer;
    const char* name;
    Pool* scratch;
    CssCustomPaintTarget* targets;
    int target_count;
    int target_capacity;
    bool valid;
};

static bool css_custom_paint_consumer(StyleNode* node, void* opaque) {
    auto* update = (CssCustomPaintUpdate*)opaque;
    const CssDeclaration* declaration = node->winning_decl;
    if (!update->valid || !declaration || node->property_code == CSS_PROPERTY_CUSTOM ||
        !css_value_contains_var_reference(declaration->value)) return true;
    pool_reset(update->scratch);
    if (!css_value_depends_on_custom_property(update->scratch, update->consumer,
            declaration->value, update->owner, update->name)) return true;
    DomElement* element = update->consumer;
    CssPropertyCode property = node->property_code;
    bool color_filter = property == CSS_PROPERTY_FILTER;
    bool background_position = property == CSS_PROPERTY_BACKGROUND_POSITION_X ||
        property == CSS_PROPERTY_BACKGROUND_POSITION_Y || property == CSS_PROPERTY_BACKGROUND_POSITION;
    if ((!color_filter && !background_position) || element->view_type == RDT_VIEW_NONE ||
        css_animation_needs_computed_sample(element, property) ||
        (color_filter && (element->first_child || !element->filter_prop() ||
            !render_filter_is_color_only(element->filterp()->functions)))) {
        update->valid = false;
        return true;
    }
    if (color_filter) {
        LayoutContext context = {};
        context.doc = element->doc; context.view = lam::up(static_cast<View*>(element));
        context.elmt = lam::up(element); context.pool = lam::up(update->scratch);
        context.css_value_scratch = lam::up(update->scratch);
        const CssValue* value = css_resolve_element_var_value(update->scratch, element,
            declaration->value, property);
        FilterFunction* functions = resolve_filter_value(&context, property, value, update->scratch);
        update->valid = render_filter_is_color_only(functions);
        radiant::destroy_filter_list(update->scratch, functions);
    }
    if (update->valid) {
        update->valid = lam::mem_grow_array(&update->targets, &update->target_capacity,
            update->target_count + 1, 8, MEM_CAT_STYLE);
        if (update->valid) update->targets[update->target_count++] = {element, property};
    }
    return true;
}

static bool css_custom_paint_element(DomNode* node, void* opaque) {
    auto* update = (CssCustomPaintUpdate*)opaque;
    if (!node->is_element()) return true;
    DomElement* element = node->as_element();
    // Generated content and spatial effects keep the ordinary cascade/layout
    // path; existing color filters and background positions need paint only.
    for (unsigned kind = 0; kind < PSEUDO_STYLE_COUNT; kind++)
        if (element->pseudo_style((PseudoStyleKind)kind)) return false;
    update->consumer = element;
    style_tree_foreach(element->specified_style, css_custom_paint_consumer, update);
    return update->valid;
}

static bool css_animation_update_custom_paint(DomElement* owner, const char* name) {
    if (!owner->doc || !owner->doc->view_tree || !name) return false;
    Pool* scratch = pool_create_sized(0);
    if (!scratch) return false;
    CssCustomPaintUpdate update = {owner, nullptr, name, scratch, nullptr, 0, 0, true};
    bool valid = view_geometry_walk_dom_tree(owner, css_custom_paint_element, &update) && update.target_count > 0;
    if (valid) {
        LayoutContext context = {};
        context.doc = owner->doc;
        context.pool = lam::up(owner->doc->view_tree->prop_pool);
        for (int index = 0; index < update.target_count; index++) {
            DomElement* element = update.targets[index].element;
            CssPropertyCode property = update.targets[index].property;
            context.view = lam::up(static_cast<View*>(element)); context.elmt = lam::up(element);
            resolve_css_property(property,
                style_tree_get_declaration(element->specified_style, property), &context);
        }
    }
    mem_free(update.targets);
    pool_destroy(scratch);
    return valid;
}

void css_animation_tick(AnimationInstance* anim, float t) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (!state || !state->keyframes || !state->element) return;
    if (state->custom_tick_pool) pool_reset(state->custom_tick_pool);
    bool started = state->event_started;
    css_animation_dispatch_start(anim, state);
    if (started && !state->suppress_events &&
               anim->current_iteration > state->event_iteration) {
        state->event_iteration = anim->current_iteration;
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationiteration", "animationName", state->keyframes->name,
            anim->duration * (double)anim->current_iteration);
    }

    // Each property has its own sequence; unrelated stops cannot split its interval.
    bool visited[CSS_PROPERTY_COUNT] = {};
    for (int stop_index = 0; stop_index < state->keyframes->stop_count; stop_index++) {
        CssKeyframeStop* stop = &state->keyframes->stops[stop_index];
        for (int index = 0; index < stop->property_count; index++) {
            CssAnimatedProp* key = &stop->properties[index];
            CssPropertyCode property = key->property_code;
            if (key->custom_name) {
                bool seen = false;
                for (int prior = 0; prior < stop_index && !seen; prior++)
                    seen = find_prop_in_stop(&state->keyframes->stops[prior], property, key->custom_name) != nullptr;
                if (seen) continue;
                if (!state->custom_tick_pool) state->custom_tick_pool = mem_pool_create(nullptr, MEM_ROLE_CSS, "css.animation.custom-tick");
                if (!state->custom_tick_pool) continue;
            } else {
                if (property >= CSS_PROPERTY_COUNT || visited[property]) continue;
                visited[property] = true;
            }
            if (css_animation_property_is_important(state, property)) continue;
            CssAnimationPropertyPair pair = {};
            if (!css_animation_property_pair(state, key, t, &pair)) continue;
            float local_t = (t - pair.from.offset) / (pair.to.offset - pair.from.offset);
            const TimingFunction* timing = pair.from.timing ? pair.from.timing : &anim->timing;
            local_t = timing_function_eval(timing, local_t, animation_easing_before(anim));
            css_animation_composite_endpoint(state, &pair.from.property);
            css_animation_composite_endpoint(state, &pair.to.property);
            CssAnimatedProp result = pair.to.property;
            css_animation_interpolate_property(state, &pair.from.property,
                &pair.to.property, local_t, &result);
            if (result.custom_name) {
                bool changed = false;
                if (!result.value.custom) changed = dom_element_clear_animation_custom_properties(state->element, state, result.custom_name);
                else {
                    CssFormatter* formatter = css_formatter_create(state->custom_tick_pool, CSS_FORMAT_COMPACT);
                    if (!formatter) continue;
                    css_format_value(formatter, const_cast<CssValue*>(result.value.custom));
                    String* text = stringbuf_to_string(formatter->output);
                    if (!text) continue;
                    CssDeclaration sample = {};
                    sample.property_code = CSS_PROPERTY_CUSTOM;
                    sample.property_name = result.custom_name;
                    sample.property_name_length = strlen(result.custom_name);
                    sample.value = const_cast<CssValue*>(result.value.custom);
                    sample.value_text = text->chars;
                    sample.value_text_len = text->len;
                    dom_element_set_animation_custom_property(state->element, &sample, state, &changed);
                }
                if (changed && !css_animation_update_custom_paint(state->element, result.custom_name)) {
                    dom_invalidate_layout_subtree(state->element);
                    anim->layout_changed = true;
                }
            } else anim->layout_changed |= apply_animated_value(state->element, &result);
            if (result.value_type == ANIM_VAL_IMAGE) { state->sampled_image = result.value.image; state->image_applied = true; }
            if (result.value_type == ANIM_VAL_FILTER) {
                radiant::destroy_filter_list(state->pool, state->sampled_filter); state->sampled_filter = nullptr;
                state->sampled_filter = lam::own(result.value.filter);
                state->filter_applied = true;
            }
            if (result.value_type == ANIM_VAL_TRANSFORM) {
                // D4.5.1v4: replace the live borrow before reclaiming the preceding sample.
                int slot = animation_transform_slot(property);
                radiant::destroy_transform_list(state->pool, state->sampled_transform[slot]);
                state->sampled_transform[slot] = lam::own(result.value.transform);
            }
        }
    }

    // transformed dirty bounds share the client-rectangle geometry, including matrix/3D samples.
    view_get_visual_bounds(static_cast<View*>(anim->target), &anim->bounds[0],
        &anim->bounds[1], &anim->bounds[2], &anim->bounds[3]);
}

void css_animation_finish(AnimationInstance* anim) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (state) {
        if (anim->fill_mode != ANIM_FILL_FORWARDS && anim->fill_mode != ANIM_FILL_BOTH &&
            dom_element_clear_animation_custom_properties(state->element, state)) dom_invalidate_layout_subtree(state->element);
        // zero-duration and no-fill effects can finish without a painted active sample.
        css_animation_dispatch_start(anim, state);
        double elapsed = anim->iteration_count >= 0
            ? anim->duration * (double)anim->iteration_count : anim->duration;
        if (!state->suppress_events)
            radiant_dispatch_css_event(state->ui_context, state->element,
                "animationend", "animationName",
                state->keyframes ? state->keyframes->name : "", elapsed);
        // expired effects must restore the cascade even if their last sample changed geometry.
        if (state->element && state->element->doc)
            doc_state_request_reflow(state->element->doc->state);
        log_debug("css-anim: animation '%s' finished for element %p",
                  state->keyframes ? state->keyframes->name : "?", state->element);
    }
}

static void css_animation_restore_underlying_transform(CssAnimState* state) {
    if (state->element && state->element->transform &&
        state->sampled_transform[0] &&
        state->element->transform->functions.get() == state->sampled_transform[0].get()) {
        ViewTree* tree = state->element->doc ? state->element->doc->view_tree.get() : nullptr;
        Pool* target_pool = tree ? tree->prop_pool : state->pool;
        state->element->transform->functions = lam::shared(
            radiant::clone_transform_list(target_pool, state->underlying_transform[0]));
        state->element->transform->functions_owner = TRANSFORM_FUNCTIONS_VIEW_POOL;
    }
    // cancellation and later seeks must release live borrows for individual transforms too.
    for (int slot = 1; slot < 4; slot++) {
        if (state->element && state->element->transform && state->sampled_transform[slot] &&
            state->element->transform->individual_sample[slot - 1] == state->sampled_transform[slot].get()) {
            ViewTree* tree = state->element->doc ? state->element->doc->view_tree.get() : nullptr;
            Pool* target_pool = tree ? tree->prop_pool.get() : state->pool;
            TransformFunction* restored = radiant::clone_transform_list(target_pool, state->underlying_transform[slot]);
            state->element->transform->individual[slot - 1] = restored ? *restored : TransformFunction{};
            state->element->transform->individual_sample[slot - 1] = nullptr;
        }
    }
}

static void css_animation_cancel(AnimationInstance* anim) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (!state) return;
    if (dom_element_clear_animation_custom_properties(state->element, state)) {
        dom_invalidate_layout_subtree(state->element);
        if (!anim->suppress_cancel_event && state->element && state->element->doc)
            doc_state_request_reflow(state->element->doc->state);
    }
    if (!anim->suppress_cancel_event && !state->suppress_events && !anim->finish_notified) {
        double now = anim->play_state == ANIM_PLAY_PAUSED ? anim->pause_time : anim->sample_time;
        if (anim->play_state != ANIM_PLAY_PAUSED && state->ui_context &&
            state->ui_context->document && state->ui_context->document->state) {
            AnimationScheduler* scheduler = state->ui_context->document->state->animation_scheduler;
            if (scheduler) now = scheduler->current_time;
        }
        double elapsed = fmax(now - anim->start_time - anim->delay, 0.0);
        if (anim->iteration_count >= 0.0)
            elapsed = fmin(elapsed, anim->duration * anim->iteration_count);
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationcancel", "animationName", state->keyframes->name, elapsed);
    }
    if (state->image_applied && state->element && state->element->bound && state->element->bound->background &&
        state->element->bound->background->image == state->sampled_image) {
        CssAnimatedProp* underlying = find_underlying_prop(state, CSS_PROPERTY_BACKGROUND_IMAGE);
        if (underlying) {
            ViewTree* tree = state->element->doc ? state->element->doc->view_tree.get() : nullptr;
            state->element->bound->background->image = underlying->value.image
                ? pool_strdup(tree ? tree->prop_pool.get() : state->pool, underlying->value.image) : nullptr;
        }
    }
    FilterProp* filter = state->element ? state->element->filter_prop() : nullptr;
    if (state->filter_applied && filter && filter->functions_borrowed &&
        filter->functions.get() == state->sampled_filter.get()) {
        ViewTree* tree = state->element->doc ? state->element->doc->view_tree.get() : nullptr;
        filter->functions = lam::own(radiant::clone_filter_list(tree ? tree->prop_pool.get() : state->pool, state->underlying_filter));
        filter->functions_borrowed = false;
    }
    radiant::destroy_filter_list(state->pool, state->sampled_filter); state->sampled_filter = nullptr;
    radiant::destroy_filter_list(state->pool, state->underlying_filter); state->underlying_filter = nullptr;
    for (int i = 0; i < state->underlying_count; i++) {
        if (state->underlying[i].value_type == ANIM_VAL_IMAGE && state->underlying[i].value.image)
            pool_free(state->pool, state->underlying[i].value.image);
    }
    if (state->underlying) pool_free(state->pool, state->underlying);
    css_animation_restore_underlying_transform(state);
    for (int slot = 0; slot < 4; slot++) {
        radiant::destroy_transform_list(state->pool, state->sampled_transform[slot]);
        radiant::destroy_transform_list(state->pool, state->underlying_transform[slot]);
    }
    css_animation_clear_value_samples(state);
    if (state->custom_underlying_pool) mem_pool_destroy(state->custom_underlying_pool);
    if (state->custom_tick_pool) mem_pool_destroy(state->custom_tick_pool);
    if (state->value_samples) pool_free(state->pool, state->value_samples);
    if (state->important_properties) pool_free(state->pool, state->important_properties);
    pool_free(state->pool, state);
    anim->state = nullptr;
}

// ============================================================================
// CSS Animation Creation
// ============================================================================

static void capture_animation_underlying(CssAnimState* state) {
    if (!state || !state->element || !state->keyframes) return;
    CssAnimatedProp* previous_image = find_underlying_prop(state, CSS_PROPERTY_BACKGROUND_IMAGE);
    char* retired_image = previous_image ? previous_image->value.image : nullptr;
    auto retired_filter = state->underlying_filter;
    state->underlying_filter = nullptr;
    state->underlying_count = 0;
    if (state->custom_underlying_pool) pool_reset(state->custom_underlying_pool);
    for (int i = 0; i < state->keyframes->stop_count; i++) {
        CssKeyframeStop* stop = &state->keyframes->stops[i];
        for (int j = 0; j < stop->property_count; j++) {
            CssAnimatedProp* key = &stop->properties[j];
            CssPropertyCode id = key->property_code;
            if (find_underlying_prop(state, id, key->custom_name)) continue;
            CssAnimatedProp captured = {};
            if (key->custom_name) {
                captured = *key;
                captured.expression = nullptr;
                if (!state->custom_underlying_pool) state->custom_underlying_pool = mem_pool_create(nullptr, MEM_ROLE_CSS, "css.animation.custom-base");
                if (!state->custom_underlying_pool) continue;
                captured.value.custom = css_compute_custom_property_base(state->custom_underlying_pool, state->element, key->custom_name);
            } else if (!capture_underlying_value(state->element, id, &captured)) continue;
            // reserve before cloning so allocation failure cannot orphan an owned snapshot.
            if (!lam::pool_grow_array(state->pool, &state->underlying,
                    &state->underlying_capacity, state->underlying_count + 1, 8)) {
                log_error("css-anim: underlying value cache allocation failed");
                if (retired_image) pool_free(state->pool, retired_image);
                radiant::destroy_filter_list(state->pool, retired_filter); retired_filter = nullptr;
                return;
            }
            if (captured.value_type == ANIM_VAL_IMAGE && captured.value.image)
                captured.value.image = pool_strdup(state->pool, captured.value.image);
            if (captured.value_type == ANIM_VAL_FILTER) {
                state->underlying_filter = lam::own(radiant::clone_filter_list(state->pool, captured.value.filter));
                captured.value.filter = state->underlying_filter;
            }
            if (captured.value_type == ANIM_VAL_TRANSFORM) {
                // D4.5.1v4: snapshots cannot borrow a relayout-owned function list.
                TransformFunction* copy = radiant::clone_transform_list(
                    state->pool, captured.value.transform);
                if (captured.value.transform && !copy) continue;
                int slot = animation_transform_slot(id);
                radiant::destroy_transform_list(state->pool, state->underlying_transform[slot]);
                state->underlying_transform[slot] = lam::own(copy);
                captured.value.transform = copy;
            }
            state->underlying[state->underlying_count++] = captured;
        }
    }
    if (retired_image) pool_free(state->pool, retired_image);
    radiant::destroy_filter_list(state->pool, retired_filter); retired_filter = nullptr;
}

AnimationInstance* css_animation_create(AnimationScheduler* scheduler,
                                        DomElement* element,
                                        CssAnimProp* anim_prop,
                                        CssKeyframes* keyframes,
                                        double now,
                                        Pool* pool) {
    if (!scheduler || !element || !anim_prop || !keyframes) return NULL;

    // allocate runtime state
    CssAnimState* state = (CssAnimState*)pool_calloc(pool, sizeof(CssAnimState));
    if (!state) return nullptr;
    state->keyframes = keyframes;
    state->element = element;
    state->pool = pool;
    state->event_iteration = -1;
    // Capture the cascade value once; a neutral keyframe must interpolate from
    // this value even after later ticks have overwritten the live BlockProp.
    capture_animation_underlying(state);

    AnimationInstance* inst = animation_instance_create(scheduler);
    if (!inst) {
        if (state->custom_underlying_pool) mem_pool_destroy(state->custom_underlying_pool);
        for (int i = 0; i < state->underlying_count; i++)
            if (state->underlying[i].value_type == ANIM_VAL_IMAGE && state->underlying[i].value.image)
                pool_free(pool, state->underlying[i].value.image);
        radiant::destroy_filter_list(pool, state->underlying_filter); state->underlying_filter = nullptr;
        if (state->underlying) pool_free(pool, state->underlying);
        for (int slot = 0; slot < 4; slot++) radiant::destroy_transform_list(pool, state->underlying_transform[slot]);
        pool_free(pool, state);
        return NULL;
    }

    inst->type = ANIM_CSS_ANIMATION;
    inst->retain_after_finish = true;
    inst->target = element;
    inst->state = state;
    css_animation_refresh_priority(state);
    inst->start_time = now;
    inst->sample_time = now;
    inst->duration = anim_prop->duration;
    inst->delay = anim_prop->delay;
    inst->iteration_count = anim_prop->iteration_count;
    inst->direction = anim_prop->direction;
    inst->fill_mode = anim_prop->fill_mode;
    inst->play_state = (anim_prop->play_state == ANIM_PLAY_PAUSED)
                       ? ANIM_PLAY_PAUSED : ANIM_PLAY_RUNNING;
    inst->pause_time = now;
    inst->timing = anim_prop->timing;
    inst->tick = css_animation_tick;
    inst->on_finish = css_animation_finish;
    inst->on_cancel = css_animation_cancel;

    // set bounds from element's layout (absolute coordinates for dirty-region marking)
    View* span = static_cast<View*>(element);
    animation_update_layout_bounds(inst, span);

    animation_scheduler_add(scheduler, inst);


    log_debug("css-anim: created animation '%s' for <%s> (duration=%.3fs delay=%.3fs iterations=%g)",
              keyframes->name, element->tag_name ? element->tag_name : "?",
              anim_prop->duration, anim_prop->delay, anim_prop->iteration_count);

    return inst;
}

static void css_web_animation_request_sampling(CssWebAnimationState* state) {
    DomElement* element = state ? state->element : nullptr;
    if (!element) return;
    // effect changes require computed style without fabricating a DOM mutation.
    dom_invalidate_layout_subtree(element);
    if (element->doc) doc_state_request_reflow(element->doc->state);
}

CssWebAnimationState* css_web_animation_create(DomElement* element,
                                                CssKeyframes* keyframes,
                                                double duration_ms,
                                                const TimingFunction* timing,
                                                Pool* pool) {
    if (!element || !keyframes || !pool) return NULL;

    CssWebAnimationState* state = (CssWebAnimationState*)pool_calloc(
        pool, sizeof(CssWebAnimationState));
    if (!state) return NULL;
    state->element = element;
    state->duration_ms = duration_ms >= 0.0 ? duration_ms : 0.0;
    state->current_time_ms = 0.0;
    state->current_time_resolved = true;
    if (timing) state->timing = *timing;
    else state->timing.type = TIMING_LINEAR;
    state->sample.keyframes = keyframes;
    state->sample.element = element;
    state->sample.pool = pool;
    state->sample.event_iteration = -1;
    state->sample.suppress_events = true;
    state->underlying_captured = false;

    state->next = (CssWebAnimationState*)element->web_animation_state();
    element->set_web_animation_state(state);
    css_web_animation_request_sampling(state);
    log_debug("web-anim: created effect for <%s> duration=%.1fms",
              element->tag_name ? element->tag_name : "?", state->duration_ms);
    return state;
}

void css_web_animation_set_current_time(CssWebAnimationState* state,
                                         double current_time_ms) {
    if (!state) return;
    if (!isfinite(current_time_ms) || current_time_ms < 0.0) {
        current_time_ms = 0.0;
    }
    if (state->current_time_resolved && state->current_time_ms == current_time_ms) return;
    state->current_time_ms = current_time_ms;
    state->current_time_resolved = true;
    css_web_animation_request_sampling(state);
}

void css_web_animation_cancel(CssWebAnimationState* state) {
    if (!state || !state->current_time_resolved) return;
    // unresolved time removes the effect; retain its document-owned state for later seeks.
    state->current_time_resolved = false;
    state->underlying_captured = false;
    // absent transform declarations leave the sampled borrow in the retained view.
    css_animation_restore_underlying_transform(&state->sample);
    css_web_animation_request_sampling(state);
}

void css_web_animation_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element) return;
    if (lycon && lycon->ui_context && lycon->ui_context->document &&
        lycon->ui_context->document->disable_css_animations) {
        return;
    }

    CssWebAnimationState* state =
        (CssWebAnimationState*)element->web_animation_state();
    while (state) {
        if (!state->current_time_resolved) {
            state = state->next;
            continue;
        }
        css_animation_refresh_priority(&state->sample);
        css_animation_refresh_value_samples(&state->sample, lycon);
        if (!state->underlying_captured) {
            capture_animation_underlying(&state->sample);
            state->underlying_captured = true;
        }

        float progress = state->duration_ms > 0.0
            ? (float)(state->current_time_ms / state->duration_ms) : 1.0f;
        float eased = timing_function_eval(&state->timing, progress);
        log_debug("web-anim: sample <%s> current=%.1fms progress=%.3f eased=%.3f underlying=%d",
                  element->tag_name ? element->tag_name : "?",
                  state->current_time_ms, progress, eased,
                  state->sample.underlying_count);
        AnimationInstance sample = {};
        sample.type = ANIM_CSS_ANIMATION;
        sample.target = element;
        sample.state = &state->sample;
        sample.duration = state->duration_ms / 1000.0;
        sample.current_iteration = 0;
        sample.play_state = ANIM_PLAY_RUNNING;
        sample.timing.type = TIMING_LINEAR;
        css_animation_tick(&sample, eased);
        if (lycon && element->blk) {
            // block layout consumes the context snapshot, so copy the sampled
            // effect after applying it to the element's resolved BlockProp.
            lycon->block.given_width = element->block()->given_width;
            lycon->block.given_height = element->block()->given_height;
        }
        state = state->next;
    }
}

// ============================================================================
// Style Resolution Integration
// ============================================================================

// Parse a timing function from a CssValue (keyword or cubic-bezier function)
static void parse_timing_function_value(const CssValue* value, TimingFunction* out) {
    if (!css_value_is_timing_function(value)) {
        *out = TIMING_EASE;
        return;
    }
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        switch (value->data.keyword) {
            case CSS_VALUE_EASE:        *out = TIMING_EASE; return;
            case CSS_VALUE_EASE_IN:     *out = TIMING_EASE_IN; return;
            case CSS_VALUE_EASE_OUT:    *out = TIMING_EASE_OUT; return;
            case CSS_VALUE_EASE_IN_OUT: *out = TIMING_EASE_IN_OUT; return;
            case CSS_VALUE_LINEAR:      out->type = TIMING_LINEAR; return;
            case CSS_VALUE_STEP_START:
                out->type = TIMING_STEPS;
                out->steps.count = 1;
                out->steps.position = STEP_JUMP_START;
                return;
            case CSS_VALUE_STEP_END:
                out->type = TIMING_STEPS;
                out->steps.count = 1;
                out->steps.position = STEP_JUMP_END;
                return;
            default:
                *out = TIMING_EASE; // default
                return;
        }
    } else if (value->type == CSS_VALUE_TYPE_FUNCTION && value->data.function) {
        CssFunction* func = value->data.function;
        if (str_ieq_cstr(func->name, "cubic-bezier")) {
            float x1 = (float)func->args[0]->data.number.value;
            float y1 = (float)func->args[1]->data.number.value;
            float x2 = (float)func->args[2]->data.number.value;
            float y2 = (float)func->args[3]->data.number.value;
            timing_cubic_bezier_init(out, x1, y1, x2, y2);
            return;
        } else if (str_ieq_cstr(func->name, "steps")) {
            out->type = TIMING_STEPS;
            out->steps.count = func->args[0]->data.number.value;
            out->steps.position = STEP_JUMP_END; // default
            if (func->arg_count >= 2) {
                const char* name = css_value_identifier_name(func->args[1]);
                static const struct { const char* name; StepPosition position; } positions[] = {
                    {"start", STEP_JUMP_START}, {"jump-start", STEP_JUMP_START},
                    {"end", STEP_JUMP_END}, {"jump-end", STEP_JUMP_END},
                    {"jump-both", STEP_JUMP_BOTH}, {"jump-none", STEP_JUMP_NONE}
                };
                for (const auto& position : positions)
                    if (name && str_ieq_cstr(name, position.name)) out->steps.position = position.position;
            }
            return;
        }
    }
    // default to ease
    *out = TIMING_EASE;
}

bool css_animation_parse_timing_function_text(const char* value, TimingFunction* out) {
    if (!value || !out) return false;
    Pool* scratch = pool_create();
    if (!scratch) return false;
    CssDeclaration* declaration = css_parse_property_declaration(
        "animation-timing-function", 25, value, strlen(value), scratch);
    bool valid = declaration && !declaration->important && css_value_is_timing_function(declaration->value);
    if (valid) parse_timing_function_value(declaration->value, out);
    pool_destroy(scratch);
    return valid;
}

struct CssAnimationKeywordOption {
    CssEnum keyword;
    int value;
};

static int css_animation_keyword(const CssValue* value,
                                  const CssAnimationKeywordOption* options,
                                  int count, int initial) {
    if (!value || value->type != CSS_VALUE_TYPE_KEYWORD) return initial;
    for (int i = 0; i < count; i++) {
        if (options[i].keyword == value->data.keyword) return options[i].value;
    }
    return initial;
}

static const CssValue* css_motion_normalize_value(Pool* pool, const CssValue* value,
    CssPropertyCode property) {
    bool time = property == CSS_PROPERTY_ANIMATION_DURATION || property == CSS_PROPERTY_ANIMATION_DELAY ||
        property == CSS_PROPERTY_TRANSITION_DURATION || property == CSS_PROPERTY_TRANSITION_DELAY;
    int count = css_value_comma_count(value);
    CssValue* output = nullptr;
    for (int i = 0; i < count; i++) {
        const CssValue* item = css_value_comma_at(value, i);
        bool name = property == CSS_PROPERTY_ANIMATION_NAME && item &&
            item->type == CSS_VALUE_TYPE_KEYWORD && item->data.keyword != CSS_VALUE_NONE;
        if (!item || (!name && (!time || item->data.length.unit != CSS_UNIT_MS))) continue;
        CssValue* normalized = (CssValue*)pool_alloc(pool, sizeof(CssValue));
        if (!normalized) return nullptr;
        *normalized = *item;
        if (name) {
            // a keyword in the name slot is a case-sensitive custom identifier.
            normalized->type = CSS_VALUE_TYPE_CUSTOM;
            normalized->data.custom_property.name = css_value_identifier_name(item);
            normalized->data.custom_property.fallback = nullptr;
        } else {
            normalized->data.length.value /= 1000.0;
            normalized->data.length.unit = CSS_UNIT_S;
        }
        if (value->type != CSS_VALUE_TYPE_LIST) return normalized;
        if (!output) {
            output = (CssValue*)pool_alloc(pool, sizeof(CssValue));
            CssValue** items = (CssValue**)pool_alloc(pool, count * sizeof(CssValue*));
            if (!output || !items) return nullptr;
            *output = *value;
            memcpy(items, value->data.list.values, count * sizeof(CssValue*));
            output->data.list.values = items;
        }
        output->data.list.values[i] = normalized;
    }
    return output ? output : value;
}

static constexpr size_t MOTION_ANIMATION_INITIAL_COUNT =
    CSS_PROPERTY_ANIMATION_PLAY_STATE - CSS_PROPERTY_ANIMATION_NAME + 1;
static constexpr size_t MOTION_INITIAL_COUNT = MOTION_ANIMATION_INITIAL_COUNT +
    CSS_PROPERTY_TRANSITION_DELAY - CSS_PROPERTY_TRANSITION_PROPERTY + 1;

struct CssMotionInitialCache : DomDocumentResourceData {
    Pool* pool;
    const CssValue* values[MOTION_INITIAL_COUNT];
};

static void css_motion_initial_cache_destroy(DomDocumentResourceData* resource) {
    auto* cache = static_cast<CssMotionInitialCache*>(resource);
    if (cache->pool) mem_pool_destroy(cache->pool);
    mem_free(cache);
}

static CssMotionInitialCache* css_motion_initial_cache(DomDocument* doc) {
    if (!doc) return nullptr;
    for (DomDocumentResource* resource = doc->resources; resource; resource = resource->next)
        if (resource->destroy == css_motion_initial_cache_destroy)
            return static_cast<CssMotionInitialCache*>(resource->data);
    auto* cache = static_cast<CssMotionInitialCache*>(
        mem_calloc(1, sizeof(CssMotionInitialCache), MEM_CAT_LAYOUT));
    if (!cache) return nullptr;
    cache->pool = mem_pool_create((MemContext*)doc->services.mem_ctx,
        MEM_ROLE_CSS, "css.motion.initial_values");
    if (!cache->pool || !dom_document_add_resource(doc, cache, css_motion_initial_cache_destroy)) {
        css_motion_initial_cache_destroy(cache);
        return nullptr;
    }
    return cache;
}

// Restyle reads motion lists once; shorthand projections, var() substitution and
// unit normalization allocate, so they must not land in retained view storage.
struct MotionScratchPool {
    Pool* pool = pool_create();
    ~MotionScratchPool() { if (pool) pool_destroy(pool); }
};

const CssValue* css_motion_computed_value(Pool* pool, DomElement* element,
    CssPropertyCode property) {
    if (!pool || (css_animation_longhand_index(property) < 0 &&
        css_transition_longhand_index(property) < 0)) return nullptr;
    DomDocument* doc = element ? element->doc : nullptr;
    while (element) {
        CssDeclaration* declaration = style_tree_get_declaration(element->specified_style, property);
        const CssValue* value = declaration
            ? css_resolve_element_var_value(pool, element, declaration->value) : nullptr;
        if (value && value->type == CSS_VALUE_TYPE_KEYWORD &&
            value->data.keyword == CSS_VALUE_INHERIT) {
            element = dom_parent_element(element);
            continue;
        }
        if (value && (declaration->property_code == CSS_PROPERTY_ANIMATION ||
            declaration->property_code == CSS_PROPERTY_TRANSITION)) {
            value = css_motion_shorthand_longhand(value, property, pool);
        }
        if (value && css_property_validate_value(property, value) &&
            !(value->type == CSS_VALUE_TYPE_KEYWORD &&
              css_enum_info(value->data.keyword) &&
              css_enum_info(value->data.keyword)->group == CSS_VALUE_GROUP_GLOBAL)) {
            return css_motion_normalize_value(pool, value, property);
        }
        break;
    }
    const CssProperty* metadata = css_property_get_by_code(property);
    if (!metadata) return nullptr;
    // Initial metadata is immutable. Re-parsing it into retained view storage on
    // every restyle otherwise accumulates four default transition graphs per element.
    CssMotionInitialCache* cache = css_motion_initial_cache(doc);
    size_t index = css_animation_longhand_index(property) >= 0
        ? (size_t)css_animation_longhand_index(property)
        : MOTION_ANIMATION_INITIAL_COUNT + (size_t)css_transition_longhand_index(property);
    if (cache && cache->values[index]) return cache->values[index];
    Pool* initial_pool = cache ? cache->pool : pool;
    CssDeclaration* initial = css_parse_property_declaration(metadata->name, strlen(metadata->name),
        metadata->initial_value, strlen(metadata->initial_value), initial_pool);
    const CssValue* value = initial ? initial->value : nullptr;
    if (cache) cache->values[index] = value;
    return value;
}

static const CssValue* css_animation_list_item(const CssValue* value, int index) {
    if (!value || value->type != CSS_VALUE_TYPE_LIST) return value;
    return value->data.list.count > 0 && value->data.list.values
        ? value->data.list.values[index % value->data.list.count] : nullptr;
}

static const char* css_animation_name(const CssValue* value) {
    if (!value || (value->type == CSS_VALUE_TYPE_KEYWORD &&
        value->data.keyword == CSS_VALUE_NONE)) return nullptr;
    return value->type == CSS_VALUE_TYPE_STRING ? value->data.string : css_math_token_name(value);
}

static bool css_keyframes_animates_property(CssKeyframes* keyframes, CssPropertyCode property) {
    for (int index = 0; keyframes && index < keyframes->stop_count; index++) {
        if (find_prop_in_stop(&keyframes->stops[index], property)) return true;
    }
    return false;
}

bool css_animation_needs_computed_sample(DomElement* element, CssPropertyCode property) {
    if (!element || !element->doc) return false;
    DomDocument* doc = element->doc;
    if (doc->disable_css_animations) return false;
    for (CssWebAnimationState* effect = element->web_animation_state(); effect; effect = effect->next) {
        if (effect->current_time_resolved &&
            css_keyframes_animates_property(effect->sample.keyframes, property)) return true;
    }
    DocState* state = (DocState*)doc->state;
    AnimationScheduler* scheduler = state ? state->animation_scheduler : nullptr;
    for (AnimationInstance* effect = scheduler ? scheduler->first : nullptr;
         effect; effect = effect->next) {
        if (effect->target != element || !effect->state) continue;
        if (effect->type == ANIM_CSS_ANIMATION && css_keyframes_animates_property(
                ((CssAnimState*)effect->state)->keyframes, property)) return true;
        if (effect->type == ANIM_CSS_TRANSITION &&
            ((CssTransitionState*)effect->state)->property_code == property) return true;
    }
    // first reads can precede effect creation; inspect the newly cascaded animation names.
    Pool* scratch = pool_create();
    if (!scratch) return false;
    const CssValue* names = css_motion_computed_value(scratch, element, CSS_PROPERTY_ANIMATION_NAME);
    int count = names ? names->type == CSS_VALUE_TYPE_LIST ? names->data.list.count : 1 : 0;
    bool needs_sample = false;
    for (int index = 0; index < count && !needs_sample; index++) {
        const char* name = css_animation_name(css_animation_list_item(names, index));
        if (!name) continue;
        if (!doc->services.keyframe_registry) {
            doc->services.keyframe_registry = keyframe_registry_create(doc, doc->document_pool);
        }
        needs_sample = css_keyframes_animates_property(
            keyframe_registry_find((KeyframeRegistry*)doc->services.keyframe_registry, name), property);
    }
    pool_destroy(scratch);
    return needs_sample;
}

void css_motion_sample_existing(DomElement* element, LayoutContext* lycon) {
    DomDocument* document = element ? element->doc.get() : nullptr;
    DocState* state = document ? (DocState*)document->state : nullptr;
    AnimationScheduler* scheduler = state ? state->animation_scheduler : nullptr;
    if (!scheduler || document->disable_css_animations) return;
    for (AnimationInstance* instance = scheduler->first; instance; instance = instance->next) {
        if (instance->target != element || instance->scheduler_removed) continue;
        if (instance->type == ANIM_CSS_ANIMATION && instance->state) {
            CssAnimState* sample = (CssAnimState*)instance->state;
            css_animation_refresh_value_samples(sample, lycon);
            bool suppressed = sample->suppress_events;
            sample->suppress_events = true;
            animation_instance_sample(instance, scheduler->current_time, true);
            sample->suppress_events = suppressed;
        } else if (instance->type == ANIM_CSS_TRANSITION) {
            animation_instance_sample(instance, scheduler->current_time, true);
        }
    }
    if (element->blk) {
        lycon->block.given_width = element->block()->given_width;
        lycon->block.given_height = element->block()->given_height;
    }
}

void css_animation_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element || !lycon || !lycon->ui_context) return;
    DomDocument* doc = lycon->ui_context->document;
    if (!doc || doc->disable_css_animations) return;
    DocState* state = (DocState*)doc->state;
    AnimationScheduler* scheduler = state ? state->animation_scheduler : nullptr;
    if (!scheduler) return;
    uint32_t sample_style_version = element->style_version;

    static const CssPropertyCode properties[] = {
        CSS_PROPERTY_ANIMATION_NAME, CSS_PROPERTY_ANIMATION_DURATION,
        CSS_PROPERTY_ANIMATION_TIMING_FUNCTION, CSS_PROPERTY_ANIMATION_DELAY,
        CSS_PROPERTY_ANIMATION_ITERATION_COUNT, CSS_PROPERTY_ANIMATION_DIRECTION,
        CSS_PROPERTY_ANIMATION_FILL_MODE, CSS_PROPERTY_ANIMATION_PLAY_STATE
    };
    MotionScratchPool scratch;
    if (!scratch.pool) return;
    const CssValue* values[8] = {};
    for (int i = 0; i < 8; i++) {
        values[i] = css_motion_computed_value(scratch.pool, element, properties[i]);
    }
    int count = values[0] ? (values[0]->type == CSS_VALUE_TYPE_LIST
        ? values[0]->data.list.count : 1) : 0;
    AnimationInstance** instances = count > 0 ? (AnimationInstance**)pool_calloc(
        scratch.pool, sizeof(AnimationInstance*) * count) : nullptr;
    if (count > 0 && !instances) return;
    for (AnimationInstance* instance = scheduler->first; instance; instance = instance->next) {
        if (instance->target == element && instance->type == ANIM_CSS_ANIMATION && instance->state) {
            ((CssAnimState*)instance->state)->matched = false;
        }
    }
    if (count && !doc->services.keyframe_registry) {
        doc->services.keyframe_registry = keyframe_registry_create(doc, doc->document_pool);
    }
    static const CssAnimationKeywordOption direction_options[] = {
        {CSS_VALUE_NORMAL, ANIM_DIR_NORMAL}, {CSS_VALUE_REVERSE, ANIM_DIR_REVERSE},
        {CSS_VALUE_ALTERNATE, ANIM_DIR_ALTERNATE},
        {CSS_VALUE_ALTERNATE_REVERSE, ANIM_DIR_ALTERNATE_REVERSE}
    };
    static const CssAnimationKeywordOption fill_options[] = {
        {CSS_VALUE_NONE, ANIM_FILL_NONE}, {CSS_VALUE_FORWARDS, ANIM_FILL_FORWARDS},
        {CSS_VALUE_BACKWARDS, ANIM_FILL_BACKWARDS}, {CSS_VALUE_BOTH, ANIM_FILL_BOTH}
    };
    double now = scheduler->current_time;
    // matching from the end preserves playback when repeated names are reordered.
    for (int index = count - 1; index >= 0; index--) {
        const char* name = css_animation_name(css_animation_list_item(values[0], index));
        if (!name) continue;
        CssKeyframes* keyframes = keyframe_registry_find(
            (KeyframeRegistry*)doc->services.keyframe_registry, name);
        if (!keyframes) continue;
        CssAnimProp config = {};
        config.name = lam::up(name);
        config.iteration_count = 1.0;
        config.direction = ANIM_DIR_NORMAL;
        config.fill_mode = ANIM_FILL_NONE;
        config.play_state = ANIM_PLAY_RUNNING;
        config.timing = TIMING_EASE;
        double seconds = 0.0;
        if (css_value_as_seconds(css_animation_list_item(values[1], index), &seconds)) {
            config.duration = seconds;
        }
        if (css_value_as_seconds(css_animation_list_item(values[3], index), &seconds)) {
            config.delay = seconds;
        }
        const CssValue* iterations = css_animation_list_item(values[4], index);
        if (iterations) config.iteration_count = iterations->type == CSS_VALUE_TYPE_NUMBER
            ? iterations->data.number.value : -1.0;
        config.direction = (AnimationDirection)css_animation_keyword(
            css_animation_list_item(values[5], index), direction_options, 4, ANIM_DIR_NORMAL);
        config.fill_mode = (AnimationFillMode)css_animation_keyword(
            css_animation_list_item(values[6], index), fill_options, 4, ANIM_FILL_NONE);
        const CssValue* play = css_animation_list_item(values[7], index);
        if (play && play->type == CSS_VALUE_TYPE_KEYWORD && play->data.keyword == CSS_VALUE_PAUSED) {
            config.play_state = ANIM_PLAY_PAUSED;
        }
        const CssValue* timing = css_animation_list_item(values[2], index);
        if (timing) parse_timing_function_value(timing, &config.timing);
        AnimationInstance* instance = scheduler->last;
        while (instance) {
            CssAnimState* sample = instance->target == element && instance->type == ANIM_CSS_ANIMATION
                ? (CssAnimState*)instance->state : nullptr;
            if (sample && !sample->matched && sample->keyframes &&
                strcmp(sample->keyframes->name, name) == 0) break;
            instance = instance->prev;
        }
        if (!instance) {
            instance = css_animation_create(scheduler, element, &config, keyframes,
                now, doc->document_pool);
        } else {
            // timing changes retain the original clock, including an expired effect.
            bool phase_changed = instance->duration != config.duration ||
                instance->delay != config.delay || instance->iteration_count != config.iteration_count;
            if (phase_changed && instance->play_state == ANIM_PLAY_FINISHED) {
                instance->play_state = ANIM_PLAY_RUNNING;
                instance->finish_notified = false;
            }
            instance->duration = config.duration;
            instance->delay = config.delay;
            instance->iteration_count = config.iteration_count;
            instance->direction = config.direction;
            instance->fill_mode = config.fill_mode;
            instance->timing = config.timing;
            if (config.play_state == ANIM_PLAY_PAUSED) {
                if (instance->play_state == ANIM_PLAY_FINISHED) {
                    instance->play_state = ANIM_PLAY_RUNNING;
                }
                animation_instance_pause(instance, now);
            }
            else animation_instance_resume(instance, now);
        }
        if (!instance || !instance->state) continue;
        CssAnimState* sample = (CssAnimState*)instance->state;
        sample->matched = true;
        sample->ui_context = lycon->ui_context;
        sample->keyframes = keyframes;
        css_animation_refresh_priority(sample);
        css_animation_refresh_value_samples(sample, lycon);
        capture_animation_underlying(sample);
        instances[index] = instance;
    }
    AnimationInstance* before = nullptr;
    for (AnimationInstance* instance = scheduler->first; instance; ) {
        AnimationInstance* next = instance->next;
        if (instance->target == element && instance->type == ANIM_CSS_ANIMATION &&
            instance->state && !((CssAnimState*)instance->state)->matched) {
            animation_scheduler_cancel(scheduler, instance);
        } else if (!before && instance->type == ANIM_CSS_TRANSITION) before = instance;
        instance = next;
    }
    // later list entries override earlier effects; transitions have higher priority.
    for (int index = 0; index < count; index++) {
        if (instances[index]) {
            animation_scheduler_move_before(scheduler, instances[index], before);
            animation_scheduler_tick(scheduler, now, nullptr, true, instances[index]);
        }
    }
    if (element->style_version != sample_style_version && lycon->doc && element->specified_style &&
        lycon->view == static_cast<View*>(element)) {
        // custom samples change computed var() consumers on this element too;
        // re-resolve before overlaying ordinary animation values in list order.
        resolve_css_styles(element, lycon);
        for (int index = 0; index < count; index++) {
            if (!instances[index]) continue;
            CssAnimState* sample = (CssAnimState*)instances[index]->state;
            css_animation_refresh_value_samples(sample, lycon);
            animation_scheduler_tick(scheduler, now, nullptr, true, instances[index]);
        }
    }
    if (element->blk) {
        lycon->block.given_width = element->block()->given_width;
        lycon->block.given_height = element->block()->given_height;
    }
}

// ============================================================================
// CSS Transitions
// ============================================================================
//
// A transition is a single from->to segment for one property. It reuses the
// exact keyframe-animation machinery: the tick builds a CssAnimatedProp and
// calls apply_animated_value. The "from" value is the used value applied on the
// previous style resolution (snapshotted per element); the "to" value is the
// used value just computed by resolve_css_styles. When the two differ and a
// transition-* declaration covers the property, an ANIM_CSS_TRANSITION instance
// is started interpolating from->to over duration/delay with the timing function.
//
// Scope: transitions use the same computed-value types as animations. Numeric
// scalar storage is shared by opacity and lengths; the value type keeps their
// interpolation/application semantics distinct.

// Read the current used value of a transitionable property from the element's
// view props (the symmetric read side of apply_animated_value). Returns false
// if the property is unsupported or its used value is not currently determinable.
static bool css_transition_read_used_value(DomElement* element,
                                           CssPropertyCode prop_id,
                                           CssAnimValueType* out_type,
                                           float* out_f, Color* out_color,
                                           float* out_ratio) {
    CssAnimatedProp used = {};
    if (!capture_underlying_value(element, prop_id, &used)) return false;
    switch (used.value_type) {
        case ANIM_VAL_LENGTH:
            if (used.value.length.keyword != CSS_VALUE__UNDEF) return false;
            *out_f = used.value.length.value;
            break;
        case ANIM_VAL_FLOAT: *out_f = used.value.f; break;
        case ANIM_VAL_COLOR: *out_color = used.value.color; break;
        case ANIM_VAL_ASPECT_RATIO:
            if (used.value.aspect_ratio.is_auto) return false;
            *out_ratio = used.value.aspect_ratio.value;
            break;
        default: return false;
    }
    *out_type = used.value_type;
    return true;
}

// Map a transition property to the same value type used by keyframes.
static CssAnimValueType css_transition_value_type_for(CssPropertyCode prop_id) {
    const CssPropertyRuntimeMetadata* metadata = css_property_runtime_metadata(prop_id);
    return metadata && metadata->transition_supported
        ? metadata->animation_type : ANIM_VAL_NONE;
}

static CssTransitionTrack* css_transition_track_for(CssTransitionElemState* es,
                                                    CssPropertyCode prop_id,
                                                    CssAnimValueType vt);

static bool css_transition_read_style_value(DomElement* element,
                                            CssPropertyCode prop_id,
                                            CssAnimValueType vt,
                                            CssTransitionValue* out) {
    if (!element || !out) return false;

    CssAnimValueType read_type;
    float used_f = 0.0f;
    float used_ratio = 0.0f;
    Color used_color; used_color.c = 0;
    if (css_transition_read_used_value(element, prop_id, &read_type,
                                       &used_f, &used_color, &used_ratio)) {
        if (read_type == ANIM_VAL_FLOAT) out->value.f = used_f;
        else if (read_type == ANIM_VAL_LENGTH) out->value.f = used_f;
        else if (read_type == ANIM_VAL_COLOR) out->value.color = used_color;
        else if (read_type == ANIM_VAL_ASPECT_RATIO) {
            out->value.aspect_ratio.value = used_ratio;
            out->value.aspect_ratio.is_auto = false;
        }
        return read_type == vt;
    }

    if (!element->specified_style || !element->doc) return false;
    CssDeclaration* declaration = dom_element_get_specified_value(element, prop_id);
    if (!declaration) return false;
    const char* serialized = css_serialize_declaration_value(
        declaration, element->doc->document_pool);
    if (!serialized) return false;

    CssAnimatedProp parsed = {};
    if (!parse_property_value(prop_id, serialized, &parsed,
                              element->doc->document_pool) || parsed.value_type != vt) {
        return false;
    }
    if (vt == ANIM_VAL_FLOAT) out->value.f = parsed.value.f;
    else if (vt == ANIM_VAL_LENGTH) {
        if (!isfinite(parsed.value.length.value)) return false;
        out->value.f = parsed.value.length.value;
    }
    else if (vt == ANIM_VAL_COLOR) out->value.color = parsed.value.color;
    else if (vt == ANIM_VAL_ASPECT_RATIO) {
        out->value.aspect_ratio.value = parsed.value.aspect_ratio.value;
        out->value.aspect_ratio.is_auto = parsed.value.aspect_ratio.is_auto;
    }
    return true;
}

void css_transition_capture_before_change(DomElement* element, CssPropertyCode prop_id) {
    if (!element || !element->doc) return;

    CssAnimValueType vt = css_transition_value_type_for(prop_id);
    if (vt == ANIM_VAL_NONE) return;

    CssTransitionProp transition = {};
    Pool* scratch = pool_create();
    if (!scratch) return;
    CssTransitionList list;
    css_transition_resolve_config(element, scratch, &list);
    bool has_transition = css_transition_select_config(&list, prop_id, &transition);
    if (!has_transition) {
        radiant_cascade_styles_for_element(element);
        css_transition_resolve_config(element, scratch, &list);
        has_transition = css_transition_select_config(&list, prop_id, &transition);
    }
    pool_destroy(scratch);
    // Capture only after a transition is configured; the preceding inline write
    // is commonly the author-supplied `from` value, not a style change to animate.
    if (!has_transition) return;

    CssTransitionElemState* es = (CssTransitionElemState*)element->transition_state_prop();
    if (!es) {
        es = (CssTransitionElemState*)pool_calloc(
            element->doc->document_pool, sizeof(CssTransitionElemState));
        if (!es) return;
        es->pool = element->doc->document_pool;
        element->set_transition_state_prop(es);
    }
    CssTransitionTrack* track = css_transition_track_for(es, prop_id, vt);
    if (!track || track->has_snapshot || track->has_pending_from) return;

    CssTransitionValue before = {};
    if (css_transition_read_style_value(element, prop_id, vt, &before)) {
        track->pending_from = before;
        track->has_pending_from = true;
    }
}

void css_transition_tick(AnimationInstance* anim, float t) {
    CssTransitionState* st = (CssTransitionState*)anim->state;
    if (!st || !st->element) return;

    CssAnimatedProp interp = {};
    interp.property_code = st->property_code;
    interp.value_type = st->value_type;
    interp.composite = CSS_ANIM_COMPOSITE_REPLACE;

    // On the final tick (play_state flipped to FINISHED by the scheduler), snap
    // exactly to the target so no rounding residue is left behind.
    bool finished = (anim->play_state == ANIM_PLAY_FINISHED);

    switch (st->value_type) {
        case ANIM_VAL_FLOAT:
            interp.value.f = finished ? st->to.value.f
                                      : css_interpolate_float(st->from.value.f, st->to.value.f, t);
            break;
        case ANIM_VAL_LENGTH:
            interp.value.length.value = finished ? st->to.value.f
                : css_interpolate_float(st->from.value.f, st->to.value.f, t);
            interp.value.length.is_percent = false;
            break;
        case ANIM_VAL_COLOR:
            interp.value.color = finished ? st->to.value.color
                                          : css_interpolate_color(st->from.value.color, st->to.value.color, t);
            break;
        case ANIM_VAL_ASPECT_RATIO:
            interp.value.aspect_ratio.value = finished
                ? st->to.value.aspect_ratio.value
                : css_interpolate_aspect_ratio(
                    st->from.value.aspect_ratio.value, st->to.value.aspect_ratio.value, t);
            interp.value.aspect_ratio.is_auto = false;
            break;
        default:
            return; // unsupported — nothing to apply
    }

    anim->layout_changed |= apply_animated_value(st->element, &interp);

    // update bounds from element's current absolute layout position for dirty-region marking
    View* span = static_cast<View*>(anim->target);
    animation_update_layout_bounds(anim, span);
}

// Locate (or lazily append) the track for a property in the element's persistent state.
static CssTransitionTrack* css_transition_track_for(CssTransitionElemState* es,
                                                    CssPropertyCode prop_id,
                                                    CssAnimValueType vt) {
    for (int i = 0; i < es->track_count; i++) {
        if (es->tracks[i].property_code == prop_id) return &es->tracks[i];
    }
    // tracks grow with the supported property set; instances retain only property IDs.
    if (!lam::pool_grow_array(es->pool, &es->tracks, &es->track_capacity,
            es->track_count + 1, 4)) return nullptr;
    CssTransitionTrack* tk = &es->tracks[es->track_count++];
    *tk = {};
    tk->property_code = prop_id;
    tk->value_type = vt;
    tk->has_snapshot = false;
    tk->has_pending_from = false;
    return tk;
}

void css_transition_finish(AnimationInstance* anim) {
    CssTransitionState* st = (CssTransitionState*)anim->state;
    if (!st || !st->element) return;
    // Snap the element's persistent snapshot to the target so a subsequent style
    // change interpolates from the true end value. We locate the track fresh (no
    // raw back-pointer is kept, to stay safe across view-pool relayouts).
    CssTransitionElemState* es = (CssTransitionElemState*)st->element->transition_state_prop();
    if (es) {
        for (int i = 0; i < es->track_count; i++) {
            if (es->tracks[i].property_code == st->property_code) {
                es->tracks[i].value_type = st->value_type;
                es->tracks[i].has_snapshot = true;
                if (st->value_type == ANIM_VAL_FLOAT) es->tracks[i].snapshot.value.f = st->to.value.f;
                else if (st->value_type == ANIM_VAL_LENGTH) es->tracks[i].snapshot.value.f = st->to.value.f;
                else if (st->value_type == ANIM_VAL_COLOR) es->tracks[i].snapshot.value.color = st->to.value.color;
                else if (st->value_type == ANIM_VAL_ASPECT_RATIO) {
                    es->tracks[i].snapshot.value.aspect_ratio.value =
                        st->to.value.aspect_ratio.value;
                    es->tracks[i].snapshot.value.aspect_ratio.is_auto =
                        st->to.value.aspect_ratio.is_auto;
                }
                break;
            }
        }
    }
    radiant_dispatch_css_event(st->ui_context, st->element,
        "transitionend", "propertyName",
        css_property_spelling_from_code(st->property_code), anim->duration);
    log_debug("css-transition: finished prop=%d for element %p", st->property_code, st->element);
    // the final sample is committed; a completed transition must not own future changes.
    anim->fill_mode = ANIM_FILL_NONE;
    pool_free(st->pool, st);
    anim->state = nullptr;
}

static void css_transition_cancel(AnimationInstance* anim) {
    CssTransitionState* st = (CssTransitionState*)anim->state;
    if (!st || !st->element) return;
    // Document teardown invalidates DOM wrappers before scheduler entries are
    // reclaimed; no transitioncancel event can target that retired document.
    if (anim->suppress_cancel_event) {
        pool_free(st->pool, st);
        anim->state = nullptr;
        return;
    }
    double now = anim->start_time;
    if (st->ui_context && st->ui_context->document) {
        DocState* doc_state = (DocState*)st->ui_context->document->state;
        if (doc_state && doc_state->animation_scheduler) {
            now = doc_state->animation_scheduler->current_time;
        }
    }
    // elapsedTime excludes transition-delay and cannot exceed the active duration.
    double elapsed = now - anim->start_time - anim->delay;
    if (elapsed < 0.0) elapsed = 0.0;
    if (elapsed > anim->duration) elapsed = anim->duration;
    radiant_dispatch_css_event(st->ui_context, st->element,
        "transitioncancel", "propertyName",
        css_property_spelling_from_code(st->property_code), elapsed);
    pool_free(st->pool, st);
    anim->state = nullptr;
}

// Find a live transition instance for (element, property) in the scheduler, or NULL.
// Scanning the authoritative list avoids dangling back-pointers across relayouts.
static AnimationInstance* css_transition_find_running(AnimationScheduler* scheduler,
                                                      DomElement* element,
                                                      CssPropertyCode prop_id) {
    for (AnimationInstance* a = scheduler->first; a; a = a->next) {
        if (a->type == ANIM_CSS_TRANSITION && a->target == element) {
            CssTransitionState* s = (CssTransitionState*)a->state;
            if (s && s->property_code == prop_id) return a;
        }
    }
    return NULL;
}

// computed lists retain authored positions, including unknown property names.
void css_transition_resolve_config(DomElement* element, Pool* pool, CssTransitionList* list) {
    if (!list) return;
    for (int i = 0; i < 4; i++) list->values[i] = css_motion_computed_value(pool, element,
        (CssPropertyCode)(CSS_PROPERTY_TRANSITION_PROPERTY + i));
}

bool css_transition_select_config(const CssTransitionList* list, CssPropertyCode property,
    CssTransitionProp* transition) {
    if (!list || !transition) return false;
    int count = css_value_comma_count(list->values[0]);
    // the last matching entry wins; shorter timing lists repeat without deduplication.
    for (int index = count - 1; index >= 0; index--) {
        const CssValue* item = css_value_comma_at(list->values[0], index);
        const char* name = css_value_identifier_name(item);
        bool all = item && item->type == CSS_VALUE_TYPE_KEYWORD && item->data.keyword == CSS_VALUE_ALL;
        CssPropertyCode named_property = name ? (CssPropertyCode)css_property_code_from_name(name)
            : CSS_PROPERTY_UNKNOWN;
        if (!all && named_property != property &&
            !css_property_shorthand_contains(named_property, property)) continue;
        const CssValue* duration = css_value_comma_at(list->values[1], index);
        const CssValue* delay = css_value_comma_at(list->values[3], index);
        transition->duration = 0.0;
        if (duration) css_value_as_seconds(duration, &transition->duration);
        transition->delay = 0.0;
        if (delay) css_value_as_seconds(delay, &transition->delay);
        transition->timing = TIMING_EASE;
        const CssValue* timing = css_value_comma_at(list->values[2], index);
        if (timing) parse_timing_function_value(timing, &transition->timing);
        return transition->duration + transition->delay > 0.0f;
    }
    return false;
}

static void css_transition_start(AnimationScheduler* scheduler, DomElement* element,
                                 CssTransitionTrack* track, const CssTransitionProp* tp,
                                 CssAnimValueType vt, float from_f, Color from_c,
                                 float from_ratio, float to_f, Color to_c,
                                 float to_ratio, double now, Pool* pool,
                                 UiContext* ui_context) {
    // If a transition for this property is already running, reverse/interrupt from
    // its current interpolated value: cancel the old one and start fresh so we don't
    // stack instances. The current applied used value IS the interpolated value.
    AnimationInstance* existing = css_transition_find_running(scheduler, element, track->property_code);
    if (existing) {
        CssAnimValueType cvt; float cf = 0; float cr = 0; Color cc; cc.c = 0;
        if (css_transition_read_used_value(element, track->property_code,
                                           &cvt, &cf, &cc, &cr)) {
            if (cvt == ANIM_VAL_FLOAT || cvt == ANIM_VAL_LENGTH) from_f = cf;
            else if (cvt == ANIM_VAL_COLOR) from_c = cc;
            else if (cvt == ANIM_VAL_ASPECT_RATIO) from_ratio = cr;
        }
        animation_scheduler_cancel(scheduler, existing);
    }

    CssTransitionState* st = (CssTransitionState*)pool_calloc(pool, sizeof(CssTransitionState));
    if (!st) return;
    st->element = element;
    st->ui_context = ui_context;
    st->pool = pool;
    st->property_code = track->property_code;
    st->value_type = vt;
    if (vt == ANIM_VAL_FLOAT || vt == ANIM_VAL_LENGTH) {
        st->from.value.f = from_f;
        st->to.value.f = to_f;
    }
    else if (vt == ANIM_VAL_COLOR) { st->from.value.color = from_c; st->to.value.color = to_c; }
    else {
        st->from.value.aspect_ratio.value = from_ratio;
        st->from.value.aspect_ratio.is_auto = false;
        st->to.value.aspect_ratio.value = to_ratio;
        st->to.value.aspect_ratio.is_auto = false;
    }

    AnimationInstance* inst = animation_instance_create(scheduler);
    if (!inst) {
        pool_free(pool, st);
        return;
    }
    inst->type = ANIM_CSS_TRANSITION;
    inst->target = element;
    inst->state = st;
    inst->start_time = now;
    inst->duration = tp->duration;
    inst->delay = tp->delay;
    inst->iteration_count = 1;
    inst->direction = ANIM_DIR_NORMAL;
    // hold the end value after completion so the transitioned property does not
    // snap back before the next style resolution re-applies it.
    inst->fill_mode = ANIM_FILL_BOTH;
    inst->play_state = ANIM_PLAY_RUNNING;
    inst->timing = tp->timing;
    inst->tick = css_transition_tick;
    inst->on_finish = css_transition_finish;
    inst->on_cancel = css_transition_cancel;

    animation_update_layout_bounds(inst, static_cast<View*>(element));

    animation_scheduler_add(scheduler, inst);

    log_debug("css-transition: started prop=%d for <%s> (dur=%.3fs delay=%.3fs)",
              track->property_code, element->tag_name ? element->tag_name : "?",
              tp->duration, tp->delay);
}

void css_transition_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element || !lycon || !lycon->ui_context) return;
    if (lycon->ui_context->document &&
        lycon->ui_context->document->disable_css_animations) {
        return;
    }

    StyleTree* style_tree = element->specified_style;
    if (!style_tree || !style_tree->tree) return;

    DomDocument* doc = lycon->ui_context->document;
    if (!doc) return;
    DocState* rs = (DocState*)doc->state;
    if (!rs || !rs->animation_scheduler) return;
    AnimationScheduler* scheduler = rs->animation_scheduler;
    Pool* pool = doc->document_pool;

    // Resolve the transition config. Even if no transition is declared we still
    // maintain the used-value snapshot below (so a later declaration starts from
    // a correct "from"), but we only START transitions when duration > 0.
    MotionScratchPool scratch;
    if (!scratch.pool) return;
    CssTransitionList list;
    css_transition_resolve_config(element, scratch.pool, &list);

    // Lazily allocate the persistent per-element transition state (survives the
    // view-pool relayout because it lives in the doc pool, not the view pool).
    CssTransitionElemState* es = (CssTransitionElemState*)element->transition_state_prop();
    if (!es) {
        es = (CssTransitionElemState*)pool_calloc(pool, sizeof(CssTransitionElemState));
        if (!es) return;
        es->track_count = 0;
        es->pool = pool;
        element->set_transition_state_prop(es);
    }

    double now = scheduler->current_time;

    // Walk the supported property set. For each: read the new used value, compare
    // to the snapshot; if changed and covered by a transition declaration (with
    // a positive combined duration), start an interpolating instance. Always update the
    // snapshot to the new used value.
    for (size_t i = 0; i < css_property_runtime_metadata_count(); i++) {
        const CssPropertyRuntimeMetadata* metadata = css_property_runtime_metadata_at(i);
        if (!metadata || !metadata->transition_supported) continue;
        CssPropertyCode prop_id = metadata->property;
        CssAnimValueType vt = metadata->animation_type;

        CssAnimValueType read_vt; float new_f = 0.0f; float new_ratio = 0.0f;
        Color new_c; new_c.c = 0;
        if (!css_transition_read_used_value(element, prop_id, &read_vt,
                                            &new_f, &new_c, &new_ratio)) {
            continue; // used value not determinable this pass — skip
        }

        CssTransitionTrack* track = css_transition_track_for(es, prop_id, vt);
        if (!track) continue;

        // compare with the active target before sampling overwrites the new cascade.
        CssTransitionProp timing = {};
        bool covered = css_transition_select_config(&list, prop_id, &timing);
        AnimationInstance* running = css_transition_find_running(scheduler, element, prop_id);
        if (running && !covered) {
            animation_scheduler_cancel(scheduler, running);
            running = nullptr;
        }
        if (running) {
            CssTransitionState* active = (CssTransitionState*)running->state;
            bool same_target = (vt == ANIM_VAL_FLOAT || vt == ANIM_VAL_LENGTH)
                ? fabsf(active->to.value.f - new_f) <= .0001f
                : vt == ANIM_VAL_COLOR ? active->to.value.color.c == new_c.c
                : fabsf(active->to.value.aspect_ratio.value - new_ratio) <= .0001f;
            // Retained relayout resets view properties, not the DOM-owned
            // transition. Preserve the new target before restoring the old sample.
            animation_scheduler_tick(scheduler, now, nullptr, false, running);
            if (same_target) {
                if (element->blk) {
                    lycon->block.given_width = element->block()->given_width;
                    lycon->block.given_height = element->block()->given_height;
                }
                continue;
            }
        }

        bool changed = false;
        float from_f = new_f; float from_ratio = new_ratio;
        Color from_c = new_c;
        if (track->has_snapshot) {
            if (vt == ANIM_VAL_FLOAT || vt == ANIM_VAL_LENGTH) {
                from_f = track->snapshot.value.f;
                changed = (fabsf(track->snapshot.value.f - new_f) > 0.0001f);
            } else if (vt == ANIM_VAL_COLOR) {
                from_c = track->snapshot.value.color;
                changed = (track->snapshot.value.color.c != new_c.c);
            } else if (vt == ANIM_VAL_ASPECT_RATIO) {
                from_ratio = track->snapshot.value.aspect_ratio.value;
                changed = fabsf(from_ratio - new_ratio) > 0.0001f;
            }
        } else if (track->has_pending_from) {
            if (vt == ANIM_VAL_FLOAT || vt == ANIM_VAL_LENGTH) {
                from_f = track->pending_from.value.f;
                changed = fabsf(from_f - new_f) > 0.0001f;
            } else if (vt == ANIM_VAL_COLOR) {
                from_c = track->pending_from.value.color;
                changed = (from_c.c != new_c.c);
            } else if (vt == ANIM_VAL_ASPECT_RATIO) {
                from_ratio = track->pending_from.value.aspect_ratio.value;
                changed = fabsf(from_ratio - new_ratio) > 0.0001f;
            }
        }

        if ((changed || running) && covered) {
            if (!track->has_snapshot && track->has_pending_from) {
                // Preserve the pre-change style when this script turn defers its
                // first layout until after the DOM mutation batch.
                track->has_snapshot = true;
                track->snapshot = track->pending_from;
            }
            css_transition_start(scheduler, element, track, &timing, vt,
                                 from_f, from_c, from_ratio, new_f, new_c,
                                 new_ratio, now, pool,
                                 lycon->ui_context);
            // Headless layout has no frame before serialization; apply the
            // transition's current time so its negative delay affects this pass.
            AnimationInstance* started = css_transition_find_running(scheduler, element, prop_id);
            if (started) animation_scheduler_tick(scheduler, now, nullptr, false, started);
            // The transition tick updates the persistent block, while block
            // layout consumes this pass's context copy; keep both at the same
            // sampled value or layout restores the cascaded target.
            if (element->blk) {
                lycon->block.given_width = element->block()->given_width;
                lycon->block.given_height = element->block()->given_height;
            }
            // snapshot stays at the OLD value until the instance finishes (finish
            // snaps it to `to`); do not overwrite here.
        } else {
            // no active transition — track the current used value as the baseline
            track->value_type = vt;
            track->has_snapshot = true;
            if (vt == ANIM_VAL_FLOAT || vt == ANIM_VAL_LENGTH) track->snapshot.value.f = new_f;
            else if (vt == ANIM_VAL_COLOR) track->snapshot.value.color = new_c;
            else if (vt == ANIM_VAL_ASPECT_RATIO) {
                track->snapshot.value.aspect_ratio.value = new_ratio;
                track->snapshot.value.aspect_ratio.is_auto = false;
            }
        }
        track->has_pending_from = false;
    }
}
