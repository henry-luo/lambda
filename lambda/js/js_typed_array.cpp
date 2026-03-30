/**
 * JavaScript Typed Array, ArrayBuffer, and DataView Implementation for Lambda
 */
#include "js_typed_array.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

extern void* heap_alloc(int size, TypeId type_id);

// Sentinel markers for type identification
static TypeMap js_typed_array_type_marker = {};
static TypeMap js_arraybuffer_type_marker = {};
static TypeMap js_dataview_type_marker = {};
char js_typed_array_marker = 'T';

static int typed_array_element_size(JsTypedArrayType type) {
    switch (type) {
    case JS_TYPED_INT8:
    case JS_TYPED_UINT8:
    case JS_TYPED_UINT8_CLAMPED: return 1;
    case JS_TYPED_INT16:
    case JS_TYPED_UINT16:   return 2;
    case JS_TYPED_INT32:
    case JS_TYPED_UINT32:
    case JS_TYPED_FLOAT32:  return 4;
    case JS_TYPED_FLOAT64:  return 8;
    default:                return 4;
    }
}

// ============================================================================
// ArrayBuffer
// ============================================================================

static JsArrayBuffer* js_arraybuffer_alloc(int byte_length) {
    JsArrayBuffer* ab = (JsArrayBuffer*)malloc(sizeof(JsArrayBuffer));
    ab->byte_length = byte_length;
    ab->data = calloc(1, byte_length > 0 ? byte_length : 1);
    return ab;
}

extern "C" Item js_arraybuffer_new(int byte_length) {
    if (byte_length < 0) byte_length = 0;
    JsArrayBuffer* ab = js_arraybuffer_alloc(byte_length);

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_arraybuffer_type_marker;
    m->data = ab;
    m->data_cap = 0;

    return (Item){.map = m};
}

extern "C" bool js_is_arraybuffer(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->type == (void*)&js_arraybuffer_type_marker;
}

// Wrap an existing JsArrayBuffer* in a Map Item (for .buffer property access)
extern "C" Item js_arraybuffer_wrap(JsArrayBuffer* ab) {
    if (!ab) return (Item){.item = ITEM_NULL};
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_arraybuffer_type_marker;
    m->data = ab;
    m->data_cap = 0;
    return (Item){.map = m};
}

extern "C" int js_arraybuffer_byte_length(Item val) {
    if (!js_is_arraybuffer(val)) return 0;
    JsArrayBuffer* ab = (JsArrayBuffer*)val.map->data;
    return ab->byte_length;
}

extern "C" Item js_arraybuffer_slice(Item val, int begin, int end) {
    if (!js_is_arraybuffer(val)) return (Item){.item = ITEM_NULL};
    JsArrayBuffer* ab = (JsArrayBuffer*)val.map->data;

    if (begin < 0) begin = ab->byte_length + begin;
    if (end < 0) end = ab->byte_length + end;
    if (begin < 0) begin = 0;
    if (end > ab->byte_length) end = ab->byte_length;
    if (begin >= end) return js_arraybuffer_new(0);

    int new_len = end - begin;
    Item result = js_arraybuffer_new(new_len);
    JsArrayBuffer* rab = (JsArrayBuffer*)result.map->data;
    memcpy(rab->data, (char*)ab->data + begin, new_len);
    return result;
}

extern "C" bool js_arraybuffer_is_view(Item val) {
    return js_is_typed_array(val) || js_is_dataview(val);
}

// Item-returning wrapper for MIR JIT calls (MIR expects Item return type)
extern "C" Item js_arraybuffer_is_view_item(Item val) {
    bool result = js_arraybuffer_is_view(val);
    return (Item){.item = result ? (ITEM_TRUE) : (ITEM_FALSE)};
}

// ============================================================================
// TypedArray
// ============================================================================

extern "C" bool js_is_typed_array(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->type == (void*)&js_typed_array_type_marker;
}

// Create a standalone typed array (owns its buffer)
extern "C" Item js_typed_array_new(int type_id, int length) {
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = typed_array_element_size(arr_type);
    int byte_length = length * elem_size;

    JsTypedArray* ta = (JsTypedArray*)malloc(sizeof(JsTypedArray));
    ta->element_type = arr_type;
    ta->length = length;
    ta->byte_length = byte_length;
    ta->byte_offset = 0;
    ta->data = calloc(length > 0 ? length : 1, elem_size);
    ta->buffer = NULL;

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_typed_array_type_marker;
    m->data = ta;
    m->data_cap = 0;

    return (Item){.map = m};
}

