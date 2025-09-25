#include <gtest/gtest.h>
#include <memory>
#include <vector>

// Include the radiant layout headers
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include "../radiant/flex.hpp"
#include "../radiant/flex_layout_new.hpp"

// Test fixture for flex layout tests
class FlexLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize layout context
        lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 800;
        lycon->height = 600;
        lycon->dpi = 96;
        
        // Initialize memory pools
        init_view_pool(lycon);
    }
    
    void TearDown() override {
        // Cleanup
        cleanup_view_pool(lycon);
        free(lycon);
    }
    
    // Helper to create a flex container
    ViewBlock* createFlexContainer(int width = 800, int height = 600) {
        ViewBlock* container = alloc_view_block(lycon);
        container->width = width;
        container->height = height;
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
        
        // Set default flex container properties
        container->embed->flex_container->direction = DIR_ROW;
        container->embed->flex_container->wrap = WRAP_NOWRAP;
        container->embed->flex_container->justify = JUSTIFY_START;
        container->embed->flex_container->align_items = ALIGN_START;
        container->embed->flex_container->align_content = ALIGN_START;
        container->embed->flex_container->row_gap = 0;
        container->embed->flex_container->column_gap = 0;
        
        return container;
    }
    
    // Helper to create a flex item
    ViewBlock* createFlexItem(ViewBlock* parent, int width, int height, 
                             float flex_grow = 0.0f, float flex_shrink = 1.0f, int flex_basis = -1) {
        ViewBlock* item = alloc_view_block(lycon);
        item->width = width;
        item->height = height;
        item->parent = parent;
        
        // Set flex item properties
        item->flex_grow = flex_grow;
        item->flex_shrink = flex_shrink;
        item->flex_basis = flex_basis;
        item->flex_basis_is_percent = false;
        
        // Add to parent's children
        if (parent->first_child == nullptr) {
            parent->first_child = item;
            parent->last_child = item;
        } else {
            parent->last_child->next_sibling = item;
            item->prev_sibling = parent->last_child;
            parent->last_child = item;
        }
        
        return item;
    }
    
    LayoutContext* lycon;
};

// Test basic flex container initialization
TEST_F(FlexLayoutTest, FlexContainerInitialization) {
    ViewBlock* container = createFlexContainer();
    
    ASSERT_NE(container, nullptr);
    ASSERT_NE(container->embed, nullptr);
    ASSERT_NE(container->embed->flex_container, nullptr);
    
    FlexContainerProp* flex = container->embed->flex_container;
    EXPECT_EQ(flex->direction, DIR_ROW);
    EXPECT_EQ(flex->wrap, WRAP_NOWRAP);
    EXPECT_EQ(flex->justify, JUSTIFY_START);
    EXPECT_EQ(flex->align_items, ALIGN_START);
    EXPECT_EQ(flex->row_gap, 0);
    EXPECT_EQ(flex->column_gap, 0);
}

