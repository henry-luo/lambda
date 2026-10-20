#include "image_psd.h"
#include "endian.h"
#include "mem.h"
#include "str.h"
#include <zlib.h>

struct PsdReader {
    const uint8_t* data;
    size_t length, position = 0;
    bool valid = true;
    const uint8_t* take(size_t count) {
        if (!valid || count > length - position) { valid = false; return nullptr; }
        const uint8_t* result = data + position; position += count; return result;
    }
    uint8_t u8() { const uint8_t* p = take(1); return p ? *p : 0; }
    uint16_t u16() { const uint8_t* p = take(2); return p ? read_be16(p) : 0; }
    uint32_t u32() { const uint8_t* p = take(4); return p ? read_be32(p) : 0; }
    int32_t i32() { return (int32_t)u32(); }
    bool signature(const char* expected) { const uint8_t* p = take(4); return p && !memcmp(p, expected, 4); }
    PsdReader section() {
        size_t count = u32(); const uint8_t* p = take(count);
        return {p, p ? count : 0, 0, p != nullptr};
    }
};

PsdDocument* psd_document_create() {
    PsdDocument* doc = (PsdDocument*)mem_calloc(1, sizeof(PsdDocument), MEM_CAT_IMAGE);
    if (!doc) return nullptr;
    doc->pool = pool_create(); doc->layers = arraylist_new(8); doc->owns_rasters = true;
    if (!doc->pool || !doc->layers) { psd_document_destroy(doc); return nullptr; }
    return doc;
}

void psd_document_destroy(PsdDocument* doc) {
    if (!doc) return;
    if (doc->layers) {
        if (doc->owns_rasters) for (int i = 0; i < doc->layers->length; i++) {
            PsdLayer* layer = (PsdLayer*)doc->layers->data[i];
            raster_buffer_destroy(layer->pixels); raster_buffer_destroy(layer->mask);
        }
        arraylist_free(doc->layers);
    }
    if (doc->owns_rasters) raster_buffer_destroy(doc->composite);
    if (doc->pool) pool_destroy(doc->pool);
    mem_free(doc);
}

static bool psd_fail(const char** error, const char* message) { *error = message; return false; }

static RasterBuffer* psd_pixels(PsdDocument* doc, int64_t width, int64_t height, uint8_t channels) {
    if (width < 1 || height < 1 || width > 30000 || height > 30000 || width * height > 16000000)
        return nullptr;
    size_t size = (size_t)width * height * channels;
    if (size > 256 * 1024 * 1024 - doc->decoded_bytes) return nullptr;
    RasterBuffer* buffer = raster_buffer_create((uint32_t)width, (uint32_t)height, channels, 64 * 1024 * 1024);
    if (buffer) doc->decoded_bytes += size;
    return buffer;
}

static char* psd_string(PsdDocument* doc, const char* bytes, size_t length) {
    char* result = (char*)pool_alloc(doc->pool, length + 1);
    if (result) { memcpy(result, bytes, length); result[length] = 0; }
    return result;
}

static bool psd_unicode(PsdDocument* doc, PsdReader* reader, PsdLayer* layer) {
    uint32_t units = reader->u32();
    if (units > 65536 || units > (reader->length - reader->position) / 2) return false;
    StrBuf* text = strbuf_new();
    if (!text) return false;
    bool valid = true;
    for (uint32_t i = 0; i < units && valid; i++) {
        uint32_t codepoint = reader->u16();
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (++i >= units) { valid = false; break; }
            uint32_t low = reader->u16();
            if (low < 0xdc00 || low > 0xdfff) { valid = false; break; }
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) { valid = false; break; }
        valid = strbuf_append_utf8(text, codepoint);
    }
    if (valid) {
        layer->name = psd_string(doc, text->str, text->length);
        layer->name_length = text->length;
        valid = layer->name != nullptr;
    }
    strbuf_free(text);
    return valid && reader->valid;
}

