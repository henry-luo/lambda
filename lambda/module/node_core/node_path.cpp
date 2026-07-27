/**
 * node_path.cpp — Node.js-style 'path' module hosted through Jube
 *
 * Provides path manipulation utilities matching Node.js path API.
 * Registered by node-core through its Jube namespace descriptor.
 */
#include "node_path.hpp"
#include "../../../lib/file.h"
#include "../../../lib/mem.h"
#include "../../../lib/path_str.h"

#include <cstring>
#include <cstdio>

static const JubeHostAPI* node_path_host = NULL;
static void* node_path_session = NULL;
static bool node_path_rooted = false;

static const char* js_type_name_for_error(Item value);

static int node_path_value_kind(Item value) {
    return node_path_host && node_path_host->value && node_path_host->value->kind ?
        node_path_host->value->kind(value) : JUBE_VALUE_OTHER;
}

static Item node_path_string(const char* text, int length) {
    if (!node_path_host || !node_path_host->value || !node_path_host->value->string_from_utf8_n ||
            !text || length < 0) return ItemNull;
    return node_path_host->value->string_from_utf8_n(text, (size_t)length);
}

static Item node_path_string(const char* text) {
    return node_path_string(text, text ? (int)strlen(text) : 0);
}

static Item node_path_throw_type_error(const char* code, const char* message) {
    if (!node_path_host || !node_path_host->node || !node_path_host->node->error ||
            !node_path_host->node->error->throw_type_error_code) return ItemNull;
    return node_path_host->node->error->throw_type_error_code(node_path_session, code, message);
}

static void node_path_describe_invalid_object(Item value, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    int kind = node_path_value_kind(value);
    if (kind == JUBE_VALUE_NULL || kind == JUBE_VALUE_UNDEFINED) {
        snprintf(out, (size_t)out_size, "Received %s", kind == JUBE_VALUE_NULL ? "null" : "undefined");
        return;
    }
    Item text = node_path_host && node_path_host->script && node_path_host->script->to_string ?
        node_path_host->script->to_string(value) : ItemNull;
    char value_text[128] = {};
    if (!node_path_host || !node_path_host->value || !node_path_host->value->string_copy ||
            !node_path_host->value->string_copy(text, value_text, sizeof(value_text), NULL)) {
        snprintf(out, (size_t)out_size, "Received type %s", js_type_name_for_error(value));
        return;
    }
    if (kind == JUBE_VALUE_STRING) {
        snprintf(out, (size_t)out_size, "Received type string ('%s')", value_text);
    } else {
        snprintf(out, (size_t)out_size, "Received type %s (%s)", js_type_name_for_error(value), value_text);
    }
}

#define get_type_id(value) node_path_value_kind(value)
#define LMD_TYPE_UNDEFINED JUBE_VALUE_UNDEFINED
#define LMD_TYPE_NULL JUBE_VALUE_NULL
#define LMD_TYPE_BOOL JUBE_VALUE_BOOLEAN
#define LMD_TYPE_INT JUBE_VALUE_NUMBER
#define LMD_TYPE_INT64 JUBE_VALUE_NUMBER
#define LMD_TYPE_FLOAT JUBE_VALUE_NUMBER
#define LMD_TYPE_STRING JUBE_VALUE_STRING
#define LMD_TYPE_ARRAY JUBE_VALUE_ARRAY
#define LMD_TYPE_MAP JUBE_VALUE_OBJECT
#define LMD_TYPE_FUNC JUBE_VALUE_FUNCTION
#define make_string_item node_path_string
#define js_array_length(ARG_ITEM) node_path_host->value->array_length(ARG_ITEM)
#define js_array_get_int(ARG_ITEM, ARG_INDEX) node_path_host->value->array_get(ARG_ITEM, ARG_INDEX)
#define js_new_object() node_path_host->value->new_object()
#define js_property_get(ARG_OBJECT, ARG_KEY) node_path_host->value->property_get(ARG_OBJECT, ARG_KEY)
#define js_property_set(ARG_OBJECT, ARG_KEY, ARG_VALUE) node_path_host->value->property_set(ARG_OBJECT, ARG_KEY, ARG_VALUE)
#define js_new_function(ARG_FUNCTION, ARG_COUNT) node_path_host->script->new_function(ARG_FUNCTION, ARG_COUNT)
#define js_throw_type_error_code(ARG_CODE, ARG_MESSAGE) node_path_throw_type_error(ARG_CODE, ARG_MESSAGE)

// Helper: get JS type name for error messages
static const char* js_type_name_for_error(Item value) {
    int kind = get_type_id(value);
    if (kind == LMD_TYPE_NULL) return "null";
    if (kind == LMD_TYPE_UNDEFINED) return "undefined";
    if (kind == LMD_TYPE_BOOL) return "boolean";
    if (kind == LMD_TYPE_INT) return "number";
    if (kind == LMD_TYPE_STRING) return "string";
    return "object";
}

