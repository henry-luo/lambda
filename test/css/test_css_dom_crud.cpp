#include <gtest/gtest.h>
#include <vector>

#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/selector_matcher.hpp"
#include "../../lambda/input/css/css_style.hpp"
#include "../../lambda/input/css/css_style_node.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lambda/mark_builder.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/format/format.h"
#include "helpers/css_test_helpers.hpp"

extern "C" {
#include "../../lib/mempool.h"
}

// Forward declaration for helper function
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent);

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
    Input* input;
    DomDocument* doc;
    SelectorMatcher* matcher;

    void SetUp() override {
        // Create Input context for MarkBuilder
        pool = pool_create();
        ASSERT_NE(pool, nullptr);

        input = Input::create(pool);
        ASSERT_NE(input, nullptr);

        // Create DomDocument for DOM tree
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

    // Helper: Create DomElement with backing Lambda Element using MarkBuilder
    DomElement* create_dom_element(const char* tag_name) {
        MarkBuilder builder(input);
        // Create element WITH an initial dummy attribute to establish a shape
        Item elem_item = builder.element(tag_name)
            .attr("_init", "placeholder")
            .final();

        if (!elem_item.element) return nullptr;

        // Set as root (might be needed for MarkEditor)
        input->root = elem_item;

        // Build DOM tree with DomDocument
        DomElement* dom_elem = build_dom_tree_from_element(elem_item.element, doc, nullptr);
        return dom_elem;
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


// NOTE: These tests now use MarkBuilder to create elements with backing Lambda Elements,
// which allows dom_element_set_attribute() to work via MarkEditor.

TEST_F(DomIntegrationTest, DomElementAttributes) {
    DomElement* element = create_dom_element("div");
    ASSERT_NE(element, nullptr);
    ASSERT_NE(element->native_element, nullptr);
    ASSERT_NE(element->doc->input, nullptr);

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

TEST_F(DomIntegrationTest, DomElementIdAttribute) {
    DomElement* element = create_dom_element("div");
    ASSERT_NE(element, nullptr);

    // Set ID attribute
    EXPECT_TRUE(dom_element_set_attribute(element, "id", "test-id"));
    EXPECT_STREQ(element->id, "test-id");
    EXPECT_STREQ(dom_element_get_attribute(element, "id"), "test-id");
}

TEST_F(DomIntegrationTest, InlineMode_ElementPointerStability) {
    // Verify that in INLINE mode, the Lambda element pointer does NOT change
    // when attributes are added/updated (only the shape and data change)
    DomElement* element = create_dom_element("div");
    ASSERT_NE(element, nullptr);

    Element* original_native_ptr = element->native_element;
    ASSERT_NE(original_native_ptr, nullptr);

    // Add a new attribute - should NOT change the element pointer in INLINE mode
    EXPECT_TRUE(dom_element_set_attribute(element, "data-test", "value1"));
    EXPECT_EQ(element->native_element, original_native_ptr)
        << "Element pointer should NOT change in INLINE mode when adding new attribute";

    // Update existing attribute - should NOT change the element pointer
    EXPECT_TRUE(dom_element_set_attribute(element, "data-test", "value2"));
    EXPECT_EQ(element->native_element, original_native_ptr)
        << "Element pointer should NOT change in INLINE mode when updating attribute";

    // Add multiple attributes - pointer should remain stable
    EXPECT_TRUE(dom_element_set_attribute(element, "id", "test-id"));
    EXPECT_EQ(element->native_element, original_native_ptr);

    EXPECT_TRUE(dom_element_set_attribute(element, "class", "test-class"));
    EXPECT_EQ(element->native_element, original_native_ptr);

    EXPECT_TRUE(dom_element_set_attribute(element, "style", "color: red;"));
    EXPECT_EQ(element->native_element, original_native_ptr);

    // Verify all attributes are still accessible
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value2");
    EXPECT_STREQ(dom_element_get_attribute(element, "id"), "test-id");
    EXPECT_STREQ(dom_element_get_attribute(element, "class"), "test-class");
    EXPECT_STREQ(dom_element_get_attribute(element, "style"), "color: red;");
}

// ============================================================================
// Style Management Tests
// ============================================================================

TEST_F(DomIntegrationTest, EdgeCase_VeryLongStrings) {
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("DIV");
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

TEST_F(DomIntegrationTest, EdgeCase_AttributeOverwrite) {
    DomElement* element = create_dom_element("div");

    // Set attribute
    dom_element_set_attribute(element, "data-test", "value1");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value1");

    // Overwrite with same key
    dom_element_set_attribute(element, "data-test", "value2");
    EXPECT_STREQ(dom_element_get_attribute(element, "data-test"), "value2");

    // Should only have one instance
    EXPECT_TRUE(dom_element_has_attribute(element, "data-test"));
}

TEST_F(DomIntegrationTest, QuirksMode_CaseInsensitiveAttributes) {
    // Enable quirks mode
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = create_dom_element("div");
    dom_element_set_attribute(element, "data-test", "ValueMixed");

    // Test with quirks mode (should be case-insensitive now)
    // Even though we pass case_insensitive=false, quirks mode should override
    bool matches = selector_matcher_matches_attribute(
        matcher, "data-test", "valuemixed", CSS_SELECTOR_ATTR_EXACT, false, element);

    // Should match because quirks mode makes attributes case-insensitive
    EXPECT_TRUE(matches);
}

TEST_F(DomIntegrationTest, AttributeStorage_ArrayMode_SmallCount) {
    // Test with < 10 attributes (should use array mode)
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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
    DomElement* svg_path = create_dom_element("path");

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
    DomElement* element = create_dom_element("g");

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
    DomElement* original = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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

TEST_F(DomIntegrationTest, Integration_QuirksModeWithManyAttributes) {
    // Test quirks mode + hybrid storage together
    selector_matcher_set_quirks_mode(matcher, true);

    DomElement* element = create_dom_element("button");

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

    DomElement* svg = create_dom_element("svg");
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
    DomElement* element = create_dom_element("div");

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

TEST_F(DomIntegrationTest, InlineStyle_SingleProperty) {
    // Test: Single inline style property
    DomElement* element = create_dom_element("div");

    dom_element_set_attribute(element, "style", "color: red");

    // Verify the declaration was applied
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_EQ(color->value->type, CSS_VALUE_TYPE_KEYWORD);
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("red"));

    // Verify inline style specificity (1,0,0,0)
    EXPECT_EQ(color->specificity.inline_style, 1);
    EXPECT_EQ(color->specificity.ids, 0);
    EXPECT_EQ(color->specificity.classes, 0);
    EXPECT_EQ(color->specificity.elements, 0);
}

TEST_F(DomIntegrationTest, InlineStyle_MultipleProperties) {
    // Test: Multiple inline style properties
    DomElement* element = create_dom_element("div");

    int applied = dom_element_apply_inline_style(element, "color: blue; font-size: 16px; background-color: yellow");
    EXPECT_EQ(applied, 3);

    // Verify all declarations
    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("blue"));
    EXPECT_EQ(color->specificity.inline_style, 1);

    CssDeclaration* font_size = dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE);
    ASSERT_NE(font_size, nullptr);
    ASSERT_NE(font_size->value, nullptr);
    EXPECT_EQ(font_size->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(font_size->value->data.length.value, 16.0);
    EXPECT_EQ(font_size->value->data.length.unit, CSS_UNIT_PX);
    EXPECT_EQ(font_size->specificity.inline_style, 1);

    CssDeclaration* bg = dom_element_get_specified_value(element, CSS_PROPERTY_BACKGROUND_COLOR);
    ASSERT_NE(bg, nullptr);
    ASSERT_NE(bg->value, nullptr);
    EXPECT_EQ(bg->value->type, CSS_VALUE_TYPE_KEYWORD);
    EXPECT_EQ(bg->value->data.keyword, css_enum_by_name("yellow"));
    EXPECT_EQ(bg->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_OverridesStylesheet) {
    // Test: Inline style overrides stylesheet rules
    DomElement* element = create_dom_element("div");
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
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("red"));
    EXPECT_EQ(color->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_OverridesIDSelector) {
    // Test: Inline style overrides even ID selectors
    DomElement* element = create_dom_element("div");
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
    EXPECT_EQ(width->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(width->value->data.length.value, 200.0);
    EXPECT_EQ(width->value->data.length.unit, CSS_UNIT_PX);
    EXPECT_EQ(width->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_WhitespaceHandling) {
    // Test: Inline style with various whitespace
    DomElement* element = create_dom_element("div");

    // Extra spaces, tabs, newlines
    int applied = dom_element_apply_inline_style(element,
        "  color  :  red  ;  font-size:16px;background-color:blue  ");
    EXPECT_EQ(applied, 3);

    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("red"));
}

TEST_F(DomIntegrationTest, InlineStyle_EmptyValue) {
    // Test: Empty inline style
    DomElement* element = create_dom_element("div");

    int applied = dom_element_apply_inline_style(element, "");
    EXPECT_EQ(applied, 0);

    // No declarations should be applied
    EXPECT_EQ(dom_element_get_specified_value(element, CSS_PROPERTY_COLOR), nullptr);
}

TEST_F(DomIntegrationTest, InlineStyle_InvalidDeclarations) {
    // Test: Invalid declarations should be skipped
    DomElement* element = create_dom_element("div");

    // Mix of valid and invalid
    int applied = dom_element_apply_inline_style(element,
        "color: red; invalid; font-size: 16px; : novalue; width: 100px");

    // Only valid declarations should be applied
    EXPECT_GE(applied, 2); // At least color and one other valid property

    CssDeclaration* color = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(color->value, nullptr);
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("red"));
}

TEST_F(DomIntegrationTest, InlineStyle_UpdateAttribute) {
    // Test: Updating style attribute replaces inline styles
    DomElement* element = create_dom_element("div");

    // Set initial inline style
    dom_element_set_attribute(element, "style", "color: red");
    CssDeclaration* color1 = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color1, nullptr);
    ASSERT_NE(color1->value, nullptr);
    EXPECT_EQ(color1->value->data.keyword, css_enum_by_name("red"));

    // Update with new inline style
    dom_element_set_attribute(element, "style", "color: blue; font-size: 14px");

    // Should have new values
    CssDeclaration* color2 = dom_element_get_specified_value(element, CSS_PROPERTY_COLOR);
    ASSERT_NE(color2, nullptr);
    ASSERT_NE(color2->value, nullptr);
    EXPECT_EQ(color2->value->data.keyword, css_enum_by_name("blue"));

    CssDeclaration* font_size = dom_element_get_specified_value(element, CSS_PROPERTY_FONT_SIZE);
    ASSERT_NE(font_size, nullptr);
    ASSERT_NE(font_size->value, nullptr);
    EXPECT_EQ(font_size->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(font_size->value->data.length.value, 14.0);
    EXPECT_EQ(font_size->value->data.length.unit, CSS_UNIT_PX);
}

TEST_F(DomIntegrationTest, InlineStyle_GetInlineStyle) {
    // Test: Retrieving inline style text
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");

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
    DomElement* element = create_dom_element("div");
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
    EXPECT_EQ(margin->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(margin->value->data.length.value, 40.0);
    EXPECT_EQ(margin->value->data.length.unit, CSS_UNIT_PX);
    EXPECT_EQ(margin->specificity.inline_style, 1);
}

TEST_F(DomIntegrationTest, InlineStyle_MultipleElements) {
    // Test: Inline styles on multiple elements are independent
    DomElement* elem1 = create_dom_element("div");
    DomElement* elem2 = create_dom_element("span");
    DomElement* elem3 = create_dom_element("p");

    dom_element_set_attribute(elem1, "style", "color: red");
    dom_element_set_attribute(elem2, "style", "color: blue");
    dom_element_set_attribute(elem3, "style", "color: green");

    // Each element should have its own color
    CssDeclaration* color1 = dom_element_get_specified_value(elem1, CSS_PROPERTY_COLOR);
    ASSERT_NE(color1, nullptr);
    ASSERT_NE(color1->value, nullptr);
    EXPECT_EQ(color1->value->data.keyword, css_enum_by_name("red"));

    CssDeclaration* color2 = dom_element_get_specified_value(elem2, CSS_PROPERTY_COLOR);
    ASSERT_NE(color2, nullptr);
    ASSERT_NE(color2->value, nullptr);
    EXPECT_EQ(color2->value->data.keyword, css_enum_by_name("blue"));

    CssDeclaration* color3 = dom_element_get_specified_value(elem3, CSS_PROPERTY_COLOR);
    ASSERT_NE(color3, nullptr);
    ASSERT_NE(color3->value, nullptr);
    EXPECT_EQ(color3->value->data.keyword, css_enum_by_name("green"));
}

TEST_F(DomIntegrationTest, InlineStyle_MixedWithOtherAttributes) {
    // Test: Inline style works alongside other attributes
    DomElement* element = create_dom_element("div");

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
    EXPECT_EQ(color->value->data.keyword, css_enum_by_name("red"));
}

// ============================================================================
// DomText Tests (New Node Type)
// ============================================================================

// ============================================================================
// DomText Backed Tests (Lambda Integration)
// ============================================================================

TEST_F(DomIntegrationTest, DomText_CreateBacked_Basic) {
    // Create parent element with backing
    DomElement* parent = create_dom_element("div");
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(parent->native_element, nullptr);

    // Append backed text node
    DomText* text = dom_element_append_text(parent, "Hello World");

    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->native_string, nullptr);
    EXPECT_NE(((DomElement*)text->parent)->doc->input, nullptr);
    EXPECT_EQ(text->parent, parent);
    EXPECT_STREQ(text->text, "Hello World");
    EXPECT_EQ(text->length, 11u);
    EXPECT_TRUE(dom_text_is_backed(text));

    // Verify Lambda backing
    ASSERT_EQ(parent->native_element->length, 1);
    Item child = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    EXPECT_EQ((String*)child.pointer, text->native_string);
    EXPECT_STREQ(((String*)child.pointer)->chars, "Hello World");
}

TEST_F(DomIntegrationTest, DomText_SetContentBacked_UpdatesLambda) {
    DomElement* parent = create_dom_element("p");
    DomText* text = dom_element_append_text(parent, "Original");
    ASSERT_NE(text, nullptr);

    // Update text content
    EXPECT_TRUE(dom_text_set_content(text, "Updated"));

    // Verify DomText updated
    EXPECT_STREQ(text->text, "Updated");
    EXPECT_EQ(text->length, 7u);

    // Verify Lambda String updated
    int64_t idx = dom_text_get_child_index(text);
    ASSERT_GE(idx, 0);
    Item child = parent->native_element->items[idx];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    EXPECT_STREQ(((String*)child.pointer)->chars, "Updated");
    EXPECT_EQ(((String*)child.pointer)->len, 7u);
}

TEST_F(DomIntegrationTest, DomText_RemoveBacked_UpdatesLambda) {
    DomElement* parent = create_dom_element("div");
    DomText* text1 = dom_element_append_text(parent, "First");
    DomText* text2 = dom_element_append_text(parent, "Second");
    ASSERT_NE(text1, nullptr);
    ASSERT_NE(text2, nullptr);

    EXPECT_EQ(parent->native_element->length, 2);

    // Remove first text node
    EXPECT_TRUE(dom_text_remove(text1));

    // Verify Lambda updated
    EXPECT_EQ(parent->native_element->length, 1);
    Item remaining = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(remaining), LMD_TYPE_STRING);
    EXPECT_STREQ(((String*)remaining.pointer)->chars, "Second");

    // Verify text2 index updated
    EXPECT_EQ(dom_text_get_child_index(text2), 0);
}

TEST_F(DomIntegrationTest, DomText_MultipleOperations_MaintainsSync) {
    DomElement* parent = create_dom_element("div");

    // Add multiple text nodes
    DomText* text1 = dom_element_append_text(parent, "One");
    DomText* text2 = dom_element_append_text(parent, "Two");
    DomText* text3 = dom_element_append_text(parent, "Three");
    ASSERT_NE(text1, nullptr);
    ASSERT_NE(text2, nullptr);
    ASSERT_NE(text3, nullptr);

    EXPECT_EQ(parent->native_element->length, 3);

    // Update middle text
    EXPECT_TRUE(dom_text_set_content(text2, "TWO"));

    // Verify all strings
    EXPECT_STREQ(((String*)parent->native_element->items[0].pointer)->chars, "One");
    EXPECT_STREQ(((String*)parent->native_element->items[1].pointer)->chars, "TWO");
    EXPECT_STREQ(((String*)parent->native_element->items[2].pointer)->chars, "Three");

    // Remove middle text
    EXPECT_TRUE(dom_text_remove(text2));

    EXPECT_EQ(parent->native_element->length, 2);
    EXPECT_STREQ(((String*)parent->native_element->items[0].pointer)->chars, "One");
    EXPECT_STREQ(((String*)parent->native_element->items[1].pointer)->chars, "Three");

    // Verify indices updated
    EXPECT_EQ(dom_text_get_child_index(text1), 0);
    EXPECT_EQ(dom_text_get_child_index(text3), 1);
}

TEST_F(DomIntegrationTest, DomText_MixedChildren_ElementsAndText) {
    // Build Lambda element tree with mixed children
    MarkBuilder builder(input);
    Item div_item = builder.element("div")
        .text("Before")
        .child(builder.element("span").text("Middle").final())
        .text("After")
        .final();

    ASSERT_NE(div_item.element, nullptr);

    // Set as input root
    input->root = div_item;

    // Build DOM tree from Lambda with DomDocument
    DomElement* parent = build_dom_tree_from_element(div_item.element, doc, nullptr);
    ASSERT_NE(parent, nullptr);

    // Verify structure in Lambda tree
    EXPECT_EQ(parent->native_element->length, 3);
    EXPECT_EQ(get_type_id(parent->native_element->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(parent->native_element->items[1]), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(parent->native_element->items[2]), LMD_TYPE_STRING);

    // Get the text nodes from DOM
    DomText* text1 = static_cast<DomText*>(parent->first_child);
    ASSERT_NE(text1, nullptr);
    ASSERT_TRUE(text1->is_text());
    EXPECT_TRUE(dom_text_is_backed(text1));

    DomElement* span = static_cast<DomElement*>(text1->next_sibling);
    ASSERT_NE(span, nullptr);
    ASSERT_TRUE(span->is_element());

    DomText* text2 = static_cast<DomText*>(span->next_sibling);
    ASSERT_NE(text2, nullptr);
    ASSERT_TRUE(text2->is_text());
    EXPECT_TRUE(dom_text_is_backed(text2));

    // Update text around element
    EXPECT_TRUE(dom_text_set_content(text1, "BEFORE"));
    EXPECT_TRUE(dom_text_set_content(text2, "AFTER"));

    // Verify Lambda tree updated
    EXPECT_STREQ(((String*)parent->native_element->items[0].pointer)->chars, "BEFORE");
    EXPECT_STREQ(((String*)parent->native_element->items[2].pointer)->chars, "AFTER");
}

TEST_F(DomIntegrationTest, DomText_ChildIndexTracking_WithRemoval) {
    DomElement* parent = create_dom_element("p");

    DomText* t0 = dom_element_append_text(parent, "Zero");
    DomText* t1 = dom_element_append_text(parent, "One");
    DomText* t2 = dom_element_append_text(parent, "Two");
    ASSERT_NE(t0, nullptr);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);

    EXPECT_EQ(dom_text_get_child_index(t0), 0);
    EXPECT_EQ(dom_text_get_child_index(t1), 1);
    EXPECT_EQ(dom_text_get_child_index(t2), 2);

    // Remove middle - indices should update
    EXPECT_TRUE(dom_text_remove(t1));

    EXPECT_EQ(dom_text_get_child_index(t0), 0);
    EXPECT_EQ(dom_text_get_child_index(t2), 1);
}

TEST_F(DomIntegrationTest, DomText_EmptyString_Backed) {
    DomElement* parent = create_dom_element("div");

    // Start with non-empty string
    DomText* text = dom_element_append_text(parent, "Initial");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(dom_text_is_backed(text));

    // Update to single space (empty strings become "lambda.nil" in MarkBuilder)
    EXPECT_TRUE(dom_text_set_content(text, " "));

    // Verify DomText updated
    EXPECT_STREQ(text->text, " ");
    EXPECT_EQ(text->length, 1u);

    // Verify Lambda String updated
    Item child = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    String* str = (String*)child.pointer;
    EXPECT_EQ(str->len, 1u);
    EXPECT_STREQ(str->chars, " ");
}

TEST_F(DomIntegrationTest, DomText_LongString_Backed) {
    const char* long_text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                           "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";

    DomElement* parent = create_dom_element("div");
    DomText* text = dom_element_append_text(parent, long_text);

    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->text, long_text);
    EXPECT_EQ(text->length, strlen(long_text));

    // Update to even longer string
    const char* longer = "This is an even longer string that tests memory handling and proper allocation strategies.";
    EXPECT_TRUE(dom_text_set_content(text, longer));
    EXPECT_STREQ(text->text, longer);

    // Verify Lambda
    int64_t idx = dom_text_get_child_index(text);
    EXPECT_STREQ(((String*)parent->native_element->items[idx].pointer)->chars, longer);
}

TEST_F(DomIntegrationTest, DomText_SequentialUpdates_Backed) {
    DomElement* parent = create_dom_element("p");
    DomText* text = dom_element_append_text(parent, "Version1");
    ASSERT_NE(text, nullptr);

    // Multiple sequential updates
    EXPECT_TRUE(dom_text_set_content(text, "Version2"));
    EXPECT_STREQ(text->text, "Version2");

    EXPECT_TRUE(dom_text_set_content(text, "Version3"));
    EXPECT_STREQ(text->text, "Version3");

    EXPECT_TRUE(dom_text_set_content(text, "Final"));
    EXPECT_STREQ(text->text, "Final");

    // Verify Lambda has latest
    Item child = parent->native_element->items[0];
    EXPECT_STREQ(((String*)child.pointer)->chars, "Final");
}

TEST_F(DomIntegrationTest, DomText_RemoveFromMiddle_UpdatesIndices) {
    DomElement* parent = create_dom_element("div");

    DomText* texts[5];
    for (int i = 0; i < 5; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "Text%d", i);
        texts[i] = dom_element_append_text(parent, buf);
        ASSERT_NE(texts[i], nullptr);
    }

    EXPECT_EQ(parent->native_element->length, 5);

    // Remove text at index 2
    EXPECT_TRUE(dom_text_remove(texts[2]));

    // Verify structure
    EXPECT_EQ(parent->native_element->length, 4);
    EXPECT_STREQ(((String*)parent->native_element->items[0].pointer)->chars, "Text0");
    EXPECT_STREQ(((String*)parent->native_element->items[1].pointer)->chars, "Text1");
    EXPECT_STREQ(((String*)parent->native_element->items[2].pointer)->chars, "Text3");  // Shifted
    EXPECT_STREQ(((String*)parent->native_element->items[3].pointer)->chars, "Text4");  // Shifted

    // Verify indices updated
    EXPECT_EQ(dom_text_get_child_index(texts[0]), 0);
    EXPECT_EQ(dom_text_get_child_index(texts[1]), 1);
    EXPECT_EQ(dom_text_get_child_index(texts[3]), 2);  // Shifted
    EXPECT_EQ(dom_text_get_child_index(texts[4]), 3);  // Shifted
}

TEST_F(DomIntegrationTest, DomText_IsBacked_DetectsCorrectly) {
    DomElement* parent = create_dom_element("div");

    // Create backed text node
    DomText* backed = dom_element_append_text(parent, "Backed");
    ASSERT_NE(backed, nullptr);
    EXPECT_TRUE(dom_text_is_backed(backed));

    // Skip unbacked test - all text nodes are now backed
    // NOTE: Text nodes now always reference Lambda String*, there's no "unbacked" mode
}

// ============================================================================
// DomComment Backed Tests (Lambda Integration)
// ============================================================================

TEST_F(DomIntegrationTest, DomComment_CreateBacked) {
    DomElement* parent = create_dom_element("div");
    DomComment* comment = dom_element_append_comment(parent, " Test comment ");

    ASSERT_NE(comment, nullptr);
    EXPECT_NE(comment->native_element, nullptr);
    EXPECT_NE(((DomElement*)comment->parent)->doc->input, nullptr);
    EXPECT_EQ(comment->parent, parent);
    EXPECT_STREQ(comment->content, " Test comment ");
    EXPECT_EQ(comment->length, 14u);
    EXPECT_EQ(comment->node_type, DOM_NODE_COMMENT);

    // Verify Lambda backing
    TypeElmt* type = (TypeElmt*)comment->native_element->type;
    EXPECT_STREQ(type->name.str, "!--");
    EXPECT_EQ(comment->native_element->length, 1);
    EXPECT_STREQ(((String*)comment->native_element->items[0].pointer)->chars, " Test comment ");
}

TEST_F(DomIntegrationTest, DomComment_SetContentBacked_UpdatesLambda) {
    DomElement* parent = create_dom_element("div");
    DomComment* comment = dom_element_append_comment(parent, "Original");

    ASSERT_NE(comment, nullptr);
    EXPECT_TRUE(dom_comment_set_content(comment, "Updated"));

    // Verify DomComment updated
    EXPECT_STREQ(comment->content, "Updated");
    EXPECT_EQ(comment->length, 7u);

    // Verify Lambda updated
    EXPECT_STREQ(((String*)comment->native_element->items[0].pointer)->chars, "Updated");
}

TEST_F(DomIntegrationTest, DomComment_RemoveBacked_UpdatesLambda) {
    DomElement* parent = create_dom_element("div");
    DomComment* comment1 = dom_element_append_comment(parent, " First ");
    DomComment* comment2 = dom_element_append_comment(parent, " Second ");

    ASSERT_NE(comment1, nullptr);
    ASSERT_NE(comment2, nullptr);
    EXPECT_EQ(parent->native_element->length, 2);

    // Remove first comment
    EXPECT_TRUE(dom_comment_remove(comment1));

    // Verify Lambda updated
    EXPECT_EQ(parent->native_element->length, 1);
    TypeElmt* remaining_type = (TypeElmt*)parent->native_element->items[0].element->type;
    EXPECT_STREQ(remaining_type->name.str, "!--");
    EXPECT_STREQ(((String*)parent->native_element->items[0].element->items[0].pointer)->chars, " Second ");
}

TEST_F(DomIntegrationTest, DomComment_MixedChildren_ElementsTextAndComments) {
    DomElement* parent = create_dom_element("div");

    // Add mixed children: comment, text, comment
    // (Note: We use only backed operations for this test)
    DomComment* comment1 = dom_element_append_comment(parent, " Start ");
    DomText* text = dom_element_append_text(parent, "Content");
    DomComment* comment2 = dom_element_append_comment(parent, " End ");

    // Verify structure in Lambda (3 children: comment, text, comment)
    EXPECT_EQ(parent->native_element->length, 3);

    // Check types in Lambda tree
    TypeElmt* type0 = (TypeElmt*)parent->native_element->items[0].element->type;
    EXPECT_STREQ(type0->name.str, "!--");
    EXPECT_EQ(get_type_id(parent->native_element->items[1]), LMD_TYPE_STRING);
    TypeElmt* type2 = (TypeElmt*)parent->native_element->items[2].element->type;
    EXPECT_STREQ(type2->name.str, "!--");

    // Verify content
    EXPECT_STREQ(comment1->content, " Start ");
    EXPECT_STREQ(text->text, "Content");
    EXPECT_STREQ(comment2->content, " End ");

    // Verify DOM sibling chain traversal includes all three
    DomNode* node = parent->first_child;
    ASSERT_NE(node, (DomNode*)NULL);
    EXPECT_EQ(node->node_type, DOM_NODE_COMMENT);

    node = node->next_sibling;
    ASSERT_NE(node, (DomNode*)NULL);
    EXPECT_EQ(node->node_type, DOM_NODE_TEXT);

    node = node->next_sibling;
    ASSERT_NE(node, (DomNode*)NULL);
    EXPECT_EQ(node->node_type, DOM_NODE_COMMENT);

    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)NULL);  // No more siblings
}

