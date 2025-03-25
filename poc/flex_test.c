#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdlib.h>
#include "flex.c"

// Helper function to create a container with items
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

void cleanupContainer(FlexContainer* container) {
    free(container->items);
    free(container);
}

// Test Suite
Test(flexbox, basic_layout) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100 };
    container->items[2] = (FlexItem){ .width = 200, .height = 100 };

    layoutFlexContainer(container);

    // Check positions
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x position");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y position");
    cr_assert_float_eq(container->items[1].positionCoords.x, 210, 0.1, "Item 1 x position");
    cr_assert_float_eq(container->items[2].positionCoords.x, 420, 0.1, "Item 2 x position");

    cleanupContainer(container);
}

Test(flexbox, flex_grow) {
    FlexContainer* container = createTestContainer(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 2 };

    layoutFlexContainer(container);

    // Total width = 800, base = 400 + 10 gap = 410, free space = 390
    // Item 0 gets 1/3 of 390 = 130, Item 1 gets 2/3 = 260
    cr_assert_float_eq(container->items[0].width, 330, 0.1, "Item 0 width with flex-grow");
    cr_assert_float_eq(container->items[1].width, 460, 0.1, "Item 1 width with flex-grow");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x position");
    cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "Item 1 x position");

    cleanupContainer(container);
}

Test(flexbox, flex_shrink) {
    FlexContainer* container = createTestContainer(2);
    container->width = 400;  // Force shrinking
    container->items[0] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 1 };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 2 };

    layoutFlexContainer(container);

    // Total base = 600 + 10 gap = 610, available = 400, deficit = 210
    // Item 0 shrinks by 1/3 of 210 = 70, Item 1 by 2/3 = 140
    cr_assert_float_eq(container->items[0].width, 230, 0.1, "Item 0 width with flex-shrink");
    cr_assert_float_eq(container->items[1].width, 160, 0.1, "Item 1 width with flex-shrink");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x position");
    cr_assert_float_eq(container->items[1].positionCoords.x, 240, 0.1, "Item 1 x position");

    cleanupContainer(container);
}

Test(flexbox, wrap) {
    FlexContainer* container = createTestContainer(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100 };
    container->items[2] = (FlexItem){ .width = 200, .height = 100 };

    layoutFlexContainer(container);

    // First line: Item 0, Second line: Item 1 and 2 (simplified check)
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x position");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x position on new line");
    cr_assert_float_eq(container->items[2].positionCoords.x, 210, 0.1, "Item 2 x position");

    cleanupContainer(container);
}

Test(flexbox, justify_content) {
    FlexContainer* container = createTestContainer(2);
    container->justify = JUSTIFY_SPACE_EVENLY;
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100 };

    layoutFlexContainer(container);

    // Total width = 800, items = 400 + 10 gap = 410, free space = 390
    // Space evenly: 390 / 3 = 130 between each and edges
    cr_assert_float_eq(container->items[0].positionCoords.x, 130, 0.1, "Item 0 x with space-evenly");
    cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "Item 1 x with space-evenly");

    cleanupContainer(container);
}

Test(flexbox, align_items) {
    FlexContainer* container = createTestContainer(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 150 };

    layoutFlexContainer(container);

    // Container height = 600, items centered vertically
    cr_assert_float_eq(container->items[0].positionCoords.y, 250, 0.1, "Item 0 y with align center");
    cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "Item 1 y with align center");

    cleanupContainer(container);
}

// Run all tests
int main(int argc, char *argv[]) {
    struct criterion_test_set *tests = criterion_initialize();
    // criterion_options_t options = { .no_early_exit = false };
    int result = criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result ? 0 : 1;
}

// zig cc -o flex_test flex_test.c -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib