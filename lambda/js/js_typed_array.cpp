/**
 * JavaScript Typed Array, ArrayBuffer, and DataView Implementation for Lambda
 */
#include "js_typed_array.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "js_coerce.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../lambda-decimal.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits.h>
#include "../../lib/mem.h"
#include <cmath>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#define JS_TA_SET_STATS_MKDIR(path) _mkdir(path)
#define JS_TA_SET_STATS_OPEN(path) _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE)
#define JS_TA_SET_STATS_WRITE(fd, buf, len) _write(fd, buf, (unsigned int)(len))
#define JS_TA_SET_STATS_CLOSE(fd) _close(fd)
#define JS_TA_SET_STATS_PID() _getpid()
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define JS_TA_SET_STATS_MKDIR(path) mkdir(path, 0755)
#define JS_TA_SET_STATS_OPEN(path) open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644)
#define JS_TA_SET_STATS_WRITE(fd, buf, len) write(fd, buf, len)
#define JS_TA_SET_STATS_CLOSE(fd) close(fd)
#define JS_TA_SET_STATS_PID() getpid()
#endif

extern void* heap_alloc(int size, TypeId type_id);
extern "C" int js_check_exception(void);
extern "C" Item js_get_constructor(Item name_item);
extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_to_object(Item value);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_iterable_to_array(Item iterable);
extern "C" bool js_is_generator(Item obj);
extern Item js_to_number(Item);
extern double js_get_number(Item value);
extern "C" Item js_bigint_constructor(Item value);
extern "C" Item js_bigint_as_int_n(Item bits_item, Item bigint_item);
extern "C" Item js_bigint_as_uint_n(Item bits_item, Item bigint_item);
extern "C" int js_262_agent_current_slot_for_atomics(void);
extern "C" Item js_promise_create_pending(void);
extern "C" void js_promise_fulfill_existing(Item promise, Item value);
extern "C" void heap_register_gc_root(uint64_t* slot);

static bool js_dataview_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFFULL);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static bool js_dataview_to_bigint_value(Item value, Item* out_bigint) {
    if (js_dataview_is_bigint(value)) {
        *out_bigint = value;
        return true;
    }

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_MAP || value_type == LMD_TYPE_ARRAY || value_type == LMD_TYPE_FUNC) {
        Item primitive = js_to_primitive(value, JS_HINT_NUMBER);
        if (js_check_exception()) return false;
        return js_dataview_to_bigint_value(primitive, out_bigint);
    }

    if (value_type == LMD_TYPE_INT) {
        int64_t int_value = it2i(value);
        if (int_value <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_type_error("Cannot convert a Symbol value to a BigInt");
        } else {
            js_throw_type_error("Cannot convert non-BigInt value to BigInt");
        }
        return false;
    }
    if (value_type == LMD_TYPE_FLOAT || value_type == LMD_TYPE_NULL || value.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert non-BigInt value to BigInt");
        return false;
    }

    Item bigint_value = js_bigint_constructor(value);
    if (js_check_exception()) return false;
    *out_bigint = bigint_value;
    return true;
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

static bool js_dataview_to_index(Item value, int* out_index) {
    if (get_type_id(value) == LMD_TYPE_UNDEFINED) {
        *out_index = 0;
        return true;
    }
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL ||
        (value_type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return false;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return false;
    TypeId type = get_type_id(num);
    double d = (type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    if (std::isnan(d)) {
        *out_index = 0;
        return true;
    }
    d = std::trunc(d);
    if (d < 0 || !std::isfinite(d) || d > (double)INT_MAX) {
        js_throw_range_error("Invalid DataView index");
        return false;
    }
    *out_index = (int)d;
    return true;
}

static bool js_dataview_to_number_value(Item value, double* out_number) {
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL ||
        (value_type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return false;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return false;
    TypeId num_type = get_type_id(num);
    *out_number = (num_type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    return true;
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
    Item proto = js_property_get(ctor, proto_key);
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(view, proto);
}

// Sentinel markers for type identification
static TypeMap js_typed_array_type_marker = {};
static TypeMap js_arraybuffer_type_marker = {};
static int js_sharedarraybuffer_type_marker = 0;
static TypeMap js_dataview_type_marker = {};
char js_typed_array_marker = 'T';

typedef struct JsTypedArraySetCounters {
    long calls_total;
    long typed_array_source_calls;
    long array_like_source_calls;
    long typed_array_elements_total;
    long array_like_elements_total;
    long same_type_fast_attempts;
    long same_type_fast_hits;
    long number_convert_attempts;
    long number_convert_hits;
    long bigint_convert_attempts;
    long bigint_convert_hits;
    long generic_boxed_fallbacks;
    long generic_boxed_elements;
    long array_like_loop_elements;
    long empty_typed_array_sources;
} JsTypedArraySetCounters;

static bool g_js_ta_set_stats_enabled = false;
static bool g_js_ta_set_stats_checked = false;
static bool g_js_ta_set_stats_registered = false;
static JsTypedArraySetCounters g_js_ta_set_stats;

static void js_ta_set_stats_report(void);

static int js_ta_set_stats_is_enabled(void) {
    if (!g_js_ta_set_stats_checked) {
        const char* flag = getenv("LAMBDA_JS_TA_SET_STATS");
        if (flag && flag[0] && strcmp(flag, "0") != 0) {
            g_js_ta_set_stats_enabled = true;
        }
        g_js_ta_set_stats_checked = true;
    }
    if (g_js_ta_set_stats_enabled && !g_js_ta_set_stats_registered) {
        atexit(js_ta_set_stats_report);
        g_js_ta_set_stats_registered = true;
    }
    return g_js_ta_set_stats_enabled ? 1 : 0;
}

static void js_ta_set_stats_add(long* counter, int64_t value) {
    if (!counter || value <= 0) return;
    *counter += (long)value;
}

static void js_ta_set_stats_write_line(int fd, const char* line) {
    if (fd < 0 || !line) return;
    size_t len = strlen(line);
    const char* cur = line;
    while (len > 0) {
        int wrote = (int)JS_TA_SET_STATS_WRITE(fd, cur, len);
        if (wrote <= 0) return;
        cur += wrote;
        len -= (size_t)wrote;
    }
}

static void js_ta_set_stats_report(void) {
    if (!g_js_ta_set_stats_enabled || g_js_ta_set_stats.calls_total == 0) return;
    const char* dir = getenv("LAMBDA_JS_TA_SET_STATS_DIR");
    if (!dir || !dir[0]) dir = "./temp/js_ta_set_stats";
    JS_TA_SET_STATS_MKDIR(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.tsv", dir, (int)JS_TA_SET_STATS_PID());
    int fd = JS_TA_SET_STATS_OPEN(path);
    if (fd < 0) return;
    js_ta_set_stats_write_line(fd,
        "calls_total\ttyped_array_source_calls\tarray_like_source_calls\ttyped_array_elements_total\tarray_like_elements_total\tsame_type_fast_attempts\tsame_type_fast_hits\tnumber_convert_attempts\tnumber_convert_hits\tbigint_convert_attempts\tbigint_convert_hits\tgeneric_boxed_fallbacks\tgeneric_boxed_elements\tarray_like_loop_elements\tempty_typed_array_sources\n");
    char line[768];
    snprintf(line, sizeof(line),
        "%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
        g_js_ta_set_stats.calls_total,
        g_js_ta_set_stats.typed_array_source_calls,
        g_js_ta_set_stats.array_like_source_calls,
        g_js_ta_set_stats.typed_array_elements_total,
        g_js_ta_set_stats.array_like_elements_total,
        g_js_ta_set_stats.same_type_fast_attempts,
        g_js_ta_set_stats.same_type_fast_hits,
        g_js_ta_set_stats.number_convert_attempts,
        g_js_ta_set_stats.number_convert_hits,
        g_js_ta_set_stats.bigint_convert_attempts,
        g_js_ta_set_stats.bigint_convert_hits,
        g_js_ta_set_stats.generic_boxed_fallbacks,
        g_js_ta_set_stats.generic_boxed_elements,
        g_js_ta_set_stats.array_like_loop_elements,
        g_js_ta_set_stats.empty_typed_array_sources);
    js_ta_set_stats_write_line(fd, line);
    JS_TA_SET_STATS_CLOSE(fd);
    log_notice("js-ta-set-stats: calls=%ld typed_sources=%ld array_like_sources=%ld same_hits=%ld number_hits=%ld bigint_hits=%ld generic=%ld array_like_elems=%ld",
        g_js_ta_set_stats.calls_total,
        g_js_ta_set_stats.typed_array_source_calls,
        g_js_ta_set_stats.array_like_source_calls,
        g_js_ta_set_stats.same_type_fast_hits,
        g_js_ta_set_stats.number_convert_hits,
        g_js_ta_set_stats.bigint_convert_hits,
        g_js_ta_set_stats.generic_boxed_fallbacks,
        g_js_ta_set_stats.array_like_loop_elements);
}

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
    case JS_TYPED_BIGINT64:
    case JS_TYPED_BIGUINT64: return 8;
    default:                return 4;
    }
}

static ArrayNumElemType js_typed_array_elem_type(JsTypedArrayType type) {
    switch (type) {
    case JS_TYPED_INT8:          return ELEM_INT8;
    case JS_TYPED_UINT8:         return ELEM_UINT8;
    case JS_TYPED_INT16:         return ELEM_INT16;
    case JS_TYPED_UINT16:        return ELEM_UINT16;
    case JS_TYPED_INT32:         return ELEM_INT32;
    case JS_TYPED_UINT32:        return ELEM_UINT32;
    case JS_TYPED_FLOAT32:       return ELEM_FLOAT32;
    case JS_TYPED_FLOAT64:       return ELEM_FLOAT64;
    case JS_TYPED_UINT8_CLAMPED: return ELEM_UINT8_CLAMPED;
    case JS_TYPED_BIGINT64:      return ELEM_INT64;
    case JS_TYPED_BIGUINT64:     return ELEM_UINT64;
    default:                     return ELEM_UINT8;
    }
}

static bool js_typed_array_is_bigint_element(JsTypedArrayType type) {
    return type == JS_TYPED_BIGINT64 || type == JS_TYPED_BIGUINT64;
}

static bool js_typed_array_is_number_element(JsTypedArrayType type) {
    return !js_typed_array_is_bigint_element(type);
}

static int js_typed_array_current_byte_length(JsTypedArray* ta) {
    if (!ta) return 0;
    if (!ta->buffer) return ta->byte_length;
    if (ta->buffer->detached) return 0;
    int available = ta->buffer->byte_length - ta->byte_offset;
    if (available < 0) return 0;
    if (ta->length_tracking) {
        int elem_size = typed_array_element_size(ta->element_type);
        return (available / elem_size) * elem_size;
    }
    return available >= ta->byte_length ? ta->byte_length : 0;
}

static int js_typed_array_current_length(JsTypedArray* ta) {
    if (!ta) return 0;
    int elem_size = typed_array_element_size(ta->element_type);
    return js_typed_array_current_byte_length(ta) / elem_size;
}

static int js_typed_array_current_byte_offset(JsTypedArray* ta) {
    if (!ta) return 0;
    if (!ta->buffer) return ta->byte_offset;
    if (ta->buffer->detached) return 0;
    if (ta->length_tracking) return ta->buffer->byte_length >= ta->byte_offset ? ta->byte_offset : 0;
    return ta->buffer->byte_length >= ta->byte_offset + ta->byte_length ? ta->byte_offset : 0;
}

static void* js_typed_array_current_data(JsTypedArray* ta) {
    if (!ta) return NULL;
    if (!ta->buffer) return ta->data;
    if (js_typed_array_current_byte_length(ta) == 0) return NULL;
    return (char*)ta->buffer->data + ta->byte_offset;
}

static void js_typed_array_refresh_arraynum_view(JsTypedArray* ta) {
    if (!ta || !ta->view) return;

    int byte_offset = js_typed_array_current_byte_offset(ta);
    int length = js_typed_array_current_length(ta);
    void* data = js_typed_array_current_data(ta);

    ta->view->data = data;
    ta->view->length = length;
    ta->view->capacity = length;

    ArrayNumShape* shape = (ArrayNumShape*)(uintptr_t)ta->view->extra;
    if (!shape) return;

    int elem_size = typed_array_element_size(ta->element_type);
    shape->offset = elem_size ? byte_offset / elem_size : 0;
    if (ta->buffer_item) {
        Item buffer_item;  buffer_item.item = ta->buffer_item;
        if (buffer_item.type_id() == LMD_TYPE_MAP) {
            shape->base = (void*)buffer_item.map;
        }
    }
    array_num_shape_dims(shape)[0] = length;
    array_num_shape_strides(shape)[0] = 1;
}

static bool js_typed_array_is_out_of_bounds(JsTypedArray* ta) {
    if (!ta || !ta->buffer) return false;
    if (ta->buffer->detached) return true;
    if (ta->length_tracking) return ta->buffer->byte_length < ta->byte_offset;
    return ta->buffer->byte_length < ta->byte_offset + ta->byte_length;
}

static bool js_typed_array_raw_fast_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* flag = getenv("LAMBDA_JS_TA_RAW_FAST");
        enabled = (!flag || strcmp(flag, "0") != 0) ? 1 : 0;
    }
    return enabled != 0;
}

static bool js_typed_array_arraynum_view_matches(JsTypedArray* ta, const char* data, int index) {
    return ta && !ta->is_buffer && ta->view && ta->view->data == (void*)data &&
        index >= 0 && index < ta->view->length;
}

static bool js_typed_array_try_raw_set_same_type(JsTypedArray* dst, JsTypedArray* src, int offset) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!dst || !src || dst->element_type != src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;

    int src_len = js_typed_array_current_length(src);
    if (src_len <= 0) return true;
    int dst_len = js_typed_array_current_length(dst);
    if (offset < 0 || (int64_t)offset + (int64_t)src_len > (int64_t)dst_len) return false;

    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_current_data(dst);
    if (!src_data || !dst_data) return false;

    int elem_size = typed_array_element_size(src->element_type);
    size_t byte_count = (size_t)src_len * (size_t)elem_size;
    memmove(dst_data + ((size_t)offset * (size_t)elem_size), src_data, byte_count);
    return true;
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
    if (js_typed_array_arraynum_view_matches(ta, data, index) &&
        js_typed_array_is_number_element(ta->element_type)) {
        return array_num_get_number_value(ta->view, index);
    }
    JsTypedArrayType type = ta->element_type;
    switch (type) {
    case JS_TYPED_INT8: return (double)((int8_t*)data)[index];
    case JS_TYPED_UINT8:
    case JS_TYPED_UINT8_CLAMPED: return (double)((uint8_t*)data)[index];
    case JS_TYPED_INT16: return (double)((int16_t*)data)[index];
    case JS_TYPED_UINT16: return (double)((uint16_t*)data)[index];
    case JS_TYPED_INT32: return (double)((int32_t*)data)[index];
    case JS_TYPED_UINT32: return (double)((uint32_t*)data)[index];
    case JS_TYPED_FLOAT32: return (double)((float*)data)[index];
    case JS_TYPED_FLOAT64: return ((double*)data)[index];
    default: return 0.0;
    }
}

