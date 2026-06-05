/*
 * Scanner tier Test Suite (GTest) — lib/str.h §17
 * ================================================
 *
 * Covers the NUL-safe parser scanner primitives:
 *   §17.1  Character classes — str_char_in_set, str_char_is_*
 *   §17.2  Bounded scanners  — strn_skip_*, strn_scan_*, strn_count_run
 *   §17.3  NUL-terminated    — str_skip_*, str_scan_*, str_count_run
 *   §17.4  Cursor            — str_cursor_peek/skip/count_run/mark
 *
 * Plus a regression test for the AsciiDoc underline crash class: a run-count
 * loop must never treat '\0' as a valid delimiter and walk past the buffer.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../../lib/str.h"
}

/* ================================================================== *
 *  §17.1  NUL-safe character classes                                 *
 * ================================================================== */

class StrCharClassTest : public ::testing::Test {};

TEST_F(StrCharClassTest, InSetNulIsNeverMember) {
    /* the core fix: '\0' must not match the terminator of the set literal */
    EXPECT_FALSE(str_char_in_set('\0', "=-~^+"));
    EXPECT_FALSE(str_char_in_set('\0', ""));
    EXPECT_FALSE(str_char_in_set('\0', nullptr));
}

TEST_F(StrCharClassTest, InSetBasic) {
    EXPECT_TRUE(str_char_in_set('=', "=-~^+"));
    EXPECT_TRUE(str_char_in_set('+', "=-~^+"));
    EXPECT_FALSE(str_char_in_set('x', "=-~^+"));
    EXPECT_FALSE(str_char_in_set('a', nullptr));
}

TEST_F(StrCharClassTest, ClassesRejectNul) {
    EXPECT_FALSE(str_char_is_ascii_space('\0'));
    EXPECT_FALSE(str_char_is_line_space('\0'));
    EXPECT_FALSE(str_char_is_digit('\0'));
    EXPECT_FALSE(str_char_is_alpha('\0'));
    EXPECT_FALSE(str_char_is_alnum('\0'));
    EXPECT_FALSE(str_char_is_ident('\0'));
}

TEST_F(StrCharClassTest, ClassesBasic) {
    EXPECT_TRUE(str_char_is_ascii_space(' '));
    EXPECT_TRUE(str_char_is_ascii_space('\t'));
    EXPECT_TRUE(str_char_is_ascii_space('\n'));
    EXPECT_TRUE(str_char_is_ascii_space('\f'));
    EXPECT_TRUE(str_char_is_ascii_space('\v'));
    EXPECT_FALSE(str_char_is_ascii_space('x'));

    EXPECT_TRUE(str_char_is_line_space(' '));
    EXPECT_TRUE(str_char_is_line_space('\t'));
    EXPECT_FALSE(str_char_is_line_space('\n'));

    EXPECT_TRUE(str_char_is_digit('0'));
    EXPECT_TRUE(str_char_is_digit('9'));
    EXPECT_FALSE(str_char_is_digit('a'));

    EXPECT_TRUE(str_char_is_alpha('a'));
    EXPECT_TRUE(str_char_is_alpha('Z'));
    EXPECT_FALSE(str_char_is_alpha('0'));

    EXPECT_TRUE(str_char_is_alnum('5'));
    EXPECT_TRUE(str_char_is_alnum('q'));
    EXPECT_FALSE(str_char_is_alnum('_'));

    EXPECT_TRUE(str_char_is_ident('_'));
    EXPECT_TRUE(str_char_is_ident('a'));
    EXPECT_TRUE(str_char_is_ident('0'));
    EXPECT_FALSE(str_char_is_ident('-'));
}

TEST_F(StrCharClassTest, ClassesSafeOnHighBitBytes) {
    /* signed-char UB concern: bytes >= 0x80 must classify, not crash/UB */
    char hi = (char)0xC3;  /* UTF-8 lead byte, negative as signed char */
    EXPECT_FALSE(str_char_is_ascii_space(hi));
    EXPECT_FALSE(str_char_is_digit(hi));
    EXPECT_FALSE(str_char_is_alpha(hi));
    EXPECT_FALSE(str_char_in_set(hi, " \t"));
}

