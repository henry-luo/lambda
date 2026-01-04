#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PDF Stream Decompression Utilities
 *
 * Supports all standard PDF filters (matching pdf.js):
 * - FlateDecode (zlib/deflate compression)
 * - LZWDecode (LZW compression)
 * - ASCII85Decode (base-85 encoding)
 * - ASCIIHexDecode (hexadecimal encoding)
 * - RunLengthDecode (run-length encoding)
 * - Combined filters (e.g., ASCII85Decode + FlateDecode)
 * - Predictor support for FlateDecode/LZWDecode
 */

/**
 * Predictor parameters for FlateDecode/LZWDecode
 * Based on pdf.js src/core/predictor_stream.js
 */
typedef struct {
    int predictor;      // 1=None, 2=TIFF, 10-15=PNG
    int colors;         // Number of color components (default 1)
    int bits;           // Bits per component (default 8)
    int columns;        // Pixels per row (default 1)
    int early_change;   // For LZW: early code size change (default 1)
} PDFDecodeParams;

// Initialize decode params to defaults
void pdf_decode_params_init(PDFDecodeParams* params);

// Decompress FlateDecode (zlib) compressed data
// Returns: decompressed data (caller must free), or NULL on error
// out_len: receives the decompressed length
char* flate_decode(const char* compressed_data, size_t compressed_len, size_t* out_len);

// Decompress FlateDecode with predictor support
// params: decode parameters (predictor, colors, bits, columns)
char* flate_decode_with_predictor(const char* compressed_data, size_t compressed_len, 
                                   size_t* out_len, const PDFDecodeParams* params);

// Decode LZW compressed data
// Returns: decoded data (caller must free), or NULL on error
// early_change: 1 for standard PDF LZW, 0 for some older encoders
char* lzw_decode(const char* compressed_data, size_t compressed_len, 
                 size_t* out_len, int early_change);

// Decode LZW with predictor support
char* lzw_decode_with_predictor(const char* compressed_data, size_t compressed_len,
                                 size_t* out_len, const PDFDecodeParams* params);

// Decode ASCII85 (base-85) encoded data
// Returns: decoded data (caller must free), or NULL on error
// out_len: receives the decoded length
char* ascii85_decode(const char* encoded_data, size_t encoded_len, size_t* out_len);

// Decode ASCIIHex (hexadecimal) encoded data
// Returns: decoded data (caller must free), or NULL on error
char* asciihex_decode(const char* encoded_data, size_t encoded_len, size_t* out_len);

// Decode RunLength encoded data
// Returns: decoded data (caller must free), or NULL on error
char* runlength_decode(const char* encoded_data, size_t encoded_len, size_t* out_len);

// Apply predictor to already decompressed data (for PNG/TIFF predictors)
// Returns: unpredicted data (caller must free), or NULL on error
char* apply_predictor(const char* data, size_t data_len, size_t* out_len,
                      const PDFDecodeParams* params);

// Decompress PDF stream with multiple filters and parameters
// Supports: FlateDecode, LZWDecode, ASCII85Decode, ASCIIHexDecode, RunLengthDecode
// filter_params: array of PDFDecodeParams (can be NULL for no params)
// Returns: decompressed data (caller must free), or NULL on error
// out_len: receives the decompressed length
char* pdf_decompress_stream(const char* data, size_t data_len,
                            const char** filters, int filter_count,
                            size_t* out_len);

// Enhanced version with parameter support
char* pdf_decompress_stream_with_params(const char* data, size_t data_len,
                                         const char** filters, int filter_count,
                                         const PDFDecodeParams* filter_params,
                                         size_t* out_len);

#ifdef __cplusplus
}
#endif
