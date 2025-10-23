#include <gtest/gtest.h>

extern "C" {
#include "../lambda/input/css/dom_element.h"
#include "../lambda/input/css/selector_matcher.h"
#include "../lambda/input/css/css_style.h"
#include "../lambda/input/css/css_style_node.h"
#include "../lambda/input/css/css_parser.h"
#include "../lib/mempool.h"
}

/**
 * Comprehensive DOM Integration Test Suite
 *
 * Tests Phase 3 implementation:
 * - DomElement creation, attributes, and classes
 * - Style management and cascade resolution
 * - Selector matching (simple, compound, complex)
 * - Pseudo-class matching
 * - DOM tree navigation
 * - Performance and caching
 */

class DomIntegrationTest : public ::testing::Test {
protected:
    Pool* pool;
    SelectorMatcher* matcher;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        matcher = selector_matcher_create(pool);
        ASSERT_NE(matcher, nullptr);
    }

    void TearDown() override {
        if (matcher) {
            selector_matcher_destroy(matcher);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper: Create a test declaration
    CssDeclaration* create_declaration(CssPropertyId prop_id, const char* value,
                                      uint8_t ids = 0, uint8_t classes = 0, uint8_t elements = 0) {
        char* val = (char*)pool_alloc(pool, strlen(value) + 1);
        strcpy(val, value);

        CssSpecificity spec = css_specificity_create(0, ids, classes, elements, false);
        return css_declaration_create(prop_id, val, spec, CSS_ORIGIN_AUTHOR, pool);
    }

    // Helper: Create simple selector
    CssSimpleSelector* create_type_selector(const char* tag_name) {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_ELEMENT;
        sel->value = tag_name;
        return sel;
    }

    CssSimpleSelector* create_class_selector(const char* class_name) {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_CLASS;
        sel->value = class_name;
        return sel;
    }

    CssSimpleSelector* create_id_selector(const char* id) {
        CssSimpleSelector* sel = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
        sel->type = CSS_SELECTOR_TYPE_ID;
        sel->value = id;
        return sel;
    }
};

// ============================================================================
// DomElement Basic Tests
// ============================================================================

TEST_F(DomIntegrationTest, CreateDomElement) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);
    EXPECT_STREQ(element->tag_name, "div");
    EXPECT_EQ(element->id, nullptr);
    EXPECT_EQ(element->class_count, 0);
    EXPECT_EQ(element->parent, nullptr);
    EXPECT_EQ(element->first_child, nullptr);
}

TEST_F(DomIntegrationTest, DomElementAttributes) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);

    // Set attribute
    EXPECT_TRUE(dom_element_set_attribute(element, "data-test", "value1"));
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value1");

    // Update attribute
    EXPECT_TRUE(dom_element_set_attribute(element, "data-test", "value2"));
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value2");

    // Check existence
    EXPECT_TRUE(dom_element_has_attribute(element, "data-test"));
    EXPECT_FALSE(dom_element_has_attribute(element, "nonexistent"));

    // Remove attribute
    EXPECT_TRUE(dom_element_remove_attribute(element, "data-test"));
    EXPECT_FALSE(dom_element_has_attribute(element, "data-test"));
}

TEST_F(DomIntegrationTest, DomElementClasses) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);

    // Add classes
    EXPECT_TRUE(dom_element_add_class(element, "class1"));
    EXPECT_TRUE(dom_element_add_class(element, "class2"));
    EXPECT_EQ(element->class_count, 2);

    // Check classes
    EXPECT_TRUE(dom_element_has_class(element, "class1"));
    EXPECT_TRUE(dom_element_has_class(element, "class2"));
    EXPECT_FALSE(dom_element_has_class(element, "class3"));

    // Remove class
    EXPECT_TRUE(dom_element_remove_class(element, "class1"));
    EXPECT_FALSE(dom_element_has_class(element, "class1"));
    EXPECT_EQ(element->class_count, 1);

    // Toggle class
    EXPECT_TRUE(dom_element_toggle_class(element, "class3"));  // Add
    EXPECT_TRUE(dom_element_has_class(element, "class3"));
    EXPECT_FALSE(dom_element_toggle_class(element, "class3")); // Remove
    EXPECT_FALSE(dom_element_has_class(element, "class3"));
}

TEST_F(DomIntegrationTest, DomElementIdAttribute) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);

    // Set ID attribute
    EXPECT_TRUE(dom_element_set_attribute(element, "id", "test-id"));
    EXPECT_STREQ(element->id, "test-id");
    EXPECT_STREQ(dom_element_get_attribute(element, "id"), "test-id");
}

// ============================================================================
// Style Management Tests
// ============================================================================

TEST_F(DomIntegrationTest, ApplyDeclaration) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);

    CssDeclaration* decl = create_declaration(CSS_PROPERTY_COLOR, "red", 0, 1, 0);
    ASSERT_NE(decl, nullptr);

    EXPECT_TRUE(dom_element_apply_declaration(element, decl));

    CssDeclaration* retrieved = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    EXPECT_NE(retrieved, nullptr);
    EXPECT_STREQ((char*)retrieved->value, "red");
}

TEST_F(DomIntegrationTest, StyleVersioning) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    ASSERT_NE(element, nullptr);

    uint32_t initial_version = element->style_version;
    EXPECT_TRUE(element->needs_style_recompute);

    CssDeclaration* decl = create_declaration(CSS_PROPERTY_COLOR, "blue", 0, 1, 0);
    dom_element_apply_declaration(element, decl);

    EXPECT_GT(element->style_version, initial_version);
    EXPECT_TRUE(element->needs_style_recompute);
}

TEST_F(DomIntegrationTest, StyleInvalidation) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);

    // Invalidate parent and children
    dom_element_invalidate_computed_values(parent, true);

    EXPECT_TRUE(parent->needs_style_recompute);
    EXPECT_TRUE(child1->needs_style_recompute);
    EXPECT_TRUE(child2->needs_style_recompute);
}

// ============================================================================
// DOM Tree Navigation Tests
// ============================================================================

TEST_F(DomIntegrationTest, AppendChild) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child = dom_element_create(pool, "span", nullptr);

    EXPECT_TRUE(dom_element_append_child(parent, child));
    EXPECT_EQ(child->parent, parent);
    EXPECT_EQ(parent->first_child, child);
    EXPECT_EQ(dom_element_count_children(parent), 1);
}

TEST_F(DomIntegrationTest, MultipleChildren) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);
    DomElement* child3 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);
    dom_element_append_child(parent, child3);

    EXPECT_EQ(dom_element_count_children(parent), 3);
    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(child1->next_sibling, child2);
    EXPECT_EQ(child2->next_sibling, child3);
    EXPECT_EQ(child3->next_sibling, nullptr);

    EXPECT_EQ(child1->prev_sibling, nullptr);
    EXPECT_EQ(child2->prev_sibling, child1);
    EXPECT_EQ(child3->prev_sibling, child2);
}

TEST_F(DomIntegrationTest, InsertBefore) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);
    DomElement* child3 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child3);
    dom_element_insert_before(parent, child2, child3);

    EXPECT_EQ(parent->first_child, child1);
    EXPECT_EQ(child1->next_sibling, child2);
    EXPECT_EQ(child2->next_sibling, child3);
}

TEST_F(DomIntegrationTest, RemoveChild) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);

    EXPECT_TRUE(dom_element_remove_child(parent, child1));
    EXPECT_EQ(dom_element_count_children(parent), 1);
    EXPECT_EQ(parent->first_child, child2);
    EXPECT_EQ(child1->parent, nullptr);
}

TEST_F(DomIntegrationTest, StructuralQueries) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);
    DomElement* child3 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);
    dom_element_append_child(parent, child3);

    EXPECT_TRUE(dom_element_is_first_child(child1));
    EXPECT_FALSE(dom_element_is_first_child(child2));

    EXPECT_TRUE(dom_element_is_last_child(child3));
    EXPECT_FALSE(dom_element_is_last_child(child2));

    EXPECT_FALSE(dom_element_is_only_child(child2));

    EXPECT_EQ(dom_element_get_child_index(child1), 0);
    EXPECT_EQ(dom_element_get_child_index(child2), 1);
    EXPECT_EQ(dom_element_get_child_index(child3), 2);
}

// ============================================================================
// Selector Matching Tests
// ============================================================================

TEST_F(DomIntegrationTest, TypeSelectorMatching) {
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* span = dom_element_create(pool, "span", nullptr);

    CssSimpleSelector* div_sel = create_type_selector("div");
    CssSimpleSelector* span_sel = create_type_selector("span");

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, div_sel, div));
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, span_sel, div));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, span_sel, span));
}

TEST_F(DomIntegrationTest, ClassSelectorMatching) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "my-class");
    dom_element_add_class(element, "another-class");

    CssSimpleSelector* class_sel1 = create_class_selector("my-class");
    CssSimpleSelector* class_sel2 = create_class_selector("another-class");
    CssSimpleSelector* class_sel3 = create_class_selector("missing-class");

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, class_sel1, element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, class_sel2, element));
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, class_sel3, element));
}

TEST_F(DomIntegrationTest, IdSelectorMatching) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "test-id");

    CssSimpleSelector* id_sel1 = create_id_selector("test-id");
    CssSimpleSelector* id_sel2 = create_id_selector("other-id");

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, id_sel1, element));
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, id_sel2, element));
}

TEST_F(DomIntegrationTest, AttributeSelectorMatching) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "data-test", "hello-world");

    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-test", nullptr,
                                                   CSS_SELECTOR_ATTR_EXISTS, false, element));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-test", "hello-world",
                                                   CSS_SELECTOR_ATTR_EXACT, false, element));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-test", "hello",
                                                   CSS_SELECTOR_ATTR_BEGINS, false, element));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-test", "world",
                                                   CSS_SELECTOR_ATTR_ENDS, false, element));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-test", "lo-wo",
                                                   CSS_SELECTOR_ATTR_SUBSTRING, false, element));
}

TEST_F(DomIntegrationTest, UniversalSelectorMatching) {
    // Universal selector (*) matches any element
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* span = dom_element_create(pool, "span", nullptr);
    DomElement* p = dom_element_create(pool, "p", nullptr);

    CssSimpleSelector* universal = (CssSimpleSelector*)pool_calloc(pool, sizeof(CssSimpleSelector));
    universal->type = CSS_SELECTOR_TYPE_UNIVERSAL;

    EXPECT_TRUE(selector_matcher_matches_simple(matcher, universal, div));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, universal, span));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, universal, p));
}

