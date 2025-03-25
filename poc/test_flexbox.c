#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdlib.h>
#include "flex.c"

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

Test(flexbox, basic_layout) {
    FlexContainer* container = createTestContainer(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100 };
    container->items[2] = (FlexItem){ .width = 200, .height = 100 };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 210, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[2].positionCoords.x, 420, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");

    cleanupContainer(container);
}

Test(flexbox, flex_grow) {
    FlexContainer* container = createTestContainer(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 2 };

    layoutFlexContainer(container);

    // 800 - (400 + 10) = 390 free space, 1:2 ratio = 130 + 260
    cr_assert_float_eq(container->items[0].width, 330, 0.1, "Item 0 width");
    cr_assert_float_eq(container->items[1].width, 460, 0.1, "Item 1 width");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox, flex_shrink) {
    FlexContainer* container = createTestContainer(2);
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 1 };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 2 };

    layoutFlexContainer(container);

    // 400 - (600 + 10) = -210 deficit, 1:2 ratio = 70 + 140 reduction
    cr_assert_float_eq(container->items[0].width, 230, 0.1, "Item 0 width");
    cr_assert_float_eq(container->items[1].width, 160, 0.1, "Item 1 width");
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 240, 0.1, "Item 1 x");

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

    // Each item on its own line: 400 - 200 = 200 free space per line
    cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Item 1 x");
    cr_assert_float_eq(container->items[1].positionCoords.y, 110, 0.1, "Item 1 y");
    cr_assert_float_eq(container->items[2].positionCoords.x, 0, 0.1, "Item 2 x");
    cr_assert_float_eq(container->items[2].positionCoords.y, 220, 0.1, "Item 2 y");

    cleanupContainer(container);
}

Test(flexbox, justify_content) {
    FlexContainer* container = createTestContainer(2);
    container->justify = JUSTIFY_SPACE_EVENLY;
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 100 };

    layoutFlexContainer(container);

    // 800 - (400 + 10) = 390, 390 / 3 = 130 spacing
    cr_assert_float_eq(container->items[0].positionCoords.x, 130, 0.1, "Item 0 x");
    cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "Item 1 x");

    cleanupContainer(container);
}

Test(flexbox, align_items) {
    FlexContainer* container = createTestContainer(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100 };
    container->items[1] = (FlexItem){ .width = 200, .height = 150 };

    layoutFlexContainer(container);

    cr_assert_float_eq(container->items[0].positionCoords.y, 250, 0.1, "Item 0 y");
    cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "Item 1 y");

    cleanupContainer(container);
}

int main(int argc, char *argv[]) {
    struct criterion_test_set *tests = criterion_initialize();
    // criterion_options_t options = { .no_early_exit = false };
    int result = criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result ? 0 : 1;
}

// zig cc -o test_flexbox.exe test_flexbox.c -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib