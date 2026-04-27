/**
 * Unit tests for radiant/dom_range.cpp.
 *
 * Constructs synthetic DOM trees directly (no HTML parser needed) and
 * exercises the DomBoundary / DomRange / DomSelection APIs.
 *
 * Bridge stubs at the top of this file satisfy the two extern "C" hooks
 * that dom_range.cpp uses to reach into RadiantState — production wires
 * these to radiant/state_store.cpp; here we point them at a tiny FakeState.
 */

#include <gtest/gtest.h>

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"
}

#include "../lambda/input/css/dom_element.hpp"
#include "../radiant/dom_range.hpp"

#include <cstring>
#include <new>

// ============================================================================
// Bridge stubs — see comment in radiant/dom_range.cpp.
// ============================================================================
struct FakeState {
    Arena* arena;
    DomRange* live_ranges;
};

extern "C" Arena* dom_range_state_arena(RadiantState* s) {
    return reinterpret_cast<FakeState*>(s)->arena;
}
extern "C" DomRange** dom_range_state_live_ranges_slot(RadiantState* s) {
    return &reinterpret_cast<FakeState*>(s)->live_ranges;
}

// ============================================================================
// Test fixture: pool + arena + a synthetic <div><span>"hello"</span><b/></div>
// ============================================================================
class DomRangeTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    FakeState fake_state{};
    RadiantState* state = nullptr;

    DomElement* div = nullptr;
    DomElement* span = nullptr;
    DomElement* b   = nullptr;
    DomText*    hello = nullptr;
    DomText*    world = nullptr;

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        arena = arena_create_default(pool);
        ASSERT_NE(arena, nullptr);
        fake_state.arena = arena;
        fake_state.live_ranges = nullptr;
        state = reinterpret_cast<RadiantState*>(&fake_state);

        div   = make_element();
        span  = make_element();
        b     = make_element();
        hello = make_text("hello", 5);
        world = make_text("world", 5);

        ASSERT_TRUE(div->append_child(span));
        ASSERT_TRUE(span->append_child(hello));
        ASSERT_TRUE(div->append_child(b));
        // div tree:  div [ span [ "hello" ], b ]
        // 'world' stays detached — used for "wrong document" testing.
    }

    void TearDown() override {
        // Nodes were heap-allocated by `new`; tear them down explicitly.
        delete world;
        delete hello;
        delete b;
        delete span;
        delete div;
        if (arena) arena_destroy(arena);
        if (pool) pool_destroy(pool);
    }

    // Create a minimally-initialized DomElement.
    DomElement* make_element() {
        return new DomElement();  // default ctor zeros all fields we care about
    }

    // Create a minimally-initialized DomText with a literal C string. The
    // string lives as long as the test (string literals have static storage).
    DomText* make_text(const char* s, size_t len) {
        DomText* t = new DomText();
        t->text = s;
        t->length = len;
        return t;
    }
};

// ============================================================================
// dom_node_boundary_length
// ============================================================================
TEST_F(DomRangeTest, BoundaryLengthForElementCountsChildren) {
    EXPECT_EQ(dom_node_boundary_length(div),  2u);
    EXPECT_EQ(dom_node_boundary_length(span), 1u);
    EXPECT_EQ(dom_node_boundary_length(b),    0u);
}

TEST_F(DomRangeTest, BoundaryLengthForTextIsUtf16Length) {
    EXPECT_EQ(dom_node_boundary_length(hello), 5u);
}

TEST_F(DomRangeTest, ChildIndexIsDenseAndSequential) {
    EXPECT_EQ(dom_node_child_index(span), 0u);
    EXPECT_EQ(dom_node_child_index(b),    1u);
    EXPECT_EQ(dom_node_child_index(div),  UINT32_MAX);  // no parent
}

