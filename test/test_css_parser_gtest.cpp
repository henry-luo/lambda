#include <gtest/gtest.h>
#include "../lambda/input/css_parser.h"
#include "../lib/mempool.h"

class CssParserTest : public ::testing::Test {
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
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
};

// Test basic stylesheet parsing
TEST_F(CssParserTest, ParseEmptyStylesheet) {
    const char* css = "";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Empty stylesheet should have 0 rules";
    EXPECT_EQ(stylesheet->rules, nullptr) << "Empty stylesheet should have NULL rules";
    EXPECT_EQ(stylesheet->error_count, 0) << "Empty stylesheet should have 0 errors";
}

TEST_F(CssParserTest, ParseWhitespaceOnlyStylesheet) {
    const char* css = "   \n\t  \r\n  ";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 0) << "Whitespace-only stylesheet should have 0 rules";
    EXPECT_EQ(stylesheet->error_count, 0) << "Whitespace-only stylesheet should have 0 errors";
}

// Test simple style rule parsing
TEST_F(CssParserTest, ParseSimpleStyleRule) {
    const char* css = "body { color: red; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 1) << "Should have 1 rule";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    ASSERT_NE(rule, nullptr) << "Rule should not be NULL";
    EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "Rule should be style rule";

    css_style_rule_t* style_rule = rule->data.style_rule;
    ASSERT_NE(style_rule, nullptr) << "Style rule should not be NULL";
    EXPECT_EQ(style_rule->declaration_count, 1) << "Should have 1 declaration";

    // Check selector
    css_selector_t* selector = style_rule->selectors;
    ASSERT_NE(selector, nullptr) << "Selector should not be NULL";
    EXPECT_GT(selector->specificity, 0) << "Selector should have specificity > 0";

    css_selector_component_t* component = selector->components;
    ASSERT_NE(component, nullptr) << "Selector component should not be NULL";
    EXPECT_EQ(component->type, CSS_SELECTOR_TYPE) << "Component should be type selector";
    EXPECT_STREQ(component->name, "body") << "Component name should be 'body'";

    // Check declaration
    css_declaration_t* decl = style_rule->declarations[0];
    ASSERT_NE(decl, nullptr) << "Declaration should not be NULL";
    EXPECT_STREQ(decl->property, "color") << "Property should be 'color'";
    EXPECT_EQ(decl->importance, CSS_IMPORTANCE_NORMAL) << "Importance should be normal";
    EXPECT_EQ(decl->token_count, 1) << "Should have 1 value token";
    EXPECT_STREQ(decl->value_tokens[0].value, "red") << "Value should be 'red'";
}

TEST_F(CssParserTest, ParseMultipleDeclarations) {
    const char* css = "div { color: blue; font-size: 14px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 1) << "Should have 1 rule";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    EXPECT_EQ(style_rule->declaration_count, 3) << "Should have 3 declarations";

    // Check first declaration
    css_declaration_t* decl1 = style_rule->declarations[0];
    EXPECT_STREQ(decl1->property, "color") << "First property should be 'color'";
    EXPECT_STREQ(decl1->value_tokens[0].value, "blue") << "First value should be 'blue'";

    // Check second declaration
    css_declaration_t* decl2 = style_rule->declarations[1];
    EXPECT_STREQ(decl2->property, "font-size") << "Second property should be 'font-size'";
    EXPECT_STREQ(decl2->value_tokens[0].value, "14") << "Second value first token should be '14'";
    EXPECT_STREQ(decl2->value_tokens[1].value, "px") << "Second value second token should be 'px'";

    // Check third declaration
    css_declaration_t* decl3 = style_rule->declarations[2];
    EXPECT_STREQ(decl3->property, "margin") << "Third property should be 'margin'";

    // Margin properties preserve dimension tokens as single tokens
    EXPECT_EQ(decl3->token_count, 1) << "Margin should have 1 dimension token";
    EXPECT_STREQ(decl3->value_tokens[0].value, "10px") << "Third value should be '10px'";
}

// Test selector parsing
TEST_F(CssParserTest, ParseClassSelector) {
    const char* css = ".container { width: 100%; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;

    EXPECT_EQ(component->type, CSS_SELECTOR_CLASS) << "Component should be class selector";
    EXPECT_STREQ(component->name, "container") << "Class name should be 'container'";
    EXPECT_EQ(selector->specificity, 10) << "Class selector should have specificity 10";
}

