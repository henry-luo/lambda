// mail_mime.c — MIME header decoding (vibe/Lambda_Demo_Mail.md MG5).

#include "mail_mime.h"

#include <errno.h>
#include <iconv.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "base64.h"
#include "log.h"
#include "memtrack.h"
#include "utf.h"

// ── charsets ───────────────────────────────────────────────────────────

// windows-1252 0x80–0x9F (WHATWG); 0 keeps the byte as its C1 code point
static const unsigned short cp1252_high[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

static void append_cp1252(StrBuf* out, const char* bytes, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c < 0x80) {
            strbuf_append_char(out, (char)c);
            continue;
        }
        uint32_t cp = c < 0xA0 && cp1252_high[c - 0x80] ? cp1252_high[c - 0x80] : c;
        strbuf_append_utf8(out, cp);
    }
}

static void append_utf8_replacing(StrBuf* out, const char* bytes, size_t length) {
    if (utf8_valid(bytes, length)) {
        strbuf_append_str_n(out, bytes, length);
        return;
    }
    size_t i = 0;
    while (i < length) {
        uint32_t cp = 0;
        int n = utf8_decode_replacement(bytes + i, length - i, &cp);
        if (n <= 0) n = 1, cp = 0xFFFD;
        strbuf_append_utf8(out, cp);
        i += (size_t)n;
    }
}

// The charset label, lower-cased without quotes or spaces; the RFC 2231
// language suffix ("utf-8*en") is dropped.
static void charset_label(const char* charset, size_t length, char* label, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < length && n + 1 < size; i++) {
        char c = charset[i];
        if (c == '*') break;
        if (c == '"' || c == '\'' || c == ' ' || c == '\t') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        label[n++] = c;
    }
    label[n] = '\0';
}

static bool label_in(const char* label, const char* const* names) {
    for (int i = 0; names[i]; i++) if (strcmp(label, names[i]) == 0) return true;
    return false;
}

// Labels mail uses for charsets that iconv names differently; the superset
// decodes what the subset produced (GB2312 text is valid GB18030).
static const char* iconv_name(const char* label) {
    static const char* const gb[] = {"gb2312", "gbk", "x-gbk", "euc-cn", "cp936", NULL};
    static const char* const kr[] = {"ks_c_5601-1987", "euc-kr", "ks_c_5601", "korean", NULL};
    static const char* const sjis[] = {"shift-jis", "shift_jis", "x-sjis", "sjis", "ms_kanji", "cp932", NULL};
    if (label_in(label, gb)) return "GB18030";
    if (label_in(label, kr)) return "CP949";
    if (label_in(label, sjis)) return "CP932";
    return label;
}

// iconv into UTF-8, replacing each undecodable byte with U+FFFD. False when
// iconv does not know the charset.
static bool append_iconv(const char* name, const char* bytes, size_t length, StrBuf* out) {
    iconv_t cd = iconv_open("UTF-8", name);
    if (cd == (iconv_t)-1) return false;
    char buffer[4096];
    char* in = (char*)bytes;
    size_t in_left = length;
    while (in_left > 0) {
        char* o = buffer;
        size_t o_left = sizeof(buffer);
        size_t result = iconv(cd, &in, &in_left, &o, &o_left);
        strbuf_append_str_n(out, buffer, sizeof(buffer) - o_left);
        if (result == (size_t)-1 && errno != E2BIG) {
            // an invalid or truncated sequence: replace one byte and resynchronize
            strbuf_append_utf8(out, 0xFFFD);
            in++;
            in_left--;
            iconv(cd, NULL, NULL, NULL, NULL);
        }
    }
    char* o = buffer;
    size_t o_left = sizeof(buffer);
    iconv(cd, NULL, NULL, &o, &o_left);   // close a stateful encoding's shift (ISO-2022-JP)
    strbuf_append_str_n(out, buffer, sizeof(buffer) - o_left);
    iconv_close(cd);
    return true;
}

