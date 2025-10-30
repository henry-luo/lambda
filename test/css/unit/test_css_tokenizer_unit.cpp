/**
 * CSS Tokenizer Unit Tests (Comprehensive)
 *
 * Purpose: Thorough testing of CSS tokenization at the lowest level
 *
 * Coverage:
 * - Basic token types (identifiers, numbers, strings, etc.)
 * - CSS3+ features (custom properties, color functions)
 * - Unicode support and escape sequences
 * - Edge cases and error recovery
 * - Critical bug regressions
 *
 * Test Categories:
 * 1. Basic Token Types - Standard CSS tokens
 * 2. Numeric Tokenization - Numbers, dimensions, percentages
 * 3. String and URL Tokenization - Quoted strings and URLs
 * 4. Function Tokenization - CSS functions
 * 5. Unicode and Escapes - UTF-8 and escape sequences
 * 6. Edge Cases - Empty input, large input, malformed CSS
 * 7. Regression Tests - Previously identified bugs
 *
 * Related Files:
 * - lambda/input/css/css_tokenizer.c
 * - lambda/input/css/css_parser.h
 */

#include <gtest/gtest.h>
#include "../helpers/css_test_helpers.hpp"

using namespace CssTestHelpers;

// =============================================================================
// Test Fixture
// =============================================================================

class CssTokenizerUnitTest : public ::testing::Test {
protected:
    PoolGuard pool;

    // Helper: Tokenize CSS and return tokens
    Tokenizer Tokenize(const char* css) {
        return Tokenizer(pool.get(), css);
    }
};

// =============================================================================
// Category 1: Basic Token Types
// =============================================================================

TEST_F(CssTokenizerUnitTest, Identifier_SimpleASCII) {
    auto tokens = Tokenize("div");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_IDENT);
    EXPECT_STREQ(tokens[0]->value, "div");
}

TEST_F(CssTokenizerUnitTest, Identifier_WithHyphen) {
    auto tokens = Tokenize("custom-element");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_IDENT);
    EXPECT_STREQ(tokens[0]->value, "custom-element");
}

TEST_F(CssTokenizerUnitTest, Identifier_WithUnderscore) {
    auto tokens = Tokenize("_private");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_IDENT);
    EXPECT_STREQ(tokens[0]->value, "_private");
}

TEST_F(CssTokenizerUnitTest, HashToken_IDSelector) {
    auto tokens = Tokenize("#header");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_HASH);
    // Note: Hash token value may or may not include the '#'
    ASSERT_NE(tokens[0]->value, nullptr);
}

TEST_F(CssTokenizerUnitTest, HashToken_HexColor) {
    auto tokens = Tokenize("#ff0000");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_HASH);
}

TEST_F(CssTokenizerUnitTest, Delimiter_Comma) {
    auto tokens = Tokenize(",");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_COMMA);
}

TEST_F(CssTokenizerUnitTest, Delimiter_Colon) {
    auto tokens = Tokenize(":");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_COLON);
}

TEST_F(CssTokenizerUnitTest, Delimiter_Semicolon) {
    auto tokens = Tokenize(";");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_SEMICOLON);
}

TEST_F(CssTokenizerUnitTest, Braces_Left) {
    auto tokens = Tokenize("{");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_LEFT_BRACE);
}

TEST_F(CssTokenizerUnitTest, Braces_Right) {
    auto tokens = Tokenize("}");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_RIGHT_BRACE);
}

TEST_F(CssTokenizerUnitTest, Parentheses_Left) {
    auto tokens = Tokenize("(");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_LEFT_PAREN);
}

TEST_F(CssTokenizerUnitTest, Parentheses_Right) {
    auto tokens = Tokenize(")");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_RIGHT_PAREN);
}

TEST_F(CssTokenizerUnitTest, Brackets_Left) {
    auto tokens = Tokenize("[");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_LEFT_BRACKET);
}

TEST_F(CssTokenizerUnitTest, Brackets_Right) {
    auto tokens = Tokenize("]");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_RIGHT_BRACKET);
}

// =============================================================================
// Category 2: Numeric Tokenization (CRITICAL)
// =============================================================================

TEST_F(CssTokenizerUnitTest, Number_Integer) {
    auto tokens = Tokenize("42");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_NUMBER);
    EXPECT_DOUBLE_EQ(tokens[0]->data.number_value, 42.0);
}

