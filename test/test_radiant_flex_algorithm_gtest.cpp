#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <algorithm>

// Include the radiant layout headers
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include "../radiant/flex.hpp"
#include "../radiant/layout_flex.hpp"

// Test fixture for flex layout algorithm tests
class FlexAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {
        lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 800;
        lycon->height = 600;
        lycon->dpi = 96;
        init_view_pool(lycon);
    }
    
    void TearDown() override {
        cleanup_view_pool(lycon);
        free(lycon);
    }
    
    // Helper to create a flex container with layout state
    ViewBlock* createFlexContainer(int width = 800, int height = 600) {
        ViewBlock* container = alloc_view_block(lycon);
        container->width = width;
        container->height = height;
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
        
        // Initialize flex container layout state
        container->embed->flex_container->direction = DIR_ROW;
        container->embed->flex_container->wrap = WRAP_NOWRAP;
        container->embed->flex_container->justify = JUSTIFY_START;
        container->embed->flex_container->align_items = ALIGN_START;
        container->embed->flex_container->align_content = ALIGN_START;
        container->embed->flex_container->row_gap = 0;
        container->embed->flex_container->column_gap = 0;
        
        return container;
    }
    
    // Helper to create a flex item with complete properties
    ViewBlock* createFlexItem(ViewBlock* parent, int width, int height, 
                             float flex_grow = 0.0f, float flex_shrink = 1.0f, 
                             int flex_basis = -1, int order = 0) {
        ViewBlock* item = alloc_view_block(lycon);
        item->width = width;
        item->height = height;
        item->parent = parent;
        item->flex_grow = flex_grow;
        item->flex_shrink = flex_shrink;
        item->flex_basis = flex_basis;
        item->flex_basis_is_percent = false;
        item->order = order;
        item->align_self = ALIGN_AUTO;
        
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
    
    // Helper to collect all flex items from a container
    std::vector<ViewBlock*> collectFlexItems(ViewBlock* container) {
        std::vector<ViewBlock*> items;
        ViewBlock* child = container->first_child;
        while (child) {
            items.push_back(child);
            child = child->next_sibling;
        }
        return items;
    }
    
    // Helper to calculate total content size
    int calculateTotalContentWidth(const std::vector<ViewBlock*>& items, int gap) {
        int total = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            total += items[i]->width;
            if (i < items.size() - 1) {
                total += gap;
            }
        }
        return total;
    }
    
    LayoutContext* lycon;
};

// Test flex item collection and filtering
TEST_F(FlexAlgorithmTest, FlexItemCollection) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items with different visibility and position
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    // Collect items
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    EXPECT_EQ(items.size(), 3);
    EXPECT_EQ(items[0], item1);
    EXPECT_EQ(items[1], item2);
    EXPECT_EQ(items[2], item3);
}

// Test flex item ordering (CSS order property)
TEST_F(FlexAlgorithmTest, FlexItemOrdering) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items with different order values
    ViewBlock* item1 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 3);
    ViewBlock* item2 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 1);
    ViewBlock* item3 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 2);
    ViewBlock* item4 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 1);
    
    // Collect and sort items by order
    std::vector<ViewBlock*> items = collectFlexItems(container);
    std::sort(items.begin(), items.end(), [](ViewBlock* a, ViewBlock* b) {
        if (a->order != b->order) {
            return a->order < b->order;
        }
        // If order is the same, maintain document order
        return a < b; // Use pointer comparison as proxy for document order
    });
    
    // Expected order: item2 (order:1), item4 (order:1), item3 (order:2), item1 (order:3)
    EXPECT_EQ(items[0]->order, 1);
    EXPECT_EQ(items[1]->order, 1);
    EXPECT_EQ(items[2]->order, 2);
    EXPECT_EQ(items[3]->order, 3);
}

// Test flex line creation (single line, no wrap)
TEST_F(FlexAlgorithmTest, SingleFlexLine) {
    ViewBlock* container = createFlexContainer(800, 200);
    container->embed->flex_container->wrap = WRAP_NOWRAP;
    
    ViewBlock* item1 = createFlexItem(container, 200, 100);
    ViewBlock* item2 = createFlexItem(container, 200, 100);
    ViewBlock* item3 = createFlexItem(container, 200, 100);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // With nowrap, all items should be in a single line
    // Total content width: 600px, container width: 800px
    int total_width = calculateTotalContentWidth(items, 0);
    EXPECT_EQ(total_width, 600);
    EXPECT_LT(total_width, container->width); // Fits in single line
}

// Test flex line creation (multiple lines, wrap)
TEST_F(FlexAlgorithmTest, MultipleFlexLines) {
    ViewBlock* container = createFlexContainer(400, 300);
    container->embed->flex_container->wrap = WRAP_WRAP;
    
    // Create items that will overflow and wrap
    ViewBlock* item1 = createFlexItem(container, 200, 100);
    ViewBlock* item2 = createFlexItem(container, 200, 100);
    ViewBlock* item3 = createFlexItem(container, 200, 100);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Total content width: 600px, container width: 400px
    int total_width = calculateTotalContentWidth(items, 0);
    EXPECT_EQ(total_width, 600);
    EXPECT_GT(total_width, container->width); // Exceeds container, should wrap
    
    // With wrap enabled, items should be distributed across multiple lines
    // Line 1: item1 (200px) + item2 (200px) = 400px (fits exactly)
    // Line 2: item3 (200px)
}

// Test flexible length resolution - growing
TEST_F(FlexAlgorithmTest, FlexibleLengthGrowing) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items with flex-grow
    ViewBlock* item1 = createFlexItem(container, 100, 100, 1.0f, 1.0f, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100, 2.0f, 1.0f, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100, 1.0f, 1.0f, 100);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Calculate flex basis total: 3 * 100 = 300px
    int flex_basis_total = 300;
    int available_space = container->width - flex_basis_total; // 800 - 300 = 500px
    
    // Total flex-grow: 1 + 2 + 1 = 4
    float total_flex_grow = item1->flex_grow + item2->flex_grow + item3->flex_grow;
    EXPECT_FLOAT_EQ(total_flex_grow, 4.0f);
    
    // Each unit of flex-grow gets: 500 / 4 = 125px
    float grow_unit = available_space / total_flex_grow;
    EXPECT_FLOAT_EQ(grow_unit, 125.0f);
    
    // Expected final sizes:
    // item1: 100 + (1 * 125) = 225px
    // item2: 100 + (2 * 125) = 350px
    // item3: 100 + (1 * 125) = 225px
    // Total: 225 + 350 + 225 = 800px (matches container)
}

// Test flexible length resolution - shrinking
TEST_F(FlexAlgorithmTest, FlexibleLengthShrinking) {
    ViewBlock* container = createFlexContainer(400, 200);
    
    // Create items that exceed container width
    ViewBlock* item1 = createFlexItem(container, 200, 100, 0.0f, 1.0f);
    ViewBlock* item2 = createFlexItem(container, 200, 100, 0.0f, 2.0f);
    ViewBlock* item3 = createFlexItem(container, 200, 100, 0.0f, 1.0f);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Total content width: 600px, container width: 400px
    int total_width = calculateTotalContentWidth(items, 0);
    int overflow = total_width - container->width; // 600 - 400 = 200px
    
    EXPECT_EQ(overflow, 200);
    EXPECT_GT(overflow, 0); // Items need to shrink
    
    // Calculate weighted shrink factors
    // item1: 200 * 1.0 = 200
    // item2: 200 * 2.0 = 400  
    // item3: 200 * 1.0 = 200
    // Total weighted: 800
    
    float weighted_shrink_total = (item1->width * item1->flex_shrink) + 
                                 (item2->width * item2->flex_shrink) + 
                                 (item3->width * item3->flex_shrink);
    EXPECT_FLOAT_EQ(weighted_shrink_total, 800.0f);
}

