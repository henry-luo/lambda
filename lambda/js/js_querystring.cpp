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
extern "C" Item js_qs_parse(Item str_item, Item sep_item, Item eq_item) {
    Item obj = js_new_object();
    if (get_type_id(str_item) != LMD_TYPE_STRING) return obj;

    String* s = it2s(str_item);
    if (s->len == 0) return obj;

    // determine separator and equals characters
    char sep = '&';
    char eq = '=';
    char buf[8];
    if (get_type_id(sep_item) == LMD_TYPE_STRING) {
        String* sep_s = it2s(sep_item);
        if (sep_s->len > 0) sep = sep_s->chars[0];
    }
    if (get_type_id(eq_item) == LMD_TYPE_STRING) {
        String* eq_s = it2s(eq_item);
        if (eq_s->len > 0) eq = eq_s->chars[0];
    }

    // copy input to mutable buffer
    int len = (int)s->len;
    char* input = (char*)mem_alloc(len + 1, MEM_CAT_JS_RUNTIME);
    memcpy(input, s->chars, len);
    input[len] = '\0';

    // skip leading '?' if present
    char* p = input;
    if (*p == '?') p++;

    // split by separator
    char sep_str[2] = {sep, '\0'};
    char* saveptr = NULL;
    char* pair = strtok_r(p, sep_str, &saveptr);
    while (pair) {
        char* eq_pos = strchr(pair, eq);
        if (eq_pos) {
            *eq_pos = '\0';
            // decode key and value
            size_t key_dec_len = 0, val_dec_len = 0;
            char* key_dec = url_decode_component(pair, strlen(pair), &key_dec_len);
            char* val_dec = url_decode_component(eq_pos + 1, strlen(eq_pos + 1), &val_dec_len);
            if (key_dec && val_dec) {
                Item key = make_string_item(key_dec, (int)key_dec_len);
                // check if key already exists — if so, convert to array
                Item existing = js_property_get(obj, key);
                if (existing.item != 0 && get_type_id(existing) != LMD_TYPE_UNDEFINED) {
                    // already has value — make array or push to existing array
                    if (js_array_length(existing) >= 0 && get_type_id(existing) != LMD_TYPE_STRING) {
                        // already an array, push
                        js_array_push(existing, make_string_item(val_dec, (int)val_dec_len));
                    } else {
                        // convert single value to array
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
        } else {
            // key with no value
            size_t key_dec_len = 0;
            char* key_dec = url_decode_component(pair, strlen(pair), &key_dec_len);
            if (key_dec) {
                js_property_set(obj, make_string_item(key_dec, (int)key_dec_len), make_string_item(""));
                mem_free(key_dec);
            }
        }
        pair = strtok_r(NULL, sep_str, &saveptr);
    }

    mem_free(input);
    return obj;
}

// ─── querystring.stringify(obj, sep, eq) ─────────────────────────────────────
// Serializes an object into a query string. Default sep='&', eq='='
extern "C" Item js_qs_stringify(Item obj_item, Item sep_item, Item eq_item) {
    if (obj_item.item == 0 || get_type_id(obj_item) == LMD_TYPE_UNDEFINED)
        return make_string_item("");

    char sep = '&';
    char eq = '=';
    if (get_type_id(sep_item) == LMD_TYPE_STRING) {
        String* sep_s = it2s(sep_item);
        if (sep_s->len > 0) sep = sep_s->chars[0];
    }
    if (get_type_id(eq_item) == LMD_TYPE_STRING) {
        String* eq_s = it2s(eq_item);
        if (eq_s->len > 0) eq = eq_s->chars[0];
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

        if (i > 0 && pos < 8000) result[pos++] = sep;

        // encode key
        Item key_str = js_to_string(key);
        String* ks = it2s(key_str);
        char* key_enc = url_encode_component(ks->chars, ks->len);

        if (js_array_length(val) >= 0 && get_type_id(val) != LMD_TYPE_STRING) {
            // value is an array — emit key=val for each element
            int64_t arr_len = js_array_length(val);
            for (int64_t j = 0; j < arr_len && pos < 8000; j++) {
                if (j > 0) result[pos++] = sep;
                int klen = (int)strlen(key_enc);
                if (pos + klen < 8000) { memcpy(result + pos, key_enc, klen); pos += klen; }
                result[pos++] = eq;
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
            result[pos++] = eq;
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