static bool psd_plane(const uint8_t* bytes, size_t length, uint32_t width, uint32_t height, uint8_t* output) {
    if (!bytes || length < 2 || !width || !height) return false;
    uint16_t compression = read_be16(bytes); bytes += 2; length -= 2;
    size_t size = (size_t)width * height;
    if (compression == 0) {
        if (length != size && !(length == size + 1 && bytes[size] == 0)) return false;
        memcpy(output, bytes, size); return true;
    }
    if (compression == 1) {
        if (height > length / 2) return false;
        const uint8_t* counts = bytes;
        bytes += height * 2; length -= height * 2;
        for (uint32_t y = 0; y < height; y++) {
            size_t row_size = read_be16(counts + y * 2), consumed = 0, written = 0;
            if (row_size > length) return false;
            while (consumed < row_size) {
                int8_t n = (int8_t)bytes[consumed++];
                size_t count = n >= 0 ? (size_t)n + 1 : 1 - (int)n;
                if (n == -128) continue;
                if (count > width - written) return false;
                if (n >= 0) {
                    if (count > row_size - consumed) return false;
                    memcpy(output + (size_t)y * width + written, bytes + consumed, count); consumed += count;
                } else {
                    if (consumed == row_size) return false;
                    memset(output + (size_t)y * width + written, bytes[consumed++], count);
                }
                written += count;
            }
            if (written != width) return false;
            bytes += row_size; length -= row_size;
        }
        return length == 0 || (length == 1 && *bytes == 0);
    }
    if (compression != 2 && compression != 3) return false;
    z_stream stream = {};
    stream.next_in = (Bytef*)bytes; stream.avail_in = (uInt)length;
    stream.next_out = output; stream.avail_out = (uInt)size;
    if (inflateInit(&stream) != Z_OK) return false;
    int status = inflate(&stream, Z_FINISH);
    bool complete = status == Z_STREAM_END && stream.total_out == size &&
        (stream.avail_in == 0 || (stream.avail_in == 1 && *stream.next_in == 0));
    inflateEnd(&stream);
    if (!complete) return false;
    if (compression == 3) for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 1; x < width; x++) output[(size_t)y * width + x] += output[(size_t)y * width + x - 1];
    return true;
}

static bool psd_record(PsdDocument* doc, PsdReader* reader, PsdLayer* layer) {
    layer->top = reader->i32(); layer->left = reader->i32();
    layer->bottom = reader->i32(); layer->right = reader->i32();
    int64_t width = (int64_t)layer->right - layer->left, height = (int64_t)layer->bottom - layer->top;
    if (width < 0 || height < 0 || width > 30000 || height > 30000) return false;
    layer->channel_count = reader->u16();
    if (layer->channel_count > 56) return false;
    for (uint16_t i = 0; i < layer->channel_count; i++) {
        layer->channels[i].id = (int16_t)reader->u16(); layer->channels[i].length = reader->u32();
        for (uint16_t previous = 0; previous < i; previous++)
            if (layer->channels[previous].id == layer->channels[i].id) return false;
        if (layer->channels[i].id < -2 || layer->channels[i].id > (doc->mode == 1 ? 0 : 2))
            layer->unsupported = true;
    }
    if (!reader->signature("8BIM")) return false;
    const uint8_t* blend = reader->take(4);
    if (!blend) return false;
    memcpy(layer->blend, blend, 4);
    layer->opacity = reader->u8(); layer->clipping = reader->u8(); layer->flags = reader->u8(); reader->u8();
    PsdReader extra = reader->section();
    PsdReader mask = extra.section();
    if (mask.length) {
        if (mask.length < 18) return false;
        layer->mask_top = mask.i32(); layer->mask_left = mask.i32();
        layer->mask_bottom = mask.i32(); layer->mask_right = mask.i32();
        layer->mask_default = mask.u8(); layer->mask_flags = mask.u8();
        if (layer->mask_flags & 16) layer->unsupported = true;
    }
    PsdReader ranges = extra.section();
    if (!ranges.valid) return false;
    for (size_t i = 0; i + 4 <= ranges.length; i += 4)
        if (memcmp(ranges.data + i, "\0\0\xff\xff", 4)) layer->unsupported = true;
    size_t name_size = extra.u8();
    const uint8_t* name = extra.take(name_size);
    if (!name) return false;
    layer->name = psd_string(doc, (const char*)name, name_size); layer->name_length = name_size;
    extra.take((4 - ((name_size + 1) % 4)) % 4);
    while (extra.valid && extra.length - extra.position >= 12) {
        if (!extra.signature("8BIM")) return false;
        const uint8_t* key = extra.take(4);
        PsdReader block = extra.section();
        if (!key || !block.valid) return false;
        if (!memcmp(key, "luni", 4)) { if (!psd_unicode(doc, &block, layer)) return false; }
        else if (!memcmp(key, "lyid", 4)) layer->id = block.u32();
        else if (!memcmp(key, "lsct", 4) || !memcmp(key, "lsdk", 4)) {
            layer->section = block.u32();
            if (block.length >= 12) {
                if (!block.signature("8BIM")) return false;
                const uint8_t* mode = block.take(4); memcpy(layer->blend, mode, 4);
            }
        } else if (memcmp(key, "lclr", 4) && memcmp(key, "lnsr", 4) && memcmp(key, "lspf", 4) &&
                memcmp(key, "clbl", 4) && memcmp(key, "infx", 4)) layer->unsupported = true;
        if (!block.valid) return false;
        if (block.length & 1) extra.take(1);
    }
    // group mask/clipping coverage is outside this profile; never silently omit it.
    if (layer->section && (mask.length || layer->clipping)) layer->unsupported = true;
    if (layer->section > 3) return false;
    return reader->valid && extra.valid && layer->name;
}

