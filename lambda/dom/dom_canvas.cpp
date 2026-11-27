/**
 * js_canvas.cpp — Canvas WebIDL bindings for the Radiant 2D surface
 *
 * Provides font-aware measurement and a bounded Canvas 2D command surface.
 * HTML canvas drawing is delegated through the Radiant DOM waist; this file
 * never receives a renderer-owned pointer (D9.5.1, D9.5.6).
 * The standalone OffscreenCanvas compatibility object remains measurement-only:
 *   new OffscreenCanvas(w, h)
 *   canvas.getContext("2d")
 *   ctx.font = "16px sans-serif"
 *   ctx.measureText(text) → { width }
 */

#include "../js/js_runtime.h"
#include "dom.h"
#include "dom_graphics.h"
#include "dom_core.h"
#include "dom_realm_hooks.h"
#include "dom_canvas.h"
#include "dom_webgl.h"
#include "realm/dom_realm.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_class.h"
#include "../js/js_function.hpp"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/heap_api.h"
#include "../runtime/root_vector.h"
#include "../js/js_typed_array.h"
#include "lambda/input/css/css_style.hpp"
#include "lambda/core/well_known_markup_names.h"
#include "dom_observers.h"
#include "lib/font/font.h"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "lib/strbuf.h"

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
    RootVector image_data_roots;
    Item image_data_slots[2]; // private brand key and intrinsic prototype
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
    JsCanvasRuntimeState* state = canvas_runtime_state();
    if (!state) {
        state = (JsCanvasRuntimeState*)context_capsule_ensure(
            context, CONTEXT_CAPSULE_DOM_CANVAS, &js_canvas_capsule_ops);
        if (state) root_vector_bind_external(&state->image_data_roots, (Context*)context,
            state->image_data_slots, 2, "Canvas ImageData intrinsics");
    }
    return state;
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

static Item js_canvas_number_value(Item input, double* out) {
    if (!out) return js_throw_type_error("Canvas numeric result is unavailable");
    Item numeric = js_to_number(input);
    if (item_is_error(numeric)) return numeric;
    TypeId type = get_type_id(numeric);
    if (type == LMD_TYPE_INT) *out = (double)it2i(numeric);
    else if (type == LMD_TYPE_INT64) *out = (double)it2l(numeric);
    else if (type == LMD_TYPE_FLOAT) *out = it2d(numeric);
    else *out = NAN;
    return ItemNull;
}

static Item js_canvas_number_arg(Item* args, int argc, int index, float* out) {
    double value = NAN;
    Item error = js_canvas_number_value(
        args && index < argc ? args[index] : make_js_undefined(), &value);
    if (!item_is_error(error)) *out = (float)value;
    return error;
}

static Item js_canvas_draw_failed(void);

static Item js_canvas_integer(Item input, bool unsigned_value, int64_t* out) {
    double value = NAN;
    Item error = js_canvas_number_value(input, &value);
    if (item_is_error(error)) return error;
    // constructor dimensions wrap as unsigned long; the pixel methods use EnforceRange.
    if (unsigned_value) {
        *out = (uint32_t)js_to_int32(value);
        return ItemNull;
    }
    value = trunc(value);
    if (!isfinite(value) || value < -2147483648.0 || value > 2147483647.0)
        return js_throw_type_error("Canvas integer is outside the WebIDL range");
    *out = (int64_t)value;
    return ItemNull;
}

static Item js_image_data_intrinsic(int slot) {
    JsCanvasRuntimeState* state = canvas_runtime_state_ensure();
    Item* value = state ? root_vector_external_at(&state->image_data_roots, slot) : nullptr;
    return value ? *value : ItemNull;
}

static Item js_image_data_state(Item receiver) {
    if (!dom_realm_object_has_class(receiver, JS_CLASS_WEB_API_RESOURCE) ||
        js_private_in(receiver, js_image_data_intrinsic(0)).item != ITEM_TRUE)
        return js_throw_type_error("Illegal ImageData receiver");
    return dom_realm_native_private_array_state(receiver, JS_CLASS_WEB_API_RESOURCE, 3,
        "Illegal ImageData receiver");
}

