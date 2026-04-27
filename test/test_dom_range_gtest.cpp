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
extern "C" struct DomSelection* dom_range_state_selection(RadiantState*) {
    return nullptr;  // tests don't exercise selection-resync paths
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
    DomDocument doc_storage{};

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
        doc_storage.pool = pool;
        doc_storage.arena = arena;

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
        DomElement* e = new DomElement();  // default ctor zeros all fields we care about
        e->doc = &doc_storage;
        return e;
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

// ============================================================================
// Phase 3: Mutation envelope live-range adjustments (WHATWG DOM §5.5/§5.3)
// ============================================================================

class DomMutationTest : public DomRangeTest {
protected:
    DomElement* p = nullptr;
    DomElement* a = nullptr;
    DomElement* c = nullptr;
    DomText*    t1 = nullptr;
    DomText*    t2 = nullptr;

    void SetUp() override {
        DomRangeTest::SetUp();
        // build:  p [ a, "hello", c ]   (and reuse `world` etc as needed)
        p  = make_element();
        a  = make_element();
        c  = make_element();
        t1 = make_text("hello", 5);
        t2 = make_text("XYZ",   3);
        ASSERT_TRUE(p->append_child(a));
        ASSERT_TRUE(p->append_child(t1));
        ASSERT_TRUE(p->append_child(c));
    }

    void TearDown() override {
        delete t2;
        delete t1;
        delete c;
        delete a;
        delete p;
        DomRangeTest::TearDown();
    }
};

// pre_remove: endpoint inside removed subtree collapses to (parent, index)
TEST_F(DomMutationTest, PreRemoveCollapsesEndpointInsideRemovedSubtree) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t1, 2, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, t1, 4, &exc));

    dom_mutation_pre_remove(state, t1);
    p->remove_child(t1);

    // Both endpoints should now point at (p, 1) — index where t1 used to be.
    EXPECT_EQ(r->start.node, (DomNode*)p);
    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.node,   (DomNode*)p);
    EXPECT_EQ(r->end.offset, 1u);
    EXPECT_TRUE(dom_range_collapsed(r));
}

// pre_remove: endpoint at (parent, off>index) decrements
TEST_F(DomMutationTest, PreRemoveDecrementsLaterSiblingOffsets) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, p, 2, &exc));   // between t1 and c
    ASSERT_TRUE(dom_range_set_end  (r, p, 3, &exc));   // after c

    dom_mutation_pre_remove(state, t1);                // remove index 1
    p->remove_child(t1);

    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.offset,   2u);
}

// pre_remove: endpoint at (parent, off<=index) is unchanged
TEST_F(DomMutationTest, PreRemoveDoesNotChangeEarlierSiblingOffsets) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, p, 0, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, p, 1, &exc));   // == index, untouched

    dom_mutation_pre_remove(state, t1);                // index 1
    p->remove_child(t1);

    EXPECT_EQ(r->start.offset, 0u);
    EXPECT_EQ(r->end.offset,   1u);
}

// post_insert: endpoint at (parent, off>index) increments (strict >)
TEST_F(DomMutationTest, PostInsertIncrementsLaterSiblingOffsets) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, p, 2, &exc));   // after t1
    ASSERT_TRUE(dom_range_set_end  (r, p, 3, &exc));   // after c

    // Insert t2 at index 1 (between a and t1) by detaching first then linking.
    ASSERT_TRUE(p->insert_before(t2, t1));
    dom_mutation_post_insert(state, p, t2);

    EXPECT_EQ(r->start.offset, 3u);
    EXPECT_EQ(r->end.offset,   4u);
}

// post_insert: endpoint at (parent, off==index) is unchanged (strict >)
TEST_F(DomMutationTest, PostInsertDoesNotMoveEqualOffset) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, p, 1, &exc));   // == insertion index
    ASSERT_TRUE(dom_range_set_end  (r, p, 1, &exc));

    ASSERT_TRUE(p->insert_before(t2, t1));
    dom_mutation_post_insert(state, p, t2);

    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.offset,   1u);
}

// text replaceData: endpoint before the edit window is unchanged
TEST_F(DomMutationTest, TextReplaceDataLeavesEarlierEndpointAlone) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t1, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, t1, 1, &exc));

    // Edit window: offset=2, count=2, replacement_len=5
    dom_mutation_text_replace_data(state, t1, 2, 2, 5);
    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.offset,   1u);
}

// text replaceData: endpoint within the deleted range clamps to offset+replLen
TEST_F(DomMutationTest, TextReplaceDataClampsEndpointInsideEditWindow) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t1, 3, &exc));   // inside (2, 4]
    ASSERT_TRUE(dom_range_set_end  (r, t1, 4, &exc));   // == offset+count

    // Edit window: offset=2, count=2, replacement_len=5  → both clamp to 7
    dom_mutation_text_replace_data(state, t1, 2, 2, 5);
    EXPECT_EQ(r->start.offset, 7u);
    EXPECT_EQ(r->end.offset,   7u);
}

