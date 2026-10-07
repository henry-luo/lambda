#include <gtest/gtest.h>

#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"
#include "../radiant/render.hpp"
#include "../radiant/event.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/css/dom_lifecycle.hpp"
#include "../lambda/input/css/style_epoch.hpp"
#include "../lambda/input/css/css_engine.hpp"
#include "../lambda/input/css/css_style_node.hpp"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lib/mem_grow.hpp"

DomElement* build_dom_tree_from_element(Element*, DomDocument*, DomElement*);

class ViewReuseTest : public ::testing::Test {
protected:
    ViewTree tree = {};

    void SetUp() override {
        tree.prop_pool = lam::own(pool_create());
        ASSERT_NE(tree.prop_pool, nullptr);
        view_tree_canonical_init(&tree);
        ASSERT_NE(tree.canonical_prop_arena, nullptr);
    }

    void TearDown() override {
        view_tree_canonical_destroy(&tree);
        pool_destroy(tree.prop_pool);
        tree.prop_pool = nullptr;
    }

    DomElement* element() {
        DomElement* value = DomElement::create_in(tree.prop_pool);
        EXPECT_NE(value, nullptr);
        return value;
    }
};

TEST_F(ViewReuseTest, ExplicitHashAndEqualityCoverEveryInlineField) {
    InlineProp left = INLINE_PROP_DEFAULT;
    InlineProp right = left;
    EXPECT_TRUE(inline_prop_equal(&left, &right));
    EXPECT_EQ(inline_prop_hash(&left), inline_prop_hash(&right));

    right.svg_stroke_width = 3.5f;
    right.has_svg_stroke_width = true;
    EXPECT_FALSE(inline_prop_equal(&left, &right));
    EXPECT_NE(inline_prop_hash(&left), inline_prop_hash(&right));
}

TEST_F(ViewReuseTest, EqualParentAndChildPromoteThenCowAtEnsureGate) {
    DomElement* parent = element();
    DomElement* child = element();
    parent->ensure_inline(&tree)->color.c = 0xff123456u;
    parent->in_line->has_color = true;
    child->ensure_inline(&tree)->color = parent->in_line->color;
    child->in_line->has_color = true;

    view_tree_commit_inline_prop(&tree, child, parent);
    ASSERT_TRUE(parent->inline_prop_shared());
    ASSERT_TRUE(child->inline_prop_shared());
    ASSERT_EQ(parent->in_line, child->in_line);
    ASSERT_TRUE(arena_owns(tree.canonical_prop_arena, parent->in_line));

    InlineProp* canonical = parent->in_line;
    InlineProp* mutable_child = child->ensure_inline(&tree);
    ASSERT_NE(mutable_child, canonical);
    EXPECT_FALSE(child->inline_prop_shared());
    EXPECT_EQ(mutable_child->color.c, canonical->color.c);
    mutable_child->opacity = 0.25f;
    EXPECT_EQ(canonical->opacity, INLINE_PROP_DEFAULT.opacity);
    EXPECT_EQ(tree.canonical_stats.inline_cows, 1u);
}

TEST_F(ViewReuseTest, CanonicalIndexReusesAnExistingExactValue) {
    DomElement* parent1 = element();
    DomElement* child1 = element();
    DomElement* parent2 = element();
    DomElement* child2 = element();
    parent1->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    child1->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    parent2->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;
    child2->ensure_inline(&tree)->cursor = CSS_VALUE_POINTER;

    view_tree_commit_inline_prop(&tree, child1, parent1);
    InlineProp* first = parent1->in_line;
    view_tree_commit_inline_prop(&tree, child2, parent2);
    EXPECT_EQ(parent2->in_line, first);
    EXPECT_EQ(child2->in_line, first);
    EXPECT_EQ(tree.inline_canonical_count, 1u);
    EXPECT_EQ(tree.canonical_stats.inline_hits, 1u);
}

TEST_F(ViewReuseTest, ComputedFamilyListsShareOneTreeLifetimeString) {
    CssValue* parts[] = {
        css_value_create_string(tree.prop_pool, "Arial"),
        css_value_create_keyword(tree.prop_pool, "sans-serif"),
    };
    CssValue* list = css_value_create_list(tree.prop_pool, parts, 2);
    ASSERT_NE(list, nullptr);
    list->data.list.comma_separated = true;
    LayoutContext lycon = {};
    lycon.selected_view_tree = lam::up(&tree);

    const char* first = css_select_font_family(&lycon, list);
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first, "Arial, sans-serif");
    // inherited and copied fonts borrow the family; nothing frees it per restyle
    FontProp parent = {};
    FontProp child = {};
    radiant_retain_font_family(&parent, lam::PoolPtr<char>((char*)first));
    radiant_copy_font_values(&child, &parent);

    PoolStats warm = {};
    pool_get_detailed_stats(tree.prop_pool, &warm);
    for (int i = 0; i < 256; i++) {
        const char* again = css_select_font_family(&lycon, list);
        ASSERT_EQ(again, first);
        radiant_retain_font_family(&parent, lam::PoolPtr<char>((char*)again));
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(tree.prop_pool, &repeated);
    EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
    EXPECT_STREQ(child.family, "Arial, sans-serif");

    // equal text from a foreign, unterminated buffer reuses the canonical copy
    char foreign[] = "Arial, sans-serif, monospace";
    EXPECT_EQ(view_tree_canonical_font_family(&tree, foreign, 17), first);
    const char* other = view_tree_canonical_font_family(&tree, foreign, strlen(foreign));
    ASSERT_NE(other, nullptr);
    EXPECT_NE(other, first);
    EXPECT_STREQ(other, "Arial, sans-serif, monospace");
    EXPECT_EQ(tree.canonical_stats.font_family_misses, 2u);
}

TEST_F(ViewReuseTest, CanonicalCapFallsBackToOwnedStorage) {
    tree.canonical_prop_cap_bytes = 0;
    DomElement* parent = element();
    DomElement* child = element();
    parent->ensure_inline(&tree)->visibility = VIS_HIDDEN;
    child->ensure_inline(&tree)->visibility = VIS_HIDDEN;
    InlineProp* parent_owned = parent->in_line;
    InlineProp* child_owned = child->in_line;

    view_tree_commit_inline_prop(&tree, child, parent);
    EXPECT_EQ(parent->in_line, parent_owned);
    EXPECT_EQ(child->in_line, child_owned);
    EXPECT_FALSE(parent->inline_prop_shared());
    EXPECT_FALSE(child->inline_prop_shared());
    EXPECT_EQ(tree.canonical_stats.cap_fallbacks, 1u);
}

class DomRetirementTest : public ::testing::Test {
protected:
    Input input = {};
    DomDocument doc;
    Element* backing = nullptr;

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
        backing = elmt_arena(doc.node_arena);
        ASSERT_NE(backing, nullptr);
    }

    void TearDown() override {
        doc.destroy();
    }

    DomElement* element(const char* tag) {
        // retirement fixtures need backed nodes; null backing marks layout-only nodes synthetic.
        DomElement* value = DomElement::create(&doc, tag, backing);
        EXPECT_NE(value, nullptr);
        if (value) EXPECT_FALSE(value->is_synthetic());
        return value;
    }

    DomElement* root() {
        DomElement* value = element("root");
        doc.root = lam::up(value);
        return value;
    }

    bool attach(DomElement* parent, DomElement* child) {
        // lifecycle tests link the DOM chain without editing Lambda content.
        return static_cast<DomNode*>(parent)->append_child(child);
    }

    CssTransitionElemState* transition_snapshot(DomElement* child) {
        auto* snapshot = static_cast<CssTransitionElemState*>(pool_calloc(
            doc.document_pool, sizeof(CssTransitionElemState)));
        if (!snapshot) return nullptr;
        snapshot->pool = doc.document_pool;
        if (!lam::pool_grow_array(snapshot->pool, &snapshot->tracks,
                &snapshot->track_capacity, 1, 4)) {
            pool_free(doc.document_pool, snapshot);
            return nullptr;
        }
        snapshot->track_count = 1;
        child->set_transition_state_prop(snapshot);
        return snapshot;
    }
};

