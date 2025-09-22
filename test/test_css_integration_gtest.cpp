#include <gtest/gtest.h>
#include <string>
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lambda/input/css_properties.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Test fixture for CSS integration tests
class CssIntegrationTest : public ::testing::Test {
protected:
    VariableMemPool* pool;
    css_parser_t* parser;

    void SetUp() override {
        pool_variable_init(&pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT); // 1MB pool
        parser = css_parser_create(pool);
        css_parser_set_strict_mode(parser, false); // Disable strict mode for integration tests
    }

    void TearDown() override {
        if (parser) {
            css_parser_destroy(parser);
        }
        if (pool) {
            pool_variable_destroy(pool);
        }
    }
};

// Test end-to-end parsing of a complete CSS stylesheet
TEST_F(CssIntegrationTest, EndToEndStylesheetParsing) {
    const char* css = R"(
        /* Reset styles */
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: Arial, sans-serif;
            line-height: 1.6;
            color: #333;
            background-color: #fff;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 0 20px;
        }
        
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 2rem 0;
        }
        
        .nav ul {
            list-style: none;
            display: flex;
            gap: 2rem;
        }
        
        .nav a {
            color: white;
            text-decoration: none;
            transition: color 0.3s ease;
        }
        
        .nav a:hover {
            color: #ffd700;
        }
        
        @media (max-width: 768px) {
            .container {
                padding: 0 10px;
            }
            
            .nav ul {
                flex-direction: column;
                gap: 1rem;
            }
        }
        
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }
        
        .fade-in {
            animation: fadeIn 0.6s ease-out;
        }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 parsing errors";
    EXPECT_GT(stylesheet->rule_count, 8) << "Should have more than 8 rules";
    
    // Verify we have different types of rules
    bool has_style_rule = false;
    bool has_media_rule = false;
    bool has_keyframes_rule = false;
    
    css_rule_t* rule = stylesheet->rules;
    while (rule) {
        switch (rule->type) {
            case CSS_RULE_STYLE:
                has_style_rule = true;
                break;
            case CSS_RULE_AT_RULE:
                if (rule->data.at_rule->type == CSS_AT_RULE_MEDIA) {
                    has_media_rule = true;
                } else if (rule->data.at_rule->type == CSS_AT_RULE_KEYFRAMES) {
                    has_keyframes_rule = true;
                }
                break;
            default:
                break;
        }
        rule = rule->next;
    }
    
    EXPECT_TRUE(has_style_rule) << "Should have style rules";
    EXPECT_TRUE(has_media_rule) << "Should have media rules";
    EXPECT_TRUE(has_keyframes_rule) << "Should have keyframes rules";
}

// Test tokenizer and parser integration with complex selectors
TEST_F(CssIntegrationTest, ComplexSelectorParsing) {
    const char* css = R"(
        /* Complex selectors test */
        div.container > .item:nth-child(2n+1) {
            background-color: #f0f0f0;
        }
        
        input[type="email"]:focus,
        input[type="password"]:focus {
            border-color: #007bff;
            box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25);
        }
        
        .sidebar ul li a::before {
            content: "→ ";
            color: #666;
        }
        
        #main-content .article:first-of-type h1 + p {
            font-weight: bold;
            margin-top: 0;
        }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 parsing errors";
    EXPECT_EQ(stylesheet->rule_count, 4) << "Should have 4 rules";
    
    // Verify first rule has complex selector
    css_rule_t* rule = stylesheet->rules;
    ASSERT_EQ(rule->type, CSS_RULE_STYLE) << "First rule should be style rule";
    css_style_rule_t* style_rule = rule->data.style_rule;
    
    // Should have one selector in the list
    css_selector_t* selector = style_rule->selectors;
    ASSERT_NE(selector, nullptr) << "First selector should not be NULL";
    EXPECT_EQ(selector->next, nullptr) << "Should have only one selector";
    
    // Second rule should have comma-separated selectors
    rule = rule->next;
    ASSERT_EQ(rule->type, CSS_RULE_STYLE) << "Second rule should be style rule";
    style_rule = rule->data.style_rule;
    
    // Should have two selectors in the list
    selector = style_rule->selectors;
    ASSERT_NE(selector, nullptr) << "First selector should not be NULL";
    ASSERT_NE(selector->next, nullptr) << "Second selector should not be NULL";
    EXPECT_EQ(selector->next->next, nullptr) << "Should have only two selectors";
}

// Test property validation integration
TEST_F(CssIntegrationTest, PropertyValidationIntegration) {
    const char* css = R"(
        .valid-properties {
            color: red;
            background-color: #ffffff;
            margin: 10px 20px;
            padding: 1em;
            font-size: 16px;
            line-height: 1.5;
            display: flex;
            justify-content: center;
        }
        
        .invalid-properties {
            /* These should be handled gracefully */
            coloor: red;  /* typo */
            background-color: invalid-value;
            margin: 10px 20px 30px 40px 50px;  /* too many values */
            unknown-property: some-value;
        }
        
        .mixed-properties {
            color: blue;
            invalid-prop: value;
            padding: 10px;
            another-invalid: another-value;
            margin: 5px;
        }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    
    // In non-strict mode, parser should handle invalid properties gracefully
    // It might create warnings but shouldn't fail completely
    EXPECT_GE(stylesheet->rule_count, 3) << "Should have at least 3 rules";
    
    // First rule should have valid properties
    css_rule_t* rule = stylesheet->rules;
    ASSERT_EQ(rule->type, CSS_RULE_STYLE) << "First rule should be style rule";
    css_style_rule_t* style_rule = rule->data.style_rule;
    EXPECT_GT(style_rule->declaration_count, 0) << "Should have declarations";
    
    // Verify we can iterate through all rules without crashing
    int rule_count = 0;
    while (rule) {
        if (rule->type == CSS_RULE_STYLE) {
            css_style_rule_t* sr = rule->data.style_rule;
            EXPECT_GE(sr->declaration_count, 0) << "Declaration count should be non-negative";
        }
        rule_count++;
        rule = rule->next;
    }
    EXPECT_EQ(rule_count, stylesheet->rule_count) << "Rule count should match iteration";
}