// text replaceData: endpoint past the edit window shifts by (replLen - count)
TEST_F(DomMutationTest, TextReplaceDataShiftsLaterEndpointByDelta) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t1, 5, &exc));   // > offset+count

    dom_mutation_text_replace_data(state, t1, 2, 2, 5); // delta = +3
    EXPECT_EQ(r->start.offset, 8u);
}

// text split: endpoints in original past offset move to new node, then sibling
// insertion is accounted for.
TEST_F(DomMutationTest, TextSplitMovesLaterEndpointsToNewNode) {
    // Range spans across the split point: starts at t1[1], ends at t1[4]
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t1, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, t1, 4, &exc));

    // Simulate a split at offset 2 into a new text node which is inserted as
    // the next sibling of t1 (i.e. into p at index 2). t2 already exists.
    ASSERT_TRUE(p->insert_before(t2, c));   // t2 placed at index 2

    dom_mutation_text_split(state, t1, t2, /*offset=*/2);

    // start (offset 1) unchanged
    EXPECT_EQ(r->start.node,   (DomNode*)t1);
    EXPECT_EQ(r->start.offset, 1u);
    // end (offset 4) moves to (t2, 4-2 = 2)
    EXPECT_EQ(r->end.node,   (DomNode*)t2);
    EXPECT_EQ(r->end.offset, 2u);
}

// text merge (normalize): endpoints inside `next` retarget to `prev`.
TEST_F(DomMutationTest, TextMergeRetargetsEndpointsToPrev) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, t2, 0, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, t2, 3, &exc));

    // t1 has utf16 length 5; merge t2 into t1 at offset 5.
    dom_mutation_text_merge(state, t1, t2, /*prev_u16_len=*/5);

    EXPECT_EQ(r->start.node,   (DomNode*)t1);
    EXPECT_EQ(r->start.offset, 5u);
    EXPECT_EQ(r->end.node,     (DomNode*)t1);
    EXPECT_EQ(r->end.offset,   8u);
}

// ============================================================================
// Phase 4: Range mutation methods
// ============================================================================

// Helper: count children of an element.
static uint32_t child_count(DomElement* e) {
    uint32_t n = 0;
    for (DomNode* c = e->first_child; c; c = c->next_sibling) n++;
    return n;
}

TEST_F(DomRangeTest, DocumentFragmentCreate) {
    DomElement* frag = dom_document_fragment_create(&doc_storage);
    ASSERT_NE(frag, nullptr);
    EXPECT_TRUE(frag->is_element());
    EXPECT_STREQ(frag->tag_name, "#document-fragment");
    EXPECT_EQ(child_count(frag), 0u);
}

TEST_F(DomRangeTest, NodeCloneShallowText) {
    DomNode* clone = dom_node_clone(hello, /*deep=*/false);
    ASSERT_NE(clone, nullptr);
    ASSERT_TRUE(clone->is_text());
    DomText* ct = clone->as_text();
    EXPECT_EQ(ct->length, 5u);
    EXPECT_EQ(memcmp(ct->text, "hello", 5), 0);
    EXPECT_EQ(ct->parent, nullptr);
}

TEST_F(DomRangeTest, NodeCloneDeepElement) {
    span->tag_name = "span";
    DomNode* clone = dom_node_clone(span, /*deep=*/true);
    ASSERT_NE(clone, nullptr);
    ASSERT_TRUE(clone->is_element());
    DomElement* ce = clone->as_element();
    EXPECT_EQ(child_count(ce), 1u);
    ASSERT_NE(ce->first_child, nullptr);
    EXPECT_TRUE(ce->first_child->is_text());
}

TEST_F(DomRangeTest, DeleteContentsCollapsedIsNoOp) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 2, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 2, &exc));
    EXPECT_TRUE(dom_range_delete_contents(r, &exc));
    EXPECT_EQ(hello->length, 5u);
}

TEST_F(DomRangeTest, DeleteContentsWithinSingleText) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 4, &exc));
    EXPECT_TRUE(dom_range_delete_contents(r, &exc));
    // "hello" -> "ho"
    EXPECT_EQ(hello->length, 2u);
    EXPECT_EQ(memcmp(hello->text, "ho", 2), 0);
    // range collapsed at (hello,1)
    EXPECT_EQ(r->start.node, (DomNode*)hello);
    EXPECT_EQ(r->start.offset, 1u);
    EXPECT_EQ(r->end.node, (DomNode*)hello);
    EXPECT_EQ(r->end.offset, 1u);
}

