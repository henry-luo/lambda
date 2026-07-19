/**
 * js_fs.cpp — Node.js-style 'fs' module for LambdaJS v15
 *
 * Provides synchronous and asynchronous file I/O backed by libuv.
 * Registered as built-in module 'fs' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_typed_array.h"
#include "js_error_codes.h"
#include "js_permission.h"
#include "js_class.h"
#include "js_property_attrs.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include "../../lib/mem.h"
#include "../../lib/file.h"
#ifdef _WIN32
// S_IFSOCK is not defined on Windows
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000
#endif
#endif

extern "C" Item js_util_custom_promisify_args_symbol(void);
extern "C" Item js_util_promisify_custom_symbol(void);
extern "C" Item js_domain_get_current(void);
extern "C" Item js_domain_call_function(Item domain, Item fn, Item this_val, Item* args, int arg_count);
extern __thread EvalContext* context;

// Helper: make JS undefined value
// Helper: extract a null-terminated C string from an Item string
// Returns a stack-allocated buffer (caller should not free)
static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

static bool fs_string_has_nul(String* s) {
    if (!s) return false;
    for (int i = 0; i < (int)s->len; i++) {
        if (s->chars[i] == '\0') return true;
    }
    return false;
}

static const char* fs_path_to_cstr(Item value, const char* name, char* buf, int buf_size) {
    bool from_url = false;
    if (get_type_id(value) == LMD_TYPE_MAP && js_class_id(value) == JS_CLASS_URL) {
        Item protocol = js_property_get(value, (Item){.item = s2it(heap_create_name("protocol", 8))});
        if (get_type_id(protocol) != LMD_TYPE_STRING ||
            it2s(protocol)->len < 5 || memcmp(it2s(protocol)->chars, "file:", 5) != 0) {
            js_throw_type_error_code("ERR_INVALID_URL_SCHEME", "The URL must be of scheme file");
            return NULL;
        }
        value = js_property_get(value, (Item){.item = s2it(heap_create_name("pathname", 8))});
        from_url = true;
    }
    if (js_is_typed_array(value)) {
        if (js_typed_array_is_out_of_bounds_item(value)) {
            js_throw_invalid_arg_type(name, "string or an instance of Buffer or URL", value);
            return NULL;
        }
        int len = js_typed_array_byte_length(value);
        void* data = js_typed_array_current_data_ptr(value);
        if (len > 0 && !data) return NULL;
        if (len >= buf_size) len = buf_size - 1;
        for (int i = 0; i < len; i++) {
            if (((char*)data)[i] == '\0') {
                js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                    "The argument 'path' must be a string, Uint8Array, or URL without null bytes.");
                return NULL;
            }
        }
        memcpy(buf, data, len);
        buf[len] = '\0';
        return buf;
    }
    if (get_type_id(value) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type(name, "string or an instance of Buffer or URL", value);
        return NULL;
    }
    String* s = it2s(value);
    if (fs_string_has_nul(s)) {
        js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
            "The argument 'path' must be a string, Uint8Array, or URL without null bytes.");
        return NULL;
    }
    if (from_url) {
        for (int i = 0; i + 2 < (int)s->len; i++) {
            if (s->chars[i] == '%' && s->chars[i + 1] == '0' && s->chars[i + 2] == '0') {
                js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                    "The argument 'path' must be a string, Uint8Array, or URL without null bytes.");
                return NULL;
            }
        }
    }
    return item_to_cstr(value, buf, buf_size);
}

static bool fs_string_equals(String* s, const char* lit) {
    if (!s || !lit) return false;
    size_t len = strlen(lit);
    return s->len == len && memcmp(s->chars, lit, len) == 0;
}

static bool fs_is_valid_encoding(String* s) {
    return fs_string_equals(s, "utf8") || fs_string_equals(s, "utf-8") ||
           fs_string_equals(s, "buffer") || fs_string_equals(s, "ascii") ||
           fs_string_equals(s, "base64") || fs_string_equals(s, "base64url") ||
           fs_string_equals(s, "hex") || fs_string_equals(s, "latin1") ||
           fs_string_equals(s, "binary") || fs_string_equals(s, "ucs2") ||
           fs_string_equals(s, "ucs-2") || fs_string_equals(s, "utf16le") ||
           fs_string_equals(s, "utf-16le");
}

static bool fs_validate_encoding_item(Item encoding_item) {
    TypeId type = get_type_id(encoding_item);
    if (type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_NULL) return true;
    if (type != LMD_TYPE_STRING) return true;
    String* s = it2s(encoding_item);
    if (fs_is_valid_encoding(s)) return true;
    char msg[256];
    snprintf(msg, sizeof(msg),
             "The argument 'encoding' is invalid encoding. Received '%.*s'",
             (int)s->len, s->chars);
    js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
    return false;
}

static Item fs_permission_callback_error(Item callback, const char* permission, const char* path, const char* message) {
    Item err = js_permission_make_fs_error(permission, path, message);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static bool fs_validate_encoding_options(Item options_item) {
    TypeId type = get_type_id(options_item);
    if (type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_NULL) return true;
    if (type == LMD_TYPE_STRING) return fs_validate_encoding_item(options_item);
    if (type == LMD_TYPE_MAP) {
        Item encoding = js_property_get(options_item, (Item){.item = s2it(heap_create_name("encoding", 8))});
        return fs_validate_encoding_item(encoding);
    }
    return true;
}

static bool fs_read_file_should_return_buffer(Item options_item) {
    TypeId type = get_type_id(options_item);
    if (type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_NULL) return true;
    if (type == LMD_TYPE_STRING) return fs_string_equals(it2s(options_item), "buffer");
    if (type == LMD_TYPE_MAP) {
        Item encoding = js_property_get(options_item, (Item){.item = s2it(heap_create_name("encoding", 8))});
        return fs_read_file_should_return_buffer(encoding);
    }
    return true;
}

static bool fs_validate_watch_options(Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return false;
    if (get_type_id(options_item) != LMD_TYPE_MAP) return true;

    Item ignore = js_property_get(options_item, (Item){.item = s2it(heap_create_name("ignore", 6))});
    TypeId type = get_type_id(ignore);
    if (type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_NULL) return true;
    if (type == LMD_TYPE_STRING) {
        if (it2s(ignore)->len == 0) {
            return js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The property 'options.ignore' is invalid. Received ''"), false;
        }
        return true;
    }
    if (type == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(ignore);
        for (int64_t i = 0; i < len; i++) {
            Item entry = js_array_get_int(ignore, i);
            if (get_type_id(entry) != LMD_TYPE_STRING) {
                return js_throw_invalid_arg_type("options.ignore", "string or an array of strings", entry), false;
            }
            if (it2s(entry)->len == 0) {
                return js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The property 'options.ignore' is invalid. Received ''"), false;
            }
        }
        return true;
    }
    return js_throw_invalid_arg_type("options.ignore", "string or an array of strings", ignore), false;
}

static bool fs_validate_int_range(Item value, const char* name, int64_t min, int64_t max, int64_t* out) {
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT) {
        js_throw_invalid_arg_type(name, "number", value);
        return false;
    }
    double number = type == LMD_TYPE_INT ? (double)it2i(value) : it2d(value);
    if (!isfinite(number) || number != floor(number)) {
        char msg[256];
        const char* received = NULL;
        if (type == LMD_TYPE_FLOAT) {
            if (isnan(number)) received = "NaN";
            else if (number == INFINITY) received = "Infinity";
            else if (number == -INFINITY) received = "-Infinity";
        }
        if (received) {
            snprintf(msg, sizeof(msg),
                "The value of \"%s\" is out of range. It must be an integer. Received %s",
                name, received);
            js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        } else {
            js_throw_out_of_range(name, "an integer", value);
        }
        return false;
    }
    if (number < (double)min || number > (double)max) {
        char range[64];
        snprintf(range, sizeof(range), ">= %lld && <= %lld", (long long)min, (long long)max);
        js_throw_out_of_range(name, range, value);
        return false;
    }
    *out = (int64_t)number;
    return true;
}

static bool fs_validate_fd(Item value, int* out_fd) {
    int64_t fd = 0;
    if (!fs_validate_int_range(value, "fd", 0, 2147483647LL, &fd)) return false;
    *out_fd = (int)fd;
    return true;
}

static bool fs_validate_uint32(Item value, const char* name, uint32_t* out) {
    int64_t number = 0;
    if (!fs_validate_int_range(value, name, 0, 4294967295LL, &number)) return false;
    *out = (uint32_t)number;
    return true;
}

static bool fs_validate_mode(Item value, uint32_t* out) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (!s || s->len == 0) {
            return false;
        }
        uint32_t mode = 0;
        for (int i = 0; i < (int)s->len; i++) {
            char ch = s->chars[i];
            if (ch < '0' || ch > '7') {
                js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The argument 'mode' must be a 32-bit unsigned integer or an octal string.");
                return false;
            }
            mode = (mode << 3) + (uint32_t)(ch - '0');
        }
        *out = mode;
        return true;
    }
    return fs_validate_uint32(value, "mode", out);
}

static bool fs_validate_uid_gid(Item value, const char* name, int* out) {
    int64_t number = 0;
    if (!fs_validate_int_range(value, name, -1, 4294967295LL, &number)) return false;
    *out = (int)number;
    return true;
}

static bool fs_validate_offset_length(Item offset_item, Item length_item, int byte_length,
                                      int* out_offset, int* out_length) {
    int64_t offset = 0, length = -1;
    TypeId offset_type = get_type_id(offset_item);
    TypeId length_type = get_type_id(length_item);
    if (offset_type != LMD_TYPE_UNDEFINED && offset_type != LMD_TYPE_NULL) {
        if (offset_type == LMD_TYPE_INT && it2i(offset_item) <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_invalid_arg_type("offset", "number", offset_item);
            return false;
        }
        if (offset_type != LMD_TYPE_INT && offset_type != LMD_TYPE_FLOAT) {
            js_throw_invalid_arg_type("offset", "number", offset_item);
            return false;
        }
        double number = offset_type == LMD_TYPE_INT ? (double)it2i(offset_item) : it2d(offset_item);
        if (!isfinite(number) || number != floor(number)) {
            js_throw_out_of_range("offset", "an integer", offset_item);
            return false;
        }
        if (number < 0) {
            js_throw_out_of_range("offset", ">= 0", offset_item);
            return false;
        }
        offset = (int64_t)number;
    }
    if (length_type != LMD_TYPE_UNDEFINED && length_type != LMD_TYPE_NULL) {
        if (length_type == LMD_TYPE_INT && it2i(length_item) <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_invalid_arg_type("length", "number", length_item);
            return false;
        }
        if (length_type != LMD_TYPE_INT && length_type != LMD_TYPE_FLOAT) {
            js_throw_invalid_arg_type("length", "number", length_item);
            return false;
        }
        double number = length_type == LMD_TYPE_INT ? (double)it2i(length_item) : it2d(length_item);
        if (!isfinite(number) || number != floor(number)) {
            js_throw_out_of_range("length", "an integer", length_item);
            return false;
        }
        if (number < 0) {
            js_throw_out_of_range("length", ">= 0", length_item);
            return false;
        }
        length = (int64_t)number;
    }
    if (offset > byte_length) {
        char range[64];
        snprintf(range, sizeof(range), "<= %d", byte_length);
        js_throw_out_of_range("offset", range, offset_item);
        return false;
    }
    if (length < 0) {
        length = byte_length - offset;
    }
    if (length > byte_length - offset) {
        char range[64];
        snprintf(range, sizeof(range), "<= %lld", (long long)(byte_length - offset));
        js_throw_out_of_range("length", range, length_item);
        return false;
    }
    *out_offset = (int)offset;
    *out_length = (int)length;
    return true;
}

// Helper: create a string Item from a C string
static Item internal_fs_binding_namespace = {0};
static Item internal_fs_default_fstat = {0};

extern "C" Item js_fs_fstatSync(Item fd_item, Item options_item);
extern int js_check_exception(void);
extern Item js_clear_exception(void);

static Item js_internal_fs_fstat(Item fd_item) {
    return js_fs_fstatSync(fd_item, make_js_undefined());
}

extern "C" Item js_get_internal_fs_binding_namespace(void) {
    if (internal_fs_binding_namespace.item == 0) {
        internal_fs_binding_namespace = js_new_object();
        heap_register_gc_root(&internal_fs_binding_namespace.item);
        internal_fs_default_fstat = js_new_function((void*)js_internal_fs_fstat, 1);
        heap_register_gc_root(&internal_fs_default_fstat.item);
        js_property_set(internal_fs_binding_namespace, make_string_item("fstat"), internal_fs_default_fstat);
        js_property_set(internal_fs_binding_namespace, make_string_item("default"), internal_fs_binding_namespace);
    }
    return internal_fs_binding_namespace;
}

static void fs_maybe_call_internal_fstat_hook(int fd) {
    if (internal_fs_binding_namespace.item == 0) return;
    Item fstat = js_property_get(internal_fs_binding_namespace, make_string_item("fstat"));
    if (fstat.item == internal_fs_default_fstat.item) return;
    if (get_type_id(fstat) != LMD_TYPE_FUNC) return;
    Item fd_item = (Item){.item = i2it((int64_t)fd)};
    js_call_function(fstat, internal_fs_binding_namespace, &fd_item, 1);
    if (js_check_exception()) {
        js_clear_exception();
    }
}

static Item js_fs_set_method(Item ns, const char* name, void* func_ptr, int param_count);

static bool fs_parse_access_mode(Item mode_item, bool has_mode, int* out_mode) {
    *out_mode = 0;
    if (!has_mode || get_type_id(mode_item) == LMD_TYPE_UNDEFINED) return true;

    TypeId mode_type = get_type_id(mode_item);
    if (mode_type == LMD_TYPE_INT) {
        int64_t value = it2i(mode_item);
        if (value < 0 || value > 7) {
            js_throw_out_of_range("mode", ">= 0 && <= 7", mode_item);
            return false;
        }
        *out_mode = (int)value;
        return true;
    }
    if (mode_type == LMD_TYPE_FLOAT) {
        double value = it2d(mode_item);
        if (!isfinite(value) || value < 0 || value > 7 || value != (double)(int)value) {
            js_throw_out_of_range("mode", ">= 0 && <= 7", mode_item);
            return false;
        }
        *out_mode = (int)value;
        return true;
    }

    js_throw_invalid_arg_type("mode", "number", mode_item);
    return false;
}

// Helper: get raw data pointer and length from a Buffer (typed array) Item
static uint8_t* buffer_data(Item buf, int* out_len) {
    if (!js_is_typed_array(buf)) { *out_len = 0; return NULL; }
    uint8_t* data = (uint8_t*)js_typed_array_current_data_ptr(buf);
    if (!data) { *out_len = 0; return NULL; }
    *out_len = js_typed_array_byte_length(buf);
    return data;
}

static Item fs_buffer_from_bytes(const char* data, int len) {
    if (len < 0) len = 0;
    Item chunk = js_typed_array_new(JS_TYPED_UINT8, len);
    if (js_is_typed_array(chunk)) {
        JsTypedArray* ta = js_get_typed_array_ptr(chunk.map);
        if (ta) {
            ta->is_buffer = true;
            uint8_t* dst = (uint8_t*)js_typed_array_prepare_write_ptr(chunk);
            if (dst && data && len > 0) {
                memcpy(dst, data, (size_t)len);
            }
        }
    }
    return chunk;
}

static const char* fs_typed_array_name(JsTypedArray* ta) {
    if (!ta) return "Uint8Array";
    if (ta->is_buffer) return "Buffer";
    switch (ta->element_type) {
        case JS_TYPED_INT8: return "Int8Array";
        case JS_TYPED_UINT8: return "Uint8Array";
        case JS_TYPED_INT16: return "Int16Array";
        case JS_TYPED_UINT16: return "Uint16Array";
        case JS_TYPED_INT32: return "Int32Array";
        case JS_TYPED_UINT32: return "Uint32Array";
        case JS_TYPED_FLOAT16: return "Float16Array";
        case JS_TYPED_FLOAT32: return "Float32Array";
        case JS_TYPED_FLOAT64: return "Float64Array";
        case JS_TYPED_UINT8_CLAMPED: return "Uint8ClampedArray";
        case JS_TYPED_BIGINT64: return "BigInt64Array";
        case JS_TYPED_BIGUINT64: return "BigUint64Array";
        default: return "Uint8Array";
    }
}

static JsTypedArray* fs_get_typed_array(Item buffer_item) {
    if (!js_is_typed_array(buffer_item)) return NULL;
    Map* map = buffer_item.map;
    if (!map) return NULL;
    return (JsTypedArray*)map->data;
}

static Item fs_throw_empty_read_buffer(JsTypedArray* ta) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "The argument 'buffer' is empty and cannot be written. Received %s(0) []",
             fs_typed_array_name(ta));
    return js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
}

// =============================================================================
// Stats prototype — provides isFile(), isDirectory(), etc. methods
// =============================================================================
static Item stats_proto = {0};

// Stats method: checks mode bits via js_get_this().__mode
extern "C" Item js_get_this(void);
extern "C" Item js_date_new_from(Item value);
extern "C" int js_check_exception(void);
extern "C" Item js_clear_exception(void);
extern "C" Item js_readable_new(Item opts);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" Item js_stream_destroy(Item self, Item err);
extern Item js_promise_resolve(Item value);
extern Item js_promise_reject(Item reason);
extern "C" Item bigint_from_int64(int64_t val);
extern "C" Item bigint_from_string(const char* str, int len);
extern "C" int bigint_cmp(Item a, Item b);
extern Item js_make_number(double d);
extern "C" Item js_buffer_alloc(Item size_item, Item fill_item);
extern "C" int64_t bigint_to_int64(Item bi);
extern "C" Item js_array_is_array(Item value);
extern "C" Item js_fs_openSync(Item path_item, Item flags_item, Item mode_item);
extern "C" Item js_fs_closeSync(Item fd_item);

static Item fs_namespace = {0};

static bool fs_is_nullish(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_UNDEFINED || type == LMD_TYPE_NULL;
}

static bool fs_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static bool fs_is_options_object(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP || js_is_typed_array(value)) return false;
    Item is_array = js_array_is_array(value);
    return is_array.item != b2it(true);
}

static bool fs_read_position_to_int64(Item position_item, int64_t* out_position) {
    if (fs_is_nullish(position_item)) {
        *out_position = -1;
        return true;
    }
    if (fs_is_bigint(position_item)) {
        // BigInt host positions must be range-checked before narrowing; bigint_to_int64 clamps oversized values.
        if (bigint_cmp(position_item, bigint_from_int64(-1)) < 0 ||
            bigint_cmp(position_item, bigint_from_int64(INT64_MAX)) > 0) {
            js_throw_out_of_range("position", ">= -1 && <= 9223372036854775807", position_item);
            return false;
        }
        int64_t position = bigint_to_int64(position_item);
        *out_position = position;
        return true;
    }
    return fs_validate_int_range(position_item, "position", -1, INT64_MAX, out_position);
}

static bool fs_read_parse_options(Item options_item, Item fallback_buffer,
                                  Item* out_buffer, Item* out_offset,
                                  Item* out_length, Item* out_position) {
    Item buffer = fallback_buffer;
    Item offset = make_js_undefined();
    Item length = make_js_undefined();
    Item position = make_js_undefined();

    if (fs_is_options_object(options_item)) {
        Item option_buffer = js_property_get(options_item, make_string_item("buffer"));
        if (!fs_is_nullish(option_buffer)) buffer = option_buffer;
        offset = js_property_get(options_item, make_string_item("offset"));
        length = js_property_get(options_item, make_string_item("length"));
        position = js_property_get(options_item, make_string_item("position"));
    }

    if (fs_is_nullish(buffer)) {
        buffer = js_buffer_alloc((Item){.item = i2it(16384)}, make_js_undefined());
    }
    if (fs_is_nullish(offset)) offset = (Item){.item = i2it(0)};
    if (fs_is_nullish(length) && fs_get_typed_array(buffer)) {
        int byte_length = js_typed_array_byte_length(buffer);
        int64_t offset_value = 0;
        if (get_type_id(offset) == LMD_TYPE_INT) {
            offset_value = it2i(offset);
        } else if (get_type_id(offset) == LMD_TYPE_FLOAT) {
            offset_value = (int64_t)it2d(offset);
        }
        length = (Item){.item = i2it(byte_length - offset_value)};
    }
    *out_buffer = buffer;
    *out_offset = offset;
    *out_length = length;
    *out_position = position;
    return true;
}

extern "C" Item js_stats_isFile() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFREG)};
}
extern "C" Item js_stats_isDirectory() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFDIR)};
}
extern "C" Item js_stats_isSymbolicLink() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFLNK)};
}
extern "C" Item js_stats_isBlockDevice() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFBLK)};
}
extern "C" Item js_stats_isCharacterDevice() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFCHR)};
}
extern "C" Item js_stats_isFIFO() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFIFO)};
}
extern "C" Item js_stats_isSocket() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFSOCK)};
}

static Item get_stats_proto() {
    if (stats_proto.item != 0) return stats_proto;
    stats_proto = js_new_object();
    js_property_set(stats_proto, make_string_item("isFile"), js_new_function((void*)js_stats_isFile, 0));
    js_property_set(stats_proto, make_string_item("isDirectory"), js_new_function((void*)js_stats_isDirectory, 0));
    js_property_set(stats_proto, make_string_item("isSymbolicLink"), js_new_function((void*)js_stats_isSymbolicLink, 0));
    js_property_set(stats_proto, make_string_item("isBlockDevice"), js_new_function((void*)js_stats_isBlockDevice, 0));
    js_property_set(stats_proto, make_string_item("isCharacterDevice"), js_new_function((void*)js_stats_isCharacterDevice, 0));
    js_property_set(stats_proto, make_string_item("isFIFO"), js_new_function((void*)js_stats_isFIFO, 0));
    js_property_set(stats_proto, make_string_item("isSocket"), js_new_function((void*)js_stats_isSocket, 0));
    return stats_proto;
}

static Item fs_bigint_from_uint64(uint64_t value) {
    if (value <= (uint64_t)INT64_MAX) return bigint_from_int64((int64_t)value);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return bigint_from_string(buf, len);
}

static Item fs_stats_uint64_value(uint64_t value, bool bigint) {
    if (bigint) return fs_bigint_from_uint64(value);
    return js_make_number((double)value);
}

static Item fs_stats_int64_value(int64_t value, bool bigint) {
    if (bigint) return bigint_from_int64(value);
    return js_make_number((double)value);
}

static Item fs_stats_time_ms_value(const uv_timespec_t& ts, bool bigint) {
    if (bigint) {
        int64_t ms = (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
        return bigint_from_int64(ms);
    }
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
    return js_make_number(ms);
}

static bool fs_options_bigint(Item options_item);

// Helper: build a Stats object from uv_stat_t
static Item make_stats_object(const uv_stat_t* st, bool bigint) {
    Item obj = js_new_object();
    
    // Store mode for isFile/isDirectory/etc methods
    js_property_set(obj, make_string_item("__mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    // stats fields have an explicit Node API face: numbers by default, BigInt only when requested.
    js_property_set(obj, make_string_item("mode"), fs_stats_uint64_value((uint64_t)st->st_mode, bigint));
    js_property_set(obj, make_string_item("size"), fs_stats_int64_value((int64_t)st->st_size, bigint));
    js_property_set(obj, make_string_item("uid"), fs_stats_uint64_value((uint64_t)st->st_uid, bigint));
    js_property_set(obj, make_string_item("gid"), fs_stats_uint64_value((uint64_t)st->st_gid, bigint));
    js_property_set(obj, make_string_item("nlink"), fs_stats_uint64_value((uint64_t)st->st_nlink, bigint));
    js_property_set(obj, make_string_item("ino"), fs_stats_uint64_value((uint64_t)st->st_ino, bigint));
    js_property_set(obj, make_string_item("dev"), fs_stats_uint64_value((uint64_t)st->st_dev, bigint));
    js_property_set(obj, make_string_item("rdev"), fs_stats_uint64_value((uint64_t)st->st_rdev, bigint));
    js_property_set(obj, make_string_item("blksize"), fs_stats_uint64_value((uint64_t)st->st_blksize, bigint));
    js_property_set(obj, make_string_item("blocks"), fs_stats_int64_value((int64_t)st->st_blocks, bigint));

    // Time properties in milliseconds
    auto set_time = [&](const char* name, const uv_timespec_t& ts) {
        js_property_set(obj, make_string_item(name), fs_stats_time_ms_value(ts, bigint));
    };
    set_time("atimeMs", st->st_atim);
    set_time("mtimeMs", st->st_mtim);
    set_time("ctimeMs", st->st_ctim);
    set_time("birthtimeMs", st->st_birthtim);

    // Time properties as Date objects (for stats.mtime instanceof Date, etc.)
    auto set_date = [&](const char* name, const uv_timespec_t& ts) {
        double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        Item ms_item;
        ms_item = lambda_float_ptr_to_item(fp);
        js_property_set(obj, make_string_item(name), js_date_new_from(ms_item));
    };
    set_date("atime", st->st_atim);
    set_date("mtime", st->st_mtim);
    set_date("ctime", st->st_ctim);
    set_date("birthtime", st->st_birthtim);

    // Set __proto__ so methods resolve via prototype chain lookup
    js_property_set(obj, make_string_item("__proto__"), get_stats_proto());
    
    return obj;
}

// =============================================================================
// Synchronous File Operations
// =============================================================================

static Item js_fs_read_file_buffer(const char* path);

// fs.readFileSync(path[, encoding])
// Returns file contents as a string (assumes UTF-8)
extern "C" Item js_fs_readFileSync(Item path_item, Item encoding_item) {
    if (!fs_validate_encoding_options(encoding_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);
    if (fs_read_file_should_return_buffer(encoding_item)) {
        return js_fs_read_file_buffer(path);
    }

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        return js_throw_system_error(fd, "open", path);
    }

    // stat to get file size
    uv_fs_t stat_req;
    fs_maybe_call_internal_fstat_hook(fd);
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        log_error("fs: readFileSync: cannot stat '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);

    // read file contents
    char* data = (char*)mem_alloc(file_size + 1, MEM_CAT_JS_RUNTIME);
    if (!data) {
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init(data, (unsigned int)file_size);
    uv_fs_t read_req;
    int bytes_read = (int)uv_fs_read(NULL, &read_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&read_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (bytes_read < 0) {
        mem_free(data);
        log_error("fs: readFileSync: read error for '%s': %s", path, uv_strerror(bytes_read));
        return ItemNull;
    }

    data[bytes_read] = '\0';
    Item result = make_string_item(data, bytes_read);
    mem_free(data);
    return result;
}

static Item js_fs_read_file_buffer(const char* path) {
    if (!path) return js_throw_invalid_arg_type("path", "string", ItemNull);
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return js_throw_system_error(fd, "open", path);

    uv_fs_t stat_req;
    fs_maybe_call_internal_fstat_hook(fd);
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        return js_throw_system_error(r, "fstat", path);
    }

    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);

    char* data = (char*)mem_alloc(file_size > 0 ? file_size : 1, MEM_CAT_JS_RUNTIME);
    if (!data) {
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init(data, (unsigned int)file_size);
    uv_fs_t read_req;
    int bytes_read = (int)uv_fs_read(NULL, &read_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&read_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (bytes_read < 0) {
        mem_free(data);
        return js_throw_system_error(bytes_read, "read", path);
    }

    Item chunk = fs_buffer_from_bytes(data, bytes_read);
    mem_free(data);
    return chunk;
}

static Item js_fs_readstream_drain(Item stream) {
    Item drained = js_property_get(stream, make_string_item("__readstream_drained__"));
    if (drained.item != 0 && get_type_id(drained) == LMD_TYPE_BOOL && it2b(drained)) {
        return make_js_undefined();
    }

    Item data_cb = js_property_get(stream, make_string_item("__readstream_data_cb__"));
    Item end_cb = js_property_get(stream, make_string_item("__readstream_end_cb__"));
    Item close_cb = js_property_get(stream, make_string_item("__readstream_close_cb__"));
    bool has_data = get_type_id(data_cb) == LMD_TYPE_FUNC;
    bool has_terminal = get_type_id(end_cb) == LMD_TYPE_FUNC || get_type_id(close_cb) == LMD_TYPE_FUNC;
    if (!has_data || !has_terminal) return make_js_undefined();

    Item path_item = js_property_get(stream, make_string_item("__readstream_path__"));
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    Item chunk = js_fs_read_file_buffer(path);
    if (js_check_exception()) return ItemNull;

    js_property_set(stream, make_string_item("__readstream_drained__"), (Item){.item = b2it(true)});
    Item data_args[1] = {chunk};
    js_call_function(data_cb, stream, data_args, 1);
    if (js_check_exception()) return ItemNull;

    if (get_type_id(end_cb) == LMD_TYPE_FUNC) {
        js_call_function(end_cb, stream, NULL, 0);
        if (js_check_exception()) return ItemNull;
    }
    if (get_type_id(close_cb) == LMD_TYPE_FUNC) {
        js_call_function(close_cb, stream, NULL, 0);
        if (js_check_exception()) return ItemNull;
    }
    return make_js_undefined();
}

extern "C" Item js_fs_readstream_on(Item event_item, Item callback_item) {
    Item stream = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING || get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return stream;
    }

    String* event = it2s(event_item);
    if (event->len == 4 && memcmp(event->chars, "data", 4) == 0) {
        js_property_set(stream, make_string_item("__readstream_data_cb__"), callback_item);
    } else if (event->len == 3 && memcmp(event->chars, "end", 3) == 0) {
        js_property_set(stream, make_string_item("__readstream_end_cb__"), callback_item);
    } else if (event->len == 5 && memcmp(event->chars, "close", 5) == 0) {
        js_property_set(stream, make_string_item("__readstream_close_cb__"), callback_item);
    }
    js_fs_readstream_drain(stream);
    return stream;
}

extern "C" Item js_fs_readstream_pipe(Item dest_item) {
    Item stream = js_get_this();
    Item path_item = js_property_get(stream, make_string_item("__readstream_path__"));
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    Item chunk = js_fs_read_file_buffer(path);
    if (js_check_exception()) return ItemNull;

    Item write_fn = js_property_get(dest_item, make_string_item("write"));
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        Item args[1] = {chunk};
        js_call_function(write_fn, dest_item, args, 1);
        if (js_check_exception()) return ItemNull;
    }

    Item end_fn = js_property_get(dest_item, make_string_item("end"));
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        js_call_function(end_fn, dest_item, NULL, 0);
        if (js_check_exception()) return ItemNull;
    }

    js_property_set(stream, make_string_item("__readstream_drained__"), (Item){.item = b2it(true)});
    return dest_item;
}

static Item js_fs_readstream_close(Item callback_item) {
    Item stream = js_get_this();
    if (get_type_id(callback_item) == LMD_TYPE_FUNC) {
        js_property_set(stream, make_string_item("__destroy_callback__"), callback_item);
    }
    return js_stream_destroy(stream, make_js_undefined());
}

extern "C" Item js_fs_createReadStream(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item auto_close = js_property_get(options_item, make_string_item("autoClose"));
        if (get_type_id(auto_close) == LMD_TYPE_BOOL && !it2b(auto_close)) {
            js_property_set(options_item, make_string_item("autoDestroy"), (Item){.item = b2it(false)});
        }
    }

    Item stream = js_readable_new(options_item);
    if (stream.item == 0) return ItemNull;
    js_property_set(stream, make_string_item("__readstream_path__"), path_item);
    js_property_set(stream, make_string_item("__readstream_drained__"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("close"), js_new_function((void*)js_fs_readstream_close, 1));
    // keep Readable.on() intact so late 'data' listeners switch buffered
    // fs streams into flowing mode; the fs-only drain path missed data-only consumers.
    js_property_set(stream, make_string_item("pipe"), js_new_function((void*)js_fs_readstream_pipe, 1));

    Item chunk = js_fs_read_file_buffer(path);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(stream, err);
        return stream;
    }

    // Buffer the whole fixture now; generic Readable listeners must own the
    // later data flow so pause/resume and data-only consumers see the chunk.
    js_readable_push(stream, chunk);
    js_readable_push(stream, ItemNull);
    return stream;
}

static bool fs_item_bytes(Item item, const char** data, int* len) {
    if (!data || !len) return false;
    *data = NULL;
    *len = 0;
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
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

static void js_fs_writestream_call_write_hook(Item stream, Item fd_item);
static void js_fs_writestream_schedule_open(Item stream);
static void js_fs_writestream_schedule_drain(Item stream);
static Item js_fs_writestream_write_after_end_error(void);

static Item js_fs_writestream_write(Item chunk_item, Item callback_item) {
    Item stream = js_get_this();
    Item finished = js_property_get(stream, make_string_item("__writestream_finished__"));
    if (get_type_id(finished) == LMD_TYPE_BOOL && it2b(finished)) {
        if (get_type_id(callback_item) == LMD_TYPE_FUNC) {
            Item err = js_fs_writestream_write_after_end_error();
            js_call_function(callback_item, stream, &err, 1);
        }
        return (Item){.item = b2it(false)};
    }

    const char* data = NULL;
    int len = 0;
    if (!fs_item_bytes(chunk_item, &data, &len)) return (Item){.item = b2it(false)};

    Item fd_item = js_property_get(stream, make_string_item("fd"));
    int fd = 0;
    bool close_after_write = false;
    if (get_type_id(fd_item) == LMD_TYPE_INT) {
        fd = (int)it2i(fd_item);
    } else {
        Item path_item = js_property_get(stream, make_string_item("__writestream_path__"));
        char path_buf[1024];
        const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
        if (!path) return (Item){.item = b2it(false)};
        if (!js_permission_has_fs_write(path)) return (Item){.item = b2it(false)};

        uv_fs_t open_req;
        fd = uv_fs_open(NULL, &open_req, path,
                        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND, 0644, NULL);
        uv_fs_req_cleanup(&open_req);
        if (fd < 0) return (Item){.item = b2it(false)};
        close_after_write = true;
    }

    if (len > 0) {
        int64_t position = -1;
        Item position_item = js_property_get(stream, make_string_item("__writestream_position__"));
        if (get_type_id(position_item) == LMD_TYPE_INT) {
            position = it2i(position_item);
        }
        uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)len);
        uv_fs_t write_req;
        uv_fs_write(NULL, &write_req, fd, &buf, 1, position, NULL);
        uv_fs_req_cleanup(&write_req);
        if (position >= 0) {
            js_property_set(stream, make_string_item("__writestream_position__"),
                            (Item){.item = i2it(position + len)});
        }
    }
    js_fs_writestream_call_write_hook(stream, fd_item);
    Item bytes_written = js_property_get(stream, make_string_item("bytesWritten"));
    int64_t total = 0;
    if (get_type_id(bytes_written) == LMD_TYPE_INT) {
        total = it2i(bytes_written);
    } else if (get_type_id(bytes_written) == LMD_TYPE_FLOAT) {
        total = (int64_t)it2d(bytes_written);
    }
    total += len;
    // WriteStream.bytesWritten is a Node JS Number surface, independent of host counter width.
    js_property_set(stream, make_string_item("bytesWritten"), js_make_number((double)total));

    Item hwm_item = js_property_get(stream, make_string_item("__writestream_high_water_mark__"));
    int64_t high_water_mark = (get_type_id(hwm_item) == LMD_TYPE_INT) ? it2i(hwm_item) : 16384;
    bool needs_drain = high_water_mark > 0 && total >= high_water_mark;
    if (needs_drain) js_fs_writestream_schedule_drain(stream);

    if (close_after_write) {
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
    }
    return (Item){.item = b2it(!needs_drain)};
}

static void js_fs_writestream_emit_finish(Item stream) {
    Item cb = js_property_get(stream, make_string_item("__writestream_finish_cb__"));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, stream, NULL, 0);
        js_microtask_flush();
    }
}

static void js_fs_writestream_emit_close(Item stream) {
    Item cb = js_property_get(stream, make_string_item("__writestream_close_cb__"));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, stream, NULL, 0);
        js_microtask_flush();
    }
}

static Item js_fs_writestream_emit_open_tick(Item stream) {
    js_property_set(stream, make_string_item("__writestream_opened__"), (Item){.item = b2it(true)});
    Item cb = js_property_get(stream, make_string_item("__writestream_open_cb__"));
    Item fd_item = js_property_get(stream, make_string_item("fd"));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = {fd_item};
        js_call_function(cb, stream, args, 1);
        js_microtask_flush();
    }
    return make_js_undefined();
}

static void js_fs_writestream_schedule_open(Item stream) {
    Item bound_args[1] = {stream};
    Item tick = js_bind_function(js_new_function((void*)js_fs_writestream_emit_open_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static Item js_fs_writestream_emit_drain_tick(Item stream) {
    js_property_set(stream, make_string_item("__writestream_drain_pending__"), (Item){.item = b2it(false)});
    Item cb = js_property_get(stream, make_string_item("__writestream_drain_cb__"));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, stream, NULL, 0);
        js_microtask_flush();
    }
    return make_js_undefined();
}

static void js_fs_writestream_schedule_drain(Item stream) {
    Item pending = js_property_get(stream, make_string_item("__writestream_drain_pending__"));
    if (get_type_id(pending) == LMD_TYPE_BOOL && it2b(pending)) return;
    js_property_set(stream, make_string_item("__writestream_drain_pending__"), (Item){.item = b2it(true)});
    Item bound_args[1] = {stream};
    Item tick = js_bind_function(js_new_function((void*)js_fs_writestream_emit_drain_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static Item js_fs_writestream_write_after_end_error(void) {
    Item err = js_new_error(make_string_item("write after end"));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_STREAM_WRITE_AFTER_END"));
    return err;
}

static Item js_fs_writestream_close_noop(Item err_item) {
    (void)err_item;
    return make_js_undefined();
}

static Item js_fs_writestream_open_hook_cb(Item err_item, Item fd_item) {
    (void)err_item;
    if (get_type_id(fd_item) == LMD_TYPE_INT) {
        js_fs_closeSync(fd_item);
    }
    return make_js_undefined();
}

static Item js_fs_writestream_io_hook_cb(Item err_item, Item value_item, Item data_item) {
    (void)err_item;
    (void)value_item;
    (void)data_item;
    return make_js_undefined();
}

static Item js_fs_writestream_hooks(Item stream) {
    Item hooks = js_property_get(stream, make_string_item("__writestream_fs_hooks__"));
    return get_type_id(hooks) == LMD_TYPE_MAP ? hooks : make_js_undefined();
}

static void js_fs_writestream_call_open_hook(Item stream, Item path_item, Item flags, Item mode) {
    Item hooks = js_fs_writestream_hooks(stream);
    if (get_type_id(hooks) != LMD_TYPE_MAP) return;
    Item open_fn = js_property_get(hooks, make_string_item("open"));
    if (get_type_id(open_fn) != LMD_TYPE_FUNC) return;
    Item cb = js_new_function((void*)js_fs_writestream_open_hook_cb, 2);
    Item args[4] = {path_item, flags, mode, cb};
    js_call_function(open_fn, hooks, args, 4);
}

static void js_fs_writestream_call_write_hook(Item stream, Item fd_item) {
    Item hooks = js_fs_writestream_hooks(stream);
    if (get_type_id(hooks) != LMD_TYPE_MAP || get_type_id(fd_item) != LMD_TYPE_INT) return;
    Item writev_fn = js_property_get(hooks, make_string_item("writev"));
    if (get_type_id(writev_fn) == LMD_TYPE_FUNC) return;
    Item write_fn = js_property_get(hooks, make_string_item("write"));
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) return;
    Item cb = js_new_function((void*)js_fs_writestream_io_hook_cb, 3);
    Item args[4] = {fd_item, make_string_item("", 0), (Item){.item = i2it(0)}, cb};
    js_call_function(write_fn, hooks, args, 4);
}

static void js_fs_writestream_call_writev_hook(Item stream) {
    Item hooks = js_fs_writestream_hooks(stream);
    if (get_type_id(hooks) != LMD_TYPE_MAP) return;
    Item writev_fn = js_property_get(hooks, make_string_item("writev"));
    if (get_type_id(writev_fn) != LMD_TYPE_FUNC) return;
    Item fd_item = js_property_get(stream, make_string_item("fd"));
    if (get_type_id(fd_item) != LMD_TYPE_INT) return;
    Item cb = js_new_function((void*)js_fs_writestream_io_hook_cb, 3);
    Item args[3] = {fd_item, js_array_new(0), cb};
    js_call_function(writev_fn, hooks, args, 3);
}

static void js_fs_writestream_close_if_needed(Item stream) {
    Item fd_item = js_property_get(stream, make_string_item("fd"));
    if (get_type_id(fd_item) != LMD_TYPE_INT) return;

    Item auto_close = js_property_get(stream, make_string_item("__writestream_auto_close__"));
    if (get_type_id(auto_close) == LMD_TYPE_BOOL && !it2b(auto_close)) return;

    Item hooks = js_fs_writestream_hooks(stream);
    Item close_fn = get_type_id(hooks) == LMD_TYPE_MAP ?
        js_property_get(hooks, make_string_item("close")) :
        js_property_get(fs_namespace, make_string_item("close"));
    if (get_type_id(close_fn) == LMD_TYPE_FUNC) {
        Item noop = js_new_function((void*)js_fs_writestream_close_noop, 1);
        Item args[2] = {fd_item, noop};
        js_call_function(close_fn, fs_namespace, args, 2);
    } else {
        js_fs_closeSync(fd_item);
    }
    js_property_set(stream, make_string_item("fd"), ItemNull);
    js_property_set(stream, make_string_item("closed"), (Item){.item = b2it(true)});
    js_fs_writestream_emit_close(stream);
}

static Item js_fs_writestream_end(Item chunk_item) {
    Item stream = js_get_this();
    TypeId chunk_type = get_type_id(chunk_item);
    if (chunk_type != LMD_TYPE_UNDEFINED && chunk_item.item != ITEM_NULL) {
        js_fs_writestream_write(chunk_item, make_js_undefined());
    }
    js_fs_writestream_call_writev_hook(stream);
    js_property_set(stream, make_string_item("__writestream_finished__"), (Item){.item = b2it(true)});
    js_fs_writestream_emit_finish(stream);
    js_fs_writestream_close_if_needed(stream);
    return stream;
}

static Item js_fs_writestream_on(Item event_item, Item callback_item) {
    Item stream = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING || get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return stream;
    }
    String* event = it2s(event_item);
    if (event->len == 6 && memcmp(event->chars, "finish", 6) == 0) {
        js_property_set(stream, make_string_item("__writestream_finish_cb__"), callback_item);
        Item finished = js_property_get(stream, make_string_item("__writestream_finished__"));
        if (get_type_id(finished) == LMD_TYPE_BOOL && it2b(finished)) {
            js_fs_writestream_emit_finish(stream);
        }
    } else if (event->len == 4 && memcmp(event->chars, "open", 4) == 0) {
        js_property_set(stream, make_string_item("__writestream_open_cb__"), callback_item);
        Item opened = js_property_get(stream, make_string_item("__writestream_opened__"));
        if (get_type_id(opened) == LMD_TYPE_BOOL && it2b(opened)) {
            Item fd_item = js_property_get(stream, make_string_item("fd"));
            Item args[1] = {fd_item};
            js_call_function(callback_item, stream, args, 1);
        }
    } else if (event->len == 5 && memcmp(event->chars, "drain", 5) == 0) {
        js_property_set(stream, make_string_item("__writestream_drain_cb__"), callback_item);
    } else if (event->len == 5 && memcmp(event->chars, "error", 5) == 0) {
        js_property_set(stream, make_string_item("__writestream_error_cb__"), callback_item);
    } else if (event->len == 5 && memcmp(event->chars, "close", 5) == 0) {
        js_property_set(stream, make_string_item("__writestream_close_cb__"), callback_item);
        Item closed = js_property_get(stream, make_string_item("closed"));
        if (get_type_id(closed) == LMD_TYPE_BOOL && it2b(closed)) {
            js_fs_writestream_emit_close(stream);
        }
    }
    return stream;
}

extern "C" Item js_fs_createWriteStream(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    Item stream = js_new_object();
    js_property_set(stream, make_string_item("__writestream_path__"), path_item);
    js_property_set(stream, make_string_item("__writestream_finished__"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("__writestream_auto_close__"), (Item){.item = b2it(true)});
    js_property_set(stream, make_string_item("__writestream_opened__"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("__writestream_drain_pending__"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("__writestream_high_water_mark__"), (Item){.item = i2it(16384)});
    js_property_set(stream, make_string_item("closed"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("bytesWritten"), js_make_number(0.0));
    js_property_set(stream, make_string_item("fd"), ItemNull);

    Item flags = make_string_item("w");
    Item mode = make_js_undefined();
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item flags_opt = js_property_get(options_item, make_string_item("flags"));
        if (get_type_id(flags_opt) == LMD_TYPE_STRING || get_type_id(flags_opt) == LMD_TYPE_INT) {
            flags = flags_opt;
        }
        Item mode_opt = js_property_get(options_item, make_string_item("mode"));
        if (!fs_is_nullish(mode_opt)) {
            uint32_t parsed_mode = 0;
            if (!fs_validate_mode(mode_opt, &parsed_mode)) return ItemNull;
            mode = (Item){.item = i2it((int64_t)parsed_mode)};
        }
        Item auto_close = js_property_get(options_item, make_string_item("autoClose"));
        if (get_type_id(auto_close) == LMD_TYPE_BOOL) {
            js_property_set(stream, make_string_item("__writestream_auto_close__"), auto_close);
        }
        Item hwm = js_property_get(options_item, make_string_item("highWaterMark"));
        if (!fs_is_nullish(hwm)) {
            int64_t parsed_hwm = 0;
            // JS numeric literals are float-backed Numbers; fs internals keep HWM as an integral counter.
            if (!fs_validate_int_range(hwm, "options.highWaterMark", 0, 9223372036854775807LL, &parsed_hwm)) return ItemNull;
            js_property_set(stream, make_string_item("__writestream_high_water_mark__"),
                            (Item){.item = i2it(parsed_hwm)});
        }
        Item hooks = js_property_get(options_item, make_string_item("fs"));
        if (get_type_id(hooks) == LMD_TYPE_MAP) {
            js_property_set(stream, make_string_item("__writestream_fs_hooks__"), hooks);
        }
        Item fd_opt = js_property_get(options_item, make_string_item("fd"));
        if (!fs_is_nullish(fd_opt)) {
            int parsed_fd = 0;
            if (!fs_validate_fd(fd_opt, &parsed_fd)) return ItemNull;
            js_property_set(stream, make_string_item("fd"), (Item){.item = i2it(parsed_fd)});
        }
        Item start_opt = js_property_get(options_item, make_string_item("start"));
        if (!fs_is_nullish(start_opt)) {
            int64_t parsed_start = 0;
            if (!fs_validate_int_range(start_opt, "options.start", 0, 9223372036854775807LL, &parsed_start)) return ItemNull;
            js_property_set(stream, make_string_item("__writestream_position__"),
                            (Item){.item = i2it(parsed_start)});
        }
    }

    Item fd_item = js_property_get(stream, make_string_item("fd"));
    if (get_type_id(fd_item) != LMD_TYPE_INT) {
        char path_buf[1024];
        const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
        if (!path) return ItemNull;
        if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);
        fd_item = js_fs_openSync(path_item, flags, mode);
        if (get_type_id(fd_item) == LMD_TYPE_INT) {
            js_property_set(stream, make_string_item("fd"), fd_item);
        }
    }
    js_fs_writestream_call_open_hook(stream, path_item, flags, mode);
    if (get_type_id(js_property_get(stream, make_string_item("fd"))) == LMD_TYPE_INT) {
        js_fs_writestream_schedule_open(stream);
    }

    js_property_set(stream, make_string_item("write"),
                    js_new_function((void*)js_fs_writestream_write, 2));
    js_property_set(stream, make_string_item("end"),
                    js_new_function((void*)js_fs_writestream_end, 1));
    js_property_set(stream, make_string_item("on"),
                    js_new_function((void*)js_fs_writestream_on, 2));
    return stream;
}

// fs.writeFileSync(path, data)
extern "C" Item js_fs_writeFileSync(Item path_item, Item data_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    // convert data to string
    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC, 0644, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        log_error("fs: writeFileSync: cannot open '%s': %s", path, uv_strerror(fd));
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init((char*)str->chars, (unsigned int)str->len);
    uv_fs_t write_req;
    int written = (int)uv_fs_write(NULL, &write_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&write_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (written < 0) {
        log_error("fs: writeFileSync: write error for '%s': %s", path, uv_strerror(written));
        return ItemNull;
    }

    return make_js_undefined();
}

// fs.existsSync(path) → bool
extern "C" Item js_fs_existsSync(Item path_item) {
    if (get_type_id(path_item) == LMD_TYPE_STRING && fs_string_has_nul(it2s(path_item))) {
        return (Item){.item = b2it(false)};
    }
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return (Item){.item = b2it(false)};

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    return (Item){.item = b2it(r == 0)};
}

// fs.unlinkSync(path) — delete a file
extern "C" Item js_fs_unlinkSync(Item path_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    uv_fs_t req;
    int r = uv_fs_unlink(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: unlinkSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.mkdirSync(path[, options])
extern "C" Item js_fs_mkdirSync(Item path_item, Item options) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    int mode = 0777;
    bool recursive = false;

    // options can be an integer (mode) or an object { recursive, mode }
    if (get_type_id(options) == LMD_TYPE_INT) {
        mode = (int)it2i(options);
    } else if (get_type_id(options) == LMD_TYPE_FLOAT) {
        mode = (int)it2d(options);
    } else if (get_type_id(options) == LMD_TYPE_MAP) {
        Item mode_val = js_property_get(options, make_string_item("mode"));
        if (get_type_id(mode_val) == LMD_TYPE_INT) mode = (int)it2i(mode_val);
        else if (get_type_id(mode_val) == LMD_TYPE_FLOAT) mode = (int)it2d(mode_val);
        Item rec_val = js_property_get(options, make_string_item("recursive"));
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        // create parent directories as needed
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", path);
        for (char* p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                uv_fs_t req;
                uv_fs_mkdir(NULL, &req, tmp, mode, NULL);
                uv_fs_req_cleanup(&req);
                *p = '/';
            }
        }
    }

    uv_fs_t req;
    int r = uv_fs_mkdir(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);

    if (recursive) {
        // for recursive, return the first directory created (or undefined)
        return make_js_undefined();
    }
    if (r < 0 && r != UV_EEXIST) {
        log_error("fs: mkdirSync: '%s': %s", path, uv_strerror(r));
        return js_new_error(make_string_item(uv_strerror(r)));
    }
    return make_js_undefined();
}

// fs.mkdir(path[, options], callback) — async version
extern "C" Item js_fs_mkdir_async(Item path_item, Item options_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = options_or_cb;

    // if second arg is a function, it's the callback (no options)
    if (get_type_id(options_or_cb) == LMD_TYPE_FUNC) {
        callback = options_or_cb;
        options = ItemNull;
    }

    // perform the operation synchronously
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) {
        return fs_permission_callback_error(callback, "FileSystemWrite", path, NULL);
    }

    int mode = 0777;
    bool recursive = false;

    if (get_type_id(options) == LMD_TYPE_INT) {
        mode = (int)it2i(options);
    } else if (get_type_id(options) == LMD_TYPE_FLOAT) {
        mode = (int)it2d(options);
    } else if (get_type_id(options) == LMD_TYPE_MAP) {
        Item mode_val = js_property_get(options, make_string_item("mode"));
        if (get_type_id(mode_val) == LMD_TYPE_INT) mode = (int)it2i(mode_val);
        else if (get_type_id(mode_val) == LMD_TYPE_FLOAT) mode = (int)it2d(mode_val);
        Item rec_val = js_property_get(options, make_string_item("recursive"));
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", path);
        for (char* p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                uv_fs_t req;
                uv_fs_mkdir(NULL, &req, tmp, mode, NULL);
                uv_fs_req_cleanup(&req);
                *p = '/';
            }
        }
    }

    uv_fs_t req;
    int r = uv_fs_mkdir(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0 && !(recursive && r == UV_EEXIST)) {
            Item err = js_new_error(make_string_item(uv_strerror(r)));
            js_property_set(err, make_string_item("code"),
                make_string_item(r == UV_EEXIST ? "EEXIST" :
                                 r == UV_ENOENT ? "ENOENT" :
                                 r == UV_EACCES ? "EACCES" : "ERR_FS"));
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

// fs.rmdirSync(path)
extern "C" Item js_fs_rmdirSync(Item path_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    uv_fs_t req;
    int r = uv_fs_rmdir(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: rmdirSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.renameSync(oldPath, newPath)
extern "C" Item js_fs_renameSync(Item old_path_item, Item new_path_item) {
    char old_buf[1024], new_buf[1024];
    const char* old_path = fs_path_to_cstr(old_path_item, "oldPath", old_buf, sizeof(old_buf));
    if (!old_path) return ItemNull;
    const char* new_path = fs_path_to_cstr(new_path_item, "newPath", new_buf, sizeof(new_buf));
    if (!new_path) return ItemNull;
    if (!js_permission_has_fs_write(old_path)) return js_permission_check_fs_write(old_path);
    if (!js_permission_has_fs_write(new_path)) return js_permission_check_fs_write(new_path);

    uv_fs_t req;
    int r = uv_fs_rename(NULL, &req, old_path, new_path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: renameSync: '%s' -> '%s': %s", old_path, new_path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.readdirSync(path) → Array<string>
extern "C" Item js_fs_readdirSync(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int n = uv_fs_scandir(NULL, &req, path, 0, NULL);
    if (n < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: readdirSync: '%s': %s", path, uv_strerror(n));
        return ItemNull;
    }

    Item arr = js_array_new(0);
    uv_dirent_t dirent;
    while (uv_fs_scandir_next(&req, &dirent) != UV_EOF) {
        js_array_push(arr, make_string_item(dirent.name));
    }

    uv_fs_req_cleanup(&req);
    return arr;
}

// fs.statSync(path[, options]) → Stats object with isFile(), isDirectory(), etc.
extern "C" Item js_fs_statSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "stat", path);
    }

    Item obj = make_stats_object(&req.statbuf, fs_options_bigint(options_item));
    uv_fs_req_cleanup(&req);
    return obj;
}

static bool fs_options_bigint(Item options_item) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return false;
    Item bigint = js_property_get(options_item, make_string_item("bigint"));
    return js_is_truthy(bigint);
}

static Item fs_statfs_number(uint64_t value, bool bigint) {
    if (bigint) return fs_bigint_from_uint64(value);
    return js_make_number((double)value);
}

static Item make_statfs_object(uint64_t type, uint64_t bsize, uint64_t frsize,
                               uint64_t blocks, uint64_t bfree, uint64_t bavail,
                               uint64_t files, uint64_t ffree, bool bigint) {
    Item obj = js_new_object();
    js_property_set(obj, make_string_item("type"), fs_statfs_number(type, bigint));
    js_property_set(obj, make_string_item("bsize"), fs_statfs_number(bsize, bigint));
    js_property_set(obj, make_string_item("frsize"), fs_statfs_number(frsize, bigint));
    js_property_set(obj, make_string_item("blocks"), fs_statfs_number(blocks, bigint));
    js_property_set(obj, make_string_item("bfree"), fs_statfs_number(bfree, bigint));
    js_property_set(obj, make_string_item("bavail"), fs_statfs_number(bavail, bigint));
    js_property_set(obj, make_string_item("files"), fs_statfs_number(files, bigint));
    js_property_set(obj, make_string_item("ffree"), fs_statfs_number(ffree, bigint));
    return obj;
}

// fs.statfsSync(path[, options])
extern "C" Item js_fs_statfsSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    bool bigint = fs_options_bigint(options_item);
#ifdef _WIN32
    return make_statfs_object(0, 4096, 4096, 0, 0, 0, 0, 0, bigint);
#else
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0) {
        return js_throw_system_error(uv_translate_sys_error(errno), "statfs", path);
    }
    uint64_t frsize = vfs.f_frsize ? (uint64_t)vfs.f_frsize : (uint64_t)vfs.f_bsize;
    return make_statfs_object(0,
                              (uint64_t)vfs.f_bsize,
                              frsize,
                              (uint64_t)vfs.f_blocks,
                              (uint64_t)vfs.f_bfree,
                              (uint64_t)vfs.f_bavail,
                              (uint64_t)vfs.f_files,
                              (uint64_t)vfs.f_ffree,
                              bigint);
#endif
}

// fs.appendFileSync(path, data)
extern "C" Item js_fs_appendFileSync(Item path_item, Item data_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND, 0644, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        log_error("fs: appendFileSync: cannot open '%s': %s", path, uv_strerror(fd));
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init((char*)str->chars, (unsigned int)str->len);
    uv_fs_t write_req;
    uv_fs_write(NULL, &write_req, fd, &buf, 1, -1, NULL);
    uv_fs_req_cleanup(&write_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    return make_js_undefined();
}

// fs.copyFileSync(src, dest)
extern "C" Item js_fs_copyFileSync(Item src_item, Item dest_item) {
    char src_buf[1024], dest_buf[1024];
    const char* src = fs_path_to_cstr(src_item, "src", src_buf, sizeof(src_buf));
    if (!src) return ItemNull;
    const char* dest = fs_path_to_cstr(dest_item, "dest", dest_buf, sizeof(dest_buf));
    if (!dest) return ItemNull;
    if (!js_permission_has_fs_read(src)) return js_permission_check_fs_read(src);
    if (!js_permission_has_fs_write(dest)) return js_permission_check_fs_write(dest);

    uv_fs_t req;
    int r = uv_fs_copyfile(NULL, &req, src, dest, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: copyFileSync: '%s' -> '%s': %s", src, dest, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.realpathSync(path) → string
extern "C" Item js_fs_realpathSync(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int r = uv_fs_realpath(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: realpathSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item((const char*)req.ptr);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.accessSync(path[, mode]) — throws if access check fails
// mode: fs.constants.F_OK (0), R_OK (4), W_OK (2), X_OK (1)
extern "C" Item js_fs_accessSync(Item path_item, Item mode_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    int mode = 0;
    if (!fs_parse_access_mode(mode_item, true, &mode)) return ItemNull;
    if ((mode & 2) && !js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);
    if (!(mode & 2) && !js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int r = uv_fs_access(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        return js_throw_system_error(r, "access", path);
    }
    return make_js_undefined();
}

// fs.rmSync(path[, options]) — remove file or directory (optionally recursive)
extern "C" Item js_fs_rmSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    bool recursive = false;
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item rec_key = make_string_item("recursive");
        Item rec_val = js_property_get(options_item, rec_key);
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        // use lib/file.c recursive delete
        int r = file_delete_recursive(path);
        if (r != 0) {
            log_error("fs: rmSync: recursive delete failed for '%s'", path);
        }
    } else {
        // try unlink first, then rmdir
        uv_fs_t req;
        int r = uv_fs_unlink(NULL, &req, path, NULL);
        uv_fs_req_cleanup(&req);
        if (r < 0) {
            r = uv_fs_rmdir(NULL, &req, path, NULL);
            uv_fs_req_cleanup(&req);
            if (r < 0) {
                log_error("fs: rmSync: '%s': %s", path, uv_strerror(r));
            }
        }
    }
    return make_js_undefined();
}

// fs.mkdtempSync(prefix) → string
extern "C" Item js_fs_mkdtempSync(Item prefix_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char prefix_buf[1024];
    const char* prefix = fs_path_to_cstr(prefix_item, "prefix", prefix_buf, sizeof(prefix_buf));
    if (!prefix) return ItemNull;

    // Node.js appends XXXXXX to the user-provided prefix for mkdtemp template
    char tpl[1030];
    snprintf(tpl, sizeof(tpl), "%sXXXXXX", prefix);
    if (!js_permission_has_fs_write(tpl)) return js_permission_check_fs_write(tpl);

    uv_fs_t req;
    int r = uv_fs_mkdtemp(NULL, &req, tpl, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: mkdtempSync: %s", uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item(req.path);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.chmodSync(path, mode)
extern "C" Item js_fs_chmodSync(Item path_item, Item mode_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);

    uint32_t mode = 0;
    if (!fs_validate_mode(mode_item, &mode)) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_chmod(NULL, &req, path, (int)mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: chmodSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

extern "C" Item js_fs_fchmodSync(Item fd_item, Item mode_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    uint32_t mode = 0;
    if (!fs_validate_mode(mode_item, &mode)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_fchmod(NULL, &req, fd, (int)mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "fchmod", NULL);
    return make_js_undefined();
}

extern "C" Item js_fs_lchmodSync(Item path_item, Item mode_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);
    uint32_t mode = 0;
    if (!fs_validate_mode(mode_item, &mode)) return ItemNull;
#ifdef __APPLE__
    int r = lchmod(path, (mode_t)mode);
    if (r != 0) return js_throw_system_error(uv_translate_sys_error(errno), "lchmod", path);
    return make_js_undefined();
#else
    uv_fs_t req;
    int r = uv_fs_chmod(NULL, &req, path, (int)mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "lchmod", path);
    return make_js_undefined();
#endif
}

extern "C" Item js_fs_chownSync(Item path_item, Item uid_item, Item gid_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);
    int uid = 0, gid = 0;
    if (!fs_validate_uid_gid(uid_item, "uid", &uid)) return ItemNull;
    if (!fs_validate_uid_gid(gid_item, "gid", &gid)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_chown(NULL, &req, path, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "chown", path);
    return make_js_undefined();
}

extern "C" Item js_fs_lchownSync(Item path_item, Item uid_item, Item gid_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) return js_permission_check_fs_write(path);
    int uid = 0, gid = 0;
    if (!fs_validate_uid_gid(uid_item, "uid", &uid)) return ItemNull;
    if (!fs_validate_uid_gid(gid_item, "gid", &gid)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_lchown(NULL, &req, path, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "lchown", path);
    return make_js_undefined();
}

extern "C" Item js_fs_fchownSync(Item fd_item, Item uid_item, Item gid_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    int uid = 0, gid = 0;
    if (!fs_validate_uid_gid(uid_item, "uid", &uid)) return ItemNull;
    if (!fs_validate_uid_gid(gid_item, "gid", &gid)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_fchown(NULL, &req, fd, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "fchown", NULL);
    return make_js_undefined();
}

extern "C" Item js_fs_linkSync(Item existing_item, Item new_item) {
    char existing_buf[1024], new_buf[1024];
    const char* existing = fs_path_to_cstr(existing_item, "existingPath", existing_buf, sizeof(existing_buf));
    if (!existing) return ItemNull;
    const char* new_path = fs_path_to_cstr(new_item, "newPath", new_buf, sizeof(new_buf));
    if (!new_path) return ItemNull;
    if (!js_permission_has_fs_read(existing)) return js_permission_check_fs_read(existing);
    if (!js_permission_has_fs_write(existing)) return js_permission_check_fs_write(existing);
    if (!js_permission_has_fs_write(new_path)) return js_permission_check_fs_write(new_path);
    uv_fs_t req;
    int r = uv_fs_link(NULL, &req, existing, new_path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) return js_throw_system_error(r, "link", existing);
    return make_js_undefined();
}

// fs.symlinkSync(target, path)
extern "C" Item js_fs_symlinkSync(Item target_item, Item path_item) {
    char target_buf[1024], path_buf[1024];
    const char* target = fs_path_to_cstr(target_item, "target", target_buf, sizeof(target_buf));
    if (!target) return ItemNull;
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (js_permission_enabled() &&
        (!js_permission_has_full_fs_read() || !js_permission_has_full_fs_write())) {
        return js_permission_throw_fs_error(NULL, NULL,
            "fs.symlink API requires full fs.read and fs.write permissions.");
    }

    uv_fs_t req;
    int r = uv_fs_symlink(NULL, &req, target, path, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: symlinkSync: '%s' -> '%s': %s", target, path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.readlinkSync(path) → string
extern "C" Item js_fs_readlinkSync(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int r = uv_fs_readlink(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: readlinkSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item((const char*)req.ptr);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.lstatSync(path[, options]) — like statSync but doesn't follow symlinks
extern "C" Item js_fs_lstatSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);

    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "lstat", path);
    }

    Item obj = make_stats_object(&req.statbuf, fs_options_bigint(options_item));
    uv_fs_req_cleanup(&req);
    return obj;
}

// =============================================================================
// Asynchronous File Operations (callback-style)
// =============================================================================

static Item make_fs_error(int uv_err, const char* path);

typedef struct JsFsReq {
    uv_fs_t req;
    Item callback;        // JS callback: (err, data) => ...
    Item domain;
    char* buffer;         // read buffer (for readFile)
    size_t buffer_size;
    int fd;               // file descriptor
    char path[1024];      // file path (for multi-step operations)
    bool roots_registered;
} JsFsReq;

static void fs_req_register_roots(JsFsReq* fsreq) {
    if (!fsreq || fsreq->roots_registered) return;
    extern void heap_register_gc_root(uint64_t* slot);
    heap_register_gc_root(&fsreq->callback.item);
    heap_register_gc_root(&fsreq->domain.item);
    fsreq->roots_registered = true;
}

static void fs_req_unregister_roots(JsFsReq* fsreq) {
    if (!fsreq || !fsreq->roots_registered) return;
    extern void heap_unregister_gc_root(uint64_t* slot);
    heap_unregister_gc_root(&fsreq->callback.item);
    heap_unregister_gc_root(&fsreq->domain.item);
    fsreq->roots_registered = false;
}

static void fs_req_free(JsFsReq* fsreq) {
    if (!fsreq) return;
    fs_req_unregister_roots(fsreq);
    mem_free(fsreq);
}

static Item fs_req_call_callback(JsFsReq* fsreq, Item* args, int arg_count) {
    if (!fsreq || get_type_id(fsreq->callback) != LMD_TYPE_FUNC) return make_js_undefined();
    return js_domain_call_function(fsreq->domain, fsreq->callback, ItemNull, args, arg_count);
}

static void on_fs_read_complete(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int result = (int)req->result;

    // close the file
    uv_fs_t close_req;
    uv_fs_close(lambda_uv_loop(), &close_req, fsreq->fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (result < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(result, fsreq->path);
            Item args[2] = {err, ItemNull};
            fs_req_call_callback(fsreq, args, 2);
        }
    } else {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item data = make_string_item(fsreq->buffer, result);
            Item args[2] = {ItemNull, data};
            fs_req_call_callback(fsreq, args, 2);
        }
    }

    if (fsreq->buffer) mem_free(fsreq->buffer);
    uv_fs_req_cleanup(req);
    fs_req_free(fsreq);
}

static void on_fs_open_for_read(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int fd = (int)req->result;
    uv_fs_req_cleanup(req);

    if (fd < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(fd, fsreq->path);
            Item args[2] = {err, ItemNull};
            fs_req_call_callback(fsreq, args, 2);
        }
        fs_req_free(fsreq);
        return;
    }

    fsreq->fd = fd;

    // stat to get file size
    uv_fs_t stat_req;
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        uv_fs_t close_req;
        uv_fs_close(lambda_uv_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(r));
            Item args[2] = {err, ItemNull};
            fs_req_call_callback(fsreq, args, 2);
        }
        fs_req_free(fsreq);
        return;
    }

    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);

    fsreq->buffer = (char*)mem_alloc(file_size + 1, MEM_CAT_JS_RUNTIME);
    fsreq->buffer_size = file_size;
    if (!fsreq->buffer) {
        uv_fs_t close_req;
        uv_fs_close(lambda_uv_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        fs_req_free(fsreq);
        return;
    }

    uv_buf_t buf = uv_buf_init(fsreq->buffer, (unsigned int)file_size);
    fsreq->req.data = fsreq;
    uv_fs_read(lambda_uv_loop(), &fsreq->req, fd, &buf, 1, 0, on_fs_read_complete);
}

// fs.readFile(path[, options], callback)
extern "C" Item js_fs_readFile(Item path_item, Item options_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(options_or_cb) == LMD_TYPE_FUNC) {
        callback = options_or_cb;
    } else if (!fs_validate_encoding_options(options_or_cb)) {
        return ItemNull;
    }

    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) {
        return fs_permission_callback_error(callback, "FileSystemRead", path, NULL);
    }

    JsFsReq* fsreq = (JsFsReq*)mem_calloc(1, sizeof(JsFsReq), MEM_CAT_JS_RUNTIME);
    if (!fsreq) return ItemNull;

    fsreq->callback = callback;
    fsreq->domain = js_domain_get_current();
    snprintf(fsreq->path, sizeof(fsreq->path), "%s", path);
    fsreq->req.data = fsreq;
    fs_req_register_roots(fsreq);

    uv_fs_open(lambda_uv_loop(), &fsreq->req, path, UV_FS_O_RDONLY, 0, on_fs_open_for_read);
    return make_js_undefined();
}

static void on_fs_write_complete(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;

    // close the file
    uv_fs_t close_req;
    uv_fs_close(lambda_uv_loop(), &close_req, fsreq->fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
        if (req->result < 0) {
            Item err = make_string_item(uv_strerror((int)req->result));
            Item args[1] = {err};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        }
    }

    if (fsreq->buffer) mem_free(fsreq->buffer);
    uv_fs_req_cleanup(req);
    mem_free(fsreq);
}

static void on_fs_open_for_write(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int fd = (int)req->result;
    uv_fs_req_cleanup(req);

    if (fd < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(fd));
            Item args[1] = {err};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        }
        if (fsreq->buffer) mem_free(fsreq->buffer);
        mem_free(fsreq);
        return;
    }

    fsreq->fd = fd;
    uv_buf_t buf = uv_buf_init(fsreq->buffer, (unsigned int)fsreq->buffer_size);
    fsreq->req.data = fsreq;
    uv_fs_write(lambda_uv_loop(), &fsreq->req, fd, &buf, 1, 0, on_fs_write_complete);
}

// fs.writeFile(path, data[, options], callback)
extern "C" Item js_fs_writeFile(Item path_item, Item data_item, Item options_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(options_or_cb) == LMD_TYPE_FUNC) {
        callback = options_or_cb;
    } else if (!fs_validate_encoding_options(options_or_cb)) {
        return ItemNull;
    }

    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) {
        return fs_permission_callback_error(callback, "FileSystemWrite", path, NULL);
    }

    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    JsFsReq* fsreq = (JsFsReq*)mem_calloc(1, sizeof(JsFsReq), MEM_CAT_JS_RUNTIME);
    if (!fsreq) return ItemNull;

    // copy write data since it may be GC'd during async operation
    fsreq->buffer = (char*)mem_alloc(str->len, MEM_CAT_JS_RUNTIME);
    if (!fsreq->buffer) { mem_free(fsreq); return ItemNull; }
    memcpy(fsreq->buffer, str->chars, str->len);
    fsreq->buffer_size = str->len;
    fsreq->callback = callback;
    fsreq->req.data = fsreq;

    uv_fs_open(lambda_uv_loop(), &fsreq->req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC, 0644, on_fs_open_for_write);
    return make_js_undefined();
}

// =============================================================================
// truncateSync
// =============================================================================

extern "C" Item js_fs_truncateSync(Item path_item, Item len_item) {
    char path[PATH_MAX];
    const char* p = fs_path_to_cstr(path_item, "path", path, sizeof(path));
    if (!p) return ItemNull;
    if (!js_permission_has_fs_write(p)) return js_permission_check_fs_write(p);

    int64_t length = 0;
    TypeId len_type = get_type_id(len_item);
    if (len_type != LMD_TYPE_UNDEFINED && len_type != LMD_TYPE_NULL) {
        // JS Number arguments are boxed FLOAT after the number-model migration.
        if (!fs_validate_int_range(len_item, "len", 0, INT64_MAX, &length)) return ItemNull;
    }

    uv_fs_t req;
    int fd = uv_fs_open(lambda_uv_loop(), &req, path, UV_FS_O_WRONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return ItemNull;

    int r = uv_fs_ftruncate(lambda_uv_loop(), &req, fd, length, NULL);
    uv_fs_req_cleanup(&req);

    uv_fs_close(lambda_uv_loop(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);

    if (r < 0) return ItemNull;
    return make_js_undefined();
}

// =============================================================================
// File Descriptor Operations: openSync, closeSync, readSync, writeSync, fstatSync
// =============================================================================

extern "C" Item js_fs_openSync(Item path_item, Item flags_item, Item mode_item) {
    char path[PATH_MAX];
    const char* p = fs_path_to_cstr(path_item, "path", path, sizeof(path));
    if (!p) return ItemNull;

    int flags = UV_FS_O_RDONLY;
    if (get_type_id(flags_item) == LMD_TYPE_STRING) {
        String* fs = it2s(flags_item);
        if (fs->len == 1 && fs->chars[0] == 'r') flags = UV_FS_O_RDONLY;
        else if (fs->len == 2 && fs->chars[0] == 'r' && fs->chars[1] == '+') flags = UV_FS_O_RDWR;
        else if (fs->len == 1 && fs->chars[0] == 'w') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (fs->len == 2 && fs->chars[0] == 'w' && fs->chars[1] == '+') flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (fs->len == 1 && fs->chars[0] == 'a') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else if (fs->len == 2 && fs->chars[0] == 'a' && fs->chars[1] == '+') flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else if (fs->len == 2 && fs->chars[0] == 'a' && fs->chars[1] == 'x') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND | UV_FS_O_EXCL;
        else if (fs->len == 2 && fs->chars[0] == 'w' && fs->chars[1] == 'x') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC | UV_FS_O_EXCL;
    } else if (get_type_id(flags_item) == LMD_TYPE_INT) {
        flags = (int)it2i(flags_item);
    }

    int mode = 0666;
    if (get_type_id(mode_item) == LMD_TYPE_INT) mode = (int)it2i(mode_item);
    bool needs_write = (flags & (UV_FS_O_WRONLY | UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC | UV_FS_O_APPEND)) != 0;
    bool needs_read = !needs_write || ((flags & UV_FS_O_RDWR) != 0);
    if (needs_read && !js_permission_has_fs_read(p)) return js_permission_check_fs_read(p);
    if (needs_write && !js_permission_has_fs_write(p)) return js_permission_check_fs_write(p);

    uv_fs_t req;
    int fd = uv_fs_open(lambda_uv_loop(), &req, p, flags, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return ItemNull;
    return (Item){.item = i2it((int64_t)fd)};
}

extern "C" Item js_fs_closeSync(Item fd_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    uv_fs_t req;
    uv_fs_close(lambda_uv_loop(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);
    return make_js_undefined();
}

extern "C" Item js_fs_readSync(Item fd_item, Item buffer_item, Item offset_item, Item length_item, Item position_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;

    Item read_buffer = buffer_item;
    Item read_offset = offset_item;
    Item read_length = length_item;
    Item read_position = position_item;
    bool options_form = fs_is_nullish(length_item) && fs_is_nullish(position_item);
    if (options_form) {
        if (!fs_is_options_object(offset_item) && !fs_is_nullish(offset_item)) {
            return js_throw_invalid_arg_type("options", "object", offset_item);
        }
        if (!fs_read_parse_options(offset_item, buffer_item, &read_buffer,
                                   &read_offset, &read_length, &read_position)) {
            return ItemNull;
        }
    }

    JsTypedArray* ta = fs_get_typed_array(read_buffer);
    if (!ta) return js_throw_invalid_arg_type("buffer", "Buffer, TypedArray, or DataView", read_buffer);
    int blen = js_typed_array_byte_length(read_buffer);
    uint8_t* data = (uint8_t*)js_typed_array_prepare_write_ptr(read_buffer);

    int offset = 0, length = blen;
    if (!fs_validate_offset_length(read_offset, read_length, blen, &offset, &length)) return ItemNull;
    if (blen == 0 && length > 0) return fs_throw_empty_read_buffer(ta);
    if (length <= 0) return js_make_number(0.0);
    if (!data) return js_make_number(0.0);

    int64_t position = -1;
    if (!fs_read_position_to_int64(read_position, &position)) return ItemNull;

    uv_buf_t buf = uv_buf_init((char*)(data + offset), length);
    uv_fs_t req;
    int nread = uv_fs_read(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nread < 0) return js_make_number(0.0);
    // fs read/write byte counts are Node JS Number results, not lossless data ids.
    return js_make_number((double)nread);
}

extern "C" Item js_fs_read(Item fd_item, Item buffer_item, Item offset_item, Item length_item, Item position_item, Item callback) {
    if (get_type_id(position_item) == LMD_TYPE_FUNC) {
        callback = position_item;
        position_item = make_js_undefined();
    } else if (get_type_id(length_item) == LMD_TYPE_FUNC) {
        callback = length_item;
        length_item = make_js_undefined();
        position_item = make_js_undefined();
    } else if (get_type_id(offset_item) == LMD_TYPE_FUNC) {
        callback = offset_item;
        offset_item = make_js_undefined();
        length_item = make_js_undefined();
        position_item = make_js_undefined();
    } else if (get_type_id(buffer_item) == LMD_TYPE_FUNC) {
        callback = buffer_item;
        buffer_item = make_js_undefined();
        offset_item = make_js_undefined();
        length_item = make_js_undefined();
        position_item = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    Item read_buffer = buffer_item;
    Item read_offset = offset_item;
    Item read_length = length_item;
    Item read_position = position_item;
    bool second_arg_is_options = !fs_get_typed_array(buffer_item) &&
                                 (fs_is_options_object(buffer_item) || fs_is_nullish(buffer_item));
    bool third_arg_is_options = fs_get_typed_array(buffer_item) &&
                                (fs_is_options_object(offset_item) || fs_is_nullish(offset_item)) &&
                                fs_is_nullish(length_item) && fs_is_nullish(position_item);
    if (second_arg_is_options) {
        if (!fs_read_parse_options(buffer_item, make_js_undefined(), &read_buffer,
                                   &read_offset, &read_length, &read_position)) {
            return ItemNull;
        }
    } else if (third_arg_is_options) {
        if (!fs_read_parse_options(offset_item, buffer_item, &read_buffer,
                                   &read_offset, &read_length, &read_position)) {
            return ItemNull;
        }
    }

    Item bytes_read = js_fs_readSync(fd_item, read_buffer, read_offset, read_length, read_position);
    if (js_check_exception()) return ItemNull;

    Item args[3] = {ItemNull, bytes_read, read_buffer};
    js_call_function(callback, make_js_undefined(), args, 3);
    return make_js_undefined();
}

static Item js_fs_filehandle_read(Item buffer_item, Item offset_item, Item length_item, Item position_item) {
    Item self = js_get_this();
    Item fd_item = js_property_get(self, make_string_item("__fd"));
    Item read_buffer = buffer_item;
    Item read_offset = offset_item;
    Item read_length = length_item;
    Item read_position = position_item;
    bool first_arg_is_options = !fs_get_typed_array(buffer_item) &&
                                (fs_is_options_object(buffer_item) || fs_is_nullish(buffer_item));
    bool second_arg_is_options = fs_get_typed_array(buffer_item) &&
                                 (fs_is_options_object(offset_item) || fs_is_nullish(offset_item)) &&
                                 fs_is_nullish(length_item) && fs_is_nullish(position_item);
    if (first_arg_is_options) {
        if (!fs_read_parse_options(buffer_item, make_js_undefined(), &read_buffer,
                                   &read_offset, &read_length, &read_position)) {
            Item err = js_clear_exception();
            return js_promise_reject(err);
        }
    } else if (second_arg_is_options) {
        if (!fs_read_parse_options(offset_item, buffer_item, &read_buffer,
                                   &read_offset, &read_length, &read_position)) {
            Item err = js_clear_exception();
            return js_promise_reject(err);
        }
    }
    Item bytes_read = js_fs_readSync(fd_item, read_buffer, read_offset, read_length, read_position);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }

    Item result = js_new_object();
    js_property_set(result, make_string_item("bytesRead"), bytes_read);
    js_property_set(result, make_string_item("buffer"), read_buffer);
    return js_promise_resolve(result);
}

static Item js_fs_filehandle_transferred_error(const char* syscall) {
    Item err = js_new_error_with_name(make_string_item("Error"),
        make_string_item("The FileHandle has been transferred"));
    js_property_set(err, make_string_item("code"), make_string_item("EBADF"));
    js_property_set(err, make_string_item("syscall"), make_string_item(syscall ? syscall : "read"));
    return err;
}

static Item js_fs_filehandle_readFile(Item options_item) {
    if (!fs_validate_encoding_options(options_item)) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }

    Item self = js_get_this();
    Item fd_item = js_property_get(self, make_string_item("__fd"));
    if (get_type_id(fd_item) != LMD_TYPE_INT || it2i(fd_item) < 0) {
        return js_promise_reject(js_fs_filehandle_transferred_error("read"));
    }
    int fd = (int)it2i(fd_item);

    uv_fs_t stat_req;
    fs_maybe_call_internal_fstat_hook(fd);
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        Item result = js_throw_system_error(r, "fstat", NULL);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            return js_promise_reject(err);
        }
        return js_promise_reject(result);
    }

    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);
    char* data = (char*)mem_alloc(file_size > 0 ? file_size : 1, MEM_CAT_JS_RUNTIME);
    if (!data) return js_promise_reject(js_fs_filehandle_transferred_error("read"));

    uv_buf_t buf = uv_buf_init(data, (unsigned int)file_size);
    uv_fs_t read_req;
    int bytes_read = (int)uv_fs_read(NULL, &read_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&read_req);

    if (bytes_read < 0) {
        mem_free(data);
        Item result = js_throw_system_error(bytes_read, "read", NULL);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            return js_promise_reject(err);
        }
        return js_promise_reject(result);
    }

    Item result = fs_read_file_should_return_buffer(options_item)
        ? fs_buffer_from_bytes(data, bytes_read)
        : make_string_item(data, bytes_read);
    mem_free(data);
    return js_promise_resolve(result);
}

static Item js_fs_filehandle_close(void) {
    Item self = js_get_this();
    Item fd_item = js_property_get(self, make_string_item("__fd"));
    if (get_type_id(fd_item) == LMD_TYPE_INT) {
        js_fs_closeSync(fd_item);
        js_property_set(self, make_string_item("__fd"), make_js_undefined());
    }
    return js_promise_resolve(make_js_undefined());
}

static Item fs_filehandle_ctor = {0};
static Item fs_filehandle_proto = {0};
static Item js_fs_set_method(Item ns, const char* name, void* func_ptr, int param_count);

static Item js_fs_filehandle_illegal_constructor(void) {
    return js_throw_type_error("FileHandle is not constructible");
}

static Item js_fs_filehandle_fd_getter(void) {
    Item self = js_get_this();
    return js_property_get(self, make_string_item("__fd"));
}

static Item fs_get_filehandle_prototype(void) {
    if (fs_filehandle_proto.item != 0) return fs_filehandle_proto;

    // Constructor/prototype caches survive the native construction call and
    // must own their partially built objects under precise-only collection.
    heap_register_gc_root(&fs_filehandle_proto.item);
    fs_filehandle_proto = js_new_object();
    Item fd_getter = js_new_function((void*)js_fs_filehandle_fd_getter, 0);
    js_install_native_accessor(fs_filehandle_proto, make_string_item("fd"), fd_getter,
                               ItemNull, JSPD_NON_ENUMERABLE);
    js_fs_set_method(fs_filehandle_proto, "read", (void*)js_fs_filehandle_read, 4);
    js_fs_set_method(fs_filehandle_proto, "readFile", (void*)js_fs_filehandle_readFile, 1);
    js_fs_set_method(fs_filehandle_proto, "close", (void*)js_fs_filehandle_close, 0);
    js_fs_set_method(fs_filehandle_proto, "__sym_14", (void*)js_fs_filehandle_close, 0);
    return fs_filehandle_proto;
}

static Item fs_get_filehandle_constructor(void) {
    if (fs_filehandle_ctor.item != 0) return fs_filehandle_ctor;

    Item proto = fs_get_filehandle_prototype();
    heap_register_gc_root(&fs_filehandle_ctor.item);
    fs_filehandle_ctor = js_new_function((void*)js_fs_filehandle_illegal_constructor, 0);
    js_property_set(fs_filehandle_ctor, make_string_item("prototype"), proto);
    js_property_set(proto, make_string_item("constructor"), fs_filehandle_ctor);
    return fs_filehandle_ctor;
}

static Item fs_create_filehandle(Item fd) {
    RootFrame roots((Context*)context, 2);
    Rooted<Item> fd_root(roots, fd);
    Rooted<Item> handle_root(roots, js_new_object());
    js_set_prototype(handle_root.get(), fs_get_filehandle_prototype());
    js_property_set(handle_root.get(), make_string_item("__fd"), fd_root.get());
    return handle_root.get();
}

extern "C" Item js_fs_writeSync(Item fd_item, Item data_item, Item offset_item, Item length_item, Item position_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;

    // data can be a string or a Buffer
    const char* write_buf = NULL;
    int write_len = 0;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        write_buf = s->chars;
        write_len = (int)s->len;
    } else {
        int blen = 0;
        uint8_t* bdata = buffer_data(data_item, &blen);
        if (bdata) {
            write_buf = (const char*)bdata;
            write_len = blen;
        }
    }
    if (!write_buf) return js_make_number(0.0);

    int offset = 0, length = write_len;
    if (!fs_validate_offset_length(offset_item, length_item, write_len, &offset, &length)) return ItemNull;
    if (length <= 0) return js_make_number(0.0);

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) position = it2i(position_item);

    uv_buf_t buf = uv_buf_init((char*)(write_buf + offset), length);
    uv_fs_t req;
    int nwritten = uv_fs_write(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nwritten < 0) return js_make_number(0.0);
    // fs read/write byte counts are Node JS Number results, not lossless data ids.
    return js_make_number((double)nwritten);
}

static bool fs_write_parse_options(Item options_item, Item data_item,
                                   Item* out_offset, Item* out_length, Item* out_position) {
    Item offset = make_js_undefined();
    Item length = make_js_undefined();
    Item position = make_js_undefined();
    if (fs_is_options_object(options_item)) {
        offset = js_property_get(options_item, make_string_item("offset"));
        length = js_property_get(options_item, make_string_item("length"));
        position = js_property_get(options_item, make_string_item("position"));
    }
    if (fs_is_nullish(offset)) offset = (Item){.item = i2it(0)};
    if (fs_is_nullish(length)) {
        int byte_length = js_typed_array_byte_length(data_item);
        int64_t offset_value = 0;
        if (get_type_id(offset) == LMD_TYPE_INT) {
            offset_value = it2i(offset);
        } else if (get_type_id(offset) == LMD_TYPE_FLOAT) {
            offset_value = (int64_t)it2d(offset);
        }
        length = (Item){.item = i2it(byte_length - offset_value)};
    }
    *out_offset = offset;
    *out_length = length;
    *out_position = position;
    return true;
}

extern "C" Item js_fs_write(Item fd_item, Item data_item, Item offset_item,
                            Item length_item, Item position_item, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(position_item) == LMD_TYPE_FUNC) {
        callback = position_item;
        position_item = make_js_undefined();
    } else if (get_type_id(length_item) == LMD_TYPE_FUNC) {
        callback = length_item;
        length_item = make_js_undefined();
        position_item = make_js_undefined();
    } else if (get_type_id(offset_item) == LMD_TYPE_FUNC) {
        callback = offset_item;
        offset_item = make_js_undefined();
        length_item = make_js_undefined();
        position_item = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    Item write_offset = offset_item;
    Item write_length = length_item;
    Item write_position = position_item;

    if (fs_get_typed_array(data_item)) {
        bool options_form = (fs_is_options_object(offset_item) || fs_is_nullish(offset_item)) &&
                            fs_is_nullish(length_item) && fs_is_nullish(position_item);
        if (options_form) {
            if (!fs_write_parse_options(offset_item, data_item, &write_offset,
                                        &write_length, &write_position)) {
                return ItemNull;
            }
        }
    } else if (get_type_id(data_item) == LMD_TYPE_STRING) {
        write_offset = (Item){.item = i2it(0)};
        write_length = make_js_undefined();
        write_position = offset_item;
        if (fs_is_nullish(write_position) || get_type_id(write_position) == LMD_TYPE_FUNC) {
            write_position = make_js_undefined();
        }
        if (!fs_validate_encoding_item(length_item)) return ItemNull;
    } else {
        return js_throw_invalid_arg_type("buffer", "Buffer, TypedArray, DataView, or string", data_item);
    }

    Item bytes_written = js_fs_writeSync(fd_item, data_item, write_offset, write_length, write_position);
    if (js_check_exception()) return ItemNull;

    Item args[3] = {ItemNull, bytes_written, data_item};
    js_call_function(callback, make_js_undefined(), args, 3);
    return make_js_undefined();
}

extern "C" Item js_fs_readvSync(Item fd_item, Item buffers_item, Item position_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    if (get_type_id(buffers_item) != LMD_TYPE_ARRAY) {
        return js_throw_invalid_arg_type("buffers", "ArrayBufferView[]", buffers_item);
    }

    int64_t count64 = js_array_length(buffers_item);
    if (count64 <= 0) return js_make_number(0.0);
    if (count64 > 1024) {
        js_throw_out_of_range("buffers.length", "<= 1024", (Item){.item = i2it(count64)});
        return ItemNull;
    }

    uv_buf_t* bufs = (uv_buf_t*)alloca((size_t)count64 * sizeof(uv_buf_t));
    for (int64_t i = 0; i < count64; i++) {
        Item buffer = js_array_get_int(buffers_item, i);
        JsTypedArray* ta = fs_get_typed_array(buffer);
        if (!ta) return js_throw_invalid_arg_type("buffers", "ArrayBufferView[]", buffer);
        int blen = js_typed_array_byte_length(buffer);
        uint8_t* data = (uint8_t*)js_typed_array_prepare_write_ptr(buffer);
        bufs[i] = uv_buf_init((char*)data, blen);
    }

    int64_t position = -1;
    if (!fs_read_position_to_int64(position_item, &position)) return ItemNull;

    uv_fs_t req;
    int nread = uv_fs_read(lambda_uv_loop(), &req, fd, bufs, (unsigned int)count64, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nread < 0) return js_make_number(0.0);
    // fs readv/writev byte counts are Node JS Number results, not lossless data ids.
    return js_make_number((double)nread);
}

extern "C" Item js_fs_readv(Item fd_item, Item buffers_item, Item position_item, Item callback_item) {
    Item callback = callback_item;
    Item position = position_item;
    if (get_type_id(position_item) == LMD_TYPE_FUNC) {
        callback = position_item;
        position = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    Item bytes_read = js_fs_readvSync(fd_item, buffers_item, position);
    if (js_check_exception()) return ItemNull;
    Item args[3] = {ItemNull, bytes_read, buffers_item};
    js_call_function(callback, make_js_undefined(), args, 3);
    return make_js_undefined();
}

extern "C" Item js_fs_writevSync(Item fd_item, Item buffers_item, Item position_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    if (get_type_id(buffers_item) != LMD_TYPE_ARRAY) {
        return js_throw_invalid_arg_type("buffers", "ArrayBufferView[]", buffers_item);
    }

    int64_t count64 = js_array_length(buffers_item);
    if (count64 <= 0) return js_make_number(0.0);
    if (count64 > 1024) {
        js_throw_out_of_range("buffers.length", "<= 1024", (Item){.item = i2it(count64)});
        return ItemNull;
    }

    uv_buf_t* bufs = (uv_buf_t*)alloca((size_t)count64 * sizeof(uv_buf_t));
    for (int64_t i = 0; i < count64; i++) {
        Item buffer = js_array_get_int(buffers_item, i);
        int blen = 0;
        uint8_t* data = buffer_data(buffer, &blen);
        if (!data && !js_is_typed_array(buffer)) {
            return js_throw_invalid_arg_type("buffers", "ArrayBufferView[]", buffer);
        }
        bufs[i] = uv_buf_init((char*)data, blen);
    }

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) {
        position = it2i(position_item);
    } else if (get_type_id(position_item) != LMD_TYPE_UNDEFINED &&
               get_type_id(position_item) != LMD_TYPE_NULL) {
        return js_throw_invalid_arg_type("position", "number", position_item);
    }

    uv_fs_t req;
    int nwritten = uv_fs_write(lambda_uv_loop(), &req, fd, bufs, (unsigned int)count64, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nwritten < 0) return js_make_number(0.0);
    // fs readv/writev byte counts are Node JS Number results, not lossless data ids.
    return js_make_number((double)nwritten);
}

extern "C" Item js_fs_writev(Item fd_item, Item buffers_item, Item position_item, Item callback_item) {
    Item callback = callback_item;
    Item position = position_item;
    if (get_type_id(position_item) == LMD_TYPE_FUNC) {
        callback = position_item;
        position = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    Item bytes_written = js_fs_writevSync(fd_item, buffers_item, position);
    if (js_check_exception()) return ItemNull;
    Item args[3] = {ItemNull, bytes_written, buffers_item};
    js_call_function(callback, make_js_undefined(), args, 3);
    return make_js_undefined();
}

static Item js_fs_exists_promisified(Item path_item) {
    Item exists = js_fs_existsSync(path_item);
    if (js_check_exception()) {
        js_clear_exception();
        exists = (Item){.item = b2it(false)};
    }
    return js_promise_resolve(exists);
}

extern "C" Item js_fs_fstatSync(Item fd_item, Item options_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_fstat(lambda_uv_loop(), &req, fd, NULL);
    if (r < 0) { uv_fs_req_cleanup(&req); return ItemNull; }

    Item result = make_stats_object(&req.statbuf, fs_options_bigint(options_item));
    uv_fs_req_cleanup(&req);
    return result;
}

// =============================================================================
// Async (callback) wrappers — perform sync then invoke callback
// =============================================================================

// helper: create an error object with code property
static Item make_fs_error(int uv_err, const char* path) {
    const char* msg = uv_strerror(uv_err);
    Item err = js_new_error(make_string_item(msg));
    const char* code = "ERR_FS";
    if (uv_err == UV_ENOENT) code = "ENOENT";
    else if (uv_err == UV_EACCES) code = "EACCES";
    else if (uv_err == UV_EEXIST) code = "EEXIST";
    else if (uv_err == UV_EISDIR) code = "EISDIR";
    else if (uv_err == UV_ENOTDIR) code = "ENOTDIR";
    else if (uv_err == UV_EPERM) code = "EPERM";
    else if (uv_err == UV_EBADF) code = "EBADF";
    js_property_set(err, make_string_item("code"), make_string_item(code));
    if (path) {
        js_property_set(err, make_string_item("path"), make_string_item(path));
    }
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it(uv_err)});
    js_property_set(err, make_string_item("syscall"), make_string_item("access"));
    return err;
}

// fs.access(path[, mode], callback)
static Item js_fs_access_async(Item path_item, Item mode_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item mode_item = mode_or_cb;
    bool has_mode = true;
    if (get_type_id(mode_or_cb) == LMD_TYPE_FUNC) {
        callback = mode_or_cb;
        mode_item = make_js_undefined();
        has_mode = false;
    } else {
        has_mode = get_type_id(mode_or_cb) != LMD_TYPE_UNDEFINED;
    }

    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    int mode = 0;
    if (!fs_parse_access_mode(mode_item, has_mode, &mode)) return ItemNull;

    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if ((mode & 2) && !js_permission_has_fs_write(path)) {
        return fs_permission_callback_error(callback, "FileSystemWrite", path, NULL);
    }
    if (!(mode & 2) && !js_permission_has_fs_read(path)) {
        return fs_permission_callback_error(callback, "FileSystemRead", path, NULL);
    }
    uv_fs_t req;
    int r = uv_fs_access(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        Item err = make_fs_error(r, path);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
    } else {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.stat(path[, options], callback)
static Item js_fs_stat_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = opts_or_cb;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
        options = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) {
        return fs_permission_callback_error(callback, "FileSystemRead", path, NULL);
    }

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        Item err = make_fs_error(r, path);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
        return make_js_undefined();
    }
    Item stat_result = make_stats_object(&req.statbuf, fs_options_bigint(options));
    uv_fs_req_cleanup(&req);
    Item args[2] = {ItemNull, stat_result};
    js_call_function(callback, ItemNull, args, 2);
    return make_js_undefined();
}

// fs.lstat(path[, options], callback)
static Item js_fs_lstat_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = opts_or_cb;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
        options = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) {
        return fs_permission_callback_error(callback, "FileSystemRead", path, NULL);
    }

    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        Item err = make_fs_error(r, path);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
        return make_js_undefined();
    }
    Item stat_result = make_stats_object(&req.statbuf, fs_options_bigint(options));
    uv_fs_req_cleanup(&req);
    Item args[2] = {ItemNull, stat_result};
    js_call_function(callback, ItemNull, args, 2);
    return make_js_undefined();
}

// fs.statfs(path[, options], callback)
static Item js_fs_statfs_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = opts_or_cb;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
        options = make_js_undefined();
    }
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    Item result = js_fs_statfsSync(path_item, options);
    if (js_check_exception()) return ItemNull;
    Item args[2] = {ItemNull, result};
    js_call_function(callback, make_js_undefined(), args, 2);
    return make_js_undefined();
}

// fs.open(path, flags[, mode], callback)
static Item js_fs_open_async(Item path_item, Item flags_item, Item mode_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item mode_item = mode_or_cb;
    if (get_type_id(mode_or_cb) == LMD_TYPE_FUNC) {
        callback = mode_or_cb;
        mode_item = make_js_undefined();
    }
    Item fd = js_fs_openSync(path_item, flags_item, mode_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (fd.item == ITEM_NULL || get_type_id(fd) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, fd};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.close(fd, callback)
static Item js_fs_close_async(Item fd_item, Item callback) {
    js_fs_closeSync(fd_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.chmod(path, mode, callback)
static Item js_fs_chmod_async(Item path_item, Item mode_item, Item callback) {
    Item result = js_fs_chmodSync(path_item, mode_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return make_js_undefined();
}

// fs.unlink(path, callback)
static Item js_fs_unlink_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_unlink(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0) {
            Item err = make_fs_error(r, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

// fs.exists(path, callback) — deprecated but still used in tests
// NOTE: callback signature is (exists) not (err, exists)
static Item js_fs_exists_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = NULL;
    if (!(get_type_id(path_item) == LMD_TYPE_STRING && fs_string_has_nul(it2s(path_item)))) {
        path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    }
    bool exists = false;
    if (path) {
        uv_fs_t req;
        int r = uv_fs_stat(NULL, &req, path, NULL);
        uv_fs_req_cleanup(&req);
        exists = (r == 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {(Item){.item = b2it(exists)}};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.rename(oldPath, newPath, callback)
static Item js_fs_rename_async(Item old_item, Item new_item, Item callback) {
    js_fs_renameSync(old_item, new_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.readdir(path[, options], callback)
static Item js_fs_readdir_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item result = js_fs_readdirSync(path_item, opts_or_cb);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (result.item == ITEM_NULL || get_type_id(result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.fstat(fd[, options], callback)
static Item js_fs_fstat_async(Item fd_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = opts_or_cb;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
        options = make_js_undefined();
    }
    Item result = js_fs_fstatSync(fd_item, options);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (result.item == ITEM_NULL || get_type_id(result) == LMD_TYPE_NULL) {
            Item err = make_fs_error(UV_EBADF, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// =============================================================================
// fs Module Namespace Object
// =============================================================================

static Item fs_internal_promises_namespace = {0};

static Item js_fs_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> ns_root(roots, ns);
    Rooted<Item> key_root(roots, make_string_item(name));
    Rooted<Item> fn_root(roots, js_new_function(func_ptr, param_count));
    js_property_set(ns_root.get(), key_root.get(), fn_root.get());
    return fn_root.get();
}

static void js_fs_set_custom_promisify_args(Item fn, const char* name1, const char* name2) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> fn_root(roots, fn);
    Rooted<Item> names_root(roots, js_array_new(0));
    Rooted<Item> symbol_root(roots, ItemNull);
    js_array_push(names_root.get(), make_string_item(name1));
    if (name2) js_array_push(names_root.get(), make_string_item(name2));
    symbol_root.set(js_util_custom_promisify_args_symbol());
    js_property_set(fn_root.get(), symbol_root.get(), names_root.get());
}

static void js_fs_set_custom_promisify(Item fn, void* func_ptr, int param_count) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> fn_root(roots, fn);
    Rooted<Item> custom_root(roots, js_new_function(func_ptr, param_count));
    Rooted<Item> symbol_root(roots, js_util_promisify_custom_symbol());
    js_property_set(fn_root.get(), symbol_root.get(), custom_root.get());
}

// ─── fs.promises wrapper functions ─────────────────────────────────────────
// Each wraps the sync version, returning a resolved/rejected Promise
extern Item js_promise_resolve(Item value);
extern Item js_promise_reject(Item reason);
extern "C" Item js_promise_with_resolvers(void);
extern "C" void js_next_tick_enqueue(Item callback);
extern int js_check_exception(void);
extern Item js_clear_exception(void);

static Item fs_promise_wrap_result(Item result) {
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }
    return js_promise_resolve(result);
}

static bool fs_options_has_signal(Item options, Item* signal_out) {
    if (signal_out) *signal_out = make_js_undefined();
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_MAP && opt_type != LMD_TYPE_OBJECT) return false;
    Item signal = js_property_get(options, make_string_item("signal"));
    if (get_type_id(signal) == LMD_TYPE_UNDEFINED || get_type_id(signal) == LMD_TYPE_NULL) return false;
    if (signal_out) *signal_out = signal;
    return true;
}

static bool fs_is_abort_signal(Item signal) {
    TypeId sig_type = get_type_id(signal);
    if (sig_type != LMD_TYPE_MAP && sig_type != LMD_TYPE_OBJECT) return false;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && get_type_id(add_event) == LMD_TYPE_FUNC;
}

static bool fs_signal_aborted(Item signal) {
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted);
}

static Item fs_make_abort_error(Item signal) {
    Item err = js_new_object();
    js_class_stamp(err, JS_CLASS_ABORT_ERROR);
    js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
    js_property_set(err, make_string_item("code"), make_string_item("ABORT_ERR"));
    js_property_set(err, make_string_item("message"), make_string_item("The operation was aborted"));
    if (get_type_id(signal) == LMD_TYPE_MAP || get_type_id(signal) == LMD_TYPE_OBJECT) {
        Item reason = js_property_get(signal, make_string_item("reason"));
        if (get_type_id(reason) != LMD_TYPE_UNDEFINED && get_type_id(reason) != LMD_TYPE_NULL) {
            js_property_set(err, make_string_item("cause"), reason);
        }
    }
    return err;
}

static Item fs_make_invalid_signal_error(void) {
    Item err = js_new_error_with_name(
        make_string_item("TypeError"),
        make_string_item("The \"options.signal\" property must be an instance of AbortSignal."));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_INVALID_ARG_TYPE"));
    return err;
}

static Item fs_readFile_promise_deferred(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item path = env[0];
    Item opts = env[1];
    Item signal = env[2];
    Item resolve_fn = env[3];
    Item reject_fn = env[4];

    if (fs_signal_aborted(signal)) {
        Item err = fs_make_abort_error(signal);
        js_call_function(reject_fn, make_js_undefined(), &err, 1);
        return make_js_undefined();
    }

    Item result = js_fs_readFileSync(path, opts);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_call_function(reject_fn, make_js_undefined(), &err, 1);
    } else {
        js_call_function(resolve_fn, make_js_undefined(), &result, 1);
    }
    return make_js_undefined();
}

static void fs_append_async_access_stack(Item err) {
    if (get_type_id(err) != LMD_TYPE_MAP) return;

    Item stack_key = make_string_item("stack");
    Item stack_item = js_property_get(err, stack_key);
    const char* async_frame = "\n    at async Object.access";
    char stack_buf[2048];

    if (get_type_id(stack_item) == LMD_TYPE_STRING) {
        String* stack = it2s(stack_item);
        int len = (int)stack->len;
        int max_len = (int)sizeof(stack_buf) - (int)strlen(async_frame) - 1;
        if (len > max_len) len = max_len;
        memcpy(stack_buf, stack->chars, (size_t)len);
        stack_buf[len] = '\0';
        snprintf(stack_buf + len, sizeof(stack_buf) - (size_t)len, "%s", async_frame);
    } else {
        snprintf(stack_buf, sizeof(stack_buf), "%s", async_frame + 1);
    }

    js_property_set(err, stack_key, make_string_item(stack_buf));
}

static bool fs_callback_pending_exception(Item callback) {
    (void)callback;
    if (!js_check_exception()) return false;
    return true;
}

extern "C" Item js_fs_readFile_promise(Item path, Item opts) {
    Item signal = make_js_undefined();
    if (fs_options_has_signal(opts, &signal)) {
        if (!fs_is_abort_signal(signal)) {
            return js_promise_reject(fs_make_invalid_signal_error());
        }
        if (fs_signal_aborted(signal)) {
            return js_promise_reject(fs_make_abort_error(signal));
        }

        Item capability = js_promise_with_resolvers();
        Item promise = js_property_get(capability, make_string_item("promise"));
        Item resolve_fn = js_property_get(capability, make_string_item("resolve"));
        Item reject_fn = js_property_get(capability, make_string_item("reject"));
        Item* env = js_alloc_env(5);
        env[0] = path;
        env[1] = opts;
        env[2] = signal;
        env[3] = resolve_fn;
        env[4] = reject_fn;
        Item callback = js_new_closure((void*)fs_readFile_promise_deferred, 0, env, 5);
        js_next_tick_enqueue(callback);
        return promise;
    }

    Item result = js_fs_readFileSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_writeFile_promise(Item path, Item data) {
    Item result = js_fs_writeFileSync(path, data, make_js_undefined());
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_stat_promise(Item path, Item opts) {
    Item result = js_fs_statSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_lstat_promise(Item path, Item opts) {
    Item result = js_fs_lstatSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_mkdir_promise(Item path, Item opts) {
    Item result = js_fs_mkdirSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_access_promise(Item path, Item mode) {
    Item result = js_fs_accessSync(path, mode);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        fs_append_async_access_stack(err);
        return js_promise_reject(err);
    }
    return js_promise_resolve(result);
}

extern "C" Item js_fs_unlink_promise(Item path) {
    Item result = js_fs_unlinkSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_rename_promise(Item oldpath, Item newpath) {
    Item result = js_fs_renameSync(oldpath, newpath);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_readdir_promise(Item path) {
    Item result = js_fs_readdirSync(path, make_js_undefined());
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_open_promise(Item path, Item flags, Item mode) {
    Item fd = js_fs_openSync(path, flags, mode);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }
    if (get_type_id(fd) != LMD_TYPE_INT) {
        return js_promise_reject(js_new_error(make_string_item("open failed")));
    }

    Item handle = fs_create_filehandle(fd);
    return js_promise_resolve(handle);
}

extern "C" Item js_fs_chmod_promise(Item path, Item mode) {
    Item result = js_fs_chmodSync(path, mode);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_chown_promise(Item path, Item uid, Item gid) {
    Item result = js_fs_chownSync(path, uid, gid);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_lchown_promise(Item path, Item uid, Item gid) {
    Item result = js_fs_lchownSync(path, uid, gid);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_realpath_promise(Item path) {
    Item result = js_fs_realpathSync(path, make_js_undefined());
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_copyFile_promise(Item src, Item dest) {
    Item result = js_fs_copyFileSync(src, dest);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_mkdtemp_promise(Item prefix) {
    Item result = js_fs_mkdtempSync(prefix, make_js_undefined());
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_truncate_promise(Item path, Item len) {
    Item result = js_fs_truncateSync(path, len);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_symlink_promise(Item target, Item path) {
    Item result = js_fs_symlinkSync(target, path);
    return fs_promise_wrap_result(result);
}

// =============================================================================
// Additional async (callback) wrappers — synchronous I/O + callback invocation
// =============================================================================

static Item js_fs_rmdir_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_write(path)) {
        return fs_permission_callback_error(callback, "FileSystemWrite", path, NULL);
    }
    uv_fs_t req;
    int r = uv_fs_rmdir(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = { r < 0 ? make_fs_error(r, path) : ItemNull };
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_copyFile_async(Item src_item, Item dest_item, Item flags_or_cb, Item callback) {
    // copyFile(src, dest, [flags], callback) — flags is optional
    Item cb = callback;
    if (get_type_id(flags_or_cb) == LMD_TYPE_FUNC) {
        cb = flags_or_cb;
    }
    Item result = js_fs_copyFileSync(src_item, dest_item);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { get_type_id(result) == LMD_TYPE_NULL ? ItemNull : ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_realpath_async(Item path_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_realpathSync(path_item, opts_or_cb);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, make_js_undefined(), args, 2);
        } else {
            Item err = make_fs_error(UV_ENOENT, NULL);
            Item args[1] = {err};
            js_call_function(cb, make_js_undefined(), args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_mkdtemp_async(Item prefix_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_mkdtempSync(prefix_item, opts_or_cb);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_readlink_async(Item path_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_readlinkSync(path_item, opts_or_cb);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_symlink_async(Item target_item, Item path_item, Item type_or_cb, Item callback) {
    Item cb = (get_type_id(type_or_cb) == LMD_TYPE_FUNC) ? type_or_cb : callback;
    js_fs_symlinkSync(target_item, path_item);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_truncate_async(Item path_item, Item len_or_cb, Item callback) {
    Item cb = (get_type_id(len_or_cb) == LMD_TYPE_FUNC) ? len_or_cb : callback;
    Item len = (get_type_id(len_or_cb) == LMD_TYPE_FUNC) ? (Item){.item = i2it(0)} : len_or_cb;
    js_fs_truncateSync(path_item, len);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_appendFile_async(Item path_item, Item data_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    js_fs_appendFileSync(path_item, data_item, opts_or_cb);
    if (fs_callback_pending_exception(cb)) return make_js_undefined();
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_fchmod_async(Item fd_item, Item mode_item, Item callback) {
    Item result = js_fs_fchmodSync(fd_item, mode_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return make_js_undefined();
}

static Item js_fs_lchmod_async(Item path_item, Item mode_item, Item callback) {
    Item result = js_fs_lchmodSync(path_item, mode_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return make_js_undefined();
}

static Item js_fs_fchown_async(Item fd_item, Item uid_item, Item gid_item, Item callback) {
    Item result = js_fs_fchownSync(fd_item, uid_item, gid_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return make_js_undefined();
}

static Item js_fs_lchown_async(Item path_item, Item uid_item, Item gid_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return js_throw_invalid_arg_type("callback", "function", callback);
    Item result = js_fs_lchownSync(path_item, uid_item, gid_item);
    if (js_check_exception()) return ItemNull;
    Item args[1] = {ItemNull};
    js_call_function(callback, ItemNull, args, 1);
    (void)result;
    return make_js_undefined();
}

static Item js_fs_chown_async(Item path_item, Item uid_item, Item gid_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return js_throw_invalid_arg_type("callback", "function", callback);
    Item result = js_fs_chownSync(path_item, uid_item, gid_item);
    if (js_check_exception()) return ItemNull;
    Item args[1] = {ItemNull};
    js_call_function(callback, ItemNull, args, 1);
    (void)result;
    return make_js_undefined();
}

static Item js_fs_link_async(Item existing_item, Item new_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    Item result = js_fs_linkSync(existing_item, new_item);
    if (fs_callback_pending_exception(callback)) return make_js_undefined();
    Item args[1] = {ItemNull};
    js_call_function(callback, ItemNull, args, 1);
    (void)result;
    return make_js_undefined();
}

static Item js_fs_watcher_close(void) {
    return make_js_undefined();
}

static Item js_fs_watcher_ref(void) {
    return js_get_this();
}

static Item js_fs_watcher_unref(void) {
    return js_get_this();
}

static Item js_fs_make_watcher(void) {
    Item watcher = js_new_object();
    js_property_set(watcher, make_string_item("close"), js_new_function((void*)js_fs_watcher_close, 0));
    js_property_set(watcher, make_string_item("ref"), js_new_function((void*)js_fs_watcher_ref, 0));
    js_property_set(watcher, make_string_item("unref"), js_new_function((void*)js_fs_watcher_unref, 0));
    return watcher;
}

static Item js_fs_watch(Item path_item, Item options_or_listener, Item listener_item) {
    (void)listener_item;
    if (get_type_id(options_or_listener) != LMD_TYPE_FUNC &&
        !fs_validate_watch_options(options_or_listener)) {
        return ItemNull;
    }
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "filename", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);
    return js_fs_make_watcher();
}

static Item js_fs_watchFile(Item path_item, Item options_or_listener, Item listener_item) {
    Item listener = listener_item;
    if (get_type_id(options_or_listener) == LMD_TYPE_FUNC) listener = options_or_listener;
    if (get_type_id(listener) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("listener", "function", listener);
    }
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "filename", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    if (!js_permission_has_fs_read(path)) return js_permission_check_fs_read(path);
    return js_fs_make_watcher();
}

static Item js_fs_unwatchFile(Item path_item, Item listener_item) {
    (void)listener_item;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "filename", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    return make_js_undefined();
}

static Item js_fs_utimesSync(Item path_item, Item atime_item, Item mtime_item) {
    (void)atime_item;
    (void)mtime_item;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    return make_js_undefined();
}

static Item js_fs_utimes_async(Item path_item, Item atime_item, Item mtime_item, Item callback) {
    Item result = js_fs_utimesSync(path_item, atime_item, mtime_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return make_js_undefined();
}

static Item js_fs_toUnixTimestamp(Item value) {
    TypeId type = get_type_id(value);
    double number = 0.0;
    if (type == LMD_TYPE_INT) {
        number = (double)it2i(value);
    } else if (type == LMD_TYPE_FLOAT) {
        number = it2d(value);
    } else if (type == LMD_TYPE_STRING) {
        char buf[128];
        String* s = it2s(value);
        int len = (int)s->len;
        if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
        memcpy(buf, s->chars, (size_t)len);
        buf[len] = '\0';
        char* end = NULL;
        number = strtod(buf, &end);
        if (end == buf || *end != '\0') {
            return js_throw_invalid_arg_type("time", "number", value);
        }
    } else {
        return js_throw_invalid_arg_type("time", "number", value);
    }
    if (!isfinite(number)) {
        return js_throw_invalid_arg_type("time", "number", value);
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = number;
    return lambda_float_ptr_to_item(fp);
}

static Item js_fs_opendirSync(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    return js_new_object();
}

extern "C" Item js_internal_fs_validateOffsetLengthRead(Item offset_item, Item length_item, Item byte_length_item) {
    int64_t byte_length = 0;
    if (!fs_validate_int_range(byte_length_item, "byteLength", 0, 2147483647LL, &byte_length)) return ItemNull;
    int offset = 0, length = 0;
    if (!fs_validate_offset_length(offset_item, length_item, (int)byte_length, &offset, &length)) return ItemNull;
    return make_js_undefined();
}

extern "C" Item js_internal_fs_validateOffsetLengthWrite(Item offset_item, Item length_item, Item byte_length_item) {
    int64_t byte_length = 0;
    if (!fs_validate_int_range(byte_length_item, "byteLength", 0, 2147483647LL, &byte_length)) return ItemNull;
    int offset = 0, length = 0;
    if (!fs_validate_offset_length(offset_item, length_item, (int)byte_length, &offset, &length)) return ItemNull;
    return make_js_undefined();
}

extern "C" Item js_get_internal_fs_utils_namespace(void) {
    RootFrame roots((Context*)context, 1);
    Rooted<Item> ns_root(roots, js_new_object());
    js_fs_set_method(ns_root.get(), "validateOffsetLengthRead",
        (void*)js_internal_fs_validateOffsetLengthRead, 3);
    js_fs_set_method(ns_root.get(), "validateOffsetLengthWrite",
        (void*)js_internal_fs_validateOffsetLengthWrite, 3);
    js_property_set(ns_root.get(), make_string_item("default"), ns_root.get());
    return ns_root.get();
}

extern "C" Item js_get_fs_namespace(void) {
    if (fs_namespace.item != 0) return fs_namespace;

    // The static module cache is the exact owner while the large namespace is
    // being assembled and after it escapes this native helper.
    heap_register_gc_root(&fs_namespace.item);
    fs_namespace = js_new_object();

    RootFrame roots((Context*)context, 3);
    Rooted<Item> constants_root(roots, ItemNull);
    Rooted<Item> promises_root(roots, ItemNull);
    Rooted<Item> default_key_root(roots, ItemNull);

    // synchronous methods
    js_fs_set_method(fs_namespace, "readFileSync",    (void*)js_fs_readFileSync, 2);
    js_fs_set_method(fs_namespace, "writeFileSync",   (void*)js_fs_writeFileSync, 3);
    js_fs_set_method(fs_namespace, "existsSync",      (void*)js_fs_existsSync, 1);
    js_fs_set_method(fs_namespace, "unlinkSync",      (void*)js_fs_unlinkSync, 1);
    js_fs_set_method(fs_namespace, "mkdirSync",       (void*)js_fs_mkdirSync, 2);
    js_fs_set_method(fs_namespace, "rmdirSync",       (void*)js_fs_rmdirSync, 1);
    js_fs_set_method(fs_namespace, "renameSync",      (void*)js_fs_renameSync, 2);
    js_fs_set_method(fs_namespace, "readdirSync",     (void*)js_fs_readdirSync, 2);
    js_fs_set_method(fs_namespace, "statSync",        (void*)js_fs_statSync, 2);
    js_fs_set_method(fs_namespace, "appendFileSync",  (void*)js_fs_appendFileSync, 3);
    js_fs_set_method(fs_namespace, "createReadStream",(void*)js_fs_createReadStream, 2);
    js_fs_set_method(fs_namespace, "createWriteStream",(void*)js_fs_createWriteStream, 2);
    js_fs_set_method(fs_namespace, "ReadStream",      (void*)js_fs_createReadStream, 2);
    js_fs_set_method(fs_namespace, "WriteStream",     (void*)js_fs_createWriteStream, 2);

    // asynchronous (callback) methods
    js_fs_set_method(fs_namespace, "readFile",        (void*)js_fs_readFile, 3);
    js_fs_set_method(fs_namespace, "writeFile",       (void*)js_fs_writeFile, 4);
    js_fs_set_method(fs_namespace, "mkdir",           (void*)js_fs_mkdir_async, 3);
    js_fs_set_method(fs_namespace, "access",          (void*)js_fs_access_async, 3);
    js_fs_set_method(fs_namespace, "stat",            (void*)js_fs_stat_async, 3);
    js_fs_set_method(fs_namespace, "statfs",          (void*)js_fs_statfs_async, 3);
    js_fs_set_method(fs_namespace, "lstat",           (void*)js_fs_lstat_async, 3);
    js_fs_set_method(fs_namespace, "open",            (void*)js_fs_open_async, 4);
    js_fs_set_method(fs_namespace, "close",           (void*)js_fs_close_async, 2);
    js_fs_set_method(fs_namespace, "chmod",           (void*)js_fs_chmod_async, 3);
    js_fs_set_method(fs_namespace, "unlink",          (void*)js_fs_unlink_async, 2);
    Item exists_fn = js_fs_set_method(fs_namespace, "exists",          (void*)js_fs_exists_async, 2);
    js_fs_set_custom_promisify(exists_fn, (void*)js_fs_exists_promisified, 1);
    js_fs_set_method(fs_namespace, "rename",          (void*)js_fs_rename_async, 3);
    js_fs_set_method(fs_namespace, "readdir",         (void*)js_fs_readdir_async, 3);
    js_fs_set_method(fs_namespace, "fstat",           (void*)js_fs_fstat_async, 3);
    js_fs_set_method(fs_namespace, "rmdir",           (void*)js_fs_rmdir_async, 2);
    js_fs_set_method(fs_namespace, "copyFile",        (void*)js_fs_copyFile_async, 4);
    Item realpath_fn = js_fs_set_method(fs_namespace, "realpath",        (void*)js_fs_realpath_async, 3);
    js_property_set(realpath_fn, make_string_item("native"), realpath_fn);
    js_fs_set_method(fs_namespace, "mkdtemp",         (void*)js_fs_mkdtemp_async, 3);
    js_fs_set_method(fs_namespace, "readlink",        (void*)js_fs_readlink_async, 3);
    js_fs_set_method(fs_namespace, "symlink",         (void*)js_fs_symlink_async, 4);
    js_fs_set_method(fs_namespace, "truncate",        (void*)js_fs_truncate_async, 3);
    js_fs_set_method(fs_namespace, "appendFile",      (void*)js_fs_appendFile_async, 4);
    js_fs_set_method(fs_namespace, "fchmod",          (void*)js_fs_fchmod_async, 3);
    js_fs_set_method(fs_namespace, "lchmod",          (void*)js_fs_lchmod_async, 3);
    js_fs_set_method(fs_namespace, "fchown",          (void*)js_fs_fchown_async, 4);
    js_fs_set_method(fs_namespace, "lchown",          (void*)js_fs_lchown_async, 4);
    js_fs_set_method(fs_namespace, "chown",           (void*)js_fs_chown_async, 4);
    js_fs_set_method(fs_namespace, "link",            (void*)js_fs_link_async, 3);
    js_fs_set_method(fs_namespace, "watch",           (void*)js_fs_watch, 3);
    js_fs_set_method(fs_namespace, "watchFile",       (void*)js_fs_watchFile, 3);
    js_fs_set_method(fs_namespace, "unwatchFile",     (void*)js_fs_unwatchFile, 2);
    js_fs_set_method(fs_namespace, "utimes",          (void*)js_fs_utimes_async, 4);

    // additional sync methods
    js_fs_set_method(fs_namespace, "copyFileSync",    (void*)js_fs_copyFileSync, 2);
    Item realpath_sync_fn = js_fs_set_method(fs_namespace, "realpathSync",    (void*)js_fs_realpathSync, 2);
    js_property_set(realpath_sync_fn, make_string_item("native"), realpath_sync_fn);
    js_fs_set_method(fs_namespace, "statfsSync",      (void*)js_fs_statfsSync, 2);
    js_fs_set_method(fs_namespace, "accessSync",      (void*)js_fs_accessSync, 2);
    js_fs_set_method(fs_namespace, "rmSync",          (void*)js_fs_rmSync, 2);
    js_fs_set_method(fs_namespace, "mkdtempSync",     (void*)js_fs_mkdtempSync, 2);
    js_fs_set_method(fs_namespace, "chmodSync",       (void*)js_fs_chmodSync, 2);
    js_fs_set_method(fs_namespace, "fchmodSync",      (void*)js_fs_fchmodSync, 2);
    js_fs_set_method(fs_namespace, "lchmodSync",      (void*)js_fs_lchmodSync, 2);
    js_fs_set_method(fs_namespace, "chownSync",       (void*)js_fs_chownSync, 3);
    js_fs_set_method(fs_namespace, "lchownSync",      (void*)js_fs_lchownSync, 3);
    js_fs_set_method(fs_namespace, "fchownSync",      (void*)js_fs_fchownSync, 3);
    js_fs_set_method(fs_namespace, "symlinkSync",     (void*)js_fs_symlinkSync, 2);
    js_fs_set_method(fs_namespace, "readlinkSync",    (void*)js_fs_readlinkSync, 2);
    js_fs_set_method(fs_namespace, "lstatSync",       (void*)js_fs_lstatSync, 2);
    js_fs_set_method(fs_namespace, "truncateSync",    (void*)js_fs_truncateSync, 2);
    js_fs_set_method(fs_namespace, "utimesSync",      (void*)js_fs_utimesSync, 3);
    js_fs_set_method(fs_namespace, "linkSync",        (void*)js_fs_linkSync, 2);
    js_fs_set_method(fs_namespace, "opendirSync",     (void*)js_fs_opendirSync, 2);
    js_fs_set_method(fs_namespace, "_toUnixTimestamp",(void*)js_fs_toUnixTimestamp, 1);

    // file descriptor operations
    js_fs_set_method(fs_namespace, "openSync",        (void*)js_fs_openSync, 3);
    js_fs_set_method(fs_namespace, "closeSync",       (void*)js_fs_closeSync, 1);
    Item read_fn = js_fs_set_method(fs_namespace, "read",            (void*)js_fs_read, 6);
    js_fs_set_custom_promisify_args(read_fn, "bytesRead", "buffer");
    js_fs_set_method(fs_namespace, "readSync",        (void*)js_fs_readSync, 5);
    Item write_fn = js_fs_set_method(fs_namespace, "write",           (void*)js_fs_write, 6);
    js_fs_set_custom_promisify_args(write_fn, "bytesWritten", "buffer");
    js_fs_set_method(fs_namespace, "writeSync",       (void*)js_fs_writeSync, 5);
    Item readv_fn = js_fs_set_method(fs_namespace, "readv",           (void*)js_fs_readv, 4);
    js_fs_set_custom_promisify_args(readv_fn, "bytesRead", "buffers");
    js_fs_set_method(fs_namespace, "readvSync",       (void*)js_fs_readvSync, 3);
    Item writev_fn = js_fs_set_method(fs_namespace, "writev",          (void*)js_fs_writev, 4);
    js_fs_set_custom_promisify_args(writev_fn, "bytesWritten", "buffer");
    js_fs_set_method(fs_namespace, "writevSync",      (void*)js_fs_writevSync, 3);
    js_fs_set_method(fs_namespace, "fstatSync",       (void*)js_fs_fstatSync, 2);

    // fs.constants — null prototype per Node.js spec
    extern Item js_object_create(Item proto);
    constants_root.set(js_object_create(ItemNull));
    Item constants = constants_root.get();
    js_property_set(constants, make_string_item("F_OK"), (Item){.item = i2it(0)});
    js_property_set(constants, make_string_item("R_OK"), (Item){.item = i2it(4)});
    js_property_set(constants, make_string_item("W_OK"), (Item){.item = i2it(2)});
    js_property_set(constants, make_string_item("X_OK"), (Item){.item = i2it(1)});
    js_property_set(constants, make_string_item("O_RDONLY"),   (Item){.item = i2it(UV_FS_O_RDONLY)});
    js_property_set(constants, make_string_item("O_WRONLY"),   (Item){.item = i2it(UV_FS_O_WRONLY)});
    js_property_set(constants, make_string_item("O_RDWR"),     (Item){.item = i2it(UV_FS_O_RDWR)});
    js_property_set(constants, make_string_item("O_CREAT"),    (Item){.item = i2it(UV_FS_O_CREAT)});
    js_property_set(constants, make_string_item("O_TRUNC"),    (Item){.item = i2it(UV_FS_O_TRUNC)});
    js_property_set(constants, make_string_item("O_APPEND"),   (Item){.item = i2it(UV_FS_O_APPEND)});
    js_property_set(constants, make_string_item("O_EXCL"),     (Item){.item = i2it(UV_FS_O_EXCL)});
    // POSIX file mode constants
    js_property_set(constants, make_string_item("S_IFMT"),  (Item){.item = i2it(S_IFMT)});
    js_property_set(constants, make_string_item("S_IFREG"), (Item){.item = i2it(S_IFREG)});
    js_property_set(constants, make_string_item("S_IFDIR"), (Item){.item = i2it(S_IFDIR)});
    js_property_set(constants, make_string_item("S_IFCHR"), (Item){.item = i2it(S_IFCHR)});
    js_property_set(constants, make_string_item("S_IFBLK"), (Item){.item = i2it(S_IFBLK)});
    js_property_set(constants, make_string_item("S_IFIFO"), (Item){.item = i2it(S_IFIFO)});
    js_property_set(constants, make_string_item("S_IFLNK"), (Item){.item = i2it(S_IFLNK)});
    js_property_set(constants, make_string_item("S_IFSOCK"), (Item){.item = i2it(S_IFSOCK)});
    js_property_set(constants, make_string_item("S_IRUSR"), (Item){.item = i2it(S_IRUSR)});
    js_property_set(constants, make_string_item("S_IWUSR"), (Item){.item = i2it(S_IWUSR)});
    js_property_set(constants, make_string_item("S_IXUSR"), (Item){.item = i2it(S_IXUSR)});
    js_property_set(constants, make_string_item("S_IRGRP"), (Item){.item = i2it(S_IRGRP)});
    js_property_set(constants, make_string_item("S_IWGRP"), (Item){.item = i2it(S_IWGRP)});
    js_property_set(constants, make_string_item("S_IXGRP"), (Item){.item = i2it(S_IXGRP)});
    js_property_set(constants, make_string_item("S_IROTH"), (Item){.item = i2it(S_IROTH)});
    js_property_set(constants, make_string_item("S_IWOTH"), (Item){.item = i2it(S_IWOTH)});
    js_property_set(constants, make_string_item("S_IXOTH"), (Item){.item = i2it(S_IXOTH)});
    // UV_DIRENT_ constants
    js_property_set(constants, make_string_item("UV_DIRENT_UNKNOWN"), (Item){.item = i2it(UV_DIRENT_UNKNOWN)});
    js_property_set(constants, make_string_item("UV_DIRENT_FILE"),    (Item){.item = i2it(UV_DIRENT_FILE)});
    js_property_set(constants, make_string_item("UV_DIRENT_DIR"),     (Item){.item = i2it(UV_DIRENT_DIR)});
    js_property_set(constants, make_string_item("UV_DIRENT_LINK"),    (Item){.item = i2it(UV_DIRENT_LINK)});
    js_property_set(constants, make_string_item("UV_DIRENT_FIFO"),    (Item){.item = i2it(UV_DIRENT_FIFO)});
    js_property_set(constants, make_string_item("UV_DIRENT_SOCKET"),  (Item){.item = i2it(UV_DIRENT_SOCKET)});
    js_property_set(constants, make_string_item("UV_DIRENT_CHAR"),    (Item){.item = i2it(UV_DIRENT_CHAR)});
    js_property_set(constants, make_string_item("UV_DIRENT_BLOCK"),   (Item){.item = i2it(UV_DIRENT_BLOCK)});
    // UV_FS_SYMLINK constants
    js_property_set(constants, make_string_item("UV_FS_SYMLINK_DIR"),      (Item){.item = i2it(UV_FS_SYMLINK_DIR)});
    js_property_set(constants, make_string_item("UV_FS_SYMLINK_JUNCTION"), (Item){.item = i2it(UV_FS_SYMLINK_JUNCTION)});
    // COPYFILE constants
    js_property_set(constants, make_string_item("COPYFILE_EXCL"),         (Item){.item = i2it(UV_FS_COPYFILE_EXCL)});
    js_property_set(constants, make_string_item("COPYFILE_FICLONE"),      (Item){.item = i2it(UV_FS_COPYFILE_FICLONE)});
    js_property_set(constants, make_string_item("COPYFILE_FICLONE_FORCE"),(Item){.item = i2it(UV_FS_COPYFILE_FICLONE_FORCE)});
    js_property_set(fs_namespace, make_string_item("constants"), constants);

    // fs.promises — promise-based API
    {
        extern Item js_promise_resolve(Item value);
        extern Item js_promise_reject(Item reason);

        promises_root.set(js_new_object());
        Item promises = promises_root.get();

        // fs.promises.readFile(path, options?) — returns Promise<Buffer|string>
        // We wrap the sync version for simplicity
        auto make_promise_wrapper_1 = [](Item (*sync_fn)(Item)) -> void* {
            // Can't create closures in C, so we'll register each individually
            return nullptr;
        };
        (void)make_promise_wrapper_1;

        // Create a set of promise-returning wrapper functions
        // Each calls the sync version and wraps result in a resolved promise
        // (or rejected promise on error)
        js_fs_set_method(promises, "readFile",    (void*)js_fs_readFile_promise, 2);
        js_fs_set_method(promises, "writeFile",   (void*)js_fs_writeFile_promise, 2);
        js_fs_set_method(promises, "stat",        (void*)js_fs_stat_promise, 2);
        js_fs_set_method(promises, "lstat",       (void*)js_fs_lstat_promise, 2);
        js_fs_set_method(promises, "mkdir",       (void*)js_fs_mkdir_promise, 2);
        js_fs_set_method(promises, "access",      (void*)js_fs_access_promise, 2);
        js_fs_set_method(promises, "unlink",      (void*)js_fs_unlink_promise, 1);
        js_fs_set_method(promises, "rm",          (void*)js_fs_unlink_promise, 1);
        js_fs_set_method(promises, "rename",      (void*)js_fs_rename_promise, 2);
        js_fs_set_method(promises, "readdir",     (void*)js_fs_readdir_promise, 1);
        js_fs_set_method(promises, "open",        (void*)js_fs_open_promise, 3);
        js_fs_set_method(promises, "chmod",       (void*)js_fs_chmod_promise, 2);
        js_fs_set_method(promises, "chown",       (void*)js_fs_chown_promise, 3);
        js_fs_set_method(promises, "lchown",      (void*)js_fs_lchown_promise, 3);
        js_fs_set_method(promises, "realpath",    (void*)js_fs_realpath_promise, 1);
        js_fs_set_method(promises, "copyFile",    (void*)js_fs_copyFile_promise, 2);
        js_fs_set_method(promises, "mkdtemp",     (void*)js_fs_mkdtemp_promise, 1);
        js_fs_set_method(promises, "truncate",    (void*)js_fs_truncate_promise, 2);
        js_fs_set_method(promises, "symlink",     (void*)js_fs_symlink_promise, 2);

        // fs.promises.constants === fs.constants
        js_property_set(promises, make_string_item("constants"), constants);

        js_property_set(fs_namespace, make_string_item("promises"), promises);
    }

    // set "default" export to the namespace itself (for `import fs from 'fs'`)
    default_key_root.set(make_string_item("default"));
    js_property_set(fs_namespace, default_key_root.get(), fs_namespace);

    return fs_namespace;
}

extern "C" Item js_get_fs_promises_namespace(void) {
    Item fs = js_get_fs_namespace();
    return js_property_get(fs, make_string_item("promises"));
}

extern "C" Item js_get_internal_fs_promises_namespace(void) {
    if (fs_internal_promises_namespace.item != 0) return fs_internal_promises_namespace;

    heap_register_gc_root(&fs_internal_promises_namespace.item);
    fs_internal_promises_namespace = js_new_object();
    js_property_set(fs_internal_promises_namespace, make_string_item("FileHandle"),
                    fs_get_filehandle_constructor());
    js_property_set(fs_internal_promises_namespace, make_string_item("default"),
                    fs_internal_promises_namespace);
    return fs_internal_promises_namespace;
}

// Reset fs namespace (for re-initialization between runs)
extern "C" void js_fs_reset(void) {
    fs_namespace = (Item){0};
    fs_internal_promises_namespace = (Item){0};
    fs_filehandle_ctor = (Item){0};
    fs_filehandle_proto = (Item){0};
}
