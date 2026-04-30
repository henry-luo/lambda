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

#include <string.h>
#include "../lambda.h"
#include "../lambda-data.hpp"

// JsClass — typed identity for built-in classes. Order is FROZEN once shipped
// (the byte is stored on TypeMap; renumbering would corrupt existing maps).
// Add new entries at the END before JS_CLASS__COUNT.
enum JsClass : uint8_t {
    JS_CLASS_NONE = 0,        // not a built-in class (default for plain objects)
    // Core JS types — match LMD_TYPE_* dispatch in js_lookup_builtin_method.
    JS_CLASS_OBJECT,
    JS_CLASS_FUNCTION,
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
    // Collections (A3-T4 additions).
    JS_CLASS_MAP,
    JS_CLASS_SET,
    JS_CLASS_WEAK_MAP,
    JS_CLASS_WEAK_SET,
    JS_CLASS_WEAK_REF,
    JS_CLASS_MAP_ITERATOR,
    JS_CLASS_SET_ITERATOR,
    // Generators / async (A3-T4 additions).
    JS_CLASS_GENERATOR,
    JS_CLASS_GENERATOR_FUNCTION,
    JS_CLASS_ASYNC_FUNCTION,
    JS_CLASS_ARGUMENTS,
    JS_CLASS_CLIPBOARD_ITEM,
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
// Takes the user-facing Item (because shape mutation needs the wrapper-aware
// underlying-map lookup) and returns the (possibly newly-cloned) TypeMap, or
// nullptr if cloning is not possible. Exported via js_class.h so callers can
// stamp typed class metadata without pulling in the broader property-attrs
// surface.
struct TypeMap;
extern "C" struct TypeMap* js_typemap_clone_for_mutation_pub(Item obj);

// Stamp a JsClass tag on a Map. Clones the Map's TypeMap for private
// mutation first so sibling Maps sharing a callsite shape cache stay on the
// original (untagged) blueprint. Idempotent within a single Map: subsequent
// calls reuse the already-private clone. No-op for non-MAP items or when
// the underlying clone fails (caller can still rely on the legacy
// `__class_name__` string write).
static inline void js_class_stamp(Item obj, JsClass cls) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return;
    TypeMap* tm = js_typemap_clone_for_mutation_pub(obj);
    if (!tm) return;
    tm->js_class = (uint8_t)cls;
}

