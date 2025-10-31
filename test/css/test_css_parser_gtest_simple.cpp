#include <gtest/gtest.h>
#include "../../lambda/input/css/css_engine.h"
#include "../../lib/mempool.h"

class CssParserTest : public ::testing::Test {
protected:
    Pool* pool;
    CssEngine* engine;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
        engine = css_engine_create(pool);
        ASSERT_NE(engine, nullptr) << "Failed to create CSS engine";
    }

    void TearDown() override {
        if (engine) {
            css_engine_destroy(engine);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

// Test basic stylesheet parsing
TEST_F(CssParserTest, ParseEmptyStylesheet) {
    const char* css = "";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Empty stylesheet should have 0 rules";
}

TEST_F(CssParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Whitespace-only stylesheet should have 0 rules";
}

// Test simple style rule parsing
TEST_F(CssParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->rule_count, 0) << "Should have at least 1 rule";

    // Basic validation - just check that we get a rule
    if (stylesheet->rule_count > 0) {
        CssRule* rule = stylesheet->rules[0];
        ASSERT_NE(rule, nullptr) << "Rule should not be NULL";
    }
}

TEST_F(CssParserTest, ParseMultipleRules) {
    const char* css = "body { color: red; } div { margin: 10px; }";
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GE(stylesheet->rule_count, 1) << "Should have at least 1 rule";
}

TEST_F(CssParserTest, ParseInvalidCSS) {
    const char* css = "body { color: ; }"; // Missing value
    CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

    // Should still create a stylesheet even with invalid CSS
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL even with invalid CSS";
}
