/*
 * Comprehensive str.h / str.c Test Suite (GTest)
 * ================================================
 *
 * Covers all 16 API categories with 80+ test cases:
 *
 *  §1  Comparison      — cmp, icmp, eq, ieq, eq_lit, ieq_lit, NULL safety
 *  §2  Prefix/Suffix   — starts_with, ends_with, _lit, case-insensitive
 *  §3  Search          — find_byte, rfind_byte, find, rfind, ifind,
 *                         contains, find_any, find_not_any, count, count_byte
 *  §4  Byte-set        — clear, add, range, many, invert, test, whitespace,
 *                         digits, alpha, alnum, find_byteset, rfind_byteset
 *  §5  Trim            — trim, ltrim, rtrim, trim_chars, all-whitespace, empty
 *  §6  Case conversion — to_lower, to_upper, inplace, is_ascii, LUT, transform
 *  §7  Copy / Fill     — copy, cat, fill, dup, dup_lower, dup_upper
 *  §8  Numeric parsing — int64, uint64, double, overflow, default, end pointer
 *  §9  Split/Tokenize  — byte split, multi-byte delim, empty tokens, count
 * §10  Replace         — replace_all, replace_first, no match, grow/shrink
 * §11  File path       — file_ext, file_basename, no ext, trailing slash
 * §12  Hashing         — hash deterministic, ihash case-insensitive equality
 * §13  UTF-8           — count, char_len, valid, decode, encode,
 *                         char_to_byte, byte_to_char, SWAR correctness
 * §14  Escape          — JSON, XML, URL modes, control chars, sizing
 * §15  Span/Predicate  — span_whitespace, span_digits, span, all, is_* preds
 * §16  Formatting      — str_fmt, hex_encode, hex_decode
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <climits>

extern "C" {
#include "../lib/str.h"
#include "../lib/utf.h"
}

/* ================================================================== *
 *  §1  Comparison                                                    *
 * ================================================================== */

class StrCmpTest : public ::testing::Test {};

TEST_F(StrCmpTest, CmpBasic) {
    EXPECT_EQ(str_cmp("abc", 3, "abc", 3), 0);
    EXPECT_LT(str_cmp("abc", 3, "abd", 3), 0);
    EXPECT_GT(str_cmp("abd", 3, "abc", 3), 0);
}

TEST_F(StrCmpTest, CmpDifferentLengths) {
    EXPECT_LT(str_cmp("abc", 3, "abcd", 4), 0);
    EXPECT_GT(str_cmp("abcd", 4, "abc", 3), 0);
}

TEST_F(StrCmpTest, CmpEmpty) {
    EXPECT_EQ(str_cmp("", 0, "", 0), 0);
    EXPECT_LT(str_cmp("", 0, "a", 1), 0);
    EXPECT_GT(str_cmp("a", 1, "", 0), 0);
}

TEST_F(StrCmpTest, CmpNull) {
    EXPECT_EQ(str_cmp(NULL, 0, NULL, 0), 0);
    EXPECT_EQ(str_cmp(NULL, 0, "", 0), 0);
    EXPECT_LT(str_cmp(NULL, 0, "a", 1), 0);
}

TEST_F(StrCmpTest, ICmpBasic) {
    EXPECT_EQ(str_icmp("Hello", 5, "hello", 5), 0);
    EXPECT_EQ(str_icmp("ABC", 3, "abc", 3), 0);
    EXPECT_LT(str_icmp("abc", 3, "ABD", 3), 0);
}

TEST_F(StrCmpTest, EqBasic) {
    EXPECT_TRUE(str_eq("hello", 5, "hello", 5));
    EXPECT_FALSE(str_eq("hello", 5, "world", 5));
    EXPECT_FALSE(str_eq("hello", 5, "hell", 4));
    EXPECT_TRUE(str_eq("", 0, "", 0));
}

TEST_F(StrCmpTest, EqNull) {
    EXPECT_TRUE(str_eq(NULL, 0, NULL, 0));
    EXPECT_TRUE(str_eq(NULL, 0, "", 0));
    EXPECT_FALSE(str_eq(NULL, 0, "a", 1));
}

TEST_F(StrCmpTest, EqLong) {
    // test SWAR path (>8 bytes)
    const char* a = "abcdefghijklmnop";
    const char* b = "abcdefghijklmnop";
    const char* c = "abcdefghijklmnoq";
    EXPECT_TRUE(str_eq(a, 16, b, 16));
    EXPECT_FALSE(str_eq(a, 16, c, 16));
}

TEST_F(StrCmpTest, IEqBasic) {
    EXPECT_TRUE(str_ieq("Hello", 5, "hello", 5));
    EXPECT_TRUE(str_ieq("ABC", 3, "abc", 3));
    EXPECT_FALSE(str_ieq("abc", 3, "abd", 3));
    EXPECT_FALSE(str_ieq("abc", 3, "ab", 2));
}

TEST_F(StrCmpTest, EqLit) {
    EXPECT_TRUE(str_eq_const("div", 3, "div"));
    EXPECT_FALSE(str_eq_const("div", 3, "span"));
    EXPECT_FALSE(str_eq_const("div", 3, "di"));
    EXPECT_TRUE(str_eq_const("", 0, ""));
    EXPECT_TRUE(str_eq_const(NULL, 0, ""));
}

TEST_F(StrCmpTest, IEqLit) {
    EXPECT_TRUE(str_ieq_const("DIV", 3, "div"));
    EXPECT_TRUE(str_ieq_const("Content-Type", 12, "content-type"));
    EXPECT_FALSE(str_ieq_const("abc", 3, "abd"));
}

/* ================================================================== *
 *  §2  Prefix / Suffix                                               *
 * ================================================================== */

class StrPrefixTest : public ::testing::Test {};

TEST_F(StrPrefixTest, StartsWith) {
    EXPECT_TRUE(str_starts_with("http://example.com", 18, "http://", 7));
    EXPECT_FALSE(str_starts_with("https://example.com", 19, "http://", 7));
    EXPECT_TRUE(str_starts_with("abc", 3, "", 0));
    EXPECT_FALSE(str_starts_with("ab", 2, "abc", 3));
}

TEST_F(StrPrefixTest, EndsWith) {
    EXPECT_TRUE(str_ends_with("file.json", 9, ".json", 5));
    EXPECT_FALSE(str_ends_with("file.xml", 8, ".json", 5));
    EXPECT_TRUE(str_ends_with("abc", 3, "", 0));
    EXPECT_FALSE(str_ends_with("ab", 2, "abc", 3));
}

TEST_F(StrPrefixTest, StartsWithLit) {
    EXPECT_TRUE(str_starts_with_const("http://x", 8, "http://"));
    EXPECT_FALSE(str_starts_with_const("ftp://x", 7, "http://"));
}

TEST_F(StrPrefixTest, EndsWithLit) {
    EXPECT_TRUE(str_ends_with_const("style.css", 9, ".css"));
    EXPECT_FALSE(str_ends_with_const("style.js", 8, ".css"));
}

TEST_F(StrPrefixTest, IStartsWith) {
    EXPECT_TRUE(str_istarts_with("HTTP://x", 8, "http://", 7));
    EXPECT_TRUE(str_istarts_with("Content-Type", 12, "content-", 8));
    EXPECT_FALSE(str_istarts_with("ftp://x", 7, "http://", 7));
}

TEST_F(StrPrefixTest, IEndsWith) {
    EXPECT_TRUE(str_iends_with("FILE.JSON", 9, ".json", 5));
    EXPECT_FALSE(str_iends_with("FILE.XML", 8, ".json", 5));
}

TEST_F(StrPrefixTest, IStartsWithConst) {
    EXPECT_TRUE(str_istarts_with_const("HTTP://example.com", 18, "http://"));
    EXPECT_TRUE(str_istarts_with_const("Content-Type", 12, "content-"));
    EXPECT_FALSE(str_istarts_with_const("ftp://x", 7, "http://"));
    EXPECT_TRUE(str_istarts_with_const("hello", 5, ""));
    EXPECT_FALSE(str_istarts_with_const("", 0, "a"));
}

TEST_F(StrPrefixTest, IEndsWithConst) {
    EXPECT_TRUE(str_iends_with_const("FILE.JSON", 9, ".json"));
    EXPECT_TRUE(str_iends_with_const("image.PNG", 9, ".png"));
    EXPECT_FALSE(str_iends_with_const("FILE.XML", 8, ".json"));
    EXPECT_TRUE(str_iends_with_const("hello", 5, ""));
    EXPECT_FALSE(str_iends_with_const("", 0, ".txt"));
}

TEST_F(StrPrefixTest, NullSafety) {
    EXPECT_TRUE(str_starts_with(NULL, 0, NULL, 0));
    EXPECT_TRUE(str_starts_with(NULL, 0, "", 0));
    EXPECT_FALSE(str_starts_with(NULL, 0, "a", 1));
    EXPECT_TRUE(str_ends_with(NULL, 0, NULL, 0));
}

/* ================================================================== *
 *  §3  Search                                                        *
 * ================================================================== */

class StrSearchTest : public ::testing::Test {};

TEST_F(StrSearchTest, FindByte) {
    EXPECT_EQ(str_find_byte("hello world", 11, 'w'), 6u);
    EXPECT_EQ(str_find_byte("hello world", 11, 'h'), 0u);
    EXPECT_EQ(str_find_byte("hello world", 11, 'd'), 10u);
    EXPECT_EQ(str_find_byte("hello world", 11, 'z'), STR_NPOS);
    EXPECT_EQ(str_find_byte(NULL, 0, 'a'), STR_NPOS);
    EXPECT_EQ(str_find_byte("", 0, 'a'), STR_NPOS);
}

TEST_F(StrSearchTest, FindByteLong) {
    // trigger SWAR path
    const char* s = "0123456789abcdef0123456789ABCDEF";
    EXPECT_EQ(str_find_byte(s, 32, 'A'), 26u);
    EXPECT_EQ(str_find_byte(s, 32, '0'), 0u);
    EXPECT_EQ(str_find_byte(s, 32, 'F'), 31u);
}

