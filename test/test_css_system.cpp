#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <memory>

extern "C" {
#include "../lib/css_property_system.h"
#include "../lib/css_style_node.h"
#include "../lib/mempool.h"
}

/**
 * Comprehensive CSS Property System and Style Node Test Suite
 * 
 * This test suite covers:
 * - CSS property database functionality
 * - Property value parsing and validation
 * - CSS specificity calculation and comparison
 * - Style tree operations and cascade resolution
 * - CSS inheritance and computed value calculation
 * - Performance and memory management
 */

class CssPropertySystemTest : public ::testing::Test {
protected:
    Pool* pool;
    
    void SetUp() override {
        pool = pool_create(); // Create pool
        ASSERT_NE(pool, nullptr);
        
        // Initialize CSS property system
        ASSERT_TRUE(css_property_system_init(pool));
    }
    
    void TearDown() override {
        css_property_system_cleanup();
        if (pool) {
            pool_destroy(pool);
        }
    }
    
    // Helper function to create a test length value
    CssLength* create_test_length(double value, int unit) {
        CssLength* length = (CssLength*)pool_calloc(pool, sizeof(CssLength));
        length->value = value;
        length->unit = static_cast<decltype(length->unit)>(unit);
        return length;
    }
    
    // Helper function to create a test color value
    CssColor* create_test_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        CssColor* color = (CssColor*)pool_calloc(pool, sizeof(CssColor));
        color->r = r;
        color->g = g;
        color->b = b;
        color->a = a;
        color->type = CSS_COLOR_RGB;
        return color;
    }
};

// ============================================================================
// CSS Property Database Tests
// ============================================================================

TEST_F(CssPropertySystemTest, PropertyDatabaseInitialization) {
    // Check that system is initialized
    EXPECT_GT(css_property_get_count(), 0);
    
    // Check basic properties exist
    EXPECT_TRUE(css_property_exists(CSS_PROPERTY_COLOR));
    EXPECT_TRUE(css_property_exists(CSS_PROPERTY_WIDTH));
    EXPECT_TRUE(css_property_exists(CSS_PROPERTY_FONT_SIZE));
    EXPECT_TRUE(css_property_exists(CSS_PROPERTY_MARGIN_TOP));
    
    // Check non-existent property
    EXPECT_FALSE(css_property_exists(static_cast<CssPropertyId>(99999)));
}

TEST_F(CssPropertySystemTest, PropertyLookupById) {
    const CssProperty* color_prop = css_property_get_by_id(CSS_PROPERTY_COLOR);
    ASSERT_NE(color_prop, nullptr);
    EXPECT_EQ(color_prop->id, CSS_PROPERTY_COLOR);
    EXPECT_STREQ(color_prop->name, "color");
    EXPECT_EQ(color_prop->type, PROP_TYPE_COLOR);
    EXPECT_EQ(color_prop->inheritance, PROP_INHERIT_YES);
    
    const CssProperty* width_prop = css_property_get_by_id(CSS_PROPERTY_WIDTH);
    ASSERT_NE(width_prop, nullptr);
    EXPECT_EQ(width_prop->id, CSS_PROPERTY_WIDTH);
    EXPECT_STREQ(width_prop->name, "width");
    EXPECT_EQ(width_prop->type, PROP_TYPE_LENGTH);
    EXPECT_EQ(width_prop->inheritance, PROP_INHERIT_NO);
}

TEST_F(CssPropertySystemTest, PropertyLookupByName) {
    const CssProperty* color_prop = css_property_get_by_name("color");
    ASSERT_NE(color_prop, nullptr);
    EXPECT_EQ(color_prop->id, CSS_PROPERTY_COLOR);
    
    const CssProperty* margin_prop = css_property_get_by_name("margin-top");
    ASSERT_NE(margin_prop, nullptr);
    EXPECT_EQ(margin_prop->id, CSS_PROPERTY_MARGIN_TOP);
    
    // Test case sensitivity
    EXPECT_EQ(css_property_get_by_name("COLOR"), nullptr);
    
    // Test non-existent property
    EXPECT_EQ(css_property_get_by_name("non-existent-property"), nullptr);
}

TEST_F(CssPropertySystemTest, PropertyIdByName) {
    EXPECT_EQ(css_property_get_id_by_name("color"), CSS_PROPERTY_COLOR);
    EXPECT_EQ(css_property_get_id_by_name("width"), CSS_PROPERTY_WIDTH);
    EXPECT_EQ(css_property_get_id_by_name("font-size"), CSS_PROPERTY_FONT_SIZE);
    EXPECT_EQ(css_property_get_id_by_name("non-existent"), 0);
}

