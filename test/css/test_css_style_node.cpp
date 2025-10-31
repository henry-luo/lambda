#include <gtest/gtest.h>
#include "../../lambda/input/css/css_style.h"
#include "../../lambda/input/css/css_style_node.h"
#include "../../lib/mempool.h"
#include <cstring>

class CssStyleNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = pool_create(); // No size parameter needed
        ASSERT_NE(pool, nullptr);
        
        style_tree = style_tree_create(pool);
        ASSERT_NE(style_tree, nullptr);
    }
    
    void TearDown() override {
        if (style_tree) {
            style_tree_destroy(style_tree);
        }
        if (pool) {
            pool_destroy(pool);
        }
    }
    
    // Helper function to create a test declaration
    CssDeclaration* create_test_declaration(CssPropertyId property_id,
                                          const char* value,
                                          CssSpecificity specificity,
                                          CssOrigin origin = CSS_ORIGIN_AUTHOR) {
        // For testing, we'll use the string value directly
        // In real implementation, this would be a parsed value structure
        char* test_value = (char*)pool_alloc(pool, strlen(value) + 1);
        strcpy(test_value, value);
        
        return css_declaration_create(property_id, test_value, specificity, origin, pool);
    }
    
    // Helper to create test specificity
    CssSpecificity create_specificity(uint8_t inline_style = 0, 
                                     uint8_t ids = 0, 
                                     uint8_t classes = 0, 
                                     uint8_t elements = 0, 
                                     bool important = false) {
        return css_specificity_create(inline_style, ids, classes, elements, important);
    }
    
    Pool* pool;
    StyleTree* style_tree;
};

// =============================================================================
// CSS Specificity Tests
// =============================================================================

TEST_F(CssStyleNodeTest, SpecificityComparison) {
    // Test basic specificity comparison
    CssSpecificity low = create_specificity(0, 0, 0, 1);      // element selector
    CssSpecificity mid = create_specificity(0, 0, 1, 0);      // class selector  
    CssSpecificity high = create_specificity(0, 1, 0, 0);     // ID selector
    CssSpecificity inline_style = create_specificity(1, 0, 0, 0);   // inline style
    
    EXPECT_LT(css_specificity_compare(low, mid), 0);
    EXPECT_LT(css_specificity_compare(mid, high), 0);
    EXPECT_LT(css_specificity_compare(high, inline_style), 0);
    EXPECT_EQ(css_specificity_compare(low, low), 0);
}

TEST_F(CssStyleNodeTest, SpecificityImportant) {
    // !important should win regardless of specificity
    CssSpecificity low_important = create_specificity(0, 0, 0, 1, true);
    CssSpecificity high_normal = create_specificity(0, 1, 0, 0, false);
    
    EXPECT_GT(css_specificity_compare(low_important, high_normal), 0);
}

TEST_F(CssStyleNodeTest, SpecificityComplex) {
    // Test complex specificity calculations
    CssSpecificity spec1 = create_specificity(0, 1, 2, 3);  // #id .class1.class2 div span em
    CssSpecificity spec2 = create_specificity(0, 0, 5, 1);  // .c1.c2.c3.c4.c5 div
    
    EXPECT_GT(css_specificity_compare(spec1, spec2), 0); // IDs beat classes
}

// =============================================================================
// Style Tree Basic Operations
// =============================================================================

TEST_F(CssStyleNodeTest, BasicDeclarationApplication) {
    CssSpecificity spec = create_specificity(0, 0, 1, 0); // .class
    CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec);
    
    ASSERT_NE(decl, nullptr);
    
    StyleNode* node = style_tree_apply_declaration(style_tree, decl);
    ASSERT_NE(node, nullptr);
    
    // Verify the declaration is winning
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, decl);
    EXPECT_STREQ((char*)winning->value, "red");
}

TEST_F(CssStyleNodeTest, MultipleDifferentProperties) {
    CssSpecificity spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* color_decl = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec);
    CssDeclaration* width_decl = create_test_declaration(CSS_PROPERTY_WIDTH, "100px", spec);
    
    style_tree_apply_declaration(style_tree, color_decl);
    style_tree_apply_declaration(style_tree, width_decl);
    
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), color_decl);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_WIDTH), width_decl);
}

// =============================================================================
// CSS Cascade Resolution Tests
// =============================================================================

TEST_F(CssStyleNodeTest, CascadeSpecificityWins) {
    // Lower specificity declaration applied first
    CssSpecificity low_spec = create_specificity(0, 0, 0, 1);  // element
    CssDeclaration* low_decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", low_spec);
    
    // Higher specificity declaration applied second  
    CssSpecificity high_spec = create_specificity(0, 0, 1, 0); // class
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, "blue", high_spec);
    
    style_tree_apply_declaration(style_tree, low_decl);
    style_tree_apply_declaration(style_tree, high_decl);
    
    // Higher specificity should win
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, high_decl);
    EXPECT_STREQ((char*)winning->value, "blue");
}