TEST_F(CssTokenizerUnitTest, Number_Decimal) {
    auto tokens = Tokenize("3.14");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_NUMBER);
    EXPECT_DOUBLE_EQ(tokens[0]->data.number_value, 3.14);
}

TEST_F(CssTokenizerUnitTest, Number_LeadingDecimalPoint) {
    auto tokens = Tokenize(".5");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_NUMBER);
    EXPECT_DOUBLE_EQ(tokens[0]->data.number_value, 0.5);
}

TEST_F(CssTokenizerUnitTest, Number_NegativeInteger) {
    auto tokens = Tokenize("-10");

    ASSERT_GE(tokens.count(), 1);
    // May be DELIM(-) + NUMBER(10) or NUMBER(-10)
    bool found_number = false;
    for (size_t i = 0; i < tokens.count(); i++) {
        if (tokens[i]->type == CSS_TOKEN_NUMBER) {
            found_number = true;
            break;
        }
    }
    EXPECT_TRUE(found_number);
}

TEST_F(CssTokenizerUnitTest, Number_PositiveWithSign) {
    auto tokens = Tokenize("+5");

    ASSERT_GE(tokens.count(), 1);
    bool found_number = false;
    for (size_t i = 0; i < tokens.count(); i++) {
        if (tokens[i]->type == CSS_TOKEN_NUMBER) {
            found_number = true;
            break;
        }
    }
    EXPECT_TRUE(found_number);
}

// CRITICAL TEST: Distinguish between .5 (number) and .container (class)
TEST_F(CssTokenizerUnitTest, REGRESSION_DotFollowedByDigit_IsNumber) {
    auto tokens = Tokenize(".5");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_NUMBER);
    EXPECT_DOUBLE_EQ(tokens[0]->data.number_value, 0.5);
}

// CRITICAL TEST: Class selector must tokenize as DELIM + IDENT
TEST_F(CssTokenizerUnitTest, REGRESSION_DotFollowedByLetter_IsDelimAndIdent) {
    auto tokens = Tokenize(".container");

    ASSERT_GE(tokens.count(), 2) << "Class selector must be DELIM + IDENT";
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_DELIM);
    TokenAssertions::AssertDelimiter(tokens[0], '.');
    ASSERT_CSS_TOKEN_TYPE(tokens[1], CSS_TOKEN_IDENT);
    EXPECT_STREQ(tokens[1]->value, "container");
}

TEST_F(CssTokenizerUnitTest, Dimension_Pixels) {
    auto tokens = Tokenize("10px");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_DIMENSION);
}

TEST_F(CssTokenizerUnitTest, Dimension_Em) {
    auto tokens = Tokenize("2em");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_DIMENSION);
}

TEST_F(CssTokenizerUnitTest, Dimension_Rem) {
    auto tokens = Tokenize("1.5rem");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_DIMENSION);
}

TEST_F(CssTokenizerUnitTest, Percentage_Simple) {
    auto tokens = Tokenize("50%");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_PERCENTAGE);
}

TEST_F(CssTokenizerUnitTest, Percentage_Decimal) {
    auto tokens = Tokenize("33.33%");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_PERCENTAGE);
}

// =============================================================================
// Category 3: String and URL Tokenization
// =============================================================================

TEST_F(CssTokenizerUnitTest, String_DoubleQuoted) {
    auto tokens = Tokenize("\"hello world\"");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_STRING);
    ASSERT_NE(tokens[0]->value, nullptr);
    // String tokens are stored without quotes in the value
    EXPECT_STREQ(tokens[0]->value, "hello world");
}

TEST_F(CssTokenizerUnitTest, String_SingleQuoted) {
    auto tokens = Tokenize("'hello world'");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_STRING);
    ASSERT_NE(tokens[0]->value, nullptr);
    // String tokens are stored without quotes in the value
    EXPECT_STREQ(tokens[0]->value, "hello world");
}

TEST_F(CssTokenizerUnitTest, String_Empty) {
    auto tokens = Tokenize("\"\"");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_STRING);
}

TEST_F(CssTokenizerUnitTest, String_WithEscapedQuote) {
    auto tokens = Tokenize("\"hello \\\" world\"");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_STRING);
}