static bool psd_layer_pixels(PsdDocument* doc, PsdReader* reader, PsdLayer* layer) {
    int64_t width = (int64_t)layer->right - layer->left, height = (int64_t)layer->bottom - layer->top;
    if (!layer->section && width && height) {
        layer->pixels = psd_pixels(doc, width, height, 4);
        if (!layer->pixels) return false;
        for (size_t p = 3; p < layer->pixels->size; p += 4) layer->pixels->pixels[p] = 255;
    }
    for (uint16_t c = 0; c < layer->channel_count; c++) {
        PsdChannel channel = layer->channels[c];
        const uint8_t* bytes = reader->take(channel.length);
        if (!bytes || channel.length < 2) return false;
        bool is_mask = channel.id == -2;
        int64_t w = is_mask ? (int64_t)layer->mask_right - layer->mask_left : width;
        int64_t h = is_mask ? (int64_t)layer->mask_bottom - layer->mask_top : height;
        if (channel.id == -3) { layer->unsupported = true; continue; }
        if (w < 0 || h < 0 || w > 30000 || h > 30000) return false;
        if (!w || !h || layer->section) continue;
        if (w * h > 16000000) return false;
        uint8_t* plane = (uint8_t*)mem_alloc((size_t)w * h, MEM_CAT_IMAGE);
        if (!plane) return false;
        bool valid = psd_plane(bytes, channel.length, (uint32_t)w, (uint32_t)h, plane);
        if (valid && is_mask) {
            if (layer->mask) { mem_free(plane); return false; }
            layer->mask = psd_pixels(doc, w, h, 1);
            valid = layer->mask != nullptr;
            if (valid) memcpy(layer->mask->pixels, plane, (size_t)w * h);
        } else if (valid && layer->pixels && channel.id >= -1 && channel.id <= 2) {
            uint8_t offset = channel.id == -1 ? 3 : (uint8_t)channel.id;
            for (size_t p = 0; p < (size_t)w * h; p++) {
                layer->pixels->pixels[p * 4 + offset] = plane[p];
                if (doc->mode == 1 && channel.id == 0)
                    layer->pixels->pixels[p * 4 + 1] = layer->pixels->pixels[p * 4 + 2] = plane[p];
            }
        }
        mem_free(plane);
        if (!valid) return false;
    }
    return reader->valid;
}