// Create a typed array as a view over an ArrayBuffer
extern "C" Item js_typed_array_new_from_buffer(int type_id, Item buffer_item, int byte_offset, int length) {
    if (!js_is_arraybuffer(buffer_item)) {
        log_error("js_typed_array_new_from_buffer: argument is not an ArrayBuffer");
        return js_typed_array_new(type_id, 0);
    }
    JsArrayBuffer* ab = (JsArrayBuffer*)buffer_item.map->data;
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = typed_array_element_size(arr_type);

    if (byte_offset < 0) byte_offset = 0;
    if (byte_offset % elem_size != 0) {
        log_error("js_typed_array_new_from_buffer: byte_offset %d not aligned to element size %d", byte_offset, elem_size);
        byte_offset = (byte_offset / elem_size) * elem_size;
    }

    int available = ab->byte_length - byte_offset;
    if (available < 0) available = 0;

    if (length < 0) {
        // auto-compute length from remaining buffer
        length = available / elem_size;
    }
    int byte_length = length * elem_size;
    if (byte_offset + byte_length > ab->byte_length) {
        log_error("js_typed_array_new_from_buffer: view exceeds buffer bounds");
        length = available / elem_size;
        byte_length = length * elem_size;
    }

    JsTypedArray* ta = (JsTypedArray*)malloc(sizeof(JsTypedArray));
    ta->element_type = arr_type;
    ta->length = length;
    ta->byte_length = byte_length;
    ta->byte_offset = byte_offset;
    ta->data = (char*)ab->data + byte_offset;  // direct pointer into buffer
    ta->buffer = ab;

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_typed_array_type_marker;
    m->data = ta;
    m->data_cap = 0;

    return (Item){.map = m};
}

// Create a typed array from another array (copy)
extern "C" Item js_typed_array_new_from_array(int type_id, Item source) {
    TypeId src_type = get_type_id(source);

    if (js_is_typed_array(source)) {
        // Copy from another typed array
        JsTypedArray* src = (JsTypedArray*)source.map->data;
        Item result = js_typed_array_new(type_id, src->length);
        JsTypedArray* dst = (JsTypedArray*)result.map->data;
        for (int i = 0; i < src->length; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item val = js_typed_array_get(source, idx);
            js_typed_array_set(result, idx, val);
        }
        return result;
    }

    if (src_type == LMD_TYPE_ARRAY) {
        // Copy from regular array
        Array* arr = source.array;
        int len = (int)arr->length;
        Item result = js_typed_array_new(type_id, len);
        for (int i = 0; i < len; i++) {
            Item idx = (Item){.item = i2it(i)};
            js_typed_array_set(result, idx, arr->items[i]);
        }
        return result;
    }

    // Fallback: treat as length
    int len = (int)it2i(source);
    return js_typed_array_new(type_id, len);
}

// Smart constructor: dispatches based on argument type
extern "C" Item js_typed_array_construct(int type_id, Item arg, int byte_offset, int length, int argc) {
    if (argc == 0) {
        return js_typed_array_new(type_id, 0);
    }

    // Check if arg is an ArrayBuffer
    if (js_is_arraybuffer(arg)) {
        return js_typed_array_new_from_buffer(type_id, arg, byte_offset, length);
    }

    // Check if arg is another TypedArray or Array
    TypeId arg_type = get_type_id(arg);
    if (js_is_typed_array(arg) || arg_type == LMD_TYPE_ARRAY) {
        return js_typed_array_new_from_array(type_id, arg);
    }

    // Number: create typed array with that length
    if (arg_type == LMD_TYPE_INT || arg_type == LMD_TYPE_FLOAT) {
        int len = (int)it2i(arg);
        if (len < 0) len = 0;
        return js_typed_array_new(type_id, len);
    }

    return js_typed_array_new(type_id, 0);
}

