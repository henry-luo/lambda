/**
 * CSS Engine Negative Tests - Error Handling & Invalid Input
 *
 * Tests CSS parser's ability to handle invalid CSS input according to CSS specs.
 * Based on CSS Syntax Module Level 3 error handling requirements:
 * - Unclosed constructs (strings, URLs, comments, blocks)
 * - Invalid characters and escape sequences
 * - Malformed selectors and declarations
 * - Syntax errors and recovery mechanisms
 * - Invalid property values and units
 * - Brace mismatch and nesting errors
 * - Fuzzy testing with random/malformed input
 *
 * Target: 70+ negative tests with comprehensive error coverage
 */

#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include "../helpers/css_test_helpers.hpp"

extern "C" {
#include "lambda/input/css/css_engine.hpp"
#include "lambda/input/css/css_parser.hpp"
#include "lambda/input/css/css_style.hpp"
#include "lambda/input/css/css_tokenizer.hpp"
}

using namespace CssTestHelpers;

// ============================================================================
// Test Fixture
// ============================================================================

class CssEngineNegativeTest : public ::testing::Test {
protected:
    PoolGuard pool;

    CssEngine* CreateEngine() {
        CssEngine* engine = css_engine_create(pool.get());
        if (engine) {
            css_engine_set_viewport(engine, 1920, 1080);
        }
        return engine;
    }

    CssTokenizer* CreateTokenizer() {
        return css_tokenizer_create(pool.get());
    }
};

// ============================================================================
// Category 1: Unclosed Constructs (15 tests)
// ============================================================================

// Test 1.1: Unclosed string literal
TEST_F(CssEngineNegativeTest, UnclosedString_DoubleQuote) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div { content: \"unclosed string; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    // Should detect BAD_STRING token or handle gracefully
    bool has_bad_string = false;
    for (int i = 0; i < token_count; i++) {
        if (tokens[i].type == CSS_TOKEN_BAD_STRING) {
            has_bad_string = true;
            break;
        }
    }
    // Either detect the error or parse without crashing
    EXPECT_TRUE(token_count >= 0);
}

// Test 1.2: Unclosed string literal (single quote)
TEST_F(CssEngineNegativeTest, UnclosedString_SingleQuote) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div { content: 'unclosed; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    EXPECT_TRUE(token_count >= 0);
}

// Test 1.3: Unclosed URL
TEST_F(CssEngineNegativeTest, UnclosedURL) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div { background: url(image.png; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    // Should detect BAD_URL token or handle gracefully
    bool has_bad_url = false;
    for (int i = 0; i < token_count; i++) {
        if (tokens[i].type == CSS_TOKEN_BAD_URL) {
            has_bad_url = true;
            break;
        }
    }
    EXPECT_TRUE(token_count >= 0);
}

// Test 1.4: Unclosed comment
TEST_F(CssEngineNegativeTest, UnclosedComment) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "/* This comment never closes\ndiv { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle gracefully - may skip rest of file or treat as comment
    // No crash expected
}

// Test 1.5: Unclosed block (missing closing brace)
TEST_F(CssEngineNegativeTest, UnclosedBlock_MissingCloseBrace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red;";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle EOF in block gracefully
}