bool mime_to_utf8(const char* charset, size_t charset_length, const char* bytes, size_t length, StrBuf* out) {
    static const char* const utf8[] = {"", "utf-8", "utf8", "us-ascii", "ascii", "ansi_x3.4-1968", NULL};
    // WHATWG decodes the latin-1 labels as windows-1252, a superset in practice
    static const char* const latin[] = {"iso-8859-1", "iso8859-1", "iso_8859-1", "latin1", "l1",
                                        "cp819", "windows-1252", "cp1252", "x-cp1252", NULL};
    size_t before = out->length;
    char label[64];
    charset_label(charset ? charset : "", charset ? charset_length : 0, label, sizeof(label));
    if (label_in(label, utf8)) append_utf8_replacing(out, bytes, length);
    else if (label_in(label, latin)) append_cp1252(out, bytes, length);
    else if (!append_iconv(iconv_name(label), bytes, length, out)) {
        log_debug("mail mime: unknown charset '%s', decoding as UTF-8", label);
        if (utf8_valid(bytes, length)) strbuf_append_str_n(out, bytes, length);
        else append_cp1252(out, bytes, length);
    }
    return out->length >= before;
}

// ── RFC 2047 encoded words ─────────────────────────────────────────────

typedef struct EncodedWord {
    const char* charset;
    size_t charset_length;
    char encoding;              // 'B' or 'Q'
    const char* text;
    size_t text_length;
    size_t length;              // of the whole =?...?= word
} EncodedWord;