TEST_F(StrSearchTest, RFindByte) {
    EXPECT_EQ(str_rfind_byte("hello world", 11, 'l'), 9u);
    EXPECT_EQ(str_rfind_byte("hello world", 11, 'h'), 0u);
    EXPECT_EQ(str_rfind_byte("hello world", 11, 'z'), STR_NPOS);
    EXPECT_EQ(str_rfind_byte(NULL, 0, 'a'), STR_NPOS);
}

TEST_F(StrSearchTest, Find) {
    EXPECT_EQ(str_find("hello world", 11, "world", 5), 6u);
    EXPECT_EQ(str_find("hello world", 11, "hello", 5), 0u);
    EXPECT_EQ(str_find("hello world", 11, "xyz", 3), STR_NPOS);
    EXPECT_EQ(str_find("hello world", 11, "", 0), 0u);
    EXPECT_EQ(str_find("aaa", 3, "aaaa", 4), STR_NPOS);
}

TEST_F(StrSearchTest, RFind) {
    EXPECT_EQ(str_rfind("abcabc", 6, "abc", 3), 3u);
    EXPECT_EQ(str_rfind("abcabc", 6, "xyz", 3), STR_NPOS);
    EXPECT_EQ(str_rfind("abcabc", 6, "", 0), 6u);
}

TEST_F(StrSearchTest, IFind) {
    EXPECT_EQ(str_ifind("Hello World", 11, "world", 5), 6u);
    EXPECT_EQ(str_ifind("Hello World", 11, "HELLO", 5), 0u);
    EXPECT_EQ(str_ifind("Hello World", 11, "xyz", 3), STR_NPOS);
}

TEST_F(StrSearchTest, Contains) {
    EXPECT_TRUE(str_contains("hello world", 11, "world", 5));
    EXPECT_FALSE(str_contains("hello world", 11, "xyz", 3));
    EXPECT_TRUE(str_contains_byte("hello", 5, 'e'));
    EXPECT_FALSE(str_contains_byte("hello", 5, 'z'));
}

TEST_F(StrSearchTest, FindAny) {
    EXPECT_EQ(str_find_any("hello world", 11, "wz", 2), 6u);
    EXPECT_EQ(str_find_any("hello", 5, "xyz", 3), STR_NPOS);
    EXPECT_EQ(str_find_any("abc", 3, "a", 1), 0u);
}

TEST_F(StrSearchTest, FindNotAny) {
    EXPECT_EQ(str_find_not_any("   hello", 8, " ", 1), 3u);
    EXPECT_EQ(str_find_not_any("aaa", 3, "a", 1), STR_NPOS);
    EXPECT_EQ(str_find_not_any("abc", 3, "xyz", 3), 0u);
}

TEST_F(StrSearchTest, Count) {
    EXPECT_EQ(str_count("abcabcabc", 9, "abc", 3), 3u);
    EXPECT_EQ(str_count("aaaa", 4, "aa", 2), 2u); // non-overlapping
    EXPECT_EQ(str_count("hello", 5, "xyz", 3), 0u);
    EXPECT_EQ(str_count("a", 1, "abc", 3), 0u);
}

TEST_F(StrSearchTest, CountByte) {
    EXPECT_EQ(str_count_byte("hello world", 11, 'l'), 3u);
    EXPECT_EQ(str_count_byte("hello world", 11, 'z'), 0u);
    EXPECT_EQ(str_count_byte(NULL, 0, 'a'), 0u);
}

TEST_F(StrSearchTest, CountByteLong) {
    // trigger SWAR path
    char buf[64];
    memset(buf, 'x', 64);
    buf[10] = 'y'; buf[20] = 'y'; buf[30] = 'y';
    EXPECT_EQ(str_count_byte(buf, 64, 'x'), 61u);
    EXPECT_EQ(str_count_byte(buf, 64, 'y'), 3u);
}

/* ================================================================== *
 *  §4  Byte-set                                                      *
 * ================================================================== */

class StrByteSetTest : public ::testing::Test {};

TEST_F(StrByteSetTest, ClearAndAdd) {
    StrByteSet set;
    str_byteset_clear(&set);
    EXPECT_FALSE(str_byteset_test(&set, 'a'));
    str_byteset_add(&set, 'a');
    EXPECT_TRUE(str_byteset_test(&set, 'a'));
    EXPECT_FALSE(str_byteset_test(&set, 'b'));
}

TEST_F(StrByteSetTest, AddRange) {
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_range(&set, '0', '9');
    EXPECT_TRUE(str_byteset_test(&set, '0'));
    EXPECT_TRUE(str_byteset_test(&set, '5'));
    EXPECT_TRUE(str_byteset_test(&set, '9'));
    EXPECT_FALSE(str_byteset_test(&set, 'a'));
}

TEST_F(StrByteSetTest, AddMany) {
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_many(&set, "aeiou", 5);
    EXPECT_TRUE(str_byteset_test(&set, 'a'));
    EXPECT_TRUE(str_byteset_test(&set, 'u'));
    EXPECT_FALSE(str_byteset_test(&set, 'b'));
}

TEST_F(StrByteSetTest, Invert) {
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add(&set, 'x');
    str_byteset_invert(&set);
    EXPECT_FALSE(str_byteset_test(&set, 'x'));
    EXPECT_TRUE(str_byteset_test(&set, 'a'));
    EXPECT_TRUE(str_byteset_test(&set, 0));
}

TEST_F(StrByteSetTest, Whitespace) {
    StrByteSet set;
    str_byteset_whitespace(&set);
    EXPECT_TRUE(str_byteset_test(&set, ' '));
    EXPECT_TRUE(str_byteset_test(&set, '\t'));
    EXPECT_TRUE(str_byteset_test(&set, '\n'));
    EXPECT_TRUE(str_byteset_test(&set, '\r'));
    EXPECT_FALSE(str_byteset_test(&set, 'a'));
}

TEST_F(StrByteSetTest, DigitsAlphaAlnum) {
    StrByteSet digits, alpha, alnum;
    str_byteset_digits(&digits);
    str_byteset_alpha(&alpha);
    str_byteset_alnum(&alnum);

    EXPECT_TRUE(str_byteset_test(&digits, '5'));
    EXPECT_FALSE(str_byteset_test(&digits, 'a'));

    EXPECT_TRUE(str_byteset_test(&alpha, 'z'));
    EXPECT_TRUE(str_byteset_test(&alpha, 'A'));
    EXPECT_FALSE(str_byteset_test(&alpha, '5'));

    EXPECT_TRUE(str_byteset_test(&alnum, 'a'));
    EXPECT_TRUE(str_byteset_test(&alnum, '9'));
    EXPECT_FALSE(str_byteset_test(&alnum, '!'));
}

TEST_F(StrByteSetTest, FindByteSet) {
    StrByteSet set;
    str_byteset_digits(&set);
    EXPECT_EQ(str_find_byteset("abc123", 6, &set), 3u);
    EXPECT_EQ(str_find_byteset("abcdef", 6, &set), STR_NPOS);
}

TEST_F(StrByteSetTest, RFindByteSet) {
    StrByteSet set;
    str_byteset_digits(&set);
    EXPECT_EQ(str_rfind_byteset("abc123xyz", 9, &set), 5u);
}

TEST_F(StrByteSetTest, FindNotByteSet) {
    StrByteSet set;
    str_byteset_whitespace(&set);
    EXPECT_EQ(str_find_not_byteset("  \thello", 8, &set), 3u);
    EXPECT_EQ(str_find_not_byteset("   ", 3, &set), STR_NPOS);
}

/* ================================================================== *
 *  §5  Trim                                                          *
 * ================================================================== */

class StrTrimTest : public ::testing::Test {};

TEST_F(StrTrimTest, Trim) {
    const char* s = "  hello  ";
    size_t len = 9;
    str_trim(&s, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "hello", 5), 0);
}

TEST_F(StrTrimTest, LTrim) {
    const char* s = "\t\n hello";
    size_t len = 8;
    str_ltrim(&s, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "hello", 5), 0);
}

TEST_F(StrTrimTest, RTrim) {
    const char* s = "hello   ";
    size_t len = 8;
    str_rtrim(&s, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "hello", 5), 0);
}

TEST_F(StrTrimTest, TrimAllWhitespace) {
    const char* s = "   \t\n ";
    size_t len = 6;
    str_trim(&s, &len);
    EXPECT_EQ(len, 0u);
}

TEST_F(StrTrimTest, TrimEmpty) {
    const char* s = "";
    size_t len = 0;
    str_trim(&s, &len);
    EXPECT_EQ(len, 0u);
}

TEST_F(StrTrimTest, TrimNoWhitespace) {
    const char* s = "hello";
    size_t len = 5;
    str_trim(&s, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "hello", 5), 0);
}

TEST_F(StrTrimTest, TrimChars) {
    const char* s = "---hello---";
    size_t len = 11;
    str_trim_chars(&s, &len, "-", 1);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "hello", 5), 0);
}

TEST_F(StrTrimTest, TrimCharsMultiple) {
    const char* s = "xyzHELLOzyx";
    size_t len = 11;
    str_trim_chars(&s, &len, "xyz", 3);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(s, "HELLO", 5), 0);
}

TEST_F(StrTrimTest, NullSafety) {
    const char* s = NULL;
    size_t len = 0;
    str_trim(&s, &len); // should not crash
    EXPECT_EQ(len, 0u);

    str_ltrim(NULL, NULL); // should not crash
    str_rtrim(NULL, NULL);
}

/* ================================================================== *
 *  §6  Case conversion                                               *
 * ================================================================== */

class StrCaseTest : public ::testing::Test {};

TEST_F(StrCaseTest, ToLower) {
    char buf[32];
    str_to_lower(buf, "HELLO WORLD", 11);
    EXPECT_EQ(memcmp(buf, "hello world", 11), 0);
}