static Item js_image_data_settings(Item settings) {
    if (settings.item == ITEM_NULL || settings.item == ITEM_JS_UNDEFINED) return ItemNull;
    if (!js_is_object_value(settings)) return js_throw_type_error("ImageData settings must be a dictionary");
    RootFrame roots(2);
    Rooted<Item> options(roots, settings), value(roots, ItemNull);
    static const char* names[] = {"colorSpace", "pixelFormat"};
    static const char* supported[] = {"srgb", "rgba-unorm8"};
    static const char* unsupported[] = {"display-p3", "rgba-float16"};
    for (int index = 0; index < 2; index++) {
        value.set(dom_realm_get_name(options.get(), names[index]));
        if (item_is_error(value.get())) return value.get();
        if (value.get().item == ITEM_JS_UNDEFINED) continue;
        value.set(js_to_string(value.get()));
        if (item_is_error(value.get())) return value.get();
        String* text = it2s(value.get());
        if (text->len == strlen(supported[index]) && !memcmp(text->chars, supported[index], text->len)) continue;
        if (text->len == strlen(unsupported[index]) && !memcmp(text->chars, unsupported[index], text->len))
            return dom_raise_exception("NotSupportedError", "ImageData currently supports sRGB RGBA8 pixels");
        return js_throw_type_error("Invalid ImageData settings enumeration");
    }
    return ItemNull;
}

static Item js_image_data_create(Item data, uint32_t width, uint32_t height) {
    RootFrame roots(4);
    Rooted<Item> pixels(roots, data), record(roots, js_array_new(0));
    Rooted<Item> key(roots, js_image_data_intrinsic(0)), prototype(roots, js_image_data_intrinsic(1));
    JS_RETURN_IF_ERROR(js_array_push(record.get(), pixels.get()));
    JS_RETURN_IF_ERROR(js_array_push(record.get(), (Item){.item = i2it(width)}));
    JS_RETURN_IF_ERROR(js_array_push(record.get(), (Item){.item = i2it(height)}));
    // the private record owns the typed array; no GC pointer escapes into native canvas storage.
    return dom_realm_native_private_array_object(JS_CLASS_WEB_API_RESOURCE,
        key.get(), record.get(), prototype.get());
}

static Item js_image_data_allocate(uint32_t width, uint32_t height) {
    if (!width || !height) return dom_raise_exception("IndexSizeError", "ImageData dimensions must be non-zero");
    if ((uint64_t)width * height > RADIANT_CANVAS_MAX_PIXELS)
        return js_throw_range_error("ImageData exceeds the canvas pixel budget");
    RootFrame roots(1);
    Rooted<Item> pixels(roots, js_typed_array_new(JS_TYPED_UINT8_CLAMPED, (int)(width * height * 4)));
    if (item_is_error(pixels.get())) return pixels.get();
    return js_image_data_create(pixels.get(), width, height);
}

extern "C" Item dom_image_data_call(Item, Item, Item*, int, uint64_t*) {
    return js_throw_type_error("ImageData requires new");
}

extern "C" Item dom_image_data_construct(Item, Item* args, int argc, Item new_target, uint64_t*) {
    if (argc < 2) return js_throw_type_error("ImageData requires at least two arguments");
    RootFrame roots(5);
    Rooted<Item> input(roots, args[0]), target(roots, new_target), result(roots, ItemNull);
    Rooted<Item> prototype(roots, ItemNull), settings(roots, ItemNull);
    bool supplied = js_is_typed_array(input.get());
    int64_t width = 0, height = 0;
    JS_RETURN_IF_ERROR(js_canvas_integer(args[supplied ? 1 : 0], true, &width));
    bool explicit_height = !supplied || (argc > 2 && args[2].item != ITEM_JS_UNDEFINED);
    if (explicit_height) JS_RETURN_IF_ERROR(js_canvas_integer(args[supplied ? 2 : 1], true, &height));
    settings.set(argc > (supplied ? 3 : 2) ? args[supplied ? 3 : 2] : make_js_undefined());
    JS_RETURN_IF_ERROR(js_image_data_settings(settings.get()));
    if (supplied) {
        JsTypedArray* array = js_get_typed_array_ptr(input.get().map);
        if (!array || array->element_type != JS_TYPED_UINT8_CLAMPED)
            return js_throw_type_error("ImageData requires a Uint8ClampedArray");
        int length = js_typed_array_length(input.get());
        if (!length || length % 4 || js_arraybuffer_view_is_out_of_bounds(&array->base))
            return dom_raise_exception("InvalidStateError", "Invalid ImageData pixel storage");
        if (!width || (uint64_t)length % ((uint64_t)width * 4))
            return dom_raise_exception("IndexSizeError", "ImageData row length does not match its width");
        int64_t inferred = length / (width * 4);
        if (!explicit_height) height = inferred;
        if (height != inferred) return dom_raise_exception("IndexSizeError", "ImageData height does not match its storage");
        result.set(js_image_data_create(input.get(), (uint32_t)width, (uint32_t)height));
    } else result.set(js_image_data_allocate((uint32_t)width, (uint32_t)height));
    if (item_is_error(result.get())) return result.get();
    prototype.set(dom_realm_get_name(target.get(), "prototype"));
    if (item_is_error(prototype.get())) return prototype.get();
    if (js_is_object_value(prototype.get())) dom_realm_set_prototype(result.get(), prototype.get());
    return result.get();
}

static Item js_image_data_get(Item callee, Item receiver, Item*, int, uint64_t*) {
    RootFrame roots(1);
    Rooted<Item> record(roots, js_image_data_state(receiver));
    if (item_is_error(record.get())) return record.get();
    int slot = (int)js_fn_native((JsFunction*)callee.function)->target.bits;
    return slot < 3 ? js_elements_get_int(record.get(), slot)
        : js_name_item(slot == 3 ? "srgb" : "rgba-unorm8");
}

static Item js_canvas_get_image_data(Item, Item receiver, Item* args, int argc, uint64_t*) {
    RootFrame roots(3);
    Rooted<Item> owner(roots, receiver), image(roots, ItemNull), pixels(roots, ItemNull);
    void* element = js_canvas_context_element(owner.get());
    if (!element) return js_throw_type_error("Illegal CanvasRenderingContext2D receiver");
    if (argc < 4) return js_throw_type_error("getImageData requires four arguments");
    int64_t rect[4] = {};
    for (int index = 0; index < 4; index++) JS_RETURN_IF_ERROR(js_canvas_integer(args[index], false, &rect[index]));
    JS_RETURN_IF_ERROR(js_image_data_settings(argc > 4 ? args[4] : make_js_undefined()));
    if (rect[2] < 0) {rect[0] += rect[2]; rect[2] = -rect[2];}
    if (rect[3] < 0) {rect[1] += rect[3]; rect[3] = -rect[3];}
    image.set(js_image_data_allocate((uint32_t)rect[2], (uint32_t)rect[3]));
    if (item_is_error(image.get())) return image.get();
    pixels.set(js_elements_get_int(js_image_data_state(image.get()), 0));
    uint8_t* bytes = (uint8_t*)js_typed_array_prepare_write_ptr(pixels.get());
    // numeric/options coercion can retire the canvas; revalidate its wrapper after callbacks.
    element = js_canvas_context_element(owner.get());
    if (!bytes || !dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::read_pixels,
            element, rect[0], rect[1], (uint32_t)rect[2], (uint32_t)rect[3], bytes,
            (size_t)rect[2] * rect[3] * 4)) return js_canvas_draw_failed();
    return image.get();
}

static Item js_canvas_create_image_data(Item, Item receiver, Item* args, int argc, uint64_t*) {
    if (!js_canvas_is_2d_context(receiver))
        return js_throw_type_error("Illegal CanvasRenderingContext2D receiver");
    if (!argc) return js_throw_type_error("createImageData requires an ImageData or dimensions");
    if (argc == 1) {
        JS_ASSIGN_OR_RETURN(record, js_image_data_state(args[0]));
        uint32_t width = (uint32_t)it2i(js_elements_get_int(record, 1));
        uint32_t height = (uint32_t)it2i(js_elements_get_int(record, 2));
        // Cloning a shape allocates fresh transparent storage.
        return js_image_data_allocate(width, height);
    }
    int64_t width = 0, height = 0;
    JS_RETURN_IF_ERROR(js_canvas_integer(args[0], false, &width));
    JS_RETURN_IF_ERROR(js_canvas_integer(args[1], false, &height));
    JS_RETURN_IF_ERROR(js_image_data_settings(argc > 2 ? args[2] : make_js_undefined()));
    return js_image_data_allocate((uint32_t)(width < 0 ? -width : width),
        (uint32_t)(height < 0 ? -height : height));
}