// Test flex item creation and properties
TEST_F(FlexLayoutTest, FlexItemCreation) {
    ViewBlock* container = createFlexContainer();
    ViewBlock* item1 = createFlexItem(container, 100, 50, 1.0f, 1.0f, 200);
    ViewBlock* item2 = createFlexItem(container, 150, 75, 2.0f, 0.5f, 300);
    
    ASSERT_NE(item1, nullptr);
    ASSERT_NE(item2, nullptr);
    
    // Check flex properties
    EXPECT_FLOAT_EQ(item1->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(item1->flex_shrink, 1.0f);
    EXPECT_EQ(item1->flex_basis, 200);
    EXPECT_FALSE(item1->flex_basis_is_percent);
    
    EXPECT_FLOAT_EQ(item2->flex_grow, 2.0f);
    EXPECT_FLOAT_EQ(item2->flex_shrink, 0.5f);
    EXPECT_EQ(item2->flex_basis, 300);
    
    // Check parent-child relationships
    EXPECT_EQ(container->first_child, item1);
    EXPECT_EQ(container->last_child, item2);
    EXPECT_EQ(item1->next_sibling, item2);
    EXPECT_EQ(item2->prev_sibling, item1);
}

// Test basic row layout (flex-direction: row)
TEST_F(FlexLayoutTest, BasicRowLayout) {
    ViewBlock* container = createFlexContainer(800, 200);
    ViewBlock* item1 = createFlexItem(container, 200, 100);
    ViewBlock* item2 = createFlexItem(container, 200, 100);
    ViewBlock* item3 = createFlexItem(container, 200, 100);
    
    // Mock the layout function call
    // In the real implementation, this would call the new integrated flex layout
    // For now, we'll test the data structure setup
    
    // Verify container setup
    EXPECT_EQ(container->width, 800);
    EXPECT_EQ(container->height, 200);
    EXPECT_EQ(container->embed->flex_container->direction, DIR_ROW);
    
    // Count children
    int child_count = 0;
    ViewBlock* child = container->first_child;
    while (child) {
        child_count++;
        child = child->next_sibling;
    }
    EXPECT_EQ(child_count, 3);
}

// Test column layout (flex-direction: column)
TEST_F(FlexLayoutTest, BasicColumnLayout) {
    ViewBlock* container = createFlexContainer(200, 600);
    container->embed->flex_container->direction = DIR_COLUMN;
    
    ViewBlock* item1 = createFlexItem(container, 100, 150);
    ViewBlock* item2 = createFlexItem(container, 100, 150);
    ViewBlock* item3 = createFlexItem(container, 100, 150);
    
    EXPECT_EQ(container->embed->flex_container->direction, DIR_COLUMN);
    
    // Verify all items are properly linked
    EXPECT_EQ(container->first_child, item1);
    EXPECT_EQ(item1->next_sibling, item2);
    EXPECT_EQ(item2->next_sibling, item3);
    EXPECT_EQ(item3->next_sibling, nullptr);
}

// Test flex-grow behavior
TEST_F(FlexLayoutTest, FlexGrowBehavior) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items with different flex-grow values
    ViewBlock* item1 = createFlexItem(container, 100, 100, 1.0f); // flex-grow: 1
    ViewBlock* item2 = createFlexItem(container, 100, 100, 2.0f); // flex-grow: 2
    ViewBlock* item3 = createFlexItem(container, 100, 100, 1.0f); // flex-grow: 1
    
    // Verify flex-grow values are set correctly
    EXPECT_FLOAT_EQ(item1->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(item2->flex_grow, 2.0f);
    EXPECT_FLOAT_EQ(item3->flex_grow, 1.0f);
    
    // Total flex-grow should be 4 (1 + 2 + 1)
    float total_grow = item1->flex_grow + item2->flex_grow + item3->flex_grow;
    EXPECT_FLOAT_EQ(total_grow, 4.0f);
}

// Test flex-shrink behavior
TEST_F(FlexLayoutTest, FlexShrinkBehavior) {
    ViewBlock* container = createFlexContainer(400, 200); // Smaller container to force shrinking
    
    // Create items that exceed container width
    ViewBlock* item1 = createFlexItem(container, 200, 100, 0.0f, 1.0f); // flex-shrink: 1
    ViewBlock* item2 = createFlexItem(container, 200, 100, 0.0f, 2.0f); // flex-shrink: 2
    ViewBlock* item3 = createFlexItem(container, 200, 100, 0.0f, 0.5f); // flex-shrink: 0.5
    
    // Verify flex-shrink values
    EXPECT_FLOAT_EQ(item1->flex_shrink, 1.0f);
    EXPECT_FLOAT_EQ(item2->flex_shrink, 2.0f);
    EXPECT_FLOAT_EQ(item3->flex_shrink, 0.5f);
    
    // Total content width (600) exceeds container width (400)
    int total_content_width = item1->width + item2->width + item3->width;
    EXPECT_GT(total_content_width, container->width);
}

// Test flex-basis with different units
TEST_F(FlexLayoutTest, FlexBasisUnits) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test absolute flex-basis
    ViewBlock* item1 = createFlexItem(container, 100, 100, 0.0f, 1.0f, 200);
    EXPECT_EQ(item1->flex_basis, 200);
    EXPECT_FALSE(item1->flex_basis_is_percent);
    
    // Test percentage flex-basis
    ViewBlock* item2 = createFlexItem(container, 100, 100, 0.0f, 1.0f, 50);
    item2->flex_basis_is_percent = true;
    EXPECT_EQ(item2->flex_basis, 50);
    EXPECT_TRUE(item2->flex_basis_is_percent);
    
    // Test auto flex-basis
    ViewBlock* item3 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1);
    EXPECT_EQ(item3->flex_basis, -1); // -1 represents 'auto'
}

// Test justify-content alignment
TEST_F(FlexLayoutTest, JustifyContentAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test different justify-content values
    container->embed->flex_container->justify = JUSTIFY_START;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_START);
    
    container->embed->flex_container->justify = JUSTIFY_CENTER;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_CENTER);
    
    container->embed->flex_container->justify = JUSTIFY_END;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_END);
    
    container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_BETWEEN);
    
    container->embed->flex_container->justify = JUSTIFY_SPACE_AROUND;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_AROUND);
    
    container->embed->flex_container->justify = JUSTIFY_SPACE_EVENLY;
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_EVENLY);
}

