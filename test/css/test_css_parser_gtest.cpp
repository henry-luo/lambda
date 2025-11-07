#include <gtest/gtest.h>
#include "../../lambda/input/css/css_tokenizer.hpp"
#include "../../lambda/input/css/css_value_parser.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lib/mempool.h"

class CssParserTest : public ::testing::Test {
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

    // Helper to validate CSS tokenization works
    void validateTokenization(const char* css, size_t min_tokens = 1) {
        size_t token_count;
        CSSToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        EXPECT_NE(tokens, nullptr) << "Should tokenize: " << css;
        EXPECT_GE(token_count, min_tokens) << "Should have at least " << min_tokens << " tokens";
    }
};

// Test basic CSS parsing components
TEST_F(CssParserTest, ParseEmptyStylesheet) {
    const char* css = "";

    // Empty CSS should still work with tokenizer
    size_t token_count;
    CSSToken* tokens = css_tokenize(css, 0, pool, &token_count);
    EXPECT_NE(tokens, nullptr) << "Empty CSS should still return tokens";
}

TEST_F(CssParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    validateTokenization(css, 1); // Should produce whitespace tokens
}

// Test simple style rule parsing
TEST_F(CssParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    validateTokenization(css, 5); // body, {, color, :, red, ;, }

    // Test that parsers can be created
    CssPropertyValueParser* prop_parser = css_property_value_parser_create(pool);
    EXPECT_NE(prop_parser, nullptr) << "Property parser should be created";
    if (prop_parser) {
        css_property_value_parser_destroy(prop_parser);
    }

    // Legacy selector parser removed - modern array-based parser is integrated into css_parser.c
    // CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
    // EXPECT_NE(sel_parser, nullptr) << "Selector parser should be created";
    // if (sel_parser) {
    //     css_selector_parser_destroy(sel_parser);
    // }
}

TEST_F(CssParserTest, ParseMultipleRules) {
    const char* css = "body { color: red; } div { margin: 10px; }";
    validateTokenization(css, 10); // Should have many tokens
}

TEST_F(CssParserTest, ParseInvalidCSS) {
    const char* css = "invalid { css } syntax";
    validateTokenization(css, 3); // Should still tokenize even if semantically invalid
}