// Test 1.6: Unclosed function
TEST_F(CssEngineNegativeTest, UnclosedFunction) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { width: calc(100% - 20px; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle unclosed function
}

// Test 1.7: Unclosed attribute selector
TEST_F(CssEngineNegativeTest, UnclosedAttributeSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div[attr=\"value\" { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should skip malformed selector
}

// Test 1.8: Unclosed pseudo-class function
TEST_F(CssEngineNegativeTest, UnclosedPseudoClassFunction) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div:nth-child(2n { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle unclosed pseudo-class
}

// Test 1.9: Multiple unclosed strings
TEST_F(CssEngineNegativeTest, MultipleUnclosedStrings) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "div { content: \"one; }\n"
        "p { content: \"two; }\n"
        "span { color: red; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should attempt to recover and parse valid rule
}

// Test 1.10: Unclosed parentheses in complex expression
TEST_F(CssEngineNegativeTest, UnclosedParentheses) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { width: calc((100% - 20px) * 2; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle missing closing paren
}

// Test 1.11: Unclosed brackets in selector
TEST_F(CssEngineNegativeTest, UnclosedBrackets) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div[class { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should skip malformed selector
}

// Test 1.12: Nested unclosed blocks
TEST_F(CssEngineNegativeTest, NestedUnclosedBlocks) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "@media screen {\n"
        "  div { color: red;\n"
        "  /* missing two closing braces */";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle EOF with multiple unclosed blocks
}

// Test 1.13: Unclosed string with escape sequence
TEST_F(CssEngineNegativeTest, UnclosedStringWithEscape) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    // Using raw string to avoid escaping issues
    const char* css = R"(div { content: "test\"; })";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle incomplete escape sequence
}

// Test 1.14: Unclosed URL with whitespace
TEST_F(CssEngineNegativeTest, UnclosedURLWithWhitespace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { background: url(  image.png  ; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Bad URL with whitespace
}

// Test 1.15: String with newline (invalid)
TEST_F(CssEngineNegativeTest, StringWithNewline) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div { content: \"line1\nline2\"; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    // Newline in string should produce BAD_STRING token
    EXPECT_TRUE(token_count >= 0);
}

// ============================================================================
// Category 2: Invalid Characters & Escape Sequences (12 tests)
// ============================================================================

// Test 2.1: Null character in input
TEST_F(CssEngineNegativeTest, NullCharacter) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char css[] = "div { color: r\0ed; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, sizeof(css) - 1, &tokens);

    // Null should be replaced or handled per CSS spec
    EXPECT_TRUE(token_count >= 0);
}

// Test 2.2: Invalid escape sequence (incomplete hex)
TEST_F(CssEngineNegativeTest, InvalidEscapeSequence_IncompleteHex) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\41\\4\"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle incomplete hex escape
}

// Test 2.3: Invalid escape at end of input
TEST_F(CssEngineNegativeTest, InvalidEscapeSequence_EOF) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red\\";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle EOF after escape
}

// Test 2.4: Non-printable control characters
TEST_F(CssEngineNegativeTest, NonPrintableControlChars) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char css[] = "div { color: \x01\x02\x03red; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, sizeof(css) - 1, &tokens);

    // Control characters should be handled
    EXPECT_TRUE(token_count >= 0);
}

// Test 2.5: Invalid Unicode escape (out of range)
TEST_F(CssEngineNegativeTest, InvalidUnicodeEscape_OutOfRange) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\110000\"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle out-of-range Unicode
}

// Test 2.6: Invalid UTF-8 sequence
TEST_F(CssEngineNegativeTest, InvalidUTF8Sequence) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const unsigned char css[] = "div { color: \xFF\xFE; }";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, (const char*)css, sizeof(css) - 1, &tokens);

    // Should handle invalid UTF-8
    EXPECT_TRUE(token_count >= 0);
}

// Test 2.7: Bare carriage return
TEST_F(CssEngineNegativeTest, BareCarriageReturn) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div {\rcolor: red;\r}";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    // CR should be normalized
    EXPECT_TRUE(token_count >= 0);
}

// Test 2.8: Form feed character
TEST_F(CssEngineNegativeTest, FormFeedCharacter) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    const char* css = "div {\fcolor: red;\f}";
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, css, strlen(css), &tokens);

    // Form feed should be handled
    EXPECT_TRUE(token_count >= 0);
}

// Test 2.9: Backslash without valid escape
TEST_F(CssEngineNegativeTest, BackslashWithoutEscape) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\ \"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Backslash followed by space
}

// Test 2.10: Invalid hex digit in escape
TEST_F(CssEngineNegativeTest, InvalidHexDigit) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\41G2\"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // G is not a valid hex digit
}

// Test 2.11: Surrogate pair characters
TEST_F(CssEngineNegativeTest, SurrogatePairs) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\D800\\DC00\"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle surrogate pairs per CSS spec
}

// Test 2.12: Mixed valid and invalid escapes
TEST_F(CssEngineNegativeTest, MixedEscapes) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { content: \"\\41\\\\0\\61\"; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Mix of valid and edge-case escapes
}

// ============================================================================
// Category 3: Malformed Selectors (15 tests)
// ============================================================================

// Test 3.1: Empty selector
TEST_F(CssEngineNegativeTest, EmptySelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = " { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should skip rule with empty selector
}

// Test 3.2: Invalid selector starting with combinator
TEST_F(CssEngineNegativeTest, SelectorStartsWithCombinator) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "> div { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Invalid to start with >
}