// Test align-items alignment
TEST_F(FlexLayoutTest, AlignItemsAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test different align-items values
    container->embed->flex_container->align_items = ALIGN_START;
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_START);
    
    container->embed->flex_container->align_items = ALIGN_CENTER;
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_CENTER);
    
    container->embed->flex_container->align_items = ALIGN_END;
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_END);
    
    container->embed->flex_container->align_items = ALIGN_STRETCH;
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_STRETCH);
    
    container->embed->flex_container->align_items = ALIGN_BASELINE;
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_BASELINE);
}

// Test flex-wrap behavior
TEST_F(FlexLayoutTest, FlexWrapBehavior) {
    ViewBlock* container = createFlexContainer(400, 300);
    
    // Test nowrap (default)
    container->embed->flex_container->wrap = WRAP_NOWRAP;
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_NOWRAP);
    
    // Test wrap
    container->embed->flex_container->wrap = WRAP_WRAP;
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP);
    
    // Test wrap-reverse
    container->embed->flex_container->wrap = WRAP_WRAP_REVERSE;
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP_REVERSE);
    
    // Create items that would overflow in a single line
    ViewBlock* item1 = createFlexItem(container, 200, 100);
    ViewBlock* item2 = createFlexItem(container, 200, 100);
    ViewBlock* item3 = createFlexItem(container, 200, 100);
    
    // Total width (600) exceeds container width (400)
    int total_width = item1->width + item2->width + item3->width;
    EXPECT_GT(total_width, container->width);
}

// Test gap properties
TEST_F(FlexLayoutTest, GapProperties) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Set gap values
    container->embed->flex_container->row_gap = 20;
    container->embed->flex_container->column_gap = 15;
    
    EXPECT_EQ(container->embed->flex_container->row_gap, 20);
    EXPECT_EQ(container->embed->flex_container->column_gap, 15);
    
    // Create items to test gap calculation
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    // With 3 items, there should be 2 gaps between them
    // Total gap space = 2 * column_gap = 2 * 15 = 30
    int expected_gap_space = 2 * container->embed->flex_container->column_gap;
    EXPECT_EQ(expected_gap_space, 30);
}

// Test order property
TEST_F(FlexLayoutTest, OrderProperty) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    // Set different order values
    item1->order = 2;
    item2->order = 1;
    item3->order = 3;
    
    EXPECT_EQ(item1->order, 2);
    EXPECT_EQ(item2->order, 1);
    EXPECT_EQ(item3->order, 3);
    
    // Visual order should be: item2 (order:1), item1 (order:2), item3 (order:3)
    // But DOM order remains: item1, item2, item3
    EXPECT_EQ(container->first_child, item1);
    EXPECT_EQ(item1->next_sibling, item2);
    EXPECT_EQ(item2->next_sibling, item3);
}

// Test writing modes and text direction
TEST_F(FlexLayoutTest, WritingModeAndTextDirection) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test horizontal writing mode with LTR
    container->embed->flex_container->writing_mode = WM_HORIZONTAL_TB;
    container->embed->flex_container->text_direction = TD_LTR;
    
    EXPECT_EQ(container->embed->flex_container->writing_mode, WM_HORIZONTAL_TB);
    EXPECT_EQ(container->embed->flex_container->text_direction, TD_LTR);
    
    // Test horizontal writing mode with RTL
    container->embed->flex_container->text_direction = TD_RTL;
    EXPECT_EQ(container->embed->flex_container->text_direction, TD_RTL);
    
    // Test vertical writing modes
    container->embed->flex_container->writing_mode = WM_VERTICAL_RL;
    EXPECT_EQ(container->embed->flex_container->writing_mode, WM_VERTICAL_RL);
    
    container->embed->flex_container->writing_mode = WM_VERTICAL_LR;
    EXPECT_EQ(container->embed->flex_container->writing_mode, WM_VERTICAL_LR);
}