// ============================================================================
// dom_boundary_compare
// ============================================================================
TEST_F(DomRangeTest, BoundaryCompareSameNode) {
    DomBoundary a{ hello, 1 };
    DomBoundary b{ hello, 4 };
    DomBoundary c{ hello, 1 };
    EXPECT_EQ(dom_boundary_compare(&a, &b), DOM_BOUNDARY_BEFORE);
    EXPECT_EQ(dom_boundary_compare(&b, &a), DOM_BOUNDARY_AFTER);
    EXPECT_EQ(dom_boundary_compare(&a, &c), DOM_BOUNDARY_EQUAL);
}

TEST_F(DomRangeTest, BoundaryCompareCrossLcaOrders) {
    // Boundary inside <span>'s "hello" text vs a boundary at div.[1] (just
    // before <b>): the text boundary lives under div.[0]/span.[0] and
    // therefore comes BEFORE div.[1].
    DomBoundary in_text{ hello,    3 };
    DomBoundary at_b   { div,      1 };
    EXPECT_EQ(dom_boundary_compare(&in_text, &at_b), DOM_BOUNDARY_BEFORE);
    EXPECT_EQ(dom_boundary_compare(&at_b, &in_text), DOM_BOUNDARY_AFTER);
}

TEST_F(DomRangeTest, BoundaryCompareDisjointTrees) {
    DomBoundary a{ hello, 0 };
    DomBoundary b{ world, 0 };  // 'world' is detached
    EXPECT_EQ(dom_boundary_compare(&a, &b), DOM_BOUNDARY_DISJOINT);
}

TEST_F(DomRangeTest, BoundaryCompareAncestorContainerVsDescendant) {
    DomBoundary at_div_0{ div, 0 };          // before <span>
    DomBoundary at_div_1{ div, 1 };          // between <span> and <b>
    DomBoundary at_div_2{ div, 2 };          // after <b>
    DomBoundary in_hello{ hello, 2 };

    EXPECT_EQ(dom_boundary_compare(&at_div_0, &in_hello), DOM_BOUNDARY_BEFORE);
    EXPECT_EQ(dom_boundary_compare(&at_div_1, &in_hello), DOM_BOUNDARY_AFTER);
    EXPECT_EQ(dom_boundary_compare(&at_div_2, &in_hello), DOM_BOUNDARY_AFTER);
    EXPECT_EQ(dom_boundary_compare(&in_hello, &at_div_0), DOM_BOUNDARY_AFTER);
    EXPECT_EQ(dom_boundary_compare(&in_hello, &at_div_1), DOM_BOUNDARY_BEFORE);
}

// ============================================================================
// UTF-16 length
// ============================================================================
TEST(DomRangeUtf16, AsciiLengthMatchesByteLength) {
    DomText t;
    const char* s = "abcdef";
    t.text = s;
    t.length = 6;
    EXPECT_EQ(dom_text_utf16_length(&t), 6u);
}

TEST(DomRangeUtf16, BmpAndAstralCount) {
    // "a" + U+00E9 ("é", BMP, 2 UTF-8 bytes) + U+1F600 (😀, astral, 4 UTF-8 bytes)
    DomText t;
    const char* s = "a\xC3\xA9\xF0\x9F\x98\x80";
    t.text = s;
    t.length = 7;
    // Expected UTF-16 code units: 1 (a) + 1 (é) + 2 (surrogate pair) = 4
    EXPECT_EQ(dom_text_utf16_length(&t), 4u);
}

// ============================================================================
// DomRange basic API
// ============================================================================
TEST_F(DomRangeTest, RangeCreateIsCollapsedAtNullStart) {
    DomRange* r = dom_range_create(state);
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(dom_range_collapsed(r));
    dom_range_release(r);
}

TEST_F(DomRangeTest, RangeSetStartEndUpdatesBoundaries) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    EXPECT_EQ(exc, nullptr);
    ASSERT_TRUE(dom_range_set_end(r, hello, 4, &exc));
    EXPECT_EQ(exc, nullptr);
    EXPECT_EQ(r->start.node, hello);
    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.offset, 4u);
    EXPECT_FALSE(dom_range_collapsed(r));
    dom_range_release(r);
}

