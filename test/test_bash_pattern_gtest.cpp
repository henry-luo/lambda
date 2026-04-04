/**
 * test_bash_pattern_gtest.cpp — Unit tests for bash_pattern.h
 *
 * Tests glob matching, extended glob patterns, bracket expressions,
 * POSIX character classes, case-insensitive matching, and edge cases.
 */

#include <gtest/gtest.h>
#include "../lambda/bash/bash_pattern.h"

// ============================================================================
// Basic glob: * ? literals
// ============================================================================

TEST(BashPattern, LiteralExact) {
    EXPECT_EQ(1, bash_pattern_match("hello", "hello", 0));
    EXPECT_EQ(0, bash_pattern_match("hello", "world", 0));
    EXPECT_EQ(1, bash_pattern_match("", "", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "", 0));
    EXPECT_EQ(0, bash_pattern_match("", "a", 0));
}

TEST(BashPattern, StarWildcard) {
    EXPECT_EQ(1, bash_pattern_match("hello", "*", 0));
    EXPECT_EQ(1, bash_pattern_match("", "*", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "h*", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "*o", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "h*o", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "*ell*", 0));
    EXPECT_EQ(0, bash_pattern_match("hello", "x*", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "**", 0));
    EXPECT_EQ(1, bash_pattern_match("hello", "h**o", 0));
}

TEST(BashPattern, QuestionMark) {
    EXPECT_EQ(1, bash_pattern_match("a", "?", 0));
    EXPECT_EQ(0, bash_pattern_match("", "?", 0));
    EXPECT_EQ(0, bash_pattern_match("ab", "?", 0));
    EXPECT_EQ(1, bash_pattern_match("ab", "??", 0));
    EXPECT_EQ(1, bash_pattern_match("abc", "a?c", 0));
    EXPECT_EQ(0, bash_pattern_match("ac", "a?c", 0));
}

TEST(BashPattern, StarAndQuestion) {
    EXPECT_EQ(1, bash_pattern_match("abc", "a*?", 0));
    EXPECT_EQ(1, bash_pattern_match("abc", "?*c", 0));
    EXPECT_EQ(1, bash_pattern_match("abc", "?*?", 0));
    EXPECT_EQ(1, bash_pattern_match("a", "?*", 0));
    EXPECT_EQ(0, bash_pattern_match("", "?*", 0));
}

TEST(BashPattern, BackslashEscape) {
    EXPECT_EQ(1, bash_pattern_match("*", "\\*", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "\\*", 0));
    EXPECT_EQ(1, bash_pattern_match("?", "\\?", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "\\?", 0));
    EXPECT_EQ(1, bash_pattern_match("hello*world", "hello\\*world", 0));
    EXPECT_EQ(1, bash_pattern_match("[", "\\[", 0));
}

// ============================================================================
// Bracket expressions
// ============================================================================

