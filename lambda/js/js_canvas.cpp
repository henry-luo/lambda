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
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/heap_api.h"
#include "../../lib/font/font.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>


// ============================================================================
// Lazy FontContext singleton — no UiContext/GLFW required
// ============================================================================

#define MAX_CANVAS_FONT_HANDLES 64

struct JsCanvasRuntimeState {
    FontContext* font_context;
    FontHandle* font_handles[MAX_CANVAS_FONT_HANDLES];
    int font_handle_count;
};

static JsCanvasRuntimeState* canvas_runtime_state(void) {
    return js_active_runtime_state ?
        (JsCanvasRuntimeState*)js_runtime_state.canvas_state : NULL;
}

static JsCanvasRuntimeState* canvas_runtime_state_ensure(void) {
    if (!js_active_runtime_state) return NULL;
    if (!js_runtime_state.canvas_state) {
        // Canvas setup is cold; measurement reads this context-owned table
        // directly and never contends with another JS realm.
        js_runtime_state.canvas_state = mem_calloc(1, sizeof(JsCanvasRuntimeState),
            MEM_CAT_JS_RUNTIME);
    }
    return (JsCanvasRuntimeState*)js_runtime_state.canvas_state;
}

static FontContext* canvas_get_font_context() {
    JsCanvasRuntimeState* state = canvas_runtime_state_ensure();
    if (!state) return nullptr;
    if (!state->font_context) {
        FontContextConfig cfg = {};
        cfg.pixel_ratio = 1.0f;
        cfg.max_cached_faces = 32;
        state->font_context = font_context_create(&cfg);
        if (!state->font_context) {
            log_error("js_canvas: failed to create FontContext");
        }
    }
    return state->font_context;
}

// ============================================================================
// Font handle pool — store FontHandle* indexed by integer ID
// ============================================================================

static int canvas_store_font_handle(FontHandle* handle) {
    if (!handle) return -1;
    JsCanvasRuntimeState* state = canvas_runtime_state_ensure();
    if (!state) {
        font_handle_release(handle);
        return -1;
    }
    // check for reuse of existing identical handle
    for (int i = 0; i < state->font_handle_count; i++) {
        if (state->font_handles[i] == handle) {
            font_handle_release(handle); // already retained by pool
            return i;
        }
    }
    if (state->font_handle_count >= MAX_CANVAS_FONT_HANDLES) {
        // evict oldest
        font_handle_release(state->font_handles[0]);
        for (int i = 1; i < MAX_CANVAS_FONT_HANDLES; i++)
            state->font_handles[i - 1] = state->font_handles[i];
        state->font_handle_count = MAX_CANVAS_FONT_HANDLES - 1;
    }
    int id = state->font_handle_count++;
    state->font_handles[id] = handle;
    return id;
}

