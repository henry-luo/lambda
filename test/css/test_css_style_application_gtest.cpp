#include <gtest/gtest.h>

#include "../../lambda/lambda.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/selector_matcher.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lambda/input/css/css_style.hpp"
#include "../../lambda/input/css/css_style_node.hpp"
#include "../../lambda/input/css/css_formatter.hpp"

extern "C" {
#include "../../lib/mempool.h"
}

/**
 * CSS Style Application Test Suite
 *
 * Tests for identified CSS application issues:
 * 1. Universal selector (*) not being applied
 * 2. Class selectors not being applied
 * 3. Font property inheritance
 * 4. Cascade order and specificity
 * 5. Default value handling
 *
 * These tests cover the bugs found in the CSS baseline test analysis.
 */

class CssStyleApplicationTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;
    DomDocument* doc;
    SelectorMatcher* matcher;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        ASSERT_TRUE(css_property_system_init(pool));

        input = Input::create(pool);
        ASSERT_NE(input, nullptr);

        doc = dom_document_create(input);
        ASSERT_NE(doc, nullptr);

        matcher = selector_matcher_create(pool);
        ASSERT_NE(matcher, nullptr);
    }

    void TearDown() override {
        if (matcher) {
            selector_matcher_destroy(matcher);
        }
        if (doc) {
            dom_document_destroy(doc);
        }
        css_property_system_cleanup();
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Create CSS declaration with specificity
    CssDeclaration* create_declaration(CssPropertyCode prop_id, const char* value,
                                      uint8_t ids = 0, uint8_t classes = 0,
                                      uint8_t elements = 0) {
        const char* property = css_property_spelling_from_code(prop_id);
        if (!property) return nullptr;
        size_t length = strlen(property) + strlen(value) + 2;
        char* text = (char*)pool_alloc(pool, length + 1);
        if (!text) return nullptr;
        snprintf(text, length + 1, "%s: %s", property, value);
        // the cascade validates CssValue trees; raw string pointers are not values.
        CssDeclaration* declaration = css_parse_declaration_text(text, length, pool);
        if (!declaration) return nullptr;
        declaration->specificity = css_specificity_create(0, ids, classes, elements, false);
        declaration->origin = CSS_ORIGIN_AUTHOR;
        return declaration;
    }

    // Helper: Create universal selector (simple selector version)
    CssSimpleSelector* create_universal_simple_selector() {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_UNIVERSAL;
        sel->value = "*";
        return sel;
    }

    // Helper: Create class selector (simple selector version)
    CssSimpleSelector* create_class_simple_selector(const char* class_name) {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_CLASS;
        sel->value = class_name;
        return sel;
    }

    // Helper: Create element type selector (simple selector version)
    CssSimpleSelector* create_element_simple_selector(const char* tag_name) {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_ELEMENT;
        sel->value = tag_name;
        return sel;
    }

    // Helper: Create full selector from simple selector
    CssSelector* create_selector_from_simple(CssSimpleSelector* simple) {
        CssSelector* sel = (CssSelector*)pool_calloc(pool, sizeof(CssSelector));
        sel->compound_selector_count = 1;
        sel->compound_selectors = (CssCompoundSelector**)pool_alloc(pool, sizeof(CssCompoundSelector*));

        CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
        compound->simple_selector_count = 1;
        compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
        compound->simple_selectors[0] = simple;

        sel->compound_selectors[0] = compound;
        return sel;
    }
};

TEST_F(CssStyleApplicationTest, AllResetCompetesWithEachProperty) {
    StyleTree* tree = style_tree_create(pool);
    ASSERT_NE(tree, nullptr);
    CssDeclaration* color = css_parse_declaration_text("color: red", 10, pool);
    CssDeclaration* direction = css_parse_declaration_text("direction: rtl", 14, pool);
    CssDeclaration* reset = css_parse_declaration_text("all: initial", 12, pool);
    CssDeclaration* width = css_parse_declaration_text("width: 40px", 11, pool);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(direction, nullptr);
    ASSERT_NE(reset, nullptr);
    ASSERT_NE(width, nullptr);

    ASSERT_NE(style_tree_apply_declaration(tree, color), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, direction), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, reset), nullptr);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_COLOR), reset);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_HEIGHT), reset);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_DIRECTION), direction);
    ASSERT_NE(style_tree_apply_declaration(tree, width), nullptr);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_WIDTH), width);

    color->specificity = css_specificity_create(0, 1, 0, 0, false);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_COLOR), color);
}

