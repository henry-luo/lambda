/**
 * @file utf.c
 * @brief Core Unicode codec and classification utilities.
 *
 * Single source of truth for UTF-8 encode/decode, UTF-16 surrogate pairs,
 * and common codepoint classification. See utf.h for API documentation.
 */

#include "utf.h"
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
 *  UTF-8 Codec
 * ══════════════════════════════════════════════════════════════════════ */

/* safe unaligned 64-bit load (internal only) */
static inline uint64_t _utf_load_u64(const void* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

size_t utf8_encode(uint32_t codepoint, char buf[4]) {
    if (!buf) return 0;
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0; /* surrogate */
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0; /* invalid codepoint */
}

size_t utf8_encode_z(uint32_t codepoint, char buf[5]) {
    if (!buf) return 0;
    size_t n = utf8_encode(codepoint, buf);
    buf[n] = '\0';
    return n;
}

int utf8_decode(const char* s, size_t len, uint32_t* out) {
    if (!s || len == 0 || !out) return -1;
    unsigned char b = (unsigned char)s[0];

    if (b < 0x80) {
        *out = b;
        return 1;
    }
    if ((b & 0xE0) == 0xC0) {
        if (len < 2) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        if (cp < 0x80) return -1; /* overlong */
        *out = cp;
        return 2;
    }
    if ((b & 0xF0) == 0xE0) {
        if (len < 3) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x0F) << 12) |
                      (((unsigned char)s[1] & 0x3F) << 6) |
                      ((unsigned char)s[2] & 0x3F);
        if (cp < 0x800) return -1;  /* overlong */
        if (cp >= 0xD800 && cp <= 0xDFFF) return -1; /* surrogate */
        *out = cp;
        return 3;
    }
    if ((b & 0xF8) == 0xF0) {
        if (len < 4) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80 ||
            ((unsigned char)s[3] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x07) << 18) |
                      (((unsigned char)s[1] & 0x3F) << 12) |
                      (((unsigned char)s[2] & 0x3F) << 6) |
                      ((unsigned char)s[3] & 0x3F);
        if (cp < 0x10000) return -1;  /* overlong */
        if (cp > 0x10FFFF) return -1; /* out of range */
        *out = cp;
        return 4;
    }
    return -1; /* invalid lead byte */
}

size_t utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0; /* invalid lead byte */
}

size_t utf8_count(const char* s, size_t len) {
    if (!s) return 0;
    size_t count = 0;
    size_t i = 0;
    /* SWAR: count bytes that are NOT continuation bytes (0x80..0xBF). */
    for (; i + 8 <= len; i += 8) {
        uint64_t w = _utf_load_u64(s + i);
        uint64_t a = w & 0x8080808080808080ULL;          /* high bit of each byte */
        uint64_t b = (w << 1) & 0x8080808080808080ULL;   /* bit 6 shifted to high */
        /* continuation = high bit set AND bit 6 clear: a & ~b */
        uint64_t cont = a & ~b;
#if defined(__GNUC__) || defined(__clang__)
        count += 8 - (size_t)__builtin_popcountll(cont);
#else
        for (int j = 0; j < 8; j++) {
            if (((unsigned char)s[i + j] & 0xC0) != 0x80) count++;
        }
#endif
    }
    for (; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) count++;
    }
    return count;
}

