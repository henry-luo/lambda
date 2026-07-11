/**
 * js_buffer.cpp — Node.js-style 'buffer' module for LambdaJS
 *
 * Provides Buffer class backed by Uint8Array (TypedArray infrastructure).
 * Buffer.alloc, Buffer.from, Buffer.concat, buf.toString, buf.write,
 * buf.copy, buf.slice, buf.fill, buf.compare, buf.equals, buf.indexOf,
 * buf.byteLength.
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_typed_array.h"
#include "js_error_codes.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/hex.h"
#include "../../lib/base64.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"

#include <cstring>
#include <cstdlib>

static const int64_t JS_BUFFER_MAX_LENGTH = (1LL << 30) - 1;
static const int64_t JS_BUFFER_MAX_STRING_LENGTH = (1LL << 28) - 16;

extern "C" Item js_get_current_this(void);
extern "C" Item js_blob_new(Item parts, Item options);
extern "C" Item js_blob_url_resolve(Item id_item);
extern "C" void js_set_function_name(Item fn_item, Item name_item);
extern Item js_make_number(double d);
void* heap_alloc(int size, TypeId type_id);

static Item make_buffer_content_string_item(const char* str, int len,
                                            bool ascii_known, bool is_ascii) {
    if (!str || len < 0 || len > JS_BUFFER_MAX_STRING_LENGTH) return ItemNull;
    size_t alloc_size = sizeof(String) + (size_t)len + 1;
    // Buffer payload strings are transient content; interning megabyte output
    // hashed and retained every byte in the name pool before this fast path.
    String* s = (String*)heap_alloc((int)alloc_size, LMD_TYPE_STRING);
    if (!s) return ItemNull;
    if (len > 0) memcpy(s->chars, str, (size_t)len);
    s->chars[len] = '\0';
    s->len = (uint32_t)len;
    s->is_ascii = ascii_known ? (is_ascii ? 1 : 0)
                              : (str_is_ascii(str, (size_t)len) ? 1 : 0);
    return (Item){.item = s2it(s)};
}

// Helper: get raw data pointer and length from a typed array Item
static uint8_t* buffer_data(Item buf, int* out_len) {
    if (!js_is_typed_array(buf)) { *out_len = 0; return NULL; }
    uint8_t* data = (uint8_t*)js_typed_array_current_data_ptr(buf);
    if (!data) { *out_len = 0; return NULL; }
    *out_len = js_typed_array_byte_length(buf);
    return data;
}

static int buffer_decode_base64_bytes(const char* str, int str_len, Base64Variant variant,
                                      uint8_t* out_buf, int max_out) {
    if (!str || str_len <= 0 || !out_buf || max_out <= 0) return 0;

    size_t decoded_len = 0;
    uint8_t* decoded = base64_decode_variant(str, (size_t)str_len, &decoded_len, variant);
    if (!decoded) return 0;

    int copy_len = decoded_len > (size_t)max_out ? max_out : (int)decoded_len;
    if (copy_len > 0) memcpy(out_buf, decoded, (size_t)copy_len);
    mem_free(decoded);
    return copy_len;
}

extern "C" Item bigint_from_int64(int64_t val);
extern "C" Item bigint_from_string(const char* str, int len);
extern "C" int64_t bigint_to_int64(Item bi);
extern "C" char* bigint_to_cstring_radix(Item bi, int radix);
extern "C" Item js_bigint_as_int_n(Item bits_item, Item bigint_item);
extern "C" Item js_bigint_as_uint_n(Item bits_item, Item bigint_item);
extern "C" int js_check_exception(void);

static bool buffer_value_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFFULL);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static bool buffer_to_bigint_value(Item value, Item* out_bigint) {
    if (buffer_value_is_bigint(value)) {
        *out_bigint = value;
        return true;
    }

    TypeId value_type = get_type_id(value);
    // buffer's BigInt write APIs require a BigInt value; generic ToBigInt would wrongly accept strings/booleans.
    if (value_type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a BigInt");
    } else {
        js_throw_type_error("Cannot convert non-BigInt value to BigInt");
    }
    return false;
}

static Item buffer_biguint64_item(uint64_t value) {
    if (value <= (uint64_t)INT64_MAX) return bigint_from_int64((int64_t)value);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return bigint_from_string(buf, len);
}

static uint64_t buffer_bigint_to_uint64_bits(Item value) {
    char* value_str = bigint_to_cstring_radix(value, 10);
    if (!value_str) return 0;
    unsigned long long raw_value = strtoull(value_str, NULL, 10);
    mem_free(value_str);
    return (uint64_t)raw_value;
}

static int buffer_valid_hex_byte_length(const char* str, int str_len, int max_out) {
    if (!str || str_len <= 0 || max_out == 0) return 0;
    int limit = str_len / 2;
    if (max_out > 0 && limit > max_out) limit = max_out;
    int count = 0;
    for (int i = 0; i < limit; i++) {
        int hi = hex_decode_byte(str[i * 2]);
        int lo = hex_decode_byte(str[i * 2 + 1]);
        if (hi < 0 || lo < 0) break;
        count++;
    }
    return count;
}

static int buffer_decode_hex_bytes(const char* str, int str_len, uint8_t* out, int max_out) {
    int byte_len = buffer_valid_hex_byte_length(str, str_len, max_out);
    for (int i = 0; i < byte_len; i++) {
        int hi = hex_decode_byte(str[i * 2]);
        int lo = hex_decode_byte(str[i * 2 + 1]);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return byte_len;
}

// Helper: create a Buffer (Uint8Array)
// We cannot set string properties on typed arrays (MAP_KIND_TYPED_ARRAY routes
// property_set to typed_array_set which only handles numeric indices).
// Instead, Buffer identity is checked via js_is_typed_array().
static Item create_buffer(int size) {
    Item buffer = js_typed_array_new(JS_TYPED_UINT8, size);
    if (js_is_typed_array(buffer)) {
        JsTypedArray* ta = js_get_typed_array_ptr(buffer.map);
        if (ta) ta->is_buffer = true;
    }
    return buffer;
}

extern "C" Item js_buffer_from_bytes(const char* data, int len) {
    if (len < 0) len = 0;
    Item buf = create_buffer(len);
    int buf_len = 0;
    uint8_t* dst = buffer_data(buf, &buf_len);
    if (dst && data && len > 0) {
        int copy_len = len < buf_len ? len : buf_len;
        memcpy(dst, data, (size_t)copy_len);
    }
    return buf;
}

static uint32_t buffer_next_utf8_codepoint(const char* str, int len, int* index) {
    if (!str || !index || *index >= len) return 0;
    int i = *index;
    unsigned char c = (unsigned char)str[i];
    if (c < 0x80) {
        *index = i + 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < len) {
        *index = i + 2;
        return ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(str[i + 1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < len) {
        *index = i + 3;
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)(str[i + 1] & 0x3F) << 6) |
               (uint32_t)(str[i + 2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < len) {
        *index = i + 4;
        return ((uint32_t)(c & 0x07) << 18) |
               ((uint32_t)(str[i + 1] & 0x3F) << 12) |
               ((uint32_t)(str[i + 2] & 0x3F) << 6) |
               (uint32_t)(str[i + 3] & 0x3F);
    }
    *index = i + 1;
    return c;
}

static int buffer_utf8_codepoint_count(const char* str, int len) {
    int count = 0;
    int index = 0;
    while (index < len) {
        buffer_next_utf8_codepoint(str, len, &index);
        count++;
    }
    return count;
}

static int buffer_utf8_encoded_len(const char* chars, int byte_len) {
    int out_len = 0;
    for (int i = 0; i < byte_len; ) {
        unsigned char lead = (unsigned char)chars[i];
        int cp_len = 1;
        if (lead >= 0xF0 && i + 4 <= byte_len) cp_len = 4;
        else if (lead >= 0xE0 && i + 3 <= byte_len) cp_len = 3;
        else if (lead >= 0xC0 && i + 2 <= byte_len) cp_len = 2;

        if (cp_len == 3 && lead == 0xED && i + 2 < byte_len) {
            unsigned char second = (unsigned char)chars[i + 1];
            bool high = second >= 0xA0 && second <= 0xAF;
            bool low = second >= 0xB0 && second <= 0xBF;
            if (high) {
                int next = i + 3;
                if (next + 2 < byte_len && (unsigned char)chars[next] == 0xED) {
                    unsigned char next_second = (unsigned char)chars[next + 1];
                    if (next_second >= 0xB0 && next_second <= 0xBF) {
                        out_len += 4;
                        i += 6;
                        continue;
                    }
                }
                out_len += 3;
                i += 3;
                continue;
            }
            if (low) {
                out_len += 3;
                i += 3;
                continue;
            }
        }

        out_len += cp_len;
        i += cp_len;
    }
    return out_len;
}

static uint16_t buffer_decode_wtf8_unit(const char* chars, int pos) {
    unsigned char b0 = (unsigned char)chars[pos];
    unsigned char b1 = (unsigned char)chars[pos + 1];
    unsigned char b2 = (unsigned char)chars[pos + 2];
    return (uint16_t)(((uint16_t)(b0 & 0x0F) << 12) |
                      ((uint16_t)(b1 & 0x3F) << 6) |
                      (uint16_t)(b2 & 0x3F));
}

static void buffer_write_replacement(uint8_t* out, int* pos) {
    out[(*pos)++] = 0xEF;
    out[(*pos)++] = 0xBF;
    out[(*pos)++] = 0xBD;
}

static void buffer_write_utf8_encoded(const char* chars, int byte_len, uint8_t* out) {
    int out_pos = 0;
    for (int i = 0; i < byte_len; ) {
        unsigned char lead = (unsigned char)chars[i];
        int cp_len = 1;
        if (lead >= 0xF0 && i + 4 <= byte_len) cp_len = 4;
        else if (lead >= 0xE0 && i + 3 <= byte_len) cp_len = 3;
        else if (lead >= 0xC0 && i + 2 <= byte_len) cp_len = 2;

        if (cp_len == 3 && lead == 0xED && i + 2 < byte_len) {
            unsigned char second = (unsigned char)chars[i + 1];
            bool high = second >= 0xA0 && second <= 0xAF;
            bool low = second >= 0xB0 && second <= 0xBF;
            if (high) {
                int next = i + 3;
                if (next + 2 < byte_len && (unsigned char)chars[next] == 0xED) {
                    unsigned char next_second = (unsigned char)chars[next + 1];
                    if (next_second >= 0xB0 && next_second <= 0xBF) {
                        uint16_t hi = buffer_decode_wtf8_unit(chars, i);
                        uint16_t lo = buffer_decode_wtf8_unit(chars, next);
                        uint32_t cp = utf16_decode_pair(hi, lo);
                        char encoded[4];
                        size_t n = utf8_encode(cp, encoded);
                        for (size_t j = 0; j < n; j++) out[out_pos++] = (uint8_t)encoded[j];
                        i += 6;
                        continue;
                    }
                }
                buffer_write_replacement(out, &out_pos);
                i += 3;
                continue;
            }
            if (low) {
                buffer_write_replacement(out, &out_pos);
                i += 3;
                continue;
            }
        }

        for (int j = 0; j < cp_len; j++) out[out_pos++] = (uint8_t)chars[i + j];
        i += cp_len;
    }
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
        case LMD_TYPE_MAP: {
            extern Item js_property_get(Item object, Item key);
            Item ctor = js_property_get(value, make_string_item("constructor"));
            Item name = get_type_id(ctor) == LMD_TYPE_FUNC
                ? js_property_get(ctor, make_string_item("name"))
                : ItemNull;
            if (get_type_id(name) == LMD_TYPE_STRING) {
                String* ns = it2s(name);
                if (ns && ns->len > 0) {
                    return snprintf(buf, buf_size, " Received an instance of %.*s",
                        (int)ns->len, ns->chars);
                }
            }
            return snprintf(buf, buf_size, " Received an instance of Object");
        }
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
        if (d != d || d < 0 || d > JS_BUFFER_MAX_LENGTH) {
            return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                "The value of \"size\" is out of range.");
        }
        size = (int64_t)d;
    }
    else {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"size\" argument must be of type number.");
    }
    if (size < 0 || size > JS_BUFFER_MAX_LENGTH) {
        return js_throw_range_error_code("ERR_OUT_OF_RANGE",
            "The value of \"size\" is out of range.");
    }
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

// ─── new Buffer(arg, encodingOrOffset?, length?) — deprecated constructor ───
// forward declaration
extern "C" Item js_buffer_from(Item data, Item encoding, Item length_item);
// new Buffer(size) → Buffer.alloc(size), new Buffer(string, enc) → Buffer.from(string, enc)
// new Buffer(array) → Buffer.from(array), new Buffer(buffer) → Buffer.from(buffer)
extern "C" Item js_buffer_construct(Item arg, Item encoding, Item length_item) {
    TypeId tid = get_type_id(arg);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT) {
        // new Buffer(size) — allocate zero-filled
        Item fill = make_js_undefined();
        return js_buffer_alloc(arg, fill);
    }
    // everything else delegates to Buffer.from
    return js_buffer_from(arg, encoding, length_item);
}

static bool buffer_from_to_index(Item item, int undefined_default, int nan_default, int* out_index, const char* name) {
    if (!out_index) return false;
    if (get_type_id(item) == LMD_TYPE_UNDEFINED || item.item == ITEM_JS_UNDEFINED) {
        *out_index = undefined_default;
        return true;
    }

    Item num = js_to_number(item);
    if (js_check_exception()) return false;

    double value = 0.0;
    TypeId num_type = get_type_id(num);
    if (num_type == LMD_TYPE_INT) {
        value = (double)it2i(num);
    } else if (num_type == LMD_TYPE_FLOAT) {
        value = it2d(num);
    } else if (num_type == LMD_TYPE_INT64) {
        value = (double)it2l(num);
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg), "The \"%s\" argument must be of type number.", name ? name : "offset");
        js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
        return false;
    }

    if (value != value) {
        *out_index = nan_default;
        return true;
    }

    if (value < 0.0 || value > 2147483647.0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "\"%s\" is outside of buffer bounds", name ? name : "offset");
        js_throw_range_error_code("ERR_BUFFER_OUT_OF_BOUNDS", msg);
        return false;
    }

    *out_index = (int)value;
    return true;
}

static int buffer_number_to_int_or_default(Item item, int default_value) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_UNDEFINED || item.item == ITEM_JS_UNDEFINED) return default_value;
    if (type == LMD_TYPE_INT) return (int)it2i(item);
    if (type == LMD_TYPE_FLOAT) {
        double value = it2d(item);
        // JS numeric arguments can be boxed doubles even when integer-valued.
        if (value != value) return default_value;
        return (int)value;
    }
    return default_value;
}

// ─── Buffer.from(data, encodingOrOffset?, length?) ─────────────────────────
// data can be: string (utf-8), array of bytes, ArrayBuffer, another Buffer/TypedArray
extern "C" Item js_buffer_from(Item data, Item encoding, Item length_item) {
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
            int byte_len = buffer_valid_hex_byte_length(s->chars, (int)s->len, -1);
            Item buf = create_buffer(byte_len);
            int buf_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_len);
            if (bdata) {
                buffer_decode_hex_bytes(s->chars, (int)s->len, bdata, buf_len);
            }
            return buf;
        }

        if (strcmp(enc_buf, "base64") == 0) {
            size_t decoded_len = 0;
            uint8_t* decoded = base64_decode_variant(s->chars, s->len, &decoded_len, BASE64_STD);
            Item buf = create_buffer(decoded ? (int)decoded_len : 0);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (decoded && bdata && buf_byte_len > 0) {
                memcpy(bdata, decoded, (size_t)buf_byte_len);
            }
            if (decoded) mem_free(decoded);
            return buf;
        }

        if (strcmp(enc_buf, "ascii") == 0) {
            // ascii: one output byte per code point, masked to 7 bits
            int byte_len = buffer_utf8_codepoint_count(s->chars, (int)s->len);
            Item buf = create_buffer(byte_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                int index = 0;
                for (int i = 0; i < byte_len; i++) {
                    bdata[i] = (uint8_t)(buffer_next_utf8_codepoint(s->chars, (int)s->len, &index) & 0x7F);
                }
            }
            return buf;
        }

        if (strcmp(enc_buf, "latin1") == 0 || strcmp(enc_buf, "binary") == 0) {
            // latin1/binary: one output byte per code point, keeping the low byte
            if (s->is_ascii) {
                Item buf = create_buffer((int)s->len);
                int buf_byte_len = 0;
                uint8_t* bdata = buffer_data(buf, &buf_byte_len);
                if (bdata && s->len > 0) memcpy(bdata, s->chars, (size_t)buf_byte_len);
                return buf;
            }
            int byte_len = buffer_utf8_codepoint_count(s->chars, (int)s->len);
            Item buf = create_buffer(byte_len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata) {
                int index = 0;
                for (int i = 0; i < byte_len; i++) {
                    bdata[i] = (uint8_t)(buffer_next_utf8_codepoint(s->chars, (int)s->len, &index) & 0xFF);
                }
            }
            return buf;
        }

        if (strcmp(enc_buf, "ucs2") == 0 || strcmp(enc_buf, "ucs-2") == 0 ||
            strcmp(enc_buf, "utf16le") == 0 || strcmp(enc_buf, "utf-16le") == 0) {
            // UCS-2 / UTF-16LE: decode UTF-8 to code points, encode each as 2 bytes LE
            if (s->is_ascii) {
                int out_bytes = (int)s->len * 2;
                Item buf = create_buffer(out_bytes);
                int buf_byte_len = 0;
                uint8_t* bdata = buffer_data(buf, &buf_byte_len);
                if (bdata) {
                    int j = 0;
                    for (uint32_t i = 0; i < s->len && j + 1 < buf_byte_len; i++) {
                        bdata[j++] = (uint8_t)s->chars[i];
                        bdata[j++] = 0;
                    }
                }
                return buf;
            }
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
                            uint16_t units[2];
                            if (utf16_encode(cp, units) == 2) {
                                bdata[j++] = (uint8_t)(units[0] & 0xFF);
                                bdata[j++] = (uint8_t)(units[0] >> 8);
                                bdata[j++] = (uint8_t)(units[1] & 0xFF);
                                bdata[j++] = (uint8_t)(units[1] >> 8);
                            }
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

        // default: utf-8 with WHATWG replacement semantics for WTF-8 surrogate halves
        if (s->is_ascii) {
            Item buf = create_buffer((int)s->len);
            int buf_byte_len = 0;
            uint8_t* bdata = buffer_data(buf, &buf_byte_len);
            if (bdata && s->len > 0) memcpy(bdata, s->chars, (size_t)buf_byte_len);
            return buf;
        }
        int byte_len = buffer_utf8_encoded_len(s->chars, (int)s->len);
        Item buf = create_buffer(byte_len);
        int buf_byte_len = 0;
        uint8_t* bdata = buffer_data(buf, &buf_byte_len);
        if (bdata && byte_len > 0) {
            buffer_write_utf8_encoded(s->chars, (int)s->len, bdata);
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

    // ArrayBuffer / SharedArrayBuffer → Buffer view over the same backing store
    if (js_is_arraybuffer(data)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(data);
        if (!ab || ab->detached) {
            return js_throw_type_error("Cannot create Buffer from a detached ArrayBuffer");
        }

        int byte_offset = 0;
        if (!buffer_from_to_index(encoding, 0, 0, &byte_offset, "offset")) return ItemNull;
        if (byte_offset > ab->byte_length) {
            return js_throw_range_error_code("ERR_BUFFER_OUT_OF_BOUNDS",
                "\"offset\" is outside of buffer bounds");
        }

        int byte_length = ab->byte_length - byte_offset;
        if (!buffer_from_to_index(length_item, byte_length, 0, &byte_length, "length")) return ItemNull;
        if (byte_length > ab->byte_length - byte_offset) {
            return js_throw_range_error_code("ERR_BUFFER_OUT_OF_BOUNDS",
                "\"length\" is outside of buffer bounds");
        }

        Item buf = js_typed_array_new_from_buffer(JS_TYPED_UINT8, data, byte_offset, byte_length);
        if (js_is_typed_array(buf)) {
            JsTypedArray* ta = js_get_typed_array_ptr(buf.map);
            if (ta) ta->is_buffer = true;
        }
        return buf;
    }

    // array of numbers
    if (tid == LMD_TYPE_ARRAY && js_array_length(data) >= 0) {
        int64_t arr_len = js_array_length(data);
        if (arr_len > 2147483647) {
            return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                "The value of \"length\" is out of range.");
        }
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

    // DataView -> copy the viewed byte window.
    if (js_is_dataview(data)) {
        JsDataView* dv = js_get_dataview_ptr(data);
        if (!dv || !dv->buffer || dv->buffer->detached) {
            return js_throw_type_error("Cannot create Buffer from a detached DataView");
        }
        int byte_length = dv->length_tracking
            ? dv->buffer->byte_length - dv->byte_offset
            : dv->byte_length;
        if (byte_length < 0 ||
            dv->buffer->byte_length < dv->byte_offset ||
            (!dv->length_tracking &&
             dv->buffer->byte_length < dv->byte_offset + dv->byte_length)) {
            return js_throw_type_error("Cannot create Buffer from an out-of-bounds DataView");
        }
        Item buffer_item = dv->buffer_item
            ? (Item){.item = dv->buffer_item}
            : js_arraybuffer_wrap(dv->buffer);
        return js_buffer_from(buffer_item, (Item){.item = i2it(dv->byte_offset)},
                              (Item){.item = i2it(byte_length)});
    }

    // plain object: {type:"Buffer", data:[...]} reviver pattern, or array-like with length
    if (tid == LMD_TYPE_MAP) {
        Item type_key = make_string_item("type", 4);
        Item data_key = make_string_item("data", 4);
        Item type_val = js_property_get(data, type_key);
        Item data_val = js_property_get(data, data_key);
        if (get_type_id(type_val) == LMD_TYPE_STRING && get_type_id(data_val) == LMD_TYPE_ARRAY) {
            // recursively create from the data array
            return js_buffer_from(data_val, encoding, make_js_undefined());
        }
        // array-like object with length property
        Item len_key = make_string_item("length", 6);
        Item len_val = js_property_get(data, len_key);
        TypeId lt = get_type_id(len_val);
        if (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) {
            int64_t arr_len = (lt == LMD_TYPE_INT) ? it2i(len_val) : (int64_t)it2d(len_val);
            if (arr_len < 0) arr_len = 0;
            if (arr_len > 2147483647) {
                return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                    "The value of \"length\" is out of range.");
            }
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

// ─── Buffer.of(...items) — create a Buffer from a list of byte values ───────
// Note: receives up to 3 positional args; called from JS with spread args
extern "C" Item js_buffer_of(Item a0, Item a1, Item a2) {
    // build array from non-undefined args
    Item arr = js_array_new(0);
    if (get_type_id(a0) != LMD_TYPE_UNDEFINED)
        js_array_push(arr, a0);
    if (get_type_id(a1) != LMD_TYPE_UNDEFINED)
        js_array_push(arr, a1);
    if (get_type_id(a2) != LMD_TYPE_UNDEFINED)
        js_array_push(arr, a2);
    return js_buffer_from(arr, make_js_undefined(), make_js_undefined());
}

// ─── Buffer.concat(list, totalLength?) ──────────────────────────────────────
extern "C" Item js_buffer_concat(Item list, Item total_length_item) {
    // list must be an actual Array, not merely array-like (Buffer/Uint8Array).
    if (get_type_id(list) != LMD_TYPE_ARRAY) {
        char msg[256];
        int pos = snprintf(msg, sizeof(msg),
            "The \"list\" argument must be an instance of Array.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, list);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    int64_t count = js_array_length(list);
    if (count == 0) {
        return create_buffer(0);
    }

    // compute natural total length
    int64_t total = 0;
    for (int64_t i = 0; i < count; i++) {
        Item buf = js_array_get_int(list, i);
        if (!js_is_typed_array(buf)) {
            char msg[256];
            int pos = snprintf(msg, sizeof(msg),
                "The \"list[%lld]\" argument must be an instance of Buffer or Uint8Array.",
                (long long)i);
            format_received_suffix(msg + pos, (int)sizeof(msg) - pos, buf);
            return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
        }
        int blen = 0;
        buffer_data(buf, &blen);
        total += blen;
        if (total > JS_BUFFER_MAX_LENGTH) {
            total = JS_BUFFER_MAX_LENGTH;
            break;
        }
    }

    // validate and honor totalLength when provided (must be number, integer, in range).
    if (get_type_id(total_length_item) != LMD_TYPE_UNDEFINED &&
        total_length_item.item != ITEM_NULL && total_length_item.item != ITEM_JS_UNDEFINED) {
        TypeId tl_type = get_type_id(total_length_item);
        double requested = 0.0;
        if (tl_type == LMD_TYPE_INT) {
            requested = (double)it2i(total_length_item);
        } else if (tl_type == LMD_TYPE_FLOAT) {
            requested = it2d(total_length_item);
        } else if (tl_type == LMD_TYPE_INT64) {
            requested = (double)it2l(total_length_item);
        } else {
            char msg[256];
            int pos = snprintf(msg, sizeof(msg),
                "The \"length\" argument must be of type number.");
            format_received_suffix(msg + pos, (int)sizeof(msg) - pos, total_length_item);
            return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
        }
        int64_t requested_int = (int64_t)requested;
        if (requested != requested || (double)requested_int != requested) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "The value of \"length\" is out of range. It must be an integer. Received %g",
                requested);
            return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        }
        if (requested < 0.0 || requested > (double)JS_BUFFER_MAX_LENGTH) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                "The value of \"length\" is out of range. It must be >= 0 && <= %lld. Received %g",
                (long long)JS_BUFFER_MAX_LENGTH, requested);
            return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        }
        total = requested_int;
    } else if (total_length_item.item == ITEM_NULL) {
        char msg[160];
        int pos = snprintf(msg, sizeof(msg),
            "The \"length\" argument must be of type number.");
        format_received_suffix(msg + pos, (int)sizeof(msg) - pos, total_length_item);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }

    Item result = create_buffer((int)total);
    int dst_len = 0;
    uint8_t* dst = buffer_data(result, &dst_len);
    int64_t offset = 0;
    for (int64_t i = 0; i < count && offset < total; i++) {
        Item buf = js_array_get_int(list, i);
        int blen = 0;
        uint8_t* bdata = buffer_data(buf, &blen);
        if (bdata && blen > 0) {
            int64_t copy = blen;
            if (offset + copy > total) copy = total - offset;
            memcpy(dst + offset, bdata, (size_t)copy);
            offset += copy;
        }
    }
    return result;
}

// ─── Buffer.isBuffer(obj) ───────────────────────────────────────────────────
extern "C" Item js_buffer_isBuffer(Item obj) {
    if (!js_is_typed_array(obj)) return (Item){.item = b2it(false)};
    JsTypedArray* ta = js_get_typed_array_ptr(obj.map);
    if (ta && ta->element_type == JS_TYPED_UINT8 && ta->is_buffer)
        return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

static Item js_buffer_has_instance(Item value) {
    return js_buffer_isBuffer(value);
}

// ─── Buffer.isUtf8(input) ──────────────────────────────────────────────────
static Item js_buffer_isUtf8(Item input) {
    if (!js_is_typed_array(input)) return (Item){.item = b2it(false)};
    const uint8_t* bytes = (const uint8_t*)js_typed_array_current_data_ptr(input);
    if (!bytes) return (Item){.item = b2it(false)};

    // Validate UTF-8 byte sequence
    size_t len = (size_t)js_typed_array_byte_length(input);
    size_t i = 0;
    while (i < len) {
        uint8_t b = bytes[i];
        int seqlen = 0;
        if (b <= 0x7F) { seqlen = 1; }
        else if ((b & 0xE0) == 0xC0) { seqlen = 2; }
        else if ((b & 0xF0) == 0xE0) { seqlen = 3; }
        else if ((b & 0xF8) == 0xF0) { seqlen = 4; }
        else return (Item){.item = b2it(false)};

        if (i + seqlen > len) return (Item){.item = b2it(false)};
        for (int j = 1; j < seqlen; j++) {
            if ((bytes[i + j] & 0xC0) != 0x80) return (Item){.item = b2it(false)};
        }
        // Check overlong encodings and surrogates
        if (seqlen == 2 && b < 0xC2) return (Item){.item = b2it(false)};
        if (seqlen == 3) {
            uint32_t cp = ((b & 0x0F) << 12) | ((bytes[i+1] & 0x3F) << 6) | (bytes[i+2] & 0x3F);
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return (Item){.item = b2it(false)};
        }
        if (seqlen == 4) {
            uint32_t cp = ((b & 0x07) << 18) | ((bytes[i+1] & 0x3F) << 12) | ((bytes[i+2] & 0x3F) << 6) | (bytes[i+3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return (Item){.item = b2it(false)};
        }
        i += seqlen;
    }
    return (Item){.item = b2it(true)};
}

// ─── Buffer.isAscii(input) ─────────────────────────────────────────────────
static Item js_buffer_isAscii(Item input) {
    if (!js_is_typed_array(input)) return (Item){.item = b2it(false)};
    int byte_length = js_typed_array_byte_length(input);
    const uint8_t* bytes = (const uint8_t*)js_typed_array_current_data_ptr(input);
    if (!bytes) return (Item){.item = b2it(true)};
    if (byte_length == 0) return (Item){.item = b2it(true)};

    for (int i = 0; i < byte_length; i++) {
        if (bytes[i] > 0x7F) return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(true)};
}

// ─── Buffer.copyBytesFrom(view, offset, length) ────────────────────────────
static Item js_buffer_copyBytesFrom(Item view, Item offset_item, Item length_item) {
    if (!js_is_typed_array(view)) return ItemNull;
    Map* m = view.map;
    JsTypedArray* ta = js_get_typed_array_ptr(m);
    const uint8_t* src_data = (const uint8_t*)js_typed_array_current_data_ptr(view);
    if (!ta || !src_data) return ItemNull;

    int64_t offset = 0;
    if (get_type_id(offset_item) == LMD_TYPE_INT) offset = it2i(offset_item);
    if (offset < 0) offset = 0;

    // Compute element size from type
    int elem_size = 1;
    switch (ta->element_type) {
        case JS_TYPED_INT8: case JS_TYPED_UINT8: case JS_TYPED_UINT8_CLAMPED: elem_size = 1; break;
        case JS_TYPED_INT16: case JS_TYPED_UINT16: case JS_TYPED_FLOAT16: elem_size = 2; break;
        case JS_TYPED_INT32: case JS_TYPED_UINT32: case JS_TYPED_FLOAT32: elem_size = 4; break;
        case JS_TYPED_FLOAT64: elem_size = 8; break;
        default: break;
    }

    int64_t view_byte_length = js_typed_array_byte_length(view);
    int64_t byte_len = view_byte_length - offset * elem_size;
    if (get_type_id(length_item) == LMD_TYPE_INT) {
        int64_t req = it2i(length_item) * elem_size;
        if (req < byte_len) byte_len = req;
    }
    if (byte_len <= 0) byte_len = 0;

    int64_t src_offset = offset * elem_size;
    extern Item js_buffer_alloc(Item size, Item fill);
    Item result = js_buffer_alloc((Item){.item = i2it(byte_len)}, ItemNull);
    if (js_is_typed_array(result) && byte_len > 0) {
        uint8_t* dst_data = (uint8_t*)js_typed_array_current_data_ptr(result);
        if (dst_data && src_offset + byte_len <= view_byte_length) {
            memcpy(dst_data, src_data + src_offset, byte_len);
        }
    }
    return result;
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
        return (Item){.item = i2it((int64_t)buffer_utf8_encoded_len(s->chars, (int)s->len))};
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

static int append_utf8_replacement(char* out, int j) {
    out[j++] = (char)0xEF;
    out[j++] = (char)0xBF;
    out[j++] = (char)0xBD;
    return j;
}

static Item js_buffer_utf8_to_string(uint8_t* slice, int slice_len) {
    char* out = (char*)mem_alloc((size_t)slice_len * 3 + 1, MEM_CAT_JS_RUNTIME);
    int j = 0;
    for (int i = 0; i < slice_len;) {
        uint8_t b0 = slice[i];
        if (b0 < 0x80) {
            out[j++] = (char)b0;
            i++;
            continue;
        }
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            if (i + 1 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b1 = slice[i + 1];
            if ((b1 & 0xC0) == 0x80) {
                out[j++] = (char)b0;
                out[j++] = (char)b1;
                i += 2;
            } else {
                j = append_utf8_replacement(out, j);
                i++;
            }
            continue;
        }
        if (b0 >= 0xE0 && b0 <= 0xEF) {
            if (i + 1 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b1 = slice[i + 1];
            bool b1_ok = false;
            if (b0 == 0xE0) b1_ok = b1 >= 0xA0 && b1 <= 0xBF;
            else if (b0 == 0xED) b1_ok = b1 >= 0x80 && b1 <= 0x9F;
            else b1_ok = (b1 & 0xC0) == 0x80;
            if (!b1_ok) {
                j = append_utf8_replacement(out, j);
                i++;
                continue;
            }
            if (i + 2 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b2 = slice[i + 2];
            if ((b2 & 0xC0) == 0x80) {
                out[j++] = (char)b0;
                out[j++] = (char)b1;
                out[j++] = (char)b2;
                i += 3;
            } else {
                j = append_utf8_replacement(out, j);
                i++;
            }
            continue;
        }
        if (b0 >= 0xF0 && b0 <= 0xF4) {
            if (i + 1 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b1 = slice[i + 1];
            bool b1_ok = false;
            if (b0 == 0xF0) b1_ok = b1 >= 0x90 && b1 <= 0xBF;
            else if (b0 == 0xF4) b1_ok = b1 >= 0x80 && b1 <= 0x8F;
            else b1_ok = (b1 & 0xC0) == 0x80;
            if (!b1_ok) {
                j = append_utf8_replacement(out, j);
                i++;
                continue;
            }
            if (i + 2 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b2 = slice[i + 2];
            if ((b2 & 0xC0) != 0x80) {
                j = append_utf8_replacement(out, j);
                i++;
                continue;
            }
            if (i + 3 >= slice_len) {
                j = append_utf8_replacement(out, j);
                break;
            }
            uint8_t b3 = slice[i + 3];
            if ((b3 & 0xC0) == 0x80) {
                out[j++] = (char)b0;
                out[j++] = (char)b1;
                out[j++] = (char)b2;
                out[j++] = (char)b3;
                i += 4;
            } else {
                j = append_utf8_replacement(out, j);
                i++;
            }
            continue;
        }
        j = append_utf8_replacement(out, j);
        i++;
    }
    out[j] = '\0';
    Item result = make_buffer_content_string_item(out, j, false, false);
    mem_free(out);
    return result;
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
    // Coerce object encodings to string (e.g. { toString: () => 'ascii' })
    if (enc_tid == LMD_TYPE_MAP) {
        encoding = js_to_string(encoding);
        enc_tid = get_type_id(encoding);
    }
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
        hex_encode(slice, (size_t)slice_len, hex);
        Item result = make_buffer_content_string_item(hex, slice_len * 2, true, true);
        mem_free(hex);
        return result;
    }

    if (strcmp(enc_buf, "base64") == 0 || strcmp(enc_buf, "base64url") == 0) {
        Base64Variant variant = (strcmp(enc_buf, "base64url") == 0) ? BASE64_URL : BASE64_STD;
        size_t out_len = base64_encoded_len((size_t)slice_len, variant);
        char* b64_str = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
        size_t j = base64_encode(slice, (size_t)slice_len, b64_str, variant);
        Item result = make_buffer_content_string_item(b64_str, (int)j, true, true);
        mem_free(b64_str);
        return result;
    }

    if (strcmp(enc_buf, "ascii") == 0) {
        char* ascii = (char*)mem_alloc((size_t)slice_len + 1, MEM_CAT_JS_RUNTIME);
        for (int i = 0; i < slice_len; i++) ascii[i] = (char)(slice[i] & 0x7F);
        ascii[slice_len] = '\0';
        Item result = make_buffer_content_string_item(ascii, slice_len, true, true);
        mem_free(ascii);
        return result;
    }

    if (strcmp(enc_buf, "latin1") == 0 || strcmp(enc_buf, "binary") == 0) {
        char* latin1 = (char*)mem_alloc((size_t)slice_len * 2 + 1, MEM_CAT_JS_RUNTIME);
        int j = 0;
        bool ascii_only = true;
        for (int i = 0; i < slice_len; i++) {
            uint8_t b = slice[i];
            if (b < 0x80) {
                latin1[j++] = (char)b;
            } else {
                ascii_only = false;
                latin1[j++] = (char)(0xC0 | (b >> 6));
                latin1[j++] = (char)(0x80 | (b & 0x3F));
            }
        }
        latin1[j] = '\0';
        Item result = make_buffer_content_string_item(latin1, j, true, ascii_only);
        mem_free(latin1);
        return result;
    }

    if (strcmp(enc_buf, "ucs2") == 0 || strcmp(enc_buf, "ucs-2") == 0 ||
        strcmp(enc_buf, "utf16le") == 0 || strcmp(enc_buf, "utf-16le") == 0) {
        // UCS-2/UTF-16LE: read pairs of bytes as little-endian uint16, encode to UTF-8
        int pairs = slice_len / 2;
        // worst case UTF-8: 3 bytes per code point (BMP), 4 for surrogates
        char* utf8 = (char*)mem_alloc(pairs * 3 + 1, MEM_CAT_JS_RUNTIME);
        int j = 0;
        bool ascii_only = true;
        for (int i = 0; i < pairs; i++) {
            uint16_t cp = (uint16_t)(slice[i * 2] | (slice[i * 2 + 1] << 8));
            if (cp < 0x80) {
                utf8[j++] = (char)cp;
            } else if (cp < 0x800) {
                ascii_only = false;
                utf8[j++] = (char)(0xC0 | (cp >> 6));
                utf8[j++] = (char)(0x80 | (cp & 0x3F));
            } else {
                ascii_only = false;
                utf8[j++] = (char)(0xE0 | (cp >> 12));
                utf8[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[j++] = (char)(0x80 | (cp & 0x3F));
            }
        }
        utf8[j] = '\0';
        Item result = make_buffer_content_string_item(utf8, j, true, ascii_only);
        mem_free(utf8);
        return result;
    }

    // default: utf-8
    return js_buffer_utf8_to_string(slice, slice_len);
}

// ─── buf.write(string, offset?, length?, encoding?) ─────────────────────────
static int encode_string_bytes(const char* str, int str_len, const char* enc,
                               uint8_t* out_buf, int max_out);

extern "C" Item js_buffer_write(Item buf, Item str_item, Item offset_item, Item length_item, Item enc_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    if (!data || blen == 0) return (Item){.item = i2it(0)};
    if (get_type_id(str_item) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};

    String* s = it2s(str_item);
    int offset = 0;
    int length = blen;
    char enc[32] = "utf8";

    if (get_type_id(offset_item) == LMD_TYPE_STRING) {
        enc_item = offset_item;
        offset_item = make_js_undefined();
        length_item = make_js_undefined();
    } else if (get_type_id(length_item) == LMD_TYPE_STRING) {
        enc_item = length_item;
        length_item = make_js_undefined();
    }

    if (get_type_id(offset_item) == LMD_TYPE_INT) offset = (int)it2i(offset_item);
    else if (get_type_id(offset_item) == LMD_TYPE_FLOAT) offset = (int)it2d(offset_item);
    if (offset < 0 || offset > blen) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"offset\" is out of range. It must be >= 0 and <= %d. Received %d", blen, offset);
        return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
    }
    if (offset >= blen) return (Item){.item = i2it(0)};

    length = blen - offset;
    if (get_type_id(length_item) == LMD_TYPE_INT) length = (int)it2i(length_item);
    else if (get_type_id(length_item) == LMD_TYPE_FLOAT) length = (int)it2d(length_item);
    if (length < 0) length = 0;
    if (length > blen - offset) length = blen - offset;

    if (normalize_encoding(enc_item, enc, sizeof(enc)) && !is_known_encoding(enc)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unknown encoding: %s", enc);
        return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
    }

    int write_len = encode_string_bytes(s->chars, (int)s->len, enc, data + offset, length);
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
        if (str) {
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
        return buffer_decode_hex_bytes(str, str_len, out_buf, max_out);
    }

    if (strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0) {
        Base64Variant variant = strcmp(enc, "base64url") == 0 ? BASE64_URL : BASE64_STD;
        return buffer_decode_base64_bytes(str, str_len, variant, out_buf, max_out);
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
        return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
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
    start = buffer_number_to_int_or_default(start_item, start);
    end = buffer_number_to_int_or_default(end_item, end);
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
    else if (get_type_id(value) == LMD_TYPE_FLOAT) fill_byte = (uint8_t)((int64_t)it2d(value) & 0xFF);
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
        if (d != d || d < 0 || d > JS_BUFFER_MAX_LENGTH) {
            return js_throw_range_error_code("ERR_OUT_OF_RANGE",
                "The value of \"size\" is out of range.");
        }
        size = (int64_t)d;
    }
    else {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"size\" argument must be of type number.");
    }
    if (size < 0 || size > JS_BUFFER_MAX_LENGTH) {
        return js_throw_range_error_code("ERR_OUT_OF_RANGE",
            "The value of \"size\" is out of range.");
    }
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
        return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
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

// validate write value is in range [min, max]
static bool validate_write_value(Item value_item, int64_t* out_val, int64_t min_val, int64_t max_val, int byte_size) {
    int64_t v = 0;
    TypeId tid = get_type_id(value_item);
    if (tid == LMD_TYPE_INT) v = it2i(value_item);
    else if (tid == LMD_TYPE_FLOAT) v = (int64_t)it2d(value_item);
    *out_val = v;
    if (v < min_val || v > max_val) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The value of \"value\" is out of range. It must be >= %lld and <= %lld. Received %lld",
            (long long)min_val, (long long)max_val, (long long)v);
        js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
        return false;
    }
    return true;
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
    return js_make_number((double)f);
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
    return js_make_number((double)f);
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
    return js_make_number(d);
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
    return js_make_number(d);
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
    if (!validate_write_value(value_item, &v, 0, 255, 1)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, 0, 65535, 2)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, 0, 65535, 2)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, 0, 4294967295LL, 4)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, 0, 4294967295LL, 4)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, -128, 127, 1)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, -32768, 32767, 2)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, -32768, 32767, 2)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, -2147483648LL, 2147483647LL, 4)) return make_js_undefined();
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
    if (!validate_write_value(value_item, &v, -2147483648LL, 2147483647LL, 4)) return make_js_undefined();
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

static Item buffer_iterator_key(const char* name, int len) {
    return (Item){.item = s2it(heap_create_name(name, len))};
}

extern "C" Item js_buffer_iterator_identity(void) {
    return js_get_current_this();
}

extern "C" Item js_buffer_iterator_next(void) {
    Item iter = js_get_current_this();
    if (get_type_id(iter) != LMD_TYPE_MAP) {
        return js_throw_type_error("Buffer Iterator.prototype.next called on incompatible receiver");
    }

    Item done_key = buffer_iterator_key("__done__", 8);
    Item done_val = js_property_get(iter, done_key);
    if (get_type_id(done_val) == LMD_TYPE_BOOL && it2b(done_val)) {
        Item result = js_new_object();
        js_property_set(result, make_string_item("value"), make_js_undefined());
        js_property_set(result, make_string_item("done"), (Item){.item = b2it(true)});
        return result;
    }

    Item target = js_property_get(iter, buffer_iterator_key("__buf__", 7));
    Item index_item = js_property_get(iter, buffer_iterator_key("__index__", 9));
    Item kind_item = js_property_get(iter, buffer_iterator_key("__kind__", 8));
    if (!js_is_typed_array(target) ||
        get_type_id(index_item) != LMD_TYPE_INT ||
        get_type_id(kind_item) != LMD_TYPE_INT) {
        return js_throw_type_error("Buffer Iterator.prototype.next called on incompatible receiver");
    }
    if (js_typed_array_is_out_of_bounds_item(target)) {
        return js_throw_type_error("Cannot perform Buffer.prototype iterator on an out-of-bounds ArrayBuffer");
    }

    int idx = (int)it2i(index_item);
    int kind = (int)it2i(kind_item); // kind values: 0=keys, 1=values, 2=entries
    int len = js_typed_array_length(target);
    if (idx >= len) {
        js_property_set(iter, done_key, (Item){.item = b2it(true)});
        Item result = js_new_object();
        js_property_set(result, make_string_item("value"), make_js_undefined());
        js_property_set(result, make_string_item("done"), (Item){.item = b2it(true)});
        return result;
    }

    js_property_set(iter, buffer_iterator_key("__index__", 9), (Item){.item = i2it(idx + 1)});
    Item value = ItemNull;
    if (kind == 0) {
        value = (Item){.item = i2it(idx)};
    } else if (kind == 2) {
        Item elem = js_typed_array_get(target, (Item){.item = i2it(idx)});
        if (elem.item == ITEM_NULL) elem = make_js_undefined();
        Item pair = js_array_new(2);
        pair.array->items[0] = (Item){.item = i2it(idx)};
        pair.array->items[1] = elem;
        value = pair;
    } else {
        value = js_typed_array_get(target, (Item){.item = i2it(idx)});
        if (value.item == ITEM_NULL) value = make_js_undefined();
    }

    Item result = js_new_object();
    js_property_set(result, make_string_item("value"), value);
    js_property_set(result, make_string_item("done"), (Item){.item = b2it(false)});
    return result;
}

static Item js_buffer_iterator_new(Item target, int kind) {
    if (!js_is_typed_array(target)) {
        return js_throw_type_error("Buffer iterator method called on incompatible receiver");
    }

    Item iter = js_new_object();
    js_property_set(iter, buffer_iterator_key("__buf__", 7), target);
    js_property_set(iter, buffer_iterator_key("__index__", 9), (Item){.item = i2it(0)});
    js_property_set(iter, buffer_iterator_key("__kind__", 8), (Item){.item = i2it(kind)});
    js_property_set(iter, buffer_iterator_key("__done__", 8), (Item){.item = b2it(false)});
    js_property_set(iter, make_string_item("next"), js_new_function((void*)js_buffer_iterator_next, 0));
    js_property_set(iter, buffer_iterator_key("__sym_1", 7), js_new_function((void*)js_buffer_iterator_identity, 0));
    return iter;
}

// ─── Variable-width read/write ──────────────────────────────────────────────

// buf.readUIntBE(offset, byteLength) — read unsigned int of 1-6 bytes, big-endian
extern "C" Item js_buffer_readUIntBE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = 0; i < nbytes; i++) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it((int64_t)val)};
}

// buf.readUIntLE(offset, byteLength)
extern "C" Item js_buffer_readUIntLE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
    if (nbytes < 1 || nbytes > 6 || offset < 0 || offset + nbytes > blen) return ItemNull;
    uint64_t val = 0;
    for (int i = nbytes - 1; i >= 0; i--) val = (val << 8) | data[offset + i];
    return (Item){.item = i2it((int64_t)val)};
}

// buf.readIntBE(offset, byteLength) — signed
extern "C" Item js_buffer_readIntBE(Item buf, Item offset_item, Item byte_len_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
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
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
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
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
    uint64_t val = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) val = (uint64_t)it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (uint64_t)(int64_t)it2d(value_item);
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
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    int nbytes = buffer_number_to_int_or_default(byte_len_item, 1);
    uint64_t val = 0;
    if (get_type_id(value_item) == LMD_TYPE_INT) val = (uint64_t)it2i(value_item);
    else if (get_type_id(value_item) == LMD_TYPE_FLOAT) val = (uint64_t)(int64_t)it2d(value_item);
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
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) raw = (raw << 8) | data[offset + i];
    // bigint64 Buffer APIs must expose JS BigInt; returning packed int made typeof value "number".
    return bigint_from_int64((int64_t)raw);
}

extern "C" Item js_buffer_readBigInt64LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--) raw = (raw << 8) | data[offset + i];
    // bigint64 Buffer APIs must expose JS BigInt; returning packed int made typeof value "number".
    return bigint_from_int64((int64_t)raw);
}

extern "C" Item js_buffer_readBigUInt64BE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    uint64_t raw = 0;
    for (int i = 0; i < 8; i++) raw = (raw << 8) | data[offset + i];
    return buffer_biguint64_item(raw);
}

extern "C" Item js_buffer_readBigUInt64LE(Item buf, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--) raw = (raw << 8) | data[offset + i];
    return buffer_biguint64_item(raw);
}

extern "C" Item js_buffer_writeBigInt64BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    Item bigint_value;
    if (!buffer_to_bigint_value(value_item, &bigint_value)) return ItemNull;
    Item wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_value);
    if (js_check_exception()) return ItemNull;
    uint64_t val = (uint64_t)bigint_to_int64(wrapped);
    for (int i = 7; i >= 0; i--) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

extern "C" Item js_buffer_writeBigInt64LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    Item bigint_value;
    if (!buffer_to_bigint_value(value_item, &bigint_value)) return ItemNull;
    Item wrapped = js_bigint_as_int_n((Item){.item = i2it(64)}, bigint_value);
    if (js_check_exception()) return ItemNull;
    uint64_t val = (uint64_t)bigint_to_int64(wrapped);
    for (int i = 0; i < 8; i++) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

extern "C" Item js_buffer_writeBigUInt64BE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    Item bigint_value;
    if (!buffer_to_bigint_value(value_item, &bigint_value)) return ItemNull;
    Item wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_value);
    if (js_check_exception()) return ItemNull;
    uint64_t val = buffer_bigint_to_uint64_bits(wrapped);
    for (int i = 7; i >= 0; i--) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

extern "C" Item js_buffer_writeBigUInt64LE(Item buf, Item value_item, Item offset_item) {
    int blen = 0;
    uint8_t* data = buffer_data(buf, &blen);
    int offset = buffer_number_to_int_or_default(offset_item, 0);
    if (offset < 0 || offset + 8 > blen) return ItemNull;
    Item bigint_value;
    if (!buffer_to_bigint_value(value_item, &bigint_value)) return ItemNull;
    Item wrapped = js_bigint_as_uint_n((Item){.item = i2it(64)}, bigint_value);
    if (js_check_exception()) return ItemNull;
    uint64_t val = buffer_bigint_to_uint64_bits(wrapped);
    for (int i = 0; i < 8; i++) {
        data[offset + i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
    return (Item){.item = i2it(offset + 8)};
}

// ─── Static Buffer.compare(buf1, buf2) ─────────────────────────────────────

extern "C" Item js_buffer_compare_static(Item a, Item b) {
    return js_buffer_compare(a, b);
}

// ─── Buffer.allocUnsafeSlow(size) ───────────────────────────────────────────

extern "C" Item js_buffer_allocUnsafeSlow(Item size_item) {
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
    } else {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"size\" argument must be of type number.");
    }
    if (size < 0 || size > 2147483647) {
        return js_throw_range_error_code("ERR_OUT_OF_RANGE",
            "The value of \"size\" is out of range.");
    }
    return js_buffer_allocUnsafe(size_item);
}

// ─── Instance method wrappers (this from js_get_current_this()) ─────────────

#define THIS js_get_current_this()

// each instance wrapper reads this from js_get_current_this() and forwards
extern "C" Item js_buf_inst_toString(Item encoding, Item start_item, Item end_item) {
    return js_buffer_toString(THIS, encoding, start_item, end_item);
}
extern "C" Item js_buf_inst_write(Item str_item, Item offset_item, Item length_item, Item enc_item) {
    return js_buffer_write(THIS, str_item, offset_item, length_item, enc_item);
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
extern "C" Item js_buf_inst_keys() {
    return js_buffer_iterator_new(THIS, 0);
}
extern "C" Item js_buf_inst_values() {
    return js_buffer_iterator_new(THIS, 1);
}
extern "C" Item js_buf_inst_entries() {
    return js_buffer_iterator_new(THIS, 2);
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
    buf_set_method(buffer_prototype, "write",      (void*)js_buf_inst_write, 4);
    buf_set_method(buffer_prototype, "copy",       (void*)js_buf_inst_copy, 4);
    buf_set_method(buffer_prototype, "equals",     (void*)js_buf_inst_equals, 1);
    buf_set_method(buffer_prototype, "compare",    (void*)js_buf_inst_compare, 1);
    buf_set_method(buffer_prototype, "indexOf",    (void*)js_buf_inst_indexOf, 3);
    buf_set_method(buffer_prototype, "lastIndexOf",(void*)js_buf_inst_lastIndexOf, 3);
    buf_set_method(buffer_prototype, "includes",   (void*)js_buf_inst_includes, 3);
    buf_set_method(buffer_prototype, "slice",      (void*)js_buf_inst_slice, 2);
    buf_set_method(buffer_prototype, "subarray",   (void*)js_buf_inst_subarray, 2);
    buf_set_method(buffer_prototype, "fill",       (void*)js_buf_inst_fill, 1);
    buf_set_method(buffer_prototype, "keys",       (void*)js_buf_inst_keys, 0);
    Item values_fn = js_new_function((void*)js_buf_inst_values, 0);
    js_property_set(buffer_prototype, make_string_item("values"), values_fn);
    js_property_set(buffer_prototype, buffer_iterator_key("__sym_1", 7), values_fn);
    buf_set_method(buffer_prototype, "entries",    (void*)js_buf_inst_entries, 0);

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

    // Buffer is both a callable function (deprecated Buffer(arg, enc)) and a namespace
    buffer_namespace = js_new_function((void*)js_buffer_construct, 3);

    // static methods (Buffer.alloc, Buffer.from, etc.)
    buf_set_method(buffer_namespace, "alloc",      (void*)js_buffer_alloc, 2);
    buf_set_method(buffer_namespace, "allocUnsafe", (void*)js_buffer_allocUnsafe, 1);
    buf_set_method(buffer_namespace, "from",       (void*)js_buffer_from, 3);
    buf_set_method(buffer_namespace, "of",         (void*)js_buffer_of, 3);
    buf_set_method(buffer_namespace, "concat",     (void*)js_buffer_concat, 2);
    buf_set_method(buffer_namespace, "isBuffer",   (void*)js_buffer_isBuffer, 1);
    buf_set_method(buffer_namespace, "isEncoding", (void*)js_buffer_isEncoding, 1);
    buf_set_method(buffer_namespace, "isUtf8",     (void*)js_buffer_isUtf8, 1);
    buf_set_method(buffer_namespace, "isAscii",    (void*)js_buffer_isAscii, 1);
    buf_set_method(buffer_namespace, "copyBytesFrom", (void*)js_buffer_copyBytesFrom, 3);
    buf_set_method(buffer_namespace, "byteLength", (void*)js_buffer_byteLength, 2);
    buf_set_method(buffer_namespace, "allocUnsafeSlow", (void*)js_buffer_allocUnsafeSlow, 1);
    buf_set_method(buffer_namespace, "compare",    (void*)js_buffer_compare_static, 2);
    buf_set_method(buffer_namespace, "toString",   (void*)js_buffer_toString, 4);
    buf_set_method(buffer_namespace, "write",      (void*)js_buffer_write, 5);
    buf_set_method(buffer_namespace, "copy",       (void*)js_buffer_copy, 5);
    buf_set_method(buffer_namespace, "equals",     (void*)js_buffer_equals, 2);
    buf_set_method(buffer_namespace, "compare",    (void*)js_buffer_compare, 2);
    buf_set_method(buffer_namespace, "indexOf",    (void*)js_buffer_indexOf, 4);
    buf_set_method(buffer_namespace, "lastIndexOf",(void*)js_buffer_lastIndexOf, 4);
    buf_set_method(buffer_namespace, "includes",   (void*)js_buffer_includes, 4);
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
    {
        Item has_instance_key = make_string_item("__sym_3");
        js_create_data_property(buffer_namespace, has_instance_key,
                                js_new_function((void*)js_buffer_has_instance, 1));
        js_mark_non_enumerable(buffer_namespace, has_instance_key);
    }

    // Buffer is the default export
    js_property_set(buffer_namespace, make_string_item("Buffer"), buffer_namespace);
    js_property_set(buffer_namespace, make_string_item("default"), buffer_namespace);

    // Node.js: buffer module also exports atob/btoa
    extern Item js_atob(Item);
    extern Item js_btoa(Item);
    buf_set_method(buffer_namespace, "atob", (void*)js_atob, 1);
    buf_set_method(buffer_namespace, "btoa", (void*)js_btoa, 1);
    {
        Item blob_ctor = js_new_function((void*)js_blob_new, 2);
        js_set_function_name(blob_ctor, make_string_item("Blob"));
        js_property_set(buffer_namespace, make_string_item("Blob"), blob_ctor);
    }
    // buffer.resolveObjectURL reads URL.createObjectURL's process-local Blob
    // registry; without the shared resolver the Buffer and URL surfaces diverge.
    buf_set_method(buffer_namespace, "resolveObjectURL", (void*)js_blob_url_resolve, 1);

    // Buffer.constants — MAX_LENGTH and MAX_STRING_LENGTH
    {
        Item constants_obj = js_new_object();
        js_property_set(constants_obj, make_string_item("MAX_LENGTH"),
            (Item){.item = i2it(JS_BUFFER_MAX_LENGTH)});
        js_property_set(constants_obj, make_string_item("MAX_STRING_LENGTH"),
            (Item){.item = i2it(JS_BUFFER_MAX_STRING_LENGTH)});
        js_property_set(buffer_namespace, make_string_item("constants"), constants_obj);
    }

    // buffer.kMaxLength, buffer.kStringMaxLength — legacy aliases
    js_property_set(buffer_namespace, make_string_item("kMaxLength"),
        (Item){.item = i2it(JS_BUFFER_MAX_LENGTH)});
    js_property_set(buffer_namespace, make_string_item("kStringMaxLength"),
        (Item){.item = i2it(JS_BUFFER_MAX_STRING_LENGTH)});

    // buffer.SlowBuffer — legacy, alias for allocUnsafeSlow
    js_property_set(buffer_namespace, make_string_item("SlowBuffer"),
        js_new_function((void*)js_buffer_allocUnsafeSlow, 1));

    // Buffer.poolSize — default 8192
    js_property_set(buffer_namespace, make_string_item("poolSize"),
        (Item){.item = i2it(8192)});

    return buffer_namespace;
}

extern "C" void js_reset_buffer_module(void) {
    buffer_namespace = (Item){0};
    buffer_prototype = (Item){0};
}