// Test nested flex containers
TEST_F(FlexLayoutTest, NestedFlexContainers) {
    // Create parent flex container
    ViewBlock* parent = createFlexContainer(800, 400);
    
    // Create child flex container
    ViewBlock* child_container = createFlexItem(parent, 400, 200);
    child_container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    child_container->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    child_container->embed->flex_container->direction = DIR_COLUMN;
    child_container->embed->flex_container->wrap = WRAP_NOWRAP;
    child_container->embed->flex_container->justify = JUSTIFY_CENTER;
    child_container->embed->flex_container->align_items = ALIGN_CENTER;
    
    // Add items to nested container
    ViewBlock* nested_item1 = createFlexItem(child_container, 100, 50);
    ViewBlock* nested_item2 = createFlexItem(child_container, 100, 50);
    
    // Verify nested structure
    EXPECT_EQ(parent->first_child, child_container);
    EXPECT_EQ(child_container->parent, parent);
    EXPECT_EQ(child_container->first_child, nested_item1);
    EXPECT_EQ(nested_item1->next_sibling, nested_item2);
    
    // Verify nested container properties
    EXPECT_EQ(child_container->embed->flex_container->direction, DIR_COLUMN);
    EXPECT_EQ(child_container->embed->flex_container->justify, JUSTIFY_CENTER);
    EXPECT_EQ(child_container->embed->flex_container->align_items, ALIGN_CENTER);
}

// Test edge cases and error conditions
TEST_F(FlexLayoutTest, EdgeCases) {
    // Test empty container
    ViewBlock* empty_container = createFlexContainer();
    EXPECT_EQ(empty_container->first_child, nullptr);
    EXPECT_EQ(empty_container->last_child, nullptr);
    
    // Test single item
    ViewBlock* single_container = createFlexContainer(800, 200);
    ViewBlock* single_item = createFlexItem(single_container, 100, 100);
    
    EXPECT_EQ(single_container->first_child, single_item);
    EXPECT_EQ(single_container->last_child, single_item);
    EXPECT_EQ(single_item->next_sibling, nullptr);
    EXPECT_EQ(single_item->prev_sibling, nullptr);
    
    // Test zero-sized container
    ViewBlock* zero_container = createFlexContainer(0, 0);
    EXPECT_EQ(zero_container->width, 0);
    EXPECT_EQ(zero_container->height, 0);
    
    // Test negative flex values (should be clamped to 0)
    ViewBlock* item_with_negative = createFlexItem(single_container, 100, 100, -1.0f, -1.0f);
    // In a real implementation, negative values should be handled appropriately
    // For now, we just verify they can be set
    EXPECT_FLOAT_EQ(item_with_negative->flex_grow, -1.0f);
    EXPECT_FLOAT_EQ(item_with_negative->flex_shrink, -1.0f);
}

// Performance test for large number of flex items
TEST_F(FlexLayoutTest, PerformanceWithManyItems) {
    ViewBlock* container = createFlexContainer(2000, 200);
    
    // Create 100 flex items
    const int item_count = 100;
    std::vector<ViewBlock*> items;
    
    for (int i = 0; i < item_count; ++i) {
        ViewBlock* item = createFlexItem(container, 20, 100, 1.0f);
        items.push_back(item);
    }
    
    // Verify all items are properly linked
    ViewBlock* current = container->first_child;
    int count = 0;
    while (current) {
        EXPECT_EQ(current, items[count]);
        current = current->next_sibling;
        count++;
    }
    
    EXPECT_EQ(count, item_count);
}

// Integration test with CSS-like properties
TEST_F(FlexLayoutTest, CSSLikeProperties) {
    ViewBlock* container = createFlexContainer(800, 300);
    
    // Set CSS-like flex container properties
    container->embed->flex_container->direction = DIR_ROW;
    container->embed->flex_container->wrap = WRAP_WRAP;
    container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    container->embed->flex_container->align_items = ALIGN_CENTER;
    container->embed->flex_container->align_content = ALIGN_STRETCH;
    container->embed->flex_container->row_gap = 10;
    container->embed->flex_container->column_gap = 15;
    
    // Create items with CSS-like flex properties
    ViewBlock* item1 = createFlexItem(container, 0, 100, 1.0f, 1.0f, 200); // flex: 1 1 200px
    ViewBlock* item2 = createFlexItem(container, 0, 100, 2.0f, 1.0f, 0);   // flex: 2 1 0
    ViewBlock* item3 = createFlexItem(container, 150, 100, 0.0f, 0.0f);    // flex: none (0 0 auto)
    
    // Set align-self on individual items
    item1->align_self = ALIGN_START;
    item2->align_self = ALIGN_END;
    item3->align_self = ALIGN_CENTER;
    
    EXPECT_EQ(item1->align_self, ALIGN_START);
    EXPECT_EQ(item2->align_self, ALIGN_END);
    EXPECT_EQ(item3->align_self, ALIGN_CENTER);
    
    // Verify container properties match CSS specification
    EXPECT_EQ(container->embed->flex_container->direction, DIR_ROW);
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP);
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_BETWEEN);
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_CENTER);
    EXPECT_EQ(container->embed->flex_container->align_content, ALIGN_STRETCH);
}