static Item js_canvas_put_image_data(Item, Item receiver, Item* args, int argc, uint64_t*) {
    if (!js_canvas_is_2d_context(receiver))
        return js_throw_type_error("Illegal CanvasRenderingContext2D receiver");
    if (argc != 3 && argc < 7) return js_throw_type_error("putImageData requires three or seven arguments");
    RootFrame roots(3);
    Rooted<Item> owner(roots, receiver), record(roots, js_image_data_state(args[0]));
    JS_RETURN_IF_ERROR(record.get());
    Rooted<Item> pixels(roots, js_elements_get_int(record.get(), 0));
    int64_t width = it2i(js_elements_get_int(record.get(), 1));
    int64_t height = it2i(js_elements_get_int(record.get(), 2));
    int64_t rect[] = {0, 0, 0, 0, width, height};
    int count = argc >= 7 ? 6 : 2;
    for (int index = 0; index < count; ++index)
        JS_RETURN_IF_ERROR(js_canvas_integer(args[index + 1], false, &rect[index]));

    // Numeric coercion can detach the array. Resolve the view only after user code has run.
    void* element = js_canvas_context_element(owner.get());
    if (!element) return js_throw_type_error("Illegal CanvasRenderingContext2D receiver");
    const uint8_t* bytes = (const uint8_t*)js_typed_array_current_data_ptr(pixels.get());
    size_t byte_length = (size_t)js_typed_array_byte_length(pixels.get());
    if (!bytes || (uint64_t)width * height > byte_length / 4)
        return dom_raise_exception("InvalidStateError", "Invalid ImageData pixel storage");

    if (rect[4] < 0) { rect[2] += rect[4]; rect[4] = -rect[4]; }
    if (rect[5] < 0) { rect[3] += rect[5]; rect[5] = -rect[5]; }
    int64_t left = rect[2] > 0 ? rect[2] : 0, top = rect[3] > 0 ? rect[3] : 0;
    int64_t right = rect[2] + rect[4] < width ? rect[2] + rect[4] : width;
    int64_t bottom = rect[3] + rect[5] < height ? rect[3] + rect[5] : height;
    if (left >= right || top >= bottom) return make_js_undefined();
    size_t row_bytes = (size_t)width * 4;
    size_t offset = (size_t)top * row_bytes + (size_t)left * 4;
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::put_pixels,
            element, rect[0] + left, rect[1] + top, (uint32_t)(right - left), (uint32_t)(bottom - top),
            bytes + offset, row_bytes, byte_length - offset)) return js_canvas_draw_failed();
    return make_js_undefined();
}