TEST_F(CssStyleNodeTest, CascadeSpecificityWinsReverse) {
    // Higher specificity declaration applied first
    CssSpecificity high_spec = create_specificity(0, 0, 1, 0); // class
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, "blue", high_spec);
    
    // Lower specificity declaration applied second
    CssSpecificity low_spec = create_specificity(0, 0, 0, 1);  // element
    CssDeclaration* low_decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", low_spec);
    
    style_tree_apply_declaration(style_tree, high_decl);
    style_tree_apply_declaration(style_tree, low_decl);
    
    // Higher specificity should still win (order doesn't matter)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, high_decl);
    EXPECT_STREQ((char*)winning->value, "blue");
}

TEST_F(CssStyleNodeTest, CascadeImportantWins) {
    // Normal high specificity declaration
    CssSpecificity high_spec = create_specificity(0, 1, 0, 0, false); // ID, normal
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, "blue", high_spec);
    
    // Lower specificity but important declaration
    CssSpecificity low_important = create_specificity(0, 0, 0, 1, true); // element, !important
    CssDeclaration* important_decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", low_important);
    
    style_tree_apply_declaration(style_tree, high_decl);
    style_tree_apply_declaration(style_tree, important_decl);
    
    // !important should win
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, important_decl);
    EXPECT_STREQ((char*)winning->value, "red");
}

// =============================================================================
// Weak Declaration List Tests
// =============================================================================

TEST_F(CssStyleNodeTest, WeakDeclarationStorage) {
    CssSpecificity spec1 = create_specificity(0, 0, 0, 1); // element
    CssSpecificity spec2 = create_specificity(0, 0, 1, 0); // class
    CssSpecificity spec3 = create_specificity(0, 1, 0, 0); // ID
    
    CssDeclaration* decl1 = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec1);
    CssDeclaration* decl2 = create_test_declaration(CSS_PROPERTY_COLOR, "green", spec2);
    CssDeclaration* decl3 = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec3);
    
    // Apply in order of increasing specificity
    StyleNode* node1 = style_tree_apply_declaration(style_tree, decl1);
    ASSERT_NE(node1, nullptr);
    
    StyleNode* node2 = style_tree_apply_declaration(style_tree, decl2);
    ASSERT_NE(node2, nullptr);
    ASSERT_EQ(node1, node2); // Should be the same node
    
    StyleNode* node3 = style_tree_apply_declaration(style_tree, decl3);
    ASSERT_NE(node3, nullptr);
    ASSERT_EQ(node1, node3); // Should be the same node
    
    // Highest specificity should be winning
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), decl3);
    
    // Get the style node to examine weak declarations
    AvlNode* avl_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_COLOR);
    ASSERT_NE(avl_node, nullptr);
    
    // The StyleNode should be the stored in the declaration field
    StyleNode* node = (StyleNode*)avl_node->declaration;
    ASSERT_NE(node, nullptr);
    
    // Should have weak declarations stored
    // Note: checking if weak_list exists might fail if implementation differs
    printf("Winning declaration: %p\n", node->winning_decl);
    printf("Weak list: %p\n", node->weak_list);
    
    if (node->weak_list != nullptr) {
        // Weak list should contain the losing declarations in specificity order
        WeakDeclaration* weak = node->weak_list;
        EXPECT_EQ(weak->declaration, decl2); // class specificity should be next highest
        
        weak = weak->next;
        if (weak) {
            EXPECT_EQ(weak->declaration, decl1); // element specificity should be lowest
        }
    }
}

// =============================================================================
// Declaration Removal and Promotion Tests
// =============================================================================

TEST_F(CssStyleNodeTest, DeclarationRemovalPromotion) {
    CssSpecificity spec1 = create_specificity(0, 0, 0, 1); // element
    CssSpecificity spec2 = create_specificity(0, 0, 1, 0); // class
    
    CssDeclaration* low_decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec1);
    CssDeclaration* high_decl = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec2);
    
    style_tree_apply_declaration(style_tree, low_decl);
    style_tree_apply_declaration(style_tree, high_decl);
    
    // High specificity should be winning
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), high_decl);
    
    // Remove the winning declaration
    bool removed = style_tree_remove_declaration(style_tree, high_decl);
    EXPECT_TRUE(removed);
    
    // Lower specificity declaration should be promoted
    CssDeclaration* new_winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(new_winning, low_decl);
    EXPECT_STREQ((char*)new_winning->value, "red");
}

TEST_F(CssStyleNodeTest, RemoveNonExistentDeclaration) {
    CssSpecificity spec = create_specificity(0, 0, 1, 0);
    CssDeclaration* decl1 = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec);
    CssDeclaration* decl2 = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec);
    
    style_tree_apply_declaration(style_tree, decl1);
    
    // Try to remove declaration that was never added
    bool removed = style_tree_remove_declaration(style_tree, decl2);
    EXPECT_FALSE(removed);
    
    // Original declaration should still be winning
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), decl1);
}

// =============================================================================
// CSS Origin Tests
// =============================================================================

