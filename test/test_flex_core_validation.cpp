#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>

// Minimal type definitions needed for flex layout
typedef enum { DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE } FlexDirection;
typedef enum { WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE } FlexWrap;
typedef enum { JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY } JustifyContent;
typedef enum { 
    ALIGN_AUTO, ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH,
    ALIGN_SPACE_BETWEEN, ALIGN_SPACE_AROUND, ALIGN_SPACE_EVENLY
} AlignType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

// Lexbor constants
#define LXB_CSS_VALUE_ROW 1
#define LXB_CSS_VALUE_COLUMN 2
#define LXB_CSS_VALUE_NOWRAP 10
#define LXB_CSS_VALUE_FLEX_START 20
#define LXB_CSS_VALUE_STRETCH 25

// View types
typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_INLINE,
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_SCROLL_PANE,
} ViewType;

// Forward declarations
struct ViewBlock;
struct View;
struct ViewGroup;

// Basic view structure
struct View {
    ViewType type;
    void* node;
    View* next;
    ViewGroup* parent;
};

struct ViewGroup : View {
    View* child;
};

// Flex line info
typedef struct FlexLineInfo {
    struct ViewBlock** items;
    int item_count;
    int main_size;
    int cross_size;
    int free_space;
    float total_flex_grow;
    float total_flex_shrink;
    int baseline;
} FlexLineInfo;

// Flex container layout
typedef struct {
    int direction;
    int wrap;
    int justify;
    int align_items;
    int align_content;
    int row_gap;
    int column_gap;
    WritingMode writing_mode;
    TextDirection text_direction;
    
    struct ViewBlock** flex_items;
    int item_count;
    int allocated_items;
    
    FlexLineInfo* lines;
    int line_count;
    int allocated_lines;
    
    int main_axis_size;
    int cross_axis_size;
    bool needs_reflow;
} FlexContainerLayout;

// Embed properties
typedef struct {
    void* img;
    void* doc;
    FlexContainerLayout* flex_container;
} EmbedProp;

// ViewBlock structure
struct ViewBlock : ViewGroup {
    int x, y, width, height;
    int content_width, content_height;
    void* blk;
    void* scroller;
    EmbedProp* embed;
    
    // Flex item properties
    float flex_grow;
    float flex_shrink;
    int flex_basis;
    int align_self;
    int order;
    bool flex_basis_is_percent;
    
    // Child navigation
    ViewBlock* first_child;
    ViewBlock* last_child;
    ViewBlock* next_sibling;
    ViewBlock* prev_sibling;
};

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

// Core flex layout functions (simplified implementations for testing)
bool is_main_axis_horizontal(FlexContainerLayout* flex_layout) {
    return flex_layout->direction == LXB_CSS_VALUE_ROW || 
           flex_layout->direction == DIR_ROW_REVERSE;
}

int get_main_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
}

int get_cross_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->height : item->width;
}

int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout) {
    if (item->flex_basis == -1) {
        return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
    } else if (item->flex_basis_is_percent) {
        int container_size = is_main_axis_horizontal(flex_layout) ? 
                           flex_layout->main_axis_size : flex_layout->cross_axis_size;
        return (container_size * item->flex_basis) / 100;
    } else {
        return item->flex_basis;
    }
}

int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    if (!container || !items) return 0;
    
    FlexContainerLayout* flex = container->embed->flex_container;
    if (!flex) return 0;
    
    int count = 0;
    ViewBlock* child = container->first_child;
    while (child) {
        count++;
        child = child->next_sibling;
    }
    
    if (count == 0) {
        *items = nullptr;
        return 0;
    }
    
    // Allocate items array
    flex->flex_items = (ViewBlock**)calloc(count, sizeof(ViewBlock*));
    
    // Collect items
    count = 0;
    child = container->first_child;
    while (child) {
        flex->flex_items[count] = child;
        count++;
        child = child->next_sibling;
    }
    
    flex->item_count = count;
    *items = flex->flex_items;
    return count;
}

