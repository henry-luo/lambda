#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>
#include "../../lambda/input/css/css_tokenizer.h"
#include "../../lib/mempool.h"

// Test fixture class for CSS files safe tests
class CssFilesSafeTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    void validateCssFile(const char* css_content, const char* test_name) {
        size_t token_count;
        CSSToken* tokens = css_tokenize(css_content, strlen(css_content), pool, &token_count);
        EXPECT_NE(tokens, nullptr) << "Should tokenize CSS file: " << test_name;
        EXPECT_GT(token_count, (size_t)0) << "Should produce tokens for: " << test_name;
    }
};

// Test parsing simple CSS file-like content
TEST_F(CssFilesSafeTest, ParseSimpleCssFile) {
    const char* css_content = R"(
        body { margin: 0; padding: 0; }
        .container { width: 100%; }
    )";

    validateCssFile(css_content, "Simple CSS file");
}

// Test parsing stylesheet-style CSS content
TEST_F(CssFilesSafeTest, ParseStylesheetCssFile) {
    const char* css_content = R"(
        @charset "UTF-8";
        /* Global styles */
        * { box-sizing: border-box; }
        body { font-family: Arial, sans-serif; }
    )";

    validateCssFile(css_content, "Stylesheet CSS file");
}

// Test parsing inline multiline CSS
TEST_F(CssFilesSafeTest, ParseInlineMultilineCss) {
    const char* css = "p{color:red;font-size:14px;}div{margin:10px;}";

    validateCssFile(css, "Inline multiline CSS");
}

// Test parsing complex selectors safely
TEST_F(CssFilesSafeTest, ParseComplexSelectors) {
    const char* css = R"(
        .class#id[attr="value"]:hover::before {
            content: "test";
        }
    )";

    validateCssFile(css, "Complex selectors");
}

// Test parsing complete CSS grammar file content
TEST_F(CssFilesSafeTest, ParseCompleteCssGrammarFile) {
    const char* css_content = R"(
        @media screen and (max-width: 768px) {
            .responsive { display: block; }
        }
        @keyframes slide {
            from { left: 0; }
            to { left: 100%; }
        }
    )";

    validateCssFile(css_content, "Complete CSS grammar file");
}

// Test parsing CSS functions sample file
TEST_F(CssFilesSafeTest, ParseCssFunctionsSampleFile) {
    const char* css_content = R"(
        .calc-example { width: calc(100% - 20px); }
        .rgb-example { color: rgb(255, 0, 0); }
        .url-example { background: url("image.png"); }
    )";

    validateCssFile(css_content, "CSS functions sample file");
}

// Test parsing stylesheet30 file
TEST_F(CssFilesSafeTest, ParseStylesheet30File) {
    const char* css_content = R"(
        .example { 
            color: red; 
            background: blue; 
            margin: 10px;
            padding: 5px;
        }
    )";

    validateCssFile(css_content, "Stylesheet30 file");
}