TEST_F(StrCaseTest, ToUpper) {
    char buf[32];
    str_to_upper(buf, "hello world", 11);
    EXPECT_EQ(memcmp(buf, "HELLO WORLD", 11), 0);
}

TEST_F(StrCaseTest, ToLowerLong) {
    // trigger SWAR path (>8 bytes)
    char src[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char dst[32];
    str_to_lower(dst, src, 26);
    EXPECT_EQ(memcmp(dst, "abcdefghijklmnopqrstuvwxyz", 26), 0);
}

TEST_F(StrCaseTest, ToUpperLong) {
    char src[] = "abcdefghijklmnopqrstuvwxyz";
    char dst[32];
    str_to_upper(dst, src, 26);
    EXPECT_EQ(memcmp(dst, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26), 0);
}

TEST_F(StrCaseTest, MixedAndNonAscii) {
    // non-ASCII bytes should pass through unchanged
    char buf[32];
    const char src[] = "H\xc3\xa9llo";  // Héllo (UTF-8)
    str_to_lower(buf, src, 6);
    EXPECT_EQ(buf[0], 'h');
    EXPECT_EQ((unsigned char)buf[1], 0xc3);
    EXPECT_EQ((unsigned char)buf[2], 0xa9);
    EXPECT_EQ(buf[3], 'l');
    EXPECT_EQ(buf[4], 'l');
    EXPECT_EQ(buf[5], 'o');
}

TEST_F(StrCaseTest, Inplace) {
    char buf[] = "Hello World";
    str_lower_inplace(buf, 11);
    EXPECT_STREQ(buf, "hello world");

    str_upper_inplace(buf, 11);
    EXPECT_STREQ(buf, "HELLO WORLD");
}

TEST_F(StrCaseTest, IsAscii) {
    EXPECT_TRUE(str_is_ascii("hello world", 11));
    EXPECT_TRUE(str_is_ascii("", 0));
    EXPECT_TRUE(str_is_ascii(NULL, 0));
    EXPECT_FALSE(str_is_ascii("h\xc3\xa9llo", 6));
}

TEST_F(StrCaseTest, IsAsciiLong) {
    // trigger SWAR path
    char buf[64];
    memset(buf, 'A', 64);
    EXPECT_TRUE(str_is_ascii(buf, 64));
    buf[32] = (char)0x80;
    EXPECT_FALSE(str_is_ascii(buf, 64));
}

TEST_F(StrCaseTest, LutAndTransform) {
    uint8_t lut[256];
    str_lut_tolower(lut);
    char buf[8];
    str_transform(buf, "HELLO", 5, lut);
    EXPECT_EQ(memcmp(buf, "hello", 5), 0);

    str_lut_toupper(lut);
    str_transform(buf, "hello", 5, lut);
    EXPECT_EQ(memcmp(buf, "HELLO", 5), 0);
}

TEST_F(StrCaseTest, NullSafety) {
    str_to_lower(NULL, "abc", 3); // should not crash
    char buf[8];
    str_to_lower(buf, NULL, 3);  // should not crash
    str_to_lower(buf, "abc", 0); // should not crash
}

/* ================================================================== *
 *  §7  Copy / Fill                                                   *
 * ================================================================== */

class StrCopyTest : public ::testing::Test {};

TEST_F(StrCopyTest, CopyBasic) {
    char buf[16];
    size_t n = str_copy(buf, 16, "hello", 5);
    EXPECT_EQ(n, 5u);
    EXPECT_STREQ(buf, "hello");
}

TEST_F(StrCopyTest, CopyTruncation) {
    char buf[4];
    size_t n = str_copy(buf, 4, "hello", 5);
    EXPECT_EQ(n, 3u);
    EXPECT_STREQ(buf, "hel");
}

TEST_F(StrCopyTest, CopyZeroCap) {
    char buf[4] = "xxx";
    size_t n = str_copy(buf, 0, "hello", 5);
    EXPECT_EQ(n, 0u);
    EXPECT_STREQ(buf, "xxx"); // unchanged
}

TEST_F(StrCopyTest, CopyNull) {
    char buf[16];
    size_t n = str_copy(buf, 16, NULL, 0);
    EXPECT_EQ(n, 0u);
    EXPECT_STREQ(buf, "");
}

TEST_F(StrCopyTest, CatBasic) {
    char buf[32] = "hello";
    size_t n = str_cat(buf, 5, 32, " world", 6);
    EXPECT_EQ(n, 11u);
    EXPECT_STREQ(buf, "hello world");
}

TEST_F(StrCopyTest, CatTruncation) {
    char buf[8] = "hello";
    size_t n = str_cat(buf, 5, 8, " world", 6);
    EXPECT_EQ(n, 7u);
    EXPECT_STREQ(buf, "hello w");
}

TEST_F(StrCopyTest, Fill) {
    char buf[8];
    str_fill(buf, 5, 'x');
    EXPECT_EQ(memcmp(buf, "xxxxx", 5), 0);
    str_fill(NULL, 5, 'x'); // should not crash
}

TEST_F(StrCopyTest, Dup) {
    char* d = str_dup("hello", 5);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "hello");
    EXPECT_EQ(strlen(d), 5u);
    free(d);
}

TEST_F(StrCopyTest, DupNull) {
    char* d = str_dup(NULL, 0);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "");
    free(d);
}

TEST_F(StrCopyTest, DupLower) {
    char* d = str_dup_lower("HELLO", 5);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "hello");
    free(d);
}

TEST_F(StrCopyTest, DupUpper) {
    char* d = str_dup_upper("hello", 5);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "HELLO");
    free(d);
}

/* ================================================================== *
 *  §8  Numeric parsing                                               *
 * ================================================================== */

class StrNumericTest : public ::testing::Test {};

TEST_F(StrNumericTest, Int64Basic) {
    int64_t v;
    const char* end;
    EXPECT_TRUE(str_to_int64("12345", 5, &v, &end));
    EXPECT_EQ(v, 12345);
    EXPECT_EQ(end, (const char*)"12345" + 5);
}

TEST_F(StrNumericTest, Int64Negative) {
    int64_t v;
    EXPECT_TRUE(str_to_int64("-42", 3, &v, NULL));
    EXPECT_EQ(v, -42);
}

TEST_F(StrNumericTest, Int64LeadingWhitespace) {
    int64_t v;
    EXPECT_TRUE(str_to_int64("  99", 4, &v, NULL));
    EXPECT_EQ(v, 99);
}

TEST_F(StrNumericTest, Int64TrailingChars) {
    int64_t v;
    const char* s = "123abc";
    const char* end;
    EXPECT_TRUE(str_to_int64(s, 6, &v, &end));
    EXPECT_EQ(v, 123);
    EXPECT_EQ(end, s + 3);
}

TEST_F(StrNumericTest, Int64Overflow) {
    int64_t v;
    // 2^63 = 9223372036854775808 > INT64_MAX
    EXPECT_FALSE(str_to_int64("9999999999999999999", 19, &v, NULL));
}

TEST_F(StrNumericTest, Int64MinValue) {
    int64_t v;
    EXPECT_TRUE(str_to_int64("-9223372036854775808", 20, &v, NULL));
    EXPECT_EQ(v, INT64_MIN);
}

TEST_F(StrNumericTest, Int64Empty) {
    int64_t v;
    EXPECT_FALSE(str_to_int64("", 0, &v, NULL));
    EXPECT_FALSE(str_to_int64(NULL, 0, &v, NULL));
    EXPECT_FALSE(str_to_int64("abc", 3, &v, NULL));
}

TEST_F(StrNumericTest, UInt64Basic) {
    uint64_t v;
    EXPECT_TRUE(str_to_uint64("42", 2, &v, NULL));
    EXPECT_EQ(v, 42u);
}

TEST_F(StrNumericTest, UInt64RejectsSign) {
    uint64_t v;
    EXPECT_FALSE(str_to_uint64("-1", 2, &v, NULL));
}

TEST_F(StrNumericTest, DoubleBasic) {
    double v;
    EXPECT_TRUE(str_to_double("3.14", 4, &v, NULL));
    EXPECT_NEAR(v, 3.14, 1e-10);
}

TEST_F(StrNumericTest, DoubleScientific) {
    double v;
    EXPECT_TRUE(str_to_double("1.5e10", 6, &v, NULL));
    EXPECT_NEAR(v, 1.5e10, 1.0);
}

TEST_F(StrNumericTest, DoubleNegative) {
    double v;
    EXPECT_TRUE(str_to_double("-2.5", 4, &v, NULL));
    EXPECT_NEAR(v, -2.5, 1e-10);
}

TEST_F(StrNumericTest, DoubleEmpty) {
    double v;
    EXPECT_FALSE(str_to_double("", 0, &v, NULL));
    EXPECT_FALSE(str_to_double("abc", 3, &v, NULL));
}

TEST_F(StrNumericTest, Int64OrDefault) {
    EXPECT_EQ(str_to_int64_default("42", 2, -1), 42);
    EXPECT_EQ(str_to_int64_default("abc", 3, -1), -1);
    EXPECT_EQ(str_to_int64_default("", 0, 99), 99);
}

TEST_F(StrNumericTest, DoubleOrDefault) {
    EXPECT_NEAR(str_to_double_default("3.14", 4, 0.0), 3.14, 1e-10);
    EXPECT_NEAR(str_to_double_default("abc", 3, -1.0), -1.0, 1e-10);
}

/* ================================================================== *
 *  §9  Split / Tokenize                                              *
 * ================================================================== */

class StrSplitTest : public ::testing::Test {};

TEST_F(StrSplitTest, ByteSplit) {
    StrSplitIter it;
    str_split_byte_init(&it, "a,b,c", 5, ',');
    const char* tok;
    size_t tok_len;

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 1u);
    EXPECT_EQ(tok[0], 'a');

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 1u);
    EXPECT_EQ(tok[0], 'b');

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 1u);
    EXPECT_EQ(tok[0], 'c');

    EXPECT_FALSE(str_split_next(&it, &tok, &tok_len));
}

