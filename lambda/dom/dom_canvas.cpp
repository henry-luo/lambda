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

#include "../js/js_runtime.h"
#include "dom.h"
#include "dom_canvas.h"
#include "realm/dom_realm.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_class.h"
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
        (JsCanvasRuntimeState*)context_capsule(context, CONTEXT_CAPSULE_DOM_CANVAS) : NULL;
}

extern __thread EvalContext* context;
static void js_canvas_capsule_destroy(void* capsule);
static const ContextCapsuleOps js_canvas_capsule_ops = {
    "js-canvas", CONTEXT_CAPSULE_LIFETIME_REALM, sizeof(JsCanvasRuntimeState),
    NULL, NULL, js_canvas_capsule_destroy
};

static JsCanvasRuntimeState* canvas_runtime_state_ensure(void) {
    if (!js_active_runtime_state) return NULL;
    // Canvas setup is cold; measurement reads this context-owned table
    // directly and never contends with another JS realm.
    return (JsCanvasRuntimeState*)context_capsule_ensure(
        context, CONTEXT_CAPSULE_DOM_CANVAS, &js_canvas_capsule_ops);
}

static bool js_canvas_is_2d_context(Item item) {
    return get_type_id(item) == LMD_TYPE_MAP &&
        js_class_id(item) == JS_CLASS_CANVAS_RENDERING_CONTEXT_2D;
}

static Item js_canvas_context_noop(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee;
    (void)this_value;
    (void)args;
    (void)argc;
    (void)result_home;
    return make_js_undefined();
}

static Item js_canvas_measure_text(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee;
    (void)result_home;
    RootFrame roots(3);
    Rooted<Item> context_root(roots, this_value);
    Rooted<Item> text_root(roots, argc > 0 && args
        ? js_to_string(args[0]) : js_name_item("undefined"));
    Rooted<Item> metrics_root(roots, js_new_object());
    if (item_is_error(text_root.get())) return text_root.get();

    String* text = get_type_id(text_root.get()) == LMD_TYPE_STRING
        ? it2s(text_root.get()) : nullptr;
    float width = 0.0f;
    Item handle_id = dom_realm_get_name(context_root.get(), "__font_handle_id");
    JsCanvasRuntimeState* state = canvas_runtime_state();
    if (text && get_type_id(handle_id) == LMD_TYPE_INT && state) {
        int id = (int)it2i(handle_id); // INT_CAST_OK: FontHandle pool index
        if (id >= 0 && id < state->font_handle_count && state->font_handles[id]) {
            width = font_measure_text(state->font_handles[id], text->chars,
                text->len).width;
        }
    }
    if (text && width <= 0.0f) {
        width = (float)text->len * 8.0f;
    }
    dom_realm_set_name(metrics_root.get(), "width", flt2it(width));
    return metrics_root.get();
}

static void js_canvas_install_context_methods(Item context) {
    struct CanvasMethodSpec {
        const char* name;
        JsNativeCallBody body;
        int formal_length;
    };
    static const CanvasMethodSpec methods[] = {
        {"scale", js_canvas_context_noop, 2},
        {"translate", js_canvas_context_noop, 2},
        {"rotate", js_canvas_context_noop, 1},
        {"beginPath", js_canvas_context_noop, 0},
        {"arc", js_canvas_context_noop, 6},
        {"stroke", js_canvas_context_noop, 0},
        {"save", js_canvas_context_noop, 0},
        {"fillRect", js_canvas_context_noop, 4},
        {"restore", js_canvas_context_noop, 0},
        {"clearRect", js_canvas_context_noop, 4},
        {"measureText", js_canvas_measure_text, 1},
    };
    RootFrame roots(2);
    Rooted<Item> context_root(roots, context);
    Rooted<Item> method_root(roots, ItemNull);
    for (int i = 0; i < (int)(sizeof(methods) / sizeof(methods[0])); i++) {
        method_root.set(js_new_native_payload_function(methods[i].body, 0,
            methods[i].formal_length));
        dom_realm_set_name(context_root.get(), methods[i].name, method_root.get());
    }
}

extern "C" void js_canvas_ctx_set_font(Item ctx_obj, Item font_val);

static Item js_canvas_2d_context_for(Item canvas) {
    RootFrame roots(7);
    Rooted<Item> canvas_root(roots, canvas);
    Rooted<Item> cached_root(roots,
        dom_realm_get_name(canvas_root.get(), "__lambda_canvas_2d_context"));
    Rooted<Item> context_root(roots, ItemNull);
    Rooted<Item> font_root(roots, js_name_item("10px sans-serif"));
    Rooted<Item> global_root(roots, js_get_global_this());
    Rooted<Item> constructor_root(roots, ItemNull);
    Rooted<Item> prototype_root(roots, ItemNull);
    if (js_canvas_is_2d_context(cached_root.get())) return cached_root.get();

    context_root.set(dom_realm_new_object_of_class(
        JS_CLASS_CANVAS_RENDERING_CONTEXT_2D));
    if (!js_canvas_is_2d_context(context_root.get())) return ItemNull;
    constructor_root.set(dom_realm_get_name(global_root.get(),
        "CanvasRenderingContext2D"));
    prototype_root.set(dom_realm_get_name(constructor_root.get(), "prototype"));
    if (get_type_id(prototype_root.get()) == LMD_TYPE_MAP) {
        // The branded native context still needs its realm-owned WebIDL prototype.
        js_set_prototype(context_root.get(), prototype_root.get());
    }
    dom_realm_set_name(context_root.get(), "canvas", canvas_root.get());
    js_canvas_ctx_set_font(context_root.get(), font_root.get());
    // Canvas commands do not affect DOM geometry; retain this narrow context
    // so chart libraries can finish their structural DOM updates.
    js_canvas_install_context_methods(context_root.get());
    dom_realm_set_name(canvas_root.get(), "__lambda_canvas_2d_context",
        context_root.get());
    return context_root.get();
}