TEST_F(CssPropertySystemTest, PropertyCharacteristics) {
    // Test inherited properties
    EXPECT_TRUE(css_property_is_inherited(CSS_PROPERTY_COLOR));
    EXPECT_TRUE(css_property_is_inherited(CSS_PROPERTY_FONT_SIZE));
    EXPECT_TRUE(css_property_is_inherited(CSS_PROPERTY_FONT_FAMILY));
    
    // Test non-inherited properties
    EXPECT_FALSE(css_property_is_inherited(CSS_PROPERTY_WIDTH));
    EXPECT_FALSE(css_property_is_inherited(CSS_PROPERTY_HEIGHT));
    EXPECT_FALSE(css_property_is_inherited(CSS_PROPERTY_MARGIN_TOP));
    
    // Test animatable properties
    EXPECT_TRUE(css_property_is_animatable(CSS_PROPERTY_WIDTH));
    EXPECT_TRUE(css_property_is_animatable(CSS_PROPERTY_COLOR));
    EXPECT_TRUE(css_property_is_animatable(CSS_PROPERTY_OPACITY));
    
    // Test non-animatable properties
    EXPECT_FALSE(css_property_is_animatable(CSS_PROPERTY_DISPLAY));
    EXPECT_FALSE(css_property_is_animatable(CSS_PROPERTY_FLOAT));
}

TEST_F(CssPropertySystemTest, InitialValues) {
    void* color_initial = css_property_get_initial_value(CSS_PROPERTY_COLOR, pool);
    ASSERT_NE(color_initial, nullptr);
    
    void* width_initial = css_property_get_initial_value(CSS_PROPERTY_WIDTH, pool);
    ASSERT_NE(width_initial, nullptr);
    
    void* display_initial = css_property_get_initial_value(CSS_PROPERTY_DISPLAY, pool);
    ASSERT_NE(display_initial, nullptr);
}

// ============================================================================
// Custom Property Tests
// ============================================================================

TEST_F(CssPropertySystemTest, CustomProperties) {
    // Register custom properties
    CssPropertyId custom1 = css_property_register_custom("--my-color", pool);
    CssPropertyId custom2 = css_property_register_custom("--my-size", pool);
    
    EXPECT_GT(custom1, 0);
    EXPECT_GT(custom2, 0);
    EXPECT_NE(custom1, custom2);
    
    // Check custom property characteristics
    EXPECT_TRUE(css_property_is_custom(custom1));
    EXPECT_TRUE(css_property_is_custom(custom2));
    EXPECT_FALSE(css_property_is_custom(CSS_PROPERTY_COLOR));
    
    // Test lookup by name
    EXPECT_EQ(css_property_get_custom_id("--my-color"), custom1);
    EXPECT_EQ(css_property_get_custom_id("--my-size"), custom2);
    EXPECT_EQ(css_property_get_custom_id("--non-existent"), 0);
    
    // Test invalid custom property names
    EXPECT_EQ(css_property_register_custom("invalid-name", pool), 0);
    EXPECT_EQ(css_property_register_custom("my-color", pool), 0);
}

// ============================================================================
// Property Value Parsing Tests
// ============================================================================

TEST_F(CssPropertySystemTest, LengthParsing) {
    CssLength length;
    
    // Test pixel values
    EXPECT_TRUE(css_parse_length("10px", &length));
    EXPECT_DOUBLE_EQ(length.value, 10.0);
    EXPECT_EQ(length.unit, CSS_UNIT_PX);
    
    // Test em values
    EXPECT_TRUE(css_parse_length("1.5em", &length));
    EXPECT_DOUBLE_EQ(length.value, 1.5);
    EXPECT_EQ(length.unit, CSS_UNIT_EM);
    
    // Test percentage values
    EXPECT_TRUE(css_parse_length("50%", &length));
    EXPECT_DOUBLE_EQ(length.value, 50.0);
    EXPECT_EQ(length.unit, CSS_UNIT_PERCENT);
    
    // Test zero values (unitless)
    EXPECT_TRUE(css_parse_length("0", &length));
    EXPECT_DOUBLE_EQ(length.value, 0.0);
    EXPECT_EQ(length.unit, CSS_UNIT_PX);
    
    // Test keyword values
    EXPECT_TRUE(css_parse_length("auto", &length));
    
    // Test invalid values
    EXPECT_FALSE(css_parse_length("invalid", &length));
    EXPECT_FALSE(css_parse_length("10", &length)); // Non-zero unitless
    EXPECT_FALSE(css_parse_length("", &length));
}

