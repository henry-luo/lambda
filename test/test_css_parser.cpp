#include <gtest/gtest.h>
#include "../lambda/input/css_parser.h"
#include "../lib/mem-pool/include/mem_pool.h"

class CSSParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = variable_mem_pool_create();
        parser = css_parser_create(pool);
    }

    void TearDown() override {
        css_parser_destroy(parser);
        variable_mem_pool_destroy(pool);
    }

    VariableMemPool* pool;
    css_parser_t* parser;
};

// Test basic stylesheet parsing
TEST_F(CSSParserTest, ParseEmptyStylesheet) {
    const char* css = "";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 0);
    EXPECT_EQ(stylesheet->rules, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
}

TEST_F(CSSParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 0);
    EXPECT_EQ(stylesheet->error_count, 0);
}

// Test simple style rule parsing
TEST_F(CSSParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    
    css_style_rule_t* style_rule = rule->data.style_rule;
    ASSERT_NE(style_rule, nullptr);
    EXPECT_EQ(style_rule->declaration_count, 1);
    
    // Check selector
    css_selector_t* selector = style_rule->selectors;
    ASSERT_NE(selector, nullptr);
    EXPECT_GT(selector->specificity, 0);
    
    css_selector_component_t* component = selector->components;
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->type, CSS_SELECTOR_TYPE);
    EXPECT_STREQ(component->name, "body");
    
    // Check declaration
    css_declaration_t* decl = style_rule->declarations[0];
    ASSERT_NE(decl, nullptr);
    EXPECT_STREQ(decl->property, "color");
    EXPECT_EQ(decl->importance, CSS_IMPORTANCE_NORMAL);
    EXPECT_EQ(decl->token_count, 1);
    EXPECT_STREQ(decl->value_tokens[0].value, "red");
}

TEST_F(CSSParserTest, ParseMultipleDeclarations) {
    const char* css = "div { color: blue; font-size: 14px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    EXPECT_EQ(style_rule->declaration_count, 3);
    
    // Check first declaration
    css_declaration_t* decl1 = style_rule->declarations[0];
    EXPECT_STREQ(decl1->property, "color");
    EXPECT_STREQ(decl1->value_tokens[0].value, "blue");
    
    // Check second declaration
    css_declaration_t* decl2 = style_rule->declarations[1];
    EXPECT_STREQ(decl2->property, "font-size");
    EXPECT_STREQ(decl2->value_tokens[0].value, "14");
    EXPECT_STREQ(decl2->value_tokens[1].value, "px");
    
    // Check third declaration
    css_declaration_t* decl3 = style_rule->declarations[2];
    EXPECT_STREQ(decl3->property, "margin");
    EXPECT_STREQ(decl3->value_tokens[0].value, "10");
    EXPECT_STREQ(decl3->value_tokens[1].value, "px");
}

// Test selector parsing
TEST_F(CSSParserTest, ParseClassSelector) {
    const char* css = ".container { width: 100%; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    EXPECT_EQ(component->type, CSS_SELECTOR_CLASS);
    EXPECT_STREQ(component->name, "container");
    EXPECT_EQ(selector->specificity, 10); // Class selector has specificity 10
}

TEST_F(CSSParserTest, ParseIdSelector) {
    const char* css = "#header { height: 80px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    EXPECT_EQ(component->type, CSS_SELECTOR_ID);
    EXPECT_STREQ(component->name, "header");
    EXPECT_EQ(selector->specificity, 100); // ID selector has specificity 100
}

TEST_F(CSSParserTest, ParseUniversalSelector) {
    const char* css = "* { box-sizing: border-box; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    EXPECT_EQ(component->type, CSS_SELECTOR_UNIVERSAL);
    EXPECT_STREQ(component->name, "*");
}

TEST_F(CSSParserTest, ParseAttributeSelector) {
    const char* css = "[type=\"text\"] { border: 1px solid gray; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;
    
    EXPECT_EQ(component->type, CSS_SELECTOR_ATTRIBUTE);
    EXPECT_STREQ(component->name, "type");
    EXPECT_STREQ(component->operator, "=");
    EXPECT_STREQ(component->value, "\"text\"");
}

TEST_F(CSSParserTest, ParsePseudoClassSelector) {
    const char* css = "a:hover { color: blue; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Should have two components: 'a' and ':hover'
    css_selector_component_t* component1 = selector->components;
    EXPECT_EQ(component1->type, CSS_SELECTOR_TYPE);
    EXPECT_STREQ(component1->name, "a");
    
    css_selector_component_t* component2 = component1->next;
    ASSERT_NE(component2, nullptr);
    EXPECT_EQ(component2->type, CSS_SELECTOR_PSEUDO_CLASS);
    EXPECT_STREQ(component2->name, "hover");
    
    EXPECT_EQ(selector->specificity, 11); // Type (1) + pseudo-class (10)
}

