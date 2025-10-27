/**
 * CSS Selector Groups Tests
 *
 * Tests for comma-separated selector groups (h1, h2, h3)
 * Covers simple element selectors, compound selectors, descendant selectors,
 * and complex mixed cases.
 */

#include <gtest/gtest.h>
#include "helpers/css_test_helpers.hpp"

extern "C" {
#include "../../lambda/input/css/css_engine.h"
#include "../../lambda/input/css/css_parser.h"
#include "../../lambda/input/css/css_style.h"
}

using namespace CssTestHelpers;

class SelectorGroupTest : public ::testing::Test {
protected:
    PoolGuard pool;

    CssEngine* CreateEngine() {
        CssEngine* engine = css_engine_create(pool.get());
        if (engine) {
            css_engine_set_viewport(engine, 1920, 1080);
        }
        return engine;
    }
};

// ============================================================================
// Simple Element Selector Groups
// ============================================================================

TEST_F(SelectorGroupTest, SimpleGroup_TwoElements) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h1, h2 { color: blue; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);
    ASSERT_NE(rule->data.style_rule.selector_group, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_EQ(group->selector_count, 2);

    // First selector: h1
    CssSelector* sel1 = group->selectors[0];
    ASSERT_NE(sel1, nullptr);
    ASSERT_EQ(sel1->compound_selector_count, 1);

    CssCompoundSelector* compound1 = sel1->compound_selectors[0];
    ASSERT_EQ(compound1->simple_selector_count, 1);
    ASSERT_EQ(compound1->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    ASSERT_STREQ(compound1->simple_selectors[0]->value, "h1");

    // Second selector: h2
    CssSelector* sel2 = group->selectors[1];
    ASSERT_NE(sel2, nullptr);
    ASSERT_EQ(sel2->compound_selector_count, 1);

    CssCompoundSelector* compound2 = sel2->compound_selectors[0];
    ASSERT_EQ(compound2->simple_selector_count, 1);
    ASSERT_EQ(compound2->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    ASSERT_STREQ(compound2->simple_selectors[0]->value, "h2");
}

TEST_F(SelectorGroupTest, SimpleGroup_ThreeElements) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h1, h2, h3 { margin: 0; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 3);

    // Verify all three selectors
    const char* expected[] = {"h1", "h2", "h3"};
    for (size_t i = 0; i < 3; i++) {
        CssSelector* sel = group->selectors[i];
        ASSERT_NE(sel, nullptr);
        ASSERT_EQ(sel->compound_selector_count, 1);

        CssCompoundSelector* compound = sel->compound_selectors[0];
        ASSERT_EQ(compound->simple_selector_count, 1);
        ASSERT_EQ(compound->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
        ASSERT_STREQ(compound->simple_selectors[0]->value, expected[i]);
    }
}

TEST_F(SelectorGroupTest, SimpleGroup_FiveElements) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h1, h2, h3, h4, h5 { font-weight: bold; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 5);

    // Verify all five selectors
    const char* expected[] = {"h1", "h2", "h3", "h4", "h5"};
    for (size_t i = 0; i < 5; i++) {
        CssSelector* sel = group->selectors[i];
        ASSERT_NE(sel, nullptr);
        ASSERT_EQ(sel->compound_selector_count, 1);

        CssCompoundSelector* compound = sel->compound_selectors[0];
        ASSERT_EQ(compound->simple_selector_count, 1);
        ASSERT_STREQ(compound->simple_selectors[0]->value, expected[i]);
    }
}

// ============================================================================
// Compound Selector Groups
// ============================================================================

TEST_F(SelectorGroupTest, CompoundGroup_ElementWithClass) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "p.intro, div.outro { padding: 10px; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First selector: p.intro
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 1);

    CssCompoundSelector* compound1 = sel1->compound_selectors[0];
    ASSERT_EQ(compound1->simple_selector_count, 2);
    ASSERT_EQ(compound1->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    ASSERT_STREQ(compound1->simple_selectors[0]->value, "p");
    ASSERT_EQ(compound1->simple_selectors[1]->type, CSS_SELECTOR_TYPE_CLASS);
    ASSERT_STREQ(compound1->simple_selectors[1]->value, "intro");

    // Second selector: div.outro
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 1);

    CssCompoundSelector* compound2 = sel2->compound_selectors[0];
    ASSERT_EQ(compound2->simple_selector_count, 2);
    ASSERT_EQ(compound2->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ELEMENT);
    ASSERT_STREQ(compound2->simple_selectors[0]->value, "div");
    ASSERT_EQ(compound2->simple_selectors[1]->type, CSS_SELECTOR_TYPE_CLASS);
    ASSERT_STREQ(compound2->simple_selectors[1]->value, "outro");
}

TEST_F(SelectorGroupTest, CompoundGroup_ClassesAndIDs) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = ".button, #submit, .link { cursor: pointer; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 3);

    // .button
    CssCompoundSelector* compound1 = group->selectors[0]->compound_selectors[0];
    ASSERT_EQ(compound1->simple_selectors[0]->type, CSS_SELECTOR_TYPE_CLASS);
    ASSERT_STREQ(compound1->simple_selectors[0]->value, "button");

    // #submit
    CssCompoundSelector* compound2 = group->selectors[1]->compound_selectors[0];
    ASSERT_EQ(compound2->simple_selectors[0]->type, CSS_SELECTOR_TYPE_ID);
    ASSERT_STREQ(compound2->simple_selectors[0]->value, "submit");

    // .link
    CssCompoundSelector* compound3 = group->selectors[2]->compound_selectors[0];
    ASSERT_EQ(compound3->simple_selectors[0]->type, CSS_SELECTOR_TYPE_CLASS);
    ASSERT_STREQ(compound3->simple_selectors[0]->value, "link");
}

TEST_F(SelectorGroupTest, CompoundGroup_MultipleClasses) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div.container.fluid, section.main.active { display: block; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: div.container.fluid
    CssCompoundSelector* compound1 = group->selectors[0]->compound_selectors[0];
    ASSERT_EQ(compound1->simple_selector_count, 3);
    ASSERT_STREQ(compound1->simple_selectors[0]->value, "div");
    ASSERT_STREQ(compound1->simple_selectors[1]->value, "container");
    ASSERT_STREQ(compound1->simple_selectors[2]->value, "fluid");

    // Second: section.main.active
    CssCompoundSelector* compound2 = group->selectors[1]->compound_selectors[0];
    ASSERT_EQ(compound2->simple_selector_count, 3);
    ASSERT_STREQ(compound2->simple_selectors[0]->value, "section");
    ASSERT_STREQ(compound2->simple_selectors[1]->value, "main");
    ASSERT_STREQ(compound2->simple_selectors[2]->value, "active");
}

// ============================================================================
// Descendant Selector Groups
// ============================================================================

TEST_F(SelectorGroupTest, DescendantGroup_TwoLevels) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav ul, footer div { list-style: none; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: nav ul
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 2);
    ASSERT_EQ(sel1->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[0]->value, "nav");
    ASSERT_STREQ(sel1->compound_selectors[1]->simple_selectors[0]->value, "ul");

    // Second: footer div
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 2);
    ASSERT_EQ(sel2->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "footer");
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[0]->value, "div");
}

TEST_F(SelectorGroupTest, DescendantGroup_ThreeLevels) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "header nav ul, footer aside div { margin: 0; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: header nav ul
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 3);
    ASSERT_EQ(sel1->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_EQ(sel1->combinators[1], CSS_COMBINATOR_DESCENDANT);
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[0]->value, "header");
    ASSERT_STREQ(sel1->compound_selectors[1]->simple_selectors[0]->value, "nav");
    ASSERT_STREQ(sel1->compound_selectors[2]->simple_selectors[0]->value, "ul");

    // Second: footer aside div
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 3);
    ASSERT_EQ(sel2->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_EQ(sel2->combinators[1], CSS_COMBINATOR_DESCENDANT);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "footer");
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[0]->value, "aside");
    ASSERT_STREQ(sel2->compound_selectors[2]->simple_selectors[0]->value, "div");
}

// ============================================================================
// Child Combinator Groups
// ============================================================================

TEST_F(SelectorGroupTest, ChildGroup_TwoSelectors) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "div > p, section > article { color: black; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: div > p
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 2);
    ASSERT_EQ(sel1->combinators[0], CSS_COMBINATOR_CHILD);
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[0]->value, "div");
    ASSERT_STREQ(sel1->compound_selectors[1]->simple_selectors[0]->value, "p");

    // Second: section > article
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 2);
    ASSERT_EQ(sel2->combinators[0], CSS_COMBINATOR_CHILD);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "section");
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[0]->value, "article");
}

// ============================================================================
// Mixed Complex Selector Groups
// ============================================================================

TEST_F(SelectorGroupTest, MixedGroup_DescendantAndChild) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav ul li, div > p.highlight { font-size: 14px; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: nav ul li (descendant)
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 3);
    ASSERT_EQ(sel1->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_EQ(sel1->combinators[1], CSS_COMBINATOR_DESCENDANT);

    // Second: div > p.highlight (child + compound)
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 2);
    ASSERT_EQ(sel2->combinators[0], CSS_COMBINATOR_CHILD);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "div");
    ASSERT_EQ(sel2->compound_selectors[1]->simple_selector_count, 2);
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[0]->value, "p");
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[1]->value, "highlight");
}