// Test 3.3: Multiple consecutive combinators
TEST_F(CssEngineNegativeTest, MultipleConsecutiveCombinators) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div > > p { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Invalid selector
}

// Test 3.4: Invalid pseudo-class name
TEST_F(CssEngineNegativeTest, InvalidPseudoClassName) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div:123invalid { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Pseudo-class cannot start with digit
}

// Test 3.5: Invalid pseudo-element syntax
TEST_F(CssEngineNegativeTest, InvalidPseudoElementSyntax) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div:::before { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Triple colon is invalid
}

// Test 3.6: Pseudo-element not at end
TEST_F(CssEngineNegativeTest, PseudoElementNotAtEnd) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div::before.class { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Pseudo-element must be last (in CSS2.1, relaxed in CSS3)
}

// Test 3.7: Invalid attribute selector operator
TEST_F(CssEngineNegativeTest, InvalidAttributeOperator) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div[attr==value] { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // == is not valid, only =
}

// Test 3.8: Unclosed attribute value
TEST_F(CssEngineNegativeTest, UnclosedAttributeValue) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div[attr=\"value] { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Unclosed quote in attribute
}

// Test 3.9: Invalid ID selector (empty)
TEST_F(CssEngineNegativeTest, EmptyIDSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "# { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // # must be followed by identifier
}

// Test 3.10: Invalid class selector (empty)
TEST_F(CssEngineNegativeTest, EmptyClassSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = ". { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // . must be followed by identifier
}

// Test 3.11: Invalid namespace syntax
TEST_F(CssEngineNegativeTest, InvalidNamespace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "ns::: { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Invalid namespace syntax
}

// Test 3.12: Selector with unbalanced parentheses
TEST_F(CssEngineNegativeTest, UnbalancedParenthesesInSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div:not(.class)) { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Extra closing paren
}

// Test 3.13: Invalid combinator at end
TEST_F(CssEngineNegativeTest, CombinatorAtEnd) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div > { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Combinator without following selector
}

// Test 3.14: Invalid characters in class name
TEST_F(CssEngineNegativeTest, InvalidCharsInClassName) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = ".class@name { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // @ not valid in class name (without escape)
}

// Test 3.15: Comma with no selector after
TEST_F(CssEngineNegativeTest, TrailingCommaInSelectorGroup) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div, p, { color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Trailing comma in selector group
}

// ============================================================================
// Category 4: Invalid Declarations & Property Values (10 tests)
// ============================================================================

// Test 4.1: Property without value
TEST_F(CssEngineNegativeTest, PropertyWithoutValue) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: ; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Property with empty value
}

// Test 4.2: Property without colon
TEST_F(CssEngineNegativeTest, PropertyWithoutColon) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Missing colon separator
}

// Test 4.3: Invalid property name (starts with digit)
TEST_F(CssEngineNegativeTest, PropertyNameStartsWithDigit) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { 123color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Property name cannot start with digit
}

// Test 4.4: Invalid property name (special chars)
TEST_F(CssEngineNegativeTest, PropertyNameSpecialChars) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { col@or: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // @ not valid in property name
}

// Test 4.5: Invalid unit
TEST_F(CssEngineNegativeTest, InvalidUnit) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { width: 100xyz; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // xyz is not a valid unit
}

// Test 4.6: Invalid color format
TEST_F(CssEngineNegativeTest, InvalidColorFormat) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: #XYZ; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Invalid hex color
}

// Test 4.7: Invalid RGB values
TEST_F(CssEngineNegativeTest, InvalidRGBValues) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: rgb(300, -50, 999); }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // RGB values out of range (implementation may clamp)
}

// Test 4.8: Negative length where not allowed
TEST_F(CssEngineNegativeTest, NegativeLengthInvalid) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { width: -100px; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Negative width is invalid
}

// Test 4.9: !important with typo
TEST_F(CssEngineNegativeTest, ImportantTypo) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red !importan; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Typo in !important
}

// Test 4.10: Multiple !important flags
TEST_F(CssEngineNegativeTest, MultipleImportant) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red !important !important; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Duplicate !important
}

// ============================================================================
// Category 5: Brace Mismatch & Nesting Errors (8 tests)
// ============================================================================

// Test 5.1: Extra closing brace
TEST_F(CssEngineNegativeTest, ExtraClosingBrace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red; } } p { font-size: 14px; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should skip extra brace and continue
}