TEST_F(CssStyleApplicationTest, AllRevertLayerRollsBackLonghandsInSameLayer) {
    StyleTree* tree = style_tree_create(pool);
    ASSERT_NE(tree, nullptr);
    CssDeclaration* earlier = css_parse_declaration_text("width: 40px", 11, pool);
    CssDeclaration* later = css_parse_declaration_text("width: 60px", 11, pool);
    CssDeclaration* rollback = css_parse_declaration_text("all: revert-layer", 17, pool);
    ASSERT_NE(earlier, nullptr);
    ASSERT_NE(later, nullptr);
    ASSERT_NE(rollback, nullptr);
    earlier->layer_order = 1;
    later->layer_order = 2;
    rollback->layer_order = 2;
    ASSERT_NE(style_tree_apply_declaration(tree, earlier), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, later), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, rollback), nullptr);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_WIDTH), earlier);

    CssDeclaration* unlayered = css_parse_declaration_text("width: 80px", 11, pool);
    ASSERT_NE(unlayered, nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, unlayered), nullptr);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_WIDTH), unlayered);
}

TEST_F(CssStyleApplicationTest, AllRevertExposesUserAgentDeclaration) {
    StyleTree* tree = style_tree_create(pool);
    ASSERT_NE(tree, nullptr);
    CssDeclaration* ua = css_parse_declaration_text("color: green", 12, pool);
    CssDeclaration* author = css_parse_declaration_text("color: red", 10, pool);
    CssDeclaration* rollback = css_parse_declaration_text("all: revert", 11, pool);
    ASSERT_NE(ua, nullptr);
    ASSERT_NE(author, nullptr);
    ASSERT_NE(rollback, nullptr);
    ua->origin = CSS_ORIGIN_USER_AGENT;
    author->origin = CSS_ORIGIN_AUTHOR;
    rollback->origin = CSS_ORIGIN_AUTHOR;
    ASSERT_NE(style_tree_apply_declaration(tree, ua), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, author), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, rollback), nullptr);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_COLOR), ua);
    EXPECT_EQ(style_tree_get_declaration(tree, CSS_PROPERTY_WIDTH), nullptr);
}

TEST_F(CssStyleApplicationTest, ComponentRollbackSharesShorthandLonghandAndLogicalSources) {
    StyleTree* tree = style_tree_create(pool);
    ASSERT_NE(tree, nullptr);
    const CssPropertyCode sources[] = {CSS_PROPERTY_MARGIN, CSS_PROPERTY_MARGIN_LEFT,
        CSS_PROPERTY_MARGIN_INLINE_START, CSS_PROPERTY_ALL};
    const char* values[] = {"margin:3px!important", "margin-left:5px!important",
        "margin-inline-start:revert-layer!important", "all:revert!important"};
    CssDeclaration* declarations[4] = {};
    for (int index = 0; index < 4; index++) {
        declarations[index] = css_parse_declaration_text(values[index], strlen(values[index]), pool);
        ASSERT_NE(declarations[index], nullptr);
        declarations[index]->origin = CSS_ORIGIN_AUTHOR;
        declarations[index]->layer_order = index == 0 ? 2 : 1;
    }
    ASSERT_NE(style_tree_apply_declaration(tree, declarations[0]), nullptr);
    ASSERT_NE(style_tree_apply_declaration(tree, declarations[1]), nullptr);
    EXPECT_EQ(style_tree_get_component_declaration(tree, sources, 4), declarations[1]);
    ASSERT_NE(style_tree_apply_declaration(tree, declarations[2]), nullptr);
    EXPECT_EQ(style_tree_get_component_declaration(tree, sources, 4), declarations[0]);
    ASSERT_NE(style_tree_apply_declaration(tree, declarations[3]), nullptr);
    EXPECT_EQ(style_tree_get_component_declaration(tree, sources, 4), nullptr);
    CssDeclaration* ua = css_parse_declaration_text("margin-left:7px", 15, pool);
    ASSERT_NE(ua, nullptr);
    ua->origin = CSS_ORIGIN_USER_AGENT;
    ASSERT_NE(style_tree_apply_declaration(tree, ua), nullptr);
    EXPECT_EQ(style_tree_get_component_declaration(tree, sources, 4), ua);
}

// ============================================================================
// Issue 1: Universal Selector Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, UniversalSelector_MatchesAllElements) {
    // Create universal selector: * { }
    CssSimpleSelector* selector = create_universal_simple_selector();
    ASSERT_NE(selector, nullptr);

    // Test that it matches various elements
    DomElement* div = DomElement::create(doc, "div", nullptr);
    DomElement* span = DomElement::create(doc, "span", nullptr);
    DomElement* body = DomElement::create(doc, "body", nullptr);

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, selector, div))
        << "Universal selector should match <div>";
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, selector, span))
        << "Universal selector should match <span>";
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, selector, body))
        << "Universal selector should match <body>";
}