TEST_F(SelectorGroupTest, MixedGroup_RealWorld_Navigation) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "nav.navbar ul.menu > li, footer.footer div.links > a { text-decoration: none; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 2);

    // First: nav.navbar ul.menu > li
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 3);
    ASSERT_EQ(sel1->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_EQ(sel1->combinators[1], CSS_COMBINATOR_CHILD);

    // nav.navbar
    ASSERT_EQ(sel1->compound_selectors[0]->simple_selector_count, 2);
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[0]->value, "nav");
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[1]->value, "navbar");

    // ul.menu
    ASSERT_EQ(sel1->compound_selectors[1]->simple_selector_count, 2);
    ASSERT_STREQ(sel1->compound_selectors[1]->simple_selectors[0]->value, "ul");
    ASSERT_STREQ(sel1->compound_selectors[1]->simple_selectors[1]->value, "menu");

    // li
    ASSERT_EQ(sel1->compound_selectors[2]->simple_selector_count, 1);
    ASSERT_STREQ(sel1->compound_selectors[2]->simple_selectors[0]->value, "li");

    // Second: footer.footer div.links > a
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 3);
    ASSERT_EQ(sel2->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_EQ(sel2->combinators[1], CSS_COMBINATOR_CHILD);

    // footer.footer
    ASSERT_EQ(sel2->compound_selectors[0]->simple_selector_count, 2);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "footer");
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[1]->value, "footer");

    // div.links
    ASSERT_EQ(sel2->compound_selectors[1]->simple_selector_count, 2);
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[0]->value, "div");
    ASSERT_STREQ(sel2->compound_selectors[1]->simple_selectors[1]->value, "links");

    // a
    ASSERT_EQ(sel2->compound_selectors[2]->simple_selector_count, 1);
    ASSERT_STREQ(sel2->compound_selectors[2]->simple_selectors[0]->value, "a");
}

TEST_F(SelectorGroupTest, MixedGroup_SimplePlusCompoundPlusDescendant) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h1, p.intro, div span { line-height: 1.5; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 3);

    // First: h1 (simple)
    CssSelector* sel1 = group->selectors[0];
    ASSERT_EQ(sel1->compound_selector_count, 1);
    ASSERT_EQ(sel1->compound_selectors[0]->simple_selector_count, 1);
    ASSERT_STREQ(sel1->compound_selectors[0]->simple_selectors[0]->value, "h1");

    // Second: p.intro (compound)
    CssSelector* sel2 = group->selectors[1];
    ASSERT_EQ(sel2->compound_selector_count, 1);
    ASSERT_EQ(sel2->compound_selectors[0]->simple_selector_count, 2);
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[0]->value, "p");
    ASSERT_STREQ(sel2->compound_selectors[0]->simple_selectors[1]->value, "intro");

    // Third: div span (descendant)
    CssSelector* sel3 = group->selectors[2];
    ASSERT_EQ(sel3->compound_selector_count, 2);
    ASSERT_EQ(sel3->combinators[0], CSS_COMBINATOR_DESCENDANT);
    ASSERT_STREQ(sel3->compound_selectors[0]->simple_selectors[0]->value, "div");
    ASSERT_STREQ(sel3->compound_selectors[1]->simple_selectors[0]->value, "span");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(SelectorGroupTest, SingleSelectorAsGroup) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "p { color: red; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    // Even a single selector should be in a group
    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 1);

    CssSelector* sel = group->selectors[0];
    ASSERT_NE(sel, nullptr);
    ASSERT_EQ(sel->compound_selector_count, 1);
    ASSERT_STREQ(sel->compound_selectors[0]->simple_selectors[0]->value, "p");
}

TEST_F(SelectorGroupTest, GroupWithWhitespace) {
    auto engine = CreateEngine();
    ASSERT_NE(engine, nullptr);

    const char* css = "h1  ,  h2  ,  h3 { font-size: 2em; }";

    CssStylesheet* sheet = css_parse_stylesheet(engine, css, nullptr);

    ASSERT_NE(sheet, nullptr);
    ASSERT_GT(sheet->rule_count, 0);

    CssRule* rule = sheet->rules[0];
    ASSERT_NE(rule, nullptr);

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->selector_count, 3);

    // All three selectors should be parsed correctly despite extra whitespace
    const char* expected[] = {"h1", "h2", "h3"};
    for (size_t i = 0; i < 3; i++) {
        ASSERT_STREQ(group->selectors[i]->compound_selectors[0]->simple_selectors[0]->value, expected[i]);
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
