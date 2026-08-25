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

int utf8_wtf8_encoded_len(const char* chars, int byte_len) {
    if (!chars || byte_len <= 0) return 0;
    int out_len = 0;
    for (int i = 0; i < byte_len; ) {
        unsigned char lead = (unsigned char)chars[i];
        int cp_len = 1;
        if (lead >= 0xF0 && i + 4 <= byte_len) cp_len = 4;
        else if (lead >= 0xE0 && i + 3 <= byte_len) cp_len = 3;
        else if (lead >= 0xC0 && i + 2 <= byte_len) cp_len = 2;

        if (cp_len == 3 && lead == 0xED && i + 2 < byte_len) {
            unsigned char second = (unsigned char)chars[i + 1];
            bool high = second >= 0xA0 && second <= 0xAF;
            bool low = second >= 0xB0 && second <= 0xBF;
            if (high) {
                int next = i + 3;
                if (next + 2 < byte_len && (unsigned char)chars[next] == 0xED) {
                    unsigned char next_second = (unsigned char)chars[next + 1];
                    if (next_second >= 0xB0 && next_second <= 0xBF) {
                        out_len += 4;
                        i += 6;
                        continue;
                    }
                }
                out_len += 3;
                i += 3;
                continue;
            }
            if (low) {
                out_len += 3;
                i += 3;
                continue;
            }
        }

        out_len += cp_len;
        i += cp_len;
    }
    return out_len;
}