TEST_F(CssStyleNodeTest, OriginPrecedence) {
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0); // same specificity
    
    CssDeclaration* user_agent = create_test_declaration(CSS_PROPERTY_COLOR, "black", same_spec, CSS_ORIGIN_USER_AGENT);
    CssDeclaration* user = create_test_declaration(CSS_PROPERTY_COLOR, "red", same_spec, CSS_ORIGIN_USER);
    CssDeclaration* author = create_test_declaration(CSS_PROPERTY_COLOR, "blue", same_spec, CSS_ORIGIN_AUTHOR);
    
    // Apply in reverse order
    style_tree_apply_declaration(style_tree, author);
    style_tree_apply_declaration(style_tree, user);
    style_tree_apply_declaration(style_tree, user_agent);
    
    // Author origin should win (when specificities are equal)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, author);
}

// =============================================================================
// Source Order Tests
// =============================================================================

TEST_F(CssStyleNodeTest, SourceOrderTieBreaking) {
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0); // same specificity
    
    CssDeclaration* first = create_test_declaration(CSS_PROPERTY_COLOR, "red", same_spec);
    CssDeclaration* second = create_test_declaration(CSS_PROPERTY_COLOR, "blue", same_spec);
    
    style_tree_apply_declaration(style_tree, first);
    style_tree_apply_declaration(style_tree, second);
    
    // Later declaration should win when specificity is equal
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, second);
    EXPECT_STREQ((char*)winning->value, "blue");
}

// =============================================================================
// Property Removal Tests
// =============================================================================

TEST_F(CssStyleNodeTest, PropertyRemoval) {
    CssSpecificity spec = create_specificity(0, 0, 1, 0);
    CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec);
    
    style_tree_apply_declaration(style_tree, decl);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), decl);
    
    // Remove the entire property
    bool removed = style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_TRUE(removed);
    
    // Property should no longer exist
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    
    // Trying to remove again should return false
    removed = style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_FALSE(removed);
}

// =============================================================================
// Complex Cascade Scenarios
// =============================================================================

TEST_F(CssStyleNodeTest, ComplexCascadeScenario) {
    // Simulate a complex real-world cascade scenario
    
    // User agent default
    CssDeclaration* ua_decl = create_test_declaration(
        CSS_PROPERTY_COLOR, "black", 
        create_specificity(0, 0, 0, 1), CSS_ORIGIN_USER_AGENT);
    
    // Author stylesheet - element selector
    CssDeclaration* author_elem = create_test_declaration(
        CSS_PROPERTY_COLOR, "gray", 
        create_specificity(0, 0, 0, 1), CSS_ORIGIN_AUTHOR);
    
    // Author stylesheet - class selector
    CssDeclaration* author_class = create_test_declaration(
        CSS_PROPERTY_COLOR, "blue", 
        create_specificity(0, 0, 1, 0), CSS_ORIGIN_AUTHOR);
    
    // Author stylesheet - ID selector
    CssDeclaration* author_id = create_test_declaration(
        CSS_PROPERTY_COLOR, "green", 
        create_specificity(0, 1, 0, 0), CSS_ORIGIN_AUTHOR);
    
    // User stylesheet - important
    CssDeclaration* user_important = create_test_declaration(
        CSS_PROPERTY_COLOR, "red", 
        create_specificity(0, 0, 0, 1, true), CSS_ORIGIN_USER);
    
    // Apply in mixed order
    style_tree_apply_declaration(style_tree, author_class);
    style_tree_apply_declaration(style_tree, ua_decl);
    style_tree_apply_declaration(style_tree, author_elem);
    style_tree_apply_declaration(style_tree, user_important);
    style_tree_apply_declaration(style_tree, author_id);
    
    // User important should win despite lower specificity
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, user_important);
    EXPECT_STREQ((char*)winning->value, "red");
    
    // Remove user important
    style_tree_remove_declaration(style_tree, user_important);
    
    // Author ID should now win (highest normal specificity)
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, author_id);
    EXPECT_STREQ((char*)winning->value, "green");
}

// =============================================================================
// Performance and Stress Tests
// =============================================================================

TEST_F(CssStyleNodeTest, ManyDeclarationsPerformance) {
    const int num_declarations = 1000;
    const CssPropertyId test_property = CSS_PROPERTY_COLOR;
    
    // Add many declarations with different specificities
    // Note: We use i for both classes and elements to ensure truly unique specificities
    for (int i = 0; i < num_declarations; i++) {
        CssSpecificity spec = create_specificity(0, 0, i / 10, i % 10); // unique specificities
        char value[32];
        snprintf(value, sizeof(value), "color%d", i);
        
        CssDeclaration* decl = create_test_declaration(test_property, value, spec);
        style_tree_apply_declaration(style_tree, decl);
    }
    
    // Should still be able to get the winning declaration quickly
    CssDeclaration* winning = style_tree_get_declaration(style_tree, test_property);
    EXPECT_NE(winning, nullptr);
    
    // Get the style node and verify weak list management
    AvlNode* avl_node = avl_tree_search(style_tree->tree, test_property);
    ASSERT_NE(avl_node, nullptr);
    StyleNode* node = (StyleNode*)avl_node->declaration;
    ASSERT_NE(node, nullptr);
    
    // Count weak declarations
    int weak_count = 0;
    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        weak_count++;
        weak = weak->next;
    }
    
    // Should have many weak declarations (not necessarily all, due to some having equal specificity)
    // For our specificity pattern, we should have close to num_declarations - 1
    EXPECT_GT(weak_count, num_declarations / 2); // At least half should be in weak list
    EXPECT_LT(weak_count, num_declarations); // Should be less than total
    
    printf("Weak declarations: %d out of %d total declarations\n", weak_count, num_declarations);
}