extern "C" Item js_typed_array_get(Item ta_item, Item index) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};

    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    int idx = (int)it2i(index);

    if (idx < 0 || idx >= ta->length) return (Item){.item = ITEM_NULL};

    switch (ta->element_type) {
    case JS_TYPED_INT8:
        return (Item){.item = i2it((int64_t)((int8_t*)ta->data)[idx])};
    case JS_TYPED_UINT8:
    case JS_TYPED_UINT8_CLAMPED:
        return (Item){.item = i2it((int64_t)((uint8_t*)ta->data)[idx])};
    case JS_TYPED_INT16:
        return (Item){.item = i2it((int64_t)((int16_t*)ta->data)[idx])};
    case JS_TYPED_UINT16:
        return (Item){.item = i2it((int64_t)((uint16_t*)ta->data)[idx])};
    case JS_TYPED_INT32:
        return (Item){.item = i2it((int64_t)((int32_t*)ta->data)[idx])};
    case JS_TYPED_UINT32:
        return (Item){.item = i2it((int64_t)((uint32_t*)ta->data)[idx])};
    case JS_TYPED_FLOAT32: {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = (double)((float*)ta->data)[idx];
        return (Item){.item = d2it(fp)};
    }
    case JS_TYPED_FLOAT64: {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ((double*)ta->data)[idx];
        return (Item){.item = d2it(fp)};
    }
    default:
        return (Item){.item = ITEM_NULL};
    }
}

extern "C" Item js_typed_array_set(Item ta_item, Item index, Item value) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};

    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    int idx = (int)it2i(index);

    if (idx < 0 || idx >= ta->length) return (Item){.item = ITEM_NULL};

    double num_val;
    TypeId vtype = get_type_id(value);
    if (vtype == LMD_TYPE_INT) {
        num_val = (double)it2i(value);
    } else if (vtype == LMD_TYPE_FLOAT) {
        num_val = it2d(value);
    } else {
        num_val = 0.0;
    }

    switch (ta->element_type) {
    case JS_TYPED_INT8:    ((int8_t*)ta->data)[idx] = (int8_t)(int32_t)num_val; break;
    case JS_TYPED_UINT8:   ((uint8_t*)ta->data)[idx] = (uint8_t)(uint32_t)num_val; break;
    case JS_TYPED_UINT8_CLAMPED: {
        // ECMAScript ToUint8Clamp: NaN→0, clamp to [0,255], then round-half-to-even
        if (isnan(num_val) || num_val <= 0.0) { ((uint8_t*)ta->data)[idx] = 0; break; }
        if (num_val >= 255.0) { ((uint8_t*)ta->data)[idx] = 255; break; }
        int f = (int)num_val;  // floor
        double fmod = num_val - f;
        uint8_t v;
        if (fmod < 0.5) v = (uint8_t)f;
        else if (fmod > 0.5) v = (uint8_t)(f + 1);
        else v = (f & 1) ? (uint8_t)(f + 1) : (uint8_t)f;  // ties to even
        ((uint8_t*)ta->data)[idx] = v;
        break;
    }
    case JS_TYPED_INT16:   ((int16_t*)ta->data)[idx] = (int16_t)(int32_t)num_val; break;
    case JS_TYPED_UINT16:  ((uint16_t*)ta->data)[idx] = (uint16_t)(uint32_t)num_val; break;
    case JS_TYPED_INT32:   ((int32_t*)ta->data)[idx] = (int32_t)num_val; break;
    case JS_TYPED_UINT32:  ((uint32_t*)ta->data)[idx] = (uint32_t)num_val; break;
    case JS_TYPED_FLOAT32: ((float*)ta->data)[idx] = (float)num_val; break;
    case JS_TYPED_FLOAT64: ((double*)ta->data)[idx] = num_val; break;
    }

    return value;
}

extern "C" int js_typed_array_length(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return 0;
    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    return ta->length;
}

extern "C" Item js_typed_array_fill(Item ta_item, Item value, int start, int end) {
    if (!js_is_typed_array(ta_item)) return ta_item;

    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;

    if (start < 0) start = ta->length + start;
    if (start < 0) start = 0;
    if (end == INT_MAX || end > ta->length) end = ta->length;
    else if (end < 0) end = ta->length + end;
    if (end < 0) end = 0;
    if (start >= end) return ta_item;

    for (int i = start; i < end; i++) {
        Item idx = (Item){.item = i2it(i)};
        js_typed_array_set(ta_item, idx, value);
    }

    return ta_item;
}

