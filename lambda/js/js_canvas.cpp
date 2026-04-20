/**
 * js_canvas.cpp — Minimal OffscreenCanvas / CanvasRenderingContext2D for JS runtime
 *
 * Provides text measurement via Lambda's unified font engine (lib/font/).
 * Only implements the subset needed by Pretext.js:
 *   new OffscreenCanvas(w, h)
 *   canvas.getContext("2d")
 *   ctx.font = "16px sans-serif"
 *   ctx.measureText(text) → { width }
 */

#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/font/font.h"
#include "../../lib/log.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

extern void* heap_alloc(int size, TypeId type_id);

// ============================================================================
// Lazy FontContext singleton — no UiContext/GLFW required
// ============================================================================

static FontContext* s_canvas_font_ctx = nullptr;

static FontContext* canvas_get_font_context() {
    if (!s_canvas_font_ctx) {
        FontContextConfig cfg = {};
        cfg.pixel_ratio = 1.0f;
        cfg.max_cached_faces = 32;
        s_canvas_font_ctx = font_context_create(&cfg);
        if (!s_canvas_font_ctx) {
            log_error("js_canvas: failed to create FontContext");
        }
    }
    return s_canvas_font_ctx;
}

// ============================================================================
// Font handle pool — store FontHandle* indexed by integer ID
// ============================================================================

#define MAX_CANVAS_FONT_HANDLES 64

static FontHandle* s_font_handles[MAX_CANVAS_FONT_HANDLES];
static int s_font_handle_count = 0;

static int canvas_store_font_handle(FontHandle* handle) {
    if (!handle) return -1;
    // check for reuse of existing identical handle
    for (int i = 0; i < s_font_handle_count; i++) {
        if (s_font_handles[i] == handle) {
            font_handle_release(handle); // already retained by pool
            return i;
        }
    }
    if (s_font_handle_count >= MAX_CANVAS_FONT_HANDLES) {
        // evict oldest
        font_handle_release(s_font_handles[0]);
        for (int i = 1; i < MAX_CANVAS_FONT_HANDLES; i++)
            s_font_handles[i - 1] = s_font_handles[i];
        s_font_handle_count = MAX_CANVAS_FONT_HANDLES - 1;
    }
    int id = s_font_handle_count++;
    s_font_handles[id] = handle;
    return id;
}

static FontHandle* canvas_get_font_handle(int id) {
    if (id < 0 || id >= s_font_handle_count) return nullptr;
    return s_font_handles[id];
}

// ============================================================================
// CSS font shorthand parser
// Supports: [style] [weight] size[/line-height] family[, family2, ...]
// Examples: "16px sans-serif", "bold 12px Arial", "italic 700 14px 'Helvetica Neue'"
// ============================================================================

