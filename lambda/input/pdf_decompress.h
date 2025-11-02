#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PDF Stream Decompression Utilities
 *
 * Supports:
 * - FlateDecode (zlib/deflate compression)
 * - ASCII85Decode (base-85 encoding)
 * - Combined filters (e.g., ASCII85Decode + FlateDecode)
 */

// Decompress FlateDecode (zlib) compressed data
// Returns: decompressed data (caller must free), or NULL on error
// out_len: receives the decompressed length
char* flate_decode(const char* compressed_data, size_t compressed_len, size_t* out_len);

// Decode ASCII85 (base-85) encoded data
// Returns: decoded data (caller must free), or NULL on error
// out_len: receives the decoded length
char* ascii85_decode(const char* encoded_data, size_t encoded_len, size_t* out_len);

// Decompress PDF stream with multiple filters
// Supports: FlateDecode, ASCII85Decode, and combinations
// Returns: decompressed data (caller must free), or NULL on error
// out_len: receives the decompressed length
char* pdf_decompress_stream(const char* data, size_t data_len,
                            const char** filters, int filter_count,
                            size_t* out_len);

#ifdef __cplusplus
}
#endif