TEST_F(DomIntegrationTest, DomComment_EmptyComment_Backed) {
    DomElement* parent = create_dom_element("div");
    DomComment* comment = dom_element_append_comment(parent, "");

    ASSERT_NE(comment, nullptr);
    EXPECT_STREQ(comment->content, "");
    EXPECT_EQ(comment->length, 0u);

    // Verify Lambda - empty comments should have empty String child
    EXPECT_EQ(comment->native_element->length, 0);

    // Set content on empty comment
    EXPECT_TRUE(dom_comment_set_content(comment, "Now has content"));
    EXPECT_STREQ(comment->content, "Now has content");
    EXPECT_EQ(comment->native_element->length, 1);
}

TEST_F(DomIntegrationTest, DomComment_LongComment_Backed) {
    const char* long_comment = "This is a very long comment that might span multiple lines\n"
                               "and contains various special characters: <>&\"'\n"
                               "It should be handled correctly by the backing system.";

    DomElement* parent = create_dom_element("div");
    DomComment* comment = dom_element_append_comment(parent, long_comment);

    ASSERT_NE(comment, nullptr);
    EXPECT_STREQ(comment->content, long_comment);
    EXPECT_EQ(comment->length, strlen(long_comment));

    // Verify Lambda backing
    EXPECT_STREQ(((String*)comment->native_element->items[0].pointer)->chars, long_comment);
}