TEST_F(StrSplitTest, MultiByteDelim) {
    StrSplitIter it;
    str_split_init(&it, "one::two::three", 15, "::", 2);
    const char* tok;
    size_t tok_len;

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 3u);
    EXPECT_EQ(memcmp(tok, "one", 3), 0);

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 3u);
    EXPECT_EQ(memcmp(tok, "two", 3), 0);

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 5u);
    EXPECT_EQ(memcmp(tok, "three", 5), 0);

    EXPECT_FALSE(str_split_next(&it, &tok, &tok_len));
}

TEST_F(StrSplitTest, EmptyTokens) {
    StrSplitIter it;
    str_split_byte_init(&it, ",a,,b,", 6, ',');
    const char* tok;
    size_t tok_len;

    // first token is empty (before first comma)
    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 0u);

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 1u);
    EXPECT_EQ(tok[0], 'a');

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 0u); // between ,,

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 1u);
    EXPECT_EQ(tok[0], 'b');

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 0u); // after trailing comma

    EXPECT_FALSE(str_split_next(&it, &tok, &tok_len));
}

TEST_F(StrSplitTest, NoDelimiter) {
    StrSplitIter it;
    str_split_byte_init(&it, "hello", 5, ',');
    const char* tok;
    size_t tok_len;

    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 5u);
    EXPECT_EQ(memcmp(tok, "hello", 5), 0);

    EXPECT_FALSE(str_split_next(&it, &tok, &tok_len));
}

TEST_F(StrSplitTest, EmptyString) {
    StrSplitIter it;
    str_split_byte_init(&it, "", 0, ',');
    const char* tok;
    size_t tok_len;

    // empty string yields one empty token
    EXPECT_TRUE(str_split_next(&it, &tok, &tok_len));
    EXPECT_EQ(tok_len, 0u);

    EXPECT_FALSE(str_split_next(&it, &tok, &tok_len));
}

TEST_F(StrSplitTest, SplitCount) {
    EXPECT_EQ(str_split_count("a,b,c", 5, ",", 1), 3u);
    EXPECT_EQ(str_split_count("hello", 5, ",", 1), 1u);
    EXPECT_EQ(str_split_count(",", 1, ",", 1), 2u);
    EXPECT_EQ(str_split_count("", 0, ",", 1), 0u);
}

/* ================================================================== *
 * §10  Replace                                                       *
 * ================================================================== */

class StrReplaceTest : public ::testing::Test {};

TEST_F(StrReplaceTest, ReplaceAll) {
    size_t out_len;
    char* r = str_replace_all("aXbXc", 5, "X", 1, "YY", 2, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(out_len, 7u);
    EXPECT_STREQ(r, "aYYbYYc");
    free(r);
}

TEST_F(StrReplaceTest, ReplaceFirst) {
    size_t out_len;
    char* r = str_replace_first("aXbXc", 5, "X", 1, "YY", 2, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(out_len, 6u);
    EXPECT_STREQ(r, "aYYbXc");
    free(r);
}

TEST_F(StrReplaceTest, NoMatch) {
    size_t out_len;
    char* r = str_replace_all("hello", 5, "xyz", 3, "!", 1, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(out_len, 5u);
    EXPECT_STREQ(r, "hello");
    free(r);
}

TEST_F(StrReplaceTest, ShrinkReplacement) {
    size_t out_len;
    char* r = str_replace_all("aaa", 3, "a", 1, "", 0, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(out_len, 0u);
    EXPECT_STREQ(r, "");
    free(r);
}

TEST_F(StrReplaceTest, GrowReplacement) {
    size_t out_len;
    char* r = str_replace_all("abc", 3, "b", 1, "BBB", 3, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(out_len, 5u);
    EXPECT_STREQ(r, "aBBBc");
    free(r);
}

TEST_F(StrReplaceTest, MultiBytePattern) {
    size_t out_len;
    char* r = str_replace_all("foo bar foo baz foo", 19, "foo", 3, "qux", 3, &out_len);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(r, "qux bar qux baz qux");
    free(r);
}

/* ================================================================== *
 * §11  File path helpers                                             *
 * ================================================================== */

class StrPathTest : public ::testing::Test {};

TEST_F(StrPathTest, FileExtBasic) {
    size_t ext_len;
    const char* ext = str_file_ext("document.json", 13, &ext_len);
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext_len, 5u);
    EXPECT_EQ(memcmp(ext, ".json", 5), 0);
}

TEST_F(StrPathTest, FileExtNone) {
    size_t ext_len;
    const char* ext = str_file_ext("Makefile", 8, &ext_len);
    EXPECT_EQ(ext, nullptr);
    EXPECT_EQ(ext_len, 0u);
}

TEST_F(StrPathTest, FileExtAfterSlash) {
    size_t ext_len;
    const char* ext = str_file_ext("/path.d/noext", 13, &ext_len);
    EXPECT_EQ(ext, nullptr);
    EXPECT_EQ(ext_len, 0u);
}

TEST_F(StrPathTest, FileExtMultipleDots) {
    size_t ext_len;
    const char* ext = str_file_ext("archive.tar.gz", 14, &ext_len);
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext_len, 3u);
    EXPECT_EQ(memcmp(ext, ".gz", 3), 0);
}

TEST_F(StrPathTest, Basename) {
    size_t name_len;
    const char* name = str_file_basename("/usr/local/bin/app", 18, &name_len);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name_len, 3u);
    EXPECT_EQ(memcmp(name, "app", 3), 0);
}

TEST_F(StrPathTest, BasenameNoSep) {
    size_t name_len;
    const char* name = str_file_basename("file.txt", 8, &name_len);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name_len, 8u);
    EXPECT_EQ(memcmp(name, "file.txt", 8), 0);
}

TEST_F(StrPathTest, BasenameWindows) {
    size_t name_len;
    const char* name = str_file_basename("C:\\Users\\doc.txt", 16, &name_len);
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name_len, 7u);
    EXPECT_EQ(memcmp(name, "doc.txt", 7), 0);
}

TEST_F(StrPathTest, NullSafety) {
    size_t ext_len, name_len;
    EXPECT_EQ(str_file_ext(NULL, 0, &ext_len), nullptr);
    EXPECT_EQ(ext_len, 0u);
    EXPECT_EQ(str_file_basename(NULL, 0, &name_len), nullptr);
    EXPECT_EQ(name_len, 0u);
}

/* ================================================================== *
 * §12  Hashing                                                       *
 * ================================================================== */

class StrHashTest : public ::testing::Test {};

TEST_F(StrHashTest, Deterministic) {
    uint64_t h1 = str_hash("hello", 5);
    uint64_t h2 = str_hash("hello", 5);
    EXPECT_EQ(h1, h2);
}

TEST_F(StrHashTest, DifferentStrings) {
    uint64_t h1 = str_hash("hello", 5);
    uint64_t h2 = str_hash("world", 5);
    EXPECT_NE(h1, h2);
}

TEST_F(StrHashTest, IHashCaseInsensitive) {
    uint64_t h1 = str_ihash("Hello", 5);
    uint64_t h2 = str_ihash("hello", 5);
    uint64_t h3 = str_ihash("HELLO", 5);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h2, h3);
}

TEST_F(StrHashTest, IHashDifferentStrings) {
    uint64_t h1 = str_ihash("hello", 5);
    uint64_t h2 = str_ihash("world", 5);
    EXPECT_NE(h1, h2);
}

TEST_F(StrHashTest, NullHash) {
    EXPECT_EQ(str_hash(NULL, 0), 0u);
    EXPECT_EQ(str_ihash(NULL, 0), 0u);
}

TEST_F(StrHashTest, EmptyHash) {
    // empty string should produce the FNV offset basis
    uint64_t h = str_hash("", 0);
    EXPECT_EQ(h, 0xCBF29CE484222325ULL);
}

/* ================================================================== *
 * §13  UTF-8                                                         *
 * ================================================================== */

class StrUtf8Test : public ::testing::Test {};

TEST_F(StrUtf8Test, CharLen) {
    EXPECT_EQ(str_utf8_char_len('A'), 1u);
    EXPECT_EQ(str_utf8_char_len(0xC3), 2u);  // 2-byte lead
    EXPECT_EQ(str_utf8_char_len(0xE4), 3u);  // 3-byte lead
    EXPECT_EQ(str_utf8_char_len(0xF0), 4u);  // 4-byte lead
    EXPECT_EQ(str_utf8_char_len(0x80), 0u);  // continuation → invalid lead
    EXPECT_EQ(str_utf8_char_len(0xFF), 0u);  // invalid
}

TEST_F(StrUtf8Test, CountAscii) {
    EXPECT_EQ(str_utf8_count("hello", 5), 5u);
    EXPECT_EQ(str_utf8_count("", 0), 0u);
    EXPECT_EQ(str_utf8_count(NULL, 0), 0u);
}

TEST_F(StrUtf8Test, CountMultibyte) {
    // "café" = c(1) a(1) f(1) é(2) = 5 bytes, 4 chars
    const char* s = "caf\xc3\xa9";
    EXPECT_EQ(str_utf8_count(s, 5), 4u);
}

TEST_F(StrUtf8Test, CountCJK) {
    // 中文 = 0xE4B8AD 0xE69687 = 6 bytes, 2 chars
    const char* s = "\xe4\xb8\xad\xe6\x96\x87";
    EXPECT_EQ(str_utf8_count(s, 6), 2u);
}

TEST_F(StrUtf8Test, CountEmoji) {
    // 😀 = F0 9F 98 80 = 4 bytes, 1 char
    const char* s = "\xf0\x9f\x98\x80";
    EXPECT_EQ(str_utf8_count(s, 4), 1u);
}

TEST_F(StrUtf8Test, CountLong) {
    // trigger SWAR path: 32 ASCII chars
    const char* s = "abcdefghijklmnopqrstuvwxyz012345";
    EXPECT_EQ(str_utf8_count(s, 32), 32u);
}

