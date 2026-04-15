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

// ─── Buffer.alloc(size, fill?, encoding?) ───────────────────────────────────
extern "C" Item js_buffer_alloc(Item size_item, Item fill_item) {
    int64_t size = 0;
    TypeId tid = get_type_id(size_item);
    if (tid == LMD_TYPE_INT) size = it2i(size_item);
    else if (tid == LMD_TYPE_FLOAT) size = (int64_t)it2d(size_item);
    if (size < 0) size = 0;
    if (size > 1048576) size = 1048576; // 1MB limit for safety

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
            memcpy(enc_buf, enc->chars, elen);
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

    // array of numbers
    if (js_array_length(data) >= 0) {
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

    return create_buffer(0);
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
extern "C" Item js_buffer_byteLength(Item str_item) {
    if (get_type_id(str_item) == LMD_TYPE_STRING) {
        String* s = it2s(str_item);
        return (Item){.item = i2it((int64_t)s->len)};
    }
    if (js_is_typed_array(str_item)) {
        int blen = 0;
        buffer_data(str_item, &blen);
        return (Item){.item = i2it(blen)};
    }
    return (Item){.item = i2it(0)};
}

// ─── buf.toString(encoding?, start?, end?) ──────────────────────────────────
extern "C" Item js_buffer_toString(Item buf, Item encoding, Item start_item, Item end_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return make_string_item("");

    // range support: start/end default to 0/blen
    int start = 0, end = blen;
    if (get_type_id(start_item) == LMD_TYPE_INT) start = (int)it2i(start_item);
    if (get_type_id(end_item) == LMD_TYPE_INT) end = (int)it2i(end_item);
    if (start < 0) start = 0;
    if (end > blen) end = blen;
    if (start >= end) return make_string_item("");

    uint8_t* slice = data + start;
    int slice_len = end - start;

    char enc_buf[32] = "utf8";
    if (get_type_id(encoding) == LMD_TYPE_STRING) {
        String* enc = it2s(encoding);
        int elen = (int)enc->len;
        if (elen >= (int)sizeof(enc_buf)) elen = (int)sizeof(enc_buf) - 1;
        memcpy(enc_buf, enc->chars, elen);
        enc_buf[elen] = '\0';
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
        int out_len = 4 * ((slice_len + 2) / 3);
        char* b64_str = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
        int j = 0;
        for (int i = 0; i < slice_len; i += 3) {
            uint32_t a = slice[i];
            uint32_t b_val = (i + 1 < slice_len) ? slice[i + 1] : 0;
            uint32_t c = (i + 2 < slice_len) ? slice[i + 2] : 0;
            uint32_t triple = (a << 16) | (b_val << 8) | c;
            b64_str[j++] = b64[(triple >> 18) & 0x3F];
            b64_str[j++] = b64[(triple >> 12) & 0x3F];
            b64_str[j++] = (i + 1 < slice_len) ? b64[(triple >> 6) & 0x3F] : '=';
            b64_str[j++] = (i + 2 < slice_len) ? b64[triple & 0x3F] : '=';
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
extern "C" Item js_buffer_copy(Item src_buf, Item dst_buf, Item target_start_item) {
    int src_len = 0, dst_len = 0;
    uint8_t* src = buffer_data(src_buf, &src_len);
    uint8_t* dst = buffer_data(dst_buf, &dst_len);
    if (!src || !dst) return (Item){.item = i2it(0)};

    int target_start = 0;
    if (get_type_id(target_start_item) == LMD_TYPE_INT)
        target_start = (int)it2i(target_start_item);
    if (target_start < 0) target_start = 0;

    int copy_len = src_len;
    if (target_start + copy_len > dst_len) copy_len = dst_len - target_start;
    if (copy_len <= 0) return (Item){.item = i2it(0)};

    memcpy(dst + target_start, src, copy_len);
    return (Item){.item = i2it(copy_len)};
}

// ─── buf.equals(otherBuffer) ────────────────────────────────────────────────
extern "C" Item js_buffer_equals(Item a, Item b) {
    int alen = 0, blen = 0;
    uint8_t* adata = buffer_data(a, &alen);
    uint8_t* bdata = buffer_data(b, &blen);
    if (alen != blen) return (Item){.item = b2it(false)};
    if (alen == 0) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(memcmp(adata, bdata, alen) == 0)};
}

// ─── buf.compare(otherBuffer) ───────────────────────────────────────────────
extern "C" Item js_buffer_compare(Item a, Item b) {
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

// ─── buf.indexOf(value) ────────────────────────────────────────────────────
extern "C" Item js_buffer_indexOf(Item buf, Item value) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(-1)};

    if (get_type_id(value) == LMD_TYPE_INT) {
        int byte_val = (int)(it2i(value) & 0xFF);
        for (int i = 0; i < blen; i++) {
            if (data[i] == byte_val) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if ((int)s->len > blen) return (Item){.item = i2it(-1)};
        for (int i = 0; i <= blen - (int)s->len; i++) {
            if (memcmp(data + i, s->chars, s->len) == 0)
                return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    return (Item){.item = i2it(-1)};
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
    else if (tid == LMD_TYPE_FLOAT) size = (int64_t)it2d(size_item);
    if (size < 0) size = 0;
    if (size > 1048576) size = 1048576;
    return create_buffer((int)size); // no zero-fill guarantee
}

// ─── buf.subarray(start?, end?) — alias for slice ───────────────────────────
extern "C" Item js_buffer_subarray(Item buf, Item start_item, Item end_item) {
    return js_buffer_slice(buf, start_item, end_item);
}

// ─── buf.includes(value) ───────────────────────────────────────────────────
extern "C" Item js_buffer_includes(Item buf, Item value) {
    Item idx = js_buffer_indexOf(buf, value);
    return (Item){.item = b2it(it2i(idx) >= 0)};
}

// ─── buf.lastIndexOf(value) ────────────────────────────────────────────────
extern "C" Item js_buffer_lastIndexOf(Item buf, Item value) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(-1)};

    if (get_type_id(value) == LMD_TYPE_INT) {
        int byte_val = (int)(it2i(value) & 0xFF);
        for (int i = blen - 1; i >= 0; i--) {
            if (data[i] == byte_val) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    if (get_type_id(value) == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if ((int)s->len > blen) return (Item){.item = i2it(-1)};
        for (int i = blen - (int)s->len; i >= 0; i--) {
            if (memcmp(data + i, s->chars, s->len) == 0)
                return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }

    return (Item){.item = i2it(-1)};
}

// ─── Endian-aware read methods ──────────────────────────────────────────────
extern "C" Item js_buffer_readUInt8(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off >= blen) return make_js_undefined();
    return (Item){.item = i2it((int64_t)data[off])};
}

extern "C" Item js_buffer_readUInt16BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return make_js_undefined();
    uint16_t v = ((uint16_t)data[off] << 8) | data[off + 1];
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt16LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return make_js_undefined();
    uint16_t v = data[off] | ((uint16_t)data[off + 1] << 8);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt32BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
    uint32_t v = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                 ((uint32_t)data[off + 2] << 8) | data[off + 3];
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readUInt32LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
    uint32_t v = data[off] | ((uint32_t)data[off + 1] << 8) |
                 ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt8(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off >= blen) return make_js_undefined();
    return (Item){.item = i2it((int64_t)(int8_t)data[off])};
}

extern "C" Item js_buffer_readInt16BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return make_js_undefined();
    int16_t v = (int16_t)(((uint16_t)data[off] << 8) | data[off + 1]);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt16LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return make_js_undefined();
    int16_t v = (int16_t)(data[off] | ((uint16_t)data[off + 1] << 8));
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt32BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
    int32_t v = (int32_t)(((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                           ((uint32_t)data[off + 2] << 8) | data[off + 3]);
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readInt32LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
    int32_t v = (int32_t)(data[off] | ((uint32_t)data[off + 1] << 8) |
                           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24));
    return (Item){.item = i2it((int64_t)v)};
}

extern "C" Item js_buffer_readFloatBE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return make_js_undefined();
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 8 > blen) return make_js_undefined();
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 8 > blen) return make_js_undefined();
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off >= blen) return (Item){.item = i2it(off + 1)};
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 1)};
}

