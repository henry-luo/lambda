// fts_token.cpp — tokens and their normal form (FTX3, FTX4).
//
// A token is a maximal run of letters, digits and combining marks; every
// other code point separates tokens, except that a Han ideograph, a kana or a
// Hangul syllable is a token of its own (a unigram), marks after it included.
// A token's normal form folds each code point to the canonical member of its
// simple case folding orbit (S17.7.1) — RE2's table, so a token equals every
// spelling RE2's (?i) matches, which keeps the lib/grep prefilter sound — and
// under unaccent first decomposes, drops nonspacing marks and recomposes.

#include "fts_internal.hpp"
#include "../mem_grow.h"
#include "../str.h"

#include <re2/unicode_casefold.h>
#include <utf8proc.h>
#include <string.h>

void fts_tokenizer_init(FtsTokenizer* t, bool fold, bool unaccent) {
    memset(t, 0, sizeof(*t));
    t->fold = fold;
    t->unaccent = unaccent;
}

void fts_tokenizer_release(FtsTokenizer* t) {
    if (t->tokens) mem_free(t->tokens);
    if (t->norm) mem_free(t->norm);
    if (t->runes) mem_free(t->runes);
    memset(t, 0, sizeof(*t));
}

uint32_t fts_fold_rune(uint32_t r) {
    if (r < 0x80) return r >= 'A' && r <= 'Z' ? r + 32 : r;
    // the orbit's smallest member, lowered: one representative per orbit
    // (K, k and KELVIN SIGN all give k; S, s and LONG S give s)
    uint32_t low = r, x = r;
    for (int guard = 0; guard < 8; guard++) {
        const re2::CaseFold* f = re2::LookupCaseFold(re2::unicode_casefold, re2::num_unicode_casefold, (re2::Rune)x);
        if (!f || (re2::Rune)x < f->lo) break;
        x = (uint32_t)re2::ApplyFold(f, (re2::Rune)x);
        if (x == r) break;
        if (x < low) low = x;
    }
    return (uint32_t)utf8proc_tolower((utf8proc_int32_t)low);
}

enum FtsClass { FTS_SEP = 0, FTS_WORD, FTS_MARK, FTS_UNIGRAM };

// the scripts written without spaces, and Hangul (FTX3): a token per character
static bool is_unigram(uint32_t c) {
    return (c >= 0x2E80 && c <= 0x2FDF) ||    // CJK radicals, Kangxi radicals
           (c >= 0x3005 && c <= 0x3007) ||    // 々 〆 〇
           (c >= 0x3021 && c <= 0x3029) ||    // Hangzhou numerals
           (c >= 0x3038 && c <= 0x303B) ||
           (c >= 0x3040 && c <= 0x30FF) ||    // Hiragana, Katakana
           (c >= 0x31F0 && c <= 0x31FF) ||    // Katakana phonetic extensions
           (c >= 0x3400 && c <= 0x4DBF) ||    // CJK extension A
           (c >= 0x4E00 && c <= 0x9FFF) ||    // CJK unified ideographs
           (c >= 0xAC00 && c <= 0xD7AF) ||    // Hangul syllables
           (c >= 0xF900 && c <= 0xFAFF) ||    // CJK compatibility ideographs
           (c >= 0xFF66 && c <= 0xFF9F) ||    // halfwidth katakana
           (c >= 0x1B000 && c <= 0x1B16F) ||  // kana supplement and extensions
           (c >= 0x20000 && c <= 0x323AF);    // CJK extensions B and later
}

static FtsClass classify(uint32_t c) {
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? FTS_WORD : FTS_SEP;
    }
    switch (utf8proc_category((utf8proc_int32_t)c)) {
    case UTF8PROC_CATEGORY_MN: case UTF8PROC_CATEGORY_MC: case UTF8PROC_CATEGORY_ME:
        return FTS_MARK;
    case UTF8PROC_CATEGORY_LU: case UTF8PROC_CATEGORY_LL: case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM: case UTF8PROC_CATEGORY_LO:
    case UTF8PROC_CATEGORY_ND: case UTF8PROC_CATEGORY_NL: case UTF8PROC_CATEGORY_NO:
        return is_unigram(c) ? FTS_UNIGRAM : FTS_WORD;
    default:
        return FTS_SEP;
    }
}

static bool norm_reserve(FtsTokenizer* t, size_t more) {
    return mem_grow_array_raw((void**)&t->norm, 1, &t->norm_cap, t->norm_length + more, 256, MEM_CAT_TEMP);
}

static void push_rune(FtsTokenizer* t, uint32_t r) {
    if (t->fold) r = fts_fold_rune(r);
    t->norm_length += str_utf8_encode(r, t->norm + t->norm_length, 4);
}

