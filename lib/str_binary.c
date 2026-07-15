/**
 * str_binary.c — Lambda binary-literal payload decoding.
 *
 * Kept outside str.c so the foundational string library does not acquire a
 * link-time dependency on the base64 allocator in small standalone targets.
 */

#include "str.h"
#include "strbuf.h"
#include "base64.h"
#include "hex.h"
#include "mem.h"

#include <string.h>

static bool binary_is_base64_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/';
}

static int binary_decode_error(StrBuf* out, int* err_off, int off) {
    strbuf_reset(out);
    if (err_off) *err_off = off;
    return -1;
}

static int binary_decode_hex(const char* content, int len, int start,
                             StrBuf* out, int* err_off) {
    int high = -1;
    int high_off = start;
    for (int i = start; i < len; i++) {
        char c = content[i];
        if (str_char_is_ascii_space(c)) continue;
        int nibble = hex_decode_byte(c);
        if (nibble < 0) return binary_decode_error(out, err_off, i);
        if (high < 0) {
            high = nibble;
            high_off = i;
        } else {
            strbuf_append_char(out, (char)((high << 4) | nibble));
            high = -1;
        }
    }
    // A byte requires two nibbles; report the unmatched nibble itself.
    if (high >= 0) return binary_decode_error(out, err_off, high_off);
    return (int)out->length;
}

static int binary_decode_base64(const char* content, int len, int start,
                                StrBuf* out, int* err_off) {
    StrBuf* compact = strbuf_new_cap((size_t)(len - start) + 1);
    if (!compact) return binary_decode_error(out, err_off, start);

    int first_padding_off = -1;
    int padding = 0;
    int data_chars = 0;
    for (int i = start; i < len; i++) {
        char c = content[i];
        if (str_char_is_ascii_space(c)) continue;
        if (c == '=') {
            if (first_padding_off < 0) first_padding_off = i;
            padding++;
        } else if (binary_is_base64_char(c)) {
            // Padding terminates the data; accepting data after it would
            // silently decode a different byte sequence.
            if (padding > 0) {
                strbuf_free(compact);
                return binary_decode_error(out, err_off, i);
            }
            data_chars++;
        } else {
            strbuf_free(compact);
            return binary_decode_error(out, err_off, i);
        }
        strbuf_append_char(compact, c);
    }

    int rem = data_chars % 4;
    bool bad_padding = padding > 2 ||
        (padding > 0 && compact->length % 4 != 0) ||
        (padding == 1 && rem != 3) || (padding == 2 && rem != 2);
    if (bad_padding || rem == 1) {
        int off = first_padding_off >= 0 ? first_padding_off :
            (len > start ? len - 1 : start);
        strbuf_free(compact);
        return binary_decode_error(out, err_off, off);
    }
    if (data_chars == 0) {
        strbuf_free(compact);
        return padding == 0 ? 0 : binary_decode_error(out, err_off, first_padding_off);
    }

    size_t decoded_len = 0;
    uint8_t* decoded = base64_decode_variant(
        compact->str, compact->length, &decoded_len, BASE64_STD);
    strbuf_free(compact);
    if (!decoded) return binary_decode_error(out, err_off, start);
    strbuf_append_str_n(out, (const char*)decoded, decoded_len);
    mem_free(decoded);
    return (int)out->length;
}

int str_binary_payload_decode(const char* content, int len, StrBuf* out, int* err_off) {
    if (err_off) *err_off = 0;
    if (!content || len < 0 || !out) return -1;
    strbuf_reset(out);

    int start = 0;
    while (start < len && str_char_is_ascii_space(content[start])) start++;
    if (start + 1 < len && content[start] == '\\' && content[start + 1] == 'x') {
        return binary_decode_hex(content, len, start + 2, out, err_off);
    }
    if (start + 2 < len && content[start] == '\\' &&
        content[start + 1] == '6' && content[start + 2] == '4') {
        return binary_decode_base64(content, len, start + 3, out, err_off);
    }
    return binary_decode_hex(content, len, start, out, err_off);
}