TEST(BashPattern, BracketBasic) {
    EXPECT_EQ(1, bash_pattern_match("a", "[abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("b", "[abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("c", "[abc]", 0));
    EXPECT_EQ(0, bash_pattern_match("d", "[abc]", 0));
    EXPECT_EQ(0, bash_pattern_match("", "[abc]", 0));
}

TEST(BashPattern, BracketNegation) {
    EXPECT_EQ(0, bash_pattern_match("a", "[!abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("d", "[!abc]", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "[^abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("d", "[^abc]", 0));
}

TEST(BashPattern, BracketRange) {
    EXPECT_EQ(1, bash_pattern_match("c", "[a-z]", 0));
    EXPECT_EQ(1, bash_pattern_match("a", "[a-z]", 0));
    EXPECT_EQ(1, bash_pattern_match("z", "[a-z]", 0));
    EXPECT_EQ(0, bash_pattern_match("A", "[a-z]", 0));
    EXPECT_EQ(1, bash_pattern_match("5", "[0-9]", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "[0-9]", 0));
}

TEST(BashPattern, BracketLiteralDash) {
    // '-' at the start or end is literal
    EXPECT_EQ(1, bash_pattern_match("-", "[-abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("-", "[abc-]", 0));
}

TEST(BashPattern, BracketLiteralCloseBracket) {
    // ']' as first char after '[' (or '[!' ) is literal
    EXPECT_EQ(1, bash_pattern_match("]", "[]abc]", 0));
    EXPECT_EQ(1, bash_pattern_match("a", "[]abc]", 0));
}

TEST(BashPattern, BracketInPattern) {
    EXPECT_EQ(1, bash_pattern_match("file.c", "file.[ch]", 0));
    EXPECT_EQ(1, bash_pattern_match("file.h", "file.[ch]", 0));
    EXPECT_EQ(0, bash_pattern_match("file.o", "file.[ch]", 0));
    EXPECT_EQ(1, bash_pattern_match("test1", "test[0-9]", 0));
}

// ============================================================================
// POSIX character classes
// ============================================================================

TEST(BashPattern, PosixAlpha) {
    EXPECT_EQ(1, bash_pattern_match("a", "[[:alpha:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("Z", "[[:alpha:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("5", "[[:alpha:]]", 0));
    EXPECT_EQ(0, bash_pattern_match(" ", "[[:alpha:]]", 0));
}

TEST(BashPattern, PosixDigit) {
    EXPECT_EQ(1, bash_pattern_match("5", "[[:digit:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "[[:digit:]]", 0));
}

TEST(BashPattern, PosixAlnum) {
    EXPECT_EQ(1, bash_pattern_match("a", "[[:alnum:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("5", "[[:alnum:]]", 0));
    EXPECT_EQ(0, bash_pattern_match(" ", "[[:alnum:]]", 0));
}

TEST(BashPattern, PosixSpace) {
    EXPECT_EQ(1, bash_pattern_match(" ", "[[:space:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("\t", "[[:space:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "[[:space:]]", 0));
}

TEST(BashPattern, PosixUpper) {
    EXPECT_EQ(1, bash_pattern_match("A", "[[:upper:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("a", "[[:upper:]]", 0));
}

TEST(BashPattern, PosixLower) {
    EXPECT_EQ(1, bash_pattern_match("a", "[[:lower:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("A", "[[:lower:]]", 0));
}

TEST(BashPattern, PosixXdigit) {
    EXPECT_EQ(1, bash_pattern_match("f", "[[:xdigit:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("A", "[[:xdigit:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("9", "[[:xdigit:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("g", "[[:xdigit:]]", 0));
}

TEST(BashPattern, PosixBlank) {
    EXPECT_EQ(1, bash_pattern_match(" ", "[[:blank:]]", 0));
    EXPECT_EQ(1, bash_pattern_match("\t", "[[:blank:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("\n", "[[:blank:]]", 0));
}

TEST(BashPattern, PosixClassInPattern) {
    EXPECT_EQ(1, bash_pattern_match("abc123", "[[:alpha:]][[:alpha:]][[:alpha:]][[:digit:]][[:digit:]][[:digit:]]", 0));
    EXPECT_EQ(0, bash_pattern_match("ab1234", "[[:alpha:]][[:alpha:]][[:alpha:]][[:digit:]][[:digit:]][[:digit:]]", 0));
}

TEST(BashPattern, PosixClassMixed) {
    // mix POSIX class with range and literal
    EXPECT_EQ(1, bash_pattern_match("a", "[[:digit:]a-c]", 0));
    EXPECT_EQ(1, bash_pattern_match("5", "[[:digit:]a-c]", 0));
    EXPECT_EQ(0, bash_pattern_match("z", "[[:digit:]a-c]", 0));
}

// ============================================================================
// Case-insensitive matching (BASH_PAT_NOCASE)
// ============================================================================

TEST(BashPattern, NocaseMatch) {
    EXPECT_EQ(1, bash_pattern_match("Hello", "hello", BASH_PAT_NOCASE));
    EXPECT_EQ(1, bash_pattern_match("HELLO", "hello", BASH_PAT_NOCASE));
    EXPECT_EQ(1, bash_pattern_match("hello", "HELLO", BASH_PAT_NOCASE));
    EXPECT_EQ(0, bash_pattern_match("hello", "HELLO", 0));  // case-sensitive
}

TEST(BashPattern, NocaseBracket) {
    EXPECT_EQ(1, bash_pattern_match("A", "[a-z]", BASH_PAT_NOCASE));
    EXPECT_EQ(1, bash_pattern_match("Z", "[a-z]", BASH_PAT_NOCASE));
}

TEST(BashPattern, NocaseWildcard) {
    EXPECT_EQ(1, bash_pattern_match("Hello World", "hello*", BASH_PAT_NOCASE));
    EXPECT_EQ(1, bash_pattern_match("FOOBAR", "foo???", BASH_PAT_NOCASE));
}

// ============================================================================
// Extended globs: @(pat|pat)
// ============================================================================

TEST(BashPattern, ExtglobAt) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("cat", "@(cat|dog)", f));
    EXPECT_EQ(1, bash_pattern_match("dog", "@(cat|dog)", f));
    EXPECT_EQ(0, bash_pattern_match("fish", "@(cat|dog)", f));
    EXPECT_EQ(0, bash_pattern_match("cats", "@(cat|dog)", f));
    EXPECT_EQ(0, bash_pattern_match("", "@(cat|dog)", f));
}

TEST(BashPattern, ExtglobAtWithRest) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("catfish", "@(cat|dog)fish", f));
    EXPECT_EQ(1, bash_pattern_match("dogfish", "@(cat|dog)fish", f));
    EXPECT_EQ(0, bash_pattern_match("fishfish", "@(cat|dog)fish", f));
}

TEST(BashPattern, ExtglobAtWithGlob) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("cat123", "@(cat|dog)*", f));
    EXPECT_EQ(1, bash_pattern_match("dog", "@(cat|dog)*", f));
    EXPECT_EQ(0, bash_pattern_match("fish", "@(cat|dog)*", f));
}

TEST(BashPattern, ExtglobAtGlobAlternative) {
    int f = BASH_PAT_EXTGLOB;
    // alternatives can contain glob chars
    EXPECT_EQ(1, bash_pattern_match("foobar", "@(foo*|baz)", f));
    EXPECT_EQ(1, bash_pattern_match("baz", "@(foo*|baz)", f));
    EXPECT_EQ(0, bash_pattern_match("bar", "@(foo*|baz)", f));
}

// ============================================================================
// Extended globs: ?(pat|pat)
// ============================================================================

TEST(BashPattern, ExtglobQuest) {
    int f = BASH_PAT_EXTGLOB;
    // zero occurrences
    EXPECT_EQ(1, bash_pattern_match("", "?(a|b)", f));
    // one occurrence
    EXPECT_EQ(1, bash_pattern_match("a", "?(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("b", "?(a|b)", f));
    // two is not ok
    EXPECT_EQ(0, bash_pattern_match("ab", "?(a|b)", f));
    EXPECT_EQ(0, bash_pattern_match("aa", "?(a|b)", f));
}

TEST(BashPattern, ExtglobQuestWithRest) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("xyz", "?(abc)xyz", f));
    EXPECT_EQ(1, bash_pattern_match("abcxyz", "?(abc)xyz", f));
    EXPECT_EQ(0, bash_pattern_match("abcabcxyz", "?(abc)xyz", f));
}

// ============================================================================
// Extended globs: *(pat|pat)
// ============================================================================

TEST(BashPattern, ExtglobStar) {
    int f = BASH_PAT_EXTGLOB;
    // zero occurrences
    EXPECT_EQ(1, bash_pattern_match("", "*(a|b)", f));
    // one
    EXPECT_EQ(1, bash_pattern_match("a", "*(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("b", "*(a|b)", f));
    // multiple
    EXPECT_EQ(1, bash_pattern_match("aab", "*(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("ba", "*(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("aaa", "*(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("bbb", "*(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("ababab", "*(a|b)", f));
    // non-matching chars
    EXPECT_EQ(0, bash_pattern_match("abc", "*(a|b)", f));
    EXPECT_EQ(0, bash_pattern_match("c", "*(a|b)", f));
}

TEST(BashPattern, ExtglobStarWithRest) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("end", "*(ab)end", f));
    EXPECT_EQ(1, bash_pattern_match("abend", "*(ab)end", f));
    EXPECT_EQ(1, bash_pattern_match("ababend", "*(ab)end", f));
    EXPECT_EQ(0, bash_pattern_match("aend", "*(ab)end", f));
}

TEST(BashPattern, ExtglobStarMultiChar) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("foobarfoo", "*(foo|bar)", f));
    EXPECT_EQ(1, bash_pattern_match("foobarbar", "*(foo|bar)", f));
    EXPECT_EQ(0, bash_pattern_match("foobarbaz", "*(foo|bar)", f));
}

// ============================================================================
// Extended globs: +(pat|pat)
// ============================================================================

TEST(BashPattern, ExtglobPlus) {
    int f = BASH_PAT_EXTGLOB;
    // zero is NOT ok
    EXPECT_EQ(0, bash_pattern_match("", "+(a|b)", f));
    // one
    EXPECT_EQ(1, bash_pattern_match("a", "+(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("b", "+(a|b)", f));
    // multiple
    EXPECT_EQ(1, bash_pattern_match("aab", "+(a|b)", f));
    EXPECT_EQ(1, bash_pattern_match("aaa", "+(a|b)", f));
    EXPECT_EQ(0, bash_pattern_match("c", "+(a|b)", f));
}

TEST(BashPattern, ExtglobPlusWithRest) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(0, bash_pattern_match("end", "+(ab)end", f));
    EXPECT_EQ(1, bash_pattern_match("abend", "+(ab)end", f));
    EXPECT_EQ(1, bash_pattern_match("ababend", "+(ab)end", f));
}

// ============================================================================
// Extended globs: !(pat|pat)
// ============================================================================

TEST(BashPattern, ExtglobNot) {
    int f = BASH_PAT_EXTGLOB;
    // !(a) matches anything that is NOT exactly "a"
    EXPECT_EQ(0, bash_pattern_match("a", "!(a)", f));
    EXPECT_EQ(1, bash_pattern_match("b", "!(a)", f));
    EXPECT_EQ(1, bash_pattern_match("ab", "!(a)", f));
    EXPECT_EQ(1, bash_pattern_match("", "!(a)", f));
}

TEST(BashPattern, ExtglobNotMultiple) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(0, bash_pattern_match("cat", "!(cat|dog)", f));
    EXPECT_EQ(0, bash_pattern_match("dog", "!(cat|dog)", f));
    EXPECT_EQ(1, bash_pattern_match("fish", "!(cat|dog)", f));
    EXPECT_EQ(1, bash_pattern_match("", "!(cat|dog)", f));
}

TEST(BashPattern, ExtglobNotWithRest) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("fish.txt", "!(cat|dog).txt", f));
    EXPECT_EQ(0, bash_pattern_match("cat.txt", "!(cat|dog).txt", f));
    EXPECT_EQ(0, bash_pattern_match("dog.txt", "!(cat|dog).txt", f));
}

// ============================================================================
// Nested extglobs
// ============================================================================

TEST(BashPattern, ExtglobNested) {
    int f = BASH_PAT_EXTGLOB;
    // @(@(a|b)|c) should match "a", "b", "c" and nothing else
    EXPECT_EQ(1, bash_pattern_match("a", "@(@(a|b)|c)", f));
    EXPECT_EQ(1, bash_pattern_match("b", "@(@(a|b)|c)", f));
    EXPECT_EQ(1, bash_pattern_match("c", "@(@(a|b)|c)", f));
    EXPECT_EQ(0, bash_pattern_match("d", "@(@(a|b)|c)", f));
}

TEST(BashPattern, ExtglobNestedStar) {
    int f = BASH_PAT_EXTGLOB;
    // *(a|+(b)) matches "", "a", "b", "bb", "abba", etc.
    EXPECT_EQ(1, bash_pattern_match("", "*(a|+(b))", f));
    EXPECT_EQ(1, bash_pattern_match("a", "*(a|+(b))", f));
    EXPECT_EQ(1, bash_pattern_match("b", "*(a|+(b))", f));
    EXPECT_EQ(1, bash_pattern_match("bb", "*(a|+(b))", f));
    EXPECT_EQ(1, bash_pattern_match("abba", "*(a|+(b))", f));
    EXPECT_EQ(0, bash_pattern_match("c", "*(a|+(b))", f));
}

// ============================================================================
// Extglob disabled (should not match extglob patterns)
// ============================================================================

TEST(BashPattern, ExtglobDisabled) {
    // without BASH_PAT_EXTGLOB, @( should be treated as literal @ and (
    EXPECT_EQ(0, bash_pattern_match("cat", "@(cat|dog)", 0));
    // but the pattern could still match something weird via literal chars
    EXPECT_EQ(1, bash_pattern_match("@(cat|dog)", "@(cat|dog)", 0));
}

// ============================================================================
// bash_bracket_match standalone
// ============================================================================

TEST(BashPattern, BracketMatchStandalone) {
    EXPECT_EQ(1, bash_bracket_match('a', "abc", 0));
    EXPECT_EQ(1, bash_bracket_match('b', "abc", 0));
    EXPECT_EQ(0, bash_bracket_match('d', "abc", 0));
    EXPECT_EQ(1, bash_bracket_match('5', "0-9", 0));
    EXPECT_EQ(1, bash_bracket_match('m', "[:alpha:]", 0));
}

// ============================================================================
// Edge cases & regression
// ============================================================================

TEST(BashPattern, NullInputs) {
    EXPECT_EQ(0, bash_pattern_match(NULL, "a", 0));
    EXPECT_EQ(0, bash_pattern_match("a", NULL, 0));
    EXPECT_EQ(0, bash_pattern_match(NULL, NULL, 0));
}

TEST(BashPattern, EmptyPatternEmptyString) {
    EXPECT_EQ(1, bash_pattern_match("", "", 0));
    EXPECT_EQ(1, bash_pattern_match("", "*", 0));
    EXPECT_EQ(0, bash_pattern_match("", "?", 0));
}

TEST(BashPattern, TrailingBackslash) {
    // trailing backslash is an error — should not match
    EXPECT_EQ(0, bash_pattern_match("a", "a\\", 0));
}

TEST(BashPattern, LongAlternation) {
    int f = BASH_PAT_EXTGLOB;
    EXPECT_EQ(1, bash_pattern_match("delta",
        "@(alpha|beta|gamma|delta|epsilon|zeta|eta|theta)", f));
    EXPECT_EQ(0, bash_pattern_match("omega",
        "@(alpha|beta|gamma|delta|epsilon|zeta|eta|theta)", f));
}

TEST(BashPattern, PatternWithDotsAndSlashes) {
    EXPECT_EQ(1, bash_pattern_match("file.tar.gz", "*.tar.gz", 0));
    EXPECT_EQ(1, bash_pattern_match("path/to/file", "path/*/file", 0));
    // without PATHNAME flag, * matches / too
    EXPECT_EQ(1, bash_pattern_match("path/to/sub/file", "path/*/file", 0));
    // with PATHNAME flag, * does NOT match /
    EXPECT_EQ(0, bash_pattern_match("path/to/sub/file", "path/*/file", BASH_PAT_PATHNAME));
}

TEST(BashPattern, CasePatternFromBashTests) {
    int f = BASH_PAT_EXTGLOB;
    // common case statement patterns
    EXPECT_EQ(1, bash_pattern_match("yes", "@(yes|no)", f));
    EXPECT_EQ(1, bash_pattern_match("no", "@(yes|no)", f));
    EXPECT_EQ(0, bash_pattern_match("maybe", "@(yes|no)", f));

    // file extension matching
    EXPECT_EQ(1, bash_pattern_match("test.c", "*.@(c|h|cpp|hpp)", f));
    EXPECT_EQ(1, bash_pattern_match("test.hpp", "*.@(c|h|cpp|hpp)", f));
    EXPECT_EQ(0, bash_pattern_match("test.py", "*.@(c|h|cpp|hpp)", f));
}

TEST(BashPattern, ParameterExpansionPatterns) {
    // patterns used in ${var/pattern/replacement}
    EXPECT_EQ(1, bash_pattern_match("hello world", "hello*", 0));
    EXPECT_EQ(1, bash_pattern_match("/usr/local/bin", "/usr/*", 0));

    // ${var#pattern} - shortest prefix
    EXPECT_EQ(1, bash_pattern_match("/path/to/file", "/*", 0));

    // ${var%%pattern} - longest suffix
    EXPECT_EQ(1, bash_pattern_match("file.tar.gz", "*.*", 0));
}

TEST(BashPattern, ExtglobNotWithGlob) {
    int f = BASH_PAT_EXTGLOB;
    // !(*.o) should not match .o files
    EXPECT_EQ(0, bash_pattern_match("file.o", "!(*.o)", f));
    EXPECT_EQ(1, bash_pattern_match("file.c", "!(*.o)", f));
    EXPECT_EQ(1, bash_pattern_match("file.h", "!(*.o)", f));
}

TEST(BashPattern, ExtglobStarSingleChar) {
    int f = BASH_PAT_EXTGLOB;
    // *(?) is equivalent to * — matches anything
    EXPECT_EQ(1, bash_pattern_match("abc", "*(?)", f));
    EXPECT_EQ(1, bash_pattern_match("", "*(?)", f));
    EXPECT_EQ(1, bash_pattern_match("x", "*(?)", f));
}