TEST_F(DomIntegrationTest, DomComment_MultipleUpdates_Backed) {
    DomElement* parent = create_dom_element("div");
    DomComment* comment = dom_element_append_comment(parent, "Version1");

    ASSERT_NE(comment, nullptr);

    // Multiple sequential updates
    EXPECT_TRUE(dom_comment_set_content(comment, "Version2"));
    EXPECT_STREQ(comment->content, "Version2");

    EXPECT_TRUE(dom_comment_set_content(comment, "Version3"));
    EXPECT_STREQ(comment->content, "Version3");

    EXPECT_TRUE(dom_comment_set_content(comment, "Final"));
    EXPECT_STREQ(comment->content, "Final");

    // Verify Lambda has latest
    EXPECT_STREQ(((String*)comment->native_element->items[0].pointer)->chars, "Final");
}

TEST_F(DomIntegrationTest, DomComment_RemoveFromMiddle_UpdatesStructure) {
    DomElement* parent = create_dom_element("div");

    DomComment* comment1 = dom_element_append_comment(parent, " C1 ");
    DomComment* comment2 = dom_element_append_comment(parent, " C2 ");
    DomComment* comment3 = dom_element_append_comment(parent, " C3 ");

    EXPECT_EQ(parent->native_element->length, 3);

    // Remove middle comment
    EXPECT_TRUE(dom_comment_remove(comment2));

    // Verify Lambda structure
    EXPECT_EQ(parent->native_element->length, 2);

    TypeElmt* type0 = (TypeElmt*)parent->native_element->items[0].element->type;
    EXPECT_STREQ(type0->name.str, "!--");
    EXPECT_STREQ(((String*)parent->native_element->items[0].element->items[0].pointer)->chars, " C1 ");

    TypeElmt* type1 = (TypeElmt*)parent->native_element->items[1].element->type;
    EXPECT_STREQ(type1->name.str, "!--");
    EXPECT_STREQ(((String*)parent->native_element->items[1].element->items[0].pointer)->chars, " C3 ");
}

