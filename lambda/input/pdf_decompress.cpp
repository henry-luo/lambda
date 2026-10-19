#include "pdf_decompress.h"
#include "../../lib/byte_builder.h"
#include "../../lib/mem.h"
#include <limits.h>
#include <string.h>
#include <zlib.h>
#include "lib/log.h"
#include "lib/hex.h"
#include "lib/endian.h"

/**
 * PDF Stream Decompression Implementation
 * Based on pdf.js stream decoders.
 */

// Initialize decode params to defaults
void pdf_decode_params_init(PDFDecodeParams* params) {
    if (!params) return;
    params->predictor = 1;      // None
    params->colors = 1;
    params->bits = 8;
    params->columns = 1;
    params->early_change = 1;   // Standard PDF LZW
}

static bool pdf_inflate_prepare_output(z_stream* stream, ByteBuilder* output,
                                       size_t minimum_writable, size_t* writable) {
    if (!byte_builder_reserve(output, minimum_writable)) return false;
    stream->next_out = byte_builder_writable_tail(output, writable);
    if (*writable == 0) return false;
    if (*writable > UINT_MAX) *writable = UINT_MAX;
    stream->avail_out = (uInt)*writable;
    return true;
}

static char* pdf_inflate_fail(z_stream* stream, ByteBuilder* output) {
    inflateEnd(stream);
    byte_builder_destroy(output);
    return NULL;
}

/**
 * Decompress FlateDecode (zlib/deflate) compressed data
 */
char* flate_decode(const char* compressed_data, size_t compressed_len, size_t* out_len) {
    if (!compressed_data || compressed_len == 0 || !out_len) {
        return NULL;
    }

    if (compressed_len > UINT_MAX || compressed_len > SIZE_MAX / 4u) {
        return NULL;
    }

    // initial buffer size estimate (typically 2-10x compression ratio)
    ByteBuilder output = {};
    if (!byte_builder_init(&output, compressed_len * 4u, MEM_CAT_INPUT_PDF, true)) return NULL;

    // initialize zlib stream
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef*)compressed_data;
    stream.avail_in = (uInt)compressed_len;

    // try different zlib initialization modes
    // try with zlib header first (most common in PDF after ASCII85)
    int ret = inflateInit(&stream);
    if (ret != Z_OK) {
        // try raw deflate (no zlib wrapper)
        ret = inflateInit2(&stream, -15);
        if (ret != Z_OK) {
            // try auto-detect
            ret = inflateInit2(&stream, 15 + 32);
            if (ret != Z_OK) {
                byte_builder_destroy(&output);
                return NULL;
            }
        }
    }

    // decompress
    size_t writable = 0;
    if (!pdf_inflate_prepare_output(&stream, &output, 64u, &writable)) {
        return pdf_inflate_fail(&stream, &output);
    }
    ret = inflate(&stream, Z_FINISH);
    if (!byte_builder_commit(&output, writable - (size_t)stream.avail_out)) {
        return pdf_inflate_fail(&stream, &output);
    }

    // handle need for larger buffer
    size_t last_output_length = output.length;
    int stall_count = 0;
    while (ret == Z_BUF_ERROR || (ret == Z_OK && stream.avail_out == 0)) {
        if (!pdf_inflate_prepare_output(&stream, &output, 64u, &writable)) {
            return pdf_inflate_fail(&stream, &output);
        }
        ret = inflate(&stream, Z_FINISH);
        if (!byte_builder_commit(&output, writable - (size_t)stream.avail_out)) {
            return pdf_inflate_fail(&stream, &output);
        }

        // safety check: if no progress, break out
        if (output.length == last_output_length) {
            stall_count++;
            if (stall_count > 3) {
                break;
            }
        } else {
            stall_count = 0;
            last_output_length = output.length;
        }
    }

    if (ret != Z_STREAM_END) {
        // if we have output and input is consumed, accept partial success
        if (output.length > 0 && stream.avail_in == 0) {
            *out_len = output.length;
            inflateEnd(&stream);
            return (char*)byte_builder_take(&output, NULL);
        }

        return pdf_inflate_fail(&stream, &output);
    }

    *out_len = output.length;
    inflateEnd(&stream);
    return (char*)byte_builder_take(&output, NULL);
}