TEST_F(CssPropertySystemTest, ColorParsing) {
    CssColor color;
    
    // Test hex colors
    EXPECT_TRUE(css_parse_color("#ff0000", &color));
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
    EXPECT_EQ(color.a, 255);
    EXPECT_EQ(color.type, CSS_COLOR_RGB);
    
    // Test named colors
    EXPECT_TRUE(css_parse_color("red", &color));
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
    EXPECT_EQ(color.type, CSS_COLOR_KEYWORD);
    
    EXPECT_TRUE(css_parse_color("blue", &color));
    EXPECT_EQ(color.r, 0);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 255);
    
    // Test special colors
    EXPECT_TRUE(css_parse_color("transparent", &color));
    EXPECT_EQ(color.type, CSS_COLOR_TRANSPARENT);
    EXPECT_EQ(color.a, 0);
    
    EXPECT_TRUE(css_parse_color("currentColor", &color));
    EXPECT_EQ(color.type, CSS_COLOR_CURRENT);
    
    // Test invalid colors
    EXPECT_FALSE(css_parse_color("invalid-color", &color));
    EXPECT_FALSE(css_parse_color("#gg0000", &color));
    EXPECT_FALSE(css_parse_color("", &color));
}

TEST_F(CssPropertySystemTest, PropertyValueValidation) {
    void* parsed_value;
    
    // Test color validation
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_COLOR, "red", &parsed_value, pool));
    ASSERT_NE(parsed_value, nullptr);
    
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_COLOR, "#ff0000", &parsed_value, pool));
    ASSERT_NE(parsed_value, nullptr);
    
    // Test length validation
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_WIDTH, "100px", &parsed_value, pool));
    ASSERT_NE(parsed_value, nullptr);
    
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_WIDTH, "50%", &parsed_value, pool));
    ASSERT_NE(parsed_value, nullptr);
    
    // Test global keywords
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_COLOR, "inherit", &parsed_value, pool));
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_WIDTH, "initial", &parsed_value, pool));
    EXPECT_TRUE(css_property_validate_value(CSS_PROPERTY_DISPLAY, "unset", &parsed_value, pool));
}

// ============================================================================
// CSS Specificity Tests
// ============================================================================

class CssSpecificityTest : public CssPropertySystemTest {};

TEST_F(CssSpecificityTest, SpecificityCreation) {
    CssSpecificity spec = css_specificity_create(0, 1, 2, 3, false);
    EXPECT_EQ(spec.inline_style, 0);
    EXPECT_EQ(spec.ids, 1);
    EXPECT_EQ(spec.classes, 2);
    EXPECT_EQ(spec.elements, 3);
    EXPECT_FALSE(spec.important);
    
    CssSpecificity important_spec = css_specificity_create(1, 0, 0, 0, true);
    EXPECT_EQ(important_spec.inline_style, 1);
    EXPECT_TRUE(important_spec.important);
}

TEST_F(CssSpecificityTest, SpecificityComparison) {
    CssSpecificity a = css_specificity_create(0, 0, 0, 1, false); // element
    CssSpecificity b = css_specificity_create(0, 0, 1, 0, false); // class
    CssSpecificity c = css_specificity_create(0, 1, 0, 0, false); // id
    CssSpecificity d = css_specificity_create(1, 0, 0, 0, false); // inline
    CssSpecificity e = css_specificity_create(0, 0, 0, 1, true);  // element !important
    
    // Basic hierarchy: element < class < id < inline
    EXPECT_LT(css_specificity_compare(a, b), 0); // element < class
    EXPECT_LT(css_specificity_compare(b, c), 0); // class < id
    EXPECT_LT(css_specificity_compare(c, d), 0); // id < inline
    
    // !important wins over everything non-important
    EXPECT_GT(css_specificity_compare(e, d), 0); // element !important > inline
    
    // Equal specificities
    CssSpecificity equal1 = css_specificity_create(0, 0, 1, 1, false);
    CssSpecificity equal2 = css_specificity_create(0, 0, 1, 1, false);
    EXPECT_EQ(css_specificity_compare(equal1, equal2), 0);
}