TEST_F(CssStyleApplicationTest, UniversalSelector_AppliesMarginReset) {
    // CSS: * { margin: 0; }
    CssDeclaration* margin_decl = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);

    // Create body element
    DomElement* body = DomElement::create(doc, "body", nullptr);

    // Apply declaration
    ASSERT_TRUE(dom_element_apply_declaration(body, margin_decl));

    // Verify margin was applied
    CssDeclaration* retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr) << "Margin property should be set by universal selector";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "0");
}

TEST_F(CssStyleApplicationTest, UniversalSelector_OverriddenByTypeSelector) {
    // CSS:
    // * { margin: 0; }  - specificity (0,0,0,0)
    // body { margin: 20px; } - specificity (0,0,0,1)

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* body_margin = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 0, 1);

    // Create body element
    DomElement* body = DomElement::create(doc, "body", nullptr);

    // Apply universal selector first (lower specificity)
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_margin));

    // Apply body selector (higher specificity - should override)
    ASSERT_TRUE(dom_element_apply_declaration(body, body_margin));

    // Verify body selector won
    CssDeclaration* retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, body_margin)
        << "Body selector (0,0,0,1) should override universal selector (0,0,0,0)";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "20px");
}

// ============================================================================
// Issue 2: Class Selector Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, ClassSelector_MatchesElementWithClass) {
    // CSS: .box { }
    CssSimpleSelector* selector = create_class_simple_selector("box");

    // Element with matching class
    DomElement* div = DomElement::create(doc, "div", nullptr);
    div->add_class("box");

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, selector, div))
        << "Class selector .box should match <div class='box'>";
}

TEST_F(CssStyleApplicationTest, ClassSelector_DoesNotMatchWithoutClass) {
    // CSS: .box { }
    CssSimpleSelector* selector = create_class_simple_selector("box");

    // Element without the class
    DomElement* div = DomElement::create(doc, "div", nullptr);

    EXPECT_FALSE(selector_matcher_matches_simple(matcher, selector, div))
        << "Class selector .box should NOT match <div> without class";
}

TEST_F(CssStyleApplicationTest, ClassSelector_AppliesMargin) {
    // CSS: .box { margin: 20px; }
    CssDeclaration* margin_decl = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 1, 0);

    // Create element with class
    DomElement* div = DomElement::create(doc, "div", nullptr);
    div->add_class("box");

    // Apply declaration
    ASSERT_TRUE(dom_element_apply_declaration(div, margin_decl));

    // Verify margin was applied
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr) << "Margin should be set by .box class selector";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "20px");
}

TEST_F(CssStyleApplicationTest, ClassSelector_OverridesUniversalSelector) {
    // CSS:
    // * { margin: 0; } - (0,0,0,0)
    // .box { margin: 20px; } - (0,0,1,0)

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* class_margin = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 1, 0);

    // Create element with class
    DomElement* div = DomElement::create(doc, "div", nullptr);
    div->add_class("box");

    // Apply both declarations
    ASSERT_TRUE(dom_element_apply_declaration(div, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(div, class_margin));

    // Verify class selector won
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, class_margin)
        << "Class selector (0,0,1,0) should override universal selector (0,0,0,0)";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "20px");
}

// ============================================================================
// Issue 3: Combined Universal and Class Selector Test (Baseline 803)
// ============================================================================