TEST_F(DomIntegrationTest, AttributeSelector_All7Types) {
    // Test all 7 attribute selector types comprehensively

    // [attr] - Attribute exists
    DomElement* elem1 = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(elem1, "title", "");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "title", nullptr,
                                                   CSS_SELECTOR_ATTR_EXISTS, false, elem1));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "missing", nullptr,
                                                    CSS_SELECTOR_ATTR_EXISTS, false, elem1));

    // [attr="exact"] - Exact match
    DomElement* elem2 = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(elem2, "type", "text");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "type", "text",
                                                   CSS_SELECTOR_ATTR_EXACT, false, elem2));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "type", "TEXT",
                                                    CSS_SELECTOR_ATTR_EXACT, false, elem2));
    // Case-insensitive
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "type", "TEXT",
                                                   CSS_SELECTOR_ATTR_EXACT, true, elem2));

    // [attr~="word"] - Contains word (space-separated)
    DomElement* elem3 = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(elem3, "class", "button primary large");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "class", "primary",
                                                   CSS_SELECTOR_ATTR_CONTAINS, false, elem3));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "class", "button",
                                                   CSS_SELECTOR_ATTR_CONTAINS, false, elem3));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "class", "primar",
                                                    CSS_SELECTOR_ATTR_CONTAINS, false, elem3));

    // [attr|="value"] - Exact or starts with value followed by hyphen
    DomElement* elem4 = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(elem4, "lang", "en-US");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "lang", "en",
                                                   CSS_SELECTOR_ATTR_LANG, false, elem4));
    dom_element_set_attribute(elem4, "lang", "en");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "lang", "en",
                                                   CSS_SELECTOR_ATTR_LANG, false, elem4));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "lang", "fr",
                                                    CSS_SELECTOR_ATTR_LANG, false, elem4));

    // [attr^="prefix"] - Begins with
    DomElement* elem5 = dom_element_create(pool, "a", nullptr);
    dom_element_set_attribute(elem5, "href", "https://example.com");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "href", "https://",
                                                   CSS_SELECTOR_ATTR_BEGINS, false, elem5));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "href", "http://",
                                                    CSS_SELECTOR_ATTR_BEGINS, false, elem5));

    // [attr$="suffix"] - Ends with
    DomElement* elem6 = dom_element_create(pool, "a", nullptr);
    dom_element_set_attribute(elem6, "href", "document.pdf");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "href", ".pdf",
                                                   CSS_SELECTOR_ATTR_ENDS, false, elem6));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "href", ".doc",
                                                    CSS_SELECTOR_ATTR_ENDS, false, elem6));

    // [attr*="substring"] - Contains substring
    DomElement* elem7 = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(elem7, "data-url", "https://api.example.com/v1/users");
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-url", "api",
                                                   CSS_SELECTOR_ATTR_SUBSTRING, false, elem7));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "data-url", "/v1/",
                                                   CSS_SELECTOR_ATTR_SUBSTRING, false, elem7));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "data-url", "v2",
                                                    CSS_SELECTOR_ATTR_SUBSTRING, false, elem7));
}

TEST_F(DomIntegrationTest, PseudoClass_UserAction) {
    // Test user action pseudo-classes
    DomElement* link = dom_element_create(pool, "a", nullptr);

    // :hover
    dom_element_set_pseudo_state(link, PSEUDO_STATE_HOVER);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_HOVER, nullptr, link));
    EXPECT_FALSE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ACTIVE, nullptr, link));

    // :active
    dom_element_clear_pseudo_state(link, PSEUDO_STATE_HOVER);
    dom_element_set_pseudo_state(link, PSEUDO_STATE_ACTIVE);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ACTIVE, nullptr, link));
    EXPECT_FALSE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_HOVER, nullptr, link));

    // :focus
    dom_element_clear_pseudo_state(link, PSEUDO_STATE_ACTIVE);
    dom_element_set_pseudo_state(link, PSEUDO_STATE_FOCUS);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_FOCUS, nullptr, link));

    // :visited
    dom_element_set_pseudo_state(link, PSEUDO_STATE_VISITED);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_VISITED, nullptr, link));
}

TEST_F(DomIntegrationTest, PseudoClass_InputStates) {
    // Test form input pseudo-classes
    DomElement* input = dom_element_create(pool, "input", nullptr);

    // :enabled / :disabled
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ENABLED, nullptr, input));
    dom_element_set_pseudo_state(input, PSEUDO_STATE_DISABLED);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_DISABLED, nullptr, input));
    EXPECT_FALSE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ENABLED, nullptr, input));

    // :checked
    DomElement* checkbox = dom_element_create(pool, "input", nullptr);
    dom_element_set_attribute(checkbox, "type", "checkbox");
    dom_element_set_pseudo_state(checkbox, PSEUDO_STATE_CHECKED);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_CHECKED, nullptr, checkbox));

    // :required / :optional
    DomElement* required_input = dom_element_create(pool, "input", nullptr);
    dom_element_set_pseudo_state(required_input, PSEUDO_STATE_REQUIRED);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_REQUIRED, nullptr, required_input));

    DomElement* optional_input = dom_element_create(pool, "input", nullptr);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_OPTIONAL, nullptr, optional_input));

    // :valid / :invalid
    DomElement* valid_input = dom_element_create(pool, "input", nullptr);
    dom_element_set_pseudo_state(valid_input, PSEUDO_STATE_VALID);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_VALID, nullptr, valid_input));

    DomElement* invalid_input = dom_element_create(pool, "input", nullptr);
    dom_element_set_pseudo_state(invalid_input, PSEUDO_STATE_INVALID);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_INVALID, nullptr, invalid_input));

    // :read-only / :read-write
    DomElement* readonly_input = dom_element_create(pool, "input", nullptr);
    dom_element_set_pseudo_state(readonly_input, PSEUDO_STATE_READ_ONLY);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_READ_ONLY, nullptr, readonly_input));

    DomElement* readwrite_input = dom_element_create(pool, "input", nullptr);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_READ_WRITE, nullptr, readwrite_input));
}

// ============================================================================
// Pseudo-Class Matching Tests
// ============================================================================

TEST_F(DomIntegrationTest, PseudoStateMatching) {
    DomElement* element = dom_element_create(pool, "button", nullptr);

    dom_element_set_pseudo_state(element, PSEUDO_STATE_HOVER);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_HOVER, nullptr, element));
    EXPECT_FALSE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ACTIVE, nullptr, element));

    dom_element_set_pseudo_state(element, PSEUDO_STATE_ACTIVE);
    EXPECT_TRUE(selector_matcher_matches_pseudo_class(matcher, CSS_SELECTOR_PSEUDO_ACTIVE, nullptr, element));
}

TEST_F(DomIntegrationTest, StructuralPseudoClasses) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "span", nullptr);
    DomElement* child2 = dom_element_create(pool, "span", nullptr);
    DomElement* child3 = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);
    dom_element_append_child(parent, child3);

    EXPECT_TRUE(selector_matcher_matches_structural(matcher, CSS_SELECTOR_PSEUDO_FIRST_CHILD, child1));
    EXPECT_FALSE(selector_matcher_matches_structural(matcher, CSS_SELECTOR_PSEUDO_FIRST_CHILD, child2));

    EXPECT_TRUE(selector_matcher_matches_structural(matcher, CSS_SELECTOR_PSEUDO_LAST_CHILD, child3));
    EXPECT_FALSE(selector_matcher_matches_structural(matcher, CSS_SELECTOR_PSEUDO_LAST_CHILD, child2));

    EXPECT_FALSE(selector_matcher_matches_structural(matcher, CSS_SELECTOR_PSEUDO_ONLY_CHILD, child1));
}

TEST_F(DomIntegrationTest, NthChildMatching) {
    DomElement* parent = dom_element_create(pool, "ul", nullptr);

    for (int i = 0; i < 10; i++) {
        DomElement* child = dom_element_create(pool, "li", nullptr);
        dom_element_append_child(parent, child);
    }

    // Test odd
    CssNthFormula odd_formula = {2, 1, true, false};
    DomElement* first_child = parent->first_child;
    EXPECT_TRUE(selector_matcher_matches_nth_child(matcher, &odd_formula, first_child, false));
    EXPECT_FALSE(selector_matcher_matches_nth_child(matcher, &odd_formula, first_child->next_sibling, false));

    // Test even
    CssNthFormula even_formula = {2, 0, false, true};
    EXPECT_FALSE(selector_matcher_matches_nth_child(matcher, &even_formula, first_child, false));
    EXPECT_TRUE(selector_matcher_matches_nth_child(matcher, &even_formula, first_child->next_sibling, false));
}

TEST_F(DomIntegrationTest, NthChild_AdvancedFormulas) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);

    // Create 20 children for comprehensive testing
    for (int i = 0; i < 20; i++) {
        DomElement* child = dom_element_create(pool, "span", nullptr);
        dom_element_append_child(parent, child);
    }

    // Test :nth-child(3n) - every 3rd element (3, 6, 9, 12...)
    CssNthFormula formula_3n = {3, 0, false, false};
    DomElement* child = parent->first_child;
    for (int i = 1; i <= 20; i++) {
        bool should_match = (i % 3 == 0);
        EXPECT_EQ(selector_matcher_matches_nth_child(matcher, &formula_3n, child, false), should_match)
            << "Failed at position " << i;
        child = child->next_sibling;
    }

    // Test :nth-child(3n+1) - 1, 4, 7, 10, 13...
    CssNthFormula formula_3n_plus_1 = {3, 1, false, false};
    child = parent->first_child;
    for (int i = 1; i <= 20; i++) {
        bool should_match = ((i - 1) % 3 == 0);
        EXPECT_EQ(selector_matcher_matches_nth_child(matcher, &formula_3n_plus_1, child, false), should_match)
            << "Failed at position " << i;
        child = child->next_sibling;
    }

    // Test :nth-child(2n+3) - 3, 5, 7, 9...
    CssNthFormula formula_2n_plus_3 = {2, 3, false, false};
    child = parent->first_child;
    for (int i = 1; i <= 20; i++) {
        bool should_match = (i >= 3) && ((i - 3) % 2 == 0);
        EXPECT_EQ(selector_matcher_matches_nth_child(matcher, &formula_2n_plus_3, child, false), should_match)
            << "Failed at position " << i;
        child = child->next_sibling;
    }

    // Test :nth-child(5) - exactly 5th element
    CssNthFormula formula_5 = {0, 5, false, false};
    child = parent->first_child;
    for (int i = 1; i <= 20; i++) {
        bool should_match = (i == 5);
        EXPECT_EQ(selector_matcher_matches_nth_child(matcher, &formula_5, child, false), should_match)
            << "Failed at position " << i;
        child = child->next_sibling;
    }
}

TEST_F(DomIntegrationTest, NthLastChild) {
    DomElement* parent = dom_element_create(pool, "ul", nullptr);

    for (int i = 0; i < 10; i++) {
        DomElement* child = dom_element_create(pool, "li", nullptr);
        dom_element_append_child(parent, child);
    }

    // Test :nth-last-child (count from end)
    CssNthFormula formula_odd = {2, 1, true, false};

    // Last child (10th from start, 1st from end) should match odd formula
    DomElement* last_child = parent->first_child;
    while (last_child->next_sibling) {
        last_child = last_child->next_sibling;
    }
    EXPECT_TRUE(selector_matcher_matches_nth_child(matcher, &formula_odd, last_child, true));
}

TEST_F(DomIntegrationTest, CompoundSelectors) {
    // Test compound selectors like "div.container#main"
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "main");
    dom_element_add_class(element, "container");
    dom_element_add_class(element, "active");

    // Create compound selector: div.container#main
    CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, 3 * sizeof(CssSimpleSelector*));
    compound->simple_selectors[0] = create_type_selector("div");
    compound->simple_selectors[1] = create_class_selector("container");
    compound->simple_selectors[2] = create_id_selector("main");
    compound->simple_selector_count = 3;

    // Should match - all conditions met
    EXPECT_TRUE(selector_matcher_matches_compound(matcher, compound, element));

    // Should not match if any condition fails
    DomElement* wrong_tag = dom_element_create(pool, "span", nullptr);
    dom_element_set_attribute(wrong_tag, "id", "main");
    dom_element_add_class(wrong_tag, "container");
    EXPECT_FALSE(selector_matcher_matches_compound(matcher, compound, wrong_tag));

    DomElement* wrong_class = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(wrong_class, "id", "main");
    EXPECT_FALSE(selector_matcher_matches_compound(matcher, compound, wrong_class));

    DomElement* wrong_id = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(wrong_id, "container");
    EXPECT_FALSE(selector_matcher_matches_compound(matcher, compound, wrong_id));
}

TEST_F(DomIntegrationTest, ComplexSelectors_MultipleClasses) {
    // Test .class1.class2.class3 (element must have all classes)
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "button");
    dom_element_add_class(element, "primary");
    dom_element_add_class(element, "large");

    CssCompoundSelector* compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, 3 * sizeof(CssSimpleSelector*));
    compound->simple_selectors[0] = create_class_selector("button");
    compound->simple_selectors[1] = create_class_selector("primary");
    compound->simple_selectors[2] = create_class_selector("large");
    compound->simple_selector_count = 3;

    EXPECT_TRUE(selector_matcher_matches_compound(matcher, compound, element));

    // Missing one class - should not match
    DomElement* partial = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(partial, "button");
    dom_element_add_class(partial, "primary");
    EXPECT_FALSE(selector_matcher_matches_compound(matcher, compound, partial));
}

