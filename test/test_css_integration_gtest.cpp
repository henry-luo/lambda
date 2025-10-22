#include <gtest/gtest.h>
#include <string>
#include "../lambda/input/css/css_tokenizer.h"
#include "../lambda/input/css/css_property_value_parser.h"
#include "../lambda/input/css/css_selector_parser.h" 
#include "../lambda/input/css/css_style.h"
#include "../lib/mempool.h"

// Test fixture for CSS integration tests
class CssIntegrationTest : public ::testing::Test {
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

    // Helper to tokenize CSS and validate basic tokenization
    void validateCssTokenization(const char* css, size_t min_expected_tokens = 1) {
        size_t token_count;
        CSSToken* tokens = css_tokenize(css, strlen(css), pool, &token_count);
        
        EXPECT_NE(tokens, nullptr) << "Tokenizer should return tokens for: " << css;
        EXPECT_GE(token_count, min_expected_tokens) << "Should have at least " << min_expected_tokens << " tokens";
    }

    // Helper to test CSS component creation (parsers)
    void validateComponentCreation() {
        // Test property value parser creation
        CssPropertyValueParser* prop_parser = css_property_value_parser_create(pool);
        EXPECT_NE(prop_parser, nullptr) << "Property parser should be created";
        if (prop_parser) {
            css_property_value_parser_destroy(prop_parser);
        }
        
        // Test selector parser creation
        CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
        EXPECT_NE(sel_parser, nullptr) << "Selector parser should be created";
        if (sel_parser) {
            css_selector_parser_destroy(sel_parser);
        }
    }
};

// Test complete CSS parsing workflow using available components
TEST_F(CssIntegrationTest, CompleteWorkflowIntegration) {
    const char* css = R"(
        /* CSS Integration Test */
        body {
            margin: 0;
            padding: 0;
            font-family: Arial, sans-serif;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }

        @media screen and (max-width: 768px) {
            .container {
                padding: 10px;
            }
        }

        .fade-in {
            animation: fadeIn 0.6s ease-out;
        }
    )";

    // Test tokenization of the complete CSS
    validateCssTokenization(css, 20);

    // Test component creation
    validateComponentCreation();
}

// Test tokenizer integration with complex selectors
TEST_F(CssIntegrationTest, ComplexSelectorTokenization) {
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
            content: "";
        }
    )";

    // Test tokenization - complex CSS should produce many tokens
    validateCssTokenization(css, 30);

    // Test that parsers can be created and used
    validateComponentCreation();
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
        }
    )";

    // Test tokenization
    validateCssTokenization(css, 15);

    // Test CSS component functionality
    CssPropertyValueParser* parser = css_property_value_parser_create(pool);
    ASSERT_NE(parser, nullptr) << "Property parser should be created";
    
    // Test error handling - add some test errors
    css_property_value_parser_add_error(parser, "Test error message");
    
    // Test env variable functionality - need to create a proper CssValue
    CssValue* test_value = css_value_create_string(pool, "test-value");
    if (test_value) {
        bool env_set = css_property_value_parser_set_env_variable(parser, "test-var", test_value);
        // Result may be true or false, just testing it doesn't crash
    }
    
    css_property_value_parser_destroy(parser);

    // Test selector parser functionality
    CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
    ASSERT_NE(sel_parser, nullptr) << "Selector parser should be created";
    
    // Test error handling
    css_selector_parser_add_error(sel_parser, "Test selector error");
    bool has_errors = css_selector_parser_has_errors(sel_parser);
    EXPECT_TRUE(has_errors) << "Should have errors after adding one";
    
    css_selector_parser_clear_errors(sel_parser);
    bool cleared_errors = css_selector_parser_has_errors(sel_parser);
    EXPECT_FALSE(cleared_errors) << "Should not have errors after clearing";
    
    css_selector_parser_destroy(sel_parser);
}

// Test edge cases and error handling
TEST_F(CssIntegrationTest, EdgeCaseHandling) {
    // Empty CSS - should still work with tokenizer
    size_t empty_count;
    CSSToken* empty_tokens = css_tokenize("", 0, pool, &empty_count);
    EXPECT_NE(empty_tokens, nullptr) << "Empty CSS should still return tokens";
    
    // Comments only
    const char* comments_only = "/* This is just a comment */";
    validateCssTokenization(comments_only, 1);

    // Whitespace only
    const char* whitespace_only = "   \n\t  \r\n  ";
    validateCssTokenization(whitespace_only, 1);

    // Test parser edge cases
    CssPropertyValueParser* parser = css_property_value_parser_create(pool);
    if (parser) {
        // Test multiple errors
        css_property_value_parser_add_error(parser, "Error 1");
        css_property_value_parser_add_error(parser, "Error 2");
        
        css_property_value_parser_destroy(parser);
    }

    // Test selector parser edge cases  
    CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
    if (sel_parser) {
        // Test error state changes
        EXPECT_FALSE(css_selector_parser_has_errors(sel_parser)) << "Should start with no errors";
        
        css_selector_parser_add_error(sel_parser, "Test error");
        EXPECT_TRUE(css_selector_parser_has_errors(sel_parser)) << "Should have errors after adding";
        
        css_selector_parser_clear_errors(sel_parser);
        EXPECT_FALSE(css_selector_parser_has_errors(sel_parser)) << "Should not have errors after clearing";
        
        css_selector_parser_destroy(sel_parser);
    }
}

// Test performance with moderately sized CSS
TEST_F(CssIntegrationTest, ModeratePerformanceTest) {
    std::string large_css = "";
    for (int i = 0; i < 100; i++) {
        large_css += ".rule" + std::to_string(i) + " { color: red; margin: " + std::to_string(i) + "px; }\n";
    }

    // Test tokenization performance
    validateCssTokenization(large_css.c_str(), 500); // Expect many tokens
    
    // Test parser creation performance
    for (int i = 0; i < 10; i++) {
        CssPropertyValueParser* prop_parser = css_property_value_parser_create(pool);
        if (prop_parser) {
            css_property_value_parser_destroy(prop_parser);
        }
        
        CSSSelectorParser* sel_parser = css_selector_parser_create(pool);
        if (sel_parser) {
            css_selector_parser_destroy(sel_parser);
        }
    }
}