TEST_F(DomRetirementTest, FocusValidationUsesOwnedRootAfterReflow) {
    DomElement* owned_root = root();
    DomElement* parser_document = element("#document");
    DomElement* button = element("button");
    ASSERT_TRUE(attach(parser_document, owned_root));
    ASSERT_TRUE(attach(owned_root, button));
    owned_root->view_type = parser_document->view_type = button->view_type = RDT_VIEW_BLOCK;
    ASSERT_NE(state_store_create(&doc), nullptr);
    DocState* state = doc.state;

    // defer mutation assertions so the test can inspect the post-prune invariant.
    state->transition_depth++;
    focus_set_programmatic(state, button);
    state_store_prune_after_reflow(state);
    EXPECT_EQ(focus_get(state), button);
    EXPECT_TRUE(state_get_bool(state, owned_root, STATE_FOCUS_WITHIN));
    EXPECT_FALSE(state_get_bool(state, parser_document, STATE_FOCUS_WITHIN));
    StateValidationReport report{};
    EXPECT_TRUE(radiant_state_validate_interaction(state, &report)) << report.message;
    state_set_bool(state, owned_root, STATE_FOCUS_WITHIN, false);
    EXPECT_FALSE(radiant_state_validate_interaction(state, &report));
    EXPECT_STREQ(report.message, ":focus-within ancestry is inconsistent");
    state_set_bool(state, owned_root, STATE_FOCUS_WITHIN, true);
    EXPECT_TRUE(parser_document->remove_child(owned_root));
    state->transition_depth--;
}

class DomGcBackingTest : public DomRetirementTest {
protected:
    Runtime runtime = {};
    EvalContext evaluator = {};
    Heap heap = {};
    TypeElmt data_type = EmptyElmt;
    DomDocument target_doc;
    Runtime target_runtime = {};
    EvalContext target_evaluator = {};
    Heap target_heap = {};
    bool target_initialized = false;

    void SetUp() override {
        DomRetirementTest::SetUp();
        heap.gc = gc_heap_create();
        ASSERT_NE(heap.gc, nullptr);
        evaluator.heap = &heap;
        runtime.eval_context = &evaluator;
        doc.lambda_runtime = &runtime;
    }

    void TearDown() override {
        // the test owns its evaluator; release native roots before its heap.
        if (target_initialized) {
            dom_lifecycle_release_backing_roots(&target_doc);
            target_doc.lambda_runtime = nullptr;
            target_doc.destroy();
            gc_heap_destroy(target_heap.gc);
        }
        dom_lifecycle_release_backing_roots(&doc);
        doc.lambda_runtime = nullptr;
        DomRetirementTest::TearDown();
        gc_heap_destroy(heap.gc);
    }

    DomElement* runtime_element(int64_t children = 0, bool attributes = false) {
        Element* source = static_cast<Element*>(gc_heap_calloc(
            heap.gc, sizeof(Element), LMD_TYPE_ELEMENT));
        if (!source) return nullptr;
        source->type_id = LMD_TYPE_ELEMENT;
        source->type = &EmptyElmt;
        if (attributes) {
            data_type.byte_size = sizeof(uint64_t);
            source->type = &data_type;
            source->data = gc_data_alloc(heap.gc, data_type.byte_size);
            source->data_cap = data_type.byte_size;
        }
        if (children) {
            source->items = static_cast<Item*>(gc_data_alloc(heap.gc, sizeof(Item) * children));
            source->length = source->capacity = children;
        }
        return DomElement::create(&doc, "span", source);
    }

    String* runtime_string() {
        const char* content = "retained text";
        size_t length = strlen(content);
        String* string = static_cast<String*>(gc_heap_calloc(
            heap.gc, sizeof(String) + length + 1, LMD_TYPE_STRING));
        if (!string) return nullptr;
        string->len = length;
        memcpy(string->chars, content, length + 1);
        return string;
    }

    void init_adoption_target(bool separate_heap) {
        ASSERT_TRUE(target_doc.init(&input));
        target_initialized = true;
        target_heap.gc = gc_heap_create();
        ASSERT_NE(target_heap.gc, nullptr);
        target_evaluator.heap = separate_heap ? &target_heap : &heap;
        target_runtime.eval_context = &target_evaluator;
        target_doc.lambda_runtime = &target_runtime;
    }

    void check_adoption(bool separate_heap) {
        init_adoption_target(separate_heap);
        ASSERT_TRUE(target_initialized);
        DomElement* child = runtime_element(1);
        ASSERT_NE(child, nullptr);
        Element* source = dom_element_render_source(child);
        Item* original_items = source->items;
        // adoption must refresh a borrow even when nobody read it after collection.
        gc_collect(heap.gc, nullptr, 0);
        ASSERT_NE(source->items, original_items);
        uint32_t id = dom_document_alloc_node_id(&target_doc);
        ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, child, &id));
        static_cast<DomNode*>(child)->id = id;
        child->doc = lam::up(&target_doc);
        ASSERT_EQ(heap.gc->root_slot_count, 1);
        EXPECT_EQ(target_heap.gc->root_slot_count, 0);
        dom_lifecycle_release_backing_roots(&doc);
        gc_collect(heap.gc, nullptr, 0);
        EXPECT_EQ(heap.gc->object_count, 1u);
        EXPECT_EQ(dom_element_to_element(child)->items, source->items);
        id = dom_document_alloc_node_id(&doc);
        ASSERT_TRUE(dom_node_registry_transfer(&target_doc, &doc, child, &id));
        static_cast<DomNode*>(child)->id = id;
        child->doc = lam::up(&doc);
        EXPECT_EQ(heap.gc->root_slot_count, 1);
        dom_lifecycle_release_backing_roots(&target_doc);
        EXPECT_EQ(heap.gc->root_slot_count, 1);
        dom_node_schedule_detached(&doc, child);
        EXPECT_EQ(dom_retire_sweep(&doc), 1u);
        EXPECT_EQ(heap.gc->root_slot_count, 0);
        gc_collect(heap.gc, nullptr, 0);
        EXPECT_EQ(heap.gc->object_count, 0u);
    }
};

TEST_F(DomGcBackingTest, DetachedWrapperRetainsBackingUntilUnpinnedRetirement) {
    DomElement* parent = root();
    DomElement* child = runtime_element();
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, RecycledNodesDoNotAccumulateBackingRoots) {
    DomElement* parent = root();
    for (int i = 0; i < 1024; i++) {
        DomElement* child = runtime_element();
        ASSERT_NE(child, nullptr);
        ASSERT_EQ(heap.gc->root_slot_count, 1);
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        ASSERT_EQ(heap.gc->root_slot_count, 0);
        gc_collect(heap.gc, nullptr, 0);
        ASSERT_EQ(heap.gc->object_count, 0u);
    }
}

TEST_F(DomGcBackingTest, RuntimeTeardownDropsAttachedRootsIdempotently) {
    ASSERT_NE(runtime_element(), nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    dom_lifecycle_release_backing_roots(&doc);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    dom_lifecycle_release_backing_roots(&doc);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, BorrowedContentBufferFollowsCompaction) {
    DomElement* child = runtime_element(1);
    ASSERT_NE(child, nullptr);
    Element* source = dom_element_render_source(child);
    Item* original = source->items;
    ASSERT_EQ(dom_element_to_element(child)->items, original);
    gc_collect(heap.gc, nullptr, 0);
    ASSERT_NE(source->items, original);
    EXPECT_EQ(dom_element_to_element(child)->items, source->items);
}

TEST_F(DomGcBackingTest, BorrowedAttributeBufferFollowsCompaction) {
    DomElement* child = runtime_element(0, true);
    ASSERT_NE(child, nullptr);
    Element* source = dom_element_render_source(child);
    void* original = source->data;
    gc_collect(heap.gc, nullptr, 0);
    ASSERT_NE(source->data, original);
    EXPECT_EQ(dom_element_to_element(child)->data, source->data);
}

TEST_F(DomGcBackingTest, IndependentDomBuffersSurviveLaterOwnerCompaction) {
    DomElement* child = runtime_element(1, true);
    ASSERT_NE(child, nullptr);
    Element* backing = dom_element_to_element(child);
    void* owned_data = arena_calloc(doc.node_arena, sizeof(uint64_t));
    Item* owned_items = static_cast<Item*>(arena_calloc(doc.node_arena, sizeof(Item)));
    backing->data = owned_data;
    backing->items = owned_items;
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(dom_element_to_element(child)->data, owned_data);
    EXPECT_EQ(dom_element_to_element(child)->items, owned_items);
}

TEST_F(DomGcBackingTest, AdoptionAndReturnKeepPhysicalHeapRoot) {
    check_adoption(true);
}

TEST_F(DomGcBackingTest, SameHeapAdoptionRefreshesBorrowedPointers) {
    check_adoption(false);
}

struct BackingRootTeardownProbe : DomDocumentResourceData {
    gc_heap_t* gc;
    bool called;
};

static void backing_root_teardown_probe(DomDocumentResourceData* resource) {
    auto* probe = static_cast<BackingRootTeardownProbe*>(resource);
    // adopted-document resources may destroy the physical source heap here.
    EXPECT_EQ(probe->gc->root_slot_count, 0);
    probe->called = true;
}

TEST_F(DomGcBackingTest, BackingRootsWithdrawBeforeDocumentResourceTeardown) {
    ASSERT_NE(runtime_element(), nullptr);
    BackingRootTeardownProbe probe = {};
    probe.gc = heap.gc;
    ASSERT_TRUE(dom_document_add_resource(&doc, &probe, backing_root_teardown_probe));
    doc.lambda_runtime = nullptr;
    doc.destroy();
    EXPECT_TRUE(probe.called);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
}

TEST_F(DomGcBackingTest, DetachedTextRetainsItsRuntimeStringUntilRetirement) {
    String* source = runtime_string();
    ASSERT_NE(source, nullptr);
    DomText* text = DomText::create_detached(source, &doc);
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    DomNodeRef ref = dom_node_ref(text);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    dom_node_schedule_detached(&doc, text);
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    EXPECT_STREQ(text->text, "retained text");
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, ReplacingTextBackingReleasesOldRuntimeString) {
    DomText* text = DomText::create_detached(runtime_string(), &doc);
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 1);
    String* replacement = dom_document_create_string(&doc, "edited", 6);
    ASSERT_NE(replacement, nullptr);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &doc, replacement));
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
    EXPECT_STREQ(text->text, "edited");
    dom_node_schedule_detached(&doc, text);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomGcBackingTest, TextAdoptionKeepsPhysicalStringHeapRoot) {
    init_adoption_target(true);
    ASSERT_TRUE(target_initialized);
    DomText* text = DomText::create_detached(runtime_string(), &doc);
    ASSERT_NE(text, nullptr);
    uint32_t id = dom_document_alloc_node_id(&target_doc);
    ASSERT_TRUE(dom_node_registry_transfer(&doc, &target_doc, text, &id));
    text->id = id;
    EXPECT_EQ(heap.gc->root_slot_count, 1);
    EXPECT_EQ(target_heap.gc->root_slot_count, 0);
    dom_lifecycle_release_backing_roots(&doc);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 1u);
    EXPECT_STREQ(text->text, "retained text");
    dom_node_schedule_detached(&target_doc, text);
    EXPECT_EQ(dom_retire_sweep(&target_doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomGcBackingTest, DetachedCommentRetainsRuntimeContentUntilRetirement) {
    DomElement* element = runtime_element(1);
    ASSERT_NE(element, nullptr);
    Element* source = dom_element_render_source(element);
    TypeElmt comment_type = EmptyElmt;
    comment_type.name.str = "!--";
    comment_type.name.length = 3;
    source->type = &comment_type;
    source->items[0] = Item{.item = s2it(runtime_string())};
    DomComment* comment = DomComment::create_detached(source, &doc);
    ASSERT_NE(comment, nullptr);
    ASSERT_EQ(heap.gc->root_slot_count, 2);
    dom_node_schedule_detached(&doc, element);
    ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 1);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 2u);
    EXPECT_STREQ(comment->content, "retained text");
    dom_node_schedule_detached(&doc, comment);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(heap.gc->root_slot_count, 0);
    gc_collect(heap.gc, nullptr, 0);
    EXPECT_EQ(heap.gc->object_count, 0u);
}