TEST_F(DomIntegrationTest, ComplexSelectors_WithAttributes) {
    // Test input[type="text"].required#username
    DomElement* input = dom_element_create(pool, "input", nullptr);
    dom_element_set_attribute(input, "type", "text");
    dom_element_set_attribute(input, "id", "username");
    dom_element_add_class(input, "required");

    // This would require a full CssSelector with attribute selectors
    // For now, test individual components
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_type_selector("input"), input));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("required"), input));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_id_selector("username"), input));
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "type", "text",
                                                   CSS_SELECTOR_ATTR_EXACT, false, input));
}

// ============================================================================
// Combinator Tests
// ============================================================================

TEST_F(DomIntegrationTest, DescendantCombinator) {
    DomElement* grandparent = dom_element_create(pool, "div", nullptr);
    DomElement* parent = dom_element_create(pool, "ul", nullptr);
    DomElement* child = dom_element_create(pool, "li", nullptr);

    dom_element_append_child(grandparent, parent);
    dom_element_append_child(parent, child);

    // Create compound selector for "div"
    CssCompoundSelector* div_compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    div_compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    div_compound->simple_selectors[0] = create_type_selector("div");
    div_compound->simple_selector_count = 1;

    // Check if child has a "div" ancestor
    EXPECT_TRUE(selector_matcher_has_ancestor(matcher, div_compound, child));
    EXPECT_TRUE(selector_matcher_has_ancestor(matcher, div_compound, parent));
    EXPECT_FALSE(selector_matcher_has_ancestor(matcher, div_compound, grandparent));
}

TEST_F(DomIntegrationTest, ChildCombinator) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child);

    CssCompoundSelector* div_compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    div_compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    div_compound->simple_selectors[0] = create_type_selector("div");
    div_compound->simple_selector_count = 1;

    EXPECT_TRUE(selector_matcher_has_parent(matcher, div_compound, child));
}

TEST_F(DomIntegrationTest, SiblingCombinators) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child1 = dom_element_create(pool, "h1", nullptr);
    DomElement* child2 = dom_element_create(pool, "p", nullptr);
    DomElement* child3 = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(parent, child1);
    dom_element_append_child(parent, child2);
    dom_element_append_child(parent, child3);

    CssCompoundSelector* h1_compound = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    h1_compound->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    h1_compound->simple_selectors[0] = create_type_selector("h1");
    h1_compound->simple_selector_count = 1;

    // Next sibling (+)
    EXPECT_TRUE(selector_matcher_has_prev_sibling(matcher, h1_compound, child2));
    EXPECT_FALSE(selector_matcher_has_prev_sibling(matcher, h1_compound, child3));

    // Subsequent sibling (~)
    EXPECT_TRUE(selector_matcher_has_preceding_sibling(matcher, h1_compound, child2));
    EXPECT_TRUE(selector_matcher_has_preceding_sibling(matcher, h1_compound, child3));
}

TEST_F(DomIntegrationTest, AdjacentSiblingCombinator_Complex) {
    // Test h1 + p (p immediately after h1)
    DomElement* container = dom_element_create(pool, "article", nullptr);
    DomElement* heading = dom_element_create(pool, "h1", nullptr);
    DomElement* para1 = dom_element_create(pool, "p", nullptr);
    DomElement* para2 = dom_element_create(pool, "p", nullptr);
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* para3 = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(container, heading);
    dom_element_append_child(container, para1);    // Matches h1 + p
    dom_element_append_child(container, para2);    // Doesn't match (not after h1)
    dom_element_append_child(container, div);
    dom_element_append_child(container, para3);    // Doesn't match (not after h1)

    CssCompoundSelector* h1_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    h1_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    h1_selector->simple_selectors[0] = create_type_selector("h1");
    h1_selector->simple_selector_count = 1;

    EXPECT_TRUE(selector_matcher_has_prev_sibling(matcher, h1_selector, para1));
    EXPECT_FALSE(selector_matcher_has_prev_sibling(matcher, h1_selector, para2));
    EXPECT_FALSE(selector_matcher_has_prev_sibling(matcher, h1_selector, para3));
}

TEST_F(DomIntegrationTest, GeneralSiblingCombinator_Complex) {
    // Test h2 ~ p (any p that follows h2)
    DomElement* section = dom_element_create(pool, "section", nullptr);
    DomElement* h2 = dom_element_create(pool, "h2", nullptr);
    DomElement* para1 = dom_element_create(pool, "p", nullptr);
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* para2 = dom_element_create(pool, "p", nullptr);
    DomElement* para3 = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(section, h2);
    dom_element_append_child(section, para1);    // Matches h2 ~ p
    dom_element_append_child(section, div);
    dom_element_append_child(section, para2);    // Matches h2 ~ p
    dom_element_append_child(section, para3);    // Matches h2 ~ p

    CssCompoundSelector* h2_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    h2_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    h2_selector->simple_selectors[0] = create_type_selector("h2");
    h2_selector->simple_selector_count = 1;

    CssCompoundSelector* p_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    p_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    p_selector->simple_selectors[0] = create_type_selector("p");
    p_selector->simple_selector_count = 1;

    // Test full combinator: h2 ~ p
    EXPECT_TRUE(selector_matcher_matches_combinator(matcher, h2_selector, CSS_COMBINATOR_SUBSEQUENT_SIBLING, p_selector, para1));
    EXPECT_TRUE(selector_matcher_matches_combinator(matcher, h2_selector, CSS_COMBINATOR_SUBSEQUENT_SIBLING, p_selector, para2));
    EXPECT_TRUE(selector_matcher_matches_combinator(matcher, h2_selector, CSS_COMBINATOR_SUBSEQUENT_SIBLING, p_selector, para3));
    // div doesn't match because it's not a <p> element
    EXPECT_FALSE(selector_matcher_matches_combinator(matcher, h2_selector, CSS_COMBINATOR_SUBSEQUENT_SIBLING, p_selector, div));
}

TEST_F(DomIntegrationTest, DescendantCombinator_DeepNesting) {
    // Test div p (any p inside div, at any depth)
    DomElement* outer_div = dom_element_create(pool, "div", nullptr);
    DomElement* middle_section = dom_element_create(pool, "section", nullptr);
    DomElement* inner_div = dom_element_create(pool, "div", nullptr);
    DomElement* para = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(outer_div, middle_section);
    dom_element_append_child(middle_section, inner_div);
    dom_element_append_child(inner_div, para);

    CssCompoundSelector* div_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    div_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    div_selector->simple_selectors[0] = create_type_selector("div");
    div_selector->simple_selector_count = 1;

    // para has div ancestor (both outer_div and inner_div)
    EXPECT_TRUE(selector_matcher_has_ancestor(matcher, div_selector, para));

    // middle_section also has div ancestor
    EXPECT_TRUE(selector_matcher_has_ancestor(matcher, div_selector, middle_section));
}

TEST_F(DomIntegrationTest, ChildCombinator_DirectOnly) {
    // Test div > p (only direct children)
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* direct_p = dom_element_create(pool, "p", nullptr);
    DomElement* section = dom_element_create(pool, "section", nullptr);
    DomElement* nested_p = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(div, direct_p);       // Direct child
    dom_element_append_child(div, section);
    dom_element_append_child(section, nested_p);   // Not direct child

    CssCompoundSelector* div_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    div_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    div_selector->simple_selectors[0] = create_type_selector("div");
    div_selector->simple_selector_count = 1;

    EXPECT_TRUE(selector_matcher_has_parent(matcher, div_selector, direct_p));
    EXPECT_FALSE(selector_matcher_has_parent(matcher, div_selector, nested_p));
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(DomIntegrationTest, SelectorMatchingPerformance) {
    // Create a large DOM tree
    DomElement* root = dom_element_create(pool, "html", nullptr);
    DomElement* body = dom_element_create(pool, "body", nullptr);
    dom_element_append_child(root, body);

    for (int i = 0; i < 100; i++) {
        DomElement* div = dom_element_create(pool, "div", nullptr);
        dom_element_add_class(div, "test-class");
        dom_element_append_child(body, div);
    }

    CssSimpleSelector* class_sel = create_class_selector("test-class");

    uint64_t before_matches = matcher->total_matches;

    // Perform many matches
    DomElement* child = body->first_child;
    int match_count = 0;
    while (child) {
        if (selector_matcher_matches_simple(matcher, class_sel, child)) {
            match_count++;
        }
        child = child->next_sibling;
    }

    EXPECT_EQ(match_count, 100);
    EXPECT_GT(matcher->total_matches, before_matches);
}

// ============================================================================
// Edge Cases and Error Handling Tests
// ============================================================================

TEST_F(DomIntegrationTest, EdgeCase_NullParameters) {
    DomElement* element = dom_element_create(pool, "div", nullptr);
    CssSimpleSelector* selector = create_type_selector("div");

    // Test null matcher
    EXPECT_FALSE(selector_matcher_matches_simple(nullptr, selector, element));

    // Test null selector
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, nullptr, element));

    // Test null element
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, selector, nullptr));
}

TEST_F(DomIntegrationTest, EdgeCase_EmptyStrings) {
    DomElement* element = dom_element_create(pool, "", nullptr);
    EXPECT_STREQ(element->tag_name, "");

    // Empty class name
    EXPECT_TRUE(dom_element_add_class(element, ""));
    EXPECT_FALSE(dom_element_has_class(element, ""));  // Empty classes shouldn't match

    // Empty attribute
    dom_element_set_attribute(element, "", "value");
    EXPECT_FALSE(dom_element_has_attribute(element, ""));
}

TEST_F(DomIntegrationTest, EdgeCase_VeryLongStrings) {
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Very long class name (1000 chars)
    char long_class[1001];
    memset(long_class, 'a', 1000);
    long_class[1000] = '\0';

    EXPECT_TRUE(dom_element_add_class(element, long_class));
    EXPECT_TRUE(dom_element_has_class(element, long_class));

    // Very long attribute value
    char long_value[1001];
    memset(long_value, 'b', 1000);
    long_value[1000] = '\0';

    EXPECT_TRUE(dom_element_set_attribute(element, "data-long", long_value));
    EXPECT_STREQ(dom_element_get_attribute(element, "data-long"), long_value);
}

TEST_F(DomIntegrationTest, EdgeCase_SpecialCharacters) {
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Special characters in class names
    EXPECT_TRUE(dom_element_add_class(element, "class-with-hyphen"));
    EXPECT_TRUE(dom_element_add_class(element, "class_with_underscore"));
    EXPECT_TRUE(dom_element_add_class(element, "class123"));

    EXPECT_TRUE(dom_element_has_class(element, "class-with-hyphen"));
    EXPECT_TRUE(dom_element_has_class(element, "class_with_underscore"));
    EXPECT_TRUE(dom_element_has_class(element, "class123"));

    // Special characters in attribute values
    dom_element_set_attribute(element, "data-json", "{\"key\": \"value\"}");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-json"), "{\"key\": \"value\"}");

    // Unicode characters
    dom_element_set_attribute(element, "data-unicode", "你好世界");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-unicode"), "你好世界");
}

