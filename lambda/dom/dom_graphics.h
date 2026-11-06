#pragma once

#include <stddef.h>
#include <stdint.h>
#include "dom_provider.hpp"
#include "../../lib/webgl.h"
#include "../../lib/canvas.h"

struct StrBuf;

// Runtime-owned native graphics contracts (D9.1.3, D9.5.6). No script values
// or renderer pointers are retained here; providers own document resources.
#define DOM_CANVAS_FONT_TEXT_MAX RADIANT_CANVAS_FONT_TEXT_MAX
using DomCanvasStateSnapshot = RadiantCanvasStateSnapshot;

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
    OP(bool, draw_image, (void* canvas_element, void* source_element, const float* coordinates, size_t coordinate_count)) \
    OP(bool, fill_text, (void* canvas_element, void* font_handle, const char* text, int text_len, float x, float y, float max_width))

struct DomCanvasEngineOps {
#define DOM_GRAPHICS_FIELD(result, name, parameters) result (*name) parameters;
    DOM_CANVAS_ENGINE_OPERATIONS(DOM_GRAPHICS_FIELD)
#undef DOM_GRAPHICS_FIELD
};

struct DomWebGlEngineOps {
#define DOM_GRAPHICS_FIELD(result, name, parameters) result (*name) parameters;
    WEBGL_ENGINE_OPERATIONS(DOM_GRAPHICS_FIELD)
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