bool psd_read(PsdDocument* doc, const uint8_t* bytes, size_t length, const char** error) {
    if (!doc || !bytes || length < 26 || length > 128 * 1024 * 1024)
        return psd_fail(error, "PSD source is truncated or exceeds the byte limit");
    PsdReader reader = {bytes, length};
    if (!reader.signature("8BPS") || reader.u16() != 1) return psd_fail(error, "unsupported PSD signature/version (PSB is separate scope)");
    const uint8_t* reserved = reader.take(6);
    if (!reserved || memcmp(reserved, "\0\0\0\0\0\0", 6)) return psd_fail(error, "invalid PSD reserved header");
    doc->channels = reader.u16(); doc->height = reader.u32(); doc->width = reader.u32();
    doc->depth = reader.u16(); doc->mode = reader.u16();
    if (!doc->width || !doc->height || doc->width > 30000 || doc->height > 30000 ||
            (uint64_t)doc->width * doc->height > 16000000 || !doc->channels || doc->channels > 56)
        return psd_fail(error, "invalid PSD dimensions or channel count");
    if (doc->depth != 8 || (doc->mode != 1 && doc->mode != 3))
        return psd_fail(error, "PSD editable profile supports 8-bit RGB/grayscale");
    PsdReader color = reader.section(), resources = reader.section();
    if (!color.valid || !resources.valid) return psd_fail(error, "truncated PSD color/resources section");
    bool real_merged = true;
    while (resources.valid && resources.position < resources.length) {
        if (!resources.signature("8BIM")) return psd_fail(error, "invalid PSD image resource signature");
        uint16_t id = resources.u16();
        size_t name = resources.u8(); resources.take(name);
        if ((name + 1) & 1) resources.take(1);
        PsdReader data = resources.section();
        if (!data.valid) return psd_fail(error, "truncated PSD image resource");
        if (id == 1039) { doc->profile = data.data; doc->profile_length = data.length; }
        if (id == 1057 && data.length >= 5) real_merged = data.data[4] != 0;
        if (data.length & 1) resources.take(1);
    }
    PsdReader layer_mask = reader.section();
    if (!reader.valid || !resources.valid || !layer_mask.valid) return psd_fail(error, "truncated PSD layer section");
    if (layer_mask.length) {
        PsdReader layers = layer_mask.section();
        if (!layers.valid) return psd_fail(error, "truncated PSD layer information");
        if (layers.length) {
            int32_t count = (int16_t)layers.u16();
            if (count < 0) { count = -count; doc->merged_transparency = true; }
            if (count > 4096) return psd_fail(error, "PSD layer count exceeds the limit");
            for (int32_t i = 0; i < count; i++) {
                PsdLayer* layer = (PsdLayer*)pool_calloc(doc->pool, sizeof(PsdLayer));
                if (!layer || !arraylist_append(doc->layers, layer) || !psd_record(doc, &layers, layer))
                    return psd_fail(error, "invalid or truncated PSD layer record");
            }
            for (int32_t i = 0; i < count; i++)
                if (!psd_layer_pixels(doc, &layers, (PsdLayer*)doc->layers->data[i]))
                    return psd_fail(error, "invalid PSD channel/mask data or decoded budget exceeded");
        }
        if (layer_mask.length - layer_mask.position >= 4) {
            PsdReader global_mask = layer_mask.section();
            if (!global_mask.valid) return psd_fail(error, "truncated PSD global mask");
            if (global_mask.length || layer_mask.position != layer_mask.length) doc->unsupported = true;
        }
    }
    if (reader.position < reader.length && real_merged) {
        size_t size = (size_t)doc->width * doc->height * doc->channels;
        if (size > 64 * 1024 * 1024) return psd_fail(error, "PSD composite exceeds the sample budget");
        uint8_t* planar = (uint8_t*)mem_alloc(size, MEM_CAT_IMAGE);
        if (!planar) return psd_fail(error, "cannot allocate PSD composite planes");
        bool valid = psd_plane(reader.data + reader.position, reader.length - reader.position,
            doc->width, doc->height * doc->channels, planar);
        if (valid) {
            doc->composite = psd_pixels(doc, doc->width, doc->height, 4);
            valid = doc->composite != nullptr;
        }
        if (valid) {
            size_t plane_size = (size_t)doc->width * doc->height;
            unsigned colors = doc->mode == 1 ? 1 : 3;
            if (doc->channels < colors) valid = false;
            else for (size_t p = 0; p < plane_size; p++) {
                for (unsigned c = 0; c < 3; c++) doc->composite->pixels[p * 4 + c] = planar[(doc->mode == 1 ? 0 : c) * plane_size + p];
                doc->composite->pixels[p * 4 + 3] = doc->merged_transparency && doc->channels > colors ? planar[colors * plane_size + p] : 255;
            }
        }
        mem_free(planar);
        if (!valid) return psd_fail(error, "invalid PSD merged image data");
    }
    if (!doc->layers->length && !doc->composite) return psd_fail(error, "PSD has neither layers nor a valid merged image");
    return true;
}

