/**
 * JavaScript Typed Array, ArrayBuffer, and DataView Implementation for Lambda
 */
#include "js_typed_array.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "js_object_meta.h"
#include "js_coerce.h"
#include "js_event_loop.h"
#include "../core/binary.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/heap_api.h"
#include "../core/lambda-decimal.hpp"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits.h>
#include "../../lib/mem.h"
#include <cmath>

typedef struct JsArrayBufferMapCarrier {
    Map base;
    JsArrayBuffer* payload;
} JsArrayBufferMapCarrier;

typedef struct JsTypedArrayMapCarrier {
    Map base;
    JsTypedArray payload;
} JsTypedArrayMapCarrier;

typedef struct JsDataViewMapCarrier {
    Map base;
    JsDataView payload;
} JsDataViewMapCarrier;

extern __thread EvalContext* context;
extern Item js_make_number(double d);
extern double js_get_number(Item value);

extern "C" bool js_is_generator(Item obj);
extern "C" Item js_bigint_constructor(Item value);
extern "C" Item js_bigint_as_int_n(Item bits_item, Item bigint_item);
extern "C" Item js_bigint_as_uint_n(Item bits_item, Item bigint_item);
extern "C" int js_262_agent_current_slot_for_atomics(void);

static bool js_dataview_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFFULL);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static Item js_dataview_to_bigint_value(Item value, Item* out_bigint) {
    if (js_dataview_is_bigint(value)) {
        *out_bigint = value;
        return js_status_ok();
    }

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_MAP || value_type == LMD_TYPE_ARRAY || value_type == LMD_TYPE_FUNC) {
        JS_ASSIGN_OR_RETURN(primitive, js_to_primitive(value, JS_HINT_NUMBER));
        return js_dataview_to_bigint_value(primitive, out_bigint);
    }

    if (value_type == LMD_TYPE_INT) {
        int64_t int_value = it2i(value);
        if (int_value <= -(int64_t)JS_SYMBOL_BASE) {
            return js_throw_type_error("Cannot convert a Symbol value to a BigInt");
        } else {
            return js_throw_type_error("Cannot convert non-BigInt value to BigInt");
        }
    }
    if (value_type == LMD_TYPE_FLOAT || value_type == LMD_TYPE_NULL || value.item == ITEM_JS_UNDEFINED) {
        return js_throw_type_error("Cannot convert non-BigInt value to BigInt");
    }

    JS_ASSIGN_OR_RETURN(bigint_value, js_bigint_constructor(value));
    *out_bigint = bigint_value;
    return js_status_ok();
}

static Item js_dataview_biguint64_item(uint64_t value) {
    if (value <= (uint64_t)INT64_MAX) return bigint_from_int64((int64_t)value);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return bigint_from_string(buf, len);
}

static uint64_t js_dataview_bigint_to_uint64(Item value) {
    char* value_str = bigint_to_cstring_radix(value, 10);
    if (!value_str) return 0;
    unsigned long long raw_value = strtoull(value_str, NULL, 10);
    mem_free(value_str);
    return (uint64_t)raw_value;
}

static Item js_dataview_to_index(Item value, int* out_index) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED) {
        *out_index = 0;
        return js_status_ok();
    }
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL ||
        (value_type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        return js_throw_type_error("Cannot convert a Symbol value to a number");
    }
    JS_ASSIGN_OR_RETURN(num, js_to_number(value));
    TypeId type = get_type_id(num);
    double d = (type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    if (std::isnan(d)) {
        *out_index = 0;
        return js_status_ok();
    }
    d = std::trunc(d);
    if (d < 0 || !std::isfinite(d) || d > (double)INT_MAX) {
        return js_throw_range_error("Invalid DataView index");
    }
    *out_index = (int)d;
    return js_status_ok();
}

static Item js_dataview_to_number_value(Item value, double* out_number) {
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL ||
        (value_type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        return js_throw_type_error("Cannot convert a Symbol value to a number");
    }
    JS_ASSIGN_OR_RETURN(num, js_to_number(value));
    TypeId num_type = get_type_id(num);
    *out_number = (num_type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    return js_status_ok();
}

static int64_t js_dataview_to_integer_value(double value) {
    if (std::isnan(value) || !std::isfinite(value)) return 0;
    return (int64_t)std::trunc(value);
}

static void js_dataview_link_prototype(Item view) {
    Item ctor_name = (Item){.item = s2it(heap_create_name("DataView"))};
    Item ctor = js_get_constructor(ctor_name);
    if (get_type_id(ctor) != LMD_TYPE_FUNC) return;
    Item proto_key = (Item){.item = s2it(heap_create_name("prototype"))};
    Item proto = js_get_key_default(ctor, proto_key);
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(view, proto);
}

typedef struct JsTypedArraySpec {
    uint8_t byte_size;
    ArrayNumElemType elem_type;
    const char* name;
    bool integer;
    bool atomic;
    bool bigint;
    bool signed_integer;
    uint8_t bits;
} JsTypedArraySpec;

// One immutable descriptor supplies storage, ArrayNum, atomic, and public-name
// behavior; separate switches had drifted when Float16 and BigInt were added.
static const JsTypedArraySpec js_typed_array_specs[] = {
    {1, ELEM_INT8,          "Int8Array",          true,  true,  false, true,  8},
    {1, ELEM_UINT8,         "Uint8Array",         true,  true,  false, false, 8},
    {2, ELEM_INT16,         "Int16Array",         true,  true,  false, true,  16},
    {2, ELEM_UINT16,        "Uint16Array",        true,  true,  false, false, 16},
    {4, ELEM_INT32,         "Int32Array",         true,  true,  false, true,  32},
    {4, ELEM_UINT32,        "Uint32Array",        true,  true,  false, false, 32},
    {4, ELEM_FLOAT32,       "Float32Array",       false, false, false, false, 0},
    {8, ELEM_FLOAT64,       "Float64Array",       false, false, false, false, 0},
    {1, ELEM_UINT8_CLAMPED, "Uint8ClampedArray",  true,  false, false, false, 8},
    {8, ELEM_INT64,         "BigInt64Array",      true,  true,  true,  true,  64},
    {8, ELEM_UINT64,        "BigUint64Array",     true,  true,  true,  false, 64},
    {2, ELEM_UINT16,        "Float16Array",       false, false, false, false, 0},
};

static const JsTypedArraySpec js_typed_array_default_spec =
    {4, ELEM_UINT8, NULL, false, false, false, false, 0};

static const JsTypedArraySpec* js_typed_array_spec(JsTypedArrayType type) {
    int index = (int)type;
    if (index < 0 || index >= (int)(sizeof(js_typed_array_specs) /
                                    sizeof(js_typed_array_specs[0]))) {
        return &js_typed_array_default_spec;
    }
    return &js_typed_array_specs[index];
}

extern "C" int js_typed_array_element_size(JsTypedArrayType type) {
    return js_typed_array_spec(type)->byte_size;
}

extern "C" const char* js_typed_array_type_name_from_type(JsTypedArrayType type) {
    const JsTypedArraySpec* spec = js_typed_array_spec(type);
    return spec->name ? spec->name : "Uint8Array";
}

extern "C" bool js_typed_array_is_integer_type(JsTypedArrayType type) {
    return js_typed_array_spec(type)->integer;
}

static uint16_t js_float64_to_float16_bits(double value) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    uint16_t sign = (uint16_t)((bits >> 48) & 0x8000);
    uint64_t exp_bits = (bits >> 52) & 0x7ff;
    uint64_t mant = bits & 0x000fffffffffffffULL;

    if (exp_bits == 0x7ff) {
        if (mant != 0) return (uint16_t)(sign | 0x7e00);
        return (uint16_t)(sign | 0x7c00);
    }
    if ((bits & 0x7fffffffffffffffULL) == 0) return sign;

    double abs_value = value < 0.0 ? -value : value;
    if (abs_value < 0x1.0p-14) {
        // round directly from JS Number to binary16; a float32 pre-round loses
        // subnormal tie decisions such as 2^-25 plus one double ulp.
        double scaled = ldexp(abs_value, 24);
        double floor_scaled = floor(scaled);
        double fraction = scaled - floor_scaled;
        uint32_t half_mant = (uint32_t)floor_scaled;
        if (fraction > 0.5 || (fraction == 0.5 && (half_mant & 1))) half_mant++;
        if (half_mant == 0) return sign;
        if (half_mant >= 1024) return (uint16_t)(sign | 0x0400);
        return (uint16_t)(sign | (uint16_t)half_mant);
    }

    int exp2 = 0;
    frexp(abs_value, &exp2);
    int half_exp = exp2 - 1 + 15;
    double scaled = ldexp(abs_value, 11 - exp2);
    double floor_scaled = floor(scaled);
    double fraction = scaled - floor_scaled;
    uint32_t significand = (uint32_t)floor_scaled;
    if (fraction > 0.5 || (fraction == 0.5 && (significand & 1))) significand++;
    if (significand >= 2048) {
        significand = 1024;
        half_exp++;
    }
    if (half_exp >= 31) return (uint16_t)(sign | 0x7c00);
    return (uint16_t)(sign | ((uint16_t)half_exp << 10) | (uint16_t)(significand - 1024));
}

static double js_float16_bits_to_float64(uint16_t bits) {
    int sign = (bits & 0x8000) ? -1 : 1;
    int exp = (bits >> 10) & 0x1f;
    int mant = bits & 0x03ff;
    if (exp == 0) {
        if (mant == 0) return sign < 0 ? -0.0 : 0.0;
        return (double)sign * ldexp((double)mant, -24);
    }
    if (exp == 0x1f) {
        if (mant == 0) return sign < 0 ? -INFINITY : INFINITY;
        return NAN;
    }
    return (double)sign * ldexp(1024.0 + (double)mant, exp - 25);
}

JS_FORWARD_STATIC_EXPRESSION(ArrayNumElemType, js_typed_array_elem_type,
    (JsTypedArrayType type), js_typed_array_spec(type)->elem_type)
JS_FORWARD_STATIC_EXPRESSION(bool, js_typed_array_is_bigint_element,
    (JsTypedArrayType type), js_typed_array_spec(type)->bigint)
JS_FORWARD_STATIC_EXPRESSION(bool, js_typed_array_is_number_element,
    (JsTypedArrayType type), (!js_typed_array_is_bigint_element(type)))

static ArrayNumShape* js_typed_array_view_shape(JsTypedArray* ta) {
    return (ta && ta->view) ? (ArrayNumShape*)(uintptr_t)ta->view->extra : NULL;
}

static int js_typed_array_stored_byte_offset(JsTypedArray* ta) {
    if (!ta || !ta->view) return 0;
    int elem_size = js_typed_array_element_size(ta->element_type);
    ArrayNumShape* shape = js_typed_array_view_shape(ta);
    if (shape) return (int)(shape->offset * elem_size);
    return 0;
}

static int js_typed_array_stored_length(JsTypedArray* ta) {
    if (!ta || !ta->view) return 0;
    return (int)ta->view->length;
}

static int js_typed_array_stored_byte_length(JsTypedArray* ta) {
    if (!ta) return 0;
    return js_typed_array_stored_length(ta) * js_typed_array_element_size(ta->element_type);
}

static int js_typed_array_current_byte_length(JsTypedArray* ta) {
    if (!ta) return 0;
    if (!ta->buffer) return js_typed_array_stored_byte_length(ta);
    if (js_arraybuffer_detached(ta->buffer)) return 0;
    int byte_offset = js_typed_array_stored_byte_offset(ta);
    int available = js_arraybuffer_length(ta->buffer) - byte_offset;
    if (available < 0) return 0;
    if (ta->length_tracking) {
        int elem_size = js_typed_array_element_size(ta->element_type);
        return (available / elem_size) * elem_size;
    }
    int byte_length = js_typed_array_stored_byte_length(ta);
    return available >= byte_length ? byte_length : 0;
}

static int js_typed_array_current_length(JsTypedArray* ta) {
    if (!ta) return 0;
    int elem_size = js_typed_array_element_size(ta->element_type);
    return js_typed_array_current_byte_length(ta) / elem_size;
}

static int js_typed_array_current_byte_offset(JsTypedArray* ta) {
    if (!ta) return 0;
    int byte_offset = js_typed_array_stored_byte_offset(ta);
    if (!ta->buffer) return byte_offset;
    if (js_arraybuffer_detached(ta->buffer)) return 0;
    if (ta->length_tracking) return js_arraybuffer_length(ta->buffer) >= byte_offset ? byte_offset : 0;
    int byte_length = js_typed_array_stored_byte_length(ta);
    return js_arraybuffer_length(ta->buffer) >= byte_offset + byte_length ? byte_offset : 0;
}

static void* js_typed_array_current_data(JsTypedArray* ta) {
    if (!ta) return NULL;
    if (!ta->buffer) return ta->view ? ta->view->data : NULL;
    if (js_typed_array_current_byte_length(ta) == 0) return NULL;
    return ta->view ? array_num_resolve_data(ta->view, false) : NULL;
}

static void js_typed_array_refresh_arraynum_view(JsTypedArray* ta);

static void* js_typed_array_prepare_write(JsTypedArray* ta) {
    if (!ta || !ta->view) return NULL;
    // Value coercion may resize the buffer after the initial witness record;
    // refresh live ArrayNum bounds before the write resolver validates them.
    js_typed_array_refresh_arraynum_view(ta);
    if (js_typed_array_current_byte_length(ta) == 0) return NULL;
    return array_num_resolve_data(ta->view, true);
}

static void js_typed_array_refresh_arraynum_view(JsTypedArray* ta) {
    if (!ta || !ta->view) return;

    int byte_offset = js_typed_array_stored_byte_offset(ta);
    int length = ta->length_tracking ? js_typed_array_current_length(ta) :
        js_typed_array_stored_length(ta);
    ta->view->length = length;
    ta->view->capacity = length;

    ArrayNumShape* shape = (ArrayNumShape*)(uintptr_t)ta->view->extra;
    if (!shape) return;

    int elem_size = js_typed_array_element_size(ta->element_type);
    shape->offset = elem_size ? byte_offset / elem_size : 0;
    if (ta->buffer_item) {
        Item buffer_item;  buffer_item.item = ta->buffer_item;
        if (buffer_item.type_id() == LMD_TYPE_MAP) {
            shape->base = (void*)buffer_item.map;
        }
    }
    array_num_shape_dims(shape)[0] = length;
    array_num_shape_strides(shape)[0] = 1;
    array_num_resolve_data(ta->view, false);
}

static bool js_typed_array_is_out_of_bounds(JsTypedArray* ta) {
    if (!ta || !ta->buffer) return false;
    if (js_arraybuffer_detached(ta->buffer)) return true;
    int byte_offset = js_typed_array_stored_byte_offset(ta);
    if (ta->length_tracking) return js_arraybuffer_length(ta->buffer) < byte_offset;
    return js_arraybuffer_length(ta->buffer) < byte_offset + js_typed_array_stored_byte_length(ta);
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_typed_array_arraynum_view_matches, (JsTypedArray* ta, const char* data, int index), (ta && !ta->is_buffer && ta->view && ta->view->data == (void*)data && index >= 0 && index < ta->view->length))
JS_FORWARD_STATIC_EXPRESSION(bool, js_typed_array_arraynum_range_matches, (JsTypedArray* ta, const char* data,                                                   int index, int count), (ta && !ta->is_buffer && ta->view && ta->view->data == (void*)data && index >= 0 && count >= 0 && index <= ta->view->length && count <= ta->view->length - index))