TEST_F(CssParserTest, ParseIdSelector) {
    const char* css = "#header { height: 80px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;

    EXPECT_EQ(component->type, CSS_SELECTOR_ID) << "Component should be ID selector";
    EXPECT_STREQ(component->name, "header") << "ID name should be 'header'";
    EXPECT_EQ(selector->specificity, 100) << "ID selector should have specificity 100";
}

TEST_F(CssParserTest, ParseUniversalSelector) {
    const char* css = "* { box-sizing: border-box; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;

    EXPECT_EQ(component->type, CSS_SELECTOR_UNIVERSAL) << "Component should be universal selector";
    EXPECT_STREQ(component->name, "*") << "Universal selector name should be '*'";
}

TEST_F(CssParserTest, ParseAttributeSelector) {
    const char* css = "[type=\"text\"] { border: 1px solid gray; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;
    css_selector_component_t* component = selector->components;

    EXPECT_EQ(component->type, CSS_SELECTOR_ATTRIBUTE) << "Component should be attribute selector";
    EXPECT_STREQ(component->name, "type") << "Attribute name should be 'type'";
    EXPECT_STREQ(component->attr_operator, "=") << "Attribute operator should be '='";
    EXPECT_STREQ(component->value, "\"text\"") << "Attribute value should be '\"text\"'";
}

TEST_F(CssParserTest, ParsePseudoClassSelector) {
    const char* css = "a:hover { color: blue; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;

    // Should have two components: 'a' and ':hover'
    css_selector_component_t* component1 = selector->components;
    EXPECT_EQ(component1->type, CSS_SELECTOR_TYPE) << "First component should be type selector";
    EXPECT_STREQ(component1->name, "a") << "First component name should be 'a'";

    css_selector_component_t* component2 = component1->next;
    ASSERT_NE(component2, nullptr) << "Second component should not be NULL";
    EXPECT_EQ(component2->type, CSS_SELECTOR_PSEUDO_CLASS) << "Second component should be pseudo-class selector";
    EXPECT_STREQ(component2->name, "hover") << "Second component name should be 'hover'";

    EXPECT_EQ(selector->specificity, 11) << "Selector specificity should be 11 (Type 1 + pseudo-class 10)";
}

TEST_F(CssParserTest, ParseSelectorList) {
    const char* css = "h1, h2, h3 { font-weight: bold; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_selector_t* selector = style_rule->selectors;

    // Check first selector
    ASSERT_NE(selector, nullptr) << "First selector should not be NULL";
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE) << "First selector should be type selector";
    EXPECT_STREQ(selector->components->name, "h1") << "First selector name should be 'h1'";

    // Check second selector
    selector = selector->next;
    ASSERT_NE(selector, nullptr) << "Second selector should not be NULL";
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE) << "Second selector should be type selector";
    EXPECT_STREQ(selector->components->name, "h2") << "Second selector name should be 'h2'";

    // Check third selector
    selector = selector->next;
    ASSERT_NE(selector, nullptr) << "Third selector should not be NULL";
    EXPECT_EQ(selector->components->type, CSS_SELECTOR_TYPE) << "Third selector should be type selector";
    EXPECT_STREQ(selector->components->name, "h3") << "Third selector name should be 'h3'";

    // Should be no more selectors
    EXPECT_EQ(selector->next, nullptr) << "Should be no more selectors";
}

// Test important declarations
TEST_F(CssParserTest, ParseImportantDeclaration) {
    const char* css = "p { color: red !important; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    css_style_rule_t* style_rule = rule->data.style_rule;
    css_declaration_t* decl = style_rule->declarations[0];

    EXPECT_EQ(decl->importance, CSS_IMPORTANCE_IMPORTANT) << "Declaration should be marked as important";
    EXPECT_STREQ(decl->property, "color") << "Property should be 'color'";
    EXPECT_EQ(decl->token_count, 1) << "Should have 1 value token (!important should be removed)";
    EXPECT_STREQ(decl->value_tokens[0].value, "red") << "Value should be 'red'";
}

// Test at-rule parsing
TEST_F(CssParserTest, ParseMediaRule) {
    const char* css = "@media screen and (max-width: 768px) { body { font-size: 14px; } }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 1) << "Should have 1 rule";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE) << "Rule should be at-rule";

    css_at_rule_t* at_rule = rule->data.at_rule;
    ASSERT_NE(at_rule, nullptr) << "At-rule should not be NULL";
    EXPECT_EQ(at_rule->type, CSS_AT_RULE_MEDIA) << "At-rule should be media rule";
    EXPECT_STREQ(at_rule->name, "@media") << "At-rule name should be '@media'";
}

