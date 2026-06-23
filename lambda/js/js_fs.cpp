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
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../lib/mem.h"
#include "../../lib/file.h"
#ifdef _WIN32
// S_IFSOCK is not defined on Windows
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000
#endif
#endif

// Helper: make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

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
    int64_t offset = 0, length = byte_length;
    TypeId offset_type = get_type_id(offset_item);
    TypeId length_type = get_type_id(length_item);
    if (offset_type != LMD_TYPE_UNDEFINED && offset_type != LMD_TYPE_NULL) {
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
static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

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
    Map* m = buf.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (!ta || !ta->data) { *out_len = 0; return NULL; }
    *out_len = ta->byte_length;
    return (uint8_t*)ta->data;
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

// Helper: build a Stats object from uv_stat_t
static Item make_stats_object(const uv_stat_t* st) {
    Item obj = js_new_object();
    
    // Store mode for isFile/isDirectory/etc methods
    js_property_set(obj, make_string_item("__mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    js_property_set(obj, make_string_item("mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    js_property_set(obj, make_string_item("size"), (Item){.item = i2it((int64_t)st->st_size)});
    js_property_set(obj, make_string_item("uid"), (Item){.item = i2it((int64_t)st->st_uid)});
    js_property_set(obj, make_string_item("gid"), (Item){.item = i2it((int64_t)st->st_gid)});
    js_property_set(obj, make_string_item("nlink"), (Item){.item = i2it((int64_t)st->st_nlink)});
    js_property_set(obj, make_string_item("ino"), (Item){.item = i2it((int64_t)st->st_ino)});
    js_property_set(obj, make_string_item("dev"), (Item){.item = i2it((int64_t)st->st_dev)});
    js_property_set(obj, make_string_item("rdev"), (Item){.item = i2it((int64_t)st->st_rdev)});
    js_property_set(obj, make_string_item("blksize"), (Item){.item = i2it((int64_t)st->st_blksize)});
    js_property_set(obj, make_string_item("blocks"), (Item){.item = i2it((int64_t)st->st_blocks)});

    // Time properties in milliseconds
    auto set_time = [&](const char* name, const uv_timespec_t& ts) {
        double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        Item val;
        val.item = d2it(fp);
        js_property_set(obj, make_string_item(name), val);
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
        ms_item.item = d2it(fp);
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

// fs.readFileSync(path[, encoding])
// Returns file contents as a string (assumes UTF-8)
extern "C" Item js_fs_readFileSync(Item path_item, Item encoding_item) {
    if (!fs_validate_encoding_options(encoding_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        return js_throw_system_error(fd, "open", path);
    }

    // stat to get file size
    uv_fs_t stat_req;
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

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return js_throw_system_error(fd, "open", path);

    uv_fs_t stat_req;
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

    Item chunk = js_typed_array_new(JS_TYPED_UINT8, bytes_read);
    if (js_is_typed_array(chunk)) {
        JsTypedArray* ta = (JsTypedArray*)chunk.map->data;
        if (ta && ta->data && bytes_read > 0) {
            memcpy(ta->data, data, (size_t)bytes_read);
        }
    }
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

static Item js_fs_readstream_close(void) {
    return js_stream_destroy(js_get_this(), make_js_undefined());
}

static Item js_fs_readstream_end_later(Item stream) {
    Item destroyed = js_property_get(stream, make_string_item("__destroyed__"));
    if (get_type_id(destroyed) == LMD_TYPE_BOOL && it2b(destroyed)) {
        return make_js_undefined();
    }
    js_readable_push(stream, ItemNull);
    return make_js_undefined();
}

extern "C" Item js_fs_createReadStream(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    Item stream = js_readable_new(ItemNull);
    if (stream.item == 0) return ItemNull;
    js_property_set(stream, make_string_item("__readstream_path__"), path_item);
    js_property_set(stream, make_string_item("__readstream_drained__"), (Item){.item = b2it(false)});
    js_property_set(stream, make_string_item("close"), js_new_function((void*)js_fs_readstream_close, 0));

    Item chunk = js_fs_read_file_buffer(path);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(stream, err);
        return stream;
    }

    js_readable_push(stream, chunk);
    Item end_fn = js_bind_function(js_new_function((void*)js_fs_readstream_end_later, 1),
                                   make_js_undefined(), &stream, 1);
    js_setImmediate(end_fn);
    return stream;
}

extern "C" Item js_fs_createWriteStream(Item path_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;
    (void)path;
    return js_new_object();
}

// fs.writeFileSync(path, data)
extern "C" Item js_fs_writeFileSync(Item path_item, Item data_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

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

// fs.statSync(path) → Stats object with isFile(), isDirectory(), etc.
extern "C" Item js_fs_statSync(Item path_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "stat", path);
    }

    Item obj = make_stats_object(&req.statbuf);
    uv_fs_req_cleanup(&req);
    return obj;
}

// fs.appendFileSync(path, data)
extern "C" Item js_fs_appendFileSync(Item path_item, Item data_item, Item options_item) {
    if (!fs_validate_encoding_options(options_item)) return ItemNull;
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

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

// fs.lstatSync(path) — like statSync but doesn't follow symlinks
extern "C" Item js_fs_lstatSync(Item path_item) {
    char path_buf[1024];
    const char* path = fs_path_to_cstr(path_item, "path", path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "lstat", path);
    }

    Item obj = make_stats_object(&req.statbuf);
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
    char* buffer;         // read buffer (for readFile)
    size_t buffer_size;
    int fd;               // file descriptor
    char path[1024];      // file path (for multi-step operations)
} JsFsReq;

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
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
    } else {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item data = make_string_item(fsreq->buffer, result);
            Item args[2] = {ItemNull, data};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
    }

    if (fsreq->buffer) mem_free(fsreq->buffer);
    uv_fs_req_cleanup(req);
    mem_free(fsreq);
}

static void on_fs_open_for_read(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int fd = (int)req->result;
    uv_fs_req_cleanup(req);

    if (fd < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(fd, fsreq->path);
            Item args[2] = {err, ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
        mem_free(fsreq);
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
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
        mem_free(fsreq);
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
        mem_free(fsreq);
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

    JsFsReq* fsreq = (JsFsReq*)mem_calloc(1, sizeof(JsFsReq), MEM_CAT_JS_RUNTIME);
    if (!fsreq) return ItemNull;

    fsreq->callback = callback;
    snprintf(fsreq->path, sizeof(fsreq->path), "%s", path);
    fsreq->req.data = fsreq;

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

    int64_t length = 0;
    if (get_type_id(len_item) == LMD_TYPE_INT) length = it2i(len_item);

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

    JsTypedArray* ta = fs_get_typed_array(buffer_item);
    if (!ta) return js_throw_invalid_arg_type("buffer", "Buffer, TypedArray, or DataView", buffer_item);
    int blen = js_typed_array_byte_length(buffer_item);
    uint8_t* data = (uint8_t*)js_typed_array_current_data_ptr(buffer_item);

    int offset = 0, length = blen;
    if (!fs_validate_offset_length(offset_item, length_item, blen, &offset, &length)) return ItemNull;
    if (blen == 0 && length > 0) return fs_throw_empty_read_buffer(ta);
    if (length <= 0) return (Item){.item = i2it(0)};
    if (!data) return (Item){.item = i2it(0)};

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) position = it2i(position_item);

    uv_buf_t buf = uv_buf_init((char*)(data + offset), length);
    uv_fs_t req;
    int nread = uv_fs_read(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nread < 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((int64_t)nread)};
}

extern "C" Item js_fs_read(Item fd_item, Item buffer_item, Item offset_item, Item length_item, Item position_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    Item bytes_read = js_fs_readSync(fd_item, buffer_item, offset_item, length_item, position_item);
    if (js_check_exception()) return ItemNull;

    Item args[3] = {ItemNull, bytes_read, buffer_item};
    js_call_function(callback, ItemNull, args, 3);
    return make_js_undefined();
}

static Item js_fs_filehandle_read(Item buffer_item, Item offset_item, Item length_item, Item position_item) {
    Item self = js_get_this();
    Item fd_item = js_property_get(self, make_string_item("__fd"));
    Item bytes_read = js_fs_readSync(fd_item, buffer_item, offset_item, length_item, position_item);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }

    Item result = js_new_object();
    js_property_set(result, make_string_item("bytesRead"), bytes_read);
    js_property_set(result, make_string_item("buffer"), buffer_item);
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
    if (!write_buf) return (Item){.item = i2it(0)};

    int offset = 0, length = write_len;
    if (!fs_validate_offset_length(offset_item, length_item, write_len, &offset, &length)) return ItemNull;
    if (length <= 0) return (Item){.item = i2it(0)};

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) position = it2i(position_item);

    uv_buf_t buf = uv_buf_init((char*)(write_buf + offset), length);
    uv_fs_t req;
    int nwritten = uv_fs_write(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nwritten < 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((int64_t)nwritten)};
}

extern "C" Item js_fs_fstatSync(Item fd_item) {
    int fd = 0;
    if (!fs_validate_fd(fd_item, &fd)) return ItemNull;
    uv_fs_t req;
    int r = uv_fs_fstat(lambda_uv_loop(), &req, fd, NULL);
    if (r < 0) { uv_fs_req_cleanup(&req); return ItemNull; }

    Item result = make_stats_object(&req.statbuf);
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
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    // Reuse statSync to build the stat result
    Item stat_result = js_fs_statSync(path_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (stat_result.item == ITEM_NULL || get_type_id(stat_result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, stat_result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.lstat(path[, options], callback)
static Item js_fs_lstat_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item stat_result = js_fs_lstatSync(path_item);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (stat_result.item == ITEM_NULL || get_type_id(stat_result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, stat_result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
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
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item result = js_fs_fstatSync(fd_item);
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

static Item fs_namespace = {0};

static void js_fs_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

// ─── fs.promises wrapper functions ─────────────────────────────────────────
// Each wraps the sync version, returning a resolved/rejected Promise
extern Item js_promise_resolve(Item value);
extern Item js_promise_reject(Item reason);
extern int js_check_exception(void);
extern Item js_clear_exception(void);

static Item fs_promise_wrap_result(Item result) {
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }
    return js_promise_resolve(result);
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

extern "C" Item js_fs_readFile_promise(Item path, Item opts) {
    Item result = js_fs_readFileSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_writeFile_promise(Item path, Item data) {
    Item result = js_fs_writeFileSync(path, data, make_js_undefined());
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_stat_promise(Item path) {
    Item result = js_fs_statSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_lstat_promise(Item path) {
    Item result = js_fs_lstatSync(path);
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

    Item handle = js_new_object();
    js_property_set(handle, make_string_item("__fd"), fd);
    js_property_set(handle, make_string_item("fd"), fd);
    js_property_set(handle, make_string_item("read"),
                    js_new_function((void*)js_fs_filehandle_read, 4));
    js_property_set(handle, make_string_item("close"),
                    js_new_function((void*)js_fs_filehandle_close, 0));
    js_property_set(handle, make_string_item("__sym_14"),
                    js_new_function((void*)js_fs_filehandle_close, 0));
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
    if (js_check_exception()) return ItemNull;
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { get_type_id(result) == LMD_TYPE_NULL ? ItemNull : ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_realpath_async(Item path_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_realpathSync(path_item, opts_or_cb);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_ENOENT, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_mkdtemp_async(Item prefix_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_mkdtempSync(prefix_item, opts_or_cb);
    if (js_check_exception()) return ItemNull;
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
    if (js_check_exception()) return ItemNull;
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
    if (js_check_exception()) return ItemNull;
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
    if (js_check_exception()) return ItemNull;
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_appendFile_async(Item path_item, Item data_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    js_fs_appendFileSync(path_item, data_item, opts_or_cb);
    if (js_check_exception()) return ItemNull;
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
    if (js_check_exception()) return ItemNull;
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
    return (Item){.item = d2it(fp)};
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
    Item ns = js_new_object();
    js_property_set(ns, make_string_item("validateOffsetLengthRead"),
                    js_new_function((void*)js_internal_fs_validateOffsetLengthRead, 3));
    js_property_set(ns, make_string_item("validateOffsetLengthWrite"),
                    js_new_function((void*)js_internal_fs_validateOffsetLengthWrite, 3));
    js_property_set(ns, make_string_item("default"), ns);
    return ns;
}

extern "C" Item js_get_fs_namespace(void) {
    if (fs_namespace.item != 0) return fs_namespace;

    fs_namespace = js_new_object();

    // synchronous methods
    js_fs_set_method(fs_namespace, "readFileSync",    (void*)js_fs_readFileSync, 2);
    js_fs_set_method(fs_namespace, "writeFileSync",   (void*)js_fs_writeFileSync, 3);
    js_fs_set_method(fs_namespace, "existsSync",      (void*)js_fs_existsSync, 1);
    js_fs_set_method(fs_namespace, "unlinkSync",      (void*)js_fs_unlinkSync, 1);
    js_fs_set_method(fs_namespace, "mkdirSync",       (void*)js_fs_mkdirSync, 2);
    js_fs_set_method(fs_namespace, "rmdirSync",       (void*)js_fs_rmdirSync, 1);
    js_fs_set_method(fs_namespace, "renameSync",      (void*)js_fs_renameSync, 2);
    js_fs_set_method(fs_namespace, "readdirSync",     (void*)js_fs_readdirSync, 2);
    js_fs_set_method(fs_namespace, "statSync",        (void*)js_fs_statSync, 1);
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
    js_fs_set_method(fs_namespace, "lstat",           (void*)js_fs_lstat_async, 3);
    js_fs_set_method(fs_namespace, "open",            (void*)js_fs_open_async, 4);
    js_fs_set_method(fs_namespace, "close",           (void*)js_fs_close_async, 2);
    js_fs_set_method(fs_namespace, "chmod",           (void*)js_fs_chmod_async, 3);
    js_fs_set_method(fs_namespace, "unlink",          (void*)js_fs_unlink_async, 2);
    js_fs_set_method(fs_namespace, "exists",          (void*)js_fs_exists_async, 2);
    js_fs_set_method(fs_namespace, "rename",          (void*)js_fs_rename_async, 3);
    js_fs_set_method(fs_namespace, "readdir",         (void*)js_fs_readdir_async, 3);
    js_fs_set_method(fs_namespace, "fstat",           (void*)js_fs_fstat_async, 3);
    js_fs_set_method(fs_namespace, "rmdir",           (void*)js_fs_rmdir_async, 2);
    js_fs_set_method(fs_namespace, "copyFile",        (void*)js_fs_copyFile_async, 4);
    js_fs_set_method(fs_namespace, "realpath",        (void*)js_fs_realpath_async, 3);
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
    js_fs_set_method(fs_namespace, "realpathSync",    (void*)js_fs_realpathSync, 2);
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
    js_fs_set_method(fs_namespace, "lstatSync",       (void*)js_fs_lstatSync, 1);
    js_fs_set_method(fs_namespace, "truncateSync",    (void*)js_fs_truncateSync, 2);
    js_fs_set_method(fs_namespace, "utimesSync",      (void*)js_fs_utimesSync, 3);
    js_fs_set_method(fs_namespace, "linkSync",        (void*)js_fs_linkSync, 2);
    js_fs_set_method(fs_namespace, "opendirSync",     (void*)js_fs_opendirSync, 2);
    js_fs_set_method(fs_namespace, "_toUnixTimestamp",(void*)js_fs_toUnixTimestamp, 1);

    // file descriptor operations
    js_fs_set_method(fs_namespace, "openSync",        (void*)js_fs_openSync, 3);
    js_fs_set_method(fs_namespace, "closeSync",       (void*)js_fs_closeSync, 1);
    js_fs_set_method(fs_namespace, "read",            (void*)js_fs_read, 6);
    js_fs_set_method(fs_namespace, "readSync",        (void*)js_fs_readSync, 5);
    js_fs_set_method(fs_namespace, "writeSync",       (void*)js_fs_writeSync, 5);
    js_fs_set_method(fs_namespace, "fstatSync",       (void*)js_fs_fstatSync, 1);

    // fs.constants — null prototype per Node.js spec
    extern Item js_object_create(Item proto);
    Item constants = js_object_create(ItemNull);
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

        Item promises = js_new_object();

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
        js_fs_set_method(promises, "stat",        (void*)js_fs_stat_promise, 1);
        js_fs_set_method(promises, "lstat",       (void*)js_fs_lstat_promise, 1);
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
    Item default_key = make_string_item("default");
    js_property_set(fs_namespace, default_key, fs_namespace);

    return fs_namespace;
}

// Reset fs namespace (for re-initialization between runs)
extern "C" void js_fs_reset(void) {
    fs_namespace = (Item){0};
}
