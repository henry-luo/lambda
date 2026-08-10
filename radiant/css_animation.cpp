#include "view.hpp"
#include "layout.hpp"
#include "event.hpp"

extern "C" {
#include "../lib/log.h"
#include "../lib/str.h"
#include "../lib/color.h"
#include "../lib/avl_tree.h"
}

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/input/css/css_formatter.hpp"
#include "../lib/tagged.hpp"
#include "../lib/mem_grow.hpp"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

// ============================================================================
// Property Interpolation
// ============================================================================

float css_interpolate_float(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)(v + 0.5f);
}

Color css_interpolate_color(Color a, Color b, float t) {
    Color result;
    result.r = lerp_u8(a.r, b.r, t);
    result.g = lerp_u8(a.g, b.g, t);
    result.b = lerp_u8(a.b, b.b, t);
    result.a = lerp_u8(a.a, b.a, t);
    return result;
}

// ============================================================================
// Keyframe Content Parsing
// ============================================================================

// Skip whitespace in a string
static const char* skip_ws(const char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

// Parse a float number from string, advance pointer
static float parse_float(const char** s) {
    char* end;
    float val = strtof(*s, &end);
    *s = end;
    return val;
}

// Parse a CSS color value from string (supports: named colors, #hex, rgb())
static bool parse_color_value(const char* val, Color* out) {
    val = skip_ws(val);

    // hex color — count the hex-digit run (tolerates trailing content) and let
    // lib/color.h handle the digit-count expansion.
    if (val[0] == '#') {
        const char* p = val + 1;
        int len = 0;
        while (isxdigit((unsigned char)p[len])) len++;
        if (len == 3 || len == 4 || len == 6 || len == 8) {
            char tmp[9];
            memcpy(tmp, p, (size_t)len);
            tmp[len] = '\0';
            uint8_t r, g, b, a;
            if (color_parse_hex(tmp, &r, &g, &b, &a)) {
                out->r = r; out->g = g; out->b = b; out->a = a;
                return true;
            }
        }
        return false;
    }

    // rgb(r, g, b) or rgba(r, g, b, a)
    if (strncmp(val, "rgb", 3) == 0) {
        const char* p = val + 3;
        if (*p == 'a') p++;
        if (*p != '(') return false;
        p++;
        out->r = (uint8_t)strtol(p, (char**)&p, 10); while (*p == ',' || isspace((unsigned char)*p)) p++;
        out->g = (uint8_t)strtol(p, (char**)&p, 10); while (*p == ',' || isspace((unsigned char)*p)) p++;
        out->b = (uint8_t)strtol(p, (char**)&p, 10); while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (*p == ')') { out->a = 255; return true; }
        float a = strtof(p, (char**)&p);
        out->a = (uint8_t)(a * 255.0f + 0.5f);
        return true;
    }

    // named colors (common ones for animation)
    CssEnum enum_id = css_enum_by_name(val);
    if (enum_id != CSS_VALUE__UNDEF) {
        uint8_t r, g, b, a;
        if (css_named_color_to_rgba(enum_id, &r, &g, &b, &a)) {
            out->r = r; out->g = g; out->b = b; out->a = a;
            return true;
        }
    }

    // transparent
    if (strncasecmp(val, "transparent", 11) == 0) {
        out->r = out->g = out->b = out->a = 0;
        return true;
    }

    return false;
}

// Determine the animation value type for a property
static CssAnimValueType property_value_type(CssPropertyCode id) {
    switch (id) {
        case CSS_PROPERTY_OPACITY:
            return ANIM_VAL_FLOAT;
        case CSS_PROPERTY_TRANSFORM:
            return ANIM_VAL_TRANSFORM;
        case CSS_PROPERTY_BACKGROUND_COLOR:
        case CSS_PROPERTY_COLOR:
        case CSS_PROPERTY_BORDER_TOP_COLOR:
        case CSS_PROPERTY_BORDER_RIGHT_COLOR:
        case CSS_PROPERTY_BORDER_BOTTOM_COLOR:
        case CSS_PROPERTY_BORDER_LEFT_COLOR:
            return ANIM_VAL_COLOR;
        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT:
        case CSS_PROPERTY_TOP:
        case CSS_PROPERTY_RIGHT:
        case CSS_PROPERTY_BOTTOM:
        case CSS_PROPERTY_LEFT:
        case CSS_PROPERTY_MARGIN_TOP:
        case CSS_PROPERTY_MARGIN_RIGHT:
        case CSS_PROPERTY_MARGIN_BOTTOM:
        case CSS_PROPERTY_MARGIN_LEFT:
        case CSS_PROPERTY_PADDING_TOP:
        case CSS_PROPERTY_PADDING_RIGHT:
        case CSS_PROPERTY_PADDING_BOTTOM:
        case CSS_PROPERTY_PADDING_LEFT:
        case CSS_PROPERTY_BORDER_TOP_WIDTH:
        case CSS_PROPERTY_BORDER_RIGHT_WIDTH:
        case CSS_PROPERTY_BORDER_BOTTOM_WIDTH:
        case CSS_PROPERTY_BORDER_LEFT_WIDTH:
            return ANIM_VAL_LENGTH;
        case CSS_PROPERTY_ASPECT_RATIO:
            return ANIM_VAL_ASPECT_RATIO;
        default:
            return ANIM_VAL_NONE;
    }
}

// Parse a single transform function from string (e.g., "translateX(20px)")
// Returns a TransformFunction allocated from pool, or NULL
static TransformFunction* parse_transform_func(const char** s, Pool* pool) {
    const char* p = skip_ws(*s);

    // find function name
    const char* name_start = p;
    while (*p && *p != '(' && !isspace((unsigned char)*p)) p++;
    if (*p != '(') return NULL;

    size_t name_len = p - name_start;
    p++; // skip '('

    TransformFunction* tf = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
    if (!tf) return NULL;
    tf->translate_x_percent = NAN;
    tf->translate_y_percent = NAN;

    if (name_len == 10 && strncmp(name_start, "translateX", 10) == 0) {
        tf->type = TRANSFORM_TRANSLATEX;
        tf->params.translate.x = parse_float(&p);
        // skip unit
        while (*p && *p != ')') p++;
    } else if (name_len == 10 && strncmp(name_start, "translateY", 10) == 0) {
        tf->type = TRANSFORM_TRANSLATEY;
        tf->params.translate.y = parse_float(&p);
        while (*p && *p != ')') p++;
    } else if (name_len == 9 && strncmp(name_start, "translate", 9) == 0) {
        tf->type = TRANSFORM_TRANSLATE;
        tf->params.translate.x = parse_float(&p);
        while (*p && *p != ',' && *p != ')') p++;
        if (*p == ',') { p++; tf->params.translate.y = parse_float(&p); }
        while (*p && *p != ')') p++;
    } else if (name_len == 6 && strncmp(name_start, "scaleX", 6) == 0) {
        tf->type = TRANSFORM_SCALEX;
        tf->params.scale.x = parse_float(&p);
        while (*p && *p != ')') p++;
    } else if (name_len == 6 && strncmp(name_start, "scaleY", 6) == 0) {
        tf->type = TRANSFORM_SCALEY;
        tf->params.scale.y = parse_float(&p);
        while (*p && *p != ')') p++;
    } else if (name_len == 5 && strncmp(name_start, "scale", 5) == 0) {
        tf->type = TRANSFORM_SCALE;
        tf->params.scale.x = parse_float(&p);
        while (*p && *p != ',' && *p != ')') p++;
        if (*p == ',') { p++; tf->params.scale.y = parse_float(&p); }
        else { tf->params.scale.y = tf->params.scale.x; }
        while (*p && *p != ')') p++;
    } else if (name_len == 6 && strncmp(name_start, "rotate", 6) == 0) {
        tf->type = TRANSFORM_ROTATE;
        float angle = parse_float(&p);
        // convert deg to radians if needed
        while (isalpha((unsigned char)*p)) p++; // skip unit like "deg"
        tf->params.angle = angle;
        while (*p && *p != ')') p++;
    } else if (name_len == 5 && strncmp(name_start, "skewX", 5) == 0) {
        tf->type = TRANSFORM_SKEWX;
        tf->params.skew.x = parse_float(&p);
        while (*p && *p != ')') p++;
    } else if (name_len == 5 && strncmp(name_start, "skewY", 5) == 0) {
        tf->type = TRANSFORM_SKEWY;
        tf->params.skew.y = parse_float(&p);
        while (*p && *p != ')') p++;
    } else {
        // unsupported transform function — skip
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
        *s = p;
        return NULL;
    }

    if (*p == ')') p++;
    *s = p;
    return tf;
}

// Parse transform value string into linked list of TransformFunction
static TransformFunction* parse_transform_value(const char* val, Pool* pool) {
    TransformFunction* head = NULL;
    TransformFunction* tail = NULL;
    const char* p = skip_ws(val);

    while (*p) {
        p = skip_ws(p);
        if (!*p) break;

        TransformFunction* tf = parse_transform_func(&p, pool);
        if (tf) {
            tf->next = NULL;
            if (tail) { tail->next = tf; tail = tf; }
            else { head = tail = tf; }
        } else {
            // skip unknown token
            while (*p && !isspace((unsigned char)*p)) p++;
        }
    }
    return head;
}

static bool parse_aspect_ratio_value(const char* val, CssAnimatedProp* out) {
    if (!val || !out) return false;

    const char* p = skip_ws(val);
    bool is_auto = strncasecmp(p, "auto", 4) == 0 &&
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
    out->property_code = prop_id;
    out->value_type = property_value_type(prop_id);
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;

    switch (out->value_type) {
        case ANIM_VAL_FLOAT: {
            out->value.f = strtof(val, NULL);
            return true;
        }
        case ANIM_VAL_COLOR: {
            return parse_color_value(val, &out->value.color);
        }
        case ANIM_VAL_LENGTH: {
            char* end;
            out->value.length.value = strtof(val, &end);
            out->value.length.is_percent = (*end == '%');
            return true;
        }
        case ANIM_VAL_ASPECT_RATIO:
            return parse_aspect_ratio_value(val, out);
        case ANIM_VAL_TRANSFORM: {
            out->value.transform = parse_transform_value(val, pool);
            return out->value.transform != NULL;
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

static CssAnimComposite parse_animation_composition(const char* value) {
    if (!value) return CSS_ANIM_COMPOSITE_REPLACE;
    if (strcasecmp(value, "add") == 0) return CSS_ANIM_COMPOSITE_ADD;
    if (strcasecmp(value, "accumulate") == 0) return CSS_ANIM_COMPOSITE_ACCUMULATE;
    return CSS_ANIM_COMPOSITE_REPLACE;
}

// Parse the content of a @keyframes rule into structured CssKeyframes
// Content format: "animName { from { prop: val; } 50% { prop: val; } to { prop: val; } }"
static CssKeyframes* parse_keyframes_content(const char* content, Pool* pool) {
    if (!content || !pool) return NULL;

    const char* p = skip_ws(content);

    // extract animation name (everything before first '{')
    const char* name_start = p;
    while (*p && *p != '{') p++;
    if (!*p) return NULL;

    // trim trailing whitespace from name
    const char* name_end = p;
    while (name_end > name_start && isspace((unsigned char)*(name_end - 1))) name_end--;
    size_t name_len = name_end - name_start;
    if (name_len == 0) return NULL;

    char* name = (char*)pool_alloc(pool, name_len + 1);
    memcpy(name, name_start, name_len);
    name[name_len] = '\0';

    p++; // skip outer '{'

    // parse keyframe stops — temporary storage
    CssKeyframeStop temp_stops[64];
    int stop_count = 0;

    // temporary property storage per stop
    CssAnimatedProp temp_props[32];

    while (*p && stop_count < 64) {
        p = skip_ws(p);
        if (*p == '}') break; // end of @keyframes

        // parse keyframe selector: "from", "to", or "N%"
        float offset = -1.0f;
        if (strncmp(p, "from", 4) == 0 && !isalnum((unsigned char)p[4])) {
            offset = 0.0f;
            p += 4;
        } else if (strncmp(p, "to", 2) == 0 && !isalnum((unsigned char)p[2])) {
            offset = 1.0f;
            p += 2;
        } else if (isdigit((unsigned char)*p) || *p == '.') {
            offset = strtof(p, (char**)&p) / 100.0f;
            if (*p == '%') p++;
        } else {
            // skip unknown content
            while (*p && *p != '{') p++;
            if (*p == '{') {
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            }
            continue;
        }

        if (offset < 0.0f || offset > 1.0f) {
            // invalid offset, skip this stop
            while (*p && *p != '{') p++;
            if (*p == '{') {
                int depth = 1;
                p++;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            }
            continue;
        }

        p = skip_ws(p);
        if (*p != '{') continue;
        p++; // skip '{'

        // parse declarations inside keyframe stop
        int prop_count = 0;
        CssAnimComposite stop_composite = CSS_ANIM_COMPOSITE_REPLACE;

        while (*p && *p != '}' && prop_count < 32) {
            p = skip_ws(p);
            if (*p == '}') break;

            // parse property name
            const char* prop_start = p;
            while (*p && *p != ':' && *p != '}') p++;
            if (*p != ':') break;

            const char* prop_end = p;
            while (prop_end > prop_start && isspace((unsigned char)*(prop_end - 1))) prop_end--;

            char prop_name[64];
            size_t plen = prop_end - prop_start;
            if (plen >= sizeof(prop_name)) plen = sizeof(prop_name) - 1;
            memcpy(prop_name, prop_start, plen);
            prop_name[plen] = '\0';

            p++; // skip ':'
            p = skip_ws(p);

            // parse property value (up to ';' or '}')
            const char* val_start = p;
            // handle nested parens (for transform functions, rgb(), etc.)
            int paren_depth = 0;
            while (*p && (paren_depth > 0 || (*p != ';' && *p != '}'))) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') paren_depth--;
                p++;
            }
            const char* val_end = p;
            while (val_end > val_start && isspace((unsigned char)*(val_end - 1))) val_end--;

            char val_buf[256];
            size_t vlen = val_end - val_start;
            if (vlen >= sizeof(val_buf)) vlen = sizeof(val_buf) - 1;
            memcpy(val_buf, val_start, vlen);
            val_buf[vlen] = '\0';

            if (*p == ';') p++;

            if (strcasecmp(prop_name, "animation-composition") == 0) {
                stop_composite = parse_animation_composition(val_buf);
                continue;
            }

            // resolve property and parse value
            CssPropertyCode prop_id = (CssPropertyCode)css_property_code_from_name(prop_name);
            if (prop_id != (CssPropertyCode)0) {
                if (parse_property_value(prop_id, val_buf, &temp_props[prop_count], pool)) {
                    prop_count++;
                }
            }
        }

        if (*p == '}') p++; // skip closing brace of keyframe stop

        if (prop_count > 0) {
            for (int i = 0; i < prop_count; i++) {
                temp_props[i].composite = stop_composite;
            }
            CssKeyframeStop* stop = &temp_stops[stop_count];
            stop->offset = offset;
            stop->timing = NULL;
            stop->property_count = prop_count;
            stop->properties = (CssAnimatedProp*)pool_alloc(pool, sizeof(CssAnimatedProp) * prop_count);
            memcpy(stop->properties, temp_props, sizeof(CssAnimatedProp) * prop_count);
            stop_count++;
        }
    }

    if (stop_count == 0) {
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
    kf->name = name;
    kf->stop_count = stop_count;
    kf->stops = (CssKeyframeStop*)pool_alloc(pool, sizeof(CssKeyframeStop) * stop_count);
    memcpy(kf->stops, temp_stops, sizeof(CssKeyframeStop) * stop_count);

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
    registry->pool = pool;
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
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->entries[i]->name, name) == 0) {
            return registry->entries[i];
        }
    }
    return NULL;
}

// ============================================================================
// CSS Animation Tick
// ============================================================================

// Find two surrounding keyframe stops for progress t, and compute local interpolation t
static void find_keyframe_pair(CssKeyframes* kf, float t,
                                int* out_stop_a, int* out_stop_b, float* out_local_t) {
    // Preserve extrapolated easing progress when the keyframes provide both
    // endpoints; clamping here discards valid CSS animation overshoot.
    if (t <= kf->stops[0].offset) {
        if (kf->stop_count > 1 && kf->stops[0].offset <= 0.0f &&
            kf->stops[1].offset > kf->stops[0].offset) {
            *out_stop_a = 0;
            *out_stop_b = 1;
            *out_local_t = (t - kf->stops[0].offset) /
                (kf->stops[1].offset - kf->stops[0].offset);
        } else {
            *out_stop_a = 0;
            *out_stop_b = 0;
            *out_local_t = 0.0f;
        }
        return;
    }
    if (t >= kf->stops[kf->stop_count - 1].offset) {
        int last = kf->stop_count - 1;
        if (last > 0 && kf->stops[last].offset >= 1.0f &&
            kf->stops[last].offset > kf->stops[last - 1].offset) {
            *out_stop_a = last - 1;
            *out_stop_b = last;
            *out_local_t = (t - kf->stops[last - 1].offset) /
                (kf->stops[last].offset - kf->stops[last - 1].offset);
        } else {
            *out_stop_a = last;
            *out_stop_b = last;
            *out_local_t = 1.0f;
        }
        return;
    }

    // find surrounding stops
    for (int i = 0; i < kf->stop_count - 1; i++) {
        if (t >= kf->stops[i].offset && t <= kf->stops[i + 1].offset) {
            *out_stop_a = i;
            *out_stop_b = i + 1;
            float range = kf->stops[i + 1].offset - kf->stops[i].offset;
            *out_local_t = (range > 0.0001f) ? (t - kf->stops[i].offset) / range : 0.0f;
            return;
        }
    }

    // fallback: use last stop
    *out_stop_a = kf->stop_count - 1;
    *out_stop_b = kf->stop_count - 1;
    *out_local_t = 1.0f;
}

// Find a property in a keyframe stop by ID
static CssAnimatedProp* find_prop_in_stop(CssKeyframeStop* stop, CssPropertyCode id) {
    for (int i = 0; i < stop->property_count; i++) {
        if (stop->properties[i].property_code == id) {
            return &stop->properties[i];
        }
    }
    return NULL;
}

static CssAnimatedProp* find_underlying_prop(CssAnimState* state, CssPropertyCode id) {
    if (!state) return NULL;
    for (int i = 0; i < state->underlying_count; i++) {
        if (state->underlying[i].property_code == id) return &state->underlying[i];
    }
    return NULL;
}

static bool capture_underlying_length(DomElement* element, CssPropertyCode id,
                                      CssAnimatedProp* out) {
    if (!element || !out || !element->blk) return false;
    const BlockProp* block = element->block();
    float value = 0.0f;
    switch (id) {
        case CSS_PROPERTY_WIDTH: value = block->given_width; break;
        case CSS_PROPERTY_HEIGHT: value = block->given_height; break;
        case CSS_PROPERTY_MIN_WIDTH: value = block->given_min_width; break;
        case CSS_PROPERTY_MAX_WIDTH: value = block->given_max_width; break;
        case CSS_PROPERTY_MIN_HEIGHT: value = block->given_min_height; break;
        case CSS_PROPERTY_MAX_HEIGHT: value = block->given_max_height; break;
        default: return false;
    }
    if (value < 0.0f) return false;
    out->property_code = id;
    out->value_type = ANIM_VAL_LENGTH;
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    out->value.length.value = value;
    out->value.length.is_percent = false;
    return true;
}

static bool capture_underlying_aspect_ratio(DomElement* element,
                                            CssAnimatedProp* out) {
    if (!element || !out) return false;
    float ratio = element->fi ? element->fi->aspect_ratio : 0.0f;
    if (ratio <= 0.0f || !isfinite(ratio)) return false;

    out->property_code = CSS_PROPERTY_ASPECT_RATIO;
    out->value_type = ANIM_VAL_ASPECT_RATIO;
    out->composite = CSS_ANIM_COMPOSITE_REPLACE;
    out->value.aspect_ratio.value = ratio;
    out->value.aspect_ratio.is_auto = false;
    return true;
}

static void animation_update_layout_bounds(AnimationInstance* animation, View* target) {
    float x = target->x;
    float y = target->y;
    for (ViewElement* parent = target->parent_view(); parent; parent = parent->parent_view()) {
        x += parent->x;
        y += parent->y;
    }
    animation->bounds[0] = x;
    animation->bounds[1] = y;
    animation->bounds[2] = target->width;
    animation->bounds[3] = target->height;
}

// Interpolate a single transform function pair
static TransformFunction* interpolate_transform_func(TransformFunction* a, TransformFunction* b,
                                                       float t, Pool* pool) {
    if (!a && !b) return NULL;

    TransformFunction* result = (TransformFunction*)pool_calloc(pool, sizeof(TransformFunction));
    result->translate_x_percent = NAN;
    result->translate_y_percent = NAN;

    if (a && b && a->type == b->type) {
        result->type = a->type;
        switch (a->type) {
            case TRANSFORM_TRANSLATE:
            case TRANSFORM_TRANSLATEX:
            case TRANSFORM_TRANSLATEY:
                result->params.translate.x = css_interpolate_float(a->params.translate.x, b->params.translate.x, t);
                result->params.translate.y = css_interpolate_float(a->params.translate.y, b->params.translate.y, t);
                break;
            case TRANSFORM_SCALE:
            case TRANSFORM_SCALEX:
            case TRANSFORM_SCALEY:
                result->params.scale.x = css_interpolate_float(a->params.scale.x, b->params.scale.x, t);
                result->params.scale.y = css_interpolate_float(a->params.scale.y, b->params.scale.y, t);
                break;
            case TRANSFORM_ROTATE:
                result->params.angle = css_interpolate_float(a->params.angle, b->params.angle, t);
                break;
            case TRANSFORM_SKEW:
            case TRANSFORM_SKEWX:
            case TRANSFORM_SKEWY:
                result->params.skew.x = css_interpolate_float(a->params.skew.x, b->params.skew.x, t);
                result->params.skew.y = css_interpolate_float(a->params.skew.y, b->params.skew.y, t);
                break;
            default:
                // unsupported — use 'a' value
                *result = *a;
                break;
        }
    } else if (a) {
        *result = *a;
    } else {
        *result = *b;
    }
    return result;
}

// Interpolate transform function lists
static TransformFunction* interpolate_transform_list(TransformFunction* a, TransformFunction* b,
                                                       float t, Pool* pool) {
    TransformFunction* head = NULL;
    TransformFunction* tail = NULL;

    while (a || b) {
        TransformFunction* interp = interpolate_transform_func(a, b, t, pool);
        if (interp) {
            interp->next = NULL;
            if (tail) { tail->next = interp; tail = interp; }
            else { head = tail = interp; }
        }
        if (a) a = a->next;
        if (b) b = b->next;
    }
    return head;
}

// Lazily ensure InlineProp exists on the span (needed for opacity/color animation
// when the element has no static opacity/color declaration)
static InlineProp* ensure_inline_prop(ViewSpan* span) {
    if (!span->in_line) {
        DomElement* el = lam::dom_require_element(span);
        if (el->doc && el->doc->view_tree) span->ensure_inline(el->doc->view_tree);
    }
    return span->in_line;
}

// Lazily ensure BoundaryProp + BackgroundProp exist (needed for background-color
// animation when the element has no static background declaration)
static BackgroundProp* ensure_background_prop(ViewSpan* span) {
    DomElement* el = lam::dom_require_element(span);
    Pool* pool = (el->doc && el->doc->view_tree) ? el->doc->view_tree->prop_pool : NULL;
    if (!pool) return NULL;
    if (!span->bound) span->ensure_boundary(el->doc->view_tree);
    if (span->bound && !span->boundary()->background) {
        span->bound->background = (BackgroundProp*)pool_calloc(pool, sizeof(BackgroundProp));
    }
    return span->bound ? span->boundary()->background : NULL;
}

// Apply an interpolated property value to a DomElement
static void apply_animated_value(DomElement* element, CssAnimatedProp* prop) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));

    switch (prop->property_code) {
        case CSS_PROPERTY_OPACITY: {
            InlineProp* il = ensure_inline_prop(span);
            if (il) il->opacity = prop->value.f;
            break;
        }
        case CSS_PROPERTY_TRANSFORM: {
            if (!span->transform) {
                if (element->doc && element->doc->view_tree)
                    span->ensure_transform(element->doc->view_tree);
            }
            if (span->transform) {
                span->transform->functions = prop->value.transform;
            }
            break;
        }
        case CSS_PROPERTY_BACKGROUND_COLOR: {
            BackgroundProp* bg = ensure_background_prop(span);
            if (bg) bg->color = prop->value.color;
            break;
        }
        case CSS_PROPERTY_COLOR: {
            InlineProp* il = ensure_inline_prop(span);
            if (il) {
                il->color = prop->value.color;
                il->has_color = true;
            }
            break;
        }
        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT: {
            ViewBlock* block = lam::view_as_block(static_cast<View*>(element));
            if (!block || !block->blk) break;
            float value = prop->value.length.value;
            // animated sizing writes the computed value back into the same
            // BlockProp consumed by layout; leaving it in the cascade only
            // changes paint-time state and cannot affect descendants.
            switch (prop->property_code) {
                case CSS_PROPERTY_WIDTH:
                    block->blk->given_width = value;
                    block->blk->given_width_type = CSS_VALUE__UNDEF;
                    block->blk->given_width_percent = NAN;
                    break;
                case CSS_PROPERTY_HEIGHT:
                    block->blk->given_height = value;
                    block->blk->given_height_type = CSS_VALUE__UNDEF;
                    block->blk->given_height_percent = NAN;
                    break;
                case CSS_PROPERTY_MIN_WIDTH: block->blk->given_min_width = value; break;
                case CSS_PROPERTY_MAX_WIDTH: block->blk->given_max_width = value; break;
                case CSS_PROPERTY_MIN_HEIGHT: block->blk->given_min_height = value; break;
                case CSS_PROPERTY_MAX_HEIGHT: block->blk->given_max_height = value; break;
                default: break;
            }
            break;
        }
        case CSS_PROPERTY_ASPECT_RATIO: {
            ViewBlock* block = lam::view_as_block(static_cast<View*>(element));
            if (!block || !block->fi) break;
            // Layout reads the resolved ratio from the flex-item property; the
            // keyframe value must update that same used-value source each tick.
            block->fi->aspect_ratio = prop->value.aspect_ratio.is_auto
                ? 0.0f : prop->value.aspect_ratio.value;
            break;
        }
        default:
            break;
    }
}

