// js_coerce.h — Lambda JS coercion kernels (J39-1).
//
// Implements:
//   ES §7.1.1   ToPrimitive(input, hint)
//   ES §7.1.1.1 OrdinaryToPrimitive(O, hint)
//
// Single source of truth for object→primitive coercion. Replaces the
// previously file-static js_op_to_primitive in js_runtime.cpp and the
// duplicated MAP-branch implementations in js_to_number / js_to_string /
// js_to_numeric / Date constructor.
//
// See vibe/jube/Transpile_Js39.md §J39-1.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ES §7.1.1 hint values. Integer-encoded so the symbol passed to
// @@toPrimitive can be derived without re-allocating per call.
typedef enum {
    JS_HINT_DEFAULT = 0,
    JS_HINT_NUMBER  = 1,
    JS_HINT_STRING  = 2
} JsHint;

// ES §7.1.1 ToPrimitive(input, hint).
//
// For non-object inputs (the wide majority of calls), returns `value`
// unchanged. For object inputs, looks up @@toPrimitive (Symbol.toPrimitive,
// represented by its realm-local Symbol.toPrimitive property key); if present and callable, invokes it
// with the hint string and validates the result is primitive. Otherwise
// runs OrdinaryToPrimitive — calling valueOf then toString (or reverse for
// JS_HINT_STRING), returning the first primitive result. If both methods
// return non-primitives (or @@toPrimitive returns a non-primitive), returns
// the merged ERROR Item produced by js_throw_type_error.
//
// Wrapper objects with __primitiveValue__ short-circuit when no
// valueOf/toString/@@toPrimitive shadow exists on the object — this
// preserves the long-standing fast path for boxed primitives.
//
// The returned Item is the complete result.  A result tagged ERROR is the
// failure lane and must be returned by the caller without consulting a flag.
Item js_to_primitive(Item value, JsHint hint);

#ifdef __cplusplus
}
#endif
