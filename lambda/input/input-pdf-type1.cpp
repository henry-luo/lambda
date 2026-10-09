// Adobe Type 1 Font Format chapters 6-8: bounded outline decoding, without executing PostScript.
#include "input-pdf-type1.hpp"
#include "../io/mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "lib/arena.h"
#include "lib/arraylist.h"
#include "lib/log.h"
#include "lib/strbuf.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Token {
    const uint8_t* data;
    size_t len;
    bool is(const char* s) const { return strlen(s) == len && !memcmp(data, s, len); }
    bool number(double* out) const {
        if (!len || len >= 64) return false;
        char buf[64]; memcpy(buf, data, len); buf[len] = 0;
        char* end; *out = strtod(buf, &end);
        return end == buf + len && isfinite(*out);
    }
};

struct Lexer {
    const uint8_t* data;
    size_t len, pos;
    Token next() {
        while (pos < len) {
            if (isspace(data[pos]) || data[pos] == 0) { pos++; continue; }
            if (data[pos] != '%') break;
            while (pos < len && data[pos] != '\n' && data[pos] != '\r') pos++;
        }
        size_t start = pos;
        if (pos < len) {
            uint8_t ch = data[pos++];
            if (!strchr("[]{}()<>/", ch) || ch == '/') {
                while (pos < len && !isspace(data[pos]) && data[pos] &&
                       !strchr("[]{}()<>/%", data[pos])) pos++;
            }
        }
        return {data + start, pos - start};
    }
};

struct Program { Token name; uint8_t* data; size_t len; };
struct Font {
    Arena* arena;
    ArrayList* glyphs;
    Program* subrs;
    int subr_count, len_iv;
    Token encoding[256];
    double matrix[6];
};

static uint8_t* decrypt(Arena* arena, const uint8_t* data, size_t len, uint16_t key) {
    uint8_t* out = (uint8_t*)arena_alloc(arena, len ? len : 1);
    if (!out) return nullptr;
    for (size_t i = 0; i < len; i++) {
        out[i] = data[i] ^ (key >> 8);
        key = (uint16_t)((data[i] + key) * 52845u + 22719u);
    }
    return out;
}

static Token literal(const char* s) { return {(const uint8_t*)s, strlen(s)}; }

static Token standard_name(int code) {
    static const char* ascii[] = {
        "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quoteright",
        "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period", "slash",
        "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
        "colon", "semicolon", "less", "equal", "greater", "question", "at",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
        "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright",
        "asciicircum", "underscore", "quoteleft", "a", "b", "c", "d", "e", "f", "g", "h", "i",
        "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
        "braceleft", "bar", "braceright", "asciitilde"
    };
    static const struct { int code; const char* name; } upper[] = {
        {161,"exclamdown"},{162,"cent"},{163,"sterling"},{164,"fraction"},{165,"yen"},
        {166,"florin"},{167,"section"},{168,"currency"},{169,"quotesingle"},{170,"quotedblleft"},
        {171,"guillemotleft"},{172,"guilsinglleft"},{173,"guilsinglright"},{174,"fi"},{175,"fl"},
        {177,"endash"},{178,"dagger"},{179,"daggerdbl"},{180,"periodcentered"},{182,"paragraph"},
        {183,"bullet"},{184,"quotesinglbase"},{185,"quotedblbase"},{186,"quotedblright"},
        {187,"guillemotright"},{188,"ellipsis"},{189,"perthousand"},{191,"questiondown"},
        {193,"grave"},{194,"acute"},{195,"circumflex"},{196,"tilde"},{197,"macron"},
        {198,"breve"},{199,"dotaccent"},{200,"dieresis"},{202,"ring"},{203,"cedilla"},
        {205,"hungarumlaut"},{206,"ogonek"},{207,"caron"},{208,"emdash"},{225,"AE"},
        {227,"ordfeminine"},{232,"Lslash"},{233,"Oslash"},{234,"OE"},{235,"ordmasculine"},
        {241,"ae"},{245,"dotlessi"},{248,"lslash"},{249,"oslash"},{250,"oe"},{251,"germandbls"}
    };
    if (code >= 32 && code <= 126) return literal(ascii[code - 32]);
    for (const auto& entry : upper) if (entry.code == code) return literal(entry.name);
    return literal(".notdef");
}

