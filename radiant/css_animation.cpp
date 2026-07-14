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
static CssAnimValueType property_value_type(CssPropertyId id) {
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

// Parse a property value into CssAnimatedProp
static bool parse_property_value(CssPropertyId prop_id, const char* val,
                                  CssAnimatedProp* out, Pool* pool) {
    out->property_id = prop_id;
    out->value_type = property_value_type(prop_id);

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
        case ANIM_VAL_TRANSFORM: {
            out->value.transform = parse_transform_value(val, pool);
            return out->value.transform != NULL;
        }
        default:
            return false;
    }
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

            // resolve property and parse value
            CssPropertyId prop_id = (CssPropertyId)css_property_id_from_name(prop_name);
            if (prop_id != (CssPropertyId)0) {
                if (parse_property_value(prop_id, val_buf, &temp_props[prop_count], pool)) {
                    prop_count++;
                }
            }
        }

        if (*p == '}') p++; // skip closing brace of keyframe stop

        if (prop_count > 0) {
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

void keyframe_registry_destroy(KeyframeRegistry* registry) {
    // pool-allocated, freed when pool is destroyed
    (void)registry;
}

// ============================================================================
// CSS Animation Tick
// ============================================================================

// Find two surrounding keyframe stops for progress t, and compute local interpolation t
static void find_keyframe_pair(CssKeyframes* kf, float t,
                                int* out_stop_a, int* out_stop_b, float* out_local_t) {
    // clamp t
    if (t <= 0.0f) { *out_stop_a = 0; *out_stop_b = 0; *out_local_t = 0.0f; return; }
    if (t >= 1.0f) { *out_stop_a = kf->stop_count - 1; *out_stop_b = kf->stop_count - 1; *out_local_t = 1.0f; return; }

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
static CssAnimatedProp* find_prop_in_stop(CssKeyframeStop* stop, CssPropertyId id) {
    for (int i = 0; i < stop->property_count; i++) {
        if (stop->properties[i].property_id == id) {
            return &stop->properties[i];
        }
    }
    return NULL;
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
        if (el->doc && el->doc->view_tree && el->doc->view_tree->pool) {
            span->in_line = (InlineProp*)pool_calloc(el->doc->view_tree->pool, sizeof(InlineProp));
            if (span->in_line) span->in_line->opacity = 1.0f;
        }
    }
    return span->in_line;
}

// Lazily ensure BoundaryProp + BackgroundProp exist (needed for background-color
// animation when the element has no static background declaration)
static BackgroundProp* ensure_background_prop(ViewSpan* span) {
    DomElement* el = lam::dom_require_element(span);
    Pool* pool = (el->doc && el->doc->view_tree) ? el->doc->view_tree->pool : NULL;
    if (!pool) return NULL;
    if (!span->bound) {
        span->bound = (BoundaryProp*)pool_calloc(pool, sizeof(BoundaryProp));
    }
    if (span->bound && !span->bound->background) {
        span->bound->background = (BackgroundProp*)pool_calloc(pool, sizeof(BackgroundProp));
    }
    return span->bound ? span->bound->background : NULL;
}

// Apply an interpolated property value to a DomElement
static void apply_animated_value(DomElement* element, CssAnimatedProp* prop) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));

    switch (prop->property_id) {
        case CSS_PROPERTY_OPACITY: {
            InlineProp* il = ensure_inline_prop(span);
            if (il) il->opacity = prop->value.f;
            break;
        }
        case CSS_PROPERTY_TRANSFORM: {
            if (!span->transform) {
                // allocate TransformProp if not present
                // use element's doc arena for allocation
                if (element->doc && element->doc->arena) {
                    span->transform = (TransformProp*)arena_calloc(element->doc->arena, sizeof(TransformProp));
                    span->transform->origin_x = 50.0f;
                    span->transform->origin_y = 50.0f;
                    span->transform->origin_x_percent = true;
                    span->transform->origin_y_percent = true;
                }
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
        default:
            break;
    }
}

void css_animation_tick(AnimationInstance* anim, float t) {
    CssAnimState* state = (CssAnimState*)anim->state;
    if (!state || !state->keyframes || !state->element) return;

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
                 state->element->doc->pool : NULL;

    // interpolate each property present in either stop
    // use stop_b's properties as the canonical set
    for (int i = 0; i < sb->property_count; i++) {
        CssAnimatedProp* prop_b = &sb->properties[i];
        CssAnimatedProp* prop_a = find_prop_in_stop(sa, prop_b->property_id);

        CssAnimatedProp interp;
        interp.property_id = prop_b->property_id;
        interp.value_type = prop_b->value_type;

        if (stop_a == stop_b || !prop_a) {
            // same stop or property only in one stop — use directly
            interp.value = prop_b->value;
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
                    interp.value.length.value = css_interpolate_float(
                        prop_a->value.length.value, prop_b->value.length.value, local_t);
                    interp.value.length.is_percent = prop_b->value.length.is_percent;
                    break;
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

        apply_animated_value(state->element, &interp);
    }

    // also apply properties only present in stop_a (but not in stop_b)
    for (int i = 0; i < sa->property_count; i++) {
        CssAnimatedProp* prop_a = &sa->properties[i];
        if (!find_prop_in_stop(sb, prop_a->property_id)) {
            apply_animated_value(state->element, prop_a);
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
        log_debug("css-anim: animation '%s' finished for element %p",
                  state->keyframes ? state->keyframes->name : "?", state->element);
    }
}

// ============================================================================
// CSS Animation Creation
// ============================================================================

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
                if (func->args[1]->data.keyword == CSS_VALUE_STEP_START)
                    out->steps.position = STEP_JUMP_START;
            }
            return;
        }
    }
    // default to ease
    *out = TIMING_EASE;
}

void css_animation_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element || !lycon || !lycon->ui_context) return;

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
    if (!doc->keyframe_registry) {
        doc->keyframe_registry = keyframe_registry_create(doc, doc->pool);
    }

    CssKeyframes* keyframes = keyframe_registry_find(
        (KeyframeRegistry*)doc->keyframe_registry, anim_name);
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
    css_animation_create(scheduler, element, &anim_prop, keyframes, now, doc->pool);
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
// Scope: this vertical slice supports the property/value-types that both the
// write side (apply_animated_value) and the read side (used-value snapshot)
// already handle: opacity (float), color (color), background-color (color).
// transform and length-valued properties are deferred — see report.

// Read the current used value of a transitionable property from the element's
// view props (the symmetric read side of apply_animated_value). Returns false
// if the property is unsupported or its used value is not currently determinable.
static bool css_transition_read_used_value(DomElement* element,
                                           CssPropertyId prop_id,
                                           CssAnimValueType* out_type,
                                           float* out_f, Color* out_color) {
    ViewSpan* span = lam::view_require_element(static_cast<View*>(element));
    switch (prop_id) {
        case CSS_PROPERTY_OPACITY: {
            *out_type = ANIM_VAL_FLOAT;
            // opacity defaults to 1.0 when no InlineProp/opacity has been set.
            *out_f = (span->in_line) ? span->in_line->opacity : 1.0f;
            return true;
        }
        case CSS_PROPERTY_COLOR: {
            // Only snapshot color once it has an explicitly resolved used value;
            // otherwise the "from" would be an arbitrary zero-initialized color.
            if (span->in_line && span->in_line->has_color) {
                *out_type = ANIM_VAL_COLOR;
                *out_color = span->in_line->color;
                return true;
            }
            return false;
        }
        case CSS_PROPERTY_BACKGROUND_COLOR: {
            if (span->bound && span->bound->background) {
                *out_type = ANIM_VAL_COLOR;
                *out_color = span->bound->background->color;
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Map a supported property id to its transitionable value type (or ANIM_VAL_NONE).
static CssAnimValueType css_transition_value_type_for(CssPropertyId prop_id) {
    switch (prop_id) {
        case CSS_PROPERTY_OPACITY:          return ANIM_VAL_FLOAT;
        case CSS_PROPERTY_COLOR:            return ANIM_VAL_COLOR;
        case CSS_PROPERTY_BACKGROUND_COLOR: return ANIM_VAL_COLOR;
        default:                            return ANIM_VAL_NONE;
    }
}

void css_transition_tick(AnimationInstance* anim, float t) {
    CssTransitionState* st = (CssTransitionState*)anim->state;
    if (!st || !st->element) return;

    CssAnimatedProp interp;
    interp.property_id = st->property_id;
    interp.value_type = st->value_type;

    // On the final tick (play_state flipped to FINISHED by the scheduler), snap
    // exactly to the target so no rounding residue is left behind.
    bool finished = (anim->play_state == ANIM_PLAY_FINISHED);

    switch (st->value_type) {
        case ANIM_VAL_FLOAT:
            interp.value.f = finished ? st->to.f
                                      : css_interpolate_float(st->from.f, st->to.f, t);
            break;
        case ANIM_VAL_COLOR:
            interp.value.color = finished ? st->to.color
                                          : css_interpolate_color(st->from.color, st->to.color, t);
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
                                                    CssPropertyId prop_id,
                                                    CssAnimValueType vt) {
    for (int i = 0; i < es->track_count; i++) {
        if (es->tracks[i].property_id == prop_id) return &es->tracks[i];
    }
    if (es->track_count >= CSS_TRANSITION_MAX_TRACKED) return NULL;
    CssTransitionTrack* tk = &es->tracks[es->track_count++];
    tk->property_id = prop_id;
    tk->value_type = vt;
    tk->has_snapshot = false;
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
            if (es->tracks[i].property_id == st->property_id) {
                es->tracks[i].value_type = st->value_type;
                es->tracks[i].has_snapshot = true;
                if (st->value_type == ANIM_VAL_FLOAT) es->tracks[i].snapshot.f = st->to.f;
                else if (st->value_type == ANIM_VAL_COLOR) es->tracks[i].snapshot.color = st->to.color;
                break;
            }
        }
    }
    log_debug("css-transition: finished prop=%d for element %p", st->property_id, st->element);
}

// Find a live transition instance for (element, property) in the scheduler, or NULL.
// Scanning the authoritative list avoids dangling back-pointers across relayouts.
static AnimationInstance* css_transition_find_running(AnimationScheduler* scheduler,
                                                      DomElement* element,
                                                      CssPropertyId prop_id) {
    for (AnimationInstance* a = scheduler->first; a; a = a->next) {
        if (a->type == ANIM_CSS_TRANSITION && a->target == element) {
            CssTransitionState* s = (CssTransitionState*)a->state;
            if (s && s->property_id == prop_id) return a;
        }
    }
    return NULL;
}

// Determine whether a resolved CssTransitionProp covers a given property, and
// return its duration/delay/timing. property_count == -1 means "all".
static bool css_transition_covers(const CssTransitionProp* tp, CssPropertyId prop_id) {
    if (tp->property_count < 0) return true; // "all"
    for (int i = 0; i < tp->property_count; i++) {
        if (tp->properties[i] == prop_id) return true;
    }
    return false;
}

// Append a property id to the transition-property list (dedup, capacity-checked).
static void css_transition_add_property(CssTransitionProp* tp, CssPropertyId* buf,
                                        int cap, CssPropertyId prop_id) {
    // css_property_id_from_name returns 0 (not -1) for unknown names; ids start at 1.
    if (prop_id == CSS_PROPERTY_UNKNOWN || (int)prop_id <= 0) return;
    if (tp->property_count < 0) return;         // already "all"
    for (int i = 0; i < tp->property_count; i++) {
        if (tp->properties[i] == prop_id) return;
    }
    if (tp->property_count >= cap) return;
    buf[tp->property_count++] = prop_id;
}

// Resolve a single CssValue item into a property id (keyword `all` -> -1 sentinel
// handled by caller; property-name keyword/custom -> CssPropertyId). Returns
// CSS_PROPERTY_UNKNOWN if not a property name.
static CssPropertyId css_transition_value_to_property(const CssValue* v, bool* out_all) {
    *out_all = false;
    if (!v) return CSS_PROPERTY_UNKNOWN;
    if (v->type == CSS_VALUE_TYPE_KEYWORD) {
        if (v->data.keyword == CSS_VALUE_ALL) { *out_all = true; return CSS_PROPERTY_UNKNOWN; }
        if (v->data.keyword == CSS_VALUE_NONE) return CSS_PROPERTY_UNKNOWN;
        const CssEnumInfo* info = css_enum_info(v->data.keyword);
        if (info && info->name) return (CssPropertyId)css_property_id_from_name(info->name);
    } else if (v->type == CSS_VALUE_TYPE_CUSTOM && v->data.custom_property.name) {
        return (CssPropertyId)css_property_id_from_name(v->data.custom_property.name);
    } else if (v->type == CSS_VALUE_TYPE_STRING && v->data.string) {
        return (CssPropertyId)css_property_id_from_name(v->data.string);
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
static const CssValue* css_transition_longhand_value(StyleTree* style_tree,
                                                     CssPropertyId property) {
    AvlNode* node = avl_tree_search(style_tree->tree, property);
    StyleNode* style = node ? (StyleNode*)node->declaration : NULL;
    CssDeclaration* declaration = style ? style->winning_decl : NULL;
    const CssValue* value = declaration ? declaration->value : NULL;
    if (value && value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        value = value->data.list.values[0];
    }
    return value;
}

static bool css_transition_resolve_config(StyleTree* style_tree, Pool* pool,
                                          CssTransitionProp* tp,
                                          CssPropertyId* prop_buf, int prop_cap) {
    memset(tp, 0, sizeof(*tp));
    tp->properties = prop_buf;
    tp->property_count = 0;
    tp->duration = 0.0f;
    tp->delay = 0.0f;
    tp->timing = TIMING_EASE;

    bool saw_duration = false;
    bool all_props = false;

    // --- longhands ---
    const CssValue* duration_value = css_transition_longhand_value(
        style_tree, CSS_PROPERTY_TRANSITION_DURATION);
    if (duration_value) {
        float secs;
        if (css_transition_read_time(duration_value, &secs)) { tp->duration = secs; saw_duration = true; }
    }

    const CssValue* delay_value = css_transition_longhand_value(
        style_tree, CSS_PROPERTY_TRANSITION_DELAY);
    if (delay_value) {
        float secs;
        if (css_transition_read_time(delay_value, &secs)) tp->delay = secs;
    }

    const CssValue* timing_value = css_transition_longhand_value(
        style_tree, CSS_PROPERTY_TRANSITION_TIMING_FUNCTION);
    if (timing_value) {
        parse_timing_function_value(timing_value, &tp->timing);
    }

    bool longhand_prop_present = false;
    AvlNode* prop_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_TRANSITION_PROPERTY);
    if (prop_node) {
        StyleNode* sn = (StyleNode*)prop_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        const CssValue* v = d ? d->value : NULL;
        longhand_prop_present = (v != NULL);
        if (v && v->type == CSS_VALUE_TYPE_LIST) {
            for (int i = 0; i < v->data.list.count; i++) {
                bool is_all = false;
                CssPropertyId pid = css_transition_value_to_property(v->data.list.values[i], &is_all);
                if (is_all) { all_props = true; break; }
                css_transition_add_property(tp, prop_buf, prop_cap, pid);
            }
        } else if (v) {
            bool is_all = false;
            CssPropertyId pid = css_transition_value_to_property(v, &is_all);
            if (is_all) all_props = true;
            else css_transition_add_property(tp, prop_buf, prop_cap, pid);
        }
    }

    // --- shorthand `transition` (not expanded by the CSS parser) ---
    // The shorthand contributes property names too. The final property set is
    // decided after both longhand and shorthand are read (see below).
    // Parse each comma-separated group: [property] [duration] [timing] [delay].
    // Time dimensions: first is duration, second is delay.
    AvlNode* sh_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_TRANSITION);
    if (sh_node) {
        StyleNode* sn = (StyleNode*)sh_node->declaration;
        CssDeclaration* d = sn ? sn->winning_decl : NULL;
        const CssValue* sv = d ? d->value : NULL;
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
                        CssPropertyId pid = css_transition_value_to_property(it, &is_all);
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

// Supported transitionable properties for the "all" keyword.
static const CssPropertyId kTransitionSupported[] = {
    CSS_PROPERTY_OPACITY, CSS_PROPERTY_COLOR, CSS_PROPERTY_BACKGROUND_COLOR,
};
static const int kTransitionSupportedCount =
    (int)(sizeof(kTransitionSupported) / sizeof(kTransitionSupported[0]));

// Start (or restart) a transition for one property from `from` to `to`.
static void css_transition_start(AnimationScheduler* scheduler, DomElement* element,
                                 CssTransitionTrack* track, const CssTransitionProp* tp,
                                 CssAnimValueType vt, float from_f, Color from_c,
                                 float to_f, Color to_c, double now, Pool* pool) {
    // If a transition for this property is already running, reverse/interrupt from
    // its current interpolated value: cancel the old one and start fresh so we don't
    // stack instances. The current applied used value IS the interpolated value.
    AnimationInstance* existing = css_transition_find_running(scheduler, element, track->property_id);
    if (existing) {
        CssAnimValueType cvt; float cf = 0; Color cc; cc.c = 0;
        if (css_transition_read_used_value(element, track->property_id, &cvt, &cf, &cc)) {
            if (cvt == ANIM_VAL_FLOAT) from_f = cf;
            else if (cvt == ANIM_VAL_COLOR) from_c = cc;
        }
        animation_scheduler_remove(scheduler, existing);
    }

    CssTransitionState* st = (CssTransitionState*)pool_calloc(pool, sizeof(CssTransitionState));
    st->element = element;
    st->property_id = track->property_id;
    st->value_type = vt;
    if (vt == ANIM_VAL_FLOAT) { st->from.f = from_f; st->to.f = to_f; }
    else { st->from.color = from_c; st->to.color = to_c; }

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

    animation_update_layout_bounds(inst, static_cast<View*>(element));

    animation_scheduler_add(scheduler, inst);

    log_debug("css-transition: started prop=%d for <%s> (dur=%.3fs delay=%.3fs)",
              track->property_id, element->tag_name ? element->tag_name : "?",
              tp->duration, tp->delay);
}

void css_transition_resolve(DomElement* element, LayoutContext* lycon) {
    if (!element || !lycon || !lycon->ui_context) return;

    StyleTree* style_tree = element->specified_style;
    if (!style_tree || !style_tree->tree) return;

    DomDocument* doc = lycon->ui_context->document;
    if (!doc) return;
    DocState* rs = (DocState*)doc->state;
    if (!rs || !rs->animation_scheduler) return;
    AnimationScheduler* scheduler = rs->animation_scheduler;
    Pool* pool = doc->pool;

    // Resolve the transition config. Even if no transition is declared we still
    // maintain the used-value snapshot below (so a later declaration starts from
    // a correct "from"), but we only START transitions when duration > 0.
    CssTransitionProp tp;
    CssPropertyId prop_buf[8];
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
        CssPropertyId prop_id = kTransitionSupported[i];
        CssAnimValueType vt = css_transition_value_type_for(prop_id);

        CssAnimValueType read_vt; float new_f = 0.0f; Color new_c; new_c.c = 0;
        if (!css_transition_read_used_value(element, prop_id, &read_vt, &new_f, &new_c)) {
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
        float from_f = new_f; Color from_c = new_c;
        if (track->has_snapshot) {
            if (vt == ANIM_VAL_FLOAT) {
                from_f = track->snapshot.f;
                changed = (fabsf(track->snapshot.f - new_f) > 0.0001f);
            } else if (vt == ANIM_VAL_COLOR) {
                from_c = track->snapshot.color;
                changed = (track->snapshot.color.c != new_c.c);
            }
        }

        bool covered = has_transition && css_transition_covers(&tp, prop_id);

        if (changed && covered) {
            css_transition_start(scheduler, element, track, &tp, vt,
                                 from_f, from_c, new_f, new_c, now, pool);
            // snapshot stays at the OLD value until the instance finishes (finish
            // snaps it to `to`); do not overwrite here.
        } else {
            // no active transition — track the current used value as the baseline
            track->value_type = vt;
            track->has_snapshot = true;
            if (vt == ANIM_VAL_FLOAT) track->snapshot.f = new_f;
            else if (vt == ANIM_VAL_COLOR) track->snapshot.color = new_c;
        }
    }
}
