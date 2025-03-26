#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/logging.h>
#include <stdlib.h>
#include "../layout_flex.c"

// Container setup helper
FlexContainer* create_test_container(int item_count) {
    FlexContainer* container = malloc(sizeof(FlexContainer));
    *container = (FlexContainer){
        .width = 800, .height = 600,
        .direction = DIR_ROW,
        .wrap = WRAP_NOWRAP,
        .justify = JUSTIFY_START,
        .align_items = ALIGN_START,
        .row_gap = 10,
        .column_gap = 10,
        .items = malloc(item_count * sizeof(FlexItem)),
        .item_count = item_count,
        .writing_mode = WM_HORIZONTAL_TB,
        .text_direction = TD_LTR
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
    container->align_items = ALIGN_CENTER;
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
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flex_grow = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flex_grow = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

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
    container->items[0] = (FlexItem){ .width = 300, .height = 100, .flex_shrink = 1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flex_shrink = 2, .position = POS_STATIC, .visibility = VIS_VISIBLE };

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
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flex_basis = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .flex_basis = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 300, "Item 0 width");
    cr_assert_eq(container->items[1].width, 400, "Item 1 width");
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 310, "Item 1 x");

    cleanup_container(container);
}

Test(flexbox_tests, flex_basis_auto) {
    FlexContainer* container = create_test_container(2);
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .flex_basis = -1, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 300, .height = 100, .flex_basis = -1, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].width, 200, "Item 0 width should match width with auto");
    cr_assert_eq(container->items[1].width, 300, "Item 1 width should match width with auto");
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[1].pos.x, 210, "Item 1 x");

    cleanup_container(container);
}

Test(flexbox_tests, align_self_override) {
    FlexContainer* container = create_test_container(2);
    container->align_items = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .align_self = ALIGN_END, .position = POS_STATIC, .visibility = VIS_VISIBLE };
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
        .min_width = 150, 
        .max_width = 250, 
        .flex_grow = 1, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[1] = (FlexItem){ 
        .width = 100, 
        .height = 100, 
        .min_width = 150, 
        .max_width = 200, 
        .flex_shrink = 1, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[2] = (FlexItem){ 
        .width = 300, 
        .height = 100, 
        .min_width = 200, 
        .max_width = 250, 
        .flex_grow = 1, 
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

Test(flexbox_tests, aspect_ratio) {
    FlexContainer* container = create_test_container(3);
    container->width = 600;
    container->height = 400;
    container->wrap = WRAP_NOWRAP;

    container->items[0] = (FlexItem){ .width = 200, .aspect_ratio = 2.0, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .height = 100, .aspect_ratio = 1.5, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 150, .height = 75, .aspect_ratio = 2.0, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].height, 100, "Item 0 height should respect aspect ratio");
    cr_assert_eq(container->items[1].width, 150, "Item 1 width should respect aspect ratio");
    cr_assert_eq(container->items[2].width, 150, "Item 2 width should remain unchanged");
    cr_assert_eq(container->items[2].height, 75, "Item 2 height should remain unchanged");

    cleanup_container(container);
}

Test(flexbox_tests, column_flex_direction) {
    FlexContainer* container = create_test_container(3);
    container->direction = DIR_COLUMN;
    container->height = 400;
    container->items[0] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 110, "Item 1 y");
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 220, "Item 2 y");

    cleanup_container(container);
}

Test(flexbox_tests, column_reverse_flex_direction) {
    FlexContainer* container = create_test_container(3);
    container->direction = DIR_COLUMN_REVERSE;
    container->height = 400;
    container->items[0] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 100, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 300, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 190, "Item 1 y"); // 300 - 100 - 10
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 80, "Item 2 y");  // 190 - 100 - 10

    cleanup_container(container);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    struct criterion_test_set *tests = criterion_initialize();
    int result = criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result ? 0 : 1;
}

// Test align-content: ALIGN_START (default, lines at top)
Test(flexbox_tests, align_content_start) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_START;  // Explicitly set to START (default)
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");    // Line 1 at top
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 110, "Item 1 y");  // Line 2 below Line 1
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 220, "Item 2 y");  // Line 3 below Line 2

    cleanup_container(container);
}

// Test align-content: ALIGN_END (lines at bottom)
Test(flexbox_tests, align_content_end) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_END;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // Total height of 3 lines + 2 gaps = 100 + 10 + 100 + 10 + 100 = 320
    // Free space = 600 - 320 = 280, so lines start at y=280
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 280, "Item 0 y");  // Line 1 at bottom - 2 lines
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 390, "Item 1 y");  // Line 2 above Line 1
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 500, "Item 2 y");  // Line 3 at bottom

    cleanup_container(container);
}

// Test align-content: ALIGN_CENTER (lines centered)
Test(flexbox_tests, align_content_center) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_CENTER;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // Total height = 320, free space = 280, half free space = 140
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 140, "Item 0 y");  // Line 1 centered
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 250, "Item 1 y");  // Line 2
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 360, "Item 2 y");  // Line 3

    cleanup_container(container);
}

// Test align-content: ALIGN_SPACE_BETWEEN (space distributed between lines)
Test(flexbox_tests, align_content_space_between) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_SPACE_BETWEEN;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // Total height = 320, free space = 280, 2 gaps = 280 / 2 = 140
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");    // First line at top
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 250, "Item 1 y");  // Middle line (100 + 10 + 140)
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 500, "Item 2 y");  // Last line at bottom

    cleanup_container(container);
}

// Test align-content: ALIGN_SPACE_AROUND (space distributed around lines)
Test(flexbox_tests, align_content_space_around) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_SPACE_AROUND;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 46, "Item 0 y");   // 46.7 rounded to 46
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 250, "Item 1 y");  // Matches output: 46.7 + 100 + 10 + 93.3 = 250
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 453, "Item 2 y");  // 453.3 rounded to 453

    cleanup_container(container);
}

// Test align-content: ALIGN_SPACE_EVENLY (space distributed evenly including edges)
Test(flexbox_tests, align_content_space_evenly) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_SPACE_EVENLY;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // Total height = 320, free space = 280, 4 spaces = 280 / 4 = 70
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 70, "Item 0 y");   // First space
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 250, "Item 1 y");  // 70 + 100 + 10 + 70
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 430, "Item 2 y");  // 250 + 10 + 100 + 70

    cleanup_container(container);
}

// Test align-content: ALIGN_STRETCH (lines stretch to fill container)
Test(flexbox_tests, align_content_stretch) {
    FlexContainer* container = create_test_container(3);
    container->wrap = WRAP_WRAP;
    container->width = 400;
    container->align_content = ALIGN_STRETCH;
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // Total original height = 320 (100 + 10 + 100 + 10 + 100), stretch factor = 600 / 320 = 1.875
    // Each line height = 100 * 1.875 = 187 (rounded), total = 187 * 3 + 10 * 2 = 581 (adjusted to fit)
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");
    cr_assert_eq(container->items[0].height, 187, "Item 0 height stretched");  // Approx
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x");
    cr_assert_eq(container->items[1].pos.y, 197, "Item 1 y");  // 187 + 10
    cr_assert_eq(container->items[1].height, 187, "Item 1 height stretched");
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 394, "Item 2 y");  // 197 + 187 + 10
    cr_assert_eq(container->items[2].height, 187, "Item 2 height stretched");

    cleanup_container(container);
}

// Test different row and column gaps
Test(flexbox_tests, different_row_column_gaps) {
    FlexContainer* container = create_test_container(4);
    container->row_gap = 20;     // Vertical gap between rows
    container->column_gap = 30;  // Horizontal gap between columns
    container->wrap = WRAP_WRAP;
    container->width = 450;
    
    // Create 4 items that will form 2 rows with 2 items each
    container->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[2] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    container->items[3] = (FlexItem){ .width = 200, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };

    layout_flex_container(container);

    // First row
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x");
    cr_assert_eq(container->items[0].pos.y, 0, "Item 0 y");
    cr_assert_eq(container->items[1].pos.x, 230, "Item 1 x");  // 0 + 200 + 30 (column_gap)
    cr_assert_eq(container->items[1].pos.y, 0, "Item 1 y");
    
    // Second row - should be positioned 20px (row_gap) below first row
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x");
    cr_assert_eq(container->items[2].pos.y, 120, "Item 2 y");  // 0 + 100 + 20 (row_gap)
    cr_assert_eq(container->items[3].pos.x, 230, "Item 3 x");  // 0 + 200 + 30 (column_gap)
    cr_assert_eq(container->items[3].pos.y, 120, "Item 3 y");  // 0 + 100 + 20 (row_gap)

    cleanup_container(container);
}

// Test for the order property
Test(flexbox_tests, item_order) {
    FlexContainer* container = create_test_container(4);
    
    // Set up 4 items with different order values that don't match their array indices
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 3,  // Will be displayed last
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 1,  // Will be displayed second
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 0,  // Will be displayed first (default value)
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[3] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 2,  // Will be displayed third
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that items are positioned in order of their order property, not their array index
    // Item 2 (order: 0) should be first
    cr_assert_eq(container->items[2].pos.x, 0, "Item with order 0 should be positioned first");
    
    // Item 1 (order: 1) should be second
    cr_assert_eq(container->items[1].pos.x, 110, "Item with order 1 should be positioned second");
    
    // Item 3 (order: 2) should be third
    cr_assert_eq(container->items[3].pos.x, 220, "Item with order 2 should be positioned third");
    
    // Item 0 (order: 3) should be fourth
    cr_assert_eq(container->items[0].pos.x, 330, "Item with order 3 should be positioned fourth");

    cleanup_container(container);
}

// Test for negative order values
Test(flexbox_tests, negative_order) {
    FlexContainer* container = create_test_container(3);
    
    // Set up 3 items with different order values including negative
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 0,    // Middle
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = -1,   // First (negative comes before 0)
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100, 
        .order = 1,    // Last
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that items are positioned in order of their order property
    // Item 1 (order: -1) should be first
    cr_assert_eq(container->items[1].pos.x, 0, "Item with order -1 should be positioned first");
    
    // Item 0 (order: 0) should be second
    cr_assert_eq(container->items[0].pos.x, 110, "Item with order 0 should be positioned second");
    
    // Item 2 (order: 1) should be third
    cr_assert_eq(container->items[2].pos.x, 220, "Item with order 1 should be positioned third");

    cleanup_container(container);
}