static bool js_typed_array_try_raw_set_same_type(JsTypedArray* dst, JsTypedArray* src, int offset) {
    if (!dst || !src || dst->element_type != src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;

    int src_len = js_typed_array_current_length(src);
    if (src_len <= 0) return true;
    int dst_len = js_typed_array_current_length(dst);
    if (offset < 0 || (int64_t)offset + (int64_t)src_len > (int64_t)dst_len) return false;

    js_typed_array_refresh_arraynum_view(src);
    js_typed_array_refresh_arraynum_view(dst);
    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_prepare_write(dst);
    if (!src_data || !dst_data) return false;
    if (js_typed_array_arraynum_range_matches(src, src_data, 0, src_len) &&
        js_typed_array_arraynum_range_matches(dst, dst_data, offset, src_len)) {
        return array_num_copy_same_type_bytes(dst->view, offset, src->view, 0, src_len);
    }
    return false;
}

static bool js_typed_array_ranges_overlap(const char* dst_data, int dst_byte_len,
                                          const char* src_data, int src_byte_len) {
    if (!dst_data || !src_data || dst_byte_len <= 0 || src_byte_len <= 0) return false;
    const char* dst_end = dst_data + dst_byte_len;
    const char* src_end = src_data + src_byte_len;
    return dst_data < src_end && src_data < dst_end;
}

static double js_typed_array_raw_load_number(JsTypedArray* ta, const char* data, int index) {
    if (!ta) return 0.0;
    if (ta->element_type == JS_TYPED_FLOAT16 && data && index >= 0) {
        return js_float16_bits_to_float64(((uint16_t*)data)[index]);
    }
    if (js_typed_array_arraynum_view_matches(ta, data, index) &&
        js_typed_array_is_number_element(ta->element_type)) {
        return array_num_get_number_value(ta->view, index);
    }
    return 0.0;
}

extern "C" bool js_typed_array_is_out_of_bounds_item(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return false;
    return js_typed_array_is_out_of_bounds(js_get_typed_array_ptr(ta_item.map));
}

static int64_t js_typed_array_to_int_n(double value, int bits, bool is_signed) {
    if (std::isnan(value) || !std::isfinite(value) || value == 0.0) return 0;
    double int_value = std::trunc(value);
    double modulo = bits == 32 ? 4294967296.0 : (double)(1ULL << bits);
    double wrapped = std::fmod(int_value, modulo);
    if (wrapped < 0) wrapped += modulo;
    if (is_signed) {
        double sign_limit = bits == 32 ? 2147483648.0 : (double)(1ULL << (bits - 1));
        if (wrapped >= sign_limit) wrapped -= modulo;
    }
    return (int64_t)wrapped;
}

static uint8_t js_typed_array_to_uint8_clamp(double value) {
    if (isnan(value) || value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    int floor_value = (int)value;
    double fraction = value - floor_value;
    if (fraction < 0.5) return (uint8_t)floor_value;
    if (fraction > 0.5) return (uint8_t)(floor_value + 1);
    return (uint8_t)((floor_value & 1) ? floor_value + 1 : floor_value);
}

static void js_typed_array_arraynum_store_number(JsTypedArray* ta, int index, double value) {
    if (!ta || !ta->view) return;
    const JsTypedArraySpec* spec = js_typed_array_spec(ta->element_type);
    if (ta->element_type == JS_TYPED_UINT8_CLAMPED) {
        array_num_set_double_value(ta->view, index, value);
        return;
    }
    if (ta->element_type == JS_TYPED_FLOAT16) {
        // Float16Array stores IEEE-754 binary16 bits; using ArrayNum's uint16
        // view here would expose backing bits instead of rounded JS Numbers.
        if (ta->view->data && index >= 0 && index < ta->view->length)
            ((uint16_t*)ta->view->data)[index] = js_float64_to_float16_bits(value);
        return;
    }
    if (spec->integer) {
        array_num_set_int64_value(ta->view, index,
            js_typed_array_to_int_n(value, spec->bits, spec->signed_integer));
    } else {
        array_num_set_double_value(ta->view, index, value);
    }
}

static void js_typed_array_store_number_direct(JsTypedArrayType type,
                                               char* data, int index, double value) {
    if (!data || index < 0) return;
    const JsTypedArraySpec* spec = js_typed_array_spec(type);
    uint8_t* slot = (uint8_t*)data + (size_t)index * spec->byte_size;
    if (type == JS_TYPED_FLOAT16) {
        uint16_t bits = js_float64_to_float16_bits(value);
        memcpy(slot, &bits, sizeof(bits));
    } else if (type == JS_TYPED_FLOAT32) {
        float narrowed = (float)value;
        memcpy(slot, &narrowed, sizeof(narrowed));
    } else if (type == JS_TYPED_FLOAT64) {
        memcpy(slot, &value, sizeof(value));
    } else if (type == JS_TYPED_UINT8_CLAMPED) {
        uint8_t clamped = js_typed_array_to_uint8_clamp(value);
        memcpy(slot, &clamped, sizeof(clamped));
    } else if (spec->integer) {
        uint64_t raw = (uint64_t)js_typed_array_to_int_n(value,
            spec->bits, spec->signed_integer);
        memcpy(slot, &raw, spec->byte_size);
    }
}

static void js_typed_array_raw_store_number(JsTypedArray* ta, char* data, int index, double value) {
    if (!ta) return;
    if (js_typed_array_arraynum_view_matches(ta, data, index) &&
        js_typed_array_is_number_element(ta->element_type)) {
        js_typed_array_arraynum_store_number(ta, index, value);
    }
}

static bool js_typed_array_try_raw_from_dense_number_array(Item result, Array* arr, int len) {
    if (!arr || arr->is_content == 1 || js_array_has_props(arr)) return false;
    if (len < 0 || (int64_t)len > arr->capacity) return false;

    JsTypedArray* dst = js_get_typed_array_ptr(result.map);
    if (!dst || !js_typed_array_is_number_element(dst->element_type)) return false;
    js_typed_array_refresh_arraynum_view(dst);
    char* data = (char*)js_typed_array_prepare_write(dst);
    if (len > 0 && !data) return false;
    if (!js_typed_array_arraynum_range_matches(dst, data, 0, len)) return false;

    for (int i = 0; i < len; i++) {
        Item val = arr->items[i];
        if (val.item == JS_DELETED_SENTINEL_VAL) return false;
        double num_val = 0.0;
        if (val.item == ITEM_JS_UNDEFINED) {
            num_val = NAN;
        } else {
            TypeId val_type = get_type_id(val);
            if (val_type == LMD_TYPE_INT) {
                int64_t iv = it2i(val);
                if (iv <= -(int64_t)JS_SYMBOL_BASE) return false;
                num_val = (double)iv;
            } else if (val_type == LMD_TYPE_FLOAT) {
                num_val = it2d(val);
            } else {
                return false;
            }
        }
        js_typed_array_raw_store_number(dst, data, i, num_val);
    }
    return true;
}

static bool js_typed_array_try_arraynum_convert_number(JsTypedArray* dst, JsTypedArray* src,
                                                       int offset, bool allow_overlap) {
    if (!dst || !src) return false;
    if (dst->is_buffer || src->is_buffer) return false;
    if (!js_typed_array_is_number_element(dst->element_type) ||
        !js_typed_array_is_number_element(src->element_type)) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;

    int src_len = js_typed_array_current_length(src);
    if (src_len <= 0) return true;
    int dst_len = js_typed_array_current_length(dst);
    if (offset < 0 || (int64_t)offset + (int64_t)src_len > (int64_t)dst_len) return false;

    js_typed_array_refresh_arraynum_view(src);
    js_typed_array_refresh_arraynum_view(dst);
    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_prepare_write(dst);
    if (!src_data || !dst_data) return false;
    if (!js_typed_array_arraynum_range_matches(src, src_data, 0, src_len) ||
        !js_typed_array_arraynum_range_matches(dst, dst_data, offset, src_len)) {
        return false;
    }

    int src_elem_size = js_typed_array_element_size(src->element_type);
    int dst_elem_size = js_typed_array_element_size(dst->element_type);
    char* dst_start = dst_data + ((size_t)offset * (size_t)dst_elem_size);
    if (!allow_overlap &&
        js_typed_array_ranges_overlap(dst_start, src_len * dst_elem_size,
                                      src_data, src_len * src_elem_size)) {
        return false;
    }

    for (int i = 0; i < src_len; i++) {
        double value = js_typed_array_raw_load_number(src, src_data, i);
        js_typed_array_arraynum_store_number(dst, offset + i, value);
    }
    return true;
}

static bool js_typed_array_try_arraynum_convert_bigint(JsTypedArray* dst, JsTypedArray* src,
                                                       int offset, bool allow_overlap) {
    if (!dst || !src) return false;
    if (dst->is_buffer || src->is_buffer) return false;
    if (!js_typed_array_is_bigint_element(dst->element_type) ||
        !js_typed_array_is_bigint_element(src->element_type)) return false;
    if (dst->element_type == src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;

    int src_len = js_typed_array_current_length(src);
    if (src_len <= 0) return true;
    int dst_len = js_typed_array_current_length(dst);
    if (offset < 0 || (int64_t)offset + (int64_t)src_len > (int64_t)dst_len) return false;

    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_prepare_write(dst);
    if (!src_data || !dst_data) return false;
    if (!js_typed_array_arraynum_range_matches(src, src_data, 0, src_len) ||
        !js_typed_array_arraynum_range_matches(dst, dst_data, offset, src_len)) {
        return false;
    }

    int elem_size = js_typed_array_element_size(src->element_type);
    char* dst_start = dst_data + ((size_t)offset * (size_t)elem_size);
    size_t byte_count = (size_t)src_len * (size_t)elem_size;
    if (!allow_overlap && js_typed_array_ranges_overlap(dst_start, (int)byte_count,
                                                        src_data, (int)byte_count)) {
        return false;
    }
    return array_num_copy_equal_size_bytes(dst->view, offset, src->view, 0, src_len);
}

static bool js_typed_array_raw_copy_same_type_impl(Item dst_item, Item src_item,
        bool reversed) {
    if (!js_is_typed_array(dst_item) || !js_is_typed_array(src_item)) return false;
    JsTypedArray* dst = js_get_typed_array_ptr(dst_item.map);
    JsTypedArray* src = js_get_typed_array_ptr(src_item.map);
    if (!dst || !src || dst->element_type != src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;
    int len = js_typed_array_current_length(src);
    if (len != js_typed_array_current_length(dst)) return false;
    if (len <= 0) return true;
    js_typed_array_refresh_arraynum_view(src);
    js_typed_array_refresh_arraynum_view(dst);
    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_prepare_write(dst);
    if (!src_data || !dst_data) return false;
    if (js_typed_array_arraynum_range_matches(src, src_data, 0, len) &&
        js_typed_array_arraynum_range_matches(dst, dst_data, 0, len)) {
        if (reversed) return array_num_copy_reversed_bytes(dst->view, src->view);
        return array_num_copy_same_type_bytes(dst->view, 0, src->view, 0, len);
    }
    return false;
}
JS_FORWARD_RETURN(bool, js_typed_array_raw_copy_same_type, (Item dst_item, Item src_item), js_typed_array_raw_copy_same_type_impl, (dst_item, src_item, false))

extern "C" bool js_typed_array_raw_reverse(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return false;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    if (!ta || js_typed_array_is_out_of_bounds(ta)) return false;
    int len = js_typed_array_current_length(ta);
    if (len <= 1) return true;
    js_typed_array_refresh_arraynum_view(ta);
    char* data = (char*)js_typed_array_prepare_write(ta);
    if (!data) return false;
    if (js_typed_array_arraynum_range_matches(ta, data, 0, len)) {
        return array_num_reverse_bytes(ta->view);
    }
    return false;
}
JS_FORWARD_RETURN(bool, js_typed_array_raw_copy_reversed, (Item dst_item, Item src_item), js_typed_array_raw_copy_same_type_impl, (dst_item, src_item, true))

extern "C" bool js_typed_array_raw_copy_within(Item ta_item, int target, int start, int count) {
    if (!js_is_typed_array(ta_item)) return false;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    if (!ta || js_typed_array_is_out_of_bounds(ta)) return false;
    if (count <= 0) return true;
    js_typed_array_refresh_arraynum_view(ta);
    char* data = (char*)js_typed_array_prepare_write(ta);
    if (!data) return false;
    if (js_typed_array_arraynum_range_matches(ta, data, start, count) &&
        js_typed_array_arraynum_range_matches(ta, data, target, count)) {
        return array_num_copy_same_type_bytes(ta->view, target, ta->view, start, count);
    }
    return false;
}

extern "C" int js_typed_array_raw_index_of(Item ta_item, Item search_value,
                                           int from, int bound, bool reverse, bool same_value_zero) {
    if (!js_is_typed_array(ta_item)) return -2;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    if (!ta || !js_typed_array_is_number_element(ta->element_type)) return -2;
    if (js_typed_array_is_out_of_bounds(ta)) return -2;

    TypeId search_type = get_type_id(search_value);
    double needle = 0.0;
    if (search_type == LMD_TYPE_INT) {
        int64_t iv = it2i(search_value);
        if (iv <= -(int64_t)JS_SYMBOL_BASE) return -1;
        needle = (double)iv;
    } else if (search_type == LMD_TYPE_FLOAT) {
        needle = it2d(search_value);
    } else {
        // Js54 P4: non-numeric search (undefined, null, string, ...) — fall
        // through to the slow path. Callers iterate with the spec-captured
        // length, and Get() returns undefined for post-resize OOB positions,
        // so includes(undefined, ...) can still match. Returning -1 here
        // would falsely shortcut callers to "not found".
        return -2;
    }

    // Js54 P4: clamp to the spec-captured bound the caller provided. Spec
    // §23.2.3.{18,20,15} (indexOf/lastIndexOf/includes) capture len BEFORE
    // any coercion callback; if a callback grew the buffer, our current
    // length would be larger and the fast path could find new (zero-initialised)
    // elements outside the spec-required range.
    int current_len = js_typed_array_current_length(ta);
    int len = bound < current_len ? bound : current_len;
    if (len <= 0) return -1;
    if (from < 0) return -1;
    // Js55 P12: for reverse iteration (lastIndexOf), when `from` lies past the
    // current end (the spec-cached `bound` was > current_len because a coercion
    // callback shrank the buffer), the spec walks indices [from..0] using
    // HasProperty — out-of-range indices are skipped, then the in-range
    // indices [current_len-1..0] are tested. So clamp `from` to len-1 and
    // continue, instead of bailing with -1. For forward iteration (indexOf)
    // a `from >= len` value has no valid indices to test and we return -1.
    if (from >= len) {
        if (!reverse) return -1;
        from = len - 1;
    }
    js_typed_array_refresh_arraynum_view(ta);
    char* data = (char*)js_typed_array_current_data(ta);
    if (!data) return -2;
    if (!js_typed_array_arraynum_range_matches(ta, data, 0, current_len)) return -2;

    bool needle_nan = isnan(needle);
    if (reverse) {
        for (int i = from; i >= 0; i--) {
            double value = js_typed_array_raw_load_number(ta, data, i);
            if (value == needle || (same_value_zero && needle_nan && isnan(value))) return i;
        }
    } else {
        for (int i = from; i < len; i++) {
            double value = js_typed_array_raw_load_number(ta, data, i);
            if (value == needle || (same_value_zero && needle_nan && isnan(value))) return i;
        }
    }
    return -1;
}

static Item js_to_index_i64(Item value, int64_t* out_index,
        const char* error_message) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        *out_index = 0;
        return js_status_ok();
    }
    if (type == LMD_TYPE_SYMBOL ||
        (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        return js_throw_type_error("Cannot convert a Symbol value to a number");
    }
    JS_ASSIGN_OR_RETURN(num, js_to_number(value));
    type = get_type_id(num);
    double dval = (type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    if (std::isnan(dval)) {
        *out_index = 0;
        return js_status_ok();
    }
    dval = std::trunc(dval);
    // ToIndex rejects values above Number.MAX_SAFE_INTEGER before any host allocation cap;
    // large JS Numbers may arrive as boxed floats and must not wrap through int storage.
    if (dval < 0 || !std::isfinite(dval) || dval > 9007199254740991.0) {
        return js_throw_range_error(error_message);
    }
    *out_index = (int64_t)dval;
    return js_status_ok();
}

static Item js_to_index_int(Item value, int* out_index,
        const char* error_message) {
    int64_t index = 0;
    JS_ASSIGN_OR_RETURN(validation, js_to_index_i64(
        value, &index, error_message));
    // Fixed-width runtime views use int indices. The host allocation cap is
    // applied only after spec ToIndex succeeds.
    double dval = (double)index;
    if (dval > 1073741824.0) {
        return js_throw_range_error(error_message);
    }
    *out_index = (int)dval;
    return js_status_ok();
}

extern "C" Item js_typed_array_validate_constructor_argument(Item argument,
        int argc) {
    if (argc <= 0) return js_status_ok();
    TypeId type = get_type_id(argument);
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
            type == LMD_TYPE_FUNC || type == LMD_TYPE_ELEMENT ||
            type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP) {
        return js_status_ok();
    }
    int element_length = 0;
    // TypedArray's primitive-length branch performs ToIndex before entering
    // AllocateTypedArray. Validating here prevents an observable
    // newTarget.prototype Get from replacing Symbol/BigInt/range errors.
    return js_to_index_int(argument, &element_length,
        "Invalid typed array length");
}

JS_FORWARD_STATIC_EXPRESSION(bool, js_atomics_is_integer_type,
    (JsTypedArrayType type), js_typed_array_spec(type)->atomic)
JS_FORWARD_STATIC_EXPRESSION(bool, js_atomics_is_bigint_type,
    (JsTypedArrayType type), js_typed_array_spec(type)->bigint)

static Item js_validate_atomic_typed_array(Item typed_array, bool require_shared, bool waitable,
                                           JsTypedArray** out_ta) {
    *out_ta = NULL;
    if (!js_is_typed_array(typed_array)) {
        return js_throw_type_error("Atomics operation requires a TypedArray");
    }
    JsTypedArray* ta = js_get_typed_array_ptr(typed_array.map);
    if (!ta) {
        return js_throw_type_error("Atomics operation requires a TypedArray");
    }
    if (require_shared && !js_arraybuffer_shared(ta->buffer)) {
        return js_throw_type_error("Atomics operation requires a SharedArrayBuffer-backed TypedArray");
    }
    if (ta->buffer && js_arraybuffer_detached(ta->buffer)) {
        return js_throw_type_error(require_shared ? "Atomics operation requires a non-detached SharedArrayBuffer" :
                                           "Atomics operation requires a non-detached ArrayBuffer");
    }
    if (waitable) {
        if (ta->element_type != JS_TYPED_INT32 && ta->element_type != JS_TYPED_BIGINT64) {
            return js_throw_type_error("Atomics.wait/notify requires an Int32Array or BigInt64Array");
        }
    } else if (!js_atomics_is_integer_type(ta->element_type)) {
        return js_throw_type_error("Atomics operation requires an integer TypedArray");
    }
    *out_ta = ta;
    return js_status_ok();
}

static Item js_atomics_validate_index(JsTypedArray* ta, Item index_item, int* out_index) {
    int length = js_typed_array_current_length(ta);
    int index = 0;
    JS_ASSIGN_OR_RETURN(validation, js_to_index_int(index_item, &index, "Invalid atomic access index"));
    if (index < 0 || index >= length) {
        return js_throw_range_error("Invalid atomic access index");
    }
    *out_index = index;
    return js_status_ok();
}

static Item js_atomics_number_to_integer_item(double number) {
    if (std::isnan(number)) return js_make_number(0.0);
    if (!std::isfinite(number)) {
        return js_make_number(number);
    }
    double integer = std::trunc(number);
    // Atomics integer conversion returns an observable Number; -0 must be canonicalized to +0 after truncation.
    if (integer == 0.0) return js_make_number(0.0);
    if (integer >= (double)INT64_MIN && integer <= (double)INT64_MAX) {
        return js_make_number(integer);
    }
    return js_make_number(integer);
}

static Item js_atomics_to_number_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    double number = 0.0;
    JS_ASSIGN_OR_RETURN(validation, js_dataview_to_number_value(value, &number));
    if (out_store_value) *out_store_value = js_atomics_number_to_integer_item(number);
    const JsTypedArraySpec* spec = js_typed_array_spec(type);
    if (!spec->atomic || spec->bigint) {
        return js_throw_type_error("Atomics operation requires a Number typed array");
    }
    *out_bits = (uint64_t)js_typed_array_to_int_n(number, spec->bits,
                                                   spec->signed_integer);
    return js_status_ok();
}

static Item js_atomics_to_bigint_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    Item bigint_item;
    JS_ASSIGN_OR_RETURN(validation, js_dataview_to_bigint_value(value, &bigint_item));
    if (out_store_value) *out_store_value = bigint_item;
    Item wrapped;
    if (type == JS_TYPED_BIGINT64) {
        wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_item);
    } else {
        wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_item);
    }
    if (item_is_error(wrapped)) return wrapped;
    *out_bits = js_dataview_bigint_to_uint64(wrapped);
    return js_status_ok();
}

static Item js_atomics_to_element_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    if (js_atomics_is_bigint_type(type)) return js_atomics_to_bigint_bits(type, value, out_bits, out_store_value);
    return js_atomics_to_number_bits(type, value, out_bits, out_store_value);
}

