#include "input-pdf-image.hpp"
#include "../../lib/endian.h"
#include "../../lib/image.h"
#include "../../lib/mem.h"
#include <openjpeg.h>
#include <limits.h>
#include <string.h>

namespace {
constexpr size_t max_sample_bytes = 128u * 1024 * 1024;

bool image_extent(int width, int height, size_t* pixels) {
    if (width <= 0 || height <= 0 || (size_t)width > max_sample_bytes / (size_t)height) return false;
    *pixels = (size_t)width * height;
    return true;
}

struct JpxInput { const uint8_t* data; size_t length, offset; };
OPJ_SIZE_T jpx_read(void* bytes, OPJ_SIZE_T length, void* user) {
    JpxInput* input = (JpxInput*)user;
    if (length > input->length - input->offset) length = input->length - input->offset;
    if (!length) return (OPJ_SIZE_T)-1;
    memcpy(bytes, input->data + input->offset, length); input->offset += length;
    return length;
}
OPJ_OFF_T jpx_skip(OPJ_OFF_T length, void* user) {
    JpxInput* input = (JpxInput*)user;
    if (length < 0 || (uint64_t)length > input->length - input->offset) return -1;
    input->offset += (size_t)length; return length;
}
OPJ_BOOL jpx_seek(OPJ_OFF_T offset, void* user) {
    JpxInput* input = (JpxInput*)user;
    if (offset < 0 || (uint64_t)offset > input->length) return OPJ_FALSE;
    input->offset = (size_t)offset; return OPJ_TRUE;
}
void jpx_error(const char* message, void*) { log_debug("PDF_JPX_DECODE: %s", message); }
}

bool pdf_decode_jpx_samples(const uint8_t* data, size_t length, int width, int height,
        bool indexed, int* components, int* bits, ByteBuilder* samples) {
    size_t pixels = 0;
    if (!data || length < 2 || !image_extent(width, height, &pixels)) return false;
    OPJ_CODEC_FORMAT format = data[0] == 0xff && data[1] == 0x4f ? OPJ_CODEC_J2K : OPJ_CODEC_JP2;
    opj_codec_t* codec = opj_create_decompress(format);
    opj_stream_t* stream = opj_stream_create(4096, OPJ_TRUE);
    opj_image_t* image = nullptr;
    struct Cleanup {
        opj_codec_t* codec; opj_stream_t* stream; opj_image_t** image;
        ~Cleanup() { if (*image) opj_image_destroy(*image); if (stream) opj_stream_destroy(stream);
            if (codec) opj_destroy_codec(codec); }
    } cleanup = {codec, stream, &image};
    if (!codec || !stream) return false;
    opj_set_error_handler(codec, jpx_error, nullptr);
    opj_set_warning_handler(codec, jpx_error, nullptr);
    opj_dparameters_t options; opj_set_default_decoder_parameters(&options);
    // PDF Indexed colour spaces apply their own palette to the decoded component samples.
    options.flags |= OPJ_DPARAMETERS_IGNORE_PCLR_CMAP_CDEF_FLAG;
    JpxInput input = {data, length, 0};
    opj_stream_set_user_data(stream, &input, nullptr);
    opj_stream_set_user_data_length(stream, length);
    opj_stream_set_read_function(stream, jpx_read);
    opj_stream_set_skip_function(stream, jpx_skip);
    opj_stream_set_seek_function(stream, jpx_seek);
    if (!opj_setup_decoder(codec, &options) || !opj_read_header(stream, codec, &image) || !image ||
            image->numcomps == 0 || image->numcomps > 4 || image->x1 - image->x0 != (uint32_t)width ||
            image->y1 - image->y0 != (uint32_t)height) return false;
    *components = (int)image->numcomps;
    unsigned precision = image->comps[0].prec;
    if (!precision || precision > 16 || pixels > max_sample_bytes / (*components * 2u)) return false;
    for (unsigned c = 0; c < image->numcomps; c++) {
        const opj_image_comp_t* component = image->comps + c;
        if (component->prec != precision || !component->w || !component->h ||
                (uint64_t)component->w * component->h > max_sample_bytes / sizeof(OPJ_INT32)) return false;
    }
    if (!opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) return false;
    *bits = precision <= 8 ? 8 : 16;
    size_t size = pixels * *components * (*bits / 8);
    if (!byte_builder_reserve(samples, size)) return false;
    uint8_t* output = samples->data + samples->length;
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        for (int c = 0; c < *components; c++) {
            const opj_image_comp_t* plane = image->comps + c;
            // component sampling grids can be smaller than the image grid.
            size_t sx = (uint64_t)x * plane->w / width, sy = (uint64_t)y * plane->h / height;
            int64_t value = plane->data[sy * plane->w + sx];
            if (plane->sgnd) value += (int64_t)1 << (precision - 1);
            uint32_t maximum = (1u << precision) - 1;
            if (value < 0 || value > maximum) return false;
            uint32_t sample = indexed ? (uint32_t)value :
                (uint32_t)value * ((*bits == 8) ? 255u : 65535u) / maximum;
            if (*bits == 8) *output++ = (uint8_t)sample;
            else { write_be16(output, (uint16_t)sample); output += 2; }
        }
    return byte_builder_commit(samples, size);
}