TEST_F(CssStyleApplicationTest, Baseline803_UniversalAndClassSelectors) {
    // Reproduce baseline_803_basic_margin.html issue
    // CSS:
    // * { margin: 0; padding: 0; }
    // body { font-family: Arial, sans-serif; }
    // .box { margin: 20px; }

    // Create declarations
    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* universal_padding = create_declaration(CSS_PROPERTY_PADDING, "0", 0, 0, 0);
    CssDeclaration* body_font = create_declaration(CSS_PROPERTY_FONT_FAMILY, "Arial, sans-serif", 0, 0, 1);
    CssDeclaration* box_margin = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 1, 0);

    // Create DOM: <body><div class="box"></div></body>
    DomElement* body = DomElement::create(doc, "body", nullptr);
    DomElement* div_box = DomElement::create(doc, "div", nullptr);
    div_box->add_class("box");
    body->append_child(div_box);

    // Apply rules to body
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_padding));
    ASSERT_TRUE(dom_element_apply_declaration(body, body_font));

    // Verify body has margin: 0 (from universal selector)
    CssDeclaration* body_margin_retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(body_margin_retrieved, nullptr) << "Body should have margin property from universal selector";
    EXPECT_STREQ(css_serialize_declaration_value(body_margin_retrieved, pool), "0")
        << "Body margin should be 0 from universal selector, not 20";

    // Apply rules to div.box
    ASSERT_TRUE(dom_element_apply_declaration(div_box, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(div_box, box_margin));

    // Verify div.box has margin: 20px (from class selector overriding universal)
    CssDeclaration* box_margin_retrieved = dom_element_get_specified_value(div_box, CSS_PROPERTY_MARGIN);
    ASSERT_NE(box_margin_retrieved, nullptr) << "Div.box should have margin property";
    EXPECT_EQ(box_margin_retrieved, box_margin)
        << "Div.box margin should be from .box class declaration";
    EXPECT_STREQ(css_serialize_declaration_value(box_margin_retrieved, pool), "20px")
        << "Div.box margin should be 20px from .box class, not 0 from universal";
}

// ============================================================================
// Issue 4: Cascade Order Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, CascadeOrder_LaterRuleSameSpecificity) {
    // CSS:
    // .box { margin: 10px; }
    // .box { margin: 20px; }
    // Later rule with same specificity should win

    CssDeclaration* margin1 = create_declaration(CSS_PROPERTY_MARGIN, "10px", 0, 1, 0);
    CssDeclaration* margin2 = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 1, 0);

    DomElement* div = DomElement::create(doc, "div", nullptr);
    div->add_class("box");

    // Apply first declaration
    ASSERT_TRUE(dom_element_apply_declaration(div, margin1));

    // Apply second declaration (same specificity, should override due to source order)
    ASSERT_TRUE(dom_element_apply_declaration(div, margin2));

    // Verify second declaration won
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, margin2)
        << "Later declaration with same specificity should win";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "20px");
}

TEST_F(CssStyleApplicationTest, CascadeOrder_SpecificityOverridesSourceOrder) {
    // CSS:
    // .box { margin: 10px; }      /* specificity: (0,0,1,0) */
    // * { margin: 20px; }         /* specificity: (0,0,0,0) */
    // Higher specificity wins even if it comes first

    CssDeclaration* class_margin = create_declaration(CSS_PROPERTY_MARGIN, "10px", 0, 1, 0);
    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 0, 0);

    DomElement* div = DomElement::create(doc, "div", nullptr);
    div->add_class("box");

    // Apply class declaration first (higher specificity)
    ASSERT_TRUE(dom_element_apply_declaration(div, class_margin));

    // Apply universal declaration later (lower specificity - should NOT override)
    ASSERT_TRUE(dom_element_apply_declaration(div, universal_margin));

    // Verify class declaration still wins
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, class_margin)
        << "Class selector should win over universal even when applied first";
    EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "10px");
}

// ============================================================================
// Specificity Calculation Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, Specificity_OrdinaryPseudoClassesAndElements) {
    struct Case {
        const char* selector;
        uint8_t classes;
        uint8_t elements;
    } cases[] = {
        {":checked", 1, 0},
        {":only-of-type", 1, 0},
        {":nth-of-type(2)", 1, 0},
        {"::marker", 0, 1},
        {"::before", 0, 1},
        {":where(#target)", 0, 0},
    };
    for (const Case& test_case : cases) {
        CssSelectorGroup* group = css_parse_selector_group_text(
            test_case.selector, strlen(test_case.selector), pool);
        ASSERT_NE(group, nullptr) << test_case.selector;
        ASSERT_EQ(group->selector_count, 1u) << test_case.selector;
        CssSpecificity specificity = selector_matcher_calculate_specificity(
            matcher, group->selectors[0]);
        EXPECT_EQ(specificity.ids, 0) << test_case.selector;
        EXPECT_EQ(specificity.classes, test_case.classes) << test_case.selector;
        EXPECT_EQ(specificity.elements, test_case.elements) << test_case.selector;
    }
}

TEST_F(CssStyleApplicationTest, Specificity_UniversalSelector) {
    // Universal selector should have specificity (0,0,0,0)
    CssSelector* selector = create_selector_from_simple(create_universal_simple_selector());

    CssSpecificity spec = selector_matcher_calculate_specificity(matcher, selector);

    EXPECT_EQ(spec.inline_style, 0);
    EXPECT_EQ(spec.ids, 0);
    EXPECT_EQ(spec.classes, 0);
    EXPECT_EQ(spec.elements, 0);
}