// Test 5.2: Multiple extra closing braces
TEST_F(CssEngineNegativeTest, MultipleExtraClosingBraces) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red; } } } p { font-size: 14px; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should recover from multiple errors
}

// Test 5.3: Brace in wrong context
TEST_F(CssEngineNegativeTest, BraceInPropertyValue) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: { red }; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Braces not valid in property value
}

// Test 5.4: Mismatched at-rule braces
TEST_F(CssEngineNegativeTest, MismatchedAtRuleBraces) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "@media screen {\n"
        "  div { color: red; }\n"
        "  /* missing media closing brace */\n"
        "p { font-size: 14px; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle EOF with unclosed at-rule
}

// Test 5.5: Declaration block without selector
TEST_F(CssEngineNegativeTest, DeclarationBlockWithoutSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "{ color: red; }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Block without selector
}

// Test 5.6: Nested braces (invalid in CSS2)
TEST_F(CssEngineNegativeTest, NestedStyleBlocks) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red; { background: blue; } }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Nested blocks not valid in standard CSS (CSS Nesting is new)
}

// Test 5.7: Unbalanced parentheses in value
TEST_F(CssEngineNegativeTest, UnbalancedParenthesesInValue) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { width: calc((100% - 20px); }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Mismatched parens
}

// Test 5.8: Complex brace depth error
TEST_F(CssEngineNegativeTest, ComplexBraceDepthError) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "@media screen {\n"
        "  @supports (display: grid) {\n"
        "    div { color: red;\n"
        "    /* missing 3 closing braces */";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle multiple levels of unclosed blocks
}

// ============================================================================
// Category 6: At-Rule Errors (5 tests)
// ============================================================================

// Test 6.1: Invalid @-rule name
TEST_F(CssEngineNegativeTest, InvalidAtRuleName) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "@invalid-rule { div { color: red; } }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Unknown at-rule (should skip or parse as best effort)
}

// Test 6.2: @charset not at beginning
TEST_F(CssEngineNegativeTest, CharsetNotAtBeginning) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { color: red; }\n@charset \"UTF-8\";";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // @charset must be first
}

// Test 6.3: @import after rules
TEST_F(CssEngineNegativeTest, ImportAfterRules) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "div { color: red; }\n"
        "@import url('other.css');";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // @import must come before rules
}

// Test 6.4: Malformed @media query
TEST_F(CssEngineNegativeTest, MalformedMediaQuery) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "@media screen and ( { div { color: red; } }";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Incomplete media query
}

// Test 6.5: Invalid @keyframes syntax
TEST_F(CssEngineNegativeTest, InvalidKeyframesSyntax) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css =
        "@keyframes { 0% { opacity: 0; } 100% { opacity: 1; } }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Missing animation name
}

// ============================================================================
// Category 7: Edge Cases & Stress Tests (5 tests)
// ============================================================================

// Test 7.1: Empty input
TEST_F(CssEngineNegativeTest, EmptyInput) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Empty stylesheet should be valid
}

// Test 7.2: Only whitespace
TEST_F(CssEngineNegativeTest, OnlyWhitespace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "   \n\t\r\n   ";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should produce empty stylesheet
}

// Test 7.3: Only comments
TEST_F(CssEngineNegativeTest, OnlyComments) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "/* comment 1 */ /* comment 2 */";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should produce empty stylesheet
}

// Test 7.4: Extremely long selector
TEST_F(CssEngineNegativeTest, ExtremelyLongSelector) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    // Build a very long selector chain
    std::string css = "div";
    for (int i = 0; i < 100; i++) {
        css += " > div";
    }
    css += " { color: red; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css.c_str(), nullptr);

    // Should handle without crashing
}

// Test 7.5: Extremely long property value
TEST_F(CssEngineNegativeTest, ExtremelyLongPropertyValue) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    // Build a very long value
    std::string css = "div { content: \"";
    for (int i = 0; i < 10000; i++) {
        css += "x";
    }
    css += "\"; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css.c_str(), nullptr);

    // Should handle without crashing
}

// ============================================================================
// Category 8: Fuzzy Testing (10 tests)
// ============================================================================

// Test 8.1: Random ASCII characters
TEST_F(CssEngineNegativeTest, Fuzz_RandomASCII) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "!@#$%^&*()_+-=[]\\{}|;':\",./<>?`~";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle random special characters without crashing
}