static float css_animation_composite_length(CssAnimState* state,
                                            CssAnimatedProp* prop,
                                            float value) {
    if (!state || !prop || prop->composite == CSS_ANIM_COMPOSITE_REPLACE) {
        return value;
    }
    CssAnimatedProp* underlying = find_underlying_prop(state, prop->property_code);
    if (!underlying) return value;

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

static float css_animation_composite_aspect_ratio(CssAnimState* state,
                                                   CssAnimatedProp* prop,
                                                   float value) {
    if (!state || !prop || prop->composite == CSS_ANIM_COMPOSITE_REPLACE) {
        return value;
    }
    CssAnimatedProp* underlying = find_underlying_prop(
        state, prop->property_code);
    // aspect-ratio has no addition operation; non-replace composition uses the
    // underlying value as the second value in the composite operation.
    return underlying && underlying->value_type == ANIM_VAL_ASPECT_RATIO
        ? underlying->value.aspect_ratio.value : value;
}

void css_animation_tick(AnimationInstance* anim, float t) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (!state || !state->keyframes || !state->element) return;

    if (!state->suppress_events && !state->event_started) {
        state->event_started = true;
        state->event_iteration = anim->current_iteration;
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationstart", "animationName", state->keyframes->name, 0.0);
    } else if (!state->suppress_events &&
               anim->current_iteration > state->event_iteration) {
        state->event_iteration = anim->current_iteration;
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationiteration", "animationName", state->keyframes->name,
            anim->duration * (double)anim->current_iteration);
    }

    CssKeyframes* kf = state->keyframes;

    int stop_a, stop_b;
    float local_t;
    find_keyframe_pair(kf, t, &stop_a, &stop_b, &local_t);

    CssKeyframeStop* sa = &kf->stops[stop_a];
    CssKeyframeStop* sb = &kf->stops[stop_b];

    // apply per-keyframe easing if present
    if (sa->timing) {
        local_t = timing_function_eval(sa->timing, local_t);
    }

    Pool* pool = anim->play_state != ANIM_PLAY_FINISHED ?
                 state->element->doc->document_pool : NULL;

    // interpolate each property present in either stop
    // use stop_b's properties as the canonical set
    for (int i = 0; i < sb->property_count; i++) {
        CssAnimatedProp* prop_b = &sb->properties[i];
        CssAnimatedProp* prop_a = find_prop_in_stop(sa, prop_b->property_code);

        CssAnimatedProp interp;
        interp.property_code = prop_b->property_code;
        interp.value_type = prop_b->value_type;
        interp.composite = prop_b->composite;

        if (stop_a == stop_b || !prop_a) {
            // same stop or property only in one stop — use directly
            if (kf->stop_count == 1 && prop_b->value_type == ANIM_VAL_LENGTH) {
                CssAnimatedProp* underlying = find_underlying_prop(state, prop_b->property_code);
                if (underlying) {
                    // CSS animation interpolation extrapolates outside the keyframe interval;
                    // the property-specific used-value rules clamp the resulting size later.
                    float progress = t;
                    bool starts_at_to = sb->offset >= 1.0f;
                    float from = starts_at_to ? underlying->value.length.value : prop_b->value.length.value;
                    float to = starts_at_to ? prop_b->value.length.value : underlying->value.length.value;
                    interp.value.length.value = css_interpolate_float(from, to, progress);
                    interp.value.length.is_percent = prop_b->value.length.is_percent;
                } else {
                    interp.value = prop_b->value;
                }
            } else if (kf->stop_count == 1 &&
                       prop_b->value_type == ANIM_VAL_ASPECT_RATIO) {
                CssAnimatedProp* underlying = find_underlying_prop(
                    state, prop_b->property_code);
                if (underlying && !prop_b->value.aspect_ratio.is_auto) {
                    interp.value.aspect_ratio.value = css_interpolate_aspect_ratio(
                        underlying->value.aspect_ratio.value,
                        css_animation_composite_aspect_ratio(
                            state, prop_b, prop_b->value.aspect_ratio.value), t);
                    interp.value.aspect_ratio.is_auto = false;
                } else {
                    interp.value = prop_b->value;
                }
            } else {
                interp.value = prop_b->value;
            }
        } else {
            switch (prop_b->value_type) {
                case ANIM_VAL_FLOAT:
                    interp.value.f = css_interpolate_float(
                        prop_a->value.f, prop_b->value.f, local_t);
                    break;
                case ANIM_VAL_COLOR:
                    interp.value.color = css_interpolate_color(
                        prop_a->value.color, prop_b->value.color, local_t);
                    break;
                case ANIM_VAL_LENGTH:
                    {
                        CssAnimatedProp* underlying = find_underlying_prop(state, prop_b->property_code);
                        float from = prop_a ? prop_a->value.length.value :
                            (underlying ? underlying->value.length.value : prop_b->value.length.value);
                        float to = prop_b->value.length.value;
                        interp.value.length.value = css_interpolate_float(from, to, local_t);
                    }
                    interp.value.length.is_percent = prop_b->value.length.is_percent;
                    break;
                case ANIM_VAL_ASPECT_RATIO: {
                    bool discrete = prop_a->value.aspect_ratio.is_auto ||
                        prop_b->value.aspect_ratio.is_auto;
                    if (discrete) {
                        interp.value.aspect_ratio = local_t < 0.5f
                            ? prop_a->value.aspect_ratio
                            : prop_b->value.aspect_ratio;
                    } else {
                        float from = css_animation_composite_aspect_ratio(
                            state, prop_a, prop_a->value.aspect_ratio.value);
                        float to = css_animation_composite_aspect_ratio(
                            state, prop_b, prop_b->value.aspect_ratio.value);
                        interp.value.aspect_ratio.value =
                            css_interpolate_aspect_ratio(from, to, local_t);
                        interp.value.aspect_ratio.is_auto = false;
                    }
                    break;
                }
                case ANIM_VAL_TRANSFORM:
                    if (pool) {
                        interp.value.transform = interpolate_transform_list(
                            prop_a->value.transform, prop_b->value.transform, local_t, pool);
                    } else {
                        interp.value.transform = prop_b->value.transform;
                    }
                    break;
                default:
                    interp.value = prop_b->value;
                    break;
            }
        }

        if (interp.value_type == ANIM_VAL_LENGTH &&
            interp.composite != CSS_ANIM_COMPOSITE_REPLACE &&
            prop_a && prop_b) {
            interp.value.length.value = css_animation_composite_length(
                state, &interp, interp.value.length.value);
        }

        apply_animated_value(state->element, &interp);
    }

    // also apply properties only present in stop_a (but not in stop_b)
    for (int i = 0; i < sa->property_count; i++) {
        CssAnimatedProp* prop_a = &sa->properties[i];
        if (!find_prop_in_stop(sb, prop_a->property_code)) {
            CssAnimatedProp* underlying = find_underlying_prop(state, prop_a->property_code);
            if (underlying && prop_a->value_type == ANIM_VAL_LENGTH) {
                CssAnimatedProp interp = *prop_a;
                interp.value.length.value = css_interpolate_float(
                    prop_a->value.length.value,
                    underlying->value.length.value, local_t);
                interp.value.length.value = css_animation_composite_length(
                    state, &interp, interp.value.length.value);
                apply_animated_value(state->element, &interp);
            } else if (underlying &&
                       prop_a->value_type == ANIM_VAL_ASPECT_RATIO) {
                CssAnimatedProp interp = *prop_a;
                interp.value.aspect_ratio.value = css_interpolate_aspect_ratio(
                    prop_a->value.aspect_ratio.value,
                    underlying->value.aspect_ratio.value, local_t);
                interp.value.aspect_ratio.is_auto = false;
                apply_animated_value(state->element, &interp);
            } else {
                apply_animated_value(state->element, prop_a);
            }
        }
    }

    // update bounds from element's current layout position (may have been
    // zero at creation time because css_animation_create runs before layout)
    // use absolute coordinates (walk parent chain) for correct dirty-region marking
    View* span = static_cast<View*>(anim->target);
    animation_update_layout_bounds(anim, span);

    // offset bounds by transform displacement so dirty region covers the
    // element's actual visual position (not expanded to include both static
    // and transformed positions — the previous-bounds tracking in
    // animation_scheduler_tick handles the old position separately)
    ViewSpan* vs = lam::view_require_element(span);
    if (vs->transform && vs->transform->functions) {
        TransformFunction* tf = vs->transform->functions;
        while (tf) {
            if (tf->type == TRANSFORM_TRANSLATE || tf->type == TRANSFORM_TRANSLATEX ||
                tf->type == TRANSFORM_TRANSLATEY) {
                float tx = tf->params.translate.x;
                float ty = tf->params.translate.y;
                if (!isnan(tf->translate_x_percent))
                    tx = tf->translate_x_percent * span->width / 100.0f;
                if (!isnan(tf->translate_y_percent))
                    ty = tf->translate_y_percent * span->height / 100.0f;
                anim->bounds[0] += tx;
                anim->bounds[1] += ty;
            } else if (tf->type == TRANSFORM_SCALE || tf->type == TRANSFORM_SCALEX ||
                       tf->type == TRANSFORM_SCALEY || tf->type == TRANSFORM_ROTATE) {
                // scale/rotate can expand bounds — use generous margin
                float margin = span->width > span->height ? span->width : span->height;
                anim->bounds[0] -= margin * 0.5f;
                anim->bounds[1] -= margin * 0.5f;
                anim->bounds[2] += margin;
                anim->bounds[3] += margin;
            }
            tf = tf->next;
        }
    }
}

