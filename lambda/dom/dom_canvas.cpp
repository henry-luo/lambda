/**
 * js_canvas.cpp — Canvas WebIDL bindings for the Radiant 2D surface
 *
 * Provides font-aware measurement and a bounded Canvas 2D command surface.
 * HTML canvas drawing is delegated through the Radiant DOM waist; this file
 * never receives a renderer-owned pointer (D7.4.1v2, D7.5.3).
 * The standalone OffscreenCanvas compatibility object remains measurement-only:
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
#include "../module/radiant/radiant_dom_bridge.hpp"
#include "../input/css/css_style.hpp"
#include "dom_observers.h"
#include "../../lib/font/font.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>


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

static bool s_in_canvas_property_intercept = false;

static void* js_canvas_context_element(Item context_item) {
    if (!js_canvas_is_2d_context(context_item)) return nullptr;
    Item canvas = dom_realm_get_name(context_item, "canvas");
    if (!dom_is_html_canvas_element(canvas)) return nullptr;
    return dom_unwrap_element(canvas);
}

static Item js_canvas_number_arg(Item* args, int argc, int index, float* out) {
    if (!out) return js_throw_type_error("Canvas numeric result is unavailable");
    Item input = args && index < argc ? args[index] : make_js_undefined();
    Item numeric = js_to_number(input);
    if (item_is_error(numeric)) return numeric;
    TypeId type = get_type_id(numeric);
    if (type == LMD_TYPE_INT) *out = (float)it2i(numeric);
    else if (type == LMD_TYPE_INT64) *out = (float)it2l(numeric);
    else if (type == LMD_TYPE_FLOAT) *out = (float)it2d(numeric);
    else *out = NAN;
    return ItemNull;
}

static bool js_canvas_numbers_are_finite(const float* values, int count) {
    for (int index = 0; index < count; index++) {
        if (!isfinite(values[index])) return false;
    }
    return true;
}

static Item js_canvas_draw_failed(void) {
    return js_throw_range_error("Canvas backing surface is unavailable");
}

static void js_canvas_note_paint(void* canvas_element) {
    dom_notify_mutation(DOM_JS_MUTATION_STYLE_REPAINT,
                        canvas_element, canvas_element);
}

extern "C" void js_canvas_ctx_set_font(Item ctx_obj, Item font_val);
static void js_canvas_apply_native_state(Item context);

static Item js_canvas_color_item(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    static const char hex[] = "0123456789abcdef";
    char color[10] = {'#', '0', '0', '0', '0', '0', '0', '0', '0', '\0'};
    color[1] = hex[r >> 4]; color[2] = hex[r & 15];
    color[3] = hex[g >> 4]; color[4] = hex[g & 15];
    color[5] = hex[b >> 4]; color[6] = hex[b & 15];
    if (a == 255) {
        color[7] = '\0';
    } else {
        color[7] = hex[a >> 4]; color[8] = hex[a & 15];
    }
    return js_name_item(color);
}

static const char* js_canvas_line_cap_name(uint8_t cap) {
    return cap == 1 ? "round" : cap == 2 ? "square" : "butt";
}

static const char* js_canvas_line_join_name(uint8_t join) {
    return join == 1 ? "round" : join == 2 ? "bevel" : "miter";
}

static const char* js_canvas_text_align_name(uint8_t align) {
    switch (align) {
    case 1: return "end";
    case 2: return "left";
    case 3: return "right";
    case 4: return "center";
    default: return "start";
    }
}

static Item js_canvas_context_save(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_save(element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_restore(Item callee, Item this_value,
                                      Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!radiant_canvas_restore(element)) return js_canvas_draw_failed();
    js_canvas_apply_native_state(this_value);
    return make_js_undefined();
}

static Item js_canvas_context_scale(Item callee, Item this_value,
                                    Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[2] = {};
    for (int index = 0; index < 2; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 2)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_scale(element, values[0], values[1])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_translate(Item callee, Item this_value,
                                        Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[2] = {};
    for (int index = 0; index < 2; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 2)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_translate(element, values[0], values[1])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_rotate(Item callee, Item this_value,
                                     Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float radians = 0.0f;
    Item error = js_canvas_number_arg(args, argc, 0, &radians);
    if (item_is_error(error)) return error;
    if (!isfinite(radians)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_rotate(element, radians) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_begin_path(Item callee, Item this_value,
                                         Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_begin_path(element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_close_path(Item callee, Item this_value,
                                         Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_close_path(element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_move_to(Item callee, Item this_value,
                                      Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[2] = {};
    for (int index = 0; index < 2; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 2)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_move_to(element, values[0], values[1])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_line_to(Item callee, Item this_value,
                                      Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[2] = {};
    for (int index = 0; index < 2; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 2)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_line_to(element, values[0], values[1])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_bezier_curve_to(Item callee, Item this_value,
                                              Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[6] = {};
    for (int index = 0; index < 6; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 6)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_bezier_curve_to(element, values[0], values[1], values[2],
                                          values[3], values[4], values[5])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_quadratic_curve_to(Item callee, Item this_value,
                                                  Item* args, int argc,
                                                  uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[4] = {};
    for (int index = 0; index < 4; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 4)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_quadratic_curve_to(element, values[0], values[1],
                                              values[2], values[3])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_path_rect(Item callee, Item this_value,
                                        Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[4] = {};
    for (int index = 0; index < 4; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 4)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_rect(element, values[0], values[1], values[2], values[3])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_arc(Item callee, Item this_value,
                                  Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    float values[5] = {};
    for (int index = 0; index < 5; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 5)) return make_js_undefined();
    if (values[2] < 0.0f) return js_throw_range_error("Canvas arc radius must be non-negative");
    bool counter_clockwise = args && argc > 5 && js_is_truthy(args[5]);
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_arc(element, values[0], values[1], values[2], values[3],
                              values[4], counter_clockwise)
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_fill(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!radiant_canvas_fill(element)) return js_canvas_draw_failed();
    js_canvas_note_paint(element);
    return make_js_undefined();
}

static Item js_canvas_context_stroke(Item callee, Item this_value,
                                     Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!radiant_canvas_stroke(element)) return js_canvas_draw_failed();
    js_canvas_note_paint(element);
    return make_js_undefined();
}

static Item js_canvas_context_clip(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    if (args && argc > 0 && get_type_id(args[0]) == LMD_TYPE_STRING) {
        String* rule = it2s(args[0]);
        if (rule && rule->len == 7 && memcmp(rule->chars, "evenodd", 7) == 0) {
            return js_throw_type_error("Canvas evenodd clipping is not implemented");
        }
    }
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return radiant_canvas_clip(element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_rect(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home,
                                   bool stroke, bool clear) {
    (void)callee; (void)result_home;
    float values[4] = {};
    for (int index = 0; index < 4; index++) {
        Item error = js_canvas_number_arg(args, argc, index, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 4)) return make_js_undefined();
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    bool drawn = clear ? radiant_canvas_clear_rect(element, values[0], values[1],
                                                    values[2], values[3])
        : stroke ? radiant_canvas_stroke_rect(element, values[0], values[1],
                                               values[2], values[3])
        : radiant_canvas_fill_rect(element, values[0], values[1],
                                   values[2], values[3]);
    if (!drawn) return js_canvas_draw_failed();
    js_canvas_note_paint(element);
    return make_js_undefined();
}

static Item js_canvas_context_fill_rect(Item callee, Item this_value,
                                        Item* args, int argc, uint64_t* result_home) {
    return js_canvas_context_rect(callee, this_value, args, argc, result_home, false, false);
}

static Item js_canvas_context_stroke_rect(Item callee, Item this_value,
                                          Item* args, int argc, uint64_t* result_home) {
    return js_canvas_context_rect(callee, this_value, args, argc, result_home, true, false);
}

static Item js_canvas_context_clear_rect(Item callee, Item this_value,
                                         Item* args, int argc, uint64_t* result_home) {
    return js_canvas_context_rect(callee, this_value, args, argc, result_home, false, true);
}

static Item js_canvas_context_fill_text(Item callee, Item this_value,
                                        Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    RootFrame roots(3);
    Rooted<Item> context_root(roots, this_value);
    Rooted<Item> text_root(roots, argc > 0 && args
        ? js_to_string(args[0]) : js_name_item("undefined"));
    Rooted<Item> handle_id_root(roots,
        dom_realm_get_name(context_root.get(), "__font_handle_id"));
    if (item_is_error(text_root.get())) return text_root.get();
    String* text = get_type_id(text_root.get()) == LMD_TYPE_STRING ? it2s(text_root.get()) : nullptr;
    if (!text) return js_throw_type_error("Canvas text is unavailable");

    float values[2] = {};
    for (int index = 0; index < 2; index++) {
        Item error = js_canvas_number_arg(args, argc, index + 1, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, 2)) return make_js_undefined();
    float max_width = NAN;
    if (argc > 3) {
        Item error = js_canvas_number_arg(args, argc, 3, &max_width);
        if (item_is_error(error)) return error;
        if (!isfinite(max_width) || max_width <= 0.0f) return make_js_undefined();
    }
    void* element = js_canvas_context_element(context_root.get());
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    JsCanvasRuntimeState* state = canvas_runtime_state();
    int handle_id = get_type_id(handle_id_root.get()) == LMD_TYPE_INT
        ? (int)it2i(handle_id_root.get()) : -1; // INT_CAST_OK: FontHandle pool index.
    if (!state || handle_id < 0 || handle_id >= state->font_handle_count ||
        !state->font_handles[handle_id]) {
        return js_throw_range_error("Canvas font resource is unavailable");
    }
    if (!radiant_canvas_fill_text(element, state->font_handles[handle_id], text->chars,
                                  text->len, values[0], values[1], max_width)) {
        return js_canvas_draw_failed();
    }
    js_canvas_note_paint(element);
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
        {"scale", js_canvas_context_scale, 2},
        {"translate", js_canvas_context_translate, 2},
        {"rotate", js_canvas_context_rotate, 1},
        {"beginPath", js_canvas_context_begin_path, 0},
        {"closePath", js_canvas_context_close_path, 0},
        {"moveTo", js_canvas_context_move_to, 2},
        {"lineTo", js_canvas_context_line_to, 2},
        {"bezierCurveTo", js_canvas_context_bezier_curve_to, 6},
        {"quadraticCurveTo", js_canvas_context_quadratic_curve_to, 4},
        {"rect", js_canvas_context_path_rect, 4},
        {"arc", js_canvas_context_arc, 6},
        {"fill", js_canvas_context_fill, 0},
        {"stroke", js_canvas_context_stroke, 0},
        {"clip", js_canvas_context_clip, 0},
        {"save", js_canvas_context_save, 0},
        {"restore", js_canvas_context_restore, 0},
        {"fillRect", js_canvas_context_fill_rect, 4},
        {"strokeRect", js_canvas_context_stroke_rect, 4},
        {"clearRect", js_canvas_context_clear_rect, 4},
        {"fillText", js_canvas_context_fill_text, 3},
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

    if (dom_is_html_canvas_element(canvas_root.get()) &&
        !radiant_canvas_ensure(dom_unwrap_element(canvas_root.get()))) {
        return ItemNull;
    }

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
    js_canvas_install_context_methods(context_root.get());
    dom_realm_set_name(context_root.get(), "fillStyle", js_name_item("#000000"));
    dom_realm_set_name(context_root.get(), "strokeStyle", js_name_item("#000000"));
    dom_realm_set_name(context_root.get(), "lineWidth", flt2it(1.0f));
    dom_realm_set_name(context_root.get(), "globalAlpha", flt2it(1.0f));
    dom_realm_set_name(context_root.get(), "lineCap", js_name_item("butt"));
    dom_realm_set_name(context_root.get(), "lineJoin", js_name_item("miter"));
    dom_realm_set_name(context_root.get(), "textAlign", js_name_item("start"));
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

    void* element = js_canvas_context_element(ctx_obj);
    if (element) radiant_canvas_set_font(element, s->chars, s->len);

    FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
    if (handle) {
        int id = canvas_store_font_handle(handle);
        dom_realm_set_name(ctx_obj, "__font_handle_id", (Item){.item = i2it(id)});
    }
}

static void js_canvas_apply_native_state(Item context) {
    void* element = js_canvas_context_element(context);
    RadiantCanvasStateSnapshot state = {};
    if (!element || !radiant_canvas_get_state(element, &state)) return;
    RootFrame roots(2);
    Rooted<Item> context_root(roots, context);
    Rooted<Item> font_root(roots, js_name_item(state.font));
    // Restore's native frame is authoritative; project it without re-entering setters.
    bool was_intercepting = s_in_canvas_property_intercept;
    s_in_canvas_property_intercept = true;
    dom_realm_set_name(context_root.get(), "fillStyle",
                       js_canvas_color_item(state.fill_r, state.fill_g,
                                            state.fill_b, state.fill_a));
    dom_realm_set_name(context_root.get(), "strokeStyle",
                       js_canvas_color_item(state.stroke_r, state.stroke_g,
                                            state.stroke_b, state.stroke_a));
    dom_realm_set_name(context_root.get(), "lineWidth", flt2it(state.line_width));
    dom_realm_set_name(context_root.get(), "globalAlpha", flt2it(state.global_alpha));
    dom_realm_set_name(context_root.get(), "lineCap", js_name_item(
        js_canvas_line_cap_name(state.line_cap)));
    dom_realm_set_name(context_root.get(), "lineJoin", js_name_item(
        js_canvas_line_join_name(state.line_join)));
    dom_realm_set_name(context_root.get(), "textAlign", js_name_item(
        js_canvas_text_align_name(state.text_align)));
    js_canvas_ctx_set_font(context_root.get(), font_root.get());
    s_in_canvas_property_intercept = was_intercepting;
}

extern "C" void dom_canvas_reset_context(Item canvas) {
    RootFrame roots(3);
    Rooted<Item> canvas_root(roots, canvas);
    Rooted<Item> context_root(roots,
        dom_realm_get_name(canvas_root.get(), "__lambda_canvas_2d_context"));
    Rooted<Item> font_root(roots, js_name_item("10px sans-serif"));
    if (!js_canvas_is_2d_context(context_root.get())) return;

    // The bitmap reset is owned by Radiant; mirror the specified JS state so
    // the context's observable defaults cannot diverge from that native state.
    s_in_canvas_property_intercept = true;
    dom_realm_set_name(context_root.get(), "fillStyle", js_name_item("#000000"));
    dom_realm_set_name(context_root.get(), "strokeStyle", js_name_item("#000000"));
    dom_realm_set_name(context_root.get(), "lineWidth", flt2it(1.0f));
    dom_realm_set_name(context_root.get(), "globalAlpha", flt2it(1.0f));
    dom_realm_set_name(context_root.get(), "lineCap", js_name_item("butt"));
    dom_realm_set_name(context_root.get(), "lineJoin", js_name_item("miter"));
    dom_realm_set_name(context_root.get(), "textAlign", js_name_item("start"));
    js_canvas_ctx_set_font(context_root.get(), font_root.get());
    s_in_canvas_property_intercept = false;
}

// ============================================================================
// ctx.measureText(text) → { width }
// ============================================================================

// ============================================================================
// Property set interception — for ctx.font = "..."
// ============================================================================

extern "C" bool js_canvas_property_set_intercept(Item obj, Item key, Item value) {
    // Keep context state native while property reads retain ordinary JS values.
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;

    // Prevent infinite recursion when a state write updates the JS projection.
    if (s_in_canvas_property_intercept) return false;

    if (js_class_id(obj) != JS_CLASS_CANVAS_RENDERING_CONTEXT_2D) return false;

    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* kname = it2s(key);
    if (!kname) return false;

    s_in_canvas_property_intercept = true;
    if (kname->len == 4 && memcmp(kname->chars, "font", 4) == 0) {
        js_canvas_ctx_set_font(obj, value);
        s_in_canvas_property_intercept = false;
        return true;
    }

    void* element = js_canvas_context_element(obj);
    if (!element) {
        s_in_canvas_property_intercept = false;
        return false;
    }

    if ((kname->len == 9 && memcmp(kname->chars, "fillStyle", 9) == 0) ||
        (kname->len == 11 && memcmp(kname->chars, "strokeStyle", 11) == 0)) {
        bool fill = kname->len == 9;
        bool valid = false;
        if (get_type_id(value) == LMD_TYPE_STRING) {
            String* text = it2s(value);
            CssColor color = {};
            valid = text && css_parse_color(text->chars, &color) &&
                color.type != CSS_COLOR_CURRENT;
            if (valid) {
                valid = fill
                    ? radiant_canvas_set_fill_color(element, color.r, color.g, color.b, color.a)
                    : radiant_canvas_set_stroke_color(element, color.r, color.g, color.b, color.a);
            }
        }
        if (valid) dom_realm_set_name(obj, fill ? "fillStyle" : "strokeStyle", value);
        s_in_canvas_property_intercept = false;
        return true;
    }

    if (kname->len == 9 && memcmp(kname->chars, "lineWidth", 9) == 0) {
        float width = 0.0f;
        Item numeric = js_to_number(value);
        bool valid = !item_is_error(numeric);
        if (valid) {
            TypeId type = get_type_id(numeric);
            width = type == LMD_TYPE_INT ? (float)it2i(numeric)
                : type == LMD_TYPE_INT64 ? (float)it2l(numeric)
                : type == LMD_TYPE_FLOAT ? (float)it2d(numeric) : NAN;
            valid = radiant_canvas_set_line_width(element, width);
        }
        if (valid) dom_realm_set_name(obj, "lineWidth", flt2it(width));
        s_in_canvas_property_intercept = false;
        return true;
    }

    if (kname->len == 11 && memcmp(kname->chars, "globalAlpha", 11) == 0) {
        float alpha = 0.0f;
        Item numeric = js_to_number(value);
        bool valid = !item_is_error(numeric);
        if (valid) {
            TypeId type = get_type_id(numeric);
            alpha = type == LMD_TYPE_INT ? (float)it2i(numeric)
                : type == LMD_TYPE_INT64 ? (float)it2l(numeric)
                : type == LMD_TYPE_FLOAT ? (float)it2d(numeric) : NAN;
            valid = radiant_canvas_set_global_alpha(element, alpha);
        }
        if (valid) dom_realm_set_name(obj, "globalAlpha", flt2it(alpha));
        s_in_canvas_property_intercept = false;
        return true;
    }

    if (kname->len == 7 && memcmp(kname->chars, "lineCap", 7) == 0) {
        uint8_t cap = 0;
        bool valid = get_type_id(value) == LMD_TYPE_STRING;
        String* text = valid ? it2s(value) : nullptr;
        if (text && text->len == 5 && memcmp(text->chars, "round", 5) == 0) cap = 1;
        else if (text && text->len == 6 && memcmp(text->chars, "square", 6) == 0) cap = 2;
        else if (!text || text->len != 4 || memcmp(text->chars, "butt", 4) != 0) valid = false;
        valid = valid && radiant_canvas_set_line_cap(element, cap);
        if (valid) dom_realm_set_name(obj, "lineCap", js_name_item(js_canvas_line_cap_name(cap)));
        s_in_canvas_property_intercept = false;
        return true;
    }

    if (kname->len == 8 && memcmp(kname->chars, "lineJoin", 8) == 0) {
        uint8_t join = 0;
        bool valid = get_type_id(value) == LMD_TYPE_STRING;
        String* text = valid ? it2s(value) : nullptr;
        if (text && text->len == 5 && memcmp(text->chars, "round", 5) == 0) join = 1;
        else if (text && text->len == 5 && memcmp(text->chars, "bevel", 5) == 0) join = 2;
        else if (!text || text->len != 5 || memcmp(text->chars, "miter", 5) != 0) valid = false;
        valid = valid && radiant_canvas_set_line_join(element, join);
        if (valid) dom_realm_set_name(obj, "lineJoin", js_name_item(js_canvas_line_join_name(join)));
        s_in_canvas_property_intercept = false;
        return true;
    }

    if (kname->len == 9 && memcmp(kname->chars, "textAlign", 9) == 0) {
        uint8_t align = 0;
        bool valid = get_type_id(value) == LMD_TYPE_STRING;
        String* text = valid ? it2s(value) : nullptr;
        if (text && text->len == 3 && memcmp(text->chars, "end", 3) == 0) align = 1;
        else if (text && text->len == 4 && memcmp(text->chars, "left", 4) == 0) align = 2;
        else if (text && text->len == 5 && memcmp(text->chars, "right", 5) == 0) align = 3;
        else if (text && text->len == 6 && memcmp(text->chars, "center", 6) == 0) align = 4;
        else if (!text || text->len != 5 || memcmp(text->chars, "start", 5) != 0) valid = false;
        valid = valid && radiant_canvas_set_text_align(element, align);
        if (valid) dom_realm_set_name(obj, "textAlign", js_name_item(js_canvas_text_align_name(align)));
        s_in_canvas_property_intercept = false;
        return true;
    }

    s_in_canvas_property_intercept = false;
    return false;
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