extern "C" void dom_canvas_install_pixel_interface(Item constructor,
        Item image_data_prototype, Item context_prototype) {
    JsCanvasRuntimeState* state = canvas_runtime_state_ensure();
    if (!state || !root_vector_ensure_external(&state->image_data_roots)) return;
    RootFrame roots(5);
    Rooted<Item> target(roots, constructor), prototype(roots, image_data_prototype);
    Rooted<Item> context_root(roots, context_prototype), function(roots, ItemNull), key(roots, ItemNull);
    key.set(dom_realm_private_key_for_class(target.get(), js_name_item("#ImageData")));
    if (item_is_error(key.get())) return;
    state->image_data_slots[0] = key.get(); state->image_data_slots[1] = prototype.get();
    static const char* names[] = {"data", "width", "height", "colorSpace", "pixelFormat"};
    for (int slot = 0; slot < 5; slot++) {
        key.set(js_name_item(names[slot]));
        function.set(js_new_native_payload_function(js_image_data_get, slot, 0));
        js_set_function_name_from_property_key(function.get(), key.get(), 1);
        dom_realm_install_accessor(prototype.get(), key.get(), function.get(), ItemNull, 0);
    }
    static const struct { const char* name; JsNativeCallBody call; int length; } methods[] = {
        {"createImageData", js_canvas_create_image_data, 1},
        {"getImageData", js_canvas_get_image_data, 4},
        {"putImageData", js_canvas_put_image_data, 3},
    };
    for (const auto& method : methods) {
        function.set(js_new_native_payload_function(method.call, 0, method.length));
        js_set_function_name(function.get(), js_name_item(method.name));
        dom_realm_set_name(context_root.get(), method.name, function.get());
    }
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::save, element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_restore(Item callee, Item this_value,
                                      Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::restore, element)) return js_canvas_draw_failed();
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::scale, element, values[0], values[1])
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::translate, element, values[0], values[1])
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::rotate, element, radians) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_matrix_dictionary(Item dictionary, float* coefficients) {
    static const char* members[] = {
        "a", "b", "c", "d", "e", "f", "m11", "m12", "m21", "m22", "m41", "m42"};
    static const double defaults[] = {1, 0, 0, 1, 0, 0};
    double values[12] = {};
    bool present[12] = {};
    RootFrame roots(2);
    Rooted<Item> source(roots, dictionary);
    Rooted<Item> member(roots, ItemNull);
    TypeId type = get_type_id(source.get());
    bool empty = source.get().item == 0 || type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED;
    if (!empty && !js_is_object_value(source.get()))
        return js_throw_type_error("Canvas transform dictionary must be an object");
    // WebIDL reads dictionary members in name order before validating their aliases.
    for (int index = 0; !empty && index < 12; index++) {
        member.set(dom_realm_get_name(source.get(), members[index]));
        if (item_is_error(member.get())) return member.get();
        if (get_type_id(member.get()) == LMD_TYPE_UNDEFINED) continue;
        Item error = js_canvas_number_value(member.get(), &values[index]);
        if (item_is_error(error)) return error;
        present[index] = true;
    }
    for (int index = 0; index < 6; index++) {
        double first = values[index], second = values[index + 6];
        if (present[index] && present[index + 6] && first != second &&
            !(isnan(first) && isnan(second)))
            return js_throw_type_error("Canvas transform aliases disagree");
        coefficients[index] = (float)(present[index + 6] ? second
            : present[index] ? first : defaults[index]);
    }
    return ItemNull;
}

static Item js_canvas_context_matrix(Item this_value, Item* args, int argc,
                                     bool replace, bool reset) {
    RootFrame roots(1);
    Rooted<Item> receiver(roots, this_value);
    void* element = js_canvas_context_element(receiver.get());
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    float coefficients[6] = {1, 0, 0, 1, 0, 0};
    if (!reset && replace && argc <= 1) {
        Item error = js_canvas_matrix_dictionary(argc ? args[0] : make_js_undefined(), coefficients);
        if (item_is_error(error)) return error;
    } else if (!reset) {
        if (argc < 6) return js_throw_type_error("Canvas transform requires six coefficients");
        for (int index = 0; index < 6; index++) {
            Item error = js_canvas_number_arg(args, argc, index, &coefficients[index]);
            if (item_is_error(error)) return error;
        }
    }
    if (!js_canvas_numbers_are_finite(coefficients, 6)) return make_js_undefined();
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::transform,
        element, coefficients, replace) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_transform(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return js_canvas_context_matrix(receiver, args, argc, false, false);
}
static Item js_canvas_context_set_transform(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return js_canvas_context_matrix(receiver, args, argc, true, false);
}
static Item js_canvas_context_reset_transform(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return js_canvas_context_matrix(receiver, args, argc, true, true);
}

static Item js_canvas_context_get_transform(Item, Item receiver, Item*, int, uint64_t*) {
    void* element = js_canvas_context_element(receiver);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    float coefficients[6];
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::get_transform,
            element, coefficients)) return js_canvas_draw_failed();
    // The matrix returned to script owns its values independently of the context.
    return dom_create_matrix_2d(coefficients);
}