TEST_F(CssSpecificityTest, SpecificityValues) {
    CssSpecificity spec1 = css_specificity_create(0, 0, 0, 1, false);
    CssSpecificity spec2 = css_specificity_create(0, 0, 1, 0, false);
    CssSpecificity spec3 = css_specificity_create(0, 1, 0, 0, false);
    CssSpecificity spec_important = css_specificity_create(0, 0, 0, 1, true);
    
    uint32_t val1 = css_specificity_to_value(spec1);
    uint32_t val2 = css_specificity_to_value(spec2);
    uint32_t val3 = css_specificity_to_value(spec3);
    uint32_t val_important = css_specificity_to_value(spec_important);
    
    EXPECT_LT(val1, val2);
    EXPECT_LT(val2, val3);
    EXPECT_GT(val_important, val3); // !important should be highest
}

// ============================================================================
// CSS Declaration Tests
// ============================================================================

class CssDeclarationTest : public CssPropertySystemTest {};

TEST_F(CssDeclarationTest, DeclarationCreation) {
    CssSpecificity spec = css_specificity_create(0, 0, 1, 0, false);
    void* color_value = create_test_color(255, 0, 0);
    
    CssDeclaration* decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, spec, CSS_ORIGIN_AUTHOR, pool);
    
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->property_id, CSS_PROPERTY_COLOR);
    EXPECT_EQ(decl->value, color_value);
    EXPECT_EQ(decl->origin, CSS_ORIGIN_AUTHOR);
    EXPECT_EQ(decl->ref_count, 1);
    EXPECT_TRUE(decl->valid);
}

TEST_F(CssDeclarationTest, DeclarationReferenceCounting) {
    CssSpecificity spec = css_specificity_create(0, 0, 1, 0, false);
    void* color_value = create_test_color(255, 0, 0);
    
    CssDeclaration* decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, spec, CSS_ORIGIN_AUTHOR, pool);
    
    ASSERT_NE(decl, nullptr);
    EXPECT_EQ(decl->ref_count, 1);
    
    // Add reference
    css_declaration_ref(decl);
    EXPECT_EQ(decl->ref_count, 2);
    
    // Remove reference
    css_declaration_unref(decl);
    EXPECT_EQ(decl->ref_count, 1);
    EXPECT_TRUE(decl->valid);
    
    // Remove last reference
    css_declaration_unref(decl);
    EXPECT_EQ(decl->ref_count, 0);
    EXPECT_FALSE(decl->valid);
}

TEST_F(CssDeclarationTest, DeclarationCascadeComparison) {
    CssSpecificity low_spec = css_specificity_create(0, 0, 0, 1, false);
    CssSpecificity high_spec = css_specificity_create(0, 0, 1, 0, false);
    CssSpecificity important_spec = css_specificity_create(0, 0, 0, 1, true);
    
    void* color_value = create_test_color(255, 0, 0);
    
    CssDeclaration* low_decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, low_spec, CSS_ORIGIN_AUTHOR, pool);
    low_decl->source_order = 1;
    
    CssDeclaration* high_decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, high_spec, CSS_ORIGIN_AUTHOR, pool);
    high_decl->source_order = 2;
    
    CssDeclaration* important_decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, important_spec, CSS_ORIGIN_AUTHOR, pool);
    important_decl->source_order = 3;
    
    // Higher specificity wins
    EXPECT_GT(css_declaration_cascade_compare(high_decl, low_decl), 0);
    
    // !important wins over higher specificity
    EXPECT_GT(css_declaration_cascade_compare(important_decl, high_decl), 0);
    
    // Source order tie-breaking
    CssDeclaration* later_decl = css_declaration_create(
        CSS_PROPERTY_COLOR, color_value, low_spec, CSS_ORIGIN_AUTHOR, pool);
    later_decl->source_order = 10;
    
    EXPECT_GT(css_declaration_cascade_compare(later_decl, low_decl), 0);
}

// ============================================================================
// Style Tree Tests
// ============================================================================

class StyleTreeTest : public CssPropertySystemTest {
protected:
    StyleTree* style_tree;
    
    void SetUp() override {
        CssPropertySystemTest::SetUp();
        style_tree = style_tree_create(pool);
        ASSERT_NE(style_tree, nullptr);
    }
    
    void TearDown() override {
        if (style_tree) {
            style_tree_destroy(style_tree);
        }
        CssPropertySystemTest::TearDown();
    }
    