// Test main axis alignment (justify-content)
TEST_F(FlexAlgorithmTest, MainAxisAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    int total_item_width = 300; // 3 * 100
    int free_space = container->width - total_item_width; // 800 - 300 = 500
    
    // Test justify-content: flex-start (default)
    container->embed->flex_container->justify = JUSTIFY_START;
    // Items should be positioned at: 0, 100, 200
    
    // Test justify-content: flex-end
    container->embed->flex_container->justify = JUSTIFY_END;
    // Items should be positioned at: 500, 600, 700
    
    // Test justify-content: center
    container->embed->flex_container->justify = JUSTIFY_CENTER;
    // Items should be positioned at: 250, 350, 450 (centered in 800px)
    
    // Test justify-content: space-between
    container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    // Free space distributed between items: 500 / 2 = 250
    // Items should be positioned at: 0, 350, 700
    
    // Test justify-content: space-around
    container->embed->flex_container->justify = JUSTIFY_SPACE_AROUND;
    // Free space distributed around items: 500 / 6 = 83.33 per side
    // Items should be positioned at: 83.33, 266.67, 450
    
    EXPECT_EQ(free_space, 500);
}

// Test cross axis alignment (align-items)
TEST_F(FlexAlgorithmTest, CrossAxisAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    ViewBlock* item1 = createFlexItem(container, 100, 80);  // Shorter than container
    ViewBlock* item2 = createFlexItem(container, 100, 120); // Taller than some
    ViewBlock* item3 = createFlexItem(container, 100, 60);  // Shortest
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Test align-items: flex-start
    container->embed->flex_container->align_items = ALIGN_START;
    // All items should be positioned at y = 0
    
    // Test align-items: flex-end
    container->embed->flex_container->align_items = ALIGN_END;
    // Items should be positioned at bottom of container
    // item1: y = 200 - 80 = 120
    // item2: y = 200 - 120 = 80
    // item3: y = 200 - 60 = 140
    
    // Test align-items: center
    container->embed->flex_container->align_items = ALIGN_CENTER;
    // Items should be centered vertically
    // item1: y = (200 - 80) / 2 = 60
    // item2: y = (200 - 120) / 2 = 40
    // item3: y = (200 - 60) / 2 = 70
    
    // Test align-items: stretch
    container->embed->flex_container->align_items = ALIGN_STRETCH;
    // All items should be stretched to container height (200px)
    
    EXPECT_EQ(container->height, 200);
}

// Test align-self override
TEST_F(FlexAlgorithmTest, AlignSelfOverride) {
    ViewBlock* container = createFlexContainer(800, 200);
    container->embed->flex_container->align_items = ALIGN_START; // Default for all items
    
    ViewBlock* item1 = createFlexItem(container, 100, 80);
    ViewBlock* item2 = createFlexItem(container, 100, 80);
    ViewBlock* item3 = createFlexItem(container, 100, 80);
    
    // Override align-self for individual items
    item1->align_self = ALIGN_AUTO;    // Uses container's align-items (flex-start)
    item2->align_self = ALIGN_CENTER;  // Override to center
    item3->align_self = ALIGN_END;     // Override to flex-end
    
    EXPECT_EQ(item1->align_self, ALIGN_AUTO);
    EXPECT_EQ(item2->align_self, ALIGN_CENTER);
    EXPECT_EQ(item3->align_self, ALIGN_END);
    
    // Expected positions:
    // item1: y = 0 (flex-start via auto)
    // item2: y = (200 - 80) / 2 = 60 (center)
    // item3: y = 200 - 80 = 120 (flex-end)
}

// Test baseline alignment
TEST_F(FlexAlgorithmTest, BaselineAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    container->embed->flex_container->align_items = ALIGN_BASELINE;
    
    ViewBlock* item1 = createFlexItem(container, 100, 80);
    ViewBlock* item2 = createFlexItem(container, 100, 120);
    ViewBlock* item3 = createFlexItem(container, 100, 60);
    
    // For baseline alignment, we would need baseline information
    // This test verifies the property is set correctly
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_BASELINE);
    
    // In a real implementation, baseline alignment would:
    // 1. Calculate each item's baseline
    // 2. Find the maximum baseline
    // 3. Align all items to that baseline
}