void utf8_wtf8_encode(const char* chars, int byte_len, uint8_t* out) {
    if (!chars || byte_len <= 0 || !out) return;
    int out_pos = 0;
    for (int i = 0; i < byte_len; ) {
        unsigned char lead = (unsigned char)chars[i];
        int cp_len = 1;
        if (lead >= 0xF0 && i + 4 <= byte_len) cp_len = 4;
        else if (lead >= 0xE0 && i + 3 <= byte_len) cp_len = 3;
        else if (lead >= 0xC0 && i + 2 <= byte_len) cp_len = 2;

        if (cp_len == 3 && lead == 0xED && i + 2 < byte_len) {
            unsigned char second = (unsigned char)chars[i + 1];
            bool high = second >= 0xA0 && second <= 0xAF;
            bool low = second >= 0xB0 && second <= 0xBF;
            if (high) {
                int next = i + 3;
                if (next + 2 < byte_len && (unsigned char)chars[next] == 0xED) {
                    unsigned char next_second = (unsigned char)chars[next + 1];
                    if (next_second >= 0xB0 && next_second <= 0xBF) {
                        uint16_t hi = (uint16_t)(((uint16_t)((unsigned char)chars[i] & 0x0F) << 12) |
                            ((uint16_t)((unsigned char)chars[i + 1] & 0x3F) << 6) |
                            (uint16_t)((unsigned char)chars[i + 2] & 0x3F));
                        uint16_t lo = (uint16_t)(((uint16_t)((unsigned char)chars[next] & 0x0F) << 12) |
                            ((uint16_t)((unsigned char)chars[next + 1] & 0x3F) << 6) |
                            (uint16_t)((unsigned char)chars[next + 2] & 0x3F));
                        uint32_t cp = utf16_decode_pair(hi, lo);
                        char encoded[4];
                        size_t n = utf8_encode(cp, encoded);
                        for (size_t j = 0; j < n; j++) out[out_pos++] = (uint8_t)encoded[j];
                        i += 6;
                        continue;
                    }
                }
                out[out_pos++] = 0xEF;
                out[out_pos++] = 0xBF;
                out[out_pos++] = 0xBD;
                i += 3;
                continue;
            }
            if (low) {
                out[out_pos++] = 0xEF;
                out[out_pos++] = 0xBF;
                out[out_pos++] = 0xBD;
                i += 3;
                continue;
            }
        }

        for (int j = 0; j < cp_len; j++) out[out_pos++] = (uint8_t)chars[i + j];
        i += cp_len;
    }
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

typedef struct UtfCodepointRange {
    uint32_t first;
    uint32_t last;
} UtfCodepointRange;

/* Unicode Script values listed by CSS Text 4 §8.2.1. */
static const UtfCodepointRange utf_cursive_script_ranges[] = {
    {0x0600, 0x0604}, {0x0606, 0x060B}, {0x060D, 0x061A}, {0x061C, 0x061E},
    {0x0620, 0x063F}, {0x0641, 0x064A}, {0x0656, 0x066F}, {0x0671, 0x06DC},
    {0x06DE, 0x06FF}, {0x0750, 0x077F}, {0x0870, 0x088E}, {0x0890, 0x0891},
    {0x0897, 0x08E1}, {0x08E3, 0x08FF}, {0xFB50, 0xFBC2}, {0xFBD3, 0xFD3D},
    {0xFD40, 0xFD8F}, {0xFD92, 0xFDC7}, {0xFDCF, 0xFDCF}, {0xFDF0, 0xFDFF},
    {0xFE70, 0xFE74}, {0xFE76, 0xFEFC}, {0x10E60, 0x10E7E}, {0x10EC2, 0x10EC4},
    {0x10EFC, 0x10EFF}, {0x1EE00, 0x1EE03}, {0x1EE05, 0x1EE1F}, {0x1EE21, 0x1EE22},
    {0x1EE24, 0x1EE24}, {0x1EE27, 0x1EE27}, {0x1EE29, 0x1EE32}, {0x1EE34, 0x1EE37},
    {0x1EE39, 0x1EE39}, {0x1EE3B, 0x1EE3B}, {0x1EE42, 0x1EE42}, {0x1EE47, 0x1EE47},
    {0x1EE49, 0x1EE49}, {0x1EE4B, 0x1EE4B}, {0x1EE4D, 0x1EE4F}, {0x1EE51, 0x1EE52},
    {0x1EE54, 0x1EE54}, {0x1EE57, 0x1EE57}, {0x1EE59, 0x1EE59}, {0x1EE5B, 0x1EE5B},
    {0x1EE5D, 0x1EE5F}, {0x1EE61, 0x1EE62}, {0x1EE64, 0x1EE64}, {0x1EE67, 0x1EE6A},
    {0x1EE6C, 0x1EE72}, {0x1EE74, 0x1EE77}, {0x1EE79, 0x1EE7C}, {0x1EE7E, 0x1EE7E},
    {0x1EE80, 0x1EE89}, {0x1EE8B, 0x1EE9B}, {0x1EEA1, 0x1EEA3}, {0x1EEA5, 0x1EEA9},
    {0x1EEAB, 0x1EEBB}, {0x1EEF0, 0x1EEF1},
    {0x07C0, 0x07FA}, {0x07FD, 0x07FF},
    {0x0840, 0x085B}, {0x085E, 0x085E},
    {0x10D00, 0x10D27}, {0x10D30, 0x10D39},
    {0x11660, 0x1166C}, {0x1800, 0x1801}, {0x1804, 0x1804}, {0x1806, 0x1819},
    {0x1820, 0x1878}, {0x1880, 0x18AA},
    {0xA840, 0xA877},
    {0x0700, 0x070D}, {0x070F, 0x074A}, {0x074D, 0x074F}, {0x0860, 0x086A}
};

bool utf_is_cursive_script(uint32_t cp) {
    size_t count = sizeof(utf_cursive_script_ranges) /
                   sizeof(utf_cursive_script_ranges[0]);
    for (size_t i = 0; i < count; i++) {
        if (cp >= utf_cursive_script_ranges[i].first &&
            cp <= utf_cursive_script_ranges[i].last) {
            return true;
        }
    }
    return false;
}

int utf_bidi_strong_class(uint32_t cp) {
    /* Keep bidi classification in the Unicode module so DOM-only users do
     * not acquire a link dependency on the HTML style resolver. */
    if (cp == 0x200E) return -1; /* LRM */
    if (cp == 0x200F) return 1;  /* RLM */
    if (cp == 0x061C) return 1;  /* ALM */
    if (cp >= 0x0590 && cp <= 0x05FF) return 1;
    if (cp >= 0x0600 && cp <= 0x07BF) return 1;
    if (cp >= 0x0860 && cp <= 0x089F) return 1;
    if (cp >= 0xFB50 && cp <= 0xFDFF) return 1;
    if (cp >= 0xFE70 && cp <= 0xFEFF) return 1;
    if (cp >= 0x07C0 && cp <= 0x07FF) return 1;
    if (cp >= 0x0700 && cp <= 0x074F) return 1;
    if (cp >= 0x0800 && cp <= 0x085F) return 1;

    if ((cp >= 0x0041 && cp <= 0x005A) ||
        (cp >= 0x0061 && cp <= 0x007A)) return -1;
    if (cp >= 0x00C0 && cp <= 0x02AF) return -1;
    if (cp >= 0x0370 && cp <= 0x03FF) return -1;
    if (cp >= 0x0400 && cp <= 0x052F) return -1;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return -1;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return -1;
    if (cp >= 0x3040 && cp <= 0x30FF) return -1;
    if (cp >= 0x0E01 && cp <= 0x0E5B) return -1;
    if (cp >= 0x0E81 && cp <= 0x0EDF) return -1;
    if (cp >= 0x10A0 && cp <= 0x10FF) return -1;
    if (cp >= 0x1100 && cp <= 0x11FF) return -1;
    if (cp >= 0x0900 && cp <= 0x0DFF) return -1;
    return 0;
}

bool utf_is_emoji_for_zwj(uint32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FFFF) || /* SMP emoji blocks */
           (cp >= 0x2600 && cp <= 0x27BF) ||   /* Misc Symbols and Dingbats */
           (cp >= 0x2300 && cp <= 0x23FF) ||   /* Misc Technical */
           (cp >= 0x2B00 && cp <= 0x2BFF) ||   /* Misc Symbols and Arrows */
           cp == 0x200D ||                      /* ZWJ itself */
           cp == 0x2764;                        /* Heavy Heart */
}