TEST_F(CssStyleApplicationTest, Specificity_ClassSelector) {
    // Class selector should have specificity (0,0,1,0)
    CssSelector* selector = create_selector_from_simple(create_class_simple_selector("box"));

    CssSpecificity spec = selector_matcher_calculate_specificity(matcher, selector);

    EXPECT_EQ(spec.inline_style, 0);
    EXPECT_EQ(spec.ids, 0);
    EXPECT_EQ(spec.classes, 1);
    EXPECT_EQ(spec.elements, 0);
}

TEST_F(CssStyleApplicationTest, Specificity_ElementSelector) {
    // Element selector should have specificity (0,0,0,1)
    CssSelector* selector = create_selector_from_simple(create_element_simple_selector("div"));

    CssSpecificity spec = selector_matcher_calculate_specificity(matcher, selector);

    EXPECT_EQ(spec.inline_style, 0);
    EXPECT_EQ(spec.ids, 0);
    EXPECT_EQ(spec.classes, 0);
    EXPECT_EQ(spec.elements, 1);
}

// ============================================================================
// Multiple Elements Integration Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, MultipleElements_UniversalSelectorAffectsAll) {
    // CSS: * { margin: 0; }
    CssDeclaration* margin_decl = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);

    // Create multiple elements
    DomElement* html = DomElement::create(doc, "html", nullptr);
    DomElement* body = DomElement::create(doc, "body", nullptr);
    DomElement* div1 = DomElement::create(doc, "div", nullptr);
    DomElement* div2 = DomElement::create(doc, "div", nullptr);
    DomElement* span = DomElement::create(doc, "span", nullptr);

    html->append_child(body);
    body->append_child(div1);
    body->append_child(div2);
    div1->append_child(span);

    // Apply universal selector declaration to all elements
    DomElement* elements[] = { html, body, div1, div2, span };
    for (int i = 0; i < 5; i++) {
        // Verify universal selector matches
        CssSimpleSelector* universal = create_universal_simple_selector();
        ASSERT_TRUE(selector_matcher_matches_simple(matcher, universal, elements[i]))
            << "Universal selector should match element " << i;

        // Apply declaration
        ASSERT_TRUE(dom_element_apply_declaration(elements[i], margin_decl));

        // Verify margin was applied
        CssDeclaration* retrieved = dom_element_get_specified_value(elements[i], CSS_PROPERTY_MARGIN);
        ASSERT_NE(retrieved, nullptr)
            << "Element " << i << " should have margin from universal selector";
        EXPECT_STREQ(css_serialize_declaration_value(retrieved, pool), "0");
    }
}

TEST_F(CssStyleApplicationTest, MultipleClasses_SelectiveApplication) {
    // CSS:
    // * { margin: 0; }
    // .highlight { background-color: yellow; }

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* highlight_bg = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "yellow", 0, 1, 0);

    // Create elements, some with .highlight class
    DomElement* div1 = DomElement::create(doc, "div", nullptr);
    div1->add_class("highlight");

    DomElement* div2 = DomElement::create(doc, "div", nullptr);

    DomElement* span = DomElement::create(doc, "span", nullptr);
    span->add_class("highlight");

    // Apply universal selector to all
    DomElement* elements[] = { div1, div2, span };
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(dom_element_apply_declaration(elements[i], universal_margin));
    }

    // Verify .highlight class selector matches correctly
    CssSimpleSelector* highlight_sel = create_class_simple_selector("highlight");
    ASSERT_TRUE(selector_matcher_matches_simple(matcher, highlight_sel, div1));
    ASSERT_FALSE(selector_matcher_matches_simple(matcher, highlight_sel, div2))
        << "div2 does not have .highlight class";
    ASSERT_TRUE(selector_matcher_matches_simple(matcher, highlight_sel, span));

    // Apply .highlight selector only to matching elements
    ASSERT_TRUE(dom_element_apply_declaration(div1, highlight_bg));
    ASSERT_TRUE(dom_element_apply_declaration(span, highlight_bg));

    // Verify only elements with .highlight have background-color
    EXPECT_NE(dom_element_get_specified_value(div1, CSS_PROPERTY_BACKGROUND_COLOR), nullptr);
    EXPECT_EQ(dom_element_get_specified_value(div2, CSS_PROPERTY_BACKGROUND_COLOR), nullptr);
    EXPECT_NE(dom_element_get_specified_value(span, CSS_PROPERTY_BACKGROUND_COLOR), nullptr);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