/**
 * LZW Decoder
 * Based on pdf.js src/core/lzw_stream.js
 * 
 * LZW is a dictionary-based compression algorithm.
 * PDF uses 12-bit codes with standard clear (256) and EOD (257) codes.
 */
#define LZW_CLEAR_CODE 256
#define LZW_EOD_CODE 257
#define LZW_MAX_DICT_SIZE 4096

char* lzw_decode(const char* compressed_data, size_t compressed_len, 
                 size_t* out_len, int early_change) {
    if (!compressed_data || compressed_len == 0 || !out_len) {
        return NULL;
    }

    if (compressed_len > SIZE_MAX / 4u) return NULL;
    ByteBuilder output = {};
    if (!byte_builder_init(&output, compressed_len * 4u, MEM_CAT_INPUT_PDF, true)) return NULL;

    // LZW dictionary
    uint8_t dict_values[LZW_MAX_DICT_SIZE];
    uint16_t dict_lengths[LZW_MAX_DICT_SIZE];
    uint16_t dict_prev_codes[LZW_MAX_DICT_SIZE];
    uint8_t current_sequence[LZW_MAX_DICT_SIZE];
    
    // initialize first 256 entries
    for (int i = 0; i < 256; i++) {
        dict_values[i] = (uint8_t)i;
        dict_lengths[i] = 1;
        dict_prev_codes[i] = 0;
    }
    
    int next_code = 258;
    int code_length = 9;
    int prev_code = -1;
    
    // bit reading state
    uint32_t cached_data = 0;
    int bits_cached = 0;
    size_t input_pos = 0;
    
    while (input_pos < compressed_len || bits_cached >= code_length) {
        // read bits for next code
        while (bits_cached < code_length && input_pos < compressed_len) {
            cached_data = (cached_data << 8) | ((uint8_t)compressed_data[input_pos++]);
            bits_cached += 8;
        }
        
        if (bits_cached < code_length) {
            break; // not enough bits
        }
        
        bits_cached -= code_length;
        int code = (cached_data >> bits_cached) & ((1 << code_length) - 1);
        
        if (code == LZW_EOD_CODE) {
            break; // end of data
        }
        
        if (code == LZW_CLEAR_CODE) {
            // reset dictionary
            next_code = 258;
            code_length = 9;
            prev_code = -1;
            continue;
        }
        
        int current_len = 0;
        
        if (code < 256) {
            // single byte
            current_sequence[0] = (uint8_t)code;
            current_len = 1;
        } else if (code < next_code) {
            // existing dictionary entry
            current_len = dict_lengths[code];
            int q = code;
            for (int j = current_len - 1; j >= 0; j--) {
                current_sequence[j] = dict_values[q];
                q = dict_prev_codes[q];
            }
        } else if (code == next_code && prev_code >= 0) {
            // special case: code not in dictionary yet
            int prev_len = (prev_code < 256) ? 1 : dict_lengths[prev_code];
            current_len = prev_len + 1;
            
            int q = prev_code;
            for (int j = prev_len - 1; j >= 0; j--) {
                current_sequence[j] = dict_values[q];
                q = dict_prev_codes[q];
            }
            current_sequence[prev_len] = current_sequence[0];
        } else {
            // invalid code
            log_error("LZW invalid code: %d (next_code=%d)", code, next_code);
            break;
        }
        
        // copy to output
        if (!byte_builder_append(&output, current_sequence, (size_t)current_len)) {
            byte_builder_destroy(&output);
            return NULL;
        }
        
        // add new dictionary entry
        if (prev_code >= 0 && next_code < LZW_MAX_DICT_SIZE) {
            dict_values[next_code] = current_sequence[0];
            dict_prev_codes[next_code] = (uint16_t)prev_code;
            dict_lengths[next_code] = ((prev_code < 256) ? 1 : dict_lengths[prev_code]) + 1;
            next_code++;
            
            // increase code length when needed
            int threshold = early_change ? (1 << code_length) - 1 : (1 << code_length);
            if (next_code >= threshold && code_length < 12) {
                code_length++;
            }
        }
        
        prev_code = code;
    }
    
    *out_len = output.length;
    return (char*)byte_builder_take(&output, NULL);
}