// the normal form of a token's bytes, appended to t->norm; a token holds only
// decoded code points, and a code point grows by at most 4 bytes in either step
static bool normalize(FtsTokenizer* t, const char* text, size_t length) {
    if (!t->unaccent) {
        if (!norm_reserve(t, 4 * length)) return false;
        size_t i = 0;
        while (i < length) {
            uint32_t r = 0;
            int n = str_utf8_decode(text + i, length - i, &r);
            if (n <= 0) return false;
            push_rune(t, r);
            i += (size_t)n;
        }
        return true;
    }
    // unaccent: decompose, drop nonspacing marks, recompose — a Hangul
    // syllable decomposes into jamo, which are letters, and recomposes whole
    size_t used = 0;
    size_t i = 0;
    while (i < length) {
        uint32_t r = 0;
        int n = str_utf8_decode(text + i, length - i, &r);
        if (n <= 0) return false;
        i += (size_t)n;
        utf8proc_int32_t parts[8];
        int boundclass = 0;
        utf8proc_ssize_t k = utf8proc_decompose_char((utf8proc_int32_t)r, parts, 8,
                                                     UTF8PROC_DECOMPOSE, &boundclass);
        if (k < 0 || k > 8) {
            parts[0] = (utf8proc_int32_t)r;
            k = 1;
        }
        if (!mem_grow_array_raw((void**)&t->runes, sizeof(int32_t), &t->runes_cap, used + (size_t)k + 1, 32, MEM_CAT_TEMP)) return false;
        for (utf8proc_ssize_t j = 0; j < k; j++) {
            if (utf8proc_category(parts[j]) != UTF8PROC_CATEGORY_MN) t->runes[used++] = parts[j];
        }
    }
    utf8proc_ssize_t composed = used ? utf8proc_normalize_utf32((utf8proc_int32_t*)t->runes, (utf8proc_ssize_t)used, UTF8PROC_COMPOSE) : 0;
    if (composed < 0) composed = (utf8proc_ssize_t)used;
    if (!norm_reserve(t, 4 * (size_t)composed)) return false;
    for (utf8proc_ssize_t j = 0; j < composed; j++) push_rune(t, (uint32_t)t->runes[j]);
    return true;
}

bool fts_tokenize(FtsTokenizer* t, const char* text, size_t length) {
    t->count = 0;
    t->norm_length = 0;
    t->source = text;
    size_t i = 0, chars = 0;
    // the token being built: none, a word, or a unigram (which takes marks only)
    enum { NONE, IN_WORD, IN_UNIGRAM } state = NONE;
    FtsToken* cur = NULL;
    bool ascii = true, upper = false;  // what the current token holds
    auto close = [&](size_t end) -> bool {
        if (!cur) return true;
        cur->length = end - cur->start;
        const char* bytes = text + cur->start;
        if ((ascii && (!t->fold || !upper)) || (!t->fold && !t->unaccent)) {
            // already in normal form: valid UTF-8 re-encodes to itself, ASCII
            // has no marks, and folding leaves lowercase ASCII alone
            cur->in_source = true;
            cur->norm = cur->start;
            cur->norm_length = cur->length;
        } else if (ascii) {
            if (!norm_reserve(t, cur->length)) return false;
            cur->norm = t->norm_length;
            for (size_t k = 0; k < cur->length; k++) {
                unsigned char c = (unsigned char)bytes[k];
                t->norm[t->norm_length++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            }
            cur->norm_length = cur->length;
        } else {
            cur->norm = t->norm_length;
            if (!normalize(t, bytes, cur->length)) return false;
            cur->norm_length = t->norm_length - cur->norm;
        }
        cur = NULL;
        return true;
    };
    auto open = [&](size_t start, size_t at_chars) -> bool {
        if (!mem_grow_array_raw((void**)&t->tokens, sizeof(FtsToken), &t->cap, t->count + 1, 64, MEM_CAT_TEMP)) return false;
        cur = &t->tokens[t->count++];
        memset(cur, 0, sizeof(*cur));
        cur->start = start;
        cur->chars = at_chars;
        ascii = true;
        upper = false;
        return true;
    };
    while (i < length) {
        unsigned char b = (unsigned char)text[i];
        if (b < 0x80) {
            // ASCII: a whole run of letters and digits, or of other ASCII
            // bytes, at a time; one code point per byte
            bool up = false;
            size_t n = str_ascii_alnum_span(text + i, length - i, &up);
            if (n) {
                if (state != IN_WORD) {
                    if (!close(i) || !open(i, chars)) return false;
                    state = IN_WORD;
                }
                upper = upper || up;
            } else {
                n = str_ascii_nonalnum_span(text + i, length - i);
                if (!close(i)) return false;
                state = NONE;
            }
            i += n;
            chars += n;
            continue;
        }
        uint32_t r = 0;
        int n = str_utf8_decode(text + i, length - i, &r);
        // an invalid byte separates; it counts as a code point unless it is a
        // continuation byte, as str_utf8_count counts
        FtsClass cls = n > 0 ? classify(r) : FTS_SEP;
        if (n <= 0) n = 1;
        size_t at = i, at_chars = chars;
        chars += (b & 0xC0) != 0x80 ? 1 : 0;
        i += (size_t)n;
        switch (cls) {
        case FTS_SEP:
            if (!close(at)) return false;
            state = NONE;
            break;
        case FTS_WORD:
            if (state != IN_WORD) {
                if (!close(at) || !open(at, at_chars)) return false;
                state = IN_WORD;
            }
            ascii = false;
            break;
        case FTS_MARK:
            if (state == NONE) {
                if (!open(at, at_chars)) return false;
                state = IN_WORD;
            }
            ascii = false;
            break;
        case FTS_UNIGRAM:
            if (!close(at) || !open(at, at_chars)) return false;
            state = IN_UNIGRAM;
            ascii = false;
            break;
        }
    }
    return close(length);
}