static bool parse_encoded_word(const char* s, size_t n, EncodedWord* word) {
    if (n < 8 || s[0] != '=' || s[1] != '?') return false;
    size_t i = 2;
    while (i < n && s[i] != '?' && s[i] != ' ' && s[i] != '\r' && s[i] != '\n') i++;
    if (i >= n || s[i] != '?' || i == 2) return false;
    word->charset = s + 2;
    word->charset_length = i - 2;
    if (i + 2 >= n || s[i + 2] != '?') return false;
    char encoding = s[i + 1];
    if (encoding == 'b') encoding = 'B';
    if (encoding == 'q') encoding = 'Q';
    if (encoding != 'B' && encoding != 'Q') return false;
    word->encoding = encoding;
    size_t text = i + 3;
    size_t j = text;
    while (j + 1 < n && !(s[j] == '?' && s[j + 1] == '=')) {
        if (s[j] == '\r' || s[j] == '\n') return false;
        j++;
    }
    if (j + 1 >= n) return false;
    word->text = s + text;
    word->text_length = j - text;
    word->length = j + 2;
    return true;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The word's bytes, before charset conversion.
static void append_word_bytes(const EncodedWord* word, StrBuf* bytes) {
    if (word->encoding == 'B') {
        // base64_decode reads to the NUL when given length 0
        if (word->text_length == 0) return;
        size_t length = 0;
        uint8_t* decoded = base64_decode(word->text, word->text_length, &length);
        if (decoded) {
            strbuf_append_str_n(bytes, (const char*)decoded, length);
            mem_free(decoded);
        }
        return;
    }
    for (size_t i = 0; i < word->text_length; i++) {
        char c = word->text[i];
        if (c == '_') {
            strbuf_append_char(bytes, ' ');
        } else if (c == '=' && i + 2 < word->text_length &&
                   hex_value(word->text[i + 1]) >= 0 && hex_value(word->text[i + 2]) >= 0) {
            strbuf_append_char(bytes, (char)(hex_value(word->text[i + 1]) * 16 + hex_value(word->text[i + 2])));
            i += 2;
        } else {
            strbuf_append_char(bytes, c);
        }
    }
}

static void append_plain(StrBuf* out, const char* s, size_t n) {
    if (utf8_valid(s, n)) strbuf_append_str_n(out, s, n);
    else append_cp1252(out, s, n);
}

static bool all_space(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') return false;
    return true;
}

bool mime_decode_header(const char* value, size_t length, StrBuf* out) {
    StrBuf* pending = strbuf_new();     // adjacent words in one charset decode together,
    if (!pending) return false;         // so a split multibyte character survives
    const char* pending_charset = NULL;
    size_t pending_charset_length = 0;
    bool after_word = false;
    size_t plain = 0;
    size_t i = 0;
    while (i < length) {
        EncodedWord word;
        if (value[i] != '=' || !parse_encoded_word(value + i, length - i, &word)) {
            i++;
            continue;
        }
        // whitespace between two encoded words is not text (RFC 2047 §6.2)
        if (!(after_word && all_space(value + plain, i - plain))) {
            if (pending->length) mime_to_utf8(pending_charset, pending_charset_length, pending->str, pending->length, out);
            strbuf_reset(pending);
            pending_charset = NULL;
            append_plain(out, value + plain, i - plain);
        }
        bool same = pending_charset && pending_charset_length == word.charset_length &&
                    strncasecmp(pending_charset, word.charset, word.charset_length) == 0;
        if (!same && pending->length) {
            mime_to_utf8(pending_charset, pending_charset_length, pending->str, pending->length, out);
            strbuf_reset(pending);
        }
        pending_charset = word.charset;
        pending_charset_length = word.charset_length;
        append_word_bytes(&word, pending);
        after_word = true;
        i += word.length;
        plain = i;
    }
    if (pending->length) mime_to_utf8(pending_charset, pending_charset_length, pending->str, pending->length, out);
    append_plain(out, value + plain, length - plain);
    strbuf_free(pending);
    return true;
}

// ── parameters ─────────────────────────────────────────────────────────

void mime_header_token(const char* value, size_t length, StrBuf* out) {
    size_t i = 0;
    while (i < length && (value[i] == ' ' || value[i] == '\t')) i++;
    size_t start = i;
    while (i < length && value[i] != ';' && value[i] != ' ' && value[i] != '\t') i++;
    for (size_t k = start; k < i; k++) {
        char c = value[k];
        strbuf_append_char(out, c >= 'A' && c <= 'Z' ? (char)(c + 32) : c);
    }
}

typedef struct MimeParam {
    int section;                // -1 when not split (RFC 2231 continuations)
    bool extended;              // name* : charset'lang'percent-encoded
    const char* value;
    size_t value_length;
    bool quoted;
} MimeParam;

#define MIME_MAX_PARAM_SECTIONS 64

// The parameters named `name`, in order of appearance.
static int collect_params(const char* v, size_t n, const char* name, MimeParam* params, int max) {
    size_t name_length = strlen(name);
    int count = 0;
    size_t i = 0;
    while (i < n && v[i] != ';') {
        if (v[i] == '"') {
            i++;
            while (i < n && v[i] != '"') i += v[i] == '\\' ? 2 : 1;
        }
        i++;
    }
    while (i < n) {
        while (i < n && (v[i] == ';' || v[i] == ' ' || v[i] == '\t')) i++;
        size_t key = i;
        while (i < n && v[i] != '=' && v[i] != ';') i++;
        size_t key_end = i;
        while (key_end > key && (v[key_end - 1] == ' ' || v[key_end - 1] == '\t')) key_end--;
        if (i >= n || v[i] != '=') continue;
        i++;
        while (i < n && (v[i] == ' ' || v[i] == '\t')) i++;
        MimeParam param = {-1, false, NULL, 0, false};
        if (i < n && v[i] == '"') {
            param.quoted = true;
            param.value = v + ++i;
            while (i < n && v[i] != '"') i += v[i] == '\\' ? 2 : 1;
            param.value_length = (i < n ? i : n) - (size_t)(param.value - v);
            if (i < n) i++;
        } else {
            param.value = v + i;
            while (i < n && v[i] != ';') i++;
            size_t end = i;
            while (end > (size_t)(param.value - v) && (v[end - 1] == ' ' || v[end - 1] == '\t')) end--;
            param.value_length = end - (size_t)(param.value - v);
        }
        // key: name, name*, name*0, name*0*
        size_t base = key_end - key;
        const char* star = memchr(v + key, '*', key_end - key);
        if (star) base = (size_t)(star - (v + key));
        if (base != name_length || strncasecmp(v + key, name, name_length) != 0) continue;
        if (star) {
            const char* p = star + 1;
            const char* end = v + key_end;
            if (p < end && *p >= '0' && *p <= '9') {
                int section = 0;
                while (p < end && *p >= '0' && *p <= '9') section = section * 10 + (*p++ - '0');
                param.section = section;
                param.extended = p < end && *p == '*';
            } else {
                param.extended = true;
            }
        }
        if (count < max) params[count++] = param;
    }
    return count;
}

static void append_unquoted(StrBuf* out, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) i++;
        strbuf_append_char(out, s[i]);
    }
}

