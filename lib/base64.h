#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Base64 alphabet selector.
 *  - BASE64_STD: standard alphabet (+ /), padded with '=' on encode.
 *  - BASE64_URL: URL/filename-safe alphabet (- _), unpadded on encode.
 */
typedef enum {
    BASE64_STD = 0,
    BASE64_URL = 1
} Base64Variant;

/**
 * Encoded length (excluding the NUL terminator) for `in_len` input bytes.
 * STD is padded to a multiple of 4; URL is unpadded.
 */
size_t base64_encoded_len(size_t in_len, Base64Variant variant);

/**
 * Encode `len` bytes into caller-provided `out`.
 * `out` must hold at least base64_encoded_len(len, variant) + 1 bytes.
 * NUL-terminates the result.
 * @return Number of bytes written (excluding the NUL terminator).
 */
size_t base64_encode(const void* data, size_t len, char* out, Base64Variant variant);

/**
 * Convenience: malloc + encode. Caller must free() the returned buffer.
 * @return Newly allocated NUL-terminated string, or NULL on allocation failure.
 */
char* base64_encode_alloc(const void* data, size_t len, Base64Variant variant);

/**
 * Decode base64 encoded data.
 *
 * @param input The base64 encoded string
 * @param input_len Length of input string (or 0 to auto-detect from null terminator)
 * @param output_len Output parameter to receive decoded data length
 * @return Newly allocated buffer containing decoded data, or NULL on error
 *         Caller must free() the returned buffer
 */
uint8_t* base64_decode(const char* input, size_t input_len, size_t* output_len);

/**
 * Decode base64, accepting either the standard or URL alphabet.
 * The URL variant also tolerates missing '=' padding. Whitespace is ignored.
 * Same contract/ownership as base64_decode().
 */
uint8_t* base64_decode_variant(const char* input, size_t input_len, size_t* output_len,
                               Base64Variant variant);

/**
 * Check if a string is a valid data URI.
 * Data URIs have format: data:[<mediatype>][;base64],<data>
 *
 * @param uri The string to check
 * @return true if string starts with "data:", false otherwise
 */
bool is_data_uri(const char* uri);

/**
 * Parse a data URI and extract the base64-decoded content.
 *
 * @param uri The data URI string
 * @param mime_type Output buffer for MIME type (can be NULL)
 * @param mime_type_size Size of mime_type buffer
 * @param output_len Output parameter to receive decoded data length
 * @return Newly allocated buffer containing decoded data, or NULL on error
 *         Caller must free() the returned buffer
 */
uint8_t* parse_data_uri(const char* uri, char* mime_type, size_t mime_type_size, size_t* output_len);

#ifdef __cplusplus
}
#endif

#endif // BASE64_H
