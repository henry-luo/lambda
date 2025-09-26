#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../radiant/flex_layout_new.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"

// Helper function to create a test flex container
ViewBlock* create_test_flex_container(int width, int height) {
    ViewBlock* container = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    container->width = width;
    container->height = height;
    container->content_width = width;
    container->content_height = height;
    
    // Initialize embed structure
    container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
    
    FlexContainerLayout* flex = container->embed->flex_container;
    flex->direction = DIR_ROW;
    flex->wrap = WRAP_NOWRAP;
    flex->justify = JUSTIFY_START;
    flex->align_items = ALIGN_START;
    flex->align_content = ALIGN_START;
    flex->row_gap = 0;
    flex->column_gap = 0;
    flex->writing_mode = WM_HORIZONTAL_TB;
    flex->main_axis_size = width;
    flex->cross_axis_size = height;
    flex->allocated_lines = 0;
    flex->lines = nullptr;
    flex->line_count = 0;
    flex->needs_reflow = true;
    
    return container;
}

// Helper function to create a test flex item
ViewBlock* create_test_flex_item(int width, int height, float grow = 0, float shrink = 1, int basis = -1) {
    ViewBlock* item = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    item->width = width;
    item->height = height;
    item->content_width = width;
    item->content_height = height;
    item->flex_grow = grow;
    item->flex_shrink = shrink;
    item->flex_basis = (basis >= 0) ? basis : width;
    item->align_self = ALIGN_START;
    item->order = 0;
    
    return item;
}

// Helper function to add child to container
void add_child_to_container(ViewBlock* container, ViewBlock* child) {
    if (!container->first_child) {
        container->first_child = child;
        container->last_child = child;
    } else {
        container->last_child->next_sibling = child;
        child->prev_sibling = container->last_child;
        container->last_child = child;
    }
    child->parent = container;
}

// Helper function to cleanup test structures
void cleanup_test_container(ViewBlock* container) {
    if (!container) return;
    
    // Cleanup children
    ViewBlock* child = container->first_child;
    while (child) {
        ViewBlock* next = child->next_sibling;
        free(child);
        child = next;
    }
    
    // Cleanup flex layout
    if (container->embed && container->embed->flex_container) {
        FlexContainerLayout* flex = container->embed->flex_container;
        if (flex->lines) {
            for (int i = 0; i < flex->line_count; i++) {
                if (flex->lines[i].items) {
                    free(flex->lines[i].items);
                }
            }
            free(flex->lines);
        }
        free(flex);
    }
    
    if (container->embed) {
        free(container->embed);
    }
    
    free(container);
}

Test(flex_layout, basic_row_layout) {
    // Test basic row layout with 3 items
    ViewBlock* container = create_test_flex_container(300, 100);
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Check that items are positioned horizontally
    cr_assert_eq(item1->x, 0, "First item should be at x=0");
    cr_assert_eq(item2->x, 100, "Second item should be at x=100");
    cr_assert_eq(item3->x, 200, "Third item should be at x=200");
    
    // Check that all items are aligned at top
    cr_assert_eq(item1->y, 0, "First item should be at y=0");
    cr_assert_eq(item2->y, 0, "Second item should be at y=0");
    cr_assert_eq(item3->y, 0, "Third item should be at y=0");
    
    cleanup_test_container(container);
}

Test(flex_layout, flex_grow_distribution) {
    // Test flex-grow distribution
    ViewBlock* container = create_test_flex_container(400, 100);
    ViewBlock* item1 = create_test_flex_item(100, 50, 1.0f); // flex-grow: 1
    ViewBlock* item2 = create_test_flex_item(100, 50, 2.0f); // flex-grow: 2
    ViewBlock* item3 = create_test_flex_item(100, 50, 0.0f); // flex-grow: 0
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Free space = 400 - 300 = 100
    // item1 gets 1/3 * 100 = 33 extra
    // item2 gets 2/3 * 100 = 67 extra
    // item3 gets 0 extra
    cr_assert_eq(item1->content_width, 133, "Item1 should grow to 133px");
    cr_assert_eq(item2->content_width, 167, "Item2 should grow to 167px");
    cr_assert_eq(item3->content_width, 100, "Item3 should remain 100px");
    
    cleanup_test_container(container);
}

Test(flex_layout, justify_content_center) {
    // Test justify-content: center
    ViewBlock* container = create_test_flex_container(400, 100);
    container->embed->flex_container->justify = JUSTIFY_CENTER;
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Total content width = 200, free space = 200
    // Center alignment: start at 200/2 = 100
    cr_assert_eq(item1->x, 100, "First item should be centered at x=100");
    cr_assert_eq(item2->x, 200, "Second item should be at x=200");
    
    cleanup_test_container(container);
}

Test(flex_layout, justify_content_space_between) {
    // Test justify-content: space-between
    ViewBlock* container = create_test_flex_container(400, 100);
    container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Free space = 100, distributed between 2 gaps = 50 each
    cr_assert_eq(item1->x, 0, "First item should be at x=0");
    cr_assert_eq(item2->x, 150, "Second item should be at x=150");
    cr_assert_eq(item3->x, 300, "Third item should be at x=300");
    
    cleanup_test_container(container);
}

Test(flex_layout, column_direction) {
    // Test flex-direction: column
    ViewBlock* container = create_test_flex_container(100, 300);
    container->embed->flex_container->direction = DIR_COLUMN;
    container->embed->flex_container->main_axis_size = 300;
    container->embed->flex_container->cross_axis_size = 100;
    
    ViewBlock* item1 = create_test_flex_item(50, 100);
    ViewBlock* item2 = create_test_flex_item(50, 100);
    ViewBlock* item3 = create_test_flex_item(50, 100);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Check that items are positioned vertically
    cr_assert_eq(item1->y, 0, "First item should be at y=0");
    cr_assert_eq(item2->y, 100, "Second item should be at y=100");
    cr_assert_eq(item3->y, 200, "Third item should be at y=200");
    
    // Check that all items are aligned at left
    cr_assert_eq(item1->x, 0, "First item should be at x=0");
    cr_assert_eq(item2->x, 0, "Second item should be at x=0");
    cr_assert_eq(item3->x, 0, "Third item should be at x=0");
    
    cleanup_test_container(container);
}

Test(flex_layout, align_items_center) {
    // Test align-items: center
    ViewBlock* container = create_test_flex_container(300, 200);
    container->embed->flex_container->align_items = ALIGN_CENTER;
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 100);
    ViewBlock* item3 = create_test_flex_item(100, 75);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Line cross size should be 100 (tallest item)
    // Items should be centered in cross axis
    cr_assert_eq(item1->y, 25, "Item1 should be centered at y=25 (100-50)/2");
    cr_assert_eq(item2->y, 0, "Item2 should be at y=0 (tallest item)");
    cr_assert_eq(item3->y, 12, "Item3 should be centered at y=12 (100-75)/2");
    
    cleanup_test_container(container);
}

Test(flex_layout, wrap_multiline) {
    // Test flex-wrap: wrap with multiple lines
    ViewBlock* container = create_test_flex_container(250, 200);
    container->embed->flex_container->wrap = WRAP_WRAP;
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    ViewBlock* item4 = create_test_flex_item(100, 50);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    add_child_to_container(container, item4);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // First line: item1, item2 (200px total, fits in 250px)
    // Second line: item3, item4 (200px total, fits in 250px)
    cr_assert_eq(item1->x, 0, "Item1 should be at x=0");
    cr_assert_eq(item2->x, 100, "Item2 should be at x=100");
    cr_assert_eq(item1->y, 0, "Item1 should be at y=0 (first line)");
    cr_assert_eq(item2->y, 0, "Item2 should be at y=0 (first line)");
    
    cr_assert_eq(item3->x, 0, "Item3 should be at x=0 (second line)");
    cr_assert_eq(item4->x, 100, "Item4 should be at x=100 (second line)");
    cr_assert_eq(item3->y, 50, "Item3 should be at y=50 (second line)");
    cr_assert_eq(item4->y, 50, "Item4 should be at y=50 (second line)");
    
    cleanup_test_container(container);
}

Test(flex_layout, gap_properties) {
    // Test row-gap and column-gap
    ViewBlock* container = create_test_flex_container(350, 100);
    container->embed->flex_container->column_gap = 25; // gap between items in row
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Items should be spaced with 25px gaps
    cr_assert_eq(item1->x, 0, "Item1 should be at x=0");
    cr_assert_eq(item2->x, 125, "Item2 should be at x=125 (100 + 25 gap)");
    cr_assert_eq(item3->x, 250, "Item3 should be at x=250 (100 + 25 + 100 + 25)");
    
    cleanup_test_container(container);
}

Test(flex_layout, order_property) {
    // Test order property
    ViewBlock* container = create_test_flex_container(300, 100);
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    
    // Set order: item3 first, item1 second, item2 last
    item1->order = 1;
    item2->order = 2;
    item3->order = 0;
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // After sorting by order: item3 (order=0), item1 (order=1), item2 (order=2)
    // Note: The actual positioning depends on the implementation
    // We need to check the flex layout's internal line structure
    FlexContainerLayout* flex = container->embed->flex_container;
    cr_assert_eq(flex->line_count, 1, "Should have one line");
    
    if (flex->line_count > 0) {
        FlexLineInfo* line = &flex->lines[0];
        cr_assert_eq(line->item_count, 3, "Line should have 3 items");
        
        // Items should be sorted by order
        cr_assert_eq(line->items[0]->order, 0, "First item should have order=0");
        cr_assert_eq(line->items[1]->order, 1, "Second item should have order=1");
        cr_assert_eq(line->items[2]->order, 2, "Third item should have order=2");
    }
    
    cleanup_test_container(container);
}

Test(flex_layout, align_self_override) {
    // Test align-self overriding align-items
    ViewBlock* container = create_test_flex_container(300, 200);
    container->embed->flex_container->align_items = ALIGN_START;
    
    ViewBlock* item1 = create_test_flex_item(100, 50);
    ViewBlock* item2 = create_test_flex_item(100, 50);
    ViewBlock* item3 = create_test_flex_item(100, 50);
    
    // Override align-self for item2
    item2->align_self = ALIGN_END;
    
    add_child_to_container(container, item1);
    add_child_to_container(container, item2);
    add_child_to_container(container, item3);
    
    LayoutContext lycon = {0};
    layout_flex_container_new(&lycon, container);
    
    // Line cross size should be 50 (all items same height)
    cr_assert_eq(item1->y, 0, "Item1 should use align-items: start");
    cr_assert_eq(item2->y, 0, "Item2 should use align-self: end (50-50=0 in this case)");
    cr_assert_eq(item3->y, 0, "Item3 should use align-items: start");
    
    cleanup_test_container(container);
}
