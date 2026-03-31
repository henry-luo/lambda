/**
 * JavaScript Typed Array, ArrayBuffer, and DataView Support for Lambda
 *
 * Implements Int8Array, Uint8Array, Int16Array, Uint16Array,
 * Int32Array, Uint32Array, Float32Array, Float64Array,
 * ArrayBuffer, and DataView.
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
    JS_TYPED_UINT8_CLAMPED,
} JsTypedArrayType;

// ArrayBuffer: raw byte storage
typedef struct JsArrayBuffer {
    void* data;         // heap-allocated byte buffer
    int byte_length;    // total bytes
} JsArrayBuffer;

// DataView: structured access into an ArrayBuffer
typedef struct JsDataView {
    JsArrayBuffer* buffer;  // backing ArrayBuffer
    int byte_offset;         // offset into buffer
    int byte_length;         // view byte length
} JsDataView;

typedef struct JsTypedArray {
    JsTypedArrayType element_type;  // offset 0: type enum (4 bytes)
    int length;                      // offset 4: element count
    int byte_length;                 // offset 8: total bytes
    int byte_offset;                 // offset 12: offset into backing buffer
    void* data;                      // offset 16: raw data pointer (direct pointer to first element)
    JsArrayBuffer* buffer;           // offset 24: optional backing ArrayBuffer (NULL if standalone)
    uint64_t buffer_item;            // offset 32: original ArrayBuffer Item for identity-preserving .buffer access
} JsTypedArray;

// Sentinel markers for identifying typed arrays, array buffers, data views
extern char js_typed_array_marker;

// Core typed array operations
Item js_typed_array_new(int type_id, int length);
Item js_typed_array_new_from_buffer(int type_id, Item buffer_item, int byte_offset, int length);
Item js_typed_array_new_from_array(int type_id, Item source);
Item js_typed_array_get(Item ta, Item index);
Item js_typed_array_set(Item ta, Item index, Item value);
int  js_typed_array_length(Item ta);
Item js_typed_array_fill(Item ta, Item value, int start, int end);
bool js_is_typed_array(Item val);
Item js_typed_array_subarray(Item ta, int start, int end);
Item js_typed_array_slice(Item ta, int start, int end);
Item js_typed_array_set_from(Item ta, Item source, int offset);

// ArrayBuffer operations
Item js_arraybuffer_new(int byte_length);
Item js_arraybuffer_wrap(JsArrayBuffer* ab);
bool js_is_arraybuffer(Item val);
int  js_arraybuffer_byte_length(Item val);
Item js_arraybuffer_slice(Item val, int begin, int end);
bool js_arraybuffer_is_view(Item val);
Item js_arraybuffer_is_view_item(Item val);

// DataView operations
Item js_dataview_new(Item buffer, int byte_offset, int byte_length);
bool js_is_dataview(Item val);
Item js_dataview_method(Item dv, Item method_name, Item* args, int argc);

// Smart constructor: dispatches based on argument type (number, ArrayBuffer, TypedArray, Array)
Item js_typed_array_construct(int type_id, Item arg, int byte_offset, int length, int argc);

#ifdef __cplusplus
}
#endif