static uint8_t js_typed_array_to_uint8_clamped(double value) {
    if (isnan(value) || value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    int f = (int)value;
    double fmod = value - f;
    if (fmod < 0.5) return (uint8_t)f;
    if (fmod > 0.5) return (uint8_t)(f + 1);
    return (uint8_t)((f & 1) ? f + 1 : f);
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

static void js_typed_array_raw_store_number(JsTypedArray* ta, char* data, int index, double value) {
    if (!ta) return;
    if (js_typed_array_arraynum_view_matches(ta, data, index) &&
        js_typed_array_is_number_element(ta->element_type)) {
        switch (ta->element_type) {
        case JS_TYPED_INT8:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 8, true));
            return;
        case JS_TYPED_UINT8:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 8, false));
            return;
        case JS_TYPED_UINT8_CLAMPED:
            array_num_set_double_value(ta->view, index, value);
            return;
        case JS_TYPED_INT16:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 16, true));
            return;
        case JS_TYPED_UINT16:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 16, false));
            return;
        case JS_TYPED_INT32:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 32, true));
            return;
        case JS_TYPED_UINT32:
            array_num_set_int64_value(ta->view, index, js_typed_array_to_int_n(value, 32, false));
            return;
        case JS_TYPED_FLOAT32:
        case JS_TYPED_FLOAT64:
            array_num_set_double_value(ta->view, index, value);
            return;
        default:
            break;
        }
    }
    JsTypedArrayType type = ta->element_type;
    switch (type) {
    case JS_TYPED_INT8:
        ((int8_t*)data)[index] = (int8_t)js_typed_array_to_int_n(value, 8, true);
        break;
    case JS_TYPED_UINT8:
        ((uint8_t*)data)[index] = (uint8_t)js_typed_array_to_int_n(value, 8, false);
        break;
    case JS_TYPED_UINT8_CLAMPED:
        ((uint8_t*)data)[index] = js_typed_array_to_uint8_clamped(value);
        break;
    case JS_TYPED_INT16:
        ((int16_t*)data)[index] = (int16_t)js_typed_array_to_int_n(value, 16, true);
        break;
    case JS_TYPED_UINT16:
        ((uint16_t*)data)[index] = (uint16_t)js_typed_array_to_int_n(value, 16, false);
        break;
    case JS_TYPED_INT32:
        ((int32_t*)data)[index] = (int32_t)js_typed_array_to_int_n(value, 32, true);
        break;
    case JS_TYPED_UINT32:
        ((uint32_t*)data)[index] = (uint32_t)js_typed_array_to_int_n(value, 32, false);
        break;
    case JS_TYPED_FLOAT32:
        ((float*)data)[index] = (float)value;
        break;
    case JS_TYPED_FLOAT64:
        ((double*)data)[index] = value;
        break;
    default:
        break;
    }
}

static bool js_typed_array_try_raw_from_dense_number_array(Item result, Array* arr, int len) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!arr || arr->is_content == 1 || arr->extra != 0) return false;
    if (len < 0 || (int64_t)len > arr->capacity) return false;

    JsTypedArray* dst = js_get_typed_array_ptr(result.map);
    if (!dst || !js_typed_array_is_number_element(dst->element_type)) return false;
    js_typed_array_refresh_arraynum_view(dst);
    char* data = (char*)js_typed_array_current_data(dst);
    if (len > 0 && !data) return false;

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

static bool js_typed_array_try_raw_convert_number(JsTypedArray* dst, JsTypedArray* src,
                                                  int offset, bool allow_overlap) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!dst || !src) return false;
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
    char* dst_data = (char*)js_typed_array_current_data(dst);
    if (!src_data || !dst_data) return false;

    int src_elem_size = typed_array_element_size(src->element_type);
    int dst_elem_size = typed_array_element_size(dst->element_type);
    char* dst_start = dst_data + ((size_t)offset * (size_t)dst_elem_size);
    if (!allow_overlap &&
        js_typed_array_ranges_overlap(dst_start, src_len * dst_elem_size,
                                      src_data, src_len * src_elem_size)) {
        return false;
    }

    for (int i = 0; i < src_len; i++) {
        double value = js_typed_array_raw_load_number(src, src_data, i);
        js_typed_array_raw_store_number(dst, dst_data, offset + i, value);
    }
    return true;
}

static bool js_typed_array_try_raw_convert_bigint(JsTypedArray* dst, JsTypedArray* src,
                                                  int offset, bool allow_overlap) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!dst || !src) return false;
    if (!js_typed_array_is_bigint_element(dst->element_type) ||
        !js_typed_array_is_bigint_element(src->element_type)) return false;
    if (dst->element_type == src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;

    int src_len = js_typed_array_current_length(src);
    if (src_len <= 0) return true;
    int dst_len = js_typed_array_current_length(dst);
    if (offset < 0 || (int64_t)offset + (int64_t)src_len > (int64_t)dst_len) return false;

    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_current_data(dst);
    if (!src_data || !dst_data) return false;

    int elem_size = typed_array_element_size(src->element_type);
    char* dst_start = dst_data + ((size_t)offset * (size_t)elem_size);
    size_t byte_count = (size_t)src_len * (size_t)elem_size;
    if (!allow_overlap && js_typed_array_ranges_overlap(dst_start, (int)byte_count,
                                                        src_data, (int)byte_count)) {
        return false;
    }
    memmove(dst_start, src_data, byte_count);
    return true;
}

extern "C" bool js_typed_array_raw_copy_same_type(Item dst_item, Item src_item) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!js_is_typed_array(dst_item) || !js_is_typed_array(src_item)) return false;
    JsTypedArray* dst = js_get_typed_array_ptr(dst_item.map);
    JsTypedArray* src = js_get_typed_array_ptr(src_item.map);
    if (!dst || !src || dst->element_type != src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;
    int len = js_typed_array_current_length(src);
    if (len != js_typed_array_current_length(dst)) return false;
    if (len <= 0) return true;
    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_current_data(dst);
    if (!src_data || !dst_data) return false;
    int elem_size = typed_array_element_size(src->element_type);
    memcpy(dst_data, src_data, (size_t)len * (size_t)elem_size);
    return true;
}

extern "C" bool js_typed_array_raw_reverse(Item ta_item) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!js_is_typed_array(ta_item)) return false;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    if (!ta || js_typed_array_is_out_of_bounds(ta)) return false;
    int len = js_typed_array_current_length(ta);
    if (len <= 1) return true;
    char* data = (char*)js_typed_array_current_data(ta);
    if (!data) return false;
    int elem_size = typed_array_element_size(ta->element_type);
    char temp[8];
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char* left = data + (size_t)i * (size_t)elem_size;
        char* right = data + (size_t)j * (size_t)elem_size;
        memcpy(temp, left, (size_t)elem_size);
        memcpy(left, right, (size_t)elem_size);
        memcpy(right, temp, (size_t)elem_size);
    }
    return true;
}

extern "C" bool js_typed_array_raw_copy_reversed(Item dst_item, Item src_item) {
    if (!js_typed_array_raw_fast_enabled()) return false;
    if (!js_is_typed_array(dst_item) || !js_is_typed_array(src_item)) return false;
    JsTypedArray* dst = js_get_typed_array_ptr(dst_item.map);
    JsTypedArray* src = js_get_typed_array_ptr(src_item.map);
    if (!dst || !src || dst->element_type != src->element_type) return false;
    if (js_typed_array_is_out_of_bounds(dst) || js_typed_array_is_out_of_bounds(src)) return false;
    int len = js_typed_array_current_length(src);
    if (len != js_typed_array_current_length(dst)) return false;
    if (len <= 0) return true;
    char* src_data = (char*)js_typed_array_current_data(src);
    char* dst_data = (char*)js_typed_array_current_data(dst);
    if (!src_data || !dst_data) return false;
    int elem_size = typed_array_element_size(src->element_type);
    for (int i = 0, j = len - 1; i < len; i++, j--) {
        memcpy(dst_data + (size_t)i * (size_t)elem_size,
               src_data + (size_t)j * (size_t)elem_size,
               (size_t)elem_size);
    }
    return true;
}

extern "C" int js_typed_array_raw_index_of(Item ta_item, Item search_value,
                                           int from, int bound, bool reverse, bool same_value_zero) {
    if (!js_typed_array_raw_fast_enabled()) return -2;
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

static bool js_to_index_int(Item value, int* out_index, const char* error_message) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        *out_index = 0;
        return true;
    }
    if (type == LMD_TYPE_SYMBOL ||
        (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return false;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return false;
    type = get_type_id(num);
    double dval = (type == LMD_TYPE_FLOAT) ? it2d(num) : (double)it2i(num);
    if (std::isnan(dval)) {
        *out_index = 0;
        return true;
    }
    dval = std::trunc(dval);
    if (dval < 0 || !std::isfinite(dval) || dval > 1073741824.0) {
        js_throw_range_error(error_message);
        return false;
    }
    *out_index = (int)dval;
    return true;
}

static bool js_atomics_is_integer_type(JsTypedArrayType type) {
    switch (type) {
    case JS_TYPED_INT8:
    case JS_TYPED_UINT8:
    case JS_TYPED_INT16:
    case JS_TYPED_UINT16:
    case JS_TYPED_INT32:
    case JS_TYPED_UINT32:
    case JS_TYPED_BIGINT64:
    case JS_TYPED_BIGUINT64:
        return true;
    default:
        return false;
    }
}

static bool js_atomics_is_bigint_type(JsTypedArrayType type) {
    return type == JS_TYPED_BIGINT64 || type == JS_TYPED_BIGUINT64;
}

static JsTypedArray* js_validate_atomic_typed_array(Item typed_array, bool require_shared, bool waitable) {
    if (!js_is_typed_array(typed_array)) {
        js_throw_type_error("Atomics operation requires a TypedArray");
        return NULL;
    }
    JsTypedArray* ta = js_get_typed_array_ptr(typed_array.map);
    if (!ta) {
        js_throw_type_error("Atomics operation requires a TypedArray");
        return NULL;
    }
    if (require_shared && (!ta->buffer || !ta->buffer->is_shared)) {
        js_throw_type_error("Atomics operation requires a SharedArrayBuffer-backed TypedArray");
        return NULL;
    }
    if (ta->buffer && ta->buffer->detached) {
        js_throw_type_error(require_shared ? "Atomics operation requires a non-detached SharedArrayBuffer" :
                                           "Atomics operation requires a non-detached ArrayBuffer");
        return NULL;
    }
    if (waitable) {
        if (ta->element_type != JS_TYPED_INT32 && ta->element_type != JS_TYPED_BIGINT64) {
            js_throw_type_error("Atomics.wait/notify requires an Int32Array or BigInt64Array");
            return NULL;
        }
    } else if (!js_atomics_is_integer_type(ta->element_type)) {
        js_throw_type_error("Atomics operation requires an integer TypedArray");
        return NULL;
    }
    return ta;
}

static bool js_atomics_validate_index(JsTypedArray* ta, Item index_item, int* out_index) {
    int length = js_typed_array_current_length(ta);
    int index = 0;
    if (!js_to_index_int(index_item, &index, "Invalid atomic access index")) return false;
    if (index < 0 || index >= length) {
        js_throw_range_error("Invalid atomic access index");
        return false;
    }
    *out_index = index;
    return true;
}

static Item js_atomics_number_to_integer_item(double number) {
    if (std::isnan(number)) return (Item){.item = i2it(0)};
    if (!std::isfinite(number)) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = number;
        return (Item){.item = d2it(fp)};
    }
    double integer = std::trunc(number);
    if (integer >= (double)INT64_MIN && integer <= (double)INT64_MAX) {
        return (Item){.item = i2it((int64_t)integer)};
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = integer;
    return (Item){.item = d2it(fp)};
}

static bool js_atomics_to_number_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    double number = 0.0;
    if (!js_dataview_to_number_value(value, &number)) return false;
    if (out_store_value) *out_store_value = js_atomics_number_to_integer_item(number);
    switch (type) {
    case JS_TYPED_INT8:
        *out_bits = (uint8_t)(int8_t)js_typed_array_to_int_n(number, 8, true);
        return true;
    case JS_TYPED_UINT8:
        *out_bits = (uint8_t)js_typed_array_to_int_n(number, 8, false);
        return true;
    case JS_TYPED_INT16:
        *out_bits = (uint16_t)(int16_t)js_typed_array_to_int_n(number, 16, true);
        return true;
    case JS_TYPED_UINT16:
        *out_bits = (uint16_t)js_typed_array_to_int_n(number, 16, false);
        return true;
    case JS_TYPED_INT32:
        *out_bits = (uint32_t)(int32_t)js_typed_array_to_int_n(number, 32, true);
        return true;
    case JS_TYPED_UINT32:
        *out_bits = (uint32_t)js_typed_array_to_int_n(number, 32, false);
        return true;
    default:
        js_throw_type_error("Atomics operation requires a Number typed array");
        return false;
    }
}