bool pdf_decode_fax_samples(const uint8_t* data, size_t length, int width, int height,
        int k, bool end_of_line, bool byte_aligned, bool black_is_one, ByteBuilder* samples) {
    size_t pixels = 0;
    if (!data || length > UINT32_MAX || !image_extent(width, height, &pixels)) return false;
    // expose the PDF fax strip to the bundled TIFF decoder, keeping the codec in lib/image.
    const struct Tag { uint16_t id, type; uint32_t count, value; } tags[] = {
        {256, 4, 1, (uint32_t)width}, {257, 4, 1, (uint32_t)height}, {258, 3, 1, 1},
        {259, 3, 1, (uint32_t)(k < 0 ? 4 : k > 0 || end_of_line ? 3 : 2)},
        {262, 3, 1, 0}, {273, 4, 1, 0}, {277, 3, 1, 1}, {278, 4, 1, (uint32_t)height},
        {279, 4, 1, (uint32_t)length}, {292, 4, 1, (uint32_t)((k > 0 ? 1 : 0) | (byte_aligned ? 4 : 0))}
    };
    const uint16_t count = sizeof(tags) / sizeof(tags[0]);
    const size_t header = 8 + 2 + count * 12 + 4;
    ByteBuilder tiff = {};
    if (!byte_builder_init(&tiff, header + length, MEM_CAT_INPUT_PDF, false)) return false;
    struct Cleanup { ByteBuilder* bytes; ~Cleanup() { byte_builder_destroy(bytes); } } cleanup = {&tiff};
    memset(tiff.data, 0, header); tiff.data[0] = tiff.data[1] = 'I';
    write_le16(tiff.data + 2, 42); write_le32(tiff.data + 4, 8); write_le16(tiff.data + 8, count);
    for (uint16_t i = 0; i < count; i++) {
        uint8_t* entry = tiff.data + 10 + i * 12;
        write_le16(entry, tags[i].id); write_le16(entry + 2, tags[i].type);
        write_le32(entry + 4, tags[i].count);
        write_le32(entry + 8, tags[i].id == 273 ? (uint32_t)header : tags[i].value);
    }
    if (!byte_builder_commit(&tiff, header) || !byte_builder_append(&tiff, data, length)) return false;
    ImageContext context; image_context_init(&context);
    ImageSource source = {tiff.data, tiff.length, nullptr, nullptr};
    ImageDecodeOptions options; image_decode_options_init(&options);
    ImageDecoded decoded = {}; ImageError error = {};
    if (image_decode(&context, &source, &options, &decoded, &error) != IMAGE_OK) return false;
    bool ok = decoded.width == (uint32_t)width && decoded.height == (uint32_t)height &&
        byte_builder_reserve(samples, pixels);
    if (ok) {
        uint8_t* output = samples->data + samples->length;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            uint8_t value = decoded.pixels[(size_t)y * decoded.stride + (size_t)x * 4];
            *output++ = black_is_one ? 255 - value : value;
        }
        ok = byte_builder_commit(samples, pixels);
    }
    image_decoded_dispose(&decoded);
    return ok;
}