/* ================================================================== *
 *  §17.2  Bounded scanners                                           *
 * ================================================================== */

class StrnScanTest : public ::testing::Test {};

TEST_F(StrnScanTest, SkipLineSpaceLandsOnContent) {
    const char* s = " \tX";
    const char* end = s + 3;
    EXPECT_EQ(*strn_skip_line_space(s, end), 'X');
}

TEST_F(StrnScanTest, SkipNeverPastEnd) {
    const char* s = "    ";          /* all spaces, no content */
    const char* end = s + 4;
    EXPECT_EQ(strn_skip_line_space(s, end), end);
    EXPECT_EQ(strn_skip_ascii_space(s, end), end);
}

TEST_F(StrnScanTest, SkipDigits) {
    const char* s = "12345abc";
    const char* end = s + 8;
    EXPECT_EQ(*strn_skip_digits(s, end), 'a');
}

TEST_F(StrnScanTest, SkipChars) {
    const char* s = "***text";
    const char* end = s + 7;
    EXPECT_EQ(*strn_skip_chars(s, end, "*"), 't');
}

TEST_F(StrnScanTest, ScanUntilChar) {
    const char* s = "abc]def";
    const char* end = s + 7;
    EXPECT_EQ(*strn_scan_until_char(s, end, ']'), ']');
}

TEST_F(StrnScanTest, ScanUntilCharNotFoundStopsAtEnd) {
    const char* s = "abcdef";
    const char* end = s + 6;
    EXPECT_EQ(strn_scan_until_char(s, end, ']'), end);
}

TEST_F(StrnScanTest, ScanUntilAny) {
    const char* s = "abc,def";
    const char* end = s + 7;
    EXPECT_EQ(*strn_scan_until_any(s, end, ",;"), ',');
}

TEST_F(StrnScanTest, ScanToLineEnd) {
    const char* s = "hello\nworld";
    const char* end = s + 11;
    EXPECT_EQ(*strn_scan_to_line_end(s, end), '\n');

    const char* cr = "hello\rworld";
    EXPECT_EQ(*strn_scan_to_line_end(cr, cr + 11), '\r');

    const char* none = "noeol";
    EXPECT_EQ(strn_scan_to_line_end(none, none + 5), none + 5);
}

TEST_F(StrnScanTest, CountRun) {
    const char* s = "====text";
    const char* end = s + 8;
    EXPECT_EQ(strn_count_run(s, end, '='), 4u);
}

TEST_F(StrnScanTest, CountRunMarkerNulReturnsZero) {
    const char* s = "====";
    const char* end = s + 4;
    EXPECT_EQ(strn_count_run(s, end, '\0'), 0u);
}

TEST_F(StrnScanTest, CountRunBoundedByEnd) {
    const char* s = "====";          /* end cuts the run short */
    EXPECT_EQ(strn_count_run(s, s + 2, '='), 2u);
}

TEST_F(StrnScanTest, NullPointerTolerant) {
    EXPECT_EQ(strn_skip_line_space(nullptr, nullptr), nullptr);
    EXPECT_EQ(strn_scan_until_char(nullptr, nullptr, 'x'), nullptr);
    EXPECT_EQ(strn_count_run(nullptr, nullptr, '='), 0u);
}

/* ================================================================== *
 *  §17.3  NUL-terminated scanners                                    *
 * ================================================================== */

class StrScanTest : public ::testing::Test {};

TEST_F(StrScanTest, SkipLineSpace) {
    EXPECT_EQ(*str_skip_line_space(" \tX"), 'X');
    EXPECT_EQ(*str_skip_line_space("X"), 'X');
}

TEST_F(StrScanTest, SkipStopsAtNul) {
    const char* s = "   ";
    EXPECT_EQ(*str_skip_line_space(s), '\0');  /* lands on terminator, not past */
}

