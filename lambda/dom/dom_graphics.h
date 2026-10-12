#pragma once

#include <stddef.h>
#include <stdint.h>
#include "dom_provider.hpp"

struct StrBuf;

// Runtime-owned native graphics contracts (D7.1.3, D7.5.3). No script values
// or renderer pointers are retained here; providers own document resources.
#define DOM_CANVAS_FONT_TEXT_MAX 512
struct DomCanvasStateSnapshot {
    uint8_t fill_r, fill_g, fill_b, fill_a;
    uint8_t stroke_r, stroke_g, stroke_b, stroke_a;
    uint8_t line_cap, line_join, text_align;
    float line_width, global_alpha;
    char font[DOM_CANVAS_FONT_TEXT_MAX];
};

// One schema supplies operation fields, native declarations and registration.
#define DOM_CANVAS_ENGINE_OPERATIONS(OP) \
    OP(bool, ensure, (void* canvas_element)) \
    OP(bool, set_pixels, (void* canvas_element, const uint8_t* rgba, uint32_t width, uint32_t height, const int64_t* strides)) \
    OP(StrBuf*, to_data_url, (void* canvas_element)) \
    OP(bool, set_dimension, (void* canvas_element, bool is_width, uint32_t value)) \
    OP(bool, reset_from_attributes, (void* canvas_element)) \
    OP(bool, set_fill_color, (void* canvas_element, uint8_t r, uint8_t g, uint8_t b, uint8_t a)) \
    OP(bool, set_stroke_color, (void* canvas_element, uint8_t r, uint8_t g, uint8_t b, uint8_t a)) \
    OP(bool, set_line_width, (void* canvas_element, float width)) \
    OP(bool, set_global_alpha, (void* canvas_element, float alpha)) \
    OP(bool, set_line_cap, (void* canvas_element, uint8_t cap)) \
    OP(bool, set_line_join, (void* canvas_element, uint8_t join)) \
    OP(bool, set_text_align, (void* canvas_element, uint8_t align)) \
    OP(bool, set_font, (void* canvas_element, const char* font, int font_len)) \
    OP(bool, get_state, (void* canvas_element, DomCanvasStateSnapshot* out)) \
    OP(bool, save, (void* canvas_element)) \
    OP(bool, restore, (void* canvas_element)) \
    OP(bool, scale, (void* canvas_element, float x, float y)) \
    OP(bool, translate, (void* canvas_element, float x, float y)) \
    OP(bool, rotate, (void* canvas_element, float radians)) \
    OP(bool, begin_path, (void* canvas_element)) \
    OP(bool, close_path, (void* canvas_element)) \
    OP(bool, move_to, (void* canvas_element, float x, float y)) \
    OP(bool, line_to, (void* canvas_element, float x, float y)) \
    OP(bool, bezier_curve_to, (void* canvas_element, float cp1x, float cp1y, float cp2x, float cp2y, float x, float y)) \
    OP(bool, quadratic_curve_to, (void* canvas_element, float cpx, float cpy, float x, float y)) \
    OP(bool, rect, (void* canvas_element, float x, float y, float width, float height)) \
    OP(bool, arc, (void* canvas_element, float x, float y, float radius, float start_angle, float end_angle, bool counter_clockwise)) \
    OP(bool, clip, (void* canvas_element)) \
    OP(bool, fill, (void* canvas_element)) \
    OP(bool, stroke, (void* canvas_element)) \
    OP(bool, fill_rect, (void* canvas_element, float x, float y, float width, float height)) \
    OP(bool, stroke_rect, (void* canvas_element, float x, float y, float width, float height)) \
    OP(bool, clear_rect, (void* canvas_element, float x, float y, float width, float height)) \
    OP(bool, fill_text, (void* canvas_element, void* font_handle, const char* text, int text_len, float x, float y, float max_width))

struct DomCanvasEngineOps {
#define DOM_GRAPHICS_FIELD(result, name, parameters) result (*name) parameters;
    DOM_CANVAS_ENGINE_OPERATIONS(DOM_GRAPHICS_FIELD)
#undef DOM_GRAPHICS_FIELD
};

enum WebGlOp {
#define WEBGL_METHOD(name, js, signature, arity) WEBGL_##js,
#include "webgl_methods.def"
#undef WEBGL_METHOD
    WEBGL_OP_COUNT
};
static inline const char* webgl_op_name(WebGlOp op) {
    switch (op) {
#define WEBGL_METHOD(native, js, signature, arity) case WEBGL_##js: return #js;
#include "webgl_methods.def"
#undef WEBGL_METHOD
        default: return "unknown";
    }
}
struct WebGlCommand {
    WebGlOp op;
    double n[16];
    uint64_t resource[16];
    const void* data;
    size_t bytes;
    const char* text;
    bool compact_pixels; // DOM image rows ignore client-view unpack state
    bool source_dimensions; // DOM-source overload supplies the upload rectangle
};
enum WebGlReplyKind { WEBGL_VOID, WEBGL_NUMBER, WEBGL_BOOLEAN, WEBGL_STRING,
    WEBGL_NUMBERS, WEBGL_RESOURCE, WEBGL_LOCATION, WEBGL_ACTIVE_INFO, WEBGL_PRECISION };
struct WebGlReply {
    WebGlReplyKind kind;
    double n[16];
    unsigned count, resource_kind;
    uint64_t resource, generation, program;
    char text[8192];
    const char* source; // synchronous borrow copied before returning to script
};
struct WebGlOptions { bool alpha, depth, stencil, antialias, premultiplied_alpha, preserve; };

#define DOM_WEBGL_ENGINE_OPERATIONS(OP) \
    OP(uint64_t, create, (void* canvas, const WebGlOptions* options)) \
    OP(bool, call, (uint64_t canvas, const WebGlCommand* command, WebGlReply* reply)) \
    OP(bool, image, (uint64_t canvas, const WebGlCommand* command, void* source, WebGlReply* reply)) \
    OP(bool, extension_supported, (uint64_t canvas, const char* name)) \
    OP(void, error, (uint64_t canvas, unsigned error)) \
    OP(bool, resize, (void* canvas, unsigned width, unsigned height)) \
    OP(bool, has_context, (void* canvas)) \
    OP(bool, lose, (uint64_t canvas)) \
    OP(bool, restore, (uint64_t canvas))

struct DomWebGlEngineOps {
#define DOM_GRAPHICS_FIELD(result, name, parameters) result (*name) parameters;
    DOM_WEBGL_ENGINE_OPERATIONS(DOM_GRAPHICS_FIELD)
#undef DOM_GRAPHICS_FIELD
};

// Complete immutable tables must have process lifetime. Publication is atomic;
// registering the same table is idempotent, a different provider is rejected.
extern "C" bool dom_canvas_register_engine(const DomCanvasEngineOps* operations);
extern "C" bool dom_webgl_register_engine(const DomWebGlEngineOps* operations);
extern "C" const DomCanvasEngineOps* dom_canvas_engine_ops(void);
extern "C" const DomWebGlEngineOps* dom_webgl_engine_ops(void);

// Absence returns false, zero or null and leaves output records untouched.
// Pixel/text/font/command arguments are synchronous borrows. Canvas encoding
// returns caller-owned StrBuf bytes, released with strbuf_free.