static FontHandle* canvas_get_font_handle(int id) {
    JsCanvasRuntimeState* state = canvas_runtime_state();
    if (!state || id < 0 || id >= state->font_handle_count) return nullptr;
    return state->font_handles[id];
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

static float canvas_font_size_px(Item ctx_obj) {
    Item font_str = js_get_name_key(ctx_obj, "font");
    if (get_type_id(font_str) != LMD_TYPE_STRING) return 16.0f;
    String* s = it2s(font_str);
    if (!s || s->len <= 0) return 16.0f;
    char buf[512];
    int len = s->len >= (int)sizeof(buf) ? (int)sizeof(buf) - 1 : (int)s->len;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    char* p = buf;
    while (*p) {
        if (isdigit((unsigned char)*p) || *p == '.') {
            char* end;
            float size_px = strtof(p, &end);
            if (end > p && strncmp(end, "px", 2) == 0) return size_px;
        }
        p++;
    }
    return 16.0f;
}

static float canvas_fallback_text_width(Item ctx_obj, String* s) {
    if (!s || s->len <= 0) return 0.0f;
    float size_px = canvas_font_size_px(ctx_obj);
    return (float)s->len * size_px * 0.5f;
}

// ============================================================================
// OffscreenCanvas constructor
// ============================================================================

extern "C" Item js_offscreen_canvas_new(Item width_arg, Item height_arg) {
    Item obj = js_new_object_with_class(JS_CLASS_OFFSCREEN_CANVAS);

    // store width/height
    int64_t w = (get_type_id(width_arg) == LMD_TYPE_INT) ? it2i(width_arg) : 300;
    int64_t h = (get_type_id(height_arg) == LMD_TYPE_INT) ? it2i(height_arg) : 150;
    Item wk = js_name_item("width");
    Item hk = js_name_item("height");
    js_set_key_default(obj, wk, (Item){.item = i2it(w)});
    js_set_key_default(obj, hk, (Item){.item = i2it(h)});

    return obj;
}

// ============================================================================
// CanvasRenderingContext2D — getContext("2d") result
// ============================================================================

extern "C" Item js_canvas_get_context(Item canvas) {
    Item obj = js_new_object_with_class(JS_CLASS_CANVAS_RENDERING_CONTEXT_2D);

    // store canvas reference
    js_set_name_key(obj, "canvas", canvas);

    // initial font property (CSS default)
    Item font_key = js_name_item("font");
    Item font_val = js_name_item("10px sans-serif");
    js_set_key_default(obj, font_key, font_val);

    // no font handle yet — will be resolved on first measureText or when font is set
    js_set_name_key(obj, "__font_handle_id", (Item){.item = i2it(-1)});

    return obj;
}

// ============================================================================
// ctx.font setter — resolve font handle when font property changes
// ============================================================================

extern "C" void js_canvas_ctx_set_font(Item ctx_obj, Item font_val) {
    // store the font string
    js_set_name_key(ctx_obj, "font", font_val);

    // resolve font handle
    if (get_type_id(font_val) != LMD_TYPE_STRING) return;
    String* s = it2s(font_val);
    if (!s || s->len == 0) return;

    FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
    if (handle) {
        int id = canvas_store_font_handle(handle);
        js_set_name_key(ctx_obj, "__font_handle_id", (Item){.item = i2it(id)});
    }
}

// ============================================================================
// ctx.measureText(text) → { width }
// ============================================================================

extern "C" Item js_canvas_measure_text(Item ctx_obj, Item text_arg) {
    // get font handle ID
    Item fh_key = js_name_item("__font_handle_id");
    Item fh_val = js_get_key_default(ctx_obj, fh_key);
    int fh_id = -1;
    if (get_type_id(fh_val) == LMD_TYPE_INT) {
        fh_id = (int)it2i(fh_val);
    }

    // if no font handle, try to resolve from current font string
    if (fh_id < 0) {
        Item font_str = js_get_name_key(ctx_obj, "font");
        if (get_type_id(font_str) == LMD_TYPE_STRING) {
            String* s = it2s(font_str);
            if (s && s->len > 0) {
                FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
                if (handle) {
                    fh_id = canvas_store_font_handle(handle);
                    js_set_key_default(ctx_obj, fh_key, (Item){.item = i2it(fh_id)});
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
            if (width <= 0.0f) width = canvas_fallback_text_width(ctx_obj, s);
        }
    } else if (get_type_id(text_arg) == LMD_TYPE_STRING) {
        String* s = it2s(text_arg);
        width = canvas_fallback_text_width(ctx_obj, s);
    }

    // return TextMetrics object: { width }
    Item result = js_new_object();
    Item wk = js_name_item("width");
    Item wv = push_d((double)width);
    js_set_key_default(result, wk, wv);
    return result;
}

// ============================================================================
// Property set interception — for ctx.font = "..."
// ============================================================================

extern "C" bool js_canvas_property_set_intercept(Item obj, Item key, Item value) {
    // only intercept CanvasRenderingContext2D.font
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;

    // reentrancy guard — prevent infinite recursion when js_canvas_ctx_set_font
    // calls js_set_key_default internally
    static bool s_in_intercept = false;
    if (s_in_intercept) return false;

    if (js_class_id(obj) != JS_CLASS_CANVAS_RENDERING_CONTEXT_2D) return false;

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
    JsCanvasRuntimeState* state = canvas_runtime_state();
    if (!state) return;
    for (int i = 0; i < state->font_handle_count; i++) {
        if (state->font_handles[i]) {
            font_handle_release(state->font_handles[i]);
            state->font_handles[i] = nullptr;
        }
    }
    state->font_handle_count = 0;

    if (state->font_context) {
        font_context_destroy(state->font_context);
        state->font_context = nullptr;
    }
}

extern "C" void js_canvas_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->canvas_state) return;
    JsCanvasRuntimeState* state = (JsCanvasRuntimeState*)runtime_state->canvas_state;
    for (int i = 0; i < state->font_handle_count; i++) {
        if (state->font_handles[i]) font_handle_release(state->font_handles[i]);
    }
    if (state->font_context) font_context_destroy(state->font_context);
    mem_free(state);
    runtime_state->canvas_state = NULL;
}
