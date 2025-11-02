#include "pdf_decompress.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <stdio.h>

/**
 * Decompress FlateDecode (zlib/deflate) compressed data
 */
char* flate_decode(const char* compressed_data, size_t compressed_len, size_t* out_len) {
    if (!compressed_data || compressed_len == 0 || !out_len) {
        return NULL;
    }

    // Initial buffer size estimate (typically 2-10x compression ratio)
    size_t buffer_size = compressed_len * 4;
    char* output = (char*)malloc(buffer_size);
    if (!output) {
        return NULL;
    }

    // Initialize zlib stream
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef*)compressed_data;
    stream.avail_in = compressed_len;
    stream.next_out = (Bytef*)output;
    stream.avail_out = buffer_size;

    // Try different zlib initialization modes
    // Try with zlib header first (most common in PDF after ASCII85)
    int ret = inflateInit(&stream);
    if (ret != Z_OK) {
        // Try raw deflate (no zlib wrapper)
        ret = inflateInit2(&stream, -15);
        if (ret != Z_OK) {
            // Try auto-detect
            ret = inflateInit2(&stream, 15 + 32);
            if (ret != Z_OK) {
                free(output);
                return NULL;
            }
        }
    }

    // Decompress
    ret = inflate(&stream, Z_FINISH);

    // Handle need for larger buffer
    size_t last_total_out = stream.total_out;
    int stall_count = 0;
    while (ret == Z_BUF_ERROR || (ret == Z_OK && stream.avail_out == 0)) {
        size_t old_size = buffer_size;
        buffer_size *= 2;
        char* new_output = (char*)realloc(output, buffer_size);
        if (!new_output) {
            inflateEnd(&stream);
            free(output);
            return NULL;
        }
        output = new_output;
        stream.next_out = (Bytef*)(output + old_size);
        stream.avail_out = buffer_size - old_size;
        ret = inflate(&stream, Z_FINISH);

        // Safety check: if no progress, break out
        if (stream.total_out == last_total_out) {
            stall_count++;
            if (stall_count > 3) {
                break;
            }
        } else {
            stall_count = 0;
            last_total_out = stream.total_out;
        }
    }

    if (ret != Z_STREAM_END) {
        // If we have output and input is consumed, accept partial success
        if (stream.total_out > 0 && stream.avail_in == 0) {
            *out_len = stream.total_out;
            inflateEnd(&stream);

            // Shrink buffer to actual size
            char* final_output = (char*)realloc(output, *out_len + 1);
            if (final_output) {
                final_output[*out_len] = '\0';
                return final_output;
            }
            output[*out_len] = '\0';
            return output;
        }

        inflateEnd(&stream);
        free(output);
        return NULL;
    }

    *out_len = stream.total_out;
    inflateEnd(&stream);

    // Shrink buffer to actual size
    char* final_output = (char*)realloc(output, *out_len + 1);
    if (final_output) {
        final_output[*out_len] = '\0'; // Null terminate for convenience
        return final_output;
    }

    output[*out_len] = '\0';
    return output;
}

/**
 * Decode ASCII85 (base-85) encoded data
 */
char* ascii85_decode(const char* encoded_data, size_t encoded_len, size_t* out_len) {
    if (!encoded_data || encoded_len == 0 || !out_len) {
        return NULL;
    }

    // Allocate output buffer (worst case: 4 bytes per 5 input chars)
    size_t buffer_size = (encoded_len * 4) / 5 + 4;
    char* output = (char*)malloc(buffer_size);
    if (!output) {
        return NULL;
    }

    const char* in = encoded_data;
    const char* in_end = encoded_data + encoded_len;
    char* out = output;
    uint32_t value = 0;
    int count = 0;

    // Skip whitespace and look for start marker
    while (in < in_end && (*in == ' ' || *in == '\t' || *in == '\n' || *in == '\r')) {
        in++;
    }

    // Handle <~ prefix if present
    if (in < in_end - 1 && in[0] == '<' && in[1] == '~') {
        in += 2;
    }

    while (in < in_end) {
        char c = *in++;

        // Skip whitespace
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }

        // Check for end marker ~>
        if (c == '~' && in < in_end && *in == '>') {
            break;
        }

        // Handle 'z' (represents 4 zero bytes)
        if (c == 'z') {
            if (count != 0) {
                free(output);
                return NULL; // 'z' can only appear at group start
            }
            *out++ = 0;
            *out++ = 0;
            *out++ = 0;
            *out++ = 0;
            continue;
        }

        // Decode base-85 character
        if (c < '!' || c > 'u') {
            free(output);
            return NULL; // Invalid character
        }

        value = value * 85 + (c - '!');
        count++;

        if (count == 5) {
            // Output 4 bytes
            *out++ = (value >> 24) & 0xFF;
            *out++ = (value >> 16) & 0xFF;
            *out++ = (value >> 8) & 0xFF;
            *out++ = value & 0xFF;
            value = 0;
            count = 0;
        }
    }

    // Handle partial group at end
    if (count > 0) {
        // Pad with 'u' characters
        for (int i = count; i < 5; i++) {
            value = value * 85 + 84; // 'u' = 84
        }
        // Output count-1 bytes
        for (int i = 0; i < count - 1; i++) {
            *out++ = (value >> (24 - i * 8)) & 0xFF;
        }
    }

    *out_len = out - output;

    // Shrink buffer to actual size
    char* final_output = (char*)realloc(output, *out_len + 1);
    if (final_output) {
        final_output[*out_len] = '\0';
        return final_output;
    }

    output[*out_len] = '\0';
    return output;
}

/**
 * Decompress PDF stream with multiple filters
 */
char* pdf_decompress_stream(const char* data, size_t data_len,
                            const char** filters, int filter_count,
                            size_t* out_len) {
    if (!data || data_len == 0 || !filters || filter_count == 0 || !out_len) {
        return NULL;
    }

    char* current_data = (char*)data;
    size_t current_len = data_len;
    bool need_free = false;

    // Apply filters in order
    for (int i = 0; i < filter_count; i++) {
        const char* filter = filters[i];
        char* decoded_data = NULL;
        size_t decoded_len = 0;

        if (strcmp(filter, "FlateDecode") == 0) {
            decoded_data = flate_decode(current_data, current_len, &decoded_len);
        } else if (strcmp(filter, "ASCII85Decode") == 0 || strcmp(filter, "A85") == 0) {
            decoded_data = ascii85_decode(current_data, current_len, &decoded_len);
        } else {
            // Unsupported filter
            if (need_free) {
                free(current_data);
            }
            return NULL;
        }

        if (!decoded_data) {
            // Failed to decode
            if (need_free) {
                free(current_data);
            }
            return NULL;
        }

        // Free previous intermediate result
        if (need_free) {
            free(current_data);
        }

        current_data = decoded_data;
        current_len = decoded_len;
        need_free = true;
    }

    *out_len = current_len;
    return current_data;
}