namespace {
// ITU-T T.88, Annex E, Table E.1: probability states for the MQ arithmetic decoder.
struct MqState { uint16_t qe; uint8_t mps, lps, change; };
const MqState mq_states[] = {
    {0x5601,1,1,1}, {0x3401,2,6,0}, {0x1801,3,9,0}, {0x0ac1,4,12,0},
    {0x0521,5,29,0}, {0x0221,38,33,0}, {0x5601,7,6,1}, {0x5401,8,14,0},
    {0x4801,9,14,0}, {0x3801,10,14,0}, {0x3001,11,17,0}, {0x2401,12,18,0},
    {0x1c01,13,20,0}, {0x1601,29,21,0}, {0x5601,15,14,1}, {0x5401,16,14,0},
    {0x5101,17,15,0}, {0x4801,18,16,0}, {0x3801,19,17,0}, {0x3401,20,18,0},
    {0x3001,21,19,0}, {0x2801,22,19,0}, {0x2401,23,20,0}, {0x2201,24,21,0},
    {0x1c01,25,22,0}, {0x1801,26,23,0}, {0x1601,27,24,0}, {0x1401,28,25,0},
    {0x1201,29,26,0}, {0x1101,30,27,0}, {0x0ac1,31,28,0}, {0x09c1,32,29,0},
    {0x08a1,33,30,0}, {0x0521,34,31,0}, {0x0441,35,32,0}, {0x02a1,36,33,0},
    {0x0221,37,34,0}, {0x0141,38,35,0}, {0x0111,39,36,0}, {0x0085,40,37,0},
    {0x0049,41,38,0}, {0x0025,42,39,0}, {0x0015,43,40,0}, {0x0009,44,41,0},
    {0x0005,45,42,0}, {0x0001,45,43,0}, {0x5601,46,46,0}
};
struct MqDecoder {
    const uint8_t* data; size_t length, offset = 0;
    uint32_t a = 0x8000, c; unsigned remaining = 0;
    uint8_t byte(size_t at) const { return at < length ? data[at] : 0xff; }
    void byte_in() {
        if (byte(offset) == 0xff && byte(offset + 1) > 0x8f) { c += 0xff00; remaining = 8; }
        else { bool stuffed = byte(offset) == 0xff; offset++;
            c += (uint32_t)byte(offset) << (stuffed ? 9 : 8); remaining = stuffed ? 7 : 8; }
    }
    MqDecoder(const uint8_t* bytes, size_t size) : data(bytes), length(size), c((uint32_t)byte(0) << 16) {
        byte_in(); c <<= 7; remaining -= 7;
    }
    uint8_t decode(uint8_t* context) {
        uint8_t mps = *context & 1, index = *context >> 1, value = mps;
        const MqState& state = mq_states[index]; a -= state.qe;
        if ((c >> 16) < state.qe) {
            if (a < state.qe) index = state.mps;
            else { value ^= 1; mps ^= state.change; index = state.lps; }
            a = state.qe;
        } else {
            c -= (uint32_t)state.qe << 16;
            if (a & 0x8000) return mps;
            if (a < state.qe) { value ^= 1; mps ^= state.change; index = state.lps; }
            else index = state.mps;
        }
        *context = (index << 1) | mps;
        do { if (!remaining) byte_in(); a <<= 1; c <<= 1; remaining--; } while (a < 0x8000);
        return value;
    }
};

bool jbig2_generic(const uint8_t* data, size_t length, int width, int height, ByteBuilder* bitmap) {
    if (!length) return false;
    uint8_t flags = *data++; length--;
    size_t pixels = 0;
    if (!image_extent(width, height, &pixels) || !byte_builder_reserve(bitmap, pixels)) return false;
    if (flags & 1) {
        if (!pdf_decode_fax_samples(data, length, width, height, -1, false, false, false, bitmap)) return false;
        for (size_t i = 0; i < pixels; i++) bitmap->data[i] = bitmap->data[i] < 128;
        return true;
    }
    unsigned kind = (flags >> 1) & 3, adaptive = kind == 0 ? 4 : 1;
    if ((flags & 0xe0) || length < adaptive * 2 + 2) return false;
    struct Point { int x, y; } points[16]; unsigned count = 0;
    // context ordering is arbitrary under T.88 6.2.5.7; keep fixed pixels first, then AT pixels.
    if (kind != 3) for (int x = -1; x <= (kind == 2 ? 1 : kind == 1 ? 2 : 1); x++)
        points[count++] = {x, -2};
    for (int x = kind == 3 ? -3 : -2; x <= (kind == 2 ? 1 : kind == 3 ? 1 : 2); x++)
        points[count++] = {x, -1};
    for (int x = kind == 2 ? -2 : kind == 1 ? -3 : -4; x < 0; x++) points[count++] = {x, 0};
    for (unsigned i = 0; i < adaptive; i++) {
        int x = (int8_t)data[2 * i], y = (int8_t)data[2 * i + 1];
        if (y > 0 || (!y && x >= 0)) return false;
        points[count++] = {x, y};
    }
    data += adaptive * 2; length -= adaptive * 2;
    // Figures 8–11 use the nominal AT positions even when the actual template moves them.
    const uint16_t typical[] = {0b0011100101010011, 0b0011110011010, 0b0011100011, 0b0110001011};
    uint8_t* states = (uint8_t*)mem_calloc(1, (size_t)1 << count, MEM_CAT_INPUT_PDF);
    if (!states) return false;
    memset(bitmap->data, 0, pixels);
    MqDecoder decoder(data, length); uint8_t predicted = 0;
    for (int y = 0; y < height; y++) {
        if (flags & 8) predicted ^= decoder.decode(states + typical[kind]);
        if (predicted) { if (y) memcpy(bitmap->data + (size_t)y * width, bitmap->data + (size_t)(y - 1) * width, width); continue; }
        for (int x = 0; x < width; x++) {
            unsigned context = 0;
            for (unsigned p = 0; p < count; p++) {
                int px = x + points[p].x, py = y + points[p].y;
                context = (context << 1) | (px >= 0 && px < width && py >= 0
                    ? bitmap->data[(size_t)py * width + px] : 0);
            }
            bitmap->data[(size_t)y * width + x] = decoder.decode(states + context);
        }
    }
    mem_free(states);
    return byte_builder_commit(bitmap, pixels);
}
}

