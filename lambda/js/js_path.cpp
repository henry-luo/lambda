/**
 * js_path.cpp — Node.js-style 'path' module for LambdaJS
 *
 * Provides path manipulation utilities matching Node.js path API.
 * Registered as built-in module 'path' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include "../../lib/mem.h"

#include <cstring>

// Helper: make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
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
static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// Internal helper: normalize a path string in-place
// Resolves '.' and '..' segments, collapses duplicate separators
// =============================================================================
static int normalize_path_buf(const char* path, char* result, int result_size) {
    const char* segments[256];
    int seg_count = 0;
    bool is_absolute = (path[0] == '/');
    bool had_trailing_sep = false;

    int plen = (int)strlen(path);
    if (plen > 0 && path[plen - 1] == '/') had_trailing_sep = true;

    // tokenize into a temp buffer (strtok modifies)
    char temp[4096];
    if (plen >= (int)sizeof(temp)) plen = (int)sizeof(temp) - 1;
    memcpy(temp, path, plen);
    temp[plen] = '\0';

    char* tok = strtok(temp, "/");
    while (tok && seg_count < 256) {
        if (strcmp(tok, ".") == 0) {
            // skip
        } else if (strcmp(tok, "..") == 0) {
            if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
                seg_count--;
            } else if (!is_absolute) {
                segments[seg_count++] = "..";
            }
        } else {
            segments[seg_count++] = tok;
        }
        tok = strtok(NULL, "/");
    }

    // rebuild
    int pos = 0;
    if (is_absolute) {
        result[pos++] = '/';
    }
    for (int i = 0; i < seg_count; i++) {
        if (i > 0) result[pos++] = '/';
        int slen = (int)strlen(segments[i]);
        if (pos + slen >= result_size - 1) break;
        memcpy(result + pos, segments[i], slen);
        pos += slen;
    }
    if (pos == 0) {
        result[0] = '.';
        pos = 1;
    }
    result[pos] = '\0';
    return pos;
}

// =============================================================================
// Path Operations
// =============================================================================

// path.basename(path[, ext])
// Returns the last portion of a path, optionally removing a suffix
extern "C" Item js_path_basename(Item path_item, Item ext_item) {
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");

    int path_len = (int)strlen(path);

    // strip trailing separators (POSIX: only '/')
    int end = path_len - 1;
#ifdef _WIN32
    while (end >= 0 && (path[end] == '/' || path[end] == '\\')) end--;
#else
    while (end >= 0 && path[end] == '/') end--;
#endif
    if (end < 0) return make_string_item("/");

    // find start of basename (after last separator)
    int start = end;
#ifdef _WIN32
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') start--;
#else
    while (start > 0 && path[start - 1] != '/') start--;
#endif

    int base_len = end - start + 1;
    const char* base = path + start;

    // if ext provided, strip it from the end
    if (get_type_id(ext_item) == LMD_TYPE_STRING) {
        char ext_buf[256];
        const char* ext = item_to_cstr(ext_item, ext_buf, sizeof(ext_buf));
        if (ext) {
            int ext_len = (int)strlen(ext);
            if (ext_len > 0 && ext_len <= base_len &&
                memcmp(base + base_len - ext_len, ext, ext_len) == 0) {
                return make_string_item(base, base_len - ext_len);
            }
        }
    }

    return make_string_item(base, base_len);
}

// path.dirname(path)
// Returns the directory name of a path
extern "C" Item js_path_dirname(Item path_item) {
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return make_string_item(".");

    char* dir = file_path_dirname(path);
    if (!dir) return make_string_item(".");

    Item result = make_string_item(dir);
    mem_free(dir);
    return result;
}

// path.extname(path)
// Returns the extension of the path
extern "C" Item js_path_extname(Item path_item) {
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || !*path) return make_string_item("");

    int path_len = (int)strlen(path);

    // strip trailing separators
    int end = path_len - 1;
    while (end >= 0 && path[end] == '/') end--;
    if (end < 0) return make_string_item("");

    // find start of basename (after last separator)
    int start = end;
    while (start > 0 && path[start - 1] != '/') start--;

    int base_len = end - start + 1;
    const char* base = path + start;

    // Node.js extname rules:
    // - Find the LAST dot in the basename
    // - If no dot, or dot is the first char with nothing after, return ""
    // - If basename is all dots (e.g. "..", "..."), return ""
    int last_dot = -1;
    for (int i = base_len - 1; i >= 0; i--) {
        if (base[i] == '.') { last_dot = i; break; }
    }
    if (last_dot <= 0) return make_string_item("");

    // Check that not all chars before last_dot are dots
    bool all_dots_before = true;
    for (int i = 0; i < last_dot; i++) {
        if (base[i] != '.') { all_dots_before = false; break; }
    }
    if (all_dots_before) return make_string_item("");

    return make_string_item(base + last_dot, base_len - last_dot);
}

// path.isAbsolute(path)
// Returns true if path is absolute
extern "C" Item js_path_isAbsolute(Item path_item) {
    char path_buf[2048];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path || path[0] == '\0') return js_to_boolean(ItemNull);

#ifdef _WIN32
    // Absolute if starts with drive letter + colon + separator, or UNC path
    if ((path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':') ||
        (path[0] >= 'a' && path[0] <= 'z' && path[1] == ':') ||
        (path[0] == '\\' && path[1] == '\\')) {
        return (Item){.item = i2it(1)};
    }
    return (Item){.item = i2it(0)};
#else
    return (Item){.item = i2it(path[0] == '/' ? 1 : 0)};
#endif
}

// path.join(...paths)
// Joins path segments together, normalizing separators
extern "C" Item js_path_join(Item args_item) {
    // args_item is an array of path segments
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) return make_string_item(".");

    int argc = (int)js_array_length(args_item);
    if (argc == 0) return make_string_item(".");

    char result[4096] = {0};
    int result_len = 0;

    for (int i = 0; i < argc; i++) {
        Item seg_item = js_array_get_int(args_item, i);
        char seg_buf[1024];
        const char* seg = item_to_cstr(seg_item, seg_buf, sizeof(seg_buf));
        if (!seg || seg[0] == '\0') continue;

        if (result_len == 0) {
            int seg_len = (int)strlen(seg);
            if (seg_len >= (int)sizeof(result)) seg_len = (int)sizeof(result) - 1;
            memcpy(result, seg, seg_len);
            result_len = seg_len;
        } else {
            char* joined = file_path_join(result, seg);
            if (joined) {
                int jlen = (int)strlen(joined);
                if (jlen >= (int)sizeof(result)) jlen = (int)sizeof(result) - 1;
                memcpy(result, joined, jlen);
                result[jlen] = '\0';
                result_len = jlen;
                mem_free(joined);
            }
        }
    }

    if (result_len == 0) return make_string_item(".");

    // normalize the result (resolve . and .. segments)
    char normalized[4096];
    int nlen = normalize_path_buf(result, normalized, sizeof(normalized));
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
        bool is_abs = (seg[0] == '/' || seg[0] == '\\' ||
                       (seg[0] >= 'A' && seg[0] <= 'Z' && seg[1] == ':') ||
                       (seg[0] >= 'a' && seg[0] <= 'z' && seg[1] == ':'));
#else
        bool is_abs = (seg[0] == '/');
#endif
        if (is_abs) {
            int slen = (int)strlen(seg);
            if (slen >= (int)sizeof(resolved)) slen = (int)sizeof(resolved) - 1;
            memcpy(resolved, seg, slen);
            resolved[slen] = '\0';
        } else {
            char* joined = file_path_join(resolved, seg);
            if (joined) {
                int jlen = (int)strlen(joined);
                if (jlen >= (int)sizeof(resolved)) jlen = (int)sizeof(resolved) - 1;
                memcpy(resolved, joined, jlen);
                resolved[jlen] = '\0';
                mem_free(joined);
            }
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
    char path_buf[4096];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return make_string_item(".");

    // use realpath to normalize if the path exists
    char* real = file_realpath(path);
    if (real) {
        Item result = make_string_item(real);
        mem_free(real);
        return result;
    }

    // fallback: basic normalization for non-existent paths
    char result[4096];
    int rlen = normalize_path_buf(path, result, sizeof(result));
    return make_string_item(result, rlen);
}

// path.relative(from, to)
// Returns the relative path from 'from' to 'to'
extern "C" Item js_path_relative(Item from_item, Item to_item) {
    char from_buf[2048], to_buf[2048];
    const char* from_path = item_to_cstr(from_item, from_buf, sizeof(from_buf));
    const char* to_path = item_to_cstr(to_item, to_buf, sizeof(to_buf));
    if (!from_path || !to_path) return make_string_item("");

    // resolve both paths to absolute
    char* from_real = file_realpath(from_path);
    char* to_real = file_realpath(to_path);

    const char* from_abs = from_real ? from_real : from_path;
    const char* to_abs = to_real ? to_real : to_path;

    // find common prefix
    int common = 0;
    int last_sep = 0;
    int fl = (int)strlen(from_abs);
    int tl = (int)strlen(to_abs);
    int minl = fl < tl ? fl : tl;

    for (int i = 0; i < minl; i++) {
        if (from_abs[i] != to_abs[i]) break;
        if (from_abs[i] == '/') last_sep = i;
        common = i + 1;
    }
    // if one fully contains the other and next char is / or end
    if (common == minl) {
        if (fl == tl) {
            // same path
            if (from_real) mem_free(from_real);
            if (to_real) mem_free(to_real);
            return make_string_item("");
        }
        if (common < fl && from_abs[common] == '/') last_sep = common;
        else if (common < tl && to_abs[common] == '/') last_sep = common;
        else last_sep = common;
    }

    // count remaining segments in 'from' after common prefix
    int up_count = 0;
    for (int i = last_sep + 1; i < fl; i++) {
        if (from_abs[i] == '/') up_count++;
    }
    if (last_sep + 1 < fl) up_count++; // trailing segment

    // build result
    char result[4096];
    int pos = 0;
    for (int i = 0; i < up_count; i++) {
        if (i > 0) result[pos++] = '/';
        result[pos++] = '.';
        result[pos++] = '.';
    }

    // append remaining portion of 'to' after common prefix
    const char* to_rest = to_abs + last_sep;
    if (to_rest[0] == '/') to_rest++;
    int rest_len = (int)strlen(to_rest);
    if (rest_len > 0) {
        if (pos > 0) result[pos++] = '/';
        if (pos + rest_len >= (int)sizeof(result) - 1) rest_len = (int)sizeof(result) - 1 - pos;
        memcpy(result + pos, to_rest, rest_len);
        pos += rest_len;
    }
    result[pos] = '\0';

    if (from_real) mem_free(from_real);
    if (to_real) mem_free(to_real);

    return make_string_item(result, pos);
}

// path.parse(path)
// Returns an object with root, dir, base, ext, name
extern "C" Item js_path_parse(Item path_item) {
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

    // root
    const char* root = "";
#ifdef _WIN32
    char root_buf[4] = {0};
    if (stripped[0] >= 'A' && stripped[0] <= 'z' && stripped[1] == ':') {
        root_buf[0] = stripped[0]; root_buf[1] = ':'; root_buf[2] = '\\';
        root = root_buf;
    } else if (stripped[0] == '\\' || stripped[0] == '/') {
        root = "/";
    }
#else
    if (stripped[0] == '/') root = "/";
#endif

    // dir — use original path for dirname (preserves internal slashes)
    char* dir = file_path_dirname(stripped);
    // Node.js: when dirname returns "." it means no directory component
    // Only keep "." if the original path had a "/" (explicit dir reference)
    if (dir && strcmp(dir, ".") == 0 && !strchr(stripped, '/')) {
        mem_free(dir);
        dir = NULL;
    }

    // base (basename) — use stripped path
    const char* base = file_path_basename(stripped);
    // Node.js: basename of root-only paths is ""
    if (base && strcmp(base, "/") == 0) base = "";

    // ext
    const char* ext = file_path_ext(stripped);
    // Node.js extension rules: ext is the portion from the LAST dot in basename,
    // but only if there's a non-dot character before that last dot.
    // So '.', '..', '.bashrc' all have ext="" while 'file.txt' has ext=".txt"
    if (ext && base) {
        int blen = (int)strlen(base);
        // Find the last dot in base
        int last_dot = -1;
        for (int i = blen - 1; i >= 0; i--) {
            if (base[i] == '.') { last_dot = i; break; }
        }
        // ext is "" if: no dot, dot at position 0, or all chars before last_dot are dots
        bool has_non_dot_before = false;
        for (int i = 0; i < last_dot; i++) {
            if (base[i] != '.') { has_non_dot_before = true; break; }
        }
        if (last_dot <= 0 || !has_non_dot_before) {
            ext = "";
        }
    }

    // name (base without ext)
    int base_len = base ? (int)strlen(base) : 0;
    int ext_len = ext ? (int)strlen(ext) : 0;
    int name_len = base_len - ext_len;
    if (name_len < 0) name_len = 0;

    js_property_set(obj, make_string_item("root"), make_string_item(root));
    js_property_set(obj, make_string_item("dir"), dir ? make_string_item(dir) : make_string_item(""));
    js_property_set(obj, make_string_item("base"), base ? make_string_item(base) : make_string_item(""));
    js_property_set(obj, make_string_item("ext"), ext ? make_string_item(ext) : make_string_item(""));
    js_property_set(obj, make_string_item("name"), base ? make_string_item(base, name_len) : make_string_item(""));

    if (dir) mem_free(dir);

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
#ifdef _WIN32
    return make_string_item("\\");
#else
    return make_string_item("/");
#endif
}

// path.delimiter — the platform-specific path delimiter
static Item js_path_get_delimiter(void) {
#ifdef _WIN32
    return make_string_item(";");
#else
    return make_string_item(":");
#endif
}

// =============================================================================
// path.toNamespacedPath(path) — on POSIX, returns path unchanged
extern "C" Item js_path_toNamespacedPath(Item path_item) {
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

    // path.win32 — stub namespace with same methods but win32 separator behavior
    // (needed for Node.js compat; full win32 path semantics deferred)
    Item win32_ns = js_new_object();
    js_path_set_method(win32_ns, "basename",   (void*)js_path_basename, 2);
    js_path_set_method(win32_ns, "dirname",    (void*)js_path_dirname, 1);
    js_path_set_method(win32_ns, "extname",    (void*)js_path_extname, 1);
    js_path_set_method(win32_ns, "isAbsolute", (void*)js_path_isAbsolute, 1);
    js_path_set_method(win32_ns, "join",       (void*)js_path_join, -1);
    js_path_set_method(win32_ns, "resolve",    (void*)js_path_resolve, -1);
    js_path_set_method(win32_ns, "normalize",  (void*)js_path_normalize, 1);
    js_path_set_method(win32_ns, "relative",   (void*)js_path_relative, 2);
    js_path_set_method(win32_ns, "parse",      (void*)js_path_parse, 1);
    js_path_set_method(win32_ns, "format",     (void*)js_path_format, 1);
    js_path_set_method(win32_ns, "toNamespacedPath", (void*)js_path_toNamespacedPath, 1);
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