void css_animation_finish(AnimationInstance* anim) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (state) {
        double elapsed = anim->iteration_count > 0
            ? anim->duration * (double)anim->iteration_count : anim->duration;
        radiant_dispatch_css_event(state->ui_context, state->element,
            "animationend", "animationName",
            state->keyframes ? state->keyframes->name : "", elapsed);
        log_debug("css-anim: animation '%s' finished for element %p",
                  state->keyframes ? state->keyframes->name : "?", state->element);
    }
}

// ============================================================================
// CSS Animation Creation
// ============================================================================

static void capture_animation_underlying(CssAnimState* state) {
    if (!state || !state->element || !state->keyframes) return;
    state->underlying_count = 0;
    for (int i = 0; i < state->keyframes->stop_count &&
                    state->underlying_count < 32; i++) {
        CssKeyframeStop* stop = &state->keyframes->stops[i];
        for (int j = 0; j < stop->property_count &&
                        state->underlying_count < 32; j++) {
            CssPropertyCode id = stop->properties[j].property_code;
            if (find_underlying_prop(state, id)) continue;
            CssAnimatedProp* slot = &state->underlying[state->underlying_count];
            bool captured = id == CSS_PROPERTY_ASPECT_RATIO
                ? capture_underlying_aspect_ratio(state->element, slot)
                : capture_underlying_length(state->element, id, slot);
            if (captured) state->underlying_count++;
        }
    }
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
    state->keyframes = keyframes;
    state->element = element;
    state->event_iteration = -1;
    // Capture the cascade value once; a neutral keyframe must interpolate from
    // this value even after later ticks have overwritten the live BlockProp.
    capture_animation_underlying(state);

    AnimationInstance* inst = animation_instance_create(scheduler);
    if (!inst) return NULL;

    inst->type = ANIM_CSS_ANIMATION;
    inst->target = element;
    inst->state = state;
    inst->start_time = now;
    inst->duration = anim_prop->duration;
    inst->delay = anim_prop->delay;
    inst->iteration_count = anim_prop->iteration_count;
    inst->direction = anim_prop->direction;
    inst->fill_mode = anim_prop->fill_mode;
    inst->play_state = (anim_prop->play_state == ANIM_PLAY_PAUSED)
                       ? ANIM_PLAY_PAUSED : ANIM_PLAY_RUNNING;
    inst->timing = anim_prop->timing;
    inst->tick = css_animation_tick;
    inst->on_finish = css_animation_finish;

    // set bounds from element's layout (absolute coordinates for dirty-region marking)
    View* span = static_cast<View*>(element);
    animation_update_layout_bounds(inst, span);

    animation_scheduler_add(scheduler, inst);


    log_debug("css-anim: created animation '%s' for <%s> (duration=%.3fs delay=%.3fs iterations=%d)",
              keyframes->name, element->tag_name ? element->tag_name : "?",
              anim_prop->duration, anim_prop->delay, anim_prop->iteration_count);

    return inst;
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
    if (timing) state->timing = *timing;
    else state->timing.type = TIMING_LINEAR;
    state->sample.keyframes = keyframes;
    state->sample.element = element;
    state->sample.event_iteration = -1;
    state->sample.suppress_events = true;
    state->underlying_captured = false;

    state->next = (CssWebAnimationState*)element->web_animation_state();
    element->set_web_animation_state(state);
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
    state->current_time_ms = current_time_ms;
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
        if (func->name && strcmp(func->name, "cubic-bezier") == 0 && func->arg_count >= 4) {
            float x1 = (float)func->args[0]->data.number.value;
            float y1 = (float)func->args[1]->data.number.value;
            float x2 = (float)func->args[2]->data.number.value;
            float y2 = (float)func->args[3]->data.number.value;
            timing_cubic_bezier_init(out, x1, y1, x2, y2);
            return;
        } else if (func->name && strcmp(func->name, "steps") == 0 && func->arg_count >= 1) {
            out->type = TIMING_STEPS;
            out->steps.count = (int)func->args[0]->data.number.value;
            out->steps.position = STEP_JUMP_END; // default
            if (func->arg_count >= 2 && func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                // steps() uses the generic start/end keywords, which are not
                // the step-start/step-end preset names.
                if (func->args[1]->data.keyword == CSS_VALUE_STEP_START ||
                    func->args[1]->data.keyword == CSS_VALUE_START)
                    out->steps.position = STEP_JUMP_START;
            }
            return;
        }
    }
    // default to ease
    *out = TIMING_EASE;
}