    CssDeclaration* create_test_declaration(CssPropertyId property_id,
                                           void* value,
                                           uint8_t classes = 1,
                                           bool important = false,
                                           int source_order = 1) {
        CssSpecificity spec = css_specificity_create(0, 0, classes, 0, important);
        CssDeclaration* decl = css_declaration_create(property_id, value, spec, CSS_ORIGIN_AUTHOR, pool);
        decl->source_order = source_order;
        return decl;
    }
};

TEST_F(StyleTreeTest, TreeCreationAndDestruction) {
    EXPECT_NE(style_tree, nullptr);
    EXPECT_EQ(style_tree->declaration_count, 0);
    EXPECT_GT(style_tree->next_source_order, 0);
}

TEST_F(StyleTreeTest, SingleDeclarationApplication) {
    void* color_value = create_test_color(255, 0, 0);
    CssDeclaration* color_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value);
    
    StyleNode* node = style_tree_apply_declaration(style_tree, color_decl);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->base.property_id, CSS_PROPERTY_COLOR);
    EXPECT_EQ(node->winning_decl, color_decl);
    EXPECT_EQ(node->weak_list, nullptr);
    
    // Check tree state
    EXPECT_EQ(style_tree->declaration_count, 1);
    
    // Check retrieval
    CssDeclaration* retrieved = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(retrieved, color_decl);
}

TEST_F(StyleTreeTest, MultipleDeclarationApplication) {
    void* color_value = create_test_color(255, 0, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    
    CssDeclaration* color_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value, 1, false, 1);
    CssDeclaration* width_decl = create_test_declaration(CSS_PROPERTY_WIDTH, width_value, 1, false, 2);
    
    StyleNode* color_node = style_tree_apply_declaration(style_tree, color_decl);
    StyleNode* width_node = style_tree_apply_declaration(style_tree, width_decl);
    
    ASSERT_NE(color_node, nullptr);
    ASSERT_NE(width_node, nullptr);
    EXPECT_NE(color_node, width_node);
    
    EXPECT_EQ(style_tree->declaration_count, 2);
    
    // Check both properties can be retrieved
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), color_decl);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_WIDTH), width_decl);
}

TEST_F(StyleTreeTest, CascadeResolution) {
    void* color_value1 = create_test_color(255, 0, 0); // red
    void* color_value2 = create_test_color(0, 255, 0); // green
    void* color_value3 = create_test_color(0, 0, 255); // blue
    
    // Apply declarations with different specificities
    CssDeclaration* low_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value1, 1, false, 1);
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value2, 2, false, 2);
    CssDeclaration* important_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value3, 1, true, 3);
    
    // Apply in order: low, high, important
    StyleNode* node1 = style_tree_apply_declaration(style_tree, low_decl);
    EXPECT_EQ(node1->winning_decl, low_decl);
    EXPECT_EQ(node1->weak_list, nullptr);
    
    StyleNode* node2 = style_tree_apply_declaration(style_tree, high_decl);
    EXPECT_EQ(node2, node1); // Same node
    EXPECT_EQ(node2->winning_decl, high_decl); // High specificity wins
    ASSERT_NE(node2->weak_list, nullptr);
    EXPECT_EQ(node2->weak_list->declaration, low_decl); // Low becomes weak
    
    StyleNode* node3 = style_tree_apply_declaration(style_tree, important_decl);
    EXPECT_EQ(node3, node1); // Same node
    EXPECT_EQ(node3->winning_decl, important_decl); // !important wins
    
    // Check weak list has both losing declarations
    WeakDeclaration* weak = node3->weak_list;
    ASSERT_NE(weak, nullptr);
    EXPECT_EQ(weak->declaration, high_decl); // Higher specificity first in weak list
    
    weak = weak->next;
    ASSERT_NE(weak, nullptr);
    EXPECT_EQ(weak->declaration, low_decl);
    
    EXPECT_EQ(weak->next, nullptr);
}

TEST_F(StyleTreeTest, PropertyRemoval) {
    void* color_value = create_test_color(255, 0, 0);
    CssDeclaration* color_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value);
    
    // Apply declaration
    StyleNode* node = style_tree_apply_declaration(style_tree, color_decl);
    ASSERT_NE(node, nullptr);
    
    // Verify it exists
    EXPECT_NE(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    
    // Remove property
    EXPECT_TRUE(style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR));
    
    // Verify it's gone
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    
    // Try to remove again
    EXPECT_FALSE(style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR));
}

