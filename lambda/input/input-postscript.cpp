#include "input-postscript.hpp"
#include "pdf_decompress.h"
#include "../../lib/hex.h"
#include "../../lib/str.h"
#include <math.h>
#include <limits.h>

bool ps_is_space(unsigned char c) {
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

const char* ps_skip_comment(const char* p, const char* end) {
    return strn_scan_to_line_end(p, end);
}

PsStringResult ps_read_literal(const char* p, const char* end, StrBuf* out,
                              size_t max_bytes, bool normalize_lines) {
    strbuf_reset(out);
    const char* q = p + 1;
    size_t depth = 1;
    while (q < end && depth) {
        unsigned char c = (unsigned char)*q++;
        if (c == '\\') {
            if (q == end) break;
            c = (unsigned char)*q++;
            if (c >= '0' && c <= '7') {
                unsigned value = c - '0';
                for (unsigned n = 1; n < 3 && q < end && *q >= '0' && *q <= '7'; n++)
                    value = value * 8 + (unsigned)(*q++ - '0');
                c = (unsigned char)value;
            } else if (c == '\n' || c == '\r') {
                if (c == '\r' && q < end && *q == '\n') q++;
                continue;
            } else {
                switch (c) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                }
            }
        } else if (c == '(') depth++;
        else if (c == ')') { if (--depth == 0) break; }
        else if (normalize_lines && c == '\r') {
            if (q < end && *q == '\n') q++;
            c = '\n';
        }
        if (out->length >= max_bytes) return {q, false, true};
        if (!strbuf_ensure_cap(out, out->length + 2)) return {q, false, true};
        strbuf_append_char(out, (char)c);
    }
    return {q, depth == 0, false};
}

namespace {

struct Scanner {
    MarkBuilder builder;
    const char* begin;
    const char* end;
    size_t max_bytes;
    size_t max_depth;
    size_t max_objects;
    size_t objects;
    const char* error;
    const char* error_at;
    Scanner(Input* owner, const char* bytes, size_t length, size_t cap, size_t depth, size_t object_cap)
        : builder(owner), begin(bytes), end(bytes + length), max_bytes(cap),
          max_depth(depth), max_objects(object_cap), objects(0), error(nullptr), error_at(bytes) {}

    void fail(const char* p, const char* message) {
        if (!error) { error = message; error_at = p; }
    }

    const char* skip(const char* p) {
        while (p < end) {
            if (ps_is_space((unsigned char)*p)) p++;
            else if (*p == '%') p = ps_skip_comment(p, end);
            else break;
        }
        return p;
    }

    Item token(const char* kind, Item value, bool executable, const char* p, const char* q) {
        MapBuilder map = builder.map();
        map.put("kind", kind); map.put("value", value); map.put("executable", executable);
        map.put("offset", builder.createInt(p - begin));
        map.put("next", builder.createInt(q - begin));
        return map.final();
    }

    Item bytes_token(const char* p, const char* q, StrBuf* bytes) {
        ArrayBuilder values = builder.array();
        for (size_t i = 0; i < bytes->length; i++)
            values.append(builder.createInt((unsigned char)bytes->str[i]));
        return token("string", values.final(), false, p, q);
    }

    Item name(const char* p, const char* q, const char* text, bool executable, bool immediate = false,
              const char* resume = nullptr) {
        StrBuf* key = strbuf_new();
        if (!key) { fail(p, "allocation failure"); return ItemNull; }
        // escaped byte keys preserve non-UTF-8 names without invalid Lambda strings.
        for (const char* c = text; c < q; c++) {
            unsigned char b = (unsigned char)*c;
            if (b >= 33 && b <= 126 && b != '%') strbuf_append_char(key, *c);
            else {
                strbuf_append_char(key, '%');
                strbuf_append_char(key, hex_encode_nibble_upper(b >> 4));
                strbuf_append_char(key, hex_encode_nibble_upper(b & 15));
            }
        }
        Item result = token(immediate ? "immediate" : "name",
            builder.createStringItem(key->str, key->length), executable, p, resume ? resume : q);
        strbuf_free(key);
        return result;
    }

    static bool delimiter(char c) { return strchr("()<>[]{}/%", c) != nullptr; }

    bool number(const char* p, const char* q, Item* result) {
        const char* hash = (const char*)memchr(p, '#', (size_t)(q - p));
        if (hash) {
            int64_t base = 0;
            const char* stop = nullptr;
            if (!str_to_int64(p, hash - p, &base, &stop) || stop != hash || base < 2 || base > 36)
                return false;
            const char* c = hash + 1;
            bool negative = c < q && *c == '-';
            if (c < q && (*c == '-' || *c == '+')) c++;
            if (c == q) return false;
            double value = 0;
            for (; c < q; c++) {
                unsigned char ch = (unsigned char)*c;
                int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'a' && ch <= 'z' ? ch - 'a' + 10 :
                    ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 : 100;
                if (digit >= base) return false;
                value = value * (double)base + digit;
            }
            if (negative) value = -value;
            if (!isfinite(value)) { fail(p, "numeric range exceeded"); return false; }
            *result = value >= INT32_MIN && value <= INT32_MAX ? builder.createInt((int64_t)value) :
                builder.createFloat(value);
            return true;
        }
        const char* c = p;
        if (c < q && (*c == '+' || *c == '-')) c++;
        bool digits = false, real = false;
        while (c < q && str_char_is_digit(*c)) { c++; digits = true; }
        if (c < q && *c == '.') {
            real = true; c++;
            while (c < q && str_char_is_digit(*c)) { c++; digits = true; }
        }
        if (!digits) return false;
        if (c < q && (*c == 'e' || *c == 'E')) {
            real = true; c++;
            if (c < q && (*c == '+' || *c == '-')) c++;
            const char* first = c;
            while (c < q && str_char_is_digit(*c)) c++;
            if (first == c) return false;
        }
        if (c != q) return false;
        double value = 0;
        const char* stop = nullptr;
        if (!str_to_double(p, q - p, &value, &stop) || stop != q || !isfinite(value)) {
            fail(p, "numeric range exceeded"); return false;
        }
        *result = !real && value >= INT32_MIN && value <= INT32_MAX ? builder.createInt((int64_t)value) :
            builder.createFloat(value);
        return true;
    }

