// speech_markup.cpp — utterance text for speech engines.
//
// Plain strings pass through; <speak> markup (Radiant_Design_Speech.md §4.2)
// is validated against the portable core and translated to SSML, escaping all
// text so data can never break the markup. Engines report word ranges in
// UTF-16 units of what they were given, so every emitted unit records the
// plain-text code point it came from.

#include "speech.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/mem.h"
#include "../lib/utf.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint32_t SPEECH_TEXT_LIMIT = 32768;   // plain-text code points per utterance
static const int SPEECH_MARKUP_DEPTH = 16;

static bool text_fail(SpeechText* t, const char* message) {
    snprintf(t->error, sizeof(t->error), "speak markup: %s", message);
    return false;
}
static bool map_reserve(SpeechText* t, uint32_t extra) {
    if (t->units + extra <= t->capacity) return true;
    uint32_t capacity = t->capacity ? t->capacity : 256;
    while (capacity < t->units + extra) capacity *= 2;
    auto* grown = (uint32_t*)mem_realloc(t->char_of, capacity * sizeof(uint32_t), MEM_CAT_RENDER);
    if (!grown) return false;
    t->char_of = grown; t->capacity = capacity;
    return true;
}
// appends UTF-8 bytes; every UTF-16 unit they encode maps to `char_index`.
static bool emit(SpeechText* t, const char* bytes, size_t n, uint32_t char_index) {
    for (size_t i = 0; i < n;) {
        uint32_t cp = 0;
        int used = utf8_decode(bytes + i, n - i, &cp);
        if (used <= 0) return false;
        uint32_t units = cp >= 0x10000 ? 2 : 1;
        if (!map_reserve(t, units)) return false;
        for (uint32_t u = 0; u < units; u++) t->char_of[t->units++] = char_index;
        i += (size_t)used;
    }
    strbuf_append_str_n(t->text, bytes, n);
    return true;
}
static bool emit_markup(SpeechText* t, const char* ascii) {
    return emit(t, ascii, strlen(ascii), SPEECH_NO_CHAR);
}
// text content: each code point becomes one plain-text character.
static bool emit_text(SpeechText* t, const char* utf8, size_t n, bool escape) {
    for (size_t i = 0; i < n;) {
        uint32_t cp = 0;
        int used = utf8_decode(utf8 + i, n - i, &cp);
        if (used <= 0) return text_fail(t, "text is not valid UTF-8");
        if (t->chars >= SPEECH_TEXT_LIMIT) return text_fail(t, "utterance is longer than 32768 characters");
        const char* entity = !escape ? nullptr : cp == '&' ? "&amp;" : cp == '<' ? "&lt;" : cp == '>' ? "&gt;"
            : cp == '"' ? "&quot;" : cp == '\'' ? "&apos;" : nullptr;
        bool ok = entity ? emit(t, entity, strlen(entity), t->chars) : emit(t, utf8 + i, (size_t)used, t->chars);
        if (!ok) return text_fail(t, "out of memory");
        t->chars++;
        i += (size_t)used;
    }
    return true;
}
// attribute values are escaped too; they never count as spoken characters.
static bool emit_attr(SpeechText* t, const char* name, const char* value) {
    if (!emit_markup(t, " ") || !emit_markup(t, name) || !emit_markup(t, "=\"")) return false;
    for (const char* p = value; *p; p++) {
        const char* entity = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '"' ? "&quot;" : nullptr;
        char one[2] = {*p, 0};
        if (!emit(t, entity ? entity : one, entity ? strlen(entity) : 1, SPEECH_NO_CHAR)) return false;
    }
    return emit_markup(t, "\"");
}

static bool speech_text_begin(SpeechText* out, bool ssml) {
    memset(out, 0, sizeof(*out));
    out->text = strbuf_new();
    out->ssml = ssml;
    return out->text != nullptr;
}

bool speech_text_from_string(const char* utf8, size_t length, SpeechText* out) {
    if (!speech_text_begin(out, false)) return false;
    if (!utf8 || !length) return text_fail(out, "utterance text is empty");
    return emit_text(out, utf8, length, false);
}

// ---------------------------------------------------------------- markup

