#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>

// Minimal test implementation without external dependencies
extern "C" {
    #include <stdlib.h>
    #include <stdio.h>
}

// Simplified versions of the key structures for testing
typedef enum {
    DIR_ROW = 0,
    DIR_ROW_REVERSE,
    DIR_COLUMN,
    DIR_COLUMN_REVERSE
} FlexDirection;

typedef enum {
    WRAP_NOWRAP = 0,
    WRAP_WRAP,
    WRAP_WRAP_REVERSE
} FlexWrap;

typedef enum {
    JUSTIFY_START = 0,
    JUSTIFY_END,
    JUSTIFY_CENTER,
    JUSTIFY_SPACE_BETWEEN,
    JUSTIFY_SPACE_AROUND,
    JUSTIFY_SPACE_EVENLY
} JustifyContent;

typedef enum {
    ALIGN_START = 0,
    ALIGN_END,
    ALIGN_CENTER,
    ALIGN_STRETCH,
    ALIGN_BASELINE,
    ALIGN_SPACE_BETWEEN,
    ALIGN_SPACE_AROUND,
    ALIGN_SPACE_EVENLY
} AlignType;

typedef enum {
    WM_HORIZONTAL_TB = 0,
    WM_VERTICAL_RL,
    WM_VERTICAL_LR
} WritingMode;

// Simplified ViewBlock for testing
struct ViewBlock {
    int x, y;
    int width, height;
    int content_width, content_height;
    float flex_grow;
    float flex_shrink;
    int flex_basis;
    AlignType align_self;
    int order;
    ViewBlock* first_child;
    ViewBlock* last_child;
    ViewBlock* next_sibling;
    ViewBlock* prev_sibling;
    ViewBlock* parent;
};

struct FlexLineInfo {
    ViewBlock** items;
    int item_count;
    int main_size;
    int cross_size;
    int free_space;
    float total_flex_grow;
    float total_flex_shrink;
};

struct FlexContainerLayout {
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType align_items;
    AlignType align_content;
    int row_gap;
    int column_gap;
    WritingMode writing_mode;
    int main_axis_size;
    int cross_axis_size;
    FlexLineInfo* lines;
    int line_count;
    int allocated_lines;
    bool needs_reflow;
};

// Test utility functions
bool is_main_axis_horizontal(FlexContainerLayout* flex_layout) {
    if (!flex_layout) return true;
    
    bool is_row = (flex_layout->direction == DIR_ROW || flex_layout->direction == DIR_ROW_REVERSE);
    bool is_horizontal_writing = (flex_layout->writing_mode == WM_HORIZONTAL_TB);
    
    return (is_row && is_horizontal_writing) || (!is_row && !is_horizontal_writing);
}

int get_main_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
}

int get_cross_axis_size(ViewBlock* item, FlexContainerLayout* flex_layout) {
    return is_main_axis_horizontal(flex_layout) ? item->height : item->width;
}

void set_main_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->x = position;
    } else {
        item->y = position;
    }
}

void set_cross_axis_position(ViewBlock* item, int position, FlexContainerLayout* flex_layout) {
    if (is_main_axis_horizontal(flex_layout)) {
        item->y = position;
    } else {
        item->x = position;
    }
}

int calculate_flex_basis(ViewBlock* item, FlexContainerLayout* flex_layout) {
    if (!item) return 0;
    
    if (item->flex_basis >= 0) {
        return item->flex_basis;
    }
    
    // Use main axis size as default
    return is_main_axis_horizontal(flex_layout) ? item->width : item->height;
}

// Collect flex items from container
int collect_flex_items(ViewBlock* container, ViewBlock*** items) {
    if (!container || !items) return 0;
    
    // Count children
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
    
    // Allocate array
    *items = (ViewBlock**)malloc(count * sizeof(ViewBlock*));
    
    // Fill array
    child = container->first_child;
    for (int i = 0; i < count; i++) {
        (*items)[i] = child;
        child = child->next_sibling;
    }
    
    return count;
}

// Sort items by order property
void sort_flex_items_by_order(ViewBlock** items, int count) {
    if (!items || count <= 1) return;
    
    // Simple bubble sort for testing
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (items[j]->order > items[j + 1]->order) {
                ViewBlock* temp = items[j];
                items[j] = items[j + 1];
                items[j + 1] = temp;
            }
        }
    }
}