TEST_F(CssTokenizerUnitTest, String_Unterminated_ErrorRecovery) {
    auto tokens = Tokenize("\"unterminated");

    // Should produce BAD_STRING token or recover gracefully
    ASSERT_GT(tokens.count(), 0);
    bool has_bad_string = false;
    for (size_t i = 0; i < tokens.count(); i++) {
        if (tokens[i]->type == CSS_TOKEN_BAD_STRING ||
            tokens[i]->type == CSS_TOKEN_STRING) {
            has_bad_string = true;
            break;
        }
    }
    EXPECT_TRUE(has_bad_string);
}

TEST_F(CssTokenizerUnitTest, URL_Simple) {
    auto tokens = Tokenize("url(image.png)");

    ASSERT_GE(tokens.count(), 1);
    // May be URL token or FUNCTION token
    bool found_url = false;
    for (size_t i = 0; i < tokens.count(); i++) {
        if (tokens[i]->type == CSS_TOKEN_URL ||
            tokens[i]->type == CSS_TOKEN_FUNCTION) {
            found_url = true;
            break;
        }
    }
    EXPECT_TRUE(found_url);
}

TEST_F(CssTokenizerUnitTest, URL_Quoted) {
    auto tokens = Tokenize("url(\"image.png\")");

    ASSERT_GT(tokens.count(), 0);
}

// =============================================================================
// Category 4: Function Tokenization
// =============================================================================

TEST_F(CssTokenizerUnitTest, Function_RGB) {
    auto tokens = Tokenize("rgb(");

    ASSERT_GE(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_FUNCTION);
    // Function tokens include the opening parenthesis in the value
    EXPECT_STREQ(tokens[0]->value, "rgb(");
}

TEST_F(CssTokenizerUnitTest, Function_Calc) {
    auto tokens = Tokenize("calc(");

    ASSERT_GE(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_FUNCTION);
    // Function tokens include the opening parenthesis in the value
    EXPECT_STREQ(tokens[0]->value, "calc(");
}

TEST_F(CssTokenizerUnitTest, Function_Var) {
    auto tokens = Tokenize("var(");

    ASSERT_GE(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_FUNCTION);
    // Function tokens include the opening parenthesis in the value
    EXPECT_STREQ(tokens[0]->value, "var(");
}

TEST_F(CssTokenizerUnitTest, AtKeyword_Media) {
    auto tokens = Tokenize("@media");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_AT_KEYWORD);
    // At-keyword tokens include the @ symbol in the value
    EXPECT_STREQ(tokens[0]->value, "@media");
}

TEST_F(CssTokenizerUnitTest, AtKeyword_Keyframes) {
    auto tokens = Tokenize("@keyframes");

    ASSERT_EQ(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_AT_KEYWORD);
    // At-keyword tokens include the @ symbol in the value
    EXPECT_STREQ(tokens[0]->value, "@keyframes");
}