TEST_F(CSSParserTest, ParseSelectorList) {
    const char* css = "h1, h2, h3 { font-weight: bold; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    
    // Check first selector
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE);
    EXPECT_STREQ(selector->components->name, "h1");
    
    // Check second selector
    selector = selector->next;
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE);
    EXPECT_STREQ(selector->components->name, "h2");
    
    // Check third selector
    selector = selector->next;
    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE);
    EXPECT_STREQ(selector->components->name, "h3");
    
    // Should be no more selectors
    EXPECT_EQ(selector->next, nullptr);
}

// Test important declarations
TEST_F(CSSParserTest, ParseImportantDeclaration) {
    const char* css = "p { color: red !important; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_declaration_t* decl = style_rule->declarations[0];
    
    EXPECT_EQ(decl->importance, CSS_IMPORTANCE_IMPORTANT);
    EXPECT_STREQ(decl->property, "color");
    EXPECT_EQ(decl->token_count, 1); // !important should be removed from value tokens
    EXPECT_STREQ(decl->value_tokens[0].value, "red");
}

// Test at-rule parsing
TEST_F(CSSParserTest, ParseMediaRule) {
    const char* css = "@media screen and (max-width: 768px) { body { font-size: 14px; } }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE);
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    ASSERT_NE(at_rule, nullptr);
    EXPECT_EQ(at_rule->type, CSS_AT_RULE_MEDIA);
    EXPECT_STREQ(at_rule->name, "@media");
}

TEST_F(CSSParserTest, ParseImportRule) {
    const char* css = "@import url('styles.css');";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 1);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE);
    
    css_at_rule_t* at_rule = rule->data.at_rule;
    ASSERT_NE(at_rule, nullptr);
    EXPECT_EQ(at_rule->type, CSS_AT_RULE_IMPORT);
    EXPECT_STREQ(at_rule->name, "@import");
}

// Test multiple rules
TEST_F(CSSParserTest, ParseMultipleRules) {
    const char* css = R"(
        body { margin: 0; padding: 0; }
        .container { width: 100%; }
        #header { height: 80px; }
        @media screen { body { font-size: 16px; } }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 4);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    // Check rule types
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    
    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    
    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    
    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE);
}

// Test comment preservation
TEST_F(CSSParserTest, ParseWithComments) {
    css_parser_set_preserve_comments(parser, true);
    
    const char* css = R"(
        /* Global styles */
        body { margin: 0; }
        /* Container styles */
        .container { width: 100%; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->rule_count, 4); // 2 comments + 2 style rules
    EXPECT_EQ(stylesheet->error_count, 0);
    
    // Check that comments are preserved
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_COMMENT);
    EXPECT_STRNE(rule->data.comment, nullptr);
}

// Test error handling
TEST_F(CSSParserTest, ParseInvalidSelector) {
    const char* css = "{ color: red; }"; // Missing selector
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->error_count, 0);
}

TEST_F(CSSParserTest, ParseMissingBrace) {
    const char* css = "body { color: red;"; // Missing closing brace
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->error_count, 0);
}

TEST_F(CSSParserTest, ParseMissingColon) {
    const char* css = "body { color red; }"; // Missing colon
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->error_count, 0);
}

TEST_F(CSSParserTest, ParseInvalidProperty) {
    const char* css = "body { 123invalid: red; }"; // Invalid property name
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->error_count, 0);
}

// Test complex CSS
TEST_F(CSSParserTest, ParseComplexCSS) {
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
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->rule_count, 5);
    EXPECT_EQ(stylesheet->error_count, 0);
}

// Test property validation
TEST_F(CSSParserTest, ValidateKnownProperties) {
    const char* css = "div { color: red; width: 100px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
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
TEST_F(CSSParserTest, CalculateSpecificity) {
    const char* css = R"(
        * { color: red; }
        div { color: blue; }
        .class { color: green; }
        #id { color: yellow; }
        div.class { color: purple; }
        #id.class { color: orange; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_EQ(stylesheet->error_count, 0);
    
    css_rule_t* rule = stylesheet->rules;
    
    // Universal selector: specificity 0
    css_selector_t* selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 0);
    
    // Type selector: specificity 1
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 1);
    
    // Class selector: specificity 10
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 10);
    
    // ID selector: specificity 100
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 100);
    
    // Type + class: specificity 11
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 11);
    
    // ID + class: specificity 110
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 110);
}

// Test strict mode
TEST_F(CSSParserTest, StrictModeStopsOnError) {
    css_parser_set_strict_mode(parser, true);
    
    const char* css = R"(
        body { color: red; }
        invalid { syntax
        p { font-size: 14px; }
    )";
    
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);
    
    ASSERT_NE(stylesheet, nullptr);
    EXPECT_GT(stylesheet->error_count, 0);
    // In strict mode, parsing should stop after the first error
    // so we shouldn't get the 'p' rule
    EXPECT_LT(stylesheet->rule_count, 3);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
