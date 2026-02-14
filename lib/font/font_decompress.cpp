/**
 * Lambda Unified Font Module — Font Decompression
 *
 * Handles WOFF1 (zlib per-table inflate) and WOFF2 (Brotli via libwoff2)
 * decompression. All decompressed output is arena-allocated so the memory
 * lives as long as the FontHandle.
 *
 * This is the ONLY file in the font module that uses C++ (due to libwoff2 API).
 * Everything else is pure C.
 *
 * Copyright (c) 2025 Lambda Script Project
 */

#include "font_internal.h"
#include <zlib.h>

// WOFF2 C++ API — confined to this file
#include <woff2/decode.h>
#include <woff2/output.h>

// ============================================================================
// WOFF1 Format Structures
// ============================================================================

// WOFF1 header (44 bytes, big-endian on disk)
typedef struct {
    uint32_t signature;         // 0x774F4646 ('wOFF')
    uint32_t flavor;            // original SFNT flavor (0x00010000 for TTF, 'OTTO' for CFF)
    uint32_t length;            // total WOFF file size
    uint16_t num_tables;        // number of tables
    uint16_t reserved;          // must be zero
    uint32_t total_sfnt_size;   // uncompressed size of original SFNT
    uint16_t major_version;     // WOFF major version
    uint16_t minor_version;     // WOFF minor version
    uint32_t meta_offset;       // metadata block offset
    uint32_t meta_length;       // compressed metadata length
    uint32_t meta_orig_length;  // uncompressed metadata length
    uint32_t priv_offset;       // private data offset
    uint32_t priv_length;       // private data length
} WoffHeader;

// WOFF1 table directory entry (20 bytes)
typedef struct {
    uint32_t tag;               // 4-byte sfnt table identifier
    uint32_t offset;            // offset to compressed data, from WOFF file start
    uint32_t comp_length;       // compressed data length
    uint32_t orig_length;       // uncompressed table length
    uint32_t orig_checksum;     // table checksum
} WoffTableDir;

// SFNT table directory entry (16 bytes)
typedef struct {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} SfntTableDir;

// ============================================================================
// Byte swapping helpers (WOFF1 is big-endian)
// ============================================================================