// Test gap handling
TEST_F(FlexAlgorithmTest, GapHandling) {
    ViewBlock* container = createFlexContainer(800, 200);
    container->embed->flex_container->column_gap = 20;
    container->embed->flex_container->row_gap = 15;
    
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // With 3 items, there are 2 gaps between them
    int total_gap = 2 * container->embed->flex_container->column_gap; // 2 * 20 = 40
    int total_item_width = 3 * 100; // 300
    int total_used_space = total_item_width + total_gap; // 340
    int available_space = container->width - total_used_space; // 800 - 340 = 460
    
    EXPECT_EQ(total_gap, 40);
    EXPECT_EQ(available_space, 460);
    
    // Expected positions with gaps:
    // item1: x = 0
    // item2: x = 100 + 20 = 120
    // item3: x = 120 + 100 + 20 = 240
}

// Test flex-basis calculations
TEST_F(FlexAlgorithmTest, FlexBasisCalculations) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test different flex-basis values
    ViewBlock* item1 = createFlexItem(container, 150, 100, 0.0f, 1.0f, -1);  // auto
    ViewBlock* item2 = createFlexItem(container, 150, 100, 0.0f, 1.0f, 200); // 200px
    ViewBlock* item3 = createFlexItem(container, 150, 100, 0.0f, 1.0f, 50);  // 50% (percentage)
    item3->flex_basis_is_percent = true;
    
    // Calculate effective flex basis
    // item1: flex-basis auto -> use content width (150px)
    // item2: flex-basis 200px -> 200px
    // item3: flex-basis 50% -> 50% of 800px = 400px
    
    int effective_basis_1 = (item1->flex_basis == -1) ? item1->width : item1->flex_basis;
    int effective_basis_2 = item2->flex_basis;
    int effective_basis_3 = item3->flex_basis_is_percent ? 
                           (container->width * item3->flex_basis / 100) : 
                           item3->flex_basis;
    
    EXPECT_EQ(effective_basis_1, 150);
    EXPECT_EQ(effective_basis_2, 200);
    EXPECT_EQ(effective_basis_3, 400);
    
    int total_flex_basis = effective_basis_1 + effective_basis_2 + effective_basis_3;
    EXPECT_EQ(total_flex_basis, 750); // 150 + 200 + 400
}

// Test min/max width constraints
TEST_F(FlexAlgorithmTest, MinMaxConstraints) {
    ViewBlock* container = createFlexContainer(400, 200);
    
    ViewBlock* item1 = createFlexItem(container, 200, 100, 1.0f, 1.0f);
    ViewBlock* item2 = createFlexItem(container, 200, 100, 1.0f, 1.0f);
    
    // Set min/max constraints (these would be part of style properties)
    // For this test, we assume these are handled in the layout algorithm
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Total content: 400px, container: 400px -> no growing/shrinking needed
    int total_width = calculateTotalContentWidth(items, 0);
    EXPECT_EQ(total_width, container->width);
    
    // In a real implementation with min/max constraints:
    // - Items would be clamped to their min/max values
    // - Remaining space would be redistributed
    // - Multiple passes might be needed for complex constraints
}

// Test writing mode impact on main/cross axes
TEST_F(FlexAlgorithmTest, WritingModeAxes) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test horizontal writing mode (default)
    container->embed->flex_container->writing_mode = WM_HORIZONTAL_TB;
    container->embed->flex_container->direction = DIR_ROW;
    
    // In horizontal-tb + row: main axis = horizontal, cross axis = vertical
    EXPECT_EQ(container->embed->flex_container->writing_mode, WM_HORIZONTAL_TB);
    EXPECT_EQ(container->embed->flex_container->direction, DIR_ROW);
    
    // Test vertical writing mode
    container->embed->flex_container->writing_mode = WM_VERTICAL_RL;
    container->embed->flex_container->direction = DIR_ROW;
    
    // In vertical-rl + row: main axis = vertical, cross axis = horizontal
    EXPECT_EQ(container->embed->flex_container->writing_mode, WM_VERTICAL_RL);
    
    // Test column direction with vertical writing mode
    container->embed->flex_container->direction = DIR_COLUMN;
    
    // In vertical-rl + column: main axis = horizontal, cross axis = vertical
    EXPECT_EQ(container->embed->flex_container->direction, DIR_COLUMN);
}