// Test error recovery integration
TEST_F(CssIntegrationTest, ErrorRecoveryIntegration) {
    const char* css = R"(
        .good-rule {
            color: red;
            margin: 10px;
        }
        
        .bad-rule {
            color: red
            /* missing semicolon above */
            margin: 10px;
        }
        
        .another-good-rule {
            background: blue;
            padding: 5px;
        }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    
    // Parser should recover from errors and continue parsing
    EXPECT_GE(stylesheet->rule_count, 2) << "Should have at least 2 valid rules";
    
    // Should have some valid rules even with parsing errors
    css_rule_t* rule = stylesheet->rules;
    bool found_valid_rule = false;
    while (rule) {
        if (rule->type == CSS_RULE_STYLE) {
            found_valid_rule = true;
            break;
        }
        rule = rule->next;
    }
    EXPECT_TRUE(found_valid_rule) << "Should find at least one valid rule";
}

// Test memory management integration
TEST_F(CssIntegrationTest, MemoryManagementIntegration) {
    const char* css = R"(
        .test1 { color: red; margin: 10px; }
        .test2 { background: blue; padding: 5px; }
        .test3 { font-size: 14px; line-height: 1.4; }
        .test4 { display: block; width: 100%; }
        .test5 { position: relative; top: 10px; }
    )";

    // Parse multiple times to test memory handling
    for (int i = 0; i < 10; i++) {
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL (iteration " << i << ")";
        EXPECT_EQ(stylesheet->rule_count, 5) << "Should have 5 rules (iteration " << i << ")";
        
        // Verify we can access all rules
        css_rule_t* rule = stylesheet->rules;
        int count = 0;
        while (rule) {
            EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "All rules should be style rules";
            count++;
            rule = rule->next;
        }
        EXPECT_EQ(count, 5) << "Should count 5 rules (iteration " << i << ")";
    }
    
    // Memory should be managed by the pool, no explicit cleanup needed for parsed data
}

// Test edge case integration
TEST_F(CssIntegrationTest, EdgeCaseIntegration) {
    // Test empty CSS
    css_stylesheet_t* empty_stylesheet = css_parse_stylesheet(parser, "");
    ASSERT_NE(empty_stylesheet, nullptr) << "Empty stylesheet should not be NULL";
    EXPECT_EQ(empty_stylesheet->rule_count, 0) << "Empty stylesheet should have 0 rules";
    
    // Test only comments
    const char* comments_only = R"(
        /* This is a comment */
        /* Another comment */
        /* Multi-line
           comment */
    )";
    
    css_stylesheet_t* comments_stylesheet = css_parse_stylesheet(parser, comments_only);
    ASSERT_NE(comments_stylesheet, nullptr) << "Comments-only stylesheet should not be NULL";
    EXPECT_EQ(comments_stylesheet->rule_count, 0) << "Comments-only stylesheet should have 0 rules";
    
    // Test whitespace only
    const char* whitespace_only = "   \n\t  \r\n  ";
    css_stylesheet_t* whitespace_stylesheet = css_parse_stylesheet(parser, whitespace_only);
    ASSERT_NE(whitespace_stylesheet, nullptr) << "Whitespace-only stylesheet should not be NULL";
    EXPECT_EQ(whitespace_stylesheet->rule_count, 0) << "Whitespace-only stylesheet should have 0 rules";
    
    // Test single character
    css_stylesheet_t* single_char_stylesheet = css_parse_stylesheet(parser, "x");
    ASSERT_NE(single_char_stylesheet, nullptr) << "Single character stylesheet should not be NULL";
    // This might create an error, but shouldn't crash
}

// Test performance integration
TEST_F(CssIntegrationTest, PerformanceIntegration) {
    // Generate a larger CSS string
    std::string large_css;
    for (int i = 0; i < 100; i++) {
        large_css += ".rule" + std::to_string(i) + " {\n";
        large_css += "  color: #" + std::to_string(i % 16) + std::to_string(i % 16) + std::to_string(i % 16) + ";\n";
        large_css += "  margin: " + std::to_string(i % 20) + "px;\n";
        large_css += "  padding: " + std::to_string(i % 10) + "em;\n";
        large_css += "  font-size: " + std::to_string(12 + i % 8) + "px;\n";
        large_css += "}\n\n";
    }
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, large_css.c_str());
    
    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";
    EXPECT_EQ(stylesheet->rule_count, 100) << "Should have 100 rules";
    
    // Verify structure is correct
    css_rule_t* rule = stylesheet->rules;
    int count = 0;
    while (rule) {
        EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "All rules should be style rules";
        css_style_rule_t* style_rule = rule->data.style_rule;
        EXPECT_EQ(style_rule->declaration_count, 4) << "Each rule should have 4 declarations";
        count++;
        rule = rule->next;
    }
    EXPECT_EQ(count, 100) << "Should count 100 rules";
}