// Test percentage-based widths
Test(flexbox_tests, percentage_widths) {
    FlexContainer* container = create_test_container(2);
    container->width = 1000;
    container->height = 600;
    
    // Item with 50% width
    container->items[0] = (FlexItem){ 
        .width = 50, .is_width_percent = 1, 
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with 30% width
    container->items[1] = (FlexItem){ 
        .width = 30, .is_width_percent = 1, 
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that percentage widths are calculated correctly
    cr_assert_eq(container->items[0].width, 500, "Item 0 width should be 50% of container (500px)");
    cr_assert_eq(container->items[1].width, 300, "Item 1 width should be 30% of container (300px)");
    
    // Check positions
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x position");
    cr_assert_eq(container->items[1].pos.x, 510, "Item 1 x position"); // 500 + 10(gap)

    cleanup_container(container);
}

// Test percentage-based heights
Test(flexbox_tests, percentage_heights) {
    FlexContainer* container = create_test_container(2);
    container->width = 800;
    container->height = 600;
    
    // Items with percentage heights
    container->items[0] = (FlexItem){ 
        .width = 200, 
        .height = 50, .is_height_percent = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    container->items[1] = (FlexItem){ 
        .width = 200, 
        .height = 25, .is_height_percent = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that percentage heights are calculated correctly
    cr_assert_eq(container->items[0].height, 300, "Item 0 height should be 50% of container (300px)");
    cr_assert_eq(container->items[1].height, 150, "Item 1 height should be 25% of container (150px)");

    cleanup_container(container);
}

// Test percentage-based flex-basis
Test(flexbox_tests, percentage_flex_basis) {
    FlexContainer* container = create_test_container(2);
    container->width = 1000;
    container->height = 600;
    
    // Items with percentage flex-basis
    container->items[0] = (FlexItem){ 
        .width = 100, 
        .flex_basis = 40, .is_flex_basis_percent = 1,
        .height = 100, 
        .flex_grow = 0,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    container->items[1] = (FlexItem){ 
        .width = 100, 
        .flex_basis = 20, .is_flex_basis_percent = 1,
        .height = 100, 
        .flex_grow = 0,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that percentage flex-basis values are calculated correctly
    cr_assert_eq(container->items[0].width, 400, "Item 0 width should be based on 40% flex-basis (400px)");
    cr_assert_eq(container->items[1].width, 200, "Item 1 width should be based on 20% flex-basis (200px)");
    
    // Check positions
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x position");
    cr_assert_eq(container->items[1].pos.x, 410, "Item 1 x position"); // 400 + 10(gap)

    cleanup_container(container);
}

// Test percentage-based min/max constraints
Test(flexbox_tests, percentage_constraints) {
    FlexContainer* container = create_test_container(2);
    container->width = 1000;
    container->height = 600;
    
    // Item with percentage min/max width constraints
    container->items[0] = (FlexItem){ 
        .width = 200, 
        .min_width = 30, .is_min_width_percent = 1,  // 30% = 300px
        .max_width = 40, .is_max_width_percent = 1,  // 40% = 400px
        .flex_grow = 1,
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with mixed constraints
    container->items[1] = (FlexItem){ 
        .width = 100, 
        .min_width = 150,  // absolute pixels
        .max_width = 20, .is_max_width_percent = 1,  // 20% = 200px
        .flex_grow = 1,
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check that constraints are applied correctly
    cr_assert_eq(container->items[0].width, 400, "Item 0 width should be constrained to 40% max (400px)");
    cr_assert_eq(container->items[1].width, 200, "Item 1 width should be constrained to 20% max (200px)");

    cleanup_container(container);
}

// Test mixture of percentage and absolute items
Test(flexbox_tests, mixed_percentage_absolute) {
    FlexContainer* container = create_test_container(3);
    container->width = 1000;
    container->height = 600;
    container->column_gap = 20; // Larger gap for clarity
    
    // Absolute width
    container->items[0] = (FlexItem){ 
        .width = 200, 
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Percentage width
    container->items[1] = (FlexItem){ 
        .width = 30, .is_width_percent = 1,  // 30% = 300px
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Percentage flex-basis
    container->items[2] = (FlexItem){ 
        .width = 100, 
        .flex_basis = 25, .is_flex_basis_percent = 1,  // 25% = 250px
        .height = 100, 
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Check widths
    cr_assert_eq(container->items[0].width, 200, "Item 0 should keep absolute width (200px)");
    cr_assert_eq(container->items[1].width, 300, "Item 1 should be 30% of container (300px)");
    cr_assert_eq(container->items[2].width, 250, "Item 2 should have flex-basis of 25% (250px)");
    
    // Check positions
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x position");
    cr_assert_eq(container->items[1].pos.x, 220, "Item 1 x position"); // 200 + 20(gap)
    cr_assert_eq(container->items[2].pos.x, 540, "Item 2 x position"); // 220 + 300 + 20(gap)

    cleanup_container(container);
}

// Test baseline alignment
Test(flexbox_tests, baseline_alignment) {
    FlexContainer* container = create_test_container(3);
    container->align_items = ALIGN_BASELINE;
    
    // Item with default baseline (3/4 of height)
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 80,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE,
        // Default baseline will be at 60px (3/4 of height)
    };
    
    // Item with explicit baseline offset
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 120,
        .baseline_offset = 100,  // Baseline is near the bottom
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with another baseline offset
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 160,
        .baseline_offset = 40,  // Baseline is near the top
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);
    
    // The maximum baseline is at 100px from the top (from item 1)
    // Each item should be positioned so its baseline is at this position
    
    // Item 0: baseline at 60px, should be positioned at y = 100 - 60 = 40
    cr_assert_eq(container->items[0].pos.y, 40, "Item 0 should be positioned to align baseline");
    
    // Item 1: baseline at 100px, should be positioned at y = 100 - 100 = 0
    cr_assert_eq(container->items[1].pos.y, 0, "Item 1 should be positioned to align baseline");
    
    // Item 2: baseline at 40px, should be positioned at y = 100 - 40 = 60
    cr_assert_eq(container->items[2].pos.y, 60, "Item 2 should be positioned to align baseline");

    cleanup_container(container);
}

// Test baseline alignment with align-self override
Test(flexbox_tests, baseline_align_self) {
    FlexContainer* container = create_test_container(3);
    container->align_items = ALIGN_START; // Default alignment
    
    // Item with baseline alignment
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 80,
        .align_self = ALIGN_BASELINE,
        .baseline_offset = 60,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE
    };
    
    // Another item with baseline alignment
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 120,
        .align_self = ALIGN_BASELINE,
        .baseline_offset = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with default alignment (start)
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 160,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);
    
    // The maximum baseline among baseline-aligned items is 100px
    
    // Item 0: baseline alignment with offset 60px
    cr_assert_eq(container->items[0].pos.y, 40, "Item 0 should align its baseline");
    
    // Item 1: baseline alignment with offset 100px
    cr_assert_eq(container->items[1].pos.y, 0, "Item 1 should align its baseline");
    
    // Item 2: start alignment (not baseline)
    cr_assert_eq(container->items[2].pos.y, 0, "Item 2 should use start alignment");

    cleanup_container(container);
}

// Test baseline alignment in column direction
Test(flexbox_tests, baseline_column_direction) {
    FlexContainer* container = create_test_container(3);
    container->direction = DIR_COLUMN;
    container->align_items = ALIGN_BASELINE;
    
    // In column direction, baseline alignment is equivalent to start alignment
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 80,
        .baseline_offset = 60,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE
    };
    
    container->items[1] = (FlexItem){ 
        .width = 150, .height = 80,
        .baseline_offset = 60,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    container->items[2] = (FlexItem){ 
        .width = 200, .height = 80,
        .baseline_offset = 60,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);
    
    // In column direction, all items should be positioned at x=0 (start)
    cr_assert_eq(container->items[0].pos.x, 0, "Item 0 x position with baseline in column");
    cr_assert_eq(container->items[1].pos.x, 0, "Item 1 x position with baseline in column");
    cr_assert_eq(container->items[2].pos.x, 0, "Item 2 x position with baseline in column");

    cleanup_container(container);
}

// Test auto margins in main axis (horizontal)
Test(flexbox_tests, auto_margins_main) {
    FlexContainer* container = create_test_container(3);
    container->width = 800;
    
    // First item with auto margin on right - pushes subsequent items to the end
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_right_auto = 1,  // Auto margin right
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Standard item
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Standard item
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Free space = 800 - (100 + 10 + 100 + 10 + 100) = 800 - 320 = 480
    // All 480px should go to the auto margin
    
    cr_assert_eq(container->items[0].pos.x, 0, "First item should start at x=0");
    cr_assert_eq(container->items[1].pos.x, 590, "Second item should be pushed to the end minus width");
    cr_assert_eq(container->items[2].pos.x, 700, "Third item should be at the end");

    cleanup_container(container);
}

// Test auto margins for centering
Test(flexbox_tests, auto_margins_center) {
    FlexContainer* container = create_test_container(1);
    container->width = 800;
    
    // Item with auto margins on both sides should be centered
    container->items[0] = (FlexItem){ 
        .width = 200, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_left_auto = 1,
        .is_margin_right_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Free space = 800 - 200 = 600px, split equally between left and right margins (300px each)
    cr_assert_eq(container->items[0].pos.x, 300, "Item should be centered");

    cleanup_container(container);
}

// Test auto margins in cross axis (vertical)
Test(flexbox_tests, auto_margins_cross) {
    FlexContainer* container = create_test_container(3);
    container->width = 600;
    container->height = 400;
    
    // Standard item
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with auto margin on top - pushes to bottom
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_top_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Item with auto margins on top and bottom - centers vertically
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_top_auto = 1,
        .is_margin_bottom_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Cross axis free space for each item = 400 - 100 = 300px
    cr_assert_eq(container->items[0].pos.y, 0, "First item should be at top (default)");
    cr_assert_eq(container->items[1].pos.y, 300, "Second item should be pushed to bottom");
    cr_assert_eq(container->items[2].pos.y, 150, "Third item should be centered vertically");

    cleanup_container(container);
}

// Test auto margins with multiple items in line
Test(flexbox_tests, auto_margins_multiple) {
    FlexContainer* container = create_test_container(4);
    container->width = 800;
    
    // First item
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Auto margin right - creates space between items 0 and 2
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_right_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Third item
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Fourth item with auto margins on both sides - creates equal space around
    container->items[3] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_left_auto = 1,
        .is_margin_right_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Free space = 800 - (100 + 10 + 100 + 10 + 100 + 10 + 100) = 370px
    // With 3 auto margins, each gets 370/3 ≈ 123px
    
    cr_assert_eq(container->items[0].pos.x, 0, "First item at start");
    cr_assert_eq(container->items[1].pos.x, 110, "Second item after first + gap");
    cr_assert_eq(container->items[2].pos.x, 343, "Third item after second + auto margin + gap"); // 110 + 100 + 123 + 10
    cr_assert_eq(container->items[3].pos.x, 577, "Fourth item centered in remaining space"); // 343 + 100 + 10 + 124

    cleanup_container(container);
}

// Test auto margins overriding justify-content
Test(flexbox_tests, auto_margins_override_justify) {
    FlexContainer* container = create_test_container(3);
    container->width = 800;
    container->justify = JUSTIFY_CENTER; // This would normally center all items
    
    // First item with auto margin on right - should override justify-content
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_right_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Regular items
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    container->items[2] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // Even though justify-content is center, the auto margin should take precedence
    cr_assert_eq(container->items[0].pos.x, 0, "First item at start");
    cr_assert_eq(container->items[1].pos.x, 590, "Second item after auto margin");
    cr_assert_eq(container->items[2].pos.x, 700, "Third item after second + gap");

    cleanup_container(container);
}

// Test auto margins overriding align-items
Test(flexbox_tests, auto_margins_override_align) {
    FlexContainer* container = create_test_container(2);
    container->width = 400;
    container->height = 400;
    container->align_items = ALIGN_CENTER; // This would normally center items vertically
    
    // First item with default align-center
    container->items[0] = (FlexItem){ 
        .width = 100, .height = 100,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };
    
    // Second item with auto margin on top - should override align-items
    container->items[1] = (FlexItem){ 
        .width = 100, .height = 100,
        .margin = {0, 0, 0, 0},
        .is_margin_top_auto = 1,
        .position = POS_STATIC, 
        .visibility = VIS_VISIBLE 
    };

    layout_flex_container(container);

    // First item should be centered (per align-items)
    cr_assert_eq(container->items[0].pos.y, 150, "First item should be centered");
    
    // Second item should be pushed to bottom (auto margin overrides align-items)
    cr_assert_eq(container->items[1].pos.y, 300, "Second item should be pushed to bottom");

    cleanup_container(container);
}

// Test deep nesting (3 levels)
Test(flexbox_tests, deep_nested_containers) {
    // Level 1 (grandparent)
    FlexContainer* grandparent = create_test_container(1);
    grandparent->width = 1000;
    grandparent->height = 800;
    grandparent->direction = DIR_COLUMN;
    
    // Level 2 (parent)
    FlexContainer* parent = create_test_container(2);
    parent->width = 800;
    parent->height = 500;
    parent->direction = DIR_ROW;
    
    // Level 3 (child)
    FlexContainer* child = create_test_container(2);
    child->width = 400;
    child->height = 300;
    child->direction = DIR_COLUMN;
    
    // Set up the child container items
    child->items[0] = (FlexItem){ .width = 300, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    child->items[1] = (FlexItem){ .width = 300, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Set up parent items - first is normal, second will contain the child container
    parent->items[0] = (FlexItem){ .width = 350, .height = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    parent->items[1] = (FlexItem){ .width = 400, .height = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Grandparent contains the parent
    grandparent->items[0] = (FlexItem){ .width = 800, .height = 500, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Layout from inside out
    layout_flex_container(child);
    layout_flex_container(parent);
    layout_flex_container(grandparent);
    
    // Child items should be positioned relative to the child container
    cr_assert_eq(child->items[0].pos.x, 0, "Child item 0 x");
    cr_assert_eq(child->items[0].pos.y, 0, "Child item 0 y");
    cr_assert_eq(child->items[1].pos.x, 0, "Child item 1 x");
    cr_assert_eq(child->items[1].pos.y, 110, "Child item 1 y"); // 100 + 10(gap)
    
    // Parent items positions
    cr_assert_eq(parent->items[0].pos.x, 0, "Parent item 0 x");
    cr_assert_eq(parent->items[0].pos.y, 0, "Parent item 0 y");
    cr_assert_eq(parent->items[1].pos.x, 360, "Parent item 1 x"); // 350 + 10(gap)
    cr_assert_eq(parent->items[1].pos.y, 0, "Parent item 1 y");
    
    // Grandparent item position
    cr_assert_eq(grandparent->items[0].pos.x, 0, "Grandparent item x");
    cr_assert_eq(grandparent->items[0].pos.y, 0, "Grandparent item y");
    
    cleanup_container(child);
    cleanup_container(parent);
    cleanup_container(grandparent);
}

// Test nested containers with different flex directions
Test(flexbox_tests, nested_different_directions) {
    // Outer container with row direction
    FlexContainer* outer = create_test_container(2);
    outer->width = 900;
    outer->height = 700;
    outer->direction = DIR_ROW;
    
    // Inner container with column direction
    FlexContainer* inner = create_test_container(3);
    inner->width = 400;
    inner->height = 600;
    inner->direction = DIR_COLUMN;
    
    // Set up inner items
    inner->items[0] = (FlexItem){ .width = 300, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 300, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[2] = (FlexItem){ .width = 300, .height = 150, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Set up outer items - second will be the inner container position
    outer->items[0] = (FlexItem){ .width = 350, .height = 500, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    outer->items[1] = (FlexItem){ .width = 400, .height = 600, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    layout_flex_container(inner);
    layout_flex_container(outer);
    
    // Check inner items are laid out in column
    cr_assert_eq(inner->items[0].pos.x, 0, "Inner item 0 x");
    cr_assert_eq(inner->items[0].pos.y, 0, "Inner item 0 y");
    cr_assert_eq(inner->items[1].pos.x, 0, "Inner item 1 x");
    cr_assert_eq(inner->items[1].pos.y, 160, "Inner item 1 y"); // 150 + 10(gap)
    cr_assert_eq(inner->items[2].pos.x, 0, "Inner item 2 x");
    cr_assert_eq(inner->items[2].pos.y, 320, "Inner item 2 y"); // 160 + 150 + 10(gap)
    
    // Check outer items are laid out in row
    cr_assert_eq(outer->items[0].pos.x, 0, "Outer item 0 x");
    cr_assert_eq(outer->items[0].pos.y, 0, "Outer item 0 y");
    cr_assert_eq(outer->items[1].pos.x, 360, "Outer item 1 x"); // 350 + 10(gap)
    cr_assert_eq(outer->items[1].pos.y, 0, "Outer item 1 y");
    
    cleanup_container(inner);
    cleanup_container(outer);
}

// Test nested containers with wrapping
Test(flexbox_tests, nested_with_wrapping) {
    // Outer container with wrapping
    FlexContainer* outer = create_test_container(3);
    outer->width = 500;
    outer->height = 800;
    outer->wrap = WRAP_WRAP;
    
    // Inner container (will be the middle item)
    FlexContainer* inner = create_test_container(2);
    inner->width = 250;
    inner->height = 300;
    inner->wrap = WRAP_WRAP;
    
    // Inner container items that will wrap
    inner->items[0] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Outer container items - middle one will be positioned after layout
    outer->items[0] = (FlexItem){ .width = 400, .height = 200, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    outer->items[1] = (FlexItem){ .width = 250, .height = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    outer->items[2] = (FlexItem){ .width = 400, .height = 200, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    layout_flex_container(inner);
    layout_flex_container(outer);
    
    // Inner items should wrap within inner container
    cr_assert_eq(inner->items[0].pos.x, 0, "Inner item 0 x");
    cr_assert_eq(inner->items[0].pos.y, 0, "Inner item 0 y");
    cr_assert_eq(inner->items[1].pos.x, 0, "Inner item 1 x");
    cr_assert_eq(inner->items[1].pos.y, 110, "Inner item 1 y"); // Wrapped to next line
    
    // Outer items should wrap too (item 2 on second line)
    cr_assert_eq(outer->items[0].pos.x, 0, "Outer item 0 x");
    cr_assert_eq(outer->items[0].pos.y, 0, "Outer item 0 y");
    cr_assert_eq(outer->items[1].pos.x, 0, "Outer item 1 x");
    cr_assert_eq(outer->items[1].pos.y, 210, "Outer item 1 y"); // 200 + 10(gap)
    cr_assert_eq(outer->items[2].pos.x, 0, "Outer item 2 x");
    cr_assert_eq(outer->items[2].pos.y, 520, "Outer item 2 y"); // 210 + 300 + 10(gap)
    
    cleanup_container(inner);
    cleanup_container(outer);
}

// Test nested containers with absolute positioning
Test(flexbox_tests, nested_with_absolute) {
    // Outer container
    FlexContainer* outer = create_test_container(2);
    outer->width = 1000;
    outer->height = 800;
    
    // Inner container
    FlexContainer* inner = create_test_container(3);
    inner->width = 600;
    inner->height = 400;
    
    // Inner items - mix of static and absolute
    inner->items[0] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 200, .height = 100, .position = POS_ABSOLUTE, .visibility = VIS_VISIBLE};
    inner->items[1].pos = (Point){.x = 50, .y = 50}; // Absolute positioned
    inner->items[2] = (FlexItem){ .width = 200, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Outer items
    outer->items[0] = (FlexItem){ .width = 300, .height = 300, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    outer->items[1] = (FlexItem){ .width = 600, .height = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    layout_flex_container(inner);
    layout_flex_container(outer);
    
    // Check static items in inner container
    cr_assert_eq(inner->items[0].pos.x, 0, "Inner item 0 x");
    cr_assert_eq(inner->items[0].pos.y, 0, "Inner item 0 y");
    cr_assert_eq(inner->items[2].pos.x, 210, "Inner item 2 x"); // 0 + 200 + 10(gap)
    cr_assert_eq(inner->items[2].pos.y, 0, "Inner item 2 y");
    
    // Check absolute item in inner container
    cr_assert_eq(inner->items[1].pos.x, 50, "Inner absolute item x"); // Set by left property
    cr_assert_eq(inner->items[1].pos.y, 50, "Inner absolute item y"); // Set by top property
    
    // Check outer items
    cr_assert_eq(outer->items[0].pos.x, 0, "Outer item 0 x");
    cr_assert_eq(outer->items[0].pos.y, 0, "Outer item 0 y");
    cr_assert_eq(outer->items[1].pos.x, 310, "Outer item 1 x"); // 300 + 10(gap)
    cr_assert_eq(outer->items[1].pos.y, 0, "Outer item 1 y");
    
    cleanup_container(inner);
    cleanup_container(outer);
}

// Test nested containers with coordinate transformations
Test(flexbox_tests, nested_coordinate_transform) {
    // Outer container
    FlexContainer* outer = create_test_container(2);
    outer->width = 1000;
    outer->height = 700;
    
    // Inner container
    FlexContainer* inner = create_test_container(2);
    inner->width = 500;
    inner->height = 400;
    
    // Inner items
    inner->items[0] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    inner->items[1] = (FlexItem){ .width = 150, .height = 100, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Outer items - second one will be inner container
    outer->items[0] = (FlexItem){ .width = 300, .height = 500, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    outer->items[1] = (FlexItem){ .width = 500, .height = 400, .position = POS_STATIC, .visibility = VIS_VISIBLE };
    
    // Layout the containers
    layout_flex_container(inner);
    layout_flex_container(outer);
    
    // Get the absolute coordinates of the inner items
    int inner_container_x = outer->items[1].pos.x;
    int inner_container_y = outer->items[1].pos.y;
    
    int item0_absolute_x = inner_container_x + inner->items[0].pos.x;
    int item0_absolute_y = inner_container_y + inner->items[0].pos.y;
    int item1_absolute_x = inner_container_x + inner->items[1].pos.x;
    int item1_absolute_y = inner_container_y + inner->items[1].pos.y;
    
    // Check inner items relative positions
    cr_assert_eq(inner->items[0].pos.x, 0, "Inner item 0 relative x");
    cr_assert_eq(inner->items[0].pos.y, 0, "Inner item 0 relative y");
    cr_assert_eq(inner->items[1].pos.x, 160, "Inner item 1 relative x");
    cr_assert_eq(inner->items[1].pos.y, 0, "Inner item 1 relative y");
    
    // Check outer container position
    cr_assert_eq(outer->items[1].pos.x, 310, "Inner container x in outer");
    cr_assert_eq(outer->items[1].pos.y, 0, "Inner container y in outer");
    
    // Check absolute coordinates calculations
    cr_assert_eq(item0_absolute_x, 310, "Inner item 0 absolute x");
    cr_assert_eq(item0_absolute_y, 0, "Inner item 0 absolute y");
    cr_assert_eq(item1_absolute_x, 470, "Inner item 1 absolute x");
    cr_assert_eq(item1_absolute_y, 0, "Inner item 1 absolute y");
    
    cleanup_container(inner);
    cleanup_container(outer);
}

// zig cc -o test_layout_flex.exe test_layout_flex.c -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib
// ./test_layout_flex.exe --verbose