static Item js_canvas_get_context(Item callee, Item this_value,
                                  Item* args, int argc, uint64_t* result_home) {
    (void)callee;
    (void)result_home;
    RootFrame roots(2);
    Rooted<Item> canvas_root(roots, this_value);
    Rooted<Item> identifier_root(roots, argc > 0 && args
        ? js_to_string(args[0]) : js_name_item("undefined"));
    if (!dom_is_html_canvas_element(canvas_root.get()) &&
        !(get_type_id(canvas_root.get()) == LMD_TYPE_MAP &&
          js_class_id(canvas_root.get()) == JS_CLASS_OFFSCREEN_CANVAS)) {
        return dom_realm_throw_type_error("Illegal invocation");
    }
    if (item_is_error(identifier_root.get())) return identifier_root.get();
    String* identifier = get_type_id(identifier_root.get()) == LMD_TYPE_STRING
        ? it2s(identifier_root.get()) : nullptr;
    if (!identifier || identifier->len != 2 ||
        memcmp(identifier->chars, "2d", 2) != 0) {
        return ItemNull;
    }
    return js_canvas_2d_context_for(canvas_root.get());
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
    RootFrame roots(2);
    Rooted<Item> obj_root(roots,
        dom_realm_new_object_of_class(JS_CLASS_OFFSCREEN_CANVAS));
    Rooted<Item> method_root(roots, js_new_native_payload_function(
        js_canvas_get_context, 0, 1));

    // store width/height
    int64_t w = (get_type_id(width_arg) == LMD_TYPE_INT) ? it2i(width_arg) : 300;
    int64_t h = (get_type_id(height_arg) == LMD_TYPE_INT) ? it2i(height_arg) : 150;
    Item wk = js_name_item("width");
    Item hk = js_name_item("height");
    dom_realm_set(obj_root.get(), wk, (Item){.item = i2it(w)});
    dom_realm_set(obj_root.get(), hk, (Item){.item = i2it(h)});
    dom_realm_set_name(obj_root.get(), "getContext", method_root.get());

    return obj_root.get();
}

extern "C" void dom_canvas_install_html_interface(Item html_canvas_prototype) {
    if (get_type_id(html_canvas_prototype) != LMD_TYPE_MAP) return;
    RootFrame roots(2);
    Rooted<Item> prototype_root(roots, html_canvas_prototype);
    Rooted<Item> method_root(roots, js_new_native_payload_function(
        js_canvas_get_context, 0, 1));
    dom_realm_set_name(prototype_root.get(), "getContext", method_root.get());
}

extern "C" void js_canvas_install_offscreen_canvas_interface(Item constructor) {
    if (!js_is_callable(constructor)) return;
    RootFrame roots(2);
    Rooted<Item> constructor_root(roots, constructor);
    Rooted<Item> prototype_root(roots,
        dom_realm_get_name(constructor_root.get(), "prototype"));
    if (get_type_id(prototype_root.get()) != LMD_TYPE_MAP) return;
    Rooted<Item> method_root(roots, js_new_native_payload_function(
        js_canvas_get_context, 0, 1));
    dom_realm_set_name(prototype_root.get(), "getContext", method_root.get());
}

// ============================================================================
// CanvasRenderingContext2D — getContext("2d") result
// ============================================================================

// ============================================================================
// ctx.font setter — resolve font handle when font property changes
// ============================================================================

extern "C" void js_canvas_ctx_set_font(Item ctx_obj, Item font_val) {
    // store the font string
    dom_realm_set_name(ctx_obj, "font", font_val);

    // resolve font handle
    if (get_type_id(font_val) != LMD_TYPE_STRING) return;
    String* s = it2s(font_val);
    if (!s || s->len == 0) return;

    FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
    if (handle) {
        int id = canvas_store_font_handle(handle);
        dom_realm_set_name(ctx_obj, "__font_handle_id", (Item){.item = i2it(id)});
    }
}

// ============================================================================
// ctx.measureText(text) → { width }
// ============================================================================

// ============================================================================
// Property set interception — for ctx.font = "..."
// ============================================================================

extern "C" bool js_canvas_property_set_intercept(Item obj, Item key, Item value) {
    // only intercept CanvasRenderingContext2D.font
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;

    // reentrancy guard — prevent infinite recursion when js_canvas_ctx_set_font
    // calls dom_realm_set internally
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

static void js_canvas_capsule_destroy(void* capsule) {
    JsCanvasRuntimeState* state = (JsCanvasRuntimeState*)capsule;
    for (int i = 0; i < state->font_handle_count; i++) {
        if (state->font_handles[i]) font_handle_release(state->font_handles[i]);
    }
    if (state->font_context) font_context_destroy(state->font_context);
    mem_free(state);
}