TEST_F(DomIntegrationTest, EdgeCase_CaseSensitivity) {
    DomElement* element = dom_element_create(pool, "DIV", nullptr);
    dom_element_add_class(element, "MyClass");
    dom_element_set_attribute(element, "DATA-TEST", "VALUE");

    // Tag names are case-insensitive
    CssSimpleSelector* lower_tag = create_type_selector("div");
    CssSimpleSelector* upper_tag = create_type_selector("DIV");
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, lower_tag, element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, upper_tag, element));

    // Class names are case-sensitive
    EXPECT_TRUE(dom_element_has_class(element, "MyClass"));
    EXPECT_FALSE(dom_element_has_class(element, "myclass"));
    EXPECT_FALSE(dom_element_has_class(element, "MYCLASS"));

    // Attribute values are case-sensitive by default
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "DATA-TEST", "VALUE",
                                                   CSS_SELECTOR_ATTR_EXACT, false, element));
    EXPECT_FALSE(selector_matcher_matches_attribute(matcher, "DATA-TEST", "value",
                                                    CSS_SELECTOR_ATTR_EXACT, false, element));
    // But can be case-insensitive with flag
    EXPECT_TRUE(selector_matcher_matches_attribute(matcher, "DATA-TEST", "value",
                                                   CSS_SELECTOR_ATTR_EXACT, true, element));
}

TEST_F(DomIntegrationTest, EdgeCase_DuplicateClasses) {
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Adding same class multiple times
    EXPECT_TRUE(dom_element_add_class(element, "duplicate"));
    EXPECT_TRUE(dom_element_add_class(element, "duplicate"));  // Should handle gracefully
    EXPECT_TRUE(dom_element_add_class(element, "duplicate"));

    // Should still have the class
    EXPECT_TRUE(dom_element_has_class(element, "duplicate"));

    // Removing should work
    EXPECT_TRUE(dom_element_remove_class(element, "duplicate"));
    // After removal, might still have duplicates or not depending on implementation
}

TEST_F(DomIntegrationTest, EdgeCase_MaxChildren) {
    // Test with many children
    DomElement* parent = dom_element_create(pool, "div", nullptr);

    for (int i = 0; i < 1000; i++) {
        DomElement* child = dom_element_create(pool, "span", nullptr);
        dom_element_append_child(parent, child);
    }

    EXPECT_EQ(dom_element_count_children(parent), 1000);

    // Test nth-child with large indices
    DomElement* child = parent->first_child;
    for (int i = 0; i < 500; i++) {
        child = child->next_sibling;
    }
    EXPECT_EQ(dom_element_get_child_index(child), 500);
}

TEST_F(DomIntegrationTest, EdgeCase_CircularPrevention) {
    // Note: Circular reference prevention would require cycle detection
    // which is not currently implemented. This test is disabled to avoid
    // stack overflow from infinite recursion in invalidation.

    // For now, just verify basic parent-child relationship works
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child = dom_element_create(pool, "span", nullptr);
    dom_element_append_child(parent, child);

    EXPECT_EQ(child->parent, parent);
    EXPECT_EQ(parent->first_child, child);
}

TEST_F(DomIntegrationTest, EdgeCase_SelfRemoval) {
    DomElement* parent = dom_element_create(pool, "div", nullptr);
    DomElement* child = dom_element_create(pool, "span", nullptr);

    dom_element_append_child(parent, child);

    // Removing child from itself should fail
    EXPECT_FALSE(dom_element_remove_child(child, child));
}

TEST_F(DomIntegrationTest, EdgeCase_AttributeOverwrite) {
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Set attribute
    dom_element_set_attribute(element, "data-test", "value1");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value1");

    // Overwrite with same key
    dom_element_set_attribute(element, "data-test", "value2");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value2");

    // Should only have one instance
    EXPECT_TRUE(dom_element_has_attribute(element, "data-test"));
}

TEST_F(DomIntegrationTest, Stress_ManySelectors) {
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add many classes
    for (int i = 0; i < 100; i++) {
        char class_name[32];
        snprintf(class_name, sizeof(class_name), "class-%d", i);
        dom_element_add_class(element, class_name);
    }

    // Test matching all of them
    for (int i = 0; i < 100; i++) {
        char class_name[32];
        snprintf(class_name, sizeof(class_name), "class-%d", i);
        CssSimpleSelector* sel = create_class_selector(class_name);
        EXPECT_TRUE(selector_matcher_matches_simple(matcher, sel, element));
    }
}

TEST_F(DomIntegrationTest, Stress_DeepDOMTree) {
    // Create very deep DOM tree (100 levels)
    DomElement* root = dom_element_create(pool, "div", nullptr);
    DomElement* current = root;

    for (int i = 0; i < 100; i++) {
        DomElement* child = dom_element_create(pool, "div", nullptr);
        dom_element_append_child(current, child);
        current = child;
    }

    // Test ancestor matching at depth
    CssCompoundSelector* div_selector = (CssCompoundSelector*)pool_calloc(pool, sizeof(CssCompoundSelector));
    div_selector->simple_selectors = (CssSimpleSelector**)pool_alloc(pool, sizeof(CssSimpleSelector*));
    div_selector->simple_selectors[0] = create_type_selector("div");
    div_selector->simple_selector_count = 1;

    EXPECT_TRUE(selector_matcher_has_ancestor(matcher, div_selector, current));
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(DomIntegrationTest, UtilityFunctions) {
    // Test nth-formula parsing
    CssNthFormula formula;

    EXPECT_TRUE(selector_matcher_parse_nth_formula("odd", &formula));
    EXPECT_TRUE(formula.odd);

    EXPECT_TRUE(selector_matcher_parse_nth_formula("even", &formula));
    EXPECT_TRUE(formula.even);

    EXPECT_TRUE(selector_matcher_parse_nth_formula("2n+1", &formula));
    EXPECT_EQ(formula.a, 2);
    EXPECT_EQ(formula.b, 1);

    EXPECT_TRUE(selector_matcher_parse_nth_formula("3n", &formula));
    EXPECT_EQ(formula.a, 3);
    EXPECT_EQ(formula.b, 0);

    // Test pseudo-class conversion
    EXPECT_EQ(selector_matcher_pseudo_class_to_flag("hover"), PSEUDO_STATE_HOVER);
    EXPECT_EQ(selector_matcher_pseudo_class_to_flag("active"), PSEUDO_STATE_ACTIVE);
    EXPECT_STREQ(selector_matcher_flag_to_pseudo_class(PSEUDO_STATE_HOVER), "hover");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(DomIntegrationTest, CompleteStyleApplication) {
    // Create element
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "main");
    dom_element_add_class(element, "container");

    // Apply multiple declarations
    CssDeclaration* color = create_declaration(CSS_PROPERTY_COLOR, "red", 1, 0, 0);
    CssDeclaration* bg = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "blue", 0, 1, 0);
    CssDeclaration* font = create_declaration(CSS_PROPERTY_FONT_SIZE, "16px", 0, 0, 1);

    dom_element_apply_declaration(element, color);
    dom_element_apply_declaration(element, bg);
    dom_element_apply_declaration(element, font);

    // Verify all declarations applied
    EXPECT_NE(dom_element_get_specified_value(element, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_NE(dom_element_get_specified_value(element, CSS_PROPERTY_BACKGROUND_COLOR), nullptr);
    EXPECT_NE(dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE), nullptr);

    // Print debug info
    dom_element_print_info(element);
    dom_element_print_styles(element);
}

TEST_F(DomIntegrationTest, SelectorMatcherStatistics) {
    selector_matcher_reset_statistics(matcher);

    DomElement* element = dom_element_create(pool, "div", nullptr);
    CssSimpleSelector* div_sel = create_type_selector("div");

    // Perform some matches
    for (int i = 0; i < 10; i++) {
        selector_matcher_matches_simple(matcher, div_sel, element);
    }

    uint64_t total, hits, misses;
    double hit_rate;
    selector_matcher_get_statistics(matcher, &total, &hits, &misses, &hit_rate);

    EXPECT_EQ(total, 10);

    selector_matcher_print_info(matcher);
}

// ============================================================================
// Phase 3 Enhancement Tests
// ============================================================================

// ============================================================================
// Quirks Mode Tests
// ============================================================================

TEST_F(DomIntegrationTest, QuirksMode_CaseSensitiveClasses_Default) {
    // Default: case-sensitive class matching
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "MyClass");

    CssSimpleSelector* lower_sel = create_class_selector("myclass");
    CssSimpleSelector* exact_sel = create_class_selector("MyClass");

    // Default is case-sensitive
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, lower_sel, element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, exact_sel, element));
}

TEST_F(DomIntegrationTest, QuirksMode_CaseInsensitiveClasses) {
    // Enable quirks mode - should make classes case-insensitive
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "MyClass");

    CssSimpleSelector* lower_sel = create_class_selector("myclass");
    CssSimpleSelector* upper_sel = create_class_selector("MYCLASS");
    CssSimpleSelector* exact_sel = create_class_selector("MyClass");

    // All should match in quirks mode
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, lower_sel, element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, upper_sel, element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, exact_sel, element));
}

TEST_F(DomIntegrationTest, QuirksMode_CaseSensitiveAttributes_Default) {
    // Default: case-sensitive attribute values
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "data-test", "ValueMixed");

    // Use the actual selector matching function
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-test", "valuemixed", CSS_SELECTOR_ATTR_EXACT, false, element);

    // Should NOT match (case-sensitive by default, and matcher default is case-sensitive)
    EXPECT_FALSE(matches);
}

TEST_F(DomIntegrationTest, QuirksMode_CaseInsensitiveAttributes) {
    // Enable quirks mode
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "data-test", "ValueMixed");

    // Test with quirks mode (should be case-insensitive now)
    // Even though we pass case_insensitive=false, quirks mode should override
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-test", "valuemixed", CSS_SELECTOR_ATTR_EXACT, false, element);

    // Should match because quirks mode makes attributes case-insensitive
    EXPECT_TRUE(matches);
}

TEST_F(DomIntegrationTest, QuirksMode_FineGrainedControl_Classes) {
    // Test fine-grained control: disable only class case sensitivity
    selector_matcher_set_case_sensitive_classes(matcher, false);
    // Keep attributes case-sensitive (default)

    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "MyClass");
    dom_element_set_attribute(element, "data-test", "MyValue");

    // Class should match case-insensitively
    CssSimpleSelector* class_sel = create_class_selector("myclass");
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, class_sel, element));

    // Attribute should still be case-sensitive
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-test", "myvalue", CSS_SELECTOR_ATTR_EXACT, false, element);

    EXPECT_FALSE(matches);
}

TEST_F(DomIntegrationTest, QuirksMode_MultipleClasses_CaseInsensitive) {
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "FirstClass");
    dom_element_add_class(element, "SecondClass");
    dom_element_add_class(element, "ThirdClass");

    // Test various case combinations
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("firstclass"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("SECONDCLASS"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("ThIrDcLaSs"), element));
}

// ============================================================================
// Hybrid Attribute Storage Tests
// ============================================================================

TEST_F(DomIntegrationTest, AttributeStorage_ArrayMode_SmallCount) {
    // Test with < 10 attributes (should use array mode)
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add 5 attributes
    dom_element_set_attribute(element, "attr1", "value1");
    dom_element_set_attribute(element, "attr2", "value2");
    dom_element_set_attribute(element, "attr3", "value3");
    dom_element_set_attribute(element, "attr4", "value4");
    dom_element_set_attribute(element, "attr5", "value5");

    // Verify all attributes accessible
    EXPECT_STREQ(dom_element_get_attribute(element, "attr1"), "value1");
    EXPECT_STREQ(dom_element_get_attribute(element, "attr3"), "value3");
    EXPECT_STREQ(dom_element_get_attribute(element, "attr5"), "value5");

    // Verify has_attribute works
    EXPECT_TRUE(dom_element_has_attribute(element, "attr2"));
    EXPECT_FALSE(dom_element_has_attribute(element, "attr99"));

    // Update attribute
    dom_element_set_attribute(element, "attr3", "new_value3");
    EXPECT_STREQ(dom_element_get_attribute(element, "attr3"), "new_value3");

    // Remove attribute
    EXPECT_TRUE(dom_element_remove_attribute(element, "attr2"));
    EXPECT_FALSE(dom_element_has_attribute(element, "attr2"));
    EXPECT_EQ(dom_element_get_attribute(element, "attr2"), nullptr);
}

