#include <gtest/gtest.h>

#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/selector_matcher.hpp"
#include "../../lambda/input/css/css_style.hpp"
#include "../../lambda/input/css/css_style_node.hpp"
#include "../../lambda/input/css/css_parser.hpp"
#include "../../lambda/mark_builder.hpp"
#include "../../lambda/input/input.hpp"
#include "helpers/css_test_helpers.hpp"

extern "C" {
#include "../../lib/mempool.h"
}

// Forward declaration for helper function
DomElement* build_dom_tree_from_element(Element* elem, Pool* pool, DomElement* parent);

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
    SelectorMatcher* matcher;

    void SetUp() override {
        // Create Input context for MarkBuilder
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        
        input = Input::create(pool);
        ASSERT_NE(input, nullptr);

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
        
        DomElement* dom_elem = build_dom_tree_from_element(elem_item.element, pool, nullptr);
        if (dom_elem) {
            dom_elem->input = input;
        }
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
    ASSERT_NE(element->input, nullptr);
    
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

