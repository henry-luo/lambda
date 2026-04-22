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
// URLSearchParams Implementation
// =============================================================================

// internal: parse query string "key=value&key2=value2" into entries array
static Item parse_query_entries(const char* qs, int qs_len) {
    Item entries = js_array_new(0);
    if (!qs || qs_len == 0) return entries;

    char buf[4096];
    if (qs_len >= (int)sizeof(buf)) qs_len = (int)sizeof(buf) - 1;
    memcpy(buf, qs, qs_len);
    buf[qs_len] = '\0';

    // URL-decode a string in-place
    auto url_decode = [](char* s) {
        char* out = s;
        for (char* p = s; *p; p++) {
            if (*p == '+') {
                *out++ = ' ';
            } else if (*p == '%' && p[1] && p[2]) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                    return 0;
                };
                *out++ = (char)((hex(p[1]) << 4) | hex(p[2]));
                p += 2;
            } else {
                *out++ = *p;
            }
        }
        *out = '\0';
    };

    char* saveptr = NULL;
    char* pair = strtok_r(buf, "&", &saveptr);
    while (pair) {
        char* eq = strchr(pair, '=');
        Item entry = js_array_new(0);
        if (eq) {
            *eq = '\0';
            url_decode(pair);
            url_decode(eq + 1);
            js_array_push(entry, make_string_item(pair));
            js_array_push(entry, make_string_item(eq + 1));
        } else {
            url_decode(pair);
            js_array_push(entry, make_string_item(pair));
            js_array_push(entry, make_string_item(""));
        }
        js_array_push(entries, entry);
        pair = strtok_r(NULL, "&", &saveptr);
    }
    return entries;
}

// URLSearchParams instance: stores entries as array of [key, value] pairs in __entries__

// URLSearchParams.prototype.append(name, value)
extern "C" Item js_usp_append(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item value_str = js_to_string(value_item);
    Item entry = js_array_new(0);
    js_array_push(entry, name_str);
    js_array_push(entry, value_str);
    js_array_push(entries, entry);
    return make_js_undefined();
}

// URLSearchParams.prototype.delete(name[, value])
extern "C" Item js_usp_delete(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    bool check_value = get_type_id(value_item) != LMD_TYPE_UNDEFINED;
    Item value_str = check_value ? js_to_string(value_item) : (Item){0};

    Item new_entries = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (check_value) {
                Item ev = js_array_get_int(entry, 1);
                Item vm = js_strict_equal(ev, value_str);
                if (js_is_truthy(vm)) continue; // delete matching
                js_array_push(new_entries, entry);
            }
            continue; // delete
        }
        js_array_push(new_entries, entry);
    }
    js_property_set(self, make_string_item("__entries__"), new_entries);
    return make_js_undefined();
}

// URLSearchParams.prototype.get(name)
extern "C" Item js_usp_get(Item name_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) return js_array_get_int(entry, 1);
    }
    return ItemNull;
}

// URLSearchParams.prototype.getAll(name)
extern "C" Item js_usp_getAll(Item name_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item result = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) js_array_push(result, js_array_get_int(entry, 1));
    }
    return result;
}

// URLSearchParams.prototype.has(name[, value])
extern "C" Item js_usp_has(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    bool check_value = get_type_id(value_item) != LMD_TYPE_UNDEFINED;
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (check_value) {
                Item ev = js_array_get_int(entry, 1);
                Item value_str = js_to_string(value_item);
                Item vm = js_strict_equal(ev, value_str);
                if (js_is_truthy(vm)) return (Item){.item = b2it(true)};
            } else {
                return (Item){.item = b2it(true)};
            }
        }
    }
    return (Item){.item = b2it(false)};
}

// URLSearchParams.prototype.set(name, value)
extern "C" Item js_usp_set(Item name_item, Item value_item) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    Item name_str = js_to_string(name_item);
    Item value_str = js_to_string(value_item);
    bool found = false;
    Item new_entries = js_array_new(0);
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item match = js_strict_equal(ek, name_str);
        if (js_is_truthy(match)) {
            if (!found) {
                Item new_entry = js_array_new(0);
                js_array_push(new_entry, name_str);
                js_array_push(new_entry, value_str);
                js_array_push(new_entries, new_entry);
                found = true;
            }
            // skip duplicate entries
        } else {
            js_array_push(new_entries, entry);
        }
    }
    if (!found) {
        Item new_entry = js_array_new(0);
        js_array_push(new_entry, name_str);
        js_array_push(new_entry, value_str);
        js_array_push(new_entries, new_entry);
    }
    js_property_set(self, make_string_item("__entries__"), new_entries);
    return make_js_undefined();
}

// URLSearchParams.prototype.sort()
extern "C" Item js_usp_sort(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    // simple bubble sort by key
    int64_t len = js_array_length(entries);
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - 1 - i; j++) {
            Item a = js_array_get_int(entries, j);
            Item b = js_array_get_int(entries, j + 1);
            Item ak = js_array_get_int(a, 0);
            Item bk = js_array_get_int(b, 0);
            String* sa = it2s(ak);
            String* sb = it2s(bk);
            if (sa && sb) {
                int cmp_len = sa->len < sb->len ? sa->len : sb->len;
                int cmp = memcmp(sa->chars, sb->chars, cmp_len);
                if (cmp > 0 || (cmp == 0 && sa->len > sb->len)) {
                    js_array_set_int(entries, j, b);
                    js_array_set_int(entries, j + 1, a);
                }
            }
        }
    }
    return make_js_undefined();
}

