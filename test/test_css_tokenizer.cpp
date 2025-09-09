#include <gtest/gtest.h>
#include "../lambda/input/css_tokenizer.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include <string.h>

class CSSTokenizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = pool_variable_create(1024 * 1024); // 1MB pool
    }
    
    void TearDown() override {
        if (pool) {
            pool_variable_destroy(pool);
        }
    }
    
    CSSToken* tokenize(const char* input, size_t* count) {
        return css_tokenize(input, strlen(input), pool, count);
    }
    
    void expectToken(const CSSToken& token, CSSTokenType type, const char* expected_text) {
        EXPECT_EQ(token.type, type);
        if (expected_text) {
            EXPECT_EQ(token.length, strlen(expected_text));
            EXPECT_EQ(strncmp(token.start, expected_text, token.length), 0);
        }
    }
    
    VariableMemPool* pool;
};

TEST_F(CSSTokenizerTest, BasicTokens) {
    size_t count;
    CSSToken* tokens = tokenize("div { color: red; }", &count);
    
    ASSERT_NE(tokens, nullptr);
    ASSERT_EQ(count, 8); // div, whitespace, {, whitespace, color, :, whitespace, red, ;, whitespace, }, EOF
    
    expectToken(tokens[0], CSS_TOKEN_IDENT, "div");
    expectToken(tokens[1], CSS_TOKEN_WHITESPACE, " ");
    expectToken(tokens[2], CSS_TOKEN_LEFT_CURLY, "{");
    expectToken(tokens[3], CSS_TOKEN_WHITESPACE, " ");
    expectToken(tokens[4], CSS_TOKEN_IDENT, "color");
    expectToken(tokens[5], CSS_TOKEN_COLON, ":");
    expectToken(tokens[6], CSS_TOKEN_WHITESPACE, " ");
    expectToken(tokens[7], CSS_TOKEN_IDENT, "red");
    expectToken(tokens[8], CSS_TOKEN_SEMICOLON, ";");
    expectToken(tokens[9], CSS_TOKEN_WHITESPACE, " ");
    expectToken(tokens[10], CSS_TOKEN_RIGHT_CURLY, "}");
    expectToken(tokens[11], CSS_TOKEN_EOF, nullptr);
}