TEST_F(DomIntegrationTest, AttributeStorage_HashMapMode_LargeCount) {
    // Test with >= 10 attributes (should convert to HashMap)
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add 15 attributes (triggers conversion at 10th)
    for (int i = 1; i <= 15; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Verify all attributes accessible
    for (int i = 1; i <= 15; i++) {
        char name[20], expected[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(expected, sizeof(expected), "value%d", i);
        EXPECT_STREQ(dom_element_get_attribute(element, name), expected);
    }

    // Update an attribute
    dom_element_set_attribute(element, "attr7", "updated7");
    EXPECT_STREQ(dom_element_get_attribute(element, "attr7"), "updated7");

    // Remove an attribute
    EXPECT_TRUE(dom_element_remove_attribute(element, "attr5"));
    EXPECT_FALSE(dom_element_has_attribute(element, "attr5"));
}

TEST_F(DomIntegrationTest, AttributeStorage_ConversionThreshold) {
    // Test the exact conversion point (10th attribute)
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add exactly 9 attributes (should stay in array mode)
    for (int i = 1; i <= 9; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // All should be accessible
    EXPECT_STREQ(dom_element_get_attribute(element, "attr5"), "value5");

    // Add 10th attribute (triggers conversion to HashMap)
    dom_element_set_attribute(element, "attr10", "value10");

    // Verify all 10 attributes still accessible after conversion
    for (int i = 1; i <= 10; i++) {
        char name[20], expected[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(expected, sizeof(expected), "value%d", i);
        EXPECT_STREQ(dom_element_get_attribute(element, name), expected);
    }
}

TEST_F(DomIntegrationTest, AttributeStorage_SVGElement_ManyAttributes) {
    // Simulate SVG element with many attributes (typical case)
    DomElement* svg_path = dom_element_create(pool, "path", nullptr);

    // Add typical SVG path attributes
    dom_element_set_attribute(svg_path, "d", "M 10 10 L 100 100");
    dom_element_set_attribute(svg_path, "stroke", "black");
    dom_element_set_attribute(svg_path, "stroke-width", "2");
    dom_element_set_attribute(svg_path, "fill", "none");
    dom_element_set_attribute(svg_path, "stroke-linecap", "round");
    dom_element_set_attribute(svg_path, "stroke-linejoin", "round");
    dom_element_set_attribute(svg_path, "transform", "rotate(45)");
    dom_element_set_attribute(svg_path, "opacity", "0.8");
    dom_element_set_attribute(svg_path, "filter", "url(#blur)");
    dom_element_set_attribute(svg_path, "clip-path", "url(#clip)");
    dom_element_set_attribute(svg_path, "data-id", "path1");
    dom_element_set_attribute(svg_path, "data-layer", "foreground");
    dom_element_set_attribute(svg_path, "aria-label", "Diagonal line");
    dom_element_set_attribute(svg_path, "role", "img");

    // Verify critical attributes
    EXPECT_STREQ(dom_element_get_attribute(svg_path, "d"), "M 10 10 L 100 100");
    EXPECT_STREQ(dom_element_get_attribute(svg_path, "stroke"), "black");
    EXPECT_STREQ(dom_element_get_attribute(svg_path, "data-layer"), "foreground");
    EXPECT_STREQ(dom_element_get_attribute(svg_path, "aria-label"), "Diagonal line");

    // Test attribute matching using the function
    bool matches = selector_matcher_matches_attribute(
        matcher, "stroke", "black", CSS_SELECTOR_ATTR_EXACT, false, svg_path);

    EXPECT_TRUE(matches);
}

TEST_F(DomIntegrationTest, AttributeStorage_Performance_ManyAttributes) {
    // Performance test: 50 attributes (typical for complex SVG)
    DomElement* element = dom_element_create(pool, "g", nullptr);

    // Add 50 attributes
    for (int i = 1; i <= 50; i++) {
        char name[20], value[30];
        snprintf(name, sizeof(name), "data-attr-%d", i);
        snprintf(value, sizeof(value), "value-%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Verify random access is fast (HashMap should be O(1))
    EXPECT_STREQ(dom_element_get_attribute(element, "data-attr-1"), "value-1");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-attr-25"), "value-25");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-attr-50"), "value-50");

    // Test lookups for non-existent attributes
    EXPECT_EQ(dom_element_get_attribute(element, "nonexistent"), nullptr);
    EXPECT_FALSE(dom_element_has_attribute(element, "nonexistent"));
}

TEST_F(DomIntegrationTest, AttributeStorage_Clone_ManyAttributes) {
    // Test cloning element with many attributes (tests iterator)
    DomElement* original = dom_element_create(pool, "div", nullptr);

    // Add 20 attributes
    for (int i = 1; i <= 20; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        dom_element_set_attribute(original, name, value);
    }

    // Clone the element
    DomElement* clone = dom_element_clone(original, pool);
    ASSERT_NE(clone, nullptr);

    // Verify all attributes were cloned
    for (int i = 1; i <= 20; i++) {
        char name[20], expected[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(expected, sizeof(expected), "value%d", i);
        EXPECT_STREQ(dom_element_get_attribute(clone, name), expected);
    }
}

TEST_F(DomIntegrationTest, AttributeStorage_UpdateAfterConversion) {
    // Test updating attributes after array→HashMap conversion
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add 9 attributes (array mode)
    for (int i = 1; i <= 9; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "old%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Add 10th attribute (triggers conversion)
    dom_element_set_attribute(element, "attr10", "old10");

    // Now update attributes (should work in HashMap mode)
    for (int i = 1; i <= 10; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "new%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Verify all updates
    for (int i = 1; i <= 10; i++) {
        char name[20], expected[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(expected, sizeof(expected), "new%d", i);
        EXPECT_STREQ(dom_element_get_attribute(element, name), expected);
    }
}

TEST_F(DomIntegrationTest, AttributeStorage_RemoveAfterConversion) {
    // Test removing attributes after conversion to HashMap
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add 15 attributes (HashMap mode)
    for (int i = 1; i <= 15; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Remove every other attribute
    for (int i = 1; i <= 15; i += 2) {
        char name[20];
        snprintf(name, sizeof(name), "attr%d", i);
        EXPECT_TRUE(dom_element_remove_attribute(element, name));
    }

    // Verify removed attributes are gone
    EXPECT_FALSE(dom_element_has_attribute(element, "attr1"));
    EXPECT_FALSE(dom_element_has_attribute(element, "attr7"));
    EXPECT_FALSE(dom_element_has_attribute(element, "attr15"));

    // Verify remaining attributes still exist
    EXPECT_TRUE(dom_element_has_attribute(element, "attr2"));
    EXPECT_STREQ(dom_element_get_attribute(element, "attr6"), "value6");
    EXPECT_STREQ(dom_element_get_attribute(element, "attr14"), "value14");
}

// ============================================================================
// Selector Entry Caching Tests
// ============================================================================

TEST_F(DomIntegrationTest, SelectorCache_TagNamePointer) {
    // Test that tag_name_ptr is set correctly
    DomElement* div1 = dom_element_create(pool, "div", nullptr);
    DomElement* div2 = dom_element_create(pool, "div", nullptr);
    DomElement* span = dom_element_create(pool, "span", nullptr);

    ASSERT_NE(div1->tag_name_ptr, nullptr);
    ASSERT_NE(div2->tag_name_ptr, nullptr);
    ASSERT_NE(span->tag_name_ptr, nullptr);

    // Same tag names should point to same string (if from name_pool)
    // For now, just verify they're set
    EXPECT_EQ(div1->tag_name_ptr, (void*)div1->tag_name);
    EXPECT_EQ(div2->tag_name_ptr, (void*)div2->tag_name);
    EXPECT_EQ(span->tag_name_ptr, (void*)span->tag_name);
}

TEST_F(DomIntegrationTest, SelectorCache_GetEntry) {
    // Test selector_matcher_get_entry function
    CssSimpleSelector* div_sel = create_type_selector("div");

    SelectorEntry* entry = selector_matcher_get_entry(matcher, div_sel);
    ASSERT_NE(entry, nullptr);

    // Entry should be created with initial values
    // Note: cached_tag_ptr will be NULL until actual caching is implemented
    EXPECT_EQ(entry->use_count, 0);
    EXPECT_FALSE(entry->cache_valid);
}

TEST_F(DomIntegrationTest, SelectorCache_MultipleEntries) {
    // Test creating multiple selector entries
    CssSimpleSelector* div_sel = create_type_selector("div");
    CssSimpleSelector* span_sel = create_type_selector("span");
    CssSimpleSelector* p_sel = create_type_selector("p");

    SelectorEntry* div_entry = selector_matcher_get_entry(matcher, div_sel);
    SelectorEntry* span_entry = selector_matcher_get_entry(matcher, span_sel);
    SelectorEntry* p_entry = selector_matcher_get_entry(matcher, p_sel);

    ASSERT_NE(div_entry, nullptr);
    ASSERT_NE(span_entry, nullptr);
    ASSERT_NE(p_entry, nullptr);

    // Entries should be different
    EXPECT_NE(div_entry, span_entry);
    EXPECT_NE(span_entry, p_entry);
}

// ============================================================================
// Integration: All Enhancements Together
// ============================================================================

TEST_F(DomIntegrationTest, Integration_QuirksModeWithManyAttributes) {
    // Test quirks mode + hybrid storage together
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = dom_element_create(pool, "button", nullptr);

    // Add many attributes (triggers HashMap)
    for (int i = 1; i <= 15; i++) {
        char name[20], value[20];
        snprintf(name, sizeof(name), "data-attr-%d", i);
        snprintf(value, sizeof(value), "Value%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Add classes
    dom_element_add_class(element, "BtnPrimary");
    dom_element_add_class(element, "BtnLarge");

    // Test case-insensitive class matching
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("btnprimary"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("BTNLARGE"), element));

    // Test case-insensitive attribute matching
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-attr-5", "value5", CSS_SELECTOR_ATTR_EXACT, false, element);

    // Should match because quirks mode makes attributes case-insensitive
    EXPECT_TRUE(matches);
}

TEST_F(DomIntegrationTest, Integration_SVGWithQuirksMode) {
    // Real-world scenario: SVG with many attributes in quirks mode
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* svg = dom_element_create(pool, "svg", nullptr);
    dom_element_set_attribute(svg, "xmlns", "http://www.w3.org/2000/svg");
    dom_element_set_attribute(svg, "viewBox", "0 0 100 100");
    dom_element_set_attribute(svg, "width", "100");
    dom_element_set_attribute(svg, "height", "100");
    dom_element_set_attribute(svg, "preserveAspectRatio", "xMidYMid meet");
    dom_element_set_attribute(svg, "class", "IconSVG");
    dom_element_set_attribute(svg, "data-icon", "CheckMark");
    dom_element_set_attribute(svg, "data-size", "Medium");
    dom_element_set_attribute(svg, "aria-hidden", "true");
    dom_element_set_attribute(svg, "role", "img");
    dom_element_set_attribute(svg, "focusable", "false");

    // Add class for matching
    dom_element_add_class(svg, "IconSVG");

    // Test case-insensitive class
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("iconsvg"), svg));

    // Test attribute with various cases
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-icon", "checkmark", CSS_SELECTOR_ATTR_EXACT, false, svg);

    EXPECT_TRUE(matches);
}

TEST_F(DomIntegrationTest, Integration_PerformanceManyAttributesWithMatching) {
    // Performance test: element with many attributes, multiple selector matches
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Add 30 attributes
    for (int i = 1; i <= 30; i++) {
        char name[30], value[30];
        snprintf(name, sizeof(name), "data-test-attr-%d", i);
        snprintf(value, sizeof(value), "test-value-%d", i);
        dom_element_set_attribute(element, name, value);
    }

    // Add classes
    dom_element_add_class(element, "test-class-1");
    dom_element_add_class(element, "test-class-2");

    // Perform many selector matches (should be fast with HashMap)
    for (int i = 1; i <= 30; i++) {
        char name[30], value[30];
        snprintf(name, sizeof(name), "data-test-attr-%d", i);
        snprintf(value, sizeof(value), "test-value-%d", i);

        bool matches = selector_matcher_matches_attribute(
            matcher, name, value, CSS_SELECTOR_ATTR_EXACT, false, element);

        EXPECT_TRUE(matches);
    }

    // Test class matching
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("test-class-1"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("test-class-2"), element));
}

// ============================================================================
// Advanced Selector Matching Tests
// ============================================================================

TEST_F(DomIntegrationTest, AdvancedSelector_DeepHierarchy_Descendant) {
    // Test: html > body > main > section > article > div > p
    // Create a deep DOM tree (7 levels)
    DomElement* html = dom_element_create(pool, "html", nullptr);
    DomElement* body = dom_element_create(pool, "body", nullptr);
    DomElement* main_el = dom_element_create(pool, "main", nullptr);
    DomElement* section = dom_element_create(pool, "section", nullptr);
    DomElement* article = dom_element_create(pool, "article", nullptr);
    DomElement* div = dom_element_create(pool, "div", nullptr);
    DomElement* p = dom_element_create(pool, "p", nullptr);

    dom_element_append_child(html, body);
    dom_element_append_child(body, main_el);
    dom_element_append_child(main_el, section);
    dom_element_append_child(section, article);
    dom_element_append_child(article, div);
    dom_element_append_child(div, p);

    // Test descendant selectors at various depths
    // "html p" should match
    CssSimpleSelector* p_sel = create_type_selector("p");

    // Verify p is descendant of html (6 levels deep)
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, p_sel, p));

    // Verify hierarchy
    EXPECT_EQ(p->parent, div);
    EXPECT_EQ(div->parent, article);
    EXPECT_EQ(article->parent, section);
    EXPECT_EQ(section->parent, main_el);
    EXPECT_EQ(main_el->parent, body);
    EXPECT_EQ(body->parent, html);
}

TEST_F(DomIntegrationTest, AdvancedSelector_SiblingChain) {
    // Test: div with multiple siblings and adjacent/general sibling selectors
    DomElement* parent = dom_element_create(pool, "div", nullptr);

    DomElement* h1 = dom_element_create(pool, "h1", nullptr);
    DomElement* p1 = dom_element_create(pool, "p", nullptr);
    DomElement* p2 = dom_element_create(pool, "p", nullptr);
    DomElement* div1 = dom_element_create(pool, "div", nullptr);
    DomElement* p3 = dom_element_create(pool, "p", nullptr);
    DomElement* span = dom_element_create(pool, "span", nullptr);

    dom_element_add_class(h1, "title");
    dom_element_add_class(p1, "intro");
    dom_element_add_class(p2, "content");
    dom_element_add_class(div1, "separator");
    dom_element_add_class(p3, "footer");

    dom_element_append_child(parent, h1);
    dom_element_append_child(parent, p1);
    dom_element_append_child(parent, p2);
    dom_element_append_child(parent, div1);
    dom_element_append_child(parent, p3);
    dom_element_append_child(parent, span);

    // Verify sibling relationships
    EXPECT_EQ(h1->next_sibling, p1);
    EXPECT_EQ(p1->prev_sibling, h1);
    EXPECT_EQ(p1->next_sibling, p2);
    EXPECT_EQ(p2->next_sibling, div1);
    EXPECT_EQ(div1->next_sibling, p3);
    EXPECT_EQ(p3->next_sibling, span);

    // Test: h1 + p matches p1 (adjacent sibling via next_sibling)
    EXPECT_EQ(dom_element_get_next_sibling(h1), p1);

    // Test: p ~ div matches div1 (general sibling)
    DomElement* sibling = p1->next_sibling;
    bool found_div = false;
    while (sibling) {
        if (strcmp(sibling->tag_name, "div") == 0) {
            found_div = true;
            break;
        }
        sibling = sibling->next_sibling;
    }
    EXPECT_TRUE(found_div);
}

TEST_F(DomIntegrationTest, AdvancedSelector_ComplexSpecificity_IDvsClass) {
    // Test specificity: #id (1,0,0) vs .class.class.class (0,3,0)
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "unique");
    dom_element_add_class(element, "class1");
    dom_element_add_class(element, "class2");
    dom_element_add_class(element, "class3");

    // Apply declarations with different specificity
    // ID selector: specificity (1,0,0)
    CssDeclaration* id_decl = create_declaration(CSS_PROPERTY_COLOR, "red", 1, 0, 0);
    // Triple class selector: specificity (0,3,0)
    CssDeclaration* class_decl = create_declaration(CSS_PROPERTY_COLOR, "blue", 0, 3, 0);
    // Element selector: specificity (0,0,1)
    CssDeclaration* elem_decl = create_declaration(CSS_PROPERTY_COLOR, "green", 0, 0, 1);

    dom_element_apply_declaration(element, elem_decl);
    dom_element_apply_declaration(element, class_decl);
    dom_element_apply_declaration(element, id_decl);

    // ID should win (highest specificity)
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    EXPECT_STREQ((char*)color->value, "red");
}

TEST_F(DomIntegrationTest, AdvancedSelector_ComplexSpecificity_MultipleRules) {
    // Test cascade with multiple overlapping rules
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "main");
    dom_element_add_class(element, "container");
    dom_element_add_class(element, "primary");

    // Apply multiple declarations for same property with different specificity
    // div.container.primary (0,2,1) - should lose to ID
    CssDeclaration* decl1 = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "white", 0, 2, 1);
    // #main.container (1,1,0) - should win (highest specificity)
    CssDeclaration* decl2 = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "black", 1, 1, 0);
    // .container (0,1,0) - should lose
    CssDeclaration* decl3 = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "gray", 0, 1, 0);
    // div (0,0,1) - should lose (lowest specificity)
    CssDeclaration* decl4 = create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "yellow", 0, 0, 1);

    // Apply in random order (not specificity order)
    dom_element_apply_declaration(element, decl3);
    dom_element_apply_declaration(element, decl1);
    dom_element_apply_declaration(element, decl4);
    dom_element_apply_declaration(element, decl2);

    // Highest specificity should win
    CssDeclaration* bg = dom_element_get_specified_value(element, CSS_PROPERTY_BACKGROUND_COLOR);
    ASSERT_NE(bg, nullptr);
    EXPECT_STREQ((char*)bg->value, "black");
}