enum AttrKind { ATTR_TEXT, ATTR_NUMBER };
struct AttrSpec { const char* name; AttrKind kind; double lo, hi; };
struct ElementSpec {
    const char* tag;
    AttrSpec attrs[3];
    int attr_count;
    bool needs_attr;        // at least one attribute is required
    bool empty;             // no content allowed (<break>, <mark>)
    bool text_only;         // text content only (<say-as>, <sub>)
};
static const ElementSpec SPEECH_ELEMENTS[] = {
    {"speak", {{"lang", ATTR_TEXT, 0, 0}}, 1, false, false, false},
    {"break", {{"time", ATTR_NUMBER, 0, 10}}, 1, true, true, false},
    {"prosody", {{"rate", ATTR_NUMBER, 0.1, 10}, {"pitch", ATTR_NUMBER, 0, 2}, {"volume", ATTR_NUMBER, 0, 1}}, 3, true, false, false},
    {"emphasis", {{"level", ATTR_TEXT, 0, 0}}, 1, false, false, false},
    {"say-as", {{"interpret", ATTR_TEXT, 0, 0}}, 1, true, false, true},
    {"sub", {{"alias", ATTR_TEXT, 0, 0}}, 1, true, false, true},
    {"lang", {{"lang", ATTR_TEXT, 0, 0}}, 1, true, false, false},
    {"mark", {{"name", ATTR_TEXT, 0, 0}}, 1, true, true, false},
};

struct AttrValues { const char* text[3]; double number[3]; bool present[3]; };

static const ElementSpec* element_spec(const char* tag) {
    for (const ElementSpec& spec : SPEECH_ELEMENTS) if (tag && !strcmp(spec.tag, tag)) return &spec;
    return nullptr;
}
static bool one_of(const char* value, const char* const* allowed) {
    for (; *allowed; allowed++) if (!strcmp(value, *allowed)) return true;
    return false;
}
static bool read_attrs(SpeechText* t, const ElementSpec* spec, const ElementReader& el, AttrValues* values) {
    memset(values, 0, sizeof(*values));
    char message[160];
    auto it = el.attrs();
    const char* key; ItemReader value;
    while (it.next(&key, &value)) {
        int index = -1;
        for (int i = 0; i < spec->attr_count; i++) if (key && !strcmp(key, spec->attrs[i].name)) index = i;
        if (index < 0) {
            snprintf(message, sizeof(message), "<%s> has no attribute '%s'", spec->tag, key ? key : "?");
            return text_fail(t, message);
        }
        const AttrSpec& a = spec->attrs[index];
        if (a.kind == ATTR_NUMBER) {
            double n = value.isNumber() ? value.asFloat() : NAN;
            if (!isfinite(n) || n < a.lo || n > a.hi) {
                snprintf(message, sizeof(message), "<%s %s> must be a number from %g to %g", spec->tag, a.name, a.lo, a.hi);
                return text_fail(t, message);
            }
            values->number[index] = n;
        } else {
            const char* s = value.isString() ? value.cstring() : value.isSymbol() ? value.asSymbol()->chars : nullptr;
            if (!s || !*s || strlen(s) > 63) {
                snprintf(message, sizeof(message), "<%s %s> must be a non-empty name of at most 63 bytes", spec->tag, a.name);
                return text_fail(t, message);
            }
            values->text[index] = s;
        }
        values->present[index] = true;
    }
    bool any = false;
    for (int i = 0; i < spec->attr_count; i++) any = any || values->present[i];
    if (spec->needs_attr && !any) {
        snprintf(message, sizeof(message), "<%s> needs %s%s", spec->tag,
            spec->attr_count > 1 ? "one of its attributes" : "the attribute ", spec->attr_count > 1 ? "" : spec->attrs[0].name);
        return text_fail(t, message);
    }
    return true;
}

static bool translate(SpeechText* t, const ElementReader& el, int depth, bool root);

static bool translate_children(SpeechText* t, const ElementReader& el, const ElementSpec* spec, int depth) {
    char message[160];
    for (int64_t i = 0; i < el.childCount(); i++) {
        ItemReader child = el.childAt(i);
        if (child.isString()) {
            String* s = child.asString();
            if (spec->empty) {
                snprintf(message, sizeof(message), "<%s> takes no content", spec->tag);
                return text_fail(t, message);
            }
            if (s && !emit_text(t, s->chars, s->len, true)) return false;
        } else if (child.isElement()) {
            if (spec->empty || spec->text_only) {
                snprintf(message, sizeof(message), "<%s> takes %s", spec->tag, spec->empty ? "no content" : "text only");
                return text_fail(t, message);
            }
            if (!translate(t, child.asElement(), depth + 1, false)) return false;
        } else if (!child.isNull()) {
            snprintf(message, sizeof(message), "<%s> content must be text or speech elements", spec->tag);
            return text_fail(t, message);
        }
    }
    return true;
}