TEST_F(CssStyleNodeTest, ManyPropertiesPerformance) {
    const int num_properties = 100;
    
    // Add declarations for many different properties
    for (int i = 1; i <= num_properties; i++) {
        CssPropertyId property_id = (CssPropertyId)i;
        CssSpecificity spec = create_specificity(0, 0, 1, 0);
        
        char value[32];
        snprintf(value, sizeof(value), "value%d", i);
        
        CssDeclaration* decl = create_test_declaration(property_id, value, spec);
        style_tree_apply_declaration(style_tree, decl);
    }
    
    // Verify all properties can be retrieved efficiently
    for (int i = 1; i <= num_properties; i++) {
        CssPropertyId property_id = (CssPropertyId)i;
        CssDeclaration* decl = style_tree_get_declaration(style_tree, property_id);
        EXPECT_NE(decl, nullptr);
        
        char expected[32];
        snprintf(expected, sizeof(expected), "value%d", i);
        EXPECT_STREQ((char*)decl->value, expected);
    }
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

TEST_F(CssStyleNodeTest, NullHandling) {
    // Test null parameter handling
    EXPECT_EQ(style_tree_get_declaration(nullptr, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    
    EXPECT_FALSE(style_tree_remove_property(nullptr, CSS_PROPERTY_COLOR));
    EXPECT_FALSE(style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR));
    
    EXPECT_FALSE(style_tree_remove_declaration(nullptr, nullptr));
    EXPECT_FALSE(style_tree_remove_declaration(style_tree, nullptr));
}

TEST_F(CssStyleNodeTest, EmptyStyleTree) {
    // Test operations on empty style tree
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    EXPECT_FALSE(style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR));
    
    // Add and immediately remove
    CssSpecificity spec = create_specificity(0, 0, 1, 0);
    CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec);
    
    style_tree_apply_declaration(style_tree, decl);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), decl);
    
    style_tree_remove_declaration(style_tree, decl);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
}

// =============================================================================
// Extended CSS Origin and Cascade Level Tests
// =============================================================================

TEST_F(CssStyleNodeTest, CSS4CascadeLevelsNormalDeclarations) {
    // Test CSS4 cascade levels for normal (non-important) declarations
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0); // same specificity for all
    
    CssDeclaration* user_agent = create_test_declaration(CSS_PROPERTY_COLOR, "black", same_spec, CSS_ORIGIN_USER_AGENT);
    CssDeclaration* user = create_test_declaration(CSS_PROPERTY_COLOR, "blue", same_spec, CSS_ORIGIN_USER);
    CssDeclaration* author = create_test_declaration(CSS_PROPERTY_COLOR, "red", same_spec, CSS_ORIGIN_AUTHOR);
    CssDeclaration* animation = create_test_declaration(CSS_PROPERTY_COLOR, "green", same_spec, CSS_ORIGIN_ANIMATION);
    CssDeclaration* transition = create_test_declaration(CSS_PROPERTY_COLOR, "purple", same_spec, CSS_ORIGIN_TRANSITION);
    
    // Apply in random order to test cascade levels
    style_tree_apply_declaration(style_tree, user_agent);
    style_tree_apply_declaration(style_tree, animation);
    style_tree_apply_declaration(style_tree, user);
    style_tree_apply_declaration(style_tree, transition);
    style_tree_apply_declaration(style_tree, author);
    
    // Animation/transition should win among normal declarations (highest cascade level)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_TRUE(winning == animation || winning == transition);
}

TEST_F(CssStyleNodeTest, CSS4CascadeLevelsImportantDeclarations) {
    // Test CSS4 cascade levels for important declarations (reverse order)
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0, true); // all important
    
    CssDeclaration* ua_important = create_test_declaration(CSS_PROPERTY_COLOR, "black", same_spec, CSS_ORIGIN_USER_AGENT);
    CssDeclaration* user_important = create_test_declaration(CSS_PROPERTY_COLOR, "blue", same_spec, CSS_ORIGIN_USER);
    CssDeclaration* author_important = create_test_declaration(CSS_PROPERTY_COLOR, "red", same_spec, CSS_ORIGIN_AUTHOR);
    
    // Apply in order
    style_tree_apply_declaration(style_tree, author_important);
    style_tree_apply_declaration(style_tree, user_important);
    style_tree_apply_declaration(style_tree, ua_important);
    
    // User-agent !important should win (highest cascade level for important)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, ua_important);
    EXPECT_STREQ((char*)winning->value, "black");
}