static FontHandle* parse_css_font_shorthand(const char* font_str, int len) {
    FontContext* ctx = canvas_get_font_context();
    if (!ctx) return nullptr;
    if (!font_str || len <= 0) return nullptr;

    // work on a null-terminated copy
    char buf[512];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, font_str, len);
    buf[len] = '\0';

    FontWeight weight = FONT_WEIGHT_NORMAL;
    FontSlant slant = FONT_SLANT_NORMAL;
    float size_px = 16.0f;
    const char* family_start = nullptr;

    // tokenize: walk through space-separated tokens
    char* p = buf;

    // skip leading whitespace
    while (*p && isspace((unsigned char)*p)) p++;

    // parse optional style
    if (strncmp(p, "italic", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        slant = FONT_SLANT_ITALIC;
        p += 6;
        while (*p && isspace((unsigned char)*p)) p++;
    } else if (strncmp(p, "oblique", 7) == 0 && (p[7] == ' ' || p[7] == '\0')) {
        slant = FONT_SLANT_OBLIQUE;
        p += 7;
        while (*p && isspace((unsigned char)*p)) p++;
    } else if (strncmp(p, "normal", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        p += 6;
        while (*p && isspace((unsigned char)*p)) p++;
    }

    // parse optional weight
    if (strncmp(p, "bold", 4) == 0 && (p[4] == ' ' || p[4] == '\0')) {
        weight = FONT_WEIGHT_BOLD;
        p += 4;
        while (*p && isspace((unsigned char)*p)) p++;
    } else if (strncmp(p, "normal", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        p += 6;
        while (*p && isspace((unsigned char)*p)) p++;
    } else if (isdigit((unsigned char)*p)) {
        // numeric weight like "700" — but only if followed by a space (not "700px")
        char* end;
        long w = strtol(p, &end, 10);
        if (end > p && *end == ' ') {
            // it's a weight (100-900), not a size
            weight = (FontWeight)w;
            p = end;
            while (*p && isspace((unsigned char)*p)) p++;
        }
        // if end points to 'p' like "16px", fall through to size parsing
    }

    // parse size (required) — e.g. "16px", "1.5em", "12pt"
    if (isdigit((unsigned char)*p) || *p == '.') {
        char* end;
        size_px = strtof(p, &end);
        if (end > p) {
            p = end;
            // skip unit
            if (strncmp(p, "px", 2) == 0) p += 2;
            else if (strncmp(p, "pt", 2) == 0) { size_px *= 4.0f / 3.0f; p += 2; }
            else if (strncmp(p, "em", 2) == 0) { size_px *= 16.0f; p += 2; }
            else if (strncmp(p, "rem", 3) == 0) { size_px *= 16.0f; p += 3; }
        }
        // skip optional /line-height
        if (*p == '/') {
            p++;
            strtof(p, &end); // consume line-height value
            if (end > p) {
                p = end;
                if (strncmp(p, "px", 2) == 0) p += 2;
            }
        }
        while (*p && isspace((unsigned char)*p)) p++;
    }

    // remainder is font family
    family_start = p;
    if (!*family_start) family_start = "sans-serif";

    // strip quotes from family if present (e.g. "'Helvetica Neue'" → "Helvetica Neue")
    // for simplicity, we handle the first family only (before comma)
    static char family_buf[256];
    const char* comma = strchr(family_start, ',');
    int fam_len = comma ? (int)(comma - family_start) : (int)strlen(family_start);
    if (fam_len >= (int)sizeof(family_buf)) fam_len = (int)sizeof(family_buf) - 1;
    memcpy(family_buf, family_start, fam_len);
    family_buf[fam_len] = '\0';

    // trim trailing whitespace
    while (fam_len > 0 && isspace((unsigned char)family_buf[fam_len - 1]))
        family_buf[--fam_len] = '\0';
    // strip surrounding quotes
    if (fam_len >= 2 && (family_buf[0] == '\'' || family_buf[0] == '"')) {
        char q = family_buf[0];
        if (family_buf[fam_len - 1] == q) {
            memmove(family_buf, family_buf + 1, fam_len - 2);
            family_buf[fam_len - 2] = '\0';
        }
    }

    FontStyleDesc style = {};
    style.family = family_buf;
    style.size_px = size_px;
    style.weight = weight;
    style.slant = slant;

    return font_resolve(ctx, &style);
}

// ============================================================================
// OffscreenCanvas constructor
// ============================================================================

extern "C" Item js_offscreen_canvas_new(Item width_arg, Item height_arg) {
    Item obj = js_new_object();

    // set class name for dispatch
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("OffscreenCanvas"))};
    js_property_set(obj, type_key, type_val);

    // store width/height
    int64_t w = (get_type_id(width_arg) == LMD_TYPE_INT) ? it2i(width_arg) : 300;
    int64_t h = (get_type_id(height_arg) == LMD_TYPE_INT) ? it2i(height_arg) : 150;
    Item wk = (Item){.item = s2it(heap_create_name("width"))};
    Item hk = (Item){.item = s2it(heap_create_name("height"))};
    js_property_set(obj, wk, (Item){.item = i2it(w)});
    js_property_set(obj, hk, (Item){.item = i2it(h)});

    return obj;
}

// ============================================================================
// CanvasRenderingContext2D — getContext("2d") result
// ============================================================================

extern "C" Item js_canvas_get_context(Item canvas) {
    Item obj = js_new_object();

    // set class name
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("CanvasRenderingContext2D"))};
    js_property_set(obj, type_key, type_val);

    // store canvas reference
    Item canvas_key = (Item){.item = s2it(heap_create_name("canvas"))};
    js_property_set(obj, canvas_key, canvas);

    // initial font property (CSS default)
    Item font_key = (Item){.item = s2it(heap_create_name("font"))};
    Item font_val = (Item){.item = s2it(heap_create_name("10px sans-serif"))};
    js_property_set(obj, font_key, font_val);

    // no font handle yet — will be resolved on first measureText or when font is set
    Item fh_key = (Item){.item = s2it(heap_create_name("__font_handle_id"))};
    js_property_set(obj, fh_key, (Item){.item = i2it(-1)});

    return obj;
}

// ============================================================================
// ctx.font setter — resolve font handle when font property changes
// ============================================================================

extern "C" void js_canvas_ctx_set_font(Item ctx_obj, Item font_val) {
    // store the font string
    Item font_key = (Item){.item = s2it(heap_create_name("font"))};
    js_property_set(ctx_obj, font_key, font_val);

    // resolve font handle
    if (get_type_id(font_val) != LMD_TYPE_STRING) return;
    String* s = it2s(font_val);
    if (!s || s->len == 0) return;

    FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
    if (handle) {
        int id = canvas_store_font_handle(handle);
        Item fh_key = (Item){.item = s2it(heap_create_name("__font_handle_id"))};
        js_property_set(ctx_obj, fh_key, (Item){.item = i2it(id)});
    }
}

// ============================================================================
// ctx.measureText(text) → { width }
// ============================================================================

