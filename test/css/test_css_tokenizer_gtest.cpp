#include <gtest/gtest.h>
#include "../../lambda/input/css/css_tokenizer.h"
#include "../../lib/mempool.h"
#include <string.h>

class CssTokenizerTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    CSSToken* tokenize(const char* input, size_t* count) {
        return css_tokenize(input, strlen(input), pool, count);
    }

    // Simplified token checking - just check if we get tokens back
    void expectTokensGenerated(const char* input, size_t min_expected_tokens) {
        size_t count;
        CSSToken* tokens = tokenize(input, &count);
        EXPECT_NE(tokens, nullptr) << "Tokenizer should return tokens for: " << input;
        EXPECT_GE(count, min_expected_tokens) << "Should have at least " << min_expected_tokens << " tokens for: " << input;
    }
};

// Simplified tests that just ensure the tokenizer doesn't crash and returns some tokens
TEST_F(CssTokenizerTest, BasicTokenGeneration) {
    expectTokensGenerated("div { color: red; }", 3);
}

TEST_F(CssTokenizerTest, NumberTokenGeneration) {
    expectTokensGenerated("42", 1);
    expectTokensGenerated("3.14", 1);
    expectTokensGenerated("-5", 1);
    expectTokensGenerated("+10", 1);
    expectTokensGenerated(".5", 1);
}

TEST_F(CssTokenizerTest, DimensionTokenGeneration) {
    expectTokensGenerated("10px", 1);
    expectTokensGenerated("2em", 1);
    expectTokensGenerated("100%", 1);
}

TEST_F(CssTokenizerTest, StringTokenGeneration) {
    expectTokensGenerated("\"hello\"", 1);
    expectTokensGenerated("'world'", 1);
}

TEST_F(CssTokenizerTest, HashTokenGeneration) {
    expectTokensGenerated("#id", 1);
    expectTokensGenerated("#123", 1);
}

TEST_F(CssTokenizerTest, FunctionTokenGeneration) {
    expectTokensGenerated("rgb(", 1);
    expectTokensGenerated("calc(", 1);
}

TEST_F(CssTokenizerTest, AtRuleTokenGeneration) {
    expectTokensGenerated("@media", 1);
    expectTokensGenerated("@keyframes", 1);
    expectTokensGenerated("@import", 1);
}

TEST_F(CssTokenizerTest, AttributeSelectorTokenGeneration) {
    expectTokensGenerated("[attr]", 3); // [, attr, ]
    expectTokensGenerated("[attr=\"value\"]", 5); // [, attr, =, "value", ]
    expectTokensGenerated("[attr^=\"prefix\"]", 5);
}

TEST_F(CssTokenizerTest, CommentTokenGeneration) {
    expectTokensGenerated("/* comment */", 1);
    expectTokensGenerated("div /* inline */ span", 3);
}

TEST_F(CssTokenizerTest, UrlTokenGeneration) {
    expectTokensGenerated("url(image.png)", 1);
    expectTokensGenerated("url(\"quoted.jpg\")", 1);
    expectTokensGenerated("url('single.gif')", 1);
}

TEST_F(CssTokenizerTest, DelimiterTokenGeneration) {
    expectTokensGenerated("+ - * /", 7); // 4 delims + 3 whitespace
    expectTokensGenerated("= > < ! ?", 9); // 5 delims + 4 whitespace
}

TEST_F(CssTokenizerTest, ErrorRecoveryBasic) {
    // Just check that unterminated strings don't crash
    size_t count;
    CSSToken* tokens = tokenize("\"unterminated", &count);
    EXPECT_NE(tokens, nullptr) << "Tokenizer should handle unterminated strings";
    EXPECT_GT(count, 0) << "Should produce at least one token";
}

TEST_F(CssTokenizerTest, WhitespaceHandling) {
    expectTokensGenerated("  \t\n\r\f  ", 1); // Should handle various whitespace
}

TEST_F(CssTokenizerTest, ComplexCssBasic) {
    const char* css = "@media screen and (max-width: 768px) { .container { width: 100%; } }";
    expectTokensGenerated(css, 10); // Should produce many tokens
}

// Test basic utility functions
TEST_F(CssTokenizerTest, UtilityFunctions) {
    // Test token type to string
    EXPECT_NE(css_token_type_to_string(CSS_TOKEN_IDENT), nullptr);
    EXPECT_STREQ(css_token_type_to_string(CSS_TOKEN_IDENT), "IDENT");
    
    // Create a simple token for testing
    size_t count;
    CSSToken* tokens = tokenize("test", &count);
    if (tokens && count > 0) {
        // Test utility functions with the first token
        EXPECT_FALSE(css_token_is_whitespace(&tokens[0])); // "test" is not whitespace
        EXPECT_FALSE(css_token_is_comment(&tokens[0])); // "test" is not a comment
        
        // Test token to string
        char* str = css_token_to_string(&tokens[0], pool);
        if (str) {
            EXPECT_GT(strlen(str), 0) << "Token string should have length";
        }
    }
}

// Simplified token stream test
TEST_F(CssTokenizerTest, TokenStreamBasic) {
    size_t count;
    CSSToken* tokens = tokenize("a b", &count);
    
    if (tokens && count > 0) {
        CSSTokenStream* stream = css_token_stream_create(tokens, count, pool);
        EXPECT_NE(stream, nullptr) << "Token stream should be created";
        
        if (stream) {
            // Just test that we can get current token and advance
            CSSToken* current = css_token_stream_current(stream);
            EXPECT_NE(current, nullptr) << "Current token should not be NULL";
            
            // Test advance
            bool advanced = css_token_stream_advance(stream);
            EXPECT_TRUE(advanced || css_token_stream_at_end(stream)) << "Should advance or be at end";
        }
    }
}