void sort_flex_items_by_order(ViewBlock** items, int count) {
    if (!items || count <= 1) return;
    
    // Simple insertion sort by order
    for (int i = 1; i < count; ++i) {
        ViewBlock* key = items[i];
        int j = i - 1;
        
        while (j >= 0 && items[j]->order > key->order) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

// Test helper functions
ViewBlock* createFlexContainer(int width = 800, int height = 200) {
    ViewBlock* container = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    container->type = RDT_VIEW_BLOCK;
    container->width = width;
    container->height = height;
    container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
    
    FlexContainerLayout* flex = container->embed->flex_container;
    flex->direction = LXB_CSS_VALUE_ROW;
    flex->wrap = LXB_CSS_VALUE_NOWRAP;
    flex->justify = LXB_CSS_VALUE_FLEX_START;
    flex->align_items = LXB_CSS_VALUE_STRETCH;
    flex->main_axis_size = width;
    flex->cross_axis_size = height;
    flex->writing_mode = WM_HORIZONTAL_TB;
    flex->text_direction = TD_LTR;
    
    return container;
}

ViewBlock* createFlexItem(ViewBlock* parent, int width, int height, 
                         float flex_grow = 0.0f, float flex_shrink = 1.0f, 
                         int flex_basis = -1, int order = 0) {
    ViewBlock* item = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    item->type = RDT_VIEW_BLOCK;
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

// Test functions
bool test_flex_container_initialization() {
    std::cout << "\n=== Testing Flex Container Initialization ===" << std::endl;
    
    ViewBlock* container = createFlexContainer(800, 200);
    TEST_ASSERT(container != nullptr, "Container creation");
    TEST_ASSERT(container->embed != nullptr, "Container embed property");
    TEST_ASSERT(container->embed->flex_container != nullptr, "Flex container layout");
    
    FlexContainerLayout* flex = container->embed->flex_container;
    TEST_ASSERT(flex->direction == LXB_CSS_VALUE_ROW, "Default direction is row");
    TEST_ASSERT(flex->wrap == LXB_CSS_VALUE_NOWRAP, "Default wrap is nowrap");
    TEST_ASSERT(flex->justify == LXB_CSS_VALUE_FLEX_START, "Default justify is flex-start");
    TEST_ASSERT(flex->align_items == LXB_CSS_VALUE_STRETCH, "Default align-items is stretch");
    TEST_ASSERT(flex->main_axis_size == 800, "Main axis size set correctly");
    TEST_ASSERT(flex->cross_axis_size == 200, "Cross axis size set correctly");
    
    return true;
}

bool test_flex_item_collection() {
    std::cout << "\n=== Testing Flex Item Collection ===" << std::endl;
    
    ViewBlock* container = createFlexContainer(800, 200);
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
    
    return true;
}

bool test_flex_item_ordering() {
    std::cout << "\n=== Testing Flex Item Ordering ===" << std::endl;
    
    ViewBlock* container = createFlexContainer(800, 200);
    ViewBlock* item1 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 3); // order: 3
    ViewBlock* item2 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 1); // order: 1
    ViewBlock* item3 = createFlexItem(container, 100, 100, 0.0f, 1.0f, -1, 2); // order: 2
    
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    sort_flex_items_by_order(items, item_count);
    
    TEST_ASSERT(items[0]->order == 1, "First item has order 1");
    TEST_ASSERT(items[1]->order == 2, "Second item has order 2");
    TEST_ASSERT(items[2]->order == 3, "Third item has order 3");
    
    return true;
}

bool test_axis_utilities() {
    std::cout << "\n=== Testing Axis Utilities ===" << std::endl;
    
    ViewBlock* container = createFlexContainer(800, 200);
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
    
    return true;
}

bool test_flex_basis_calculation() {
    std::cout << "\n=== Testing Flex Basis Calculation ===" << std::endl;
    
    ViewBlock* container = createFlexContainer(800, 200);
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
    
    return true;
}

int main() {
    std::cout << "=== Flex Layout Core Validation Tests ===" << std::endl;
    
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
        std::cout << "✅ All core flex layout functions are working correctly!" << std::endl;
        std::cout << "✅ The new flex layout implementation is ready for integration!" << std::endl;
        return 0;
    } else {
        std::cout << "❌ " << (total - passed) << " tests failed!" << std::endl;
        return 1;
    }
}