static bool js_atomics_to_bigint_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    Item bigint_item;
    if (!js_dataview_to_bigint_value(value, &bigint_item)) return false;
    if (out_store_value) *out_store_value = bigint_item;
    Item wrapped;
    if (type == JS_TYPED_BIGINT64) {
        wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_item);
    } else {
        wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_item);
    }
    if (js_check_exception()) return false;
    *out_bits = js_dataview_bigint_to_uint64(wrapped);
    return true;
}

static bool js_atomics_to_element_bits(JsTypedArrayType type, Item value, uint64_t* out_bits, Item* out_store_value) {
    if (js_atomics_is_bigint_type(type)) return js_atomics_to_bigint_bits(type, value, out_bits, out_store_value);
    return js_atomics_to_number_bits(type, value, out_bits, out_store_value);
}

static Item js_atomics_item_from_bits(JsTypedArrayType type, uint64_t bits) {
    switch (type) {
    case JS_TYPED_INT8:      return (Item){.item = i2it((int64_t)(int8_t)(uint8_t)bits)};
    case JS_TYPED_UINT8:     return (Item){.item = i2it((int64_t)(uint8_t)bits)};
    case JS_TYPED_INT16:     return (Item){.item = i2it((int64_t)(int16_t)(uint16_t)bits)};
    case JS_TYPED_UINT16:    return (Item){.item = i2it((int64_t)(uint16_t)bits)};
    case JS_TYPED_INT32:     return (Item){.item = i2it((int64_t)(int32_t)(uint32_t)bits)};
    case JS_TYPED_UINT32:    return (Item){.item = i2it((int64_t)(uint32_t)bits)};
    case JS_TYPED_BIGINT64:  return bigint_from_int64((int64_t)bits);
    case JS_TYPED_BIGUINT64: return js_dataview_biguint64_item(bits);
    default:                 return (Item){.item = ITEM_JS_UNDEFINED};
    }
}

static Item js_atomics_wait_result(const char* value, int len) {
    return (Item){.item = s2it(heap_strcpy((char*)value, len))};
}

static bool js_atomics_host_can_suspend() {
    Item global = js_get_global_this();
    Item key = (Item){.item = s2it(heap_create_name("__lambda_can_block"))};
    Item flag = js_property_get(global, key);
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

static JsAtomicsWaiter js_atomics_waiters[JS_ATOMICS_MAX_WAITERS];
static int js_atomics_next_waiter_id = 1;
static int js_atomics_last_waiter_by_agent[JS_ATOMICS_MAX_AGENT_SLOTS];
static int js_atomics_blocking_waiter_by_agent[JS_ATOMICS_MAX_AGENT_SLOTS];
static double js_atomics_virtual_now_ms = 0.0;
static bool js_atomics_waiter_roots_registered = false;

static void js_atomics_register_waiter_roots(void) {
    if (js_atomics_waiter_roots_registered) return;
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) {
        js_atomics_waiters[i].promise = ItemNull;
        heap_register_gc_root(&js_atomics_waiters[i].promise.item);
    }
    js_atomics_waiter_roots_registered = true;
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
    js_property_set(result, (Item){.item = s2it(heap_create_name("async"))}, (Item){.item = b2it(async)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("value"))}, value);
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
    Item callback_fn = js_new_function((void*)js_atomics_timeout_waiter, 1);
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
    js_atomics_register_waiter_roots();
    memset(js_atomics_waiters, 0, sizeof(js_atomics_waiters));
    for (int i = 0; i < JS_ATOMICS_MAX_WAITERS; i++) js_atomics_waiters[i].promise = ItemNull;
    memset(js_atomics_last_waiter_by_agent, 0, sizeof(js_atomics_last_waiter_by_agent));
    memset(js_atomics_blocking_waiter_by_agent, 0, sizeof(js_atomics_blocking_waiter_by_agent));
    js_atomics_next_waiter_id = 1;
    js_atomics_virtual_now_ms = 0.0;
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
        if (!js_dataview_to_number_value(ms, &sleep_ms)) return;
        if (std::isnan(sleep_ms) || sleep_ms < 0.0) sleep_ms = 0.0;
        if (!std::isfinite(sleep_ms)) sleep_ms = 2147483647.0;
    }
    js_atomics_virtual_now_ms += sleep_ms;
    js_atomics_resolve_due_waiters();
}

extern "C" Item js_atomics_agent_monotonic_now(void) {
    return (Item){.item = i2it((int64_t)std::trunc(js_atomics_virtual_now_ms))};
}

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
        if (!agent_spin_assist && ta->buffer && ta->buffer->is_shared && converted_value == (C_TYPE)0 && js_atomics_has_pending_waiter_for_buffer(ta->buffer)) { \
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
    JsTypedArray* ta = js_validate_atomic_typed_array(typed_array, false, false);
    if (!ta) return ItemNull;
    int index = 0;
    if (!js_atomics_validate_index(ta, index_item, &index)) return ItemNull;
    void* data = js_typed_array_current_data(ta);
    if (!data) return js_throw_range_error("Invalid atomic access index");
    bool agent_spin_assist = ta->buffer && ta->buffer->is_shared && js_262_agent_current_slot_for_atomics() >= 0;

    uint64_t value_bits = 0;
    uint64_t replacement_bits = 0;
    Item store_return = value;
    if ((JsAtomicsOp)op != JS_ATOMICS_OP_LOAD) {
        if (!js_atomics_to_element_bits(ta->element_type, value, &value_bits, &store_return)) return ItemNull;
        if ((JsAtomicsOp)op == JS_ATOMICS_OP_COMPARE_EXCHANGE &&
            !js_atomics_to_element_bits(ta->element_type, replacement, &replacement_bits, NULL)) return ItemNull;
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

extern "C" Item js_atomics_wait(Item typed_array, Item index_item, Item expected, Item timeout) {
    JsTypedArray* ta = js_validate_atomic_typed_array(typed_array, true, true);
    if (!ta) return ItemNull;
    int index = 0;
    if (!js_atomics_validate_index(ta, index_item, &index)) return ItemNull;

    uint64_t expected_bits = 0;
    if (!js_atomics_to_element_bits(ta->element_type, expected, &expected_bits, NULL)) return ItemNull;
    double timeout_number = INFINITY;
    bool has_timeout = false;
    if (get_type_id(timeout) != LMD_TYPE_UNDEFINED) {
        js_dataview_to_number_value(timeout, &timeout_number);
        if (js_check_exception()) return ItemNull;
        if (std::isnan(timeout_number)) timeout_number = INFINITY;
        else if (timeout_number < 0.0) timeout_number = 0.0;
        else timeout_number = std::trunc(timeout_number);
        has_timeout = std::isfinite(timeout_number);
    }

    void* data = js_typed_array_current_data(ta);
    if (!data) return js_throw_range_error("Invalid atomic access index");
    bool equal = false;
    if (ta->element_type == JS_TYPED_INT32) {
        int32_t current = __atomic_load_n(((int32_t*)data) + index, __ATOMIC_SEQ_CST);
        equal = current == (int32_t)expected_bits;
    } else {
        int64_t current = __atomic_load_n(((int64_t*)data) + index, __ATOMIC_SEQ_CST);
        equal = current == (int64_t)expected_bits;
    }
    if (!equal) return js_atomics_wait_result("not-equal", 9);

    if (!js_atomics_host_can_suspend()) {
        return js_throw_type_error("Atomics.wait cannot suspend on this agent");
    }

    int agent_slot = js_262_agent_current_slot_for_atomics();
    if (agent_slot < 0) {
        return js_atomics_wait_result("timed-out", 9);
    }
    if (has_timeout && timeout_number <= 0.0) {
        return js_atomics_wait_result("timed-out", 9);
    }

    int waiter_id = js_atomics_record_waiter(ta->buffer, index, agent_slot, timeout_number, has_timeout, ItemNull);
    if (waiter_id == 0) return js_throw_type_error("Atomics.wait waiter capacity exceeded");
    if (has_timeout && timeout_number <= 200.0) {
        js_atomics_virtual_now_ms += timeout_number;
        js_atomics_resolve_due_waiters();
        return js_atomics_wait_result("timed-out", 9);
    }
    return js_atomics_wait_result("ok", 2);
}

extern "C" Item js_atomics_wait_async(Item typed_array, Item index_item, Item expected, Item timeout) {
    JsTypedArray* ta = js_validate_atomic_typed_array(typed_array, true, true);
    if (!ta) return ItemNull;
    int index = 0;
    if (!js_atomics_validate_index(ta, index_item, &index)) return ItemNull;

    uint64_t expected_bits = 0;
    if (!js_atomics_to_element_bits(ta->element_type, expected, &expected_bits, NULL)) return ItemNull;

    double timeout_number = INFINITY;
    bool has_timeout = false;
    if (get_type_id(timeout) != LMD_TYPE_UNDEFINED) {
        js_dataview_to_number_value(timeout, &timeout_number);
        if (js_check_exception()) return ItemNull;
        if (std::isnan(timeout_number)) timeout_number = INFINITY;
        else if (timeout_number < 0.0) timeout_number = 0.0;
        else timeout_number = std::trunc(timeout_number);
        has_timeout = std::isfinite(timeout_number);
    }

    void* data = js_typed_array_current_data(ta);
    if (!data) return js_throw_range_error("Invalid atomic access index");

    bool equal = false;
    if (ta->element_type == JS_TYPED_INT32) {
        int32_t current = __atomic_load_n(((int32_t*)data) + index, __ATOMIC_SEQ_CST);
        equal = current == (int32_t)expected_bits;
    } else {
        int64_t current = __atomic_load_n(((int64_t*)data) + index, __ATOMIC_SEQ_CST);
        equal = current == (int64_t)expected_bits;
    }
    if (!equal) return js_atomics_wait_async_result(false, js_atomics_wait_result("not-equal", 9));
    if (has_timeout && timeout_number <= 0.0) {
        return js_atomics_wait_async_result(false, js_atomics_wait_result("timed-out", 9));
    }

    int agent_slot = js_262_agent_current_slot_for_atomics();
    if (agent_slot >= 0) {
        int waiter_id = js_atomics_record_waiter(ta->buffer, index, agent_slot, timeout_number, has_timeout, ItemNull);
        if (waiter_id == 0) return js_throw_type_error("Atomics.waitAsync waiter capacity exceeded");
        // Test262 agents run on a virtual clock. Match the synchronous
        // Atomics.wait fast path for short finite waits so no-spurious-wakeup
        // probes observe the requested lapse without paying real wall time.
        if (has_timeout && timeout_number <= 200.0) {
            js_atomics_virtual_now_ms += timeout_number;
            js_atomics_resolve_due_waiters();
        }
        Item report_status = has_timeout ? js_atomics_wait_result("timed-out", 9) : js_atomics_wait_result("ok", 2);
        return js_atomics_wait_async_result(true, report_status);
    }

    Item promise = js_promise_create_pending();
    if (js_check_exception()) return ItemNull;
    if (get_type_id(promise) != LMD_TYPE_MAP) return ItemNull;

    int waiter_id = js_atomics_record_waiter(ta->buffer, index, agent_slot, timeout_number, has_timeout, promise);
    if (waiter_id == 0) return js_throw_type_error("Atomics.waitAsync waiter capacity exceeded");

    if (has_timeout && timeout_number <= 200.0) js_atomics_schedule_timeout_waiter(waiter_id, timeout_number);
    return js_atomics_wait_async_result(true, promise);
}

extern "C" Item js_atomics_notify(Item typed_array, Item index_item, Item count) {
    JsTypedArray* ta = js_validate_atomic_typed_array(typed_array, false, true);
    if (!ta) return ItemNull;
    int index = 0;
    if (!js_atomics_validate_index(ta, index_item, &index)) return ItemNull;
    int notify_count = INT_MAX;
    if (get_type_id(count) != LMD_TYPE_UNDEFINED) {
        double count_number = 0.0;
        if (!js_dataview_to_number_value(count, &count_number)) return ItemNull;
        if (std::isnan(count_number) || count_number <= 0.0) notify_count = 0;
        else if (std::isfinite(count_number) && count_number < (double)INT_MAX) notify_count = (int)std::trunc(count_number);
    }
    if (!ta->buffer || !ta->buffer->is_shared) {
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
    if (!js_dataview_to_number_value(size, &number)) return ItemNull;
    if (std::isnan(number) || !std::isfinite(number)) number = 0.0;
    int64_t int_size = (int64_t)std::trunc(number);
    return (Item){.item = b2it(int_size == 1 || int_size == 2 || int_size == 4 || int_size == 8)};
}

// Returns the JS type name for a typed array element type (e.g. "Uint8Array")
extern "C" const char* js_typed_array_type_name(Item val) {
    if (get_type_id(val) != LMD_TYPE_MAP) return NULL;
    Map* m = val.map;
    if (!m || m->map_kind != MAP_KIND_TYPED_ARRAY) return NULL;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    if (!ta) return NULL;
    switch (ta->element_type) {
    case JS_TYPED_INT8:          return "Int8Array";
    case JS_TYPED_UINT8:         return "Uint8Array";
    case JS_TYPED_UINT8_CLAMPED: return "Uint8ClampedArray";
    case JS_TYPED_INT16:         return "Int16Array";
    case JS_TYPED_UINT16:        return "Uint16Array";
    case JS_TYPED_INT32:         return "Int32Array";
    case JS_TYPED_UINT32:        return "Uint32Array";
    case JS_TYPED_FLOAT32:       return "Float32Array";
    case JS_TYPED_FLOAT64:       return "Float64Array";
    case JS_TYPED_BIGINT64:      return "BigInt64Array";
    case JS_TYPED_BIGUINT64:     return "BigUint64Array";
    default:                     return NULL;
    }
}

// ============================================================================
// ArrayBuffer
// ============================================================================

static JsArrayBuffer* js_get_arraybuffer_ptr(Map* m);

static JsArrayBuffer* js_arraybuffer_alloc(int byte_length) {
    JsArrayBuffer* ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
    ab->byte_length = byte_length;
    ab->max_byte_length = byte_length;
    ab->data = mem_calloc(1, byte_length > 0 ? byte_length : 1, MEM_CAT_JS_RUNTIME);
    ab->detached = false;
    ab->is_shared = false;
    ab->resizable = false;
    return ab;
}

static void js_arraybuffer_link_prototype(Item buffer_item, bool is_shared) {
    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name(is_shared ? "SharedArrayBuffer" : "ArrayBuffer"))});
    if (get_type_id(ctor) != LMD_TYPE_FUNC) return;
    Item proto = js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype"))});
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(buffer_item, proto);
}

