#include <gtest/gtest.h>

#include "../../lambda/lambda.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/selector_matcher.hpp"
#include "../../lambda/input/css/css_style.hpp"
#include "../../lambda/input/css/css_style_node.hpp"

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
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Create CSS declaration with specificity
    CssDeclaration* create_declaration(CssPropertyId prop_id, const char* value,
                                      uint8_t ids = 0, uint8_t classes = 0,
                                      uint8_t elements = 0) {
        char* val = (char*)pool_alloc(pool, strlen(value) + 1);
        strcpy(val, value);

        CssSpecificity spec = css_specificity_create(0, ids, classes, elements, false);
        return css_declaration_create(prop_id, val, spec, CSS_ORIGIN_AUTHOR, pool);
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

// ============================================================================
// Issue 1: Universal Selector Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, UniversalSelector_MatchesAllElements) {
    // Create universal selector: * { }
    CssSimpleSelector* selector = create_universal_simple_selector();
    ASSERT_NE(selector, nullptr);

    // Test that it matches various elements
    DomElement* div = dom_element_create(doc, "div", nullptr);
    DomElement* span = dom_element_create(doc, "span", nullptr);
    DomElement* body = dom_element_create(doc, "body", nullptr);

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
    DomElement* body = dom_element_create(doc, "body", nullptr);

    // Apply declaration
    ASSERT_TRUE(dom_element_apply_declaration(body, margin_decl));

    // Verify margin was applied
    CssDeclaration* retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr) << "Margin property should be set by universal selector";
    EXPECT_STREQ((char*)retrieved->value, "0");
}

TEST_F(CssStyleApplicationTest, UniversalSelector_OverriddenByTypeSelector) {
    // CSS:
    // * { margin: 0; }  - specificity (0,0,0,0)
    // body { margin: 20px; } - specificity (0,0,0,1)

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* body_margin = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 0, 1);

    // Create body element
    DomElement* body = dom_element_create(doc, "body", nullptr);

    // Apply universal selector first (lower specificity)
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_margin));

    // Apply body selector (higher specificity - should override)
    ASSERT_TRUE(dom_element_apply_declaration(body, body_margin));

    // Verify body selector won
    CssDeclaration* retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, body_margin)
        << "Body selector (0,0,0,1) should override universal selector (0,0,0,0)";
    EXPECT_STREQ((char*)retrieved->value, "20");
}

// ============================================================================
// Issue 2: Class Selector Tests
// ============================================================================

TEST_F(CssStyleApplicationTest, ClassSelector_MatchesElementWithClass) {
    // CSS: .box { }
    CssSimpleSelector* selector = create_class_simple_selector("box");

    // Element with matching class
    DomElement* div = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div, "box");

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, selector, div))
        << "Class selector .box should match <div class='box'>";
}

TEST_F(CssStyleApplicationTest, ClassSelector_DoesNotMatchWithoutClass) {
    // CSS: .box { }
    CssSimpleSelector* selector = create_class_simple_selector("box");

    // Element without the class
    DomElement* div = dom_element_create(doc, "div", nullptr);

    EXPECT_FALSE(selector_matcher_matches_simple(matcher, selector, div))
        << "Class selector .box should NOT match <div> without class";
}

TEST_F(CssStyleApplicationTest, ClassSelector_AppliesMargin) {
    // CSS: .box { margin: 20px; }
    CssDeclaration* margin_decl = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 1, 0);

    // Create element with class
    DomElement* div = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div, "box");

    // Apply declaration
    ASSERT_TRUE(dom_element_apply_declaration(div, margin_decl));

    // Verify margin was applied
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr) << "Margin should be set by .box class selector";
    EXPECT_STREQ((char*)retrieved->value, "20");
}

