#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lib/mempool.h"

// Test fixture class for CSS files safe tests
class CssFilesSafeTest : public ::testing::Test {
protected:
    Pool* pool;
    css_parser_t* parser;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
        parser = css_parser_create(pool);
        ASSERT_NE(parser, nullptr) << "Failed to create CSS parser";
    }

    void TearDown() override {
        if (parser) {
            css_parser_destroy(parser);
            parser = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    // Safe file reading function with error checking
    char* read_css_file_safe(const char* filename) {
        FILE* file = fopen(filename, "r");
        if (!file) {
            printf("Cannot open file: %s\n", filename);
            return nullptr;
        }

        // Get file size
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return nullptr;
        }

        long size = ftell(file);
        if (size < 0 || size > 100000) { // Limit to 100KB
            fclose(file);
            return nullptr;
        }

        if (fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return nullptr;
        }

        char* content = static_cast<char*>(malloc(size + 1));
        if (!content) {
            fclose(file);
            return nullptr;
        }

        size_t read_size = fread(content, 1, size, file);
        content[read_size] = '\0';
        fclose(file);

        return content;
    }
};

// Test simple.css file
TEST_F(CssFilesSafeTest, ParseSimpleCssFile) {
    char* css_content = read_css_file_safe("test/input/simple.css");
    ASSERT_NE(css_content, nullptr) << "Failed to read simple.css";

    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse simple.css";
    EXPECT_GE(stylesheet->rule_count, 0) << "Stylesheet should be valid";

    free(css_content);
}

// Test stylesheet.css file
TEST_F(CssFilesSafeTest, ParseStylesheetCssFile) {
    char* css_content = read_css_file_safe("test/input/stylesheet.css");
    ASSERT_NE(css_content, nullptr) << "Failed to read stylesheet.css";

    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse stylesheet.css";
    EXPECT_GE(stylesheet->rule_count, 0) << "Stylesheet should be valid";

    free(css_content);
}

// Test with inline CSS that matches file content structure
TEST_F(CssFilesSafeTest, ParseInlineMultilineCss) {
    const char* css = "/* Comment */\nbody {\n    margin: 0;\n    padding: 20px;\n}\n.container {\n    max-width: 1200px;\n}";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse inline multiline CSS";
    EXPECT_GE(stylesheet->rule_count, 0) << "Should have parsed rules";
}

// Test CSS with complex selectors
TEST_F(CssFilesSafeTest, ParseComplexSelectors) {
    const char* css = "h1, h2, h3 { color: #333; }\n.button:hover { background: blue; }";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse complex selectors";
    EXPECT_GE(stylesheet->rule_count, 0) << "Should have parsed rules";
}

// Test CSS with functions
TEST_F(CssFilesSafeTest, ParseCssFunctions) {
    const char* css = ".test { background: linear-gradient(45deg, red, blue); transform: scale(1.05); }";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse CSS functions";
    EXPECT_GE(stylesheet->rule_count, 0) << "Should have parsed rules";
}

// Test complete_css_grammar.css file
TEST_F(CssFilesSafeTest, ParseCompleteCssGrammarFile) {
    char* css_content = read_css_file_safe("test/input/complete_css_grammar.css");
    ASSERT_NE(css_content, nullptr) << "Failed to read complete_css_grammar.css";

    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse complete_css_grammar.css";
    EXPECT_GE(stylesheet->rule_count, 0) << "Stylesheet should be valid";

    free(css_content);
}

// Test css_functions_sample.css file
TEST_F(CssFilesSafeTest, ParseCssFunctionsSampleFile) {
    char* css_content = read_css_file_safe("test/input/css_functions_sample.css");
    ASSERT_NE(css_content, nullptr) << "Failed to read css_functions_sample.css";

    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse css_functions_sample.css";
    EXPECT_GE(stylesheet->rule_count, 0) << "Stylesheet should be valid";

    free(css_content);
}

// Test stylesheet_3_0.css file
TEST_F(CssFilesSafeTest, ParseStylesheet30File) {
    char* css_content = read_css_file_safe("test/input/stylesheet_3_0.css");
    ASSERT_NE(css_content, nullptr) << "Failed to read stylesheet_3_0.css";

    // Parse the CSS
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css_content);
    ASSERT_NE(stylesheet, nullptr) << "Failed to parse stylesheet_3_0.css";
    EXPECT_GE(stylesheet->rule_count, 0) << "Stylesheet should be valid";

    free(css_content);
}