TEST_F(StyleTreeTest, DeclarationRemoval) {
    void* color_value1 = create_test_color(255, 0, 0);
    void* color_value2 = create_test_color(0, 255, 0);
    
    CssDeclaration* low_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value1, 1, false, 1);
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, color_value2, 2, false, 2);
    
    // Apply both declarations
    style_tree_apply_declaration(style_tree, low_decl);
    StyleNode* node = style_tree_apply_declaration(style_tree, high_decl);
    
    // High declaration should be winning
    EXPECT_EQ(node->winning_decl, high_decl);
    ASSERT_NE(node->weak_list, nullptr);
    EXPECT_EQ(node->weak_list->declaration, low_decl);
    
    // Remove winning declaration
    EXPECT_TRUE(style_tree_remove_declaration(style_tree, high_decl));
    
    // Low declaration should be promoted to winning
    EXPECT_EQ(node->winning_decl, low_decl);
    EXPECT_EQ(node->weak_list, nullptr);
    
    // Remove remaining declaration
    EXPECT_TRUE(style_tree_remove_declaration(style_tree, low_decl));
    EXPECT_EQ(node->winning_decl, nullptr);
}

// ============================================================================
// Style Inheritance Tests
// ============================================================================

class StyleInheritanceTest : public CssPropertySystemTest {
protected:
    StyleTree* parent_tree;
    StyleTree* child_tree;
    
    void SetUp() override {
        CssPropertySystemTest::SetUp();
        parent_tree = style_tree_create(pool);
        child_tree = style_tree_create(pool);
        ASSERT_NE(parent_tree, nullptr);
        ASSERT_NE(child_tree, nullptr);
    }
    
    void TearDown() override {
        if (parent_tree) style_tree_destroy(parent_tree);
        if (child_tree) style_tree_destroy(child_tree);
        CssPropertySystemTest::TearDown();
    }
};

TEST_F(StyleInheritanceTest, InheritedPropertyInheritance) {
    // Set color on parent (inherited property)
    void* parent_color = create_test_color(255, 0, 0);
    CssSpecificity spec = css_specificity_create(0, 0, 1, 0, false);
    CssDeclaration* parent_decl = css_declaration_create(
        CSS_PROPERTY_COLOR, parent_color, spec, CSS_ORIGIN_AUTHOR, pool);
    
    style_tree_apply_declaration(parent_tree, parent_decl);
    
    // Apply inheritance
    int inherited_count = style_tree_apply_inheritance(child_tree, parent_tree);
    EXPECT_GT(inherited_count, 0);
    
    // Child should inherit parent's color
    void* child_color = style_tree_get_computed_value(child_tree, CSS_PROPERTY_COLOR, parent_tree);
    EXPECT_NE(child_color, nullptr);
    // Note: In a full implementation, we'd compare the actual color values
}

TEST_F(StyleInheritanceTest, NonInheritedPropertyNoInheritance) {
    // Set width on parent (non-inherited property)
    void* parent_width = create_test_length(200, CSS_UNIT_PX);
    CssSpecificity spec = css_specificity_create(0, 0, 1, 0, false);
    CssDeclaration* parent_decl = css_declaration_create(
        CSS_PROPERTY_WIDTH, parent_width, spec, CSS_ORIGIN_AUTHOR, pool);
    
    style_tree_apply_declaration(parent_tree, parent_decl);
    
    // Check that child doesn't inherit width
    CssDeclaration* child_width_decl = style_tree_get_declaration(child_tree, CSS_PROPERTY_WIDTH);
    EXPECT_EQ(child_width_decl, nullptr);
    
    // Child should get initial value, not parent's value
    void* child_width = style_tree_get_computed_value(child_tree, CSS_PROPERTY_WIDTH, parent_tree);
    // Should get initial value ("auto"), not parent's value
    EXPECT_NE(child_width, parent_width);
}

TEST_F(StyleInheritanceTest, ExplicitInheritance) {
    // Set width on parent
    void* parent_width = create_test_length(200, CSS_UNIT_PX);
    CssSpecificity parent_spec = css_specificity_create(0, 0, 1, 0, false);
    CssDeclaration* parent_decl = css_declaration_create(
        CSS_PROPERTY_WIDTH, parent_width, parent_spec, CSS_ORIGIN_AUTHOR, pool);
    
    style_tree_apply_declaration(parent_tree, parent_decl);
    
    // Explicitly inherit width on child (using "inherit" keyword would be checked in value parsing)
    bool inherited = style_tree_inherit_property(child_tree, parent_tree, CSS_PROPERTY_WIDTH);
    EXPECT_TRUE(inherited);
    
    // Child should now have width declaration
    CssDeclaration* child_decl = style_tree_get_declaration(child_tree, CSS_PROPERTY_WIDTH);
    EXPECT_NE(child_decl, nullptr);
}