bool css_animation_parse_timing_function_text(const char* value,
                                              TimingFunction* out) {
    if (!value || !out) return false;
    const char* p = skip_ws(value);
    if (strcasecmp(p, "linear") == 0) {
        out->type = TIMING_LINEAR;
        return true;
    }
    if (strcasecmp(p, "ease") == 0) {
        *out = TIMING_EASE;
        return true;
    }
    if (strcasecmp(p, "ease-in") == 0) {
        *out = TIMING_EASE_IN;
        return true;
    }
    if (strcasecmp(p, "ease-out") == 0) {
        *out = TIMING_EASE_OUT;
        return true;
    }
    if (strcasecmp(p, "ease-in-out") == 0) {
        *out = TIMING_EASE_IN_OUT;
        return true;
    }

    if (strncasecmp(p, "steps(", 6) == 0) {
        p += 6;
        char* end = nullptr;
        long count = strtol(p, &end, 10);
        if (end == p || count < 1) return false;
        p = skip_ws(end);
        if (*p == ',') p++;
        p = skip_ws(p);
        out->type = TIMING_STEPS;
        out->steps.count = (int)count;
        out->steps.position = STEP_JUMP_END;
        if (strncasecmp(p, "start", 5) == 0) {
            out->steps.position = STEP_JUMP_START;
        }
        return true;
    }

    if (strncasecmp(p, "cubic-bezier(", 13) == 0) {
        p += 13;
        float values[4];
        for (int i = 0; i < 4; i++) {
            p = skip_ws(p);
            if (*p == ',') p = skip_ws(p + 1);
            char* end = nullptr;
            values[i] = strtof(p, &end);
            if (end == p) return false;
            p = end;
        }
        timing_cubic_bezier_init(out, values[0], values[1], values[2], values[3]);
        return true;
    }
    return false;
}