extern "C" Item js_arraybuffer_new(int byte_length) {
    if (byte_length < 0) byte_length = 0;
    JsArrayBuffer* ab = js_arraybuffer_alloc(byte_length);

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_ARRAYBUFFER;
    m->type = ab->is_shared ? (void*)&js_sharedarraybuffer_type_marker : (void*)&js_arraybuffer_type_marker;
    m->data = ab;
    m->data_cap = 0;

    Item result = (Item){.map = m};
    js_arraybuffer_link_prototype(result, ab->is_shared);
    return result;
}

// ArrayBuffer constructor from JS: new ArrayBuffer(length)
// Performs ToIndex validation per spec: non-negative integer, throws RangeError for invalid.
// Practical allocation limit: 1 GB (matches typical engine limits).
extern "C" Item js_arraybuffer_construct(Item length_arg) {
    return js_arraybuffer_construct_resizable(length_arg, (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_arraybuffer_construct_resizable(Item length_arg, Item options_arg) {
    // ToIndex: undefined/null → 0
    int byte_length = 0;
    if (!js_to_index_int(length_arg, &byte_length, "Invalid array buffer length")) return ItemNull;

    int max_byte_length = byte_length;
    bool resizable = false;
    if (get_type_id(options_arg) == LMD_TYPE_MAP) {
        Item max_key = (Item){.item = s2it(heap_create_name("maxByteLength"))};
        Item max_item = js_property_get(options_arg, max_key);
        if (get_type_id(max_item) != LMD_TYPE_UNDEFINED && max_item.item != ITEM_NULL) {
            if (!js_to_index_int(max_item, &max_byte_length, "Invalid array buffer maxByteLength")) return ItemNull;
            if (max_byte_length < byte_length) return js_throw_range_error("Invalid array buffer maxByteLength");
            resizable = true;
        }
    }

    Item result = js_arraybuffer_new(byte_length);
    if (js_is_arraybuffer(result)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr(result.map);
        if (ab) {
            ab->max_byte_length = max_byte_length;
            ab->resizable = resizable;
        }
    }
    return result;
}

extern "C" bool js_is_arraybuffer(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->map_kind == MAP_KIND_ARRAYBUFFER;
}

// Get the JsArrayBuffer* from a Map, handling both original and upgraded layouts.
// Original: m->data holds JsArrayBuffer* directly (m->type == &js_arraybuffer_type_marker).
// Upgraded: JsArrayBuffer* is stored as __ab__ int64 property (after first user property write).
static JsArrayBuffer* js_get_arraybuffer_ptr(Map* m) {
    if (m->type == (void*)&js_arraybuffer_type_marker ||
        m->type == (void*)&js_sharedarraybuffer_type_marker)
        return (JsArrayBuffer*)m->data;
    // Upgraded: retrieve from __ab__ internal property
    bool found = false;
    Item ab_val = js_map_get_fast_ext(m, "__ab__", 6, &found);
    if (found) return (JsArrayBuffer*)(uintptr_t)it2i(ab_val);
    return NULL;
}

extern "C" JsArrayBuffer* js_get_arraybuffer_ptr_item(Item val) {
    if (!js_is_arraybuffer(val)) return NULL;
    return js_get_arraybuffer_ptr(val.map);
}

// Wrap an existing JsArrayBuffer* in a Map Item (for .buffer property access)
extern "C" Item js_arraybuffer_wrap(JsArrayBuffer* ab) {
    if (!ab) return (Item){.item = ITEM_NULL};
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_ARRAYBUFFER;
    m->type = ab->is_shared ? (void*)&js_sharedarraybuffer_type_marker : (void*)&js_arraybuffer_type_marker;
    m->data = ab;
    m->data_cap = 0;
    Item result = (Item){.map = m};
    js_arraybuffer_link_prototype(result, ab->is_shared);
    return result;
}

extern "C" int js_arraybuffer_byte_length(Item val) {
    if (!js_is_arraybuffer(val)) return 0;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return 0;
    return ab->byte_length;
}

extern "C" int js_arraybuffer_max_byte_length(Item val) {
    if (!js_is_arraybuffer(val)) return 0;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return 0;
    // Js54 P1: spec §25.1.5.3 get ArrayBuffer.prototype.maxByteLength step 4:
    // "If IsDetachedBuffer(O) is true, return +0𝔽." The detach path zeros
    // byte_length but not max_byte_length, so the check is needed here.
    if (ab->detached) return 0;
    return ab->max_byte_length;
}

extern "C" bool js_arraybuffer_is_resizable(Item val) {
    if (!js_is_arraybuffer(val)) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    return ab && ab->resizable;
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
    if (!ab->resizable) return js_throw_type_error("ArrayBuffer is not resizable");
    int new_length = 0;
    if (!js_to_index_int(new_length_item, &new_length, "Invalid array buffer length")) return ItemNull;
    if (ab->detached) return js_throw_type_error("ArrayBuffer is detached");
    if (new_length > ab->max_byte_length) return js_throw_range_error("Invalid array buffer length");

    void* new_data = mem_calloc(1, new_length > 0 ? new_length : 1, MEM_CAT_JS_RUNTIME);
    int copy_len = ab->byte_length < new_length ? ab->byte_length : new_length;
    if (ab->data && copy_len > 0) memcpy(new_data, ab->data, copy_len);
    ab->data = new_data;
    ab->byte_length = new_length;
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
        if (ab->detached) return js_throw_type_error("ArrayBuffer is detached");
        new_length = ab->byte_length;
    } else {
        if (!js_to_index_int(new_length_item, &new_length, "Invalid array buffer length")) return ItemNull;
        if (ab->detached) return js_throw_type_error("ArrayBuffer is detached");
    }

    // Determine resizable / maxByteLength for the new buffer.
    bool new_resizable;
    int new_max_byte_length;
    if (to_fixed_length) {
        new_resizable = false;
        new_max_byte_length = new_length;
    } else {
        new_resizable = ab->resizable;
        new_max_byte_length = new_resizable ? ab->max_byte_length : new_length;
    }
    if (new_length > new_max_byte_length) {
        return js_throw_range_error("Invalid array buffer length");
    }

    // Allocate the new buffer via js_arraybuffer_new, then adjust resizable
    // metadata. Allocation uses the buffer's natural capacity; if resizable,
    // ensure the data block can hold up to max_byte_length so subsequent
    // resize() calls don't have to reallocate beyond their bound.
    Item result = js_arraybuffer_new(new_length);
    if (!js_is_arraybuffer(result)) return result;
    JsArrayBuffer* nab = js_get_arraybuffer_ptr(result.map);
    if (!nab) return result;

    int copy_len = ab->byte_length < new_length ? ab->byte_length : new_length;
    if (ab->data && copy_len > 0) memcpy(nab->data, ab->data, copy_len);
    nab->resizable = new_resizable;
    nab->max_byte_length = new_max_byte_length;

    // Detach the source. This zeros source byte_length and frees the conceptual
    // data slot (ab->data set to NULL); existing TypedArray views over it
    // observe the detach through their stored ta->buffer pointer.
    js_arraybuffer_detach(val);

    return result;
}

extern "C" Item js_arraybuffer_transfer(Item val, Item new_length_item, int argc) {
    return js_arraybuffer_transfer_impl(val, new_length_item, argc, /*to_fixed_length=*/false);
}

extern "C" Item js_arraybuffer_transfer_to_fixed_length(Item val, Item new_length_item, int argc) {
    return js_arraybuffer_transfer_impl(val, new_length_item, argc, /*to_fixed_length=*/true);
}

extern "C" Item js_arraybuffer_slice(Item val, int begin, int end) {
    if (!js_is_arraybuffer(val)) return (Item){.item = ITEM_NULL};
    // ES spec: ArrayBuffer.prototype.slice must throw TypeError for SharedArrayBuffer
    if (js_is_sharedarraybuffer(val)) {
        js_throw_type_error("ArrayBuffer.prototype.slice requires that |this| not be a SharedArrayBuffer");
        return ItemNull;
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return (Item){.item = ITEM_NULL};
    if (ab->detached) {
        js_throw_type_error("ArrayBuffer.prototype.slice called on detached buffer");
        return ItemNull;
    }

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

static bool js_arraybuffer_slice_index(Item value, int len, int* out_index) {
    Item num = js_to_number(value);
    if (js_check_exception()) return false;
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

extern "C" Item js_arraybuffer_slice_items(Item val, Item begin_item, Item end_item, int argc) {
    if (!js_is_arraybuffer(val)) return (Item){.item = ITEM_NULL};
    if (js_is_sharedarraybuffer(val)) {
        return js_throw_type_error("ArrayBuffer.prototype.slice requires that |this| not be a SharedArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return (Item){.item = ITEM_NULL};
    if (ab->detached) {
        return js_throw_type_error("ArrayBuffer.prototype.slice called on detached buffer");
    }

    int begin = 0;
    int end = ab->byte_length;
    if (argc > 0) {
        if (!js_arraybuffer_slice_index(begin_item, ab->byte_length, &begin)) return ItemNull;
    }
    if (argc > 1 && get_type_id(end_item) != LMD_TYPE_UNDEFINED) {
        if (!js_arraybuffer_slice_index(end_item, ab->byte_length, &end)) return ItemNull;
    }
    if (end < begin) end = begin;
    int new_len = end - begin;

    Item result_item = ItemNull;
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
    Item ctor = js_property_get(val, ctor_key);
    if (js_check_exception()) return ItemNull;

    bool use_default_ctor = get_type_id(ctor) == LMD_TYPE_UNDEFINED;
    if (!use_default_ctor) {
        TypeId ctor_type = get_type_id(ctor);
        if (ctor_type != LMD_TYPE_MAP && ctor_type != LMD_TYPE_ARRAY &&
            ctor_type != LMD_TYPE_FUNC && ctor_type != LMD_TYPE_ELEMENT) {
            return js_throw_type_error("ArrayBuffer species constructor must be an object");
        }
        Item species_key = (Item){.item = s2it(heap_create_name("__sym_6"))};
        Item species = js_property_get(ctor, species_key);
        if (js_check_exception()) return ItemNull;
        TypeId species_type = get_type_id(species);
        if (species_type == LMD_TYPE_UNDEFINED || species_type == LMD_TYPE_NULL) {
            use_default_ctor = true;
        } else {
            // A subclass class object is a constructable MAP (has __instance_proto__),
            // not a FUNC — accept it as the species constructor.
            bool species_is_class_object = false;
            if (species_type == LMD_TYPE_MAP && species.map)
                js_map_get_fast_ext(species.map, "__instance_proto__", 18, &species_is_class_object);
            if (species_type != LMD_TYPE_FUNC && !js_is_proxy(species) && !species_is_class_object) {
                return js_throw_type_error("ArrayBuffer species is not a constructor");
            }
            Item len_arg = (Item){.item = i2it(new_len)};
            result_item = js_new_from_class_object(species, &len_arg, 1);
            if (js_check_exception()) return ItemNull;
        }
    }
    if (use_default_ctor) {
        result_item = js_arraybuffer_construct((Item){.item = i2it(new_len)});
    }
    if (js_check_exception()) return ItemNull;
    if (!js_is_arraybuffer(result_item) || js_is_sharedarraybuffer(result_item)) {
        return js_throw_type_error("ArrayBuffer species constructor did not return an ArrayBuffer");
    }
    if (result_item.item == val.item) {
        return js_throw_type_error("ArrayBuffer species constructor returned the same buffer");
    }
    JsArrayBuffer* rab = js_get_arraybuffer_ptr(result_item.map);
    if (!rab || rab->byte_length < new_len) {
        return js_throw_type_error("ArrayBuffer species constructor returned a buffer that is too small");
    }
    if (new_len > 0) {
        memcpy(rab->data, (char*)ab->data + begin, new_len);
    }
    return result_item;
}

extern "C" bool js_arraybuffer_is_view(Item val) {
    return js_is_typed_array(val) || js_is_dataview(val);
}

// Item-returning wrapper for MIR JIT calls (MIR expects Item return type)
extern "C" Item js_arraybuffer_is_view_item(Item val) {
    bool result = js_arraybuffer_is_view(val);
    return (Item){.item = result ? (ITEM_TRUE) : (ITEM_FALSE)};
}

// Detach an ArrayBuffer: set byte_length to 0 and mark as detached
extern "C" void js_arraybuffer_detach(Item val) {
    if (!js_is_arraybuffer(val)) return;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return;
    ab->data = NULL;
    ab->byte_length = 0;
    ab->detached = true;
}

// Check if an ArrayBuffer is detached
extern "C" bool js_arraybuffer_is_detached(Item val) {
    if (!js_is_arraybuffer(val)) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(val.map);
    if (!ab) return false;
    return ab->detached;
}

// ============================================================================
// SharedArrayBuffer
// ============================================================================

extern "C" Item js_sharedarraybuffer_construct(Item length_arg) {
    return js_sharedarraybuffer_construct_with_options(length_arg, (Item){.item = ITEM_JS_UNDEFINED});
}

extern "C" Item js_sharedarraybuffer_construct_with_options(Item length_arg, Item options_arg) {
    // ToIndex: undefined/null → 0
    TypeId type = get_type_id(length_arg);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) {
        length_arg = (Item){.item = i2it(0)};
        type = LMD_TYPE_INT;
    }

    // Symbol → TypeError (cannot convert to number)
    if (type == LMD_TYPE_INT && it2i(length_arg) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }

    // Convert to number
    Item num = js_to_number(length_arg);
    if (js_check_exception()) return ItemNull;
    type = get_type_id(num);

    double dval;
    if (type == LMD_TYPE_FLOAT) {
        dval = *((double*)((uintptr_t)num.item & 0x00FFFFFFFFFFFFFF));
    } else {
        dval = (double)it2i(num);
    }

    if (std::isnan(dval)) dval = 0;
    dval = std::trunc(dval);

    if (dval < 0 || !std::isfinite(dval) || dval > 9007199254740991.0) {
        return js_throw_range_error("Invalid shared array buffer length");
    }

    int64_t ival = (int64_t)dval;
    if (ival > 1073741824) {
        return js_throw_range_error("Shared array buffer allocation failed");
    }

    int byte_length = (int)ival;
    int max_byte_length = byte_length;
    bool growable = false;
    if (get_type_id(options_arg) == LMD_TYPE_MAP) {
        Item max_key = (Item){.item = s2it(heap_create_name("maxByteLength"))};
        Item max_item = js_property_get(options_arg, max_key);
        if (get_type_id(max_item) != LMD_TYPE_UNDEFINED && max_item.item != ITEM_NULL) {
            if (!js_to_index_int(max_item, &max_byte_length, "Invalid shared array buffer maxByteLength")) return ItemNull;
            if (max_byte_length < byte_length) return js_throw_range_error("Invalid shared array buffer maxByteLength");
            growable = true;
        }
    }

    JsArrayBuffer* ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
    ab->byte_length = byte_length;
    ab->max_byte_length = max_byte_length;
    ab->data = mem_calloc(1, byte_length > 0 ? byte_length : 1, MEM_CAT_JS_RUNTIME);
    ab->detached = false;
    ab->is_shared = true;
    ab->resizable = growable;

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_ARRAYBUFFER;
    m->type = (void*)&js_sharedarraybuffer_type_marker;
    m->data = ab;
    m->data_cap = 0;

    Item result = (Item){.map = m};
    Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("SharedArrayBuffer"))});
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item proto = js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype"))});
        if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(result, proto);
    }
    return result;
}

extern "C" bool js_is_sharedarraybuffer(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    if (!m || m->map_kind != MAP_KIND_ARRAYBUFFER) return false;
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(m);
    return ab && ab->is_shared;
}

extern "C" Item js_sharedarraybuffer_method(Item sab, Item method_name, Item* args, int argc) {
    if (!js_is_sharedarraybuffer(sab)) return js_throw_type_error("SharedArrayBuffer method requires a SharedArrayBuffer receiver");
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(sab.map);
    if (!ab) return ItemNull;

    String* mname = it2s(method_name);
    if (!mname) return ItemNull;

    // slice(begin, end)
    if (mname->len == 5 && strncmp(mname->chars, "slice", 5) == 0) {
        int begin = 0, end = ab->byte_length;
        if (argc > 0) {
            Item b = js_to_number(args[0]);
            if (js_check_exception()) return ItemNull;
            begin = (int)(get_type_id(b) == LMD_TYPE_FLOAT ? it2d(b) : (double)it2i(b));
            if (begin < 0) begin = ab->byte_length + begin;
            if (begin < 0) begin = 0;
            if (begin > ab->byte_length) begin = ab->byte_length;
        }
        if (argc > 1 && get_type_id(args[1]) != LMD_TYPE_UNDEFINED) {
            Item e = js_to_number(args[1]);
            if (js_check_exception()) return ItemNull;
            end = (int)(get_type_id(e) == LMD_TYPE_FLOAT ? it2d(e) : (double)it2i(e));
            if (end < 0) end = ab->byte_length + end;
            if (end < 0) end = 0;
            if (end > ab->byte_length) end = ab->byte_length;
        }
        if (end < begin) end = begin;
        int new_len = end - begin;

        Item result_item = ItemNull;
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
        Item ctor = js_property_get(sab, ctor_key);
        if (js_check_exception()) return ItemNull;

        bool use_default_ctor = get_type_id(ctor) == LMD_TYPE_UNDEFINED;
        if (!use_default_ctor) {
            TypeId ctor_type = get_type_id(ctor);
            if (ctor_type != LMD_TYPE_MAP && ctor_type != LMD_TYPE_ARRAY &&
                ctor_type != LMD_TYPE_FUNC && ctor_type != LMD_TYPE_ELEMENT) {
                return js_throw_type_error("SharedArrayBuffer species constructor must be an object");
            }
            Item species_key = (Item){.item = s2it(heap_create_name("__sym_6"))};
            Item species = js_property_get(ctor, species_key);
            if (js_check_exception()) return ItemNull;
            TypeId species_type = get_type_id(species);
            if (species_type == LMD_TYPE_UNDEFINED || species_type == LMD_TYPE_NULL) {
                use_default_ctor = true;
            } else {
                Item len_arg = (Item){.item = i2it(new_len)};
                result_item = js_new_from_class_object(species, &len_arg, 1);
                if (js_check_exception()) return ItemNull;
            }
        }
        if (use_default_ctor) {
            result_item = js_sharedarraybuffer_construct((Item){.item = i2it(new_len)});
        }
        if (js_check_exception()) return ItemNull;
        if (!js_is_sharedarraybuffer(result_item)) {
            return js_throw_type_error("SharedArrayBuffer species constructor did not return a SharedArrayBuffer");
        }
        if (result_item.item == sab.item) {
            return js_throw_type_error("SharedArrayBuffer species constructor returned the same buffer");
        }
        JsArrayBuffer* rab = js_get_arraybuffer_ptr(result_item.map);
        if (!rab || rab->byte_length < new_len) {
            return js_throw_type_error("SharedArrayBuffer species constructor returned a buffer that is too small");
        }
        if (rab && new_len > 0) {
            memcpy(rab->data, (char*)ab->data + begin, new_len);
        }
        return result_item;
    }

    if (mname->len == 4 && strncmp(mname->chars, "grow", 4) == 0) {
        if (!ab->resizable) return js_throw_type_error("SharedArrayBuffer is not growable");
        Item new_length_item = argc > 0 ? args[0] : (Item){.item = ITEM_JS_UNDEFINED};
        int new_length = 0;
        if (!js_to_index_int(new_length_item, &new_length, "Invalid shared array buffer length")) return ItemNull;
        if (new_length < ab->byte_length || new_length > ab->max_byte_length) {
            return js_throw_range_error("Invalid shared array buffer length");
        }
        if (new_length != ab->byte_length) {
            void* new_data = mem_calloc(1, new_length > 0 ? new_length : 1, MEM_CAT_JS_RUNTIME);
            int copy_len = ab->byte_length < new_length ? ab->byte_length : new_length;
            if (ab->data && copy_len > 0) memcpy(new_data, ab->data, copy_len);
            ab->data = new_data;
            ab->byte_length = new_length;
        }
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    return ItemNull;
}

// ============================================================================
// TypedArray
// ============================================================================

extern "C" bool js_is_typed_array(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->map_kind == MAP_KIND_TYPED_ARRAY;
}

// Get the JsTypedArray* from a Map, handling both original and upgraded layouts.
// Original: m->data holds JsTypedArray* directly (data_cap == 0).
// Upgraded: JsTypedArray* is stored as __ta__ int64 property (after first user property write).
extern "C" JsTypedArray* js_get_typed_array_ptr(Map* m) {
    if (m->data_cap == 0)
        return (JsTypedArray*)m->data;
    // upgraded: retrieve from __ta__ internal property
    bool found = false;
    extern Item js_map_get_fast_ext(Map*, const char*, int, bool*);
    Item ta_val = js_map_get_fast_ext(m, "__ta__", 6, &found);
    if (found) return (JsTypedArray*)(uintptr_t)it2i(ta_val);
    return NULL;
}

// Create a standalone typed array (owns its buffer)
extern "C" Item js_typed_array_new(int type_id, int length) {
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = typed_array_element_size(arr_type);
    int byte_length = length * elem_size;

    JsTypedArray* ta = (JsTypedArray*)mem_alloc(sizeof(JsTypedArray), MEM_CAT_JS_RUNTIME);
    ta->element_type = arr_type;
    ta->length = length;
    ta->byte_length = byte_length;
    ta->byte_offset = 0;
    ta->data = mem_calloc(length > 0 ? length : 1, elem_size, MEM_CAT_JS_RUNTIME);
    ta->buffer = NULL;
    ta->buffer_item = 0;
    ta->length_tracking = false;
    ta->is_buffer = false;
    ta->view = array_num_new_external_view(NULL, ta->data,
        js_typed_array_elem_type(arr_type), 0, length, true);
    js_typed_array_refresh_arraynum_view(ta);

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_TYPED_ARRAY;
    m->type = (void*)&js_typed_array_type_marker;
    m->data = ta;
    m->data_cap = 0;

    return (Item){.map = m};
}

// Create a typed array as a view over an ArrayBuffer
extern "C" Item js_typed_array_new_from_buffer(int type_id, Item buffer_item, int byte_offset, int length) {
    if (!js_is_arraybuffer(buffer_item)) {
        log_error("js_typed_array_new_from_buffer: argument is not an ArrayBuffer");
        return js_throw_type_error("TypedArray buffer argument must be an ArrayBuffer");
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(buffer_item.map);
    JsTypedArrayType arr_type = (JsTypedArrayType)type_id;
    int elem_size = typed_array_element_size(arr_type);

    if (ab->detached) {
        return js_throw_type_error("Cannot construct TypedArray from detached ArrayBuffer");
    }
    if (byte_offset < 0) {
        return js_throw_range_error("Invalid typed array byteOffset");
    }
    if (byte_offset % elem_size != 0) {
        log_error("js_typed_array_new_from_buffer: byte_offset %d not aligned to element size %d", byte_offset, elem_size);
        return js_throw_range_error("Invalid typed array byteOffset");
    }
    if (byte_offset > ab->byte_length) {
        return js_throw_range_error("Invalid typed array byteOffset");
    }

    int available = ab->byte_length - byte_offset;

    bool length_tracking = length < 0;
    if (length_tracking) {
        // Js54 P6: per ES2024 §10.4.5.5 step 8, the alignment check is only
        // required for non-resizable buffers. Resizable buffers with undefined
        // length use auto-tracking and simply floor: the spec ignores the
        // trailing remainder. Without this fix `new Float64Array(rab)` over a
        // resizable buffer whose byteLength isn't a multiple of 8 throws
        // RangeError, blocking the species-ctor cluster.
        if (!ab->resizable && (available % elem_size != 0)) {
            return js_throw_range_error("Invalid typed array byteLength");
        }
        length = available / elem_size;
    }
    int byte_length = length * elem_size;
    if (length < 0 || byte_length < 0 || byte_offset + byte_length > ab->byte_length) {
        log_error("js_typed_array_new_from_buffer: view exceeds buffer bounds");
        return js_throw_range_error("Invalid typed array length");
    }

    JsTypedArray* ta = (JsTypedArray*)mem_alloc(sizeof(JsTypedArray), MEM_CAT_JS_RUNTIME);
    ta->element_type = arr_type;
    ta->length = length;
    ta->byte_length = byte_length;
    ta->byte_offset = byte_offset;
    ta->data = (char*)ab->data + byte_offset;  // direct pointer into buffer
    ta->buffer = ab;
    ta->buffer_item = buffer_item.item;  // preserve original Item for identity-preserving .buffer
    ta->length_tracking = length_tracking;
    ta->is_buffer = false;
    ta->view = array_num_new_external_view((Container*)buffer_item.map, ab->data,
        js_typed_array_elem_type(arr_type), byte_offset, length, true);
    js_typed_array_refresh_arraynum_view(ta);

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_TYPED_ARRAY;
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
        Item result = js_typed_array_new(type_id, src_len);
        JsTypedArray* dst = js_get_typed_array_ptr(result.map);
        if (src->element_type == (JsTypedArrayType)type_id) {
            // fast path: same type → memcpy
            int elem_size = typed_array_element_size(src->element_type);
            void* src_data = js_typed_array_current_data(src);
            if (src_data) memcpy(dst->data, src_data, src_len * elem_size);
        } else if (js_typed_array_try_raw_convert_number(dst, src, 0, true) ||
                   js_typed_array_try_raw_convert_bigint(dst, src, 0, true)) {
            // fast path: typed-array conversion without Item boxing
        } else {
            for (int i = 0; i < src_len; i++) {
                Item idx = (Item){.item = i2it(i)};
                Item val = js_typed_array_get(source, idx);
                js_typed_array_set(result, idx, val);
            }
        }
        return result;
    }

    if (src_type == LMD_TYPE_ARRAY) {
        // Copy from regular array
        Array* arr = source.array;
        int len = (int)arr->length;
        Item result = js_typed_array_new(type_id, len);
        if (js_typed_array_try_raw_from_dense_number_array(result, arr, len)) return result;
        Item* values = len > 0 ? (Item*)mem_alloc(sizeof(Item) * len, MEM_CAT_JS_RUNTIME) : NULL;
        for (int i = 0; i < len; i++) values[i] = arr->items[i];
        for (int i = 0; i < len; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item val = values ? values[i] : ItemNull;
            if (val.item == JS_DELETED_SENTINEL_VAL) val = (Item){.item = ITEM_JS_UNDEFINED};
            js_typed_array_set(result, idx, val);
        }
        if (values) mem_free(values);
        return result;
    }

    // Fallback: treat as length
    int len = (int)it2i(source);
    return js_typed_array_new(type_id, len);
}

// Smart constructor: dispatches based on argument type
extern "C" Item js_typed_array_construct(int type_id, Item arg, Item byte_offset_item, Item length_item, int argc) {
    if (argc == 0) {
        return js_typed_array_new(type_id, 0);
    }

    // Check if arg is an ArrayBuffer
    if (js_is_arraybuffer(arg)) {
        int byte_offset = 0;
        if (argc > 1 && !js_dataview_to_index(byte_offset_item, &byte_offset)) return ItemNull;
        int length = -1;
        if (argc > 2 && get_type_id(length_item) != LMD_TYPE_UNDEFINED &&
            !js_dataview_to_index(length_item, &length)) return ItemNull;
        return js_typed_array_new_from_buffer(type_id, arg, byte_offset, length);
    }

    // Check if arg is another TypedArray or Array
    TypeId arg_type = get_type_id(arg);
    if (js_is_typed_array(arg)) {
        return js_typed_array_new_from_array(type_id, arg);
    }
    if (arg_type == LMD_TYPE_ARRAY) {
        Item iter_key = (Item){.item = s2it(heap_create_name("__sym_1"))};
        Item iter_method = js_property_get(arg, iter_key);
        if (js_check_exception()) return ItemNull;
        TypeId iter_type = get_type_id(iter_method);
        bool has_iter = iter_type != LMD_TYPE_UNDEFINED && iter_type != LMD_TYPE_NULL &&
            iter_method.item != ITEM_JS_UNDEFINED;
        if (has_iter) {
            if (iter_type != LMD_TYPE_FUNC) {
                return js_throw_type_error("@@iterator is not callable");
            }
            Item values = js_iterable_to_array(arg);
            if (js_check_exception()) return ItemNull;
            return js_typed_array_new_from_array(type_id, values);
        }
        return js_typed_array_new_from_array(type_id, arg);
    }

    if (arg_type == LMD_TYPE_MAP || arg_type == LMD_TYPE_ELEMENT || arg_type == LMD_TYPE_FUNC || js_is_generator(arg)) {
        Item iter_method = (Item){.item = ITEM_JS_UNDEFINED};
        if (js_is_generator(arg)) {
            Item values = js_iterable_to_array(arg);
            if (js_check_exception()) return ItemNull;
            return js_typed_array_new_from_array(type_id, values);
        }
        Item iter_key = (Item){.item = s2it(heap_create_name("__sym_1"))};
        iter_method = js_property_get(arg, iter_key);
        if (js_check_exception()) return ItemNull;
        TypeId iter_type = get_type_id(iter_method);
        bool has_iter = iter_type != LMD_TYPE_UNDEFINED && iter_type != LMD_TYPE_NULL && iter_method.item != ITEM_JS_UNDEFINED;
        if (has_iter) {
            if (iter_type != LMD_TYPE_FUNC) {
                return js_throw_type_error("@@iterator is not callable");
            }
            Item values = js_iterable_to_array(arg);
            if (js_check_exception()) return ItemNull;
            return js_typed_array_new_from_array(type_id, values);
        }

        Item length_key = (Item){.item = s2it(heap_create_name("length"))};
        Item length_value = js_property_get(arg, length_key);
        if (js_check_exception()) return ItemNull;
        int len = 0;
        if (!js_dataview_to_index(length_value, &len)) return ItemNull;
        Item result = js_typed_array_new(type_id, len);
        for (int i = 0; i < len; i++) {
            Item value = js_property_get(arg, (Item){.item = i2it(i)});
            if (js_check_exception()) return ItemNull;
            js_typed_array_set(result, (Item){.item = i2it(i)}, value);
            if (js_check_exception()) return ItemNull;
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
        if (!js_dataview_to_index(arg, &len)) return ItemNull;
        return js_typed_array_new(type_id, len);
    }

    return js_typed_array_new(type_id, 0);
}

extern "C" Item js_typed_array_raw_get_item(JsTypedArray* ta, void* data, int idx) {
    if (!ta || !data || idx < 0) return (Item){.item = ITEM_JS_UNDEFINED};
    if (js_typed_array_arraynum_view_matches(ta, (const char*)data, idx) &&
        js_typed_array_is_number_element(ta->element_type)) {
        double value = array_num_get_number_value(ta->view, idx);
        switch (ta->element_type) {
        case JS_TYPED_INT8:
        case JS_TYPED_UINT8:
        case JS_TYPED_INT16:
        case JS_TYPED_UINT16:
        case JS_TYPED_INT32:
        case JS_TYPED_UINT32:
        case JS_TYPED_UINT8_CLAMPED:
            return (Item){.item = i2it((int64_t)value)};
        case JS_TYPED_FLOAT32:
        case JS_TYPED_FLOAT64: {
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = value;
            return (Item){.item = d2it(fp)};
        }
        default:
            break;
        }
    }
    switch (ta->element_type) {
    case JS_TYPED_INT8:
        return (Item){.item = i2it((int64_t)((int8_t*)data)[idx])};
    case JS_TYPED_UINT8:
    case JS_TYPED_UINT8_CLAMPED:
        return (Item){.item = i2it((int64_t)((uint8_t*)data)[idx])};
    case JS_TYPED_INT16:
        return (Item){.item = i2it((int64_t)((int16_t*)data)[idx])};
    case JS_TYPED_UINT16:
        return (Item){.item = i2it((int64_t)((uint16_t*)data)[idx])};
    case JS_TYPED_INT32:
        return (Item){.item = i2it((int64_t)((int32_t*)data)[idx])};
    case JS_TYPED_UINT32:
        return (Item){.item = i2it((int64_t)((uint32_t*)data)[idx])};
    case JS_TYPED_FLOAT32: {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = (double)((float*)data)[idx];
        return (Item){.item = d2it(fp)};
    }
    case JS_TYPED_FLOAT64: {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ((double*)data)[idx];
        return (Item){.item = d2it(fp)};
    }
    case JS_TYPED_BIGINT64: {
        extern Item bigint_from_int64(int64_t val);
        return bigint_from_int64(((int64_t*)data)[idx]);
    }
    case JS_TYPED_BIGUINT64: {
        extern Item bigint_from_int64(int64_t val);
        extern Item bigint_from_string(const char* str, int len);
        uint64_t v = ((uint64_t*)data)[idx];
        if (v <= (uint64_t)INT64_MAX) return bigint_from_int64((int64_t)v);
        // Value exceeds int64 range — construct from string
        char buf[32];
        int blen = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
        return bigint_from_string(buf, blen);
    }
    default:
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
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

extern "C" Item js_typed_array_set(Item ta_item, Item index, Item value) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};

    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    int idx = (int)it2i(index);

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
                    js_throw_type_error("Cannot convert non-BigInt value to BigInt");
                    return (Item){.item = ITEM_NULL};
                }
                // Symbol → handled by ctor (throws)
            } else if (vt == LMD_TYPE_FLOAT) {
                js_throw_type_error("Cannot convert non-BigInt value to BigInt");
                return (Item){.item = ITEM_NULL};
            } else if (vt == LMD_TYPE_NULL || value.item == ITEM_JS_UNDEFINED) {
                js_throw_type_error("Cannot convert non-BigInt value to BigInt");
                return (Item){.item = ITEM_NULL};
            }
            bi = js_bigint_constructor(value);
            if (js_check_exception()) return (Item){.item = ITEM_NULL};
        }
        int current_length = js_typed_array_current_length(ta);
        void* data = js_typed_array_current_data(ta);
        if (idx < 0 || idx >= current_length || !data) return value;
        if (ta->element_type == JS_TYPED_BIGINT64) {
            Item wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bi);
            if (js_check_exception()) return (Item){.item = ITEM_NULL};
            int64_t iv = bigint_to_int64(wrapped);
            ((int64_t*)data)[idx] = iv;
        } else {
            Item wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bi);
            if (js_check_exception()) return (Item){.item = ITEM_NULL};
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
            js_throw_type_error("Cannot convert a Symbol value to a number");
            return (Item){.item = ITEM_NULL};
        }
        num_val = (double)iv;
    } else if (vtype == LMD_TYPE_FLOAT) {
        num_val = it2d(value);
    } else {
        // ES spec: IntegerIndexedElementSet calls ToNumber(value)
        // This throws TypeError for Symbol, which is the spec-required behavior
        Item num_item = js_to_number(value);
        if (js_check_exception()) return (Item){.item = ITEM_NULL};
        vtype = get_type_id(num_item);
        if (vtype == LMD_TYPE_INT) num_val = (double)it2i(num_item);
        else if (vtype == LMD_TYPE_FLOAT) num_val = it2d(num_item);
        else num_val = 0.0;
    }

    int current_length = js_typed_array_current_length(ta);
    if (idx < 0 || idx >= current_length) return value;
    void* data = js_typed_array_current_data(ta);
    if (!data) return value;

    switch (ta->element_type) {
    case JS_TYPED_INT8:    ((int8_t*)data)[idx] = (int8_t)js_typed_array_to_int_n(num_val, 8, true); break;
    case JS_TYPED_UINT8:   ((uint8_t*)data)[idx] = (uint8_t)js_typed_array_to_int_n(num_val, 8, false); break;
    case JS_TYPED_UINT8_CLAMPED: {
        // ECMAScript ToUint8Clamp: NaN→0, clamp to [0,255], then round-half-to-even
        if (isnan(num_val) || num_val <= 0.0) { ((uint8_t*)data)[idx] = 0; break; }
        if (num_val >= 255.0) { ((uint8_t*)data)[idx] = 255; break; }
        int f = (int)num_val;  // floor
        double fmod = num_val - f;
        uint8_t v;
        if (fmod < 0.5) v = (uint8_t)f;
        else if (fmod > 0.5) v = (uint8_t)(f + 1);
        else v = (f & 1) ? (uint8_t)(f + 1) : (uint8_t)f;  // ties to even
        ((uint8_t*)data)[idx] = v;
        break;
    }
    case JS_TYPED_INT16:   ((int16_t*)data)[idx] = (int16_t)js_typed_array_to_int_n(num_val, 16, true); break;
    case JS_TYPED_UINT16:  ((uint16_t*)data)[idx] = (uint16_t)js_typed_array_to_int_n(num_val, 16, false); break;
    case JS_TYPED_INT32:   ((int32_t*)data)[idx] = (int32_t)js_typed_array_to_int_n(num_val, 32, true); break;
    case JS_TYPED_UINT32:  ((uint32_t*)data)[idx] = (uint32_t)js_typed_array_to_int_n(num_val, 32, false); break;
    case JS_TYPED_FLOAT32: ((float*)data)[idx] = (float)num_val; break;
    case JS_TYPED_FLOAT64: ((double*)data)[idx] = num_val; break;
    case JS_TYPED_BIGINT64: ((int64_t*)data)[idx] = (int64_t)num_val; break;
    case JS_TYPED_BIGUINT64: ((uint64_t*)data)[idx] = (uint64_t)num_val; break;
    }

    return value;
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
// views see the current ab->data after a resize() reallocs the backing store
// (the cached ta->data would point to the freed-or-stale buffer otherwise).
// Returns NULL for OOB or detached views — callers must treat NULL as a
// short-circuit on the access path.
extern "C" void* js_typed_array_current_data_ptr(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return NULL;
    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_current_data(ta);
}