// Map a built-in class-name string (the value previously stored in
// `__class_name__`) to its JsClass tag. Returns JS_CLASS_NONE for unknown
// names. Centralized here so writers and the legacy reader path stay in
// sync as we migrate.
static inline JsClass js_class_from_name(const char* nm, int nl) {
    if (!nm || nl <= 0) return JS_CLASS_NONE;
    switch (nl) {
        case 3:
            if (!strncmp(nm, "URL", 3)) return JS_CLASS_URL;
            if (!strncmp(nm, "Map", 3)) return JS_CLASS_MAP;
            if (!strncmp(nm, "Set", 3)) return JS_CLASS_SET;
            break;
        case 4:
            if (!strncmp(nm, "Date", 4)) return JS_CLASS_DATE;
            if (!strncmp(nm, "Blob", 4)) return JS_CLASS_BLOB;
            if (!strncmp(nm, "File", 4)) return JS_CLASS_FILE;
            break;
        case 5:
            if (!strncmp(nm, "Array", 5)) return JS_CLASS_ARRAY;
            if (!strncmp(nm, "Error", 5)) return JS_CLASS_ERROR;
            if (!strncmp(nm, "Range", 5)) return JS_CLASS_RANGE;
            if (!strncmp(nm, "Agent", 5)) return JS_CLASS_AGENT;
            if (!strncmp(nm, "Event", 5)) return JS_CLASS_EVENT;
            break;
        case 6:
            if (!strncmp(nm, "Number", 6)) return JS_CLASS_NUMBER;
            if (!strncmp(nm, "String", 6)) return JS_CLASS_STRING;
            if (!strncmp(nm, "Object", 6)) return JS_CLASS_OBJECT;
            if (!strncmp(nm, "Symbol", 6)) return JS_CLASS_SYMBOL;
            if (!strncmp(nm, "BigInt", 6)) return JS_CLASS_BIGINT;
            if (!strncmp(nm, "RegExp", 6)) return JS_CLASS_REGEXP;
            if (!strncmp(nm, "Server", 6)) return JS_CLASS_SERVER;
            if (!strncmp(nm, "Socket", 6)) return JS_CLASS_SOCKET;
            break;
        case 7:
            if (!strncmp(nm, "Boolean", 7)) return JS_CLASS_BOOLEAN;
            if (!strncmp(nm, "Promise", 7)) return JS_CLASS_PROMISE;
            if (!strncmp(nm, "Timeout", 7)) return JS_CLASS_TIMEOUT;
            if (!strncmp(nm, "WeakMap", 7)) return JS_CLASS_WEAK_MAP;
            if (!strncmp(nm, "WeakSet", 7)) return JS_CLASS_WEAK_SET;
            if (!strncmp(nm, "WeakRef", 7)) return JS_CLASS_WEAK_REF;
            break;
        case 8:
            if (!strncmp(nm, "DataView", 8)) return JS_CLASS_DATA_VIEW;
            if (!strncmp(nm, "FormData", 8)) return JS_CLASS_FORM_DATA;
            if (!strncmp(nm, "Function", 8)) return JS_CLASS_FUNCTION;
            break;
        case 9:
            if (!strncmp(nm, "TLSServer", 9)) return JS_CLASS_TLS_SERVER;
            if (!strncmp(nm, "TLSSocket", 9)) return JS_CLASS_TLS_SOCKET;
            if (!strncmp(nm, "Selection", 9)) return JS_CLASS_SELECTION;
            if (!strncmp(nm, "Generator", 9)) return JS_CLASS_GENERATOR;
            if (!strncmp(nm, "Arguments", 9)) return JS_CLASS_ARGUMENTS;
            break;
        case 10:
            if (!strncmp(nm, "TypedArray", 10)) return JS_CLASS_TYPED_ARRAY;
            if (!strncmp(nm, "AbortError", 10)) return JS_CLASS_ABORT_ERROR;
            break;
        case 11:
            if (!strncmp(nm, "AbortSignal", 11)) return JS_CLASS_ABORT_SIGNAL;
            if (!strncmp(nm, "ArrayBuffer", 11)) return JS_CLASS_ARRAY_BUFFER;
            if (!strncmp(nm, "MessagePort", 11)) return JS_CLASS_MESSAGE_PORT;
            if (!strncmp(nm, "CustomEvent", 11)) return JS_CLASS_CUSTOM_EVENT;
            if (!strncmp(nm, "EventTarget", 11)) return JS_CLASS_EVENT_TARGET;
            if (!strncmp(nm, "TextDecoder", 11)) return JS_CLASS_TEXT_DECODER;
            if (!strncmp(nm, "TextEncoder", 11)) return JS_CLASS_TEXT_ENCODER;
            if (!strncmp(nm, "MapIterator", 11)) return JS_CLASS_MAP_ITERATOR;
            if (!strncmp(nm, "SetIterator", 11)) return JS_CLASS_SET_ITERATOR;
            break;
        case 12:
            if (!strncmp(nm, "DOMException", 12)) return JS_CLASS_DOM_EXCEPTION;
            if (!strncmp(nm, "EventEmitter", 12)) return JS_CLASS_EVENT_EMITTER;
            if (!strncmp(nm, "DataTransfer", 12)) return JS_CLASS_DATA_TRANSFER;
            break;
        case 13:
            if (!strncmp(nm, "ClientRequest", 13)) return JS_CLASS_CLIENT_REQUEST;
            if (!strncmp(nm, "StringDecoder", 13)) return JS_CLASS_STRING_DECODER;
            if (!strncmp(nm, "SecureContext", 13)) return JS_CLASS_SECURE_CONTEXT;
            if (!strncmp(nm, "AsyncFunction", 13)) return JS_CLASS_ASYNC_FUNCTION;
            if (!strncmp(nm, "ClipboardItem", 13)) return JS_CLASS_CLIPBOARD_ITEM;
            break;
        case 14:
            if (!strncmp(nm, "AggregateError", 14)) return JS_CLASS_AGGREGATE_ERROR;
            if (!strncmp(nm, "ReadableStream", 14)) return JS_CLASS_READABLE_STREAM;
            if (!strncmp(nm, "WritableStream", 14)) return JS_CLASS_WRITABLE_STREAM;
            if (!strncmp(nm, "MessageChannel", 14)) return JS_CLASS_MESSAGE_CHANNEL;
            if (!strncmp(nm, "ServerResponse", 14)) return JS_CLASS_SERVER_RESPONSE;
            break;
        case 15:
            if (!strncmp(nm, "URLSearchParams", 15)) return JS_CLASS_URL_SEARCH_PARAMS;
            if (!strncmp(nm, "AbortController", 15)) return JS_CLASS_ABORT_CONTROLLER;
            if (!strncmp(nm, "OffscreenCanvas", 15)) return JS_CLASS_OFFSCREEN_CANVAS;
            if (!strncmp(nm, "IncomingMessage", 15)) return JS_CLASS_INCOMING_MESSAGE;
            break;
        case 17:
            if (!strncmp(nm, "GeneratorFunction", 17)) return JS_CLASS_GENERATOR_FUNCTION;
            break;
        case 21:
            if (!strncmp(nm, "CSSNestedDeclarations", 21)) return JS_CLASS_CSS_NESTED_DECLARATIONS;
            break;
        case 24:
            if (!strncmp(nm, "CanvasRenderingContext2D", 24)) return JS_CLASS_CANVAS_RENDERING_CONTEXT_2D;
            break;
    }
    return JS_CLASS_NONE;
}

// Forward declaration — defined in js_runtime.cpp. Returns the value of
// `m[key_str]` (or ItemNull when absent); `*out_found` flips to true iff
// the key resolves through an own/proto entry.
extern "C" Item js_map_get_fast_ext(Map* m, const char* key_str, int key_len, bool* out_found);

// Unified class-identity resolver. Prefers the typed JsClass byte stamped on
// the Map's TypeMap (A3-T3a/T3b). When the byte is JS_CLASS_NONE — the
// blueprint hasn't been migrated yet — falls back to reading the legacy
// `__class_name__` string property and mapping it via `js_class_from_name`.
//
// Use this from reader sites (predicates, dispatch, instanceof name fallback)
// during the A3-T4 migration. Once A3-T5 drops `__class_name__` writes the
// fallback becomes dead and can be removed.
//
// Returns JS_CLASS_NONE for non-MAP items, plain objects without a class
// stamp, and class names not present in `js_class_from_name`.
static inline JsClass js_class_id(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return JS_CLASS_NONE;
    Map* m = obj.map;
    if (!m) return JS_CLASS_NONE;
    if (m->type) {
        JsClass cls = (JsClass)((TypeMap*)m->type)->js_class;
        if (cls != JS_CLASS_NONE) return cls;
    }
    bool found = false;
    Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &found);
    if (!found || get_type_id(cn) != LMD_TYPE_STRING) return JS_CLASS_NONE;
    String* s = it2s(cn);
    if (!s) return JS_CLASS_NONE;
    return js_class_from_name(s->chars, (int)s->len);
}