TEST_F(DomRetirementTest, UnpinnedDetachedNodeRetiresAndRejectsStaleRef) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);

    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, 1u);
    EXPECT_EQ(stats.retired_primary_bytes, sizeof(DomElement));
    EXPECT_EQ(stats.stale_ref_rejections, 1u);
}

TEST_F(DomRetirementTest, PinBlocksRetirementUntilReleased) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
    ASSERT_TRUE(dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomRetirementTest, ReinsertionCancelsDetachedCandidate) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);

    ASSERT_TRUE(parent->remove_child(child));
    ASSERT_TRUE(attach(parent, child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
}

TEST_F(DomRetirementTest, PinnedDescendantBlocksBottomUpSubtreeRetirement) {
    DomElement* outer = root();
    DomElement* branch = element("branch");
    DomElement* leaf = element("leaf");
    ASSERT_TRUE(attach(outer, branch));
    ASSERT_TRUE(attach(branch, leaf));
    DomNodeRef leaf_ref = dom_node_ref(leaf);
    ASSERT_TRUE(dom_node_pin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));

    ASSERT_TRUE(outer->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    ASSERT_TRUE(dom_node_unpin(&doc, leaf_ref, DOM_NODE_PIN_RANGE));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
}

TEST_F(DomRetirementTest, UnregisteredLayoutBoxDoesNotRetainAuthoredSubtree) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    DomElement* generated = DomElement::create_in(doc.document_pool);
    ASSERT_NE(generated, nullptr);
    ASSERT_TRUE(generated->is_synthetic());
    ASSERT_TRUE(attach(parent, branch));
    ASSERT_TRUE(attach(branch, generated));
    DomNodeRef ref = dom_node_ref(branch);

    ASSERT_TRUE(parent->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
}

TEST_F(DomRetirementTest, RetirementRepairsStaleEdgesOnceForTheWholeBatch) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    ASSERT_TRUE(attach(parent, branch));
    for (int i = 0; i < 1024; i++) {
        ASSERT_TRUE(attach(branch, element("leaf")));
    }
    DomElement* retained = element("retained");
    ASSERT_TRUE(attach(parent, retained));

    // A backing-tree edit can leave raw edges in a surviving wrapper.
    retained->first_child = branch->first_child;
    retained->last_child = branch->last_child;
    ASSERT_TRUE(parent->remove_child(branch));
    retained->prev_sibling = lam::up(branch);
    EXPECT_EQ(dom_retire_sweep(&doc), 1025u);
    EXPECT_EQ(retained->first_child, nullptr);
    EXPECT_EQ(retained->last_child, nullptr);
    EXPECT_EQ(retained->prev_sibling, nullptr);
    EXPECT_EQ(parent->first_child, retained);
    EXPECT_EQ(parent->last_child, retained);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retirement_edge_visits, stats.registered_nodes);
}

TEST_F(DomRetirementTest, ConsumedMutationRecordsReleaseDetachedSubtree) {
    DomElement* parent = root();
    DomElement* branch = element("branch");
    DomElement* leaf = element("leaf");
    ASSERT_TRUE(attach(parent, branch));
    ASSERT_TRUE(attach(branch, leaf));
    DomNodeRef ref = dom_node_ref(leaf);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_RECONCILE));
    doc.js.mutation_record_count = 1;
    doc.js.mutation_records[0].target = leaf;
    doc.js.mutation_records[0].target_id = ref.expected_id;
    ASSERT_TRUE(parent->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);

    dom_js_mutation_records_reset(&doc);
    EXPECT_EQ(doc.js.mutation_record_count, 0);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, 2u);
}

TEST_F(DomRetirementTest, GeneratedTextPayloadIsFreedWithItsNode) {
    DomElement* parent = root();
    DomText* text = DomText::create_copy("before", 6, parent);
    ASSERT_NE(text, nullptr);
    String* replacement = dom_document_create_string(&doc, "after", 5);
    ASSERT_NE(replacement, nullptr);
    ASSERT_TRUE(dom_text_adopt_document_string(text, &doc, replacement));
    ASSERT_TRUE(parent->append_child(text));
    PoolStats before = {};
    pool_get_detailed_stats(doc.document_pool, &before);

    ASSERT_TRUE(parent->remove_child(text));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_GT(after.free_count, before.free_count);
}

TEST_F(DomRetirementTest, TransitionSnapshotLivesUntilUnpinnedRetirement) {
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    auto* snapshot = transition_snapshot(child);
    ASSERT_NE(snapshot, nullptr);
    CssTransitionTrack* tracks = snapshot->tracks;
    snapshot->tracks[0].has_snapshot = true;
    snapshot->tracks[0].snapshot.value.f = 0.25f;
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_WRAPPER));

    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_TRUE(pool_owns(doc.document_pool, snapshot));
    EXPECT_TRUE(pool_owns(doc.document_pool, tracks));
    EXPECT_EQ(child->transition_state_prop(), snapshot);
    EXPECT_FLOAT_EQ(snapshot->tracks[0].snapshot.value.f, 0.25f);

    dom_node_unpin(&doc, ref, DOM_NODE_PIN_WRAPPER);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    EXPECT_FALSE(pool_owns(doc.document_pool, snapshot));
    EXPECT_FALSE(pool_owns(doc.document_pool, tracks));
}

TEST_F(DomRetirementTest, TransitionSnapshotStoragePlateausAcrossRetirements) {
    DomElement* parent = root();
    PoolStats warm = {};
    for (int i = 0; i < 1056; i++) {
        DomElement* child = element("child");
        auto* snapshot = transition_snapshot(child);
        ASSERT_NE(snapshot, nullptr);
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        if (i == 31) pool_get_detailed_stats(doc.document_pool, &warm);
    }
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, warm.live_bytes);
    EXPECT_GE(after.free_count - warm.free_count, 1024u);
}

struct DeferredDomRetirementScope {
    bool previous = dom_retire_set_deferred(true);
    ~DeferredDomRetirementScope() { dom_retire_set_deferred(previous); }
};