static inline uint16_t read_u16_be(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline void write_u16_be(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static inline void write_u32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

// ============================================================================
// WOFF1 Decompression
// ============================================================================

bool font_decompress_woff1(Arena* arena, const uint8_t* data, size_t len,
                           uint8_t** out, size_t* out_len) {
    if (!arena || !data || !out || !out_len) return false;

    // validate minimum header size
    if (len < sizeof(WoffHeader)) {
        log_error("font_decompress_woff1: data too short for WOFF header (%zu bytes)", len);
        return false;
    }

    // parse header (big-endian)
    uint32_t signature      = read_u32_be(data + 0);
    uint32_t flavor         = read_u32_be(data + 4);
    // uint32_t woff_length = read_u32_be(data + 8);
    uint16_t num_tables     = read_u16_be(data + 12);
    // uint16_t reserved    = read_u16_be(data + 14);
    uint32_t total_sfnt_sz  = read_u32_be(data + 16);

    if (signature != 0x774F4646) { // 'wOFF'
        log_error("font_decompress_woff1: bad signature 0x%08X", signature);
        return false;
    }

    if (num_tables == 0 || total_sfnt_sz == 0) {
        log_error("font_decompress_woff1: empty font (tables=%u, sfnt_size=%u)", num_tables, total_sfnt_sz);
        return false;
    }

    // validate table directory fits
    size_t dir_offset = 44; // sizeof WoffHeader
    size_t dir_size   = (size_t)num_tables * 20; // sizeof WoffTableDir
    if (dir_offset + dir_size > len) {
        log_error("font_decompress_woff1: table directory overflows input");
        return false;
    }

    // allocate output SFNT buffer from arena
    uint8_t* sfnt = (uint8_t*)arena_calloc(arena, total_sfnt_sz);
    if (!sfnt) {
        log_error("font_decompress_woff1: arena_calloc failed for %u bytes", total_sfnt_sz);
        return false;
    }

    // build SFNT header
    // SFNT offset table: 12 bytes + num_tables * 16 bytes
    size_t sfnt_header_size = 12 + (size_t)num_tables * 16;
    if (sfnt_header_size > total_sfnt_sz) {
        log_error("font_decompress_woff1: sfnt header exceeds total size");
        return false;
    }

    // write SFNT offset table header
    write_u32_be(sfnt + 0, flavor);          // scaler type / flavor
    write_u16_be(sfnt + 4, num_tables);

    // compute searchRange, entrySelector, rangeShift
    uint16_t search_range = 1;
    uint16_t entry_selector = 0;
    while (search_range * 2 <= num_tables) {
        search_range *= 2;
        entry_selector++;
    }
    search_range *= 16;
    uint16_t range_shift = num_tables * 16 - search_range;

    write_u16_be(sfnt + 6, search_range);
    write_u16_be(sfnt + 8, entry_selector);
    write_u16_be(sfnt + 10, range_shift);

    // decompress each table and write SFNT table directory + data
    size_t sfnt_data_offset = sfnt_header_size;

    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t* woff_entry = data + dir_offset + (size_t)i * 20;
        uint32_t tag         = read_u32_be(woff_entry + 0);
        uint32_t comp_off    = read_u32_be(woff_entry + 4);
        uint32_t comp_len    = read_u32_be(woff_entry + 8);
        uint32_t orig_len    = read_u32_be(woff_entry + 12);
        uint32_t orig_cksum  = read_u32_be(woff_entry + 16);

        // validate source range
        if ((size_t)comp_off + comp_len > len) {
            log_error("font_decompress_woff1: table %d data overflows input", i);
            return false;
        }

        // validate destination range
        if (sfnt_data_offset + orig_len > total_sfnt_sz) {
            log_error("font_decompress_woff1: table %d data overflows output", i);
            return false;
        }

        uint8_t* dest = sfnt + sfnt_data_offset;

        if (comp_len < orig_len) {
            // table is zlib-compressed — decompress
            uLongf dest_len = (uLongf)orig_len;
            int zret = uncompress(dest, &dest_len, data + comp_off, (uLong)comp_len);
            if (zret != Z_OK) {
                log_error("font_decompress_woff1: zlib uncompress failed for table %d (tag=0x%08X, ret=%d)",
                          i, tag, zret);
                return false;
            }
            if (dest_len != orig_len) {
                log_error("font_decompress_woff1: table %d size mismatch (expected %u, got %lu)",
                          i, orig_len, (unsigned long)dest_len);
                return false;
            }
        } else {
            // table is stored uncompressed — just copy
            memcpy(dest, data + comp_off, orig_len);
        }

        // write SFNT table directory entry
        uint8_t* sfnt_dir = sfnt + 12 + (size_t)i * 16;
        write_u32_be(sfnt_dir + 0, tag);
        write_u32_be(sfnt_dir + 4, orig_cksum);
        write_u32_be(sfnt_dir + 8, (uint32_t)sfnt_data_offset);
        write_u32_be(sfnt_dir + 12, orig_len);

        // advance output offset (4-byte aligned per SFNT spec)
        sfnt_data_offset += orig_len;
        sfnt_data_offset = (sfnt_data_offset + 3) & ~(size_t)3;
    }

    *out = sfnt;
    *out_len = total_sfnt_sz;

    log_info("font_decompress_woff1: decompressed %zu -> %u bytes (%u tables)",
             len, total_sfnt_sz, num_tables);
    return true;
}

// ============================================================================
// WOFF2 Decompression (wraps libwoff2)
// ============================================================================

bool font_decompress_woff2(Arena* arena, const uint8_t* data, size_t len,
                           uint8_t** out, size_t* out_len) {
    if (!arena || !data || !out || !out_len) return false;

    // compute final size first
    size_t final_size = woff2::ComputeWOFF2FinalSize(data, len);
    if (final_size == 0) {
        log_error("font_decompress_woff2: ComputeWOFF2FinalSize returned 0");
        return false;
    }

    // allocate output buffer from arena (no std::string needed!)
    uint8_t* buf = (uint8_t*)arena_alloc(arena, final_size);
    if (!buf) {
        log_error("font_decompress_woff2: arena_alloc failed for %zu bytes", final_size);
        return false;
    }

    // use WOFF2MemoryOut to write directly into our arena buffer
    woff2::WOFF2MemoryOut output(buf, final_size);

    if (!woff2::ConvertWOFF2ToTTF(data, len, &output)) {
        log_error("font_decompress_woff2: ConvertWOFF2ToTTF failed");
        // arena memory is not individually freed; it's fine
        return false;
    }

    *out = buf;
    *out_len = output.Size();

    log_info("font_decompress_woff2: decompressed %zu -> %zu bytes", len, *out_len);
    return true;
}

// ============================================================================
// Format Detection and Unified Decompression
// ============================================================================

FontFormat font_detect_format(const uint8_t* data, size_t len) {
    if (!data || len < 4) return FONT_FORMAT_UNKNOWN;

    uint32_t magic = read_u32_be(data);

    switch (magic) {
        case 0x774F4646: return FONT_FORMAT_WOFF;     // 'wOFF'
        case 0x774F4632: return FONT_FORMAT_WOFF2;    // 'wOF2'
        case 0x00010000: return FONT_FORMAT_TTF;      // TrueType
        case 0x74727565: return FONT_FORMAT_TTF;      // 'true' (Apple)
        case 0x4F54544F: return FONT_FORMAT_OTF;      // 'OTTO' (OpenType/CFF)
        case 0x74746366: return FONT_FORMAT_TTC;      // 'ttcf' (TrueType Collection)
        default:         return FONT_FORMAT_UNKNOWN;
    }
}

FontFormat font_detect_format_ext(const char* path) {
    if (!path) return FONT_FORMAT_UNKNOWN;
    size_t len = strlen(path);

    // check extension (case-insensitive by checking last chars)
    if (len >= 4) {
        const char* ext = path + len - 4;
        if (strcasecmp(ext, ".ttf") == 0) return FONT_FORMAT_TTF;
        if (strcasecmp(ext, ".otf") == 0) return FONT_FORMAT_OTF;
        if (strcasecmp(ext, ".ttc") == 0) return FONT_FORMAT_TTC;
    }
    if (len >= 5) {
        const char* ext = path + len - 5;
        if (strcasecmp(ext, ".woff") == 0) return FONT_FORMAT_WOFF;
    }
    if (len >= 6) {
        const char* ext = path + len - 6;
        if (strcasecmp(ext, ".woff2") == 0) return FONT_FORMAT_WOFF2;
    }

    return FONT_FORMAT_UNKNOWN;
}

bool font_decompress_if_needed(Arena* arena, const uint8_t* data, size_t len,
                               FontFormat format,
                               const uint8_t** out, size_t* out_len) {
    if (!arena || !data || !out || !out_len) return false;

    switch (format) {
        case FONT_FORMAT_WOFF: {
            uint8_t* decompressed = NULL;
            size_t   decompressed_len = 0;
            if (!font_decompress_woff1(arena, data, len, &decompressed, &decompressed_len)) {
                return false;
            }
            *out = decompressed;
            *out_len = decompressed_len;
            return true;
        }
        case FONT_FORMAT_WOFF2: {
            uint8_t* decompressed = NULL;
            size_t   decompressed_len = 0;
            if (!font_decompress_woff2(arena, data, len, &decompressed, &decompressed_len)) {
                return false;
            }
            *out = decompressed;
            *out_len = decompressed_len;
            return true;
        }
        case FONT_FORMAT_TTF:
        case FONT_FORMAT_OTF:
        case FONT_FORMAT_TTC:
            // no decompression needed — pass through
            *out = data;
            *out_len = len;
            return true;

        case FONT_FORMAT_UNKNOWN:
        default:
            log_error("font_decompress_if_needed: unknown format");
            return false;
    }
}