TEST_F(DomRangeTest, ExtractContentsFromSingleText) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 4, &exc));
    DomElement* frag = dom_range_extract_contents(r, &exc);
    ASSERT_NE(frag, nullptr);
    EXPECT_STREQ(frag->tag_name, "#document-fragment");
    // fragment should contain a text node "ell"
    ASSERT_NE(frag->first_child, nullptr);
    ASSERT_TRUE(frag->first_child->is_text());
    DomText* ft = frag->first_child->as_text();
    EXPECT_EQ(ft->length, 3u);
    EXPECT_EQ(memcmp(ft->text, "ell", 3), 0);
    // source mutated to "ho"
    EXPECT_EQ(hello->length, 2u);
    EXPECT_EQ(memcmp(hello->text, "ho", 2), 0);
}

TEST_F(DomRangeTest, CloneContentsLeavesSourceIntact) {
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 4, &exc));
    DomElement* frag = dom_range_clone_contents(r, &exc);
    ASSERT_NE(frag, nullptr);
    ASSERT_NE(frag->first_child, nullptr);
    DomText* ft = frag->first_child->as_text();
    EXPECT_EQ(ft->length, 3u);
    EXPECT_EQ(memcmp(ft->text, "ell", 3), 0);
    // source untouched
    EXPECT_EQ(hello->length, 5u);
    EXPECT_EQ(memcmp(hello->text, "hello", 5), 0);
}

TEST_F(DomRangeTest, ExtractContentsAcrossElements) {
    // range from div.[0] to div.[2] -> extracts everything (span+b)
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, div, 0, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, div, 2, &exc));
    DomElement* frag = dom_range_extract_contents(r, &exc);
    ASSERT_NE(frag, nullptr);
    EXPECT_EQ(child_count(frag), 2u);
    EXPECT_EQ(child_count(div), 0u);
    // Detach: span/b moved into frag and our raw pointers no longer own them via div
    div->first_child = nullptr;
    div->last_child  = nullptr;
}

TEST_F(DomRangeTest, InsertNodeAtElementBoundary) {
    DomElement* extra = make_element();
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, div, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, div, 1, &exc));
    EXPECT_TRUE(dom_range_insert_node(r, extra, &exc));
    // div now: [span, extra, b]
    EXPECT_EQ(child_count(div), 3u);
    EXPECT_EQ(div->first_child, (DomNode*)span);
    EXPECT_EQ(span->next_sibling, (DomNode*)extra);
    EXPECT_EQ(extra->next_sibling, (DomNode*)b);
}

TEST_F(DomRangeTest, InsertNodeSplitsTextAtMidOffset) {
    // insert <b> at offset 2 inside hello: should split hello -> "he" + "llo"
    // and place a new node between them inside span.
    DomElement* mark = make_element();
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 2, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 2, &exc));
    EXPECT_TRUE(dom_range_insert_node(r, mark, &exc));
    // hello truncated to "he"
    EXPECT_EQ(hello->length, 2u);
    EXPECT_EQ(memcmp(hello->text, "he", 2), 0);
    // span now has 3 children: hello, mark, new_text("llo")
    EXPECT_EQ(child_count(span), 3u);
    EXPECT_EQ(span->first_child, (DomNode*)hello);
    EXPECT_EQ(hello->next_sibling, (DomNode*)mark);
    DomNode* tail = mark->next_sibling;
    ASSERT_NE(tail, nullptr);
    ASSERT_TRUE(tail->is_text());
    DomText* tt = tail->as_text();
    EXPECT_EQ(tt->length, 3u);
    EXPECT_EQ(memcmp(tt->text, "llo", 3), 0);
}

TEST_F(DomRangeTest, SurroundContentsWrapsTextSubrange) {
    DomElement* wrap = make_element();
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 1, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, hello, 4, &exc));
    EXPECT_TRUE(dom_range_surround_contents(r, wrap, &exc));
    // span should now contain: hello"h", wrap[ "ell" ], hello-tail"o"
    EXPECT_EQ(child_count(span), 3u);
    // wrap got the "ell" text
    EXPECT_EQ(child_count(wrap), 1u);
    ASSERT_NE(wrap->first_child, nullptr);
    EXPECT_TRUE(wrap->first_child->is_text());
}

TEST_F(DomRangeTest, SurroundContentsRejectsPartialElement) {
    // range from hello-mid to outside span: partial non-text containment
    DomElement* wrap = make_element();
    DomRange* r = dom_range_create(state);
    dom_range_link_into_state(state, r);
    const char* exc = nullptr;
    ASSERT_TRUE(dom_range_set_start(r, hello, 2, &exc));
    ASSERT_TRUE(dom_range_set_end  (r, div, 2, &exc));
    exc = nullptr;
    EXPECT_FALSE(dom_range_surround_contents(r, wrap, &exc));
    ASSERT_NE(exc, nullptr);
    EXPECT_STREQ(exc, "InvalidStateError");
}