TEST_F(DomRetirementTest, DeferredRetirementWaitsForIdleAndRecyclesLater) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(parent->remove_child(child));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);

    // A zero recycle budget still commits logical retirement at the safe point.
    EXPECT_TRUE(dom_retire_idle(0));
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_GT(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 0u);
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 1u);
}

TEST_F(DomRetirementTest, ReattachedNodeSurvivesDeferredSweep) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    DomNodeRef ref = dom_node_ref(child);
    ASSERT_TRUE(parent->remove_child(child));
    dom_retire_sweep(&doc);
    ASSERT_TRUE(attach(parent, child));
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    EXPECT_EQ(dom_node_ref_validate(&doc, ref), child);
}

TEST_F(DomRetirementTest, DestroyDiscardsQueuedArenaRecycling) {
    DeferredDomRetirementScope deferred;
    DomElement* parent = root();
    DomElement* child = element("child");
    ASSERT_TRUE(attach(parent, child));
    ASSERT_TRUE(parent->remove_child(child));
    dom_retire_sweep(&doc);
    EXPECT_TRUE(dom_retire_idle(0));
    dom_retire_begin_destroy(&doc);
    EXPECT_FALSE(dom_retire_idle(UINT64_MAX));
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.pending_primary_bytes, 0u);
    EXPECT_EQ(stats.recycled_nodes, 0u);
}

TEST_F(DomRetirementTest, RetiredTextLeavesBorrowedAncestorFontAllocated) {
    // the view tree owns the layout props a retirement sweep returns
    ViewTree tree = {};
    tree.prop_pool = lam::own(pool_create());
    ASSERT_NE(tree.prop_pool, nullptr);
    view_tree_canonical_init(&tree);
    doc.view_tree = lam::own(&tree);

    // layout_text points a text node at the FontProp of its nearest
    // font-owning ancestor; an unstyled span owns none, so its text borrows
    // the live parent's
    DomElement* parent = root();
    DomElement* span = element("span");
    ASSERT_TRUE(attach(parent, span));
    DomText* text = DomText::create_copy("world", 5, span);
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(span->append_child(text));
    FontProp* owned = (FontProp*)tree.alloc_prop(sizeof(FontProp));
    parent->font = lam::view_prop(owned);
    text->font = lam::view_ref(owned);
    TextRect* rect = tree.alloc_text_rect();
    ASSERT_NE(rect, nullptr);
    text->rect = lam::view_prop(rect);

    ASSERT_TRUE(parent->remove_child(span));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    // the retired borrower only drops its pointer; freeing the prop left the
    // live parent's font dangling
    EXPECT_TRUE(pool_owns(tree.prop_pool, owned));
    EXPECT_EQ(parent->font, owned);
    EXPECT_EQ(tree.alloc_text_rect(), rect);

    parent->font = nullptr;
    doc.view_tree = nullptr;
    view_tree_canonical_destroy(&tree);
    pool_destroy(tree.prop_pool);
}

TEST_F(DomRetirementTest, NodeArenaGrowthPlateausAcrossTenThousandRetirements) {
    DomElement* parent = root();
    for (int i = 0; i < 128; i++) {
        DomElement* child = element("child");
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats warm = {};
    arena_get_stats(doc.node_arena, &warm);

    uint32_t last_id = 0;
    for (int i = 0; i < 10000; i++) {
        DomElement* child = element("child");
        uint32_t node_id = static_cast<DomNode*>(child)->id;
        ASSERT_GT(node_id, last_id);
        last_id = node_id;
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GE(after.bump_back_count - warm.bump_back_count, 10000u);
}

TEST_F(DomRetirementTest, RecycledNodesReleaseTheirRegistryRecords) {
    DomElement* parent = root();
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    DomLifecycleStats warm_stats = {};
    dom_lifecycle_get_stats(&doc, &warm_stats);
    DomElement* children[64] = {};
    DomNodeRef refs[64] = {};
    for (int i = 0; i < 64; i++) {
        children[i] = element("child");
        ASSERT_NE(children[i], nullptr);
        refs[i] = dom_node_ref(static_cast<DomNode*>(children[i]));
        ASSERT_TRUE(attach(parent, children[i]));
    }
    for (int i = 0; i < 64; i++) ASSERT_TRUE(parent->remove_child(children[i]));
    EXPECT_EQ(dom_retire_sweep(&doc), 64u);
    // a record outlives no recycled slot; a stale ref still fails validation
    PoolStats after = {};
    pool_get_detailed_stats(doc.document_pool, &after);
    EXPECT_EQ(after.live_bytes, warm.live_bytes);
    for (int i = 0; i < 64; i++) EXPECT_EQ(dom_node_ref_validate(&doc, refs[i]), nullptr);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.recycled_nodes - warm_stats.recycled_nodes, 64u);
    // the recycled slots are reused by the next generation of nodes
    DomElement* next = element("child");
    ASSERT_NE(next, nullptr);
    EXPECT_NE(dom_node_ref_validate(&doc, dom_node_ref(static_cast<DomNode*>(next))), nullptr);
}

TEST_F(DomRetirementTest, WrapperOnlyDetachedSubtreesReportStrandedBytesForIdleCollection) {
    DomElement* parent = root();
    DomElement* held = element("held");
    DomElement* held_child = element("child");
    DomElement* observed = element("observed");
    ASSERT_TRUE(attach(parent, held));
    ASSERT_TRUE(attach(held, held_child));
    ASSERT_TRUE(attach(parent, observed));
    DomNodeRef held_ref = dom_node_ref(static_cast<DomNode*>(held));
    DomNodeRef observed_ref = dom_node_ref(static_cast<DomNode*>(observed));
    // a wrapper on a descendant strands the whole detached subtree; an observer
    // pin is not a wrapper, so that subtree waits on something a GC cannot clear
    ASSERT_TRUE(dom_node_pin(&doc, dom_node_ref(static_cast<DomNode*>(held_child)), DOM_NODE_PIN_WRAPPER));
    ASSERT_TRUE(dom_node_pin(&doc, observed_ref, DOM_NODE_PIN_OBSERVER));
    ASSERT_TRUE(parent->remove_child(held));
    ASSERT_TRUE(parent->remove_child(observed));
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);

    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    ASSERT_GT(stats.wrapper_stranded_bytes, 0u);
    size_t stranded = stats.wrapper_stranded_bytes;
    EXPECT_TRUE(dom_retire_wrapper_collection_due(&doc, stranded));
    EXPECT_FALSE(dom_retire_wrapper_collection_due(&doc, stranded + 1));
    // repeated sweeps charge a detachment once
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.wrapper_stranded_bytes, stranded);
    // a collection that leaves the wrapper alive resets pacing to the survivors
    dom_retire_wrapper_collection_done(&doc);
    EXPECT_FALSE(dom_retire_wrapper_collection_due(&doc, 1));

    // clearing the wrapper (what the collection does) retires the subtree
    ASSERT_TRUE(dom_node_unpin(&doc, dom_node_ref(static_cast<DomNode*>(held_child)), DOM_NODE_PIN_WRAPPER));
    EXPECT_EQ(dom_retire_sweep(&doc), 2u);
    EXPECT_EQ(dom_node_ref_validate(&doc, held_ref), nullptr);
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.wrapper_stranded_bytes, 0u);
    ASSERT_TRUE(dom_node_unpin(&doc, observed_ref, DOM_NODE_PIN_OBSERVER));
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
}

TEST_F(DomRetirementTest, MoreThanMutationRecordCapRetiresAfterPinsRelease) {
    DomElement* parent = root();
    DomNodeRef refs[DOM_JS_MUTATION_RECORD_CAP * 4] = {};
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomElement* child = element("held");
        refs[i] = dom_node_ref(child);
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_pin(&doc, refs[i], reason));
        ASSERT_TRUE(attach(parent, child));
        ASSERT_TRUE(parent->remove_child(child));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    for (int i = 0; i < DOM_JS_MUTATION_RECORD_CAP * 4; i++) {
        DomNodePinReason reason = (DomNodePinReason)(i % DOM_NODE_PIN_REASON_COUNT);
        ASSERT_TRUE(dom_node_unpin(&doc, refs[i], reason));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), DOM_JS_MUTATION_RECORD_CAP * 4u);
}

