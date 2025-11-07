#include <gtest/gtest.h>
#include "helpers/css_test_helpers.hpp"

extern "C" {
#include "../../lambda/input/css/css_engine.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lambda/input/css/css_style.hpp"
}

using namespace CssTestHelpers;

class CompoundDescendantSelectorTest : public ::testing::Test {
protected:
    PoolGuard pool;

    CssEngine* CreateEngine() {
        CssEngine* engine = css_engine_create(pool.get());
        if (engine) {
            // Set default viewport
            css_engine_set_viewport(engine, 1920, 1080);
        }
        return engine;
    }
};

// ============================================================================
// Compound Selector Tests
// ============================================================================

TEST_F(CompoundDescendantSelectorTest, CompoundSelector_ElementWithClass) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "p.intro { color: blue; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);
    ASSERT_NE(rule->data.style_rule.selector, nullptr);

    // Should have a compound selector with 2 simple selectors (element + class)
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;
    ASSERT_EQ(selector->compound_selector_count, 1);

    CssCompoundSelector* compound = selector->compound_selectors[0];
    ASSERT_NE(compound, nullptr);
    EXPECT_EQ(compound->simple_selector_count, 2);

    // First should be element selector 'p'
    EXPECT_EQ(compound->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(compound->simple_selectors[0]->value, "p");

    // Second should be class selector 'intro'
    EXPECT_EQ(compound->simple_selectors[1]->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(compound->simple_selectors[1]->value, "intro");
}

TEST_F(CompoundDescendantSelectorTest, CompoundSelector_ElementWithID) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div#main { width: 100%; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;
    ASSERT_EQ(selector->compound_selector_count, 1);

    CssCompoundSelector* compound = selector->compound_selectors[0];
    EXPECT_EQ(compound->simple_selector_count, 2);

    // Element + ID
    EXPECT_EQ(compound->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(compound->simple_selectors[0]->value, "div");

    EXPECT_EQ(compound->simple_selectors[1]->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(compound->simple_selectors[1]->value, "main");
}

TEST_F(CompoundDescendantSelectorTest, CompoundSelector_ElementWithMultipleClasses) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div.container.fluid { padding: 10px; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;
    ASSERT_EQ(selector->compound_selector_count, 1);

    CssCompoundSelector* compound = selector->compound_selectors[0];
    EXPECT_EQ(compound->simple_selector_count, 3);

    // Element + two classes
    EXPECT_EQ(compound->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(compound->simple_selectors[0]->value, "div");

    EXPECT_EQ(compound->simple_selectors[1]->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(compound->simple_selectors[1]->value, "container");

    EXPECT_EQ(compound->simple_selectors[2]->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(compound->simple_selectors[2]->value, "fluid");
}

TEST_F(CompoundDescendantSelectorTest, CompoundSelector_ClassWithID) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = ".highlight#special { background: yellow; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;
    ASSERT_EQ(selector->compound_selector_count, 1);

    CssCompoundSelector* compound = selector->compound_selectors[0];
    EXPECT_EQ(compound->simple_selector_count, 2);

    // Class + ID
    EXPECT_EQ(compound->simple_selectors[0]->type, CSS_SELECTOR_TYPE_CLASS);
    EXPECT_STREQ(compound->simple_selectors[0]->value, "highlight");

    EXPECT_EQ(compound->simple_selectors[1]->type, CSS_SELECTOR_TYPE_ID);
    EXPECT_STREQ(compound->simple_selectors[1]->value, "special");
}

// ============================================================================
// Descendant Selector Tests
// ============================================================================

TEST_F(CompoundDescendantSelectorTest, DescendantSelector_TwoElements) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div p { margin: 0; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    // Should have 2 compound selectors with a descendant combinator
    ASSERT_EQ(selector->compound_selector_count, 2);

    // First compound: 'div'
    CssCompoundSelector* first = selector->compound_selectors[0];
    EXPECT_EQ(first->simple_selector_count, 1);
    EXPECT_EQ(first->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(first->simple_selectors[0]->value, "div");

    // Combinator should be DESCENDANT
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_DESCENDANT);

    // Second compound: 'p'
    CssCompoundSelector* second = selector->compound_selectors[1];
    EXPECT_EQ(second->simple_selector_count, 1);
    EXPECT_EQ(second->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    EXPECT_STREQ(second->simple_selectors[0]->value, "p");
}

TEST_F(CompoundDescendantSelectorTest, DescendantSelector_ThreeLevels) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav ul li { list-style: none; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    // Should have 3 compound selectors
    ASSERT_EQ(selector->compound_selector_count, 3);

    // nav
    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "nav");
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_DESCENDANT);

    // ul
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "ul");
    EXPECT_EQ(selector->combinators[1], CSS_COMBINATOR_DESCENDANT);

    // li
    EXPECT_STREQ(selector->compound_selectors[2]->simple_selectors[0]->value, "li");
}

TEST_F(CompoundDescendantSelectorTest, DescendantSelector_WithClasses) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div.container p.text { font-size: 14px; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    // Should have 2 compound selectors
    ASSERT_EQ(selector->compound_selector_count, 2);

    // First: div.container (2 simple selectors)
    CssCompoundSelector* first = selector->compound_selectors[0];
    EXPECT_EQ(first->simple_selector_count, 2);
    EXPECT_STREQ(first->simple_selectors[0]->value, "div");
    EXPECT_STREQ(first->simple_selectors[1]->value, "container");

    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_DESCENDANT);

    // Second: p.text (2 simple selectors)
    CssCompoundSelector* second = selector->compound_selectors[1];
    EXPECT_EQ(second->simple_selector_count, 2);
    EXPECT_STREQ(second->simple_selectors[0]->value, "p");
    EXPECT_STREQ(second->simple_selectors[1]->value, "text");
}

// ============================================================================
// Child Combinator Tests
// ============================================================================

TEST_F(CompoundDescendantSelectorTest, ChildCombinator_TwoElements) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div > p { margin: 0; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 2);

    // Should be CHILD combinator, not DESCENDANT
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_CHILD);

    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "div");
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "p");
}

