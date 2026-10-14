#include "input.hpp"
#include "input-parsers.h"
#include "input-postscript.hpp"
#include "input-context.hpp"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include <math.h>

using namespace lambda;

namespace {
const unsigned char DOS_MAGIC[] = {0xc5, 0xd0, 0xd3, 0xc6};
constexpr size_t EPS_MAX_SOURCE_BYTES = 64u * 1024u * 1024u;
constexpr size_t PS_HEADER_LENGTH = sizeof("%!PS-Adobe-") - 1;

uint32_t little_u32(const char* p) {
    const unsigned char* b = (const unsigned char*)p;
    return b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

bool valid_span(size_t offset, size_t count, size_t size) {
    return offset <= size && count <= size - offset;
}

const char* after_line(const char* p, const char* end) {
    p = ps_skip_comment(p, end);
    if (p < end && *p == '\r') p++;
    if (p < end && *p == '\n') p++;
    return p;
}

bool starts(const char* p, const char* end, const char* text) {
    return str_starts_with_const(p, end - p, text);
}

bool read_box(const char* p, const char* end, double* values) {
    for (size_t i = 0; i < 4; i++) {
        p = strn_skip_chars(p, end, " \t");
        const char* stop = nullptr;
        if (!str_to_double(p, end - p, values + i, &stop) || stop == p || !isfinite(values[i])) return false;
        p = stop;
    }
    p = strn_skip_chars(p, end, " \t");
    return p == end && values[2] > values[0] && values[3] > values[1];
}

Item box_item(MarkBuilder& builder, const double* values) {
    ArrayBuilder array = builder.array();
    for (size_t i = 0; i < 4; i++) array.append(builder.createFloat(values[i]));
    return array.final();
}
}

bool eps_source_expected(const char* path, const char* bytes, size_t length) {
    if (path && (str_iends_with_const(path, strlen(path), ".eps") ||
                 str_iends_with_const(path, strlen(path), ".epsi"))) return true;
    if (!bytes) return false;
    if (length >= 4 && !memcmp(bytes, DOS_MAGIC, 4)) return true;
    const char* line_end = ps_skip_comment(bytes, bytes + length);
    return starts(bytes, line_end, "%!PS-Adobe-") &&
        str_find(bytes, line_end - bytes, " EPSF-", 6) != STR_NPOS;
}

void parse_eps(Input* input, const char* bytes, size_t length) {
    if (!input) return;
    InputContext ctx(input);
    auto fail = [&](const char* message) {
        ctx.addErrorCode(SourceLocation(), "eps_parse", "%s", message);
        ctx.logErrors();
        input->root = ItemNull;
    };
    if (!bytes || !length || length > EPS_MAX_SOURCE_BYTES) { fail("EPS source is empty or exceeds the 64 MiB input limit"); return; }
    size_t offset = 0, count = length;
    ArrayBuilder previews = ctx.builder.array();
    if (length >= 4 && !memcmp(bytes, DOS_MAGIC, 4)) {
        if (length < 30) { fail("truncated DOS EPS header"); return; }
        uint16_t checksum = (unsigned char)bytes[28] | ((uint16_t)(unsigned char)bytes[29] << 8);
        uint16_t actual = 0;
        for (size_t i = 0; i < 28; i += 2)
            actual ^= (unsigned char)bytes[i] | ((uint16_t)(unsigned char)bytes[i + 1] << 8);
        if (checksum != 0xffff && checksum != actual) { fail("invalid DOS EPS checksum"); return; }
        offset = little_u32(bytes + 4); count = little_u32(bytes + 8);
        if (offset < 30 || !count || !valid_span(offset, count, length)) { fail("invalid DOS EPS PostScript section"); return; }
        const char* kinds[] = {"wmf", "tiff"};
        for (size_t i = 0; i < 2; i++) {
            size_t start = little_u32(bytes + 12 + i * 8), size = little_u32(bytes + 16 + i * 8);
            if (!start && !size) continue;
            if (start < 30 || !size || !valid_span(start, size, length) ||
                (start < offset + count && offset < start + size)) { fail("invalid or overlapping DOS EPS preview section"); return; }
            MapBuilder preview = ctx.builder.map();
            preview.put("kind", kinds[i]); preview.put("offset", ctx.builder.createInt(start));
            preview.put("length", ctx.builder.createInt(size)); previews.append(preview.final());
        }
    }
    const char* begin = bytes + offset;
    const char* end = begin + count;
    const char* header_end = ps_skip_comment(begin, end);
    size_t eps_marker = str_find(begin, header_end - begin, " EPSF-", 6);
    if (!starts(begin, header_end, "%!PS-Adobe-") || eps_marker == STR_NPOS) { fail("expected a PS-Adobe EPSF header"); return; }
    const char* version_end = nullptr;
    double ps_version = 0, eps_version = 0;
    if (!str_to_double(begin + PS_HEADER_LENGTH, eps_marker - PS_HEADER_LENGTH, &ps_version, &version_end) ||
        version_end != begin + eps_marker || !isfinite(ps_version) || ps_version <= 0 ||
        !str_to_double(begin + eps_marker + 6, header_end - begin - eps_marker - 6, &eps_version, &version_end) ||
        strn_skip_chars(version_end, header_end, " \t") != header_end || !isfinite(eps_version) || eps_version <= 0) {
        fail("invalid EPS header version"); return;
    }
    MarkBuilder& builder = ctx.builder;
    MapBuilder metadata = builder.map();
    ArrayBuilder sections = builder.array();
    double box[4] = {}, hires[4] = {};
    bool have_box = false, have_hires = false, box_pending = false, hires_pending = false;
    bool trailer = false, header = true;
    size_t documents = 0, procedures = 0;
    int64_t language = 1;
    StrBuf* scratch = strbuf_new();
    if (!scratch) { fail("EPS scratch allocation failed"); return; }
    for (const char* p = after_line(begin, end); p < end;) {
        if (*p == '(') {
            PsStringResult r = ps_read_literal(p, end, scratch, EPS_MAX_SOURCE_BYTES, true);
            if (!r.terminated) { strbuf_free(scratch); fail("unterminated EPS string"); return; }
            p = r.next; continue;
        }
        if (*p == '<' && p + 1 < end && p[1] != '<') {
            p++;
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }
        if (*p == '<' && p + 1 < end && p[1] == '<') { p += 2; continue; }
        if (*p == '{') procedures++;
        if (*p == '}' && procedures) procedures--;
        if (*p != '%') { p++; continue; }
        const char* line_end = ps_skip_comment(p, end);
        const char* next = after_line(line_end, end);
        bool line_start = p == begin || p[-1] == '\n' || p[-1] == '\r';
        if (!line_start || procedures) { p = next; continue; }
        if (starts(p, line_end, "%%BeginDocument:")) documents++;
        else if (starts(p, line_end, "%%EndDocument") && documents) documents--;
        else if (starts(p, line_end, "%%BeginData:") || starts(p, line_end, "%%BeginBinary:")) {
            const char* number = p + (starts(p, line_end, "%%BeginData:") ? 12 : 14);
            number = strn_skip_chars(number, line_end, " \t");
            uint64_t size = 0; const char* stop = nullptr;
            if (!str_to_uint64(number, line_end - number, &size, &stop)) {
                strbuf_free(scratch); fail("invalid DSC data count"); return;
            }
            bool lines = str_find(stop, line_end - stop, "Lines", 5) != STR_NPOS;
            if (lines) {
                for (uint64_t i = 0; i < size; i++) {
                    if (next == end) { strbuf_free(scratch); fail("truncated DSC line data"); return; }
                    next = after_line(next, end);
                }
            } else {
                if (size > (uint64_t)(end - next)) { strbuf_free(scratch); fail("truncated DSC byte data"); return; }
                next += (size_t)size;
            }
        } else if (!documents) {
            if (starts(p, line_end, "%%Trailer")) { trailer = true; header = false; }
            if (starts(p, line_end, "%%EndComments") || starts(p, line_end, "%%BeginProlog") ||
                starts(p, line_end, "%%Page:")) header = false;
            if (starts(p, line_end, "%%BoundingBox:") || starts(p, line_end, "%%HiResBoundingBox:")) {
                bool precise = starts(p, line_end, "%%HiResBoundingBox:");
                const char* value = strn_skip_chars(p + (precise ? 19 : 14), line_end, " \t");
                bool& pending = precise ? hires_pending : box_pending;
                bool& found = precise ? have_hires : have_box;
                if (header && starts(value, line_end, "(atend)")) pending = true;
                else if (header || (trailer && pending)) {
                    if (!read_box(value, line_end, precise ? hires : box)) {
                        strbuf_free(scratch); fail("invalid EPS bounding box"); return;
                    }
                    found = true; pending = false;
                }
            }
            if (header && starts(p, line_end, "%%LanguageLevel:")) {
                const char* value = strn_skip_chars(p + 16, line_end, " \t");
                if (!str_to_int64(value, line_end - value, &language, nullptr) || language < 1 || language > 3) {
                    strbuf_free(scratch); fail("invalid EPS language level"); return;
                }
            }
            const char* keys[] = {"Title", "Creator", "CreationDate", "DocumentNeededResources", "DocumentSuppliedResources"};
            for (const char* key : keys) {
                size_t n = strlen(key);
                if (line_end - p >= (ptrdiff_t)(n + 3) && p[0] == '%' && p[1] == '%' &&
                    !memcmp(p + 2, key, n) && p[n + 2] == ':') {
                    const char* value = strn_skip_chars(p + n + 3, line_end, " \t");
                    // retain undecodable DSC bytes rather than guessing a character encoding.
                    metadata.put(key, utf8_valid(value, line_end - value)
                        ? builder.createStringItem(value, line_end - value)
                        : Item{.item = x2it(builder.createBinary(value, line_end - value))});
                }
            }
            if (starts(p, line_end, "%%Begin") || starts(p, line_end, "%%Trailer")) {
                MapBuilder section = builder.map();
                section.put("offset", builder.createInt(p - bytes));
                section.put("length", builder.createInt(line_end - p));
                sections.append(section.final());
            }
        }
        p = next;
    }
    strbuf_free(scratch);
    if (documents) { fail("unterminated DSC embedded document"); return; }
    if (!have_box && !box_pending) { fail("EPS requires BoundingBox metadata"); return; }
    MapBuilder program = builder.map();
    program.put("offset", builder.createInt(offset)); program.put("length", builder.createInt(count));
    MapBuilder root = builder.map();
    root.put("format", "eps"); root.put("schema_version", 1);
    root.put("eps_version", builder.createStringItem(begin + eps_marker + 6, header_end - begin - eps_marker - 6));
    root.put("language_level", builder.createInt(language));
    root.put("source", {.item = x2it(builder.createBinary(bytes, length))});
    root.put("program", program.final());
    root.put("bbox", have_box ? box_item(builder, box) : ItemNull);
    root.put("hires_bbox", have_hires ? box_item(builder, hires) : ItemNull);
    root.put("bbox_pending", box_pending || hires_pending);
    root.put("metadata", metadata.final()); root.put("sections", sections.final());
    root.put("previews", previews.final()); root.put("diagnostics", builder.array().final());
    input->root = root.final();
    log_debug("eps-input: parsed %zu PostScript bytes at offset %zu", count, offset);
}