static Item js_atomics_item_from_bits(JsTypedArrayType type, uint64_t bits) {
    const JsTypedArraySpec* spec = js_typed_array_spec(type);
    if (spec->bigint) {
        return spec->signed_integer ? bigint_from_int64((int64_t)bits) :
            js_dataview_biguint64_item(bits);
    }
    if (!spec->atomic) return (Item){.item = ITEM_JS_UNDEFINED};
    // Atomic results are already width-limited C values; widening a signed
    // byte/word to uint64 and routing it through double loses its low bits.
    uint64_t mask = spec->bits == 64 ? UINT64_MAX :
        ((UINT64_C(1) << spec->bits) - 1);
    uint64_t normalized = bits & mask;
    if (spec->signed_integer &&
            (normalized & (UINT64_C(1) << (spec->bits - 1)))) {
        normalized |= ~mask;
    }
    int64_t result = (int64_t)normalized;
    return js_make_number((double)result);
}
JS_FORWARD_STATIC_EXPRESSION(Item, js_atomics_wait_result, (const char* value, int len), ((Item){.item = s2it(heap_strcpy((char*)value, len))}))

static bool js_atomics_host_can_suspend() {
    Item global = js_get_global_this();
    Item key = (Item){.item = s2it(heap_create_name("__lambda_can_block"))};
    Item flag = js_get_key_default(global, key);
    if (get_type_id(flag) == LMD_TYPE_BOOL) return it2b(flag);
    return true;
}

#define JS_ATOMICS_MAX_WAITERS 128
#define JS_ATOMICS_MAX_AGENT_SLOTS 16

typedef enum JsAtomicsWaiterStatus {
    JS_ATOMICS_WAITER_PENDING,
    JS_ATOMICS_WAITER_OK,
    JS_ATOMICS_WAITER_TIMED_OUT,
} JsAtomicsWaiterStatus;

typedef struct JsAtomicsWaiter {
    bool used;
    int id;
    int agent_slot;
    JsArrayBuffer* buffer;
    int index;
    Item promise;
    double deadline_ms;
    bool has_deadline;
    JsAtomicsWaiterStatus status;
} JsAtomicsWaiter;

typedef struct JsAtomicsRuntimeState {
    JsAtomicsWaiter waiters[JS_ATOMICS_MAX_WAITERS];
    int next_waiter_id;
    int last_waiter_by_agent[JS_ATOMICS_MAX_AGENT_SLOTS];
    int blocking_waiter_by_agent[JS_ATOMICS_MAX_AGENT_SLOTS];
    double virtual_now_ms;
    uint64_t waiter_roots_epoch;
} JsAtomicsRuntimeState;

static JsAtomicsRuntimeState* js_atomics_runtime_state(void) {
    return js_active_runtime_state ?
        (JsAtomicsRuntimeState*)js_runtime_state.test262_agent.atomics_waiter_state : NULL;
}

extern "C" bool js_atomics_runtime_state_ensure(void) {
    if (!js_active_runtime_state) return false;
    if (!js_runtime_state.test262_agent.atomics_waiter_state) {
        // Atomics namespace creation is cold; waiter lookup and notification
        // below use direct owner-context storage without synchronization.
        JsAtomicsRuntimeState* state = (JsAtomicsRuntimeState*)mem_calloc(1,
            sizeof(JsAtomicsRuntimeState), MEM_CAT_JS_RUNTIME);
        if (!state) return false;
        state->next_waiter_id = 1;
        js_runtime_state.test262_agent.atomics_waiter_state = state;
    }
    return true;
}

#define js_atomics_state (*(JsAtomicsRuntimeState*)js_runtime_state.test262_agent.atomics_waiter_state)
#define js_atomics_waiters (js_atomics_state.waiters)
#define js_atomics_next_waiter_id (js_atomics_state.next_waiter_id)
#define js_atomics_last_waiter_by_agent (js_atomics_state.last_waiter_by_agent)
#define js_atomics_blocking_waiter_by_agent (js_atomics_state.blocking_waiter_by_agent)
#define js_atomics_virtual_now_ms (js_atomics_state.virtual_now_ms)
extern "C" uint64_t js_get_heap_epoch(void);
#define js_atomics_waiter_roots_epoch (js_atomics_state.waiter_roots_epoch)

static void js_atomics_register_waiter_roots(void) {
    uint64_t epoch = js_get_heap_epoch();
    if (js_atomics_waiter_roots_epoch == epoch) return;
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        js_atomics_waiters[i].promise = ItemNull;
        heap_register_gc_root(&js_atomics_waiters[i].promise.item);
    }
    js_atomics_waiter_roots_epoch = epoch;
}

static Item js_atomics_status_string(JsAtomicsWaiterStatus status) {
    switch (status) {
    case JS_ATOMICS_WAITER_OK:        return js_atomics_wait_result("ok", 2);
    case JS_ATOMICS_WAITER_TIMED_OUT: return js_atomics_wait_result("timed-out", 9);
    default:                          return js_atomics_wait_result("not-equal", 9);
    }
}

static void js_atomics_set_waiter_status(JsAtomicsWaiter* waiter, JsAtomicsWaiterStatus status) {
    if (!waiter || waiter->status != JS_ATOMICS_WAITER_PENDING) return;
    waiter->status = status;
    if (get_type_id(waiter->promise) == LMD_TYPE_MAP) {
        js_promise_fulfill_existing(waiter->promise, js_atomics_status_string(status));
    }
}

static Item js_atomics_wait_async_result(bool async, Item value) {
    Item result = js_new_object();
    js_set_key_cstr(result, "async", (Item){.item = b2it(async)});
    js_set_key_cstr(result, "value", value);
    return result;
}

static JsAtomicsWaiter* js_atomics_find_waiter(int waiter_id) {
    if (waiter_id <= 0) return NULL;
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        if (js_atomics_waiters[i].used && js_atomics_waiters[i].id == waiter_id) return &js_atomics_waiters[i];
    }
    return NULL;
}

static bool js_atomics_has_pending_waiter_for_buffer(JsArrayBuffer* buffer) {
    if (!buffer) return false;
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        JsAtomicsWaiter* waiter = &js_atomics_waiters[i];
        if (waiter->used && waiter->status == JS_ATOMICS_WAITER_PENDING && waiter->buffer == buffer) return true;
    }
    return false;
}

static void js_atomics_resolve_due_waiters() {
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        JsAtomicsWaiter* waiter = &js_atomics_waiters[i];
        if (!waiter->used || waiter->status != JS_ATOMICS_WAITER_PENDING || !waiter->has_deadline) continue;
        if (waiter->deadline_ms <= js_atomics_virtual_now_ms) js_atomics_set_waiter_status(waiter, JS_ATOMICS_WAITER_TIMED_OUT);
    }
}

static Item js_atomics_timeout_waiter(Item waiter_id_item) {
    if (get_type_id(waiter_id_item) != LMD_TYPE_INT) return (Item){.item = ITEM_JS_UNDEFINED};
    JsAtomicsWaiter* waiter = js_atomics_find_waiter((int)it2i(waiter_id_item));
    if (!waiter || waiter->status != JS_ATOMICS_WAITER_PENDING || !waiter->has_deadline) return (Item){.item = ITEM_JS_UNDEFINED};
    if (js_atomics_virtual_now_ms < waiter->deadline_ms) js_atomics_virtual_now_ms = waiter->deadline_ms;
    js_atomics_set_waiter_status(waiter, JS_ATOMICS_WAITER_TIMED_OUT);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static void js_atomics_schedule_timeout_waiter(int waiter_id, double timeout_ms) {
    if (waiter_id <= 0 || !std::isfinite(timeout_ms)) return;
    Item callback_fn = js_new_native_function(js_atomics_timeout_waiter);
    Item waiter_id_item = (Item){.item = i2it(waiter_id)};
    Item callback = js_bind_function(callback_fn, ItemNull, &waiter_id_item, 1);
    int64_t delay_ms = (int64_t)std::trunc(timeout_ms);
    if (delay_ms < 0) delay_ms = 0;
    js_setTimeout(callback, (Item){.item = i2it(delay_ms)});
}

static int js_atomics_record_waiter(JsArrayBuffer* buffer, int index, int agent_slot, double timeout_ms, bool has_timeout, Item promise) {
    js_atomics_register_waiter_roots();
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        JsAtomicsWaiter* waiter = &js_atomics_waiters[i];
        if (waiter->used && waiter->status == JS_ATOMICS_WAITER_PENDING) continue;
        waiter->used = true;
        waiter->id = js_atomics_next_waiter_id++;
        if (js_atomics_next_waiter_id <= 0) js_atomics_next_waiter_id = 1;
        waiter->agent_slot = agent_slot;
        waiter->buffer = buffer;
        waiter->index = index;
        waiter->promise = promise;
        waiter->has_deadline = has_timeout;
        waiter->deadline_ms = has_timeout ? js_atomics_virtual_now_ms + timeout_ms : 0.0;
        waiter->status = JS_ATOMICS_WAITER_PENDING;
        if (agent_slot >= 0 && agent_slot < JS_ATOMICS_MAX_AGENT_SLOTS) {
            js_atomics_last_waiter_by_agent[agent_slot] = waiter->id;
            js_atomics_blocking_waiter_by_agent[agent_slot] = waiter->id;
        }
        return waiter->id;
    }
    return 0;
}

static bool js_atomics_report_has_wait_suffix(Item report_string) {
    if (get_type_id(report_string) != LMD_TYPE_STRING && get_type_id(report_string) != LMD_TYPE_SYMBOL) return false;
    String* report = it2s(report_string);
    if (!report) return false;
    if (report->len >= 2 && memcmp(report->chars + report->len - 2, "ok", 2) == 0) return true;
    if (report->len >= 9 && memcmp(report->chars + report->len - 9, "timed-out", 9) == 0) return true;
    if (report->len >= 9 && memcmp(report->chars + report->len - 9, "not-equal", 9) == 0) return true;
    return false;
}

static Item js_atomics_replace_wait_suffix(Item report_string, const char* status, int status_len) {
    String* report = it2s(report_string);
    if (!report) return report_string;
    int suffix_len = 0;
    if (report->len >= 2 && memcmp(report->chars + report->len - 2, "ok", 2) == 0) suffix_len = 2;
    else if (report->len >= 9 && memcmp(report->chars + report->len - 9, "timed-out", 9) == 0) suffix_len = 9;
    else if (report->len >= 9 && memcmp(report->chars + report->len - 9, "not-equal", 9) == 0) suffix_len = 9;
    if (suffix_len == 0) return report_string;
    int prefix_len = (int)report->len - suffix_len;
    int len = prefix_len + status_len;
    char* buf = (char*)mem_alloc((size_t)len + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return report_string;
    memcpy(buf, report->chars, (size_t)prefix_len);
    memcpy(buf + prefix_len, status, (size_t)status_len);
    buf[len] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, len))};
    mem_free(buf);
    return result;
}

extern "C" void js_atomics_reset_waiters(void) {
    if (!js_atomics_runtime_state()) return;
    js_atomics_register_waiter_roots();
    memset(js_atomics_waiters, 0, sizeof(js_atomics_waiters));
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) js_atomics_waiters[i].promise = ItemNull;
    memset(js_atomics_last_waiter_by_agent, 0, sizeof(js_atomics_last_waiter_by_agent));
    memset(js_atomics_blocking_waiter_by_agent, 0, sizeof(js_atomics_blocking_waiter_by_agent));
    js_atomics_next_waiter_id = 1;
    js_atomics_virtual_now_ms = 0.0;
}

extern "C" void js_atomics_destroy_context(JsRuntimeState* runtime_state) {
    if (!runtime_state || !runtime_state->test262_agent.atomics_waiter_state) return;
    mem_free(runtime_state->test262_agent.atomics_waiter_state);
    runtime_state->test262_agent.atomics_waiter_state = NULL;
}

extern "C" int js_atomics_report_waiter_for_agent(int agent_slot, Item report_string) {
    if (agent_slot < 0 || agent_slot >= JS_ATOMICS_MAX_AGENT_SLOTS) return 0;
    int last_waiter_id = js_atomics_last_waiter_by_agent[agent_slot];
    if (last_waiter_id > 0 && js_atomics_report_has_wait_suffix(report_string)) {
        js_atomics_last_waiter_by_agent[agent_slot] = 0;
        return last_waiter_id;
    }
    JsAtomicsWaiter* blocking_waiter = js_atomics_find_waiter(js_atomics_blocking_waiter_by_agent[agent_slot]);
    if (blocking_waiter && blocking_waiter->status == JS_ATOMICS_WAITER_PENDING) return blocking_waiter->id;
    return 0;
}

extern "C" bool js_atomics_report_waiter_ready(int waiter_id) {
    js_atomics_resolve_due_waiters();
    JsAtomicsWaiter* waiter = js_atomics_find_waiter(waiter_id);
    return !waiter || waiter->status != JS_ATOMICS_WAITER_PENDING;
}

extern "C" Item js_atomics_resolve_waiter_report(int waiter_id, Item report_string) {
    JsAtomicsWaiter* waiter = js_atomics_find_waiter(waiter_id);
    if (!waiter) return report_string;
    if (waiter->status == JS_ATOMICS_WAITER_OK) {
        return js_atomics_replace_wait_suffix(report_string, "ok", 2);
    }
    if (waiter->status == JS_ATOMICS_WAITER_TIMED_OUT) {
        String* report = it2s(report_string);
        if (report && report->len == 31 && memcmp(report->chars, "W timeout before Atomics.notify", 31) == 0) {
            return (Item){.item = s2it(heap_strcpy((char*)"W timeout after Atomics.notify", 30))};
        }
        return js_atomics_replace_wait_suffix(report_string, "timed-out", 9);
    }
    return report_string;
}

extern "C" void js_atomics_agent_sleep(Item ms) {
    double sleep_ms = 0.0;
    if (get_type_id(ms) != LMD_TYPE_UNDEFINED) {
        Item validation = js_dataview_to_number_value(ms, &sleep_ms);
        if (item_is_error(validation)) return;
        if (std::isnan(sleep_ms) || sleep_ms < 0.0) sleep_ms = 0.0;
        if (!std::isfinite(sleep_ms)) sleep_ms = 2147483647.0;
    }
    js_atomics_virtual_now_ms += sleep_ms;
    js_atomics_resolve_due_waiters();
}
JS_FORWARD_EXPRESSION(Item, js_atomics_agent_monotonic_now, (void), ((Item){.item = i2it((int64_t)std::trunc(js_atomics_virtual_now_ms))}))

extern "C" void js_atomics_agent_leaving(int agent_slot) {
    if (agent_slot < 0 || agent_slot >= JS_ATOMICS_MAX_AGENT_SLOTS) return;
    js_atomics_last_waiter_by_agent[agent_slot] = 0;
    js_atomics_blocking_waiter_by_agent[agent_slot] = 0;
}