/**
 * Decode ASCII85 (base-85) encoded data
 */
bool ascii85_decode_bounded(const char* data, size_t length, StrBuf* output,
                            size_t max_bytes, bool require_eod, size_t* consumed) {
    if (!data || !output || !consumed) return false;
    strbuf_reset(output);
    *consumed = 0;
    size_t pos = 0;
    uint64_t value = 0;
    unsigned count = 0;
    bool closed = false;
    while (pos < length && (data[pos] == 0 || data[pos] == 9 || data[pos] == 10 ||
            data[pos] == 12 || data[pos] == 13 || data[pos] == 32)) pos++;
    if (length - pos >= 2 && data[pos] == '<' && data[pos + 1] == '~') pos += 2;
    while (pos < length) {
        unsigned char c = (unsigned char)data[pos++];
        if (c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32) continue;
        if (c == '~' && pos < length && data[pos] == '>') { pos++; closed = true; break; }
        unsigned produced = 0;
        if (c == 'z' && count == 0) { value = 0; produced = 4; }
        else if (c >= '!' && c <= 'u') {
            value = value * 85 + c - '!';
            if (++count == 5) {
                if (value > UINT32_MAX) return false;
                produced = 4;
            }
        } else return false;
        // 'z' expands one source byte to four bytes; encoded length is not an output capacity.
        if (produced > max_bytes - output->length) return false;
        if (!strbuf_ensure_cap(output, output->length + produced + 1)) return false;
        for (unsigned i = 0; i < produced; i++)
            strbuf_append_char(output, (char)(value >> (24 - i * 8)));
        if (produced) { count = 0; value = 0; }
    }
    *consumed = pos;
    if ((require_eod && !closed) || count == 1) return false;
    if (count > 1) {
        for (unsigned i = count; i < 5; i++) value = value * 85 + 84;
        if (value > UINT32_MAX || count - 1 > max_bytes - output->length) return false;
        if (!strbuf_ensure_cap(output, output->length + count)) return false;
        for (unsigned i = 0; i < count - 1; i++)
            strbuf_append_char(output, (char)(value >> (24 - i * 8)));
    }
    return true;
}

static char* copy_decoded_buffer(StrBuf* buffer, size_t* out_len) {
    char* result = (char*)mem_alloc(buffer->length + 1, MEM_CAT_INPUT_PDF);
    if (result) {
        if (buffer->length) memcpy(result, buffer->str, buffer->length);
        result[buffer->length] = 0;
        *out_len = buffer->length;
    }
    strbuf_free(buffer);
    return result;
}

char* ascii85_decode(const char* encoded_data, size_t encoded_len, size_t* out_len) {
    if (!encoded_data || !encoded_len || !out_len) return NULL;
    StrBuf* buffer = strbuf_new();
    size_t consumed = 0;
    if (!buffer || !ascii85_decode_bounded(encoded_data, encoded_len, buffer, SIZE_MAX, false, &consumed)) {
        strbuf_free(buffer); return NULL;
    }
    return copy_decoded_buffer(buffer, out_len);
}

/**
 * Decode ASCIIHex (hexadecimal) encoded data
 * Based on pdf.js src/core/ascii_hex_stream.js
 */
bool asciihex_decode_bounded(const char* data, size_t length, StrBuf* output,
                             size_t max_bytes, bool strict, bool require_eod,
                             size_t* consumed) {
    if (!data || !output || !consumed) return false;
    strbuf_reset(output);
    *consumed = 0;
    int high = -1;
    bool closed = false;
    for (size_t pos = 0; pos < length; pos++) {
        unsigned char c = (unsigned char)data[pos];
        *consumed = pos + 1;
        if (c == '>') { closed = true; break; }
        int digit = hex_decode_byte((char)c);
        if (digit < 0) {
            if (strict && !(c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32)) return false;
            continue;
        }
        if (high < 0) { high = digit; continue; }
        if (output->length >= max_bytes || !strbuf_ensure_cap(output, output->length + 2)) return false;
        strbuf_append_char(output, (char)(high * 16 + digit));
        high = -1;
    }
    if (require_eod && !closed) return false;
    if (high >= 0) {
        if (output->length >= max_bytes || !strbuf_ensure_cap(output, output->length + 2)) return false;
        strbuf_append_char(output, (char)(high * 16));
    }
    return true;
}

