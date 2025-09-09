#include <criterion/criterion.h>
#include <string>
#include "../lambda/input/css_parser.h"
#include "../lambda/input/css_tokenizer.h"
#include "../lambda/input/css_properties.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Global test variables
static VariableMemPool* pool;
static css_parser_t* parser;

// Setup function for all tests
void setup(void) {
    pool_variable_init(&pool, 1024 * 1024, MEM_POOL_NO_BEST_FIT); // 1MB pool
    parser = css_parser_create(pool);
    css_parser_set_strict_mode(parser, false); // Disable strict mode for integration tests
}

// Teardown function for all tests
void teardown(void) {
    if (parser) {
        css_parser_destroy(parser);
    }
    if (pool) {
        pool_variable_destroy(pool);
    }
}

// Test end-to-end parsing of a complete CSS stylesheet
Test(css_integration, end_to_end_stylesheet_parsing, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    cr_expect_gt(stylesheet->rule_count, 8, "Should have more than 8 rules");
    
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
    
    cr_expect(has_style_rule, "Should have style rules");
    cr_expect(has_media_rule, "Should have media rules");
    cr_expect(has_keyframes_rule, "Should have keyframes rules");
}

// Test tokenizer and parser integration with complex selectors
Test(css_integration, complex_selector_parsing, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    cr_expect_eq(stylesheet->rule_count, 4, "Should have 4 rules");
    
    // Check first rule has complex selector
    css_rule_t* rule = stylesheet->rules;
    cr_assert_eq(rule->type, CSS_RULE_STYLE, "First rule should be style rule");
    
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    cr_assert_neq(selector, NULL, "Selector should not be NULL");
    
    // Should have multiple components in the selector
    int component_count = 0;
    css_selector_component_t* component = selector->components;
    while (component) {
        component_count++;
        component = component->next;
    }
    cr_expect_gt(component_count, 1, "Complex selector should have multiple components");
    
    // Check selector list in second rule
    rule = rule->next;
    cr_assert_eq(rule->type, CSS_RULE_STYLE, "Second rule should be style rule");
    style_rule = rule->data.style_rule;
    
    // Should have two selectors in the list
    selector = style_rule->selectors;
    cr_assert_neq(selector, NULL, "First selector should not be NULL");
    cr_assert_neq(selector->next, NULL, "Second selector should not be NULL");
    cr_expect_eq(selector->next->next, NULL, "Should have only two selectors");
}

// Test property validation integration
Test(css_integration, property_validation_integration, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 2, "Should have 2 rules");
    
    // Check first rule declarations
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    cr_expect_eq(style_rule->declaration_count, 9, "First rule should have 9 declarations");
    
    // Verify some declarations are properly parsed
    bool found_color = false;
    bool found_margin = false;
    bool found_display = false;
    
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        if (strcmp(decl->property, "color") == 0) {
            found_color = true;
            cr_expect_str_eq(decl->value_tokens[0].value, "red", "Color value should be 'red'");
        } else if (strcmp(decl->property, "margin") == 0) {
            found_margin = true;
            printf("DEBUG: Margin has %d tokens: ", decl->token_count);
            for (int j = 0; j < decl->token_count; j++) {
                printf("'%s' ", decl->value_tokens[j].value ? decl->value_tokens[j].value : "NULL");
            }
            printf("\n");
            cr_expect_eq(decl->token_count, 2, "Margin should have 2 tokens (10px 20px)");
        } else if (strcmp(decl->property, "display") == 0) {
            found_display = true;
            cr_expect_str_eq(decl->value_tokens[0].value, "flex", "Display value should be 'flex'");
        }
    }
    
    cr_expect(found_color, "Should find color declaration");
    cr_expect(found_margin, "Should find margin declaration");
    cr_expect(found_display, "Should find display declaration");
    
    // Check second rule has !important declaration
    rule = rule->next;
    style_rule = rule->data.style_rule;
    
    bool found_important = false;
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        if (decl->importance == CSS_IMPORTANCE_IMPORTANT) {
            found_important = true;
            cr_expect_str_eq(decl->property, "color", "Important property should be 'color'");
            cr_expect_str_eq(decl->value_tokens[0].value, "blue", "Important color value should be 'blue'");
        }
    }
    cr_expect(found_important, "Should find !important declaration");
}

// Test error recovery and reporting
Test(css_integration, error_recovery_integration, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    
    // Should have some errors but still parse what it can
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors for invalid syntax");
    cr_expect_gt(stylesheet->rule_count, 0, "Should still parse valid rules");
    
    // First rule should be valid
    css_rule_t* rule = stylesheet->rules;
    if (rule && rule->type == CSS_RULE_STYLE) {
        css_style_rule_t* style_rule = rule->data.style_rule;
        cr_expect_gt(style_rule->declaration_count, 0, "Valid rule should have declarations");
    }
}

// Test memory management integration
Test(css_integration, memory_management_integration, .init = setup, .fini = teardown) {
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
        
        cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
        cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
        cr_expect_eq(stylesheet->rule_count, 1, "Should have 1 rule");
        
        css_rule_t* rule = stylesheet->rules;
        cr_assert_eq(rule->type, CSS_RULE_STYLE, "Rule should be style rule");
        
        css_style_rule_t* style_rule = rule->data.style_rule;
        cr_expect_eq(style_rule->declaration_count, 10, "Should have 10 declarations");
        
        // Memory is managed by the pool, so no explicit cleanup needed
    }
}

// Test tokenizer-parser integration with edge cases
Test(css_integration, edge_case_integration, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    cr_expect_eq(stylesheet->rule_count, 5, "Should have 5 rules");
    
    // Verify all rules parsed correctly
    css_rule_t* rule = stylesheet->rules;
    int rule_count = 0;
    while (rule) {
        cr_expect_eq(rule->type, CSS_RULE_STYLE, "All rules should be style rules");
        rule_count++;
        rule = rule->next;
    }
    cr_expect_eq(rule_count, 5, "Should count 5 rules");
}

// Test performance with larger CSS
Test(css_integration, performance_integration, .init = setup, .fini = teardown) {
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
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    cr_expect_eq(stylesheet->rule_count, 100, "Should have 100 rules");
    
    // Verify structure is correct
    css_rule_t* rule = stylesheet->rules;
    int count = 0;
    while (rule) {
        cr_expect_eq(rule->type, CSS_RULE_STYLE, "All rules should be style rules");
        css_style_rule_t* style_rule = rule->data.style_rule;
        cr_expect_eq(style_rule->declaration_count, 4, "Each rule should have 4 declarations");
        count++;
        rule = rule->next;
    }
    cr_expect_eq(count, 100, "Should count 100 rules");
}

// Criterion tests don't need a main function