// URLSearchParams.prototype.toString()
extern "C" Item js_usp_toString(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    if (len == 0) return make_string_item("", 0);

    char buf[8192];
    int pos = 0;
    for (int64_t i = 0; i < len && pos < (int)sizeof(buf) - 100; i++) {
        if (i > 0) buf[pos++] = '&';
        Item entry = js_array_get_int(entries, i);
        Item ek = js_array_get_int(entry, 0);
        Item ev = js_array_get_int(entry, 1);
        // URL-encode key and value
        auto url_encode = [&](Item s) {
            if (get_type_id(s) != LMD_TYPE_STRING) return;
            String* str = it2s(s);
            if (!str) return;
            for (int j = 0; j < (int)str->len && pos < (int)sizeof(buf) - 4; j++) {
                char c = str->chars[j];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' ||
                    c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') {
                    buf[pos++] = c;
                } else if (c == ' ') {
                    buf[pos++] = '+';
                } else {
                    static const char hex[] = "0123456789ABCDEF";
                    buf[pos++] = '%';
                    buf[pos++] = hex[(unsigned char)c >> 4];
                    buf[pos++] = hex[(unsigned char)c & 0x0F];
                }
            }
        };
        url_encode(ek);
        buf[pos++] = '=';
        url_encode(ev);
    }
    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

// URLSearchParams.prototype.forEach(callback[, thisArg])
extern "C" Item js_usp_forEach(Item callback, Item this_arg) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item this_val = get_type_id(this_arg) != LMD_TYPE_UNDEFINED ? this_arg : self;
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        Item value = js_array_get_int(entry, 1);
        Item key = js_array_get_int(entry, 0);
        Item args[3] = {value, key, self};
        js_call_function(callback, this_val, args, 3);
    }
    return make_js_undefined();
}

// URLSearchParams.prototype.keys() — returns iterator
extern "C" Item js_usp_keys(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item result = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        js_array_push(result, js_array_get_int(entry, 0));
    }
    return result;
}

// URLSearchParams.prototype.values() — returns iterator
extern "C" Item js_usp_values(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    int64_t len = js_array_length(entries);
    Item result = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(entries, i);
        js_array_push(result, js_array_get_int(entry, 1));
    }
    return result;
}

// URLSearchParams.prototype.entries() — returns iterator
extern "C" Item js_usp_entries(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    return entries;
}

// URLSearchParams.prototype[Symbol.iterator]() — same as entries
// (handled by setting Symbol.iterator to entries)

// size getter
extern "C" Item js_usp_size(void) {
    Item self = js_get_this();
    Item entries = js_property_get(self, make_string_item("__entries__"));
    return (Item){.item = i2it((int)js_array_length(entries))};
}

// new URLSearchParams([init])
extern "C" Item js_url_search_params_new(Item init) {
    Item obj = js_new_object();
    js_property_set(obj, make_string_item("__class_name__"), make_string_item("URLSearchParams"));

    Item entries;
    TypeId init_type = get_type_id(init);

    if (init_type == LMD_TYPE_STRING) {
        String* s = it2s(init);
        const char* qs = s->chars;
        int qs_len = (int)s->len;
        if (qs_len > 0 && qs[0] == '?') { qs++; qs_len--; }
        entries = parse_query_entries(qs, qs_len);
    } else if (init_type == LMD_TYPE_MAP) {
        entries = js_array_new(0);
        Item keys = js_object_keys(init);
        int64_t klen = js_array_length(keys);
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_array_get_int(keys, i);
            Item val = js_property_get(init, key);
            Item entry = js_array_new(0);
            js_array_push(entry, js_to_string(key));
            js_array_push(entry, js_to_string(val));
            js_array_push(entries, entry);
        }
    } else if (init_type == LMD_TYPE_ARRAY) {
        entries = js_array_new(0);
        int64_t len = js_array_length(init);
        for (int64_t i = 0; i < len; i++) {
            Item pair = js_array_get_int(init, i);
            Item entry = js_array_new(0);
            js_array_push(entry, js_to_string(js_array_get_int(pair, 0)));
            js_array_push(entry, js_to_string(js_array_get_int(pair, 1)));
            js_array_push(entries, entry);
        }
    } else {
        entries = js_array_new(0);
    }

    js_property_set(obj, make_string_item("__entries__"), entries);

    // set methods
    auto usp_method = [&](const char* name, void* fn, int params) {
        js_property_set(obj, make_string_item(name), js_new_function(fn, params));
    };
    usp_method("append",  (void*)js_usp_append, 2);
    usp_method("delete",  (void*)js_usp_delete, 2);
    usp_method("get",     (void*)js_usp_get, 1);
    usp_method("getAll",  (void*)js_usp_getAll, 1);
    usp_method("has",     (void*)js_usp_has, 2);
    usp_method("set",     (void*)js_usp_set, 2);
    usp_method("sort",    (void*)js_usp_sort, 0);
    usp_method("toString",(void*)js_usp_toString, 0);
    usp_method("forEach", (void*)js_usp_forEach, 2);
    usp_method("keys",    (void*)js_usp_keys, 0);
    usp_method("values",  (void*)js_usp_values, 0);
    usp_method("entries",  (void*)js_usp_entries, 0);

    // size as getter
    int64_t sz = js_array_length(entries);
    js_property_set(obj, make_string_item("size"), (Item){.item = i2it((int)sz)});

    return obj;
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

    // URLSearchParams constructor
    js_url_set_method(url_module_namespace, "URLSearchParams", (void*)js_url_search_params_new, 1);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(url_module_namespace, default_key, url_module_namespace);

    return url_module_namespace;
}

extern "C" void js_url_module_reset(void) {
    url_module_namespace = (Item){0};
}
