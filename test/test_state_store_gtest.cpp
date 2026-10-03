#include <gtest/gtest.h>

extern "C" {
#include "../lib/arena.h"
#include "../lib/mempool.h"
}
#include "../lib/test_utils.h"

#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../radiant/event.hpp"
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
        arena = arena_create_default();
        ASSERT_NE(arena, nullptr);

        doc.document_pool = lam::own(pool);
        doc.node_arena = lam::own(arena);

        root = make_element();
        live = make_element();
        orphan = make_element();
        drop = make_element();
        doc.root = lam::up(root);

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
        DomElement* element = new DomElement{};
        element->node_type = DOM_NODE_ELEMENT;
        element->set_synthetic(true);
        element->doc = lam::up(&doc);
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

TEST_F(StateStoreDomMutationTest, DetachedRegisteredViewStopsResolvingBeforeRetirement) {
    DocState* doc_state = state();
    ASSERT_TRUE(dom_lifecycle_init(&doc));
    ASSERT_TRUE(dom_node_registry_register(&doc, orphan, sizeof(DomElement), false));

    doc_state_set_hover_target(doc_state, static_cast<View*>(orphan));
    ViewStateEntry query = {.view_id = static_cast<DomNode*>(orphan)->id,
        .kind = VIEW_STATE_BASE};
    const ViewStateEntry* entry = static_cast<const ViewStateEntry*>(
        hashmap_get(doc_state->view_state_map, &query));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(view_state_entry_resolve_view(doc_state, entry), static_cast<View*>(orphan));
    DomElement* layout_wrapper = make_element();
    root->parent = lam::up(layout_wrapper);
    EXPECT_EQ(view_state_entry_resolve_view(doc_state, entry), static_cast<View*>(orphan));
    root->parent = nullptr;
    delete layout_wrapper;

    ASSERT_TRUE(root->remove_child(orphan));
    // a detached node remains registry-valid until retirement, but is no longer a live view.
    EXPECT_EQ(view_state_entry_resolve_view(doc_state, entry), nullptr);
    EXPECT_GT(state_store_prune_after_reflow(doc_state), 0u);
    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);
    EXPECT_EQ(doc_state->hover_target, nullptr);
    EXPECT_FALSE(state_get_bool(doc_state, root, STATE_HOVER));
    EXPECT_TRUE(radiant_state_validate_interaction(doc_state, nullptr));

    doc_state->active_cascade_depth++;
    doc_state_set_hover_target(doc_state, static_cast<View*>(orphan));
    doc_state->active_cascade_depth--;
    EXPECT_EQ(doc_state->hover_target, nullptr);
    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);

    orphan->tag_name = lam::up("button");
    orphan->tag_id = MARKUP_NAME_BUTTON;
    doc_state->active_cascade_depth++;
    focus_set_programmatic(doc_state, static_cast<View*>(orphan));
    doc_state->active_cascade_depth--;
    EXPECT_EQ(focus_get(doc_state), nullptr);
    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);
}

TEST_F(StateStoreDomMutationTest, DetachedPointerPhasesDoNotRecreatePrunedState) {
    DocState* doc_state = state();
    ASSERT_TRUE(dom_lifecycle_init(&doc));
    ASSERT_TRUE(dom_node_registry_register(&doc, orphan, sizeof(DomElement), false));

    orphan->tag_name = lam::up("button");
    orphan->tag_id = MARKUP_NAME_BUTTON;
    doc_state_set_hover_target(doc_state, static_cast<View*>(orphan));
    doc_state_set_active_target(doc_state, static_cast<View*>(orphan));
    ASSERT_NE(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);

    ASSERT_TRUE(root->remove_child(orphan));
    ASSERT_GT(state_store_prune_after_reflow(doc_state), 0u);
    ASSERT_EQ(doc_state->hover_target, nullptr);
    ASSERT_EQ(doc_state->active_target, nullptr);

    const size_t state_count = hashmap_count(doc_state->view_state_map);
    const uint64_t version = doc_state->version;

    // later pointer phases still hold the detached target after reactive replacement.
    doc_state->active_cascade_depth++;
    for (size_t phase = 0; phase < 8; phase++) {
        view_state_set_hovered(doc_state, static_cast<View*>(orphan), true);
        view_state_set_active(doc_state, static_cast<View*>(orphan), true);
        view_state_set_focused(doc_state, static_cast<View*>(orphan), true);
        doc_state_set_hover_target(doc_state, static_cast<View*>(orphan));
        doc_state_set_active_target(doc_state, static_cast<View*>(orphan));
        focus_set_programmatic(doc_state, static_cast<View*>(orphan));

        EXPECT_EQ(hashmap_count(doc_state->view_state_map), state_count);
        EXPECT_EQ(doc_state->version, version);
        EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), nullptr);
        EXPECT_EQ(doc_state->hover_target, nullptr);
        EXPECT_EQ(doc_state->active_target, nullptr);
        EXPECT_EQ(focus_get(doc_state), nullptr);
    }
    doc_state->active_cascade_depth--;
    EXPECT_TRUE(radiant_state_validate_interaction(doc_state, nullptr));
    EXPECT_EQ(state_store_prune_after_reflow(doc_state), 0u);
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