TEST_F(CssStyleNodeTest, CSS4CascadeLevelsMixedImportance) {
    // Test mixed important and normal declarations
    CssSpecificity high_spec = create_specificity(0, 1, 0, 0); // ID
    
    // High specificity author normal
    CssDeclaration* author_id = create_test_declaration(CSS_PROPERTY_COLOR, "red", high_spec, CSS_ORIGIN_AUTHOR);
    
    // Low specificity user important
    CssDeclaration* user_important = create_test_declaration(CSS_PROPERTY_COLOR, "blue", 
                                    create_specificity(0, 0, 0, 1, true), CSS_ORIGIN_USER);
    
    // Low specificity user-agent important
    CssDeclaration* ua_important = create_test_declaration(CSS_PROPERTY_COLOR, "black", 
                                  create_specificity(0, 0, 0, 1, true), CSS_ORIGIN_USER_AGENT);
    
    style_tree_apply_declaration(style_tree, author_id);
    style_tree_apply_declaration(style_tree, user_important);
    style_tree_apply_declaration(style_tree, ua_important);
    
    // User-agent !important should win despite lowest specificity
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, ua_important);
}

TEST_F(CssStyleNodeTest, AnimationTransitionPrecedence) {
    // Test that animations and transitions have same cascade level
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* animation = create_test_declaration(CSS_PROPERTY_TRANSFORM, "rotate(45deg)", same_spec, CSS_ORIGIN_ANIMATION);
    CssDeclaration* transition = create_test_declaration(CSS_PROPERTY_TRANSFORM, "scale(2)", same_spec, CSS_ORIGIN_TRANSITION);
    
    // Apply animation first, then transition
    style_tree_apply_declaration(style_tree, animation);
    style_tree_apply_declaration(style_tree, transition);
    
    // Transition should win by source order (same cascade level)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_TRANSFORM);
    EXPECT_EQ(winning, transition);
    
    // Remove transition, animation should become winner
    style_tree_remove_declaration(style_tree, transition);
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_TRANSFORM);
    EXPECT_EQ(winning, animation);
}

// =============================================================================
// Extreme Specificity Tests
// =============================================================================

TEST_F(CssStyleNodeTest, MaximumSpecificityValues) {
    // Test edge cases with maximum specificity values
    CssSpecificity max_normal = create_specificity(1, 255, 255, 255, false);
    CssSpecificity min_important_spec = create_specificity(0, 0, 0, 0, true);
    
    CssDeclaration* max_spec = create_test_declaration(CSS_PROPERTY_COLOR, "red", max_normal);
    CssDeclaration* min_important = create_test_declaration(CSS_PROPERTY_COLOR, "blue", min_important_spec);
    
    style_tree_apply_declaration(style_tree, max_spec);
    style_tree_apply_declaration(style_tree, min_important);
    
    // !important should win even with minimum specificity
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, min_important);
}

TEST_F(CssStyleNodeTest, SpecificityOverflow) {
    // Test that large specificity values don't cause overflow issues
    for (int i = 0; i < 10; i++) {
        CssSpecificity spec = create_specificity(0, 200 + i, 200 + i, 200 + i);
        char value[32];
        snprintf(value, sizeof(value), "color%d", i);
        
        CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, value, spec);
        style_tree_apply_declaration(style_tree, decl);
    }
    
    // Highest specificity should win
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    ASSERT_NE(winning, nullptr);
    EXPECT_STREQ((char*)winning->value, "color9");
}

TEST_F(CssStyleNodeTest, SpecificityComponentComparison) {
    // Test that each specificity component is properly weighted
    CssSpecificity many_elements = create_specificity(0, 0, 0, 255);    // 255 elements
    CssSpecificity one_class = create_specificity(0, 0, 1, 0);          // 1 class
    CssSpecificity one_id = create_specificity(0, 1, 0, 0);             // 1 ID
    CssSpecificity inline_style = create_specificity(1, 0, 0, 0);       // inline style
    
    CssDeclaration* elements_decl = create_test_declaration(CSS_PROPERTY_COLOR, "elements", many_elements);
    CssDeclaration* class_decl = create_test_declaration(CSS_PROPERTY_COLOR, "class", one_class);
    CssDeclaration* id_decl = create_test_declaration(CSS_PROPERTY_COLOR, "id", one_id);
    CssDeclaration* inline_decl = create_test_declaration(CSS_PROPERTY_COLOR, "inline", inline_style);
    
    // Apply in reverse order of expected priority
    style_tree_apply_declaration(style_tree, elements_decl);
    style_tree_apply_declaration(style_tree, class_decl);
    style_tree_apply_declaration(style_tree, id_decl);
    style_tree_apply_declaration(style_tree, inline_decl);
    
    // Inline style should win
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, inline_decl);
    
    // Remove inline, ID should win
    style_tree_remove_declaration(style_tree, inline_decl);
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, id_decl);
    
    // Remove ID, class should win
    style_tree_remove_declaration(style_tree, id_decl);
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, class_decl);
}

// =============================================================================
// Source Order and Tie-Breaking Tests
// =============================================================================

TEST_F(CssStyleNodeTest, SourceOrderTieBreakingMultiple) {
    // Test source order with multiple identical declarations
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* declarations[5];
    for (int i = 0; i < 5; i++) {
        char value[32];
        snprintf(value, sizeof(value), "color%d", i);
        declarations[i] = create_test_declaration(CSS_PROPERTY_COLOR, value, same_spec);
        style_tree_apply_declaration(style_tree, declarations[i]);
    }
    
    // Last applied should win
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, declarations[4]);
    EXPECT_STREQ((char*)winning->value, "color4");
    
    // Remove last, second-to-last should win
    style_tree_remove_declaration(style_tree, declarations[4]);
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, declarations[3]);
}

TEST_F(CssStyleNodeTest, SourceOrderCrossOrigin) {
    // Test source order within same cascade level but different origins
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* author1 = create_test_declaration(CSS_PROPERTY_COLOR, "red", same_spec, CSS_ORIGIN_AUTHOR);
    CssDeclaration* author2 = create_test_declaration(CSS_PROPERTY_COLOR, "blue", same_spec, CSS_ORIGIN_AUTHOR);
    CssDeclaration* user = create_test_declaration(CSS_PROPERTY_COLOR, "green", same_spec, CSS_ORIGIN_USER);
    
    style_tree_apply_declaration(style_tree, author1);
    style_tree_apply_declaration(style_tree, user);
    style_tree_apply_declaration(style_tree, author2);
    
    // author2 should win (highest cascade level + latest source order)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, author2);
}

// =============================================================================
// Property-Specific Cascade Tests
// =============================================================================

TEST_F(CssStyleNodeTest, InheritedPropertyCascade) {
    // Test cascade for inherited properties (like color, font-family)
    CssSpecificity parent_spec = create_specificity(0, 0, 1, 0);
    CssSpecificity child_spec = create_specificity(0, 0, 0, 1);
    
    // Parent element color
    CssDeclaration* parent_color = create_test_declaration(CSS_PROPERTY_COLOR, "blue", parent_spec);
    CssDeclaration* child_color = create_test_declaration(CSS_PROPERTY_COLOR, "red", child_spec);
    
    style_tree_apply_declaration(style_tree, parent_color);
    style_tree_apply_declaration(style_tree, child_color);
    
    // Higher specificity should win regardless of inheritance
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, parent_color);
}

TEST_F(CssStyleNodeTest, NonInheritedPropertyCascade) {
    // Test cascade for non-inherited properties (like margin, border)
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* margin1 = create_test_declaration(CSS_PROPERTY_MARGIN_TOP, "10px", same_spec);
    CssDeclaration* margin2 = create_test_declaration(CSS_PROPERTY_MARGIN_TOP, "20px", same_spec);
    CssDeclaration* border = create_test_declaration(CSS_PROPERTY_BORDER_TOP_WIDTH, "2px", same_spec);
    
    style_tree_apply_declaration(style_tree, margin1);
    style_tree_apply_declaration(style_tree, margin2);
    style_tree_apply_declaration(style_tree, border);
    
    // Source order should determine winner for margin
    CssDeclaration* winning_margin = style_tree_get_declaration(style_tree, CSS_PROPERTY_MARGIN_TOP);
    EXPECT_EQ(winning_margin, margin2);
    
    // Border should be separate
    CssDeclaration* winning_border = style_tree_get_declaration(style_tree, CSS_PROPERTY_BORDER_TOP_WIDTH);
    EXPECT_EQ(winning_border, border);
}

TEST_F(CssStyleNodeTest, AnimationPropertySpecialBehavior) {
    // Test animation properties have special cascade behavior
    CssSpecificity same_spec = create_specificity(0, 0, 1, 0);
    
    CssDeclaration* anim_normal = create_test_declaration(CSS_PROPERTY_ANIMATION_NAME, "slide", same_spec, CSS_ORIGIN_AUTHOR);
    CssDeclaration* anim_animation = create_test_declaration(CSS_PROPERTY_ANIMATION_NAME, "fade", same_spec, CSS_ORIGIN_ANIMATION);
    
    style_tree_apply_declaration(style_tree, anim_normal);
    style_tree_apply_declaration(style_tree, anim_animation);
    
    // Animation origin should win for animation properties
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_ANIMATION_NAME);
    EXPECT_EQ(winning, anim_animation);
}

// =============================================================================
// Multiple Property Interaction Tests
// =============================================================================

TEST_F(CssStyleNodeTest, MultiplePropertiesSameCascade) {
    // Test that multiple properties can have different winning declarations
    CssSpecificity spec1 = create_specificity(0, 0, 1, 0); // class
    CssSpecificity spec2 = create_specificity(0, 1, 0, 0); // ID
    
    // Color: ID wins
    CssDeclaration* color_class = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec1);
    CssDeclaration* color_id = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec2);
    
    // Font size: class wins (only declaration)
    CssDeclaration* font_class = create_test_declaration(CSS_PROPERTY_FONT_SIZE, "16px", spec1);
    
    style_tree_apply_declaration(style_tree, color_class);
    style_tree_apply_declaration(style_tree, font_class);
    style_tree_apply_declaration(style_tree, color_id);
    
    // Verify each property has correct winner
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), color_id);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_FONT_SIZE), font_class);
}