static void append_percent_decoded(StrBuf* out, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
            strbuf_append_char(out, (char)(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2])));
            i += 2;
        } else {
            strbuf_append_char(out, s[i]);
        }
    }
}

bool mime_header_param(const char* value, size_t length, const char* name, bool decode_words, StrBuf* out) {
    MimeParam params[MIME_MAX_PARAM_SECTIONS];
    int count = collect_params(value, length, name, params, MIME_MAX_PARAM_SECTIONS);
    if (count == 0) return false;
    bool sectioned = false;
    for (int i = 0; i < count; i++) sectioned = sectioned || params[i].section >= 0;
    if (!sectioned) {
        const MimeParam* p = &params[0];
        if (p->extended) {
            // charset'language'percent-encoded (RFC 2231 §4)
            const char* q1 = memchr(p->value, '\'', p->value_length);
            const char* q2 = q1 ? memchr(q1 + 1, '\'', p->value_length - (size_t)(q1 + 1 - p->value)) : NULL;
            StrBuf* bytes = strbuf_new();
            if (!bytes) return false;
            if (q2) {
                append_percent_decoded(bytes, q2 + 1, p->value_length - (size_t)(q2 + 1 - p->value));
                mime_to_utf8(p->value, (size_t)(q1 - p->value), bytes->str, bytes->length, out);
            } else {
                append_percent_decoded(bytes, p->value, p->value_length);
                mime_to_utf8(NULL, 0, bytes->str, bytes->length, out);
            }
            strbuf_free(bytes);
            return true;
        }
        // senders often put RFC 2047 words in quoted filenames; decode them there
        StrBuf* raw = strbuf_new();
        if (!raw) return false;
        append_unquoted(raw, p->value, p->value_length);
        if (decode_words) mime_decode_header(raw->str, raw->length, out);
        else strbuf_append_str_n(out, raw->str, raw->length);
        strbuf_free(raw);
        return true;
    }
    // RFC 2231 continuations: section 0 names the charset when extended
    StrBuf* bytes = strbuf_new();
    if (!bytes) return false;
    const char* charset = NULL;
    size_t charset_length = 0;
    for (int section = 0; section < MIME_MAX_PARAM_SECTIONS; section++) {
        const MimeParam* p = NULL;
        for (int i = 0; i < count && !p; i++) if (params[i].section == section) p = &params[i];
        if (!p) break;
        const char* text = p->value;
        size_t text_length = p->value_length;
        if (section == 0 && p->extended) {
            const char* q1 = memchr(text, '\'', text_length);
            const char* q2 = q1 ? memchr(q1 + 1, '\'', text_length - (size_t)(q1 + 1 - text)) : NULL;
            if (q2) {
                charset = text;
                charset_length = (size_t)(q1 - text);
                text_length -= (size_t)(q2 + 1 - text);
                text = q2 + 1;
            }
        }
        if (p->extended) append_percent_decoded(bytes, text, text_length);
        else append_unquoted(bytes, text, text_length);
    }
    mime_to_utf8(charset, charset_length, bytes->str, bytes->length, out);
    strbuf_free(bytes);
    return true;
}

// ── addresses ──────────────────────────────────────────────────────────

static void trim(const char** s, size_t* n) {
    while (*n && ((*s)[0] == ' ' || (*s)[0] == '\t' || (*s)[0] == '\r' || (*s)[0] == '\n')) (*s)++, (*n)--;
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\r' || (*s)[*n - 1] == '\n')) (*n)--;
}