char* asciihex_decode(const char* encoded_data, size_t encoded_len, size_t* out_len) {
    if (!encoded_data || !encoded_len || !out_len) return NULL;
    StrBuf* buffer = strbuf_new();
    size_t consumed = 0;
    if (!buffer || !asciihex_decode_bounded(encoded_data, encoded_len, buffer, SIZE_MAX, false, false, &consumed)) {
        strbuf_free(buffer); return NULL;
    }
    return copy_decoded_buffer(buffer, out_len);
}

/**
 * Decode RunLength encoded data
 * Based on pdf.js src/core/run_length_stream.js
 * 
 * Format: n (0-127) = copy next n+1 bytes
 *         n (129-255) = repeat next byte (257-n) times
 *         n (128) = end of data
 */
bool runlength_decode_bounded(const char* data, size_t length, StrBuf* out,
                              size_t max_bytes, bool strict, bool require_eod, size_t* consumed) {
    strbuf_reset(out); *consumed = 0;
    while (*consumed < length) {
        unsigned n = (unsigned char)data[(*consumed)++];
        if (n == 128) return true;
        size_t amount = n < 128 ? n + 1 : 257 - n;
        size_t input = n < 128 ? amount : 1;
        if (input > length - *consumed) {
            if (strict) return false;
            if (n >= 128) break;
            amount = input = length - *consumed;
        }
        if (amount > max_bytes - out->length || !strbuf_ensure_cap(out, out->length + amount + 1)) return false;
        if (n < 128) strbuf_append_str_n(out, data + *consumed, amount);
        else for (size_t i = 0; i < amount; i++) strbuf_append_char(out, data[*consumed]);
        *consumed += input;
    }
    return !require_eod;
}

char* runlength_decode(const char* data, size_t length, size_t* out_len) {
    if (!data || !length || !out_len) return nullptr;
    StrBuf* out = strbuf_new(); size_t consumed = 0;
    if (!out) return nullptr;
    if (!runlength_decode_bounded(data, length, out, SIZE_MAX - 1, false, false, &consumed)) {
        strbuf_free(out); return nullptr;
    }
    return copy_decoded_buffer(out, out_len);
}

/**
 * Apply PNG predictor to decompressed data
 * Based on pdf.js src/core/predictor_stream.js
 * 
 * PNG predictors (10-15):
 * 10 = None
 * 11 = Sub (byte to left)
 * 12 = Up (byte above)
 * 13 = Average (average of left and above)
 * 14 = Paeth (use best of left, above, upper-left)
 * 15 = Optimum (per-row indicator)
 */
static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    // a = left, b = above, c = upper-left
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