extern "C" Item js_canvas_measure_text(Item ctx_obj, Item text_arg) {
    // get font handle ID
    Item fh_key = (Item){.item = s2it(heap_create_name("__font_handle_id"))};
    bool has = false;
    Item fh_val = js_property_get(ctx_obj, fh_key);
    int fh_id = -1;
    if (get_type_id(fh_val) == LMD_TYPE_INT) {
        fh_id = (int)it2i(fh_val);
    }

    // if no font handle, try to resolve from current font string
    if (fh_id < 0) {
        Item font_key = (Item){.item = s2it(heap_create_name("font"))};
        Item font_str = js_property_get(ctx_obj, font_key);
        if (get_type_id(font_str) == LMD_TYPE_STRING) {
            String* s = it2s(font_str);
            if (s && s->len > 0) {
                FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
                if (handle) {
                    fh_id = canvas_store_font_handle(handle);
                    js_property_set(ctx_obj, fh_key, (Item){.item = i2it(fh_id)});
                }
            }
        }
    }

    FontHandle* handle = canvas_get_font_handle(fh_id);

    // get text string
    float width = 0.0f;
    if (get_type_id(text_arg) == LMD_TYPE_STRING && handle) {
        String* s = it2s(text_arg);
        if (s && s->len > 0) {
            TextExtents ext = font_measure_text(handle, s->chars, s->len);
            width = ext.width;
        }
    } else if (get_type_id(text_arg) == LMD_TYPE_STRING) {
        // fallback: no font loaded, return 0
        width = 0.0f;
    }

    // return TextMetrics object: { width }
    Item result = js_new_object();
    Item wk = (Item){.item = s2it(heap_create_name("width"))};
    double* dp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *dp = (double)width;
    Item wv = (Item){.item = d2it(dp)};
    js_property_set(result, wk, wv);
    return result;
}

// ============================================================================
// Method dispatch — called from js_map_method in js_runtime.cpp
// ============================================================================

extern "C" bool js_canvas_method_dispatch(Item obj, Item method_name, Item* args, int argc, Item* result) {
    // only handle MAP objects
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;
    // check __class_name__
    bool has_cls = false;
    Item cls = js_map_get_fast_ext(obj.map, "__class_name__", 14, &has_cls);
    if (!has_cls || get_type_id(cls) != LMD_TYPE_STRING) return false;

    String* cname = it2s(cls);
    if (!cname) return false;

    String* method = it2s(method_name);
    if (!method) return false;

    // OffscreenCanvas.getContext("2d")
    if (cname->len == 15 && memcmp(cname->chars, "OffscreenCanvas", 15) == 0) {
        if (method->len == 10 && memcmp(method->chars, "getContext", 10) == 0) {
            *result = js_canvas_get_context(obj);
            return true;
        }
        return false;
    }

    // CanvasRenderingContext2D methods
    if (cname->len == 24 && memcmp(cname->chars, "CanvasRenderingContext2D", 24) == 0) {
        if (method->len == 11 && memcmp(method->chars, "measureText", 11) == 0) {
            Item text = argc > 0 ? args[0] : ItemNull;
            *result = js_canvas_measure_text(obj, text);
            return true;
        }
        return false;
    }

    return false;
}

// ============================================================================
// Property set interception — for ctx.font = "..."
// ============================================================================

extern "C" bool js_canvas_property_set_intercept(Item obj, Item key, Item value) {
    // only intercept CanvasRenderingContext2D.font
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;

    // reentrancy guard — prevent infinite recursion when js_canvas_ctx_set_font
    // calls js_property_set internally
    static bool s_in_intercept = false;
    if (s_in_intercept) return false;

    bool has_cls = false;
    Item cls = js_map_get_fast_ext(obj.map, "__class_name__", 14, &has_cls);
    if (!has_cls || get_type_id(cls) != LMD_TYPE_STRING) return false;

    String* cname = it2s(cls);
    if (!cname || cname->len != 24 || memcmp(cname->chars, "CanvasRenderingContext2D", 24) != 0)
        return false;

    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* kname = it2s(key);
    if (!kname || kname->len != 4 || memcmp(kname->chars, "font", 4) != 0)
        return false;

    // intercept: resolve font handle
    s_in_intercept = true;
    js_canvas_ctx_set_font(obj, value);
    s_in_intercept = false;
    return true;
}

// ============================================================================
// Cleanup
// ============================================================================

extern "C" void js_canvas_cleanup(void) {
    for (int i = 0; i < s_font_handle_count; i++) {
        if (s_font_handles[i]) {
            font_handle_release(s_font_handles[i]);
            s_font_handles[i] = nullptr;
        }
    }
    s_font_handle_count = 0;

    if (s_canvas_font_ctx) {
        font_context_destroy(s_canvas_font_ctx);
        s_canvas_font_ctx = nullptr;
    }
}