TEST_F(DomRetirementTest, MutationJournalGrowsPreservingOrderAndReusesStorage) {
    DomElement* parent = root();
    const int count = DOM_JS_MUTATION_RECORD_CAP * 4;
    for (int i = 0; i < count; i++) {
        DomElement* child = element("journal");
        ASSERT_TRUE(attach(parent, child));
        DomNodeRef ref = dom_node_ref(child);
        ASSERT_TRUE(dom_node_pin(&doc, ref, DOM_NODE_PIN_RECONCILE));
        ASSERT_TRUE(dom_js_mutation_records_reserve(&doc, i + 1));
        DomJsMutationRecord* record = &doc.js.mutation_records[i];
        *record = {};
        record->sequence = i + 1;
        record->target = child;
        record->target_id = ref.expected_id;
        doc.js.mutation_record_count++;
        ASSERT_TRUE(parent->remove_child(child));
    }
    EXPECT_EQ(dom_retire_sweep(&doc), 0u);
    for (int i = 0; i < count; i++) {
        EXPECT_EQ(doc.js.mutation_records[i].sequence, i + 1);
        EXPECT_NE(dom_node_ref_validate(&doc, {doc.js.mutation_records[i].target,
                  doc.js.mutation_records[i].target_id}), nullptr);
    }
    DomJsMutationRecord* retained = doc.js.mutation_records;
    int capacity = doc.js.mutation_record_capacity;
    dom_js_mutation_records_reset(&doc);
    EXPECT_EQ(doc.js.mutation_record_count, 0);
    EXPECT_EQ(doc.js.mutation_record_overflow, 0);
    DomLifecycleStats stats = {};
    dom_lifecycle_get_stats(&doc, &stats);
    EXPECT_EQ(stats.retired_nodes, count);
    ASSERT_TRUE(dom_js_mutation_records_reserve(&doc, count));
    EXPECT_EQ(doc.js.mutation_records, retained);
    EXPECT_EQ(doc.js.mutation_record_capacity, capacity);
}

TEST_F(DomRetirementTest, VariableTextSizesReuseArenaBlocksAfterWarmup) {
    DomElement* parent = root();
    char text[513];
    memset(text, 'x', sizeof(text));
    for (size_t len = 1; len <= 512; len++) {
        DomText* node = DomText::create_copy(text, len, parent);
        ASSERT_NE(node, nullptr);
        ASSERT_TRUE(parent->append_child(node));
        ASSERT_TRUE(parent->remove_child(node));
        ASSERT_EQ(dom_retire_sweep(&doc), 1u);
    }
    ArenaStats warm = {};
    arena_get_stats(doc.node_arena, &warm);
    for (int cycle = 0; cycle < 20; cycle++) {
        for (size_t len = 1; len <= 512; len++) {
            DomText* node = DomText::create_copy(text, len, parent);
            ASSERT_NE(node, nullptr);
            ASSERT_TRUE(parent->append_child(node));
            ASSERT_TRUE(parent->remove_child(node));
            ASSERT_EQ(dom_retire_sweep(&doc), 1u);
        }
    }
    ArenaStats after = {};
    arena_get_stats(doc.node_arena, &after);
    EXPECT_EQ(after.fresh_growth_bytes, warm.fresh_growth_bytes);
    EXPECT_GT(after.bump_back_count, warm.bump_back_count);
}

TEST(DomRetirementOwnerArenaTest, FatLambdaNodeReturnsToItsInputArena) {
    Pool* input_pool = pool_create();
    ASSERT_NE(input_pool, nullptr);
    Arena* input_arena = arena_create_default();
    ASSERT_NE(input_arena, nullptr);
    Input input = {};
    input.arena = input_arena;
    DomDocument doc;
    ASSERT_TRUE(doc.init(&input));

    DomElement* root = DomElement::create(&doc, "root", nullptr);
    ASSERT_NE(root, nullptr);
    doc.root = lam::up(root);
    DomElement* storage = DomElement::create_in(input_arena);
    ASSERT_NE(storage, nullptr);
    Element* backing = elmt_arena(input_arena);
    ASSERT_NE(backing, nullptr);
    DomElement* child = DomElement::create_in(
        storage, &doc, "child", backing);
    ASSERT_NE(child, nullptr);
    ASSERT_FALSE(child->is_synthetic());
    ASSERT_TRUE(static_cast<DomNode*>(root)->append_child(child));
    ASSERT_TRUE(root->remove_child(child));

    ArenaStats before = {};
    arena_get_stats(input_arena, &before);
    EXPECT_EQ(dom_retire_sweep(&doc), 1u);
    ArenaStats after = {};
    arena_get_stats(input_arena, &after);
    EXPECT_EQ(after.retire_count, before.retire_count + 1u);

    doc.destroy();
    arena_destroy(input_arena);
    pool_destroy(input_pool);
}

TEST(DomRetirementOwnerArenaTest, FlattenedArrayTextIsRegisteredAndRetired) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    Item first = {.item = s2it(builder.createDomTextString("first", 5))};
    Item second = {.item = s2it(builder.createDomTextString("second", 6))};
    Item nested = builder.array().append(second).final();
    Item children = builder.array().append(first).append(nested).final();
    Item branch_source = builder.element("branch").child(children).final();
    Item root_source = builder.element("root").child(branch_source).final();
    doc.root = lam::up(build_dom_tree_from_element(root_source.element, &doc, nullptr));
    ASSERT_NE(doc.root, nullptr);
    DomElement* branch = doc.root->first_child->as_element();
    DomNode* first_node = branch->first_child;
    DomNode* second_node = first_node->next_sibling;
    ASSERT_NE(second_node, nullptr);
    DomNodeRef first_ref = dom_node_ref(first_node);
    DomNodeRef second_ref = dom_node_ref(second_node);
    EXPECT_EQ(dom_node_ref_validate(&doc, first_ref), first_node);
    EXPECT_EQ(dom_node_ref_validate(&doc, second_ref), second_node);

    ASSERT_TRUE(static_cast<DomNode*>(doc.root)->remove_child(branch));
    EXPECT_EQ(dom_retire_sweep(&doc), 3u);
    EXPECT_EQ(dom_node_ref_validate(&doc, first_ref), nullptr);
    EXPECT_EQ(dom_node_ref_validate(&doc, second_ref), nullptr);
    doc.destroy();
    pool_destroy(pool);
}

TEST(DomAttributeValueTest, RepeatedValuesReuseInputStorageAndPreserveSnapshots) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Input* input = Input::create(pool, nullptr);
    ASSERT_NE(input, nullptr);
    input->ui_mode = true;
    DomDocument doc;
    ASSERT_TRUE(doc.init(input));
    MarkBuilder builder(input);
    Item source = builder.element("div").attr("id", "kept").final();
    auto* element = build_dom_tree_from_element(source.element, &doc, nullptr);
    ASSERT_NE(element, nullptr);
    doc.root = lam::up(element);
    ASSERT_TRUE(element->set_attribute("aria-hidden", "true"));
    const char* saved = element->get_attribute("aria-hidden");
    ASSERT_TRUE(element->set_attribute("aria-hidden", "false"));
    ASSERT_TRUE(element->set_attribute("inert", ""));
    ASSERT_TRUE(element->remove_attribute("inert"));
    size_t warm = arena_total_used(input->arena);
    for (size_t i = 0; i < 256; i++) {
        ASSERT_TRUE(element->set_attribute("aria-hidden", i % 2 ? "true" : "false"));
        ASSERT_TRUE(element->set_attribute("inert", ""));
        ASSERT_TRUE(element->remove_attribute("inert"));
    }
    EXPECT_EQ(arena_total_used(input->arena), warm);
    EXPECT_STREQ(saved, "true");
    EXPECT_STREQ(element->get_attribute("aria-hidden"), "true");
    EXPECT_STREQ(element->get_attribute("id"), "kept");
    doc.destroy();
    pool_destroy(pool);
}

class StyleEpochTest : public ::testing::Test {
protected:
    Input input = {};
    DomDocument doc;
    CssEngine engine = {};
    DomElement* document_root = nullptr;

    void SetUp() override {
        ASSERT_TRUE(doc.init(&input));
        engine.context.viewport_width = 1280.0;
        engine.context.viewport_height = 720.0;
        engine.context.device_pixel_ratio = 2.0;
        document_root = DomElement::create(&doc, "root", nullptr);
        ASSERT_NE(document_root, nullptr);
        doc.root = lam::up(document_root);
    }

    void TearDown() override {
        doc.destroy();
    }

    DomElement* append(const char* tag) {
        DomElement* child = DomElement::create(&doc, tag, nullptr);
        EXPECT_NE(child, nullptr);
        EXPECT_TRUE(document_root->append_child(child));
        return child;
    }

