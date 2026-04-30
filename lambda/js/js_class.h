/**
 * A3 — JsClass enum: typed replacement for `__class_name__` magic-string class identity.
 *
 * Stage A3 of the Transpile_Js38 refactor (see vibe/jube/Transpile_Js38_Refactor.md).
 * The legacy scheme stores a string property `__class_name__` on each built-in
 * prototype/instance to mark its built-in class for downstream dispatch
 * (js_proto_class_method_dispatch, instanceof name fallback, duck-typing in
 * builtins). This file introduces a typed alternative:
 *
 *   - `JsClass`: uint8_t enum covering every class name currently written via
 *     `__class_name__` across `lambda/js/*.cpp` (snapshot from A3-T1
 *     reconnaissance — 50 distinct names).
 *   - `js_class` byte on `TypeMap` (declared in `lambda-data.hpp`) carries
 *     the class for any Map whose TypeMap is the per-Map private clone (A2-T1).
 *     Zero-init = JS_CLASS_NONE so every existing TypeMap is implicitly None.
 *   - `js_class_get(Item)` reader — returns JS_CLASS_NONE for non-MAP or when
 *     the byte hasn't been set; callers fall back to `__class_name__` string.
 *   - `js_class_set_for_map(Map*, JsClass)` writer — clones the Map's TypeMap
 *     for private mutation (via the existing A2-T1 primitive) before stamping
 *     the byte, so sibling Maps sharing a callsite shape cache aren't
 *     contaminated.
 *
 * Migration plan (phased, each independently shippable):
 *   - A3-T1 (this file): foundation only. No call-site changes.
 *   - A3-T2: route `js_proto_class_method_dispatch` through the enum byte
 *     with the legacy string check as fallback.
 *   - A3-T3+: per-file writer migration (dual-write byte AND string), then
 *     reader migration, then drop string writes/reads.
 */
#pragma once

#include "../lambda.h"
#include "../lambda-data.hpp"

// JsClass — typed identity for built-in classes. Order is FROZEN once shipped
// (the byte is stored on TypeMap; renumbering would corrupt existing maps).
// Add new entries at the END before JS_CLASS__COUNT.
enum JsClass : uint8_t {
    JS_CLASS_NONE = 0,        // not a built-in class (default for plain objects)
    // Core JS types — match LMD_TYPE_* dispatch in js_lookup_builtin_method.
    JS_CLASS_OBJECT,
    JS_CLASS_BOOLEAN,
    JS_CLASS_NUMBER,
    JS_CLASS_STRING,
    JS_CLASS_SYMBOL,
    JS_CLASS_BIGINT,
    JS_CLASS_ARRAY,
    JS_CLASS_DATE,
    JS_CLASS_REGEXP,
    JS_CLASS_ERROR,
    JS_CLASS_AGGREGATE_ERROR,
    JS_CLASS_PROMISE,
    // Typed-array family.
    JS_CLASS_TYPED_ARRAY,
    JS_CLASS_ARRAY_BUFFER,
    JS_CLASS_DATA_VIEW,
    // Iterator / stream protocols.
    JS_CLASS_READABLE_STREAM,
    JS_CLASS_WRITABLE_STREAM,
    JS_CLASS_STRING_DECODER,
    JS_CLASS_TEXT_DECODER,
    JS_CLASS_TEXT_ENCODER,
    // Web/DOM event surface.
    JS_CLASS_EVENT,
    JS_CLASS_CUSTOM_EVENT,
    JS_CLASS_EVENT_TARGET,
    JS_CLASS_EVENT_EMITTER,
    JS_CLASS_DOM_EXCEPTION,
    JS_CLASS_ABORT_CONTROLLER,
    JS_CLASS_ABORT_SIGNAL,
    JS_CLASS_ABORT_ERROR,
    JS_CLASS_MESSAGE_CHANNEL,
    JS_CLASS_MESSAGE_PORT,
    // URL / forms.
    JS_CLASS_URL,
    JS_CLASS_URL_SEARCH_PARAMS,
    JS_CLASS_FORM_DATA,
    JS_CLASS_BLOB,
    JS_CLASS_FILE,
    JS_CLASS_DATA_TRANSFER,
    // HTTP / net stack.
    JS_CLASS_AGENT,
    JS_CLASS_CLIENT_REQUEST,
    JS_CLASS_INCOMING_MESSAGE,
    JS_CLASS_SERVER,
    JS_CLASS_SERVER_RESPONSE,
    JS_CLASS_SOCKET,
    JS_CLASS_SECURE_CONTEXT,
    JS_CLASS_TLS_SERVER,
    JS_CLASS_TLS_SOCKET,
    // Browser surface.
    JS_CLASS_RANGE,
    JS_CLASS_SELECTION,
    JS_CLASS_CANVAS_RENDERING_CONTEXT_2D,
    JS_CLASS_OFFSCREEN_CANVAS,
    JS_CLASS_CSS_NESTED_DECLARATIONS,
    // Timers.
    JS_CLASS_TIMEOUT,
    JS_CLASS__COUNT  // sentinel
};

// Read the JsClass tag carried by an object. Returns JS_CLASS_NONE for
// non-MAP items, for MAPs without a TypeMap (`type==NULL`), and for MAPs
// whose TypeMap was zero-init'd (the common case until a writer stamps it).
// Pure read — does not allocate.
static inline JsClass js_class_get(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return JS_CLASS_NONE;
    Map* m = obj.map;
    if (!m || !m->type) return JS_CLASS_NONE;
    return (JsClass)((TypeMap*)m->type)->js_class;
}

// Forward declaration — defined in js_property_attrs.cpp (A2-T1).
// We don't include that header here to keep js_class.h independent of the
// shape-mutation surface; callers of js_class_set_for_map already include
// js_property_attrs.h.
struct Map;
extern "C" void js_typemap_clone_for_mutation(struct Map* m);

// Stamp a JsClass tag on a Map. Clones the Map's TypeMap for private
// mutation first so sibling Maps sharing a callsite shape cache stay on the
// original (untagged) blueprint. Idempotent within a single Map: subsequent
// calls reuse the already-private clone.
static inline void js_class_set_for_map(Map* m, JsClass cls) {
    if (!m) return;
    js_typemap_clone_for_mutation(m);
    if (!m->type) return;  // clone failed (out of memory) — caller falls back to string write
    ((TypeMap*)m->type)->js_class = (uint8_t)cls;
}