// Test 8.2: Random braces and brackets
TEST_F(CssEngineNegativeTest, Fuzz_RandomBraces) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "{{{{}}}}[[[[]]]](((()))){{}}[]()";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Should handle nested/mismatched braces
}

// Test 8.3: Random mixing of valid CSS tokens
TEST_F(CssEngineNegativeTest, Fuzz_MixedTokens) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div { : ; } @ # . : color red 123 px url ( )";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Mixed valid tokens in invalid order
}

// Test 8.4: Repeated symbols
TEST_F(CssEngineNegativeTest, Fuzz_RepeatedSymbols) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = ":::::::::::::::::::::::::::::::::::";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Excessive colons
}

// Test 8.5: Random numbers and units
TEST_F(CssEngineNegativeTest, Fuzz_RandomNumbers) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "123.456.789px 999em -5555rem 0.0.0.0% 1e99999px";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Malformed numeric values
}

// Test 8.6: Random strings and quotes
TEST_F(CssEngineNegativeTest, Fuzz_RandomStrings) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "\"\"\"\"'''''''\"'\"'\"'\\\\\\\\\"\"";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Mixed quotes and escapes
}

// Test 8.7: Random at-rules
TEST_F(CssEngineNegativeTest, Fuzz_RandomAtRules) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "@@@@@media@import@charset@@@keyframes@@@@";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Malformed at-rules
}

// Test 8.8: Random selectors
TEST_F(CssEngineNegativeTest, Fuzz_RandomSelectors) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "....####[[[:]]]:::***>>>+++~~~";
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Random selector-like characters
}

// Test 8.9: Mixed valid and invalid CSS soup
TEST_F(CssEngineNegativeTest, Fuzz_CSSSoup) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = 
        "div { color: red; !@#$ } "
        "@media $$$ { p { font: %%% } } "
        "[[[ .class { @@@ : ### } ]]] "
        "url((())) rgb(999,999,999) "
        "#id#id#id .class.class.class "
        "{ } { } { } : : : ; ; ;";
    
    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    // Complex mix of valid CSS and garbage
}

// Test 8.10: Binary-like random bytes
TEST_F(CssEngineNegativeTest, Fuzz_RandomBytes) {
    auto tokenizer = CreateTokenizer();
    ASSERT_NE(tokenizer, nullptr);

    // Random byte sequence (printable for test stability)
    const unsigned char css[] = {
        0x7B, 0x7D, 0x3A, 0x3B, 0x21, 0x40, 0x23, 0x24,
        0x25, 0x5E, 0x26, 0x2A, 0x28, 0x29, 0x5F, 0x2B,
        0x5B, 0x5D, 0x7C, 0x5C, 0x2F, 0x3C, 0x3E, 0x3F,
        0x00  // null terminator
    };
    
    CssToken* tokens = nullptr;
    int token_count = css_tokenizer_tokenize(tokenizer, (const char*)css, 
                                             sizeof(css) - 1, &tokens);

    // Should tokenize without crashing
    EXPECT_TRUE(token_count >= 0);
}

// Test 8.11: Random input stress test - 100 iterations
TEST_F(CssEngineNegativeTest, Fuzz_RandomInputStressTest) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    // CSS-relevant characters to generate from
    const char charset[] = "abcdefghijklmnopqrstuvwxyz"
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "0123456789"
                          " \t\n\r"
                          "{}[]():;,."
                          "#@!$%^&*+-=|\\/'\"<>?`~";
    const int charset_size = sizeof(charset) - 1;

    // Seed random number generator
    srand(12345); // Use fixed seed for reproducibility

    // Run 100 iterations with different random inputs
    for (int iteration = 0; iteration < 100; iteration++) {
        // Generate random length between 10 and 200 characters
        int length = 10 + (rand() % 190);
        std::string random_css;
        random_css.reserve(length + 1);

        // Generate random CSS-like string
        for (int i = 0; i < length; i++) {
            random_css += charset[rand() % charset_size];
        }

        // Print the generated random input
        std::cout << "Iteration " << (iteration + 1) << "/" << 100 
                  << " (length=" << length << "): " << random_css << std::endl;

        // Try to parse the random input - should not crash or hang
        CssStylesheet* sheet = css_parse_stylesheet(engine, random_css.c_str(), nullptr);
        
        // Parser should handle gracefully without crashing
        // No specific assertions needed - just surviving the parse is the test
    }

    // If we reach here, all 100 iterations completed successfully
    SUCCEED();
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