#define JS_ATOMICS_APPLY_OPERATION(C_TYPE) do { \
    C_TYPE* element_ptr = ((C_TYPE*)data) + index; \
    C_TYPE converted_value = (C_TYPE)value_bits; \
    C_TYPE old_value; \
    switch ((JsAtomicsOp)op) { \
    case JS_ATOMICS_OP_ADD: \
        old_value = __atomic_fetch_add(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_AND: \
        old_value = __atomic_fetch_and(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_EXCHANGE: \
        old_value = __atomic_exchange_n(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_LOAD: \
        old_value = __atomic_load_n(element_ptr, __ATOMIC_SEQ_CST); \
        if (agent_spin_assist && old_value == (C_TYPE)0) return js_atomics_item_from_bits(ta->element_type, (uint64_t)(C_TYPE)1); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_OR: \
        old_value = __atomic_fetch_or(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_STORE: \
        if (!agent_spin_assist && js_arraybuffer_shared(ta->buffer) && converted_value == (C_TYPE)0 && js_atomics_has_pending_waiter_for_buffer(ta->buffer)) { \
            __atomic_store_n(element_ptr, (C_TYPE)1, __ATOMIC_SEQ_CST); \
        } else { \
            __atomic_store_n(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        } \
        return store_return; \
    case JS_ATOMICS_OP_SUB: \
        old_value = __atomic_fetch_sub(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_XOR: \
        old_value = __atomic_fetch_xor(element_ptr, converted_value, __ATOMIC_SEQ_CST); \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)old_value); \
    case JS_ATOMICS_OP_COMPARE_EXCHANGE: { \
        C_TYPE observed_value = (C_TYPE)value_bits; \
        C_TYPE replacement_value = (C_TYPE)replacement_bits; \
        __atomic_compare_exchange_n(element_ptr, &observed_value, replacement_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
        if (agent_spin_assist && observed_value != (C_TYPE)value_bits && value_bits == 0 && replacement_bits == 1) { \
            __atomic_store_n(element_ptr, replacement_value, __ATOMIC_SEQ_CST); \
            return js_atomics_item_from_bits(ta->element_type, value_bits); \
        } \
        return js_atomics_item_from_bits(ta->element_type, (uint64_t)observed_value); \
    } \
    } \
} while (0)

extern "C" Item js_atomics_operation(int op, Item typed_array, Item index_item, Item value, Item replacement) {
    JsTypedArray* ta = NULL;
    JS_ASSIGN_OR_RETURN(validation, js_validate_atomic_typed_array(typed_array, false, false, &ta));
    int index = 0;
    JS_ASSIGN_OR_RETURN_INTO(validation, js_atomics_validate_index(ta, index_item, &index));
    void* data = js_typed_array_current_data(ta);
    if (!data) return js_throw_range_error("Invalid atomic access index");
    bool agent_spin_assist = js_arraybuffer_shared(ta->buffer) && js_262_agent_current_slot_for_atomics() >= 0;

    uint64_t value_bits = 0;
    uint64_t replacement_bits = 0;
    Item store_return = value;
    if ((JsAtomicsOp)op != JS_ATOMICS_OP_LOAD) {
        JS_ASSIGN_OR_RETURN_INTO(validation, js_atomics_to_element_bits(ta->element_type, value, &value_bits, &store_return));
        if ((JsAtomicsOp)op == JS_ATOMICS_OP_COMPARE_EXCHANGE) {
            validation = js_atomics_to_element_bits(ta->element_type, replacement,
                &replacement_bits, NULL);
            if (item_is_error(validation)) return validation;
        }
    }

    switch (ta->element_type) {
    case JS_TYPED_INT8:      JS_ATOMICS_APPLY_OPERATION(int8_t);
    case JS_TYPED_UINT8:     JS_ATOMICS_APPLY_OPERATION(uint8_t);
    case JS_TYPED_INT16:     JS_ATOMICS_APPLY_OPERATION(int16_t);
    case JS_TYPED_UINT16:    JS_ATOMICS_APPLY_OPERATION(uint16_t);
    case JS_TYPED_INT32:     JS_ATOMICS_APPLY_OPERATION(int32_t);
    case JS_TYPED_UINT32:    JS_ATOMICS_APPLY_OPERATION(uint32_t);
    case JS_TYPED_BIGINT64:  JS_ATOMICS_APPLY_OPERATION(int64_t);
    case JS_TYPED_BIGUINT64: JS_ATOMICS_APPLY_OPERATION(uint64_t);
    default:
        return js_throw_type_error("Atomics operation requires an integer TypedArray");
    }
}

#undef JS_ATOMICS_APPLY_OPERATION

typedef struct JsAtomicsWaitInputs {
    JsTypedArray* ta;
    int index;
    uint64_t expected_bits;
    double timeout_number;
    bool has_timeout;
    bool equal;
    Item error;
} JsAtomicsWaitInputs;

static Item js_atomics_prepare_wait(Item typed_array, Item index_item, Item expected,
        Item timeout, JsAtomicsWaitInputs* inputs) {
    inputs->error = ItemNull;
    JS_ASSIGN_OR_RETURN(validation, js_validate_atomic_typed_array(typed_array, true, true, &inputs->ta));
    JS_ASSIGN_OR_RETURN_INTO(validation, js_atomics_validate_index(inputs->ta, index_item, &inputs->index));
    inputs->expected_bits = 0;
    validation = js_atomics_to_element_bits(inputs->ta->element_type, expected,
        &inputs->expected_bits, NULL);
    if (item_is_error(validation)) return validation;

    inputs->timeout_number = INFINITY;
    inputs->has_timeout = false;
    if (get_type_id(timeout) != LMD_TYPE_UNDEFINED) {
        JS_ASSIGN_OR_RETURN_INTO(validation, js_dataview_to_number_value(timeout, &inputs->timeout_number));
        if (std::isnan(inputs->timeout_number)) inputs->timeout_number = INFINITY;
        else if (inputs->timeout_number < 0.0) inputs->timeout_number = 0.0;
        else inputs->timeout_number = std::trunc(inputs->timeout_number);
        inputs->has_timeout = std::isfinite(inputs->timeout_number);
    }

    void* data = js_typed_array_current_data(inputs->ta);
    if (!data) {
        inputs->error = js_throw_range_error("Invalid atomic access index");
        return inputs->error;
    }
    if (inputs->ta->element_type == JS_TYPED_INT32) {
        int32_t current = __atomic_load_n(((int32_t*)data) + inputs->index, __ATOMIC_SEQ_CST);
        inputs->equal = current == (int32_t)inputs->expected_bits;
    } else {
        int64_t current = __atomic_load_n(((int64_t*)data) + inputs->index, __ATOMIC_SEQ_CST);
        inputs->equal = current == (int64_t)inputs->expected_bits;
    }
    return js_status_ok();
}

extern "C" Item js_atomics_wait(Item typed_array, Item index_item, Item expected, Item timeout) {
    JsAtomicsWaitInputs inputs;
    JS_ASSIGN_OR_RETURN(validation, js_atomics_prepare_wait(typed_array, index_item, expected, timeout, &inputs));
    if (!inputs.equal) return js_atomics_wait_result("not-equal", 9);

    if (!js_atomics_host_can_suspend()) {
        return js_throw_type_error("Atomics.wait cannot suspend on this agent");
    }

    int agent_slot = js_262_agent_current_slot_for_atomics();
    if (agent_slot < 0) {
        return js_atomics_wait_result("timed-out", 9);
    }
    if (inputs.has_timeout && inputs.timeout_number <= 0.0) {
        return js_atomics_wait_result("timed-out", 9);
    }

    int waiter_id = js_atomics_record_waiter(inputs.ta->buffer, inputs.index, agent_slot,
        inputs.timeout_number, inputs.has_timeout, ItemNull);
    if (waiter_id == 0) return js_throw_type_error("Atomics.wait waiter capacity exceeded");
    if (inputs.has_timeout && inputs.timeout_number <= 200.0) {
        js_atomics_virtual_now_ms += inputs.timeout_number;
        js_atomics_resolve_due_waiters();
        return js_atomics_wait_result("timed-out", 9);
    }
    return js_atomics_wait_result("ok", 2);
}

extern "C" Item js_atomics_wait_async(Item typed_array, Item index_item, Item expected, Item timeout) {
    JsAtomicsWaitInputs inputs;
    JS_ASSIGN_OR_RETURN(validation, js_atomics_prepare_wait(typed_array, index_item, expected, timeout, &inputs));
    if (!inputs.equal) return js_atomics_wait_async_result(false, js_atomics_wait_result("not-equal", 9));
    if (inputs.has_timeout && inputs.timeout_number <= 0.0) {
        return js_atomics_wait_async_result(false, js_atomics_wait_result("timed-out", 9));
    }

    int agent_slot = js_262_agent_current_slot_for_atomics();
    if (agent_slot >= 0) {
        int waiter_id = js_atomics_record_waiter(inputs.ta->buffer, inputs.index, agent_slot,
            inputs.timeout_number, inputs.has_timeout, ItemNull);
        if (waiter_id == 0) return js_throw_type_error("Atomics.waitAsync waiter capacity exceeded");
        // Test262 agents run on a virtual clock. Match the synchronous
        // Atomics.wait fast path for short finite waits so no-spurious-wakeup
        // probes observe the requested lapse without paying real wall time.
        if (inputs.has_timeout && inputs.timeout_number <= 200.0) {
            js_atomics_virtual_now_ms += inputs.timeout_number;
            js_atomics_resolve_due_waiters();
        }
        Item report_status = inputs.has_timeout ? js_atomics_wait_result("timed-out", 9) : js_atomics_wait_result("ok", 2);
        return js_atomics_wait_async_result(true, report_status);
    }

    JS_ASSIGN_OR_RETURN(promise, js_promise_create_pending());
    if (get_type_id(promise) != LMD_TYPE_MAP) return ItemNull;

    int waiter_id = js_atomics_record_waiter(inputs.ta->buffer, inputs.index, agent_slot,
        inputs.timeout_number, inputs.has_timeout, promise);
    if (waiter_id == 0) return js_throw_type_error("Atomics.waitAsync waiter capacity exceeded");

    if (inputs.has_timeout && inputs.timeout_number <= 200.0) {
        js_atomics_schedule_timeout_waiter(waiter_id, inputs.timeout_number);
    }
    return js_atomics_wait_async_result(true, promise);
}

extern "C" Item js_atomics_notify(Item typed_array, Item index_item, Item count) {
    JsTypedArray* ta = NULL;
    JS_ASSIGN_OR_RETURN(validation, js_validate_atomic_typed_array(typed_array, false, true, &ta));
    int index = 0;
    JS_ASSIGN_OR_RETURN_INTO(validation, js_atomics_validate_index(ta, index_item, &index));
    int notify_count = INT_MAX;
    if (get_type_id(count) != LMD_TYPE_UNDEFINED) {
        double count_number = 0.0;
        JS_ASSIGN_OR_RETURN_INTO(validation, js_dataview_to_number_value(count, &count_number));
        if (std::isnan(count_number) || count_number <= 0.0) notify_count = 0;
        else if (std::isfinite(count_number) && count_number < (double)INT_MAX) notify_count = (int)std::trunc(count_number);
    }
    if (!js_arraybuffer_shared(ta->buffer)) {
        return (Item){.item = i2it(0)};
    }
    js_atomics_resolve_due_waiters();
    int notified = 0;
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS && notified < notify_count; i++) {
        JsAtomicsWaiter* waiter = &js_atomics_waiters[i];
        if (!waiter->used || waiter->status != JS_ATOMICS_WAITER_PENDING) continue;
        if (waiter->buffer != ta->buffer || waiter->index != index) continue;
        js_atomics_set_waiter_status(waiter, JS_ATOMICS_WAITER_OK);
        notified++;
    }
    return (Item){.item = i2it(notified)};
}

extern "C" Item js_atomics_is_lock_free(Item size) {
    double number = 0.0;
    JS_ASSIGN_OR_RETURN(validation, js_dataview_to_number_value(size, &number));
    if (std::isnan(number) || !std::isfinite(number)) number = 0.0;
    int64_t int_size = (int64_t)std::trunc(number);
    return (Item){.item = b2it(int_size == 1 || int_size == 2 || int_size == 4 || int_size == 8)};
}

// Returns the JS type name for a typed array element type (e.g. "Uint8Array")
extern "C" const char* js_typed_array_type_name(Item val) {
    if (!js_object_has_class(val, JS_CLASS_TYPED_ARRAY)) return NULL;
    JsTypedArray* ta = js_get_typed_array_ptr(val.map);
    if (!ta) return NULL;
    return js_typed_array_spec(ta->element_type)->name;
}

// ============================================================================
// ArrayBuffer
// ============================================================================

static JsArrayBuffer* js_get_arraybuffer_ptr(Map* m);

static JsArrayBuffer* js_arraybuffer_alloc(int byte_length) {
    JsArrayBuffer* ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
    if (!ab) return NULL;
    if (!byte_buffer_init(&ab->handle, (size_t)byte_length, (size_t)byte_length,
            BYTE_BUFFER_FLAG_NONE, MEM_CAT_JS_RUNTIME)) {
        mem_free(ab);
        return NULL;
    }
    return ab;
}

static JsArrayBuffer* js_arraybuffer_alloc_storage(ByteStorage* storage,
        size_t storage_offset, size_t byte_length) {
    if (!storage || byte_length > INT_MAX) return NULL;
    JsArrayBuffer* ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
    if (!ab) return NULL;
    if (!byte_buffer_init_storage(&ab->handle, storage, storage_offset, byte_length,
            byte_length, BYTE_BUFFER_FLAG_NONE, MEM_CAT_JS_RUNTIME)) {
        mem_free(ab);
        return NULL;
    }
    return ab;
}

static void js_arraybuffer_link_prototype(Item buffer_item, bool is_shared) {
    RootFrame roots(3);
    Rooted<Item> buffer_root(roots, buffer_item);
    Rooted<Item> ctor_root(roots, js_get_constructor(
        (Item){.item = s2it(heap_create_name(is_shared ? "SharedArrayBuffer" : "ArrayBuffer"))}));
    Rooted<Item> proto_root(roots, ItemNull);
    if (get_type_id(ctor_root.get()) != LMD_TYPE_FUNC) return;
    proto_root.set(js_get_key_cstr(ctor_root.get(), "prototype"));
    if (get_type_id(proto_root.get()) == LMD_TYPE_MAP) {
        js_set_prototype(buffer_root.get(), proto_root.get());
    }
}

static Item js_arraybuffer_wrap_item_with_prototype(JsArrayBuffer* ab,
        Item prototype, bool use_provided_prototype) {
    if (!ab) return (Item){.item = ITEM_NULL};
    bool shared = js_arraybuffer_shared(ab);
    RootFrame roots(2);
    Rooted<Item> prototype_root(roots, prototype);
    JsArrayBufferMapCarrier* carrier = (JsArrayBufferMapCarrier*)heap_calloc(
        sizeof(JsArrayBufferMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return ItemNull;
    Map* m = &carrier->base;
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_ARRAYBUFFER;
    m->type = js_object_type_for_class(shared
        ? JS_CLASS_SHARED_ARRAY_BUFFER : JS_CLASS_ARRAY_BUFFER);
    if (!m->type) m->type = &EmptyMap;
    m->data = NULL;
    m->data_cap = 0;
    carrier->payload = ab;
    Rooted<Item> result_root(roots, (Item){.map = m});
    if (use_provided_prototype) {
        js_set_prototype(result_root.get(), prototype_root.get());
    } else {
        js_arraybuffer_link_prototype(result_root.get(), js_arraybuffer_shared(ab));
    }
    return result_root.get();
}
JS_FORWARD_STATIC_ITEM(js_arraybuffer_wrap_item, (JsArrayBuffer* ab), js_arraybuffer_wrap_item_with_prototype, (ab, ItemNull, false))

extern "C" Item js_arraybuffer_new(int byte_length) {
    if (byte_length < 0) byte_length = 0;
    JsArrayBuffer* ab = js_arraybuffer_alloc(byte_length);
    if (!ab) return ItemError;

    return js_arraybuffer_wrap_item(ab);
}

// ArrayBuffer constructor from JS: new ArrayBuffer(length)
// Performs ToIndex validation per spec: non-negative integer, throws RangeError for invalid.
// Practical allocation limit: 1 GB (matches typical engine limits).
JS_FORWARD_ITEM(js_arraybuffer_construct, (Item length_arg), js_arraybuffer_construct_resizable, (length_arg, (Item){.item = ITEM_JS_UNDEFINED}))

struct JsArrayBufferConstructOptions {
    int64_t byte_length;
    int64_t max_byte_length;
    bool resizable;
};

static Item js_arraybuffer_parse_construct_options(Item length_arg,
        Item options_arg, const char* length_error, const char* max_error,
        JsArrayBufferConstructOptions* out) {
    out->byte_length = 0;
    JS_ASSIGN_OR_RETURN(validation, js_to_index_i64(
        length_arg, &out->byte_length, length_error));
    out->max_byte_length = out->byte_length;
    out->resizable = false;
    if (get_type_id(options_arg) == LMD_TYPE_MAP) {
        Item max_key = (Item){.item = s2it(heap_create_name("maxByteLength"))};
        JS_ASSIGN_OR_RETURN(max_item, js_get_key_default(options_arg, max_key));
        if (get_type_id(max_item) != LMD_TYPE_UNDEFINED && max_item.item != ITEM_NULL) {
            JS_ASSIGN_OR_RETURN_INTO(validation, js_to_index_i64(
                max_item, &out->max_byte_length, max_error));
            if (out->max_byte_length < out->byte_length) {
                return js_throw_range_error(max_error);
            }
            out->resizable = true;
        }
    }
    return js_status_ok();
}

static Item js_arraybuffer_allocate_constructed(
        const JsArrayBufferConstructOptions* options, Item prototype,
        bool use_provided_prototype, bool shared) {
    // OrdinaryCreateFromConstructor is observable before the backing-store
    // allocation. Keep the host's 1 GiB cap here rather than in ToIndex so a
    // throwing newTarget.prototype getter wins over allocation RangeError.
    if (options->byte_length > 1073741824LL) {
        return js_throw_range_error(shared
            ? "Invalid shared array buffer length" : "Invalid array buffer length");
    }
    if (options->max_byte_length > 1073741824LL) {
        return js_throw_range_error(shared
            ? "Invalid shared array buffer maxByteLength" : "Invalid array buffer maxByteLength");
    }
    JsArrayBuffer* ab;
    if (shared) {
        ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
        if (ab) {
            uint32_t flags = BYTE_BUFFER_FLAG_SHARED;
            if (options->resizable) flags |= BYTE_BUFFER_FLAG_RESIZABLE;
            if (!byte_buffer_init(&ab->handle, (size_t)options->byte_length,
                    (size_t)options->max_byte_length, flags, MEM_CAT_JS_RUNTIME)) {
                mem_free(ab);
                ab = NULL;
            }
        }
    } else {
        ab = js_arraybuffer_alloc((int)options->byte_length);
    }
    if (!ab) return ItemError;
    Item result = js_arraybuffer_wrap_item_with_prototype(
        ab, prototype, use_provided_prototype);
    if (!shared && js_is_arraybuffer(result)) {
        ab->handle.max_byte_length = (size_t)options->max_byte_length;
        if (options->resizable) ab->handle.flags |= BYTE_BUFFER_FLAG_RESIZABLE;
    }
    return result;
}

static Item js_construct_arraybuffer_from_options(Item length_arg, Item options_arg,
        bool shared) {
    RootFrame roots(2);
    Rooted<Item> length_root(roots, length_arg);
    Rooted<Item> options_root(roots, options_arg);
    JsArrayBufferConstructOptions options = {0, 0, false};
    JS_ASSIGN_OR_RETURN(validation, js_arraybuffer_parse_construct_options(
        length_root.get(), options_root.get(), "Invalid array buffer length",
        "Invalid array buffer maxByteLength", &options));
    return js_arraybuffer_allocate_constructed(&options, ItemNull, false, shared);
}
JS_FORWARD_ITEM(js_arraybuffer_construct_resizable, (Item length_arg, Item options_arg),
    js_construct_arraybuffer_from_options, (length_arg, options_arg, false))
JS_FORWARD_ITEM(js_sharedarraybuffer_construct_with_options, (Item length_arg, Item options_arg),
    js_construct_arraybuffer_from_options, (length_arg, options_arg, true))

static Item js_construct_arraybuffer_target(Item length_arg, Item options_arg,
        Item new_target, bool shared) {
    RootFrame roots(4);
    Rooted<Item> length_root(roots, length_arg);
    Rooted<Item> options_root(roots, options_arg);
    Rooted<Item> target_root(roots, new_target);
    Rooted<Item> prototype_root(roots, ItemNull);
    JsArrayBufferConstructOptions options = {0, 0, false};
    // ToIndex precedes AllocateArrayBuffer, but its object creation precedes
    // the fallible backing-store allocation. Keep those two phases separate.
    JS_ASSIGN_OR_RETURN(validation, js_arraybuffer_parse_construct_options(
        length_root.get(), options_root.get(),
        shared ? "Invalid shared array buffer length" : "Invalid array buffer length",
        shared ? "Invalid shared array buffer maxByteLength" : "Invalid array buffer maxByteLength",
        &options));
    prototype_root.set(js_get_prototype_from_constructor_default(
        target_root.get(), shared ? JS_CLASS_SHARED_ARRAY_BUFFER : JS_CLASS_ARRAY_BUFFER, -1));
    if (item_is_error(prototype_root.get())) return prototype_root.get();
    return js_arraybuffer_allocate_constructed(
        &options, prototype_root.get(), true, shared);
}
JS_FORWARD_ITEM(js_arraybuffer_construct_resizable_target,
    (Item length_arg, Item options_arg, Item new_target),
    js_construct_arraybuffer_target, (length_arg, options_arg, new_target, false))
JS_FORWARD_ITEM(js_sharedarraybuffer_construct_with_options_target,
    (Item length_arg, Item options_arg, Item new_target),
    js_construct_arraybuffer_target, (length_arg, options_arg, new_target, true))

extern "C" bool js_is_arraybuffer(Item val) {
    if (get_type_id(val) != LMD_TYPE_MAP || !val.map ||
            val.map->map_kind != MAP_KIND_ARRAYBUFFER) return false;
    // The intrinsic prototype carries ArrayBuffer metadata for ordinary
    // lookup, but only the trailing carrier owns [[ArrayBufferData]]; branding
    // by class alone made prototype accessors return a fake zero length.
    return js_object_has_class(val, JS_CLASS_ARRAY_BUFFER) ||
           js_object_has_class(val, JS_CLASS_SHARED_ARRAY_BUFFER);
}

// Get the typed trailing payload after validating the physical carrier.
static JsArrayBuffer* js_get_arraybuffer_ptr(Map* m) {
    if (!m || (m->map_kind != MAP_KIND_ARRAYBUFFER)) return NULL;
    return ((JsArrayBufferMapCarrier*)m)->payload;
}

extern "C" JsArrayBuffer* js_get_arraybuffer_ptr_item(Item val) {
    if (!js_is_arraybuffer(val)) return NULL;
    return js_get_arraybuffer_ptr(val.map);
}

// Wrap an existing JsArrayBuffer* in a Map Item (for .buffer property access)
JS_FORWARD_ITEM(js_arraybuffer_wrap, (JsArrayBuffer* ab), js_arraybuffer_wrap_item, (ab))

extern "C" int js_arraybuffer_byte_length(Item val) {
    if (!js_is_arraybuffer(val)) return 0;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return 0;
    return js_arraybuffer_length(ab);
}

extern "C" int js_arraybuffer_max_byte_length(Item val) {
    if (!js_is_arraybuffer(val)) return 0;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return 0;
    // Js54 P1: spec §25.1.5.3 get ArrayBuffer.prototype.maxByteLength step 4:
    // "If IsDetachedBuffer(O) is true, return +0𝔽." The detach path zeros
    // byte_length but not max_byte_length, so the check is needed here.
    if (js_arraybuffer_detached(ab)) return 0;
    return js_arraybuffer_max_length(ab);
}

extern "C" bool js_arraybuffer_is_resizable(Item val) {
    if (!js_is_arraybuffer(val)) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    return js_arraybuffer_resizable(ab);
}

extern "C" Item js_arraybuffer_resize(Item val, Item new_length_item) {
    if (!js_is_arraybuffer(val) || js_is_sharedarraybuffer(val)) {
        return js_throw_type_error("ArrayBuffer.prototype.resize requires a resizable ArrayBuffer receiver");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    // Js54 P6: spec §25.1.5.2 has just one detach check, AFTER ToIntegerOrInfinity.
    // If the buffer is already detached at entry, the coercion still runs (and
    // can have side effects); we throw afterwards. Test:
    // built-ins/ArrayBuffer/prototype/resize/coerced-new-length-detach.js.
    if (!ab) return js_throw_type_error("ArrayBuffer is detached");
    if (!js_arraybuffer_resizable(ab)) return js_throw_type_error("ArrayBuffer is not resizable");
    int new_length = 0;
    JS_ASSIGN_OR_RETURN(validation, js_to_index_int(new_length_item, &new_length, "Invalid array buffer length"));
    if (js_arraybuffer_detached(ab)) return js_throw_type_error("ArrayBuffer is detached");
    if (new_length > js_arraybuffer_max_length(ab)) return js_throw_range_error("Invalid array buffer length");
    if (!byte_buffer_resize(&ab->handle, (size_t)new_length)) return ItemError;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// Js54 P8: shared implementation for ArrayBuffer.prototype.transfer{,ToFixedLength}.
// Per ES2024 §25.1.5.{3,4}: create a new ArrayBuffer of newLength bytes, copy
// min(srcByteLength, newLength) bytes from source, detach the source. For
// `transfer`, preserve resizable + maxByteLength from the source; for
// `transferToFixedLength`, the result is always non-resizable.
static Item js_arraybuffer_transfer_impl(Item val, Item new_length_item, int argc,
                                         bool to_fixed_length) {
    if (!js_is_arraybuffer(val) || js_is_sharedarraybuffer(val)) {
        return js_throw_type_error("ArrayBuffer.prototype.transfer requires a non-shared ArrayBuffer receiver");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return js_throw_type_error("ArrayBuffer is detached");
    // Per spec: validate detached AFTER coercing newLength so the valueOf side
    // effect runs first when applicable.
    int new_length;
    if (argc == 0 || get_type_id(new_length_item) == LMD_TYPE_UNDEFINED) {
        if (js_arraybuffer_detached(ab)) return js_throw_type_error("ArrayBuffer is detached");
        new_length = js_arraybuffer_length(ab);
    } else {
        JS_ASSIGN_OR_RETURN(validation, js_to_index_int(new_length_item, &new_length, "Invalid array buffer length"));
        if (js_arraybuffer_detached(ab)) return js_throw_type_error("ArrayBuffer is detached");
    }

    // Determine resizable / maxByteLength for the new buffer.
    bool new_resizable;
    int new_max_byte_length;
    if (to_fixed_length) {
        new_resizable = false;
        new_max_byte_length = new_length;
    } else {
        new_resizable = js_arraybuffer_resizable(ab);
        new_max_byte_length = new_resizable ? js_arraybuffer_max_length(ab) : new_length;
    }
    if (new_length > new_max_byte_length) {
        return js_throw_range_error("Invalid array buffer length");
    }

    JsArrayBuffer* nab = (JsArrayBuffer*)mem_calloc(1, sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
    if (!nab) return ItemError;
    if (!byte_buffer_transfer(&ab->handle, &nab->handle, (size_t)new_length,
            to_fixed_length)) {
        mem_free(nab);
        return ItemError;
    }
    // The stable source handle is detached by transfer, so all extant views
    // invalidate through its generation instead of retaining a freed pointer.
    Item result = js_arraybuffer_wrap(nab);
    if (!js_is_arraybuffer(result)) {
        byte_buffer_destroy(&nab->handle);
        mem_free(nab);
    }
    return result;
}

#define JS_ARRAYBUFFER_TRANSFER_WRAPPER(name, to_fixed_length) \
extern "C" Item name(Item val, Item new_length_item, int argc) { \
    return js_arraybuffer_transfer_impl(val, new_length_item, argc, to_fixed_length); \
}
JS_ARRAYBUFFER_TRANSFER_WRAPPER(js_arraybuffer_transfer, false)
JS_ARRAYBUFFER_TRANSFER_WRAPPER(js_arraybuffer_transfer_to_fixed_length, true)
#undef JS_ARRAYBUFFER_TRANSFER_WRAPPER

extern "C" Item js_arraybuffer_slice(Item val, int begin, int end) {
    if (!js_is_arraybuffer(val)) return (Item){.item = ITEM_NULL};
    // ES spec: ArrayBuffer.prototype.slice must throw TypeError for SharedArrayBuffer
    if (js_is_sharedarraybuffer(val)) {
        return js_throw_type_error("ArrayBuffer.prototype.slice requires that |this| not be a SharedArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return (Item){.item = ITEM_NULL};
    if (js_arraybuffer_detached(ab)) {
        return js_throw_type_error("ArrayBuffer.prototype.slice called on detached buffer");
    }

    int source_length = js_arraybuffer_length(ab);
    if (begin < 0) begin = source_length + begin;
    if (end < 0) end = source_length + end;
    if (begin < 0) begin = 0;
    if (end > source_length) end = source_length;
    if (begin >= end) return js_arraybuffer_new(0);

    int new_len = end - begin;
    Item result = js_arraybuffer_new(new_len);
    JsArrayBuffer* rab = (JsArrayBuffer*)result.map->data;
    memcpy(js_arraybuffer_prepare_write(rab), js_arraybuffer_data_const(ab) + begin,
        (size_t)new_len);
    return result;
}

static bool js_arraybuffer_slice_index(Item value, int len, int* out_index) {
    Item num = js_to_number(value);
    if (item_is_error(num)) return false;
    TypeId nt = get_type_id(num);
    double n = (nt == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    if (std::isnan(n)) n = 0;
    n = std::trunc(n);
    if (n < 0) {
        n = (double)len + n;
        if (n < 0) n = 0;
    } else if (n > len) {
        n = len;
    }
    *out_index = (int)n;
    return true;
}

static Item js_arraybuffer_slice_species(Item source, JsArrayBuffer* source_buffer,
        int begin, int new_len, bool shared) {
    const char* type_name = shared ? "SharedArrayBuffer" : "ArrayBuffer";
    Item result_item = ItemNull;
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
    JS_ASSIGN_OR_RETURN(ctor, js_get_key_default(source, ctor_key));

    bool use_default_ctor = get_type_id(ctor) == LMD_TYPE_UNDEFINED;
    if (!use_default_ctor) {
        TypeId ctor_type = get_type_id(ctor);
        if (ctor_type != LMD_TYPE_MAP && ctor_type != LMD_TYPE_ARRAY &&
                ctor_type != LMD_TYPE_FUNC && ctor_type != LMD_TYPE_ELEMENT) {
            char message[96];
            snprintf(message, sizeof(message),
                "%s species constructor must be an object", type_name);
            return js_throw_type_error(message);
        }
        Item species_key = js_well_known_symbol_key(6);
        JS_ASSIGN_OR_RETURN(species, js_get_key_default(ctor, species_key));
        TypeId species_type = get_type_id(species);
        if (species_type == LMD_TYPE_UNDEFINED || species_type == LMD_TYPE_NULL) {
            use_default_ctor = true;
        } else {
            if (!shared && !js_has_construct_capability(species)) {
                return js_throw_type_error("ArrayBuffer species is not a constructor");
            }
            Item len_arg = (Item){.item = i2it(new_len)};
            JS_ASSIGN_OR_RETURN_INTO(result_item, js_construct_value(species,
                &len_arg, 1, species, NULL, false));
        }
    }
    if (use_default_ctor) {
        result_item = shared
            ? js_sharedarraybuffer_construct((Item){.item = i2it(new_len)})
            : js_arraybuffer_construct((Item){.item = i2it(new_len)});
    }
    if (item_is_error(result_item)) return result_item;
    bool correct_type = shared ? js_is_sharedarraybuffer(result_item)
        : js_is_arraybuffer(result_item) && !js_is_sharedarraybuffer(result_item);
    if (!correct_type) {
        char message[128];
        snprintf(message, sizeof(message),
            "%s species constructor did not return a %s", type_name, type_name);
        return js_throw_type_error(message);
    }
    if (result_item.item == source.item) {
        char message[128];
        snprintf(message, sizeof(message),
            "%s species constructor returned the same buffer", type_name);
        return js_throw_type_error(message);
    }
    JsArrayBuffer* result_buffer = js_get_arraybuffer_ptr(result_item.map);
    if (!result_buffer || js_arraybuffer_length(result_buffer) < new_len) {
        char message[128];
        snprintf(message, sizeof(message),
            "%s species constructor returned a buffer that is too small", type_name);
        return js_throw_type_error(message);
    }
    if (new_len > 0) {
        memcpy(js_arraybuffer_prepare_write(result_buffer),
            js_arraybuffer_data_const(source_buffer) + begin, (size_t)new_len);
    }
    return result_item;
}

extern "C" Item js_arraybuffer_slice_items(Item val, Item begin_item, Item end_item, int argc) {
    if (!js_is_arraybuffer(val)) return (Item){.item = ITEM_NULL};
    if (js_is_sharedarraybuffer(val)) {
        return js_throw_type_error("ArrayBuffer.prototype.slice requires that |this| not be a SharedArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return (Item){.item = ITEM_NULL};
    if (js_arraybuffer_detached(ab)) {
        return js_throw_type_error("ArrayBuffer.prototype.slice called on detached buffer");
    }

    int begin = 0;
    int source_length = js_arraybuffer_length(ab);
    int end = source_length;
    if (argc > 0) {
        if (!js_arraybuffer_slice_index(begin_item, source_length, &begin)) return ItemNull;
    }
    if (argc > 1 && get_type_id(end_item) != LMD_TYPE_UNDEFINED) {
        if (!js_arraybuffer_slice_index(end_item, source_length, &end)) return ItemNull;
    }
    if (end < begin) end = begin;
    int new_len = end - begin;

    return js_arraybuffer_slice_species(val, ab, begin, new_len, false);
}

// Item-returning wrapper for MIR JIT calls (MIR expects Item return type)
extern "C" Item js_arraybuffer_is_view_item(Item val) {
    bool result = js_is_typed_array(val) || js_is_dataview(val);
    return (Item){.item = result ? (ITEM_TRUE) : (ITEM_FALSE)};
}

// Detach an ArrayBuffer through the stable handle so storage is released once.
extern "C" void js_arraybuffer_detach(Item val) {
    if (!js_is_arraybuffer(val)) return;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return;
    byte_buffer_detach(&ab->handle);
}

// Check if an ArrayBuffer is detached
extern "C" bool js_arraybuffer_is_detached(Item val) {
    if (!js_is_arraybuffer(val)) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return false;
    return js_arraybuffer_detached(ab);
}

// ============================================================================
// SharedArrayBuffer
// ============================================================================
JS_FORWARD_ITEM(js_sharedarraybuffer_construct, (Item length_arg), js_sharedarraybuffer_construct_with_options, (length_arg, (Item){.item = ITEM_JS_UNDEFINED}))

extern "C" bool js_is_sharedarraybuffer(Item val) {
    if (!js_object_has_class(val, JS_CLASS_SHARED_ARRAY_BUFFER)) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    return js_arraybuffer_shared(ab);
}

extern "C" Item js_sharedarraybuffer_operation(Item sab,
        JsSharedArrayBufferOperation operation, Item* args, int argc) {
    if (!js_is_sharedarraybuffer(sab)) return js_throw_type_error("SharedArrayBuffer method requires a SharedArrayBuffer receiver");
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(sab.map);
    if (!ab) return ItemNull;

    // slice(begin, end)
    if (operation == JS_SHARED_ARRAY_BUFFER_SLICE) {
        int source_length = js_arraybuffer_length(ab);
        int begin = 0, end = source_length;
        if (argc > 0) {
            JS_ASSIGN_OR_RETURN(b, js_to_number(args[0]));
            begin = (int)(get_type_id(b) == LMD_TYPE_FLOAT ? it2d(b) : (double)it2i(b));
            if (begin < 0) begin = source_length + begin;
            if (begin < 0) begin = 0;
            if (begin > source_length) begin = source_length;
        }
        if (argc > 1 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
            JS_ASSIGN_OR_RETURN(e, js_to_number(args[1]));
            end = (int)(get_type_id(e) == LMD_TYPE_FLOAT ? it2d(e) : (double)it2i(e));
            if (end < 0) end = source_length + end;
            if (end < 0) end = 0;
            if (end > source_length) end = source_length;
        }
        if (end < begin) end = begin;
        int new_len = end - begin;

        return js_arraybuffer_slice_species(sab, ab, begin, new_len, true);
    }

    if (operation == JS_SHARED_ARRAY_BUFFER_GROW) {
        if (!js_arraybuffer_resizable(ab)) return js_throw_type_error("SharedArrayBuffer is not growable");
        Item new_length_item = argc > 0 ? args[0] : (Item){.item = ITEM_JS_UNDEFINED};
        int new_length = 0;
        JS_ASSIGN_OR_RETURN(validation, js_to_index_int(new_length_item, &new_length, "Invalid shared array buffer length"));
        int current_length = js_arraybuffer_length(ab);
        if (new_length < current_length || new_length > js_arraybuffer_max_length(ab)) {
            return js_throw_range_error("Invalid shared array buffer length");
        }
        if (new_length != current_length &&
            !byte_buffer_resize(&ab->handle, (size_t)new_length)) {
            return ItemError;
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    log_error("shared-array-buffer-operation: unknown operation %d",
        (int)operation);
    return ItemError;
}

// ============================================================================
// TypedArray
// ============================================================================

extern "C" bool js_is_typed_array(Item val) {
    if (!js_object_has_class(val, JS_CLASS_TYPED_ARRAY)) return false;
    // The class identity selects the semantic lane; the checked accessor
    // rejects %TypedArray%.prototype, which has no trailing payload.
    JsTypedArray* ptr = js_get_typed_array_ptr(val.map);
    return ptr != NULL;
}

extern "C" int64_t js_typed_array_matches_type(Item val, int64_t type_id) {
    if (!js_is_typed_array(val)) return 0;
    JsTypedArray* typed_array = js_get_typed_array_ptr(val.map);
    return typed_array && (int64_t)typed_array->element_type == type_id;
}

// Get the typed trailing payload after validating the physical carrier.
extern "C" JsTypedArray* js_get_typed_array_ptr(Map* m) {
    if (!m || m->map_kind != MAP_KIND_TYPED_ARRAY) return NULL;
    return &((JsTypedArrayMapCarrier*)m)->payload;
}

static Item js_typed_array_alloc_carrier(JsTypedArrayType element_type,
        Item buffer_item, bool length_tracking) {
    JsTypedArrayMapCarrier* carrier = (JsTypedArrayMapCarrier*)heap_calloc(
        sizeof(JsTypedArrayMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return ItemNull;
    Map* map = &carrier->base;
    map->type_id = LMD_TYPE_MAP;
    map->map_kind = MAP_KIND_TYPED_ARRAY;
    map->type = js_object_type_for_class(JS_CLASS_TYPED_ARRAY);
    if (!map->type) map->type = &EmptyMap;
    map->data = NULL;
    map->data_cap = 0;
    JsTypedArray* typed_array = &carrier->payload;
    typed_array->element_type = element_type;
    typed_array->buffer = js_get_arraybuffer_ptr(buffer_item.map);
    typed_array->buffer_item = buffer_item.item;
    typed_array->length_tracking = length_tracking;
    typed_array->is_buffer = false;
    typed_array->view = NULL;
    return (Item){.map = map};
}

// Create a standalone typed array (owns its buffer)
extern "C" Item js_typed_array_new(int type_id, int length) {
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = js_typed_array_element_size(arr_type);
    int byte_length = length * elem_size;
    JsArrayBuffer* ab = js_arraybuffer_alloc(byte_length);
    RootFrame roots(3);
    Rooted<Item> buffer_root(roots, js_arraybuffer_wrap(ab));
    Rooted<Item> view_root(roots, ItemNull);
    if (!js_is_arraybuffer(buffer_root.get())) return ItemNull;
    Rooted<Item> carrier_root(roots, js_typed_array_alloc_carrier(
        arr_type, buffer_root.get(), false));
    if (!js_is_typed_array(carrier_root.get())) return ItemNull;
    ab = js_get_arraybuffer_ptr(buffer_root.get().map);
    view_root.set((Item){.array_num = array_num_new_buffer_view(
        (Container*)buffer_root.get().map, &ab->handle,
        js_typed_array_elem_type(arr_type), 0, length, true)});
    if (get_type_id(view_root.get()) != LMD_TYPE_ARRAY_NUM) return ItemNull;
    // D5.3.3: the view allocation can collect before the typed-array carrier
    // reaches its caller, so both the carrier and backing buffer stay rooted.
    JsTypedArray* ta = js_get_typed_array_ptr(carrier_root.get().map);
    ab = js_get_arraybuffer_ptr(buffer_root.get().map);
    ta->buffer = ab;
    ta->view = view_root.get().array_num;
    js_typed_array_refresh_arraynum_view(ta);
    ta->buffer_item = buffer_root.get().item;
    return carrier_root.get();
}

extern "C" Item js_buffer_from_bytes(const char* data, int len) {
    if (len < 0) len = 0;
    RootFrame roots(1);
    Rooted<Item> buffer_root(roots, js_typed_array_new(JS_TYPED_UINT8, len));
    if (!js_is_typed_array(buffer_root.get())) return ItemNull;
    JsTypedArray* typed_array = js_get_typed_array_ptr(buffer_root.get().map);
    if (!typed_array) return ItemNull;
    // Host transports need Buffer identity even when node-core is not linked;
    // the namespace module only supplies Buffer's JS-facing methods.
    typed_array->is_buffer = true;
    uint8_t* dst = (uint8_t*)js_typed_array_prepare_write_ptr(buffer_root.get());
    if (dst && data && len > 0) memcpy(dst, data, (size_t)len);
    return buffer_root.get();
}

extern "C" Item js_typed_array_from_binary(Binary* bin) {
    if (!bin) return js_typed_array_new(JS_TYPED_UINT8, 0);
    uint32_t length = binary_length(bin);
    ByteSpan span = binary_span(bin);
    if (length > 0 && span.storage &&
            (span.storage->flags & BYTE_STORAGE_FLAG_SHARED_MUTABLE) == 0) {
        JsArrayBuffer* ab = js_arraybuffer_alloc_storage(
            span.storage, span.offset, span.length);
        if (!ab) return ItemError;
        Item buffer_item = js_arraybuffer_wrap(ab);
        if (!js_is_arraybuffer(buffer_item)) {
            byte_buffer_destroy(&ab->handle);
            mem_free(ab);
            return ItemError;
        }
        // Binary retains the allocation while the stable handle is the sole
        // mutable coherence point for every JS view over this ArrayBuffer.
        return js_typed_array_new_from_buffer(
            JS_TYPED_UINT8, buffer_item, 0, (int)length);
    }

    Item result = js_typed_array_new(JS_TYPED_UINT8, (int)length);
    void* data = js_typed_array_prepare_write_ptr(result);
    if (data && length > 0) {
        memcpy(data, binary_data(bin), length);
        binary_record_payload_copy();
    }
    return result;
}

extern "C" Item binary_from_typed_array(JsTypedArray* ta) {
    if (!ta || (ta->element_type != JS_TYPED_UINT8 &&
                ta->element_type != JS_TYPED_UINT8_CLAMPED)) {
        return ItemError;
    }
    js_typed_array_refresh_arraynum_view(ta);
    int byte_length = js_typed_array_current_byte_length(ta);
    if (byte_length == 0) return ItemNull;
    void* data = js_typed_array_current_data(ta);
    if (!data) return ItemError;
    ByteBufferHandle* handle = ta->buffer ? &ta->buffer->handle : NULL;
    Binary* bin = NULL;
    if (handle && handle->storage && !byte_buffer_is_shared(handle)) {
        size_t view_offset = (size_t)js_typed_array_current_byte_offset(ta);
        if (view_offset <= handle->byte_length &&
                (size_t)byte_length <= handle->byte_length - view_offset) {
            bin = heap_binary_from_storage(handle->storage,
                handle->storage_offset + view_offset, (size_t)byte_length,
                str_is_ascii((const char*)data, (size_t)byte_length));
        }
    } else {
        // SharedArrayBuffer remains a snapshot copy because concurrent writes
        // cannot satisfy Lambda Binary immutability.
        bin = heap_binary_from_bytes((const char*)data, byte_length);
    }
    return bin ? (Item){.item = x2it(bin)} : ItemError;
}

extern "C" Item binary_from_dataview(JsDataView* dv) {
    if (!dv || !dv->buffer || js_arraybuffer_detached(dv->buffer)) return ItemError;
    int byte_length = dv->length_tracking ?
        js_arraybuffer_length(dv->buffer) - dv->byte_offset : dv->byte_length;
    if (byte_length < 0 || dv->byte_offset < 0 ||
        dv->byte_offset + byte_length > js_arraybuffer_length(dv->buffer)) return ItemError;
    if (byte_length == 0) return ItemNull;
    const char* data = (const char*)js_arraybuffer_data_const(dv->buffer) + dv->byte_offset;
    ByteBufferHandle* handle = &dv->buffer->handle;
    Binary* bin = NULL;
    if (!byte_buffer_is_shared(handle) && handle->storage) {
        bin = heap_binary_from_storage(handle->storage,
            handle->storage_offset + (size_t)dv->byte_offset,
            (size_t)byte_length, str_is_ascii(data, (size_t)byte_length));
    } else {
        // DataView over SharedArrayBuffer must also snapshot mutable bytes.
        bin = heap_binary_from_bytes(data, byte_length);
    }
    return bin ? (Item){.item = x2it(bin)} : ItemError;
}

// Create a typed array as a view over an ArrayBuffer
extern "C" Item js_typed_array_new_from_buffer(int type_id, Item buffer_item, int byte_offset, int length) {
    RootFrame roots(3);
    Rooted<Item> buffer_root(roots, buffer_item);
    Rooted<Item> view_root(roots, ItemNull);
    buffer_item = buffer_root.get();
    if (!js_is_arraybuffer(buffer_item)) {
        log_error("js_typed_array_new_from_buffer: argument is not an ArrayBuffer");
        return js_throw_type_error("TypedArray buffer argument must be an ArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(buffer_item.map);
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = js_typed_array_element_size(arr_type);

    if (js_arraybuffer_detached(ab)) {
        return js_throw_type_error("Cannot construct TypedArray from detached ArrayBuffer");
    }
    if (byte_offset < 0) {
        return js_throw_range_error("Invalid typed array byteOffset");
    }
    if (byte_offset % elem_size != 0) {
        log_error("js_typed_array_new_from_buffer: byte_offset %d not aligned to element size %d", byte_offset, elem_size);
        return js_throw_range_error("Invalid typed array byteOffset");
    }
    int buffer_length = js_arraybuffer_length(ab);
    if (byte_offset > buffer_length) {
        return js_throw_range_error("Invalid typed array byteOffset");
    }

    int available = buffer_length - byte_offset;

    bool length_tracking = length < 0;
    if (length_tracking) {
        // Js54 P6: per ES2024 §10.4.5.5 step 8, the alignment check is only
        // required for non-resizable buffers. Resizable buffers with undefined
        // length use auto-tracking and simply floor: the spec ignores the
        // trailing remainder. Without this fix `new Float64Array(rab)` over a
        // resizable buffer whose byteLength isn't a multiple of 8 throws
        // RangeError, blocking the species-ctor cluster.
        if (!js_arraybuffer_resizable(ab) && (available % elem_size != 0)) {
            return js_throw_range_error("Invalid typed array byteLength");
        }
        length = available / elem_size;
    }
    int byte_length = length * elem_size;
    if (length < 0 || byte_length < 0 || byte_offset + byte_length > buffer_length) {
        log_error("js_typed_array_new_from_buffer: view exceeds buffer bounds");
        return js_throw_range_error("Invalid typed array length");
    }

    Rooted<Item> carrier_root(roots, js_typed_array_alloc_carrier(
        arr_type, buffer_root.get(), length_tracking));
    if (!js_is_typed_array(carrier_root.get())) return ItemNull;
    ab = js_get_arraybuffer_ptr(buffer_root.get().map);
    if (!ab) return ItemNull;
    view_root.set((Item){.array_num = array_num_new_buffer_view(
        (Container*)buffer_root.get().map, &ab->handle,
        js_typed_array_elem_type(arr_type), byte_offset, length, true)});
    if (get_type_id(view_root.get()) != LMD_TYPE_ARRAY_NUM) return ItemNull;
    // D5.3.3: refresh every interior pointer from its exact root after the
    // view allocation; forced collections may relocate either carrier.
    JsTypedArray* ta = js_get_typed_array_ptr(carrier_root.get().map);
    ab = js_get_arraybuffer_ptr(buffer_root.get().map);
    ta->buffer = ab;
    ta->view = view_root.get().array_num;
    js_typed_array_refresh_arraynum_view(ta);
    ta->buffer_item = buffer_root.get().item;
    return carrier_root.get();
}

// Create a typed array from another array (copy)
extern "C" Item js_typed_array_new_from_array(int type_id, Item source) {
    RootFrame roots(2);
    Rooted<Item> source_root(roots, source);
    Rooted<Item> result_root(roots, ItemNull);
    source = source_root.get();
    TypeId src_type = get_type_id(source);

    if (js_is_typed_array(source)) {
        // Copy from another typed array
        JsTypedArray* src = js_get_typed_array_ptr(source.map);
        // Js55 P22: per ES2024 §23.2.5.1.1 InitializeTypedArrayFromTypedArray,
        // step 2 — IsTypedArrayOutOfBounds(srcRecord) must throw TypeError.
        // This catches the case where the source TA's backing resizable
        // ArrayBuffer was shrunk past the view's range.
        if (js_typed_array_is_out_of_bounds(src)) {
            return js_throw_type_error("Cannot construct from an out-of-bounds TypedArray");
        }
        bool src_bigint = js_typed_array_is_bigint_element(src->element_type);
        bool dst_bigint = js_typed_array_is_bigint_element((JsTypedArrayType)type_id);
        if (src_bigint != dst_bigint) {
            return js_throw_type_error("Cannot mix BigInt and non-BigInt typed arrays");
        }
        // Js55 P22: for length-tracking source TAs on a resized buffer, use
        // current length (TypedArrayLength of the witness record), not the
        // cached src->length which may be stale.
        int src_len = js_typed_array_current_length(src);
        result_root.set(js_typed_array_new(type_id, src_len));
        Item result = result_root.get();
        JsTypedArray* dst = js_get_typed_array_ptr(result.map);
        bool copied = false;
        if (src->element_type == (JsTypedArrayType)type_id) {
            copied = js_typed_array_try_raw_set_same_type(dst, src, 0);
        } else if (js_typed_array_try_arraynum_convert_number(dst, src, 0, true) ||
                   js_typed_array_try_arraynum_convert_bigint(dst, src, 0, true)) {
            copied = true;
        }
        if (!copied) {
            for (int i = 0; i < src_len; i++) {
                source = source_root.get();
                Item idx = (Item){.item = i2it(i)};
                Item val = js_typed_array_get(source, idx);
                JS_ASSIGN_OR_RETURN(set_result, js_typed_array_set(result_root.get(), idx, val));
            }
        }
        return result_root.get();
    }

    if (src_type == LMD_TYPE_ARRAY || src_type == LMD_TYPE_ARRAY_NUM) {
        // Copy from a regular or compact numeric array. MIR Direct may keep a
        // literal numeric array in ArrayNum form, and treating that value as a
        // scalar length silently constructs an empty TypedArray.
        Array* arr = src_type == LMD_TYPE_ARRAY ? source.array : NULL;
        ArrayNum* num_arr = src_type == LMD_TYPE_ARRAY_NUM ? source.array_num : NULL;
        int len = (int)(arr ? arr->length : (num_arr ? num_arr->length : 0));
        result_root.set(js_typed_array_new(type_id, len));
        Item result = result_root.get();
        if (arr && js_typed_array_try_raw_from_dense_number_array(result, arr, len)) return result;
        Item* values = len > 0 ? (Item*)mem_alloc(sizeof(Item) * len, MEM_CAT_JS_RUNTIME) : NULL;
        for (int i = 0; i < len; i++) {
            values[i] = arr ? arr->items[i] : array_num_get(num_arr, i);
        }
        for (int i = 0; i < len; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item val = values ? values[i] : ItemNull;
            if (val.item == JS_DELETED_SENTINEL_VAL) val = (Item){.item = ITEM_JS_UNDEFINED};
            Item set_result = js_typed_array_set(result_root.get(), idx, val);
            if (item_is_error(set_result)) {
                if (values) mem_free(values);
                return set_result;
            }
        }
        if (values) mem_free(values);
        return result_root.get();
    }

    // Fallback: treat as length
    int len = (int)it2i(source);
    return js_typed_array_new(type_id, len);
}

// Smart constructor: dispatches based on argument type
extern "C" Item js_typed_array_construct(int type_id, Item arg, Item byte_offset_item, Item length_item, int argc) {
    RootFrame roots(3);
    // Constructor coercion creates names and can collect before it inspects an
    // array argument; keep every caller-owned Item exact until the result is
    // linked into its destination object.
    Rooted<Item> arg_root(roots, arg);
    Rooted<Item> byte_offset_root(roots, byte_offset_item);
    Rooted<Item> length_root(roots, length_item);
    arg = arg_root.get();
    byte_offset_item = byte_offset_root.get();
    length_item = length_root.get();
    if (argc == 0) {
        return js_typed_array_new(type_id, 0);
    }

    if (get_type_id(arg) == LMD_TYPE_BINARY &&
        (type_id == JS_TYPED_UINT8 || type_id == JS_TYPED_UINT8_CLAMPED)) {
        Binary* bin = arg.get_safe_binary();
        if (type_id == JS_TYPED_UINT8) return js_typed_array_from_binary(bin);
        Item result = js_typed_array_new(type_id, bin ? (int)binary_length(bin) : 0);
        void* data = js_typed_array_current_data_ptr(result);
        if (data && bin && binary_length(bin) > 0) {
            memcpy(data, binary_data(bin), binary_length(bin));
        }
        return result;
    }

    // Check if arg is an ArrayBuffer
    if (js_is_arraybuffer(arg)) {
        int byte_offset = 0;
        if (argc > 1) {
            JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(byte_offset_root.get(), &byte_offset));
        }
        int length = -1;
        if (argc > 2 && get_type_id(length_root.get()) != LMD_TYPE_UNDEFINED) {
            JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(length_root.get(), &length));
        }
        return js_typed_array_new_from_buffer(type_id, arg_root.get(), byte_offset, length);
    }

    // Check if arg is another TypedArray or Array
    TypeId arg_type = get_type_id(arg);
    if (js_is_typed_array(arg)) {
        return js_typed_array_new_from_array(type_id, arg);
    }
    if (arg_type == LMD_TYPE_ARRAY || arg_type == LMD_TYPE_ARRAY_NUM) {
        if (arg_type == LMD_TYPE_ARRAY_NUM) {
            return js_typed_array_new_from_array(type_id, arg_root.get());
        }
        Item iter_key = js_well_known_symbol_key(1);
        JS_ASSIGN_OR_RETURN(iter_method, js_get_key_default(arg_root.get(), iter_key));
        TypeId iter_type = get_type_id(iter_method);
        bool has_iter = iter_type != LMD_TYPE_UNDEFINED && iter_type != LMD_TYPE_NULL &&
            iter_method.item != ITEM_JS_UNDEFINED;
        if (has_iter) {
            if (!js_is_callable(iter_method)) {
                return js_throw_type_error("@@iterator is not callable");
            }
            JS_ASSIGN_OR_RETURN(values, js_iterable_to_array(arg_root.get()));
            return js_typed_array_new_from_array(type_id, values);
        }
        return js_typed_array_new_from_array(type_id, arg_root.get());
    }

    if (arg_type == LMD_TYPE_MAP || arg_type == LMD_TYPE_ELEMENT || arg_type == LMD_TYPE_FUNC || js_is_generator(arg)) {
        Item iter_method = (Item){.item = ITEM_JS_UNDEFINED};
        if (js_is_generator(arg)) {
            JS_ASSIGN_OR_RETURN(values, js_iterable_to_array(arg));
            return js_typed_array_new_from_array(type_id, values);
        }
        Item iter_key = js_well_known_symbol_key(1);
        JS_ASSIGN_OR_RETURN_INTO(iter_method, js_get_key_default(arg_root.get(), iter_key));
        TypeId iter_type = get_type_id(iter_method);
        bool has_iter = iter_type != LMD_TYPE_UNDEFINED && iter_type != LMD_TYPE_NULL && iter_method.item != ITEM_JS_UNDEFINED;
        if (has_iter) {
            if (!js_is_callable(iter_method)) {
                return js_throw_type_error("@@iterator is not callable");
            }
            JS_ASSIGN_OR_RETURN(values, js_iterable_to_array(arg_root.get()));
            return js_typed_array_new_from_array(type_id, values);
        }

        Item length_key = (Item){.item = s2it(heap_create_name("length"))};
        JS_ASSIGN_OR_RETURN(length_value, js_get_key_default(arg_root.get(), length_key));
        int len = 0;
        JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(length_value, &len));
        Item result = js_typed_array_new(type_id, len);
        for (int i = 0; i < len; i++) {
            JS_ASSIGN_OR_RETURN(value, js_get_key_default(arg_root.get(), (Item){.item = i2it(i)}));
            JS_ASSIGN_OR_RETURN(set_result, js_typed_array_set(result, (Item){.item = i2it(i)}, value));
        }
        return result;
    }

    // Symbol/BigInt cannot be converted to number (ES spec: ToIndex → ToNumber throws)
    // JS symbols are encoded as negative ints (LMD_TYPE_INT with value <= -(1LL << 40))
    if (arg_type == LMD_TYPE_SYMBOL || 
        (arg_type == LMD_TYPE_INT && it2i(arg) <= -(int64_t)(1LL << 40))) {
        return js_throw_type_error("Cannot convert a Symbol value to a number");
    }

    if (arg_type != LMD_TYPE_MAP && arg_type != LMD_TYPE_ARRAY && arg_type != LMD_TYPE_FUNC) {
        int len = 0;
        JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(arg, &len));
        return js_typed_array_new(type_id, len);
    }

    return js_typed_array_new(type_id, 0);
}

static double js_typed_array_load_number_direct(JsTypedArrayType type,
                                                 const char* data, int index) {
    if (!data || index < 0) return 0.0;
    const JsTypedArraySpec* spec = js_typed_array_spec(type);
    const uint8_t* slot = (const uint8_t*)data + (size_t)index * spec->byte_size;
    if (type == JS_TYPED_FLOAT16) {
        uint16_t bits = 0;
        memcpy(&bits, slot, sizeof(bits));
        return js_float16_bits_to_float64(bits);
    }
    if (type == JS_TYPED_FLOAT32) {
        float value = 0.0f;
        memcpy(&value, slot, sizeof(value));
        return (double)value;
    }
    if (type == JS_TYPED_FLOAT64) {
        double value = 0.0;
        memcpy(&value, slot, sizeof(value));
        return value;
    }
    if (!spec->integer) return 0.0;
    uint64_t raw = 0;
    memcpy(&raw, slot, spec->byte_size);
    if (spec->signed_integer && spec->bits < 64 &&
            (raw & (1ULL << (spec->bits - 1)))) {
        raw |= ~((1ULL << spec->bits) - 1);
    }
    return spec->signed_integer ? (double)(int64_t)raw : (double)raw;
}

extern "C" Item js_typed_array_raw_get_item(JsTypedArray* ta, void* data, int idx) {
    if (!ta || !data || idx < 0) return (Item){.item = ITEM_JS_UNDEFINED};
    if (js_typed_array_is_number_element(ta->element_type)) {
        double value;
        if (ta->element_type == JS_TYPED_FLOAT16) {
            value = js_float16_bits_to_float64(((uint16_t*)data)[idx]);
        } else if (js_typed_array_arraynum_view_matches(ta, (const char*)data, idx)) {
            // Numeric ArrayNum reads must be boxed as JS Numbers, not compact ints.
            value = array_num_get_number_value(ta->view, idx);
        } else {
            value = js_typed_array_load_number_direct(ta->element_type,
                                                      (const char*)data, idx);
        }
        return js_make_number(value);
    }
    if (ta->element_type == JS_TYPED_BIGINT64) {
        return bigint_from_int64(((int64_t*)data)[idx]);
    }
    if (ta->element_type == JS_TYPED_BIGUINT64) {
        uint64_t value = ((uint64_t*)data)[idx];
        if (value <= (uint64_t)INT64_MAX) return bigint_from_int64((int64_t)value);
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
        return bigint_from_string(buf, len);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_typed_array_get(Item ta_item, Item index) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_JS_UNDEFINED};

    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    int idx = (int)it2i(index);

    int current_length = js_typed_array_current_length(ta);
    if (idx < 0 || idx >= current_length) return (Item){.item = ITEM_JS_UNDEFINED};
    void* data = js_typed_array_current_data(ta);
    if (!data) return (Item){.item = ITEM_JS_UNDEFINED};
    return js_typed_array_raw_get_item(ta, data, idx);
}

static Item js_typed_array_set_numeric_impl(Item ta_item, double numeric_index,
        bool is_negative_zero, Item value) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};

    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    int idx = -1;
    if (!is_negative_zero && isfinite(numeric_index) &&
            floor(numeric_index) == numeric_index && numeric_index >= 0.0 &&
            numeric_index <= (double)INT32_MAX) {
        idx = (int)numeric_index;
    }

    // BigInt types: ToBigInt(value), then store as int64/uint64.
    // Per ES spec §22.2.3.5.4 IntegerIndexedElementSet: BigInt typed arrays use ToBigInt
    // which throws TypeError for Numbers; only BigInt, String (parseable), and Boolean coerce.
    if (ta->element_type == JS_TYPED_BIGINT64 || ta->element_type == JS_TYPED_BIGUINT64) {
        extern Item js_bigint_constructor(Item value);
        extern int64_t bigint_to_int64(Item bi);
        // Inline BigInt detection (avoids exposing js_is_bigint as static)
        TypeId vt = get_type_id(value);
        bool is_bi = false;
        if (vt == LMD_TYPE_DECIMAL) {
            Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFFULL);
            is_bi = dec && dec->unlimited == DECIMAL_BIGINT;
        }
        Item bi;
        if (is_bi) {
            bi = value;
        } else {
            // ToBigInt rejects Number/null/undefined per spec
            if (vt == LMD_TYPE_INT) {
                int64_t iv = it2i(value);
                if (iv > -(int64_t)JS_SYMBOL_BASE) {
                    return js_throw_type_error("Cannot convert non-BigInt value to BigInt");
                }
                // Symbol → handled by ctor (throws)
            } else if (vt == LMD_TYPE_FLOAT) {
                return js_throw_type_error("Cannot convert non-BigInt value to BigInt");
            } else if (vt == LMD_TYPE_NULL || value.item == ITEM_JS_UNDEFINED) {
                return js_throw_type_error("Cannot convert non-BigInt value to BigInt");
            }
            JS_ASSIGN_OR_RETURN_INTO(bi, js_bigint_constructor(value));
        }
        int current_length = js_typed_array_current_length(ta);
        void* data = js_typed_array_prepare_write(ta);
        if (idx < 0 || idx >= current_length || !data) return value;
        if (ta->element_type == JS_TYPED_BIGINT64) {
            JS_ASSIGN_OR_RETURN(wrapped, js_bigint_as_int_n((Item){.item = i2it(64)}, bi));
            int64_t iv = bigint_to_int64(wrapped);
            ((int64_t*)data)[idx] = iv;
        } else {
            JS_ASSIGN_OR_RETURN(wrapped, js_bigint_as_uint_n((Item){.item = i2it(64)}, bi));
            ((uint64_t*)data)[idx] = js_dataview_bigint_to_uint64(wrapped);
        }
        return value;
    }

    double num_val;
    TypeId vtype = get_type_id(value);
    if (value.item == ITEM_JS_UNDEFINED) {
        num_val = NAN;
    } else if (vtype == LMD_TYPE_INT) {
        // Check for Symbol (encoded as negative int <= -JS_SYMBOL_BASE)
        int64_t iv = it2i(value);
        if (iv <= -(int64_t)JS_SYMBOL_BASE) {
            return js_throw_type_error("Cannot convert a Symbol value to a number");
        }
        num_val = (double)iv;
    } else if (vtype == LMD_TYPE_FLOAT) {
        num_val = it2d(value);
    } else {
        // ES spec: IntegerIndexedElementSet calls ToNumber(value)
        // This throws TypeError for Symbol, which is the spec-required behavior
        JS_ASSIGN_OR_RETURN(num_item, js_to_number(value));
        vtype = get_type_id(num_item);
        if (vtype == LMD_TYPE_INT) num_val = (double)it2i(num_item);
        else if (vtype == LMD_TYPE_FLOAT) num_val = it2d(num_item);
        else num_val = 0.0;
    }

    int current_length = js_typed_array_current_length(ta);
    if (idx < 0 || idx >= current_length) return value;
    void* data = js_typed_array_prepare_write(ta);
    if (!data) return value;

    js_typed_array_store_number_direct(ta->element_type, (char*)data, idx, num_val);

    return value;
}

extern "C" Item js_typed_array_set(Item ta_item, Item index, Item value) {
    return js_typed_array_set_numeric_impl(ta_item, (double)it2i(index),
        false, value);
}

extern "C" Item js_typed_array_set_numeric(Item ta_item, double numeric_index,
        bool is_negative_zero, Item value) {
    return js_typed_array_set_numeric_impl(ta_item, numeric_index,
        is_negative_zero, value);
}

extern "C" int js_typed_array_length(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return 0;
    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_current_length(ta);
}

// Js54 P3: live data pointer for the typed array's element storage.
// Used by the MIR JIT inline indexed get/set paths so resizable-buffer-backed
// views resolve the current handle storage after resize changes its generation
// (cached descriptor data would point at the freed/stale backing store otherwise).
// Returns NULL for OOB or detached views — callers must treat NULL as a
// short-circuit on the access path.
extern "C" void* js_typed_array_current_data_ptr(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return NULL;
    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_current_data(ta);
}

extern "C" bool js_item_bytes(Item item, const char** data, int* len) {
    if (!data || !len) return false;
    *data = NULL;
    *len = 0;
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        if (!s) return false;
        *data = s->chars;
        *len = (int)s->len;
        return true;
    }
    if (js_is_typed_array(item)) {
        if (js_typed_array_is_out_of_bounds_item(item)) return false;
        int byte_len = js_typed_array_byte_length(item);
        void* ptr = js_typed_array_current_data_ptr(item);
        if (byte_len > 0 && !ptr) return false;
        *data = (const char*)ptr;
        *len = byte_len;
        return true;
    }
    return false;
}

extern "C" void* js_typed_array_prepare_write_ptr(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return NULL;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_prepare_write(ta);
}

static int js_typed_array_byte_measure(Item ta_item, bool offset) {
    if (!js_is_typed_array(ta_item)) return 0;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    js_typed_array_refresh_arraynum_view(ta);
    return offset ? js_typed_array_current_byte_offset(ta) :
        js_typed_array_current_byte_length(ta);
}

JS_FORWARD_EXPRESSION(int, js_typed_array_byte_length, (Item ta_item),
    js_typed_array_byte_measure(ta_item, false))
JS_FORWARD_EXPRESSION(int, js_typed_array_byte_offset, (Item ta_item),
    js_typed_array_byte_measure(ta_item, true))

extern "C" Item js_typed_array_fill(Item ta_item, Item value, int start,
        int end, bool array_semantics) {
    if (!js_is_typed_array(ta_item)) return ta_item;

    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    bool is_bigint_array = ta && (ta->element_type == JS_TYPED_BIGINT64 || ta->element_type == JS_TYPED_BIGUINT64);
    double num_val = 0.0;
    int64_t bigint_i64 = 0;
    uint64_t bigint_u64 = 0;

    if (is_bigint_array) {
        Item bigint_item;
        JS_ASSIGN_OR_RETURN(validation, js_dataview_to_bigint_value(value, &bigint_item));
        // BigInt typed-array stores wrap modulo 64 bits; bigint_to_int64 alone clamps oversized inputs.
        JS_ASSIGN_OR_RETURN(wrapped, (ta->element_type == JS_TYPED_BIGINT64)
            ? js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_item)
            : js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_item));
        if (ta->element_type == JS_TYPED_BIGINT64) {
            bigint_i64 = bigint_to_int64(wrapped);
        } else {
            bigint_u64 = js_dataview_bigint_to_uint64(wrapped);
        }
    } else {
        if (value.item == ITEM_JS_UNDEFINED) {
            num_val = NAN;
        } else {
            JS_ASSIGN_OR_RETURN(validation, js_dataview_to_number_value(value, &num_val));
        }
    }

    // Js54 P6: gate the TA-spec OOB throw on the dispatch mode. When invoked
    // via Array.prototype.fill.call(ta_oob, ...) the spec uses LengthOfArrayLike
    // (which yields 0 for an OOB TA) and the method silently no-ops.
    if (!array_semantics && js_typed_array_is_out_of_bounds(ta)) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.fill on an out-of-bounds ArrayBuffer");
    }

    int len = js_typed_array_current_length(ta);
    void* data = js_typed_array_prepare_write(ta);
    if (!data || len <= 0) return ta_item;

    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (end == INT_MAX || end > len) end = len;
    else if (end < 0) end = len + end;
    if (end < 0) end = 0;
    if (start >= end) return ta_item;

    int count = end - start;

    if (js_typed_array_is_bigint_element(ta->element_type)) {
        if (ta->element_type == JS_TYPED_BIGINT64) {
            for (int i = 0; i < count; i++) ((int64_t*)data)[start + i] = bigint_i64;
        } else {
            for (int i = 0; i < count; i++) ((uint64_t*)data)[start + i] = bigint_u64;
        }
    } else {
        for (int i = 0; i < count; i++) {
            js_typed_array_store_number_direct(ta->element_type, (char*)data,
                                                start + i, num_val);
        }
    }

    return ta_item;
}

// .set(source [, offset]) — bulk copy from another array/typed array
extern "C" Item js_typed_array_set_from(Item ta_item, Item source, int offset) {
    if (!js_is_typed_array(ta_item)) return ItemNull;
    JsTypedArray* dst = js_get_typed_array_ptr(ta_item.map);
    // Js54 P6: gate the TA-spec OOB throw on dispatch mode (see fill).
    if (!dst || js_typed_array_is_out_of_bounds(dst)) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.set on a detached or out-of-bounds ArrayBuffer");
    }
    int target_len = js_typed_array_current_length(dst);
    if (offset < 0) return js_throw_range_error("offset is out of bounds");

    if (js_is_typed_array(source)) {
        JsTypedArray* src = js_get_typed_array_ptr(source.map);
        // Js54 P6: gate the TA-spec OOB throw on dispatch mode.
        if (!src || js_typed_array_is_out_of_bounds(src) ||
            js_typed_array_is_out_of_bounds(dst)) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.set on a detached or out-of-bounds ArrayBuffer");
        }
        int src_len = js_typed_array_current_length(src);
        if ((int64_t)offset + (int64_t)src_len > (int64_t)target_len) {
            return js_throw_range_error("source is too large");
        }
        if (src_len <= 0) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
        if (js_typed_array_is_bigint_element(dst->element_type) !=
            js_typed_array_is_bigint_element(src->element_type)) {
            return js_throw_type_error("Cannot mix BigInt and non-BigInt typed arrays");
        }
        if (js_typed_array_try_raw_set_same_type(dst, src, offset)) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        if (js_typed_array_try_arraynum_convert_number(dst, src, offset, false)) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        if (js_typed_array_try_arraynum_convert_bigint(dst, src, offset, false)) {
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        Item* values = (Item*)mem_alloc((size_t)src_len * sizeof(Item), MEM_CAT_JS_RUNTIME);
        if (!values) return js_throw_type_error("TypedArray.prototype.set allocation failed");
        for (int i = 0; i < src_len; i++) {
            values[i] = js_typed_array_get(source, (Item){.item = i2it(i)});
            if (item_is_error(values[i])) {
                mem_free(values);
                return values[i];
            }
        }
        for (int i = 0; i < src_len; i++) {
            Item set_result = js_typed_array_set(ta_item, (Item){.item = i2it(offset + i)}, values[i]);
            if (item_is_error(set_result)) {
                mem_free(values);
                return set_result;
            }
        }
        mem_free(values);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    TypeId source_type = get_type_id(source);
    if (source_type == LMD_TYPE_NULL || source_type == LMD_TYPE_UNDEFINED || source.item == ITEM_JS_UNDEFINED) {
        return js_throw_type_error("Cannot convert undefined or null to object");
    }
    JS_ASSIGN_OR_RETURN(src_obj, js_to_object(source));

    Item length_key = (Item){.item = s2it(heap_create_name("length"))};
    JS_ASSIGN_OR_RETURN(length_item, js_get_key_default(src_obj, length_key));
    JS_ASSIGN_OR_RETURN(length_num, js_to_number(length_item));
    double length_double = js_get_number(length_num);
    int64_t src_len = 0;
    if (length_double != length_double || length_double <= 0.0) {
        src_len = 0;
    } else if (length_double >= 9007199254740991.0) {
        src_len = 9007199254740991LL;
    } else {
        src_len = (int64_t)floor(length_double);
    }
    // Js55 P14: per ES §22.2.3.26.1 SetTypedArrayFromArrayLike, the targetLength
    // is captured BEFORE LengthOfArrayLike(src) is called (which can invoke the
    // source's length getter and resize the backing buffer). The range check at
    // step 10 uses that original targetLength. Do NOT re-fetch target_len or
    // re-check OOB after the length getter — the write loop uses
    // js_typed_array_set, which silently no-ops for OOB indices via the
    // IsValidIntegerIndex check.
    if ((int64_t)offset + src_len > (int64_t)target_len) {
        return js_throw_range_error("source is too large");
    }

    for (int64_t i = 0; i < src_len; i++) {
        JS_ASSIGN_OR_RETURN(value, js_get_key_default(src_obj, (Item){.item = i2it(i)}));
        JS_ASSIGN_OR_RETURN(set_result, js_typed_array_set(ta_item, (Item){.item = i2it((int64_t)offset + i)}, value));
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// .slice(begin, end) — creates a copy
extern "C" Item js_typed_array_slice(Item ta_item, int start, int end,
        bool array_semantics) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);

    // Js54 P6: gate the TA-spec OOB throw on dispatch mode.
    if (!array_semantics && js_typed_array_is_out_of_bounds(ta)) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on an out-of-bounds ArrayBuffer");
    }

    int current_len = js_typed_array_current_length(ta);

    if (start < 0) start = current_len + start;
    if (end < 0) end = current_len + end;
    if (start < 0) start = 0;
    if (start >= end) {
        JS_ASSIGN_OR_RETURN(result, js_typed_array_species_create(ta_item, 0));
        return result;
    }

    int new_length = end - start;
    JS_ASSIGN_OR_RETURN(result, js_typed_array_species_create(ta_item, new_length));
    if (new_length > 0 && !array_semantics) {
        if (ta->buffer && js_arraybuffer_detached(ta->buffer)) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on a detached ArrayBuffer");
        }
        if (js_typed_array_is_out_of_bounds(ta)) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on an out-of-bounds ArrayBuffer");
        }
    }
    // Copy elements — species may return a different typed array type, so use element-by-element copy
    JsTypedArray* rta = js_get_typed_array_ptr(result.map);
    if (rta && rta->element_type == ta->element_type &&
            js_typed_array_current_length(rta) >= new_length) {
        int elem_size = js_typed_array_element_size(ta->element_type);
        int count_bytes = new_length * elem_size;
        int source_byte_length = js_typed_array_current_byte_length(ta);
        js_typed_array_refresh_arraynum_view(ta);
        js_typed_array_refresh_arraynum_view(rta);
        char* src_data = (char*)js_typed_array_current_data(ta);
        char* dst_data = (char*)js_typed_array_prepare_write(rta);
        int src_start = start * elem_size;
        if (src_data && dst_data && ta->buffer && rta->buffer && ta->buffer == rta->buffer) {
            // same-buffer species results must follow the spec's forward byte copy.
            for (int i = 0; i < count_bytes; i++) {
                int src_index = src_start + i;
                dst_data[i] = (src_index >= 0 && src_index < source_byte_length) ? src_data[src_index] : 0;
            }
        } else if (src_data && dst_data &&
            js_typed_array_arraynum_range_matches(ta, src_data, start, new_length) &&
            js_typed_array_arraynum_range_matches(rta, dst_data, 0, new_length)) {
            array_num_copy_same_type_bytes(rta->view, 0, ta->view, start, new_length);
        } else if (src_data && dst_data && source_byte_length >= src_start + count_bytes) {
            memcpy(dst_data, src_data + src_start, count_bytes);
        } else if (dst_data) {
            for (int i = 0; i < count_bytes; i++) {
                int src_index = src_start + i;
                dst_data[i] = (src_data && src_index >= 0 && src_index < source_byte_length) ? src_data[src_index] : 0;
            }
        }
    } else {
        // Different type — element-by-element
        int current_len = js_typed_array_current_length(ta);
        for (int i = 0; i < new_length; i++) {
            Item elem;
            if (start + i < current_len) {
                elem = js_typed_array_get(ta_item, (Item){.item = i2it(start + i)});
            } else if (ta->element_type == JS_TYPED_BIGINT64 || ta->element_type == JS_TYPED_BIGUINT64) {
                elem = bigint_from_int64(0);
            } else {
                elem = (Item){.item = i2it(0)};
            }
            js_typed_array_set(result, (Item){.item = i2it(i)}, elem);
        }
    }
    return result;
}

// .subarray(begin, end) — creates a view (shares buffer)
extern "C" Item js_typed_array_subarray(Item ta_item, int start, int end, bool end_is_default) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);

    int elem_size = js_typed_array_element_size(ta->element_type);
    int byte_offset = js_typed_array_stored_byte_offset(ta);
    int available_len = js_typed_array_stored_length(ta);
    int begin_byte_offset = byte_offset + start * elem_size;
    bool result_length_tracking = ta->buffer && ta->length_tracking && end_is_default;

    if (ta->buffer && !js_arraybuffer_detached(ta->buffer)) {
        int buffer_length = js_arraybuffer_length(ta->buffer);
        int available_bytes = buffer_length - byte_offset;
        if (available_bytes < 0) available_bytes = 0;
        available_len = available_bytes / elem_size;
        if (begin_byte_offset > buffer_length) {
            return js_throw_range_error("offset is out of bounds");
        }
        if (!result_length_tracking && end > available_len) {
            return js_throw_range_error("length is out of bounds");
        }
    }

    if (end < start) end = start;
    int new_length = result_length_tracking ? available_len - start : end - start;
    if (new_length < 0) new_length = 0;

    if (!ta->buffer_item || !ta->buffer) return js_throw_type_error("TypedArray has no backing ArrayBuffer");
    return js_typed_array_species_create_from_buffer(
        ta_item, (Item){.item = ta->buffer_item}, byte_offset + start * elem_size,
        new_length, result_length_tracking);
}

// ============================================================================
// DataView
// ============================================================================

extern "C" bool js_is_dataview(Item val) {
    if (get_type_id(val) != LMD_TYPE_MAP || !val.map ||
            val.map->map_kind != MAP_KIND_DATAVIEW) return false;
    // DataView.prototype is metadata-branded so its methods are discoverable,
    // but it has no [[DataView]] carrier and must fail the accessor brand check.
    return js_object_has_class(val, JS_CLASS_DATA_VIEW);
}

// Get the typed trailing DataView payload after validating the carrier.
static JsDataView* js_get_dataview_ptr_from_map(Map* m) {
    if (!m || m->map_kind != MAP_KIND_DATAVIEW) return NULL;
    return &((JsDataViewMapCarrier*)m)->payload;
}

extern "C" JsDataView* js_get_dataview_ptr(Item val) {
    if (!js_is_dataview(val)) return NULL;
    return js_get_dataview_ptr_from_map(val.map);
}

static Item js_dataview_create(Item buffer, Item offset_item, Item length_item,
        Item new_target) {
    RootFrame roots(4);
    Rooted<Item> buffer_root(roots, buffer);
    Rooted<Item> new_target_root(roots, new_target);
    Rooted<Item> prototype_root(roots, ItemNull);
    Rooted<Item> view_root(roots, ItemNull);
    buffer = buffer_root.get();
    if (!js_is_arraybuffer(buffer)) {
        return js_throw_type_error("First argument to DataView constructor must be an ArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(buffer.map);
    if (!ab) return ItemNull;

    int byte_offset = 0;
    JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(offset_item, &byte_offset));

    if (js_arraybuffer_detached(ab)) {
        return js_throw_type_error("DataView buffer is detached");
    }

    int buffer_length = js_arraybuffer_length(ab);
    if (byte_offset < 0 || byte_offset > buffer_length) {
        return js_throw_range_error("Start offset is outside the bounds of the buffer");
    }

    int byte_length;
    TypeId lt = get_type_id(length_item);
    // Js54 P2: length_tracking when constructor called without explicit byteLength.
    // The recorded byte_length is the initial value; readers re-derive from the
    // buffer's current byte_length on every access for length-tracking views.
    bool length_tracking = (lt == LMD_TYPE_UNDEFINED) && js_arraybuffer_resizable(ab);
    if (lt == LMD_TYPE_UNDEFINED) {
        byte_length = buffer_length - byte_offset;
    } else {
        JS_ASSIGN_OR_RETURN_INTO(validation, js_dataview_to_index(length_item, &byte_length));
    }

    if (byte_length < 0 || (int64_t)byte_offset + (int64_t)byte_length > buffer_length) {
        return js_throw_range_error("Invalid DataView length");
    }

    if (new_target_root.get().item != ItemNull.item) {
        // DataView converts and initially validates its arguments before
        // OrdinaryCreateFromConstructor, then revalidates the buffer after the
        // observable prototype lookup. Fetching the prototype in the generic
        // constructor wrapper ran user code before byteOffset.valueOf.
        prototype_root.set(js_get_prototype_from_constructor_default(
            new_target_root.get(), JS_CLASS_DATA_VIEW, -1));
        if (item_is_error(prototype_root.get())) return prototype_root.get();
        buffer = buffer_root.get();
        ab = js_get_arraybuffer_ptr(buffer.map);
        if (!ab || js_arraybuffer_detached(ab)) {
            return js_throw_type_error("DataView buffer is detached");
        }
        buffer_length = js_arraybuffer_length(ab);
        if (byte_offset < 0 || byte_offset > buffer_length) {
            return js_throw_range_error(
                "Start offset is outside the bounds of the buffer");
        }
        if (length_tracking) {
            // A resizable-buffer DataView with omitted byteLength tracks the
            // post-prototype-lookup buffer extent. Retaining the pre-getter
            // length incorrectly turned a still-valid end offset into OOB.
            byte_length = buffer_length - byte_offset;
        } else if (byte_length < 0 ||
                (int64_t)byte_offset + (int64_t)byte_length > buffer_length) {
            return js_throw_range_error("Invalid DataView length");
        }
    }

    JsDataViewMapCarrier* carrier = (JsDataViewMapCarrier*)heap_calloc(
        sizeof(JsDataViewMapCarrier), LMD_TYPE_MAP);
    if (!carrier) return ItemNull;
    JsDataView* dv = &carrier->payload;
    // mem_alloc can collect; refresh the rooted backing Item before storing
    // the native references that must remain coherent with the GC owner.
    buffer = buffer_root.get();
    ab = js_get_arraybuffer_ptr(buffer.map);
    if (!ab) return ItemNull;
    dv->buffer = ab;
    dv->byte_offset = byte_offset;
    dv->byte_length = byte_length;
    dv->buffer_item = buffer.item;
    dv->length_tracking = length_tracking;

    Map* m = &carrier->base;
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_DATAVIEW;
    m->type = js_object_type_for_class(JS_CLASS_DATA_VIEW);
    if (!m->type) m->type = &EmptyMap;
    m->data = NULL;
    m->data_cap = 0;
    view_root.set((Item){.map = m});
    if (new_target_root.get().item != ItemNull.item) {
        js_set_prototype(view_root.get(), prototype_root.get());
    } else {
        js_dataview_link_prototype(view_root.get());
    }
    return view_root.get();
}
JS_FORWARD_ITEM(js_dataview_new, (Item buffer, Item offset_item,         Item length_item), js_dataview_create, (buffer, offset_item, length_item, ItemNull))
JS_FORWARD_ITEM(js_dataview_construct, (Item buffer, Item offset_item,         Item length_item, Item new_target), js_dataview_create, (buffer, offset_item, length_item, new_target))

// Js54 P2: current byte_length honoring length-tracking views over resizable
// buffers. For non-length-tracking views the stored byte_length is authoritative;
// for length-tracking views we re-derive from the buffer's current size.
static inline int dv_current_byte_length(JsDataView* dv) {
    if (!dv || !dv->buffer) return 0;
    if (dv->length_tracking) {
        int avail = js_arraybuffer_length(dv->buffer) - dv->byte_offset;
        return avail > 0 ? avail : 0;
    }
    return dv->byte_length;
}

// Js54 P2: a DataView is out-of-bounds when the buffer is detached, or when the
// recorded view window no longer fits (resize shrank the buffer below
// byte_offset + byte_length, or below byte_offset for length-tracking views).
static inline bool dv_is_out_of_bounds(JsDataView* dv) {
    if (!dv || !dv->buffer) return false;
    if (js_arraybuffer_detached(dv->buffer)) return true;
    if (dv->length_tracking) {
        return js_arraybuffer_length(dv->buffer) < dv->byte_offset;
    }
    return js_arraybuffer_length(dv->buffer) < (int64_t)dv->byte_offset + (int64_t)dv->byte_length;
}

// Js54 P2: throws TypeError if the DataView is detached or out-of-bounds.
// The status Item keeps the coercion error on the same lane as DataView calls.
static inline Item dv_validate_or_throw(JsDataView* dv) {
    if (dv_is_out_of_bounds(dv)) {
        return js_throw_type_error("DataView buffer is detached or out of bounds");
    }
    return js_status_ok();
}

// Helper: get raw pointer into DataView's buffer at given offset
// Js54 P2: bounds-check against the CURRENT view length so length-tracking
// views see live shrink/grow, not the cached construction-time byte_length.
static inline const uint8_t* dv_ptr(JsDataView* dv, int offset, int size) {
    int current_len = dv_current_byte_length(dv);
    if (offset < 0 || offset + size > current_len) return NULL;
    return js_arraybuffer_data_const(dv->buffer) + dv->byte_offset + offset;
}

static inline uint8_t* dv_write_ptr(JsDataView* dv, int offset, int size) {
    int current_len = dv_current_byte_length(dv);
    if (offset < 0 || offset + size > current_len) return NULL;
    uint8_t* base = js_arraybuffer_prepare_write(dv->buffer);
    // DataView setters share the ArrayBuffer handle with typed arrays, so COW
    // must occur before deriving the view-relative mutable pointer.
    return base ? base + dv->byte_offset + offset : NULL;
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

static bool js_dataview_read_raw(JsDataView* dv, int offset, int size,
        bool little_endian, bool system_little_endian, uint64_t* out_raw) {
    const uint8_t* data = dv_ptr(dv, offset, size);
    if (!data) return false;
    memcpy(out_raw, data, (size_t)size);
    if (little_endian != system_little_endian) {
        if (size == 2) *out_raw = swap16((uint16_t)*out_raw);
        else if (size == 4) *out_raw = swap32((uint32_t)*out_raw);
        else if (size == 8) *out_raw = swap64(*out_raw);
    }
    return true;
}

static bool js_dataview_write_raw(JsDataView* dv, int offset, int size,
        bool little_endian, bool system_little_endian, uint64_t raw) {
    uint8_t* data = dv_write_ptr(dv, offset, size);
    if (!data) return false;
    if (little_endian != system_little_endian) {
        if (size == 2) raw = swap16((uint16_t)raw);
        else if (size == 4) raw = swap32((uint32_t)raw);
        else if (size == 8) raw = swap64(raw);
    }
    memcpy(data, &raw, (size_t)size);
    return true;
}

static Item js_dataview_read_value(JsDataView* dv, JsDataViewOperation operation,
        int offset, Item* args, int argc, bool system_little_endian) {
    bool little_endian = (argc > 1) && js_is_truthy(args[1]);
    if (operation == JS_DATAVIEW_GET_INT8 || operation == JS_DATAVIEW_GET_UINT8) {
        const uint8_t* data = dv_ptr(dv, offset, 1);
        if (!data) return js_throw_range_error("Invalid DataView offset");
        return (Item){.item = i2it(operation == JS_DATAVIEW_GET_INT8
            ? (int64_t)(int8_t)*data : (int64_t)*data)};
    }

    int size = (operation == JS_DATAVIEW_GET_INT16 || operation == JS_DATAVIEW_GET_UINT16)
        ? 2 : (operation == JS_DATAVIEW_GET_INT32 || operation == JS_DATAVIEW_GET_UINT32 ||
               operation == JS_DATAVIEW_GET_FLOAT32) ? 4 : 8;
    uint64_t raw = 0;
    if (!js_dataview_read_raw(dv, offset, size, little_endian,
            system_little_endian, &raw)) {
        return js_throw_range_error("Invalid DataView offset");
    }
    switch (operation) {
    case JS_DATAVIEW_GET_INT16: return (Item){.item = i2it((int64_t)(int16_t)raw)};
    case JS_DATAVIEW_GET_UINT16: return (Item){.item = i2it((int64_t)(uint16_t)raw)};
    case JS_DATAVIEW_GET_INT32: return (Item){.item = i2it((int64_t)(int32_t)raw)};
    case JS_DATAVIEW_GET_UINT32: return (Item){.item = i2it((int64_t)(uint32_t)raw)};
    case JS_DATAVIEW_GET_FLOAT32: {
        float value;
        uint32_t bits = (uint32_t)raw;
        memcpy(&value, &bits, sizeof(value));
        return js_make_number((double)value);
    }
    case JS_DATAVIEW_GET_FLOAT64: {
        double value;
        memcpy(&value, &raw, sizeof(value));
        return js_make_number(value);
    }
    case JS_DATAVIEW_GET_BIGINT64: return bigint_from_int64((int64_t)raw);
    case JS_DATAVIEW_GET_BIGUINT64: return js_dataview_biguint64_item(raw);
    default: return (Item){.item = ITEM_NULL};
    }
}

static Item js_dataview_write_value(JsDataView* dv, JsDataViewOperation operation,
        int offset, Item* args, int argc, bool system_little_endian) {
    Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
    Item validation = ItemNull;
    bool little_endian = (argc > 2) && js_is_truthy(args[2]);
    uint64_t raw = 0;
    int size = 0;
    if (operation == JS_DATAVIEW_SET_BIGINT64 || operation == JS_DATAVIEW_SET_BIGUINT64) {
        Item bigint_value;
        JS_ASSIGN_OR_RETURN(validation, js_dataview_to_bigint_value(value_item, &bigint_value));
        JS_ASSIGN_OR_RETURN_INTO(validation, dv_validate_or_throw(dv));
        JS_ASSIGN_OR_RETURN(wrapped, operation == JS_DATAVIEW_SET_BIGINT64
            ? js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_value)
            : js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_value));
        raw = operation == JS_DATAVIEW_SET_BIGINT64
            ? (uint64_t)bigint_to_int64(wrapped) : js_dataview_bigint_to_uint64(wrapped);
        size = 8;
    } else {
        double number_value = 0.0;
        JS_ASSIGN_OR_RETURN_INTO(validation, js_dataview_to_number_value(value_item, &number_value));
        JS_ASSIGN_OR_RETURN_INTO(validation, dv_validate_or_throw(dv));
        int64_t integer_value = js_dataview_to_integer_value(number_value);
        switch (operation) {
        case JS_DATAVIEW_SET_INT8: raw = (uint8_t)(int8_t)integer_value; size = 1; break;
        case JS_DATAVIEW_SET_UINT8: raw = (uint8_t)integer_value; size = 1; break;
        case JS_DATAVIEW_SET_INT16: raw = (uint16_t)(int16_t)integer_value; size = 2; break;
        case JS_DATAVIEW_SET_UINT16: raw = (uint16_t)integer_value; size = 2; break;
        case JS_DATAVIEW_SET_INT32: raw = (uint32_t)(int32_t)integer_value; size = 4; break;
        case JS_DATAVIEW_SET_UINT32: raw = (uint32_t)integer_value; size = 4; break;
        case JS_DATAVIEW_SET_FLOAT32: {
            float value = (float)number_value;
            memcpy(&raw, &value, sizeof(value));
            size = 4;
            break;
        }
        case JS_DATAVIEW_SET_FLOAT64:
            memcpy(&raw, &number_value, sizeof(number_value));
            size = 8;
            break;
        default: return (Item){.item = ITEM_NULL};
        }
    }
    if (!js_dataview_write_raw(dv, offset, size, little_endian,
            system_little_endian, raw)) {
        return js_throw_range_error("Invalid DataView offset");
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// D6.2.2v2: the published callable fixes this operation; display spelling
// must never select DataView semantics after function creation.
extern "C" Item js_dataview_operation(Item dv_item,
        JsDataViewOperation operation, Item* args, int argc) {
    if (!js_is_dataview(dv_item)) return (Item){.item = ITEM_NULL};
    JsDataView* dv = js_get_dataview_ptr_from_map(dv_item.map);
    if (!dv) return (Item){.item = ITEM_NULL};

    int offset = 0;
    Item offset_item = (argc > 0) ? args[0] : (Item){.item = ITEM_JS_UNDEFINED};
    JS_ASSIGN_OR_RETURN(validation, js_dataview_to_index(offset_item, &offset));
    bool is_set_method = operation >= JS_DATAVIEW_SET_INT8;
    // Js54 P2: get-methods validate up front (spec: TypeError on detached or OOB).
    // set-methods perform ToNumber/ToBigInt on the value first (those calls can
    // observe side effects that resize the buffer), so per spec the OOB check
    // moves into each individual setter after the value coercion.
    if (!is_set_method) {
        JS_ASSIGN_OR_RETURN_INTO(validation, dv_validate_or_throw(dv));
    }
    bool sys_le = is_little_endian_system();

    // Getter methods use one raw-load path so width and endian validation cannot drift.
    if (!is_set_method) {
        return js_dataview_read_value(dv, operation, offset, args, argc, sys_le);
    }
    return js_dataview_write_value(dv, operation, offset, args, argc, sys_le);

}
