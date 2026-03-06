/**
 * JavaScript Typed Array Implementation for Lambda v5
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

// Sentinel marker for typed array identification
static TypeMap js_typed_array_type_marker = {};
char js_typed_array_marker = 'T';

static int typed_array_element_size(JsTypedArrayType type) {
    switch (type) {
    case JS_TYPED_INT8:
    case JS_TYPED_UINT8:
        return 1;
    case JS_TYPED_INT16:
    case JS_TYPED_UINT16:
        return 2;
    case JS_TYPED_INT32:
    case JS_TYPED_UINT32:
    case JS_TYPED_FLOAT32:
        return 4;
    case JS_TYPED_FLOAT64:
        return 8;
    default:
        return 4;
    }
}

extern "C" bool js_is_typed_array(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->type == (void*)&js_typed_array_type_marker;
}

extern "C" Item js_typed_array_new(int type_id, int length) {
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = typed_array_element_size(arr_type);
    int byte_length = length * elem_size;

    // Allocate the JsTypedArray struct
    JsTypedArray* ta = (JsTypedArray*)malloc(sizeof(JsTypedArray));
    ta->element_type = arr_type;
    ta->length = length;
    ta->byte_length = byte_length;
    ta->data = calloc(length, elem_size);  // zero-initialized

    // Wrap in a Map with sentinel marker (same pattern as DOM wrappers)
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->type = (void*)&js_typed_array_type_marker;
    m->data = ta;
    m->data_cap = 0;

    return (Item){.map = m};
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

    // Convert value to number
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
    case JS_TYPED_INT8:
        ((int8_t*)ta->data)[idx] = (int8_t)(int32_t)num_val;
        break;
    case JS_TYPED_UINT8:
        ((uint8_t*)ta->data)[idx] = (uint8_t)(uint32_t)num_val;
        break;
    case JS_TYPED_INT16:
        ((int16_t*)ta->data)[idx] = (int16_t)(int32_t)num_val;
        break;
    case JS_TYPED_UINT16:
        ((uint16_t*)ta->data)[idx] = (uint16_t)(uint32_t)num_val;
        break;
    case JS_TYPED_INT32:
        ((int32_t*)ta->data)[idx] = (int32_t)num_val;
        break;
    case JS_TYPED_UINT32:
        ((uint32_t*)ta->data)[idx] = (uint32_t)num_val;
        break;
    case JS_TYPED_FLOAT32:
        ((float*)ta->data)[idx] = (float)num_val;
        break;
    case JS_TYPED_FLOAT64:
        ((double*)ta->data)[idx] = num_val;
        break;
    }

    return value;
}

extern "C" int js_typed_array_length(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return 0;
    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    return ta->length;
}

extern "C" Item js_typed_array_fill(Item ta_item, Item value) {
    if (!js_is_typed_array(ta_item)) return ta_item;

    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;

    // Fill every element
    for (int i = 0; i < ta->length; i++) {
        Item idx = (Item){.item = i2it(i)};
        js_typed_array_set(ta_item, idx, value);
    }

    return ta_item;
}

extern "C" Item js_typed_array_subarray(Item ta_item, int start, int end) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};

    Map* m = ta_item.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;

    if (start < 0) start = ta->length + start;
    if (end < 0) end = ta->length + end;
    if (start < 0) start = 0;
    if (end > ta->length) end = ta->length;
    if (start >= end) {
        return js_typed_array_new((int)ta->element_type, 0);
    }

    int new_length = end - start;
    Item result = js_typed_array_new((int)ta->element_type, new_length);
    Map* rm = result.map;
    JsTypedArray* rta = (JsTypedArray*)rm->data;

    int elem_size = typed_array_element_size(ta->element_type);
    memcpy(rta->data, (char*)ta->data + start * elem_size, new_length * elem_size);

    return result;
}
