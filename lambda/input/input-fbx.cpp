// FBX 7.x raw input (vibe/Lambda_Pkg_Motion.md §4): binary and ASCII files map
// 1:1 onto one uninterpreted record tree. A record becomes an element named by
// the record, its ordered properties one `props` array attribute, and its
// nested records the element content. Connections, units and object semantics
// belong to the character converter.
#include "input-scene3d.hpp"
#include "input-parsers.h"
#include "../../lib/endian.h"
#include "../../lib/str.h"
#include "../../lib/strbuf.h"
#include "../../lib/zlib_exact.h"

using namespace lambda;

namespace {

static constexpr size_t FBX_DECODED_LIMIT = 256u * 1024 * 1024; // decoded-array bytes per file
static constexpr uint64_t FBX_ARRAY_LIMIT = 8388608;           // components per array
static constexpr unsigned FBX_DEPTH_LIMIT = 64;
static constexpr int64_t FBX_FIRST_VERSION = 7100, FBX_LAST_VERSION = 7700;
// "Kaydara FBX Binary", two spaces, NUL, 0x1A, NUL; the uint32 version follows
static const char FBX_MAGIC[] = "Kaydara FBX Binary  \0\x1a";
static constexpr size_t FBX_MAGIC_LENGTH = 23;

struct FbxRaw {
    SceneAsset& out;
    size_t decoded = 0;
    explicit FbxRaw(SceneAsset& asset) : out(asset) {}

    Item named(const char* name, size_t length) {
        return out.element(out.ctx.builder.createName(name, length)->chars);
    }
    // the per-array cap and the per-file budget bound compression amplification
    bool reserve(uint64_t count, size_t width) {
        if (count > FBX_ARRAY_LIMIT) {
            out.ctx.addError("FBX input: array of %llu elements exceeds %llu",
                (unsigned long long)count, (unsigned long long)FBX_ARRAY_LIMIT);
            return false;
        }
        if (count * width > FBX_DECODED_LIMIT - decoded) {
            out.ctx.addError("FBX input: decoded arrays exceed %zu MiB", FBX_DECODED_LIMIT >> 20);
            return false;
        }
        decoded += count * width;
        return true;
    }
    bool version(int64_t value) {
        if (value >= FBX_FIRST_VERSION && value <= FBX_LAST_VERSION) return true;
        out.ctx.addError(value < FBX_FIRST_VERSION
            ? "FBX input: version %lld predates FBX 7.1; FBX 6.x and older use another object model"
            : "FBX input: version %lld is newer than FBX 7.7", (long long)value);
        return false;
    }
};

// ---- binary ----------------------------------------------------------------

struct FbxBinary : FbxRaw {
    const uint8_t* data;
    size_t size;
    bool wide = false;  // 7500+: 64-bit record header fields

    FbxBinary(SceneAsset& asset, const char* source, size_t length)
        : FbxRaw(asset), data((const uint8_t*)source), size(length) {}

    bool fail(const char* what, size_t at) {
        out.ctx.addError("FBX input: %s at byte %zu", what, at);
        return false;
    }
    size_t header() const { return wide ? 25 : 13; }
    uint64_t field(size_t at, unsigned index) const {
        return wide ? read_le64(data + at + 8 * index) : read_le32(data + at + 4 * index);
    }

    // binary names spell `Name\x00\x01Class`; the ASCII spelling is `Class::Name`
    Item text(const char* bytes, size_t length, size_t at) {
        if (!length) return ItemNull;
        if (!str_utf8_valid(bytes, length)) {
            out.ctx.addWarning("FBX input: string at byte %zu is not UTF-8; kept as binary", at);
            return {.item = x2it(out.ctx.builder.createBinary(bytes, length))};
        }
        for (size_t i = 0; i + 1 < length; i++) {
            if (bytes[i] != 0 || bytes[i + 1] != 1) continue;
            StrBuf* name = strbuf_new();
            strbuf_append_str_n(name, bytes + i + 2, length - i - 2);
            strbuf_append_str(name, "::");
            strbuf_append_str_n(name, bytes, i);
            Item value = out.ctx.builder.createStringItem(name->str, name->length);
            strbuf_free(name);
            return value;
        }
        return out.ctx.builder.createStringItem(bytes, length);
    }

