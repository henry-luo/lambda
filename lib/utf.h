/**
 * @file utf.h
 * @brief Core Unicode codec and classification utilities.
 *
 * Centralises UTF-8 encode/decode, UTF-16 surrogate pair handling, and
 * common codepoint classification that was previously duplicated across
 * 20+ files in lambda/ and radiant/.
 *
 * All functions are pure C, no external dependencies.
 */

#ifndef LIB_UTF_H
#define LIB_UTF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── UTF-8 Codec ──────────────────────────────────────────────────── */

/**
 * Encode a Unicode codepoint as UTF-8.
 * @param codepoint  Unicode codepoint (0 – 0x10FFFF, not surrogate)
 * @param buf        Output buffer, must hold at least 4 bytes.
 *                   NOT null-terminated.
 * @return Bytes written (1–4), or 0 on invalid codepoint
 *         (surrogates 0xD800–0xDFFF, > 0x10FFFF, or NULL buf).
 */
size_t utf8_encode(uint32_t codepoint, char buf[4]);

/**
 * Encode a Unicode codepoint as null-terminated UTF-8.
 * Same as utf8_encode() but appends '\0' after the encoded bytes.
 * @param buf  Output buffer, must hold at least 5 bytes.
 * @return Bytes written excluding NUL (1–4), or 0 on invalid codepoint.
 */
size_t utf8_encode_z(uint32_t codepoint, char buf[5]);

/**
 * Decode one UTF-8 codepoint from s[0..len-1].
 * @param s    Input bytes
 * @param len  Available bytes
 * @param out  Receives decoded codepoint
 * @return Bytes consumed (1–4), or -1 on invalid/truncated/overlong/surrogate.
 *         NULL-safe: returns -1 for NULL s or len==0.
 */
int utf8_decode(const char* s, size_t len, uint32_t* out);

/** Byte length of a UTF-8 sequence given its lead byte (1–4), or 0 if invalid. */
size_t utf8_char_len(unsigned char lead);

/** Count Unicode codepoints in s[0..len). SWAR-accelerated. */
size_t utf8_count(const char* s, size_t len);

/** Validate that s[0..len) is well-formed UTF-8. */
bool utf8_valid(const char* s, size_t len);

/** Convert char index → byte offset.  Returns (size_t)-1 if out of range. */
size_t utf8_char_to_byte(const char* s, size_t len, size_t char_index);

/** Convert byte offset → char index. */
size_t utf8_byte_to_char(const char* s, size_t len, size_t byte_offset);

/* ── UTF-16 Surrogate Pairs ───────────────────────────────────────── */

/**
 * Decode a UTF-16 surrogate pair to a Unicode codepoint.
 * @return Codepoint >= 0x10000, or 0 if the pair is invalid.
 */
uint32_t utf16_decode_pair(uint16_t high, uint16_t low);

/**
 * Encode a codepoint as UTF-16 into utf16[2].
 * @return Number of uint16_t units written: 1 (BMP) or 2 (supplementary).
 *         Returns 0 on invalid codepoint (surrogate, > 0x10FFFF).
 */
int utf16_encode(uint32_t codepoint, uint16_t utf16[2]);

/* ── Codepoint Classification ─────────────────────────────────────── */

/** True if cp is a UTF-16 surrogate (0xD800–0xDFFF). */
static inline bool utf_is_surrogate(uint32_t cp) {
    return cp >= 0xD800 && cp <= 0xDFFF;
}

/** True if cp is a valid Unicode scalar value (0–0x10FFFF, not surrogate). */
static inline bool utf_is_valid_codepoint(uint32_t cp) {
    return cp <= 0x10FFFF && !utf_is_surrogate(cp);
}

/**
 * CJK character detection (Han, Kana, Hangul).
 * Covers CJK Unified Ideographs (main + Extensions A–E),
 * Hiragana, Katakana, Hangul Syllables, Halfwidth Katakana.
 */
bool utf_is_cjk(uint32_t cp);

/**
 * Hangul detection: Jamo, Compatibility Jamo, Syllables, Extended A/B.
 */
bool utf_is_hangul(uint32_t cp);

/**
 * Emoji that participates in ZWJ composition sequences.
 * SMP emoji blocks, Misc Symbols, Dingbats, Misc Technical, ZWJ, Heavy Heart.
 */
bool utf_is_emoji_for_zwj(uint32_t cp);

/**
 * Codepoints that can serve as the base (left side) of a ZWJ emoji
 * composition sequence. (Unicode UTS #51, emoji-zwj-sequences.txt)
 */
bool utf_is_zwj_composition_base(uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif /* LIB_UTF_H */
