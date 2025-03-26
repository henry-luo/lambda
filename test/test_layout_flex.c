#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/logging.h>
#include <stdlib.h>
#include "../layout_flex.c"

// Container setup helper
FlexContainer* create_test_container(int itemCount) {
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
        .writingMode = WM_HORIZONTAL_TB,
        .textDirection = TD_LTR
    };
    return container;
}

// Container cleanup helper
void cleanup_container(FlexContainer* container) {
    free(container->items);
    free(container);
}

// Test suite definition
TestSuite(flexbox_tests, .description = "Flexbox layout tests");

// Individual test cases
Test(flexbox_tests, basic_layout) {
    FlexContainer* container = create_test_container(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 210, "Item 1 x");
    cr_assert_eq(container->items[2].pos.x, 420, "Item 2 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");

    cleanup_container(container);
}

Test(flexbox_tests, wrap) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 110, "Item 1 y");
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 220, "Item 2 y");

    cleanup_container(container);
}

Test(flexbox_tests, align_items) {
    FlexContainer* container = create_test_container(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.y, 250, "Item 0 y");
    cr_assert_eq(container->items[1].pos.y, 225, "Item 1 y");

    cleanup_container(container);
}

Test(flexbox_tests, column_direction) {
    FlexContainer* container = create_test_container(2);
    container->direction = DIR_COLUMN;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 110, "Item 1 y");

    cleanup_container(container);
}

Test(flexbox_tests, flex_grow) {
    FlexContainer* container = create_test_container(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 330, "Item 0 width");  // Corrected from 333
    cr_assert_eq(container->items[1].width, 460, "Item 1 width");  // Corrected from 467
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 340, "Item 1 x");  // Corrected from 343

    cleanup_container(container);
}

Test(flexbox_tests, flex_shrink) {
    FlexContainer* container = create_test_container(2);
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexShrink = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 230, "Item 0 width");  // Correct per spec
    cr_assert_eq(container->items[1].width, 160, "Item 1 width");  // Correct per spec
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 240, "Item 1 x");  // 230 + 10

    cleanup_container(container);
}

Test(flexbox_tests, justify_content) {
    FlexContainer* container = create_test_container(2);
    container->justify = JUSTIFY_SPACE_EVENLY;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 130, "Item 0 x");  // Correct per spec
    cr_assert_eq(container->items[1].pos.x, 470, "Item 1 x");  // Correct per spec

    cleanup_container(container);
}

Test(flexbox_tests, row_reverse) {
    FlexContainer* container = create_test_container(2);
    container->direction = DIR_ROW_REVERSE;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 390, "Item 0 x");  // Correct per spec
    cr_assert_eq(container->items[1].pos.x, 600, "Item 1 x");  // Correct per spec
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");

    cleanup_container(container);
}

Test(flexbox_tests, absolute_positioning) {
    FlexContainer* container = create_test_container(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_ABSOLUTE, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[2].pos.x, 210, "Item 2 x");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x unchanged");

    cleanup_container(container);
}

Test(flexbox_tests, hidden_visibility) {
    FlexContainer* container = create_test_container(3);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_HIDDEN };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[2].pos.x, 210, "Item 2 x");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x unchanged");

    cleanup_container(container);
}

Test(flexbox_tests, flex_basis) {
    FlexContainer* container = create_test_container(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flexBasis = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 300, "Item 0 width");
    cr_assert_eq(container->items[1].width, 400, "Item 1 width");
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 310, "Item 1 x");

    cleanup_container(container);
}

Test(flexbox_tests, flex_basis_auto) {
    FlexContainer* container = create_test_container(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flexBasis = -1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flexBasis = -1, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 200, "Item 0 width should match width with auto");
    cr_assert_eq(container->items[1].width, 300, "Item 1 width should match width with auto");
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 210, "Item 1 x");

    cleanup_container(container);
}

Test(flexbox_tests, align_self_override) {
    FlexContainer* container = create_test_container(2);
    container->alignItems = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .alignSelf = ALIGN_END, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.y, 500, "Item 0 y");
    cr_assert_eq(container->items[1].pos.y, 225, "Item 1 y");

    cleanup_container(container);
}

Test(flexbox_tests, zero_size_container) {
    FlexContainer* container = create_test_container(2);
    container->width = 0;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");

    cleanup_container(container);
}

Test(flexbox_tests, min_max_constraints) {
    FlexContainer* container = create_test_container(3);
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

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 250, "Item 0 width should be at max");
    cr_assert_eq(container->items[1].width, 150, "Item 1 width should be at min");
    cr_assert_eq(container->items[2].width, 250, "Item 2 width should be at max");
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 260, "Item 1 x");
    cr_assert_eq(container->items[2].pos.x, 420, "Item 2 x");

    cleanup_container(container);
}

Test(flexbox_tests, wrap_reverse) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP_REVERSE;
    container->width = 400;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 500, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 390, "Item 1 y");
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 280, "Item 2 y");

    cleanup_container(container);
}

Test(flexbox_tests, nested_containers) {
    FlexContainer* outer = create_test_container(1);
    outer->direction = DIR_COLUMN;
    
    FlexContainer* inner = create_test_container(2);
    inner->width = 400;
    inner->height = 200;
    inner->items[0] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    outer->items[0] = (FlexItem){
        .width = 400,
        .height = 200,
        .position = POS_STATIC,
        .visibility = VIS_VISIBLE
    };

    layout_flex_container(inner);
    layout_flex_container(outer);

    cr_assert_eq(inner->items[0].pos.x, 0, "Inner item 0 x");
    cr_assert_eq(inner->items[0].pos.y, 0, "Inner item 0 y");
    cr_assert_eq(inner->items[1].pos.x, 160, "Inner item 1 x");
    cr_assert_eq(inner->items[1].pos.y, 0, "Inner item 1 y");
    cr_assert_eq(outer->items[0].pos.x, 0, "Outer item 0 x");
    cr_assert_eq(outer->items[0].pos.y, 0, "Outer item 0 y");

    cleanup_container(inner);
    cleanup_container(outer);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    struct criterion_test_set *tests = criterion_initialize();
    int result = criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result ? 0 : 1;
}

// zig cc -o test_layout_flex.exe test_layout_flex.c -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib
// ./test_layout_flex.exe --verbose