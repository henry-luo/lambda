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

// Single test suite to ensure sequential execution
TestSuite(flexbox_tests, .description = "Flexbox layout tests");

Test(flexbox_tests, all_tests) {
    // Ensure sequential execution by running all tests in one block
    
    // 1. Basic layout test
    {
        FlexContainer* container = createTestContainer(3);
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Basic: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 210, 0.1, "Basic: Item 1 x");
        cr_assert_float_eq(container->items[2].positionCoords.x, 420, 0.1, "Basic: Item 2 x");
        cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Basic: Item 0 y");

        cleanupContainer(container);
    }

    // 2. Flex grow test
    {
        FlexContainer* container = createTestContainer(2);
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].width, 330, 0.1, "FlexGrow: Item 0 width");
        cr_assert_float_eq(container->items[1].width, 460, 0.1, "FlexGrow: Item 1 width");
        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "FlexGrow: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 340, 0.1, "FlexGrow: Item 1 x");

        cleanupContainer(container);
    }

    // 3. Flex shrink test
    {
        FlexContainer* container = createTestContainer(2);
        container->width = 400;
        container->items[0] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].width, 230, 0.1, "FlexShrink: Item 0 width");
        cr_assert_float_eq(container->items[1].width, 160, 0.1, "FlexShrink: Item 1 width");
        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "FlexShrink: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 240, 0.1, "FlexShrink: Item 1 x");

        cleanupContainer(container);
    }

    // 4. Wrap test
    {
        FlexContainer* container = createTestContainer(3);
        container->wrap = WRAP_WRAP;
        container->width = 400;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Wrap: Item 0 x");
        cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Wrap: Item 0 y");
        cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Wrap: Item 1 x");
        cr_assert_float_eq(container->items[1].positionCoords.y, 110, 0.1, "Wrap: Item 1 y");
        cr_assert_float_eq(container->items[2].positionCoords.x, 0, 0.1, "Wrap: Item 2 x");
        cr_assert_float_eq(container->items[2].positionCoords.y, 220, 0.1, "Wrap: Item 2 y");

        cleanupContainer(container);
    }

    // 5. Justify content test
    {
        FlexContainer* container = createTestContainer(2);
        container->justify = JUSTIFY_SPACE_EVENLY;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 130, 0.1, "Justify: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 470, 0.1, "Justify: Item 1 x");

        cleanupContainer(container);
    }

    // 6. Align items test
    {
        FlexContainer* container = createTestContainer(2);
        container->alignItems = ALIGN_CENTER;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.y, 250, 0.1, "Align: Item 0 y");
        cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "Align: Item 1 y");

        cleanupContainer(container);
    }

    // 7. Column direction test
    {
        FlexContainer* container = createTestContainer(2);
        container->direction = DIR_COLUMN;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Column: Item 0 x");
        cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "Column: Item 0 y");
        cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Column: Item 1 x");
        cr_assert_float_eq(container->items[1].positionCoords.y, 110, 0.1, "Column: Item 1 y");

        cleanupContainer(container);
    }

    // 8. Row reverse test
    {
        FlexContainer* container = createTestContainer(2);
        container->direction = DIR_ROW_REVERSE;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 410, 0.1, "RowReverse: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 200, 0.1, "RowReverse: Item 1 x");
        cr_assert_float_eq(container->items[0].positionCoords.y, 0, 0.1, "RowReverse: Item 0 y");

        cleanupContainer(container);
    }

    // 9. Absolute positioning test
    {
        FlexContainer* container = createTestContainer(3);
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_ABSOLUTE, .visibility = VIS_VISIBLE };
        container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Absolute: Item 0 x");
        cr_assert_float_eq(container->items[2].positionCoords.x, 210, 0.1, "Absolute: Item 2 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Absolute: Item 1 x unchanged");

        cleanupContainer(container);
    }

    // 10. Hidden visibility test
    {
        FlexContainer* container = createTestContainer(3);
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_HIDDEN };
        container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "Hidden: Item 0 x");
        cr_assert_float_eq(container->items[2].positionCoords.x, 210, 0.1, "Hidden: Item 2 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 0, 0.1, "Hidden: Item 1 x unchanged");

        cleanupContainer(container);
    }

    // 11. Flex basis test
    {
        FlexContainer* container = createTestContainer(2);
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].width, 300, 0.1, "FlexBasis: Item 0 width");
        cr_assert_float_eq(container->items[1].width, 400, 0.1, "FlexBasis: Item 1 width");
        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "FlexBasis: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 310, 0.1, "FlexBasis: Item 1 x");

        cleanupContainer(container);
    }

    // 12. Align self override test
    {
        FlexContainer* container = createTestContainer(2);
        container->alignItems = ALIGN_CENTER;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .alignSelf = ALIGN_END, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.y, 500, 0.1, "AlignSelf: Item 0 y");
        cr_assert_float_eq(container->items[1].positionCoords.y, 225, 0.1, "AlignSelf: Item 1 y");

        cleanupContainer(container);
    }

    // 13. Zero size container test
    {
        FlexContainer* container = createTestContainer(2);
        container->width = 0;
        container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
        container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

        layoutFlexContainer(container);

        cr_assert_float_eq(container->items[0].positionCoords.x, 0, 0.1, "ZeroSize: Item 0 x");
        cr_assert_float_eq(container->items[1].positionCoords.x, 210, 0.1, "ZeroSize: Item 1 x");

        cleanupContainer(container);
    }
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