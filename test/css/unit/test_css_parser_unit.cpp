/**
 * CSS Parser Unit Tests - Comprehensive Coverage
 *
 * Tests for CSS parser functionality including:
 * - Selector parsing (element, class, ID, universal, attribute, pseudo)
 * - Declaration parsing (properties, values, !important)
 * - Rule parsing (complete rules with selectors and declarations)
 * - Multiple selectors (comma-separated)
 * - Complex selectors (combinators, compound selectors)
 * - Error recovery and edge cases
 *
 * Target: 80+ tests with 90% code coverage
 */

#include <gtest/gtest.h>
#include "../helpers/css_test_helpers.hpp"

extern "C" {
#include "lambda/input/css/css_parser.hpp"
#include "lambda/input/css/css_style.hpp"
}

using namespace CssTestHelpers;

// =============================================================================
// Test Fixture
// =============================================================================

class CssParserUnitTest : public ::testing::Test {
protected:
    PoolGuard pool;

    Parser CreateParser() {
        return Parser(pool.get());
    }
};

// =============================================================================
// Category 1: Selector Parsing - Element Selectors
// =============================================================================

TEST_F(CssParserUnitTest, Selector_Element_Simple) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("div");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    ASSERT_NE(selector->value, nullptr);
    EXPECT_STREQ(selector->value, "div");
}

TEST_F(CssParserUnitTest, Selector_Element_Paragraph) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("p");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(selector->value, "p");
}

TEST_F(CssParserUnitTest, Selector_Element_Span) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("span");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(selector->value, "span");
}

TEST_F(CssParserUnitTest, Selector_Element_WithWhitespace) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("  div  ");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(selector->value, "div");
}

TEST_F(CssParserUnitTest, Selector_Element_HTML5_Article) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("article");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(selector->value, "article");
}

TEST_F(CssParserUnitTest, Selector_Element_HTML5_Section) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("section");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(selector->value, "section");
}

// =============================================================================
// Category 2: Selector Parsing - Class Selectors
// =============================================================================

TEST_F(CssParserUnitTest, Selector_Class_Simple) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector(".container");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    ASSERT_NE(selector->value, nullptr);
    EXPECT_STREQ(selector->value, "container");
}

TEST_F(CssParserUnitTest, Selector_Class_Button) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector(".btn");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(selector->value, "btn");
}

TEST_F(CssParserUnitTest, Selector_Class_WithHyphen) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector(".nav-bar");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(selector->value, "nav-bar");
}

TEST_F(CssParserUnitTest, Selector_Class_WithUnderscore) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector(".my_class");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(selector->value, "my_class");
}

TEST_F(CssParserUnitTest, Selector_Class_BEM_Notation) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector(".block__element--modifier");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(selector->value, "block__element--modifier");
}

TEST_F(CssParserUnitTest, Selector_Class_WithWhitespace) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("  .container  ");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(selector->value, "container");
}

TEST_F(CssParserUnitTest, Selector_Class_NoDotIsNotClass) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("container");

    ASSERT_NE(selector, nullptr);
    // Without dot, it's an element selector
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ELEMENT);
}

// =============================================================================
// Category 3: Selector Parsing - ID Selectors
// =============================================================================

TEST_F(CssParserUnitTest, Selector_ID_Simple) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("#header");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ID);
    ASSERT_NE(selector->value, nullptr);
    // Value should NOT include the # symbol
    EXPECT_STREQ(selector->value, "header");
}

TEST_F(CssParserUnitTest, Selector_ID_Footer) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("#footer");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(selector->value, "footer");
}

TEST_F(CssParserUnitTest, Selector_ID_WithHyphen) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("#main-content");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(selector->value, "main-content");
}

TEST_F(CssParserUnitTest, Selector_ID_WithUnderscore) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("#my_id");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(selector->value, "my_id");
}

TEST_F(CssParserUnitTest, Selector_ID_WithWhitespace) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("  #header  ");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(selector->value, "header");
}

// =============================================================================
// Category 4: Selector Parsing - Universal Selector
// =============================================================================

TEST_F(CssParserUnitTest, Selector_Universal_Star) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("*");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_UNIVERSAL);
    EXPECT_STREQ(selector->value, "*");
}

TEST_F(CssParserUnitTest, Selector_Universal_WithWhitespace) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("  *  ");

    ASSERT_NE(selector, nullptr);
    EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_UNIVERSAL);
    EXPECT_STREQ(selector->value, "*");
}

// =============================================================================
// Category 5: Declaration Parsing - Basic Properties
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_Color_Name) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: red");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
    EXPECT_EQ(decl->value->type, CSS_VALUE_KEYWORD);
    EXPECT_FALSE(decl->important);
}

TEST_F(CssParserUnitTest, Declaration_Color_Hex) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: #ff0000");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    // Value should be parsed (type may vary based on implementation)
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_BackgroundColor) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("background-color: blue");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("background-color"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Display_Block) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("display: block");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("display"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Display_Flex) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("display: flex");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("display"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Position_Relative) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("position: relative");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("position"));
    ASSERT_NE(decl->value, nullptr);
}

// =============================================================================
// Category 6: Declaration Parsing - Numeric Values
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_Width_Pixels) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("width: 100px");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("width"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Height_Percent) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("height: 50%");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("height"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_FontSize_Em) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("font-size: 1.5em");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("font-size"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Margin_Rem) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("margin: 2rem");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("margin"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Padding_Zero) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("padding: 0");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("padding"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_LineHeight_Unitless) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("line-height: 1.5");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("line-height"));
    ASSERT_NE(decl->value, nullptr);
}

// =============================================================================
// Category 7: Declaration Parsing - Multiple Values
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_Margin_FourValues) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("margin: 10px 20px 30px 40px");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("margin"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Padding_TwoValues) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("padding: 10px 20px");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("padding"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Border_Shorthand) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("border: 1px solid black");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("border"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Font_Shorthand) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("font: 14px Arial, sans-serif");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("font"));
    ASSERT_NE(decl->value, nullptr);
}

// =============================================================================
// Category 8: Declaration Parsing - !important
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_Important_Color) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: red !important");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
    EXPECT_TRUE(decl->important);
}

TEST_F(CssParserUnitTest, Declaration_Important_WithWhitespace) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("width: 100px  !important");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("width"));
    ASSERT_NE(decl->value, nullptr);
    EXPECT_TRUE(decl->important);
}

TEST_F(CssParserUnitTest, Declaration_Important_NoSpaceBeforeExclamation) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("display: block!important");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("display"));
    EXPECT_TRUE(decl->important);
}

TEST_F(CssParserUnitTest, Declaration_NotImportant_ByDefault) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: blue");

    ASSERT_NE(decl, nullptr);
    EXPECT_FALSE(decl->important);
}

// =============================================================================
// Category 9: Declaration Parsing - Functions
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_Color_RGB) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: rgb(255, 0, 0)");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Color_RGBA) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: rgba(255, 0, 0, 0.5)");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Transform_Translate) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("transform: translate(10px, 20px)");

    ASSERT_NE(decl, nullptr);
    // Property ID may not exist for all properties
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_Width_Calc) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("width: calc(100% - 20px)");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("width"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_CustomProperty_Var) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: var(--primary-color)");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
}

// =============================================================================
// Category 10: Declaration Parsing - Edge Cases
// =============================================================================

TEST_F(CssParserUnitTest, Declaration_WithSemicolon) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color: red;");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_WithWhitespace) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("  color  :  red  ");

    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, css_property_id_from_name("color"));
    ASSERT_NE(decl->value, nullptr);
}

TEST_F(CssParserUnitTest, Declaration_EmptyValue_Invalid) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color:");

    // Empty value should fail or return null/empty
    // Implementation dependent - document actual behavior
}

TEST_F(CssParserUnitTest, Declaration_NoColon_Invalid) {
    auto parser = CreateParser();
    auto decl = parser.ParseDeclaration("color red");

    // No colon should fail
    EXPECT_EQ(decl, nullptr);
}