// A display name without its quotes and comments, RFC 2047 words decoded.
static void decode_display_name(const char* s, size_t n, StrBuf* raw, StrBuf* out, StrBuf* comment) {
    strbuf_reset(raw);
    int depth = 0;
    // a quoted string's "(" is text, not a comment (RFC 5322 §3.2.4): "Ann (Forge)"
    bool quoted = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!depth && c == '"') { quoted = !quoted; continue; }
        if (depth) {
            if (c == '\\' && i + 1 < n) {
                i++;
                if (comment) strbuf_append_char(comment, s[i]);
                continue;
            }
            if (c == '(') depth++;
            else if (c == ')') depth--;
            else if (comment) strbuf_append_char(comment, c);
            continue;
        }
        if (c == '(' && !quoted) { depth = 1; continue; }
        if (c == '\\' && i + 1 < n) c = s[++i];
        strbuf_append_char(raw, c);
    }
    const char* t = raw->str;
    size_t tn = raw->length;
    trim(&t, &tn);
    mime_decode_header(t, tn, out);
}

// One mailbox: `Name <addr>`, `addr`, or `addr (Name)`.
static bool emit_mailbox(const char* s, size_t n, MimeAddressVisitor visit, void* user) {
    trim(&s, &n);
    if (!n) return false;
    bool quoted = false;
    int depth = 0;
    const char* angle = NULL;
    for (size_t i = 0; i < n && !angle; i++) {
        char c = s[i];
        if (quoted) { if (c == '\\') i++; else if (c == '"') quoted = false; continue; }
        if (depth) { if (c == '\\') i++; else if (c == '(') depth++; else if (c == ')') depth--; continue; }
        if (c == '"') quoted = true;
        else if (c == '(') depth = 1;
        else if (c == '<') angle = s + i;
    }
    StrBuf* raw = strbuf_new();
    StrBuf* name = strbuf_new();
    StrBuf* address = strbuf_new();
    if (!raw || !name || !address) {
        if (raw) strbuf_free(raw);
        if (name) strbuf_free(name);
        if (address) strbuf_free(address);
        return false;
    }
    if (angle) {
        const char* close = memchr(angle, '>', n - (size_t)(angle - s));
        const char* a = angle + 1;
        size_t an = (close ? close : s + n) - a;
        // drop an obsolete source route: <@relay:user@host>
        const char* route = an && a[0] == '@' ? memchr(a, ':', an) : NULL;
        if (route) an -= (size_t)(route + 1 - a), a = route + 1;
        trim(&a, &an);
        strbuf_append_str_n(address, a, an);
        decode_display_name(s, (size_t)(angle - s), raw, name, NULL);
    } else {
        // the address without comments; an old-style comment is the name
        StrBuf* comment = strbuf_new();
        int d = 0;
        for (size_t i = 0; i < n; i++) {
            char c = s[i];
            if (d) {
                if (c == '(') d++;
                else if (c == ')') d--;
                else if (comment) strbuf_append_char(comment, c);
                continue;
            }
            if (c == '(') { d = 1; continue; }
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') strbuf_append_char(address, c);
        }
        if (comment && comment->length) {
            const char* t = comment->str;
            size_t tn = comment->length;
            trim(&t, &tn);
            mime_decode_header(t, tn, name);
        }
        if (comment) strbuf_free(comment);
    }
    bool emitted = address->length > 0;
    if (emitted) visit(user, name->str ? name->str : "", name->length, address->str, address->length);
    strbuf_free(raw);
    strbuf_free(name);
    strbuf_free(address);
    return emitted;
}

int mime_parse_addresses(const char* value, size_t length, MimeAddressVisitor visit, void* user) {
    int count = 0;
    bool quoted = false;
    bool angle = false;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; i++) {
        char c = i < length ? value[i] : ',';
        if (i < length) {
            if (quoted) { if (c == '\\') i++; else if (c == '"') quoted = false; continue; }
            if (depth) { if (c == '\\') i++; else if (c == '(') depth++; else if (c == ')') depth--; continue; }
            if (c == '"') { quoted = true; continue; }
            if (c == '(') { depth = 1; continue; }
            if (c == '<') { angle = true; continue; }
            if (c == '>') { angle = false; continue; }
            if (angle) continue;
            // a group's display name ends at ':'; its members follow
            if (c == ':') { start = i + 1; continue; }
            if (c != ',' && c != ';') continue;
        }
        if (emit_mailbox(value + start, i - start, visit, user)) count++;
        start = i + 1;
    }
    return count;
}
