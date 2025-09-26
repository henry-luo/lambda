#include <gtest/gtest.h>
#include <memory>
#include <vector>

// Include the radiant layout headers
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include "../radiant/flex.hpp"
#include "../radiant/flex_layout_new.hpp"

// Forward declare helper functions from new implementation
// These functions are already declared in flex_layout_new.hpp

// Test fixture for new flex layout features
class FlexNewFeaturesTest : public ::testing::Test {
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
    
    // Helper to create a flex container with new layout
    ViewBlock* createFlexContainer(int width = 800, int height = 600) {
        ViewBlock* container = alloc_view_block(lycon);
        container->width = width;
        container->height = height;
        
        // Initialize flex container using new implementation
        init_flex_container(container);
        
        return container;
    }
    
    // Helper to create a flex item with all new properties
    ViewBlock* createAdvancedFlexItem(ViewBlock* parent, int width, int height) {
        ViewBlock* item = alloc_view_block(lycon);
        item->width = width;
        item->height = height;
        item->parent = parent;
        
        // Initialize position and visibility for proper filtering
        item->position = POS_STATIC;  // Default position
        item->visibility = VIS_VISIBLE;  // Default visibility
        
        // Add to parent's children using ViewBlock hierarchy
        if (!parent->first_child) {
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

// Test the new init_flex_container function
TEST_F(FlexNewFeaturesTest, InitFlexContainer) {
    ViewBlock* container = alloc_view_block(lycon);
    container->width = 800;
    container->height = 400;
    
    // Initialize flex container
    init_flex_container(container);
    
    ASSERT_NE(container->embed, nullptr);
    ASSERT_NE(container->embed->flex_container, nullptr);
    
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Check default values are set correctly
    EXPECT_EQ(flex->direction, LXB_CSS_VALUE_ROW);
    EXPECT_EQ(flex->wrap, LXB_CSS_VALUE_NOWRAP);
    EXPECT_EQ(flex->justify, LXB_CSS_VALUE_FLEX_START);
    EXPECT_EQ(flex->align_items, LXB_CSS_VALUE_FLEX_START);
    EXPECT_EQ(flex->align_content, LXB_CSS_VALUE_FLEX_START);
    EXPECT_EQ(flex->row_gap, 0);
    EXPECT_EQ(flex->column_gap, 0);
    EXPECT_FALSE(flex->needs_reflow);
}

// Test collect_flex_items with filtering
TEST_F(FlexNewFeaturesTest, CollectFlexItemsWithFiltering) {
    ViewBlock* container = createFlexContainer(800, 400);
    
    // Create items with different position and visibility
    ViewBlock* visible_item = createAdvancedFlexItem(container, 100, 100);
    visible_item->position = POS_STATIC;
    visible_item->visibility = VIS_VISIBLE;
    
    ViewBlock* absolute_item = createAdvancedFlexItem(container, 100, 100);
    absolute_item->position = POS_ABSOLUTE; // Should be filtered out
    absolute_item->visibility = VIS_VISIBLE;
    
    ViewBlock* hidden_item = createAdvancedFlexItem(container, 100, 100);
    hidden_item->position = POS_STATIC;
    hidden_item->visibility = VIS_HIDDEN; // Should be filtered out
    
    ViewBlock* another_visible = createAdvancedFlexItem(container, 100, 100);
    another_visible->position = POS_STATIC;
    another_visible->visibility = VIS_VISIBLE;
    
    // Collect flex items
    ViewBlock** items = nullptr;
    int count = collect_flex_items(container, &items);
    
    // Should only collect visible, non-absolute items
    EXPECT_EQ(count, 2); // Only visible_item and another_visible
    ASSERT_NE(items, nullptr);
    
    // Verify the collected items are the correct ones
    bool found_visible = false;
    bool found_another = false;
    for (int i = 0; i < count; i++) {
        if (items[i] == visible_item) found_visible = true;
        if (items[i] == another_visible) found_another = true;
    }
    
    EXPECT_TRUE(found_visible);
    EXPECT_TRUE(found_another);
}

// Test apply_constraints function
TEST_F(FlexNewFeaturesTest, ApplyConstraints) {
    ViewBlock* container = createFlexContainer(800, 400);
    ViewBlock* item = createAdvancedFlexItem(container, 100, 100);
    
    // Set up constraints
    item->min_width = 80;
    item->max_width = 200;
    item->min_height = 60;
    item->max_height = 150;
    item->aspect_ratio = 1.5f; // 3:2 ratio
    
    // Test normal case (within constraints)
    apply_constraints(item, 800, 400);
    EXPECT_GE(item->width, item->min_width);
    EXPECT_LE(item->width, item->max_width);
    EXPECT_GE(item->height, item->min_height);
    EXPECT_LE(item->height, item->max_height);
    
    // Test with percentage values
    item->width = 25; // 25%
    item->height = 50; // 50%
    item->width_is_percent = true;
    item->height_is_percent = true;
    
    apply_constraints(item, 800, 400);
    // 25% of 800 = 200, 50% of 400 = 200
    // But constrained by max_width = 200, max_height = 150
    EXPECT_EQ(item->width, 200);
    EXPECT_EQ(item->height, 150);
}

// Test aspect ratio handling
TEST_F(FlexNewFeaturesTest, AspectRatioHandling) {
    ViewBlock* container = createFlexContainer(800, 400);
    ViewBlock* item = createAdvancedFlexItem(container, 100, 0);
    
    // Set aspect ratio
    item->aspect_ratio = 2.0f; // 2:1 ratio (wide)
    
    apply_constraints(item, 800, 400);
    
    // Height should be calculated from width and aspect ratio
    // height = width / aspect_ratio = 100 / 2.0 = 50
    EXPECT_EQ(item->height, 50);
    
    // Test the other direction
    ViewBlock* item2 = createAdvancedFlexItem(container, 0, 100);
    item2->aspect_ratio = 0.5f; // 1:2 ratio (tall)
    
    apply_constraints(item2, 800, 400);
    
    // Width should be calculated from height and aspect ratio
    // width = height * aspect_ratio = 100 * 0.5 = 50
    EXPECT_EQ(item2->width, 50);
}

// Test clamp_value helper function
TEST_F(FlexNewFeaturesTest, ClampValueFunction) {
    // Test normal clamping
    EXPECT_FLOAT_EQ(clamp_value(50.0f, 0.0f, 100.0f), 50.0f);
    EXPECT_FLOAT_EQ(clamp_value(-10.0f, 0.0f, 100.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp_value(150.0f, 0.0f, 100.0f), 100.0f);
    
    // Test with no maximum (max_val = 0)
    EXPECT_FLOAT_EQ(clamp_value(150.0f, 50.0f, 0.0f), 150.0f);
    EXPECT_FLOAT_EQ(clamp_value(25.0f, 50.0f, 0.0f), 50.0f);
    
    // Test edge cases
    EXPECT_FLOAT_EQ(clamp_value(0.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp_value(100.0f, 100.0f, 100.0f), 100.0f);
}

// Test resolve_percentage helper function
TEST_F(FlexNewFeaturesTest, ResolvePercentageFunction) {
    // Test percentage resolution
    EXPECT_EQ(resolve_percentage(50, true, 800), 400); // 50% of 800
    EXPECT_EQ(resolve_percentage(25, true, 400), 100); // 25% of 400
    EXPECT_EQ(resolve_percentage(100, true, 300), 300); // 100% of 300
    EXPECT_EQ(resolve_percentage(0, true, 1000), 0); // 0% of anything
    
    // Test non-percentage values (should return as-is)
    EXPECT_EQ(resolve_percentage(200, false, 800), 200);
    EXPECT_EQ(resolve_percentage(0, false, 500), 0);
    EXPECT_EQ(resolve_percentage(1000, false, 100), 1000);
    
    // Test edge cases
    EXPECT_EQ(resolve_percentage(150, true, 0), 0); // 150% of 0
    EXPECT_EQ(resolve_percentage(50, true, 1), 0); // 50% of 1 (rounds down)
}

// Test find_max_baseline function
TEST_F(FlexNewFeaturesTest, FindMaxBaselineFunction) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items for a flex line
    ViewBlock* item1 = createAdvancedFlexItem(container, 100, 80);
    ViewBlock* item2 = createAdvancedFlexItem(container, 100, 120);
    ViewBlock* item3 = createAdvancedFlexItem(container, 100, 100);
    
    // Set baseline offsets and align-self
    item1->baseline_offset = 60;
    item1->align_self = LXB_CSS_VALUE_BASELINE;
    
    item2->baseline_offset = 90;
    item2->align_self = LXB_CSS_VALUE_BASELINE;
    
    item3->baseline_offset = 0; // Should use default (3/4 of height = 75)
    item3->align_self = LXB_CSS_VALUE_BASELINE;
    
    // Create a flex line info structure
    FlexLineInfo line;
    ViewBlock* line_items[] = {item1, item2, item3};
    line.items = line_items;
    line.item_count = 3;
    
    // Find max baseline
    int max_baseline = find_max_baseline(&line);
    
    // Should be 90 (from item2)
    EXPECT_EQ(max_baseline, 90);
    
    // Test with no baseline items
    item1->align_self = LXB_CSS_VALUE_FLEX_START;
    item2->align_self = LXB_CSS_VALUE_CENTER;
    item3->align_self = LXB_CSS_VALUE_FLEX_END;
    
    max_baseline = find_max_baseline(&line);
    EXPECT_EQ(max_baseline, 0); // No baseline items
}

// Test is_valid_flex_item function
TEST_F(FlexNewFeaturesTest, IsValidFlexItemFunction) {
    ViewBlock* container = createFlexContainer(800, 200);
    ViewBlock* block_item = createAdvancedFlexItem(container, 100, 100);
    block_item->type = RDT_VIEW_BLOCK;
    
    ViewBlock* inline_block_item = createAdvancedFlexItem(container, 100, 100);
    inline_block_item->type = RDT_VIEW_INLINE_BLOCK;
    
    ViewBlock* text_item = createAdvancedFlexItem(container, 100, 100);
    text_item->type = RDT_VIEW_TEXT;
    
    // Test valid items
    EXPECT_TRUE(is_valid_flex_item(block_item));
    EXPECT_TRUE(is_valid_flex_item(inline_block_item));
    
    // Test invalid items
    EXPECT_FALSE(is_valid_flex_item(text_item));
    EXPECT_FALSE(is_valid_flex_item(nullptr));
}

// Test sort_flex_items_by_order function
TEST_F(FlexNewFeaturesTest, SortFlexItemsByOrder) {
    ViewBlock* container = createFlexContainer(800, 200);
    
    // Create items with different order values
    ViewBlock* item1 = createAdvancedFlexItem(container, 100, 100);
    ViewBlock* item2 = createAdvancedFlexItem(container, 100, 100);
    ViewBlock* item3 = createAdvancedFlexItem(container, 100, 100);
    ViewBlock* item4 = createAdvancedFlexItem(container, 100, 100);
    
    item1->order = 3;
    item2->order = 1;
    item3->order = 4;
    item4->order = 2;
    
    // Create array of items in DOM order
    ViewBlock* items[] = {item1, item2, item3, item4};
    int count = 4;
    
    // Sort by order
    sort_flex_items_by_order(items, count);
    
    // Check that items are sorted by order value
    EXPECT_EQ(items[0]->order, 1); // item2
    EXPECT_EQ(items[1]->order, 2); // item4
    EXPECT_EQ(items[2]->order, 3); // item1
    EXPECT_EQ(items[3]->order, 4); // item3
    
    // Verify the actual items
    EXPECT_EQ(items[0], item2);
    EXPECT_EQ(items[1], item4);
    EXPECT_EQ(items[2], item1);
    EXPECT_EQ(items[3], item3);
}

// Test integration with percentage-based constraints
TEST_F(FlexNewFeaturesTest, PercentageConstraintsIntegration) {
    ViewBlock* container = createFlexContainer(800, 400);
    ViewBlock* item = createAdvancedFlexItem(container, 100, 100);
    
    // Set percentage-based constraints
    item->min_width = 10; // 10%
    item->max_width = 50; // 50%
    item->min_height = 15; // 15%
    item->max_height = 75; // 75%
    
    item->min_width_is_percent = true;
    item->max_width_is_percent = true;
    item->min_height_is_percent = true;
    item->max_height_is_percent = true;
    
    // Apply constraints
    apply_constraints(item, 800, 400);
    
    // Check that constraints are resolved correctly
    // min_width: 10% of 800 = 80
    // max_width: 50% of 800 = 400
    // min_height: 15% of 400 = 60
    // max_height: 75% of 400 = 300
    
    EXPECT_GE(item->width, 80);   // >= min_width
    EXPECT_LE(item->width, 400);  // <= max_width
    EXPECT_GE(item->height, 60);  // >= min_height
    EXPECT_LE(item->height, 300); // <= max_height
}

// Test complex scenario with all new features
TEST_F(FlexNewFeaturesTest, ComplexIntegrationTest) {
    ViewBlock* container = createFlexContainer(1000, 500);
    
    // Set up container properties
    FlexContainerLayout* flex = container->embed->flex_container;
    flex->direction = LXB_CSS_VALUE_ROW;
    flex->wrap = LXB_CSS_VALUE_WRAP;
    flex->justify = LXB_CSS_VALUE_SPACE_BETWEEN;
    flex->align_items = LXB_CSS_VALUE_BASELINE;
    flex->row_gap = 20;
    flex->column_gap = 15;
    
    // Create complex items
    ViewBlock* item1 = createAdvancedFlexItem(container, 30, 0); // 30% width, auto height
    item1->width_is_percent = true;
    item1->aspect_ratio = 1.6f; // Golden ratio-ish
    item1->margin_left_auto = true;
    item1->baseline_offset = 100;
    item1->flex_grow = 1.0f;
    
    ViewBlock* item2 = createAdvancedFlexItem(container, 200, 150);
    item2->min_width = 15; // 15%
    item2->max_width = 40; // 40%
    item2->min_width_is_percent = true;
    item2->max_width_is_percent = true;
    item2->margin_top_auto = true;
    item2->margin_bottom_auto = true;
    item2->flex_shrink = 2.0f;
    
    ViewBlock* item3 = createAdvancedFlexItem(container, 180, 120);
    item3->aspect_ratio = 1.5f;
    item3->position = POS_STATIC; // Should be included
    item3->visibility = VIS_VISIBLE;
    item3->order = -1; // Should appear first after sorting
    
    ViewBlock* hidden_item = createAdvancedFlexItem(container, 100, 100);
    hidden_item->visibility = VIS_HIDDEN; // Should be filtered out
    
    // Test item collection (should filter out hidden item)
    ViewBlock** collected_items = nullptr;
    int collected_count = collect_flex_items(container, &collected_items);
    
    EXPECT_EQ(collected_count, 3); // Only visible items
    
    // Test constraints application
    apply_constraints(item1, 1000, 500);
    apply_constraints(item2, 1000, 500);
    apply_constraints(item3, 1000, 500);
    
    // Verify item1 calculations
    EXPECT_EQ(item1->width, 300); // 30% of 1000
    EXPECT_EQ(item1->height, 187); // 300 / 1.6 ≈ 187
    
    // Verify item2 constraints
    EXPECT_GE(item2->width, 150); // >= 15% of 1000
    EXPECT_LE(item2->width, 400); // <= 40% of 1000
    
    // Test sorting by order
    if (collected_items) {
        sort_flex_items_by_order(collected_items, collected_count);
        EXPECT_EQ(collected_items[0], item3); // order: -1 should be first
    }
    
    // Verify all properties are preserved
    EXPECT_TRUE(item1->width_is_percent);
    EXPECT_FLOAT_EQ(item1->aspect_ratio, 1.6f);
    EXPECT_TRUE(item1->margin_left_auto);
    EXPECT_EQ(item1->baseline_offset, 100);
    
    EXPECT_TRUE(item2->min_width_is_percent);
    EXPECT_TRUE(item2->max_width_is_percent);
    EXPECT_TRUE(item2->margin_top_auto);
    EXPECT_TRUE(item2->margin_bottom_auto);
    
    EXPECT_FLOAT_EQ(item3->aspect_ratio, 1.5f);
    EXPECT_EQ(item3->order, -1);
}

// Test cleanup function
TEST_F(FlexNewFeaturesTest, CleanupFlexContainer) {
    ViewBlock* container = createFlexContainer(800, 400);
    
    // Verify container is initialized
    ASSERT_NE(container->embed, nullptr);
    ASSERT_NE(container->embed->flex_container, nullptr);
    
    // Add some items to allocate arrays
    ViewBlock* item1 = createAdvancedFlexItem(container, 100, 100);
    ViewBlock* item2 = createAdvancedFlexItem(container, 100, 100);
    
    ViewBlock** items = nullptr;
    int count = collect_flex_items(container, &items);
    EXPECT_GT(count, 0);
    
    // Cleanup should free all allocated memory
    cleanup_flex_container(container);
    
    // After cleanup, flex_container should be null
    if (container->embed) {
        EXPECT_EQ(container->embed->flex_container, nullptr);
    }
}