bool utf_is_emoji_presentation_default(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
    if (cp >= 0xE0020 && cp <= 0xE007F) return true;
    if (cp < 0x231A || cp > 0x3299) return false;
    if (cp <= 0x231B) return true;
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;
    if (cp == 0x25AA || cp == 0x25AB) return true;
    if (cp == 0x25B6 || cp == 0x25C0) return true;
    if (cp >= 0x25FB && cp <= 0x25FE) return true;
    if (cp == 0x2614 || cp == 0x2615) return true;
    if (cp >= 0x2648 && cp <= 0x2653) return true;
    if (cp == 0x267F || cp == 0x2693 || cp == 0x26A1) return true;
    if (cp == 0x26AA || cp == 0x26AB) return true;
    if (cp == 0x26BD || cp == 0x26BE) return true;
    if (cp == 0x26C4 || cp == 0x26C5) return true;
    if (cp == 0x26CE || cp == 0x26D4 || cp == 0x26EA) return true;
    if (cp == 0x26F2 || cp == 0x26F3 || cp == 0x26F5) return true;
    if (cp == 0x26FA || cp == 0x26FD) return true;
    if (cp == 0x2702 || cp == 0x2705) return true;
    if (cp >= 0x2708 && cp <= 0x270D) return true;
    if (cp == 0x270F || cp == 0x2712) return true;
    if (cp == 0x2714 || cp == 0x2716) return true;
    if (cp == 0x271D || cp == 0x2721 || cp == 0x2728) return true;
    if (cp == 0x2733 || cp == 0x2734) return true;
    if (cp == 0x2744 || cp == 0x2747) return true;
    if (cp == 0x274C || cp == 0x274E) return true;
    if (cp >= 0x2753 && cp <= 0x2755) return true;
    if (cp == 0x2757) return true;
    if (cp == 0x2763 || cp == 0x2764) return true;
    if (cp >= 0x2795 && cp <= 0x2797) return true;
    if (cp == 0x27A1 || cp == 0x27B0 || cp == 0x27BF) return true;
    if (cp == 0x2934 || cp == 0x2935) return true;
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;
    if (cp == 0x2B1B || cp == 0x2B1C) return true;
    if (cp == 0x2B50 || cp == 0x2B55) return true;
    if (cp == 0x3030 || cp == 0x303D) return true;
    if (cp == 0x3297 || cp == 0x3299) return true;
    return false;
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