TEST_F(DomRangeTest, RangeSetEndBeforeStartCollapsesToEnd) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 4, &exc));
    // Setting end before start should collapse start to end (per spec).
    ASSERT_TRUE(dom_range_set_end(r, hello, 1, &exc));
    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.offset, 1u);
    EXPECT_TRUE(dom_range_collapsed(r));
    dom_range_release(r);
}

TEST_F(DomRangeTest, RangeSetStartOffsetOutOfBoundsReturnsIndexSizeError) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    EXPECT_FALSE(dom_range_set_start(r, hello, 999, &exc));
    EXPECT_STREQ(exc, "IndexSizeError");
    dom_range_release(r);
}

TEST_F(DomRangeTest, SelectNodeSpansParentChildIndices) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_select_node(r, span, &exc));
    EXPECT_EQ(r->start.node, div);
    EXPECT_EQ(r->start.offset, 0u);
    EXPECT_EQ(r->end.node, div);
    EXPECT_EQ(r->end.offset, 1u);
    dom_range_release(r);
}

TEST_F(DomRangeTest, SelectNodeContentsCoversWholeNode) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_select_node_contents(r, hello, &exc));
    EXPECT_EQ(r->start.node, hello);
    EXPECT_EQ(r->start.offset, 0u);
    EXPECT_EQ(r->end.offset, 5u);
    dom_range_release(r);
}

TEST_F(DomRangeTest, CollapseToStartAndEnd) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_select_node_contents(r, hello, &exc));
    dom_range_collapse(r, /*to_start=*/true);
    EXPECT_TRUE(dom_range_collapsed(r));
    EXPECT_EQ(r->start.offset, 0u);

    ASSERT_TRUE(dom_range_select_node_contents(r, hello, &exc));
    dom_range_collapse(r, /*to_start=*/false);
    EXPECT_TRUE(dom_range_collapsed(r));
    EXPECT_EQ(r->start.offset, 5u);
    dom_range_release(r);
}

TEST_F(DomRangeTest, CommonAncestorWalksToLca) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 0, &exc));
    ASSERT_TRUE(dom_range_set_end(r, div, 2, &exc));
    EXPECT_EQ(dom_range_common_ancestor(r), div);
    dom_range_release(r);
}

TEST_F(DomRangeTest, IsPointInRange) {
    DomRange* r = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end(r, hello, 4, &exc));
    EXPECT_TRUE (dom_range_is_point_in_range(r, hello, 2));
    EXPECT_TRUE (dom_range_is_point_in_range(r, hello, 1));   // boundaries inclusive
    EXPECT_TRUE (dom_range_is_point_in_range(r, hello, 4));
    EXPECT_FALSE(dom_range_is_point_in_range(r, hello, 0));
    EXPECT_FALSE(dom_range_is_point_in_range(r, hello, 5));
    dom_range_release(r);
}

TEST_F(DomRangeTest, CompareBoundaryPointsBetweenRanges) {
    DomRange* a = dom_range_create(state);
    DomRange* b = dom_range_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(a, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (a, hello, 3, &exc));
    ASSERT_TRUE(dom_range_set_start(b, hello, 2, &exc));
    ASSERT_TRUE(dom_range_set_end  (b, hello, 4, &exc));

    EXPECT_EQ(dom_range_compare_boundary_points(a, DOM_RANGE_START_TO_START, b, &exc), -1);
    EXPECT_EQ(dom_range_compare_boundary_points(a, DOM_RANGE_END_TO_END,     b, &exc), -1);
    EXPECT_EQ(dom_range_compare_boundary_points(b, DOM_RANGE_START_TO_START, a, &exc),  1);
    dom_range_release(a);
    dom_range_release(b);
}

// ============================================================================
// DomSelection
// ============================================================================
TEST_F(DomRangeTest, SelectionInitiallyEmpty) {
    DomSelection* s = dom_selection_create(state);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(dom_selection_range_count(s), 0u);
    EXPECT_TRUE(dom_selection_is_collapsed(s));
    EXPECT_STREQ(dom_selection_type(s), "None");
}