TEST_F(StateStoreDomMutationTest, PruneAfterReflowRestoresFocusAssignedBeforeViewIdentity) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    live->tag_name = lam::up("input");
    live->tag_id = MARKUP_NAME_INPUT;
    static_cast<DomNode*>(live)->id = 0;
    ASSERT_EQ(live->tag(), MARKUP_NAME_INPUT);
    ASSERT_TRUE(is_view_programmatically_focusable(static_cast<View*>(live)));
    // Script focus can precede retained view-id assignment during first layout.
    doc_state->transition_depth++;
    focus_set_programmatic(doc_state, static_cast<View*>(live));
    doc_state->transition_depth--;
    ASSERT_EQ(focus_get(doc_state), static_cast<View*>(live));

    static_cast<DomNode*>(live)->id = doc.next_node_id++;
    EXPECT_FALSE(state_get_bool(doc_state, live, STATE_FOCUS));

    uint32_t pruned = state_store_prune_after_reflow(doc_state);
    EXPECT_GT(pruned, 0u);
    EXPECT_TRUE(state_get_bool(doc_state, live, STATE_FOCUS));
    EXPECT_TRUE(radiant_state_validate_interaction(doc_state, nullptr));
}

TEST_F(StateStoreDomMutationTest, TextControlValueIsViewStateOwnedAcrossPropRebuild) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);

    FormControlProp* original_prop = new FormControlProp{};
    ASSERT_NE(original_prop, nullptr);
    original_prop->control_type = FORM_CONTROL_TEXT;
    live->form = lam::own(original_prop);
    ASSERT_TRUE(form_control_store_text_value(doc_state, static_cast<View*>(live),
                                              "state-owned", 11, 11));

    ViewState* view_state = view_state_get(doc_state, static_cast<View*>(live));
    ASSERT_NE(view_state, nullptr);
    ASSERT_TRUE(view_state->data.form.has_current_value);
    ASSERT_NE(live->form, nullptr);
    EXPECT_EQ(live->form->current_value, view_state->data.form.current_value);
    EXPECT_STREQ(live->form->current_value, "state-owned");

    FormControlProp* rebuilt_prop = new FormControlProp{};
    ASSERT_NE(rebuilt_prop, nullptr);
    rebuilt_prop->control_type = FORM_CONTROL_TEXT;
    live->form = lam::own(rebuilt_prop);

    // Reflow must rebind the newly pooled prop without allocating a second value.
    state_store_prune_after_reflow(doc_state);
    EXPECT_EQ(live->form->current_value, view_state->data.form.current_value);
    EXPECT_STREQ(live->form->current_value, "state-owned");

    live->form = nullptr;
    delete rebuilt_prop;
    delete original_prop;
}

TEST_F(StateStoreDomMutationTest, DetachedTextControlRetainsValueAcrossReflow) {
    DocState* doc_state = state();
    ASSERT_NE(doc_state, nullptr);
    ASSERT_TRUE(dom_lifecycle_init(&doc));
    ASSERT_TRUE(dom_node_registry_register(&doc, orphan, sizeof(DomElement), false));

    orphan->tag_name = lam::up("input");
    orphan->tag_id = MARKUP_NAME_INPUT;
    FormControlProp* form = new FormControlProp{};
    form->control_type = FORM_CONTROL_TEXT;
    orphan->form = lam::own(form);
    ASSERT_TRUE(root->remove_child(orphan));
    ASSERT_TRUE(form_control_store_text_value(doc_state, static_cast<View*>(orphan),
                                               "detached value", 14, 14));
    ViewState* value_state = view_state_get(doc_state, static_cast<View*>(orphan));
    ASSERT_NE(value_state, nullptr);

    state_store_prune_after_reflow(doc_state);
    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), value_state);
    EXPECT_EQ(form->current_value, value_state->data.form.current_value);
    EXPECT_STREQ(form->current_value, "detached value");
    EXPECT_TRUE(radiant_state_validate_interaction(doc_state, nullptr));

    ASSERT_TRUE(root->append_child(orphan));
    state_store_prune_after_reflow(doc_state);
    EXPECT_EQ(view_state_get(doc_state, static_cast<View*>(orphan)), value_state);
    EXPECT_STREQ(form->current_value, "detached value");
    orphan->form = nullptr;
    delete form;
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
