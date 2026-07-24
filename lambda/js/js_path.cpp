/**
 * js_path.cpp — Node.js-style 'path' module for LambdaJS
 *
 * Provides path manipulation utilities matching Node.js path API.
 * Registered as built-in module 'path' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include "../../lib/mem.h"
#include "../../lib/path_str.h"

#include <cstring>
#include <cstdio>

// Helper: get JS type name for error messages
static const char* js_type_name_for_error(Item value) {
    TypeId tid = get_type_id(value);
    switch (tid) {
        case LMD_TYPE_NULL:       return "null";
        case LMD_TYPE_UNDEFINED:  return "undefined";
        case LMD_TYPE_BOOL:       return "boolean";
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64:
        case LMD_TYPE_FLOAT:      return "number";
        case LMD_TYPE_STRING:     return "string";
        case LMD_TYPE_ARRAY:      return "object"; // arrays are objects in JS
        case LMD_TYPE_MAP:
        case LMD_TYPE_FUNC:       return "object";
        default:                  return "object";
    }
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
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
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

    int dir_len = 0;
    if (path_str_posix_dirname_span(path, &dir_len)) {
        return make_string_item(path, dir_len);
    }
    return make_string_item(".");
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

    // resolve both paths to absolute
    char* from_real = file_realpath(from_path);
    char* to_real = file_realpath(to_path);

    const char* from_abs = from_real ? from_real : from_path;
    const char* to_abs = to_real ? to_real : to_path;

    char result[4096];
    int result_len = path_str_relative_posix(from_abs, to_abs, result, sizeof(result));

    if (from_real) mem_free(from_real);
    if (to_real) mem_free(to_real);

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

    int dir_len = 0;
    bool has_dir = path_str_posix_dirname_span(stripped, &dir_len);

    int base_start = 0;
    int base_len = 0;
    bool has_base = path_str_posix_basename_span(stripped, &base_start, &base_len);
    if (has_base && base_len == 1 && stripped[base_start] == '/') {
        base_len = 0;
    }

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
    if (get_type_id(obj_item) != LMD_TYPE_MAP) return make_string_item("");

    Item dir = js_property_get(obj_item, make_string_item("dir"));
    Item root = js_property_get(obj_item, make_string_item("root"));
    Item base = js_property_get(obj_item, make_string_item("base"));
    Item name = js_property_get(obj_item, make_string_item("name"));
    Item ext = js_property_get(obj_item, make_string_item("ext"));

    char result[4096];
    int pos = 0;

    // if dir is present, use it; otherwise use root
    if (get_type_id(dir) == LMD_TYPE_STRING && it2s(dir)->len > 0) {
        String* d = it2s(dir);
        int dlen = (int)d->len;
        if (dlen >= (int)sizeof(result) - 2) dlen = (int)sizeof(result) - 2;
        memcpy(result, d->chars, dlen);
        pos = dlen;
        // ensure separator
        if (pos > 0 && result[pos - 1] != '/' && result[pos - 1] != '\\') {
            result[pos++] = '/';
        }
    } else if (get_type_id(root) == LMD_TYPE_STRING && it2s(root)->len > 0) {
        String* r = it2s(root);
        int rlen = (int)r->len;
        if (rlen >= (int)sizeof(result) - 2) rlen = (int)sizeof(result) - 2;
        memcpy(result, r->chars, rlen);
        pos = rlen;
    }

    // if base is present, use it; otherwise build from name + ext
    if (get_type_id(base) == LMD_TYPE_STRING && it2s(base)->len > 0) {
        String* b = it2s(base);
        int blen = (int)b->len;
        if (pos + blen >= (int)sizeof(result)) blen = (int)sizeof(result) - 1 - pos;
        memcpy(result + pos, b->chars, blen);
        pos += blen;
    } else {
        if (get_type_id(name) == LMD_TYPE_STRING && it2s(name)->len > 0) {
            String* n = it2s(name);
            int nlen = (int)n->len;
            if (pos + nlen >= (int)sizeof(result)) nlen = (int)sizeof(result) - 1 - pos;
            memcpy(result + pos, n->chars, nlen);
            pos += nlen;
        }
        if (get_type_id(ext) == LMD_TYPE_STRING && it2s(ext)->len > 0) {
            String* e = it2s(ext);
            int elen = (int)e->len;
            if (pos + elen >= (int)sizeof(result)) elen = (int)sizeof(result) - 1 - pos;
            memcpy(result + pos, e->chars, elen);
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
    // Delegate to posix parse for now — most fields are the same
    return js_path_parse(path_item);
}

// win32.format(pathObject)
static Item js_path_win32_format(Item obj) {
    return js_path_format(obj);
}

// win32.toNamespacedPath(path) — on non-Windows, returns path unchanged
static Item js_path_win32_toNamespacedPath(Item path_item) {
    return path_item;
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

extern "C" Item js_get_path_namespace(void) {
    if (path_namespace.item != 0) return path_namespace;

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
    js_path_set_method(win32_ns, "toNamespacedPath", (void*)js_path_win32_toNamespacedPath, 1);
    js_property_set(win32_ns, make_string_item("sep"), make_string_item("\\"));
    js_property_set(win32_ns, make_string_item("delimiter"), make_string_item(";"));
    js_property_set(path_namespace, make_string_item("win32"), win32_ns);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(path_namespace, default_key, path_namespace);

    return path_namespace;
}

extern "C" void js_path_reset(void) {
    path_namespace = (Item){0};
}

extern "C" Item js_get_path_win32_namespace(void) {
    // ensure path namespace is initialized
    Item ns = js_get_path_namespace();
    return js_property_get(ns, make_string_item("win32"));
}