    bool array(size_t& at, size_t stop, char code, ArrayBuilder& props) {
        size_t start = at - 1;
        if (stop - at < 12) return fail("truncated array header", start);
        uint32_t count = read_le32(data + at), encoding = read_le32(data + at + 4),
            stored = read_le32(data + at + 8);
        at += 12;
        if (stored > stop - at) return fail("array data overruns its record", start);
        ArrayNumElemType type = code == 'f' ? ELEM_FLOAT32 : code == 'd' ? ELEM_FLOAT64 :
            code == 'i' ? ELEM_INT32 : code == 'l' ? ELEM_INT64 : ELEM_BOOL;
        size_t width = ELEM_TYPE_SIZE[type >> 4], bytes = (size_t)count * width;
        if (!reserve(count, width)) return false;
        int64_t length = count;
        Item value = out.ctx.builder.createNumericArray(type, 1, &length);
        if (get_type_id(value) != LMD_TYPE_ARRAY_NUM) return fail("array allocation failed", start);
        // lanes are little-endian on disk and on every supported host
        uint8_t* lanes = (uint8_t*)value.array_num->data;
        size_t used = 0;
        if (encoding == 0) {
            if (stored != bytes) return fail("raw array size does not match its element count", start);
            if (bytes) memcpy(lanes, data + at, bytes);
        } else if (encoding == 1) {
            if (!zlib_inflate_exact(data + at, stored, lanes, bytes, &used) || used != stored)
                return fail("compressed array does not inflate to exactly its element count", start);
        } else return fail("unknown array encoding", start);
        if (type == ELEM_BOOL) for (size_t i = 0; i < count; i++) lanes[i] = lanes[i] != 0;
        at += stored;
        props.append(value);
        return true;
    }

    bool property(size_t& at, size_t stop, ArrayBuilder& props) {
        size_t start = at;
        if (at >= stop) return fail("property list overruns its declared length", start);
        char code = (char)data[at++];
        size_t room = stop - at;
        size_t width = code == 'C' ? 1 : code == 'Y' ? 2 : code == 'I' || code == 'F' ? 4 :
            code == 'D' || code == 'L' ? 8 : 0;
        if (width) {
            if (room < width) return fail("truncated property", start);
            const uint8_t* p = data + at;
            uint64_t bits = width == 1 ? p[0] : width == 2 ? read_le16(p) : width == 4 ? read_le32(p) : read_le64(p);
            at += width;
            Item value;
            if (code == 'C') value = out.ctx.builder.createBool(bits != 0);
            else if (code == 'Y') value = {.item = i16_to_item((int16_t)bits)};
            else if (code == 'I') value = {.item = i32_to_item((int32_t)bits)};
            else if (code == 'F') {
                uint32_t raw = (uint32_t)bits; float real; memcpy(&real, &raw, 4);
                value = {.item = f32_to_item(real)};
            } else if (code == 'D') { double real; memcpy(&real, &bits, 8); value = out.ctx.builder.createFloat(real); }
            else value = out.ctx.builder.createLong((int64_t)bits);
            props.append(value);
            return true;
        }
        if (code == 'S' || code == 'R') {
            if (room < 4) return fail("truncated string length", start);
            uint32_t length = read_le32(data + at);
            at += 4;
            if (length > room - 4) return fail("string data overruns its record", start);
            const char* bytes = (const char*)data + at;
            at += length;
            if (code == 'S') props.append(text(bytes, length, start));
            else props.append(length ? (Item){.item = x2it(out.ctx.builder.createBinary(bytes, length))} : ItemNull);
            return true;
        }
        if (code == 'f' || code == 'd' || code == 'i' || code == 'l' || code == 'b')
            return array(at, stop, code, props);
        return fail("unknown property type code", start);
    }

    // one record inside [at, stop); a null record ends its parent's list
    bool record(size_t& at, size_t stop, Item parent, unsigned depth, bool& sentinel) {
        size_t start = at;
        if (stop - at < header()) return fail("truncated record header", start);
        uint64_t end = field(at, 0), count = field(at, 1), length = field(at, 2);
        uint8_t name_length = data[at + header() - 1];
        at += header();
        if (!end && !count && !length && !name_length) { sentinel = true; return true; }
        if (end <= at || end > stop) return fail("record end offset outside its parent", start);
        if (!name_length) return fail("record without a name", start);
        if (depth >= FBX_DEPTH_LIMIT) return fail("records nest deeper than 64", start);
        if (name_length > end - at) return fail("record name overruns the record", start);
        Item node = named((const char*)data + at, name_length);
        at += name_length;
        if (length > end - at || count > length) return fail("property list overruns the record", start);
        size_t props_end = at + (size_t)length;
        if (count) {
            ArrayBuilder props = out.ctx.builder.array();
            for (uint64_t i = 0; i < count; i++) if (!property(at, props_end, props)) return false;
            out.attr(node, "props", props.final());
        }
        if (at != props_end) return fail("property list length does not match its properties", start);
        while (at < end) {
            bool done = false;
            if (!record(at, (size_t)end, node, depth + 1, done)) return false;
            if (done) break;
        }
        if (at != end) return fail("nested records do not end at the record end", start);
        out.append(parent, node);
        return true;
    }