extern "C" Item js_buffer_writeUInt16BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return (Item){.item = i2it(off + 2)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return (Item){.item = i2it(off + 2)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off >= blen) return (Item){.item = i2it(off + 1)};
    int64_t v = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) v = it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    data[off] = (uint8_t)(v & 0xFF);
    return (Item){.item = i2it(off + 1)};
}

extern "C" Item js_buffer_writeInt16BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return (Item){.item = i2it(off + 2)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 2 > blen) return (Item){.item = i2it(off + 2)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 4 > blen) return (Item){.item = i2it(off + 4)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 8 > blen) return (Item){.item = i2it(off + 8)};
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
    int off = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) off = (int)it2i(offset_item);
    if (!data || off < 0 || off + 8 > blen) return (Item){.item = i2it(off + 8)};
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
    Item arr = js_array_new(blen);
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

// ─── Namespace ───────────────────────────────────────────────────────────────

static Item buffer_namespace = {0};

static void buf_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

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
    buf_set_method(buffer_namespace, "byteLength", (void*)js_buffer_byteLength, 1);

    // instance methods (called as Buffer methods with 'this' as first arg)
    buf_set_method(buffer_namespace, "toString",   (void*)js_buffer_toString, 4);
    buf_set_method(buffer_namespace, "write",      (void*)js_buffer_write, 3);
    buf_set_method(buffer_namespace, "copy",       (void*)js_buffer_copy, 3);
    buf_set_method(buffer_namespace, "equals",     (void*)js_buffer_equals, 2);
    buf_set_method(buffer_namespace, "compare",    (void*)js_buffer_compare, 2);
    buf_set_method(buffer_namespace, "indexOf",    (void*)js_buffer_indexOf, 2);
    buf_set_method(buffer_namespace, "lastIndexOf",(void*)js_buffer_lastIndexOf, 2);
    buf_set_method(buffer_namespace, "includes",   (void*)js_buffer_includes, 2);
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

    // Buffer is the default export
    js_property_set(buffer_namespace, make_string_item("Buffer"), buffer_namespace);
    js_property_set(buffer_namespace, make_string_item("default"), buffer_namespace);

    return buffer_namespace;
}

extern "C" void js_reset_buffer_module(void) {
    buffer_namespace = (Item){0};
}