    CssRule* rule(CssPropertyCode property, CssValue* value,
                  uint32_t source_order = 1) {
        CssDeclaration* declaration = css_declaration_create(
            property, value, {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
        EXPECT_NE(declaration, nullptr);
        declaration->source_order = source_order;
        declaration->value_text = "snapshot";
        declaration->value_text_len = 8;

        CssRule* result = (CssRule*)pool_calloc(doc.document_pool, sizeof(CssRule));
        EXPECT_NE(result, nullptr);
        result->type = CSS_RULE_STYLE;
        result->pool = doc.document_pool;
        result->origin = CSS_ORIGIN_AUTHOR;
        result->source_order = source_order;
        result->data.style_rule.declarations = (CssDeclaration**)pool_alloc(
            doc.document_pool, sizeof(CssDeclaration*));
        EXPECT_NE(result->data.style_rule.declarations, nullptr);
        result->data.style_rule.declarations[0] = declaration;
        result->data.style_rule.declaration_count = 1;
        return result;
    }

    void apply(DomElement* root, CssRule* first, DomElement* a,
               DomElement* b = nullptr, bool global_change = false) {
        ASSERT_TRUE(style_epoch_cascade_begin(
            &doc, root, &engine, global_change));
        ASSERT_EQ(dom_element_apply_rule(a, first, {}), 1);
        if (b) ASSERT_EQ(dom_element_apply_rule(b, first, {}), 1);
        style_epoch_cascade_end(&doc);
    }
};

TEST_F(StyleEpochTest, ExactRecipesBindOneCanonicalTreeWithoutElementClones) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));

    apply(document_root, width, first, second);

    ASSERT_TRUE(first->specified_style_shared());
    ASSERT_TRUE(second->specified_style_shared());
    EXPECT_EQ(first->specified_style, second->specified_style);
    CssDeclaration* canonical = dom_element_get_specified_value(
        first, CSS_PROPERTY_WIDTH);
    ASSERT_NE(canonical, nullptr);
    EXPECT_FALSE(canonical->owns_payload);
    EXPECT_NE(canonical->payload_owner, nullptr);
    EXPECT_NE(canonical->value,
        width->data.style_rule.declarations[0]->value);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GE(stats.hit_count, 1u);
    EXPECT_EQ(stats.bound_element_refs, 3u);
    EXPECT_GT(stats.current_reserved_bytes, 0u);
}

TEST_F(StyleEpochTest, CanonicalRecipesShareFrozenPayloadSnapshots) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin(&doc, document_root, &engine, false));
    ASSERT_EQ(dom_element_apply_rule(first, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, height, {}), 1);
    style_epoch_cascade_end(&doc);

    CssDeclaration* first_width = dom_element_get_specified_value(
        first, CSS_PROPERTY_WIDTH);
    CssDeclaration* second_width = dom_element_get_specified_value(
        second, CSS_PROPERTY_WIDTH);
    ASSERT_NE(first_width, nullptr);
    ASSERT_NE(second_width, nullptr);
    EXPECT_NE(first->specified_style, second->specified_style);
    EXPECT_EQ(first_width->value, second_width->value);
    EXPECT_EQ(first_width->payload_owner, second_width->payload_owner);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.current_payload_count, 2u);
    EXPECT_EQ(stats.current_payload_ref_count, 3u);
}

TEST_F(StyleEpochTest, ReplaceModeRebuildsWithoutPriorRecipeBase) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, document_root, &engine));
    ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
    style_epoch_cascade_end(&doc);
    ASSERT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);

    dom_element_clear_cascaded_styles(child);
    ASSERT_TRUE(style_epoch_cascade_begin_replace(&doc, child, &engine));
    ASSERT_EQ(dom_element_apply_rule(child, height, {}), 1);
    style_epoch_cascade_end(&doc);

    EXPECT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_HEIGHT), nullptr);
}

TEST_F(StyleEpochTest, InvalidLaterDeclarationPreservesLastValidValue) {
    DomElement* child = append("child");
    CssRule* valid_height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* invalid_height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, -1.0, CSS_UNIT_PX), 2);

    ASSERT_TRUE(style_epoch_cascade_begin(
        &doc, document_root, &engine, false));
    EXPECT_EQ(dom_element_apply_rule(child, valid_height, {}), 1);
    EXPECT_EQ(dom_element_apply_rule(child, invalid_height, {}), 0);
    style_epoch_cascade_end(&doc);

    CssDeclaration* winner = dom_element_get_specified_value(
        child, CSS_PROPERTY_HEIGHT);
    ASSERT_NE(winner, nullptr);
    ASSERT_NE(winner->value, nullptr);
    EXPECT_EQ(winner->value->type, CSS_VALUE_TYPE_LENGTH);
    EXPECT_DOUBLE_EQ(winner->value->data.length.value, 40.0);
}

TEST_F(StyleEpochTest, ForcedHashCollisionStillRequiresExactRecipeEquality) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    DomElement* third = append("third");
    DomElement* fourth = append("fourth");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);
    style_epoch_debug_force_hash_collision(&doc, true);

    ASSERT_TRUE(style_epoch_cascade_begin(&doc, document_root, &engine, false));
    ASSERT_EQ(dom_element_apply_rule(first, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(second, width, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(third, height, {}), 1);
    ASSERT_EQ(dom_element_apply_rule(fourth, height, {}), 1);
    style_epoch_cascade_end(&doc);

    EXPECT_EQ(first->specified_style, second->specified_style);
    EXPECT_EQ(third->specified_style, fourth->specified_style);
    EXPECT_NE(first->specified_style, third->specified_style);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_EQ(dom_element_get_specified_value(first, CSS_PROPERTY_HEIGHT), nullptr);
    EXPECT_NE(dom_element_get_specified_value(third, CSS_PROPERTY_HEIGHT), nullptr);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GT(stats.collision_count, 0u);
}

TEST_F(StyleEpochTest, InlineMutationAndPropertyRemovalCowIndependently) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, first, second);
    StyleTree* canonical = second->specified_style;

    CssSpecificity inline_specificity = {};
    inline_specificity.inline_style = 1;
    CssDeclaration* height = css_declaration_create(
        CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 25.0, CSS_UNIT_PX),
        inline_specificity, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_TRUE(dom_element_apply_declaration(first, height));
    EXPECT_FALSE(first->specified_style_shared());
    EXPECT_EQ(second->specified_style, canonical);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_HEIGHT), nullptr);
    EXPECT_EQ(dom_element_get_specified_value(second, CSS_PROPERTY_HEIGHT), nullptr);

    ASSERT_TRUE(dom_element_remove_property(second, CSS_PROPERTY_WIDTH));
    EXPECT_FALSE(second->specified_style_shared());
    EXPECT_EQ(dom_element_get_specified_value(second, CSS_PROPERTY_WIDTH), nullptr);
    EXPECT_NE(dom_element_get_specified_value(first, CSS_PROPERTY_WIDTH), nullptr);
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.cow_count, 2u);
}

TEST_F(StyleEpochTest, OldAndNewEpochsCoexistThenReleaseAtLastBinding) {
    DomElement* first = append("first");
    DomElement* second = append("second");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, first, second);
    uint64_t old_epoch = style_epoch_current_id(&doc);

    style_epoch_unbind_element(second);
    CssRule* height = rule(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX), 2);
    apply(second, height, second, nullptr, true);
    EXPECT_GT(style_epoch_current_id(&doc), old_epoch);
    StyleEpochStats coexist = {};
    style_epoch_get_stats(&doc, &coexist);
    EXPECT_GT(coexist.retired_referenced_reserved_bytes, 0u);

    style_epoch_unbind_element(first);
    style_epoch_unbind_element(document_root);
    StyleEpochStats released = {};
    style_epoch_get_stats(&doc, &released);
    EXPECT_EQ(released.retired_referenced_reserved_bytes, 0u);
    EXPECT_GE(released.released_epoch_count, 1u);
    EXPECT_TRUE(second->specified_style_shared());
}

TEST_F(StyleEpochTest, MediaEnvironmentChangeAdvancesButLocalRematchDoesNot) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 40.0, CSS_UNIT_PX));
    apply(document_root, width, child);
    uint64_t initial_epoch = style_epoch_current_id(&doc);

    style_epoch_unbind_element(child);
    apply(child, width, child);
    EXPECT_EQ(style_epoch_current_id(&doc), initial_epoch);

    style_epoch_unbind_element(child);
    engine.context.viewport_width = 640.0;
    apply(child, width, child);
    EXPECT_GT(style_epoch_current_id(&doc), initial_epoch);
}

TEST_F(StyleEpochTest, CowValueSnapshotSurvivesSourceMutationAndEpochRelease) {
    DomElement* child = append("child");
    CssValue* source_value = css_value_create_string(doc.document_pool, "epoch-value");
    CssRule* content = rule(CSS_PROPERTY_CONTENT, source_value);
    apply(document_root, content, child);
    ASSERT_TRUE(style_epoch_ensure_owned(child));
    CssDeclaration* owned = dom_element_get_specified_value(
        child, CSS_PROPERTY_CONTENT);
    ASSERT_NE(owned, nullptr);
    ASSERT_NE(owned->value, source_value);
    ASSERT_NE(owned->value->data.string, source_value->data.string);

    char* mutable_source = (char*)source_value->data.string;
    mutable_source[0] = 'X';
    style_epoch_unbind_element(document_root);
    style_epoch_mark_global_change(&doc);
    ASSERT_TRUE(style_epoch_cascade_begin(&doc, child, &engine, false));
    style_epoch_cascade_end(&doc);

    EXPECT_STREQ(owned->value->data.string, "epoch-value");
    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_GE(stats.released_epoch_count, 1u);
}