    Item read(const char** cursor, size_t depth) {
        const char* p = skip(*cursor);
        *cursor = p;
        if (p == end) return token("eof", ItemNull, false, p, p);
        if (objects++ >= max_objects) { fail(p, "scan object limit exceeded"); return ItemNull; }
        if (depth > max_depth) { fail(p, "procedure nesting limit exceeded"); return ItemNull; }
        const char* q = p + 1;
        if (*p == '{') {
            ArrayBuilder body = builder.array();
            while (!error) {
                q = skip(q);
                if (q == end) { fail(p, "unterminated procedure"); break; }
                if (*q == '}') { q++; break; }
                if ((size_t)(q - p) > max_bytes) { fail(p, "procedure byte limit exceeded"); break; }
                body.append(read(&q, depth + 1));
            }
            if ((size_t)(q - p) > max_bytes) fail(p, "procedure byte limit exceeded");
            *cursor = q;
            return token("procedure", body.final(), true, p, q);
        }
        if (*p == ')' || *p == '}') { fail(p, "unmatched delimiter"); return ItemNull; }
        if (*p == '(' || (*p == '<' && q < end && *q != '<')) {
            StrBuf* buf = strbuf_new();
            if (!buf) { fail(p, "allocation failure"); return ItemNull; }
            if (*p == '(') {
                PsStringResult r = ps_read_literal(p, end, buf, max_bytes, true);
                q = r.next;
                if (!r.terminated) fail(p, r.limited ? "string byte limit exceeded" : "unterminated string");
            } else if (*q == '~') {
                size_t consumed = 0;
                if (!ascii85_decode_bounded(p, end - p, buf, max_bytes, true, &consumed))
                    fail(p, "invalid, unterminated or oversized ASCII85 string");
                q = p + consumed;
            } else {
                size_t consumed = 0;
                if (!asciihex_decode_bounded(p + 1, end - p - 1, buf, max_bytes, true, true, &consumed))
                    fail(p, "invalid, unterminated or oversized hex string");
                q = p + 1 + consumed;
            }
            if (buf->length > max_bytes) fail(p, "string byte limit exceeded");
            *cursor = q;
            Item result = bytes_token(p, q, buf);
            strbuf_free(buf);
            return result;
        }
        if (*p == '[' || *p == ']') { *cursor = q; return name(p, q, p, true); }
        if (*p == '<' || *p == '>') {
            if (q == end || *q != *p) { fail(p, "unmatched dictionary delimiter"); return ItemNull; }
            q++; *cursor = q; return name(p, q, p, true);
        }
        bool literal = *p == '/';
        bool immediate = literal && q < end && *q == '/';
        const char* text = literal ? p + (immediate ? 2 : 1) : p;
        q = text;
        while (q < end && !ps_is_space((unsigned char)*q) && !delimiter(*q)) {
            if ((size_t)(q - text) >= max_bytes) { fail(p, "token byte limit exceeded"); break; }
            q++;
        }
        // the scanner consumes one whitespace terminator; CRLF is one PostScript line end.
        const char* resume = q;
        if (resume < end && ps_is_space((unsigned char)*resume)) {
            if (*resume++ == '\r' && resume < end && *resume == '\n') resume++;
        }
        *cursor = resume;
        Item numeric = ItemNull;
        if (!literal && number(text, q, &numeric)) return token("number", numeric, false, p, resume);
        return name(p, q, text, !literal, immediate, resume);
    }
};
}

Item postscript_scan_io(Input* owner, Item source, size_t offset,
                        size_t max_bytes, size_t max_depth, size_t source_end, size_t max_objects) {
    ByteBuilder scratch = {};
    const char* data = nullptr; size_t length = 0;
    if (!owner || !postscript_byte_view(source, &data, &length, &scratch)) return ItemNull;
    if (source_end < length) length = source_end;
    Scanner scanner(owner, data, length, max_bytes, max_depth, max_objects);
    const char* cursor = data + (offset <= length ? offset : length);
    if (offset > length) scanner.fail(cursor, "source offset out of range");
    Item result = scanner.error ? ItemNull : scanner.read(&cursor, 0);
    if (!scanner.error) { byte_builder_destroy(&scratch); return result; }
    MapBuilder error = scanner.builder.map();
    error.put("kind", "error"); error.put("message", scanner.error);
    error.put("offset", scanner.builder.createInt(scanner.error_at - data));
    error.put("next", scanner.builder.createInt(cursor - data));
    result = error.final();
    byte_builder_destroy(&scratch);
    return result;
}