static Item js_canvas_context_begin_path(Item callee, Item this_value,
                                         Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::begin_path, element) ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_close_path(Item callee, Item this_value,
                                         Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::close_path, element) ? make_js_undefined() : js_canvas_draw_failed();
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::move_to, element, values[0], values[1])
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::line_to, element, values[0], values[1])
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::bezier_curve_to, element, values[0], values[1], values[2],
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::quadratic_curve_to, element, values[0], values[1],
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::rect, element, values[0], values[1], values[2], values[3])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_path_numbers(Item* args, int argc, int required, double* values) {
    if (argc < required) return js_throw_type_error("Canvas path requires more arguments");
    for (int index = 0; index < required; index++) {
        Item error = js_canvas_number_value(args[index], &values[index]);
        if (item_is_error(error)) return error;
    }
    for (int index = 0; index < required; index++) {
        if (!isfinite(values[index])) return make_js_undefined();
    }
    return ItemNull;
}

static Item js_canvas_path_ellipse(Item receiver, Item* args, int argc, bool circle) {
    void* element = js_canvas_context_element(receiver);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    int required = circle ? 5 : 7;
    double values[7] = {};
    Item result = js_canvas_path_numbers(args, argc, required, values);
    if (result.item != ITEM_NULL) return result;
    double radius_y = circle ? values[2] : values[3];
    if (values[2] < 0.0 || radius_y < 0.0)
        return dom_raise_exception("IndexSizeError", "Canvas radii must be non-negative");
    float bounds[] = {(float)values[0], (float)values[1], (float)values[2], (float)radius_y};
    if (!js_canvas_numbers_are_finite(bounds, 4)) return make_js_undefined();
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::ellipse, element,
                              bounds[0], bounds[1], bounds[2], bounds[3], circle ? 0.0 : values[4],
                              values[required - 2], values[required - 1],
                              argc > required && js_is_truthy(args[required]))
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_arc(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return js_canvas_path_ellipse(receiver, args, argc, true);
}

static Item js_canvas_context_ellipse(Item, Item receiver, Item* args, int argc, uint64_t*) {
    return js_canvas_path_ellipse(receiver, args, argc, false);
}

static Item js_canvas_context_arc_to(Item, Item receiver, Item* args, int argc, uint64_t*) {
    void* element = js_canvas_context_element(receiver);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    double values[5] = {};
    Item result = js_canvas_path_numbers(args, argc, 5, values);
    if (result.item != ITEM_NULL) return result;
    if (values[4] < 0.0) return dom_raise_exception("IndexSizeError", "Canvas radius must be non-negative");
    float points[5];
    for (int index = 0; index < 5; index++) points[index] = (float)values[index];
    if (!js_canvas_numbers_are_finite(points, 5)) return make_js_undefined();
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::arc_to, element,
        points[0], points[1], points[2], points[3], points[4])
        ? make_js_undefined() : js_canvas_draw_failed();
}

static Item js_canvas_context_fill(Item callee, Item this_value,
                                   Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::fill, element)) return js_canvas_draw_failed();
    js_canvas_note_paint(element);
    return make_js_undefined();
}

static Item js_canvas_context_stroke(Item callee, Item this_value,
                                     Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)args; (void)argc; (void)result_home;
    void* element = js_canvas_context_element(this_value);
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::stroke, element)) return js_canvas_draw_failed();
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
    return dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::clip, element) ? make_js_undefined() : js_canvas_draw_failed();
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
    bool drawn = clear ? dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::clear_rect, element, values[0], values[1],
                                                    values[2], values[3])
        : stroke ? dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::stroke_rect, element, values[0], values[1],
                                               values[2], values[3])
        : dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::fill_rect, element, values[0], values[1],
                                   values[2], values[3]);
    if (!drawn) return js_canvas_draw_failed();
    js_canvas_note_paint(element);
    return make_js_undefined();
}

