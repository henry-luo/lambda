#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