static void parse_clear(Font* font, Lexer lex) {
    bool in_encoding = false;
    Token history[3] = {};
    for (Token t = lex.next(); t.len; t = lex.next()) {
        if (t.is("/FontMatrix")) {
            if (!lex.next().is("[")) continue;
            double matrix[6]; bool valid = true;
            for (double& v : matrix) if (!lex.next().number(&v)) valid = false;
            if (valid && lex.next().is("]")) memcpy(font->matrix, matrix, sizeof(matrix));
        }
        if (t.is("/Encoding")) in_encoding = true;
        else if (in_encoding && t.is("StandardEncoding")) {
            for (int i = 0; i < 256; i++) font->encoding[i] = standard_name(i);
        }
        else if (in_encoding && t.is("put") && history[0].is("dup") && history[2].len > 1 &&
                 history[2].data[0] == '/') {
            double code;
            if (history[1].number(&code) && code >= 0 && code < 256 && code == floor(code)) {
                font->encoding[(int)code] = {history[2].data + 1, history[2].len - 1};
            }
        }
        else if (in_encoding && t.is("def")) in_encoding = false;
        history[0] = history[1]; history[1] = history[2]; history[2] = t;
    }
}

static bool parse_private(Font* font, Lexer lex) {
    Token history[3] = {};
    bool in_glyphs = false;
    for (Token t = lex.next(); t.len; t = lex.next()) {
        if (t.is("/lenIV")) {
            double n;
            if (!lex.next().number(&n) || n < -1 || n > 255 || n != floor(n)) return false;
            font->len_iv = (int)n;
        }
        else if (t.is("/Subrs")) {
            double n;
            if (!lex.next().number(&n) || n < 0 || n > 65536 || n != floor(n)) return false;
            font->subr_count = (int)n;
            font->subrs = (Program*)arena_alloc(font->arena, sizeof(Program) * (font->subr_count + 1));
            if (!font->subrs) return false;
            memset(font->subrs, 0, sizeof(Program) * (font->subr_count + 1));
        }
        else if (t.is("/CharStrings")) in_glyphs = true;
        else if (t.is("RD") || t.is("-|")) {
            double size, index = -1;
            bool subr = !in_glyphs && history[0].is("dup") && history[1].number(&index);
            bool glyph = in_glyphs && history[1].len > 1 && history[1].data[0] == '/';
            if ((!subr && !glyph) || !history[2].number(&size)) continue;
            if (size < 0 || size > (double)(lex.len - lex.pos) || size != floor(size)) return false;
            // RD consumes exactly one separator; the following bytes are binary, including whitespace.
            if (lex.pos >= lex.len || !isspace(lex.data[lex.pos])) return false;
            if (lex.data[lex.pos++] == '\r' && lex.pos < lex.len && lex.data[lex.pos] == '\n') lex.pos++;
            size_t len = (size_t)size;
            if (len > lex.len - lex.pos) return false;
            uint8_t* data = font->len_iv < 0 ? (uint8_t*)lex.data + lex.pos :
                decrypt(font->arena, lex.data + lex.pos, len, 4330);
            lex.pos += len;
            size_t skip = font->len_iv < 0 ? 0 : (size_t)font->len_iv;
            if (!data || skip > len) return false;
            Program program = {{}, data + skip, len - skip};
            if (subr) {
                if (index < 0 || index >= font->subr_count || index != floor(index)) return false;
                font->subrs[(int)index] = program;
            } else {
                if (font->glyphs->length >= 65536) return false;
                Program* p = (Program*)arena_alloc(font->arena, sizeof(Program));
                if (!p) return false;
                *p = program; p->name = {history[1].data + 1, history[1].len - 1};
                if (!arraylist_append(font->glyphs, p)) return false;
            }
        }
        history[0] = history[1]; history[1] = history[2]; history[2] = t;
    }
    return font->glyphs->length > 0;
}

static Program* find_glyph(Font* font, Token name) {
    for (int i = 0; i < font->glyphs->length; i++) {
        Program* p = (Program*)font->glyphs->data[i];
        if (p->name.len == name.len && !memcmp(p->name.data, name.data, name.len)) return p;
    }
    return nullptr;
}

struct Outline {
    Font* font;
    StrBuf* path;
    double x, y, offset_x, offset_y, side_x;
    double stack[48], results[48], flex[14];
    int count, result_count, result_pos, flex_count, budget;
    bool flexing, open;
};

static bool point(Outline* o, double x, double y) {
    const double* m = o->font->matrix;
    x += o->offset_x; y += o->offset_y;
    double px = 1000 * (m[0] * x + m[2] * y + m[4]);
    double py = 1000 * (m[1] * x + m[3] * y + m[5]);
    if (!isfinite(px) || !isfinite(py) || fabs(px) > 1e9 || fabs(py) > 1e9) return false;
    strbuf_append_format(o->path, "%.6g %.6g ", px, py);
    return true;
}

static void close_path(Outline* o) {
    if (o->open) strbuf_append_str(o->path, "Z ");
    o->open = false;
}

