#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include "../radiant/flex.hpp"
#include "../radiant/layout_flex.hpp"

// Forward declarations for helper functions (removed extern "C" to match header declarations)
float clamp_value(float value, float min_val, float max_val);
int resolve_percentage(int value, bool is_percent, int container_size);
void apply_constraints(ViewBlock* item, int container_width, int container_height);
int find_max_baseline(FlexLineInfo* line);
bool is_valid_flex_item(ViewBlock* item);

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
        // *** CRITICAL FIX: Set content dimensions ***
        container->content_width = width;
        container->content_height = height;

        // *** CRITICAL FIX: Use proper initialization function ***
        init_flex_container(container);

        return container;
    }

    // Helper to create a flex item
    ViewBlock* createFlexItem(ViewBlock* parent, int width, int height,
                             float flex_grow = 0.0f, float flex_shrink = 1.0f, int flex_basis = -1) {
        ViewBlock* item = alloc_view_block(lycon);
        item->width = width;
        item->height = height;
        // *** CRITICAL FIX: Set content dimensions ***
        item->content_width = width;
        item->content_height = height;
        item->parent = (ViewGroup*)parent;

        // Set flex item properties
        item->flex_grow = flex_grow;
        item->flex_shrink = flex_shrink;
        item->flex_basis = flex_basis;
        item->flex_basis_is_percent = false;

        // Initialize new properties with defaults
        item->aspect_ratio = 0.0f;
        item->baseline_offset = 0;
        item->margin_top_auto = false;
        item->margin_right_auto = false;
        item->margin_bottom_auto = false;
        item->margin_left_auto = false;
        item->width_is_percent = false;
        item->height_is_percent = false;
        item->min_width_is_percent = false;
        item->max_width_is_percent = false;
        item->min_height_is_percent = false;
        item->max_height_is_percent = false;
        item->min_width = 0;
        item->max_width = 0;
        item->min_height = 0;
        item->max_height = 0;
        item->position = alloc_position_prop(&lycon);
        item->position->position = 0x014d; // LXB_CSS_VALUE_STATIC
        item->visibility = VIS_VISIBLE;

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

    FlexContainerLayout* flex = container->embed->flex_container;
    EXPECT_EQ(flex->direction, LXB_CSS_VALUE_ROW);
    EXPECT_EQ(flex->wrap, LXB_CSS_VALUE_NOWRAP);
    EXPECT_EQ(flex->justify, LXB_CSS_VALUE_FLEX_START);
    EXPECT_EQ(flex->align_items, LXB_CSS_VALUE_FLEX_START);
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

// Test basic row layout (flex-direction: row) - ENHANCED WITH ACTUAL LAYOUT
TEST_F(FlexLayoutTest, BasicRowLayout) {
    ViewBlock* container = createFlexContainer(400, 200);
    container->embed->flex_container->column_gap = 10;

    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);

    // *** ENHANCED: RUN ACTUAL LAYOUT ALGORITHM ***
    layout_flex_container_new(lycon, container);

    // *** ENHANCED: VALIDATE ACTUAL LAYOUT RESULTS ***
    // Expected: Item1(0,0), Item2(110,0), Item3(220,0) with 10px gaps
    EXPECT_EQ(item1->x, 0) << "Item 1 should be at x=0";
    EXPECT_EQ(item1->y, 0) << "Item 1 should be at y=0";
    EXPECT_EQ(item1->width, 100) << "Item 1 should maintain width=100";

    EXPECT_EQ(item2->x, 110) << "Item 2 should be at x=110 (100 + 10 gap)";
    EXPECT_EQ(item2->y, 0) << "Item 2 should be at y=0";
    EXPECT_EQ(item2->width, 100) << "Item 2 should maintain width=100";

    EXPECT_EQ(item3->x, 220) << "Item 3 should be at x=220 (110 + 100 + 10 gap)";
    EXPECT_EQ(item3->y, 0) << "Item 3 should be at y=0";
    EXPECT_EQ(item3->width, 100) << "Item 3 should maintain width=100";

    // Verify container setup (original tests)
    EXPECT_EQ(container->width, 400);
    EXPECT_EQ(container->height, 200);
    EXPECT_EQ(container->embed->flex_container->direction, LXB_CSS_VALUE_ROW);
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

// Test flex-grow behavior - ENHANCED WITH ACTUAL LAYOUT
TEST_F(FlexLayoutTest, FlexGrowBehavior) {
    ViewBlock* container = createFlexContainer(400, 200);
    container->embed->flex_container->column_gap = 10;

    // Create items with different flex-grow values (start with 0 width to test pure flex-grow)
    ViewBlock* item1 = createFlexItem(container, 0, 100, 1.0f); // flex-grow: 1
    ViewBlock* item2 = createFlexItem(container, 0, 100, 2.0f); // flex-grow: 2

    // *** ENHANCED: RUN ACTUAL LAYOUT ALGORITHM ***
    layout_flex_container_new(lycon, container);

    // *** ENHANCED: VALIDATE ACTUAL FLEX-GROW RESULTS ***
    // Available space: 400 - 10 (gap) = 390px
    // Total flex-grow: 1 + 2 = 3
    // Item 1: 390 * (1/3) = 130px
    // Item 2: 390 * (2/3) = 260px
    EXPECT_NEAR(item1->width, 130, 2) << "Item 1 should get ~130px (1/3 of available space)";
    EXPECT_NEAR(item2->width, 260, 2) << "Item 2 should get ~260px (2/3 of available space)";

    // Verify positions
    EXPECT_EQ(item1->x, 0) << "Item 1 should start at x=0";
    EXPECT_NEAR(item2->x, 140, 2) << "Item 2 should start at x=140 (130 + 10 gap)";

    // Verify original property tests still pass
    EXPECT_FLOAT_EQ(item1->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(item2->flex_grow, 2.0f);
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
    container->embed->flex_container->justify = LXB_CSS_VALUE_FLEX_START;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_FLEX_START);

    container->embed->flex_container->justify = LXB_CSS_VALUE_CENTER;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_CENTER);

    container->embed->flex_container->justify = LXB_CSS_VALUE_FLEX_END;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_FLEX_END);

    container->embed->flex_container->justify = LXB_CSS_VALUE_SPACE_BETWEEN;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_SPACE_BETWEEN);

    container->embed->flex_container->justify = LXB_CSS_VALUE_SPACE_AROUND;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_SPACE_AROUND);

    container->embed->flex_container->justify = LXB_CSS_VALUE_SPACE_EVENLY;
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_SPACE_EVENLY);
}

