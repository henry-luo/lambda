#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>

// Define FLEX_TEST_MODE to avoid external dependencies
#define FLEX_TEST_MODE

// Minimal includes needed for flex layout
#include "../radiant/flex.hpp"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"

// Simple test framework
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << message << std::endl; \
            return false; \
        } else { \
            std::cout << "PASS: " << message << std::endl; \
        } \
    } while(0)

// Simple memory allocation for testing
static void* test_alloc(size_t size) {
    return calloc(1, size);
}

// Test support functions
void init_view_pool(LayoutContext* lycon) {
    lycon->pool = nullptr; // Simple approach for testing
}

void cleanup_view_pool(LayoutContext* lycon) {
    lycon->pool = nullptr;
}

ViewBlock* alloc_view_block(LayoutContext* lycon) {
    ViewBlock* block = (ViewBlock*)test_alloc(sizeof(ViewBlock));
    if (!block) return nullptr;
    
    // Initialize basic fields
    block->type = RDT_VIEW_BLOCK;
    block->parent = nullptr;
    block->next = nullptr;
    block->first_child = nullptr;
    block->last_child = nullptr;
    block->next_sibling = nullptr;
    block->prev_sibling = nullptr;
    
    // Initialize flex properties
    block->flex_grow = 0.0f;
    block->flex_shrink = 1.0f;
    block->flex_basis = -1; // auto
    block->flex_basis_is_percent = false;
    block->align_self = ALIGN_AUTO;
    block->order = 0;
    
    return block;
}

// Test helper functions
ViewBlock* createFlexContainer(LayoutContext* lycon, int width = 800, int height = 200) {
    ViewBlock* container = alloc_view_block(lycon);
    if (!container) return nullptr;
    
    container->width = width;
    container->height = height;
    container->embed = (EmbedProp*)test_alloc(sizeof(EmbedProp));
    container->embed->flex_container = (FlexContainerLayout*)test_alloc(sizeof(FlexContainerLayout));
    
    // Initialize flex container with defaults
    FlexContainerLayout* flex = container->embed->flex_container;
    flex->direction = LXB_CSS_VALUE_ROW;
    flex->wrap = LXB_CSS_VALUE_NOWRAP;
    flex->justify = LXB_CSS_VALUE_FLEX_START;
    flex->align_items = LXB_CSS_VALUE_STRETCH;
    flex->align_content = LXB_CSS_VALUE_STRETCH;
    flex->row_gap = 0;
    flex->column_gap = 0;
    flex->writing_mode = WM_HORIZONTAL_TB;
    flex->text_direction = TD_LTR;
    flex->main_axis_size = width;
    flex->cross_axis_size = height;
    
    // Initialize arrays
    flex->allocated_items = 8;
    flex->flex_items = (ViewBlock**)test_alloc(flex->allocated_items * sizeof(ViewBlock*));
    flex->allocated_lines = 4;
    flex->lines = (FlexLineInfo*)test_alloc(flex->allocated_lines * sizeof(FlexLineInfo));
    flex->needs_reflow = true;
    
    return container;
}

ViewBlock* createFlexItem(ViewBlock* parent, int width, int height, 
                         float flex_grow = 0.0f, float flex_shrink = 1.0f, 
                         int flex_basis = -1, int order = 0) {
    ViewBlock* item = alloc_view_block(nullptr);
    if (!item) return nullptr;
    
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

// Include the flex layout implementation
#include "../radiant/flex_layout_new.cpp"

// Test functions
bool test_flex_container_initialization() {
    std::cout << "\n=== Testing Flex Container Initialization ===" << std::endl;
    
    LayoutContext lycon = {0};
    init_view_pool(&lycon);
    
    ViewBlock* container = createFlexContainer(&lycon, 800, 200);
    TEST_ASSERT(container != nullptr, "Container creation");
    TEST_ASSERT(container->embed != nullptr, "Container embed property");
    TEST_ASSERT(container->embed->flex_container != nullptr, "Flex container layout");
    
    FlexContainerLayout* flex = container->embed->flex_container;
    TEST_ASSERT(flex->direction == LXB_CSS_VALUE_ROW, "Default direction is row");
    TEST_ASSERT(flex->wrap == LXB_CSS_VALUE_NOWRAP, "Default wrap is nowrap");
    TEST_ASSERT(flex->justify == LXB_CSS_VALUE_FLEX_START, "Default justify is flex-start");
    TEST_ASSERT(flex->align_items == LXB_CSS_VALUE_STRETCH, "Default align-items is stretch");
    
    cleanup_view_pool(&lycon);
    return true;
}

bool test_flex_item_collection() {
    std::cout << "\n=== Testing Flex Item Collection ===" << std::endl;
    
    LayoutContext lycon = {0};
    init_view_pool(&lycon);
    
    ViewBlock* container = createFlexContainer(&lycon, 800, 200);
    ViewBlock* item1 = createFlexItem(container, 100, 100);
    ViewBlock* item2 = createFlexItem(container, 100, 100);
    ViewBlock* item3 = createFlexItem(container, 100, 100);
    
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    
    TEST_ASSERT(item_count == 3, "Collected 3 flex items");
    TEST_ASSERT(items != nullptr, "Items array is not null");
    TEST_ASSERT(items[0] == item1, "First item matches");
    TEST_ASSERT(items[1] == item2, "Second item matches");
    TEST_ASSERT(items[2] == item3, "Third item matches");
    
    cleanup_view_pool(&lycon);
    return true;
}

bool test_flex_item_ordering() {
    std::cout << "\n=== Testing Flex Item Ordering ===" << std::endl;
    
    LayoutContext lycon = {0};
    init_view_pool(&lycon);
    
    ViewBlock* container = createFlexContainer(&lycon, 800, 200);
    ViewBlock* item1 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 3); // order: 3
    ViewBlock* item2 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 1); // order: 1
    ViewBlock* item3 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 2); // order: 2
    
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    sort_flex_items_by_order(items, item_count);
    
    TEST_ASSERT(items[0]->order == 1, "First item has order 1");
    TEST_ASSERT(items[1]->order == 2, "Second item has order 2");
    TEST_ASSERT(items[2]->order == 3, "Third item has order 3");
    
    cleanup_view_pool(&lycon);
    return true;
}

