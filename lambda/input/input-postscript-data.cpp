#include "input-postscript.hpp"
#include "input-pdf-image.hpp"
#include "pdf_decompress.h"
#include "../core/mark_reader.hpp"
#include "../../lib/hex.h"
#include "../../lib/mem.h"
#include <limits.h>

bool postscript_byte_view(Item source, const char** bytes, size_t* length, ByteBuilder* scratch) {
    if (Binary* binary = source.get_safe_binary()) {
        *bytes = (const char*)binary_data(binary); *length = binary_length(binary); return true;
    }
    if (String* string = source.get_safe_string()) {
        *bytes = string->chars; *length = string->len; return true;
    }
    ItemReader reader = MarkReader(source).getRoot();
    if (!reader.isArray()) return false;
    ArrayReader array = reader.asArray();
    if (array.length() > 64 * 1024 * 1024 ||
        !byte_builder_init(scratch, array.length() + 1, MEM_CAT_INPUT_PDF, false)) return false;
    for (int64_t i = 0; i < array.length(); i++) {
        ItemReader value = array.get(i);
        if (!value.isInt() || value.asInt() < 0 || value.asInt() > 255) {
            byte_builder_destroy(scratch); return false;
        }
        uint8_t byte = (uint8_t)value.asInt();
        if (!byte_builder_append(scratch, &byte, 1)) { byte_builder_destroy(scratch); return false; }
    }
    *bytes = (const char*)scratch->data; *length = scratch->length; return true;
}

Item postscript_read_io(Input* owner, Item source, size_t offset, Item options) {
    if (!owner) return ItemNull;
    MarkBuilder builder(owner); MapReader request(options.get_safe_map());
    const char* kind = request.get("kind").cstring();
    int64_t count = request.get("count").isInt() ? request.get("count").asInt() : 0;
    int64_t limit = request.get("limit").isInt() ? request.get("limit").asInt() : 1048576;
    ByteBuilder scratch = {}; const char* bytes = nullptr; size_t length = 0;
    if (!kind || count < 0 || limit < 1 || limit > 64 * 1024 * 1024 || count > limit ||
        !postscript_byte_view(source, &bytes, &length, &scratch)) return ItemNull;
    if (request.get("end").isInt() && request.get("end").asInt() >= 0 &&
        (size_t)request.get("end").asInt() < length) length = request.get("end").asInt();
    StrBuf* out = strbuf_new(); size_t consumed = 0;
    bool ok = out && offset <= length, complete = false;
    const char* error = "ioerror: invalid byte read";
    if (ok) {
        const char* p = bytes + offset; size_t remaining = length - offset;
        if (!strcmp(kind, "ASCII85Decode")) {
            ok = ascii85_decode_bounded(p, remaining, out, limit, true, &consumed); complete = ok;
        } else if (!strcmp(kind, "ASCIIHexDecode")) {
            ok = asciihex_decode_bounded(p, remaining, out, limit, true, true, &consumed); complete = ok;
        } else if (!strcmp(kind, "RunLengthDecode")) {
            ok = runlength_decode_bounded(p, remaining, out, limit, true, true, &consumed); complete = ok;
        } else if (!strcmp(kind, "raw") || !strcmp(kind, "line") || !strcmp(kind, "hex")) {
            int high = -1;
            while (ok && consumed < remaining && out->length < (size_t)count) {
                unsigned char c = p[consumed++];
                if (!strcmp(kind, "line") && (c == '\r' || c == '\n')) {
                    if (c == '\r' && consumed < remaining && p[consumed] == '\n') consumed++;
                    complete = true; break;
                }
                if (!strcmp(kind, "hex")) {
                    if (ps_is_space(c)) continue;
                    int digit = hex_decode_byte(c);
                    if (digit < 0) { ok = false; break; }
                    if (high < 0) { high = digit; continue; }
                    c = (unsigned char)((high << 4) | digit); high = -1;
                }
                if (!strbuf_ensure_cap(out, out->length + 2)) { ok = false; break; }
                strbuf_append_char(out, (char)c);
            }
            if (!strcmp(kind, "hex") && high >= 0) strbuf_append_char(out, (char)(high << 4));
            if (!strcmp(kind, "line")) {
                // an exact-capacity line is valid if its terminator follows the final byte.
                if (!complete && out->length == (size_t)count && consumed < remaining) {
                    if (p[consumed] == '\r' || p[consumed] == '\n') {
                        if (p[consumed++] == '\r' && consumed < remaining && p[consumed] == '\n') consumed++;
                        complete = true;
                    } else { error = "rangecheck: line exceeds destination string"; ok = false; }
                }
            } else complete = out->length == (size_t)count;
        } else ok = false;
    }
    MapBuilder result = builder.map();
    if (ok) {
        ArrayBuilder data = builder.array();
        for (size_t i = 0; i < out->length; i++) data.append(builder.createInt((unsigned char)out->str[i]));
        result.put("bytes", data.final()); result.put("next", builder.createInt(offset + consumed));
        result.put("complete", complete);
    } else result.put("error", error);
    if (out) strbuf_free(out);
    byte_builder_destroy(&scratch);
    return result.final();
}