TEST_F(StyleEpochTest, ConditionEvaluationIsBoundedAndInvalidatesOnEnvironmentChange) {
    const char* media = "(min-width: 1px)";
    const char* supports = "(display: block)";
    CssEngine* conditional_engine = css_engine_create(doc.document_pool);
    ASSERT_NE(conditional_engine, nullptr);
    css_engine_set_viewport(conditional_engine, 1280.0, 720.0);
    ASSERT_TRUE(css_evaluate_media_query(conditional_engine, media));
    ASSERT_TRUE(css_evaluate_supports_condition(conditional_engine, supports));
    uint64_t warm_evaluations = conditional_engine->condition_evaluations;
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);

    for (size_t i = 0; i < 100; i++) {
        EXPECT_TRUE(css_evaluate_media_query(conditional_engine, media));
        EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, supports));
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_EQ(conditional_engine->condition_evaluations, warm_evaluations);
    EXPECT_GE(conditional_engine->condition_cache_hits, 200u);
    EXPECT_EQ(stable.live_bytes, warm.live_bytes);

    css_engine_set_viewport(conditional_engine, 0.0, 720.0);
    EXPECT_FALSE(css_evaluate_media_query(conditional_engine, media));
    EXPECT_GT(conditional_engine->condition_evaluations, warm_evaluations);

    PoolStats before_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &before_uncached);
    EXPECT_TRUE(css_evaluate_media_query(conditional_engine, "(min-height: 1px)"));
    EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, "(color: red)"));
    PoolStats after_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &after_uncached);
    // New keys retain only their bounded engine-owned cache text, not parser
    // scratch or value graphs. Repeating them must add no further storage.
    EXPECT_LE(after_uncached.live_bytes, before_uncached.live_bytes + 4096u);
    EXPECT_TRUE(css_evaluate_media_query(conditional_engine, "(min-height: 1px)"));
    EXPECT_TRUE(css_evaluate_supports_condition(conditional_engine, "(color: red)"));
    PoolStats repeated_uncached = {};
    pool_get_detailed_stats(doc.document_pool, &repeated_uncached);
    EXPECT_EQ(repeated_uncached.live_bytes, after_uncached.live_bytes);
}

TEST_F(StyleEpochTest, RecascadeRetiresStylesheetCustomPropertyRecords) {
    DomElement* child = append("child");
    CssDeclaration* declaration = css_declaration_create(
        CSS_PROPERTY_UNKNOWN, css_value_create_string(doc.document_pool, "blue"),
        {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(declaration, nullptr);
    declaration->property_name = pool_strdup(doc.document_pool, "--theme");
    declaration->value_text = "blue";
    declaration->value_text_len = 4;
    CssRule* custom = (CssRule*)pool_calloc(doc.document_pool, sizeof(CssRule));
    ASSERT_NE(custom, nullptr);
    custom->type = CSS_RULE_STYLE;
    custom->pool = doc.document_pool;
    custom->origin = CSS_ORIGIN_AUTHOR;
    custom->data.style_rule.declarations = (CssDeclaration**)pool_alloc(
        doc.document_pool, sizeof(CssDeclaration*));
    ASSERT_NE(custom->data.style_rule.declarations, nullptr);
    custom->data.style_rule.declarations[0] = declaration;
    custom->data.style_rule.declaration_count = 1;

    for (size_t i = 0; i < 32; i++) {
        dom_element_clear_cascaded_styles(child);
        apply(document_root, custom, child);
        size_t records = 0;
        for (CssCustomProp* prop = child->css_variables; prop; prop = prop->next) records++;
        EXPECT_EQ(records, 1u);
    }
}

TEST_F(StyleEpochTest, RecascadeReclaimsExclusiveOwnedStyleTrees) {
    DomElement* child = append("child");
    CssRule* calc = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));

    // No epoch batch makes this an exclusive, element-owned cascade tree.
    dom_element_apply_rule(child, calc, {});
    PoolStats first = {};
    pool_get_detailed_stats(doc.document_pool, &first);
    for (size_t i = 0; i < 32; i++) {
        dom_element_clear_cascaded_styles(child);
        ASSERT_EQ(dom_element_apply_rule(child, calc, {}), 1);
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, first.live_bytes + 4096u);
}

TEST_F(StyleEpochTest, RecascadeWithInlineStateReclaimsRemovedCascadeRecords) {
    DomElement* child = append("child");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));
    CssSpecificity specificity = {};
    specificity.inline_style = 1;
    CssDeclaration* height = css_declaration_create(CSS_PROPERTY_HEIGHT,
        css_value_create_length(doc.document_pool, 25.0, CSS_UNIT_PX),
        specificity, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_TRUE(dom_element_apply_declaration(child, height));
    ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 128; i++) {
        dom_element_clear_cascaded_styles(child);
        ASSERT_EQ(dom_element_apply_rule(child, width, {}), 1);
        ASSERT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_HEIGHT), height);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_LE(stable.live_bytes, warm.live_bytes + 4096u);
}

TEST_F(StyleEpochTest, MotionInitialValuesDoNotGrowRetainedCallerStorage) {
    DomElement* child = append("child");
    Pool* view_pool = pool_create();
    ASSERT_NE(view_pool, nullptr);
    CssTransitionList first = {};
    css_transition_resolve_config(child, view_pool, &first);
    PoolStats warm = {};
    pool_get_detailed_stats(view_pool, &warm);
    for (size_t i = 0; i < 128; i++) {
        CssTransitionList sampled = {};
        css_transition_resolve_config(child, view_pool, &sampled);
        for (size_t property = 0; property < 4; property++)
            EXPECT_EQ(sampled.values[property], first.values[property]);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(view_pool, &stable);
    EXPECT_EQ(stable.live_bytes, warm.live_bytes);
    pool_destroy(view_pool);
}

TEST_F(StyleEpochTest, ReplacingInlineValuesReclaimsParserPayloads) {
    DomElement* child = append("child");
    const char* styles[] = {
        "transform:translate(1px,2px);opacity:0.5;color:blue;line-height:1.4;--accent:red",
        "transform:translate(3px,4px);opacity:0.8;color:green;line-height:1.6;--accent:blue",
        "transform:none;opacity:1;color:red;line-height:normal;--accent:green"
    };
    for (const char* style : styles) ASSERT_TRUE(child->set_attribute("style", style));
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    // Replacement must retire both ordinary declarations and custom values.
    for (size_t i = 0; i < 192; i++) {
        ASSERT_TRUE(child->set_attribute("style", styles[i % 3]));
        CssDeclaration* transform = dom_element_get_specified_value(child,
            CSS_PROPERTY_TRANSFORM);
        ASSERT_NE(transform, nullptr);
        EXPECT_TRUE(transform->owns_payload);
        ASSERT_NE(child->css_variables, nullptr);
        ASSERT_NE(child->css_variables->declaration, nullptr);
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, warm.live_bytes + 4096u);
    ASSERT_TRUE(child->remove_attribute("style"));
    EXPECT_EQ(dom_element_get_specified_value(child, CSS_PROPERTY_TRANSFORM), nullptr);
    EXPECT_EQ(child->css_variables, nullptr);
}

TEST_F(StyleEpochTest, RejectedRuleDoesNotRetainCascadeCopies) {
    DomElement* child = append("child");
    CssRule* invalid = rule(CSS_PROPERTY_WIDTH,
        css_value_create_string(doc.document_pool, "invalid-width"));
    ASSERT_EQ(dom_element_apply_rule(child, invalid, {}), 0);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 128; i++)
        ASSERT_EQ(dom_element_apply_rule(child, invalid, {}), 0);
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_EQ(repeated.live_bytes, warm.live_bytes);
}

TEST_F(StyleEpochTest, OwnedDeclarationRetainsKeywordSpelling) {
    CssValue* value = css_value_create_keyword(doc.document_pool, "auto");
    ASSERT_NE(value, nullptr);
    char* spelling = pool_strdup(doc.document_pool, "auto");
    ASSERT_NE(spelling, nullptr);
    value->has_keyword_spelling = true;
    value->data.keyword_token.spelling = spelling;
    CssDeclaration* source = css_declaration_create(CSS_PROPERTY_WIDTH, value,
        {}, CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(source, nullptr);
    CssDeclaration* copy = css_declaration_clone_owned(source, {},
        CSS_ORIGIN_AUTHOR, doc.document_pool);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->value->data.keyword_token.spelling, spelling);
    // Retiring or reusing parser bytes cannot change a published owned value.
    spelling[0] = 'x';
    EXPECT_STREQ(copy->value->data.keyword_token.spelling, "auto");
    css_declaration_destroy_owned(copy, doc.document_pool);
}