// ============================================================================
// Style Tree Traversal and Statistics Tests
// ============================================================================

TEST_F(StyleTreeTest, TreeTraversal) {
    // Add multiple properties
    void* color_value = create_test_color(255, 0, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    void* height_value = create_test_length(200, CSS_UNIT_PX);
    
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_HEIGHT, height_value));
    
    // Test traversal
    std::vector<CssPropertyId> visited_properties;
    
    int count = style_tree_foreach(style_tree, [](StyleNode* node, void* context) -> bool {
        std::vector<CssPropertyId>* props = static_cast<std::vector<CssPropertyId>*>(context);
        props->push_back(node->base.property_id);
        return true;
    }, &visited_properties);
    
    EXPECT_EQ(count, 3);
    EXPECT_EQ(visited_properties.size(), 3);
    
    // Properties should be visited in sorted order (AVL tree in-order traversal)
    EXPECT_TRUE(std::is_sorted(visited_properties.begin(), visited_properties.end()));
}

TEST_F(StyleTreeTest, TreeStatistics) {
    // Add properties with cascading declarations
    void* color_value1 = create_test_color(255, 0, 0);
    void* color_value2 = create_test_color(0, 255, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    
    // Color with cascade (2 declarations)
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value1, 1));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value2, 2));
    
    // Width with single declaration
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value));
    
    int total_nodes, total_declarations;
    double avg_weak_count;
    
    style_tree_get_statistics(style_tree, &total_nodes, &total_declarations, &avg_weak_count);
    
    EXPECT_EQ(total_nodes, 2); // color and width
    EXPECT_EQ(total_declarations, 3); // 2 color + 1 width
    EXPECT_GT(avg_weak_count, 0); // Should have some weak declarations
}

// ============================================================================
// Advanced Style Operations Tests
// ============================================================================