Item postscript_png_io(Input* owner, Item planes_item, Item geometry, Item limits) {
    if (!owner) return ItemNull;
    MapReader shape(geometry.get_safe_map()), caps(limits.get_safe_map());
    int64_t w = shape.get("width").asInt(), h = shape.get("height").asInt();
    int64_t bits = shape.get("bits").asInt(), components = shape.get("components").asInt();
    int64_t max_pixels = caps.get("max_image_pixels").asInt();
    if (w <= 0 || h <= 0 || w > INT_MAX || h > INT_MAX || max_pixels <= 0 || w > max_pixels / h ||
        (uint64_t)w > (64 * 1024 * 1024) / ((uint64_t)h * 4) ||
        !(bits == 1 || bits == 2 || bits == 4 || bits == 8) ||
        !(components == 1 || components == 3 || components == 4)) return ItemNull;
    ArrayReader planes = ArrayReader::fromItem(planes_item);
    bool planar = planes.length() == components;
    if (planes.length() != 1 && !planar) return ItemNull;
    ByteBuilder storage[4] = {}; const char* data[4] = {}; size_t lengths[4] = {};
    size_t row = ((size_t)w * bits * (planar ? 1 : components) + 7) / 8;
    bool valid = true;
    for (int64_t c = 0; valid && c < planes.length(); c++)
        valid = postscript_byte_view(planes.get(c).item(), data + c, lengths + c, storage + c) && lengths[c] >= row * h;
    uint8_t* rgba = valid ? (uint8_t*)mem_alloc((size_t)w * h * 4, MEM_CAT_INPUT_PDF) : nullptr;
    // samples are packed per row; planar channels have independent row padding.
    if (rgba) for (int64_t y = 0; y < h; y++) for (int64_t x = 0; x < w; x++) {
        uint8_t values[4] = {};
        for (int64_t c = 0; c < components; c++) {
            size_t bit = ((size_t)x * (planar ? 1 : components) + (planar ? 0 : c)) * bits;
            unsigned raw = ((unsigned char)data[planar ? c : 0][y * row + bit / 8] >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
            values[c] = (uint8_t)(raw * 255 / ((1u << bits) - 1));
        }
        uint8_t* pixel = rgba + ((size_t)y * w + x) * 4;
        for (int c = 0; c < 3; c++) pixel[c] = components == 1 ? values[0] : components == 3 ? values[c] :
            (uint8_t)((255 - values[c]) * (255 - values[3]) / 255);
        pixel[3] = 255;
    }
    String* uri = rgba ? pdf_rgba_image_data_uri(owner, rgba, (int)w, (int)h) : nullptr;
    if (rgba) mem_free(rgba);
    for (int i = 0; i < 4; i++) byte_builder_destroy(storage + i);
    return uri ? Item{.item = s2it(uri)} : ItemNull;
}
