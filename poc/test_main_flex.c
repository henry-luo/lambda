#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../lib/strbuf.h"
#include "flexbox.h"

// Helper to cleanup FlexNode tree
static void cleanup() {
}

// Test Suite
TestSuite(FlexboxEnhanced);

FlexNode* parse_html_css(const char* html, const char* css) {
    StrBuf* html_buf = strbuf_new_cap(1024);
    strbuf_append_str(html_buf, "<html><head><style>");
    strbuf_append_str(html_buf, css);
    strbuf_append_str(html_buf, "</style></head><body>");
    strbuf_append_str(html_buf, html);
    strbuf_append_str(html_buf, "</body></html>");

    FlexNode* root = parseHTMLandCSS(html_buf->s);
    strbuf_free(html_buf);
    return root;
}

// Test basic row layout with flex-start
Test(FlexboxEnhanced, Row_FlexStart) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; flex-direction: row; justify-content: flex-start; align-items: flex-start; }"
                      ".item { flex-basis: 100px; height: 50px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root, "Failed to parse HTML/CSS");

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 2, "Incorrect number of children");
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];

    cr_expect_eq(item1->position_main, 0, "Item 1 main position incorrect");
    cr_expect_eq(item1->main_size, 100, "Item 1 main size incorrect");
    cr_expect_eq(item1->position_cross, 0, "Item 1 cross position incorrect");
    cr_expect_eq(item1->cross_size, 50, "Item 1 cross size incorrect");

    cr_expect_eq(item2->position_main, 100, "Item 2 main position incorrect");
    cr_expect_eq(item2->main_size, 100, "Item 2 main size incorrect");

    destroyFlexNode(root);
}

// Test row with flex-grow
Test(FlexboxEnhanced, Row_FlexGrow) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; flex-direction: row; justify-content: flex-start; align-items: center; }"
                      ".item { flex-basis: 100px; flex-grow: 1; height: 50px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 2);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];

    cr_expect_eq(item1->main_size, 250, "Item 1 main size incorrect"); // 100 + (500-200)/2
    cr_expect_eq(item1->position_cross, 125, "Item 1 cross position incorrect"); // (300-50)/2
    cr_expect_eq(item2->position_main, 250, "Item 2 main position incorrect");
    cr_expect_eq(item2->main_size, 250, "Item 2 main size incorrect");

    destroyFlexNode(root);
}

// Test column with space-around
Test(FlexboxEnhanced, Column_SpaceAround) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div></div>";
    const char* css = ".container { display: flex; width: 300px; height: 500px; flex-direction: column; justify-content: space-around; align-items: stretch; }"
                      ".item { flex-basis: 100px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 2);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];

    cr_expect_eq(item1->position_main, 75, "Item 1 main position incorrect"); // (500-200)/(2*2)
    cr_expect_eq(item1->main_size, 100, "Item 1 main size incorrect");
    cr_expect_eq(item1->cross_size, 300, "Item 1 cross size incorrect"); // Stretched to container width

    cr_expect_eq(item2->position_main, 325, "Item 2 main position incorrect");

    destroyFlexNode(root);
}

// Test row-reverse with flex-end
Test(FlexboxEnhanced, RowReverse_FlexEnd) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; flex-direction: row-reverse; justify-content: flex-end; align-items: flex-start; }"
                      ".item { flex-basis: 100px; height: 50px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 2);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];

    cr_expect_eq(item2->position_main, 300, "Item 2 main position incorrect"); // Reverse order, 500-200
    cr_expect_eq(item1->position_main, 400, "Item 1 main position incorrect");
    cr_expect_eq(item1->main_size, 100, "Item 1 main size incorrect");
    cr_expect_eq(item1->cross_size, 50, "Item 1 cross size incorrect");

    destroyFlexNode(root);
}

