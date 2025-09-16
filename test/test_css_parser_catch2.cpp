#include <catch2/catch_test_macros.hpp>
#include <cstring>

extern "C" {
#include "../lambda/input/css_parser.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

// Global variables for setup/teardown
static VariableMemPool* pool = nullptr;
static css_parser_t* parser = nullptr;

void setup_css_parser() {
    if (!pool) {
        MemPoolError err = pool_variable_init(&pool, 1024 * 1024, 10);  // 1MB pool
        REQUIRE(err == MEM_POOL_ERR_OK);
        parser = css_parser_create(pool);
        REQUIRE(parser != nullptr);
    }
}

void teardown_css_parser() {
    if (parser) {
        css_parser_destroy(parser);
        parser = nullptr;
    }
    if (pool) {
        pool_variable_destroy(pool);
        pool = nullptr;
    }
}

TEST_CASE("CSS Parser - Empty Stylesheet", "[css][parser][empty]") {
    setup_css_parser();
    
    const char* css = "";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 0);
    REQUIRE(stylesheet->rules == nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Whitespace Only Stylesheet", "[css][parser][whitespace]") {
    setup_css_parser();
    
    const char* css = "   \n\t  \r\n  ";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 0);
    REQUIRE(stylesheet->error_count == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Simple Style Rule", "[css][parser][simple]") {
    setup_css_parser();
    
    const char* css = "body { color: red; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 1);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule != nullptr);
    REQUIRE(rule->type == CSS_RULE_STYLE);
    
    css_style_rule_t* style_rule = rule->data.style_rule;
    REQUIRE(style_rule != nullptr);
    REQUIRE(style_rule->declaration_count == 1);
    
    // Check selector
    css_selector_t* selector = style_rule->selectors;
    REQUIRE(selector != nullptr);
    REQUIRE(selector->specificity > 0);
    
    css_selector_component_t* component = selector->components;
    REQUIRE(component != nullptr);
    REQUIRE(component->type == CSS_SELECTOR_TYPE);
    REQUIRE(strcmp(component->name, "body") == 0);
    
    // Check declaration
    css_declaration_t* decl = style_rule->declarations[0];
    REQUIRE(decl != nullptr);
    REQUIRE(strcmp(decl->property, "color") == 0);
    REQUIRE(decl->importance == CSS_IMPORTANCE_NORMAL);
    REQUIRE(decl->token_count == 1);
    REQUIRE(strcmp(decl->value_tokens[0].value, "red") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Multiple Declarations", "[css][parser][multiple_declarations]") {
    setup_css_parser();
    
    const char* css = "div { color: blue; font-size: 14px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 1);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    REQUIRE(style_rule->declaration_count == 3);
    
    // Check first declaration
    css_declaration_t* decl1 = style_rule->declarations[0];
    REQUIRE(strcmp(decl1->property, "color") == 0);
    REQUIRE(strcmp(decl1->value_tokens[0].value, "blue") == 0);
    
    // Check second declaration
    css_declaration_t* decl2 = style_rule->declarations[1];
    REQUIRE(strcmp(decl2->property, "font-size") == 0);
    REQUIRE(strcmp(decl2->value_tokens[0].value, "14") == 0);
    REQUIRE(strcmp(decl2->value_tokens[1].value, "px") == 0);
    
    // Check third declaration
    css_declaration_t* decl3 = style_rule->declarations[2];
    REQUIRE(strcmp(decl3->property, "margin") == 0);
    
    // Margin properties preserve dimension tokens as single tokens
    REQUIRE(decl3->token_count == 1);
    REQUIRE(strcmp(decl3->value_tokens[0].value, "10px") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Class Selector", "[css][parser][class_selector]") {
    setup_css_parser();
    
    const char* css = ".container { width: 100%; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    REQUIRE(component->type == CSS_SELECTOR_CLASS);
    REQUIRE(strcmp(component->name, "container") == 0);
    REQUIRE(selector->specificity == 10);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - ID Selector", "[css][parser][id_selector]") {
    setup_css_parser();
    
    const char* css = "#header { height: 80px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    REQUIRE(component->type == CSS_SELECTOR_ID);
    REQUIRE(strcmp(component->name, "header") == 0);
    REQUIRE(selector->specificity == 100);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Universal Selector", "[css][parser][universal_selector]") {
    setup_css_parser();
    
    const char* css = "* { box-sizing: border-box; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    REQUIRE(component->type == CSS_SELECTOR_UNIVERSAL);
    REQUIRE(strcmp(component->name, "*") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Attribute Selector", "[css][parser][attribute_selector]") {
    setup_css_parser();
    
    const char* css = "[type=\"text\"] { border: 1px solid gray; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    REQUIRE(component->type == CSS_SELECTOR_ATTRIBUTE);
    REQUIRE(strcmp(component->name, "type") == 0);
    REQUIRE(strcmp(component->attr_operator, "=") == 0);
    REQUIRE(strcmp(component->value, "\"text\"") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Pseudo Class Selector", "[css][parser][pseudo_class]") {
    setup_css_parser();
    
    const char* css = "a:hover { color: blue; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Should have two components: 'a' and ':hover'
    css_selector_component_t* component1 = selector->components;
    REQUIRE(component1->type == CSS_SELECTOR_TYPE);
    REQUIRE(strcmp(component1->name, "a") == 0);
    
    css_selector_component_t* component2 = component1->next;
    REQUIRE(component2 != nullptr);
    REQUIRE(component2->type == CSS_SELECTOR_PSEUDO_CLASS);
    REQUIRE(strcmp(component2->name, "hover") == 0);
    
    REQUIRE(selector->specificity == 11); // Type 1 + pseudo-class 10
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Selector List", "[css][parser][selector_list]") {
    setup_css_parser();
    
    const char* css = "h1, h2, h3 { font-weight: bold; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Check first selector
    REQUIRE(selector != nullptr);
    REQUIRE(selector->components->type == CSS_SELECTOR_TYPE);
    REQUIRE(strcmp(selector->components->name, "h1") == 0);
    
    // Check second selector
    selector = selector->next;
    REQUIRE(selector != nullptr);
    REQUIRE(selector->components->type == CSS_SELECTOR_TYPE);
    REQUIRE(strcmp(selector->components->name, "h2") == 0);
    
    // Check third selector
    selector = selector->next;
    REQUIRE(selector != nullptr);
    REQUIRE(selector->components->type == CSS_SELECTOR_TYPE);
    REQUIRE(strcmp(selector->components->name, "h3") == 0);
    
    // Should be no more selectors
    REQUIRE(selector->next == nullptr);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Important Declaration", "[css][parser][important]") {
    setup_css_parser();
    
    const char* css = "p { color: red !important; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_declaration_t* decl = style_rule->declarations[0];
    
    REQUIRE(decl->importance == CSS_IMPORTANCE_IMPORTANT);
    REQUIRE(strcmp(decl->property, "color") == 0);
    REQUIRE(decl->token_count == 1); // !important should be removed
    REQUIRE(strcmp(decl->value_tokens[0].value, "red") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Media Rule", "[css][parser][media_rule]") {
    setup_css_parser();
    
    const char* css = "@media screen and (max-width: 768px) { body { font-size: 14px; } }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 1);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule->type == CSS_RULE_AT_RULE);
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    REQUIRE(at_rule != nullptr);
    REQUIRE(at_rule->type == CSS_AT_RULE_MEDIA);
    REQUIRE(strcmp(at_rule->name, "@media") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Import Rule", "[css][parser][import_rule]") {
    setup_css_parser();
    
    const char* css = "@import url('styles.css');";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 1);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule->type == CSS_RULE_AT_RULE);
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    REQUIRE(at_rule != nullptr);
    REQUIRE(at_rule->type == CSS_AT_RULE_IMPORT);
    REQUIRE(strcmp(at_rule->name, "@import") == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Multiple Rules", "[css][parser][multiple_rules]") {
    setup_css_parser();
    
    const char* css = R"(
        body { margin: 0; padding: 0; }
        .container { width: 100%; }
        #header { height: 80px; }
        @media screen { body { font-size: 16px; } }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 4);
    REQUIRE(stylesheet->error_count == 0);
    
    // Check rule types
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule->type == CSS_RULE_STYLE);
    
    rule = rule->next;
    REQUIRE(rule->type == CSS_RULE_STYLE);
    
    rule = rule->next;
    REQUIRE(rule->type == CSS_RULE_STYLE);
    
    rule = rule->next;
    REQUIRE(rule->type == CSS_RULE_AT_RULE);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Comments Preservation", "[css][parser][comments]") {
    setup_css_parser();
    
    css_parser_set_preserve_comments(parser, true);
    
    const char* css = R"(
        /* Global styles */
        body { margin: 0; }
        /* Container styles */
        .container { width: 100%; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count == 4); // 2 comments + 2 style rules
    REQUIRE(stylesheet->error_count == 0);
    
    // Check that comments are preserved
    css_rule_t* rule = stylesheet->rules;
    REQUIRE(rule->type == CSS_RULE_COMMENT);
    REQUIRE(rule->data.comment != nullptr);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Error Handling", "[css][parser][errors]") {
    setup_css_parser();
    
    SECTION("Invalid selector") {
        const char* css = "{ color: red; }"; // Missing selector
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        REQUIRE(stylesheet != nullptr);
        REQUIRE(stylesheet->error_count > 0);
    }
    
    SECTION("Missing brace") {
        const char* css = "body { color: red;"; // Missing closing brace
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        REQUIRE(stylesheet != nullptr);
        REQUIRE(stylesheet->error_count > 0);
    }
    
    SECTION("Missing colon") {
        const char* css = "body { color red; }"; // Missing colon
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        REQUIRE(stylesheet != nullptr);
        REQUIRE(stylesheet->error_count > 0);
    }
    
    SECTION("Invalid property") {
        const char* css = "body { 123invalid: red; }"; // Invalid property name
        css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
        
        REQUIRE(stylesheet != nullptr);
        REQUIRE(stylesheet->error_count > 0);
    }
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Complex CSS", "[css][parser][complex]") {
    setup_css_parser();
    
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
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->rule_count > 5);
    REQUIRE(stylesheet->error_count == 0);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Property Validation", "[css][parser][validation]") {
    setup_css_parser();
    
    const char* css = "div { color: red; width: 100px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    
    // All declarations should be valid for known properties
    for (int i = 0; i < style_rule->declaration_count; i++) {
        css_declaration_t* decl = style_rule->declarations[i];
        // The parser should have attempted validation
        // (actual validation depends on property database implementation)
        REQUIRE(decl != nullptr);
    }
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Specificity Calculation", "[css][parser][specificity]") {
    setup_css_parser();
    
    const char* css = R"(
        * { color: red; }
        div { color: blue; }
        .class { color: green; }
        #id { color: yellow; }
        div.class { color: purple; }
        #id.class { color: orange; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count == 0);
    
    css_rule_t* rule = stylesheet->rules;
    
    // Universal selector: specificity 0
    css_selector_t* selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 0);
    
    // Type selector: specificity 1
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 1);
    
    // Class selector: specificity 10
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 10);
    
    // ID selector: specificity 100
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 100);
    
    // Type + class: specificity 11
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 11);
    
    // ID + class: specificity 110
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    REQUIRE(selector->specificity == 110);
    
    teardown_css_parser();
}

TEST_CASE("CSS Parser - Strict Mode", "[css][parser][strict_mode]") {
    setup_css_parser();
    
    css_parser_set_strict_mode(parser, true);
    
    const char* css = R"(
        body { color: red; }
        invalid { syntax
        p { font-size: 14px; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    REQUIRE(stylesheet != nullptr);
    REQUIRE(stylesheet->error_count > 0);
    // In strict mode, parsing should stop after the first error
    // so we shouldn't get the 'p' rule
    REQUIRE(stylesheet->rule_count < 3);
    
    teardown_css_parser();
}