void css_animation_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element || !lycon || !lycon->ui_context) return;
    if (lycon->ui_context->document &&
        lycon->ui_context->document->disable_css_animations) {
        return;
    }

    // check if element has animation-name set
    StyleTree* style_tree = element->specified_style;
    if (!style_tree || !style_tree->tree) return;

    // look up animation-name in the element's specified styles
    AvlNode* name_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_NAME);
    if (!name_node) return;

    StyleNode* style_node = (StyleNode*)name_node->declaration;
    CssDeclaration* decl = style_node ? style_node->winning_decl : NULL;
    if (!decl || !decl->value) return;

    const CssValue* value = decl->value;

    // extract animation name
    const char* anim_name = NULL;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_NONE) return; // animation-name: none
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        anim_name = info ? info->name : NULL;
    } else if (value->type == CSS_VALUE_TYPE_STRING) {
        anim_name = value->data.string;
    } else if (value->type == CSS_VALUE_TYPE_CUSTOM) {
        anim_name = value->data.custom_property.name;
    }

    if (!anim_name) return;

    // check if this element already has an animation running for this name
    DomDocument* doc = lycon->ui_context->document;
    if (!doc) return;

    DocState* rs = (DocState*)doc->state;
    if (!rs || !rs->animation_scheduler) return;

    // check if animation already running for this element
    AnimationScheduler* scheduler = rs->animation_scheduler;
    AnimationInstance* existing = scheduler->first;
    while (existing) {
        if (existing->target == element && existing->type == ANIM_CSS_ANIMATION) {
            CssAnimState* as = (CssAnimState*)existing->state;
            if (as && as->keyframes && strcmp(as->keyframes->name, anim_name) == 0) {
                return; // already running
            }
        }
        existing = existing->next;
    }

    // build keyframe registry if not yet built
    if (!doc->services.keyframe_registry) {
        doc->services.keyframe_registry = keyframe_registry_create(doc, doc->document_pool);
    }

    CssKeyframes* keyframes = keyframe_registry_find(
        (KeyframeRegistry*)doc->services.keyframe_registry, anim_name);
    if (!keyframes) {
        log_debug("css-anim: no @keyframes found for '%s'", anim_name);
        return;
    }

    // build CssAnimProp from resolved animation properties
    CssAnimProp anim_prop;
    memset(&anim_prop, 0, sizeof(anim_prop));
    anim_prop.name = anim_name;
    anim_prop.duration = 0.0f;
    anim_prop.delay = 0.0f;
    anim_prop.iteration_count = 1;
    anim_prop.direction = ANIM_DIR_NORMAL;
    anim_prop.fill_mode = ANIM_FILL_NONE;
    anim_prop.play_state = ANIM_PLAY_RUNNING;
    anim_prop.timing = TIMING_EASE;

    // resolve animation-duration
    AvlNode* dur_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_DURATION);
    if (dur_node) {
        StyleNode* sn = (StyleNode*)dur_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value && d->value->type == CSS_VALUE_TYPE_LENGTH) {
            float val = (float)d->value->data.length.value;
            if (d->value->data.length.unit == CSS_UNIT_MS) val /= 1000.0f;
            anim_prop.duration = val;
        }
    }

    // resolve animation-delay
    AvlNode* delay_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_DELAY);
    if (delay_node) {
        StyleNode* sn = (StyleNode*)delay_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value && d->value->type == CSS_VALUE_TYPE_LENGTH) {
            float val = (float)d->value->data.length.value;
            if (d->value->data.length.unit == CSS_UNIT_MS) val /= 1000.0f;
            anim_prop.delay = val;
        }
    }

    // resolve animation-iteration-count
    AvlNode* iter_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_ITERATION_COUNT);
    if (iter_node) {
        StyleNode* sn = (StyleNode*)iter_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value) {
            if (d->value->type == CSS_VALUE_TYPE_KEYWORD && d->value->data.keyword == CSS_VALUE_INFINITE) {
                anim_prop.iteration_count = -1;
            } else if (d->value->type == CSS_VALUE_TYPE_NUMBER) {
                anim_prop.iteration_count = (int)d->value->data.number.value;
                if (anim_prop.iteration_count < 1) anim_prop.iteration_count = 1;
            }
        }
    }

    // resolve animation-direction
    AvlNode* dir_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_DIRECTION);
    if (dir_node) {
        StyleNode* sn = (StyleNode*)dir_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value && d->value->type == CSS_VALUE_TYPE_KEYWORD) {
            switch (d->value->data.keyword) {
                case CSS_VALUE_NORMAL:             anim_prop.direction = ANIM_DIR_NORMAL; break;
                case CSS_VALUE_REVERSE:            anim_prop.direction = ANIM_DIR_REVERSE; break;
                case CSS_VALUE_ALTERNATE:          anim_prop.direction = ANIM_DIR_ALTERNATE; break;
                case CSS_VALUE_ALTERNATE_REVERSE:  anim_prop.direction = ANIM_DIR_ALTERNATE_REVERSE; break;
                default: break;
            }
        }
    }

    // resolve animation-fill-mode
    AvlNode* fill_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_FILL_MODE);
    if (fill_node) {
        StyleNode* sn = (StyleNode*)fill_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value && d->value->type == CSS_VALUE_TYPE_KEYWORD) {
            switch (d->value->data.keyword) {
                case CSS_VALUE_NONE:      anim_prop.fill_mode = ANIM_FILL_NONE; break;
                case CSS_VALUE_FORWARDS:  anim_prop.fill_mode = ANIM_FILL_FORWARDS; break;
                case CSS_VALUE_BACKWARDS: anim_prop.fill_mode = ANIM_FILL_BACKWARDS; break;
                case CSS_VALUE_BOTH:      anim_prop.fill_mode = ANIM_FILL_BOTH; break;
                default: break;
            }
        }
    }

    // resolve animation-play-state
    AvlNode* ps_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_PLAY_STATE);
    if (ps_node) {
        StyleNode* sn = (StyleNode*)ps_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value && d->value->type == CSS_VALUE_TYPE_KEYWORD) {
            if (d->value->data.keyword == CSS_VALUE_PAUSED)
                anim_prop.play_state = ANIM_PLAY_PAUSED;
            else
                anim_prop.play_state = ANIM_PLAY_RUNNING;
        }
    }

    // resolve animation-timing-function
    AvlNode* tf_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_ANIMATION_TIMING_FUNCTION);
    if (tf_node) {
        StyleNode* sn = (StyleNode*)tf_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        if (d && d->value) {
            parse_timing_function_value(d->value, &anim_prop.timing);
        }
    }

    // skip zero-duration, zero-iteration animations
    if (anim_prop.duration <= 0.0f && anim_prop.iteration_count != -1) {
        log_debug("css-anim: skipping zero-duration animation '%s'", anim_name);
        return;
    }

    // create the animation
    double now = scheduler->current_time;
    AnimationInstance* instance = css_animation_create(
        scheduler, element, &anim_prop, keyframes, now, doc->document_pool);
    if (instance && instance->state) {
        ((CssAnimState*)instance->state)->ui_context = lycon->ui_context;
        // CSS animation values participate in the first computed layout; the
        // headless layout command has no frame tick before it serializes boxes.
        animation_scheduler_tick(scheduler, now, NULL);
        if (element->blk) {
            lycon->block.given_width = element->block()->given_width;
            lycon->block.given_height = element->block()->given_height;
        }
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
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));
    switch (prop_id) {
        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT: {
            if (!span->blk) return false;
            const BlockProp* block = span->block();
            float value = -1.0f;
            switch (prop_id) {
                case CSS_PROPERTY_WIDTH: value = block->given_width; break;
                case CSS_PROPERTY_HEIGHT: value = block->given_height; break;
                case CSS_PROPERTY_MIN_WIDTH: value = block->given_min_width; break;
                case CSS_PROPERTY_MAX_WIDTH: value = block->given_max_width; break;
                case CSS_PROPERTY_MIN_HEIGHT: value = block->given_min_height; break;
                case CSS_PROPERTY_MAX_HEIGHT: value = block->given_max_height; break;
                default: break;
            }
            if (value < 0.0f || !isfinite(value)) return false;
            *out_type = ANIM_VAL_LENGTH;
            *out_f = value;
            return true;
        }
        case CSS_PROPERTY_OPACITY: {
            *out_type = ANIM_VAL_FLOAT;
            // opacity defaults to 1.0 when no InlineProp/opacity has been set.
            *out_f = (span->in_line) ? span->inl()->opacity : 1.0f;
            return true;
        }
        case CSS_PROPERTY_COLOR: {
            // Only snapshot color once it has an explicitly resolved used value;
            // otherwise the "from" would be an arbitrary zero-initialized color.
            if (span->in_line && span->inl()->has_color) {
                *out_type = ANIM_VAL_COLOR;
                *out_color = span->inl()->color;
                return true;
            }
            return false;
        }
        case CSS_PROPERTY_BACKGROUND_COLOR: {
            if (span->bound && span->boundary_mut()->background) {
                *out_type = ANIM_VAL_COLOR;
                *out_color = span->boundary()->background->color;
                return true;
            }
            return false;
        }
        case CSS_PROPERTY_ASPECT_RATIO: {
            // aspect-ratio is already resolved into the flex-item property;
            // omitting this used-value source makes transitions jump to `to`.
            if (!element->fi || element->fi->aspect_ratio <= 0.0f ||
                !isfinite(element->fi->aspect_ratio)) return false;
            *out_type = ANIM_VAL_ASPECT_RATIO;
            *out_ratio = element->fi->aspect_ratio;
            return true;
        }
        default:
            return false;
    }
}