static Item js_canvas_context_draw_image(Item callee, Item this_value,
        Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    if (argc < 3 || (argc != 3 && argc != 5 && argc < 9))
        return dom_realm_throw_type_error("drawImage requires 3, 5 or 9 arguments");
    RootFrame roots(2);
    Rooted<Item> context(roots, this_value);
    Rooted<Item> source(roots, args[0]);
    void* element = js_canvas_context_element(context.get());
    DomElement* image = (DomElement*)dom_unwrap_element(source.get());
    if (!element) return dom_realm_throw_type_error("CanvasRenderingContext2D receiver required");
    bool svg_image = image && image->tag() == MARKUP_NAME_IMAGE && dom_element_is_svg(image);
    if (!image || (image->tag() != MARKUP_NAME_IMG && image->tag() != MARKUP_NAME_CANVAS && !svg_image))
        return dom_realm_throw_type_error("drawImage requires an image or canvas source");
    int count = argc >= 9 ? 8 : argc - 1;
    float values[8] = {};
    for (int index = 0; index < count; index++) {
        Item error = js_canvas_number_arg(args, argc, index + 1, &values[index]);
        if (item_is_error(error)) return error;
    }
    if (!js_canvas_numbers_are_finite(values, count)) return make_js_undefined();
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::draw_image,
            element, image, values, (size_t)count)) return js_canvas_draw_failed();
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
    if (!dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::fill_text, element, state->font_handles[handle_id], text->chars,
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
        {"transform", js_canvas_context_transform, 6},
        {"setTransform", js_canvas_context_set_transform, 0},
        {"resetTransform", js_canvas_context_reset_transform, 0},
        {"getTransform", js_canvas_context_get_transform, 0},
        {"beginPath", js_canvas_context_begin_path, 0},
        {"closePath", js_canvas_context_close_path, 0},
        {"moveTo", js_canvas_context_move_to, 2},
        {"lineTo", js_canvas_context_line_to, 2},
        {"bezierCurveTo", js_canvas_context_bezier_curve_to, 6},
        {"quadraticCurveTo", js_canvas_context_quadratic_curve_to, 4},
        {"rect", js_canvas_context_path_rect, 4},
        {"arc", js_canvas_context_arc, 5},
        {"arcTo", js_canvas_context_arc_to, 5},
        {"ellipse", js_canvas_context_ellipse, 7},
        {"fill", js_canvas_context_fill, 0},
        {"stroke", js_canvas_context_stroke, 0},
        {"clip", js_canvas_context_clip, 0},
        {"save", js_canvas_context_save, 0},
        {"restore", js_canvas_context_restore, 0},
        {"fillRect", js_canvas_context_fill_rect, 4},
        {"strokeRect", js_canvas_context_stroke_rect, 4},
        {"clearRect", js_canvas_context_clear_rect, 4},
        {"drawImage", js_canvas_context_draw_image, 3},
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
        !dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::ensure, dom_unwrap_element(canvas_root.get()))) {
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
    Rooted<Item> identifier_root(roots, ItemNull);
    if (!dom_is_html_canvas_element(canvas_root.get()) &&
        !(get_type_id(canvas_root.get()) == LMD_TYPE_MAP &&
          js_class_id(canvas_root.get()) == JS_CLASS_OFFSCREEN_CANVAS)) {
        return dom_realm_throw_type_error("Illegal invocation");
    }
    identifier_root.set(argc > 0 && args ? js_to_string(args[0]) : js_name_item("undefined"));
    if (item_is_error(identifier_root.get())) return identifier_root.get();
    String* identifier = get_type_id(identifier_root.get()) == LMD_TYPE_STRING
        ? it2s(identifier_root.get()) : nullptr;
    if (identifier && identifier->len==6 && memcmp(identifier->chars,"webgl2",6)==0)
        return dom_webgl_context_for(canvas_root.get(),argc>1?args[1]:ItemNull);
    if (dom_is_html_canvas_element(canvas_root.get()) && dom_provider_call(dom_webgl_engine_ops(), &DomWebGlEngineOps::has_context, dom_unwrap_element(canvas_root.get()))) return ItemNull;
    if (!identifier || identifier->len != 2 ||
        memcmp(identifier->chars, "2d", 2) != 0) {
        return ItemNull;
    }
    return js_canvas_2d_context_for(canvas_root.get());
}