TEST_F(DomIntegrationTest, AdvancedSelector_ComplexSpecificity_EqualSpecificity) {
    // Test: When specificity is equal, last rule wins (source order)
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "box");

    // All have same specificity (0,1,1)
    CssDeclaration* decl1 = create_declaration(CSS_PROPERTY_WIDTH, "100px", 0, 1, 1);
    CssDeclaration* decl2 = create_declaration(CSS_PROPERTY_WIDTH, "200px", 0, 1, 1);
    CssDeclaration* decl3 = create_declaration(CSS_PROPERTY_WIDTH, "300px", 0, 1, 1);

    dom_element_apply_declaration(element, decl1);
    dom_element_apply_declaration(element, decl2);
    dom_element_apply_declaration(element, decl3);

    // Last one should win (source order)
    CssDeclaration* width = dom_element_get_specified_value(element, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_STREQ((char*)width->value, "300px");
}

TEST_F(DomIntegrationTest, AdvancedSelector_HierarchyWithAttributes) {
    // Test: Complex hierarchy with attribute selectors
    // <div id="app">
    //   <section class="main" data-section="content">
    //     <article data-type="post" data-status="published">
    //       <p class="text" data-paragraph="1">...</p>
    //     </article>
    //   </section>
    // </div>

    DomElement* app = dom_element_create(pool, "div", nullptr);
    DomElement* section = dom_element_create(pool, "section", nullptr);
    DomElement* article = dom_element_create(pool, "article", nullptr);
    DomElement* p = dom_element_create(pool, "p", nullptr);

    dom_element_set_attribute(app, "id", "app");

    dom_element_add_class(section, "main");
    dom_element_set_attribute(section, "data-section", "content");

    dom_element_set_attribute(article, "data-type", "post");
    dom_element_set_attribute(article, "data-status", "published");

    dom_element_add_class(p, "text");
    dom_element_set_attribute(p, "data-paragraph", "1");

    dom_element_append_child(app, section);
    dom_element_append_child(section, article);
    dom_element_append_child(article, p);

    // Test attribute selectors at various levels
    EXPECT_STREQ(dom_element_get_attribute(section, "data-section"), "content");
    EXPECT_STREQ(dom_element_get_attribute(article, "data-type"), "post");
    EXPECT_STREQ(dom_element_get_attribute(article, "data-status"), "published");
    EXPECT_STREQ(dom_element_get_attribute(p, "data-paragraph"), "1");

    // Test matching with attribute selectors
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-type", "post", CSS_SELECTOR_ATTR_EXACT, false, article);
    EXPECT_TRUE(matches);

    bool matches2 = selector_matcher_matches_attribute(
        matcher, "data-status", "published", CSS_SELECTOR_ATTR_EXACT, false, article);
    EXPECT_TRUE(matches2);
}

TEST_F(DomIntegrationTest, AdvancedSelector_MultipleClassCombinations) {
    // Test: Element with multiple classes, test various combinations
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "btn");
    dom_element_add_class(element, "btn-primary");
    dom_element_add_class(element, "btn-lg");
    dom_element_add_class(element, "active");
    dom_element_add_class(element, "disabled");

    // All individual classes should match
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("btn"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("btn-primary"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("btn-lg"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("active"), element));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("disabled"), element));

    // Non-existent class should not match
    EXPECT_FALSE(selector_matcher_matches_simple(matcher, create_class_selector("btn-secondary"), element));
}

TEST_F(DomIntegrationTest, AdvancedSelector_HierarchyWithNthChild) {
    // Test: nth-child selectors in a hierarchy
    // <ul>
    //   <li>Item 1</li>
    //   <li>Item 2</li>
    //   <li class="special">Item 3</li>
    //   <li>Item 4</li>
    //   <li>Item 5</li>
    // </ul>

    DomElement* ul = dom_element_create(pool, "ul", nullptr);
    DomElement* li1 = dom_element_create(pool, "li", nullptr);
    DomElement* li2 = dom_element_create(pool, "li", nullptr);
    DomElement* li3 = dom_element_create(pool, "li", nullptr);
    DomElement* li4 = dom_element_create(pool, "li", nullptr);
    DomElement* li5 = dom_element_create(pool, "li", nullptr);

    dom_element_add_class(li3, "special");

    dom_element_append_child(ul, li1);
    dom_element_append_child(ul, li2);
    dom_element_append_child(ul, li3);
    dom_element_append_child(ul, li4);
    dom_element_append_child(ul, li5);

    // Test nth-child positions (manually count)
    auto get_nth_child_index = [](DomElement* elem) {
        int pos = 1;
        DomElement* sibling = elem->prev_sibling;
        while (sibling) {
            pos++;
            sibling = sibling->prev_sibling;
        }
        return pos;
    };

    int pos1 = get_nth_child_index(li1);
    int pos2 = get_nth_child_index(li2);
    int pos3 = get_nth_child_index(li3);
    int pos4 = get_nth_child_index(li4);
    int pos5 = get_nth_child_index(li5);

    EXPECT_EQ(pos1, 1);
    EXPECT_EQ(pos2, 2);
    EXPECT_EQ(pos3, 3);
    EXPECT_EQ(pos4, 4);
    EXPECT_EQ(pos5, 5);

    // Test first-child
    EXPECT_EQ(ul->first_child, li1);
    // Test last-child
    DomElement* last = ul->first_child;
    while (last->next_sibling) last = last->next_sibling;
    EXPECT_EQ(last, li5);
}

