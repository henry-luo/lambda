#include "base64.h"
#include "log.h"
#include "str.h"
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

uint8_t* base64_decode(const char* input, size_t input_len, size_t* output_len) {
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

    // first pass: count valid base64 characters and padding
    size_t valid_chars = 0;
    size_t padding = 0;
    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        int8_t val = base64_decode_table[c];
        if (val >= 0) {
            valid_chars++;
        } else if (val == -2) { // '='
            padding++;
            valid_chars++;
        } else if (!isspace(c)) {
            // invalid character
            log_error("base64_decode: invalid character '%c' (0x%02x) at position %zu",
                      isprint(c) ? c : '?', c, i);
            *output_len = 0;
            return NULL;
        }
        // whitespace is silently ignored
    }

    // base64 should be in groups of 4
    if (valid_chars % 4 != 0) {
        log_error("base64_decode: invalid length %zu (not multiple of 4)", valid_chars);
        *output_len = 0;
        return NULL;
    }

    // calculate output size: 3 bytes per 4 base64 chars, minus padding
    size_t decoded_len = (valid_chars / 4) * 3;
    if (padding > 0) {
        decoded_len -= padding;
    }

    // allocate output buffer
    uint8_t* output = (uint8_t*)malloc(decoded_len + 1); // +1 for potential null terminator
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
        int8_t val = base64_decode_table[c];

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

    output[decoded_len] = '\0'; // null-terminate for safety
    *output_len = decoded_len;

    log_debug("base64_decode: decoded %zu chars to %zu bytes", valid_chars, decoded_len);
    return output;
}

bool is_data_uri(const char* uri) {
    if (!uri) return false;
    return strncmp(uri, "data:", 5) == 0;
}

uint8_t* parse_data_uri(const char* uri, char* mime_type, size_t mime_type_size, size_t* output_len) {
    if (!uri || !output_len) {
        return NULL;
    }

    // check for data: prefix
    if (strncmp(uri, "data:", 5) != 0) {
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
    if (!comma) {
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

    if (is_base64) {
        // decode base64 data
        log_debug("parse_data_uri: decoding base64 data");
        return base64_decode(data, 0, output_len);
    } else {
        // raw data (possibly URL-encoded) - just return as-is for now
        // note: proper implementation should URL-decode
        size_t data_len = strlen(data);
        uint8_t* output = (uint8_t*)malloc(data_len + 1);
        if (!output) {
            *output_len = 0;
            return NULL;
        }
        memcpy(output, data, data_len);
        output[data_len] = '\0';
        *output_len = data_len;
        return output;
    }
}