TEST_F(CompoundDescendantSelectorTest, ChildCombinator_WithCompound) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav.main > ul.menu { display: flex; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 2);
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_CHILD);

    // nav.main
    EXPECT_EQ(selector->compound_selectors[0]->simple_selector_count, 2);
    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "nav");
    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[1]->value, "main");

    // ul.menu
    EXPECT_EQ(selector->compound_selectors[1]->simple_selector_count, 2);
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "ul");
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[1]->value, "menu");
}

// ============================================================================
// Mixed Combinator Tests
// ============================================================================

TEST_F(CompoundDescendantSelectorTest, MixedCombinators_DescendantAndChild) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "article div > p { line-height: 1.5; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 3);

    // article (descendant) div (child) p
    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "article");
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_DESCENDANT);

    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "div");
    EXPECT_EQ(selector->combinators[1], CSS_COMBINATOR_CHILD);

    EXPECT_STREQ(selector->compound_selectors[2]->simple_selectors[0]->value, "p");
}

TEST_F(CompoundDescendantSelectorTest, SiblingCombinators_NextSibling) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h2 + p { margin-top: 0; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 2);
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_NEXT_SIBLING);

    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "h2");
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "p");
}

TEST_F(CompoundDescendantSelectorTest, SiblingCombinators_SubsequentSibling) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h2 ~ p { color: gray; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 2);
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_SUBSEQUENT_SIBLING);

    EXPECT_STREQ(selector->compound_selectors[0]->simple_selectors[0]->value, "h2");
    EXPECT_STREQ(selector->compound_selectors[1]->simple_selectors[0]->value, "p");
}

// ============================================================================
// Complex Real-World Examples
// ============================================================================

TEST_F(CompoundDescendantSelectorTest, ComplexSelector_Navigation) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav.navbar div.container ul.menu > li.item { display: inline-block; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    // Should have 4 compound selectors: nav.navbar, div.container, ul.menu, li.item
    ASSERT_EQ(selector->compound_selector_count, 4);

    // All compounds should have 2 simple selectors each
    EXPECT_EQ(selector->compound_selectors[0]->simple_selector_count, 2);
    EXPECT_EQ(selector->compound_selectors[1]->simple_selector_count, 2);
    EXPECT_EQ(selector->compound_selectors[2]->simple_selector_count, 2);
    EXPECT_EQ(selector->compound_selectors[3]->simple_selector_count, 2);

    // Check combinators: descendant, descendant, child
    EXPECT_EQ(selector->combinators[0], CSS_COMBINATOR_DESCENDANT);
    EXPECT_EQ(selector->combinators[1], CSS_COMBINATOR_DESCENDANT);
    EXPECT_EQ(selector->combinators[2], CSS_COMBINATOR_CHILD);
}

TEST_F(CompoundDescendantSelectorTest, ComplexSelector_Form) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "form#login div.field > input.text#username { width: 100%; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    CssSelector* selector = (CssSelector*)rule->data.style_rule.selector;

    ASSERT_EQ(selector->compound_selector_count, 3);

    // form#login: element + id
    EXPECT_EQ(selector->compound_selectors[0]->simple_selector_count, 2);

    // div.field: element + class
    EXPECT_EQ(selector->compound_selectors[1]->simple_selector_count, 2);

    // input.text#username: element + class + id
    EXPECT_EQ(selector->compound_selectors[2]->simple_selector_count, 3);
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