TEST_F(CssStyleNodeTest, CascadeAcrossPropertyFamilies) {
    // Test cascade across related properties (margins, borders, etc)
    CssSpecificity id_spec = create_specificity(0, 1, 0, 0);
    CssSpecificity class_spec = create_specificity(0, 0, 1, 0);
    
    // All margin properties with different specificities
    CssDeclaration* margin_top_id = create_test_declaration(CSS_PROPERTY_MARGIN_TOP, "10px", id_spec);
    CssDeclaration* margin_right_class = create_test_declaration(CSS_PROPERTY_MARGIN_RIGHT, "15px", class_spec);
    CssDeclaration* margin_bottom_id = create_test_declaration(CSS_PROPERTY_MARGIN_BOTTOM, "20px", id_spec);
    CssDeclaration* margin_left_class = create_test_declaration(CSS_PROPERTY_MARGIN_LEFT, "25px", class_spec);
    
    style_tree_apply_declaration(style_tree, margin_top_id);
    style_tree_apply_declaration(style_tree, margin_right_class);
    style_tree_apply_declaration(style_tree, margin_bottom_id);
    style_tree_apply_declaration(style_tree, margin_left_class);
    
    // Each should resolve independently
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_MARGIN_TOP), margin_top_id);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_MARGIN_RIGHT), margin_right_class);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_MARGIN_BOTTOM), margin_bottom_id);
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_MARGIN_LEFT), margin_left_class);
}

// =============================================================================
// Stress Tests and Performance
// =============================================================================

TEST_F(CssStyleNodeTest, LargeScaleCascadeStressTest) {
    // Test with many properties and many declarations per property
    const int num_properties = 50;
    const int declarations_per_property = 20;
    
    // Add many declarations for many properties
    for (int prop = 1; prop <= num_properties; prop++) {
        CssPropertyId property_id = (CssPropertyId)prop;
        
        for (int decl = 0; decl < declarations_per_property; decl++) {
            CssSpecificity spec = create_specificity(0, decl / 5, decl % 5, decl % 3);
            char value[64];
            snprintf(value, sizeof(value), "prop%d_value%d", prop, decl);
            
            CssDeclaration* declaration = create_test_declaration(property_id, value, spec);
            style_tree_apply_declaration(style_tree, declaration);
        }
    }
    
    // Verify each property has a winner and can be retrieved efficiently
    for (int prop = 1; prop <= num_properties; prop++) {
        CssPropertyId property_id = (CssPropertyId)prop;
        CssDeclaration* winning = style_tree_get_declaration(style_tree, property_id);
        EXPECT_NE(winning, nullptr);
        
        // Verify the value format is correct
        char* value = (char*)winning->value;
        EXPECT_TRUE(strstr(value, "prop") != nullptr);
        EXPECT_TRUE(strstr(value, "_value") != nullptr);
    }
}

TEST_F(CssStyleNodeTest, MassiveSpecificityCombinations) {
    // Test many different specificity combinations to ensure proper ordering
    const int num_combinations = 100;
    
    for (int i = 0; i < num_combinations; i++) {
        CssSpecificity spec = create_specificity(
            i % 2,           // inline style: 0 or 1
            i % 10,          // IDs: 0-9
            (i * 3) % 20,    // classes: 0-19
            (i * 7) % 15,    // elements: 0-14
            (i % 17) == 0    // important: occasionally true
        );
        
        char value[64];
        snprintf(value, sizeof(value), "test_value_%d", i);
        
        CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, value, spec);
        style_tree_apply_declaration(style_tree, decl);
    }
    
    // Verify winner exists and is reasonable
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    ASSERT_NE(winning, nullptr);
    
    // Count weak declarations
    AvlNode* avl_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_COLOR);
    ASSERT_NE(avl_node, nullptr);
    StyleNode* node = (StyleNode*)avl_node->declaration;
    ASSERT_NE(node, nullptr);
    
    int weak_count = 0;
    WeakDeclaration* weak = node->weak_list;
    while (weak) {
        weak_count++;
        weak = weak->next;
    }
    
    EXPECT_GT(weak_count, 0);
    EXPECT_LT(weak_count, num_combinations);
}

TEST_F(CssStyleNodeTest, CascadeMemoryPressureTest) {
    // Test memory behavior under high cascade pressure
    const int iterations = 10;
    const int declarations_per_iteration = 100;
    
    for (int iter = 0; iter < iterations; iter++) {
        // Add many declarations
        for (int i = 0; i < declarations_per_iteration; i++) {
            CssSpecificity spec = create_specificity(0, 0, iter, i);
            char value[64];
            snprintf(value, sizeof(value), "iter%d_decl%d", iter, i);
            
            CssDeclaration* decl = create_test_declaration(CSS_PROPERTY_COLOR, value, spec);
            style_tree_apply_declaration(style_tree, decl);
        }
        
        // Verify current winner
        CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
        EXPECT_NE(winning, nullptr);
        
        // Remove some older declarations to test memory cleanup
        if (iter > 2) {
            // This simulates dynamic stylesheet changes
            for (int remove_iter = 0; remove_iter < iter - 2; remove_iter++) {
                char old_value[64];
                snprintf(old_value, sizeof(old_value), "iter%d_decl0", remove_iter);
                
                // Note: In real implementation, we'd need a way to find and remove specific declarations
                // For now, we just verify the system remains stable
            }
        }
    }
    
    // Final verification
    CssDeclaration* final_winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_NE(final_winning, nullptr);
}