bool utf8_valid(const char* s, size_t len) {
    if (!s) return true;
    size_t i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)s[i];
        size_t seq_len;
        uint32_t cp;

        if (b < 0x80) { i++; continue; }
        else if ((b & 0xE0) == 0xC0) { seq_len = 2; cp = b & 0x1F; if (cp < 2) return false; }
        else if ((b & 0xF0) == 0xE0) { seq_len = 3; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { seq_len = 4; cp = b & 0x07; }
        else return false;

        if (i + seq_len > len) return false;
        for (size_t j = 1; j < seq_len; j++) {
            unsigned char c = (unsigned char)s[i + j];
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (seq_len == 2 && cp < 0x80) return false;
        if (seq_len == 3 && cp < 0x800) return false;
        if (seq_len == 4 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += seq_len;
    }
    return true;
}

size_t utf8_char_to_byte(const char* s, size_t len, size_t char_index) {
    if (!s || len == 0) return char_index == 0 ? 0 : (size_t)-1;
    size_t ci = 0, bi = 0;
    while (bi < len && ci < char_index) {
        unsigned char b = (unsigned char)s[bi];
        size_t seq;
        if (b < 0x80) seq = 1;
        else if ((b & 0xE0) == 0xC0) seq = 2;
        else if ((b & 0xF0) == 0xE0) seq = 3;
        else if ((b & 0xF8) == 0xF0) seq = 4;
        else seq = 1;
        if (bi + seq > len) seq = 1;
        bi += seq;
        ci++;
    }
    return (ci == char_index) ? bi : (size_t)-1;
}

size_t utf8_byte_to_char(const char* s, size_t len, size_t byte_offset) {
    if (!s) return 0;
    if (byte_offset > len) byte_offset = len;
    return utf8_count(s, byte_offset);
}

/* ══════════════════════════════════════════════════════════════════════
 *  UTF-16 Surrogate Pairs
 * ══════════════════════════════════════════════════════════════════════ */

uint32_t utf16_decode_pair(uint16_t high, uint16_t low) {
    if (high < 0xD800 || high > 0xDBFF) return 0;
    if (low  < 0xDC00 || low  > 0xDFFF) return 0;
    return 0x10000 + ((uint32_t)(high - 0xD800) << 10) + (low - 0xDC00);
}

int utf16_encode(uint32_t codepoint, uint16_t utf16[2]) {
    if (!utf16) return 0;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0; /* surrogate */
    if (codepoint <= 0xFFFF) {
        utf16[0] = (uint16_t)codepoint;
        return 1;
    }
    if (codepoint <= 0x10FFFF) {
        uint32_t cp = codepoint - 0x10000;
        utf16[0] = (uint16_t)(0xD800 + (cp >> 10));
        utf16[1] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        return 2;
    }
    return 0; /* invalid codepoint */
}

/* ══════════════════════════════════════════════════════════════════════
 *  Codepoint Classification
 * ══════════════════════════════════════════════════════════════════════ */

bool utf_is_cjk(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unified Ideographs */
           (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Extension A */
           (cp >= 0x20000 && cp <= 0x2A6DF) || /* CJK Extension B */
           (cp >= 0x2A700 && cp <= 0x2B73F) || /* CJK Extension C */
           (cp >= 0x2B740 && cp <= 0x2B81F) || /* CJK Extension D */
           (cp >= 0x2B820 && cp <= 0x2CEAF) || /* CJK Extension E */
           (cp >= 0x3040 && cp <= 0x309F) ||   /* Hiragana */
           (cp >= 0x30A0 && cp <= 0x30FF) ||   /* Katakana */
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   /* Hangul Syllables */
           (cp >= 0xFF65 && cp <= 0xFF9F);     /* Halfwidth Katakana */
}

bool utf_is_hangul(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) ||   /* Hangul Jamo */
           (cp >= 0x3130 && cp <= 0x318F) ||   /* Hangul Compatibility Jamo */
           (cp >= 0xA960 && cp <= 0xA97F) ||   /* Hangul Jamo Extended-A */
           (cp >= 0xAC00 && cp <= 0xD7AF) ||   /* Hangul Syllables */
           (cp >= 0xD7B0 && cp <= 0xD7FF);     /* Hangul Jamo Extended-B */
}

bool utf_is_emoji_for_zwj(uint32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FFFF) || /* SMP emoji blocks */
           (cp >= 0x2600 && cp <= 0x27BF) ||   /* Misc Symbols and Dingbats */
           (cp >= 0x2300 && cp <= 0x23FF) ||   /* Misc Technical */
           (cp >= 0x2B00 && cp <= 0x2BFF) ||   /* Misc Symbols and Arrows */
           cp == 0x200D ||                      /* ZWJ itself */
           cp == 0x2764;                        /* Heavy Heart */
}

bool utf_is_zwj_composition_base(uint32_t cp) {
    return (cp >= 0x1F466 && cp <= 0x1F469) || /* Boy, Girl, Man, Woman */
           cp == 0x1F9D1 ||                     /* Person (gender-neutral) */
           cp == 0x1F441 ||                     /* Eye */
           (cp >= 0x1F3F3 && cp <= 0x1F3F4) || /* Flags */
           cp == 0x1F408 || cp == 0x1F415 ||   /* Cat, Dog */
           cp == 0x1F43B || cp == 0x1F426 ||   /* Bear, Bird */
           cp == 0x1F48B || cp == 0x2764;       /* Kiss Mark, Heart */
}
