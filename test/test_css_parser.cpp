#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../lambda/input/css_parser.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Global variables for setup/teardown
static VariableMemPool* pool;
static css_parser_t* parser;

void setup(void) {
    MemPoolError err = pool_variable_init(&pool, 1024 * 1024, 10);  // 1MB pool
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Failed to create memory pool");
    parser = css_parser_create(pool);
    cr_assert_neq(parser, NULL, "Failed to create CSS parser");
}

void teardown(void) {
    if (parser) {
        css_parser_destroy(parser);
    }
    if (pool) {
        pool_variable_destroy(pool);
    }
}

// Test basic stylesheet parsing
Test(css_parser, parse_empty_stylesheet, .init = setup, .fini = teardown) {
    const char* css = "";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 0, "Empty stylesheet should have 0 rules");
    cr_expect_eq(stylesheet->rules, NULL, "Empty stylesheet should have NULL rules");
    cr_expect_eq(stylesheet->error_count, 0, "Empty stylesheet should have 0 errors");
}

Test(css_parser, parse_whitespace_only_stylesheet, .init = setup, .fini = teardown) {
    const char* css = "   \n\t  \r\n  ";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 0, "Whitespace-only stylesheet should have 0 rules");
    cr_expect_eq(stylesheet->error_count, 0, "Whitespace-only stylesheet should have 0 errors");
}

// Test simple style rule parsing
Test(css_parser, parse_simple_style_rule, .init = setup, .fini = teardown) {
    const char* css = "body { color: red; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 1, "Should have 1 rule");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    cr_assert_neq(rule, NULL, "Rule should not be NULL");
    cr_expect_eq(rule->type, CSS_RULE_STYLE, "Rule should be style rule");
    
    css_style_rule_t* style_rule = rule->data.style_rule;
    cr_assert_neq(style_rule, NULL, "Style rule should not be NULL");
    cr_expect_eq(style_rule->declaration_count, 1, "Should have 1 declaration");
    
    // Check selector
    css_selector_t* selector = style_rule->selectors;
    cr_assert_neq(selector, NULL, "Selector should not be NULL");
    cr_expect_gt(selector->specificity, 0, "Selector should have specificity > 0");
    
    css_selector_component_t* component = selector->components;
    cr_assert_neq(component, NULL, "Selector component should not be NULL");
    cr_expect_eq(component->type, CSS_SELECTOR_TYPE, "Component should be type selector");
    cr_expect_str_eq(component->name, "body", "Component name should be 'body'");
    
    // Check declaration
    css_declaration_t* decl = style_rule->declarations[0];
    cr_assert_neq(decl, NULL, "Declaration should not be NULL");
    cr_expect_str_eq(decl->property, "color", "Property should be 'color'");
    cr_expect_eq(decl->importance, CSS_IMPORTANCE_NORMAL, "Importance should be normal");
    cr_expect_eq(decl->token_count, 1, "Should have 1 value token");
    cr_expect_str_eq(decl->value_tokens[0].value, "red", "Value should be 'red'");
}

Test(css_parser, parse_multiple_declarations, .init = setup, .fini = teardown) {
    const char* css = "div { color: blue; font-size: 14px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 1, "Should have 1 rule");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    cr_expect_eq(style_rule->declaration_count, 3, "Should have 3 declarations");
    
    // Check first declaration
    css_declaration_t* decl1 = style_rule->declarations[0];
    cr_expect_str_eq(decl1->property, "color", "First property should be 'color'");
    cr_expect_str_eq(decl1->value_tokens[0].value, "blue", "First value should be 'blue'");
    
    // Check second declaration
    css_declaration_t* decl2 = style_rule->declarations[1];
    cr_expect_str_eq(decl2->property, "font-size", "Second property should be 'font-size'");
    cr_expect_str_eq(decl2->value_tokens[0].value, "14", "Second value first token should be '14'");
    cr_expect_str_eq(decl2->value_tokens[1].value, "px", "Second value second token should be 'px'");
    
    // Check third declaration
    css_declaration_t* decl3 = style_rule->declarations[2];
    cr_expect_str_eq(decl3->property, "margin", "Third property should be 'margin'");
    
    // Margin properties preserve dimension tokens as single tokens
    cr_expect_eq(decl3->token_count, 1, "Margin should have 1 dimension token");
    cr_expect_str_eq(decl3->value_tokens[0].value, "10px", "Third value should be '10px'");
}

// Test selector parsing
Test(css_parser, parse_class_selector, .init = setup, .fini = teardown) {
    const char* css = ".container { width: 100%; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    cr_expect_eq(component->type, CSS_SELECTOR_CLASS, "Component should be class selector");
    cr_expect_str_eq(component->name, "container", "Class name should be 'container'");
    cr_expect_eq(selector->specificity, 10, "Class selector should have specificity 10");
}