    void parse() {
        if (size < FBX_MAGIC_LENGTH + 4) { fail("truncated header", 0); return; }
        uint32_t file_version = read_le32(data + FBX_MAGIC_LENGTH);
        out.attr(out.root, "binary", out.ctx.builder.createBool(true));
        out.attr(out.root, "version", out.ctx.builder.createInt(file_version));
        if (!version(file_version)) return;
        wide = file_version >= 7500;
        // the footer after the top-level null record varies by writer
        for (size_t at = FBX_MAGIC_LENGTH + 4;;) {
            bool done = false;
            if (!record(at, size, out.root, 0, done) || done) return;
        }
    }
};

// ---- ASCII -----------------------------------------------------------------

struct FbxText : FbxRaw {
    const char* p;
    const char* end;
    unsigned line = 1;
    int64_t header_version = 0;  // FBXHeaderExtension/FBXVersion

    FbxText(SceneAsset& asset, const char* source, size_t length)
        : FbxRaw(asset), p(source), end(source + length) {
        if (length >= 3 && !memcmp(source, "\xef\xbb\xbf", 3)) p += 3;
    }

    bool fail(const char* what) {
        out.ctx.addError("FBX input: %s on line %u", what, line);
        return false;
    }
    // blanks and `;` comments; line breaks only where the grammar allows them
    void skip(bool newlines) {
        while (p < end) {
            if (*p == ';') while (p < end && *p != '\n') p++;
            else if (*p == '\n' && newlines) { line++; p++; }
            else if (*p == ' ' || *p == '\t' || *p == '\r') p++;
            else return;
        }
    }
    bool word_char(char c) const {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '|' || c == '-';
    }
    StrView word() {
        const char* start = p;
        while (p < end && word_char(*p)) p++;
        return {start, (size_t)(p - start)};
    }
    // a decimal token; `real` reports a fraction or exponent
    bool number(int64_t* integer, double* value, bool* real) {
        const char* start = p;
        *real = false;
        if (p < end && (*p == '-' || *p == '+')) p++;
        while (p < end) {
            if (*p == '.' || *p == 'e' || *p == 'E') *real = true;
            else if ((*p == '-' || *p == '+') && (p[-1] == 'e' || p[-1] == 'E')) {}
            else if (*p < '0' || *p > '9') break;
            p++;
        }
        size_t length = (size_t)(p - start);
        // writers that spell non-finite values (`1.#INF`) are outside this profile
        if (p < end && (word_char(*p) || *p == '#')) return fail("malformed number");
        if (*real ? !str_to_double(start, length, value, NULL) || !isfinite(*value)
                  : !str_to_int64(start, length, integer, NULL)) return fail("malformed number");
        return true;
    }
    bool numeric(char c) const { return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'; }

    // `*N { a: v, v, … }`: one ArrayNum of int, or of float once any value is
    bool array(ArrayBuilder& props) {
        p++;
        int64_t count; double unused; bool real;
        if (!number(&count, &unused, &real) || real || count < 0) return fail("malformed array length");
        if (!reserve((uint64_t)count, 8)) return false;
        skip(true);
        if (p >= end || *p != '{') return fail("expected '{' after the array length");
        p++; skip(true);
        StrView key = word();
        skip(true);
        if (!strview_equal(&key, "a") || p >= end || *p != ':') return fail("expected 'a:' in an array");
        p++;
        // lanes start as int64 and are rewritten in place as doubles at the first real
        int64_t* lanes = count ? (int64_t*)mem_alloc((size_t)count * 8, MEM_CAT_INPUT_OTHER) : nullptr;
        bool floating = false, ok = true;
        int64_t n = 0;
        for (skip(true); p < end && *p != '}'; skip(true)) {
            if (n) {
                if (*p != ',') { ok = fail("expected ',' between array values"); break; }
                p++; skip(true);
            }
            int64_t integer = 0; double value = 0;
            if (p >= end || !numeric(*p)) { ok = fail("malformed array value"); break; }
            if (!number(&integer, &value, &real)) { ok = false; break; }
            if (n == count) { ok = fail("array has more values than its length"); break; }
            if (real && !floating) {
                for (int64_t i = 0; i < n; i++) { double d = (double)lanes[i]; memcpy(&lanes[i], &d, 8); }
                floating = true;
            }
            if (floating) { if (!real) value = (double)integer; memcpy(&lanes[n], &value, 8); }
            else lanes[n] = integer;
            n++;
        }
        if (ok && p >= end) ok = fail("unterminated array");
        if (ok && n != count) ok = fail("array has fewer values than its length");
        if (ok) {
            p++;
            props.append(out.ctx.builder.createNumericArray(floating ? ELEM_FLOAT64 : ELEM_INT, 1, &count, lanes));
        }
        if (lanes) mem_free(lanes);
        return ok;
    }

    bool value(ArrayBuilder& props) {
        if (p >= end) return fail("expected a value");
        if (*p == '"') {
            const char* start = ++p;
            while (p < end && *p != '"' && *p != '\n') p++;
            if (p >= end || *p != '"') return fail("unterminated string");
            size_t length = (size_t)(p++ - start);
            props.append(length ? out.ctx.builder.createStringItem(start, length) : ItemNull);
            return true;
        }
        if (*p == '*') return array(props);
        if (numeric(*p)) {
            int64_t integer; double real_value; bool real;
            if (!number(&integer, &real_value, &real)) return false;
            props.append(real ? out.ctx.builder.createFloat(real_value) : out.ctx.builder.createInt(integer));
            return true;
        }
        StrView bare = word();
        if (!bare.length) return fail("unexpected character in a property list");
        // bare-word tokens such as `T` or `Y` carry no type code
        props.append({.item = y2it(out.ctx.builder.createSymbol(bare.str, bare.length))});
        return true;
    }

    // `Name: values {children}`; a comma continues the values across lines
    bool record(Item parent, unsigned depth, bool header_child) {
        StrView name = word();
        if (!name.length) return fail("expected a record name");
        skip(false);
        if (p >= end || *p != ':') return fail("expected ':' after a record name");
        if (depth >= FBX_DEPTH_LIMIT) return fail("records nest deeper than 64");
        p++;
        Item node = named(name.str, name.length);
        ArrayBuilder props = out.ctx.builder.array();
        size_t count = 0;
        skip(false);
        // embedded `Content: ,` data starts its values after a leading comma
        if (p < end && *p == ',') { p++; skip(true); }
        if (p < end && *p != '\n' && *p != '{' && *p != '}') {
            for (;;) {
                if (!value(props)) return false;
                count++;
                skip(false);
                if (p >= end || *p != ',') break;
                p++;
                skip(true);
            }
        }
        if (count) {
            Item list = props.final();
            out.attr(node, "props", list);
            ItemReader first = asset_at(asset_read(list), 0);
            if (header_child && strview_equal(&name, "FBXVersion") && first.isInt()) header_version = first.asInt();
        }
        skip(false);
        if (p < end && *p == '{') {
            p++;
            bool header = depth == 0 && strview_equal(&name, "FBXHeaderExtension");
            for (skip(true); p < end && *p != '}'; skip(true))
                if (!record(node, depth + 1, header)) return false;
            if (p >= end) return fail("unterminated record body");
            p++;
            skip(false);
        }
        if (p < end && *p != '\n' && *p != '}') return fail("unexpected text after a record");
        out.append(parent, node);
        return true;
    }

    void parse() {
        for (skip(true); p < end; skip(true)) {
            if (*p == '}') { fail("unmatched '}'"); return; }
            if (!record(out.root, 0, false)) return;
        }
        out.attr(out.root, "binary", out.ctx.builder.createBool(false));
        if (!header_version) { out.ctx.addError("FBX input: missing FBXHeaderExtension FBXVersion"); return; }
        out.attr(out.root, "version", out.ctx.builder.createInt(header_version));
        version(header_version);
    }
};

} // namespace

void input_fbx(Input* input, const char* source, size_t length) {
    SceneAsset asset(input, "fbx");
    if (!source) { source = ""; length = 0; }
    if (length > SCENE_ASSET_BUFFER_LIMIT) asset.ctx.addError("FBX input: source exceeds %zu MiB", SCENE_ASSET_BUFFER_LIMIT >> 20);
    else if (length >= FBX_MAGIC_LENGTH && !memcmp(source, FBX_MAGIC, FBX_MAGIC_LENGTH)) FbxBinary(asset, source, length).parse();
    else if (memchr(source, '\0', length)) asset.ctx.addError("FBX input: embedded NUL in ASCII input");
    else FbxText(asset, source, length).parse();
    asset.finish();
}