TEST_F(StrScanTest, ScanUntilCharStopsAtNul) {
    const char* s = "abc";
    EXPECT_EQ(*str_scan_until_char(s, ']'), '\0');
}

TEST_F(StrScanTest, ScanToLineEnd) {
    EXPECT_EQ(*str_scan_to_line_end("abc\ndef"), '\n');
    EXPECT_EQ(*str_scan_to_line_end("abc"), '\0');
}

TEST_F(StrScanTest, CountRunNulTerminatedMode) {
    EXPECT_EQ(str_count_run("~~~x", 0, '~'), 3u);
}

TEST_F(StrScanTest, CountRunEmptyAndNulMarker) {
    EXPECT_EQ(str_count_run("", 0, '\0'), 0u);
    EXPECT_EQ(str_count_run("====", 0, '\0'), 0u);
    EXPECT_EQ(str_count_run("", 0, '='), 0u);
}

TEST_F(StrScanTest, CountRunMaxLen) {
    EXPECT_EQ(str_count_run("=====", 3, '='), 3u);
}

/* ================================================================== *
 *  §17.4  Cursor                                                     *
 * ================================================================== */

class StrCursorTest : public ::testing::Test {};

TEST_F(StrCursorTest, PeekReturnsNulAtEnd) {
    const char* s = "ab";
    StrCursor c = { s, s + 2 };
    EXPECT_EQ(str_cursor_peek(&c), 'a');
    c.p++;
    EXPECT_EQ(str_cursor_peek(&c), 'b');
    c.p++;
    EXPECT_TRUE(str_cursor_at_end(&c));
    EXPECT_EQ(str_cursor_peek(&c), '\0');   /* never over-reads */
}

TEST_F(StrCursorTest, SkipAndCountRun) {
    const char* s = "  ===rest";
    StrCursor c = { s, s + 9 };
    str_cursor_skip_line_space(&c);
    EXPECT_EQ(str_cursor_peek(&c), '=');
    const char* mark = str_cursor_mark(&c);
    size_t run = str_cursor_count_run(&c, '=');
    EXPECT_EQ(run, 3u);
    EXPECT_EQ(str_cursor_mark(&c) - mark, 3);
    EXPECT_EQ(str_cursor_peek(&c), 'r');
}

TEST_F(StrCursorTest, NullTolerant) {
    EXPECT_TRUE(str_cursor_at_end(nullptr));
    EXPECT_EQ(str_cursor_peek(nullptr), '\0');
    EXPECT_EQ(str_cursor_count_run(nullptr, '='), 0u);
    EXPECT_EQ(str_cursor_mark(nullptr), nullptr);
}

/* ================================================================== *
 *  Regression: AsciiDoc underline NUL-run crash class                *
 * ================================================================== */

class ScanRegressionTest : public ::testing::Test {};

TEST_F(ScanRegressionTest, EmptyLineUnderlineDoesNotWalkPastBuffer) {
    /* Reproduces the failure shape from asciidoc_adapter.cpp: an empty next
     * line reaches a delimiter-membership + run-count scan. The old code did
     *   if (strchr("=-~^+", *ul)) { char m=*ul; while (*ul==m) ul++; }
     * which, on *ul=='\0', matched the literal's terminator and counted a run
     * of NUL bytes off the end. The NUL-safe helpers must stay put. */
    const char* empty_line = "";
    EXPECT_FALSE(str_char_in_set(*empty_line, "=-~^+"));
    /* even if a caller skipped the membership guard, run-count is NUL-safe */
    EXPECT_EQ(str_count_run(empty_line, 0, *empty_line), 0u);
}

TEST_F(ScanRegressionTest, UnderlineRunCountedCorrectly) {
    const char* ul = "====";
    ASSERT_TRUE(str_char_in_set(*ul, "=-~^+"));
    EXPECT_EQ(str_count_run(ul, 0, *ul), 4u);
}