TEST_F(CssParserTest, ParseImportRule) {
    const char* css = "@import url('styles.css');";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 1) << "Should have 1 rule";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE) << "Rule should be at-rule";

    css_at_rule_t* at_rule = rule->data.at_rule;
    ASSERT_NE(at_rule, nullptr) << "At-rule should not be NULL";
    EXPECT_EQ(at_rule->type, CSS_AT_RULE_IMPORT) << "At-rule should be import rule";
    EXPECT_STREQ(at_rule->name, "@import") << "At-rule name should be '@import'";
}

// Test multiple rules
TEST_F(CssParserTest, ParseMultipleRules) {
    const char* css = R"(
        body { margin: 0; padding: 0; }
        .container { width: 100%; }
        #header { height: 80px; }
        @media screen { body { font-size: 16px; } }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 4) << "Should have 4 rules";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    // Check rule types
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "First rule should be style rule";

    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "Second rule should be style rule";

    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_STYLE) << "Third rule should be style rule";

    rule = rule->next;
    EXPECT_EQ(rule->type, CSS_RULE_AT_RULE) << "Fourth rule should be at-rule";
}

// Test comment preservation
TEST_F(CssParserTest, ParseWithComments) {
    css_parser_set_preserve_comments(parser, true);

    const char* css = R"(
        /* Global styles */
        body { margin: 0; }
        /* Container styles */
        .container { width: 100%; }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->rule_count, 4) << "Should have 4 rules (2 comments + 2 style rules)";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    // Check that comments are preserved
    css_rule_t* rule = stylesheet->rules;
    EXPECT_EQ(rule->type, CSS_RULE_COMMENT) << "First rule should be comment";
    EXPECT_NE(rule->data.comment, nullptr) << "Comment data should not be NULL";
}

// Test error handling
TEST_F(CssParserTest, ParseInvalidSelector) {
    const char* css = "{ color: red; }"; // Missing selector
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->error_count, 0) << "Should have errors for missing selector";
}

TEST_F(CssParserTest, ParseMissingBrace) {
    const char* css = "body { color: red;"; // Missing closing brace
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->error_count, 0) << "Should have errors for missing closing brace";
}

TEST_F(CssParserTest, ParseMissingColon) {
    const char* css = "body { color red; }"; // Missing colon
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->error_count, 0) << "Should have errors for missing colon";
}

TEST_F(CssParserTest, ParseInvalidProperty) {
    const char* css = "body { 123invalid: red; }"; // Invalid property name
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->error_count, 0) << "Should have errors for invalid property name";
}

// Test complex CSS
TEST_F(CssParserTest, ParseComplexCss) {
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

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->rule_count, 5) << "Should have more than 5 rules";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";
}

// Test property validation
TEST_F(CssParserTest, ValidateKnownProperties) {
    const char* css = "div { color: red; width: 100px; margin: 10px; }";
    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

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
TEST_F(CssParserTest, CalculateSpecificity) {
    const char* css = R"(
        * { color: red; }
        div { color: blue; }
        .class { color: green; }
        #id { color: yellow; }
        div.class { color: purple; }
        #id.class { color: orange; }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_EQ(stylesheet->error_count, 0) << "Should have 0 errors";

    css_rule_t* rule = stylesheet->rules;

    // Universal selector: specificity 0
    css_selector_t* selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 0) << "Universal selector should have specificity 0";

    // Type selector: specificity 1
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 1) << "Type selector should have specificity 1";

    // Class selector: specificity 10
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 10) << "Class selector should have specificity 10";

    // ID selector: specificity 100
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 100) << "ID selector should have specificity 100";

    // Type + class: specificity 11
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 11) << "Type + class selector should have specificity 11";

    // ID + class: specificity 110
    rule = rule->next;
    selector = rule->data.style_rule->selectors;
    EXPECT_EQ(selector->specificity, 110) << "ID + class selector should have specificity 110";
}

// Test strict mode
TEST_F(CssParserTest, StrictModeStopsOnError) {
    css_parser_set_strict_mode(parser, true);

    const char* css = R"(
        body { color: red; }
        invalid { syntax
        p { font-size: 14px; }
    )";

    css_stylesheet_t* stylesheet = css_parse_stylesheet(parser, css);

    ASSERT_NE(stylesheet, nullptr) << "Stylesheet should not be NULL";
    EXPECT_GT(stylesheet->error_count, 0) << "Should have errors in strict mode";
    // In strict mode, parsing should stop after the first error
    // so we shouldn't get the 'p' rule
    EXPECT_LT(stylesheet->rule_count, 3) << "Should have fewer than 3 rules due to strict mode stopping on error";
}