TEST_F(StrUtf8Test, CountLongMultibyte) {
    // trigger SWAR path: 8 × 2-byte chars = 16 bytes, 8 chars
    // ü = C3 BC
    char buf[16];
    for (int i = 0; i < 8; i++) {
        buf[i*2]   = (char)0xC3;
        buf[i*2+1] = (char)0xBC;
    }
    EXPECT_EQ(str_utf8_count(buf, 16), 8u);
}

TEST_F(StrUtf8Test, ValidAscii) {
    EXPECT_TRUE(str_utf8_valid("hello", 5));
    EXPECT_TRUE(str_utf8_valid("", 0));
    EXPECT_TRUE(str_utf8_valid(NULL, 0));
}

TEST_F(StrUtf8Test, ValidMultibyte) {
    EXPECT_TRUE(str_utf8_valid("caf\xc3\xa9", 5));    // café
    EXPECT_TRUE(str_utf8_valid("\xe4\xb8\xad", 3));    // 中
    EXPECT_TRUE(str_utf8_valid("\xf0\x9f\x98\x80", 4)); // 😀
}

TEST_F(StrUtf8Test, InvalidOverlong) {
    // overlong 2-byte encoding of NUL: C0 80
    EXPECT_FALSE(str_utf8_valid("\xc0\x80", 2));
}

TEST_F(StrUtf8Test, InvalidSurrogate) {
    // U+D800 = ED A0 80
    EXPECT_FALSE(str_utf8_valid("\xed\xa0\x80", 3));
}

TEST_F(StrUtf8Test, InvalidTruncated) {
    // 3-byte lead but only 1 continuation
    EXPECT_FALSE(str_utf8_valid("\xe4\xb8", 2));
}

TEST_F(StrUtf8Test, InvalidLeadByte) {
    EXPECT_FALSE(str_utf8_valid("\xff", 1));
    EXPECT_FALSE(str_utf8_valid("\xfe", 1));
}

TEST_F(StrUtf8Test, DecodeAscii) {
    uint32_t cp;
    EXPECT_EQ(str_utf8_decode("A", 1, &cp), 1);
    EXPECT_EQ(cp, 0x41u);
}

TEST_F(StrUtf8Test, DecodeTwoByte) {
    uint32_t cp;
    EXPECT_EQ(str_utf8_decode("\xc3\xa9", 2, &cp), 2);  // é = U+00E9
    EXPECT_EQ(cp, 0x00E9u);
}

TEST_F(StrUtf8Test, DecodeThreeByte) {
    uint32_t cp;
    EXPECT_EQ(str_utf8_decode("\xe4\xb8\xad", 3, &cp), 3);  // 中 = U+4E2D
    EXPECT_EQ(cp, 0x4E2Du);
}

TEST_F(StrUtf8Test, DecodeFourByte) {
    uint32_t cp;
    EXPECT_EQ(str_utf8_decode("\xf0\x9f\x98\x80", 4, &cp), 4);  // 😀 = U+1F600
    EXPECT_EQ(cp, 0x1F600u);
}

TEST_F(StrUtf8Test, DecodeInvalid) {
    uint32_t cp;
    EXPECT_EQ(str_utf8_decode("\xc0\x80", 2, &cp), -1);  // overlong
    EXPECT_EQ(str_utf8_decode("\xed\xa0\x80", 3, &cp), -1);  // surrogate
    EXPECT_EQ(str_utf8_decode(NULL, 0, &cp), -1);
    EXPECT_EQ(str_utf8_decode("", 0, &cp), -1);
}

TEST_F(StrUtf8Test, EncodeAscii) {
    char buf[4];
    EXPECT_EQ(str_utf8_encode(0x41, buf, 4), 1u);
    EXPECT_EQ(buf[0], 'A');
}

TEST_F(StrUtf8Test, EncodeTwoByte) {
    char buf[4];
    EXPECT_EQ(str_utf8_encode(0x00E9, buf, 4), 2u);  // é
    EXPECT_EQ((unsigned char)buf[0], 0xC3u);
    EXPECT_EQ((unsigned char)buf[1], 0xA9u);
}

TEST_F(StrUtf8Test, EncodeThreeByte) {
    char buf[4];
    EXPECT_EQ(str_utf8_encode(0x4E2D, buf, 4), 3u);  // 中
    EXPECT_EQ((unsigned char)buf[0], 0xE4u);
    EXPECT_EQ((unsigned char)buf[1], 0xB8u);
    EXPECT_EQ((unsigned char)buf[2], 0xADu);
}

TEST_F(StrUtf8Test, EncodeFourByte) {
    char buf[4];
    EXPECT_EQ(str_utf8_encode(0x1F600, buf, 4), 4u);  // 😀
    EXPECT_EQ((unsigned char)buf[0], 0xF0u);
    EXPECT_EQ((unsigned char)buf[1], 0x9Fu);
    EXPECT_EQ((unsigned char)buf[2], 0x98u);
    EXPECT_EQ((unsigned char)buf[3], 0x80u);
}

TEST_F(StrUtf8Test, EncodeInvalid) {
    char buf[4];
    EXPECT_EQ(str_utf8_encode(0xD800, buf, 4), 0u);    // surrogate
    EXPECT_EQ(str_utf8_encode(0x110000, buf, 4), 0u);   // out of range
    EXPECT_EQ(str_utf8_encode(0x41, buf, 0), 0u);       // no capacity
    EXPECT_EQ(str_utf8_encode(0x41, NULL, 4), 0u);      // null buf
}

TEST_F(StrUtf8Test, EncodeCapTooSmall) {
    char buf[2];
    EXPECT_EQ(str_utf8_encode(0x4E2D, buf, 2), 0u);  // needs 3 bytes
}

TEST_F(StrUtf8Test, CharToByte) {
    // "café" = c(1) a(1) f(1) é(2) — byte positions: 0, 1, 2, 3
    const char* s = "caf\xc3\xa9";
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 0), 0u);
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 1), 1u);
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 2), 2u);
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 3), 3u);
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 4), 5u);  // one past end
    EXPECT_EQ(str_utf8_char_to_byte(s, 5, 5), STR_NPOS);  // out of range
}

TEST_F(StrUtf8Test, ByteToChar) {
    const char* s = "caf\xc3\xa9";  // 5 bytes, 4 chars
    EXPECT_EQ(str_utf8_byte_to_char(s, 5, 0), 0u);
    EXPECT_EQ(str_utf8_byte_to_char(s, 5, 1), 1u);
    EXPECT_EQ(str_utf8_byte_to_char(s, 5, 3), 3u);
    EXPECT_EQ(str_utf8_byte_to_char(s, 5, 5), 4u);
}

TEST_F(StrUtf8Test, RoundTripDecodeEncode) {
    // decode then re-encode should produce the same bytes
    const char* inputs[] = {
        "A",                       // 1-byte
        "\xc3\xa9",               // 2-byte (é)
        "\xe4\xb8\xad",          // 3-byte (中)
        "\xf0\x9f\x98\x80",      // 4-byte (😀)
    };
    size_t lengths[] = {1, 2, 3, 4};

    for (int i = 0; i < 4; i++) {
        uint32_t cp;
        int consumed = str_utf8_decode(inputs[i], lengths[i], &cp);
        EXPECT_EQ((size_t)consumed, lengths[i]);

        char buf[4];
        size_t written = str_utf8_encode(cp, buf, 4);
        EXPECT_EQ(written, lengths[i]);
        EXPECT_EQ(memcmp(buf, inputs[i], lengths[i]), 0);
    }
}

/* ================================================================== *
 * §13b  utf.h – Direct UTF Codec & Classification                    *
 * ================================================================== */

class UtfCodecTest : public ::testing::Test {};

// ── utf8_encode ──────────────────────────────────────────────────────

TEST_F(UtfCodecTest, EncodeAscii) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0x41, buf), 1u);
    EXPECT_EQ(buf[0], 'A');
}

TEST_F(UtfCodecTest, EncodeTwoByte) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0x00E9, buf), 2u);  // é
    EXPECT_EQ((unsigned char)buf[0], 0xC3u);
    EXPECT_EQ((unsigned char)buf[1], 0xA9u);
}

TEST_F(UtfCodecTest, EncodeThreeByte) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0x4E2D, buf), 3u);  // 中
    EXPECT_EQ((unsigned char)buf[0], 0xE4u);
    EXPECT_EQ((unsigned char)buf[1], 0xB8u);
    EXPECT_EQ((unsigned char)buf[2], 0xADu);
}

TEST_F(UtfCodecTest, EncodeFourByte) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0x1F600, buf), 4u);  // 😀
    EXPECT_EQ((unsigned char)buf[0], 0xF0u);
    EXPECT_EQ((unsigned char)buf[1], 0x9Fu);
    EXPECT_EQ((unsigned char)buf[2], 0x98u);
    EXPECT_EQ((unsigned char)buf[3], 0x80u);
}

TEST_F(UtfCodecTest, EncodeNullCodepoint) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0, buf), 1u);
    EXPECT_EQ(buf[0], '\0');
}

TEST_F(UtfCodecTest, EncodeBoundaries) {
    char buf[4];
    // max 1-byte: U+007F
    EXPECT_EQ(utf8_encode(0x7F, buf), 1u);
    EXPECT_EQ((unsigned char)buf[0], 0x7Fu);
    // min 2-byte: U+0080
    EXPECT_EQ(utf8_encode(0x80, buf), 2u);
    EXPECT_EQ((unsigned char)buf[0], 0xC2u);
    EXPECT_EQ((unsigned char)buf[1], 0x80u);
    // max 2-byte: U+07FF
    EXPECT_EQ(utf8_encode(0x7FF, buf), 2u);
    // min 3-byte: U+0800
    EXPECT_EQ(utf8_encode(0x800, buf), 3u);
    // max 3-byte: U+FFFF (skip surrogates)
    EXPECT_EQ(utf8_encode(0xFFFF, buf), 3u);
    // min 4-byte: U+10000
    EXPECT_EQ(utf8_encode(0x10000, buf), 4u);
    // max valid: U+10FFFF
    EXPECT_EQ(utf8_encode(0x10FFFF, buf), 4u);
}