// Map a supported property id to its transitionable value type (or ANIM_VAL_NONE).
static CssAnimValueType css_transition_value_type_for(CssPropertyCode prop_id) {
    switch (prop_id) {
        case CSS_PROPERTY_WIDTH:
        case CSS_PROPERTY_HEIGHT:
        case CSS_PROPERTY_MIN_WIDTH:
        case CSS_PROPERTY_MAX_WIDTH:
        case CSS_PROPERTY_MIN_HEIGHT:
        case CSS_PROPERTY_MAX_HEIGHT: return ANIM_VAL_LENGTH;
        case CSS_PROPERTY_OPACITY:          return ANIM_VAL_FLOAT;
        case CSS_PROPERTY_COLOR:            return ANIM_VAL_COLOR;
        case CSS_PROPERTY_BACKGROUND_COLOR: return ANIM_VAL_COLOR;
        case CSS_PROPERTY_ASPECT_RATIO:     return ANIM_VAL_ASPECT_RATIO;
        default:                            return ANIM_VAL_NONE;
    }
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
    else if (vt == ANIM_VAL_LENGTH) out->value.f = parsed.value.length.value;
    else if (vt == ANIM_VAL_COLOR) out->value.color = parsed.value.color;
    else if (vt == ANIM_VAL_ASPECT_RATIO) {
        out->value.aspect_ratio.value = parsed.value.aspect_ratio.value;
        out->value.aspect_ratio.is_auto = parsed.value.aspect_ratio.is_auto;
    }
    return true;
}