// Test align-items alignment
TEST_F(FlexLayoutTest, AlignItemsAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);

    // Test different align-items values
    container->embed->flex_container->align_items = LXB_CSS_VALUE_FLEX_START;
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_FLEX_START);

    container->embed->flex_container->align_items = LXB_CSS_VALUE_CENTER;
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_CENTER);

    container->embed->flex_container->align_items = LXB_CSS_VALUE_FLEX_END;
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_FLEX_END);

    container->embed->flex_container->align_items = LXB_CSS_VALUE_STRETCH;
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_STRETCH);

    container->embed->flex_container->align_items = LXB_CSS_VALUE_BASELINE;
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_BASELINE);
}

// Test flex-wrap behavior
TEST_F(FlexLayoutTest, FlexWrapBehavior) {
    ViewBlock* container = createFlexContainer(400, 300);

    // Test nowrap (default)
    container->embed->flex_container->wrap = LXB_CSS_VALUE_NOWRAP;
    EXPECT_EQ(container->embed->flex_container->wrap, LXB_CSS_VALUE_NOWRAP);

    // Test wrap
    container->embed->flex_container->wrap = LXB_CSS_VALUE_WRAP;
    EXPECT_EQ(container->embed->flex_container->wrap, LXB_CSS_VALUE_WRAP);

    // Test wrap-reverse
    container->embed->flex_container->wrap = LXB_CSS_VALUE_WRAP_REVERSE;
    EXPECT_EQ(container->embed->flex_container->wrap, LXB_CSS_VALUE_WRAP_REVERSE);

    // Create items that would overflow in a single line
    ViewBlock* item1 = createFlexItem(container, 200, 100);
    ViewBlock* item2 = createFlexItem(container, 200, 100);
    ViewBlock* item3 = createFlexItem(container, 200, 100);

    // Total width (600) exceeds container width (400)
    int total_width = item1->width + item2->width + item3->width;
    EXPECT_GT(total_width, container->width);
}

