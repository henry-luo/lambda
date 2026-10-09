#include "base64.h"
#include "memtrack.h"
#include "log.h"
#include "str.h"
#include "url.h"
#include "math_checked.hpp"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// base64 decoding table (maps ASCII to 6-bit values, -1 for invalid, -2 for padding '=')
static const int8_t base64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 0-15
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 16-31
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63, // 32-47 (+,/)
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1, // 48-63 (0-9,=)
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, // 64-79 (A-O)
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1, // 80-95 (P-Z)
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, // 96-111 (a-o)
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1, // 112-127 (p-z)
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 128-143
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 144-159
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 160-175
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 176-191
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 192-207
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 208-223
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 224-239
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 240-255
};

// URL-safe decoding table: identical to the standard table but maps '-' (45) -> 62
// and '_' (95) -> 63 instead of '+' / '/'. For leniency we also keep '+' and '/'
// valid so a mixed input still round-trips.
static const int8_t base64url_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 0-15
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 16-31
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,62,-1,63, // 32-47 (+ and - both ->62, / ->63)
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1, // 48-63 (0-9,=)
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, // 64-79 (A-O)
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63, // 80-95 (P-Z, _ ->63)
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, // 96-111 (a-o)
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1, // 112-127 (p-z)
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 128-143
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 144-159
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 160-175
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 176-191
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 192-207
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 208-223
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 224-239
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 240-255
};

// standard and URL-safe encode alphabets
static const char base64_encode_table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char base64url_encode_table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t base64_encoded_len(size_t in_len, Base64Variant variant) {
    size_t groups = in_len / 3;
    if (in_len % 3 != 0 && !math_checked_add(groups, 1, &groups)) return 0;
    if (variant == BASE64_URL) {
        size_t full_len = 0;
        if (!math_checked_mul(in_len / 3, 4, &full_len)) return 0;
        size_t tail_len = in_len % 3 == 0 ? 0 : in_len % 3 + 1;
        size_t encoded_len = 0;
        return math_checked_add(full_len, tail_len, &encoded_len) ? encoded_len : 0;
    }
    // padded to a multiple of 4
    size_t encoded_len = 0;
    return math_checked_mul(groups, 4, &encoded_len) ? encoded_len : 0;
}

size_t base64_encode(const void* data, size_t len, char* out, Base64Variant variant) {
    if (!out) return 0;
    const unsigned char* p = (const unsigned char*)data;
    const char* table = (variant == BASE64_URL) ? base64url_encode_table : base64_encode_table;
    bool pad = (variant != BASE64_URL);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        size_t group = len - i; // input bytes available in this group (>= 1)
        uint32_t a = p[i];
        uint32_t b = (group > 1) ? p[i + 1] : 0;
        uint32_t c = (group > 2) ? p[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        if (group > 1)  out[j++] = table[(triple >> 6) & 0x3F];
        else if (pad)   out[j++] = '=';
        if (group > 2)  out[j++] = table[triple & 0x3F];
        else if (pad)   out[j++] = '=';
    }
    out[j] = '\0';
    return j;
}

char* base64_encode_alloc(const void* data, size_t len, Base64Variant variant) {
    size_t out_len = base64_encoded_len(len, variant);
    size_t allocation_size = 0;
    if ((len > 0 && out_len == 0) ||
        !math_checked_add(out_len, 1, &allocation_size)) {
        log_error("base64_encode_alloc: encoded length overflow for %zu bytes", len);
        return NULL;
    }
    char* out = (char*)mem_alloc(allocation_size, MEM_CAT_TEMP);
    if (!out) {
        log_error("base64_encode_alloc: malloc failed for %zu bytes", allocation_size);
        return NULL;
    }
    base64_encode(data, len, out, variant);
    return out;
}

uint8_t* base64_decode_variant(const char* input, size_t input_len, size_t* output_len,
                               Base64Variant variant) {
    if (!input || !output_len) {
        return NULL;
    }

    // auto-detect length if not provided
    if (input_len == 0) {
        input_len = strlen(input);
    }

    if (input_len == 0) {
        *output_len = 0;
        return NULL;
    }

    const int8_t* table = (variant == BASE64_URL) ? base64url_decode_table : base64_decode_table;

    // Validate padding while sizing so the decode pass cannot stop before the
    // number of bytes promised to the caller has actually been initialized.
    size_t data_chars = 0;
    size_t padding_chars = 0;
    bool saw_padding = false;
    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        int8_t val = table[c];
        if (val >= 0) {
            if (saw_padding) {
                log_error("base64_decode: data after padding at position %zu", i);
                *output_len = 0;
                return NULL;
            }
            data_chars++;
        } else if (val == -2) {
            saw_padding = true;
            padding_chars++;
            if (padding_chars > 2) {
                log_error("base64_decode: too much padding at position %zu", i);
                *output_len = 0;
                return NULL;
            }
        } else if (!isspace(c)) {
            // invalid character
            log_error("base64_decode: invalid character '%c' (0x%02x) at position %zu",
                      isprint(c) ? c : '?', c, i);
            *output_len = 0;
            return NULL;
        }
        // whitespace is silently ignored
    }

    // a lone trailing data char (rem == 1) cannot encode a full byte
    size_t rem = data_chars % 4;
    if (rem == 1) {
        log_error("base64_decode: invalid data length %zu (mod 4 == 1)", data_chars);
        *output_len = 0;
        return NULL;
    }
    if (padding_chars > 0 &&
        ((data_chars + padding_chars) % 4 != 0 ||
         (padding_chars == 1 && rem != 3) ||
         (padding_chars == 2 && rem != 2))) {
        log_error("base64_decode: invalid padding for %zu data characters", data_chars);
        *output_len = 0;
        return NULL;
    }

    // calculate output size from the non-padding data characters
    size_t decoded_len = (data_chars / 4) * 3;
    if (rem == 2) decoded_len += 1;
    else if (rem == 3) decoded_len += 2;

    // allocate output buffer
    size_t allocation_size = 0;
    if (!math_checked_add(decoded_len, 1, &allocation_size)) {
        *output_len = 0;
        return NULL;
    }
    uint8_t* output = (uint8_t*)mem_alloc(allocation_size, MEM_CAT_TEMP); // +1 for null terminator
    if (!output) {
        log_error("base64_decode: malloc failed for %zu bytes", decoded_len);
        *output_len = 0;
        return NULL;
    }

    // second pass: decode
    size_t out_idx = 0;
    uint32_t accum = 0;
    int bits = 0;

    for (size_t i = 0; i < input_len && out_idx < decoded_len; i++) {
        unsigned char c = (unsigned char)input[i];
        int8_t val = table[c];

        if (val >= 0) {
            accum = (accum << 6) | (uint32_t)val;
            bits += 6;

            if (bits >= 8) {
                bits -= 8;
                output[out_idx++] = (uint8_t)(accum >> bits);
                accum &= ((1 << bits) - 1);
            }
        } else if (val == -2) {
            // padding - stop decoding
            break;
        }
        // whitespace: silently skip
    }

    if (out_idx != decoded_len) {
        log_error("base64_decode: decoded length mismatch (%zu != %zu)", out_idx, decoded_len);
        mem_free(output);
        *output_len = 0;
        return NULL;
    }
    output[out_idx] = '\0'; // null-terminate for safety
    *output_len = out_idx;

    log_debug("base64_decode: decoded %zu data chars to %zu bytes", data_chars, decoded_len);
    return output;
}

uint8_t* base64_decode(const char* input, size_t input_len, size_t* output_len) {
    return base64_decode_variant(input, input_len, output_len, BASE64_STD);
}

bool is_data_uri(const char* uri) {
    if (!uri) return false;
    return strlen(uri) >= 5 && str_ieq(uri, 5, "data:", 5);
}

uint8_t* parse_data_uri(const char* uri, char* mime_type, size_t mime_type_size, size_t* output_len) {
    if (!uri || !output_len) {
        return NULL;
    }

    // check for data: prefix
    if (!is_data_uri(uri)) {
        log_error("parse_data_uri: not a data URI");
        *output_len = 0;
        return NULL;
    }

    const char* ptr = uri + 5; // skip "data:"

    // parse optional mime type and parameters
    // format: data:[<mediatype>][;base64],<data>
    // mediatype can include charset, e.g., "text/plain;charset=utf-8"

    // find the comma separator
    const char* comma = strchr(ptr, ',');
    const char* fragment = strchr(ptr, '#');
    if (!comma || (fragment && fragment < comma)) {
        log_error("parse_data_uri: missing comma separator");
        *output_len = 0;
        return NULL;
    }

    // check for ";base64" before comma
    bool is_base64 = false;
    const char* base64_marker = ";base64";
    size_t marker_len = strlen(base64_marker);

    // look for ";base64" in the metadata portion
    const char* meta_end = comma;
    if (meta_end - ptr >= (ptrdiff_t)marker_len) {
        const char* base64_pos = meta_end - marker_len;
        if (str_ieq(base64_pos, marker_len, base64_marker, marker_len)) {
            is_base64 = true;
            meta_end = base64_pos;
        }
    }

    // extract mime type if requested
    if (mime_type && mime_type_size > 0) {
        size_t mime_len = meta_end - ptr;
        if (mime_len > 0) {
            if (mime_len >= mime_type_size) {
                mime_len = mime_type_size - 1;
            }
            strncpy(mime_type, ptr, mime_len);
            mime_type[mime_len] = '\0';
        } else {
            // default mime type
            strncpy(mime_type, "text/plain", mime_type_size - 1);
            mime_type[mime_type_size - 1] = '\0';
        }
    }

    // get the data portion
    const char* data = comma + 1;

    size_t data_len = fragment ? (size_t)(fragment - data) : strlen(data);
    uint8_t* output = (uint8_t*)mem_alloc(data_len + 1, MEM_CAT_TEMP);
    if (!output) {
        *output_len = 0;
        return NULL;
    }
    // data URL bodies percent-decode before base64; '+' is payload, and fragments are excluded.
    size_t decoded_len = url_decode_lenient_write(data, data_len, false, (char*)output);
    output[decoded_len] = '\0';
    if (is_base64 && decoded_len) {
        uint8_t* decoded = base64_decode((const char*)output, decoded_len, output_len);
        mem_free(output);
        return decoded;
    }
    *output_len = decoded_len;
    return output;
}