Test(css_parser, parse_id_selector, .init = setup, .fini = teardown) {
    const char* css = "#header { height: 80px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    cr_expect_eq(component->type, CSS_SELECTOR_ID, "Component should be ID selector");
    cr_expect_str_eq(component->name, "header", "ID name should be 'header'");
    cr_expect_eq(selector->specificity, 100, "ID selector should have specificity 100");
}

Test(css_parser, parse_universal_selector, .init = setup, .fini = teardown) {
    const char* css = "* { box-sizing: border-box; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    cr_expect_eq(component->type, CSS_SELECTOR_UNIVERSAL, "Component should be universal selector");
    cr_expect_str_eq(component->name, "*", "Universal selector name should be '*'");
}

Test(css_parser, parse_attribute_selector, .init = setup, .fini = teardown) {
    const char* css = "[type=\"text\"] { border: 1px solid gray; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    cr_expect_eq(component->type, CSS_SELECTOR_ATTRIBUTE, "Component should be attribute selector");
    cr_expect_str_eq(component->name, "type", "Attribute name should be 'type'");
    cr_expect_str_eq(component->attr_operator, "=", "Attribute operator should be '='");
    cr_expect_str_eq(component->value, "\"text\"", "Attribute value should be '\"text\"'");
}

Test(css_parser, parse_pseudo_class_selector, .init = setup, .fini = teardown) {
    const char* css = "a:hover { color: blue; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Should have two components: 'a' and ':hover'
    css_selector_component_t* component1 = selector->components;
    cr_expect_eq(component1->type, CSS_SELECTOR_TYPE, "First component should be type selector");
    cr_expect_str_eq(component1->name, "a", "First component name should be 'a'");
    
    css_selector_component_t* component2 = component1->next;
    cr_assert_neq(component2, NULL, "Second component should not be NULL");
    cr_expect_eq(component2->type, CSS_SELECTOR_PSEUDO_CLASS, "Second component should be pseudo-class selector");
    cr_expect_str_eq(component2->name, "hover", "Second component name should be 'hover'");
    
    cr_expect_eq(selector->specificity, 11, "Selector specificity should be 11 (Type 1 + pseudo-class 10)");
}

Test(css_parser, parse_selector_list, .init = setup, .fini = teardown) {
    const char* css = "h1, h2, h3 { font-weight: bold; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Check first selector
    cr_assert_neq(selector, NULL, "First selector should not be NULL");
    cr_expect_eq(selector->components->type, CSS_SELECTOR_TYPE, "First selector should be type selector");
    cr_expect_str_eq(selector->components->name, "h1", "First selector name should be 'h1'");
    
    // Check second selector
    selector = selector->next;
    cr_assert_neq(selector, NULL, "Second selector should not be NULL");
    cr_expect_eq(selector->components->type, CSS_SELECTOR_TYPE, "Second selector should be type selector");
    cr_expect_str_eq(selector->components->name, "h2", "Second selector name should be 'h2'");
    
    // Check third selector
    selector = selector->next;
    cr_assert_neq(selector, NULL, "Third selector should not be NULL");
    cr_expect_eq(selector->components->type, CSS_SELECTOR_TYPE, "Third selector should be type selector");
    cr_expect_str_eq(selector->components->name, "h3", "Third selector name should be 'h3'");
    
    // Should be no more selectors
    cr_expect_eq(selector->next, NULL, "Should be no more selectors");
}

// Test important declarations
Test(css_parser, parse_important_declaration, .init = setup, .fini = teardown) {
    const char* css = "p { color: red !important; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_declaration_t* decl = style_rule->declarations[0];
    
    cr_expect_eq(decl->importance, CSS_IMPORTANCE_IMPORTANT, "Declaration should be marked as important");
    cr_expect_str_eq(decl->property, "color", "Property should be 'color'");
    cr_expect_eq(decl->token_count, 1, "Should have 1 value token (!important should be removed)");
    cr_expect_str_eq(decl->value_tokens[0].value, "red", "Value should be 'red'");
}

// Test at-rule parsing
Test(css_parser, parse_media_rule, .init = setup, .fini = teardown) {
    const char* css = "@media screen and (max-width: 768px) { body { font-size: 14px; } }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 1, "Should have 1 rule");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    cr_expect_eq(rule->type, CSS_RULE_AT_RULE, "Rule should be at-rule");
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    cr_assert_neq(at_rule, NULL, "At-rule should not be NULL");
    cr_expect_eq(at_rule->type, CSS_AT_RULE_MEDIA, "At-rule should be media rule");
    cr_expect_str_eq(at_rule->name, "@media", "At-rule name should be '@media'");
}

Test(css_parser, parse_import_rule, .init = setup, .fini = teardown) {
    const char* css = "@import url('styles.css');";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 1, "Should have 1 rule");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    cr_expect_eq(rule->type, CSS_RULE_AT_RULE, "Rule should be at-rule");
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    cr_assert_neq(at_rule, NULL, "At-rule should not be NULL");
    cr_expect_eq(at_rule->type, CSS_AT_RULE_IMPORT, "At-rule should be import rule");
    cr_expect_str_eq(at_rule->name, "@import", "At-rule name should be '@import'");
}