TEST_F(StyleEpochTest, PresentationLayerPreservesAuthoredStateAndCascade) {
    DomElement* child = append("child");
    const char* authored = "opacity:0.3;color:red !important";
    ASSERT_TRUE(child->set_attribute("style", authored));
    bool changed = false;
    ASSERT_TRUE(dom_element_set_presentation_style(child, "opacity", "0.8", &changed));
    EXPECT_TRUE(changed);
    ASSERT_TRUE(dom_element_set_presentation_style(child, "color", "green", &changed));
    EXPECT_STREQ(child->get_attribute("style"), authored);
    EXPECT_DOUBLE_EQ(style_tree_get_authored_declaration(child->specified_style,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.3);
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.8);
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_COLOR)->important);

    dom_element_clear_cascaded_styles(child);
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 20.0, CSS_UNIT_PX));
    apply(document_root, width, child);
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_OPACITY)->presentation_value);
    ASSERT_TRUE(child->set_attribute("style", "opacity:0.4;color:blue"));
    EXPECT_TRUE(dom_element_get_specified_value(child, CSS_PROPERTY_COLOR)->presentation_value);
    EXPECT_DOUBLE_EQ(style_tree_get_authored_declaration(child->specified_style,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.4);

    EXPECT_FALSE(dom_element_set_presentation_style(child, "opacity", "invalid", &changed));
    EXPECT_FALSE(dom_element_set_presentation_style(child, "opacity", "0.2 !important", &changed));
    EXPECT_FALSE(dom_element_set_presentation_style(child, "not-a-css-property", "1", &changed));
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.8);
    EXPECT_TRUE(dom_element_clear_presentation_style(child));
    EXPECT_FALSE(dom_element_clear_presentation_style(child));
    EXPECT_DOUBLE_EQ(dom_element_get_specified_value(child,
        CSS_PROPERTY_OPACITY)->value->data.number.value, 0.4);
    EXPECT_STREQ(child->get_attribute("style"), "opacity:0.4;color:blue");
}

TEST_F(StyleEpochTest, MixedAuthoredTreeRetainsInlineInsetsDuringRecascade) {
    DomElement* child = append("child");
    ASSERT_TRUE(child->set_attribute("style", "left:200px;top:40px;width:80px"));
    CssRule* position = rule(CSS_PROPERTY_POSITION,
        css_value_create_keyword(doc.document_pool, "absolute"));
    CssRule* opacity = rule(CSS_PROPERTY_OPACITY,
        css_value_create_number(doc.document_pool, 0.8));
    apply(document_root, position, child);
    apply(document_root, opacity, child);
    EXPECT_TRUE(style_tree_has_inline_declarations(child->specified_style));
    EXPECT_TRUE(style_tree_has_local_declarations(child->specified_style));
    dom_element_clear_cascaded_styles(child);
    apply(document_root, position, child);
    apply(document_root, opacity, child);
    EXPECT_FALSE(child->specified_style_shared());
    CssDeclaration* left = dom_element_get_specified_value(child, CSS_PROPERTY_LEFT);
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value->data.length.value, 200.0);
    EXPECT_TRUE(left->specificity.inline_style);
}

TEST_F(StyleEpochTest, PresentationSamplesDoNotGrowRetainedPoolsOrArenas) {
    DomElement* child = append("child");
    const char* transforms[] = {
        "translate(1px,2px) rotate(10deg) scale(1,1)",
        "translate(3px,4px) rotate(20deg) scale(1.2,0.8)", "none"
    };
    bool changed = false;
    for (const char* transform : transforms)
        ASSERT_TRUE(dom_element_set_presentation_style(child, "transform", transform, &changed));
    ASSERT_TRUE(dom_element_set_presentation_style(child, "clip-path", "inset(0 10% 0 0)", &changed));
    ASSERT_TRUE(dom_element_set_presentation_style(child, "fill", "#336699", &changed));
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    size_t input_used = arena_total_used(input.arena);
    size_t nodes_used = arena_total_used(doc.node_arena);
    for (size_t i = 0; i < 256; i++) {
        ASSERT_TRUE(dom_element_set_presentation_style(child, "transform", transforms[i % 3], &changed));
        ASSERT_TRUE(dom_element_set_presentation_style(child, "opacity", i % 2 ? "0.5" : "1", &changed));
    }
    PoolStats repeated = {};
    pool_get_detailed_stats(doc.document_pool, &repeated);
    EXPECT_LE(repeated.live_bytes, warm.live_bytes + 4096u);
    EXPECT_EQ(arena_total_used(input.arena), input_used);
    EXPECT_EQ(arena_total_used(doc.node_arena), nodes_used);
    EXPECT_EQ(child->get_attribute("style"), nullptr);
    EXPECT_TRUE(dom_element_clear_presentation_style(child));
    EXPECT_TRUE(style_tree_is_empty(child->specified_style));
}

TEST_F(StyleEpochTest, ColdCanonicalEntriesRespectCacheBudgetAfterCascade) {
    DomElement* child = append("child");
    CssRule* variants[8] = {};
    for (size_t i = 0; i < 8; i++) {
        variants[i] = rule(CSS_PROPERTY_WIDTH,
            css_value_create_length(doc.document_pool, 20.0 + (double)i,
                                    CSS_UNIT_PX), (uint32_t)(i + 1u));
    }

    // A zero budget makes every unbound canonical snapshot eligible at the
    // outer cascade boundary while the child remains a live consumer.
    style_epoch_debug_set_cold_cache_cap(&doc, 0);
    for (size_t i = 0; i < 8; i++) {
        dom_element_clear_cascaded_styles(child);
        apply(document_root, variants[i], child);
    }

    StyleEpochStats stats = {};
    style_epoch_get_stats(&doc, &stats);
    EXPECT_EQ(stats.current_unbound_entry_count, 0u);
    EXPECT_EQ(stats.current_unbound_bytes, 0u);
    EXPECT_GT(stats.cache_eviction_count, 0u);
    EXPECT_TRUE(child->specified_style_shared());
    EXPECT_NE(dom_element_get_specified_value(child, CSS_PROPERTY_WIDTH), nullptr);
}

TEST_F(StyleEpochTest, RecascadeRetiresPseudoTreesAfterBorrowersRebind) {
    DomElement* host = append("host");
    CssRule* width = rule(CSS_PROPERTY_WIDTH,
        css_value_create_length(doc.document_pool, 12.0, CSS_UNIT_PX));
    ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    StyleTree* first = host->pseudo_style(PSEUDO_STYLE_BEFORE);
    ASSERT_NE(first, nullptr);

    DomElement* generated = DomElement::create(&doc, "::before", nullptr);
    ASSERT_NE(generated, nullptr);
    dom_element_borrow_specified_style(generated, first);
    EXPECT_EQ(first->borrow_ref_count, 1u);

    ASSERT_TRUE(dom_element_clear_pseudo_styles(host));
    StyleTree* second = host->pseudo_style(PSEUDO_STYLE_BEFORE);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second, first);
    EXPECT_EQ(generated->specified_style, first);
    EXPECT_TRUE(generated->specified_style_borrowed());

    dom_element_borrow_specified_style(generated, second);
    EXPECT_EQ(generated->specified_style, second);
    EXPECT_EQ(second->borrow_ref_count, 1u);
    dom_element_destroy(generated);
    EXPECT_EQ(second->borrow_ref_count, 0u);

    ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    PoolStats warm = {};
    pool_get_detailed_stats(doc.document_pool, &warm);
    for (size_t i = 0; i < 32; i++) {
        ASSERT_TRUE(dom_element_clear_pseudo_styles(host));
        ASSERT_EQ(dom_element_apply_pseudo_element_rule(host, width, {}, 1), 1);
    }
    PoolStats stable = {};
    pool_get_detailed_stats(doc.document_pool, &stable);
    EXPECT_LE(stable.live_bytes, warm.live_bytes + 4096u);
}

// Every view-tree allocator is registered under its document's context, so
// the view tree is a subtree of the document in the ownership tree.
TEST(ViewTreeOwnershipTest, AllocatorsRegisterUnderTheOwnerContext) {
    MemContext* doc_ctx = mem_context_create(mem_context_root(), MEM_ROLE_NODE, "test.document");
    ASSERT_NE(doc_ctx, nullptr);
    ViewTree tree = {};
    tree.init(doc_ctx);
    ASSERT_NE(tree.prop_pool, nullptr);
    EXPECT_EQ(mem_node_owner((MemNode*)pool_get_mem_node(tree.prop_pool)), doc_ctx);
    Arena* arenas[] = {tree.canonical_prop_arena, tree.scratch_arena, tree.layout_pass_arena,
                       tree.render_scratch_arena, tree.display_list_arena};
    for (Arena* arena : arenas) {
        ASSERT_NE(arena, nullptr);
        EXPECT_EQ(mem_node_owner((MemNode*)arena_get_mem_node(arena)), doc_ctx);
    }
    tree.destroy();
    EXPECT_EQ(mem_context_live_count(doc_ctx), 0u);
    mem_context_destroy(doc_ctx);
}