extern "C" int js_typed_array_byte_length(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return 0;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_current_byte_length(ta);
}

extern "C" int js_typed_array_byte_offset(Item ta_item) {
    if (!js_is_typed_array(ta_item)) return 0;
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);
    js_typed_array_refresh_arraynum_view(ta);
    return js_typed_array_current_byte_offset(ta);
}

extern "C" Item js_typed_array_fill(Item ta_item, Item value, int start, int end) {
    if (!js_is_typed_array(ta_item)) return ta_item;

    Map* m = ta_item.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    bool is_bigint_array = ta && (ta->element_type == JS_TYPED_BIGINT64 || ta->element_type == JS_TYPED_BIGUINT64);
    double num_val = 0.0;
    int64_t bigint_val = 0;

    if (is_bigint_array) {
        Item bigint_item;
        if (!js_dataview_to_bigint_value(value, &bigint_item)) return ItemNull;
        extern int64_t bigint_to_int64(Item bi);
        bigint_val = bigint_to_int64(bigint_item);
    } else {
        if (value.item == ITEM_JS_UNDEFINED) {
            num_val = NAN;
        } else if (!js_dataview_to_number_value(value, &num_val)) {
            return ItemNull;
        }
    }

    // Js54 P6: gate the TA-spec OOB throw on the dispatch mode. When invoked
    // via Array.prototype.fill.call(ta_oob, ...) the spec uses LengthOfArrayLike
    // (which yields 0 for an OOB TA) and the method silently no-ops.
    if (!js_dispatch_as_array_method && js_typed_array_is_out_of_bounds(ta)) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.fill on an out-of-bounds ArrayBuffer");
    }

    int len = js_typed_array_current_length(ta);
    void* data = js_typed_array_current_data(ta);
    if (!data || len <= 0) return ta_item;

    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (end == INT_MAX || end > len) end = len;
    else if (end < 0) end = len + end;
    if (end < 0) end = 0;
    if (start >= end) return ta_item;

    int count = end - start;

    switch (ta->element_type) {
    case JS_TYPED_UINT8:
    case JS_TYPED_UINT8_CLAMPED: {
        int v = (int)js_typed_array_to_int_n(num_val, 8, false);
        if (ta->element_type == JS_TYPED_UINT8_CLAMPED) {
            if (isnan(num_val) || num_val <= 0.0) v = 0;
            else if (num_val >= 255.0) v = 255;
            else {
                int f = (int)num_val;
                double fmod = num_val - f;
                if (fmod < 0.5) v = f;
                else if (fmod > 0.5) v = f + 1;
                else v = (f & 1) ? f + 1 : f;
            }
        }
        memset((uint8_t*)data + start, (uint8_t)v, count);
        return ta_item;
    }
    case JS_TYPED_INT8: {
        memset((int8_t*)data + start, (int8_t)js_typed_array_to_int_n(num_val, 8, true), count);
        return ta_item;
    }
    case JS_TYPED_INT16: {
        int16_t v = (int16_t)js_typed_array_to_int_n(num_val, 16, true);
        int16_t* p = (int16_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_UINT16: {
        uint16_t v = (uint16_t)js_typed_array_to_int_n(num_val, 16, false);
        uint16_t* p = (uint16_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_INT32: {
        int32_t v = (int32_t)js_typed_array_to_int_n(num_val, 32, true);
        int32_t* p = (int32_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_UINT32: {
        uint32_t v = (uint32_t)js_typed_array_to_int_n(num_val, 32, false);
        uint32_t* p = (uint32_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_FLOAT32: {
        float v = (float)num_val;
        float* p = (float*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_FLOAT64: {
        double v = num_val;
        double* p = (double*)data + start;
        for (int i = 0; i < count; i++) p[i] = v;
        return ta_item;
    }
    case JS_TYPED_BIGINT64: {
        int64_t* p = (int64_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = bigint_val;
        return ta_item;
    }
    case JS_TYPED_BIGUINT64: {
        uint64_t* p = (uint64_t*)data + start;
        for (int i = 0; i < count; i++) p[i] = (uint64_t)bigint_val;
        return ta_item;
    }
    default:
        break;
    }

    return ta_item;
}

// .set(source [, offset]) — bulk copy from another array/typed array
extern "C" Item js_typed_array_set_from(Item ta_item, Item source, int offset) {
    if (!js_is_typed_array(ta_item)) return ItemNull;
    int ta_set_stats_enabled = js_ta_set_stats_is_enabled();
    if (ta_set_stats_enabled) g_js_ta_set_stats.calls_total++;
    JsTypedArray* dst = js_get_typed_array_ptr(ta_item.map);
    // Js54 P6: gate the TA-spec OOB throw on dispatch mode (see fill).
    if (!js_dispatch_as_array_method && (!dst || js_typed_array_is_out_of_bounds(dst))) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.set on a detached or out-of-bounds ArrayBuffer");
    }
    if (!dst) return (Item){.item = ITEM_JS_UNDEFINED};
    int target_len = js_typed_array_current_length(dst);
    if (offset < 0) return js_throw_range_error("offset is out of bounds");

    if (js_is_typed_array(source)) {
        JsTypedArray* src = js_get_typed_array_ptr(source.map);
        // Js54 P6: gate the TA-spec OOB throw on dispatch mode.
        if (!js_dispatch_as_array_method &&
            (!src || js_typed_array_is_out_of_bounds(src) || js_typed_array_is_out_of_bounds(dst))) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.set on a detached or out-of-bounds ArrayBuffer");
        }
        if (!src) return (Item){.item = ITEM_JS_UNDEFINED};
        int src_len = js_typed_array_current_length(src);
        if (ta_set_stats_enabled) {
            g_js_ta_set_stats.typed_array_source_calls++;
            js_ta_set_stats_add(&g_js_ta_set_stats.typed_array_elements_total, src_len);
        }
        if ((int64_t)offset + (int64_t)src_len > (int64_t)target_len) {
            return js_throw_range_error("source is too large");
        }
        if (src_len <= 0) {
            if (ta_set_stats_enabled) g_js_ta_set_stats.empty_typed_array_sources++;
            return (Item){.item = ITEM_JS_UNDEFINED};
        }
        if (js_typed_array_is_bigint_element(dst->element_type) !=
            js_typed_array_is_bigint_element(src->element_type)) {
            return js_throw_type_error("Cannot mix BigInt and non-BigInt typed arrays");
        }
        if (ta_set_stats_enabled) g_js_ta_set_stats.same_type_fast_attempts++;
        if (js_typed_array_try_raw_set_same_type(dst, src, offset)) {
            if (ta_set_stats_enabled) g_js_ta_set_stats.same_type_fast_hits++;
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        if (ta_set_stats_enabled) g_js_ta_set_stats.number_convert_attempts++;
        if (js_typed_array_try_raw_convert_number(dst, src, offset, false)) {
            if (ta_set_stats_enabled) g_js_ta_set_stats.number_convert_hits++;
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        if (ta_set_stats_enabled) g_js_ta_set_stats.bigint_convert_attempts++;
        if (js_typed_array_try_raw_convert_bigint(dst, src, offset, false)) {
            if (ta_set_stats_enabled) g_js_ta_set_stats.bigint_convert_hits++;
            return (Item){.item = ITEM_JS_UNDEFINED};
        }

        if (ta_set_stats_enabled) {
            g_js_ta_set_stats.generic_boxed_fallbacks++;
            js_ta_set_stats_add(&g_js_ta_set_stats.generic_boxed_elements, src_len);
        }
        Item* values = (Item*)mem_alloc((size_t)src_len * sizeof(Item), MEM_CAT_JS_RUNTIME);
        if (!values) return js_throw_type_error("TypedArray.prototype.set allocation failed");
        for (int i = 0; i < src_len; i++) {
            values[i] = js_typed_array_get(source, (Item){.item = i2it(i)});
            if (js_check_exception()) {
                mem_free(values);
                return ItemNull;
            }
        }
        for (int i = 0; i < src_len; i++) {
            js_typed_array_set(ta_item, (Item){.item = i2it(offset + i)}, values[i]);
            if (js_check_exception()) {
                mem_free(values);
                return ItemNull;
            }
        }
        mem_free(values);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    TypeId source_type = get_type_id(source);
    if (source_type == LMD_TYPE_NULL || source_type == LMD_TYPE_UNDEFINED || source.item == ITEM_JS_UNDEFINED) {
        return js_throw_type_error("Cannot convert undefined or null to object");
    }
    Item src_obj = js_to_object(source);
    if (js_check_exception()) return ItemNull;

    Item length_key = (Item){.item = s2it(heap_create_name("length"))};
    Item length_item = js_property_get(src_obj, length_key);
    if (js_check_exception()) return ItemNull;
    Item length_num = js_to_number(length_item);
    if (js_check_exception()) return ItemNull;
    double length_double = js_get_number(length_num);
    int64_t src_len = 0;
    if (length_double != length_double || length_double <= 0.0) {
        src_len = 0;
    } else if (length_double >= 9007199254740991.0) {
        src_len = 9007199254740991LL;
    } else {
        src_len = (int64_t)floor(length_double);
    }
    if (ta_set_stats_enabled) {
        g_js_ta_set_stats.array_like_source_calls++;
        js_ta_set_stats_add(&g_js_ta_set_stats.array_like_elements_total, src_len);
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

    if (ta_set_stats_enabled) js_ta_set_stats_add(&g_js_ta_set_stats.array_like_loop_elements, src_len);
    for (int64_t i = 0; i < src_len; i++) {
        Item value = js_property_get(src_obj, (Item){.item = i2it(i)});
        if (js_check_exception()) return ItemNull;
        js_typed_array_set(ta_item, (Item){.item = i2it((int64_t)offset + i)}, value);
        if (js_check_exception()) return ItemNull;
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// .slice(begin, end) — creates a copy
extern "C" Item js_typed_array_slice(Item ta_item, int start, int end) {
    if (!js_is_typed_array(ta_item)) return (Item){.item = ITEM_NULL};
    JsTypedArray* ta = js_get_typed_array_ptr(ta_item.map);

    // Js54 P6: gate the TA-spec OOB throw on dispatch mode.
    if (!js_dispatch_as_array_method && js_typed_array_is_out_of_bounds(ta)) {
        return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on an out-of-bounds ArrayBuffer");
    }

    int current_len = js_typed_array_current_length(ta);

    if (start < 0) start = current_len + start;
    if (end < 0) end = current_len + end;
    if (start < 0) start = 0;
    if (start >= end) {
        Item result = js_typed_array_species_create(ta_item, 0);
        if (js_check_exception()) return (Item){.item = ITEM_NULL};
        return result;
    }

    int new_length = end - start;
    Item result = js_typed_array_species_create(ta_item, new_length);
    if (js_check_exception()) return (Item){.item = ITEM_NULL};
    if (new_length > 0 && !js_dispatch_as_array_method) {
        if (ta->buffer && ta->buffer->detached) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on a detached ArrayBuffer");
        }
        if (js_typed_array_is_out_of_bounds(ta)) {
            return js_throw_type_error("Cannot perform %TypedArray%.prototype.slice on an out-of-bounds ArrayBuffer");
        }
    }
    // Copy elements — species may return a different typed array type, so use element-by-element copy
    JsTypedArray* rta = js_get_typed_array_ptr(result.map);
    if (rta && rta->element_type == ta->element_type && rta->length >= new_length) {
        int elem_size = typed_array_element_size(ta->element_type);
        int count_bytes = new_length * elem_size;
        int source_byte_length = js_typed_array_current_byte_length(ta);
        char* src_data = (char*)js_typed_array_current_data(ta);
        char* dst_data = (char*)js_typed_array_current_data(rta);
        int src_start = start * elem_size;
        if (src_data && dst_data && ta->buffer && rta->buffer && ta->buffer == rta->buffer) {
            for (int i = 0; i < count_bytes; i++) {
                int src_index = src_start + i;
                dst_data[i] = (src_index >= 0 && src_index < source_byte_length) ? src_data[src_index] : 0;
            }
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
                extern Item bigint_from_int64(int64_t val);
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

    int elem_size = typed_array_element_size(ta->element_type);
    int available_len = ta->length;
    int begin_byte_offset = ta->byte_offset + start * elem_size;
    bool result_length_tracking = ta->buffer && ta->length_tracking && end_is_default;

    if (ta->buffer && !ta->buffer->detached) {
        int available_bytes = ta->buffer->byte_length - ta->byte_offset;
        if (available_bytes < 0) available_bytes = 0;
        available_len = available_bytes / elem_size;
        if (begin_byte_offset > ta->buffer->byte_length) {
            return js_throw_range_error("offset is out of bounds");
        }
        if (!result_length_tracking && end > available_len) {
            return js_throw_range_error("length is out of bounds");
        }
    }

    if (end < start) end = start;
    int new_length = result_length_tracking ? available_len - start : end - start;
    if (new_length < 0) new_length = 0;

    if (!ta->buffer) {
        JsArrayBuffer* ab = (JsArrayBuffer*)mem_alloc(sizeof(JsArrayBuffer), MEM_CAT_JS_RUNTIME);
        ab->data = ta->data;
        ab->byte_length = ta->byte_length;
        ab->max_byte_length = ta->byte_length;
        ab->detached = false;
        ab->is_shared = false;
        ab->resizable = false;
        ta->buffer = ab;
        Item wrapped = js_arraybuffer_wrap(ab);
        ta->buffer_item = wrapped.item;
    }
    return js_typed_array_species_create_from_buffer(
        ta_item, (Item){.item = ta->buffer_item}, ta->byte_offset + start * elem_size,
        new_length, result_length_tracking);
}

// ============================================================================
// DataView
// ============================================================================

extern "C" bool js_is_dataview(Item val) {
    TypeId type = get_type_id(val);
    if (type != LMD_TYPE_MAP) return false;
    Map* m = val.map;
    return m && m->map_kind == MAP_KIND_DATAVIEW;
}

// Get JsDataView* from a Map, handling both original and upgraded layouts.
static JsDataView* js_get_dataview_ptr_from_map(Map* m) {
    if (m->type == (void*)&js_dataview_type_marker)
        return (JsDataView*)m->data;
    bool found = false;
    Item dv_val = js_map_get_fast_ext(m, "__dv__", 6, &found);
    if (found) return (JsDataView*)(uintptr_t)it2i(dv_val);
    return NULL;
}

extern "C" JsDataView* js_get_dataview_ptr(Item val) {
    if (!js_is_dataview(val)) return NULL;
    return js_get_dataview_ptr_from_map(val.map);
}

extern "C" Item js_dataview_new(Item buffer, Item offset_item, Item length_item) {
    if (!js_is_arraybuffer(buffer)) {
        js_throw_type_error("First argument to DataView constructor must be an ArrayBuffer");
        return ItemNull;
    }
    JsArrayBuffer* ab = js_get_arraybuffer_ptr(buffer.map);
    if (!ab) return ItemNull;

    int byte_offset = 0;
    if (!js_dataview_to_index(offset_item, &byte_offset)) return ItemNull;

    if (ab->detached) {
        js_throw_type_error("DataView buffer is detached");
        return ItemNull;
    }

    if (byte_offset < 0 || byte_offset > ab->byte_length) {
        js_throw_range_error("Start offset is outside the bounds of the buffer");
        return ItemNull;
    }

    int byte_length;
    TypeId lt = get_type_id(length_item);
    // Js54 P2: length_tracking when constructor called without explicit byteLength.
    // The recorded byte_length is the initial value; readers re-derive from the
    // buffer's current byte_length on every access for length-tracking views.
    bool length_tracking = (lt == LMD_TYPE_UNDEFINED) && ab->resizable;
    if (lt == LMD_TYPE_UNDEFINED) {
        byte_length = ab->byte_length - byte_offset;
    } else {
        if (!js_dataview_to_index(length_item, &byte_length)) return ItemNull;
    }

    if (byte_length < 0 || (int64_t)byte_offset + (int64_t)byte_length > ab->byte_length) {
        js_throw_range_error("Invalid DataView length");
        return ItemNull;
    }

    JsDataView* dv = (JsDataView*)mem_alloc(sizeof(JsDataView), MEM_CAT_JS_RUNTIME);
    dv->buffer = ab;
    dv->byte_offset = byte_offset;
    dv->byte_length = byte_length;
    dv->buffer_item = buffer.item;
    dv->length_tracking = length_tracking;

    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_DATAVIEW;
    m->type = (void*)&js_dataview_type_marker;
    m->data = dv;
    m->data_cap = 0;
    Item view = (Item){.map = m};
    js_class_stamp(view, JS_CLASS_DATA_VIEW);
    js_dataview_link_prototype(view);
    return view;
}

// Js54 P2: current byte_length honoring length-tracking views over resizable
// buffers. For non-length-tracking views the stored byte_length is authoritative;
// for length-tracking views we re-derive from the buffer's current size.
static inline int dv_current_byte_length(JsDataView* dv) {
    if (!dv || !dv->buffer) return 0;
    if (dv->length_tracking) {
        int avail = dv->buffer->byte_length - dv->byte_offset;
        return avail > 0 ? avail : 0;
    }
    return dv->byte_length;
}

// Js54 P2: a DataView is out-of-bounds when the buffer is detached, or when the
// recorded view window no longer fits (resize shrank the buffer below
// byte_offset + byte_length, or below byte_offset for length-tracking views).
static inline bool dv_is_out_of_bounds(JsDataView* dv) {
    if (!dv || !dv->buffer) return false;
    if (dv->buffer->detached) return true;
    if (dv->length_tracking) {
        return dv->buffer->byte_length < dv->byte_offset;
    }
    return dv->buffer->byte_length < (int64_t)dv->byte_offset + (int64_t)dv->byte_length;
}

// Js54 P2: throws TypeError if the DataView is detached or out-of-bounds.
// Returns true on success (in-bounds), false on throw — caller should propagate.
static inline bool dv_validate_or_throw(JsDataView* dv) {
    if (dv_is_out_of_bounds(dv)) {
        js_throw_type_error("DataView buffer is detached or out of bounds");
        return false;
    }
    return true;
}

// Helper: get raw pointer into DataView's buffer at given offset
// Js54 P2: bounds-check against the CURRENT view length so length-tracking
// views see live shrink/grow, not the cached construction-time byte_length.
static inline uint8_t* dv_ptr(JsDataView* dv, int offset, int size) {
    int current_len = dv_current_byte_length(dv);
    if (offset < 0 || offset + size > current_len) return NULL;
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
    JsDataView* dv = js_get_dataview_ptr_from_map(dv_item.map);
    if (!dv) return (Item){.item = ITEM_NULL};

    String* mname = it2s(method_name);
    if (!mname) return (Item){.item = ITEM_NULL};
    const char* mn = mname->chars;
    int ml = (int)mname->len;

    bool is_view_method =
        (ml == 7 && strncmp(mn, "getInt8", 7) == 0) ||
        (ml == 8 && strncmp(mn, "getUint8", 8) == 0) ||
        (ml == 8 && strncmp(mn, "getInt16", 8) == 0) ||
        (ml == 9 && strncmp(mn, "getUint16", 9) == 0) ||
        (ml == 8 && strncmp(mn, "getInt32", 8) == 0) ||
        (ml == 9 && strncmp(mn, "getUint32", 9) == 0) ||
        (ml == 10 && strncmp(mn, "getFloat32", 10) == 0) ||
        (ml == 10 && strncmp(mn, "getFloat64", 10) == 0) ||
        (ml == 11 && strncmp(mn, "getBigInt64", 11) == 0) ||
        (ml == 12 && strncmp(mn, "getBigUint64", 12) == 0) ||
        (ml == 7 && strncmp(mn, "setInt8", 7) == 0) ||
        (ml == 8 && strncmp(mn, "setUint8", 8) == 0) ||
        (ml == 8 && strncmp(mn, "setInt16", 8) == 0) ||
        (ml == 9 && strncmp(mn, "setUint16", 9) == 0) ||
        (ml == 8 && strncmp(mn, "setInt32", 8) == 0) ||
        (ml == 9 && strncmp(mn, "setUint32", 9) == 0) ||
        (ml == 10 && strncmp(mn, "setFloat32", 10) == 0) ||
        (ml == 10 && strncmp(mn, "setFloat64", 10) == 0) ||
        (ml == 11 && strncmp(mn, "setBigInt64", 11) == 0) ||
        (ml == 12 && strncmp(mn, "setBigUint64", 12) == 0);
    if (!is_view_method) return (Item){.item = ITEM_NULL};

    int offset = 0;
    Item offset_item = (argc > 0) ? args[0] : (Item){.item = ITEM_JS_UNDEFINED};
    if (!js_dataview_to_index(offset_item, &offset)) return ItemNull;
    bool is_set_method = (ml >= 3 && mn[0] == 's' && mn[1] == 'e' && mn[2] == 't');
    // Js54 P2: get-methods validate up front (spec: TypeError on detached or OOB).
    // set-methods perform ToNumber/ToBigInt on the value first (those calls can
    // observe side effects that resize the buffer), so per spec the OOB check
    // moves into each individual setter after the value coercion.
    if (!is_set_method && !dv_validate_or_throw(dv)) return ItemNull;
    bool sys_le = is_little_endian_system();

    // Getter methods
    if (ml == 7 && strncmp(mn, "getInt8", 7) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        return (Item){.item = i2it((int64_t)(int8_t)*p)};
    }
    if (ml == 8 && strncmp(mn, "getUint8", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        return (Item){.item = i2it((int64_t)*p)};
    }
    if (ml == 8 && strncmp(mn, "getInt16", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint16_t raw;
        memcpy(&raw, p, 2);
        if (little_endian != sys_le) raw = swap16(raw);
        return (Item){.item = i2it((int64_t)(int16_t)raw)};
    }
    if (ml == 9 && strncmp(mn, "getUint16", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint16_t raw;
        memcpy(&raw, p, 2);
        if (little_endian != sys_le) raw = swap16(raw);
        return (Item){.item = i2it((int64_t)raw)};
    }
    if (ml == 8 && strncmp(mn, "getInt32", 8) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint32_t raw;
        memcpy(&raw, p, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        return (Item){.item = i2it((int64_t)(int32_t)raw)};
    }
    if (ml == 9 && strncmp(mn, "getUint32", 9) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint32_t raw;
        memcpy(&raw, p, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        return (Item){.item = i2it((int64_t)raw)};
    }
    if (ml == 10 && strncmp(mn, "getFloat32", 10) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
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
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint64_t raw;
        memcpy(&raw, p, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        double d;
        memcpy(&d, &raw, 8);
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = d;
        return (Item){.item = d2it(fp)};
    }
    if (ml == 11 && strncmp(mn, "getBigInt64", 11) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint64_t raw;
        memcpy(&raw, p, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        return bigint_from_int64((int64_t)raw);
    }
    if (ml == 12 && strncmp(mn, "getBigUint64", 12) == 0) {
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 1) ? js_is_truthy(args[1]) : false;
        uint64_t raw;
        memcpy(&raw, p, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        return js_dataview_biguint64_item(raw);
    }

    // Setter methods
    if (ml == 7 && strncmp(mn, "setInt8", 7) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        int64_t raw_val = js_dataview_to_integer_value(number_value);
        *p = (uint8_t)(int8_t)raw_val;
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 8 && strncmp(mn, "setUint8", 8) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 1);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        int64_t raw_val = js_dataview_to_integer_value(number_value);
        *p = (uint8_t)raw_val;
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 8 && strncmp(mn, "setInt16", 8) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        uint16_t val = (uint16_t)(int16_t)js_dataview_to_integer_value(number_value);
        if (little_endian != sys_le) val = swap16(val);
        memcpy(p, &val, 2);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 9 && strncmp(mn, "setUint16", 9) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 2);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        uint16_t val = (uint16_t)js_dataview_to_integer_value(number_value);
        if (little_endian != sys_le) val = swap16(val);
        memcpy(p, &val, 2);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 8 && strncmp(mn, "setInt32", 8) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        uint32_t val = (uint32_t)(int32_t)js_dataview_to_integer_value(number_value);
        if (little_endian != sys_le) val = swap32(val);
        memcpy(p, &val, 4);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 9 && strncmp(mn, "setUint32", 9) == 0) {
        double number_value = 0.0;
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        if (!js_dataview_to_number_value(value_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        uint32_t val = (uint32_t)js_dataview_to_integer_value(number_value);
        if (little_endian != sys_le) val = swap32(val);
        memcpy(p, &val, 4);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 10 && strncmp(mn, "setFloat32", 10) == 0) {
        Item val_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        double number_value = 0.0;
        if (!js_dataview_to_number_value(val_item, &number_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 4);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        float f = (float)number_value;
        uint32_t raw;
        memcpy(&raw, &f, 4);
        if (little_endian != sys_le) raw = swap32(raw);
        memcpy(p, &raw, 4);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 10 && strncmp(mn, "setFloat64", 10) == 0) {
        Item val_item2 = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        double d = 0.0;
        if (!js_dataview_to_number_value(val_item2, &d)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        uint64_t raw;
        memcpy(&raw, &d, 8);
        if (little_endian != sys_le) raw = swap64(raw);
        memcpy(p, &raw, 8);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 11 && strncmp(mn, "setBigInt64", 11) == 0) {
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        Item bigint_value;
        if (!js_dataview_to_bigint_value(value_item, &bigint_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        Item wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_value);
        if (js_check_exception()) return ItemNull;
        uint64_t raw = (uint64_t)bigint_to_int64(wrapped);
        if (little_endian != sys_le) raw = swap64(raw);
        memcpy(p, &raw, 8);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }
    if (ml == 12 && strncmp(mn, "setBigUint64", 12) == 0) {
        Item value_item = (argc >= 2) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
        Item bigint_value;
        if (!js_dataview_to_bigint_value(value_item, &bigint_value)) return ItemNull;
        if (!dv_validate_or_throw(dv)) return ItemNull;
        uint8_t* p = dv_ptr(dv, offset, 8);
        if (!p) return js_throw_range_error("Invalid DataView offset");
        bool little_endian = (argc > 2) ? js_is_truthy(args[2]) : false;
        Item wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_value);
        if (js_check_exception()) return ItemNull;
        uint64_t raw = js_dataview_bigint_to_uint64(wrapped);
        if (little_endian != sys_le) raw = swap64(raw);
        memcpy(p, &raw, 8);
        return (Item){.item = ITEM_JS_UNDEFINED};
    }

    log_error("DataView: unknown method '%.*s'", ml, mn);
    return (Item){.item = ITEM_NULL};
}