// Test wrap with space-between
Test(FlexboxEnhanced, Wrap_SpaceBetween) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div><div class=\"item\">Item 3</div></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; flex-direction: row; justify-content: flex-start; align-items: center; flex-wrap: wrap; align-content: space-between; }"
                      ".item { flex-basis: 300px; height: 50px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 3);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];
    FlexNode* item3 = root->children[2];

    cr_expect_eq(item1->position_main, 0, "Item 1 main position incorrect");
    cr_expect_eq(item1->position_cross, 0, "Item 1 cross position incorrect"); // Space-between
    cr_expect_eq(item1->main_size, 300, "Item 1 main size incorrect");
    cr_expect_eq(item1->cross_size, 50, "Item 1 cross size incorrect");

    cr_expect_eq(item2->position_cross, 125, "Item 2 cross position incorrect"); // (300-150)/2 between lines
    cr_expect_eq(item3->position_cross, 250, "Item 3 cross position incorrect");

    destroyFlexNode(root);
}

// Test wrap-reverse with stretch
Test(FlexboxEnhanced, WrapReverse_Stretch) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div><div class=\"item\">Item 3</div></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; flex-direction: row; justify-content: center; align-items: stretch; flex-wrap: wrap-reverse; align-content: stretch; }"
                      ".item { flex-basis: 200px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 3);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];
    FlexNode* item3 = root->children[2];

    cr_expect_eq(item1->position_main, 150, "Item 1 main position incorrect"); // Centered in line
    cr_expect_eq(item2->position_main, 150, "Item 2 main position incorrect");
    cr_expect_eq(item1->position_cross, 150, "Item 1 cross position incorrect"); // Reverse stretch
    cr_expect_eq(item2->position_cross, 150, "Item 2 cross position incorrect");
    cr_expect_eq(item3->position_cross, 0, "Item 3 cross position incorrect");
    cr_expect_eq(item1->cross_size, 150, "Item 1 cross size incorrect"); // 300/2

    destroyFlexNode(root);
}

// Test flex-shrink
Test(FlexboxEnhanced, FlexShrink) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item 1</div><div class=\"item\">Item 2</div></div>";
    const char* css = ".container { display: flex; width: 300px; height: 300px; flex-direction: row; justify-content: flex-start; align-items: center; }"
                      ".item { flex-basis: 200px; flex-shrink: 1; height: 50px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_assert_eq(root->num_children, 2);
    FlexNode* item1 = root->children[0];
    FlexNode* item2 = root->children[1];

    cr_expect_eq(item1->main_size, 150, "Item 1 main size incorrect"); // (400-300)/2 shrunk equally
    cr_expect_eq(item2->main_size, 150, "Item 2 main size incorrect");
    cr_expect_eq(item2->position_main, 150, "Item 2 main position incorrect");

    destroyFlexNode(root);
}

// Test invalid CSS input
Test(FlexboxEnhanced, InvalidCSS) {
    const char* html = "<div class=\"container\"><div class=\"item\">Item</div></div>";
    const char* css = ".container { display: invalid; width: 500px; height: 300px; }"; // Invalid display

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_expect_eq(root->num_children, 1, "Child should still be parsed");
    cr_expect_eq(root->children[0]->main_size, 0, "Item main size should be 0 with no flex");

    destroyFlexNode(root);
}

// Test empty container
Test(FlexboxEnhanced, EmptyContainer) {
    const char* html = "<div class=\"container\"></div>";
    const char* css = ".container { display: flex; width: 500px; height: 300px; }";

    FlexNode* root = parse_html_css(html, css);
    cr_assert_not_null(root);

    calculateFlexLayout(root, NULL);

    cr_expect_eq(root->num_children, 0, "No children expected");

    destroyFlexNode(root);
}

// clang -o test_flex.exe test_main_flex.c layout_flex.c ../lib/strbuf.c -I/usr/local/include/lexbor -L/usr/local/lib -llexbor -lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I./lib -rpath /usr/local/lib