bool pdf_decode_jbig2_samples(const uint8_t* data, size_t length, int width, int height,
        ByteBuilder* samples) {
    size_t pixels = 0;
    if (!data || !image_extent(width, height, &pixels) || !byte_builder_reserve(samples, pixels)) return false;
    uint8_t* page = samples->data + samples->length;
    bool has_page = false, override_operator = false; unsigned page_operator = 0;
    while (length) {
        if (length < 6) return false;
        uint32_t number = read_be32(data); uint8_t flags = data[4]; unsigned references = data[5] >> 5;
        size_t header = 6;
        if (references == 7) {
            if (length < 9) return false;
            references = read_be32(data + 5) & 0x1fffffff;
            header = 9 + ((size_t)references + 8) / 8;
        } else if (references > 4) return false;
        if (header > length) return false;
        size_t ref_width = number <= 256 ? 1 : number <= 65536 ? 2 : 4;
        if (references > (length - header) / ref_width) return false;
        header += references * ref_width + (flags & 64 ? 4 : 1);
        if (header > length || length - header < 4) return false;
        uint32_t size = read_be32(data + header); header += 4;
        if (size > length - header) return false;
        const uint8_t* body = data + header; unsigned type = flags & 63;
        if (type == 48) {
            if (has_page || size < 19 || read_be32(body) != (uint32_t)width || read_be32(body + 4) != (uint32_t)height) return false;
            memset(page, (body[16] & 4) ? 1 : 0, pixels);
            page_operator = (body[16] >> 3) & 3; override_operator = body[16] & 64; has_page = true;
        } else if (type == 38 || type == 39) {
            if (!has_page || size < 18 || references) return false;
            uint32_t rw = read_be32(body), rh = read_be32(body + 4), rx = read_be32(body + 8), ry = read_be32(body + 12);
            if (!rw || !rh || rw > INT_MAX || rh > INT_MAX) return false;
            unsigned operation = override_operator ? body[16] & 7 : page_operator;
            if (operation > 4) return false;
            ByteBuilder region = {}; byte_builder_init(&region, 0, MEM_CAT_INPUT_PDF, false);
            bool decoded = jbig2_generic(body + 17, size - 17, (int)rw, (int)rh, &region);
            if (decoded) for (uint32_t y = 0; y < rh && (uint64_t)y + ry < (uint32_t)height; y++)
                for (uint32_t x = 0; x < rw && (uint64_t)x + rx < (uint32_t)width; x++) {
                    uint8_t value = region.data[(size_t)y * rw + x];
                    uint8_t* target = page + ((size_t)y + ry) * width + x + rx;
                    switch (operation) {
                        case 0: *target |= value; break;
                        case 1: *target &= value; break;
                        case 2: *target ^= value; break;
                        case 3: *target = 1 ^ (*target ^ value); break;
                        case 4: *target = value; break;
                    }
                }
            byte_builder_destroy(&region);
            if (!decoded) return false;
        } else if (type != 49 && type != 50 && type != 51 && type != 62 && type != 63) {
            log_debug("PDF_JBIG2_DECODE: unsupported segment type %u", type); return false;
        }
        data += header + size; length -= header + size;
    }
    if (!has_page) return false;
    for (size_t i = 0; i < pixels; i++) page[i] = page[i] ? 0 : 255;
    return byte_builder_commit(samples, pixels);
}