char* apply_predictor(const char* data, size_t data_len, size_t* out_len,
                      const PDFDecodeParams* params) {
    if (!data || data_len == 0 || !out_len || !params) {
        return NULL;
    }

    int predictor = params->predictor;
    
    // no predictor
    if (predictor <= 1) {
        char* output = mem_dup_n(data, data_len, MEM_CAT_INPUT_PDF);
        if (!output) return NULL;
        *out_len = data_len;
        return output;
    }

    int colors = params->colors, bits = params->bits, columns = params->columns;
    if (colors <= 0 || colors > 32 || columns <= 0 ||
            !(bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16)) return nullptr;
    size_t pix_bytes = ((size_t)colors * bits + 7) / 8;
    size_t row_bytes = ((size_t)columns * colors * bits + 7) / 8;
    if (predictor == 2) {
        // TIFF differencing operates on component samples, including 16-bit carry and packed rows.
        if (data_len % row_bytes) return nullptr;
        uint8_t* output = (uint8_t*)mem_dup_n(data, data_len, MEM_CAT_INPUT_PDF);
        if (!output) return nullptr;
        unsigned mask = (1u << bits) - 1;
        for (size_t row = 0; row < data_len; row += row_bytes) {
            unsigned previous[32] = {};
            for (size_t sample = 0; sample < (size_t)columns * colors; sample++) {
                size_t bit = sample * bits, byte = row + bit / 8;
                unsigned value = pdf_read_sample(output + row, bit, bits);
                value = (value + previous[sample % colors]) & mask;
                previous[sample % colors] = value;
                if (bits == 16) write_be16(output + byte, (uint16_t)value);
                else {
                    unsigned shift = 8 - bits - bit % 8;
                    output[byte] = (output[byte] & ~(mask << shift)) | (value << shift);
                }
            }
        }
        *out_len = data_len; return (char*)output;
    }
    if (predictor < 10 || predictor > 15 || data_len % (row_bytes + 1)) return nullptr;
    
    // for PNG predictors (10-15), each row has a 1-byte type indicator
    size_t input_row_bytes = row_bytes + 1;
    size_t num_rows = data_len / input_row_bytes;
    
    size_t output_size = num_rows * row_bytes;
    char* output = (char*)mem_alloc(output_size + 1, MEM_CAT_INPUT_PDF);
    if (!output) return NULL;
    
    // previous row buffer (for Up and Paeth predictors)
    uint8_t* prev_row = (uint8_t*)mem_calloc(row_bytes, 1, MEM_CAT_INPUT_PDF);
    if (!prev_row) {
        mem_free(output);
        return NULL;
    }
    
    const uint8_t* in = (const uint8_t*)data;
    uint8_t* out = (uint8_t*)output;
    
    for (size_t row = 0; row < num_rows; row++) {
        int row_predictor = predictor;
        
        // for predictor 15, read per-row type
        if (predictor >= 10 && predictor <= 15) {
            row_predictor = *in++ + 10;  // PNG predictor types are 0-4, map to 10-14
        }
        if (row_predictor > 14) { mem_free(prev_row); mem_free(output); return nullptr; }
        
        for (size_t col = 0; col < row_bytes; col++) {
            uint8_t raw = *in++;
            uint8_t left = (col >= pix_bytes) ? *(out - pix_bytes) : 0;
            uint8_t above = prev_row[col];
            uint8_t upper_left = (col >= pix_bytes) ? prev_row[col - pix_bytes] : 0;
            
            uint8_t decoded;
            switch (row_predictor) {
                case 10: // None
                    decoded = raw;
                    break;
                case 11: // Sub
                    decoded = raw + left;
                    break;
                case 12: // Up
                    decoded = raw + above;
                    break;
                case 13: // Average
                    decoded = raw + ((left + above) / 2);
                    break;
                case 14: // Paeth
                    decoded = raw + paeth_predictor(left, above, upper_left);
                    break;
                default:
                    decoded = raw;
                    break;
            }
            
            *out++ = decoded;
        }
        memcpy(prev_row, out - row_bytes, row_bytes);
    }
    
    mem_free(prev_row);
    
    *out_len = output_size;
    output[output_size] = '\0';
    return output;
}

/**
 * Decompress FlateDecode with predictor support
 */
static char* apply_predictor_to_decoded(char* decompressed, size_t decompressed_len,
                                        size_t* out_len, const PDFDecodeParams* params) {
    if (!decompressed) return NULL;
    if (params && params->predictor > 1) {
        size_t final_len = 0;
        char* final_data = apply_predictor(decompressed, decompressed_len, &final_len, params);
        mem_free(decompressed);
        if (!final_data) return NULL;
        *out_len = final_len;
        return final_data;
    }
    *out_len = decompressed_len;
    return decompressed;
}

char* flate_decode_with_predictor(const char* compressed_data, size_t compressed_len, 
                                   size_t* out_len, const PDFDecodeParams* params) {
    size_t decompressed_len = 0;
    char* decompressed = flate_decode(compressed_data, compressed_len, &decompressed_len);
    return apply_predictor_to_decoded(decompressed, decompressed_len, out_len, params);
}

/**
 * Decompress LZW with predictor support
 */
