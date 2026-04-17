/**
 * js_querystring.cpp — Node.js-style 'querystring' module for LambdaJS
 *
 * Provides parse, stringify, escape, unescape.
 * Registered as built-in module 'querystring' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include "../../lib/mem.h"

#include <cstring>

extern Input* js_input;

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// ─── querystring.escape(str) ─────────────────────────────────────────────────
// Percent-encodes a string (same as encodeURIComponent)
extern "C" Item js_qs_escape(Item str_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) return make_string_item("");
    String* s = it2s(str_item);
    char* encoded = url_encode_component(s->chars, s->len);
    if (!encoded) return make_string_item("");
    Item result = make_string_item(encoded, (int)strlen(encoded));
    mem_free(encoded);
    return result;
}

// ─── querystring.unescape(str) ───────────────────────────────────────────────
// Decodes a percent-encoded string (same as decodeURIComponent)
extern "C" Item js_qs_unescape(Item str_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) return make_string_item("");
    String* s = it2s(str_item);
    size_t decoded_len = 0;
    char* decoded = url_decode_component(s->chars, s->len, &decoded_len);
    if (!decoded) return make_string_item("");
    Item result = make_string_item(decoded, (int)decoded_len);
    mem_free(decoded);
    return result;
}

// ─── querystring.parse(str, sep, eq) ─────────────────────────────────────────
// Parses a query string into an object. Default sep='&', eq='='
// In query strings, '+' is decoded as space (before percent-decoding)
static void qs_plus_to_space(char* s) {
    for (; *s; s++) {
        if (*s == '+') *s = ' ';
    }
}

extern "C" Item js_qs_parse(Item str_item, Item sep_item, Item eq_item) {
    extern Item js_object_create(Item proto);
    Item obj = js_object_create(ItemNull);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return obj;

    String* s = it2s(str_item);
    if (s->len == 0) return obj;

    // determine separator and equals strings (support multi-char)
    const char* sep = "&";
    int sep_len = 1;
    const char* eq = "=";
    int eq_len = 1;
    if (get_type_id(sep_item) == LMD_TYPE_STRING) {
        String* sep_s = it2s(sep_item);
        if (sep_s->len > 0) { sep = sep_s->chars; sep_len = (int)sep_s->len; }
    }
    if (get_type_id(eq_item) == LMD_TYPE_STRING) {
        String* eq_s = it2s(eq_item);
        if (eq_s->len > 0) { eq = eq_s->chars; eq_len = (int)eq_s->len; }
    }

    // copy input to mutable buffer
    int len = (int)s->len;
    char* input = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    memcpy(input, s->chars, len);
    input[len] = '\0';

    // skip leading '?' if present
    char* p = input;
    if (*p == '?') p++;
    char* end = input + len;

    // helper lambda-like: process one key=value pair
    while (p < end) {
        // find next separator
        char* sep_pos = (sep_len == 1) ? strchr(p, sep[0]) : strstr(p, sep);
        int pair_len = sep_pos ? (int)(sep_pos - p) : (int)(end - p);

        // find equals within this pair
        char* pair_end = p + pair_len;
        char saved = *pair_end;
        *pair_end = '\0';
        char* eq_pos = (eq_len == 1) ? strchr(p, eq[0]) : strstr(p, eq);
        *pair_end = saved;

        if (eq_pos && eq_pos < pair_end) {
            // key = value
            int key_raw_len = (int)(eq_pos - p);
            int val_raw_len = (int)(pair_end - eq_pos - eq_len);
            char key_buf[4096], val_buf[4096];
            if (key_raw_len >= (int)sizeof(key_buf)) key_raw_len = (int)sizeof(key_buf) - 1;
            if (val_raw_len >= (int)sizeof(val_buf)) val_raw_len = (int)sizeof(val_buf) - 1;
            memcpy(key_buf, p, key_raw_len); key_buf[key_raw_len] = '\0';
            memcpy(val_buf, eq_pos + eq_len, val_raw_len); val_buf[val_raw_len] = '\0';

            qs_plus_to_space(key_buf);
            qs_plus_to_space(val_buf);

            size_t key_dec_len = 0, val_dec_len = 0;
            char* key_dec = url_decode_component(key_buf, strlen(key_buf), &key_dec_len);
            char* val_dec = url_decode_component(val_buf, strlen(val_buf), &val_dec_len);
            if (key_dec && val_dec) {
                Item key = make_string_item(key_dec, (int)key_dec_len);
                Item existing = js_property_get(obj, key);
                if (existing.item != 0 && get_type_id(existing) != LMD_TYPE_UNDEFINED) {
                    if (js_array_length(existing) >= 0 && get_type_id(existing) != LMD_TYPE_STRING) {
                        js_array_push(existing, make_string_item(val_dec, (int)val_dec_len));
                    } else {
                        Item arr = js_array_new(0);
                        js_array_push(arr, existing);
                        js_array_push(arr, make_string_item(val_dec, (int)val_dec_len));
                        js_property_set(obj, key, arr);
                    }
                } else {
                    js_property_set(obj, key, make_string_item(val_dec, (int)val_dec_len));
                }
            }
            if (key_dec) mem_free(key_dec);
            if (val_dec) mem_free(val_dec);
        } else if (pair_len > 0) {
            // key with no value
            char key_buf[4096];
            if (pair_len >= (int)sizeof(key_buf)) pair_len = (int)sizeof(key_buf) - 1;
            memcpy(key_buf, p, pair_len); key_buf[pair_len] = '\0';
            qs_plus_to_space(key_buf);
            size_t key_dec_len = 0;
            char* key_dec = url_decode_component(key_buf, strlen(key_buf), &key_dec_len);
            if (key_dec) {
                js_property_set(obj, make_string_item(key_dec, (int)key_dec_len), make_string_item(""));
                mem_free(key_dec);
            }
        }

        if (!sep_pos) break;
        p = sep_pos + sep_len;
    }

    mem_free(input);
    return obj;
}

// ─── querystring.stringify(obj, sep, eq) ─────────────────────────────────────
// Serializes an object into a query string. Default sep='&', eq='='
extern "C" Item js_qs_stringify(Item obj_item, Item sep_item, Item eq_item) {
    if (obj_item.item == 0 || get_type_id(obj_item) == LMD_TYPE_UNDEFINED)
        return make_string_item("");

    const char* sep = "&";
    int sep_len = 1;
    const char* eq = "=";
    int eq_len = 1;
    if (get_type_id(sep_item) == LMD_TYPE_STRING) {
        String* sep_s = it2s(sep_item);
        if (sep_s->len > 0) { sep = sep_s->chars; sep_len = (int)sep_s->len; }
    }
    if (get_type_id(eq_item) == LMD_TYPE_STRING) {
        String* eq_s = it2s(eq_item);
        if (eq_s->len > 0) { eq = eq_s->chars; eq_len = (int)eq_s->len; }
    }

    Item keys = js_object_keys(obj_item);
    int64_t key_count = js_array_length(keys);
    if (key_count <= 0) return make_string_item("");

    // build result string
    char result[8192];
    int pos = 0;

    for (int64_t i = 0; i < key_count && pos < 8000; i++) {
        Item key = js_array_get_int(keys, i);
        Item val = js_property_get(obj_item, key);

        if (i > 0 && pos + sep_len < 8000) {
            memcpy(result + pos, sep, sep_len); pos += sep_len;
        }

        // encode key
        Item key_str = js_to_string(key);
        String* ks = it2s(key_str);
        char* key_enc = url_encode_component(ks->chars, ks->len);

        if (js_array_length(val) >= 0 && get_type_id(val) != LMD_TYPE_STRING) {
            // value is an array — emit key=val for each element
            int64_t arr_len = js_array_length(val);
            for (int64_t j = 0; j < arr_len && pos < 8000; j++) {
                if (j > 0 && pos + sep_len < 8000) {
                    memcpy(result + pos, sep, sep_len); pos += sep_len;
                }
                int klen = (int)strlen(key_enc);
                if (pos + klen < 8000) { memcpy(result + pos, key_enc, klen); pos += klen; }
                if (pos + eq_len < 8000) { memcpy(result + pos, eq, eq_len); pos += eq_len; }
                Item elem = js_array_get_int(val, j);
                Item elem_str = js_to_string(elem);
                String* es = it2s(elem_str);
                char* val_enc = url_encode_component(es->chars, es->len);
                int vlen = (int)strlen(val_enc);
                if (pos + vlen < 8000) { memcpy(result + pos, val_enc, vlen); pos += vlen; }
                mem_free(val_enc);
            }
        } else {
            // single value
            int klen = (int)strlen(key_enc);
            if (pos + klen < 8000) { memcpy(result + pos, key_enc, klen); pos += klen; }
            if (pos + eq_len < 8000) { memcpy(result + pos, eq, eq_len); pos += eq_len; }
            Item val_str = js_to_string(val);
            String* vs = it2s(val_str);
            char* val_enc = url_encode_component(vs->chars, vs->len);
            int vlen = (int)strlen(val_enc);
            if (pos + vlen < 8000) { memcpy(result + pos, val_enc, vlen); pos += vlen; }
            mem_free(val_enc);
        }

        mem_free(key_enc);
    }

    result[pos] = '\0';
    return make_string_item(result, pos);
}

// ─── Namespace ───────────────────────────────────────────────────────────────

static Item qs_namespace = {0};

static void qs_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_querystring_namespace(void) {
    if (qs_namespace.item != 0) return qs_namespace;

    qs_namespace = js_new_object();

    qs_set_method(qs_namespace, "parse",     (void*)js_qs_parse, 3);
    qs_set_method(qs_namespace, "stringify",  (void*)js_qs_stringify, 3);
    qs_set_method(qs_namespace, "escape",    (void*)js_qs_escape, 1);
    qs_set_method(qs_namespace, "unescape",  (void*)js_qs_unescape, 1);
    qs_set_method(qs_namespace, "decode",    (void*)js_qs_parse, 3);     // alias
    qs_set_method(qs_namespace, "encode",    (void*)js_qs_stringify, 3); // alias

    // default export is the namespace itself
    js_property_set(qs_namespace, make_string_item("default"), qs_namespace);

    return qs_namespace;
}

extern "C" void js_reset_querystring_module(void) {
    qs_namespace = (Item){0};
}