bool test_axis_utilities() {
    std::cout << "\n=== Testing Axis Utilities ===" << std::endl;
    
    LayoutContext lycon = {0};
    init_view_pool(&lycon);
    
    ViewBlock* container = createFlexContainer(&lycon, 800, 200);
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Test horizontal main axis (row direction)
    flex->direction = LXB_CSS_VALUE_ROW;
    TEST_ASSERT(is_main_axis_horizontal(flex) == true, "Row direction has horizontal main axis");
    
    // Test vertical main axis (column direction)  
    flex->direction = LXB_CSS_VALUE_COLUMN;
    TEST_ASSERT(is_main_axis_horizontal(flex) == false, "Column direction has vertical main axis");
    
    ViewBlock* item = createFlexItem(container, 100, 50);
    
    // Test size getters for horizontal main axis
    flex->direction = LXB_CSS_VALUE_ROW;
    TEST_ASSERT(get_main_axis_size(item, flex) == 100, "Main axis size (width) for row");
    TEST_ASSERT(get_cross_axis_size(item, flex) == 50, "Cross axis size (height) for row");
    
    // Test size getters for vertical main axis
    flex->direction = LXB_CSS_VALUE_COLUMN;
    TEST_ASSERT(get_main_axis_size(item, flex) == 50, "Main axis size (height) for column");
    TEST_ASSERT(get_cross_axis_size(item, flex) == 100, "Cross axis size (width) for column");
    
    cleanup_view_pool(&lycon);
    return true;
}

bool test_flex_basis_calculation() {
    std::cout << "\n=== Testing Flex Basis Calculation ===" << std::endl;
    
    LayoutContext lycon = {0};
    init_view_pool(&lycon);
    
    ViewBlock* container = createFlexContainer(&lycon, 800, 200);
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Test auto flex-basis
    ViewBlock* item1 = createFlexItem(container, 150, 100, 0.0f, 1.0f, -1);
    int basis1 = calculate_flex_basis(item1, flex);
    TEST_ASSERT(basis1 == 150, "Auto flex-basis uses content width");
    
    // Test fixed flex-basis
    ViewBlock* item2 = createFlexItem(container, 150, 100, 0.0f, 1.0f, 200);
    int basis2 = calculate_flex_basis(item2, flex);
    TEST_ASSERT(basis2 == 200, "Fixed flex-basis value");
    
    // Test percentage flex-basis
    ViewBlock* item3 = createFlexItem(container, 150, 100, 0.0f, 1.0f, 50);
    item3->flex_basis_is_percent = true;
    int basis3 = calculate_flex_basis(item3, flex);
    TEST_ASSERT(basis3 == 400, "50% flex-basis of 800px container = 400px");
    
    cleanup_view_pool(&lycon);
    return true;
}

int main() {
    std::cout << "=== Flex Layout Standalone Tests ===" << std::endl;
    
    int passed = 0;
    int total = 0;
    
    // Run tests
    total++; if (test_flex_container_initialization()) passed++;
    total++; if (test_flex_item_collection()) passed++;
    total++; if (test_flex_item_ordering()) passed++;
    total++; if (test_axis_utilities()) passed++;
    total++; if (test_flex_basis_calculation()) passed++;
    
    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << " tests" << std::endl;
    
    if (passed == total) {
        std::cout << "✅ All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "❌ " << (total - passed) << " tests failed!" << std::endl;
        return 1;
    }
}