TEST_F(CSSTokenizerTest, Numbers) {
    size_t count;
    CSSToken* tokens = tokenize("42 3.14 -5 +10 .5", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    // Find number tokens (skip whitespace)
    int token_idx = 0;
    
    // 42
    expectToken(tokens[token_idx], CSS_TOKEN_NUMBER, "42");
    EXPECT_EQ(tokens[token_idx].data.number_value, 42.0);
    token_idx += 2; // Skip whitespace
    
    // 3.14
    expectToken(tokens[token_idx], CSS_TOKEN_NUMBER, "3.14");
    EXPECT_NEAR(tokens[token_idx].data.number_value, 3.14, 0.001);
    token_idx += 2; // Skip whitespace
    
    // -5
    expectToken(tokens[token_idx], CSS_TOKEN_NUMBER, "-5");
    EXPECT_EQ(tokens[token_idx].data.number_value, -5.0);
    token_idx += 2; // Skip whitespace
    
    // +10
    expectToken(tokens[token_idx], CSS_TOKEN_NUMBER, "+10");
    EXPECT_EQ(tokens[token_idx].data.number_value, 10.0);
    token_idx += 2; // Skip whitespace
    
    // .5
    expectToken(tokens[token_idx], CSS_TOKEN_NUMBER, ".5");
    EXPECT_NEAR(tokens[token_idx].data.number_value, 0.5, 0.001);
}

TEST_F(CSSTokenizerTest, Dimensions) {
    size_t count;
    CSSToken* tokens = tokenize("10px 2em 50% 1.5rem", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // 10px
    expectToken(tokens[token_idx], CSS_TOKEN_DIMENSION, "10px");
    EXPECT_EQ(tokens[token_idx].data.number_value, 10.0);
    token_idx += 2; // Skip whitespace
    
    // 2em
    expectToken(tokens[token_idx], CSS_TOKEN_DIMENSION, "2em");
    EXPECT_EQ(tokens[token_idx].data.number_value, 2.0);
    token_idx += 2; // Skip whitespace
    
    // 50%
    expectToken(tokens[token_idx], CSS_TOKEN_PERCENTAGE, "50%");
    EXPECT_EQ(tokens[token_idx].data.number_value, 50.0);
    token_idx += 2; // Skip whitespace
    
    // 1.5rem
    expectToken(tokens[token_idx], CSS_TOKEN_DIMENSION, "1.5rem");
    EXPECT_NEAR(tokens[token_idx].data.number_value, 1.5, 0.001);
}

TEST_F(CSSTokenizerTest, Strings) {
    size_t count;
    CSSToken* tokens = tokenize("\"hello\" 'world' \"escaped\\\"quote\"", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // "hello"
    expectToken(tokens[token_idx], CSS_TOKEN_STRING, "\"hello\"");
    token_idx += 2; // Skip whitespace
    
    // 'world'
    expectToken(tokens[token_idx], CSS_TOKEN_STRING, "'world'");
    token_idx += 2; // Skip whitespace
    
    // "escaped\"quote"
    expectToken(tokens[token_idx], CSS_TOKEN_STRING, "\"escaped\\\"quote\"");
}

TEST_F(CSSTokenizerTest, HashTokens) {
    size_t count;
    CSSToken* tokens = tokenize("#id #123 #-webkit-transform", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // #id
    expectToken(tokens[token_idx], CSS_TOKEN_HASH, "#id");
    EXPECT_EQ(tokens[token_idx].data.hash_type, CSS_HASH_ID);
    token_idx += 2; // Skip whitespace
    
    // #123
    expectToken(tokens[token_idx], CSS_TOKEN_HASH, "#123");
    EXPECT_EQ(tokens[token_idx].data.hash_type, CSS_HASH_UNRESTRICTED);
    token_idx += 2; // Skip whitespace
    
    // #-webkit-transform
    expectToken(tokens[token_idx], CSS_TOKEN_HASH, "#-webkit-transform");
    EXPECT_EQ(tokens[token_idx].data.hash_type, CSS_HASH_ID);
}

TEST_F(CSSTokenizerTest, Functions) {
    size_t count;
    CSSToken* tokens = tokenize("rgb( calc( url(", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // rgb(
    expectToken(tokens[token_idx], CSS_TOKEN_FUNCTION, "rgb");
    expectToken(tokens[token_idx + 1], CSS_TOKEN_LEFT_PAREN, "(");
    token_idx += 3; // Skip whitespace
    
    // calc(
    expectToken(tokens[token_idx], CSS_TOKEN_FUNCTION, "calc");
    expectToken(tokens[token_idx + 1], CSS_TOKEN_LEFT_PAREN, "(");
    token_idx += 3; // Skip whitespace
    
    // url( - special handling
    expectToken(tokens[token_idx], CSS_TOKEN_FUNCTION, "url");
    expectToken(tokens[token_idx + 1], CSS_TOKEN_LEFT_PAREN, "(");
}

TEST_F(CSSTokenizerTest, AtRules) {
    size_t count;
    CSSToken* tokens = tokenize("@media @keyframes @import", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // @media
    expectToken(tokens[token_idx], CSS_TOKEN_AT_KEYWORD, "@media");
    token_idx += 2; // Skip whitespace
    
    // @keyframes
    expectToken(tokens[token_idx], CSS_TOKEN_AT_KEYWORD, "@keyframes");
    token_idx += 2; // Skip whitespace
    
    // @import
    expectToken(tokens[token_idx], CSS_TOKEN_AT_KEYWORD, "@import");
}

TEST_F(CSSTokenizerTest, AttributeSelectors) {
    size_t count;
    CSSToken* tokens = tokenize("[attr] [attr=\"value\"] [attr^=\"prefix\"]", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    // Should contain LEFT_SQUARE, RIGHT_SQUARE, PREFIX_MATCH tokens
    bool found_left_square = false;
    bool found_right_square = false;
    bool found_prefix_match = false;
    
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_LEFT_SQUARE) found_left_square = true;
        if (tokens[i].type == CSS_TOKEN_RIGHT_SQUARE) found_right_square = true;
        if (tokens[i].type == CSS_TOKEN_PREFIX_MATCH) found_prefix_match = true;
    }
    
    EXPECT_TRUE(found_left_square);
    EXPECT_TRUE(found_right_square);
    EXPECT_TRUE(found_prefix_match);
}

TEST_F(CSSTokenizerTest, Comments) {
    size_t count;
    CSSToken* tokens = tokenize("/* comment */ div /* another */", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    int token_idx = 0;
    
    // /* comment */
    expectToken(tokens[token_idx], CSS_TOKEN_COMMENT, "/* comment */");
    token_idx += 2; // Skip whitespace
    
    // div
    expectToken(tokens[token_idx], CSS_TOKEN_IDENT, "div");
    token_idx += 2; // Skip whitespace
    
    // /* another */
    expectToken(tokens[token_idx], CSS_TOKEN_COMMENT, "/* another */");
}

TEST_F(CSSTokenizerTest, URLs) {
    size_t count;
    CSSToken* tokens = tokenize("url(image.png) url(\"quoted.jpg\") url('single.gif')", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    // Should find URL tokens
    bool found_unquoted_url = false;
    bool found_double_quoted_url = false;
    bool found_single_quoted_url = false;
    
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_URL) {
            if (strncmp(tokens[i].start, "url(image.png)", tokens[i].length) == 0) {
                found_unquoted_url = true;
            } else if (strncmp(tokens[i].start, "url(\"quoted.jpg\")", tokens[i].length) == 0) {
                found_double_quoted_url = true;
            } else if (strncmp(tokens[i].start, "url('single.gif')", tokens[i].length) == 0) {
                found_single_quoted_url = true;
            }
        }
    }
    
    EXPECT_TRUE(found_unquoted_url);
    EXPECT_TRUE(found_double_quoted_url);
    EXPECT_TRUE(found_single_quoted_url);
}

TEST_F(CSSTokenizerTest, Delimiters) {
    size_t count;
    CSSToken* tokens = tokenize("+ - * / = > < ! ?", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    // Should find DELIM tokens for various characters
    bool found_plus = false;
    bool found_minus = false;
    bool found_asterisk = false;
    bool found_slash = false;
    
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DELIM) {
            if (tokens[i].data.delimiter == '+') found_plus = true;
            if (tokens[i].data.delimiter == '-') found_minus = true;
            if (tokens[i].data.delimiter == '*') found_asterisk = true;
            if (tokens[i].data.delimiter == '/') found_slash = true;
        }
    }
    
    EXPECT_TRUE(found_plus);
    EXPECT_TRUE(found_minus);
    EXPECT_TRUE(found_asterisk);
    EXPECT_TRUE(found_slash);
}

TEST_F(CSSTokenizerTest, ErrorRecovery) {
    size_t count;
    
    // Unterminated string
    CSSToken* tokens = tokenize("\"unterminated", &count);
    ASSERT_NE(tokens, nullptr);
    
    // Should still produce a string token (even if unterminated)
    bool found_string = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_STRING) {
            found_string = true;
            break;
        }
    }
    EXPECT_TRUE(found_string);
}

TEST_F(CSSTokenizerTest, Whitespace) {
    size_t count;
    CSSToken* tokens = tokenize("  \t\n\r\f  ", &count);
    
    ASSERT_NE(tokens, nullptr);
    ASSERT_GE(count, 2); // At least whitespace + EOF
    
    expectToken(tokens[0], CSS_TOKEN_WHITESPACE, nullptr);
    expectToken(tokens[count - 1], CSS_TOKEN_EOF, nullptr);
}

TEST_F(CSSTokenizerTest, ComplexCSS) {
    const char* css = R"(
        @media screen and (max-width: 768px) {
            .container {
                width: 100%;
                padding: 10px 20px;
                background: linear-gradient(45deg, #ff0000, #00ff00);
                font-family: "Helvetica Neue", Arial, sans-serif;
            }
            
            .button:hover::before {
                content: "→";
                transform: translateX(-50%) scale(1.2);
            }
        }
    )";
    
    size_t count;
    CSSToken* tokens = tokenize(css, &count);
    
    ASSERT_NE(tokens, nullptr);
    ASSERT_GT(count, 50); // Should have many tokens
    
    // Verify we have expected token types
    bool found_at_keyword = false;
    bool found_function = false;
    bool found_hash = false;
    bool found_string = false;
    bool found_dimension = false;
    bool found_percentage = false;
    
    for (size_t i = 0; i < count; i++) {
        switch (tokens[i].type) {
            case CSS_TOKEN_AT_KEYWORD: found_at_keyword = true; break;
            case CSS_TOKEN_FUNCTION: found_function = true; break;
            case CSS_TOKEN_HASH: found_hash = true; break;
            case CSS_TOKEN_STRING: found_string = true; break;
            case CSS_TOKEN_DIMENSION: found_dimension = true; break;
            case CSS_TOKEN_PERCENTAGE: found_percentage = true; break;
            default: break;
        }
    }
    
    EXPECT_TRUE(found_at_keyword);
    EXPECT_TRUE(found_function);
    EXPECT_TRUE(found_hash);
    EXPECT_TRUE(found_string);
    EXPECT_TRUE(found_dimension);
    EXPECT_TRUE(found_percentage);
}

// Token stream tests
TEST_F(CSSTokenizerTest, TokenStream) {
    size_t count;
    CSSToken* tokens = tokenize("div { color: red; }", &count);
    
    CSSTokenStream* stream = css_token_stream_create(tokens, count, pool);
    ASSERT_NE(stream, nullptr);
    
    // Test current token
    CSSToken* current = css_token_stream_current(stream);
    ASSERT_NE(current, nullptr);
    expectToken(*current, CSS_TOKEN_IDENT, "div");
    
    // Test advance
    EXPECT_TRUE(css_token_stream_advance(stream));
    current = css_token_stream_current(stream);
    expectToken(*current, CSS_TOKEN_WHITESPACE, " ");
    
    // Test peek
    CSSToken* peeked = css_token_stream_peek(stream, 1);
    ASSERT_NE(peeked, nullptr);
    expectToken(*peeked, CSS_TOKEN_LEFT_CURLY, "{");
    
    // Test consume
    EXPECT_TRUE(css_token_stream_consume(stream, CSS_TOKEN_WHITESPACE));
    current = css_token_stream_current(stream);
    expectToken(*current, CSS_TOKEN_LEFT_CURLY, "{");
    
    // Test at_end
    EXPECT_FALSE(css_token_stream_at_end(stream));
    
    // Advance to end
    while (!css_token_stream_at_end(stream)) {
        css_token_stream_advance(stream);
    }
    EXPECT_TRUE(css_token_stream_at_end(stream));
}

TEST_F(CSSTokenizerTest, TokenUtilities) {
    size_t count;
    CSSToken* tokens = tokenize("div /* comment */ red", &count);
    
    // Test utility functions
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_WHITESPACE) {
            EXPECT_TRUE(css_token_is_whitespace(&tokens[i]));
            EXPECT_FALSE(css_token_is_comment(&tokens[i]));
        } else if (tokens[i].type == CSS_TOKEN_COMMENT) {
            EXPECT_FALSE(css_token_is_whitespace(&tokens[i]));
            EXPECT_TRUE(css_token_is_comment(&tokens[i]));
        }
        
        if (tokens[i].type == CSS_TOKEN_IDENT) {
            if (css_token_equals_string(&tokens[i], "div")) {
                EXPECT_TRUE(true); // Found div token
            } else if (css_token_equals_string(&tokens[i], "red")) {
                EXPECT_TRUE(true); // Found red token
            }
        }
    }
    
    // Test token to string
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_IDENT) {
            char* str = css_token_to_string(&tokens[i], pool);
            ASSERT_NE(str, nullptr);
            EXPECT_GT(strlen(str), 0);
        }
    }
}