TEST_F(DomIntegrationTest, DomComment_IsBacked_DetectsCorrectly) {
    DomElement* parent = create_dom_element("div");

    // Create backed comment
    DomComment* backed = dom_element_append_comment(parent, "Backed");
    ASSERT_NE(backed, nullptr);
    EXPECT_TRUE(dom_comment_is_backed(backed));

    // Note: All comments are now backed (reference Lambda String*), no unbacked mode exists
}

TEST_F(DomIntegrationTest, DomComment_DOMTraversal_IncludesComments) {
    DomElement* parent = create_dom_element("div");

    // Build structure: comment -> text -> element -> comment
    DomComment* comment1 = dom_element_append_comment(parent, " Start ");
    DomText* text = dom_element_append_text(parent, "Text");
    DomElement* span = create_dom_element("span");
    dom_element_append_child(parent, span);
    DomComment* comment2 = dom_element_append_comment(parent, " End ");

    // Traverse via DOM sibling chain
    DomNode* child = parent->first_child;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->node_type, DOM_NODE_COMMENT);
    EXPECT_EQ(child, (DomNode*)comment1);

    child = child->next_sibling;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->node_type, DOM_NODE_TEXT);
    EXPECT_EQ(child, (DomNode*)text);

    child = child->next_sibling;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->node_type, DOM_NODE_ELEMENT);
    EXPECT_EQ(child, (DomNode*)span);

    child = child->next_sibling;
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->node_type, DOM_NODE_COMMENT);
    EXPECT_EQ(child, (DomNode*)comment2);

    child = child->next_sibling;
    EXPECT_EQ(child, nullptr);
}