// Helper: validate path argument is a string, throw ERR_INVALID_ARG_TYPE if not
// Returns true if valid (is a string), false if error was thrown
static bool validate_path_string(Item value, const char* arg_name) {
    if (get_type_id(value) == LMD_TYPE_STRING) return true;
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The \"%s\" argument must be of type string. Received type %s",
        arg_name, js_type_name_for_error(value));
    js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    return false;
}

// Helper: extract a null-terminated C string from an Item string
static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING || !node_path_host ||
            !node_path_host->value || !node_path_host->value->string_copy ||
            !buf || buf_size <= 0) return NULL;
    return node_path_host->value->string_copy(value, buf, (size_t)buf_size, NULL) ? buf : NULL;
}

// Helper: create a string Item from a C string
// =============================================================================
// Internal helper: normalize a path string in-place
// Resolves '.' and '..' segments, collapses duplicate separators
// =============================================================================
static int normalize_path_buf(const char* path, char* result, int result_size) {
    return path_str_normalize_lexical_posix(path, result, result_size, true);
}

// =============================================================================
// Path Operations
// =============================================================================

// path.basename(path[, ext])
// Returns the last portion of a path, optionally removing a suffix
extern "C" Item js_path_basename(Item path_item, Item ext_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    if (get_type_id(ext_item) != LMD_TYPE_UNDEFINED
        && !validate_path_string(ext_item, "ext")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");
    bool only_separators = true;
    for (int i = 0; path[i]; i++) {
        if (path[i] != '/') { only_separators = false; break; }
    }
    if (only_separators) return make_string_item("");

    int base_start = 0;
    int base_len = 0;
    path_str_posix_basename_span(path, &base_start, &base_len);
    int path_len = (int)strlen(path);
    const char* base = path + base_start;

    // if ext provided, strip it from the end
    if (get_type_id(ext_item) == LMD_TYPE_STRING) {
        char ext_buf[256];
        const char* ext = item_to_cstr(ext_item, ext_buf, sizeof(ext_buf));
        if (ext) {
            int ext_len = (int)strlen(ext);
            // Node.js: if entire path equals suffix, return ''
            // but if suffix matches full basename (with dir prefix), don't strip
            if (ext_len > 0 && ext_len == path_len &&
                memcmp(path, ext, ext_len) == 0) {
                return make_string_item("");
            }
            if (ext_len > 0 && ext_len < base_len &&
                memcmp(base + base_len - ext_len, ext, ext_len) == 0) {
                return make_string_item(base, base_len - ext_len);
            }
        }
    }

    return make_string_item(base, base_len);
}

// path.dirname(path)
// Returns the directory name of a path (POSIX compliant, handles // prefix)
extern "C" Item js_path_dirname(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item(".");

    int length = (int)strlen(path);
    bool all_separators = true;
    for (int i = 0; i < length; i++) {
        if (path[i] != '/') { all_separators = false; break; }
    }
    if (all_separators) return make_string_item("/");
    int end = length - 1;
    while (end > 0 && path[end] == '/') end--;
    int slash = end;
    while (slash >= 0 && path[slash] != '/') slash--;
    if (slash < 0) return make_string_item(".");
    int run_start = slash;
    while (run_start > 0 && path[run_start - 1] == '/') run_start--;
    if (run_start == 0 && slash > 0) return make_string_item(path, slash + 1);
    int run_length = slash - run_start + 1;
    return make_string_item(path, run_length > 1 ? run_start + 1 : slash ? slash : 1);
}

// path.extname(path)
// Returns the extension of the path
extern "C" Item js_path_extname(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");

    int ext_start = 0;
    int ext_len = 0;
    if (!path_str_posix_extname_span(path, &ext_start, &ext_len)) {
        return make_string_item("");
    }
    return make_string_item(path + ext_start, ext_len);
}

// path.isAbsolute(path)
// Returns true if path is absolute
extern "C" Item js_path_isAbsolute(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || path[0] == '\0') return (Item){.item = ITEM_FALSE};

    return (Item){.item = path_str_posix_is_absolute(path) ? ITEM_TRUE : ITEM_FALSE};
}

// path.win32.isAbsolute(path)
// Returns true if path is a Windows absolute path
static Item js_path_win32_isAbsolute(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    return (Item){.item = path_str_win32_is_absolute(path) ? ITEM_TRUE : ITEM_FALSE};
}

// path.join(...paths)
// Joins path segments together, normalizing separators
extern "C" Item js_path_join(Item args_item) {
    // args_item is an array of path segments
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) return make_string_item(".");

    int argc = (int)js_array_length(args_item);
    if (argc == 0) return make_string_item(".");

    // validate all arguments are strings
    for (int i = 0; i < argc; i++) {
        Item seg_item = js_array_get_int(args_item, i);
        if (!validate_path_string(seg_item, "path")) return ItemNull;
    }

    char result[4096] = {0};
    int result_len = 0;

    for (int i = 0; i < argc; i++) {
        Item seg_item = js_array_get_int(args_item, i);
        char seg_buf[1024];
        const char* seg = item_to_cstr(seg_item, seg_buf, sizeof(seg_buf));
        if (!seg || seg[0] == '\0') continue;

        if (result_len == 0) {
            result_len = path_str_copy(result, sizeof(result), seg);
        } else {
            char joined[4096];
            result_len = path_str_join_posix_into(joined, sizeof(joined), result, seg);
            path_str_copy(result, sizeof(result), joined);
        }
    }

    if (result_len == 0) return make_string_item(".");

    // check if result has trailing separator before normalizing
    bool had_trailing_sep = (result_len > 0 && result[result_len - 1] == '/');

    // normalize the result (resolve . and .. segments)
    char normalized[4096];
    int nlen = normalize_path_buf(result, normalized, sizeof(normalized));

    // Node.js: preserve trailing slash if original joined path had one
    if (had_trailing_sep && nlen > 0 && normalized[nlen - 1] != '/') {
        normalized[nlen] = '/';
        nlen++;
        normalized[nlen] = '\0';
    }
    return make_string_item(normalized, nlen);
}