TEST_F(CssStyleApplicationTest, ClassSelector_OverridesUniversalSelector) {
    // CSS:
    // * { margin: 0; } - (0,0,0,0)
    // .box { margin: 20px; } - (0,0,1,0)

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* class_margin = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 1, 0);

    // Create element with class
    DomElement* div = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div, "box");

    // Apply both declarations
    ASSERT_TRUE(dom_element_apply_declaration(div, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(div, class_margin));

    // Verify class selector won
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, class_margin)
        << "Class selector (0,0,1,0) should override universal selector (0,0,0,0)";
    EXPECT_STREQ((char*)retrieved->value, "20");
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
    CssDeclaration* box_margin = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 1, 0);

    // Create DOM: <body><div class="box"></div></body>
    DomElement* body = dom_element_create(doc, "body", nullptr);
    DomElement* div_box = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div_box, "box");
    dom_element_append_child(body, div_box);

    // Apply rules to body
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(body, universal_padding));
    ASSERT_TRUE(dom_element_apply_declaration(body, body_font));

    // Verify body has margin: 0 (from universal selector)
    CssDeclaration* body_margin_retrieved = dom_element_get_specified_value(body, CSS_PROPERTY_MARGIN);
    ASSERT_NE(body_margin_retrieved, nullptr) << "Body should have margin property from universal selector";
    EXPECT_STREQ((char*)body_margin_retrieved->value, "0")
        << "Body margin should be 0 from universal selector, not 20";

    // Apply rules to div.box
    ASSERT_TRUE(dom_element_apply_declaration(div_box, universal_margin));
    ASSERT_TRUE(dom_element_apply_declaration(div_box, box_margin));

    // Verify div.box has margin: 20px (from class selector overriding universal)
    CssDeclaration* box_margin_retrieved = dom_element_get_specified_value(div_box, CSS_PROPERTY_MARGIN);
    ASSERT_NE(box_margin_retrieved, nullptr) << "Div.box should have margin property";
    EXPECT_EQ(box_margin_retrieved, box_margin)
        << "Div.box margin should be from .box class declaration";
    EXPECT_STREQ((char*)box_margin_retrieved->value, "20")
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

    CssDeclaration* margin1 = create_declaration(CSS_PROPERTY_MARGIN, "10", 0, 1, 0);
    CssDeclaration* margin2 = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 1, 0);

    DomElement* div = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div, "box");

    // Apply first declaration
    ASSERT_TRUE(dom_element_apply_declaration(div, margin1));

    // Apply second declaration (same specificity, should override due to source order)
    ASSERT_TRUE(dom_element_apply_declaration(div, margin2));

    // Verify second declaration won
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, margin2)
        << "Later declaration with same specificity should win";
    EXPECT_STREQ((char*)retrieved->value, "20");
}

TEST_F(CssStyleApplicationTest, CascadeOrder_SpecificityOverridesSourceOrder) {
    // CSS:
    // .box { margin: 10px; }      /* specificity: (0,0,1,0) */
    // * { margin: 20px; }         /* specificity: (0,0,0,0) */
    // Higher specificity wins even if it comes first

    CssDeclaration* class_margin = create_declaration(CSS_PROPERTY_MARGIN, "10", 0, 1, 0);
    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "20", 0, 0, 0);

    DomElement* div = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div, "box");

    // Apply class declaration first (higher specificity)
    ASSERT_TRUE(dom_element_apply_declaration(div, class_margin));

    // Apply universal declaration later (lower specificity - should NOT override)
    ASSERT_TRUE(dom_element_apply_declaration(div, universal_margin));

    // Verify class declaration still wins
    CssDeclaration* retrieved = dom_element_get_specified_value(div, CSS_PROPERTY_MARGIN);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, class_margin)
        << "Class selector should win over universal even when applied first";
    EXPECT_STREQ((char*)retrieved->value, "10");
}

// ============================================================================
// Specificity Calculation Tests
// ============================================================================

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
    DomElement* html = dom_element_create(doc, "html", nullptr);
    DomElement* body = dom_element_create(doc, "body", nullptr);
    DomElement* div1 = dom_element_create(doc, "div", nullptr);
    DomElement* div2 = dom_element_create(doc, "div", nullptr);
    DomElement* span = dom_element_create(doc, "span", nullptr);

    dom_element_append_child(html, body);
    dom_element_append_child(body, div1);
    dom_element_append_child(body, div2);
    dom_element_append_child(div1, span);

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
        EXPECT_STREQ((char*)retrieved->value, "0");
    }
}

TEST_F(CssStyleApplicationTest, MultipleClasses_SelectiveApplication) {
    // CSS:
    // * { margin: 0; }
    // .highlight { background-color: yellow; }

    CssDeclaration* universal_margin = create_declaration(CSS_PROPERTY_MARGIN, "0", 0, 0, 0);
    CssDeclaration* highlight_bg = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "yellow", 0, 1, 0);

    // Create elements, some with .highlight class
    DomElement* div1 = dom_element_create(doc, "div", nullptr);
    dom_element_add_class(div1, "highlight");

    DomElement* div2 = dom_element_create(doc, "div", nullptr);

    DomElement* span = dom_element_create(doc, "span", nullptr);
    dom_element_add_class(span, "highlight");

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
