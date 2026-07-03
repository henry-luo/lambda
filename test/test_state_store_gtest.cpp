#include <gtest/gtest.h>

extern "C" {
#include "../lib/arena.h"
#include "../lib/mempool.h"
}
#include "../lib/test_utils.h"

#include "../lambda/input/css/dom_element.hpp"
#include "../radiant/state_store.hpp"
#include "../radiant/view.hpp"

#include <new>

class StateStoreDomMutationTest : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    DomDocument doc{};

    DomElement* root = nullptr;
    DomElement* live = nullptr;
    DomElement* orphan = nullptr;
    DomElement* drop = nullptr;

    void SetUp() override {
        pool = tu_setup_pool();
        arena = arena_create_default(pool);
        ASSERT_NE(arena, nullptr);

        doc.pool = pool;
        doc.arena = arena;

        root = make_element();
        live = make_element();
        orphan = make_element();
        drop = make_element();
        doc.root = root;

        ASSERT_TRUE(root->append_child(live));
        ASSERT_TRUE(root->append_child(orphan));
        ASSERT_TRUE(root->append_child(drop));

        StateStore* store = state_store_create(&doc);
        ASSERT_NE(store, nullptr);
        ASSERT_NE(state_store_doc_state(store), nullptr);
    }

    void TearDown() override {
        state_store_destroy(&doc);
        delete drop;
        delete orphan;
        delete live;
        delete root;
        if (arena) arena_destroy(arena);
        tu_teardown_pool(pool);
    }

    DomElement* make_element() {
        DomElement* element = new DomElement();
        element->doc = &doc;
        static_cast<DomNode*>(element)->id = doc.next_node_id++;
        element->view_type = RDT_VIEW_BLOCK;
        return element;
    }

    DocState* state() {
        return doc.state;
    }
};

TEST_F(StateStoreDomMutationTest, PruneAfterReflowKeepsLiveViewStateAndDropsOrphan) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    doc_state_set_hover_target(doc_state, static_cast<View*>(live));
    view_state_set_active(doc_state, static_cast<View*>(orphan), true);

    ViewState* live_before = view_state_get(doc_state, static_cast<View*>(live));
    ViewState* orphan_before = view_state_get(doc_state, static_cast<View*>(orphan));
    ASSERT_NE(live_before, nullptr);
    ASSERT_NE(orphan_before, nullptr);
    EXPECT_TRUE(live_before->flags.hovered);
    EXPECT_TRUE(orphan_before->flags.active);

    ASSERT_TRUE(root->remove_child(orphan));

    uint32_t pruned = state_store_prune_after_reflow(doc_state);
    EXPECT_GT(pruned, 0u);

    ViewState* live_after = view_state_get(doc_state, static_cast<View*>(live));
    EXPECT_EQ(live_after, live_before);
    ASSERT_NE(live_after, nullptr);
    EXPECT_TRUE(live_after->flags.hovered);
    EXPECT_EQ(doc_state->hover_target, static_cast<View*>(live));

    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);
}

TEST_F(StateStoreDomMutationTest, PruneAfterReflowKeepsLiveStateMapEntriesOnly) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    state_set_bool(doc_state, live, "mutation-live-state", true);
    state_set_bool(doc_state, orphan, "mutation-orphan-state", true);
    ASSERT_TRUE(state_get_bool(doc_state, live, "mutation-live-state"));
    ASSERT_TRUE(state_get_bool(doc_state, orphan, "mutation-orphan-state"));

    ASSERT_TRUE(root->remove_child(orphan));

    uint32_t pruned = state_store_prune_after_reflow(doc_state);
    EXPECT_GT(pruned, 0u);

    EXPECT_TRUE(state_get_bool(doc_state, live, "mutation-live-state"));
    EXPECT_FALSE(state_get_bool(doc_state, orphan, "mutation-orphan-state"));
}

TEST_F(StateStoreDomMutationTest, PruneAfterReflowKeepsDragWhenOnlyDropTargetRemoved) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    DragDropState* drag = doc_state_begin_drag_drop(doc_state, static_cast<View*>(live),
                                                    4.0f, 5.0f, "text/plain");
    ASSERT_NE(drag, nullptr);
    doc_state_set_drag_drop_active(doc_state, true);

    DomBoundary start = { static_cast<DomNode*>(drop), 0 };
    DomBoundary end = { static_cast<DomNode*>(drop), 0 };
    doc_state_set_drag_drop_target(doc_state, static_cast<View*>(drop), &start, &end);
    ASSERT_EQ(drag->source_view, static_cast<View*>(live));
    ASSERT_EQ(drag->drop_target, static_cast<View*>(drop));
    ASSERT_TRUE(drag->active);
    ASSERT_TRUE(drag->has_drop_range);

    ASSERT_TRUE(root->remove_child(drop));

    uint32_t pruned = state_store_prune_after_reflow(doc_state);
    EXPECT_GT(pruned, 0u);

    ASSERT_NE(doc_state->drag_drop, nullptr);
    EXPECT_EQ(doc_state->drag_drop->source_view, static_cast<View*>(live));
    EXPECT_EQ(doc_state->drag_drop->source_node_id, static_cast<DomNode*>(live)->id);
    EXPECT_TRUE(doc_state->drag_drop->active);
    EXPECT_FALSE(doc_state->drag_drop->pending);
    EXPECT_EQ(doc_state->drag_drop->drop_target, nullptr);
    EXPECT_EQ(doc_state->drag_drop->drop_target_node_id, 0u);
    EXPECT_FALSE(doc_state->drag_drop->has_drop_range);
}

TEST_F(StateStoreDomMutationTest, PruneAfterReflowClearsDragWhenSourceRemoved) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    DragDropState* drag = doc_state_begin_drag_drop(doc_state, static_cast<View*>(orphan),
                                                    2.0f, 3.0f, "text/plain");
    ASSERT_NE(drag, nullptr);
    doc_state_set_drag_drop_active(doc_state, true);
    doc_state_set_drag_drop_target(doc_state, static_cast<View*>(drop), nullptr, nullptr);
    ASSERT_TRUE(drag->active);
    ASSERT_EQ(drag->source_view, static_cast<View*>(orphan));

    ASSERT_TRUE(root->remove_child(orphan));

    uint32_t pruned = state_store_prune_after_reflow(doc_state);
    EXPECT_GT(pruned, 0u);

    ASSERT_NE(doc_state->drag_drop, nullptr);
    EXPECT_EQ(doc_state->drag_drop->source_view, nullptr);
    EXPECT_EQ(doc_state->drag_drop->source_node_id, 0u);
    EXPECT_FALSE(doc_state->drag_drop->active);
    EXPECT_FALSE(doc_state->drag_drop->pending);
    EXPECT_EQ(doc_state->drag_drop->drop_target, nullptr);
}