TEST_F(UtfCodecTest, EncodeRejectsSurrogates) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0xD800, buf), 0u);  // high surrogate start
    EXPECT_EQ(utf8_encode(0xDBFF, buf), 0u);  // high surrogate end
    EXPECT_EQ(utf8_encode(0xDC00, buf), 0u);  // low surrogate start
    EXPECT_EQ(utf8_encode(0xDFFF, buf), 0u);  // low surrogate end
}

TEST_F(UtfCodecTest, EncodeRejectsOutOfRange) {
    char buf[4];
    EXPECT_EQ(utf8_encode(0x110000, buf), 0u);
    EXPECT_EQ(utf8_encode(0xFFFFFFFF, buf), 0u);
}

TEST_F(UtfCodecTest, EncodeNullBuf) {
    EXPECT_EQ(utf8_encode(0x41, NULL), 0u);
}

// ── utf8_encode_z ────────────────────────────────────────────────────

TEST_F(UtfCodecTest, EncodeZNullTerminated) {
    char buf[5];
    memset(buf, 0xFF, sizeof(buf));
    EXPECT_EQ(utf8_encode_z(0x41, buf), 1u);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[1], '\0');
}

TEST_F(UtfCodecTest, EncodeZFourByte) {
    char buf[5];
    memset(buf, 0xFF, sizeof(buf));
    EXPECT_EQ(utf8_encode_z(0x1F600, buf), 4u);
    EXPECT_EQ(buf[4], '\0');
}

TEST_F(UtfCodecTest, EncodeZInvalid) {
    char buf[5];
    EXPECT_EQ(utf8_encode_z(0xD800, buf), 0u);
}

// ── utf8_decode ──────────────────────────────────────────────────────

TEST_F(UtfCodecTest, DecodeAscii) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("Z", 1, &cp), 1);
    EXPECT_EQ(cp, 0x5Au);
}

TEST_F(UtfCodecTest, DecodeTwoByte) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\xc3\xa9", 2, &cp), 2);  // é
    EXPECT_EQ(cp, 0xE9u);
}

TEST_F(UtfCodecTest, DecodeThreeByte) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\xe4\xb8\xad", 3, &cp), 3);  // 中
    EXPECT_EQ(cp, 0x4E2Du);
}

TEST_F(UtfCodecTest, DecodeFourByte) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\xf0\x9f\x98\x80", 4, &cp), 4);  // 😀
    EXPECT_EQ(cp, 0x1F600u);
}

TEST_F(UtfCodecTest, DecodeRejectsOverlong) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\xc0\x80", 2, &cp), -1);      // overlong NUL
    EXPECT_EQ(utf8_decode("\xe0\x80\x80", 3, &cp), -1);   // overlong NUL 3-byte
    EXPECT_EQ(utf8_decode("\xf0\x80\x80\x80", 4, &cp), -1); // overlong NUL 4-byte
    EXPECT_EQ(utf8_decode("\xc1\xbf", 2, &cp), -1);       // overlong U+007F
}

TEST_F(UtfCodecTest, DecodeRejectsSurrogate) {
    uint32_t cp;
    // U+D800 = ED A0 80
    EXPECT_EQ(utf8_decode("\xed\xa0\x80", 3, &cp), -1);
    // U+DFFF = ED BF BF
    EXPECT_EQ(utf8_decode("\xed\xbf\xbf", 3, &cp), -1);
}

TEST_F(UtfCodecTest, DecodeTruncated) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\xc3", 1, &cp), -1);      // 2-byte missing cont
    EXPECT_EQ(utf8_decode("\xe4\xb8", 2, &cp), -1);   // 3-byte missing cont
    EXPECT_EQ(utf8_decode("\xf0\x9f\x98", 3, &cp), -1); // 4-byte missing cont
}

TEST_F(UtfCodecTest, DecodeNullSafe) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode(NULL, 0, &cp), -1);
    EXPECT_EQ(utf8_decode("a", 0, &cp), -1);
}

TEST_F(UtfCodecTest, DecodeInvalidLeadBytes) {
    uint32_t cp;
    EXPECT_EQ(utf8_decode("\x80", 1, &cp), -1);   // bare continuation
    EXPECT_EQ(utf8_decode("\xff", 1, &cp), -1);
    EXPECT_EQ(utf8_decode("\xfe", 1, &cp), -1);
}

// ── utf8_char_len ────────────────────────────────────────────────────

TEST_F(UtfCodecTest, CharLenAll) {
    EXPECT_EQ(utf8_char_len(0x00), 1u);  // NUL is 1-byte
    EXPECT_EQ(utf8_char_len('A'), 1u);
    EXPECT_EQ(utf8_char_len(0x7F), 1u);
    EXPECT_EQ(utf8_char_len(0xC2), 2u);
    EXPECT_EQ(utf8_char_len(0xDF), 2u);
    EXPECT_EQ(utf8_char_len(0xE0), 3u);
    EXPECT_EQ(utf8_char_len(0xEF), 3u);
    EXPECT_EQ(utf8_char_len(0xF0), 4u);
    EXPECT_EQ(utf8_char_len(0xF4), 4u);
    EXPECT_EQ(utf8_char_len(0x80), 0u);  // continuation
    EXPECT_EQ(utf8_char_len(0xBF), 0u);  // continuation
    // note: 0xC0/0xC1 return 2 (lead-byte pattern); overlong rejection is in utf8_decode
    EXPECT_EQ(utf8_char_len(0xC0), 2u);
    EXPECT_EQ(utf8_char_len(0xC1), 2u);
    // note: 0xF5+ return 4 (lead-byte pattern); range rejection is in utf8_decode
    EXPECT_EQ(utf8_char_len(0xF5), 4u);
    EXPECT_EQ(utf8_char_len(0xFF), 0u);
}

// ── utf8_count ───────────────────────────────────────────────────────

TEST_F(UtfCodecTest, CountEmpty) {
    EXPECT_EQ(utf8_count("", 0), 0u);
    EXPECT_EQ(utf8_count(NULL, 0), 0u);
}

TEST_F(UtfCodecTest, CountAscii) {
    EXPECT_EQ(utf8_count("hello", 5), 5u);
}

TEST_F(UtfCodecTest, CountMixed) {
    // "aé中😀" = 1 + 2 + 3 + 4 = 10 bytes, 4 chars
    const char* s = "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80";
    EXPECT_EQ(utf8_count(s, 10), 4u);
}

TEST_F(UtfCodecTest, CountSWARPath) {
    // 64 ASCII bytes to exercise SWAR acceleration
    char buf[64];
    memset(buf, 'x', 64);
    EXPECT_EQ(utf8_count(buf, 64), 64u);
}

TEST_F(UtfCodecTest, CountSWARMultibyte) {
    // 16 × 3-byte chars = 48 bytes, should trigger SWAR
    char buf[48];
    for (int i = 0; i < 16; i++) {
        buf[i*3]   = (char)0xE4;
        buf[i*3+1] = (char)0xB8;
        buf[i*3+2] = (char)0xAD;
    }
    EXPECT_EQ(utf8_count(buf, 48), 16u);
}

// ── utf8_valid ───────────────────────────────────────────────────────

TEST_F(UtfCodecTest, ValidGood) {
    EXPECT_TRUE(utf8_valid("hello", 5));
    EXPECT_TRUE(utf8_valid("", 0));
    EXPECT_TRUE(utf8_valid(NULL, 0));
    EXPECT_TRUE(utf8_valid("caf\xc3\xa9", 5));
    EXPECT_TRUE(utf8_valid("\xe4\xb8\xad", 3));
    EXPECT_TRUE(utf8_valid("\xf0\x9f\x98\x80", 4));
}

TEST_F(UtfCodecTest, ValidBad) {
    EXPECT_FALSE(utf8_valid("\xc0\x80", 2));           // overlong
    EXPECT_FALSE(utf8_valid("\xed\xa0\x80", 3));       // surrogate
    EXPECT_FALSE(utf8_valid("\xe4\xb8", 2));           // truncated
    EXPECT_FALSE(utf8_valid("\xff", 1));               // invalid lead
    EXPECT_FALSE(utf8_valid("\x80", 1));               // bare continuation
    EXPECT_FALSE(utf8_valid("abc\xfe\x80\x80\x80", 7)); // invalid embedded
}

TEST_F(UtfCodecTest, ValidLongMixed) {
    // valid: 20 ascii + 2-byte + 3-byte + 4-byte = 29 bytes
    char buf[29];
    memset(buf, 'a', 20);
    buf[20] = (char)0xC3; buf[21] = (char)0xA9;          // é
    buf[22] = (char)0xE4; buf[23] = (char)0xB8; buf[24] = (char)0xAD;  // 中
    buf[25] = (char)0xF0; buf[26] = (char)0x9F; buf[27] = (char)0x98; buf[28] = (char)0x80; // 😀
    EXPECT_TRUE(utf8_valid(buf, 29));
}

// ── utf8_char_to_byte / utf8_byte_to_char ────────────────────────────

TEST_F(UtfCodecTest, CharToByteMultibyte) {
    // "aé中" = 1 + 2 + 3 = 6 bytes, 3 chars
    const char* s = "a\xc3\xa9\xe4\xb8\xad";
    EXPECT_EQ(utf8_char_to_byte(s, 6, 0), 0u);   // 'a' at byte 0
    EXPECT_EQ(utf8_char_to_byte(s, 6, 1), 1u);   // 'é' at byte 1
    EXPECT_EQ(utf8_char_to_byte(s, 6, 2), 3u);   // '中' at byte 3
    EXPECT_EQ(utf8_char_to_byte(s, 6, 3), 6u);   // one past end
    EXPECT_EQ(utf8_char_to_byte(s, 6, 4), (size_t)-1);  // out of range
}

