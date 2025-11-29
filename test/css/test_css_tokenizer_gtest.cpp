#include <gtest/gtest.h>
#include "../../lambda/input/css/css_tokenizer.hpp"
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

// ============================================================================
// Regression Tests for Token Union Copying Issue
// ============================================================================

TEST_F(CssTokenizerTest, DimensionToken_PreservesUnitField) {
    // Test that dimension tokens properly preserve the unit field in the union
    // This is a regression test for the bug where only number_value was copied,
    // losing the dimension.unit field
    
    size_t count;
    CSSToken* tokens = tokenize("16px", &count);
    
    ASSERT_NE(tokens, nullptr) << "Should tokenize dimension value";
    ASSERT_GE(count, 1) << "Should have at least one token";
    
    // Find the DIMENSION token (skip whitespace/EOF)
    CSSToken* dim_token = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            dim_token = &tokens[i];
            break;
        }
    }
    
    ASSERT_NE(dim_token, nullptr) << "Should have a DIMENSION token";
    EXPECT_EQ(dim_token->data.dimension.value, 16.0) << "Dimension value should be 16";
    EXPECT_EQ(dim_token->data.dimension.unit, CSS_UNIT_PX) << "Dimension unit should be CSS_UNIT_PX (1)";
    EXPECT_NE(dim_token->data.dimension.unit, CSS_UNIT_NONE) << "Dimension unit should not be CSS_UNIT_NONE (0)";
}

TEST_F(CssTokenizerTest, DimensionToken_MultipleDifferentUnits) {
    // Test that multiple dimension tokens in sequence each preserve their units
    size_t count;
    CSSToken* tokens = tokenize("10px 2em 50% 1.5rem", &count);
    
    ASSERT_NE(tokens, nullptr) << "Should tokenize multiple dimensions";
    
    // Collect all dimension tokens
    struct DimInfo {
        double value;
        CssUnit expected_unit;
    };
    
    DimInfo expected[] = {
        {10.0, CSS_UNIT_PX},
        {2.0, CSS_UNIT_EM},
        {1.5, CSS_UNIT_REM}
    };
    
    int dim_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            ASSERT_LT(dim_count, 3) << "Should not have more than 3 dimension tokens";
            EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, expected[dim_count].value) 
                << "Dimension " << dim_count << " value mismatch";
            EXPECT_EQ(tokens[i].data.dimension.unit, expected[dim_count].expected_unit) 
                << "Dimension " << dim_count << " unit mismatch";
            dim_count++;
        } else if (tokens[i].type == CSS_TOKEN_PERCENTAGE) {
            // Percentage is stored differently, just verify it exists
            EXPECT_EQ(tokens[i].data.number_value, 50.0) << "Percentage value should be 50";
        }
    }
    
    EXPECT_EQ(dim_count, 3) << "Should have found 3 dimension tokens";
}

TEST_F(CssTokenizerTest, DimensionToken_SignedNumbers) {
    // Test signed dimension values preserve units
    size_t count;
    CSSToken* tokens = tokenize("-5px +3em", &count);
    
    ASSERT_NE(tokens, nullptr) << "Should tokenize signed dimensions";
    
    int dim_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            if (dim_count == 0) {
                EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, -5.0) << "First dimension value";
                EXPECT_EQ(tokens[i].data.dimension.unit, CSS_UNIT_PX) << "First dimension unit";
            } else if (dim_count == 1) {
                EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, 3.0) << "Second dimension value";
                EXPECT_EQ(tokens[i].data.dimension.unit, CSS_UNIT_EM) << "Second dimension unit";
            }
            dim_count++;
        }
    }
    
    EXPECT_EQ(dim_count, 2) << "Should have found 2 dimension tokens";
}

TEST_F(CssTokenizerTest, DimensionToken_DecimalValues) {
    // Test decimal dimension values with units
    size_t count;
    CSSToken* tokens = tokenize("0.5px 1.25em .75rem", &count);
    
    ASSERT_NE(tokens, nullptr) << "Should tokenize decimal dimensions";
    
    struct Expected {
        double value;
        CssUnit unit;
    } expected[] = {
        {0.5, CSS_UNIT_PX},
        {1.25, CSS_UNIT_EM},
        {0.75, CSS_UNIT_REM}
    };
    
    int dim_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            ASSERT_LT(dim_count, 3);
            EXPECT_NEAR(tokens[i].data.dimension.value, expected[dim_count].value, 0.0001) 
                << "Dimension " << dim_count << " value";
            EXPECT_EQ(tokens[i].data.dimension.unit, expected[dim_count].unit) 
                << "Dimension " << dim_count << " unit";
            dim_count++;
        }
    }
    
    EXPECT_EQ(dim_count, 3) << "Should have 3 dimension tokens";
}

TEST_F(CssTokenizerTest, DimensionToken_ViewportUnits) {
    // Test viewport units (vw, vh, vmin, vmax)
    size_t count;
    CSSToken* tokens = tokenize("100vw 50vh 10vmin 20vmax", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    struct Expected {
        double value;
        CssUnit unit;
    } expected[] = {
        {100.0, CSS_UNIT_VW},
        {50.0, CSS_UNIT_VH},
        {10.0, CSS_UNIT_VMIN},
        {20.0, CSS_UNIT_VMAX}
    };
    
    int dim_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            ASSERT_LT(dim_count, 4);
            EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, expected[dim_count].value);
            EXPECT_EQ(tokens[i].data.dimension.unit, expected[dim_count].unit);
            dim_count++;
        }
    }
    
    EXPECT_EQ(dim_count, 4) << "Should have 4 viewport unit tokens";
}

TEST_F(CssTokenizerTest, DimensionToken_AllMetadataFieldsCopied) {
    // Test that all token fields are copied, not just the union
    size_t count;
    CSSToken* tokens = tokenize("42px", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            // Verify all standard fields are set
            EXPECT_NE(tokens[i].start, nullptr) << "Token start should be set";
            EXPECT_GT(tokens[i].length, 0) << "Token length should be positive";
            EXPECT_NE(tokens[i].value, nullptr) << "Token value should be set";
            
            // Verify union fields
            EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, 42.0);
            EXPECT_EQ(tokens[i].data.dimension.unit, CSS_UNIT_PX);
            
            // Metadata fields should exist (even if 0/NULL for simple test case)
            // Just verify they don't cause crashes when accessed
            int line = tokens[i].line;
            int column = tokens[i].column;
            bool escaped = tokens[i].is_escaped;
            uint32_t codepoint = tokens[i].unicode_codepoint;
            
            (void)line; (void)column; (void)escaped; (void)codepoint; // Suppress unused warnings
            break;
        }
    }
}

TEST_F(CssTokenizerTest, BorderShorthand_MultipleDimensions) {
    // Regression test for the original bug: border shorthand with dimension
    size_t count;
    CSSToken* tokens = tokenize("1px solid #999", &count);
    
    ASSERT_NE(tokens, nullptr) << "Should tokenize border shorthand";
    
    // Should have: 1px (DIMENSION), whitespace, solid (IDENT), whitespace, #999 (HASH), EOF
    bool found_dimension = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DIMENSION) {
            EXPECT_DOUBLE_EQ(tokens[i].data.dimension.value, 1.0) << "Border width value";
            EXPECT_EQ(tokens[i].data.dimension.unit, CSS_UNIT_PX) << "Border width unit";
            found_dimension = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_dimension) << "Should find dimension token in border shorthand";
}

TEST_F(CssTokenizerTest, TokenCopyingPreservesUnion_HashToken) {
    // Test that union copying works for other union types (not just dimension)
    size_t count;
    CSSToken* tokens = tokenize("#ff0000", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    bool found_hash = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_HASH) {
            // Hash tokens use data.hash_type field in the union
            // The hash_type might be 0 (unrestricted), so just verify the value is set
            EXPECT_NE(tokens[i].value, nullptr) << "Hash value should be set";
            if (tokens[i].value) {
                EXPECT_GT(strlen(tokens[i].value), 0) << "Hash value should not be empty";
            }
            found_hash = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_hash) << "Should find hash token";
}

TEST_F(CssTokenizerTest, TokenCopyingPreservesUnion_DelimiterToken) {
    // Test delimiter tokens which use data.delimiter field
    size_t count;
    CSSToken* tokens = tokenize("+", &count);
    
    ASSERT_NE(tokens, nullptr);
    
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_DELIM) {
            EXPECT_EQ(tokens[i].data.delimiter, '+') << "Delimiter character should be preserved";
            break;
        }
    }
}

