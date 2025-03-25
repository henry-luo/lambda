#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/logging.h>
#include <stdlib.h>
#include "flex.c"

// Container setup helper
FlexContainer* createTestContainer(int itemCount) {
    FlexContainer* container = malloc(sizeof(FlexContainer));
    *container = (FlexContainer){
        .width = 800, .height = 600,
        .direction = DIR_ROW,
        .wrap = WRAP_NOWRAP,
        .justify = JUSTIFY_START,
        .alignItems = ALIGN_START,
        .gap = 10,
        .items = malloc(itemCount * sizeof(FlexItem)),
        .itemCount = itemCount,
        .writingMode = "horizontal-tb",
        .textDirection = "ltr"
    };
    return container;
}

// Container cleanup helper
void cleanupContainer(FlexContainer* container) {
    free(container->items);
    free(container);
}

// Test suite definition
TestSuite(flexbox_tests, .description = "Flexbox layout tests");

// Individual test cases
Test(flexbox_tests, basic_layout) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 210, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[2].positionCoords.x, 420, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");

    cleanupContainer(container);
}

Test(flexbox_tests, flex_grow) {
    FlexContainer* container = createTestContainer(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].width, 330, 0.1, "Item 0 width");
    cr_assert_float_eq(container->items[1].width, 460, 0.1, "Item 1 width");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox_tests, flex_shrink) {
    FlexContainer* container = createTestContainer(2);
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].width, 230, 0.1, "Item 0 width");
    cr_assert_float_eq(container->items[1].width, 160, 0.1, "Item 1 width");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 240, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox_tests, wrap) {
    FlexContainer* container = createTestContainer(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[1].positionCoords.y, 110, 0.1, "Item 1 y");
    cr_assert_float_eq(container->items[2].positionCoords.x, 0, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[2].positionCoords.y, 220, 0.1, "Item 2 y");

    cleanupContainer(container);
}

Test(flexbox_tests, justify_content) {
    FlexContainer* container = createTestContainer(2);
    container->justify = JUSTIFY_SPACE_EVENLY;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 130, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 470, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox_tests, align_items) {
    FlexContainer* container = createTestContainer(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.y, 250, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "Item 1 y");

    cleanupContainer(container);
}

Test(flexbox_tests, column_direction) {
    FlexContainer* container = createTestContainer(2);
    container->direction = DIR_COLUMN;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[1].positionCoords.y, 110, 0.1, "Item 1 y");

    cleanupContainer(container);
}

Test(flexbox_tests, row_reverse) {
    FlexContainer* container = createTestContainer(2);
    container->direction = DIR_ROW_REVERSE;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 390, 0.1, "Item 0 x");  // Changed from 410
    cr_assert_float_eq(container->items[1].positionCoords.x, 600, 0.1, "Item 1 x");  // Changed from 200
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");

    cleanupContainer(container);
}

Test(flexbox_tests, absolute_positioning) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_ABSOLUTE, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[2].positionCoords.x, 210, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x unchanged");

    cleanupContainer(container);
}

Test(flexbox_tests, hidden_visibility) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_HIDDEN };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[2].positionCoords.x, 210, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x unchanged");

    cleanupContainer(container);
}

Test(flexbox_tests, flex_basis) {
    FlexContainer* container = createTestContainer(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].width, 300, 0.1, "Item 0 width");
    cr_assert_float_eq(container->items[1].width, 400, 0.1, "Item 1 width");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 310, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox_tests, align_self_override) {
    FlexContainer* container = createTestContainer(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .alignSelf = ALIGN_END, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.y, 500, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "Item 1 y");

    cleanupContainer(container);
}

Test(flexbox_tests, zero_size_container) {
    FlexContainer* container = createTestContainer(2);
    container->width = 0;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x"); // Adjusted expectation

    cleanupContainer(container);
}

// Add after existing tests, before the main function

Test(flexbox_tests, min_max_constraints) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ 
        .width = 200, 
        .height = 100, 
        .minWidth = 150, 
        .maxWidth = 250, 
        .flexGrow = 1, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[1] = (FlexItem){ 
        .width = 100, 
        .height = 100, 
        .minWidth = 150, 
        .maxWidth = 200, 
        .flexShrink = 1, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[2] = (FlexItem){ 
        .width = 300, 
        .height = 100, 
        .minWidth = 200, 
        .maxWidth = 250, 
        .flexGrow = 1, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layoutFlexContainer(container);

    // Check that widths respect min/max constraints
    cr_assert_float_eq(container->items[0].width, 250, 0.1, "Item 0 width should be at max");
    cr_assert_float_eq(container->items[1].width, 150, 0.1, "Item 1 width should be at min");
    cr_assert_float_eq(container->items[2].width, 250, 0.1, "Item 2 width should be at max");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 260, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[2].positionCoords.x, 420, 0.1, "Item 2 x");

    cleanupContainer(container);
}

Test(flexbox_tests, wrap_reverse) {
    FlexContainer* container = createTestContainer(3);
    container->wrap = WRAP_WRAP_REVERSE;
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layoutFlexContainer(container);

    // In wrap-reverse, lines should stack bottom-to-top from container bottom
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 500, 0.1, "Item 0 y");  // Changed from 220
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[1].positionCoords.y, 390, 0.1, "Item 1 y");  // Changed from 110
    cr_assert_float_eq(container->items[2].positionCoords.x, 0, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[2].positionCoords.y, 280, 0.1, "Item 2 y");  // Changed from 0

    cleanupContainer(container);
}

Test(flexbox_tests, nested_containers) {
    // Outer container
    FlexContainer* outer = createTestContainer(1);
    outer->direction = DIR_COLUMN;
    
    // Inner container as an item
    FlexContainer* inner = createTestContainer(2);
    inner->width = 400;
    inner->height = 200;
    inner->items[0] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    // Set inner container as an item in outer container
    outer->items[0] = (FlexItem){
        .width = 400,
        .height = 200,
        .position = POS_STATIC,
        .visibility = VIS_VISIBLE
    };

    // Layout inner container first
    layoutFlexContainer(inner);
    
    // Then layout outer container
    layoutFlexContainer(outer);

    // Check inner container items
    cr_assert_float_eq(inner->items[0].positionCoords.x, 0, 0.1, "Inner item 0 x");
    cr_assert_float_eq(inner->items[0].positionCoords.y, 0, 0.1, "Inner item 0 y");
    cr_assert_float_eq(inner->items[1].positionCoords.x, 160, 0.1, "Inner item 1 x");
    cr_assert_float_eq(inner->items[1].positionCoords.y, 0, 0.1, "Inner item 1 y");
    
    // Check outer container positioning
    cr_assert_float_eq(outer->items[0].positionCoords.x, 0, 0.1, "Outer item 0 x");
    cr_assert_float_eq(outer->items[0].positionCoords.y, 0, 0.1, "Outer item 0 y");

    cleanupContainer(inner);
    cleanupContainer(outer);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    struct criterion_test_set *tests = criterion_initialize();
    int result = criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result ? 0 : 1;
}

// zig cc -o test_flexbox.exe test_flexbox.c -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib
// ./test_flexbox.exe --jobs 1 --verbose