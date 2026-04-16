/**
 * js_url_module.cpp — Node.js-style 'url' module for LambdaJS
 *
 * Provides WHATWG URL API and legacy url.parse/url.format.
 * Registered as built-in module 'url' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include "../../lib/mem.h"

#include <cstring>
#include <cstdlib>

// Helper: make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Helper: extract C string from Item
static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

// Helper: create string Item
static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// Helper: convert Url* to JS object with URL properties
static Item url_to_js_object(Url* url) {
    if (!url) return ItemNull;

    Item obj = js_new_object();

    // set __class_name__ for instanceof checks
    js_property_set(obj, make_string_item("__class_name__"), make_string_item("URL"));

    const char* href = url_get_href(url);
    const char* origin_str = url_get_origin(url);
    const char* protocol = url_get_protocol(url);
    const char* username = url_get_username(url);
    const char* password = url_get_password(url);
    const char* host = url_get_host(url);
    const char* hostname = url_get_hostname(url);
    const char* port = url_get_port(url);
    const char* pathname = url_get_pathname(url);
    const char* search = url_get_search(url);
    const char* hash = url_get_hash(url);

    js_property_set(obj, make_string_item("href"), href ? make_string_item(href) : make_string_item(""));
    js_property_set(obj, make_string_item("origin"), origin_str ? make_string_item(origin_str) : make_string_item(""));
    js_property_set(obj, make_string_item("protocol"), protocol ? make_string_item(protocol) : make_string_item(""));
    js_property_set(obj, make_string_item("username"), username ? make_string_item(username) : make_string_item(""));
    js_property_set(obj, make_string_item("password"), password ? make_string_item(password) : make_string_item(""));
    js_property_set(obj, make_string_item("host"), host ? make_string_item(host) : make_string_item(""));
    js_property_set(obj, make_string_item("hostname"), hostname ? make_string_item(hostname) : make_string_item(""));
    js_property_set(obj, make_string_item("port"), port ? make_string_item(port) : make_string_item(""));
    js_property_set(obj, make_string_item("pathname"), pathname ? make_string_item(pathname) : make_string_item(""));
    js_property_set(obj, make_string_item("search"), search ? make_string_item(search) : make_string_item(""));
    js_property_set(obj, make_string_item("hash"), hash ? make_string_item(hash) : make_string_item(""));

    // searchParams — basic object (not full URLSearchParams)
    if (search && search[0] == '?' && search[1] != '\0') {
        Item params = js_new_object();
        // parse query string into key-value pairs
        char query_buf[4096];
        int qlen = (int)strlen(search + 1);
        if (qlen >= (int)sizeof(query_buf)) qlen = (int)sizeof(query_buf) - 1;
        memcpy(query_buf, search + 1, qlen);
        query_buf[qlen] = '\0';

        char* pair = strtok(query_buf, "&");
        while (pair) {
            char* eq = strchr(pair, '=');
            if (eq) {
                *eq = '\0';
                js_property_set(params, make_string_item(pair), make_string_item(eq + 1));
            } else {
                js_property_set(params, make_string_item(pair), make_string_item(""));
            }
            pair = strtok(NULL, "&");
        }
        js_property_set(obj, make_string_item("searchParams"), params);
    } else {
        js_property_set(obj, make_string_item("searchParams"), js_new_object());
    }

    return obj;
}

// =============================================================================
// URL Functions
// =============================================================================

// new URL(input[, base]) — construct URL object
extern "C" Item js_url_module_construct(Item input_item, Item base_item) {
    char input_buf[4096];
    const char* input = item_to_cstr(input_item, input_buf, sizeof(input_buf));
    if (!input) {
        log_error("url: URL constructor: invalid input");
        return ItemNull;
    }

    Url* url = NULL;

    if (get_type_id(base_item) == LMD_TYPE_STRING) {
        char base_buf[4096];
        const char* base = item_to_cstr(base_item, base_buf, sizeof(base_buf));
        if (base) {
            Url* base_url = url_parse(base);
            if (base_url) {
                url = url_parse_with_base(input, base_url);
                url_destroy(base_url);
            }
        }
    }

    if (!url) {
        url = url_parse(input);
    }

    if (!url || !url->is_valid) {
        if (url) url_destroy(url);
        log_error("url: URL constructor: invalid URL '%s'", input);
        return ItemNull;
    }

    Item result = url_to_js_object(url);
    url_destroy(url);
    return result;
}

// url.parse(urlString) — legacy Node.js url.parse
extern "C" Item js_url_parse_legacy(Item url_item) {
    char url_buf[4096];
    const char* url_str = item_to_cstr(url_item, url_buf, sizeof(url_buf));
    if (!url_str) return ItemNull;

    Url* url = url_parse(url_str);
    if (!url) return ItemNull;

    Item result = url_to_js_object(url);
    // legacy fields
    js_property_set(result, make_string_item("path"),
        js_property_get(result, make_string_item("pathname")));
    js_property_set(result, make_string_item("query"),
        js_property_get(result, make_string_item("search")));
    js_property_set(result, make_string_item("slashes"),
        (Item){.item = i2it(1)});

    url_destroy(url);
    return result;
}

// url.format(urlObject) — serialize URL object back to string
extern "C" Item js_url_format(Item obj_item) {
    if (get_type_id(obj_item) == LMD_TYPE_STRING) return obj_item;
    if (get_type_id(obj_item) != LMD_TYPE_MAP) return make_string_item("");

    // try to get href directly
    Item href = js_property_get(obj_item, make_string_item("href"));
    if (get_type_id(href) == LMD_TYPE_STRING && it2s(href)->len > 0) {
        return href;
    }

    // reconstruct from parts
    char buf[4096];
    int pos = 0;

    Item protocol = js_property_get(obj_item, make_string_item("protocol"));
    if (get_type_id(protocol) == LMD_TYPE_STRING) {
        String* p = it2s(protocol);
        memcpy(buf + pos, p->chars, p->len);
        pos += (int)p->len;
        if (p->len > 0 && p->chars[p->len - 1] != ':') buf[pos++] = ':';
        buf[pos++] = '/'; buf[pos++] = '/';
    }

    Item hostname = js_property_get(obj_item, make_string_item("hostname"));
    Item host = js_property_get(obj_item, make_string_item("host"));
    if (get_type_id(host) == LMD_TYPE_STRING && it2s(host)->len > 0) {
        String* h = it2s(host);
        memcpy(buf + pos, h->chars, h->len);
        pos += (int)h->len;
    } else if (get_type_id(hostname) == LMD_TYPE_STRING) {
        String* h = it2s(hostname);
        memcpy(buf + pos, h->chars, h->len);
        pos += (int)h->len;
    }

    Item pathname = js_property_get(obj_item, make_string_item("pathname"));
    if (get_type_id(pathname) == LMD_TYPE_STRING) {
        String* p = it2s(pathname);
        if (p->len > 0 && p->chars[0] != '/') buf[pos++] = '/';
        memcpy(buf + pos, p->chars, p->len);
        pos += (int)p->len;
    }

    Item search = js_property_get(obj_item, make_string_item("search"));
    if (get_type_id(search) == LMD_TYPE_STRING && it2s(search)->len > 0) {
        String* s = it2s(search);
        memcpy(buf + pos, s->chars, s->len);
        pos += (int)s->len;
    }

    Item hash = js_property_get(obj_item, make_string_item("hash"));
    if (get_type_id(hash) == LMD_TYPE_STRING && it2s(hash)->len > 0) {
        String* h = it2s(hash);
        memcpy(buf + pos, h->chars, h->len);
        pos += (int)h->len;
    }

    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

// url.resolve(from, to) — legacy url.resolve
extern "C" Item js_url_resolve(Item from_item, Item to_item) {
    char from_buf[4096], to_buf[4096];
    const char* from_str = item_to_cstr(from_item, from_buf, sizeof(from_buf));
    const char* to_str = item_to_cstr(to_item, to_buf, sizeof(to_buf));
    if (!from_str || !to_str) return ItemNull;

    Url* base = url_parse(from_str);
    if (!base) return make_string_item(to_str);

    Url* resolved = url_parse_with_base(to_str, base);
    if (!resolved) {
        url_destroy(base);
        return make_string_item(to_str);
    }

    const char* href = url_get_href(resolved);
    Item result = href ? make_string_item(href) : make_string_item(to_str);

    url_destroy(resolved);
    url_destroy(base);
    return result;
}

// ─── fileURLToPath(url) — convert file:// URL to local file path ────────────
extern "C" Item js_url_fileURLToPath(Item url_item) {
    if (get_type_id(url_item) != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(url_item);
    char buf[4096];
    int len = (int)s->len;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // strip "file://" prefix
    const char* path = buf;
    if (strncmp(path, "file://", 7) == 0) {
        path += 7;
        // handle "file:///path" -> "/path"
        // on macOS/Linux: file:///Users/foo -> /Users/foo
        // on Windows: file:///C:/foo -> C:/foo
#ifdef _WIN32
        if (path[0] == '/' && path[2] == ':') path++; // skip leading /
#endif
    }

    // URL-decode %XX sequences
    char decoded[4096];
    int di = 0;
    for (int i = 0; path[i] && di < (int)sizeof(decoded) - 1; i++) {
        if (path[i] == '%' && path[i+1] && path[i+2]) {
            auto hex_digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return 0;
            };
            decoded[di++] = (char)((hex_digit(path[i+1]) << 4) | hex_digit(path[i+2]));
            i += 2;
        } else {
            decoded[di++] = path[i];
        }
    }
    decoded[di] = '\0';

    return make_string_item(decoded, di);
}

// ─── pathToFileURL(path) — convert local path to file:// URL ────────────────
extern "C" Item js_url_pathToFileURL(Item path_item) {
    if (get_type_id(path_item) != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(path_item);
    char buf[4096];
    int len = (int)s->len;
    if (len >= (int)sizeof(buf) - 8) len = (int)sizeof(buf) - 9;

    // build file:// URL — simple path encoding
    int pos = 0;
    memcpy(buf, "file://", 7);
    pos = 7;

#ifdef _WIN32
    // windows: file:///C:/foo
    buf[pos++] = '/';
#endif

    // encode path: replace spaces with %20, leave / alone
    for (int i = 0; i < len && pos < (int)sizeof(buf) - 4; i++) {
        char c = s->chars[i];
        if (c == ' ') {
            buf[pos++] = '%'; buf[pos++] = '2'; buf[pos++] = '0';
        } else {
            buf[pos++] = c;
        }
    }
    buf[pos] = '\0';

    return make_string_item(buf, pos);
}

// =============================================================================
// url Module Namespace Object
// =============================================================================

static Item url_module_namespace = {0};

static void js_url_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_url_namespace(void) {
    if (url_module_namespace.item != 0) return url_module_namespace;

    url_module_namespace = js_new_object();

    // URL constructor (as a function, not class)
    js_url_set_method(url_module_namespace, "URL", (void*)js_url_module_construct, 2);

    // legacy methods
    js_url_set_method(url_module_namespace, "parse", (void*)js_url_parse_legacy, 1);
    js_url_set_method(url_module_namespace, "format", (void*)js_url_format, 1);
    js_url_set_method(url_module_namespace, "resolve", (void*)js_url_resolve, 2);

    // file URL conversion
    js_url_set_method(url_module_namespace, "fileURLToPath", (void*)js_url_fileURLToPath, 1);
    js_url_set_method(url_module_namespace, "pathToFileURL", (void*)js_url_pathToFileURL, 1);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(url_module_namespace, default_key, url_module_namespace);

    return url_module_namespace;
}

extern "C" void js_url_module_reset(void) {
    url_module_namespace = (Item){0};
}