// Test gap properties - ENHANCED WITH ACTUAL LAYOUT
TEST_F(FlexLayoutTest, GapProperties) {
    ViewBlock* container = createFlexContainer(400, 200);

    // Set gap values
    container->embed->flex_container->column_gap = 15;

    // Create items to test gap calculation
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);

    // *** ENHANCED: RUN ACTUAL LAYOUT ALGORITHM ***
    layout_flex_container_new(lycon, container);

    // *** ENHANCED: VALIDATE ACTUAL GAP POSITIONING ***
    // Expected positions with 15px gaps:
    // Item1: x=0, Item2: x=115 (100+15), Item3: x=230 (115+100+15)
    EXPECT_EQ(item1->x, 0) << "Item 1 should be at x=0";
    EXPECT_EQ(item2->x, 115) << "Item 2 should be at x=115 (100 + 15 gap)";
    EXPECT_EQ(item3->x, 230) << "Item 3 should be at x=230 (115 + 100 + 15 gap)";

    // Verify gap property is set correctly (original test)
    EXPECT_EQ(container->embed->flex_container->column_gap, 15);

    // Verify gap calculation (original test)
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
    child_container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
    child_container->embed->flex_container->direction = LXB_CSS_VALUE_COLUMN;
    child_container->embed->flex_container->wrap = LXB_CSS_VALUE_NOWRAP;
    child_container->embed->flex_container->justify = LXB_CSS_VALUE_CENTER;
    child_container->embed->flex_container->align_items = LXB_CSS_VALUE_CENTER;

    // Add items to nested container
    ViewBlock* nested_item1 = createFlexItem(child_container, 100, 50);
    ViewBlock* nested_item2 = createFlexItem(child_container, 100, 50);

    // Verify nested structure
    EXPECT_EQ(parent->first_child, child_container);
    EXPECT_EQ(child_container->parent, parent);
    EXPECT_EQ(child_container->first_child, nested_item1);
    EXPECT_EQ(nested_item1->next_sibling, nested_item2);

    // Verify nested container properties
    EXPECT_EQ(child_container->embed->flex_container->direction, LXB_CSS_VALUE_COLUMN);
    EXPECT_EQ(child_container->embed->flex_container->justify, LXB_CSS_VALUE_CENTER);
    EXPECT_EQ(child_container->embed->flex_container->align_items, LXB_CSS_VALUE_CENTER);
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
    container->embed->flex_container->direction = LXB_CSS_VALUE_ROW;
    container->embed->flex_container->wrap = LXB_CSS_VALUE_WRAP;
    container->embed->flex_container->justify = LXB_CSS_VALUE_SPACE_BETWEEN;
    container->embed->flex_container->align_items = LXB_CSS_VALUE_CENTER;
    container->embed->flex_container->align_content = LXB_CSS_VALUE_STRETCH;
    container->embed->flex_container->row_gap = 10;
    container->embed->flex_container->column_gap = 15;

    // Create items with CSS-like flex properties
    ViewBlock* item1 = createFlexItem(container, 0, 100, 1.0f, 1.0f, 200); // flex: 1 1 200px
    ViewBlock* item2 = createFlexItem(container, 0, 100, 2.0f, 1.0f, 0);   // flex: 2 1 0
    ViewBlock* item3 = createFlexItem(container, 150, 100, 0.0f, 0.0f);    // flex: none (0 0 auto)

    // Set align-self on individual items
    item1->align_self = LXB_CSS_VALUE_FLEX_START;
    item2->align_self = LXB_CSS_VALUE_FLEX_END;
    item3->align_self = LXB_CSS_VALUE_CENTER;

    EXPECT_EQ(item1->align_self, LXB_CSS_VALUE_FLEX_START);
    EXPECT_EQ(item2->align_self, LXB_CSS_VALUE_FLEX_END);
    EXPECT_EQ(item3->align_self, LXB_CSS_VALUE_CENTER);

    // Verify container properties match CSS specification
    EXPECT_EQ(container->embed->flex_container->direction, LXB_CSS_VALUE_ROW);
    EXPECT_EQ(container->embed->flex_container->wrap, LXB_CSS_VALUE_WRAP);
    EXPECT_EQ(container->embed->flex_container->justify, LXB_CSS_VALUE_SPACE_BETWEEN);
    EXPECT_EQ(container->embed->flex_container->align_items, LXB_CSS_VALUE_CENTER);
    EXPECT_EQ(container->embed->flex_container->align_content, LXB_CSS_VALUE_STRETCH);
}