struct PsdWriter {
    StrBuf* output;
    bool valid = true;
    void bytes(const void* data, size_t size) {
        if (!valid || size > 128 * 1024 * 1024 - output->length ||
                !strbuf_ensure_cap(output, output->length + size + 1)) { valid = false; return; }
        if (size) memcpy(output->str + output->length, data, size);
        output->length += size; output->str[output->length] = 0;
    }
    void u8(uint8_t n) { bytes(&n, 1); }
    void u16(uint16_t n) { uint8_t data[2]; write_be16(data, n); bytes(data, 2); }
    void u32(uint32_t n) { uint8_t data[4]; write_be32(data, n); bytes(data, 4); }
    size_t section() { size_t offset = output->length; u32(0); return offset; }
    void finish(size_t offset, bool even = false) {
        if (even && ((output->length - offset - 4) & 1)) u8(0);
        if (valid) write_be32((uint8_t*)output->str + offset, (uint32_t)(output->length - offset - 4));
    }
};

static bool psd_write_unicode(PsdWriter* writer, const char* text, size_t length) {
    size_t count_offset = writer->section();
    uint32_t units = 0;
    for (size_t i = 0; i < length;) {
        uint32_t cp;
        int size = str_utf8_decode(text + i, length - i, &cp);
        if (size <= 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += (size_t)size;
        if (cp < 0x10000) { writer->u16((uint16_t)cp); units++; }
        else { cp -= 0x10000; writer->u16((uint16_t)(0xd800 + (cp >> 10))); writer->u16((uint16_t)(0xdc00 + (cp & 1023))); units += 2; }
        if (units > 65536) return false;
    }
    if (writer->valid) write_be32((uint8_t*)writer->output->str + count_offset, units);
    return writer->valid;
}

static void psd_write_plane(PsdWriter* writer, const RasterBuffer* buffer, unsigned channel) {
    writer->u16(0);
    if (!buffer) return;
    size_t pixels = (size_t)buffer->width * buffer->height;
    for (size_t p = 0; p < pixels; p++) {
        const uint8_t* sample = raster_buffer_pixel(buffer, (uint32_t)(p % buffer->width), (uint32_t)(p / buffer->width));
        uint8_t n;
        if (channel == 3) n = buffer->channels == 2 || buffer->channels == 4 ? sample[buffer->channels - 1] : 255;
        else n = sample[buffer->channels <= 2 ? 0 : channel];
        writer->u8(n);
    }
}

bool psd_write(const PsdDocument* doc, StrBuf* output, const char** error) {
    if (!doc || !output || !doc->width || !doc->height || doc->width > 30000 || doc->height > 30000 ||
            !doc->composite || doc->composite->channels != 4 ||
            doc->composite->width != doc->width || doc->composite->height != doc->height || !doc->layers ||
            doc->layers->length < 1 || doc->layers->length > 4096 || doc->unsupported)
        return psd_fail(error, "PSD writer requires a supported layered RGB8 document and fresh composite");
    PsdWriter writer = {output};
    writer.bytes("8BPS", 4); writer.u16(1); writer.bytes("\0\0\0\0\0\0", 6);
    writer.u16(4); writer.u32(doc->height); writer.u32(doc->width); writer.u16(8); writer.u16(3);
    writer.u32(0);
    size_t resources = writer.section();
    if (doc->profile_length) {
        writer.bytes("8BIM", 4); writer.u16(1039); writer.u16(0);
        size_t profile = writer.section(); writer.bytes(doc->profile, doc->profile_length); writer.finish(profile);
        if (doc->profile_length & 1) writer.u8(0);
    }
    writer.bytes("8BIM", 4); writer.u16(1057); writer.u16(0);
    size_t version = writer.section(); writer.u32(1); writer.u8(1);
    if (!psd_write_unicode(&writer, "Lambda", 6) || !psd_write_unicode(&writer, "Lambda", 6))
        return psd_fail(error, "cannot encode PSD version information");
    writer.u32(1); writer.finish(version); if ((output->length - version - 4) & 1) writer.u8(0);
    writer.finish(resources);
    size_t section = writer.section(), layers = writer.section();
    writer.u16((uint16_t)-(int16_t)doc->layers->length);
    for (int i = 0; i < doc->layers->length; i++) {
        const PsdLayer* layer = (const PsdLayer*)doc->layers->data[i];
        if (layer->unsupported || layer->clipping || strcmp(layer->blend, "norm"))
            return psd_fail(error, "unsupported PSD layer feature requires explicit rasterization");
        const RasterBuffer* pixels = layer->pixels;
        const RasterBuffer* mask = layer->mask;
        if ((pixels && (pixels->width > 30000 || pixels->height > 30000 ||
                (int64_t)layer->right - layer->left != pixels->width ||
                (int64_t)layer->bottom - layer->top != pixels->height)) ||
                (mask && (mask->channels != 1 || mask->width > 30000 || mask->height > 30000 ||
                (int64_t)layer->mask_right - layer->mask_left != mask->width ||
                (int64_t)layer->mask_bottom - layer->mask_top != mask->height)))
            return psd_fail(error, "PSD sample extent is outside the editable profile or disagrees with its bounds");
        uint32_t plane_size = pixels ? pixels->width * pixels->height : 0;
        writer.u32((uint32_t)layer->top); writer.u32((uint32_t)layer->left);
        writer.u32((uint32_t)layer->bottom); writer.u32((uint32_t)layer->right);
        writer.u16((uint16_t)((pixels ? 4 : 0) + (layer->mask ? 1 : 0)));
        if (pixels) for (int16_t c = 0; c < 4; c++) {
            writer.u16((uint16_t)(c == 3 ? -1 : c)); writer.u32(plane_size + 2);
        }
        if (layer->mask) { writer.u16((uint16_t)-2); writer.u32((uint32_t)layer->mask->size + 2); }
        writer.bytes("8BIMnorm", 8); writer.u8(layer->opacity); writer.u8(0);
        writer.u8((uint8_t)(8 | (layer->flags & 2) | (layer->section ? 16 : 0))); writer.u8(0);
        size_t extra = writer.section();
        if (layer->mask) {
            writer.u32(20); writer.u32((uint32_t)layer->mask_top); writer.u32((uint32_t)layer->mask_left);
            writer.u32((uint32_t)layer->mask_bottom); writer.u32((uint32_t)layer->mask_right);
            writer.u8(layer->mask_default); writer.u8(layer->mask_flags); writer.u16(0);
        } else writer.u32(0);
        writer.u32(0);
        size_t name_length = layer->name_length < 255 ? layer->name_length : 255;
        writer.u8((uint8_t)name_length);
        for (size_t n = 0; n < name_length; n++) writer.u8((uint8_t)layer->name[n] < 128 ? layer->name[n] : '?');
        for (size_t n = name_length + 1; n % 4; n++) writer.u8(0);
        writer.bytes("8BIMluni", 8); size_t unicode = writer.section();
        if (!psd_write_unicode(&writer, layer->name ? layer->name : "", layer->name_length))
            return psd_fail(error, "PSD layer name is not valid UTF-8");
        writer.finish(unicode);
        writer.bytes("8BIMlyid", 8); writer.u32(4); writer.u32((uint32_t)i + 1);
        if (layer->section) {
            writer.bytes("8BIMlsct", 8); writer.u32(layer->section == 3 ? 4 : 12); writer.u32(layer->section);
            if (layer->section != 3) writer.bytes("8BIMnorm", 8);
        }
        writer.finish(extra);
    }
    for (int i = 0; i < doc->layers->length; i++) {
        const PsdLayer* layer = (const PsdLayer*)doc->layers->data[i];
        if (layer->pixels) for (unsigned c = 0; c < 4; c++) psd_write_plane(&writer, layer->pixels, c);
        if (layer->mask) psd_write_plane(&writer, layer->mask, 0);
    }
    writer.finish(layers, true); writer.u32(0); writer.finish(section);
    // composite has one shared compression word followed by planar samples.
    writer.u16(0);
    size_t plane_size = (size_t)doc->width * doc->height;
    for (unsigned c = 0; c < 4; c++) for (size_t p = 0; p < plane_size; p++)
        writer.u8(doc->composite->pixels[p * 4 + c]);
    return writer.valid || psd_fail(error, "PSD output exceeds the byte/allocation limit");
}