// path.resolve(...paths)
// Resolves a sequence of paths to an absolute path
extern "C" Item js_path_resolve(Item args_item) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) {
        char* cwd = file_getcwd();
        Item result = make_string_item(cwd ? cwd : "/");
        if (cwd) mem_free(cwd);
        return result;
    }

    int argc = (int)js_array_length(args_item);

    // validate all arguments are strings
    for (int i = 0; i < argc; i++) {
        Item seg_item = js_array_get_int(args_item, i);
        if (!validate_path_string(seg_item, "path")) return ItemNull;
    }

    // start with cwd
    char resolved[4096] = {0};
    char* cwd = file_getcwd();
    if (cwd) {
        int clen = (int)strlen(cwd);
        if (clen >= (int)sizeof(resolved)) clen = (int)sizeof(resolved) - 1;
        memcpy(resolved, cwd, clen);
        resolved[clen] = '\0';
        mem_free(cwd);
    }

    for (int i = 0; i < argc; i++) {
        Item seg_item = js_array_get_int(args_item, i);
        char seg_buf[1024];
        const char* seg = item_to_cstr(seg_item, seg_buf, sizeof(seg_buf));
        if (!seg || seg[0] == '\0') continue;

        // if absolute, replace resolved
#ifdef _WIN32
        bool is_abs = path_str_win32_is_absolute(seg);
#else
        bool is_abs = path_str_posix_is_absolute(seg);
#endif
        if (is_abs) {
            path_str_copy(resolved, sizeof(resolved), seg);
        } else {
            char joined[4096];
            path_str_join_posix_into(joined, sizeof(joined), resolved, seg);
            path_str_copy(resolved, sizeof(resolved), joined);
        }
    }

    // normalize via realpath if the path exists, otherwise return as-is
    char* real = file_realpath(resolved);
    if (real) {
        Item result = make_string_item(real);
        mem_free(real);
        return result;
    }

    // path doesn't exist — normalize manually (remove trailing slashes, resolve . and ..)
    char normalized[4096];
    int nlen = normalize_path_buf(resolved, normalized, sizeof(normalized));
    return make_string_item(normalized, nlen);
}

// path.normalize(path)
// Normalizes a path, resolving '..' and '.' segments
extern "C" Item js_path_normalize(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return make_string_item(".");

    char result[4096];
    int rlen = normalize_path_buf(path, result, sizeof(result));
    int path_len = (int)strlen(path);
    if (path_len > 0 && path[path_len - 1] == '/' && rlen > 0 && result[rlen - 1] != '/' &&
            rlen < (int)sizeof(result) - 1) {
        // Node's lexical normalizer retains a trailing POSIX separator.
        result[rlen++] = '/';
        result[rlen] = '\0';
    }
    return make_string_item(result, rlen);
}

// path.relative(from, to)
// Returns the relative path from 'from' to 'to'
extern "C" Item js_path_relative(Item from_item, Item to_item) {
    if (!validate_path_string(from_item, "from")) return ItemNull;
    if (!validate_path_string(to_item, "to")) return ItemNull;
    char from_buf[2048], to_buf[2048];
    const char* from_path = item_to_cstr(from_item, from_buf, sizeof(from_buf));
    const char* to_path = item_to_cstr(to_item, to_buf, sizeof(to_buf));
    if (!from_path || !to_path) return make_string_item("");

    char* cwd = NULL;
    if (!from_path[0] || !to_path[0]) cwd = file_getcwd();
    if (!from_path[0] && cwd) from_path = cwd;
    if (!to_path[0] && cwd) to_path = cwd;

    // resolve both paths to absolute
    char* from_real = file_realpath(from_path);
    char* to_real = file_realpath(to_path);

    const char* from_abs = from_real ? from_real : from_path;
    const char* to_abs = to_real ? to_real : to_path;

    char result[4096];
    int result_len = path_str_relative_posix(from_abs, to_abs, result, sizeof(result));

    if (from_real) mem_free(from_real);
    if (to_real) mem_free(to_real);
    if (cwd) mem_free(cwd);

    return make_string_item(result, result_len);
}