static bool translate(SpeechText* t, const ElementReader& el, int depth, bool root) {
    char message[160];
    if (depth > SPEECH_MARKUP_DEPTH) return text_fail(t, "markup is nested more than 16 levels");
    const char* tag = el.tagName();
    const ElementSpec* spec = element_spec(tag);
    if (!spec) {
        snprintf(message, sizeof(message), "unknown element <%s>", tag ? tag : "?");
        return text_fail(t, message);
    }
    if (root != !strcmp(tag, "speak"))
        return text_fail(t, root ? "the utterance must be a <speak> element" : "<speak> cannot be nested");
    AttrValues v;
    if (!read_attrs(t, spec, el, &v)) return false;
    bool ok = true;
    if (!strcmp(tag, "speak")) {
        ok = emit_markup(t, "<speak") && (!v.present[0] || emit_attr(t, "xml:lang", v.text[0])) && emit_markup(t, ">");
    } else if (!strcmp(tag, "break")) {
        char ms[32]; snprintf(ms, sizeof(ms), "%dms", (int)lround(v.number[0] * 1000));
        ok = emit_markup(t, "<break") && emit_attr(t, "time", ms) && emit_markup(t, "/>");
        return ok || text_fail(t, "out of memory");
    } else if (!strcmp(tag, "mark")) {
        ok = emit_markup(t, "<mark") && emit_attr(t, "name", v.text[0]) && emit_markup(t, "/>");
        return ok || text_fail(t, "out of memory");
    } else if (!strcmp(tag, "prosody")) {
        char buf[32];
        ok = emit_markup(t, "<prosody");
        if (ok && v.present[0]) { snprintf(buf, sizeof(buf), "%d%%", (int)lround(v.number[0] * 100)); ok = emit_attr(t, "rate", buf); }
        if (ok && v.present[1]) { snprintf(buf, sizeof(buf), "%+d%%", (int)lround((v.number[1] - 1) * 100)); ok = emit_attr(t, "pitch", buf); }
        if (ok && v.present[2]) {
            if (v.number[2] <= 0) snprintf(buf, sizeof(buf), "silent");
            else snprintf(buf, sizeof(buf), "%+.1fdB", 20 * log10(v.number[2]));
            ok = emit_attr(t, "volume", buf);
        }
        ok = ok && emit_markup(t, ">");
    } else if (!strcmp(tag, "emphasis")) {
        static const char* const levels[] = {"strong", "moderate", "reduced", nullptr};
        if (v.present[0] && !one_of(v.text[0], levels)) return text_fail(t, "<emphasis level> must be strong, moderate or reduced");
        ok = emit_markup(t, "<emphasis") && (!v.present[0] || emit_attr(t, "level", v.text[0])) && emit_markup(t, ">");
    } else if (!strcmp(tag, "say-as")) {
        static const char* const kinds[] = {"characters", "digits", "date", "time", nullptr};
        if (!one_of(v.text[0], kinds)) return text_fail(t, "<say-as interpret> must be characters, digits, date or time");
        ok = emit_markup(t, "<say-as") && emit_attr(t, "interpret-as", v.text[0]) && emit_markup(t, ">");
    } else if (!strcmp(tag, "sub")) {
        ok = emit_markup(t, "<sub") && emit_attr(t, "alias", v.text[0]) && emit_markup(t, ">");
    } else if (!strcmp(tag, "lang")) {
        ok = emit_markup(t, "<lang") && emit_attr(t, "xml:lang", v.text[0]) && emit_markup(t, ">");
    }
    if (!ok) return text_fail(t, "out of memory");
    if (!translate_children(t, el, spec, depth)) return false;
    char close[24]; snprintf(close, sizeof(close), "</%s>", tag);
    return emit_markup(t, close) || text_fail(t, "out of memory");
}

bool speech_text_from_markup(Item element, SpeechText* out) {
    if (!speech_text_begin(out, true)) return false;
    ItemReader reader(element.to_const());
    if (!reader.isElement()) return text_fail(out, "the utterance must be a string or a <speak> element");
    if (!translate(out, reader.asElement(), 0, true)) return false;
    if (!out->chars) return text_fail(out, "the utterance has no text");
    return true;
}

void speech_text_release(SpeechText* text) {
    if (!text) return;
    if (text->text) strbuf_free(text->text);
    mem_free(text->char_of);
    memset(text, 0, sizeof(*text));
}

bool speech_text_char_range(const SpeechText* text, uint32_t start, uint32_t length,
        uint32_t* char_index, uint32_t* char_length) {
    if (!text || !text->char_of || start >= text->units) return false;
    uint32_t end = length > text->units - start ? text->units : start + length;
    uint32_t first = SPEECH_NO_CHAR, last = 0;
    for (uint32_t u = start; u < end; u++) {
        uint32_t c = text->char_of[u];
        if (c == SPEECH_NO_CHAR) continue;
        if (first == SPEECH_NO_CHAR) first = c;
        last = c;
    }
    if (first == SPEECH_NO_CHAR) return false;
    *char_index = first;
    *char_length = last - first + 1;
    return true;
}
