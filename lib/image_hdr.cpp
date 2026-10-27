#include "image_hdr.h"
#include "memtrack.h"
#include <math.h>
#include <string.h>

// the header is text lines up to an empty one; then comes the resolution line
static bool hdr_line(const uint8_t* bytes, size_t length, size_t* offset, const char** line, size_t* size) {
    if (*offset >= length) return false;
    const uint8_t* start = bytes + *offset;
    const uint8_t* end = (const uint8_t*)memchr(start, '\n', length - *offset);
    if (!end) return false;
    *line = (const char*)start; *size = (size_t)(end - start); *offset += *size + 1;
    return true;
}
static bool hdr_number(const char** cursor, const char* end, uint32_t* value) {
    while (*cursor < end && **cursor == ' ') (*cursor)++;
    uint64_t number = 0; const char* first = *cursor;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9' && number <= UINT32_MAX) number = number * 10 + (uint64_t)(*(*cursor)++ - '0');
    if (*cursor == first || number > UINT32_MAX) return false;
    *value = (uint32_t)number; return true;
}
static bool hdr_token(const char** cursor, const char* end, const char* token) {
    while (*cursor < end && **cursor == ' ') (*cursor)++;
    size_t size = strlen(token);
    if ((size_t)(end - *cursor) < size || memcmp(*cursor, token, size)) return false;
    *cursor += size; return true;
}
// one scanline of `width` RGBE texels from `bytes` at `*offset`, run-length encoded per channel
static bool hdr_scanline_rle(const uint8_t* bytes, size_t length, size_t* offset, uint32_t width, uint8_t* scanline) {
    for (unsigned channel = 0; channel < 4; channel++) {
        for (uint32_t x = 0; x < width;) {
            if (*offset >= length) return false;
            unsigned count = bytes[(*offset)++];
            if (count > 128) {
                // a run of one value
                count -= 128;
                if (!count || count > width - x || *offset >= length) return false;
                uint8_t value = bytes[(*offset)++];
                for (unsigned i = 0; i < count; i++) scanline[(x++) * 4 + channel] = value;
            } else {
                if (!count || count > width - x || count > length - *offset) return false;
                for (unsigned i = 0; i < count; i++) scanline[(x++) * 4 + channel] = bytes[(*offset)++];
            }
        }
    }
    return true;
}

bool hdr_read(const uint8_t* bytes, size_t length, uint32_t max_dimension, HdrImage* image, const char** error) {
    *image = {};
    const char* line; size_t size, offset = 0;
    if (!bytes || length < 2 || memcmp(bytes, "#?", 2)) { *error = "not a Radiance HDR image"; return false; }
    for (;;) {
        if (!hdr_line(bytes, length, &offset, &line, &size)) { *error = "truncated HDR header"; return false; }
        if (!size) break;
        // the pixels must be RGBE; XYZE images hold other primaries
        if (size >= 7 && !memcmp(line, "FORMAT=", 7) && (size - 7 != 15 || memcmp(line + 7, "32-bit_rle_rgbe", 15))) {
            *error = "unsupported HDR pixel format"; return false;
        }
    }
    uint32_t width = 0, height = 0;
    if (!hdr_line(bytes, length, &offset, &line, &size)) { *error = "missing HDR resolution"; return false; }
    const char* cursor = line; const char* end = line + size;
    if (end > cursor && end[-1] == '\r') end--;
    if (!hdr_token(&cursor, end, "-Y") || !hdr_number(&cursor, end, &height) || !hdr_token(&cursor, end, "+X") ||
        !hdr_number(&cursor, end, &width) || cursor != end) {
        *error = "unsupported HDR orientation (only -Y height +X width)"; return false;
    }
    if (!width || !height || width > max_dimension || height > max_dimension) { *error = "HDR dimensions out of range"; return false; }
    uint8_t* scanline = (uint8_t*)mem_alloc((size_t)width * 4, MEM_CAT_IMAGE);
    float* pixels = (float*)mem_alloc((size_t)width * height * 3 * sizeof(float), MEM_CAT_IMAGE);
    if (!scanline || !pixels) { mem_free(scanline); mem_free(pixels); *error = "HDR allocation failed"; return false; }
    // as RGBELoader: scanlines are run-length encoded only when the first one says so, else flat
    bool encoded = width >= 8 && width <= 0x7fff && length - offset >= 4 && bytes[offset] == 2 && bytes[offset + 1] == 2 &&
        !(bytes[offset + 2] & 0x80);
    for (uint32_t y = 0; y < height; y++) {
        bool read;
        if (encoded) {
            read = length - offset >= 4 && bytes[offset] == 2 && bytes[offset + 1] == 2 &&
                (uint32_t)((bytes[offset + 2] << 8) | bytes[offset + 3]) == width;
            offset += 4;
            read = read && hdr_scanline_rle(bytes, length, &offset, width, scanline);
        } else {
            read = length - offset >= (size_t)width * 4;
            if (read) { memcpy(scanline, bytes + offset, (size_t)width * 4); offset += (size_t)width * 4; }
        }
        if (!read) { mem_free(scanline); mem_free(pixels); *error = "truncated or malformed HDR scanline"; return false; }
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t* rgbe = scanline + x * 4;
            float* out = pixels + ((size_t)y * width + x) * 3;
            float scale = rgbe[3] ? (float)(ldexp(1.0, rgbe[3] - 128) / 255.0) : 0.0f;
            out[0] = rgbe[0] * scale; out[1] = rgbe[1] * scale; out[2] = rgbe[2] * scale;
        }
    }
    mem_free(scanline);
    image->pixels = pixels; image->width = width; image->height = height;
    return true;
}

void hdr_image_release(HdrImage* image) {
    if (!image) return;
    mem_free(image->pixels); *image = {};
}