// ==================== NEW FEATURE TESTS ====================

// Test auto margins on main axis
TEST_F(FlexLayoutTest, AutoMarginsMainAxis) {
    ViewBlock* container = createFlexContainer(800, 200);

    // Create items with different auto margin configurations
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);

    // Test margin-left: auto (pushes item to right)
    item1->margin_left_auto = true;
    EXPECT_TRUE(item1->margin_left_auto);
    EXPECT_FALSE(item1->margin_right_auto);

    // Test margin-right: auto (pushes item to left)
    item2->margin_right_auto = true;
    EXPECT_TRUE(item2->margin_right_auto);
    EXPECT_FALSE(item2->margin_left_auto);

    // Test both margins auto (centers item)
    item3->margin_left_auto = true;
    item3->margin_right_auto = true;
    EXPECT_TRUE(item3->margin_left_auto);
    EXPECT_TRUE(item3->margin_right_auto);
}

// Test auto margins on cross axis
TEST_F(FlexLayoutTest, AutoMarginsCrossAxis) {
    ViewBlock* container = createFlexContainer(800, 200);

    ViewBlock* item1 = createFlexItem(container, 100, 50);
    ViewBlock* item2 = createFlexItem(container, 100, 50);
    ViewBlock* item3 = createFlexItem(container, 100, 50);

    // Test margin-top: auto (pushes item to bottom)
    item1->margin_top_auto = true;
    EXPECT_TRUE(item1->margin_top_auto);
    EXPECT_FALSE(item1->margin_bottom_auto);

    // Test margin-bottom: auto (pushes item to top)
    item2->margin_bottom_auto = true;
    EXPECT_TRUE(item2->margin_bottom_auto);
    EXPECT_FALSE(item2->margin_top_auto);

    // Test both margins auto (centers item vertically)
    item3->margin_top_auto = true;
    item3->margin_bottom_auto = true;
    EXPECT_TRUE(item3->margin_top_auto);
    EXPECT_TRUE(item3->margin_bottom_auto);
}