// path.parse(path)
// Returns an object with root, dir, base, ext, name
extern "C" Item js_path_parse(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));

    Item obj = js_new_object();

    if (!path || path[0] == '\0') {
        js_property_set(obj, make_string_item("root"), make_string_item(""));
        js_property_set(obj, make_string_item("dir"), make_string_item(""));
        js_property_set(obj, make_string_item("base"), make_string_item(""));
        js_property_set(obj, make_string_item("ext"), make_string_item(""));
        js_property_set(obj, make_string_item("name"), make_string_item(""));
        return obj;
    }

    // Strip trailing slashes for parsing (Node.js behavior)
    // but preserve the original for dir computation on paths like /foo//bar.baz
    char stripped_buf[2048];
    strncpy(stripped_buf, path, sizeof(stripped_buf) - 1);
    stripped_buf[sizeof(stripped_buf) - 1] = '\0';
    int slen = (int)strlen(stripped_buf);
    // strip trailing slashes, but don't strip the root slash itself
    while (slen > 1 && (stripped_buf[slen - 1] == '/' || stripped_buf[slen - 1] == '\\')) {
        stripped_buf[--slen] = '\0';
    }
    const char* stripped = stripped_buf;

    const char* root = path_str_posix_is_absolute(stripped) ? "/" : "";

    int base_start = 0;
    int base_len = 0;
    bool has_base = path_str_posix_basename_span(stripped, &base_start, &base_len);
    if (has_base && base_len == 1 && stripped[base_start] == '/') {
        base_len = 0;
    }

    int root_len = root[0] ? 1 : 0;
    int dir_len = base_start > root_len ? base_start - 1 : root_len;
    bool has_dir = dir_len > 0;

    int ext_start = 0;
    int ext_len = 0;
    if (!path_str_posix_extname_span(stripped, &ext_start, &ext_len) ||
        ext_start < base_start ||
        ext_start > base_start + base_len) {
        ext_len = 0;
    }

    int name_len = base_len - ext_len;
    if (name_len < 0) name_len = 0;
    const char* base = stripped + base_start;

    js_property_set(obj, make_string_item("root"), make_string_item(root));
    js_property_set(obj, make_string_item("dir"), has_dir ? make_string_item(stripped, dir_len) : make_string_item(""));
    js_property_set(obj, make_string_item("base"), make_string_item(base, base_len));
    js_property_set(obj, make_string_item("ext"), ext_len > 0 ? make_string_item(stripped + ext_start, ext_len) : make_string_item(""));
    js_property_set(obj, make_string_item("name"), make_string_item(base, name_len));

    return obj;
}

// path.format(pathObject)
// Returns a path string from an object (opposite of path.parse)
extern "C" Item js_path_format(Item obj_item) {
    if (get_type_id(obj_item) != LMD_TYPE_MAP) {
        char received[192] = {};
        node_path_describe_invalid_object(obj_item, received, sizeof(received));
        char message[256];
        snprintf(message, sizeof(message),
            "The \"pathObject\" argument must be of type object. %s", received);
        js_throw_type_error_code("ERR_INVALID_ARG_TYPE", message);
        return ItemNull;
    }

    Item dir = js_property_get(obj_item, make_string_item("dir"));
    Item root = js_property_get(obj_item, make_string_item("root"));
    Item base = js_property_get(obj_item, make_string_item("base"));
    Item name = js_property_get(obj_item, make_string_item("name"));
    Item ext = js_property_get(obj_item, make_string_item("ext"));

    char result[4096];
    int pos = 0;

    char dir_buf[2048] = {};
    char root_buf[2048] = {};
    char base_buf[2048] = {};
    char name_buf[2048] = {};
    char ext_buf[2048] = {};
    const char* dir_text = item_to_cstr(dir, dir_buf, sizeof(dir_buf));
    const char* root_text = item_to_cstr(root, root_buf, sizeof(root_buf));
    const char* base_text = item_to_cstr(base, base_buf, sizeof(base_buf));
    const char* name_text = item_to_cstr(name, name_buf, sizeof(name_buf));
    const char* ext_text = item_to_cstr(ext, ext_buf, sizeof(ext_buf));

    // The value API copies string bytes so this module never borrows String
    // internals from the JS heap while it constructs the formatted result.
    if (dir_text && dir_text[0]) {
        int dlen = (int)strlen(dir_text);
        if (dlen >= (int)sizeof(result) - 2) dlen = (int)sizeof(result) - 2;
        memcpy(result, dir_text, dlen);
        pos = dlen;
        bool dir_is_root = root_text && root_text[0] && strcmp(dir_text, root_text) == 0;
        // path.format inserts its separator after a non-root dir even when
        // the dir already ends in one, preserving parse()/format() roundtrips.
        if (!dir_is_root && pos < (int)sizeof(result) - 1) {
            result[pos++] = '/';
        }
    } else if (root_text && root_text[0]) {
        int rlen = (int)strlen(root_text);
        if (rlen >= (int)sizeof(result) - 2) rlen = (int)sizeof(result) - 2;
        memcpy(result, root_text, rlen);
        pos = rlen;
    }

    // if base is present, use it; otherwise build from name + ext
    if (base_text && base_text[0]) {
        int blen = (int)strlen(base_text);
        if (pos + blen >= (int)sizeof(result)) blen = (int)sizeof(result) - 1 - pos;
        memcpy(result + pos, base_text, blen);
        pos += blen;
    } else {
        if (name_text && name_text[0]) {
            int nlen = (int)strlen(name_text);
            if (pos + nlen >= (int)sizeof(result)) nlen = (int)sizeof(result) - 1 - pos;
            memcpy(result + pos, name_text, nlen);
            pos += nlen;
        }
        if (ext_text && ext_text[0]) {
            if (ext_text[0] != '.' && pos < (int)sizeof(result) - 1) {
                // Node accepts either ".ext" or "ext" and supplies the dot.
                result[pos++] = '.';
            }
            int elen = (int)strlen(ext_text);
            if (pos + elen >= (int)sizeof(result)) elen = (int)sizeof(result) - 1 - pos;
            memcpy(result + pos, ext_text, elen);
            pos += elen;
        }
    }

    result[pos] = '\0';
    return make_string_item(result, pos);
}