static bool css_transition_covers(const CssTransitionProp* tp, CssPropertyCode prop_id);

void css_transition_capture_before_change(DomElement* element, CssPropertyCode prop_id) {
    if (!element || !element->doc) return;

    CssAnimValueType vt = css_transition_value_type_for(prop_id);
    if (vt == ANIM_VAL_NONE) return;

    CssTransitionProp transition = {};
    CssPropertyCode property_buffer[10];
    bool has_transition = element->specified_style &&
        css_transition_resolve_config(element->specified_style,
                                      element->doc->document_pool,
                                      &transition, property_buffer, 10);
    if (!has_transition) {
        radiant_cascade_styles_for_element(element);
        has_transition = element->specified_style &&
            css_transition_resolve_config(element->specified_style,
                                          element->doc->document_pool,
                                          &transition, property_buffer, 10);
    }
    // Capture only after a transition is configured; the preceding inline write
    // is commonly the author-supplied `from` value, not a style change to animate.
    if (!has_transition || !css_transition_covers(&transition, prop_id)) return;

    CssTransitionElemState* es = (CssTransitionElemState*)element->transition_state;
    if (!es) {
        es = (CssTransitionElemState*)pool_calloc(
            element->doc->document_pool, sizeof(CssTransitionElemState));
        if (!es) return;
        element->transition_state = es;
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

    CssAnimatedProp interp;
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

    apply_animated_value(st->element, &interp);

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
    if (es->track_count >= CSS_TRANSITION_MAX_TRACKED) return NULL;
    CssTransitionTrack* tk = &es->tracks[es->track_count++];
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
    CssTransitionElemState* es = (CssTransitionElemState*)st->element->transition_state;
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
}

static void css_transition_cancel(AnimationInstance* anim) {
    CssTransitionState* st = (CssTransitionState*)anim->state;
    if (!st || !st->element) return;
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

// Determine whether a resolved CssTransitionProp covers a given property, and
// return its duration/delay/timing. property_count == -1 means "all".
static bool css_transition_covers(const CssTransitionProp* tp, CssPropertyCode prop_id) {
    if (tp->property_count < 0) return true; // "all"
    for (int i = 0; i < tp->property_count; i++) {
        if (tp->properties[i] == prop_id) return true;
    }
    return false;
}

// Append a property id to the transition-property list (dedup, capacity-checked).
static void css_transition_add_property(CssTransitionProp* tp, CssPropertyCode* buf,
                                        int cap, CssPropertyCode prop_id) {
    // css_property_code_from_name returns 0 (not -1) for unknown names; ids start at 1.
    if (prop_id == CSS_PROPERTY_UNKNOWN || (int)prop_id <= 0) return;
    if (tp->property_count < 0) return;         // already "all"
    for (int i = 0; i < tp->property_count; i++) {
        if (tp->properties[i] == prop_id) return;
    }
    if (tp->property_count >= cap) return;
    buf[tp->property_count++] = prop_id;
}

// Resolve a single CssValue item into a property id (keyword `all` -> -1 sentinel
// handled by caller; property-name keyword/custom -> CssPropertyCode). Returns
// CSS_PROPERTY_UNKNOWN if not a property name.
static CssPropertyCode css_transition_value_to_property(const CssValue* v, bool* out_all) {
    *out_all = false;
    if (!v) return CSS_PROPERTY_UNKNOWN;
    if (v->type == CSS_VALUE_TYPE_KEYWORD) {
        if (v->data.keyword == CSS_VALUE_ALL) { *out_all = true; return CSS_PROPERTY_UNKNOWN; }
        if (v->data.keyword == CSS_VALUE_NONE) return CSS_PROPERTY_UNKNOWN;
        const CssEnumInfo* info = css_enum_info(v->data.keyword);
        if (info && info->name) return (CssPropertyCode)css_property_code_from_name(info->name);
    } else if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name) {
        return (CssPropertyCode)css_property_code_from_name(v->data.custom_property.name);
    } else if (v->type == CSS_VALUE_TYPE_STRING && v->data.string) {
        return (CssPropertyCode)css_property_code_from_name(v->data.string);
    }
    return CSS_PROPERTY_UNKNOWN;
}

// Read a duration/delay CssValue (a time dimension, stored as CSS_VALUE_TYPE_LENGTH
// with unit s/ms) into seconds. Returns false if not a time value.
static bool css_transition_read_time(const CssValue* v, float* out_seconds) {
    if (!v) return false;
    if (v->type == CSS_VALUE_TYPE_LENGTH || v->type == CSS_VALUE_TYPE_TIME) {
        float val = (float)v->data.length.value;
        if (v->data.length.unit == CSS_UNIT_MS) val /= 1000.0f;
        *out_seconds = val;
        return true;
    }
    return false;
}

// Resolve the element's transition-* declarations (longhands + `transition`
// shorthand) into a CssTransitionProp. `prop_buf` backs the property list.
// Returns true if a usable transition config with duration > 0 was found.
static const CssValue* css_transition_first_value(const CssValue* value) {
    if (value && value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        return value->data.list.values[0];
    }
    return value;
}

static const CssValue* css_transition_tree_value(StyleTree* style_tree,
                                                 CssPropertyCode property) {
    if (!style_tree || !style_tree->tree) return NULL;
    AvlNode* node = avl_tree_search(style_tree->tree, property);
    StyleNode* style = node ? (StyleNode*)node->declaration : NULL;
    CssDeclaration* declaration = style ? style->winning_decl : NULL;
    return declaration ? declaration->value : NULL;
}

bool css_transition_resolve_values(const CssValue* shorthand_value,
                                   const CssValue* duration_value,
                                   const CssValue* delay_value,
                                   const CssValue* property_value,
                                   const CssValue* timing_value,
                                   CssTransitionProp* tp,
                                   CssPropertyCode* prop_buf, int prop_cap) {
    memset(tp, 0, sizeof(*tp));
    tp->properties = prop_buf;
    tp->property_count = 0;
    tp->duration = 0.0f;
    tp->delay = 0.0f;
    tp->timing = TIMING_EASE;

    bool saw_duration = false;
    bool all_props = false;

    // --- longhands ---
    duration_value = css_transition_first_value(duration_value);
    if (duration_value) {
        float secs;
        if (css_transition_read_time(duration_value, &secs)) { tp->duration = secs; saw_duration = true; }
    }

    delay_value = css_transition_first_value(delay_value);
    if (delay_value) {
        float secs;
        if (css_transition_read_time(delay_value, &secs)) tp->delay = secs;
    }

    timing_value = css_transition_first_value(timing_value);
    if (timing_value) {
        parse_timing_function_value(timing_value, &tp->timing);
    }

    bool longhand_prop_present = false;
    if (property_value) {
        const CssValue* v = property_value;
        longhand_prop_present = (v != NULL);
        if (v && v->type == CSS_VALUE_TYPE_LIST) {
            for (int i = 0; i < v->data.list.count; i++) {
                bool is_all = false;
                CssPropertyCode pid = css_transition_value_to_property(v->data.list.values[i], &is_all);
                if (is_all) { all_props = true; break; }
                css_transition_add_property(tp, prop_buf, prop_cap, pid);
            }
        } else if (v) {
            bool is_all = false;
            CssPropertyCode pid = css_transition_value_to_property(v, &is_all);
            if (is_all) all_props = true;
            else css_transition_add_property(tp, prop_buf, prop_cap, pid);
        }
    }

    // --- shorthand `transition` (not expanded by the CSS parser) ---
    // The shorthand contributes property names too. The final property set is
    // decided after both longhand and shorthand are read (see below).
    // Parse each comma-separated group: [property] [duration] [timing] [delay].
    // Time dimensions: first is duration, second is delay.
    if (shorthand_value) {
        const CssValue* sv = shorthand_value;
        if (sv) {
            // Normalize into a flat item list. A single group is a LIST of items;
            // multiple comma groups are a LIST of LISTs. We take the first group's
            // duration/delay/timing (single-timing slice) but collect property names
            // across all groups.
            const CssValue* const* groups = NULL;
            int group_count = 0;
            const CssValue* single_items[1];
            const CssValue* first_group_flat[16];
            if (sv->type == CSS_VALUE_TYPE_LIST && sv->data.list.count > 0 &&
                sv->data.list.values[0] &&
                sv->data.list.values[0]->type == CSS_VALUE_TYPE_LIST) {
                groups = sv->data.list.values;
                group_count = sv->data.list.count;
            } else {
                single_items[0] = sv;
                groups = single_items;
                group_count = 1;
            }

            bool sh_saw_time = false;
            for (int g = 0; g < group_count; g++) {
                const CssValue* grp = groups[g];
                const CssValue* const* items;
                int item_count;
                if (grp && grp->type == CSS_VALUE_TYPE_LIST) {
                    items = grp->data.list.values;
                    item_count = grp->data.list.count;
                } else {
                    first_group_flat[0] = grp;
                    items = first_group_flat;
                    item_count = 1;
                }
                int time_seen = 0;
                for (int i = 0; i < item_count; i++) {
                    const CssValue* it = items[i];
                    if (!it) continue;
                    float secs;
                    if (css_transition_read_time(it, &secs)) {
                        // only the first group drives duration/delay for this slice
                        if (g == 0) {
                            if (time_seen == 0) { tp->duration = secs; saw_duration = true; sh_saw_time = true; }
                            else if (time_seen == 1) { tp->delay = secs; }
                        }
                        time_seen++;
                    } else if (it->type == CSS_VALUE_TYPE_FUNCTION ||
                               (it->type == CSS_VALUE_TYPE_KEYWORD &&
                                (it->data.keyword == CSS_VALUE_EASE || it->data.keyword == CSS_VALUE_EASE_IN ||
                                 it->data.keyword == CSS_VALUE_EASE_OUT || it->data.keyword == CSS_VALUE_EASE_IN_OUT ||
                                 it->data.keyword == CSS_VALUE_LINEAR || it->data.keyword == CSS_VALUE_STEP_START ||
                                 it->data.keyword == CSS_VALUE_STEP_END))) {
                        if (g == 0) parse_timing_function_value(it, &tp->timing);
                    } else {
                        bool is_all = false;
                        CssPropertyCode pid = css_transition_value_to_property(it, &is_all);
                        if (is_all) all_props = true;
                        else css_transition_add_property(tp, prop_buf, prop_cap, pid);
                    }
                }
            }
            (void)sh_saw_time;
        }
    }

    // Decide the property set. "all" wins if any source said `all`. Otherwise, if
    // an explicit list was collected (from shorthand or longhand), use it. If no
    // source named any property, the initial value "all" applies.
    bool any_explicit_list = (tp->property_count > 0);
    (void)longhand_prop_present;
    if (all_props || !any_explicit_list) {
        tp->property_count = -1;   // covers all supported properties
        tp->properties = NULL;
    }

    return saw_duration && tp->duration > 0.0f;
}

bool css_transition_resolve_config(StyleTree* style_tree, Pool* pool,
                                   CssTransitionProp* tp,
                                   CssPropertyCode* prop_buf, int prop_cap) {
    (void)pool;
    return css_transition_resolve_values(
        css_transition_tree_value(style_tree, CSS_PROPERTY_TRANSITION),
        css_transition_tree_value(style_tree, CSS_PROPERTY_TRANSITION_DURATION),
        css_transition_tree_value(style_tree, CSS_PROPERTY_TRANSITION_DELAY),
        css_transition_tree_value(style_tree, CSS_PROPERTY_TRANSITION_PROPERTY),
        css_transition_tree_value(style_tree, CSS_PROPERTY_TRANSITION_TIMING_FUNCTION),
        tp, prop_buf, prop_cap);
}

// Supported transitionable properties for the "all" keyword.
static const CssPropertyCode kTransitionSupported[] = {
    CSS_PROPERTY_WIDTH, CSS_PROPERTY_HEIGHT,
    CSS_PROPERTY_MIN_WIDTH, CSS_PROPERTY_MAX_WIDTH,
    CSS_PROPERTY_MIN_HEIGHT, CSS_PROPERTY_MAX_HEIGHT,
    CSS_PROPERTY_OPACITY, CSS_PROPERTY_COLOR, CSS_PROPERTY_BACKGROUND_COLOR,
    CSS_PROPERTY_ASPECT_RATIO,
};
static const int kTransitionSupportedCount =
    (int)(sizeof(kTransitionSupported) / sizeof(kTransitionSupported[0]));

// Start (or restart) a transition for one property from `from` to `to`.
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
            if (cvt == ANIM_VAL_FLOAT) from_f = cf;
            else if (cvt == ANIM_VAL_COLOR) from_c = cc;
            else if (cvt == ANIM_VAL_ASPECT_RATIO) from_ratio = cr;
        }
        animation_scheduler_cancel(scheduler, existing);
    }

    CssTransitionState* st = (CssTransitionState*)pool_calloc(pool, sizeof(CssTransitionState));
    st->element = element;
    st->ui_context = ui_context;
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
    if (!inst) return;
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
    inst->fill_mode = ANIM_FILL_FORWARDS;
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
    CssTransitionProp tp;
    CssPropertyCode prop_buf[8];
    bool has_transition = css_transition_resolve_config(style_tree, pool, &tp, prop_buf, 8);
    if (has_transition) {
        log_debug("css-transition: resolve <%s> dur=%.3fs count=%d",
                  element->tag_name ? element->tag_name : "?", tp.duration, tp.property_count);
    }

    // Lazily allocate the persistent per-element transition state (survives the
    // view-pool relayout because it lives in the doc pool, not the view pool).
    CssTransitionElemState* es = (CssTransitionElemState*)element->transition_state;
    if (!es) {
        es = (CssTransitionElemState*)pool_calloc(pool, sizeof(CssTransitionElemState));
        es->track_count = 0;
        element->transition_state = es;
    }

    double now = scheduler->current_time;

    // Walk the supported property set. For each: read the new used value, compare
    // to the snapshot; if changed and covered by a transition declaration (with
    // a positive duration), start an interpolating instance. Always update the
    // snapshot to the new used value.
    for (int i = 0; i < kTransitionSupportedCount; i++) {
        CssPropertyCode prop_id = kTransitionSupported[i];
        CssAnimValueType vt = css_transition_value_type_for(prop_id);

        CssAnimValueType read_vt; float new_f = 0.0f; float new_ratio = 0.0f;
        Color new_c; new_c.c = 0;
        if (!css_transition_read_used_value(element, prop_id, &read_vt,
                                            &new_f, &new_c, &new_ratio)) {
            continue; // used value not determinable this pass — skip
        }

        CssTransitionTrack* track = css_transition_track_for(es, prop_id, vt);
        if (!track) continue;

        // A running transition owns the property: its own tick overwrites the used
        // value each frame, so we must NOT diff against the snapshot (that would be
        // a spurious change) and must NOT overwrite the snapshot. Scan the scheduler
        // (authoritative) rather than trusting a raw pointer across relayouts.
        bool is_running = (css_transition_find_running(scheduler, element, prop_id) != NULL);
        if (is_running) continue;

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

        bool covered = has_transition && css_transition_covers(&tp, prop_id);

        if (changed && covered) {
            if (!track->has_snapshot && track->has_pending_from) {
                // Preserve the pre-change style when this script turn defers its
                // first layout until after the DOM mutation batch.
                track->has_snapshot = true;
                track->snapshot = track->pending_from;
            }
            css_transition_start(scheduler, element, track, &tp, vt,
                                 from_f, from_c, from_ratio, new_f, new_c,
                                 new_ratio, now, pool,
                                 lycon->ui_context);
            // Headless layout has no frame before serialization; apply the
            // transition's current time so its negative delay affects this pass.
            animation_scheduler_tick(scheduler, now, NULL);
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