// ============================================================================
// Comprehensive CRUD Integration Test
// ============================================================================

TEST_F(DomIntegrationTest, ComprehensiveCRUD_AllOperationsWithFormatValidation) {
    // Purpose: Verify DOM CRUD operations (Create, Update, format) are properly routed
    // to underlying Lambda elements by checking formatted HTML output

    // 1. CREATE: Build initial DOM element backed by Lambda
    DomElement* root = create_dom_element("div");
    ASSERT_NE(root, nullptr);
    ASSERT_NE(root->native_element, nullptr);
    ASSERT_NE(root->doc->input, nullptr);

    // Set input root so we can format it
    input->root = Item{.element = root->native_element};

    // 2. SET ATTRIBUTES: Add various attributes
    EXPECT_TRUE(dom_element_set_attribute(root, "id", "container"));
    EXPECT_TRUE(dom_element_set_attribute(root, "data-test", "value1"));
    EXPECT_TRUE(dom_element_set_attribute(root, "title", "Test Container"));

    // 3. INSERT TEXT: Add text content
    DomText* text1 = dom_element_append_text(root, "Hello ");
    ASSERT_NE(text1, nullptr);
    EXPECT_TRUE(dom_text_is_backed(text1));

    // 4. INSERT MORE TEXT: Add more text
    DomText* text2 = dom_element_append_text(root, "World");
    ASSERT_NE(text2, nullptr);

    // 5. INSERT COMMENT: Add a comment
    DomComment* comment = dom_element_append_comment(root, " test comment ");
    ASSERT_NE(comment, nullptr);
    EXPECT_TRUE(dom_comment_is_backed(comment));

    // 6. UPDATE OPERATIONS: Modify existing content
    EXPECT_TRUE(dom_text_set_content(text1, "Greetings "));
    EXPECT_TRUE(dom_text_set_content(text2, "Universe!"));
    EXPECT_TRUE(dom_element_set_attribute(root, "data-test", "updated"));
    EXPECT_TRUE(dom_comment_set_content(comment, " modified comment "));

    // 7. ADD MORE ATTRIBUTES: Test attribute addition after updates
    EXPECT_TRUE(dom_element_set_attribute(root, "role", "main"));

    // 8. Format the underlying Lambda element to HTML
    Item root_item = {.element = root->native_element};
    String* html = format_html(pool, root_item);
    ASSERT_NE(html, nullptr);
    ASSERT_NE(html->chars, nullptr);

    const char* output = html->chars;

    printf("=== Formatted HTML Output ===\n%s\n=== End Output ===\n", output);

    // Verify the output contains all our CRUD operations:

    // Check root attributes (all set/updated attributes should be present)
    EXPECT_TRUE(strstr(output, "id=\"container\"") != nullptr)
        << "Missing id attribute in output";
    EXPECT_TRUE(strstr(output, "data-test=\"updated\"") != nullptr)
        << "Missing updated data-test attribute in output";
    EXPECT_TRUE(strstr(output, "title=\"Test Container\"") != nullptr)
        << "Missing title attribute in output";
    EXPECT_TRUE(strstr(output, "role=\"main\"") != nullptr)
        << "Missing role attribute in output";

    // Check text content (all updated text should be present)
    EXPECT_TRUE(strstr(output, "Greetings ") != nullptr)
        << "Missing first updated text in output";
    EXPECT_TRUE(strstr(output, "Universe!") != nullptr)
        << "Missing second updated text in output";

    // Check comment (updated comment should be present)
    EXPECT_TRUE(strstr(output, "<!-- modified comment -->") != nullptr)
        << "Missing updated comment in output";

    // 9. VERIFY LAMBDA TREE STRUCTURE: Ensure backing structure is correct
    EXPECT_EQ(root->native_element->length, 3);  // text1, text2, comment

    // Verify first child is text1
    Item child0 = root->native_element->items[0];
    EXPECT_EQ(get_type_id(child0), LMD_TYPE_STRING);
    EXPECT_STREQ(((String*)child0.pointer)->chars, "Greetings ");

    // Verify second child is text2
    Item child1 = root->native_element->items[1];
    EXPECT_EQ(get_type_id(child1), LMD_TYPE_STRING);
    EXPECT_STREQ(((String*)child1.pointer)->chars, "Universe!");

    // Verify third child is comment
    Item child2 = root->native_element->items[2];
    EXPECT_EQ(get_type_id(child2), LMD_TYPE_ELEMENT);
    Element* comment_elem = child2.element;
    TypeElmt* comment_type = (TypeElmt*)comment_elem->type;
    EXPECT_STREQ(comment_type->name.str, "!--");
    EXPECT_EQ(comment_elem->length, 1);
    EXPECT_STREQ(((String*)comment_elem->items[0].pointer)->chars, " modified comment ");

    // 10. DELETE OPERATIONS: Test removal of text and comment nodes
    printf("\n=== Testing Deletions ===\n");
    printf("Before deletion - Lambda tree has %d children\n", root->native_element->length);

    // Delete text2 ("Universe!")
    EXPECT_TRUE(dom_text_remove(text2));
    printf("After removing text2 - Lambda tree has %d children\n", root->native_element->length);

    // Delete comment
    EXPECT_TRUE(dom_comment_remove(comment));
    printf("After removing comment - Lambda tree has %d children\n", root->native_element->length);

    // 11. Format after deletions and verify
    String* html_after = format_html(pool, root_item);
    ASSERT_NE(html_after, nullptr);

    const char* output_after = html_after->chars;
    printf("\n=== After Deletion Output ===\n%s\n=== End Output ===\n", output_after);

    // Verify "Universe!" was deleted
    EXPECT_TRUE(strstr(output_after, "Universe!") == nullptr)
        << "Universe! should be deleted but found in: " << output_after;

    // Verify comment was deleted
    EXPECT_TRUE(strstr(output_after, "<!--") == nullptr)
        << "Comment should be deleted but found in: " << output_after;

    // Verify "Greetings " still exists
    EXPECT_TRUE(strstr(output_after, "Greetings ") != nullptr)
        << "Greetings should remain after deletions in: " << output_after;

    // 12. Verify final Lambda tree structure
    printf("Final Lambda tree length: %d\n", root->native_element->length);
    EXPECT_EQ(root->native_element->length, 1) << "Should have 1 child remaining";

    if (root->native_element->length > 0) {
        Item remaining = root->native_element->items[0];
        EXPECT_EQ(get_type_id(remaining), LMD_TYPE_STRING);
        if (get_type_id(remaining) == LMD_TYPE_STRING) {
            EXPECT_STREQ(((String*)remaining.pointer)->chars, "Greetings ");
        }
    }
}