// path.sep — the platform-specific path separator
static Item js_path_get_sep(void) {
    return make_string_item("/");
}

// path.delimiter — the platform-specific path delimiter
static Item js_path_get_delimiter(void) {
    return make_string_item(":");
}

// =============================================================================
// path.toNamespacedPath(path) — on POSIX, returns path unchanged
extern "C" Item js_path_toNamespacedPath(Item path_item) {
    return path_item;
}

// =============================================================================
// Win32 path helpers
// =============================================================================

// win32.normalize(path)
static Item js_path_win32_normalize(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || path[0] == '\0') return make_string_item(".");

    char result[4096];
    int result_len = path_str_normalize_lexical_win32(path, result, sizeof(result), true);
    int input_len = (int)strlen(path);
    if (input_len > 0 && path_str_is_win32_separator(path[input_len - 1]) &&
            result_len > 0 && !path_str_is_win32_separator(result[result_len - 1]) &&
            result_len < (int)sizeof(result) - 1) {
        // normalize preserves a trailing separator for non-empty Win32 paths.
        result[result_len++] = '\\';
        result[result_len] = '\0';
    }
    return make_string_item(result, result_len);
}

// win32.basename(path, ext)
static Item js_path_win32_basename(Item path_item, Item ext_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    if (get_type_id(ext_item) != LMD_TYPE_UNDEFINED
        && !validate_path_string(ext_item, "ext")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");

    int path_len = (int)strlen(path);
    int base_start = 0;
    int base_len = 0;
    if (!path_str_win32_basename_span(path, &base_start, &base_len)) {
        return make_string_item("");
    }
    const char* base = path + base_start;

    if (get_type_id(ext_item) == LMD_TYPE_STRING) {
        char ext_buf[256];
        const char* ext = item_to_cstr(ext_item, ext_buf, sizeof(ext_buf));
        if (ext) {
            int ext_len = (int)strlen(ext);
            // Node.js: if entire path equals suffix, return ''
            // but if suffix matches full basename (with dir prefix), don't strip
            if (ext_len > 0 && ext_len == path_len &&
                memcmp(path, ext, ext_len) == 0) {
                return make_string_item("");
            }
            if (ext_len > 0 && ext_len < base_len &&
                memcmp(base + base_len - ext_len, ext, ext_len) == 0) {
                return make_string_item(base, base_len - ext_len);
            }
        }
    }
    return make_string_item(base, base_len);
}

// win32.dirname(path)
static Item js_path_win32_dirname(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item(".");
    bool all_separators = true;
    for (int i = 0; path[i]; i++) {
        if (!path_str_is_win32_separator(path[i])) { all_separators = false; break; }
    }
    if (all_separators) return make_string_item(path, 1);

    // A UNC share is itself a root.  The generic separator-span helper only
    // knows about drive roots, so preserve the share boundary here rather
    // than accidentally returning the server name as its parent.
    if (path[0] == '\\' && path[1] == '\\') {
        int server_end = 2;
        while (path[server_end] && !path_str_is_win32_separator(path[server_end])) server_end++;
        int share_start = server_end;
        while (path_str_is_win32_separator(path[share_start])) share_start++;
        int share_end = share_start;
        while (path[share_end] && !path_str_is_win32_separator(path[share_end])) share_end++;
        if (share_start > server_end && share_end > share_start) {
            if (path[share_end] == '\0') return make_string_item(path);
            int tail = share_end;
            while (path[tail] && path_str_is_win32_separator(path[tail])) tail++;
            if (!path[tail]) return make_string_item(path, share_end + 1);
            int end = (int)strlen(path) - 1;
            while (end > share_end && path_str_is_win32_separator(path[end])) end--;
            int slash = end;
            while (slash > share_end && !path_str_is_win32_separator(path[slash])) slash--;
            if (slash <= share_end) return make_string_item(path, share_end + 1);
            while (slash > share_end && path_str_is_win32_separator(path[slash])) slash--;
            return make_string_item(path, slash + 1);
        }
    }

    int root_end = path_str_win32_root_end(path);
    int end = (int)strlen(path) - 1;
    while (end >= root_end && path_str_is_win32_separator(path[end])) end--;
    int slash = end;
    while (slash >= root_end && !path_str_is_win32_separator(path[slash])) slash--;
    if (slash >= root_end) {
        int run_start = slash;
        while (run_start > root_end && path_str_is_win32_separator(path[run_start - 1])) run_start--;
        int run_length = slash - run_start + 1;
        int dir_len = run_length > 1 ? run_start + 1 : slash;
        return make_string_item(path, dir_len);
    }

    int dir_len = 0;
    if (path_str_win32_dirname_span(path, &dir_len)) {
        return make_string_item(path, dir_len);
    }
    return make_string_item(".");
}

// win32.extname — same as POSIX but handles backslash separators
static Item js_path_win32_extname(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");

    int ext_start = 0;
    int ext_len = 0;
    if (!path_str_win32_extname_span(path, &ext_start, &ext_len)) {
        return make_string_item("");
    }
    return make_string_item(path + ext_start, ext_len);
}

// win32.resolve(...paths) — resolve to absolute path using backslashes
static Item js_path_win32_resolve(Item args_item) {
    // For cross-platform simulation, resolve as posix but with backslashes
    // Simple: delegate to posix resolve, then replace / with backslash
    Item posix_result = js_path_resolve(args_item);
    if (get_type_id(posix_result) != LMD_TYPE_STRING) return posix_result;

    char buf[4096];
    const char* s = item_to_cstr(posix_result, buf, sizeof(buf));
    if (!s) return posix_result;

    // Replace / with backslash
    for (char* p = buf; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    return make_string_item(buf);
}

// win32.join(...paths)
static Item js_path_win32_join(Item args_item) {
    Item posix_result = js_path_join(args_item);
    if (get_type_id(posix_result) != LMD_TYPE_STRING) return posix_result;

    char buf[4096];
    const char* s = item_to_cstr(posix_result, buf, sizeof(buf));
    if (!s) return posix_result;

    for (char* p = buf; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    return make_string_item(buf);
}

// win32.relative(from, to) — compute relative path with backslashes
static Item js_path_win32_relative(Item from_item, Item to_item) {
    Item posix_result = js_path_relative(from_item, to_item);
    if (get_type_id(posix_result) != LMD_TYPE_STRING) return posix_result;

    char buf[4096];
    const char* s = item_to_cstr(posix_result, buf, sizeof(buf));
    if (!s) return posix_result;

    for (char* p = buf; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    return make_string_item(buf);
}

// win32.parse(path)
static Item js_path_win32_parse(Item path_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    Item obj = js_new_object();
    if (!path || !path[0]) {
        js_property_set(obj, make_string_item("root"), make_string_item(""));
        js_property_set(obj, make_string_item("dir"), make_string_item(""));
        js_property_set(obj, make_string_item("base"), make_string_item(""));
        js_property_set(obj, make_string_item("ext"), make_string_item(""));
        js_property_set(obj, make_string_item("name"), make_string_item(""));
        return obj;
    }

    bool only_separators = true;
    for (int i = 0; path[i]; i++) {
        if (!path_str_is_win32_separator(path[i])) { only_separators = false; break; }
    }
    if (only_separators) {
        Item root = make_string_item(path, 1);
        js_property_set(obj, make_string_item("root"), root);
        js_property_set(obj, make_string_item("dir"), root);
        js_property_set(obj, make_string_item("base"), make_string_item(""));
        js_property_set(obj, make_string_item("ext"), make_string_item(""));
        js_property_set(obj, make_string_item("name"), make_string_item(""));
        return obj;
    }

    int path_len = (int)strlen(path);
    int root_len = path_str_win32_root_end(path);
    if (path[0] == '\\' && path[1] == '\\' && path[2] == '?' &&
            path_str_is_win32_separator(path[3]) &&
            strncmp(path + 4, "UNC\\", 4) == 0) {
        root_len = 8;
    } else if (path[0] == '\\' && path[1] == '\\') {
        int server_end = 2;
        while (path[server_end] && !path_str_is_win32_separator(path[server_end])) server_end++;
        int share_start = server_end;
        while (path_str_is_win32_separator(path[share_start])) share_start++;
        int share_end = share_start;
        while (path[share_end] && !path_str_is_win32_separator(path[share_end])) share_end++;
        if (share_end > share_start) root_len = path[share_end] ? share_end + 1 : share_end;
    }

    int end = path_len;
    while (end > root_len && path_str_is_win32_separator(path[end - 1])) end--;
    int base_start = end;
    while (base_start > root_len && !path_str_is_win32_separator(path[base_start - 1])) base_start--;
    int base_len = end - base_start;
    int dir_len = base_start > root_len ? base_start - 1 : base_start;
    if (base_start == root_len && root_len > 0) dir_len = root_len;

    int ext_at = -1;
    for (int i = end - 1; i > base_start; i--) {
        if (path[i] == '.') { ext_at = i; break; }
    }
    bool base_is_all_dots = base_len > 0;
    for (int i = 0; i < base_len; i++) {
        if (path[base_start + i] != '.') { base_is_all_dots = false; break; }
    }
    if (base_is_all_dots) ext_at = -1;
    int ext_len = ext_at >= 0 ? end - ext_at : 0;
    int name_len = base_len - ext_len;
    js_property_set(obj, make_string_item("root"), make_string_item(path, root_len));
    js_property_set(obj, make_string_item("dir"), dir_len ? make_string_item(path, dir_len) : make_string_item(""));
    js_property_set(obj, make_string_item("base"), make_string_item(path + base_start, base_len));
    js_property_set(obj, make_string_item("ext"), ext_len ? make_string_item(path + ext_at, ext_len) : make_string_item(""));
    js_property_set(obj, make_string_item("name"), make_string_item(path + base_start, name_len));
    return obj;
}

// win32.format(pathObject)
static Item js_path_win32_format(Item obj) {
    Item formatted = js_path_format(obj);
    if (get_type_id(formatted) != LMD_TYPE_STRING) return formatted;
    char result[4096];
    const char* text = item_to_cstr(formatted, result, sizeof(result));
    if (!text) return formatted;
    for (char* p = result; *p; p++) if (*p == '/') *p = '\\';
    return make_string_item(result);
}

// win32.toNamespacedPath(path) — on non-Windows, returns path unchanged
static Item js_path_win32_toNamespacedPath(Item path_item) {
    if (get_type_id(path_item) != LMD_TYPE_STRING) return path_item;
    char path_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) {
        return path_item;
    }
    if (path_str_is_win32_separator(path[0]) && path_str_is_win32_separator(path[1]) &&
            (path[2] == '?' || path[2] == '.')) {
        for (char* p = path_buf; *p; p++) if (*p == '/') *p = '\\';
        return make_string_item(path_buf);
    }
    char normalized[4096];
    path_str_normalize_lexical_win32(path, normalized, sizeof(normalized), true);
    if (path_str_is_win32_separator(normalized[0]) && path_str_is_win32_separator(normalized[1])) {
        char result[4096];
        snprintf(result, sizeof(result), "\\\\?\\UNC\\%s%s", normalized + 2,
            normalized[strlen(normalized) - 1] == '\\' ? "" : "\\");
        return make_string_item(result);
    }
    if (path_str_is_drive_letter(normalized[0]) && normalized[1] == ':' &&
            path_str_is_win32_separator(normalized[2])) {
        char result[4096];
        snprintf(result, sizeof(result), "\\\\?\\%s", normalized);
        return make_string_item(result);
    }
    return path_item;
}

static bool node_path_glob_class_matches(const char** pattern_ptr, char ch) {
    const char* pattern = *pattern_ptr;
    bool invert = *pattern == '!';
    if (invert) pattern++;
    bool matched = false;
    while (*pattern && *pattern != ']') {
        char start = *pattern++;
        if (pattern[0] == '-' && pattern[1] && pattern[1] != ']') {
            char end = pattern[1];
            pattern += 2;
            if (ch >= start && ch <= end) matched = true;
        } else if (ch == start) {
            matched = true;
        }
    }
    while (*pattern && *pattern != ']') pattern++;
    if (*pattern == ']') pattern++;
    *pattern_ptr = pattern;
    return invert ? !matched : matched;
}

static bool node_path_glob_matches(const char* text, const char* pattern, char separator) {
    while (*pattern) {
        if (*pattern == '*') {
            bool deep = pattern[1] == '*';
            while (*pattern == '*') pattern++;
            if (!*pattern) {
                if (deep) return true;
                return strchr(text, separator) == NULL;
            }
            for (const char* probe = text;; probe++) {
                if (node_path_glob_matches(probe, pattern, separator)) return true;
                if (!*probe || (!deep && *probe == separator)) break;
            }
            return false;
        }
        if (!*text) return false;
        if (*pattern == '?') {
            if (*text == separator) return false;
            pattern++;
            text++;
            continue;
        }
        if (*pattern == '[') {
            const char* after = pattern + 1;
            if (*text == separator || !node_path_glob_class_matches(&after, *text)) return false;
            pattern = after;
            text++;
            continue;
        }
        if (*pattern != *text) return false;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static Item js_path_matches_glob(Item path_item, Item pattern_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    if (!validate_path_string(pattern_item, "pattern")) return ItemNull;
    char path_buf[4096];
    char pattern_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    const char* pattern = item_to_cstr(pattern_item, pattern_buf, sizeof(pattern_buf));
    return (Item){.item = node_path_glob_matches(path, pattern, '/') ? ITEM_TRUE : ITEM_FALSE};
}

static Item js_path_win32_matches_glob(Item path_item, Item pattern_item) {
    if (!validate_path_string(path_item, "path")) return ItemNull;
    if (!validate_path_string(pattern_item, "pattern")) return ItemNull;
    char path_buf[4096];
    char pattern_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    const char* pattern = item_to_cstr(pattern_item, pattern_buf, sizeof(pattern_buf));
    for (char* p = path_buf; *p; p++) if (*p == '/') *p = '\\';
    for (char* p = pattern_buf; *p; p++) if (*p == '/') *p = '\\';
    return (Item){.item = node_path_glob_matches(path, pattern, '\\') ? ITEM_TRUE : ITEM_FALSE};
}

// =============================================================================
// path Module Namespace Object
// =============================================================================

static Item path_namespace = {0};

static void js_path_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

Item node_path_namespace(void) {
    if (path_namespace.item != 0) return path_namespace;
    if (!node_path_host || !node_path_session) return ItemNull;

    path_namespace = js_new_object();

    js_path_set_method(path_namespace, "basename",   (void*)js_path_basename, 2);
    js_path_set_method(path_namespace, "dirname",    (void*)js_path_dirname, 1);
    js_path_set_method(path_namespace, "extname",    (void*)js_path_extname, 1);
    js_path_set_method(path_namespace, "isAbsolute", (void*)js_path_isAbsolute, 1);
    js_path_set_method(path_namespace, "join",       (void*)js_path_join, -1);
    js_path_set_method(path_namespace, "resolve",    (void*)js_path_resolve, -1);
    js_path_set_method(path_namespace, "normalize",  (void*)js_path_normalize, 1);
    js_path_set_method(path_namespace, "relative",   (void*)js_path_relative, 2);
    js_path_set_method(path_namespace, "parse",      (void*)js_path_parse, 1);
    js_path_set_method(path_namespace, "format",     (void*)js_path_format, 1);
    js_path_set_method(path_namespace, "matchesGlob", (void*)js_path_matches_glob, 2);
    js_path_set_method(path_namespace, "toNamespacedPath", (void*)js_path_toNamespacedPath, 1);

    // properties
    js_property_set(path_namespace, make_string_item("sep"), js_path_get_sep());
    js_property_set(path_namespace, make_string_item("delimiter"), js_path_get_delimiter());

    // path.posix = path (on POSIX systems, posix is the same as the default)
    js_property_set(path_namespace, make_string_item("posix"), path_namespace);

    // path.win32 — win32-specific path implementations
    Item win32_ns = js_new_object();
    js_path_set_method(win32_ns, "basename",   (void*)js_path_win32_basename, 2);
    js_path_set_method(win32_ns, "dirname",    (void*)js_path_win32_dirname, 1);
    js_path_set_method(win32_ns, "extname",    (void*)js_path_win32_extname, 1);
    js_path_set_method(win32_ns, "isAbsolute", (void*)js_path_win32_isAbsolute, 1);
    js_path_set_method(win32_ns, "join",       (void*)js_path_win32_join, -1);
    js_path_set_method(win32_ns, "resolve",    (void*)js_path_win32_resolve, -1);
    js_path_set_method(win32_ns, "normalize",  (void*)js_path_win32_normalize, 1);
    js_path_set_method(win32_ns, "relative",   (void*)js_path_win32_relative, 2);
    js_path_set_method(win32_ns, "parse",      (void*)js_path_win32_parse, 1);
    js_path_set_method(win32_ns, "format",     (void*)js_path_win32_format, 1);
    js_path_set_method(win32_ns, "matchesGlob", (void*)js_path_win32_matches_glob, 2);
    js_path_set_method(win32_ns, "toNamespacedPath", (void*)js_path_win32_toNamespacedPath, 1);
    js_property_set(win32_ns, make_string_item("sep"), make_string_item("\\"));
    js_property_set(win32_ns, make_string_item("delimiter"), make_string_item(";"));
    js_property_set(path_namespace, make_string_item("win32"), win32_ns);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(path_namespace, default_key, path_namespace);

    return path_namespace;
}

static void node_path_cache_reset(void) {
    path_namespace = (Item){0};
}

Item node_path_win32_namespace(void) {
    // ensure path namespace is initialized
    Item ns = node_path_namespace();
    return js_property_get(ns, make_string_item("win32"));
}

int node_path_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->node->error || !host->value || !host->script ||
            !host->value->kind || !host->value->string_copy ||
            !host->value->string_from_utf8_n || !host->script->to_string) return -1;
    node_path_host = host;
    return 0;
}

void node_path_shutdown(void) {
    node_path_cache_reset();
    node_path_rooted = false;
    node_path_session = NULL;
    node_path_host = NULL;
}

void node_path_runtime_attach(void* session) {
    if (!node_path_host || !node_path_host->node || !node_path_host->node->runtime ||
            !node_path_host->node->runtime->session_is_live ||
            !node_path_host->node->runtime->session_is_live(session)) return;
    node_path_session = session;
    if (node_path_host->node->roots->persistent_root_register(session,
            &path_namespace.item) == 0) {
        node_path_rooted = true;
    }
}

void node_path_runtime_reset(void* session) {
    if (session != node_path_session) return;
    // All cached namespace Items belong to the current JS heap and must be
    // cleared before its global object is reset.
    node_path_cache_reset();
}

void node_path_runtime_detach(void* session) {
    if (session != node_path_session || !node_path_host) return;
    node_path_cache_reset();
    if (node_path_rooted) {
        node_path_host->node->roots->persistent_root_unregister(session, &path_namespace.item);
        node_path_rooted = false;
    }
    node_path_session = NULL;
}
