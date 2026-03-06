/**
 * JavaScript Typed Array Support for Lambda v5
 *
 * Implements Int8Array, Uint8Array, Int16Array, Uint16Array,
 * Int32Array, Uint32Array, Float32Array, Float64Array.
 *
 * Typed arrays are stored as Map objects with a sentinel marker,
 * using the same zero-overhead wrapper pattern as DOM nodes.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

typedef enum JsTypedArrayType {
    JS_TYPED_INT8,
    JS_TYPED_UINT8,
    JS_TYPED_INT16,
    JS_TYPED_UINT16,
    JS_TYPED_INT32,
    JS_TYPED_UINT32,
    JS_TYPED_FLOAT32,
    JS_TYPED_FLOAT64,
} JsTypedArrayType;

typedef struct JsTypedArray {
    JsTypedArrayType element_type;
    int length;
    int byte_length;
    void* data;     // raw buffer (heap-allocated)
} JsTypedArray;

// Sentinel marker for identifying typed arrays
extern char js_typed_array_marker;

// Core operations
Item js_typed_array_new(int type_id, int length);
Item js_typed_array_get(Item ta, Item index);
Item js_typed_array_set(Item ta, Item index, Item value);
int  js_typed_array_length(Item ta);
Item js_typed_array_fill(Item ta, Item value);
bool js_is_typed_array(Item val);
Item js_typed_array_subarray(Item ta, int start, int end);

#ifdef __cplusplus
}
#endif