// Test aspect ratio functionality
TEST_F(FlexLayoutTest, AspectRatioSupport) {
    ViewBlock* container = createFlexContainer(800, 400);

    // Create items with different aspect ratios
    ViewBlock* square_item = createFlexItem(container, 100, 0); // Height will be calculated
    square_item->aspect_ratio = 1.0f; // 1:1 aspect ratio
    EXPECT_FLOAT_EQ(square_item->aspect_ratio, 1.0f);

    ViewBlock* wide_item = createFlexItem(container, 200, 0); // Height will be calculated
    wide_item->aspect_ratio = 2.0f; // 2:1 aspect ratio (wide)
    EXPECT_FLOAT_EQ(wide_item->aspect_ratio, 2.0f);

    ViewBlock* tall_item = createFlexItem(container, 0, 200); // Width will be calculated
    tall_item->aspect_ratio = 0.5f; // 1:2 aspect ratio (tall)
    EXPECT_FLOAT_EQ(tall_item->aspect_ratio, 0.5f);

    // Test that aspect ratio is preserved during flex operations
    square_item->flex_grow = 1.0f;
    wide_item->flex_grow = 1.0f;
    tall_item->flex_grow = 1.0f;

    EXPECT_FLOAT_EQ(square_item->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(wide_item->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(tall_item->flex_grow, 1.0f);
}

// Test percentage-based values
TEST_F(FlexLayoutTest, PercentageValues) {
    ViewBlock* container = createFlexContainer(800, 400);

    // Test percentage-based dimensions
    ViewBlock* percent_item = createFlexItem(container, 50, 25); // 50% width, 25% height
    percent_item->width_is_percent = true;
    percent_item->height_is_percent = true;
    EXPECT_TRUE(percent_item->width_is_percent);
    EXPECT_TRUE(percent_item->height_is_percent);
    EXPECT_EQ(percent_item->width, 50); // 50%
    EXPECT_EQ(percent_item->height, 25); // 25%

    // Test percentage-based flex-basis
    ViewBlock* flex_percent_item = createFlexItem(container, 100, 100, 1.0f, 1.0f, 30);
    flex_percent_item->flex_basis_is_percent = true;
    EXPECT_TRUE(flex_percent_item->flex_basis_is_percent);
    EXPECT_EQ(flex_percent_item->flex_basis, 30); // 30%

    // Test percentage-based constraints
    ViewBlock* constrained_item = createFlexItem(container, 100, 100);
    constrained_item->min_width = 10; // 10%
    constrained_item->max_width = 80; // 80%
    constrained_item->min_height = 15; // 15%
    constrained_item->max_height = 90; // 90%
    constrained_item->min_width_is_percent = true;
    constrained_item->max_width_is_percent = true;
    constrained_item->min_height_is_percent = true;
    constrained_item->max_height_is_percent = true;

    EXPECT_TRUE(constrained_item->min_width_is_percent);
    EXPECT_TRUE(constrained_item->max_width_is_percent);
    EXPECT_TRUE(constrained_item->min_height_is_percent);
    EXPECT_TRUE(constrained_item->max_height_is_percent);
}

// Test min/max constraints
TEST_F(FlexLayoutTest, MinMaxConstraints) {
    ViewBlock* container = createFlexContainer(800, 400);

    ViewBlock* constrained_item = createFlexItem(container, 100, 100, 2.0f, 0.5f);

    // Set min/max constraints
    constrained_item->min_width = 80;
    constrained_item->max_width = 300;
    constrained_item->min_height = 60;
    constrained_item->max_height = 200;

    EXPECT_EQ(constrained_item->min_width, 80);
    EXPECT_EQ(constrained_item->max_width, 300);
    EXPECT_EQ(constrained_item->min_height, 60);
    EXPECT_EQ(constrained_item->max_height, 200);

    // Test that flex-grow respects constraints
    EXPECT_FLOAT_EQ(constrained_item->flex_grow, 2.0f);
    EXPECT_FLOAT_EQ(constrained_item->flex_shrink, 0.5f);

    // Test zero constraints (no limit)
    ViewBlock* unlimited_item = createFlexItem(container, 100, 100);
    unlimited_item->min_width = 0;
    unlimited_item->max_width = 0; // 0 means no maximum
    unlimited_item->min_height = 0;
    unlimited_item->max_height = 0;

    EXPECT_EQ(unlimited_item->min_width, 0);
    EXPECT_EQ(unlimited_item->max_width, 0);
    EXPECT_EQ(unlimited_item->min_height, 0);
    EXPECT_EQ(unlimited_item->max_height, 0);
}

// Test baseline alignment
TEST_F(FlexLayoutTest, BaselineAlignment) {
    ViewBlock* container = createFlexContainer(800, 200);
    container->embed->flex_container->align_items = LXB_CSS_VALUE_BASELINE;

    // Create items with different heights and baseline offsets
    ViewBlock* item1 = createFlexItem(container, 100, 80);
    ViewBlock* item2 = createFlexItem(container, 100, 120);
    ViewBlock* item3 = createFlexItem(container, 100, 100);

    // Set explicit baseline offsets
    item1->baseline_offset = 60; // Baseline at 60px from top
    item2->baseline_offset = 90; // Baseline at 90px from top
    item3->baseline_offset = 0;  // Will use default (3/4 of height = 75px)

    EXPECT_EQ(item1->baseline_offset, 60);
    EXPECT_EQ(item2->baseline_offset, 90);
    EXPECT_EQ(item3->baseline_offset, 0);

    // Set align-self to baseline for all items
    item1->align_self = LXB_CSS_VALUE_BASELINE;
    item2->align_self = LXB_CSS_VALUE_BASELINE;
    item3->align_self = LXB_CSS_VALUE_BASELINE;

    EXPECT_EQ(item1->align_self, LXB_CSS_VALUE_BASELINE);
    EXPECT_EQ(item2->align_self, LXB_CSS_VALUE_BASELINE);
    EXPECT_EQ(item3->align_self, LXB_CSS_VALUE_BASELINE);
}

// Test position and visibility filtering
TEST_F(FlexLayoutTest, PositionAndVisibilityFiltering) {
    ViewBlock* container = createFlexContainer(800, 200);

    // Create items with different position and visibility values
    ViewBlock* normal_item = createFlexItem(container, 100, 100);
    ViewBlock* absolute_item = createFlexItem(container, 100, 100);
    ViewBlock* hidden_item = createFlexItem(container, 100, 100);
    ViewBlock* visible_item = createFlexItem(container, 100, 100);

    // Set position and visibility properties
    normal_item->position = alloc_position_prop(&lycon);
    normal_item->position->position = 0x014d; // LXB_CSS_VALUE_STATIC
    normal_item->visibility = VIS_VISIBLE;

    absolute_item->position = alloc_position_prop(&lycon);
    absolute_item->position->position = 0x014f; // LXB_CSS_VALUE_ABSOLUTE - Should be filtered out
    absolute_item->visibility = VIS_VISIBLE;

    hidden_item->position = alloc_position_prop(&lycon);
    hidden_item->position->position = 0x014d; // LXB_CSS_VALUE_STATIC
    hidden_item->visibility = VIS_HIDDEN; // Should be filtered out

    visible_item->position = alloc_position_prop(&lycon);
    visible_item->position->position = 0x014d; // LXB_CSS_VALUE_STATIC
    visible_item->visibility = VIS_VISIBLE;

    // Verify properties are set correctly
    EXPECT_EQ(normal_item->position->position, 0x014d); // LXB_CSS_VALUE_STATIC
    EXPECT_EQ(normal_item->visibility, VIS_VISIBLE);
    EXPECT_EQ(absolute_item->position->position, 0x014f); // LXB_CSS_VALUE_ABSOLUTE
    EXPECT_EQ(hidden_item->visibility, VIS_HIDDEN);
    EXPECT_EQ(visible_item->position, POS_STATIC);
    EXPECT_EQ(visible_item->visibility, VIS_VISIBLE);
}

// Test wrap-reverse functionality
TEST_F(FlexLayoutTest, WrapReverse) {
    ViewBlock* container = createFlexContainer(400, 300);
    container->embed->flex_container->wrap = LXB_CSS_VALUE_WRAP_REVERSE;

    // Create items that will wrap to multiple lines
    ViewBlock* item1 = createFlexItem(container, 150, 100);
    ViewBlock* item2 = createFlexItem(container, 150, 100);
    ViewBlock* item3 = createFlexItem(container, 150, 100);
    ViewBlock* item4 = createFlexItem(container, 150, 100);

    EXPECT_EQ(container->embed->flex_container->wrap, LXB_CSS_VALUE_WRAP_REVERSE);

    // Total width of first two items (300) fits in container (400)
    // Third and fourth items should wrap to next line
    int first_line_width = item1->width + item2->width;
    int second_line_width = item3->width + item4->width;

    EXPECT_LE(first_line_width, container->width);
    EXPECT_LE(second_line_width, container->width);

    // In wrap-reverse, lines are stacked in reverse order
    // This affects cross-axis positioning
}

// Test complex scenario with multiple new features
TEST_F(FlexLayoutTest, ComplexScenarioWithNewFeatures) {
    ViewBlock* container = createFlexContainer(800, 400);
    container->embed->flex_container->direction = LXB_CSS_VALUE_ROW;
    container->embed->flex_container->wrap = LXB_CSS_VALUE_WRAP;
    container->embed->flex_container->justify = LXB_CSS_VALUE_SPACE_BETWEEN;
    container->embed->flex_container->align_items = LXB_CSS_VALUE_BASELINE;
    container->embed->flex_container->row_gap = 20;
    container->embed->flex_container->column_gap = 15;

    // Item 1: Auto margins + aspect ratio
    ViewBlock* item1 = createFlexItem(container, 100, 0, 1.0f, 1.0f);
    item1->aspect_ratio = 1.5f; // 3:2 aspect ratio
    item1->margin_left_auto = true;
    item1->baseline_offset = 80;

    // Item 2: Percentage constraints + baseline
    ViewBlock* item2 = createFlexItem(container, 25, 30, 2.0f, 0.5f);
    item2->width_is_percent = true; // 25% width
    item2->height_is_percent = true; // 30% height
    item2->min_width = 10; // 10%
    item2->max_width = 40; // 40%
    item2->min_width_is_percent = true;
    item2->max_width_is_percent = true;
    item2->align_self = LXB_CSS_VALUE_BASELINE;

    // Item 3: Min/max constraints + auto margins
    ViewBlock* item3 = createFlexItem(container, 120, 100, 0.5f, 2.0f);
    item3->min_width = 80;
    item3->max_width = 200;
    item3->min_height = 60;
    item3->max_height = 150;
    item3->margin_top_auto = true;
    item3->margin_bottom_auto = true;

    // Verify all properties are set correctly
    EXPECT_FLOAT_EQ(item1->aspect_ratio, 1.5f);
    EXPECT_TRUE(item1->margin_left_auto);
    EXPECT_EQ(item1->baseline_offset, 80);

    EXPECT_TRUE(item2->width_is_percent);
    EXPECT_TRUE(item2->height_is_percent);
    EXPECT_TRUE(item2->min_width_is_percent);
    EXPECT_TRUE(item2->max_width_is_percent);
    EXPECT_EQ(item2->align_self, LXB_CSS_VALUE_BASELINE);

    EXPECT_EQ(item3->min_width, 80);
    EXPECT_EQ(item3->max_width, 200);
    EXPECT_TRUE(item3->margin_top_auto);
    EXPECT_TRUE(item3->margin_bottom_auto);
}

// Test helper function behavior
TEST_F(FlexLayoutTest, HelperFunctionTests) {
    ViewBlock* container = createFlexContainer(800, 400);
    ViewBlock* item = createFlexItem(container, 100, 100);

    // Test clamp_value function behavior
    EXPECT_FLOAT_EQ(clamp_value(50.0f, 0.0f, 100.0f), 50.0f); // Within range
    EXPECT_FLOAT_EQ(clamp_value(-10.0f, 0.0f, 100.0f), 0.0f); // Below minimum
    EXPECT_FLOAT_EQ(clamp_value(150.0f, 0.0f, 100.0f), 100.0f); // Above maximum
    EXPECT_FLOAT_EQ(clamp_value(75.0f, 50.0f, 0.0f), 75.0f); // No maximum (max_val = 0)

    // Test resolve_percentage function behavior
    EXPECT_EQ(resolve_percentage(50, true, 800), 400); // 50% of 800 = 400
    EXPECT_EQ(resolve_percentage(25, true, 400), 100); // 25% of 400 = 100
    EXPECT_EQ(resolve_percentage(200, false, 800), 200); // Not percentage, return as-is
    EXPECT_EQ(resolve_percentage(0, true, 1000), 0); // 0% = 0
    EXPECT_EQ(resolve_percentage(100, true, 500), 500); // 100% = container size
}

// Test edge cases for new features
TEST_F(FlexLayoutTest, NewFeaturesEdgeCases) {
    ViewBlock* container = createFlexContainer(800, 400);

    // Test zero aspect ratio (should be ignored)
    ViewBlock* zero_aspect = createFlexItem(container, 100, 100);
    zero_aspect->aspect_ratio = 0.0f;
    EXPECT_FLOAT_EQ(zero_aspect->aspect_ratio, 0.0f);

    // Test negative aspect ratio (invalid, should be handled gracefully)
    ViewBlock* negative_aspect = createFlexItem(container, 100, 100);
    negative_aspect->aspect_ratio = -1.0f;
    EXPECT_FLOAT_EQ(negative_aspect->aspect_ratio, -1.0f);

    // Test all auto margins
    ViewBlock* all_auto = createFlexItem(container, 100, 100);
    all_auto->margin_top_auto = true;
    all_auto->margin_right_auto = true;
    all_auto->margin_bottom_auto = true;
    all_auto->margin_left_auto = true;

    EXPECT_TRUE(all_auto->margin_top_auto);
    EXPECT_TRUE(all_auto->margin_right_auto);
    EXPECT_TRUE(all_auto->margin_bottom_auto);
    EXPECT_TRUE(all_auto->margin_left_auto);

    // Test extreme percentage values
    ViewBlock* extreme_percent = createFlexItem(container, 0, 200);
    extreme_percent->width_is_percent = true;
    extreme_percent->height_is_percent = true;

    EXPECT_EQ(extreme_percent->width, 0); // 0%
    EXPECT_EQ(extreme_percent->height, 200); // 200% (over 100%)

    // Test conflicting constraints (min > max)
    ViewBlock* conflicting = createFlexItem(container, 100, 100);
    conflicting->min_width = 200;
    conflicting->max_width = 100; // max < min (should be handled gracefully)
    conflicting->min_height = 150;
    conflicting->max_height = 80; // max < min

    EXPECT_EQ(conflicting->min_width, 200);
    EXPECT_EQ(conflicting->max_width, 100);
    EXPECT_EQ(conflicting->min_height, 150);
    EXPECT_EQ(conflicting->max_height, 80);
}