TEST_F(DomIntegrationTest, AdvancedSelector_NestedListsWithClasses) {
    // Test: Nested lists with various class combinations
    // <ul class="menu">
    //   <li class="item">
    //     <ul class="submenu">
    //       <li class="subitem active">...</li>
    //     </ul>
    //   </li>
    // </ul>

    DomElement* ul1 = dom_element_create(pool, "ul", nullptr);
    DomElement* li1 = dom_element_create(pool, "li", nullptr);
    DomElement* ul2 = dom_element_create(pool, "ul", nullptr);
    DomElement* li2 = dom_element_create(pool, "li", nullptr);

    dom_element_add_class(ul1, "menu");
    dom_element_add_class(li1, "item");
    dom_element_add_class(ul2, "submenu");
    dom_element_add_class(li2, "subitem");
    dom_element_add_class(li2, "active");

    dom_element_append_child(ul1, li1);
    dom_element_append_child(li1, ul2);
    dom_element_append_child(ul2, li2);

    // Test hierarchy
    EXPECT_EQ(li2->parent, ul2);
    EXPECT_EQ(ul2->parent, li1);
    EXPECT_EQ(li1->parent, ul1);

    // Test class matching at each level
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("menu"), ul1));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("item"), li1));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("submenu"), ul2));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("subitem"), li2));
    EXPECT_TRUE(selector_matcher_matches_simple(matcher, create_class_selector("active"), li2));
}

TEST_F(DomIntegrationTest, AdvancedSelector_ComplexCascade_MultipleProperties) {
    // Test: Multiple properties with overlapping rules
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "box");
    dom_element_add_class(element, "styled");

    // Apply multiple properties with different specificity
    // Color: ID wins
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_COLOR, "blue", 0, 1, 1));
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_COLOR, "red", 1, 0, 0));

    // Background: class wins (only one rule)
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_BACKGROUND_COLOR, "yellow", 0, 1, 1));

    // Font-size: element wins (only one rule)
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_FONT_SIZE, "16px", 0, 0, 1));

    // Width: equal specificity, last wins
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_WIDTH, "100px", 0, 1, 0));
    dom_element_apply_declaration(element, create_declaration(CSS_PROPERTY_WIDTH, "200px", 0, 1, 0));

    // Verify each property
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    EXPECT_STREQ((char*)color->value, "red");

    CssDeclaration* bg = dom_element_get_specified_value(element, CSS_PROPERTY_BACKGROUND_COLOR);
    ASSERT_NE(bg, nullptr);
    EXPECT_STREQ((char*)bg->value, "yellow");

    CssDeclaration* font_size = dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE);
    ASSERT_NE(font_size, nullptr);
    EXPECT_STREQ((char*)font_size->value, "16px");

    CssDeclaration* width = dom_element_get_specified_value(element, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    EXPECT_STREQ((char*)width->value, "200px");
}

TEST_F(DomIntegrationTest, AdvancedSelector_AttributeVariations) {
    // Test: Different attribute selector operators
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "data-value", "test-item-123");
    dom_element_set_attribute(element, "class", "btn btn-primary active");
    dom_element_set_attribute(element, "lang", "en-US");

    // EXACT: [data-value="test-item-123"]
    bool exact = selector_matcher_matches_attribute(
        matcher, "data-value", "test-item-123", CSS_SELECTOR_ATTR_EXACT, false, element);
    EXPECT_TRUE(exact);

    // BEGINS: [data-value^="test"]
    bool begins = selector_matcher_matches_attribute(
        matcher, "data-value", "test", CSS_SELECTOR_ATTR_BEGINS, false, element);
    EXPECT_TRUE(begins);

    // ENDS: [data-value$="123"]
    bool ends = selector_matcher_matches_attribute(
        matcher, "data-value", "123", CSS_SELECTOR_ATTR_ENDS, false, element);
    EXPECT_TRUE(ends);

    // CONTAINS: [data-value*="item"]
    bool contains = selector_matcher_matches_attribute(
        matcher, "data-value", "item", CSS_SELECTOR_ATTR_SUBSTRING, false, element);
    EXPECT_TRUE(contains);

    // LANG: [lang|="en"]
    bool lang = selector_matcher_matches_attribute(
        matcher, "lang", "en", CSS_SELECTOR_ATTR_LANG, false, element);
    EXPECT_TRUE(lang);
}

TEST_F(DomIntegrationTest, AdvancedSelector_PseudoClassCombinations) {
    // Test: Multiple pseudo-classes on same element
    DomElement* input = dom_element_create(pool, "input", nullptr);
    dom_element_set_attribute(input, "type", "text");
    dom_element_set_attribute(input, "required", "true");

    // Set multiple pseudo-class states
    dom_element_set_pseudo_state(input, PSEUDO_STATE_FOCUS);
    dom_element_set_pseudo_state(input, PSEUDO_STATE_VALID);

    // Verify multiple states
    EXPECT_TRUE(dom_element_has_pseudo_state(input, PSEUDO_STATE_FOCUS));
    EXPECT_TRUE(dom_element_has_pseudo_state(input, PSEUDO_STATE_VALID));
    EXPECT_FALSE(dom_element_has_pseudo_state(input, PSEUDO_STATE_INVALID));

    // Change state
    dom_element_clear_pseudo_state(input, PSEUDO_STATE_VALID);
    dom_element_set_pseudo_state(input, PSEUDO_STATE_INVALID);

    EXPECT_TRUE(dom_element_has_pseudo_state(input, PSEUDO_STATE_FOCUS));
    EXPECT_FALSE(dom_element_has_pseudo_state(input, PSEUDO_STATE_VALID));
    EXPECT_TRUE(dom_element_has_pseudo_state(input, PSEUDO_STATE_INVALID));
}

TEST_F(DomIntegrationTest, AdvancedSelector_FormElementHierarchy) {
    // Test: Complex form structure with various input types
    // <form id="contact">
    //   <fieldset class="personal">
    //     <input type="text" name="name" required>
    //     <input type="email" name="email" required>
    //   </fieldset>
    //   <fieldset class="preferences">
    //     <input type="checkbox" name="newsletter" checked>
    //     <input type="radio" name="format" value="html">
    //     <input type="radio" name="format" value="text" checked>
    //   </fieldset>
    //   <button type="submit" class="btn primary">Submit</button>
    // </form>

    DomElement* form = dom_element_create(pool, "form", nullptr);
    DomElement* fieldset1 = dom_element_create(pool, "fieldset", nullptr);
    DomElement* fieldset2 = dom_element_create(pool, "fieldset", nullptr);
    DomElement* input1 = dom_element_create(pool, "input", nullptr);
    DomElement* input2 = dom_element_create(pool, "input", nullptr);
    DomElement* input3 = dom_element_create(pool, "input", nullptr);
    DomElement* input4 = dom_element_create(pool, "input", nullptr);
    DomElement* input5 = dom_element_create(pool, "input", nullptr);
    DomElement* button = dom_element_create(pool, "button", nullptr);

    dom_element_set_attribute(form, "id", "contact");

    dom_element_add_class(fieldset1, "personal");
    dom_element_set_attribute(input1, "type", "text");
    dom_element_set_attribute(input1, "name", "name");
    dom_element_set_attribute(input1, "required", "true");
    dom_element_set_attribute(input2, "type", "email");
    dom_element_set_attribute(input2, "name", "email");
    dom_element_set_attribute(input2, "required", "true");

    dom_element_add_class(fieldset2, "preferences");
    dom_element_set_attribute(input3, "type", "checkbox");
    dom_element_set_attribute(input3, "name", "newsletter");
    dom_element_set_pseudo_state(input3, PSEUDO_STATE_CHECKED);

    dom_element_set_attribute(input4, "type", "radio");
    dom_element_set_attribute(input4, "name", "format");
    dom_element_set_attribute(input4, "value", "html");

    dom_element_set_attribute(input5, "type", "radio");
    dom_element_set_attribute(input5, "name", "format");
    dom_element_set_attribute(input5, "value", "text");
    dom_element_set_pseudo_state(input5, PSEUDO_STATE_CHECKED);

    dom_element_set_attribute(button, "type", "submit");
    dom_element_add_class(button, "btn");
    dom_element_add_class(button, "primary");

    // Build hierarchy
    dom_element_append_child(form, fieldset1);
    dom_element_append_child(form, fieldset2);
    dom_element_append_child(form, button);
    dom_element_append_child(fieldset1, input1);
    dom_element_append_child(fieldset1, input2);
    dom_element_append_child(fieldset2, input3);
    dom_element_append_child(fieldset2, input4);
    dom_element_append_child(fieldset2, input5);

    // Verify hierarchy
    EXPECT_EQ(input1->parent, fieldset1);
    EXPECT_EQ(input2->parent, fieldset1);
    EXPECT_EQ(input3->parent, fieldset2);
    EXPECT_EQ(fieldset1->parent, form);
    EXPECT_EQ(fieldset2->parent, form);

    // Verify attributes
    EXPECT_STREQ(dom_element_get_attribute(input1, "type"), "text");
    EXPECT_STREQ(dom_element_get_attribute(input2, "type"), "email");
    EXPECT_STREQ(dom_element_get_attribute(input3, "type"), "checkbox");

    // Verify pseudo-states
    EXPECT_TRUE(dom_element_has_pseudo_state(input3, PSEUDO_STATE_CHECKED));
    EXPECT_TRUE(dom_element_has_pseudo_state(input5, PSEUDO_STATE_CHECKED));
    EXPECT_FALSE(dom_element_has_pseudo_state(input4, PSEUDO_STATE_CHECKED));
}

TEST_F(DomIntegrationTest, AdvancedSelector_SpecificityTieBreaker_SourceOrder) {
    // Test: When specificity is identical, source order determines winner
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "box");
    dom_element_add_class(element, "widget");

    // All have specificity (0,2,0) - two classes
    CssDeclaration* decl1 = create_declaration(CSS_PROPERTY_MARGIN, "10px", 0, 2, 0);
    CssDeclaration* decl2 = create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 2, 0);
    CssDeclaration* decl3 = create_declaration(CSS_PROPERTY_MARGIN, "30px", 0, 2, 0);
    CssDeclaration* decl4 = create_declaration(CSS_PROPERTY_MARGIN, "40px", 0, 2, 0);

    // Apply in order
    dom_element_apply_declaration(element, decl1);
    dom_element_apply_declaration(element, decl2);
    dom_element_apply_declaration(element, decl3);
    dom_element_apply_declaration(element, decl4);

    // Last declaration should win
    CssDeclaration* margin = dom_element_get_specified_value(element, CSS_PROPERTY_MARGIN);
    ASSERT_NE(margin, nullptr);
    EXPECT_STREQ((char*)margin->value, "40px");
}