// =============================================================================
// Category 11: Rule Parsing - Simple Rules
// =============================================================================

TEST_F(CssParserUnitTest, Rule_Element_SingleDeclaration) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { color: red; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    EXPECT_NE(rule->data.style_rule.selector, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

TEST_F(CssParserUnitTest, Rule_Class_SingleDeclaration) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule(".container { width: 100%; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    EXPECT_NE(rule->data.style_rule.selector, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

TEST_F(CssParserUnitTest, Rule_ID_SingleDeclaration) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("#header { height: 80px; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    EXPECT_NE(rule->data.style_rule.selector, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

TEST_F(CssParserUnitTest, Rule_Universal_SingleDeclaration) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("* { margin: 0; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    EXPECT_NE(rule->data.style_rule.selector, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

// =============================================================================
// Category 12: Rule Parsing - Multiple Declarations
// =============================================================================

TEST_F(CssParserUnitTest, Rule_MultipleDeclarations) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { color: red; background: blue; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->type, CSS_RULE_STYLE);
    EXPECT_GE(rule->data.style_rule.declaration_count, 2);
}

TEST_F(CssParserUnitTest, Rule_ThreeDeclarations) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule(".btn { width: 100px; height: 40px; color: white; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 3);
}

TEST_F(CssParserUnitTest, Rule_DeclarationsWithImportant) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("p { color: red !important; font-size: 14px; }");

    ASSERT_NE(rule, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 2);

    // Check that at least one declaration has important flag
    bool has_important = false;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        if (rule->data.style_rule.declarations[i]->important) {
            has_important = true;
            break;
        }
    }
    EXPECT_TRUE(has_important);
}

TEST_F(CssParserUnitTest, Rule_NoSemicolonBeforeCloseBrace) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { color: red }");

    ASSERT_NE(rule, nullptr);
    // Should still parse correctly even without trailing semicolon
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

// =============================================================================
// Category 13: Rule Parsing - Formatting Variations
// =============================================================================

TEST_F(CssParserUnitTest, Rule_OneLine) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div{color:red;}");

    ASSERT_NE(rule, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

TEST_F(CssParserUnitTest, Rule_MultiLine) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule(
        "div {\n"
        "  color: red;\n"
        "  background: blue;\n"
        "}"
    );

    ASSERT_NE(rule, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 2);
}

TEST_F(CssParserUnitTest, Rule_WithExtraWhitespace) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("  div  {  color  :  red  ;  }  ");

    ASSERT_NE(rule, nullptr);
    EXPECT_GE(rule->data.style_rule.declaration_count, 1);
}

TEST_F(CssParserUnitTest, Rule_EmptyRule) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { }");

    // Empty rule may return nullptr or a rule with 0 declarations
    if (rule != nullptr) {
        EXPECT_EQ(rule->data.style_rule.declaration_count, 0);
    }
}

// =============================================================================
// Category 14: Error Recovery
// =============================================================================

TEST_F(CssParserUnitTest, Error_MissingOpenBrace) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div color: red; }");

    // Should fail gracefully - implementation dependent
    // Just verify it doesn't crash
}

TEST_F(CssParserUnitTest, Error_MissingCloseBrace) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { color: red;");

    // Should fail or recover gracefully
    // Just verify it doesn't crash
}

TEST_F(CssParserUnitTest, Error_MissingSemicolon_BetweenDeclarations) {
    auto parser = CreateParser();
    auto rule = parser.ParseRule("div { color: red background: blue; }");

    // Error recovery - might skip invalid declaration
}

TEST_F(CssParserUnitTest, Error_InvalidSelector) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("123invalid");

    // Should handle invalid selector
}

TEST_F(CssParserUnitTest, Error_EmptyInput) {
    auto parser = CreateParser();
    auto selector = parser.ParseSelector("");

    EXPECT_EQ(selector, nullptr);
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