char* lzw_decode_with_predictor(const char* compressed_data, size_t compressed_len,
                                 size_t* out_len, const PDFDecodeParams* params) {
    int early_change = params ? params->early_change : 1;
    size_t decompressed_len = 0;
    char* decompressed = lzw_decode(compressed_data, compressed_len, &decompressed_len, early_change);
    return apply_predictor_to_decoded(decompressed, decompressed_len, out_len, params);
}

/**
 * Decompress PDF stream with multiple filters
 * Legacy function for backward compatibility
 */
char* pdf_decompress_stream(const char* data, size_t data_len,
                            const char** filters, int filter_count,
                            size_t* out_len) {
    return pdf_decompress_stream_with_params(data, data_len, filters, filter_count, NULL, out_len);
}

/**
 * Decompress PDF stream with multiple filters and parameters
 * Enhanced version supporting all standard PDF filters
 */
char* pdf_decompress_stream_with_params(const char* data, size_t data_len,
                                         const char** filters, int filter_count,
                                         const PDFDecodeParams* filter_params,
                                         size_t* out_len) {
    if (!data || data_len == 0 || !filters || filter_count == 0 || !out_len) {
        return NULL;
    }

    char* current_data = (char*)data;
    size_t current_len = data_len;
    bool need_free = false;

    // apply filters in order
    for (int i = 0; i < filter_count; i++) {
        const char* filter = filters[i];
        const PDFDecodeParams* params = filter_params ? &filter_params[i] : NULL;
        char* decoded_data = NULL;
        size_t decoded_len = 0;

        log_debug("Applying PDF filter: %s", filter);

        if (strcmp(filter, "FlateDecode") == 0 || strcmp(filter, "Fl") == 0) {
            if (params && params->predictor > 1) {
                decoded_data = flate_decode_with_predictor(current_data, current_len, &decoded_len, params);
            } else {
                decoded_data = flate_decode(current_data, current_len, &decoded_len);
            }
        } else if (strcmp(filter, "LZWDecode") == 0 || strcmp(filter, "LZW") == 0) {
            if (params) {
                decoded_data = lzw_decode_with_predictor(current_data, current_len, &decoded_len, params);
            } else {
                decoded_data = lzw_decode(current_data, current_len, &decoded_len, 1);
            }
        } else if (strcmp(filter, "ASCII85Decode") == 0 || strcmp(filter, "A85") == 0) {
            decoded_data = ascii85_decode(current_data, current_len, &decoded_len);
        } else if (strcmp(filter, "ASCIIHexDecode") == 0 || strcmp(filter, "AHx") == 0) {
            decoded_data = asciihex_decode(current_data, current_len, &decoded_len);
        } else if (strcmp(filter, "RunLengthDecode") == 0 || strcmp(filter, "RL") == 0) {
            decoded_data = runlength_decode(current_data, current_len, &decoded_len);
        } else if (strcmp(filter, "DCTDecode") == 0 || strcmp(filter, "DCT") == 0 ||
                   strcmp(filter, "JPXDecode") == 0 || strcmp(filter, "JPX") == 0 ||
                   strcmp(filter, "CCITTFaxDecode") == 0 || strcmp(filter, "CCF") == 0 ||
                   strcmp(filter, "JBIG2Decode") == 0) {
            // image codecs interpret these payloads after byte filters and predictor decoding.
            decoded_data = mem_dup_n(current_data, current_len, MEM_CAT_INPUT_PDF);
            if (decoded_data) {
                decoded_len = current_len;
            }
            log_debug("PDF_IMAGE_FILTER: %s retains %zu encoded bytes", filter, decoded_len);
        } else {
            // unsupported filter
            log_error("Unsupported PDF filter: %s", filter);
            if (need_free) {
                mem_free(current_data);
            }
            return NULL;
        }

        if (!decoded_data) {
            // failed to decode
            log_error("Failed to decode with filter: %s", filter);
            if (need_free) {
                mem_free(current_data);
            }
            return NULL;
        }

        // free previous intermediate result
        if (need_free) {
            mem_free(current_data);
        }

        current_data = decoded_data;
        current_len = decoded_len;
        need_free = true;
        
        log_debug("Filter %s: %zu bytes output", filter, decoded_len);
    }

    *out_len = current_len;
    return current_data;
}