// .set(source [, offset]) — bulk copy from another array/typed array
extern "C" Item js_typed_array_set_from(Item ta_item, Item source, int offset) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* dst = (JsTypedArray*)ta_item.map->data;
    if (offset < 0) offset = 0;

    if (js_is_typed_array(source)) {
        JsTypedArray* src = (JsTypedArray*)source.map->data;
        for (int i = 0; i < src->length && (offset + i) < dst->length; i++) {
            Item idx_s = (Item){.item = i2it(i)};
            Item idx_d = (Item){.item = i2it(offset + i)};
            Item val = js_typed_array_get(source, idx_s);
            js_typed_array_set(ta_item, idx_d, val);
        }
    } else if (get_type_id(source) == LMD_TYPE_ARRAY) {
        Array* arr = source.array;
        for (int i = 0; i < (int)arr->length && (offset + i) < dst->length; i++) {
            Item idx_d = (Item){.item = i2it(offset + i)};
            js_typed_array_set(ta_item, idx_d, arr->items[i]);
        }
    }
    return (Item){.item = ITEM_NULL};
}

// .slice(begin, end) — creates a copy
extern "C" Item js_typed_array_slice(Item ta_item, int start, int end) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* ta = (JsTypedArray*)ta_item.map->data;

    if (start < 0) start = ta->length + start;
    if (end < 0) end = ta->length + end;
    if (start < 0) start = 0;
    if (end > ta->length) end = ta->length;
    if (start >= end) return js_typed_array_new((int)ta->element_type, 0);

    int new_length = end - start;
    Item result = js_typed_array_new((int)ta->element_type, new_length);
    JsTypedArray* rta = (JsTypedArray*)result.map->data;
    int elem_size = typed_array_element_size(ta->element_type);
    memcpy(rta->data, (char*)ta->data + start * elem_size, new_length * elem_size);
    return result;
}

// .subarray(begin, end) — creates a view (shares buffer)
extern "C" Item js_typed_array_subarray(Item ta_item, int start, int end) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* ta = (JsTypedArray*)ta_item.map->data;

    if (start < 0) start = ta->length + start;
    if (end < 0) end = ta->length + end;
    if (start < 0) start = 0;
    if (end > ta->length) end = ta->length;
    if (start >= end) return js_typed_array_new((int)ta->element_type, 0);

    int elem_size = typed_array_element_size(ta->element_type);
    int new_length = end - start;

    JsTypedArray* sub = (JsTypedArray*)malloc(sizeof(JsTypedArray));
    sub->element_type = ta->element_type;
    sub->length = new_length;
    sub->byte_length = new_length * elem_size;
    sub->byte_offset = ta->byte_offset + start * elem_size;
    sub->data = (char*)ta->data + start * elem_size;
    sub->buffer = ta->buffer;  // share the backing buffer

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_typed_array_type_marker;
    m->data = sub;
    m->data_cap = 0;

    return (Item){.map = m};
}

// ============================================================================
// DataView
// ============================================================================

extern "C" bool js_is_dataview(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->type == (void*)&js_dataview_type_marker;
}

extern "C" Item js_dataview_new(Item buffer, int byte_offset, int byte_length) {
    if (!js_is_arraybuffer(buffer)) {
        log_error("DataView: first argument must be an ArrayBuffer");
        return (Item){.item = ITEM_NULL};
    }
    JsArrayBuffer* ab = (JsArrayBuffer*)buffer.map->data;

    if (byte_offset < 0) byte_offset = 0;
    if (byte_offset > ab->byte_length) byte_offset = ab->byte_length;

    if (byte_length < 0) {
        // auto: rest of buffer
        byte_length = ab->byte_length - byte_offset;
    }
    if (byte_offset + byte_length > ab->byte_length) {
        byte_length = ab->byte_length - byte_offset;
    }

    JsDataView* dv = (JsDataView*)malloc(sizeof(JsDataView));
    dv->buffer = ab;
    dv->byte_offset = byte_offset;
    dv->byte_length = byte_length;

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_dataview_type_marker;
    m->data = dv;
    m->data_cap = 0;

    return (Item){.map = m};
}

// Helper: get raw pointer into DataView's buffer at given offset
static inline uint8_t* dv_ptr(JsDataView* dv, int offset, int size) {
    if (offset < 0 || offset + size > dv->byte_length) return NULL;
    return (uint8_t*)dv->buffer->data + dv->byte_offset + offset;
}

// Endianness helpers
static inline uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}
static inline uint64_t swap64(uint64_t v) {
    v = ((v >> 8) & 0x00FF00FF00FF00FFULL) | ((v << 8) & 0xFF00FF00FF00FF00ULL);
    v = ((v >> 16) & 0x0000FFFF0000FFFFULL) | ((v << 16) & 0xFFFF0000FFFF0000ULL);
    return (v >> 32) | (v << 32);
}