// Test multiple rules
Test(css_parser, parse_multiple_rules, .init = setup, .fini = teardown) {
    const char* css = R"(
        body { margin: 0; padding: 0; }
        .container { width: 100%; }
        #header { height: 80px; }
        @media screen { body { font-size: 16px; } }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 4, "Should have 4 rules");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    // Check rule types
    css_rule_t* rule = stylesheet->rules;
    cr_expect_eq(rule->type, CSS_RULE_STYLE, "First rule should be style rule");
    
    rule = rule->next;
    cr_expect_eq(rule->type, CSS_RULE_STYLE, "Second rule should be style rule");
    
    rule = rule->next;
    cr_expect_eq(rule->type, CSS_RULE_STYLE, "Third rule should be style rule");
    
    rule = rule->next;
    cr_expect_eq(rule->type, CSS_RULE_AT_RULE, "Fourth rule should be at-rule");
}

// Test comment preservation
Test(css_parser, parse_with_comments, .init = setup, .fini = teardown) {
    css_parser_set_preserve_comments(parser, true);
    
    const char* css = R"(
        /* Global styles */
        body { margin: 0; }
        /* Container styles */
        .container { width: 100%; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->rule_count, 4, "Should have 4 rules (2 comments + 2 style rules)");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    // Check that comments are preserved
    css_rule_t* rule = stylesheet->rules;
    cr_expect_eq(rule->type, CSS_RULE_COMMENT, "First rule should be comment");
    cr_expect_neq(rule->data.comment, NULL, "Comment data should not be NULL");
}

// Test error handling
Test(css_parser, parse_invalid_selector, .init = setup, .fini = teardown) {
    const char* css = "{ color: red; }"; // Missing selector
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors for missing selector");
}

Test(css_parser, parse_missing_brace, .init = setup, .fini = teardown) {
    const char* css = "body { color: red;"; // Missing closing brace
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors for missing closing brace");
}

Test(css_parser, parse_missing_colon, .init = setup, .fini = teardown) {
    const char* css = "body { color red; }"; // Missing colon
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors for missing colon");
}

Test(css_parser, parse_invalid_property, .init = setup, .fini = teardown) {
    const char* css = "body { 123invalid: red; }"; // Invalid property name
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors for invalid property name");
}

// Test complex CSS
Test(css_parser, parse_complex_css, .init = setup, .fini = teardown) {
    const char* css = R"(
        @charset "UTF-8";
        @import url('reset.css');
        
        * {
            box-sizing: border-box;
        }
        
        body, html {
            margin: 0;
            padding: 0;
            font-family: Arial, sans-serif;
            line-height: 1.6;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 0 20px;
        }
        
        .header {
            background-color: #333;
            color: white;
            padding: 1rem 0;
        }
        
        .nav ul {
            list-style: none;
            display: flex;
            gap: 2rem;
        }
        
        .nav a:hover {
            color: #007bff;
            text-decoration: underline;
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
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->rule_count, 5, "Should have more than 5 rules");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
}

// Test property validation
Test(css_parser, validate_known_properties, .init = setup, .fini = teardown) {
    const char* css = "div { color: red; width: 100px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    
    // All declarations should be valid for known properties
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        // The parser should have attempted validation
        // (actual validation depends on property database implementation)
    }
}

// Test specificity calculation
Test(css_parser, calculate_specificity, .init = setup, .fini = teardown) {
    const char* css = R"(
        * { color: red; }
        div { color: blue; }
        .class { color: green; }
        #id { color: yellow; }
        div.class { color: purple; }
        #id.class { color: orange; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_eq(stylesheet->error_count, 0, "Should have 0 errors");
    
    css_rule_t* rule = stylesheet->rules;
    
    // Universal selector: specificity 0
    css_selector_t* selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 0, "Universal selector should have specificity 0");
    
    // Type selector: specificity 1
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 1, "Type selector should have specificity 1");
    
    // Class selector: specificity 10
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 10, "Class selector should have specificity 10");
    
    // ID selector: specificity 100
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 100, "ID selector should have specificity 100");
    
    // Type + class: specificity 11
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 11, "Type + class selector should have specificity 11");
    
    // ID + class: specificity 110
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    cr_expect_eq(selector->specificity, 110, "ID + class selector should have specificity 110");
}

// Test strict mode
Test(css_parser, strict_mode_stops_on_error, .init = setup, .fini = teardown) {
    css_parser_set_strict_mode(parser, true);
    
    const char* css = R"(
        body { color: red; }
        invalid { syntax
        p { font-size: 14px; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    cr_assert_neq(stylesheet, NULL, "Stylesheet should not be NULL");
    cr_expect_gt(stylesheet->error_count, 0, "Should have errors in strict mode");
    // In strict mode, parsing should stop after the first error
    // so we shouldn't get the 'p' rule
    cr_expect_lt(stylesheet->rule_count, 3, "Should have fewer than 3 rules due to strict mode stopping on error");
}

// Criterion tests don't need a main function