TEST_F(UtfCodecTest, ByteToCharMultibyte) {
    const char* s = "a\xc3\xa9\xe4\xb8\xad";
    EXPECT_EQ(utf8_byte_to_char(s, 6, 0), 0u);
    EXPECT_EQ(utf8_byte_to_char(s, 6, 1), 1u);
    EXPECT_EQ(utf8_byte_to_char(s, 6, 3), 2u);
    EXPECT_EQ(utf8_byte_to_char(s, 6, 6), 3u);
}

// ── utf16_decode_pair ────────────────────────────────────────────────

TEST_F(UtfCodecTest, Utf16DecodePairValid) {
    // U+1F600 = D83D DE00
    EXPECT_EQ(utf16_decode_pair(0xD83D, 0xDE00), 0x1F600u);
    // U+10000 = D800 DC00
    EXPECT_EQ(utf16_decode_pair(0xD800, 0xDC00), 0x10000u);
    // U+10FFFF = DBFF DFFF
    EXPECT_EQ(utf16_decode_pair(0xDBFF, 0xDFFF), 0x10FFFFu);
}

TEST_F(UtfCodecTest, Utf16DecodePairInvalid) {
    // reversed surrogates
    EXPECT_EQ(utf16_decode_pair(0xDE00, 0xD83D), 0u);
    // non-surrogates
    EXPECT_EQ(utf16_decode_pair(0x0041, 0xDC00), 0u);
    EXPECT_EQ(utf16_decode_pair(0xD800, 0x0041), 0u);
}

// ── utf16_encode ─────────────────────────────────────────────────────

TEST_F(UtfCodecTest, Utf16EncodeBMP) {
    uint16_t buf[2] = {0, 0};
    EXPECT_EQ(utf16_encode(0x41, buf), 1);      // 'A'
    EXPECT_EQ(buf[0], 0x0041u);
    EXPECT_EQ(utf16_encode(0xFFFF, buf), 1);    // max BMP
    EXPECT_EQ(buf[0], 0xFFFFu);
}

TEST_F(UtfCodecTest, Utf16EncodeSupplementary) {
    uint16_t buf[2] = {0, 0};
    EXPECT_EQ(utf16_encode(0x1F600, buf), 2);   // 😀
    EXPECT_EQ(buf[0], 0xD83Du);
    EXPECT_EQ(buf[1], 0xDE00u);
}

TEST_F(UtfCodecTest, Utf16EncodeBoundary) {
    uint16_t buf[2] = {0, 0};
    EXPECT_EQ(utf16_encode(0x10000, buf), 2);   // min supplementary
    EXPECT_EQ(buf[0], 0xD800u);
    EXPECT_EQ(buf[1], 0xDC00u);
    EXPECT_EQ(utf16_encode(0x10FFFF, buf), 2);  // max valid
    EXPECT_EQ(buf[0], 0xDBFFu);
    EXPECT_EQ(buf[1], 0xDFFFu);
}

TEST_F(UtfCodecTest, Utf16EncodeInvalid) {
    uint16_t buf[2];
    EXPECT_EQ(utf16_encode(0xD800, buf), 0);    // surrogate
    EXPECT_EQ(utf16_encode(0xDFFF, buf), 0);    // surrogate
    EXPECT_EQ(utf16_encode(0x110000, buf), 0);  // out of range
}

TEST_F(UtfCodecTest, Utf16RoundTrip) {
    // encode then decode should round-trip for supplementary codepoints
    uint32_t cps[] = {0x10000, 0x1F600, 0x10FFFF, 0x20000};
    for (int i = 0; i < 4; i++) {
        uint16_t buf[2];
        int n = utf16_encode(cps[i], buf);
        EXPECT_EQ(n, 2);
        uint32_t decoded = utf16_decode_pair(buf[0], buf[1]);
        EXPECT_EQ(decoded, cps[i]);
    }
}

// ── utf_is_surrogate / utf_is_valid_codepoint ────────────────────────

TEST_F(UtfCodecTest, IsSurrogate) {
    EXPECT_TRUE(utf_is_surrogate(0xD800));
    EXPECT_TRUE(utf_is_surrogate(0xDBFF));
    EXPECT_TRUE(utf_is_surrogate(0xDC00));
    EXPECT_TRUE(utf_is_surrogate(0xDFFF));
    EXPECT_FALSE(utf_is_surrogate(0xD7FF));
    EXPECT_FALSE(utf_is_surrogate(0xE000));
    EXPECT_FALSE(utf_is_surrogate(0x0041));
}

TEST_F(UtfCodecTest, IsValidCodepoint) {
    EXPECT_TRUE(utf_is_valid_codepoint(0));
    EXPECT_TRUE(utf_is_valid_codepoint(0x41));
    EXPECT_TRUE(utf_is_valid_codepoint(0xFFFF));
    EXPECT_TRUE(utf_is_valid_codepoint(0x10FFFF));
    EXPECT_FALSE(utf_is_valid_codepoint(0xD800));
    EXPECT_FALSE(utf_is_valid_codepoint(0xDFFF));
    EXPECT_FALSE(utf_is_valid_codepoint(0x110000));
}

// ── utf_is_cjk ──────────────────────────────────────────────────────

class UtfClassifyTest : public ::testing::Test {};

TEST_F(UtfClassifyTest, CjkUnifiedIdeographs) {
    EXPECT_TRUE(utf_is_cjk(0x4E00));   // first
    EXPECT_TRUE(utf_is_cjk(0x9FFF));   // last
    EXPECT_TRUE(utf_is_cjk(0x6C49));   // 汉
}

TEST_F(UtfClassifyTest, CjkExtensionA) {
    EXPECT_TRUE(utf_is_cjk(0x3400));
    EXPECT_TRUE(utf_is_cjk(0x4DBF));
}

TEST_F(UtfClassifyTest, CjkExtensionB) {
    EXPECT_TRUE(utf_is_cjk(0x20000));
    EXPECT_TRUE(utf_is_cjk(0x2A6DF));
}

TEST_F(UtfClassifyTest, Hiragana) {
    EXPECT_TRUE(utf_is_cjk(0x3040));   // first
    EXPECT_TRUE(utf_is_cjk(0x309F));   // last
    EXPECT_TRUE(utf_is_cjk(0x3042));   // あ
}

TEST_F(UtfClassifyTest, Katakana) {
    EXPECT_TRUE(utf_is_cjk(0x30A0));
    EXPECT_TRUE(utf_is_cjk(0x30FF));
    EXPECT_TRUE(utf_is_cjk(0x30A2));   // ア
}

TEST_F(UtfClassifyTest, HangulSyllables) {
    EXPECT_TRUE(utf_is_cjk(0xAC00));   // 가
    EXPECT_TRUE(utf_is_cjk(0xD7AF));
}

TEST_F(UtfClassifyTest, HalfwidthKatakana) {
    EXPECT_TRUE(utf_is_cjk(0xFF65));
    EXPECT_TRUE(utf_is_cjk(0xFF9F));
}

TEST_F(UtfClassifyTest, CjkNonMatches) {
    EXPECT_FALSE(utf_is_cjk(0x41));     // 'A'
    EXPECT_FALSE(utf_is_cjk(0x0410));   // Cyrillic А
    EXPECT_FALSE(utf_is_cjk(0x1100));   // Hangul Jamo (not in CJK range)
}

// ── utf_is_hangul ────────────────────────────────────────────────────

TEST_F(UtfClassifyTest, HangulJamo) {
    EXPECT_TRUE(utf_is_hangul(0x1100));   // first
    EXPECT_TRUE(utf_is_hangul(0x11FF));   // last
}

TEST_F(UtfClassifyTest, HangulCompatibilityJamo) {
    EXPECT_TRUE(utf_is_hangul(0x3130));
    EXPECT_TRUE(utf_is_hangul(0x318F));
}

TEST_F(UtfClassifyTest, HangulJamoExtendedA) {
    EXPECT_TRUE(utf_is_hangul(0xA960));
    EXPECT_TRUE(utf_is_hangul(0xA97F));
}

TEST_F(UtfClassifyTest, HangulSyllablesClassify) {
    EXPECT_TRUE(utf_is_hangul(0xAC00));   // 가
    EXPECT_TRUE(utf_is_hangul(0xD7AF));
}

TEST_F(UtfClassifyTest, HangulJamoExtendedB) {
    EXPECT_TRUE(utf_is_hangul(0xD7B0));
    EXPECT_TRUE(utf_is_hangul(0xD7FF));
}

TEST_F(UtfClassifyTest, HangulNonMatches) {
    EXPECT_FALSE(utf_is_hangul(0x41));
    EXPECT_FALSE(utf_is_hangul(0x4E00));   // CJK ideograph
    EXPECT_FALSE(utf_is_hangul(0x10FF));   // just before Jamo
    EXPECT_FALSE(utf_is_hangul(0xD800));   // surrogate, not Hangul
}

// ── utf_is_emoji_for_zwj ─────────────────────────────────────────────

TEST_F(UtfClassifyTest, EmojiForZwjSMP) {
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x1F600));  // 😀
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x1F000));  // first SMP emoji block
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x1FFFF));  // last SMP emoji block
}

TEST_F(UtfClassifyTest, EmojiForZwjMiscSymbols) {
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x2600));   // ☀
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x27BF));   // end Dingbats
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x2300));   // Misc Technical start
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x2B50));   // ⭐ in Misc Symbols&Arrows
}

TEST_F(UtfClassifyTest, EmojiForZwjSpecials) {
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x200D));   // ZWJ itself
    EXPECT_TRUE(utf_is_emoji_for_zwj(0x2764));   // ❤ Heavy heart
}

TEST_F(UtfClassifyTest, EmojiForZwjNonMatches) {
    EXPECT_FALSE(utf_is_emoji_for_zwj(0x41));     // 'A'
    EXPECT_FALSE(utf_is_emoji_for_zwj(0x4E00));   // CJK
    EXPECT_FALSE(utf_is_emoji_for_zwj(0x0041));   // Latin
}