TEST_F(CssTokenizerUnitTest, CustomProperty_Declaration) {
    auto tokens = Tokenize("--primary-color");

    ASSERT_GE(tokens.count(), 1);
    // Custom properties may be tokenized as IDENT or CUSTOM_PROPERTY
    bool found_custom = false;
    for (size_t i = 0; i < tokens.count(); i++) {
        if (tokens[i]->type == CSS_TOKEN_IDENT ||
            tokens[i]->type == CSS_TOKEN_CUSTOM_PROPERTY) {
            if (tokens[i]->value && strstr(tokens[i]->value, "primary-color")) {
                found_custom = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_custom);
}

// =============================================================================
// Category 5: Unicode and Escapes
// =============================================================================

TEST_F(CssTokenizerUnitTest, Unicode_BasicMultibyte) {
    auto tokens = Tokenize("測試");

    ASSERT_GE(tokens.count(), 1);
    // Should tokenize as IDENT
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_IDENT);
}

TEST_F(CssTokenizerUnitTest, Unicode_EmojiInIdentifier) {
    auto tokens = Tokenize("button-🔥");

    ASSERT_GE(tokens.count(), 1);
}

TEST_F(CssTokenizerUnitTest, UnicodeEscape_BasicHex) {
    auto tokens = Tokenize("\\26");  // & character

    ASSERT_GT(tokens.count(), 0);
}

// =============================================================================
// Category 6: Edge Cases
// =============================================================================

TEST_F(CssTokenizerUnitTest, EdgeCase_EmptyInput) {
    auto tokens = Tokenize("");

    // Empty input should return valid tokens array (may be empty or EOF token)
    ASSERT_NE(tokens.tokens(), nullptr);
}

TEST_F(CssTokenizerUnitTest, EdgeCase_OnlyWhitespace) {
    auto tokens = Tokenize("   \t\n  ");

    // Should produce whitespace token(s)
    ASSERT_GT(tokens.count(), 0);
}

TEST_F(CssTokenizerUnitTest, EdgeCase_VeryLongIdentifier) {
    std::string long_ident(1000, 'a');
    auto tokens = Tokenize(long_ident.c_str());

    ASSERT_GE(tokens.count(), 1);
    ASSERT_CSS_TOKEN_TYPE(tokens[0], CSS_TOKEN_IDENT);
}

TEST_F(CssTokenizerUnitTest, EdgeCase_Comment) {
    auto tokens = Tokenize("/* comment */");

    ASSERT_GT(tokens.count(), 0);
    // May produce COMMENT token or skip it
}

TEST_F(CssTokenizerUnitTest, EdgeCase_MultipleWhitespace) {
    auto tokens = Tokenize("a  \t\n  b");

    // Should have at least 2 tokens (identifiers) possibly with whitespace between
    ASSERT_GE(tokens.count(), 2);
}

// =============================================================================
// Category 7: Complex Real-World Cases
// =============================================================================

TEST_F(CssTokenizerUnitTest, RealWorld_SimpleRule) {
    auto tokens = Tokenize("div { color: red; }");

    // Should tokenize: div, {, color, :, red, ;, }
    ASSERT_GE(tokens.count(), 7);
}

TEST_F(CssTokenizerUnitTest, RealWorld_ClassWithProperties) {
    auto tokens = Tokenize(".container { width: 100%; margin: 0 auto; }");

    // Should have many tokens
    ASSERT_GT(tokens.count(), 10);
}

TEST_F(CssTokenizerUnitTest, RealWorld_MultipleSelectors) {
    auto tokens = Tokenize("h1, h2, h3 { font-weight: bold; }");

    ASSERT_GT(tokens.count(), 10);
}

// =============================================================================
// Parameterized Tests for Token Types
// =============================================================================

struct TokenTypeTestCase {
    const char* input;
    CssTokenType expected_first_type;
    size_t min_token_count;
};

class TokenTypeParameterizedTest : public ::testing::TestWithParam<TokenTypeTestCase> {
protected:
    PoolGuard pool;
};

TEST_P(TokenTypeParameterizedTest, TokenType) {
    auto test_case = GetParam();

    Tokenizer tokenizer(pool.get(), test_case.input);

    ASSERT_GE(tokenizer.count(), test_case.min_token_count)
        << "For input: " << test_case.input;

    // Check token type
    SCOPED_TRACE("For input: " + std::string(test_case.input));
    ASSERT_CSS_TOKEN_TYPE(tokenizer[0], test_case.expected_first_type);
}

INSTANTIATE_TEST_SUITE_P(
    BasicTokenTypes,
    TokenTypeParameterizedTest,
    ::testing::Values(
        TokenTypeTestCase{"div", CSS_TOKEN_IDENT, 1},
        TokenTypeTestCase{"#id", CSS_TOKEN_HASH, 1},
        TokenTypeTestCase{"42", CSS_TOKEN_NUMBER, 1},
        TokenTypeTestCase{"10px", CSS_TOKEN_DIMENSION, 1},
        TokenTypeTestCase{"50%", CSS_TOKEN_PERCENTAGE, 1},
        TokenTypeTestCase{"\"string\"", CSS_TOKEN_STRING, 1},
        TokenTypeTestCase{"rgb(", CSS_TOKEN_FUNCTION, 1},
        TokenTypeTestCase{"@media", CSS_TOKEN_AT_KEYWORD, 1},
        TokenTypeTestCase{":", CSS_TOKEN_COLON, 1},
        TokenTypeTestCase{";", CSS_TOKEN_SEMICOLON, 1},
        TokenTypeTestCase{",", CSS_TOKEN_COMMA, 1},
        TokenTypeTestCase{"{", CSS_TOKEN_LEFT_BRACE, 1},
        TokenTypeTestCase{"}", CSS_TOKEN_RIGHT_BRACE, 1}
    )
);

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