TEST_F(DomRangeTest, SelectionCollapseCreatesCaret) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_collapse(s, hello, 2, &exc));
    EXPECT_EQ(dom_selection_range_count(s), 1u);
    EXPECT_TRUE(dom_selection_is_collapsed(s));
    EXPECT_STREQ(dom_selection_type(s), "Caret");
    EXPECT_EQ(dom_selection_anchor_node(s), hello);
    EXPECT_EQ(dom_selection_focus_offset(s), 2u);
}

TEST_F(DomRangeTest, SelectionCollapseNullClearsRanges) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_collapse(s, hello, 2, &exc));
    ASSERT_TRUE(dom_selection_collapse(s, nullptr, 0, &exc));
    EXPECT_EQ(dom_selection_range_count(s), 0u);
}

TEST_F(DomRangeTest, SelectionExtendForwardSetsForwardDirection) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_collapse(s, hello, 1, &exc));
    ASSERT_TRUE(dom_selection_extend(s, hello, 4, &exc));
    EXPECT_FALSE(dom_selection_is_collapsed(s));
    EXPECT_STREQ(dom_selection_type(s), "Range");
    EXPECT_EQ(s->direction, DOM_SEL_DIR_FORWARD);
    EXPECT_EQ(s->ranges[0]->start.offset, 1u);
    EXPECT_EQ(s->ranges[0]->end.offset, 4u);
}

TEST_F(DomRangeTest, SelectionExtendBackwardSetsBackwardDirection) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_collapse(s, hello, 4, &exc));
    ASSERT_TRUE(dom_selection_extend(s, hello, 1, &exc));
    EXPECT_FALSE(dom_selection_is_collapsed(s));
    EXPECT_EQ(s->direction, DOM_SEL_DIR_BACKWARD);
    EXPECT_EQ(s->anchor.offset, 4u);
    EXPECT_EQ(s->focus.offset, 1u);
    // range stored in min/max order:
    EXPECT_EQ(s->ranges[0]->start.offset, 1u);
    EXPECT_EQ(s->ranges[0]->end.offset, 4u);
}

TEST_F(DomRangeTest, SelectionSetBaseAndExtentAcrossNodes) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_set_base_and_extent(s, hello, 2, div, 2, &exc));
    EXPECT_EQ(dom_selection_anchor_node(s), hello);
    EXPECT_EQ(dom_selection_focus_node(s),  div);
    EXPECT_FALSE(dom_selection_is_collapsed(s));
    EXPECT_EQ(s->direction, DOM_SEL_DIR_FORWARD);
}

TEST_F(DomRangeTest, SelectionContainsNodeFullVsPartial) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    // Range covers only the first child (<span>) of <div>.
    ASSERT_TRUE(dom_selection_set_base_and_extent(s, div, 0, div, 1, &exc));
    EXPECT_TRUE (dom_selection_contains_node(s, span, /*allow_partial=*/false));
    EXPECT_FALSE(dom_selection_contains_node(s, b,    /*allow_partial=*/false));
    EXPECT_TRUE (dom_selection_contains_node(s, span, /*allow_partial=*/true));
}

TEST_F(DomRangeTest, SelectAllChildrenCoversEverything) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_select_all_children(s, div, &exc));
    EXPECT_EQ(dom_selection_anchor_node(s), div);
    EXPECT_EQ(dom_selection_anchor_offset(s), 0u);
    EXPECT_EQ(dom_selection_focus_offset(s),  2u);
    EXPECT_FALSE(dom_selection_is_collapsed(s));
}

TEST_F(DomRangeTest, RemoveAllRangesEmptiesSelection) {
    DomSelection* s = dom_selection_create(state);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_selection_collapse(s, hello, 0, &exc));
    EXPECT_EQ(dom_selection_range_count(s), 1u);
    dom_selection_remove_all_ranges(s);
    EXPECT_EQ(dom_selection_range_count(s), 0u);
    EXPECT_TRUE(dom_selection_is_collapsed(s));
    EXPECT_STREQ(dom_selection_type(s), "None");
}