// ── utf_is_zwj_composition_base ──────────────────────────────────────

TEST_F(UtfClassifyTest, ZwjCompBasePersonEmoji) {
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F466));  // Boy
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F469));  // Woman
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F9D1));  // Person
}

TEST_F(UtfClassifyTest, ZwjCompBaseObjects) {
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F441));  // Eye
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F3F3));  // Flag white
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F3F4));  // Flag black
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F408));  // Cat
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F415));  // Dog
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F43B));  // Bear
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F426));  // Bird
    EXPECT_TRUE(utf_is_zwj_composition_base(0x1F48B));  // Kiss Mark
    EXPECT_TRUE(utf_is_zwj_composition_base(0x2764));   // Heart
}

TEST_F(UtfClassifyTest, ZwjCompBaseNonMatches) {
    EXPECT_FALSE(utf_is_zwj_composition_base(0x41));
    EXPECT_FALSE(utf_is_zwj_composition_base(0x1F600));  // 😀 not a ZWJ base
    EXPECT_FALSE(utf_is_zwj_composition_base(0x1F465));  // just before Boy
    EXPECT_FALSE(utf_is_zwj_composition_base(0x1F46A));  // just after Woman
}

// ── Round-trip: utf8_encode → utf8_decode ────────────────────────────

TEST_F(UtfCodecTest, RoundTripEncDec) {
    uint32_t test_cps[] = {
        0, 0x41, 0x7F,           // 1-byte
        0x80, 0xE9, 0x7FF,      // 2-byte
        0x800, 0x4E2D, 0xFFFD, 0xFFFF,  // 3-byte
        0x10000, 0x1F600, 0x10FFFF      // 4-byte
    };
    for (size_t i = 0; i < sizeof(test_cps)/sizeof(test_cps[0]); i++) {
        char buf[4];
        size_t n = utf8_encode(test_cps[i], buf);
        ASSERT_GT(n, 0u) << "encode failed for U+" << std::hex << test_cps[i];

        uint32_t decoded;
        int consumed = utf8_decode(buf, n, &decoded);
        EXPECT_EQ((size_t)consumed, n) << "decode length mismatch for U+" << std::hex << test_cps[i];
        EXPECT_EQ(decoded, test_cps[i]) << "round-trip failed for U+" << std::hex << test_cps[i];
    }
}

/* ================================================================== *
 * §14  Escape                                                        *
 * ================================================================== */

class StrEscapeTest : public ::testing::Test {};

TEST_F(StrEscapeTest, JsonBasic) {
    const char* s = "hello \"world\"\n";
    size_t needed = str_escape_len(s, 14, STR_ESC_JSON);
    char* buf = (char*)malloc(needed + 1);
    size_t written = str_escape(buf, s, 14, STR_ESC_JSON);
    buf[written] = '\0';
    EXPECT_STREQ(buf, "hello \\\"world\\\"\\n");
    EXPECT_EQ(written, needed);
    free(buf);
}

TEST_F(StrEscapeTest, JsonControlChars) {
    const char* s = "\x01\x02";
    size_t needed = str_escape_len(s, 2, STR_ESC_JSON);
    EXPECT_EQ(needed, 12u); // two \u00XX sequences
    char buf[16];
    str_escape(buf, s, 2, STR_ESC_JSON);
    buf[12] = '\0';
    EXPECT_STREQ(buf, "\\u0001\\u0002");
}

TEST_F(StrEscapeTest, JsonSpecials) {
    const char* s = "\\\t\b\f\r";
    char buf[32];
    size_t w = str_escape(buf, s, 5, STR_ESC_JSON);
    buf[w] = '\0';
    EXPECT_STREQ(buf, "\\\\\\t\\b\\f\\r");
}

TEST_F(StrEscapeTest, XmlBasic) {
    const char* s = "<div class=\"main\">&</div>";
    size_t needed = str_escape_len(s, 25, STR_ESC_XML);
    char* buf = (char*)malloc(needed + 1);
    size_t written = str_escape(buf, s, 25, STR_ESC_XML);
    buf[written] = '\0';
    EXPECT_STREQ(buf, "&lt;div class=&quot;main&quot;&gt;&amp;&lt;/div&gt;");
    free(buf);
}

TEST_F(StrEscapeTest, UrlBasic) {
    const char* s = "hello world!";
    size_t needed = str_escape_len(s, 12, STR_ESC_URL);
    char* buf = (char*)malloc(needed + 1);
    size_t written = str_escape(buf, s, 12, STR_ESC_URL);
    buf[written] = '\0';
    EXPECT_STREQ(buf, "hello%20world%21");
    free(buf);
}

TEST_F(StrEscapeTest, UrlSafeChars) {
    const char* s = "abc-_.~123";
    size_t needed = str_escape_len(s, 10, STR_ESC_URL);
    EXPECT_EQ(needed, 10u); // all safe
}

TEST_F(StrEscapeTest, SizingWithNull) {
    // str_escape(NULL, ...) should give the same result as str_escape_len
    const char* s = "hello\n\"world\"";
    size_t len1 = str_escape(NULL, s, 13, STR_ESC_JSON);
    size_t len2 = str_escape_len(s, 13, STR_ESC_JSON);
    EXPECT_EQ(len1, len2);
}

TEST_F(StrEscapeTest, NullInput) {
    EXPECT_EQ(str_escape_len(NULL, 0, STR_ESC_JSON), 0u);
}

/* ================================================================== *
 * §15  Span / Predicate                                              *
 * ================================================================== */

class StrSpanTest : public ::testing::Test {};

TEST_F(StrSpanTest, SpanWhitespace) {
    EXPECT_EQ(str_span_whitespace("  \thello", 8), 3u);
    EXPECT_EQ(str_span_whitespace("hello", 5), 0u);
    EXPECT_EQ(str_span_whitespace("   ", 3), 3u);
    EXPECT_EQ(str_span_whitespace(NULL, 0), 0u);
}

TEST_F(StrSpanTest, SpanDigits) {
    EXPECT_EQ(str_span_digits("12345abc", 8), 5u);
    EXPECT_EQ(str_span_digits("abc", 3), 0u);
    EXPECT_EQ(str_span_digits("999", 3), 3u);
}

TEST_F(StrSpanTest, SpanCustom) {
    EXPECT_EQ(str_span("aaabcd", 6, str_is_alpha), 6u);
    EXPECT_EQ(str_span("123abc", 6, str_is_digit), 3u);
    EXPECT_EQ(str_span("abc", 3, str_is_digit), 0u);
}

TEST_F(StrSpanTest, All) {
    EXPECT_TRUE(str_all("12345", 5, str_is_digit));
    EXPECT_FALSE(str_all("123a5", 5, str_is_digit));
    EXPECT_TRUE(str_all("", 0, str_is_digit)); // vacuously true
}

TEST_F(StrSpanTest, Predicates) {
    EXPECT_TRUE(str_is_space(' '));
    EXPECT_TRUE(str_is_space('\t'));
    EXPECT_FALSE(str_is_space('a'));

    EXPECT_TRUE(str_is_digit('0'));
    EXPECT_TRUE(str_is_digit('9'));
    EXPECT_FALSE(str_is_digit('a'));

    EXPECT_TRUE(str_is_alpha('a'));
    EXPECT_TRUE(str_is_alpha('Z'));
    EXPECT_FALSE(str_is_alpha('5'));

    EXPECT_TRUE(str_is_alnum('a'));
    EXPECT_TRUE(str_is_alnum('5'));
    EXPECT_FALSE(str_is_alnum('!'));

    EXPECT_TRUE(str_is_upper('A'));
    EXPECT_FALSE(str_is_upper('a'));

    EXPECT_TRUE(str_is_lower('a'));
    EXPECT_FALSE(str_is_lower('A'));

    EXPECT_TRUE(str_is_hex('0'));
    EXPECT_TRUE(str_is_hex('a'));
    EXPECT_TRUE(str_is_hex('F'));
    EXPECT_FALSE(str_is_hex('g'));
}

/* ================================================================== *
 * §16  Formatting                                                    *
 * ================================================================== */

class StrFmtTest : public ::testing::Test {};

TEST_F(StrFmtTest, FmtBasic) {
    char buf[64];
    int n = str_fmt(buf, 64, "hello %s %d", "world", 42);
    EXPECT_EQ(n, 14);
    EXPECT_STREQ(buf, "hello world 42");
}

TEST_F(StrFmtTest, FmtTruncation) {
    char buf[8];
    int n = str_fmt(buf, 8, "hello world");
    EXPECT_EQ(n, 7); // capped at cap-1
    EXPECT_STREQ(buf, "hello w");
}

TEST_F(StrFmtTest, FmtZeroCap) {
    char buf[4] = "xxx";
    int n = str_fmt(buf, 0, "hello");
    EXPECT_EQ(n, 0);
}

TEST_F(StrFmtTest, HexEncode) {
    char buf[16];
    str_hex_encode(buf, "\x01\xAB\xFF", 3);
    EXPECT_STREQ(buf, "01abff");
}

TEST_F(StrFmtTest, HexDecode) {
    char buf[4];
    size_t n = str_hex_decode(buf, "48656c6c", 8);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(memcmp(buf, "Hell", 4), 0);
}

TEST_F(StrFmtTest, HexRoundTrip) {
    const char* orig = "Hello";
    char hex[16];
    str_hex_encode(hex, orig, 5);
    char decoded[8];
    size_t n = str_hex_decode(decoded, hex, 10);
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(memcmp(decoded, orig, 5), 0);
}

TEST_F(StrFmtTest, HexDecodeBadInput) {
    char buf[4];
    size_t n = str_hex_decode(buf, "zz", 2);
    EXPECT_EQ(n, 0u); // 'z' is not valid hex
}

TEST_F(StrFmtTest, HexNullSafety) {
    EXPECT_EQ(str_hex_encode(NULL, "a", 1), nullptr);
    char buf[4];
    EXPECT_EQ(str_hex_decode(buf, NULL, 0), 0u);
}
