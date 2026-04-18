/**
 * js_buffer.cpp — Node.js-style 'buffer' module for LambdaJS
 *
 * Provides Buffer class backed by Uint8Array (TypedArray infrastructure).
 * Buffer.alloc, Buffer.from, Buffer.concat, buf.toString, buf.write,
 * buf.copy, buf.slice, buf.fill, buf.compare, buf.equals, buf.indexOf,
 * buf.byteLength.
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>
#include <cstdlib>

extern Input* js_input;

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
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

// Helper: get raw data pointer and length from a typed array Item
static uint8_t* buffer_data(Item buf, int* out_len) {
    if (!js_is_typed_array(buf)) { *out_len = 0; return NULL; }
    // typed array keeps JsTypedArray* in map->data
    Map* m = buf.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (!ta || !ta->data) { *out_len = 0; return NULL; }
    *out_len = ta->byte_length;
    return (uint8_t*)ta->data;
}

// Helper: create a Buffer (Uint8Array)
// We cannot set string properties on typed arrays (MAP_KIND_TYPED_ARRAY routes
// property_set to typed_array_set which only handles numeric indices).
// Instead, Buffer identity is checked via js_is_typed_array().
static Item create_buffer(int size) {
    return js_typed_array_new(JS_TYPED_UINT8, size);
}

// Helper: format "Received type <type> (<value>)" suffix for ERR_INVALID_ARG_TYPE errors
static int format_received_suffix(char* buf, int buf_size, Item value) {
    TypeId tid = get_type_id(value);
    switch (tid) {
        case LMD_TYPE_INT:
            return snprintf(buf, buf_size, " Received type number (%lld)", (long long)it2i(value));
        case LMD_TYPE_FLOAT: {
            double d = it2d(value);
            if (d != d) return snprintf(buf, buf_size, " Received type number (NaN)");
            if (d == 1.0/0.0) return snprintf(buf, buf_size, " Received type number (Infinity)");
            if (d == -1.0/0.0) return snprintf(buf, buf_size, " Received type number (-Infinity)");
            return snprintf(buf, buf_size, " Received type number (%g)", d);
        }
        case LMD_TYPE_BOOL:
            return snprintf(buf, buf_size, " Received type boolean (%s)", it2b(value) ? "true" : "false");
        case LMD_TYPE_STRING:
            return snprintf(buf, buf_size, " Received type string ('%.*s')",
                (int)(it2s(value)->len > 25 ? 25 : it2s(value)->len), it2s(value)->chars);
        case LMD_TYPE_NULL:
            return snprintf(buf, buf_size, " Received null");
        case LMD_TYPE_UNDEFINED:
            return snprintf(buf, buf_size, " Received undefined");
        case LMD_TYPE_MAP:
            return snprintf(buf, buf_size, " Received an instance of Object");
        default:
            return snprintf(buf, buf_size, " Received type object");
    }
}

// ─── Buffer.alloc(size, fill?, encoding?) ───────────────────────────────────
extern "C" Item js_buffer_alloc(Item size_item, Item fill_item) {
    int64_t size = 0;
    TypeId tid = get_type_id(size_item);
    if (tid == LMD_TYPE_INT) size = it2i(size_item);
    else if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(size_item);
        if (d != d || d < 0 || d > 2147483647) {
            return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                "The value of \"size\" is out of range.");
        }
        size = (int64_t)d;
    }
    else {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"size\" argument must be of type number.");
    }
    if (size < 0 || size > 2147483647) {
        return js_throw_range_error_code("ERR_OUT_OF_RANGE",
            "The value of \"size\" is out of range.");
    }
    if (size > 1048576) size = 1048576; // practical limit

    Item buf = create_buffer((int)size);
    // alloc zero-fills by default via typed_array_new

    // fill support
    TypeId fill_type = get_type_id(fill_item);
    if (fill_type == LMD_TYPE_INT) {
        int fill_val = (int)(it2i(fill_item) & 0xFF);
        if (fill_val != 0) {
            int blen = 0;
            uint8_t* data = buffer_data(buf, &blen);
            if (data) memset(data, fill_val, blen);
        }
    } else if (fill_type == LMD_TYPE_STRING) {
        String* s = it2s(fill_item);
        if (s && s->len > 0) {
            int blen = 0;
            uint8_t* data = buffer_data(buf, &blen);
            if (data) {
                for (int i = 0; i < blen; i++)
                    data[i] = (uint8_t)s->chars[i % s->len];
            }
        }
    }

    return buf;
}

// ─── new Buffer(arg, encoding?) — deprecated constructor ────────────────────
// forward declaration
extern "C" Item js_buffer_from(Item data, Item encoding);
// new Buffer(size) → Buffer.alloc(size), new Buffer(string, enc) → Buffer.from(string, enc)
// new Buffer(array) → Buffer.from(array), new Buffer(buffer) → Buffer.from(buffer)
extern "C" Item js_buffer_construct(Item arg, Item encoding) {
    TypeId tid = get_type_id(arg);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT) {
        // new Buffer(size) — allocate zero-filled
        Item fill = make_js_undefined();
        return js_buffer_alloc(arg, fill);
    }
    // everything else delegates to Buffer.from
    return js_buffer_from(arg, encoding);
}

// ─── Buffer.from(data, encoding?) ───────────────────────────────────────────
// data can be: string (utf-8), array of bytes, another Buffer/TypedArray
extern "C" Item js_buffer_from(Item data, Item encoding) {
    TypeId tid = get_type_id(data);

    if (tid == LMD_TYPE_STRING) {
        // string → utf-8 bytes
        String* s = it2s(data);

        // check encoding
        char enc_buf[32] = "utf8";
        if (get_type_id(encoding) == LMD_TYPE_STRING) {
            String* enc = it2s(encoding);
            int elen = (int)enc->len;
            if (elen >= (int)sizeof(enc_buf)) elen = (int)sizeof(enc_buf) - 1;
            for (int i = 0; i < elen; i++)
                enc_buf[i] = (enc->chars[i] >= 'A' && enc->chars[i] <= 'Z') ? enc->chars[i] + 32 : enc->chars[i];
            enc_buf[elen] = '\0';
        }

        if (strcmp(enc_buf, "hex") == 0) {
            // hex decode
            int hex_len = (int)s->len;
            int byte_len = hex_len / 2;
            Item buf = create_buffer(byte_len);
            int buf_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_len);
            if (bdata) {
                for (int i = 0; i < byte_len && i * 2 + 1 < hex_len; i++) {
                    char hi = s->chars[i * 2];
                    char lo = s->chars[i * 2 + 1];
                    auto hex_digit = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                        return 0;
                    };
                    bdata[i] = (uint8_t)((hex_digit(hi) << 4) | hex_digit(lo));
                }
            }
            return buf;
        }

        if (strcmp(enc_buf, "base64") == 0) {
            // base64 decode — use atob-style decode
            // simple base64 decode
            static const int b64_table[256] = {
                ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
                ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
                ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
                ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
                ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
                ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
                ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
                ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
            };
            int in_len = (int)s->len;
            int out_len = in_len * 3 / 4;
            if (in_len > 0 && s->chars[in_len - 1] == '=') out_len--;
            if (in_len > 1 && s->chars[in_len - 2] == '=') out_len--;

            Item buf = create_buffer(out_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                int j = 0;
                for (int i = 0; i < in_len; i += 4) {
                    uint32_t sextet_a = (i < in_len) ? b64_table[(unsigned char)s->chars[i]] : 0;
                    uint32_t sextet_b = (i+1 < in_len) ? b64_table[(unsigned char)s->chars[i+1]] : 0;
                    uint32_t sextet_c = (i+2 < in_len) ? b64_table[(unsigned char)s->chars[i+2]] : 0;
                    uint32_t sextet_d = (i+3 < in_len) ? b64_table[(unsigned char)s->chars[i+3]] : 0;
                    uint32_t triple = (sextet_a << 18) | (sextet_b << 12) | (sextet_c << 6) | sextet_d;
                    if (j < out_len) bdata[j++] = (triple >> 16) & 0xFF;
                    if (j < out_len) bdata[j++] = (triple >> 8) & 0xFF;
                    if (j < out_len) bdata[j++] = triple & 0xFF;
                }
            }
            return buf;
        }

        if (strcmp(enc_buf, "ascii") == 0) {
            // ASCII: mask to 7 bits
            int byte_len = (int)s->len;
            Item buf = create_buffer(byte_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                for (int i = 0; i < byte_len; i++)
                    bdata[i] = (uint8_t)(s->chars[i] & 0x7F);
            }
            return buf;
        }

        if (strcmp(enc_buf, "latin1") == 0 || strcmp(enc_buf, "binary") == 0) {
            // Latin1/binary: each char → one byte (identity mapping)
            int byte_len = (int)s->len;
            Item buf = create_buffer(byte_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata && byte_len > 0) {
                memcpy(bdata, s->chars, byte_len);
            }
            return buf;
        }

        if (strcmp(enc_buf, "ucs2") == 0 || strcmp(enc_buf, "ucs-2") == 0 ||
            strcmp(enc_buf, "utf16le") == 0 || strcmp(enc_buf, "utf-16le") == 0) {
            // UCS-2 / UTF-16LE: decode UTF-8 to code points, encode each as 2 bytes LE
            const char* p = s->chars;
            const char* end = p + s->len;
            // first pass: count code points
            int cp_count = 0;
            const char* q = p;
            while (q < end) {
                unsigned char c = (unsigned char)*q;
                if (c < 0x80) q += 1;
                else if ((c & 0xE0) == 0xC0) q += 2;
                else if ((c & 0xF0) == 0xE0) q += 3;
                else q += 4;
                cp_count++;
            }
            int out_bytes = cp_count * 2;
            Item buf = create_buffer(out_bytes);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                int j = 0;
                q = p;
                while (q < end && j + 1 < out_bytes + 1) {
                    uint32_t cp = 0;
                    unsigned char c = (unsigned char)*q;
                    if (c < 0x80) { cp = c; q += 1; }
                    else if ((c & 0xE0) == 0xC0 && q + 1 < end) {
                        cp = ((c & 0x1F) << 6) | (q[1] & 0x3F); q += 2;
                    } else if ((c & 0xF0) == 0xE0 && q + 2 < end) {
                        cp = ((c & 0x0F) << 12) | ((q[1] & 0x3F) << 6) | (q[2] & 0x3F); q += 3;
                    } else if ((c & 0xF8) == 0xF0 && q + 3 < end) {
                        cp = ((c & 0x07) << 18) | ((q[1] & 0x3F) << 12) | ((q[2] & 0x3F) << 6) | (q[3] & 0x3F); q += 4;
                        // surrogate pair for code points > 0xFFFF
                        if (cp > 0xFFFF && j + 3 < out_bytes + 1) {
                            cp -= 0x10000;
                            uint16_t hi = 0xD800 + (uint16_t)(cp >> 10);
                            uint16_t lo = 0xDC00 + (uint16_t)(cp & 0x3FF);
                            bdata[j++] = (uint8_t)(hi & 0xFF);
                            bdata[j++] = (uint8_t)(hi >> 8);
                            bdata[j++] = (uint8_t)(lo & 0xFF);
                            bdata[j++] = (uint8_t)(lo >> 8);
                            continue;
                        }
                    } else { q += 1; }
                    if (cp > 0xFFFF) cp = 0xFFFD; // replacement char
                    bdata[j++] = (uint8_t)(cp & 0xFF);
                    bdata[j++] = (uint8_t)((cp >> 8) & 0xFF);
                }
            }
            return buf;
        }

        // default: utf-8
        int byte_len = (int)s->len;
        Item buf = create_buffer(byte_len);
        int buf_byte_len = 0;
        uint8_t* bdata = buffer_data(buf, &buf_byte_len);
        if (bdata && byte_len > 0) {
            memcpy(bdata, s->chars, byte_len);
        }
        return buf;
    }

    // reject primitives that should not be accepted
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg),
            "The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an Array-like Object.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, data);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }

    // array of numbers
    if (tid == LMD_TYPE_ARRAY && js_array_length(data) >= 0) {
        int64_t arr_len = js_array_length(data);
        if (arr_len > 1048576) arr_len = 1048576;
        Item buf = create_buffer((int)arr_len);
        int buf_byte_len = 0;
        uint8_t* bdata = buffer_data(buf, &buf_byte_len);
        if (bdata) {
            for (int64_t i = 0; i < arr_len; i++) {
                Item elem = js_array_get_int(data, i);
                int64_t v = 0;
                TypeId et = get_type_id(elem);
                if (et == LMD_TYPE_INT) v = it2i(elem);
                else if (et == LMD_TYPE_FLOAT) v = (int64_t)it2d(elem);
                bdata[i] = (uint8_t)(v & 0xFF);
            }
        }
        return buf;
    }

    // typed array / buffer → copy
    if (js_is_typed_array(data)) {
        int src_len = 0;
        uint8_t* src_data = buffer_data(data, &src_len);
        Item buf = create_buffer(src_len);
        int dst_len = 0;
        uint8_t* dst_data = buffer_data(buf, &dst_len);
        if (src_data && dst_data && src_len > 0) {
            memcpy(dst_data, src_data, src_len);
        }
        return buf;
    }

    // plain object: {type:"Buffer", data:[...]} reviver pattern, or array-like with length
    if (tid == LMD_TYPE_MAP) {
        Item type_key = make_string_item("type", 4);
        Item data_key = make_string_item("data", 4);
        Item type_val = js_property_get(data, type_key);
        Item data_val = js_property_get(data, data_key);
        if (get_type_id(type_val) == LMD_TYPE_STRING && get_type_id(data_val) == LMD_TYPE_ARRAY) {
            // recursively create from the data array
            return js_buffer_from(data_val, encoding);
        }
        // array-like object with length property
        Item len_key = make_string_item("length", 6);
        Item len_val = js_property_get(data, len_key);
        TypeId lt = get_type_id(len_val);
        if (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) {
            int64_t arr_len = (lt == LMD_TYPE_INT) ? it2i(len_val) : (int64_t)it2d(len_val);
            if (arr_len < 0) arr_len = 0;
            if (arr_len > 1048576) arr_len = 1048576;
            Item buf = create_buffer((int)arr_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                for (int64_t i = 0; i < arr_len; i++) {
                    char idx[24];
                    snprintf(idx, sizeof(idx), "%lld", (long long)i);
                    Item idx_key = make_string_item(idx, (int)strlen(idx));
                    Item elem = js_property_get(data, idx_key);
                    int64_t v = 0;
                    TypeId et = get_type_id(elem);
                    if (et == LMD_TYPE_INT) v = it2i(elem);
                    else if (et == LMD_TYPE_FLOAT) v = (int64_t)it2d(elem);
                    bdata[i] = (uint8_t)(v & 0xFF);
                }
            }
            return buf;
        }
    }

    // unsupported type — throw TypeError
    {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg),
            "The first argument must be of type string or an instance of Buffer, ArrayBuffer, or Array or an Array-like Object.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, data);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
}

// ─── Buffer.concat(list, totalLength?) ──────────────────────────────────────
extern "C" Item js_buffer_concat(Item list) {
    if (js_array_length(list) < 0) return create_buffer(0);

    int64_t count = js_array_length(list);
    // compute total length
    int total = 0;
    for (int64_t i = 0; i < count; i++) {
        Item buf = js_array_get_int(list, i);
        int blen = 0;
        buffer_data(buf, &blen);
        total += blen;
    }
    if (total > 1048576) total = 1048576;

    Item result = create_buffer(total);
    int dst_len = 0;
    uint8_t* dst = buffer_data(result, &dst_len);
    int offset = 0;
    for (int64_t i = 0; i < count && offset < total; i++) {
        Item buf = js_array_get_int(list, i);
        int blen = 0;
        uint8_t* bdata = buffer_data(buf, &blen);
        if (bdata && blen > 0) {
            int copy = blen;
            if (offset + copy > total) copy = total - offset;
            memcpy(dst + offset, bdata, copy);
            offset += copy;
        }
    }
    return result;
}

// ─── Buffer.isBuffer(obj) ───────────────────────────────────────────────────
extern "C" Item js_buffer_isBuffer(Item obj) {
    if (!js_is_typed_array(obj)) return (Item){.item = b2it(false)};
    // Buffer is a Uint8Array — check element type
    Map* m = obj.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (ta && ta->element_type == JS_TYPED_UINT8)
        return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

// ─── Buffer.isEncoding(encoding) ────────────────────────────────────────────
extern "C" Item js_buffer_isEncoding(Item enc_item) {
    if (get_type_id(enc_item) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* enc = it2s(enc_item);
    if (!enc || enc->len == 0) return (Item){.item = b2it(false)};

    char buf[32];
    int len = (int)enc->len;
    if (len >= (int)sizeof(buf)) return (Item){.item = b2it(false)};
    for (int i = 0; i < len; i++)
        buf[i] = (enc->chars[i] >= 'A' && enc->chars[i] <= 'Z') ? enc->chars[i] + 32 : enc->chars[i];
    buf[len] = '\0';

    if (strcmp(buf, "utf8") == 0 || strcmp(buf, "utf-8") == 0 ||
        strcmp(buf, "hex") == 0 || strcmp(buf, "base64") == 0 ||
        strcmp(buf, "ascii") == 0 || strcmp(buf, "latin1") == 0 ||
        strcmp(buf, "binary") == 0 || strcmp(buf, "ucs2") == 0 ||
        strcmp(buf, "ucs-2") == 0 || strcmp(buf, "utf16le") == 0 ||
        strcmp(buf, "utf-16le") == 0 || strcmp(buf, "base64url") == 0)
        return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

// ─── Buffer.byteLength(string, encoding?) ───────────────────────────────────

// count UTF-8 code points in a string
static int utf8_codepoint_count(const char* s, int byte_len) {
    int count = 0;
    for (int i = 0; i < byte_len; ) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else i += 4;
        count++;
    }
    return count;
}

// compute base64 decoded byte length (accounting for padding)
static int base64_decoded_length(const char* s, int len) {
    if (len == 0) return 0;
    // count trailing '=' padding chars
    int pad = 0;
    int i = len - 1;
    while (i >= 0 && s[i] == '=') { pad++; i--; }
    // base64: decoded_bytes = floor((len - pad) * 3 / 4)
    int effective = len - pad;
    int n = (effective * 3) / 4;
    return n > 0 ? n : 0;
}

// normalize encoding string to lowercase in buf, return true if valid
static bool normalize_encoding(Item enc_item, char* out, int out_size) {
    if (get_type_id(enc_item) != LMD_TYPE_STRING) return false;
    String* enc = it2s(enc_item);
    if (!enc || enc->len == 0) return false;
    int len = (int)enc->len;
    if (len >= out_size) len = out_size - 1;
    for (int i = 0; i < len; i++)
        out[i] = (enc->chars[i] >= 'A' && enc->chars[i] <= 'Z') ? enc->chars[i] + 32 : enc->chars[i];
    out[len] = '\0';
    return true;
}

static bool is_known_encoding(const char* enc) {
    return strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0 ||
           strcmp(enc, "hex") == 0 ||
           strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0 ||
           strcmp(enc, "ascii") == 0 || strcmp(enc, "latin1") == 0 ||
           strcmp(enc, "binary") == 0 ||
           strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
           strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0;
}

extern "C" Item js_buffer_byteLength(Item str_item, Item enc_item) {
    if (get_type_id(str_item) == LMD_TYPE_STRING) {
        String* s = it2s(str_item);

        char enc[32] = "utf8";
        if (normalize_encoding(enc_item, enc, sizeof(enc))) {
            // recognized encodings with specific byte lengths
        }

        if (strcmp(enc, "hex") == 0) {
            return (Item){.item = i2it((int64_t)(s->len / 2))};
        }
        if (strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0) {
            return (Item){.item = i2it(base64_decoded_length(s->chars, (int)s->len))};
        }
        if (strcmp(enc, "ascii") == 0 || strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0) {
            return (Item){.item = i2it(utf8_codepoint_count(s->chars, (int)s->len))};
        }
        if (strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
            strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0) {
            return (Item){.item = i2it(utf8_codepoint_count(s->chars, (int)s->len) * 2)};
        }
        // utf8 (default, or unrecognized encoding)
        return (Item){.item = i2it((int64_t)s->len)};
    }
    if (js_is_typed_array(str_item)) {
        int blen = 0;
        buffer_data(str_item, &blen);
        return (Item){.item = i2it(blen)};
    }
    // check for ArrayBuffer
    if (js_is_arraybuffer(str_item)) {
        return (Item){.item = i2it(js_arraybuffer_byte_length(str_item))};
    }
    // check for DataView
    if (js_is_dataview(str_item)) {
        // DataView byte_length: get from the DataView struct via byteLength property
        Item bl_key = make_string_item("byteLength", 10);
        Item bl_val = js_property_get(str_item, bl_key);
        if (get_type_id(bl_val) == LMD_TYPE_INT) return bl_val;
        return (Item){.item = i2it(0)};
    }
    char msg[256];
    int pos = snprintf(msg, sizeof(msg),
        "The \"string\" argument must be of type string or an instance of Buffer or ArrayBuffer.");
    format_received_suffix(msg + pos, (int)sizeof(msg) - pos, str_item);
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
}

// ─── buf.toString(encoding?, start?, end?) ──────────────────────────────────
// coerce a JS value to an integer for toString start/end params
// undefined → default_val, NaN/objects → 0, strings → parseFloat, bools → 0/1
// result is clamped to [0, blen]
static int coerce_slice_index(Item item, int default_val, int blen) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_UNDEFINED) return (default_val < 0) ? 0 : (default_val > blen ? blen : default_val);

    double d = 0.0;
    if (tid == LMD_TYPE_INT) {
        d = (double)it2i(item);
    } else if (tid == LMD_TYPE_FLOAT) {
        d = it2d(item);
    } else if (tid == LMD_TYPE_STRING) {
        String* s = it2s(item);
        char* endp;
        d = strtod(s->chars, &endp);
        if (endp == s->chars) d = 0.0; // unparseable → NaN → 0
    } else if (tid == LMD_TYPE_BOOL) {
        d = it2i(item) ? 1.0 : 0.0;
    }
    // null, object, array, function etc. → d stays 0.0

    // NaN → 0
    if (d != d) d = 0.0;

    // handle infinity before casting
    if (isinf(d)) return d > 0 ? blen : 0;

    // truncate toward zero (Math.trunc)
    if (d > 0) d = floor(d);
    else if (d < 0) d = ceil(d);

    // clamp to [0, blen]
    int val = (int)d;
    if (val < 0) val = 0;
    if (val > blen) val = blen;
    return val;
}

extern "C" Item js_buffer_toString(Item buf, Item encoding, Item start_item, Item end_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return make_string_item("");

    // range support: start/end default to 0/blen
    int start = coerce_slice_index(start_item, 0, blen);
    int end = coerce_slice_index(end_item, blen, blen);
    if (start >= end) return make_string_item("");

    uint8_t* slice = data + start;
    int slice_len = end - start;

    char enc_buf[32] = "utf8";
    TypeId enc_tid = get_type_id(encoding);
    if (enc_tid == LMD_TYPE_STRING) {
        String* enc = it2s(encoding);
        int elen = (int)enc->len;
        if (elen >= (int)sizeof(enc_buf)) elen = (int)sizeof(enc_buf) - 1;
        for (int i = 0; i < elen; i++)
            enc_buf[i] = (enc->chars[i] >= 'A' && enc->chars[i] <= 'Z') ? enc->chars[i] + 32 : enc->chars[i];
        enc_buf[elen] = '\0';
        if (!is_known_encoding(enc_buf) &&
            strcmp(enc_buf, "utf8") != 0 && strcmp(enc_buf, "utf-8") != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Unknown encoding: %.*s", (int)enc->len, enc->chars);
            return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
        }
    } else if (enc_tid != LMD_TYPE_UNDEFINED) {
        // non-string, non-undefined encoding → throw ERR_UNKNOWN_ENCODING
        const char* type_str = "unknown";
        if (enc_tid == LMD_TYPE_INT) type_str = "number";
        else if (enc_tid == LMD_TYPE_FLOAT) type_str = "number";
        else if (enc_tid == LMD_TYPE_NULL) type_str = "null";
        else if (enc_tid == LMD_TYPE_BOOL) type_str = it2i(encoding) ? "true" : "false";
        char msg[256];
        if (enc_tid == LMD_TYPE_INT)
            snprintf(msg, sizeof(msg), "Unknown encoding: %lld", (long long)it2i(encoding));
        else if (enc_tid == LMD_TYPE_FLOAT)
            snprintf(msg, sizeof(msg), "Unknown encoding: %g", it2d(encoding));
        else
            snprintf(msg, sizeof(msg), "Unknown encoding: %s", type_str);
        return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
    }

    if (strcmp(enc_buf, "hex") == 0) {
        char* hex = (char*)mem_alloc(slice_len * 2 + 1, MEM_CAT_JS_RUNTIME);
        for (int i = 0; i < slice_len; i++) {
            static const char hx[] = "0123456789abcdef";
            hex[i * 2] = hx[slice[i] >> 4];
            hex[i * 2 + 1] = hx[slice[i] & 0xf];
        }
        hex[slice_len * 2] = '\0';
        Item result = make_string_item(hex, slice_len * 2);
        mem_free(hex);
        return result;
    }

    if (strcmp(enc_buf, "base64") == 0 || strcmp(enc_buf, "base64url") == 0) {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static const char b64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        bool is_url = (strcmp(enc_buf, "base64url") == 0);
        const char* table = is_url ? b64url : b64;
        int out_len = 4 * ((slice_len + 2) / 3);
        char* b64_str = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
        int j = 0;
        for (int i = 0; i < slice_len; i += 3) {
            uint32_t a = slice[i];
            uint32_t b_val = (i + 1 < slice_len) ? slice[i + 1] : 0;
            uint32_t c = (i + 2 < slice_len) ? slice[i + 2] : 0;
            uint32_t triple = (a << 16) | (b_val << 8) | c;
            b64_str[j++] = table[(triple >> 18) & 0x3F];
            b64_str[j++] = table[(triple >> 12) & 0x3F];
            if (i + 1 < slice_len) b64_str[j++] = table[(triple >> 6) & 0x3F];
            else if (!is_url) b64_str[j++] = '=';
            if (i + 2 < slice_len) b64_str[j++] = table[triple & 0x3F];
            else if (!is_url) b64_str[j++] = '=';
        }
        b64_str[j] = '\0';
        Item result = make_string_item(b64_str, j);
        mem_free(b64_str);
        return result;
    }

    if (strcmp(enc_buf, "ascii") == 0 || strcmp(enc_buf, "latin1") == 0 || strcmp(enc_buf, "binary") == 0) {
        // latin1: each byte maps directly to a char (ISO 8859-1)
        return make_string_item((const char*)slice, slice_len);
    }

    if (strcmp(enc_buf, "ucs2") == 0 || strcmp(enc_buf, "ucs-2") == 0 ||
        strcmp(enc_buf, "utf16le") == 0 || strcmp(enc_buf, "utf-16le") == 0) {
        // UCS-2/UTF-16LE: read pairs of bytes as little-endian uint16, encode to UTF-8
        int pairs = slice_len / 2;
        // worst case UTF-8: 3 bytes per code point (BMP), 4 for surrogates
        char* utf8 = (char*)mem_alloc(pairs * 3 + 1, MEM_CAT_JS_RUNTIME);
        int j = 0;
        for (int i = 0; i < pairs; i++) {
            uint16_t cp = (uint16_t)(slice[i * 2] | (slice[i * 2 + 1] << 8));
            if (cp < 0x80) {
                utf8[j++] = (char)cp;
            } else if (cp < 0x800) {
                utf8[j++] = (char)(0xC0 | (cp >> 6));
                utf8[j++] = (char)(0x80 | (cp & 0x3F));
            } else {
                utf8[j++] = (char)(0xE0 | (cp >> 12));
                utf8[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[j++] = (char)(0x80 | (cp & 0x3F));
            }
        }
        utf8[j] = '\0';
        Item result = make_string_item(utf8, j);
        mem_free(utf8);
        return result;
    }

    // default: utf-8
    return make_string_item((const char*)slice, slice_len);
}

// ─── buf.write(string, offset?, length?, encoding?) ─────────────────────────
extern "C" Item js_buffer_write(Item buf, Item str_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(0)};
    if (get_type_id(str_item) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};

    String* s = it2s(str_item);
    int offset = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) offset = (int)it2i(offset_item);
    if (offset < 0) offset = 0;
    if (offset >= blen) return (Item){.item = i2it(0)};

    int write_len = (int)s->len;
    if (offset + write_len > blen) write_len = blen - offset;
    memcpy(data + offset, s->chars, write_len);
    return (Item){.item = i2it(write_len)};
}

// ─── buf.copy(target, targetStart?, sourceStart?, sourceEnd?) ───────────────
// helper: coerce Item to int for copy offsets (string → parseInt, NaN → 0)
static int coerce_copy_offset(Item item, int default_val) {
    TypeId tid = get_type_id(item);
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED) return default_val;
    if (tid == LMD_TYPE_INT) return (int)it2i(item);
    if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(item);
        if (d != d) return 0; // NaN → 0
        return (int)d;
    }
    if (tid == LMD_TYPE_STRING) {
        String* str = it2s(item);
        if (str && str->chars) {
            char* end = nullptr;
            long val = strtol(str->chars, &end, 10);
            if (end == str->chars) return 0; // non-numeric string → 0
            return (int)val;
        }
    }
    return default_val;
}

extern "C" Item js_buffer_copy(Item src_buf, Item dst_buf, Item target_start_item, Item source_start_item, Item source_end_item) {
    int src_len = 0, dst_len = 0;
    uint8_t* src = buffer_data(src_buf, &src_len);
    uint8_t* dst = buffer_data(dst_buf, &dst_len);
    if (!src || !dst) return (Item){.item = i2it(0)};

    int target_start = coerce_copy_offset(target_start_item, 0);
    if (target_start < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"targetStart\" is out of range. It must be >= 0. Received %d", target_start);
        return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
    }
    if (target_start >= dst_len) return (Item){.item = i2it(0)};

    int source_start = coerce_copy_offset(source_start_item, 0);
    if (source_start < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"sourceStart\" is out of range. It must be >= 0. Received %d", source_start);
        return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
    }
    if (source_start > src_len) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"sourceStart\" is out of range. It must be >= 0 && <= %d. Received %d", src_len, source_start);
        return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
    }

    int source_end = src_len;
    TypeId se_tid = get_type_id(source_end_item);
    if (se_tid != LMD_TYPE_NULL && se_tid != LMD_TYPE_UNDEFINED) {
        source_end = coerce_copy_offset(source_end_item, src_len);
        if (source_end < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "The value of \"sourceEnd\" is out of range. It must be >= 0. Received %d", source_end);
            return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        }
    }
    if (source_end > src_len) source_end = src_len;
    if (source_end <= source_start) return (Item){.item = i2it(0)};

    int copy_len = source_end - source_start;
    if (target_start + copy_len > dst_len) copy_len = dst_len - target_start;
    if (copy_len <= 0) return (Item){.item = i2it(0)};

    memmove(dst + target_start, src + source_start, copy_len);
    return (Item){.item = i2it(copy_len)};
}

// ─── buf.equals(otherBuffer) ────────────────────────────────────────────────
extern "C" Item js_buffer_equals(Item a, Item b) {
    if (!js_is_typed_array(b)) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg), "The \"otherBuffer\" argument must be an instance of Buffer or Uint8Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, b);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    int alen = 0, blen = 0;
    uint8_t* adata = buffer_data(a, &alen);
    uint8_t* bdata = buffer_data(b, &blen);
    if (alen != blen) return (Item){.item = b2it(false)};
    if (alen == 0) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(memcmp(adata, bdata, alen) == 0)};
}

// ─── buf.compare(otherBuffer) ───────────────────────────────────────────────
extern "C" Item js_buffer_compare(Item a, Item b) {
    if (!js_is_typed_array(a)) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg), "The \"buf1\" argument must be an instance of Buffer or Uint8Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, a);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    if (!js_is_typed_array(b)) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg), "The \"buf2\" argument must be an instance of Buffer or Uint8Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, b);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    int alen = 0, blen = 0;
    uint8_t* adata = buffer_data(a, &alen);
    uint8_t* bdata = buffer_data(b, &blen);
    int min_len = alen < blen ? alen : blen;
    int cmp = min_len > 0 ? memcmp(adata, bdata, min_len) : 0;
    if (cmp == 0) cmp = alen - blen;
    if (cmp < 0) return (Item){.item = i2it(-1)};
    if (cmp > 0) return (Item){.item = i2it(1)};
    return (Item){.item = i2it(0)};
}

// helper: resolve byteOffset to a valid start index for indexOf
// NaN/undefined → default_val, negative → buf.length + offset, float → truncate
static int resolve_byte_offset(Item offset_item, int blen, int default_val) {
    TypeId tid = get_type_id(offset_item);
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED) return default_val;
    int offset;
    if (tid == LMD_TYPE_INT) {
        offset = (int)it2i(offset_item);
    } else if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(offset_item);
        if (d != d) return default_val; // NaN
        if (d >= (double)blen) return blen;
        if (d <= -(double)blen) return 0;
        offset = (int)d;
    } else {
        return default_val;
    }
    if (offset < 0) offset = blen + offset;
    if (offset < 0) offset = 0;
    if (offset > blen) offset = blen;
    return offset;
}

// helper: encode a JS string to bytes using specified encoding
// writes to out_buf (caller provides), returns byte count written
// max_out limits output size
static int encode_string_bytes(const char* str, int str_len, const char* enc,
                               uint8_t* out_buf, int max_out) {
    if (str_len == 0 || max_out == 0) return 0;

    if (strcmp(enc, "hex") == 0) {
        int byte_len = str_len / 2;
        if (byte_len > max_out) byte_len = max_out;
        for (int i = 0; i < byte_len && i * 2 + 1 < str_len; i++) {
            char hi = str[i * 2], lo = str[i * 2 + 1];
            auto hd = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return 0;
            };
            out_buf[i] = (uint8_t)((hd(hi) << 4) | hd(lo));
        }
        return byte_len;
    }

    if (strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0) {
        static const int b64[256] = {
            ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
            ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
            ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
            ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
            ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
            ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
            ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
            ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
            ['-']=62,['_']=63 // base64url variants
        };
        int pad = 0;
        int in_len = str_len;
        while (in_len > 0 && str[in_len - 1] == '=') { pad++; in_len--; }
        int out_len = (in_len * 3) / 4;
        if (out_len > max_out) out_len = max_out;
        int j = 0;
        for (int i = 0; i < str_len && j < out_len; i += 4) {
            uint32_t a = (i < str_len) ? b64[(unsigned char)str[i]] : 0;
            uint32_t b = (i+1 < str_len) ? b64[(unsigned char)str[i+1]] : 0;
            uint32_t c = (i+2 < str_len) ? b64[(unsigned char)str[i+2]] : 0;
            uint32_t d = (i+3 < str_len) ? b64[(unsigned char)str[i+3]] : 0;
            uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
            if (j < out_len) out_buf[j++] = (triple >> 16) & 0xFF;
            if (j < out_len) out_buf[j++] = (triple >> 8) & 0xFF;
            if (j < out_len) out_buf[j++] = triple & 0xFF;
        }
        return j;
    }

    if (strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
        strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0) {
        // encode each code point as 2 LE bytes
        int j = 0;
        for (int i = 0; i < str_len && j + 1 < max_out; ) {
            unsigned char ch = (unsigned char)str[i];
            uint32_t cp;
            if (ch < 0x80) { cp = ch; i += 1; }
            else if ((ch & 0xE0) == 0xC0) { cp = ch & 0x1F; if (i+1<str_len) cp = (cp<<6)|(str[i+1]&0x3F); i += 2; }
            else if ((ch & 0xF0) == 0xE0) { cp = ch & 0x0F; if (i+1<str_len) cp = (cp<<6)|(str[i+1]&0x3F); if (i+2<str_len) cp = (cp<<6)|(str[i+2]&0x3F); i += 3; }
            else { cp = ch & 0x07; if (i+1<str_len) cp = (cp<<6)|(str[i+1]&0x3F); if (i+2<str_len) cp = (cp<<6)|(str[i+2]&0x3F); if (i+3<str_len) cp = (cp<<6)|(str[i+3]&0x3F); i += 4; }
            out_buf[j++] = (uint8_t)(cp & 0xFF);
            out_buf[j++] = (uint8_t)((cp >> 8) & 0xFF);
        }
        return j;
    }

    if (strcmp(enc, "ascii") == 0) {
        int n = str_len > max_out ? max_out : str_len;
        for (int i = 0; i < n; i++) out_buf[i] = (uint8_t)(str[i] & 0x7F);
        return n;
    }

    if (strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0) {
        int n = str_len > max_out ? max_out : str_len;
        memcpy(out_buf, str, n);
        return n;
    }

    // utf8 (default) — raw bytes are already UTF-8
    int n = str_len > max_out ? max_out : str_len;
    memcpy(out_buf, str, n);
    return n;
}

// ─── buf.indexOf(value[, byteOffset[, encoding]]) ──────────────────────────
static bool is_ucs2_enc(const char* enc) {
    return strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
           strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0;
}

extern "C" Item js_buffer_indexOf(Item buf, Item value, Item offset_item, Item enc_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(-1)};

    // if offset_item is a string, treat it as encoding, offset = 0
    if (get_type_id(offset_item) == LMD_TYPE_STRING && get_type_id(enc_item) != LMD_TYPE_STRING) {
        enc_item = offset_item;
        offset_item = (Item){.item = ITEM_NULL};
    }

    int start = resolve_byte_offset(offset_item, blen, 0);

    // validate value type
    TypeId vtid = get_type_id(value);
    if (vtid != LMD_TYPE_INT && vtid != LMD_TYPE_FLOAT && vtid != LMD_TYPE_STRING
        && !js_is_typed_array(value)) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg),
            "The \"value\" argument must be one of type number or string "
            "or an instance of Buffer or Uint8Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, value);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }

    if (vtid == LMD_TYPE_INT || vtid == LMD_TYPE_FLOAT) {
        if (start >= blen) return (Item){.item = i2it(-1)};
        int byte_val;
        if (vtid == LMD_TYPE_INT) byte_val = (int)(it2i(value) & 0xFF);
        else byte_val = (int)((int64_t)it2d(value) & 0xFF);
        for (int i = start; i < blen; i++) {
            if (data[i] == byte_val) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    // resolve encoding
    char enc[32] = "utf8";
    if (normalize_encoding(enc_item, enc, sizeof(enc)) && !is_known_encoding(enc)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unknown encoding: %s", enc);
        return js_throw_type_error(msg);
    }

    // search for string value
    const uint8_t* needle = NULL;
    int needle_len = 0;
    uint8_t enc_buf[4096]; // stack buffer for encoded needle

    if (vtid == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0) {
            needle = (const uint8_t*)s->chars;
            needle_len = (int)s->len;
        } else {
            needle_len = encode_string_bytes(s->chars, (int)s->len, enc,
                                             enc_buf, (int)sizeof(enc_buf));
            needle = enc_buf;
        }
    } else if (js_is_typed_array(value)) {
        int vlen = 0;
        needle = buffer_data(value, &vlen);
        needle_len = vlen;
    }
    if (!needle) return (Item){.item = i2it(-1)};

    // UCS-2 mode: search in uint16_t units
    if (is_ucs2_enc(enc)) {
        int h_chars = blen / 2;
        int n_chars = needle_len / 2;
        if (n_chars == 0) return (Item){.item = i2it(-1)};
        int start_char = start / 2;
        const uint16_t* h16 = (const uint16_t*)data;
        const uint16_t* n16 = (const uint16_t*)needle;
        for (int i = start_char; i <= h_chars - n_chars; i++) {
            if (memcmp(h16 + i, n16, n_chars * 2) == 0)
                return (Item){.item = i2it(i * 2)};
        }
        return (Item){.item = i2it(-1)};
    }

    if (needle_len > 0) {
        for (int i = start; i <= blen - needle_len; i++) {
            if (memcmp(data + i, needle, needle_len) == 0)
                return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // empty needle: return byteOffset clamped to length
    return (Item){.item = i2it(start)};
}

// ─── buf.slice(start?, end?) — returns a new Buffer ─────────────────────────
extern "C" Item js_buffer_slice(Item buf, Item start_item, Item end_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return create_buffer(0);

    int start = 0, end = blen;
    if (get_type_id(start_item) == LMD_TYPE_INT) start = (int)it2i(start_item);
    if (get_type_id(end_item) == LMD_TYPE_INT) end = (int)it2i(end_item);
    if (start < 0) start = blen + start;
    if (end < 0) end = blen + end;
    if (start < 0) start = 0;
    if (end > blen) end = blen;
    if (start >= end) return create_buffer(0);

    int slice_len = end - start;
    Item result = create_buffer(slice_len);
    int dst_len = 0;
    uint8_t* dst = buffer_data(result, &dst_len);
    if (dst) memcpy(dst, data + start, slice_len);
    return result;
}

// ─── buf.fill(value, offset?, end?) ─────────────────────────────────────────
extern "C" Item js_buffer_fill(Item buf, Item value) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return buf;

    uint8_t fill_byte = 0;
    if (get_type_id(value) == LMD_TYPE_INT) fill_byte = (uint8_t)(it2i(value) & 0xFF);
    memset(data, fill_byte, blen);
    return buf;
}

// ─── Buffer.allocUnsafe(size) ───────────────────────────────────────────────
extern "C" Item js_buffer_allocUnsafe(Item size_item) {
    int64_t size = 0;
    TypeId tid = get_type_id(size_item);
    if (tid == LMD_TYPE_INT) size = it2i(size_item);
    else if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(size_item);
        if (d != d || d < 0 || d > 2147483647) {
            return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                "The value of \"size\" is out of range.");
        }
        size = (int64_t)d;
    }
    else {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"size\" argument must be of type number.");
    }
    if (size < 0 || size > 2147483647) {
        return js_throw_range_error_code("ERR_OUT_OF_RANGE",
            "The value of \"size\" is out of range.");
    }
    if (size > 1048576) size = 1048576;
    return create_buffer((int)size); // no zero-fill guarantee
}

// ─── buf.subarray(start?, end?) — alias for slice ───────────────────────────
extern "C" Item js_buffer_subarray(Item buf, Item start_item, Item end_item) {
    return js_buffer_slice(buf, start_item, end_item);
}

// ─── buf.includes(value[, byteOffset[, encoding]]) ─────────────────────────
extern "C" Item js_buffer_includes(Item buf, Item value, Item offset_item, Item enc_item) {
    Item idx = js_buffer_indexOf(buf, value, offset_item, enc_item);
    return (Item){.item = b2it(it2i(idx) >= 0)};
}

// ─── buf.lastIndexOf(value[, byteOffset[, encoding]]) ──────────────────────
extern "C" Item js_buffer_lastIndexOf(Item buf, Item value, Item offset_item, Item enc_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(-1)};

    // if offset_item is a string, treat it as encoding, offset = default
    if (get_type_id(offset_item) == LMD_TYPE_STRING && get_type_id(enc_item) != LMD_TYPE_STRING) {
        enc_item = offset_item;
        offset_item = (Item){.item = ITEM_NULL};
    }

    int end = resolve_byte_offset(offset_item, blen, blen - 1);
    if (end < 0) return (Item){.item = i2it(-1)};

    if (get_type_id(value) == LMD_TYPE_INT) {
        int byte_val = (int)(it2i(value) & 0xFF);
        int limit = (end < blen - 1) ? end : blen - 1;
        for (int i = limit; i >= 0; i--) {
            if (data[i] == byte_val) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    // resolve encoding
    char enc[32] = "utf8";
    if (normalize_encoding(enc_item, enc, sizeof(enc)) && !is_known_encoding(enc)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unknown encoding: %s", enc);
        return js_throw_type_error(msg);
    }

    // search for string or buffer value
    const uint8_t* needle = NULL;
    int needle_len = 0;
    uint8_t enc_buf[4096];

    if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0) {
            needle = (const uint8_t*)s->chars;
            needle_len = (int)s->len;
        } else {
            needle_len = encode_string_bytes(s->chars, (int)s->len, enc,
                                             enc_buf, (int)sizeof(enc_buf));
            needle = enc_buf;
        }
    } else if (js_is_typed_array(value)) {
        int vlen = 0;
        needle = buffer_data(value, &vlen);
        needle_len = vlen;
    }
    if (!needle) return (Item){.item = i2it(-1)};

    // UCS-2 mode: search in uint16_t units (reverse)
    if (is_ucs2_enc(enc)) {
        int h_chars = blen / 2;
        int n_chars = needle_len / 2;
        if (n_chars == 0) return (Item){.item = i2it(-1)};
        int end_char = end / 2;
        const uint16_t* h16 = (const uint16_t*)data;
        const uint16_t* n16 = (const uint16_t*)needle;
        int limit = end_char;
        if (limit > h_chars - n_chars) limit = h_chars - n_chars;
        for (int i = limit; i >= 0; i--) {
            if (memcmp(h16 + i, n16, n_chars * 2) == 0)
                return (Item){.item = i2it(i * 2)};
        }
        return (Item){.item = i2it(-1)};
    }

    if (needle_len > 0) {
        int limit = end;
        if (limit > blen - needle_len) limit = blen - needle_len;
        for (int i = limit; i >= 0; i--) {
            if (memcmp(data + i, needle, needle_len) == 0)
                return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    if (needle && needle_len == 0) {
        return (Item){.item = i2it(end < blen ? end : blen)};
    }

    return (Item){.item = i2it(-1)};
}

// ─── Offset validation helper for read/write methods ────────────────────────
// Returns the validated offset (>= 0), or -1 if an error was thrown.
// If an error is thrown, 'err_out' is set to the error Item.
static int validate_rw_offset(Item offset_item, int blen, int byte_size, const char* name, Item* err_out) {
    *err_out = ItemNull;
    TypeId tid = get_type_id(offset_item);

    // undefined → default to 0
    if (tid == LMD_TYPE_UNDEFINED) {
        if (byte_size > blen) {
            *err_out = js_throw_range_error_code("ERR_BUFFER_OUT_OF_BOUNDS",
                "Attempt to access memory outside buffer bounds");
            return -1;
        }
        return 0;
    }

    // must be a number (null, string, object, bool, etc. → type error)
    if (tid != LMD_TYPE_INT && tid != LMD_TYPE_FLOAT) {
        char msg[256];
        snprintf(msg, sizeof(msg), "The \"%s\" argument must be of type number.", name);
        *err_out = js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
        return -1;
    }

    double d;
    if (tid == LMD_TYPE_INT) {
        d = (double)it2i(offset_item);
    } else {
        d = it2d(offset_item);
    }

    // Infinity → ERR_OUT_OF_RANGE (treated as out-of-range integer, not "must be an integer")
    if (isinf(d)) {
        int max_off = blen - byte_size;
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"%s\" is out of range. It must be >= 0 and <= %d. Received %s",
            name, max_off, d > 0 ? "Infinity" : "-Infinity");
        *err_out = js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        return -1;
    }

    // NaN or fractional → ERR_OUT_OF_RANGE "must be an integer"
    if (d != d || d != (double)(int64_t)d) {
        char msg[256];
        if (d != d)
            snprintf(msg, sizeof(msg),
                "The value of \"%s\" is out of range. It must be an integer. Received NaN", name);
        else
            snprintf(msg, sizeof(msg),
                "The value of \"%s\" is out of range. It must be an integer. Received %.2f", name, d);
        *err_out = js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        return -1;
    }

    int off = (int)(int64_t)d;
    int max_off = blen - byte_size;
    if (off < 0 || off > max_off) {
        if (max_off < 0) {
            // data can't fit at any offset → buffer out of bounds
            *err_out = js_throw_range_error_code("ERR_BUFFER_OUT_OF_BOUNDS",
                "Attempt to access memory outside buffer bounds");
            return -1;
        }
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"%s\" is out of range. It must be >= 0 and <= %d. Received %d", name, max_off, off);
        *err_out = js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        return -1;
    }
    return off;
}

// ─── Endian-aware read methods ──────────────────────────────────────────────
extern "C" Item js_buffer_readUInt8(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 1, "offset", &err);
    if (off < 0) return err;
    return (Item){.item = i2it((int64_t)data[off])};
}

extern "C" Item js_buffer_readUInt16BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    uint16_t v = ((uint16_t)data[off] << 8) | data[off + 1];
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt16LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    uint16_t v = data[off] | ((uint16_t)data[off + 1] << 8);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt32BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    uint32_t v = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                 ((uint32_t)data[off + 2] << 8) | data[off + 3];
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt32LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    uint32_t v = data[off] | ((uint32_t)data[off + 1] << 8) |
                 ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt8(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 1, "offset", &err);
    if (off < 0) return err;
    return (Item){.item = i2it((int64_t)(int8_t)data[off])};
}

extern "C" Item js_buffer_readInt16BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int16_t v = (int16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt16LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int16_t v = (int16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt32BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int32_t v = (int32_t)(((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                           ((uint32_t)data[off + 2] << 8) | data[off + 3]);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt32LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int32_t v = (int32_t)(data[off] | ((uint32_t)data[off + 1] << 8) |
                           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24));
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readFloatBE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    uint32_t bits = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                    ((uint32_t)data[off + 2] << 8) | data[off + 3];
    float f;
    memcpy(&f, &bits, sizeof(float));
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = (double)f;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_buffer_readFloatLE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    uint32_t bits = data[off] | ((uint32_t)data[off + 1] << 8) |
                    ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
    float f;
    memcpy(&f, &bits, sizeof(float));
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = (double)f;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_buffer_readDoubleBE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 8, "offset", &err);
    if (off < 0) return err;
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) bits = (bits << 8) | data[off + i];
    double d;
    memcpy(&d, &bits, sizeof(double));
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = d;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_buffer_readDoubleLE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return make_js_undefined();
    Item err;
    int off = validate_rw_offset(offset_item, blen, 8, "offset", &err);
    if (off < 0) return err;
    double d;
    memcpy(&d, data + off, sizeof(double));
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = d;
    return (Item){.item = d2it(fp)};
}

// ─── Endian-aware write methods ──────────────────────────────────────────────
extern "C" Item js_buffer_writeUInt8(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(1)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 1, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 1)};
}

extern "C" Item js_buffer_writeUInt16BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(2)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)((v >> 8) & 0xFF);
    data[off + 1] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 2)};
}

extern "C" Item js_buffer_writeUInt16LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(2)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)(v & 0xFF);
    data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    return (Item){.item = i2it(off + 2)};
}

extern "C" Item js_buffer_writeUInt32BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)((v >> 24) & 0xFF);
    data[off + 1] = (uint8_t)((v >> 16) & 0xFF);
    data[off + 2] = (uint8_t)((v >> 8) & 0xFF);
    data[off + 3] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

extern "C" Item js_buffer_writeUInt32LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)(v & 0xFF);
    data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    data[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    data[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

// ─── Signed Integer Write Methods ────────────────────────────────────────────

extern "C" Item js_buffer_writeInt8(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(1)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 1, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 1)};
}

extern "C" Item js_buffer_writeInt16BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(2)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)((v >> 8) & 0xFF);
    data[off + 1] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 2)};
}

extern "C" Item js_buffer_writeInt16LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(2)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 2, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)(v & 0xFF);
    data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    return (Item){.item = i2it(off + 2)};
}

extern "C" Item js_buffer_writeInt32BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)((v >> 24) & 0xFF);
    data[off + 1] = (uint8_t)((v >> 16) & 0xFF);
    data[off + 2] = (uint8_t)((v >> 8) & 0xFF);
    data[off + 3] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

extern "C" Item js_buffer_writeInt32LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off]     = (uint8_t)(v & 0xFF);
    data[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    data[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    data[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

// ─── Float/Double Write Methods ──────────────────────────────────────────────

extern "C" Item js_buffer_writeFloatBE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    float val = 0;
    if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (float)it2d(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_INT) val = (float)it2i(value_item);
    uint32_t bits;
    memcpy(&bits, &val, 4);
    data[off]     = (uint8_t)((bits >> 24) & 0xFF);
    data[off + 1] = (uint8_t)((bits >> 16) & 0xFF);
    data[off + 2] = (uint8_t)((bits >> 8) & 0xFF);
    data[off + 3] = (uint8_t)(bits & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

extern "C" Item js_buffer_writeFloatLE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(4)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 4, "offset", &err);
    if (off < 0) return err;
    float val = 0;
    if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (float)it2d(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_INT) val = (float)it2i(value_item);
    uint32_t bits;
    memcpy(&bits, &val, 4);
    data[off]     = (uint8_t)(bits & 0xFF);
    data[off + 1] = (uint8_t)((bits >> 8) & 0xFF);
    data[off + 2] = (uint8_t)((bits >> 16) & 0xFF);
    data[off + 3] = (uint8_t)((bits >> 24) & 0xFF);
    return (Item){.item = i2it(off + 4)};
}

extern "C" Item js_buffer_writeDoubleBE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(8)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 8, "offset", &err);
    if (off < 0) return err;
    double val = 0;
    if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = it2d(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_INT) val = (double)it2i(value_item);
    uint64_t bits;
    memcpy(&bits, &val, 8);
    for (int j = 7; j >= 0; j--) {
        data[off + (7 - j)] = (uint8_t)((bits >> (j * 8)) & 0xFF);
    }
    return (Item){.item = i2it(off + 8)};
}

extern "C" Item js_buffer_writeDoubleLE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data) return (Item){.item = i2it(8)};
    Item err;
    int off = validate_rw_offset(offset_item, blen, 8, "offset", &err);
    if (off < 0) return err;
    double val = 0;
    if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = it2d(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_INT) val = (double)it2i(value_item);
    uint64_t bits;
    memcpy(&bits, &val, 8);
    for (int j = 0; j < 8; j++) {
        data[off + j] = (uint8_t)((bits >> (j * 8)) & 0xFF);
    }
    return (Item){.item = i2it(off + 8)};
}

// ─── toJSON ──────────────────────────────────────────────────────────────────

extern "C" Item js_buffer_toJSON(Item buf) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    Item result = js_new_object();
    js_property_set(result, make_string_item("type"), make_string_item("Buffer"));
    Item arr = js_array_new(0);
    if (data) {
        for (int i = 0; i < blen; i++) {
            js_array_push(arr, (Item){.item = i2it((int64_t)data[i])});
        }
    }
    js_property_set(result, make_string_item("data"), arr);
    return result;
}

// ─── swap16 / swap32 ─────────────────────────────────────────────────────────

extern "C" Item js_buffer_swap16(Item buf) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen % 2 != 0) return buf;
    for (int i = 0; i < blen; i += 2) {
        uint8_t tmp = data[i];
        data[i] = data[i + 1];
        data[i + 1] = tmp;
    }
    return buf;
}

extern "C" Item js_buffer_swap32(Item buf) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen % 4 != 0) return buf;
    for (int i = 0; i < blen; i += 4) {
        uint8_t t0 = data[i], t1 = data[i + 1];
        data[i] = data[i + 3];
        data[i + 1] = data[i + 2];
        data[i + 2] = t1;
        data[i + 3] = t0;
    }
    return buf;
}

extern "C" Item js_buffer_swap64(Item buf) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen % 8 != 0) return buf;
    for (int i = 0; i < blen; i += 8) {
        for (int j = 0; j < 4; j++) {
            uint8_t tmp = data[i + j];
            data[i + j] = data[i + 7 - j];
            data[i + 7 - j] = tmp;
        }
    }
    return buf;
}

// ─── Helpers & statics ──────────────────────────────────────────────────────

static Item buffer_namespace = {0};
static Item buffer_prototype = {0};

static void buf_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

// ─── Variable-width read/write ──────────────────────────────────────────────

// buf.readUIntBE(offset, byteLength) — read unsigned int of 1-6 bytes, big-endian
extern "C" Item js_buffer_readUIntBE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = 0; i < nbytes; i++) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it((int64_t)val)};
}

// buf.readUIntLE(offset, byteLength)
extern "C" Item js_buffer_readUIntLE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = nbytes - 1; i >= 0; i--) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it((int64_t)val)};
}

// buf.readIntBE(offset, byteLength) — signed
extern "C" Item js_buffer_readIntBE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = 0; i < nbytes; i++) val = (val << 8) | data[offset + i];
    // sign extend
    if (val & ((uint64_t)1 << (nbytes * 8 - 1))) {
        val |= ~(((uint64_t)1 << (nbytes * 8)) - 1);
    }
    return (Item){.item = i2it((int64_t)val)};
}

// buf.readIntLE(offset, byteLength)
extern "C" Item js_buffer_readIntLE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = nbytes - 1; i >= 0; i--) val = (val << 8) | data[offset + i];
    if (val & ((uint64_t)1 << (nbytes * 8 - 1))) {
        val |= ~(((uint64_t)1 << (nbytes * 8)) - 1);
    }
    return (Item){.item = i2it((int64_t)val)};
}

// buf.writeUIntBE(value, offset, byteLength)
extern "C" Item js_buffer_writeUIntBE(Item buf, Item value_item, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    uint64_t val = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) val = (uint64_t)it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (uint64_t)it2d(value_item);
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    for (int i = nbytes - 1; i >= 0; i--) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + nbytes)};
}

// buf.writeUIntLE(value, offset, byteLength)
extern "C" Item js_buffer_writeUIntLE(Item buf, Item value_item, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    int nbytes = (get_type_id(byte_len_item) == LMD_TYPE_INT) ? (int)it2i(byte_len_item) : 1;
    uint64_t val = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) val = (uint64_t)it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (uint64_t)it2d(value_item);
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    for (int i = 0; i < nbytes; i++) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + nbytes)};
}

// buf.writeIntBE(value, offset, byteLength)
extern "C" Item js_buffer_writeIntBE(Item buf, Item value_item, Item offset_item, Item byte_len_item) {
    return js_buffer_writeUIntBE(buf, value_item, offset_item, byte_len_item);
}

// buf.writeIntLE(value, offset, byteLength)
extern "C" Item js_buffer_writeIntLE(Item buf, Item value_item, Item offset_item, Item byte_len_item) {
    return js_buffer_writeUIntLE(buf, value_item, offset_item, byte_len_item);
}

// ─── BigInt64 read/write ────────────────────────────────────────────────────

extern "C" Item js_buffer_readBigInt64BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    int64_t val = 0;
    for (int i = 0; i < 8; i++) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it(val)};
}

extern "C" Item js_buffer_readBigInt64LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    int64_t val = 0;
    for (int i = 7; i >= 0; i--) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it(val)};
}

extern "C" Item js_buffer_readBigUInt64BE(Item buf, Item offset_item) {
    return js_buffer_readBigInt64BE(buf, offset_item);
}

extern "C" Item js_buffer_readBigUInt64LE(Item buf, Item offset_item) {
    return js_buffer_readBigInt64LE(buf, offset_item);
}

extern "C" Item js_buffer_writeBigInt64BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    int64_t val = (get_type_id(value_item) == LMD_TYPE_INT) ? it2i(value_item) : 0;
    for (int i = 7; i >= 0; i--) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

extern "C" Item js_buffer_writeBigInt64LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = (get_type_id(offset_item) == LMD_TYPE_INT) ? (int)it2i(offset_item) : 0;
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    int64_t val = (get_type_id(value_item) == LMD_TYPE_INT) ? it2i(value_item) : 0;
    for (int i = 0; i < 8; i++) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

extern "C" Item js_buffer_writeBigUInt64BE(Item buf, Item value_item, Item offset_item) {
    return js_buffer_writeBigInt64BE(buf, value_item, offset_item);
}

extern "C" Item js_buffer_writeBigUInt64LE(Item buf, Item value_item, Item offset_item) {
    return js_buffer_writeBigInt64LE(buf, value_item, offset_item);
}

// ─── Static Buffer.compare(buf1, buf2) ─────────────────────────────────────

extern "C" Item js_buffer_compare_static(Item a, Item b) {
    return js_buffer_compare(a, b);
}

// ─── Buffer.allocUnsafeSlow(size) ───────────────────────────────────────────

extern "C" Item js_buffer_allocUnsafeSlow(Item size_item) {
    return js_buffer_allocUnsafe(size_item);
}

// ─── Instance method wrappers (this from js_get_current_this()) ─────────────

extern "C" Item js_get_current_this(void);

#define THIS js_get_current_this()

// each instance wrapper reads this from js_get_current_this() and forwards
extern "C" Item js_buf_inst_toString(Item encoding, Item start_item, Item end_item) {
    return js_buffer_toString(THIS, encoding, start_item, end_item);
}
extern "C" Item js_buf_inst_write(Item str_item, Item offset_item) {
    return js_buffer_write(THIS, str_item, offset_item);
}
extern "C" Item js_buf_inst_copy(Item dst_buf, Item target_start_item, Item source_start_item, Item source_end_item) {
    return js_buffer_copy(THIS, dst_buf, target_start_item, source_start_item, source_end_item);
}
extern "C" Item js_buf_inst_equals(Item other) {
    return js_buffer_equals(THIS, other);
}
extern "C" Item js_buf_inst_compare(Item other) {
    if (!js_is_typed_array(other)) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg), "The \"target\" argument must be an instance of Buffer or Uint8Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, other);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    return js_buffer_compare(THIS, other);
}
extern "C" Item js_buf_inst_indexOf(Item value, Item offset_item, Item enc_item) {
    return js_buffer_indexOf(THIS, value, offset_item, enc_item);
}
extern "C" Item js_buf_inst_lastIndexOf(Item value, Item offset_item, Item enc_item) {
    return js_buffer_lastIndexOf(THIS, value, offset_item, enc_item);
}
extern "C" Item js_buf_inst_includes(Item value, Item offset_item, Item enc_item) {
    return js_buffer_includes(THIS, value, offset_item, enc_item);
}
extern "C" Item js_buf_inst_slice(Item start_item, Item end_item) {
    return js_buffer_slice(THIS, start_item, end_item);
}
extern "C" Item js_buf_inst_subarray(Item start_item, Item end_item) {
    return js_buffer_subarray(THIS, start_item, end_item);
}
extern "C" Item js_buf_inst_fill(Item value) {
    return js_buffer_fill(THIS, value);
}

// endian read wrappers
extern "C" Item js_buf_inst_readUInt8(Item off) { return js_buffer_readUInt8(THIS, off); }
extern "C" Item js_buf_inst_readUInt16BE(Item off) { return js_buffer_readUInt16BE(THIS, off); }
extern "C" Item js_buf_inst_readUInt16LE(Item off) { return js_buffer_readUInt16LE(THIS, off); }
extern "C" Item js_buf_inst_readUInt32BE(Item off) { return js_buffer_readUInt32BE(THIS, off); }
extern "C" Item js_buf_inst_readUInt32LE(Item off) { return js_buffer_readUInt32LE(THIS, off); }
extern "C" Item js_buf_inst_readInt8(Item off) { return js_buffer_readInt8(THIS, off); }
extern "C" Item js_buf_inst_readInt16BE(Item off) { return js_buffer_readInt16BE(THIS, off); }
extern "C" Item js_buf_inst_readInt16LE(Item off) { return js_buffer_readInt16LE(THIS, off); }
extern "C" Item js_buf_inst_readInt32BE(Item off) { return js_buffer_readInt32BE(THIS, off); }
extern "C" Item js_buf_inst_readInt32LE(Item off) { return js_buffer_readInt32LE(THIS, off); }
extern "C" Item js_buf_inst_readFloatBE(Item off) { return js_buffer_readFloatBE(THIS, off); }
extern "C" Item js_buf_inst_readFloatLE(Item off) { return js_buffer_readFloatLE(THIS, off); }
extern "C" Item js_buf_inst_readDoubleBE(Item off) { return js_buffer_readDoubleBE(THIS, off); }
extern "C" Item js_buf_inst_readDoubleLE(Item off) { return js_buffer_readDoubleLE(THIS, off); }

// variable-width read wrappers
extern "C" Item js_buf_inst_readUIntBE(Item off, Item bl) { return js_buffer_readUIntBE(THIS, off, bl); }
extern "C" Item js_buf_inst_readUIntLE(Item off, Item bl) { return js_buffer_readUIntLE(THIS, off, bl); }
extern "C" Item js_buf_inst_readIntBE(Item off, Item bl) { return js_buffer_readIntBE(THIS, off, bl); }
extern "C" Item js_buf_inst_readIntLE(Item off, Item bl) { return js_buffer_readIntLE(THIS, off, bl); }

// BigInt64 read wrappers
extern "C" Item js_buf_inst_readBigInt64BE(Item off) { return js_buffer_readBigInt64BE(THIS, off); }
extern "C" Item js_buf_inst_readBigInt64LE(Item off) { return js_buffer_readBigInt64LE(THIS, off); }
extern "C" Item js_buf_inst_readBigUInt64BE(Item off) { return js_buffer_readBigUInt64BE(THIS, off); }
extern "C" Item js_buf_inst_readBigUInt64LE(Item off) { return js_buffer_readBigUInt64LE(THIS, off); }

// endian write wrappers
extern "C" Item js_buf_inst_writeUInt8(Item v, Item o) { return js_buffer_writeUInt8(THIS, v, o); }
extern "C" Item js_buf_inst_writeUInt16BE(Item v, Item o) { return js_buffer_writeUInt16BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeUInt16LE(Item v, Item o) { return js_buffer_writeUInt16LE(THIS, v, o); }
extern "C" Item js_buf_inst_writeUInt32BE(Item v, Item o) { return js_buffer_writeUInt32BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeUInt32LE(Item v, Item o) { return js_buffer_writeUInt32LE(THIS, v, o); }
extern "C" Item js_buf_inst_writeInt8(Item v, Item o) { return js_buffer_writeInt8(THIS, v, o); }
extern "C" Item js_buf_inst_writeInt16BE(Item v, Item o) { return js_buffer_writeInt16BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeInt16LE(Item v, Item o) { return js_buffer_writeInt16LE(THIS, v, o); }
extern "C" Item js_buf_inst_writeInt32BE(Item v, Item o) { return js_buffer_writeInt32BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeInt32LE(Item v, Item o) { return js_buffer_writeInt32LE(THIS, v, o); }
extern "C" Item js_buf_inst_writeFloatBE(Item v, Item o) { return js_buffer_writeFloatBE(THIS, v, o); }
extern "C" Item js_buf_inst_writeFloatLE(Item v, Item o) { return js_buffer_writeFloatLE(THIS, v, o); }
extern "C" Item js_buf_inst_writeDoubleBE(Item v, Item o) { return js_buffer_writeDoubleBE(THIS, v, o); }
extern "C" Item js_buf_inst_writeDoubleLE(Item v, Item o) { return js_buffer_writeDoubleLE(THIS, v, o); }

// variable-width write wrappers
extern "C" Item js_buf_inst_writeUIntBE(Item v, Item o, Item bl) { return js_buffer_writeUIntBE(THIS, v, o, bl); }
extern "C" Item js_buf_inst_writeUIntLE(Item v, Item o, Item bl) { return js_buffer_writeUIntLE(THIS, v, o, bl); }
extern "C" Item js_buf_inst_writeIntBE(Item v, Item o, Item bl) { return js_buffer_writeIntBE(THIS, v, o, bl); }
extern "C" Item js_buf_inst_writeIntLE(Item v, Item o, Item bl) { return js_buffer_writeIntLE(THIS, v, o, bl); }

// BigInt64 write wrappers
extern "C" Item js_buf_inst_writeBigInt64BE(Item v, Item o) { return js_buffer_writeBigInt64BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeBigInt64LE(Item v, Item o) { return js_buffer_writeBigInt64LE(THIS, v, o); }
extern "C" Item js_buf_inst_writeBigUInt64BE(Item v, Item o) { return js_buffer_writeBigUInt64BE(THIS, v, o); }
extern "C" Item js_buf_inst_writeBigUInt64LE(Item v, Item o) { return js_buffer_writeBigUInt64LE(THIS, v, o); }

// other instance wrappers
extern "C" Item js_buf_inst_toJSON() { return js_buffer_toJSON(THIS); }
extern "C" Item js_buf_inst_swap16() { return js_buffer_swap16(THIS); }
extern "C" Item js_buf_inst_swap32() { return js_buffer_swap32(THIS); }
extern "C" Item js_buf_inst_swap64() { return js_buffer_swap64(THIS); }

#undef THIS

// ─── Buffer Prototype ───────────────────────────────────────────────────────

extern "C" Item js_get_buffer_prototype(void) {
    if (buffer_prototype.item != 0) return buffer_prototype;

    buffer_prototype = js_new_object();

    buf_set_method(buffer_prototype, "toString",   (void*)js_buf_inst_toString, 3);
    buf_set_method(buffer_prototype, "write",      (void*)js_buf_inst_write, 2);
    buf_set_method(buffer_prototype, "copy",       (void*)js_buf_inst_copy, 4);
    buf_set_method(buffer_prototype, "equals",     (void*)js_buf_inst_equals, 1);
    buf_set_method(buffer_prototype, "compare",    (void*)js_buf_inst_compare, 1);
    buf_set_method(buffer_prototype, "indexOf",    (void*)js_buf_inst_indexOf, 3);
    buf_set_method(buffer_prototype, "lastIndexOf",(void*)js_buf_inst_lastIndexOf, 3);
    buf_set_method(buffer_prototype, "includes",   (void*)js_buf_inst_includes, 3);
    buf_set_method(buffer_prototype, "slice",      (void*)js_buf_inst_slice, 2);
    buf_set_method(buffer_prototype, "subarray",   (void*)js_buf_inst_subarray, 2);
    buf_set_method(buffer_prototype, "fill",       (void*)js_buf_inst_fill, 1);

    // endian-aware reads (1 arg: offset)
    buf_set_method(buffer_prototype, "readUInt8",     (void*)js_buf_inst_readUInt8, 1);
    buf_set_method(buffer_prototype, "readUInt16BE",  (void*)js_buf_inst_readUInt16BE, 1);
    buf_set_method(buffer_prototype, "readUInt16LE",  (void*)js_buf_inst_readUInt16LE, 1);
    buf_set_method(buffer_prototype, "readUInt32BE",  (void*)js_buf_inst_readUInt32BE, 1);
    buf_set_method(buffer_prototype, "readUInt32LE",  (void*)js_buf_inst_readUInt32LE, 1);
    buf_set_method(buffer_prototype, "readInt8",      (void*)js_buf_inst_readInt8, 1);
    buf_set_method(buffer_prototype, "readInt16BE",   (void*)js_buf_inst_readInt16BE, 1);
    buf_set_method(buffer_prototype, "readInt16LE",   (void*)js_buf_inst_readInt16LE, 1);
    buf_set_method(buffer_prototype, "readInt32BE",   (void*)js_buf_inst_readInt32BE, 1);
    buf_set_method(buffer_prototype, "readInt32LE",   (void*)js_buf_inst_readInt32LE, 1);
    buf_set_method(buffer_prototype, "readFloatBE",   (void*)js_buf_inst_readFloatBE, 1);
    buf_set_method(buffer_prototype, "readFloatLE",   (void*)js_buf_inst_readFloatLE, 1);
    buf_set_method(buffer_prototype, "readDoubleBE",  (void*)js_buf_inst_readDoubleBE, 1);
    buf_set_method(buffer_prototype, "readDoubleLE",  (void*)js_buf_inst_readDoubleLE, 1);

    // variable-width reads (2 args: offset, byteLength)
    buf_set_method(buffer_prototype, "readUIntBE",    (void*)js_buf_inst_readUIntBE, 2);
    buf_set_method(buffer_prototype, "readUIntLE",    (void*)js_buf_inst_readUIntLE, 2);
    buf_set_method(buffer_prototype, "readIntBE",     (void*)js_buf_inst_readIntBE, 2);
    buf_set_method(buffer_prototype, "readIntLE",     (void*)js_buf_inst_readIntLE, 2);

    // BigInt64 reads (1 arg: offset)
    buf_set_method(buffer_prototype, "readBigInt64BE",  (void*)js_buf_inst_readBigInt64BE, 1);
    buf_set_method(buffer_prototype, "readBigInt64LE",  (void*)js_buf_inst_readBigInt64LE, 1);
    buf_set_method(buffer_prototype, "readBigUInt64BE", (void*)js_buf_inst_readBigUInt64BE, 1);
    buf_set_method(buffer_prototype, "readBigUInt64LE", (void*)js_buf_inst_readBigUInt64LE, 1);

    // endian-aware writes (2 args: value, offset)
    buf_set_method(buffer_prototype, "writeUInt8",    (void*)js_buf_inst_writeUInt8, 2);
    buf_set_method(buffer_prototype, "writeUInt16BE", (void*)js_buf_inst_writeUInt16BE, 2);
    buf_set_method(buffer_prototype, "writeUInt16LE", (void*)js_buf_inst_writeUInt16LE, 2);
    buf_set_method(buffer_prototype, "writeUInt32BE", (void*)js_buf_inst_writeUInt32BE, 2);
    buf_set_method(buffer_prototype, "writeUInt32LE", (void*)js_buf_inst_writeUInt32LE, 2);
    buf_set_method(buffer_prototype, "writeInt8",     (void*)js_buf_inst_writeInt8, 2);
    buf_set_method(buffer_prototype, "writeInt16BE",  (void*)js_buf_inst_writeInt16BE, 2);
    buf_set_method(buffer_prototype, "writeInt16LE",  (void*)js_buf_inst_writeInt16LE, 2);
    buf_set_method(buffer_prototype, "writeInt32BE",  (void*)js_buf_inst_writeInt32BE, 2);
    buf_set_method(buffer_prototype, "writeInt32LE",  (void*)js_buf_inst_writeInt32LE, 2);
    buf_set_method(buffer_prototype, "writeFloatBE",  (void*)js_buf_inst_writeFloatBE, 2);
    buf_set_method(buffer_prototype, "writeFloatLE",  (void*)js_buf_inst_writeFloatLE, 2);
    buf_set_method(buffer_prototype, "writeDoubleBE", (void*)js_buf_inst_writeDoubleBE, 2);
    buf_set_method(buffer_prototype, "writeDoubleLE", (void*)js_buf_inst_writeDoubleLE, 2);

    // variable-width writes (3 args: value, offset, byteLength)
    buf_set_method(buffer_prototype, "writeUIntBE",   (void*)js_buf_inst_writeUIntBE, 3);
    buf_set_method(buffer_prototype, "writeUIntLE",   (void*)js_buf_inst_writeUIntLE, 3);
    buf_set_method(buffer_prototype, "writeIntBE",    (void*)js_buf_inst_writeIntBE, 3);
    buf_set_method(buffer_prototype, "writeIntLE",    (void*)js_buf_inst_writeIntLE, 3);

    // BigInt64 writes (2 args: value, offset)
    buf_set_method(buffer_prototype, "writeBigInt64BE",  (void*)js_buf_inst_writeBigInt64BE, 2);
    buf_set_method(buffer_prototype, "writeBigInt64LE",  (void*)js_buf_inst_writeBigInt64LE, 2);
    buf_set_method(buffer_prototype, "writeBigUInt64BE", (void*)js_buf_inst_writeBigUInt64BE, 2);
    buf_set_method(buffer_prototype, "writeBigUInt64LE", (void*)js_buf_inst_writeBigUInt64LE, 2);

    // other instance methods
    buf_set_method(buffer_prototype, "toJSON",  (void*)js_buf_inst_toJSON, 0);
    buf_set_method(buffer_prototype, "swap16",  (void*)js_buf_inst_swap16, 0);
    buf_set_method(buffer_prototype, "swap32",  (void*)js_buf_inst_swap32, 0);
    buf_set_method(buffer_prototype, "swap64",  (void*)js_buf_inst_swap64, 0);

    return buffer_prototype;
}

// ─── Namespace ───────────────────────────────────────────────────────────────

extern "C" Item js_get_buffer_namespace(void) {
    if (buffer_namespace.item != 0) return buffer_namespace;

    buffer_namespace = js_new_object();

    // static methods (Buffer.alloc, Buffer.from, etc.)
    buf_set_method(buffer_namespace, "alloc",      (void*)js_buffer_alloc, 2);
    buf_set_method(buffer_namespace, "allocUnsafe", (void*)js_buffer_allocUnsafe, 1);
    buf_set_method(buffer_namespace, "from",       (void*)js_buffer_from, 2);
    buf_set_method(buffer_namespace, "concat",     (void*)js_buffer_concat, 1);
    buf_set_method(buffer_namespace, "isBuffer",   (void*)js_buffer_isBuffer, 1);
    buf_set_method(buffer_namespace, "isEncoding", (void*)js_buffer_isEncoding, 1);
    buf_set_method(buffer_namespace, "byteLength", (void*)js_buffer_byteLength, 2);
    buf_set_method(buffer_namespace, "allocUnsafeSlow", (void*)js_buffer_allocUnsafeSlow, 1);
    buf_set_method(buffer_namespace, "compare",    (void*)js_buffer_compare_static, 2);
    buf_set_method(buffer_namespace, "toString",   (void*)js_buffer_toString, 4);
    buf_set_method(buffer_namespace, "write",      (void*)js_buffer_write, 3);
    buf_set_method(buffer_namespace, "copy",       (void*)js_buffer_copy, 5);
    buf_set_method(buffer_namespace, "equals",     (void*)js_buffer_equals, 2);
    buf_set_method(buffer_namespace, "compare",    (void*)js_buffer_compare, 2);
    buf_set_method(buffer_namespace, "indexOf",    (void*)js_buffer_indexOf, 3);
    buf_set_method(buffer_namespace, "lastIndexOf",(void*)js_buffer_lastIndexOf, 3);
    buf_set_method(buffer_namespace, "includes",   (void*)js_buffer_includes, 3);
    buf_set_method(buffer_namespace, "slice",      (void*)js_buffer_slice, 3);
    buf_set_method(buffer_namespace, "subarray",   (void*)js_buffer_subarray, 3);
    buf_set_method(buffer_namespace, "fill",       (void*)js_buffer_fill, 2);

    // endian-aware read methods
    buf_set_method(buffer_namespace, "readUInt8",     (void*)js_buffer_readUInt8, 2);
    buf_set_method(buffer_namespace, "readUInt16BE",  (void*)js_buffer_readUInt16BE, 2);
    buf_set_method(buffer_namespace, "readUInt16LE",  (void*)js_buffer_readUInt16LE, 2);
    buf_set_method(buffer_namespace, "readUInt32BE",  (void*)js_buffer_readUInt32BE, 2);
    buf_set_method(buffer_namespace, "readUInt32LE",  (void*)js_buffer_readUInt32LE, 2);
    buf_set_method(buffer_namespace, "readInt8",      (void*)js_buffer_readInt8, 2);
    buf_set_method(buffer_namespace, "readInt16BE",   (void*)js_buffer_readInt16BE, 2);
    buf_set_method(buffer_namespace, "readInt16LE",   (void*)js_buffer_readInt16LE, 2);
    buf_set_method(buffer_namespace, "readInt32BE",   (void*)js_buffer_readInt32BE, 2);
    buf_set_method(buffer_namespace, "readInt32LE",   (void*)js_buffer_readInt32LE, 2);
    buf_set_method(buffer_namespace, "readFloatBE",   (void*)js_buffer_readFloatBE, 2);
    buf_set_method(buffer_namespace, "readFloatLE",   (void*)js_buffer_readFloatLE, 2);
    buf_set_method(buffer_namespace, "readDoubleBE",  (void*)js_buffer_readDoubleBE, 2);
    buf_set_method(buffer_namespace, "readDoubleLE",  (void*)js_buffer_readDoubleLE, 2);

    // endian-aware write methods
    buf_set_method(buffer_namespace, "writeUInt8",    (void*)js_buffer_writeUInt8, 3);
    buf_set_method(buffer_namespace, "writeUInt16BE", (void*)js_buffer_writeUInt16BE, 3);
    buf_set_method(buffer_namespace, "writeUInt16LE", (void*)js_buffer_writeUInt16LE, 3);
    buf_set_method(buffer_namespace, "writeUInt32BE", (void*)js_buffer_writeUInt32BE, 3);
    buf_set_method(buffer_namespace, "writeUInt32LE", (void*)js_buffer_writeUInt32LE, 3);
    buf_set_method(buffer_namespace, "writeInt8",     (void*)js_buffer_writeInt8, 3);
    buf_set_method(buffer_namespace, "writeInt16BE",  (void*)js_buffer_writeInt16BE, 3);
    buf_set_method(buffer_namespace, "writeInt16LE",  (void*)js_buffer_writeInt16LE, 3);
    buf_set_method(buffer_namespace, "writeInt32BE",  (void*)js_buffer_writeInt32BE, 3);
    buf_set_method(buffer_namespace, "writeInt32LE",  (void*)js_buffer_writeInt32LE, 3);
    buf_set_method(buffer_namespace, "writeFloatBE",  (void*)js_buffer_writeFloatBE, 3);
    buf_set_method(buffer_namespace, "writeFloatLE",  (void*)js_buffer_writeFloatLE, 3);
    buf_set_method(buffer_namespace, "writeDoubleBE", (void*)js_buffer_writeDoubleBE, 3);
    buf_set_method(buffer_namespace, "writeDoubleLE", (void*)js_buffer_writeDoubleLE, 3);
    buf_set_method(buffer_namespace, "toJSON",        (void*)js_buffer_toJSON, 1);
    buf_set_method(buffer_namespace, "swap16",        (void*)js_buffer_swap16, 1);
    buf_set_method(buffer_namespace, "swap32",        (void*)js_buffer_swap32, 1);
    buf_set_method(buffer_namespace, "swap64",        (void*)js_buffer_swap64, 1);

    // variable-width read/write (static, buf as first arg)
    buf_set_method(buffer_namespace, "readUIntBE",    (void*)js_buffer_readUIntBE, 3);
    buf_set_method(buffer_namespace, "readUIntLE",    (void*)js_buffer_readUIntLE, 3);
    buf_set_method(buffer_namespace, "readIntBE",     (void*)js_buffer_readIntBE, 3);
    buf_set_method(buffer_namespace, "readIntLE",     (void*)js_buffer_readIntLE, 3);
    buf_set_method(buffer_namespace, "writeUIntBE",   (void*)js_buffer_writeUIntBE, 4);
    buf_set_method(buffer_namespace, "writeUIntLE",   (void*)js_buffer_writeUIntLE, 4);
    buf_set_method(buffer_namespace, "writeIntBE",    (void*)js_buffer_writeIntBE, 4);
    buf_set_method(buffer_namespace, "writeIntLE",    (void*)js_buffer_writeIntLE, 4);

    // BigInt64 read/write (static, buf as first arg)
    buf_set_method(buffer_namespace, "readBigInt64BE",   (void*)js_buffer_readBigInt64BE, 2);
    buf_set_method(buffer_namespace, "readBigInt64LE",   (void*)js_buffer_readBigInt64LE, 2);
    buf_set_method(buffer_namespace, "readBigUInt64BE",  (void*)js_buffer_readBigUInt64BE, 2);
    buf_set_method(buffer_namespace, "readBigUInt64LE",  (void*)js_buffer_readBigUInt64LE, 2);
    buf_set_method(buffer_namespace, "writeBigInt64BE",  (void*)js_buffer_writeBigInt64BE, 3);
    buf_set_method(buffer_namespace, "writeBigInt64LE",  (void*)js_buffer_writeBigInt64LE, 3);
    buf_set_method(buffer_namespace, "writeBigUInt64BE", (void*)js_buffer_writeBigUInt64BE, 3);
    buf_set_method(buffer_namespace, "writeBigUInt64LE", (void*)js_buffer_writeBigUInt64LE, 3);

    // set up prototype (lazy, cached)
    js_property_set(buffer_namespace, make_string_item("prototype"), js_get_buffer_prototype());

    // Buffer is the default export
    js_property_set(buffer_namespace, make_string_item("Buffer"), buffer_namespace);
    js_property_set(buffer_namespace, make_string_item("default"), buffer_namespace);

    // Node.js: buffer module also exports atob/btoa
    extern Item js_atob(Item);
    extern Item js_btoa(Item);
    buf_set_method(buffer_namespace, "atob", (void*)js_atob, 1);
    buf_set_method(buffer_namespace, "btoa", (void*)js_btoa, 1);

    // Buffer.constants — MAX_LENGTH and MAX_STRING_LENGTH
    {
        Item constants_obj = js_new_object();
        js_property_set(constants_obj, make_string_item("MAX_LENGTH"),
            (Item){.item = i2it((int64_t)(1LL << 31) - 1)}); // 2GB - 1
        js_property_set(constants_obj, make_string_item("MAX_STRING_LENGTH"),
            (Item){.item = i2it((int64_t)(1LL << 28) - 16)}); // ~256MB
        js_property_set(buffer_namespace, make_string_item("constants"), constants_obj);
    }

    // buffer.kMaxLength, buffer.kStringMaxLength — legacy aliases
    js_property_set(buffer_namespace, make_string_item("kMaxLength"),
        (Item){.item = i2it((int64_t)(1LL << 31) - 1)});
    js_property_set(buffer_namespace, make_string_item("kStringMaxLength"),
        (Item){.item = i2it((int64_t)(1LL << 28) - 16)});

    // buffer.SlowBuffer — legacy, alias for allocUnsafeSlow
    js_property_set(buffer_namespace, make_string_item("SlowBuffer"),
        js_new_function((void*)js_buffer_allocUnsafeSlow, 1));

    return buffer_namespace;
}

extern "C" void js_reset_buffer_module(void) {
    buffer_namespace = (Item){0};
    buffer_prototype = (Item){0};
}