// ============================================================================
// Advanced CRUD Combination Tests - Comprehensive Coverage
// ============================================================================

TEST_F(DomIntegrationTest, CRUD_InterleavedTextAndComments) {
    // Test interleaved text and comment operations
    DomElement* root = create_dom_element("div");

    // Create pattern: T1 C1 T2 C2 T3
    DomText* t1 = dom_element_append_text(root, "Text1");
    DomComment* c1 = dom_element_append_comment(root, " Comment1 ");
    DomText* t2 = dom_element_append_text(root, "Text2");
    DomComment* c2 = dom_element_append_comment(root, " Comment2 ");
    DomText* t3 = dom_element_append_text(root, "Text3");

    // Verify Lambda tree structure
    EXPECT_EQ(root->native_element->length, 5);
    EXPECT_EQ(get_type_id(root->native_element->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(root->native_element->items[1]), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(root->native_element->items[2]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(root->native_element->items[3]), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(root->native_element->items[4]), LMD_TYPE_STRING);

    // Verify DOM tree traversal
    DomNode* node = root->first_child;
    EXPECT_EQ(node, (DomNode*)t1);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)c1);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t2);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)c2);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t3);
    node = node->next_sibling;
    EXPECT_EQ(node, nullptr);

    // Update all nodes
    EXPECT_TRUE(dom_text_set_content(t1, "UpdatedText1"));
    EXPECT_TRUE(dom_comment_set_content(c1, " UpdatedComment1 "));
    EXPECT_TRUE(dom_text_set_content(t2, "UpdatedText2"));
    EXPECT_TRUE(dom_comment_set_content(c2, " UpdatedComment2 "));
    EXPECT_TRUE(dom_text_set_content(t3, "UpdatedText3"));

    // Verify Lambda tree has all updates
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "UpdatedText1");
    EXPECT_STREQ(((String*)root->native_element->items[1].element->items[0].pointer)->chars, " UpdatedComment1 ");
    EXPECT_STREQ(((String*)root->native_element->items[2].pointer)->chars, "UpdatedText2");
    EXPECT_STREQ(((String*)root->native_element->items[3].element->items[0].pointer)->chars, " UpdatedComment2 ");
    EXPECT_STREQ(((String*)root->native_element->items[4].pointer)->chars, "UpdatedText3");

    // Remove alternating nodes (c1, t2, c2)
    EXPECT_TRUE(dom_comment_remove(c1));
    EXPECT_TRUE(dom_text_remove(t2));
    EXPECT_TRUE(dom_comment_remove(c2));

    // Verify final structure: T1 T3
    EXPECT_EQ(root->native_element->length, 2);
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "UpdatedText1");
    EXPECT_STREQ(((String*)root->native_element->items[1].pointer)->chars, "UpdatedText3");

    // Verify DOM traversal
    node = root->first_child;
    EXPECT_EQ(node, (DomNode*)t1);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t3);
    node = node->next_sibling;
    EXPECT_EQ(node, nullptr);
}