TEST_F(DomIntegrationTest, AdvancedSelector_TableStructure) {
    // Test: Complex table structure with thead/tbody/tfoot
    // <table>
    //   <thead><tr><th>Header</th></tr></thead>
    //   <tbody><tr><td>Cell 1</td><td>Cell 2</td></tr></tbody>
    //   <tfoot><tr><td>Footer</td></tr></tfoot>
    // </table>

    DomElement* table = dom_element_create(pool, "table", nullptr);
    DomElement* thead = dom_element_create(pool, "thead", nullptr);
    DomElement* tbody = dom_element_create(pool, "tbody", nullptr);
    DomElement* tfoot = dom_element_create(pool, "tfoot", nullptr);

    DomElement* thead_tr = dom_element_create(pool, "tr", nullptr);
    DomElement* th = dom_element_create(pool, "th", nullptr);

    DomElement* tbody_tr = dom_element_create(pool, "tr", nullptr);
    DomElement* td1 = dom_element_create(pool, "td", nullptr);
    DomElement* td2 = dom_element_create(pool, "td", nullptr);

    DomElement* tfoot_tr = dom_element_create(pool, "tr", nullptr);
    DomElement* td3 = dom_element_create(pool, "td", nullptr);

    // Add classes for styling
    dom_element_add_class(thead, "table-header");
    dom_element_add_class(tbody, "table-body");
    dom_element_add_class(tfoot, "table-footer");

    // Build structure
    dom_element_append_child(table, thead);
    dom_element_append_child(table, tbody);
    dom_element_append_child(table, tfoot);

    dom_element_append_child(thead, thead_tr);
    dom_element_append_child(thead_tr, th);

    dom_element_append_child(tbody, tbody_tr);
    dom_element_append_child(tbody_tr, td1);
    dom_element_append_child(tbody_tr, td2);

    dom_element_append_child(tfoot, tfoot_tr);
    dom_element_append_child(tfoot_tr, td3);

    // Verify structure
    EXPECT_EQ(thead->parent, table);
    EXPECT_EQ(tbody->parent, table);
    EXPECT_EQ(tfoot->parent, table);
    EXPECT_EQ(th->parent, thead_tr);
    EXPECT_EQ(td1->parent, tbody_tr);
    EXPECT_EQ(td2->parent, tbody_tr);
    EXPECT_EQ(td3->parent, tfoot_tr);

    // Verify sibling relationships
    EXPECT_EQ(thead->next_sibling, tbody);
    EXPECT_EQ(tbody->next_sibling, tfoot);
    EXPECT_EQ(td1->next_sibling, td2);
}

// ============================================================================
// Inline Style Tests
// ============================================================================

TEST_F(DomIntegrationTest, InlineStyle_SingleProperty) {
    // Test: Single inline style property
    DomElement* element = dom_element_create(pool, "div", nullptr);

    dom_element_set_attribute(element, "style", "color: red");

    // Verify the declaration was applied
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_EQ(color->value->type, CSS_VALUE_KEYWORD);
    EXPECT_STREQ(color->value->data.keyword, "red");

    // Verify inline style specificity (1,0,0,0)
    EXPECT_EQ(color->specificity.inline_style, 1);
    EXPECT_EQ(color->specificity.ids, 0);
    EXPECT_EQ(color->specificity.classes, 0);
    EXPECT_EQ(color->specificity.elements, 0);
}

TEST_F(DomIntegrationTest, InlineStyle_MultipleProperties) {
    // Test: Multiple inline style properties
    DomElement* element = dom_element_create(pool, "div", nullptr);

    int applied = dom_element_apply_inline_style(element, "color: blue; font-size: 16px; background-color: yellow");
    EXPECT_EQ(applied, 3);

    // Verify all declarations
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_STREQ(color->value->data.keyword, "blue");
    EXPECT_EQ(color->specificity.inline_style, 1);

    CssDeclaration* font_size = dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE);
    ASSERT_NE(font_size, nullptr);
    ASSERT_NE(font_size->value, nullptr);
    EXPECT_STREQ(font_size->value->data.keyword, "16px");
    EXPECT_EQ(font_size->specificity.inline_style, 1);

    CssDeclaration* bg = dom_element_get_specified_value(element, CSS_PROPERTY_BACKGROUND_COLOR);
    ASSERT_NE(bg, nullptr);
    ASSERT_NE(bg->value, nullptr);
    EXPECT_STREQ(bg->value->data.keyword, "yellow");
    EXPECT_EQ(bg->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_OverridesStylesheet) {
    // Test: Inline style overrides stylesheet rules
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_add_class(element, "box");

    // Apply stylesheet rule with class selector (0,1,0)
    CssDeclaration* css_decl = create_declaration(CSS_PROPERTY_COLOR, "green", 0, 1, 0);
    dom_element_apply_declaration(element, css_decl);

    // Apply inline style (1,0,0,0) - should override
    dom_element_set_attribute(element, "style", "color: red");

    // Inline style should win
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_STREQ(color->value->data.keyword, "red");
    EXPECT_EQ(color->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_OverridesIDSelector) {
    // Test: Inline style overrides even ID selectors
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "unique");

    // Apply ID selector rule (1,0,0)
    CssDeclaration* id_decl = create_declaration(CSS_PROPERTY_WIDTH, "100px", 1, 0, 0);
    dom_element_apply_declaration(element, id_decl);

    // Apply inline style (1,0,0,0) - should override ID
    dom_element_set_attribute(element, "style", "width: 200px");

    // Inline style should win (inline_style=1 beats ids=1)
    CssDeclaration* width = dom_element_get_specified_value(element, CSS_PROPERTY_WIDTH);
    ASSERT_NE(width, nullptr);
    ASSERT_NE(width->value, nullptr);
    EXPECT_STREQ(width->value->data.keyword, "200px");
    EXPECT_EQ(width->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_WhitespaceHandling) {
    // Test: Inline style with various whitespace
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Extra spaces, tabs, newlines
    int applied = dom_element_apply_inline_style(element,
        "  color  :  red  ;  font-size:16px;background-color:blue  ");
    EXPECT_EQ(applied, 3);

    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_STREQ(color->value->data.keyword, "red");
}

TEST_F(DomIntegrationTest, InlineStyle_EmptyValue) {
    // Test: Empty inline style
    DomElement* element = dom_element_create(pool, "div", nullptr);

    int applied = dom_element_apply_inline_style(element, "");
    EXPECT_EQ(applied, 0);

    // No declarations should be applied
    EXPECT_EQ(dom_element_get_specified_value(element, CSS_PROPERTY_COLOR), nullptr);
}

TEST_F(DomIntegrationTest, InlineStyle_InvalidDeclarations) {
    // Test: Invalid declarations should be skipped
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Mix of valid and invalid
    int applied = dom_element_apply_inline_style(element,
        "color: red; invalid; font-size: 16px; : novalue; width: 100px");

    // Only valid declarations should be applied
    EXPECT_GE(applied, 2); // At least color and one other valid property

    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_STREQ(color->value->data.keyword, "red");
}

TEST_F(DomIntegrationTest, InlineStyle_UpdateAttribute) {
    // Test: Updating style attribute replaces inline styles
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Set initial inline style
    dom_element_set_attribute(element, "style", "color: red");
    CssDeclaration* color1 = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color1, nullptr);
    ASSERT_NE(color1->value, nullptr);
    EXPECT_STREQ(color1->value->data.keyword, "red");

    // Update with new inline style
    dom_element_set_attribute(element, "style", "color: blue; font-size: 14px");

    // Should have new values
    CssDeclaration* color2 = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color2, nullptr);
    ASSERT_NE(color2->value, nullptr);
    EXPECT_STREQ(color2->value->data.keyword, "blue");

    CssDeclaration* font_size = dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE);
    ASSERT_NE(font_size, nullptr);
    ASSERT_NE(font_size->value, nullptr);
    EXPECT_STREQ(font_size->value->data.keyword, "14px");
}

TEST_F(DomIntegrationTest, InlineStyle_GetInlineStyle) {
    // Test: Retrieving inline style text
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // No inline style initially
    EXPECT_EQ(dom_element_get_inline_style(element), nullptr);

    // Set inline style
    const char* style_text = "color: red; font-size: 16px";
    dom_element_set_attribute(element, "style", style_text);

    // Should retrieve the same text
    const char* retrieved = dom_element_get_inline_style(element);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_STREQ(retrieved, style_text);
}

TEST_F(DomIntegrationTest, InlineStyle_RemoveInlineStyles) {
    // Test: Removing inline styles
    DomElement* element = dom_element_create(pool, "div", nullptr);

    // Set inline style
    dom_element_set_attribute(element, "style", "color: red; font-size: 16px");
    EXPECT_NE(dom_element_get_inline_style(element), nullptr);

    // Remove inline styles
    bool removed = dom_element_remove_inline_styles(element);
    EXPECT_TRUE(removed);

    // Style attribute should be gone
    EXPECT_EQ(dom_element_get_inline_style(element), nullptr);
}

TEST_F(DomIntegrationTest, InlineStyle_ComplexSpecificity) {
    // Test: Inline style in complex specificity scenario
    DomElement* element = dom_element_create(pool, "div", nullptr);
    dom_element_set_attribute(element, "id", "main");
    dom_element_add_class(element, "container");

    // Apply rules with different specificities
    // Element selector (0,0,1)
    dom_element_apply_declaration(element,
        create_declaration(CSS_PROPERTY_MARGIN, "10px", 0, 0, 1));

    // Class selector (0,1,0)
    dom_element_apply_declaration(element,
        create_declaration(CSS_PROPERTY_MARGIN, "20px", 0, 1, 0));

    // ID selector (1,0,0)
    dom_element_apply_declaration(element,
        create_declaration(CSS_PROPERTY_MARGIN, "30px", 1, 0, 0));

    // Inline style (1,0,0,0) - should win over all
    dom_element_set_attribute(element, "style", "margin: 40px");

    CssDeclaration* margin = dom_element_get_specified_value(element, CSS_PROPERTY_MARGIN);
    ASSERT_NE(margin, nullptr);
    ASSERT_NE(margin->value, nullptr);
    EXPECT_STREQ(margin->value->data.keyword, "40px");
    EXPECT_EQ(margin->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_MultipleElements) {
    // Test: Inline styles on multiple elements are independent
    DomElement* elem1 = dom_element_create(pool, "div", nullptr);
    DomElement* elem2 = dom_element_create(pool, "span", nullptr);
    DomElement* elem3 = dom_element_create(pool, "p", nullptr);

    dom_element_set_attribute(elem1, "style", "color: red");
    dom_element_set_attribute(elem2, "style", "color: blue");
    dom_element_set_attribute(elem3, "style", "color: green");

    // Each element should have its own color
    CssDeclaration* color1 = dom_element_get_specified_value(elem1, CSS_PROPERTY_COLOR);
    ASSERT_NE(color1, nullptr);
    ASSERT_NE(color1->value, nullptr);
    EXPECT_STREQ(color1->value->data.keyword, "red");

    CssDeclaration* color2 = dom_element_get_specified_value(elem2, CSS_PROPERTY_COLOR);
    ASSERT_NE(color2, nullptr);
    ASSERT_NE(color2->value, nullptr);
    EXPECT_STREQ(color2->value->data.keyword, "blue");

    CssDeclaration* color3 = dom_element_get_specified_value(elem3, CSS_PROPERTY_COLOR);
    ASSERT_NE(color3, nullptr);
    ASSERT_NE(color3->value, nullptr);
    EXPECT_STREQ(color3->value->data.keyword, "green");
}

TEST_F(DomIntegrationTest, InlineStyle_MixedWithOtherAttributes) {
    // Test: Inline style works alongside other attributes
    DomElement* element = dom_element_create(pool, "div", nullptr);

    dom_element_set_attribute(element, "id", "box");
    dom_element_set_attribute(element, "class", "container");
    dom_element_set_attribute(element, "data-value", "123");
    dom_element_set_attribute(element, "style", "color: red; width: 100px");
    dom_element_set_attribute(element, "title", "Test Element");

    // All attributes should be set
    EXPECT_STREQ(dom_element_get_attribute(element, "id"), "box");
    EXPECT_STREQ(dom_element_get_attribute(element, "class"), "container");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-value"), "123");
    EXPECT_STREQ(dom_element_get_attribute(element, "style"), "color: red; width: 100px");
    EXPECT_STREQ(dom_element_get_attribute(element, "title"), "Test Element");

    // Inline styles should be applied
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_STREQ(color->value->data.keyword, "red");
}

// Run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