// Create single flex line (no wrapping for simplicity)
int create_flex_lines(FlexContainerLayout* flex_layout, ViewBlock** items, int item_count) {
    if (!flex_layout || !items || item_count == 0) return 0;
    
    flex_layout->line_count = 1;
    flex_layout->allocated_lines = 1;
    flex_layout->lines = (FlexLineInfo*)malloc(sizeof(FlexLineInfo));
    
    FlexLineInfo* line = &flex_layout->lines[0];
    line->items = (ViewBlock**)malloc(item_count * sizeof(ViewBlock*));
    line->item_count = item_count;
    line->main_size = 0;
    line->cross_size = 0;
    line->total_flex_grow = 0;
    line->total_flex_shrink = 0;
    
    for (int i = 0; i < item_count; i++) {
        line->items[i] = items[i];
        line->main_size += calculate_flex_basis(items[i], flex_layout);
        line->total_flex_grow += items[i]->flex_grow;
        line->total_flex_shrink += items[i]->flex_shrink;
        
        int cross_size = get_cross_axis_size(items[i], flex_layout);
        if (cross_size > line->cross_size) {
            line->cross_size = cross_size;
        }
    }
    
    line->free_space = flex_layout->main_axis_size - line->main_size;
    
    return 1;
}

// Align items on main axis
void align_items_main_axis(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    if (!flex_layout || !line || line->item_count == 0) return;
    
    int container_size = flex_layout->main_axis_size;
    int content_size = line->main_size;
    int free_space = container_size - content_size;
    
    int start_pos = 0;
    int item_spacing = 0;
    
    switch (flex_layout->justify) {
        case JUSTIFY_START:
            start_pos = 0;
            break;
            
        case JUSTIFY_END:
            start_pos = free_space;
            break;
            
        case JUSTIFY_CENTER:
            start_pos = free_space / 2;
            break;
            
        case JUSTIFY_SPACE_BETWEEN:
            start_pos = 0;
            if (line->item_count > 1) {
                item_spacing = free_space / (line->item_count - 1);
            }
            break;
            
        case JUSTIFY_SPACE_AROUND:
            if (line->item_count > 0) {
                item_spacing = free_space / line->item_count;
                start_pos = item_spacing / 2;
            }
            break;
            
        case JUSTIFY_SPACE_EVENLY:
            if (line->item_count > 0) {
                item_spacing = free_space / (line->item_count + 1);
                start_pos = item_spacing;
            }
            break;
    }
    
    // Position items
    int current_pos = start_pos;
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        set_main_axis_position(item, current_pos, flex_layout);
        
        int item_size = get_main_axis_size(item, flex_layout);
        current_pos += item_size + item_spacing;
    }
}

// Simple flex layout test function
void test_flex_layout(FlexContainerLayout* flex_layout, ViewBlock* container) {
    if (!flex_layout || !container) return;
    
    // Collect items
    ViewBlock** items;
    int item_count = collect_flex_items(container, &items);
    
    if (item_count == 0) return;
    
    // Sort by order
    sort_flex_items_by_order(items, item_count);
    
    // Create lines
    create_flex_lines(flex_layout, items, item_count);
    
    // Align items
    if (flex_layout->line_count > 0) {
        align_items_main_axis(flex_layout, &flex_layout->lines[0]);
    }
    
    free(items);
}

// Test helper functions
ViewBlock* create_test_item(int width, int height, float grow = 0, float shrink = 1, int order = 0) {
    ViewBlock* item = (ViewBlock*)calloc(1, sizeof(ViewBlock));
    item->width = width;
    item->height = height;
    item->content_width = width;
    item->content_height = height;
    item->flex_grow = grow;
    item->flex_shrink = shrink;
    item->flex_basis = width;
    item->align_self = ALIGN_START;
    item->order = order;
    return item;
}

void add_child(ViewBlock* parent, ViewBlock* child) {
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }
    child->parent = parent;
}

FlexContainerLayout* create_flex_container(int width, int height) {
    FlexContainerLayout* flex = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
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
    return flex;
}