// Detect system endianness at startup
static bool is_little_endian_system() {
    uint16_t test = 1;
    return *((uint8_t*)&test) == 1;
}

// DataView method dispatch
extern "C" Item js_dataview_method(Item dv_item, Item method_name, Item* args, int argc) {
    if (!js_is_dataview(dv_item)) return (Item){.item = ITEM_NULL};
    JsDataView* dv = (JsDataView*)dv_item.map->data;

    String* mname = it2s(method_name);
    if (!mname) return (Item){.item = ITEM_NULL};
    const char* mn = mname->chars;
    int ml = (int)mname->len;

    int offset = (argc > 0) ? (int)it2i(args[0]) : 0;
    bool sys_le = is_little_endian_system();

    // Getter methods
    if (ml == 7 && strncmp(mn, "getInt8", 7) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return (Item){.item = ITEM_NULL};
        return (Item){.item = i2it((int64_t)(int8_t)*p)};
    }
    if (ml == 8 && strncmp(mn, "getUint8", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return (Item){.item = ITEM_NULL};
        return (Item){.item = i2it((int64_t)*p)};
    }
    if (ml == 8 && strncmp(mn, "getInt16", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint16_t raw;
        memcpy(&raw, p, 2);
        if (little_endian != sys_le) raw = swap16(raw);
        return (Item){.item = i2it((int64_t)(int16_t)raw)};
    }
    if (ml == 9 && strncmp(mn, "getUint16", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint16_t raw;
        memcpy(&raw, p, 2);
        if (little_endian != sys_le) raw = swap16(raw);
        return (Item){.item = i2it((int64_t)raw)};
    }
    if (ml == 8 && strncmp(mn, "getInt32", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint32_t raw;
        memcpy(&raw, p, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        return (Item){.item = i2it((int64_t)(int32_t)raw)};
    }
    if (ml == 9 && strncmp(mn, "getUint32", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint32_t raw;
        memcpy(&raw, p, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        return (Item){.item = i2it((int64_t)raw)};
    }
    if (ml == 10 && strncmp(mn, "getFloat32", 10) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint32_t raw;
        memcpy(&raw, p, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        float f;
        memcpy(&f, &raw, 4);
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = (double)f;
        return (Item){.item = d2it(fp)};
    }
    if (ml == 10 && strncmp(mn, "getFloat64", 10) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 1) ? (it2i(args[1]) != 0) : false;
        uint64_t raw;
        memcpy(&raw, p, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        double d;
        memcpy(&d, &raw, 8);
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = d;
        return (Item){.item = d2it(fp)};
    }

    // Setter methods
    if (ml == 7 && strncmp(mn, "setInt8", 7) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        *p = (uint8_t)(int8_t)it2i(args[1]);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 8 && strncmp(mn, "setUint8", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        *p = (uint8_t)it2i(args[1]);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 8 && strncmp(mn, "setInt16", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        uint16_t val = (uint16_t)(int16_t)it2i(args[1]);
        if (little_endian != sys_le) val = swap16(val);
        memcpy(p, &val, 2);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 9 && strncmp(mn, "setUint16", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        uint16_t val = (uint16_t)it2i(args[1]);
        if (little_endian != sys_le) val = swap16(val);
        memcpy(p, &val, 2);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 8 && strncmp(mn, "setInt32", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        uint32_t val = (uint32_t)(int32_t)it2i(args[1]);
        if (little_endian != sys_le) val = swap32(val);
        memcpy(p, &val, 4);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 9 && strncmp(mn, "setUint32", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        uint32_t val = (uint32_t)it2i(args[1]);
        if (little_endian != sys_le) val = swap32(val);
        memcpy(p, &val, 4);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 10 && strncmp(mn, "setFloat32", 10) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        float f = (float)it2d(args[1]);
        uint32_t raw;
        memcpy(&raw, &f, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        memcpy(p, &raw, 4);
        return (Item){.item = ITEM_NULL};
    }
    if (ml == 10 && strncmp(mn, "setFloat64", 10) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p || argc < 2) return (Item){.item = ITEM_NULL};
        bool little_endian = (argc > 2) ? (it2i(args[2]) != 0) : false;
        double d = it2d(args[1]);
        uint64_t raw;
        memcpy(&raw, &d, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        memcpy(p, &raw, 8);
        return (Item){.item = ITEM_NULL};
    }

    log_error("DataView: unknown method '%.*s'", ml, mn);
    return (Item){.item = ITEM_NULL};
}