TEST_F(DomIntegrationTest, CRUD_NestedElements_WithMixedContent) {
    // Test nested elements with text and comments at multiple levels
    DomElement* root = create_dom_element("div");

    // Level 1: root with text and child element
    DomText* root_text = dom_element_append_text(root, "Root text ");
    DomComment* root_comment = dom_element_append_comment(root, " Root comment ");

    // Create nested element
    MarkBuilder builder(input);
    Item child_item = builder.element("span")
        .attr("class", "nested")
        .final();
    // Build DOM wrapper without parent (nullptr) so it doesn't link to root yet
    DomElement* child = build_dom_tree_from_element(child_item.element, doc, nullptr);
    // Now explicitly append child to root (updates both Lambda tree and DOM chain)
    dom_element_append_child(root, child);

    // Add content to nested element
    DomText* child_text = dom_element_append_text(child, "Child text");
    DomComment* child_comment = dom_element_append_comment(child, " Child comment ");

    // Verify root Lambda tree (text, comment, element)
    EXPECT_EQ(root->native_element->length, 3);
    EXPECT_EQ(get_type_id(root->native_element->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(root->native_element->items[1]), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(root->native_element->items[2]), LMD_TYPE_ELEMENT);

    // Verify child Lambda tree
    EXPECT_EQ(child->native_element->length, 2);
    EXPECT_EQ(get_type_id(child->native_element->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(child->native_element->items[1]), LMD_TYPE_ELEMENT);

    // Update content at both levels
    EXPECT_TRUE(dom_text_set_content(root_text, "Updated root text "));
    EXPECT_TRUE(dom_comment_set_content(root_comment, " Updated root comment "));
    EXPECT_TRUE(dom_text_set_content(child_text, "Updated child text"));
    EXPECT_TRUE(dom_comment_set_content(child_comment, " Updated child comment "));

    // Verify updates in Lambda tree
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "Updated root text ");
    EXPECT_STREQ(((String*)child->native_element->items[0].pointer)->chars, "Updated child text");

    // Remove content from child
    EXPECT_TRUE(dom_text_remove(child_text));
    EXPECT_EQ(child->native_element->length, 1);

    // Verify DOM traversal still works
    DomNode* node = root->first_child;
    EXPECT_EQ(node->node_type, DOM_NODE_TEXT);
    node = node->next_sibling;
    EXPECT_EQ(node->node_type, DOM_NODE_COMMENT);
    node = node->next_sibling;
    EXPECT_EQ(node->node_type, DOM_NODE_ELEMENT);
}

TEST_F(DomIntegrationTest, CRUD_BulkOperations_ManyNodes) {
    // Test bulk operations on many nodes
    DomElement* root = create_dom_element("ul");

    const int COUNT = 20;
    DomText* texts[COUNT];
    DomComment* comments[COUNT];

    // Create many interleaved text and comment nodes
    for (int i = 0; i < COUNT; i++) {
        char text_buf[50], comment_buf[50];
        snprintf(text_buf, sizeof(text_buf), "Item %d", i);
        snprintf(comment_buf, sizeof(comment_buf), " Comment %d ", i);

        texts[i] = dom_element_append_text(root, text_buf);
        comments[i] = dom_element_append_comment(root, comment_buf);
    }

    // Verify count: 20 texts + 20 comments = 40 children
    EXPECT_EQ(root->native_element->length, COUNT * 2);

    // Update every even-indexed text node
    for (int i = 0; i < COUNT; i += 2) {
        char buf[50];
        snprintf(buf, sizeof(buf), "Updated Item %d", i);
        EXPECT_TRUE(dom_text_set_content(texts[i], buf));
    }

    // Update every odd-indexed comment node
    for (int i = 1; i < COUNT; i += 2) {
        char buf[50];
        snprintf(buf, sizeof(buf), " Updated Comment %d ", i);
        EXPECT_TRUE(dom_comment_set_content(comments[i], buf));
    }

    // Verify updates in Lambda tree
    for (int i = 0; i < COUNT; i += 2) {
        char expected[50];
        snprintf(expected, sizeof(expected), "Updated Item %d", i);
        EXPECT_STREQ(((String*)root->native_element->items[i * 2].pointer)->chars, expected);
    }

    // Remove every third node (text or comment)
    int removed_count = 0;
    for (int i = 0; i < COUNT; i += 3) {
        EXPECT_TRUE(dom_text_remove(texts[i]));
        removed_count++;
        if (i + 1 < COUNT) {
            EXPECT_TRUE(dom_comment_remove(comments[i + 1]));
            removed_count++;
        }
    }

    // Verify count decreased
    EXPECT_EQ(root->native_element->length, COUNT * 2 - removed_count);

    // Verify DOM traversal integrity
    int traversal_count = 0;
    DomNode* node = root->first_child;
    while (node) {
        traversal_count++;
        node = node->next_sibling;
    }
    EXPECT_EQ(traversal_count, COUNT * 2 - removed_count);
}

TEST_F(DomIntegrationTest, CRUD_AttributeAndContent_Simultaneous) {
    // Test simultaneous attribute and content modifications
    DomElement* root = create_dom_element("article");

    // Set initial attributes
    EXPECT_TRUE(dom_element_set_attribute(root, "id", "article1"));
    EXPECT_TRUE(dom_element_set_attribute(root, "class", "featured"));
    EXPECT_TRUE(dom_element_set_attribute(root, "data-priority", "high"));

    // Add content
    DomText* title = dom_element_append_text(root, "Title");
    DomComment* separator = dom_element_append_comment(root, " --- ");
    DomText* body = dom_element_append_text(root, "Body text");

    // Verify initial state
    EXPECT_EQ(root->native_element->length, 3);
    EXPECT_STREQ(dom_element_get_attribute(root, "id"), "article1");

    // Update attributes
    EXPECT_TRUE(dom_element_set_attribute(root, "id", "article2"));
    EXPECT_TRUE(dom_element_set_attribute(root, "data-priority", "urgent"));
    EXPECT_TRUE(dom_element_add_class(root, "highlighted"));

    // Update content
    EXPECT_TRUE(dom_text_set_content(title, "Updated Title"));
    EXPECT_TRUE(dom_comment_set_content(separator, " === "));
    EXPECT_TRUE(dom_text_set_content(body, "Updated body text"));

    // Verify both attribute and content updates
    EXPECT_STREQ(dom_element_get_attribute(root, "id"), "article2");
    EXPECT_STREQ(dom_element_get_attribute(root, "data-priority"), "urgent");
    EXPECT_TRUE(dom_element_has_class(root, "featured"));
    EXPECT_TRUE(dom_element_has_class(root, "highlighted"));

    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "Updated Title");
    EXPECT_STREQ(((String*)root->native_element->items[1].element->items[0].pointer)->chars, " === ");
    EXPECT_STREQ(((String*)root->native_element->items[2].pointer)->chars, "Updated body text");

    // Remove attributes and content
    EXPECT_TRUE(dom_element_remove_attribute(root, "data-priority"));
    EXPECT_TRUE(dom_text_remove(title));
    EXPECT_TRUE(dom_comment_remove(separator));

    // Verify removals
    EXPECT_EQ(dom_element_get_attribute(root, "data-priority"), nullptr);
    EXPECT_EQ(root->native_element->length, 1);
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "Updated body text");
}

