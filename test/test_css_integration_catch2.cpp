#include <catch2/catch_test_macros.hpp>
#include <string>
#include <cstring>
#include <cstdio>

extern "C" {
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lambda/input/css_properties.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

// Global test variables
static VariableMemPool* pool = nullptr;
static css_parser_t* parser = nullptr;

void setup_css_integration() {
    if (!pool) {
        pool_variable_init(&pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT); // 1MB pool
        parser = css_parser_create(pool);
        css_parser_set_strict_mode(parser, false); // Disable strict mode for integration tests
    }
}

void teardown_css_integration() {
    if (parser) {
        css_parser_destroy(parser);
        parser = nullptr;
    }
    if (pool) {
        pool_variable_destroy(pool);
        pool = nullptr;
    }
}

TEST_CASE("CSS Integration - End-to-End Stylesheet Parsing", "[css][integration][end_to_end]") {
    setup_css_integration();
    
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
            text-align: center;
        }
        
        .nav ul {
            list-style: none;
            display: flex;
            justify-content: center;
            gap: 2rem;
        }
        
        .nav a {
            color: white;
            text-decoration: none;
            font-weight: 500;
            transition: color 0.3s ease;
        }
        
        .nav a:hover,
        .nav a:focus {
            color: #ffd700;
        }
        
        @media (max-width: 768px) {
            .container {
                padding: 0 15px;
            }
            
            .nav ul {
                flex-direction: column;
                gap: 1rem;
            }
        }
        
        @keyframes fadeIn {
            from { opacity: 0; }
            to { opacity: 1; }
        }
    )";
    
    // Parse the complete stylesheet
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    REQUIRE(stylesheet->rule_count > 8);
    
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
    
    REQUIRE(has_style_rule);
    REQUIRE(has_media_rule);
    REQUIRE(has_keyframes_rule);
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Complex Selector Parsing", "[css][integration][complex_selectors]") {
    setup_css_integration();
    
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
            font-size: 1.2em;
            font-weight: 300;
        }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    REQUIRE(stylesheet->rule_count == 4);
    
    // Check first rule has complex selector
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule->type == CSS_RULE_STYLE);
    
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    REQUIRE(selector != nullptr);
    
    // Should have multiple components in the selector
    int component_count = 0;
    css_selector_component_t* component = selector->components;
    while (component) {
        component_count++;
        component = component->next;
    }
    REQUIRE(component_count > 1);
    
    // Check selector list in second rule
    rule = rule->next;
    REQUIRE(rule->type == CSS_RULE_STYLE);
    style_rule = rule->data.style_rule;
    
    // Should have two selectors in the list
    selector = style_rule->selectors;
    REQUIRE(selector != nullptr);
    REQUIRE(selector->next != nullptr);
    REQUIRE(selector->next->next == nullptr);
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Property Validation", "[css][integration][property_validation]") {
    setup_css_integration();
    
    const char* css = R"(
        .valid-properties {
            color: red;
            background-color: #ffffff;
            margin: 10px 20px;
            padding: 1em;
            font-size: 16px;
            line-height: 1.5;
            display: flex;
            position: relative;
            z-index: 100;
        }
        
        .mixed-properties {
            /* Valid properties */
            width: 100%;
            height: auto;
            
            /* Unknown property (should still parse but may not validate) */
            custom-property: some-value;
            
            /* Valid with !important */
            color: blue !important;
        }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 2);
    
    // Check first rule declarations
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    REQUIRE(style_rule->declaration_count == 9);
    
    // Verify some declarations are properly parsed
    bool found_color = false;
    bool found_margin = false;
    bool found_display = false;
    
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        if (strcmp(decl->property, "color") == 0) {
            found_color = true;
            REQUIRE(strcmp(decl->value_tokens[0].value, "red") == 0);
        } else if (strcmp(decl->property, "margin") == 0) {
            found_margin = true;
            printf("DEBUG: Margin has %d tokens: ", decl->token_count);
            for (int j = 0; j < decl->token_count; j++) {
                printf("'%s' ", decl->value_tokens[j].value ? decl->value_tokens[j].value : "NULL");
            }
            printf("\n");
            REQUIRE(decl->token_count == 2); // 10px 20px
        } else if (strcmp(decl->property, "display") == 0) {
            found_display = true;
            REQUIRE(strcmp(decl->value_tokens[0].value, "flex") == 0);
        }
    }
    
    REQUIRE(found_color);
    REQUIRE(found_margin);
    REQUIRE(found_display);
    
    // Check second rule has !important declaration
    rule = rule->next;
    style_rule = rule->data.style_rule;
    
    bool found_important = false;
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        if (decl->importance == CSS_IMPORTANCE_IMPORTANT) {
            found_important = true;
            REQUIRE(strcmp(decl->property, "color") == 0);
            REQUIRE(strcmp(decl->value_tokens[0].value, "blue") == 0);
        }
    }
    REQUIRE(found_important);
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Error Recovery", "[css][integration][error_recovery]") {
    setup_css_integration();
    
    const char* css = R"(
        /* Valid rule */
        .good-rule {
            color: green;
            margin: 10px;
        }
        
        /* Invalid rule - missing closing brace */
        .bad-rule {
            color: red;
            padding: 20px;
        /* Missing } */
        
        /* Another valid rule - should still parse */
        .another-good-rule {
            background: white;
        }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    
    // Should have some errors but still parse what it can
    REQUIRE(stylesheet->error_count > 0);
    REQUIRE(stylesheet->rule_count > 0);
    
    // First rule should be valid
    css_rule_t* rule = stylesheet->rules;
    if (rule && rule->type == CSS_RULE_STYLE) {
        css_style_rule_t* style_rule = rule->data.style_rule;
        REQUIRE(style_rule->declaration_count > 0);
    }
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Memory Management", "[css][integration][memory]") {
    setup_css_integration();
    
    const char* css = R"(
        .memory-test {
            color: red;
            background: blue;
            margin: 10px;
            padding: 5px;
            border: 1px solid black;
            font-size: 14px;
            line-height: 1.4;
            text-align: center;
            display: block;
            position: static;
        }
    )";
    
    // Parse multiple times to test memory allocation
    for (int i = 0; i < 10; i++) {
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        REQUIRE(stylesheet != nullptr);
        REQUIRE(stylesheet->error_count == 0);
        REQUIRE(stylesheet->rule_count == 1);
        
        css_rule_t* rule = stylesheet->rules;
        REQUIRE(rule->type == CSS_RULE_STYLE);
        
        css_style_rule_t* style_rule = rule->data.style_rule;
        REQUIRE(style_rule->declaration_count == 10);
        
        // Memory is managed by the pool, so no explicit cleanup needed
    }
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Edge Cases", "[css][integration][edge_cases]") {
    setup_css_integration();
    
    const char* css = R"(
        /* Edge cases */
        
        /* Empty rule */
        .empty { }
        
        /* Rule with only whitespace */
        .whitespace {
            
        }
        
        /* Rule with comments inside */
        .with-comments {
            /* This is a comment */
            color: red; /* Another comment */
            /* Final comment */
        }
        
        /* Unicode and special characters */
        .unicode-test {
            content: "→ ← ↑ ↓";
            font-family: "Helvetica Neue", Arial;
        }
        
        /* Numbers and units */
        .numbers {
            width: 100px;
            height: 50%;
            margin: 1.5em;
            padding: 0.25rem;
            border-width: 2pt;
            font-size: 14px;
        }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    REQUIRE(stylesheet->rule_count == 5);
    
    // Verify all rules parsed correctly
    css_rule_t* rule = stylesheet->rules;
    int rule_count = 0;
    while (rule) {
        REQUIRE(rule->type == CSS_RULE_STYLE);
        rule_count++;
        rule = rule->next;
    }
    REQUIRE(rule_count == 5);
    
    teardown_css_integration();
}

TEST_CASE("CSS Integration - Performance", "[css][integration][performance]") {
    setup_css_integration();
    
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
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    REQUIRE(stylesheet->rule_count == 100);
    
    // Verify structure is correct
    css_rule_t* rule = stylesheet->rules;
    int count = 0;
    while (rule) {
        REQUIRE(rule->type == CSS_RULE_STYLE);
        css_style_rule_t* style_rule = rule->data.style_rule;
        REQUIRE(style_rule->declaration_count == 4);
        count++;
        rule = rule->next;
    }
    REQUIRE(count == 100);
    
    teardown_css_integration();
}