static bool move(Outline* o, double dx, double dy) {
    o->x += dx; o->y += dy;
    if (o->flexing) return true;
    close_path(o); o->open = true;
    strbuf_append_char(o->path, 'M');
    return point(o, o->x, o->y);
}

static bool curve(Outline* o, const double* v) {
    strbuf_append_char(o->path, 'C');
    for (int i = 0; i < 6; i += 2) {
        o->x += v[i]; o->y += v[i + 1];
        if (!point(o, o->x, o->y)) return false;
    }
    return true;
}

static bool execute(Outline* o, Program* p, int depth) {
    if (!p || !p->data || depth > 16) return false;
    for (size_t i = 0; i < p->len;) {
        if (--o->budget < 0) return false;
        int b = p->data[i++]; double value;
        if (b >= 32) {
            if (b <= 246) value = b - 139;
            else if (b <= 254) {
                if (i >= p->len) return false;
                value = (b <= 250 ? b - 247 : b - 251) * 256 + p->data[i++] + 108;
                if (b > 250) value = -value;
            } else {
                if (p->len - i < 4) return false;
                uint32_t n = 0;
                for (int k = 0; k < 4; k++) n = (n << 8) | p->data[i++];
                value = (int32_t)n;
            }
            if (o->count >= 48) return false;
            o->stack[o->count++] = value; continue;
        }
        if (b == 12) {
            if (i >= p->len) return false;
            b = 256 + p->data[i++];
        }
        double* s = o->stack; int n = o->count;
        switch (b) {
        case 1: case 3: case 256: case 257: case 258: break; // stem/dot hints do not change outlines
        case 4: case 21: case 22:
            if (n != (b == 21 ? 2 : 1) || !move(o, b == 4 ? 0 : s[0], b == 22 ? 0 : s[n - 1])) return false;
            break;
        case 5: case 6: case 7:
            if (!n || (b == 5 && n % 2)) return false;
            for (int k = 0; k < n;) {
                o->x += b == 7 ? 0 : s[k++];
                o->y += b == 6 ? 0 : s[k++];
                strbuf_append_char(o->path, 'L');
                if (!point(o, o->x, o->y)) return false;
            }
            break;
        case 8:
            if (!n || n % 6) return false;
            for (int k = 0; k < n; k += 6) if (!curve(o, s + k)) return false;
            break;
        case 30: case 31:
            if (n != 4) return false;
            { double v[6] = {b == 31 ? s[0] : 0, b == 30 ? s[0] : 0, s[1], s[2],
                             b == 30 ? s[3] : 0, b == 31 ? s[3] : 0};
              if (!curve(o, v)) return false; }
            break;
        case 9: close_path(o); break;
        case 10:
            if (!n || s[n - 1] < 0 || s[n - 1] >= o->font->subr_count || s[n - 1] != floor(s[n - 1])) return false;
            o->count--;
            if (!execute(o, o->font->subrs + (int)s[n - 1], depth + 1)) return false;
            continue;
        case 11: return true;
        case 13: case 263:
            if (n != (b == 13 ? 2 : 4)) return false;
            o->side_x = o->x = s[0]; o->y = b == 13 ? 0 : s[1]; break;
        case 14: close_path(o); return true;
        case 268:
            if (n < 2 || s[n - 1] == 0) return false;
            s[n - 2] /= s[n - 1];
            if (!isfinite(s[n - 2])) return false;
            o->count--; continue;
        case 272: {
            if (n < 2 || s[n - 2] < 0 || s[n - 2] > n - 2 || s[n - 2] != floor(s[n - 2]) ||
                s[n - 1] < 0 || s[n - 1] > 65535 || s[n - 1] != floor(s[n - 1])) return false;
            int args = (int)s[n - 2], subr = (int)s[n - 1], start = n - 2 - args;
            o->result_count = args; o->result_pos = 0;
            memcpy(o->results, s + start, args * sizeof(double));
            if (subr == 1) { o->flexing = true; o->flex_count = 0; }
            else if (subr == 2) {
                if (!o->flexing || o->flex_count >= 7) return false;
                o->flex[o->flex_count * 2] = o->x; o->flex[o->flex_count++ * 2 + 1] = o->y;
            } else if (subr == 0) {
                if (!o->flexing || o->flex_count != 7 || args != 3) return false;
                strbuf_append_char(o->path, 'C');
                for (int k = 2; k < 14; k += 2) {
                    if (k == 8) strbuf_append_char(o->path, 'C');
                    if (!point(o, o->flex[k], o->flex[k + 1])) return false;
                }
                o->flexing = false;
                o->results[0] = s[start + 1]; o->results[1] = s[start + 2]; o->result_count = 2;
            }
            o->count = start; continue;
        }
        case 273:
            if (n >= 48 || o->result_pos >= o->result_count) return false;
            s[o->count++] = o->results[o->result_pos++]; continue;
        case 289:
            if (n != 2) return false;
            o->x = s[0]; o->y = s[1]; break;
        case 262: {
            if (n != 5 || s[3] < 0 || s[3] > 255 || s[4] < 0 || s[4] > 255) return false;
            Outline base = *o; base.count = 0; base.x = base.y = 0;
            if (!execute(&base, find_glyph(o->font, standard_name((int)s[3])), depth + 1)) return false;
            Outline accent = *o; accent.count = 0; accent.x = accent.y = 0;
            accent.offset_x += s[1] - s[0] + o->side_x; accent.offset_y += s[2]; accent.budget = base.budget;
            if (!execute(&accent, find_glyph(o->font, standard_name((int)s[4])), depth + 1)) return false;
            o->budget = accent.budget; return true;
        }
        default: return false;
        }
        o->count = 0;
    }
    return false;
}