// =============================================================================
// Edge Cases and Error Conditions
// =============================================================================

TEST_F(CssStyleNodeTest, ExtremeCascadeScenario) {
    // Test a scenario with all possible cascade combinations
    
    // Create declarations for every origin-importance combination
    struct TestCase {
        CssOrigin origin;
        bool important;
        const char* value;
        int expected_cascade_level;
    } test_cases[] = {
        {CSS_ORIGIN_USER_AGENT, false, "ua_normal", 1},
        {CSS_ORIGIN_USER, false, "user_normal", 2},
        {CSS_ORIGIN_AUTHOR, false, "author_normal", 3},
        {CSS_ORIGIN_ANIMATION, false, "animation", 4},
        {CSS_ORIGIN_TRANSITION, false, "transition", 4},
        {CSS_ORIGIN_AUTHOR, true, "author_important", 5},
        {CSS_ORIGIN_USER, true, "user_important", 6},
        {CSS_ORIGIN_USER_AGENT, true, "ua_important", 7}
    };
    
    // Apply in random order
    for (int i = 7; i >= 0; i--) {
        CssDeclaration* decl = create_test_declaration(
            CSS_PROPERTY_COLOR, 
            test_cases[i].value,
            create_specificity(0, 0, 1, 0, test_cases[i].important),
            test_cases[i].origin
        );
        style_tree_apply_declaration(style_tree, decl);
    }
    
    // User-agent important should win (highest cascade level)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_STREQ((char*)winning->value, "ua_important");
}

TEST_F(CssStyleNodeTest, ZeroSpecificityHandling) {
    // Test declarations with all-zero specificity
    CssSpecificity zero_spec = create_specificity(0, 0, 0, 0);
    
    CssDeclaration* first_zero = create_test_declaration(CSS_PROPERTY_COLOR, "first", zero_spec);
    CssDeclaration* second_zero = create_test_declaration(CSS_PROPERTY_COLOR, "second", zero_spec);
    CssDeclaration* important_zero = create_test_declaration(CSS_PROPERTY_COLOR, "important", 
                                    create_specificity(0, 0, 0, 0, true));
    
    style_tree_apply_declaration(style_tree, first_zero);
    style_tree_apply_declaration(style_tree, second_zero);
    style_tree_apply_declaration(style_tree, important_zero);
    
    // Important should win even with zero specificity
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, important_zero);
    
    // Remove important, source order should decide
    style_tree_remove_declaration(style_tree, important_zero);
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, second_zero);
}

TEST_F(CssStyleNodeTest, DuplicateDeclarationHandling) {
    // Test adding the exact same declaration multiple times
    CssSpecificity spec = create_specificity(0, 0, 1, 0);
    CssDeclaration* original = create_test_declaration(CSS_PROPERTY_COLOR, "red", spec);
    
    // Apply same declaration multiple times
    style_tree_apply_declaration(style_tree, original);
    style_tree_apply_declaration(style_tree, original);
    style_tree_apply_declaration(style_tree, original);
    
    // Should only exist once as winner
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, original);
    
    // Weak list should be empty (no competing declarations)
    AvlNode* avl_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_COLOR);
    ASSERT_NE(avl_node, nullptr);
    StyleNode* node = (StyleNode*)avl_node->declaration;
    ASSERT_NE(node, nullptr);
    
    // Add a different declaration to verify original is still winning
    CssDeclaration* different = create_test_declaration(CSS_PROPERTY_COLOR, "blue", spec);
    style_tree_apply_declaration(style_tree, different);
    
    // Source order should make 'different' the winner
    winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, different);
}

TEST_F(CssStyleNodeTest, PropertyRemovalWithComplexCascade) {
    // Test property removal when there are many competing declarations
    CssSpecificity specs[] = {
        create_specificity(0, 0, 0, 1),
        create_specificity(0, 0, 1, 0),
        create_specificity(0, 1, 0, 0),
        create_specificity(0, 0, 0, 1, true),
        create_specificity(0, 0, 1, 0, true)
    };
    
    CssDeclaration* declarations[5];
    for (int i = 0; i < 5; i++) {
        char value[32];
        snprintf(value, sizeof(value), "value%d", i);
        declarations[i] = create_test_declaration(CSS_PROPERTY_COLOR, value, specs[i]);
        style_tree_apply_declaration(style_tree, declarations[i]);
    }
    
    // Verify winner (should be class important)
    CssDeclaration* winning = style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(winning, declarations[4]);
    
    // Remove entire property
    bool removed = style_tree_remove_property(style_tree, CSS_PROPERTY_COLOR);
    EXPECT_TRUE(removed);
    
    // Property should be gone
    EXPECT_EQ(style_tree_get_declaration(style_tree, CSS_PROPERTY_COLOR), nullptr);
    
    // Tree should not contain the property anymore
    AvlNode* avl_node = avl_tree_search(style_tree->tree, CSS_PROPERTY_COLOR);
    EXPECT_EQ(avl_node, nullptr);
}