TEST_F(DomIntegrationTest, CRUD_EmptyToFull_FullToEmpty) {
    // Test transitions between empty and full states
    DomElement* root = create_dom_element("div");

    // Start empty
    EXPECT_EQ(root->native_element->length, 0);
    EXPECT_EQ(root->first_child, nullptr);

    // Add single text
    DomText* t1 = dom_element_append_text(root, "First");
    EXPECT_EQ(root->native_element->length, 1);
    EXPECT_EQ(root->first_child, (DomNode*)t1);

    // Add more content
    DomComment* c1 = dom_element_append_comment(root, " comment ");
    DomText* t2 = dom_element_append_text(root, "Second");
    EXPECT_EQ(root->native_element->length, 3);

    // Remove all content one by one
    EXPECT_TRUE(dom_text_remove(t1));
    EXPECT_EQ(root->native_element->length, 2);
    EXPECT_EQ(root->first_child, (DomNode*)c1);

    EXPECT_TRUE(dom_comment_remove(c1));
    EXPECT_EQ(root->native_element->length, 1);
    EXPECT_EQ(root->first_child, (DomNode*)t2);

    EXPECT_TRUE(dom_text_remove(t2));
    EXPECT_EQ(root->native_element->length, 0);
    EXPECT_EQ(root->first_child, nullptr);

    // Add content again
    DomText* t3 = dom_element_append_text(root, "Refilled");
    EXPECT_EQ(root->native_element->length, 1);
    EXPECT_EQ(root->first_child, (DomNode*)t3);
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "Refilled");
}

TEST_F(DomIntegrationTest, CRUD_UpdateExistingTextMultipleTimes) {
    // Test multiple updates to same text node
    DomElement* root = create_dom_element("p");
    DomText* text = dom_element_append_text(root, "Version1");

    // Get initial String pointer
    String* original_string = text->native_string;
    EXPECT_NE(original_string, nullptr);

    // Update 10 times
    for (int i = 2; i <= 10; i++) {
        char buf[50];
        snprintf(buf, sizeof(buf), "Version%d", i);
        EXPECT_TRUE(dom_text_set_content(text, buf));

        // Verify text updated
        EXPECT_STREQ(text->text, buf);

        // Verify Lambda String updated (new String created each time)
        EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, buf);

        // String pointer should change (new String allocated)
        EXPECT_NE(text->native_string, original_string);
        original_string = text->native_string;
    }

    // Final verification
    EXPECT_STREQ(text->text, "Version10");
    EXPECT_EQ(root->native_element->length, 1);
}

TEST_F(DomIntegrationTest, CRUD_RemoveFirstMiddleLast) {
    // Test removing nodes from different positions
    DomElement* root = create_dom_element("div");

    // Create 5 text nodes
    DomText* t0 = dom_element_append_text(root, "T0");
    DomText* t1 = dom_element_append_text(root, "T1");
    DomText* t2 = dom_element_append_text(root, "T2");
    DomText* t3 = dom_element_append_text(root, "T3");
    DomText* t4 = dom_element_append_text(root, "T4");

    EXPECT_EQ(root->native_element->length, 5);

    // Remove last (T4)
    EXPECT_TRUE(dom_text_remove(t4));
    EXPECT_EQ(root->native_element->length, 4);
    EXPECT_EQ(root->first_child, (DomNode*)t0);

    // Verify DOM chain
    DomNode* node = root->first_child;
    EXPECT_EQ(node, (DomNode*)t0);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t1);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t2);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t3);
    node = node->next_sibling;
    EXPECT_EQ(node, nullptr);

    // Remove first (T0)
    EXPECT_TRUE(dom_text_remove(t0));
    EXPECT_EQ(root->native_element->length, 3);
    EXPECT_EQ(root->first_child, (DomNode*)t1);

    // Remove middle (T2)
    EXPECT_TRUE(dom_text_remove(t2));
    EXPECT_EQ(root->native_element->length, 2);

    // Verify final structure: T1, T3
    EXPECT_STREQ(((String*)root->native_element->items[0].pointer)->chars, "T1");
    EXPECT_STREQ(((String*)root->native_element->items[1].pointer)->chars, "T3");

    node = root->first_child;
    EXPECT_EQ(node, (DomNode*)t1);
    EXPECT_EQ(node->prev_sibling, nullptr);
    node = node->next_sibling;
    EXPECT_EQ(node, (DomNode*)t3);
    EXPECT_EQ(node->next_sibling, nullptr);
}

TEST_F(DomIntegrationTest, CRUD_MixedOperations_StressTest) {
    // Stress test with random mix of operations
    DomElement* root = create_dom_element("section");

    // Phase 1: Rapid additions
    std::vector<DomText*> texts;
    std::vector<DomComment*> comments;

    for (int i = 0; i < 15; i++) {
        char buf[50];
        snprintf(buf, sizeof(buf), "Text%d", i);
        texts.push_back(dom_element_append_text(root, buf));

        snprintf(buf, sizeof(buf), " Comment%d ", i);
        comments.push_back(dom_element_append_comment(root, buf));
    }

    EXPECT_EQ(root->native_element->length, 30);

    // Phase 2: Random updates
    for (int i = 0; i < 15; i += 3) {
        char buf[50];
        snprintf(buf, sizeof(buf), "Updated%d", i);
        EXPECT_TRUE(dom_text_set_content(texts[i], buf));
    }

    for (int i = 1; i < 15; i += 3) {
        char buf[50];
        snprintf(buf, sizeof(buf), " Updated%d ", i);
        EXPECT_TRUE(dom_comment_set_content(comments[i], buf));
    }

    // Phase 3: Random removals
    int initial_count = root->native_element->length;
    int removed = 0;

    for (int i = 0; i < 15; i += 2) {
        if (i < (int)texts.size()) {
            EXPECT_TRUE(dom_text_remove(texts[i]));
            removed++;
        }
    }

    for (int i = 1; i < 15; i += 4) {
        if (i < (int)comments.size()) {
            EXPECT_TRUE(dom_comment_remove(comments[i]));
            removed++;
        }
    }

    EXPECT_EQ(root->native_element->length, initial_count - removed);

    // Phase 4: Verify DOM tree integrity
    int traverse_count = 0;
    DomNode* node = root->first_child;
    DomNode* prev = nullptr;

    while (node) {
        // Verify prev_sibling consistency
        EXPECT_EQ(node->prev_sibling, prev);

        traverse_count++;
        prev = node;
        node = node->next_sibling;
    }

    EXPECT_EQ(traverse_count, root->native_element->length);
}