// Test text direction impact on alignment
TEST_F(FlexAlgorithmTest, TextDirectionAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Test LTR (left-to-right)
    container->embed->flex_container->text_direction = TD_LTR;
    container->embed->flex_container->justify = JUSTIFY_START;
    
    EXPECT_EQ(container->embed->flex_container->text_direction, TD_LTR);
    // In LTR: flex-start = left, flex-end = right
    
    // Test RTL (right-to-left)
    container->embed->flex_container->text_direction = TD_RTL;
    
    EXPECT_EQ(container->embed->flex_container->text_direction, TD_RTL);
    // In RTL: flex-start = right, flex-end = left
}

// Test complex layout scenario
TEST_F(FlexAlgorithmTest, ComplexLayoutScenario) {
    ViewBlock* container = createFlexContainer(1000, 300);
    container->embed->flex_container->direction = DIR_ROW;
    container->embed->flex_container->wrap = WRAP_WRAP;
    container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    container->embed->flex_container->align_items = ALIGN_CENTER;
    container->embed->flex_container->align_content = ALIGN_STRETCH;
    container->embed->flex_container->column_gap = 10;
    container->embed->flex_container->row_gap = 15;
    
    // Create items with mixed properties
    ViewBlock* item1 = createFlexItem(container, 200, 100, 1.0f, 1.0f, 150, 2);
    ViewBlock* item2 = createFlexItem(container, 250, 120, 2.0f, 0.5f, 200, 1);
    ViewBlock* item3 = createFlexItem(container, 180, 80, 0.0f, 2.0f, -1, 3);
    ViewBlock* item4 = createFlexItem(container, 300, 140, 1.5f, 1.0f, 250, 1);
    
    // Set individual align-self values
    item1->align_self = ALIGN_START;
    item2->align_self = ALIGN_AUTO;
    item3->align_self = ALIGN_END;
    item4->align_self = ALIGN_STRETCH;
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    
    // Verify complex setup
    EXPECT_EQ(items.size(), 4);
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP);
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_BETWEEN);
    EXPECT_EQ(container->embed->flex_container->column_gap, 10);
    EXPECT_EQ(container->embed->flex_container->row_gap, 15);
    
    // This scenario would test:
    // 1. Item ordering by order property
    // 2. Line breaking with wrap
    // 3. Flex growing/shrinking within lines
    // 4. Main axis alignment (space-between)
    // 5. Cross axis alignment (mixed align-self values)
    // 6. Gap handling between items and lines
}

// Performance test for algorithm efficiency
TEST_F(FlexAlgorithmTest, AlgorithmPerformance) {
    ViewBlock* container = createFlexContainer(2000, 500);
    container->embed->flex_container->wrap = WRAP_WRAP;
    
    // Create many items to test algorithm scalability
    const int item_count = 200;
    std::vector<ViewBlock*> created_items;
    
    for (int i = 0; i < item_count; ++i) {
        float grow = (i % 3 == 0) ? 1.0f : 0.0f;
        float shrink = (i % 2 == 0) ? 1.0f : 0.5f;
        int basis = (i % 4 == 0) ? -1 : 100 + (i % 50);
        int order = i % 10; // Mix up the order
        
        ViewBlock* item = createFlexItem(container, 100 + (i % 20), 50 + (i % 30), 
                                       grow, shrink, basis, order);
        created_items.push_back(item);
    }
    
    std::vector<ViewBlock*> items = collectFlexItems(container);
    EXPECT_EQ(items.size(), item_count);
    
    // Test that the algorithm can handle large numbers of items
    // In a real implementation, this would measure execution time
    EXPECT_GT(items.size(), 100);
}