TEST_F(StyleTreeTest, TreeCloning) {
    // Add some properties
    void* color_value = create_test_color(255, 0, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value));
    
    // Clone the tree
    Pool* clone_pool = pool_create();
    StyleTree* cloned = style_tree_clone(style_tree, clone_pool);
    
    ASSERT_NE(cloned, nullptr);
    
    // Check that properties are present in clone
    EXPECT_NE(style_tree_get_declaration(cloned, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_NE(style_tree_get_declaration(cloned, CSS_PROPERTY_WIDTH), nullptr);
    
    // Trees should be independent
    style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_NE(style_tree_get_declaration(cloned, CSS_PROPERTY_COLOR), nullptr);
    
    style_tree_destroy(cloned);
    pool_destroy(clone_pool);
}

TEST_F(StyleTreeTest, TreeMerging) {
    StyleTree* source_tree = style_tree_create(pool);
    ASSERT_NE(source_tree, nullptr);
    
    // Add properties to both trees
    void* color_value = create_test_color(255, 0, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    void* height_value = create_test_length(200, CSS_UNIT_PX);
    
    // Target tree: color + width
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value));
    
    // Source tree: width (different value) + height
    void* width_value2 = create_test_length(150, CSS_UNIT_PX);
    style_tree_apply_declaration(source_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value2, 2)); // Higher specificity
    style_tree_apply_declaration(source_tree, create_test_declaration(CSS_PROPERTY_HEIGHT, height_value));
    
    int merged_count = style_tree_merge(style_tree, source_tree);
    EXPECT_GT(merged_count, 0);
    
    // Target should now have all three properties
    EXPECT_NE(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_NE(style_tree_get_declaration(style_tree, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_NE(style_tree_get_declaration(style_tree, CSS_PROPERTY_HEIGHT), nullptr);
    
    // Width should have cascade with two declarations
    StyleNode* width_node = (StyleNode*)avl_tree_search(style_tree->tree, CSS_PROPERTY_WIDTH)->declaration;
    ASSERT_NE(width_node, nullptr);
    EXPECT_NE(width_node->weak_list, nullptr); // Should have weak declaration
    
    style_tree_destroy(source_tree);
}

TEST_F(StyleTreeTest, TreeSubset) {
    // Add multiple properties
    void* color_value = create_test_color(255, 0, 0);
    void* width_value = create_test_length(100, CSS_UNIT_PX);
    void* height_value = create_test_length(200, CSS_UNIT_PX);
    
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_COLOR, color_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_WIDTH, width_value));
    style_tree_apply_declaration(style_tree, create_test_declaration(CSS_PROPERTY_HEIGHT, height_value));
    
    // Create subset with only color and width
    CssPropertyId subset_props[] = { CSS_PROPERTY_COLOR, CSS_PROPERTY_WIDTH };
    Pool* subset_pool = pool_create();
    
    StyleTree* subset = style_tree_create_subset(style_tree, subset_props, 2, subset_pool);
    ASSERT_NE(subset, nullptr);
    
    // Subset should have color and width, but not height
    EXPECT_NE(style_tree_get_declaration(subset, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_NE(style_tree_get_declaration(subset, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_EQ(style_tree_get_declaration(subset, CSS_PROPERTY_HEIGHT), nullptr);
    
    style_tree_destroy(subset);
    pool_destroy(subset_pool);
}

// ============================================================================
// Performance and Memory Tests
// ============================================================================

TEST_F(StyleTreeTest, PerformanceStressTest) {
    const int property_count = 1000;
    const int declarations_per_property = 5;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Add many declarations to stress test the system
    for (int prop = 1; prop <= property_count; prop++) {
        CssPropertyId property_id = static_cast<CssPropertyId>(prop);
        
        for (int decl = 0; decl < declarations_per_property; decl++) {
            void* test_value = create_test_color(decl * 50, 0, 0); // Varying values
            CssDeclaration* declaration = create_test_declaration(
                property_id, test_value, decl + 1, false, decl + 1);
            
            StyleNode* node = style_tree_apply_declaration(style_tree, declaration);
            ASSERT_NE(node, nullptr);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    printf("Applied %d declarations to %d properties in %ld ms\n", 
           property_count * declarations_per_property, property_count, duration.count());
    
    // Verify tree state
    EXPECT_EQ(avl_tree_size(style_tree->tree), property_count);
    EXPECT_EQ(style_tree->declaration_count, property_count * declarations_per_property);
    
    // Test lookup performance
    start = std::chrono::high_resolution_clock::now();
    
    for (int prop = 1; prop <= property_count; prop++) {
        CssPropertyId property_id = static_cast<CssPropertyId>(prop);
        CssDeclaration* decl = style_tree_get_declaration(style_tree, property_id);
        EXPECT_NE(decl, nullptr);
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    printf("Looked up %d properties in %ld microseconds\n", property_count, duration.count());
    
    // Should complete within reasonable time
    EXPECT_LT(duration.count(), 10000); // 10ms for 1000 lookups
}

// ============================================================================
// Error Handling and Edge Cases
// ============================================================================

TEST_F(StyleTreeTest, NullParameterHandling) {
    // Test null style tree
    EXPECT_EQ(style_tree_apply_declaration(nullptr, nullptr), nullptr);
    EXPECT_EQ(style_tree_get_declaration(nullptr, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_FALSE(style_tree_remove_property(nullptr, CSS_PROPERTY_COLOR));
    
    // Test null declarations
    EXPECT_EQ(style_tree_apply_declaration(style_tree, nullptr), nullptr);
    EXPECT_FALSE(style_tree_remove_declaration(style_tree, nullptr));
    
    // Test traversal with null callback
    EXPECT_EQ(style_tree_foreach(style_tree, nullptr, nullptr), 0);
}

TEST_F(StyleTreeTest, EmptyTreeOperations) {
    // Operations on empty tree
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_FALSE(style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR));
    
    void* computed = style_tree_get_computed_value(style_tree, CSS_PROPERTY_COLOR, nullptr);
    // Should return initial value for inherited properties
    // Implementation detail: might return null or initial value
    
    // Traversal of empty tree
    int count = style_tree_foreach(style_tree, [](StyleNode* node, void* context) -> bool {
        return true;
    }, nullptr);
    EXPECT_EQ(count, 0);
    
    // Statistics of empty tree
    int total_nodes, total_declarations;
    double avg_weak_count;
    style_tree_get_statistics(style_tree, &total_nodes, &total_declarations, &avg_weak_count);
    
    EXPECT_EQ(total_nodes, 0);
    EXPECT_EQ(total_declarations, 0);
    EXPECT_EQ(avg_weak_count, 0.0);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}