void cleanup_flex_container(FlexContainerLayout* flex) {
    if (!flex) return;
    
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

// Test functions
void test_basic_row_layout() {
    std::cout << "Testing basic row layout..." << std::endl;
    
    ViewBlock container = {0};
    container.width = 300;
    container.height = 100;
    
    ViewBlock* item1 = create_test_item(100, 50);
    ViewBlock* item2 = create_test_item(100, 50);
    ViewBlock* item3 = create_test_item(100, 50);
    
    add_child(&container, item1);
    add_child(&container, item2);
    add_child(&container, item3);
    
    FlexContainerLayout* flex = create_flex_container(300, 100);
    test_flex_layout(flex, &container);
    
    // Check positions
    assert(item1->x == 0);
    assert(item2->x == 100);
    assert(item3->x == 200);
    
    std::cout << "✓ Basic row layout test passed" << std::endl;
    
    cleanup_flex_container(flex);
    free(item1);
    free(item2);
    free(item3);
}

void test_justify_center() {
    std::cout << "Testing justify-content: center..." << std::endl;
    
    ViewBlock container = {0};
    container.width = 400;
    container.height = 100;
    
    ViewBlock* item1 = create_test_item(100, 50);
    ViewBlock* item2 = create_test_item(100, 50);
    
    add_child(&container, item1);
    add_child(&container, item2);
    
    FlexContainerLayout* flex = create_flex_container(400, 100);
    flex->justify = JUSTIFY_CENTER;
    test_flex_layout(flex, &container);
    
    // Total content width = 200, free space = 200
    // Center alignment: start at 200/2 = 100
    assert(item1->x == 100);
    assert(item2->x == 200);
    
    std::cout << "✓ Justify center test passed" << std::endl;
    
    cleanup_flex_container(flex);
    free(item1);
    free(item2);
}

void test_justify_space_between() {
    std::cout << "Testing justify-content: space-between..." << std::endl;
    
    ViewBlock container = {0};
    container.width = 400;
    container.height = 100;
    
    ViewBlock* item1 = create_test_item(100, 50);
    ViewBlock* item2 = create_test_item(100, 50);
    ViewBlock* item3 = create_test_item(100, 50);
    
    add_child(&container, item1);
    add_child(&container, item2);
    add_child(&container, item3);
    
    FlexContainerLayout* flex = create_flex_container(400, 100);
    flex->justify = JUSTIFY_SPACE_BETWEEN;
    test_flex_layout(flex, &container);
    
    // Free space = 100, distributed between 2 gaps = 50 each
    assert(item1->x == 0);
    assert(item2->x == 150);
    assert(item3->x == 300);
    
    std::cout << "✓ Justify space-between test passed" << std::endl;
    
    cleanup_flex_container(flex);
    free(item1);
    free(item2);
    free(item3);
}

void test_order_property() {
    std::cout << "Testing order property..." << std::endl;
    
    ViewBlock container = {0};
    container.width = 300;
    container.height = 100;
    
    ViewBlock* item1 = create_test_item(100, 50, 0, 1, 1);  // order = 1
    ViewBlock* item2 = create_test_item(100, 50, 0, 1, 2);  // order = 2
    ViewBlock* item3 = create_test_item(100, 50, 0, 1, 0);  // order = 0
    
    add_child(&container, item1);
    add_child(&container, item2);
    add_child(&container, item3);
    
    FlexContainerLayout* flex = create_flex_container(300, 100);
    test_flex_layout(flex, &container);
    
    // After sorting by order: item3 (order=0), item1 (order=1), item2 (order=2)
    // Check that the line has items in correct order
    assert(flex->line_count == 1);
    assert(flex->lines[0].item_count == 3);
    assert(flex->lines[0].items[0]->order == 0);  // item3
    assert(flex->lines[0].items[1]->order == 1);  // item1
    assert(flex->lines[0].items[2]->order == 2);  // item2
    
    std::cout << "✓ Order property test passed" << std::endl;
    
    cleanup_flex_container(flex);
    free(item1);
    free(item2);
    free(item3);
}

int main() {
    std::cout << "Running Flex Layout Tests..." << std::endl;
    std::cout << "=============================" << std::endl;
    
    test_basic_row_layout();
    test_justify_center();
    test_justify_space_between();
    test_order_property();
    
    std::cout << "=============================" << std::endl;
    std::cout << "✅ All flex layout tests passed!" << std::endl;
    
    return 0;
}