static Item js_canvas_to_data_url(Item callee, Item this_value,
                                  Item* args, int argc, uint64_t* result_home) {
    (void)callee; (void)result_home;
    RootFrame roots(2);
    Rooted<Item> canvas_root(roots, this_value);
    if (!dom_is_html_canvas_element(canvas_root.get())) {
        return dom_realm_throw_type_error("Illegal invocation");
    }
    Rooted<Item> type_root(roots, argc > 0 && args
        ? js_to_string(args[0]) : js_name_item("image/png"));
    if (item_is_error(type_root.get())) return type_root.get();
    // HTML serialization falls back to PNG for unsupported MIME types.
    StrBuf* bytes = dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::to_data_url, dom_unwrap_element(canvas_root.get()));
    if (!bytes) return make_string_item("data:,");
    Item result = make_string_item(bytes->str, (int)bytes->length);
    strbuf_free(bytes);
    return result;
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
    str_copy(buf, sizeof(buf), font_str, len);

    FontWeight weight = FONT_WEIGHT_NORMAL;
    FontSlant slant = FONT_SLANT_NORMAL;
    float size_px = 16.0f;
    const char* family_start = nullptr;

    // tokenize: walk through space-separated tokens
    const char* p = buf;

    // skip leading whitespace
    p = str_skip_ascii_space(p);

    // parse optional style
    if (strncmp(p, "italic", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        slant = FONT_SLANT_ITALIC;
        p += 6;
        p = str_skip_ascii_space(p);
    } else if (strncmp(p, "oblique", 7) == 0 && (p[7] == ' ' || p[7] == '\0')) {
        slant = FONT_SLANT_OBLIQUE;
        p += 7;
        p = str_skip_ascii_space(p);
    } else if (strncmp(p, "normal", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        p += 6;
        p = str_skip_ascii_space(p);
    }

    // parse optional weight
    if (strncmp(p, "bold", 4) == 0 && (p[4] == ' ' || p[4] == '\0')) {
        weight = FONT_WEIGHT_BOLD;
        p += 4;
        p = str_skip_ascii_space(p);
    } else if (strncmp(p, "normal", 6) == 0 && (p[6] == ' ' || p[6] == '\0')) {
        p += 6;
        p = str_skip_ascii_space(p);
    } else if (isdigit((unsigned char)*p)) {
        // numeric weight like "700" — but only if followed by a space (not "700px")
        char* end;
        long w = strtol(p, &end, 10);
        if (end > p && *end == ' ') {
            // it's a weight (100-900), not a size
            weight = (FontWeight)w;
            p = end;
            p = str_skip_ascii_space(p);
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
        p = str_skip_ascii_space(p);
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
    str_copy(family_buf, sizeof(family_buf), family_start, fam_len);

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
    RootFrame roots(3);
    Rooted<Item> prototype_root(roots, html_canvas_prototype);
    Rooted<Item> method_root(roots, js_new_native_payload_function(
        js_canvas_get_context, 0, 1));
    dom_realm_set_name(prototype_root.get(), "getContext", method_root.get());
    Rooted<Item> data_url_root(roots, js_new_native_payload_function(
        js_canvas_to_data_url, 0, 0));
    dom_realm_set_name(prototype_root.get(), "toDataURL", data_url_root.get());
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
    if (element) dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_font, element, s->chars, s->len);

    FontHandle* handle = parse_css_font_shorthand(s->chars, s->len);
    if (handle) {
        int id = canvas_store_font_handle(handle);
        dom_realm_set_name(ctx_obj, "__font_handle_id", (Item){.item = i2it(id)});
    }
}

static void js_canvas_apply_native_state(Item context) {
    void* element = js_canvas_context_element(context);
    DomCanvasStateSnapshot state = {};
    if (!element || !dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::get_state, element, &state)) return;
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
                    ? dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_fill_color, element, color.r, color.g, color.b, color.a)
                    : dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_stroke_color, element, color.r, color.g, color.b, color.a);
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
            valid = dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_line_width, element, width);
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
            valid = dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_global_alpha, element, alpha);
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
        valid = valid && dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_line_cap, element, cap);
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
        valid = valid && dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_line_join, element, join);
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
        valid = valid && dom_provider_call(dom_canvas_engine_ops(), &DomCanvasEngineOps::set_text_align, element, align);
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
    root_vector_destroy(&state->image_data_roots);
    for (int i = 0; i < state->font_handle_count; i++) {
        if (state->font_handles[i]) font_handle_release(state->font_handles[i]);
    }
    if (state->font_context) font_context_destroy(state->font_context);
    mem_free(state);
}