static bool apply_encoding(Font* font, Item encoding) {
    MapReader dict(encoding.get_safe_map());
    String* base = encoding.get_safe_string();
    if (!base) base = dict.get("BaseEncoding").item().get_safe_string();
    if (base) {
        if (strcmp(base->chars, "StandardEncoding")) return false;
        for (int i = 0; i < 256; i++) font->encoding[i] = standard_name(i);
    }
    ArrayReader differences = dict.get("Differences").asArray();
    int code = -1;
    for (int64_t i = 0; i < differences.length(); i++) {
        ItemReader entry = differences.get(i);
        if (entry.isInt() || entry.isFloat()) {
            double n = entry.isInt() ? (double)entry.asInt() : entry.asFloat();
            if (!isfinite(n) || n < 0 || n > 255 || n != floor(n)) return false;
            code = (int)n;
        }
        else if (String* name = entry.item().get_safe_string()) {
            if (code < 0 || code >= 256) return false;
            font->encoding[code++] = {(const uint8_t*)name->chars, name->len};
        } else return false;
    }
    return true;
}

} // namespace

Item pdf_type1_glyph_paths(Input* input, String* program, int clear_length, Item encoding) {
    if (!program || !program->len || clear_length < 0 || (size_t)clear_length > program->len) return ItemNull;
    Font font = {};
    font.arena = arena_create_default(); font.glyphs = arraylist_new(128); font.len_iv = 4;
    font.matrix[0] = font.matrix[3] = 0.001;
    if (!font.arena || !font.glyphs) { arena_destroy(font.arena); arraylist_free(font.glyphs); return ItemNull; }
    Lexer clear = {(const uint8_t*)program->chars, program->len, 0};
    bool encrypted = false;
    for (Token t = clear.next(); t.len; t = clear.next()) {
        if (t.is("eexec")) { encrypted = true; break; }
    }
    size_t start = clear_length ? (size_t)clear_length : clear.pos;
    if (!clear_length) while (start < clear.len && isspace(clear.data[start])) start++;
    parse_clear(&font, {clear.data, encrypted ? clear.pos : clear.len, 0});
    uint8_t* private_data = encrypted ? decrypt(font.arena, clear.data + start, clear.len - start, 55665) :
        (uint8_t*)clear.data;
    size_t skip = encrypted ? 4 : 0, len = encrypted ? clear.len - start : clear.len;
    Item result = ItemNull;
    if (private_data && len >= skip && parse_private(&font, {private_data + skip, len - skip, 0}) &&
        apply_encoding(&font, encoding)) {
        MarkBuilder builder(input);
        MapBuilder paths = builder.map();
        StrBuf* path = strbuf_new();
        int decoded = 0;
        for (int code = 0; path && code < 256; code++) {
            Program* glyph = find_glyph(&font, font.encoding[code]);
            if (!glyph) continue;
            strbuf_reset(path);
            Outline outline = {}; outline.font = &font; outline.path = path; outline.budget = 100000;
            if (!execute(&outline, glyph, 0)) {
                log_warn("pdf_type1: unsupported or malformed glyph %.*s", (int)glyph->name.len, glyph->name.data);
                continue;
            }
            char key[4]; snprintf(key, sizeof(key), "%d", code);
            paths.put(builder.createString(key), {.item = s2it(builder.createString(path->str, path->length))});
            decoded++;
        }
        if (decoded) result = paths.final();
        strbuf_free(path);
        log_debug("pdf_type1: decoded %d encoded glyph outlines", decoded);
    } else log_warn("pdf_type1: unsupported or malformed embedded font program/encoding");
    arraylist_free(font.glyphs); arena_destroy(font.arena);
    return result;
}
