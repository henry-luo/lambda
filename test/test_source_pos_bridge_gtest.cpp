// test_source_pos_bridge_gtest.cpp — C-side helpers for the editor
// source-position bridge (Phase R7, Radiant integration step 1).
// The pure-Lambda half is exercised via the auto-discovered test
// `editor_dom_bridge_basic.ls`; this file covers the small C-side
// scaffolding (path lifecycle, equality, stub return values).

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda-data.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/render_map.h"
#include "../lambda/runtime/template_registry.h"
#include "../lambda/runtime/lambda-error.h"
#include "lib/mempool.h"
#include "lib/arena.h"
#include "lib/log.h"
#include "lib/hashmap.h"
#include "lib/test_utils.h"
#include "lambda/input/css/dom_node.hpp"
#include "lambda/input/css/dom_element.hpp"
#include "lambda/io/mark_builder.hpp"
#include "lambda/input/input.hpp"
#include "radiant/event.hpp"
#include "../lambda/module/radiant/source_position_adapter.hpp"

class SourcePositionHostEnvironment : public ::testing::Environment {
    void SetUp() override { ASSERT_TRUE(lambda_source_position_host_register()); }
};
static ::testing::Environment* const source_position_host_environment =
    ::testing::AddGlobalTestEnvironment(new SourcePositionHostEnvironment);

// ---------------------------------------------------------------------------
// Bridge stubs (see test_dom_range_gtest.cpp). dom_range.cpp links against
// these DocState accessors but the bridge tests don't exercise the
// selection-resync paths, so trivial stubs satisfy the linker.
// ---------------------------------------------------------------------------
struct Arena;
struct DocState;
struct DomRange;
struct DomSelection;

extern "C" Arena* dom_range_state_arena(DocState*) { return nullptr; }
extern "C" DomRange** dom_range_state_live_ranges_slot(DocState*) {
    static DomRange* slot = nullptr;
    return &slot;
}
extern "C" DomRange** dom_range_state_range_freelist_slot(DocState*) {
    static DomRange* slot = nullptr;
    return &slot;
}
extern "C" struct DomSelection* dom_range_state_selection(DocState*) {
    return nullptr;
}

// render_map.cpp pulls in context-owned template state and lambda-mem.cpp's
// GC root registration. Stub them out for this non-collecting unit target.
struct TemplateRegistry;
static TemplateRegistry* test_template_registry = nullptr;
extern "C" TemplateRegistry** template_registry_current_slot(void) {
    return &test_template_registry;
}
extern __thread EvalContext* context;
static Heap source_pos_test_heap = {};
static EvalContext source_pos_test_context = {};

struct SourcePosTestContextBinding {
    SourcePosTestContextBinding() {
        source_pos_test_context.heap = &source_pos_test_heap;
        context = &source_pos_test_context;
    }

    ~SourcePosTestContextBinding() {
        context = nullptr;
    }
};

static SourcePosTestContextBinding source_pos_test_context_binding;

extern "C" void heap_register_gc_root(uint64_t*) {}
extern "C" void heap_register_gc_root_range(uint64_t*, int) {}
extern "C" bool heap_try_register_gc_root_range(uint64_t*, int) { return true; }
extern "C" void heap_unregister_gc_root_range(uint64_t*) {}
extern "C" bool heap_register_gc_root_range_for(Context*, uint64_t*, int) { return true; }
extern "C" void heap_unregister_gc_root_range_for(Context*, uint64_t*) {}
// security_context's path cache uses the same non-collecting fixture root hooks.
extern "C" bool heap_register_gc_root_for(Context* owner, uint64_t* slot) {
    return heap_register_gc_root_range_for(owner, slot, 1);
}
extern "C" void heap_unregister_gc_root_for(Context* owner, uint64_t* slot) {
    heap_unregister_gc_root_range_for(owner, slot);
}
extern "C" void set_runtime_error(LambdaErrorCode code, const char* format, ...) {
    ADD_FAILURE() << "unexpected standalone runtime error " << (int)code << ": " << format;
}

TEST(SourcePosBridgeC, StubsReturnFalse) {
    // Null/empty inputs should fail safely.
    SourcePosC pos;
    source_path_init(&pos.path);
    pos.offset = 0;
    pos.kind = SOURCE_POS_TEXT;
    EXPECT_FALSE(source_pos_from_dom_boundary(nullptr, &pos));
    EXPECT_FALSE(source_pos_from_dom_range(nullptr, &pos, &pos));
    EXPECT_FALSE(dom_boundary_from_source_pos(nullptr, &pos, nullptr));
    SourcePathC out_path;
    EXPECT_FALSE(render_map_reverse_lookup_with_path(Item{0}, nullptr, &out_path));
    source_pos_free(&pos);
}

// ---------------------------------------------------------------------------
// Path side-table: record + reverse-lookup round-trip.
// ---------------------------------------------------------------------------

static Item synthetic_bridge_item(int64_t value) {
    // Side-table tests use identity-only values, but Item pointer lanes must
    // still be valid tagged scalars before render-map code inspects their type.
    Item result;
    result.item = i2it(value);
    return result;
}

TEST(SourcePosBridgePathTable, RecordAndLookupRoundTrip) {
    source_pos_bridge_reset();
    render_map_reset();

    // Two synthetic source items + two synthetic result items.
    Item src_a = synthetic_bridge_item(0xA000);
    Item src_b = synthetic_bridge_item(0xB000);
    Item res_a = synthetic_bridge_item(0xA111);
    Item res_b = synthetic_bridge_item(0xB222);
    const char* tref = "test_template";

    // Record forward (source → result) and side-table (source → path).
    render_map_record(src_a, tref, res_a, Item{0}, 0);
    render_map_record(src_b, tref, res_b, Item{0}, 1);
    int path_a[] = { 0, 2 };
    int path_b[] = { 1 };
    render_map_record_path(src_a, tref, path_a, 2);
    render_map_record_path(src_b, tref, path_b, 1);

    RenderMapLookup lk;
    SourcePathC out;
    ASSERT_TRUE(render_map_reverse_lookup_with_path(res_a, &lk, &out));
    EXPECT_EQ(lk.source_item.item, src_a.item);
    EXPECT_STREQ(lk.template_ref, tref);
    EXPECT_EQ(out.depth, 2);
    EXPECT_EQ(out.indices[0], 0);
    EXPECT_EQ(out.indices[1], 2);
    source_path_free(&out);

    ASSERT_TRUE(render_map_reverse_lookup_with_path(res_b, &lk, &out));
    EXPECT_EQ(lk.source_item.item, src_b.item);
    EXPECT_EQ(out.depth, 1);
    EXPECT_EQ(out.indices[0], 1);
    source_path_free(&out);

    source_pos_bridge_reset();
    render_map_reset();
}

TEST(SourcePosBridgePathTable, OverwriteFreesPriorPath) {
    source_pos_bridge_reset();
    render_map_reset();

    Item src = synthetic_bridge_item(0xCAFE);
    Item res = synthetic_bridge_item(0xBEEF);
    const char* tref = "tmpl_x";
    render_map_record(src, tref, res, Item{0}, 0);

    int p1[] = { 0, 0, 0, 0, 0 };
    int p2[] = { 7, 8 };
    render_map_record_path(src, tref, p1, 5);
    render_map_record_path(src, tref, p2, 2);  // should free p1's heap copy

    RenderMapLookup lk;
    SourcePathC out;
    ASSERT_TRUE(render_map_reverse_lookup_with_path(res, &lk, &out));
    EXPECT_EQ(out.depth, 2);
    EXPECT_EQ(out.indices[0], 7);
    EXPECT_EQ(out.indices[1], 8);
    source_path_free(&out);

    source_pos_bridge_reset();
    render_map_reset();
}

TEST(SourcePosBridgePathTable, MissingPathStillReturnsLookup) {
    // reverse_lookup hits but no path was recorded: function still returns
    // true (the (item, template_ref) is itself useful for handler dispatch),
    // out_path stays empty.
    source_pos_bridge_reset();
    render_map_reset();
    Item src = synthetic_bridge_item(0x1234);
    Item res = synthetic_bridge_item(0x5678);
    const char* tref = "tmpl_y";
    render_map_record(src, tref, res, Item{0}, 0);
    // (deliberately no record_path)
    RenderMapLookup lk;
    SourcePathC out;
    ASSERT_TRUE(render_map_reverse_lookup_with_path(res, &lk, &out));
    EXPECT_EQ(lk.source_item.item, src.item);
    EXPECT_EQ(out.depth, 0);
    EXPECT_EQ(out.indices, nullptr);
    source_pos_bridge_reset();
    render_map_reset();
}

TEST(SourcePosBridgePathTable, ReusedResultRefreshesDirectOwner) {
    source_pos_bridge_reset();
    render_map_reset();

    Item old_source = synthetic_bridge_item(0x4100);
    Item new_source = synthetic_bridge_item(0x4200);
    Item reused_result = synthetic_bridge_item(0x4300);
    const char* tref = "reused_template";
    render_map_record(old_source, tref, reused_result, Item{0}, 0);
    render_map_record(new_source, tref, reused_result, Item{0}, 0);

    RenderMapLookup lookup;
    SourcePathC path;
    ASSERT_TRUE(render_map_reverse_lookup_with_path(reused_result, &lookup, &path));
    EXPECT_EQ(lookup.source_item.item, new_source.item);
    EXPECT_STREQ(lookup.template_ref, tref);
    source_path_free(&path);

    source_pos_bridge_reset();
    render_map_reset();
}

TEST(SourcePosBridgePathTable, MissOnUnknownResultItem) {
    source_pos_bridge_reset();
    render_map_reset();
    Item ghost = synthetic_bridge_item(0xDEADBEEF);
    RenderMapLookup lk;
    SourcePathC out;
    EXPECT_FALSE(render_map_reverse_lookup_with_path(ghost, &lk, &out));
    EXPECT_EQ(out.depth, 0);
    EXPECT_EQ(out.indices, nullptr);
    source_pos_bridge_reset();
    render_map_reset();
}

TEST(RenderMapOwnership, IndependentInvocationsReusingAResultDoNotWrap) {
    render_map_destroy();
    Item result = synthetic_bridge_item(0x4300);
    Item first_source = synthetic_bridge_item(0x4100);
    Item next_source = synthetic_bridge_item(0x4200);
    uint64_t first = render_map_invocation_begin();
    render_map_record_invocation(first_source, "first", result, ItemNull, -1, first);
    uint64_t next = render_map_invocation_begin();
    render_map_record_invocation(next_source, "next", result, ItemNull, -1, next);

    RenderMapLookup lookup = {};
    ASSERT_TRUE(render_map_reverse_lookup(result, &lookup));
    EXPECT_EQ(lookup.source_item.item, next_source.item);
    EXPECT_STREQ(lookup.template_ref, "next");
    EXPECT_FALSE(render_map_wrapper_lookup(lookup, &lookup));
    render_map_destroy();
}

TEST(RenderMapOwnership, NestedInvocationsKeepInnerAndOutwardHandlerOrder) {
    // distinct models may recursively select the same template reference.
    const char* refs[][3] = {{"outer", "middle", "inner"}, {"recursive", "recursive", "recursive"}};
    for (const auto& names : refs) {
        render_map_destroy();
        Item result = synthetic_bridge_item(0x4300);
        Item sources[] = {synthetic_bridge_item(1), synthetic_bridge_item(2), synthetic_bridge_item(3)};
        uint64_t starts[3];
        for (int i = 0; i < 3; i++) starts[i] = render_map_invocation_begin();
        for (int i = 2; i >= 0; i--) {
            render_map_record_invocation(sources[i], names[i], result, ItemNull, -1, starts[i]);
        }

        RenderMapLookup lookup = {};
        ASSERT_TRUE(render_map_reverse_lookup(result, &lookup));
        for (int i = 2; i >= 0; i--) {
            EXPECT_EQ(lookup.source_item.item, sources[i].item);
            EXPECT_STREQ(lookup.template_ref, names[i]);
            EXPECT_EQ(render_map_wrapper_lookup(lookup, &lookup), i > 0);
        }
    }
    render_map_destroy();
}

static const RenderMapEntry* find_render_entry(Item source, const char* template_ref) {
    size_t iterator = 0;
    void* value;
    while (hashmap_iter(render_map_get_map(), &iterator, &value)) {
        const RenderMapEntry* entry = (const RenderMapEntry*)value;
        if (entry->key.source_item.item == source.item &&
            strcmp(entry->key.template_ref, template_ref) == 0) return entry;
    }
    return nullptr;
}

TEST(RenderMapOwnership, FragmentPlacementKeepsIndependentSharedResultOwners) {
    render_map_destroy();
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item fragment = builder.array().append(builder.element("span").text("shared").final()).final();
    Item parents[] = {builder.element("main").final(), builder.element("aside").final()};
    Item sources[] = {synthetic_bridge_item(1), synthetic_bridge_item(2)};
    const char* refs[] = {"first", "second"};
    for (int i = 0; i < 2; i++) {
        uint64_t start = render_map_invocation_begin();
        render_map_record_invocation(sources[i], refs[i], fragment, ItemNull, -1, start);
        render_map_bind_fragment_parent(fragment, parents[i], i + 2, 1);
    }
    for (int i = 0; i < 2; i++) {
        const RenderMapEntry* entry = find_render_entry(sources[i], refs[i]);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->parent_result.item, parents[i].item);
        EXPECT_EQ(entry->child_index, i + 2);
        EXPECT_EQ(entry->child_count, 1);
    }
    render_map_destroy();
}

TEST(RenderMapOwnership, FragmentPlacementBindsNestedWrappersAndRejectsStaleOwners) {
    render_map_destroy();
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item fragment = builder.array().append(builder.element("span").text("child").final()).final();
    Item parent = builder.element("main").final();
    Item sources[] = {synthetic_bridge_item(1), synthetic_bridge_item(2)};
    uint64_t outer_start = render_map_invocation_begin();
    uint64_t inner_start = render_map_invocation_begin();
    render_map_record_invocation(sources[0], "inner", fragment, ItemNull, -1, inner_start);
    render_map_record_invocation(sources[1], "outer", fragment, ItemNull, -1, outer_start);
    render_map_bind_fragment_parent(fragment, parent, 3, 1);
    const char* refs[] = {"inner", "outer"};
    for (int i = 0; i < 2; i++) {
        const RenderMapEntry* entry = find_render_entry(sources[i], refs[i]);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->parent_result.item, parent.item);
        EXPECT_EQ(entry->child_index, 3);
        EXPECT_EQ(entry->child_count, 1);
    }
    Item replacement = builder.element("p").text("replacement").final();
    render_map_record(sources[0], "inner", replacement, parent, 0);
    render_map_bind_fragment_parent(fragment, parent, 9, 4);
    EXPECT_EQ(find_render_entry(sources[0], "inner")->child_index, 0);
    EXPECT_EQ(find_render_entry(sources[1], "outer")->child_index, 3);
    Item plain = builder.array().append(replacement).final();
    render_map_bind_fragment_parent(plain, parent, 7, 1);
    EXPECT_EQ(find_render_entry(sources[0], "inner")->child_index, 0);
    render_map_destroy();
}

TEST(RenderMapOwnership, CleanQueriesFollowMarkingReplacementRetirementAndReset) {
    render_map_destroy();
    EXPECT_FALSE(render_map_has_dirty());
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item source = synthetic_bridge_item(1);
    Item first = builder.element("p").text("first").final();
    Item replacement = builder.element("p").text("replacement").final();
    render_map_record(source, "body", first, ItemNull, -1);
    EXPECT_FALSE(render_map_has_dirty());
    render_map_mark_dirty(source, "unknown");
    EXPECT_FALSE(render_map_has_dirty());
    render_map_mark_dirty(source, "body");
    EXPECT_TRUE(render_map_has_dirty());
    EXPECT_TRUE(render_map_has_dirty());
    render_map_record(source, "body", replacement, ItemNull, -1);
    EXPECT_FALSE(render_map_has_dirty());
    render_map_mark_dirty(source, "body");
    EXPECT_TRUE(render_map_has_dirty());
    render_map_forget_retired_result(context, replacement);
    EXPECT_FALSE(render_map_has_dirty());
    render_map_record(source, "body", first, ItemNull, -1);
    render_map_mark_dirty(source, "body");
    render_map_reset();
    EXPECT_FALSE(render_map_has_dirty());
    render_map_destroy();
}

TEST(SourcePosBridgePathIndex, RecordsOnlyItemsOwnedBySourceRoot) {
    source_pos_bridge_reset();
    render_map_reset();

    Pool* pool = tu_setup_pool();
    ASSERT_NE(pool, nullptr);
    List* root = (List*)pool_calloc(pool, sizeof(List));
    ASSERT_NE(root, nullptr);
    root->type_id = LMD_TYPE_ARRAY;
    root->items = (Item*)pool_calloc(pool, sizeof(Item) * 2);
    ASSERT_NE(root->items, nullptr);
    root->length = 2;
    root->capacity = 2;
    Item child_a = synthetic_bridge_item(0xD101);
    Item child_b = synthetic_bridge_item(0xD102);
    root->items[0] = child_a;
    root->items[1] = child_b;
    Item root_item = {.array = (Array*)root};
    const char* tref = "path_index_template";

    render_map_set_path_recorder(&render_map_record_path);
    render_map_set_source_doc_root(root_item);

    Item child_result = synthetic_bridge_item(0xD201);
    render_map_record(child_b, tref, child_result, Item{0}, 0);
    render_map_record_source_path(child_b, tref);

    RenderMapLookup lookup;
    SourcePathC path;
    ASSERT_TRUE(render_map_reverse_lookup_with_path(child_result, &lookup, &path));
    ASSERT_EQ(path.depth, 1);
    EXPECT_EQ(path.indices[0], 1);
    source_path_free(&path);

    // Directory listings and other generated models are outside the editable
    // source tree, so they must not trigger a root-wide source-path scan.
    Item external = synthetic_bridge_item(0xD103);
    Item external_result = synthetic_bridge_item(0xD202);
    render_map_record(external, tref, external_result, Item{0}, 0);
    render_map_record_source_path(external, tref);
    ASSERT_TRUE(render_map_reverse_lookup_with_path(external_result, &lookup, &path));
    EXPECT_EQ(path.depth, 0);
    EXPECT_EQ(path.indices, nullptr);
    source_path_free(&path);

    source_pos_bridge_reset();
    render_map_reset();
    tu_teardown_pool(pool);
}

// ---------------------------------------------------------------------------
// End-to-end round-trip: tiny synthetic DOM + render_map + path table.
//
// Source tree (modeled, not built):  doc [ para [ "hello" ], hr ]
// DOM tree:                          div [ p   [ "hello" ], b  ]
//
// The DomElements' embedded Lambda Elements are recorded in render_map
// alongside their source paths via
// render_map_record_path. `source_pos_from_dom_boundary` and
// `dom_boundary_from_source_pos` should be exact inverses.
// ---------------------------------------------------------------------------

class SourcePosBridgeRoundTrip : public ::testing::Test {
protected:
    Pool* pool = nullptr;
    Arena* arena = nullptr;
    DomDocument doc_storage{};

    DomElement* root  = nullptr;  // models doc
    DomElement* para  = nullptr;  // models doc.content[0]
    DomElement* hr    = nullptr;  // models doc.content[1]
    DomText*    hello = nullptr;  // text leaf inside para

    // Backing Items are used only for render-map identity in this fixture.
    Item root_item{};
    Item para_item{};
    Item hr_item{};
    const char* tref = "rt_template";

    void SetUp() override {
        pool = tu_setup_pool();
        arena = arena_create_default();
        // fixtures must initialize canonical document ownership after the
        // legacy pool/arena aliases were removed.
        doc_storage.document_pool = lam::own(pool);
        doc_storage.node_arena = lam::own(arena);

        root  = make_element();
        para  = make_element();
        hr    = make_element();
        hello = make_text("hello", 5);
        ASSERT_TRUE(root->append_child(para));
        ASSERT_TRUE(root->append_child(hr));
        ASSERT_TRUE(para->append_child(hello));

        // These test nodes participate in the modeled Lambda tree, so their
        // embedded Elements—not synthetic stand-ins—define stable identity.
        root->set_synthetic(false);
        para->set_synthetic(false);
        hr->set_synthetic(false);
        root_item.element = dom_element_to_element(root);
        para_item.element = dom_element_to_element(para);
        hr_item.element = dom_element_to_element(hr);

        // Source items used as render_map keys; values don't matter beyond
        // uniqueness. Use the same bits as the result items.
        source_pos_bridge_reset();
        render_map_reset();
        render_map_set_source_doc_root(root_item);
        render_map_record(root_item, tref, root_item, Item{0}, 0);
        render_map_record(para_item, tref, para_item, root_item, 0);
        render_map_record(hr_item,   tref, hr_item,   root_item, 1);

        // Source paths: doc=[], doc.content[0]=[0], doc.content[1]=[1].
        render_map_record_path(root_item, tref, nullptr, 0);
        int p0[] = { 0 };
        int p1[] = { 1 };
        render_map_record_path(para_item, tref, p0, 1);
        render_map_record_path(hr_item,   tref, p1, 1);
    }

    void TearDown() override {
        source_pos_bridge_reset();
        render_map_reset();
        delete hello;
        delete hr;
        delete para;
        delete root;
        if (arena) arena_destroy(arena);
        tu_teardown_pool(pool);
    }

    DomElement* make_element() {
        DomElement* e = new DomElement{};
        e->node_type = DOM_NODE_ELEMENT;
        e->set_synthetic(true);
        e->doc = lam::up(&doc_storage);
        return e;
    }
    DomText* make_text(const char* s, size_t len) {
        DomText* t = new DomText{};
        t->node_type = DOM_NODE_TEXT;
        t->text = lam::up(s);
        t->length = len;
        return t;
    }

    void record_para_source(Item source) {
        render_map_record(source, tref, para_item, root_item, 0);
        int path[] = {0};
        render_map_record_path(source, tref, path, 1);
    }
};

TEST_F(SourcePosBridgeRoundTrip, DomTextBoundaryToSourcePos) {
    // Caret at offset 3 inside "hello" (UTF-16 == UTF-8 here).
    DomBoundary b{ (DomNode*)hello, 3 };
    SourcePosC pos;
    ASSERT_TRUE(source_pos_from_dom_boundary(&b, &pos));
    EXPECT_EQ(pos.kind, SOURCE_POS_TEXT);
    EXPECT_EQ(pos.offset, 3u);
    // para's recorded path is [0]; the text leaf is its 0th child → [0, 0].
    ASSERT_EQ(pos.path.depth, 2);
    EXPECT_EQ(pos.path.indices[0], 0);
    EXPECT_EQ(pos.path.indices[1], 0);
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, DomElementBoundaryToSourcePos) {
    // Boundary at root, offset 1 → "between para and hr".
    DomBoundary b{ (DomNode*)root, 1 };
    SourcePosC pos;
    ASSERT_TRUE(source_pos_from_dom_boundary(&b, &pos));
    EXPECT_EQ(pos.kind, SOURCE_POS_ELEMENT);
    EXPECT_EQ(pos.offset, 1u);
    EXPECT_EQ(pos.path.depth, 0);  // root's recorded path is empty
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, UnrecordedOwnerDoesNotProjectAsDocumentRoot) {
    // handler dispatch still recognizes the owner, but it has no editor path.
    source_pos_bridge_reset();
    DomBoundary boundary{root, 0};
    SourcePosC position{};
    EXPECT_FALSE(source_pos_from_dom_boundary(&boundary, &position));
    source_pos_free(&position);
    DomBoundary resolved{};
    EXPECT_FALSE(dom_boundary_from_source_pos(root, &position, &resolved));
}

TEST_F(SourcePosBridgeRoundTrip, ApplicationRootPathDoesNotProjectAsEditorDocument) {
    // a model redraw replaces the source root while the outer app keeps its old [] path.
    render_map_set_source_doc_root(para_item);
    DomBoundary boundary{root, 0};
    SourcePosC position{};
    EXPECT_FALSE(source_pos_from_dom_boundary(&boundary, &position));
    source_pos_free(&position);
}

TEST_F(SourcePosBridgeRoundTrip, RecordedTemplatePathSurvivesSourceRootReset) {
    // ordinary template callbacks retain path records after the editor-root index resets.
    render_map_reset();
    render_map_record(root_item, tref, root_item, ItemNull, -1);
    DomBoundary boundary{root, 0};
    SourcePosC position{};
    ASSERT_TRUE(source_pos_from_dom_boundary(&boundary, &position));
    EXPECT_EQ(position.path.depth, 0);
    EXPECT_EQ(position.offset, 0u);
    DomBoundary resolved{};
    ASSERT_TRUE(dom_boundary_from_source_pos(root, &position, &resolved));
    EXPECT_EQ(resolved.node, root);
    source_pos_free(&position);
}

TEST_F(SourcePosBridgeRoundTrip, SourcePosToDomTextBoundary) {
    // Build a SourcePos pointing at offset 4 inside "hello".
    SourcePosC pos;
    source_path_init(&pos.path);
    pos.path.indices = (int*)malloc(sizeof(int) * 2);
    pos.path.indices[0] = 0;
    pos.path.indices[1] = 0;
    pos.path.depth = 2;
    pos.offset = 4;
    pos.kind = SOURCE_POS_TEXT;
    DomBoundary out{ nullptr, 0 };
    ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &pos, &out));
    EXPECT_EQ(out.node, (DomNode*)hello);
    EXPECT_EQ(out.offset, 4u);
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, TextOffsetsCountCodePoints) {
    // text-leaf offsets use the unit Lambda strings index by (S2.5.8): after the
    // CJK run and the emoji, UTF-16 units, code points and UTF-8 bytes all differ.
    static const char text[] = "ro腿部空间😀m";
    hello->text = lam::up(text);
    hello->length = sizeof(text) - 1;
    struct { uint32_t u16; uint32_t cp; } cases[] = {{0, 0}, {2, 2}, {4, 4}, {6, 6}, {8, 7}, {9, 8}};
    for (const auto& c : cases) {
        DomBoundary b{ (DomNode*)hello, c.u16 };
        SourcePosC pos;
        ASSERT_TRUE(source_pos_from_dom_boundary(&b, &pos));
        EXPECT_EQ(pos.offset, c.cp) << "utf16 offset " << c.u16;
        DomBoundary out{ nullptr, 0 };
        ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &pos, &out));
        EXPECT_EQ(out.node, (DomNode*)hello);
        EXPECT_EQ(out.offset, c.u16) << "code point " << c.cp;
        source_pos_free(&pos);
    }
    // a model offset past the leaf clamps to its end.
    SourcePosC past;
    source_path_init(&past.path);
    past.path.indices = (int*)malloc(sizeof(int) * 2);
    past.path.indices[0] = 0;
    past.path.indices[1] = 0;
    past.path.depth = 2;
    past.offset = 40;
    past.kind = SOURCE_POS_TEXT;
    DomBoundary end{ nullptr, 0 };
    ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &past, &end));
    EXPECT_EQ(end.offset, 9u);
    source_pos_free(&past);
}

TEST_F(SourcePosBridgeRoundTrip, SourcePosToDomTextUsesRecordedAncestor) {
    // A retransform can retain only the doc-level render-map entry while an
    // intermediate apply() result is rebuilt. The source bridge must still
    // follow the remaining child path to restore the editor caret.
    source_pos_bridge_reset();
    render_map_reset();
    // Reset also clears the source root used to validate an empty source path.
    render_map_set_source_doc_root(root_item);
    render_map_record(root_item, tref, root_item, Item{0}, 0);
    render_map_record_path(root_item, tref, nullptr, 0);

    SourcePosC pos;
    source_path_init(&pos.path);
    pos.path.indices = (int*)malloc(sizeof(int) * 2);
    pos.path.indices[0] = 0;
    pos.path.indices[1] = 0;
    pos.path.depth = 2;
    pos.offset = 4;
    pos.kind = SOURCE_POS_TEXT;
    DomBoundary out{ nullptr, 0 };
    ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &pos, &out));
    EXPECT_EQ(out.node, (DomNode*)hello);
    EXPECT_EQ(out.offset, 4u);
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, SourcePosToDomPrefersMappedDescendantOverAncestorShape) {
    // Generated wrappers (such as a table's implicit tbody) change DOM child
    // indices. An ancestor's positional guess must not beat a mapped owner.
    DomElement* wrapper = make_element();
    DomText* wrong = make_text("wrong", 5);
    ASSERT_TRUE(root->remove_child(para));
    ASSERT_TRUE(root->insert_before(wrapper, hr));
    ASSERT_TRUE(wrapper->append_child(wrong));
    ASSERT_TRUE(wrapper->append_child(para));

    DomBoundary original{hello, 3};
    SourcePosC pos;
    ASSERT_TRUE(source_pos_from_dom_boundary(&original, &pos));
    DomBoundary restored{nullptr, 0};
    ASSERT_TRUE(dom_boundary_from_source_pos(root, &pos, &restored));
    EXPECT_EQ(restored.node, original.node);
    EXPECT_EQ(restored.offset, original.offset);
    source_pos_free(&pos);
    delete wrong;
    delete wrapper;
}

TEST_F(SourcePosBridgeRoundTrip, SourcePosToDomElementBoundary) {
    SourcePosC pos;
    source_path_init(&pos.path);
    pos.offset = 1;
    pos.kind = SOURCE_POS_ELEMENT;
    DomBoundary out{ nullptr, 0 };
    ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &pos, &out));
    EXPECT_EQ(out.node, (DomNode*)root);
    EXPECT_EQ(out.offset, 1u);
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, ModelContainerPositionUsesChildOffsetWithoutKindField) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    MapBuilder model = builder.map();
    model.put("kind", builder.createSymbolItem("node"));
    Item source = model.final();
    record_para_source(source);

    // Lambda SourcePos omits kind even at a container boundary beside an image.
    SourcePosC position;
    source_path_init(&position.path);
    int path[] = {0};
    position.path.indices = path;
    position.path.depth = 1;
    position.kind = SOURCE_POS_TEXT;
    position.offset = 1;
    DomBoundary result{nullptr, 0};
    ASSERT_TRUE(dom_boundary_from_source_pos(root, &position, &result));
    EXPECT_EQ(result.node, para);
    EXPECT_EQ(result.offset, 1u);
}

TEST_F(SourcePosBridgeRoundTrip, EmptyModelPlaceholderMapsToContainerCaret) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    MapBuilder model = builder.map();
    model.put("kind", builder.createSymbolItem("node"));
    model.put("content", builder.array().final());
    record_para_source(model.final());
    hello->text = lam::up("\xE2\x80\x8B");
    hello->length = 3;

    // both sides of the placeholder, and its DOM child slot, name the same empty model caret.
    DomBoundary boundaries[] = {{hello, 0}, {hello, 1}, {para, 0}, {para, 1}};
    for (const DomBoundary& boundary : boundaries) {
        SourcePosC position{};
        ASSERT_TRUE(source_pos_from_dom_boundary(&boundary, &position));
        ASSERT_EQ(position.path.depth, 1);
        EXPECT_EQ(position.path.indices[0], 0);
        EXPECT_EQ(position.kind, SOURCE_POS_ELEMENT);
        EXPECT_EQ(position.offset, 0u);
        DomBoundary restored{};
        ASSERT_TRUE(dom_boundary_from_source_pos(root, &position, &restored));
        EXPECT_EQ(restored.node, para);
        EXPECT_EQ(restored.offset, 0u);
        source_pos_free(&position);
    }
}

TEST_F(SourcePosBridgeRoundTrip, AuthoredZeroWidthTextRetainsItsLeafPosition) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    MapBuilder leaf = builder.map();
    leaf.put("kind", builder.createSymbolItem("text"));
    leaf.put("text", "\xE2\x80\x8B");
    MapBuilder model = builder.map();
    model.put("kind", builder.createSymbolItem("node"));
    model.put("content", builder.array().append(leaf.final()).final());
    record_para_source(model.final());
    hello->text = lam::up("\xE2\x80\x8B");
    hello->length = 3;

    DomBoundary boundary{hello, 1};
    SourcePosC position{};
    ASSERT_TRUE(source_pos_from_dom_boundary(&boundary, &position));
    ASSERT_EQ(position.path.depth, 2);
    EXPECT_EQ(position.path.indices[0], 0);
    EXPECT_EQ(position.path.indices[1], 0);
    EXPECT_EQ(position.kind, SOURCE_POS_TEXT);
    // the zero-width space is one code point (three UTF-8 bytes).
    EXPECT_EQ(position.offset, 1u);
    source_pos_free(&position);
}

TEST_F(SourcePosBridgeRoundTrip, FullRoundTripText) {
    DomBoundary in{ (DomNode*)hello, 2 };
    SourcePosC pos;
    ASSERT_TRUE(source_pos_from_dom_boundary(&in, &pos));
    DomBoundary back{ nullptr, 0 };
    ASSERT_TRUE(dom_boundary_from_source_pos((DomNode*)root, &pos, &back));
    EXPECT_EQ(back.node, in.node);
    EXPECT_EQ(back.offset, in.offset);
    source_pos_free(&pos);
}

TEST_F(SourcePosBridgeRoundTrip, GeneratedBeforeContentDoesNotEnterSourceTextPaths) {
    DomElement* before = make_element();
    before->tag_name = lam::up("::before");
    DomText* glyph = make_text("check", 5);
    ASSERT_TRUE(before->append_child(glyph));
    ASSERT_TRUE(para->insert_before(before, hello));

    DomBoundary element_edge{para, 1};
    SourcePosC edge;
    ASSERT_TRUE(source_pos_from_dom_boundary(&element_edge, &edge));
    EXPECT_EQ(edge.offset, 0u);
    source_pos_free(&edge);

    DomBoundary original{hello, 2};
    SourcePosC pos;
    ASSERT_TRUE(source_pos_from_dom_boundary(&original, &pos));
    ASSERT_EQ(pos.path.depth, 2);
    EXPECT_EQ(pos.path.indices[1], 0);
    DomBoundary restored{nullptr, 0};
    ASSERT_TRUE(dom_boundary_from_source_pos(root, &pos, &restored));
    EXPECT_EQ(restored.node, original.node);
    EXPECT_EQ(restored.offset, original.offset);

    // a text-leaf projection can record the wrapper itself at the leaf path.
    render_map_record_path(para_item, tref, pos.path.indices, pos.path.depth);
    ASSERT_TRUE(dom_boundary_from_source_pos(root, &pos, &restored));
    EXPECT_EQ(restored.node, original.node);
    EXPECT_EQ(restored.offset, original.offset);
    source_pos_free(&pos);
    ASSERT_TRUE(para->remove_child(before));
    delete glyph;
    delete before;
}

TEST_F(SourcePosBridgeRoundTrip, MissingPathHasNoMatch) {
    // SourcePos with a path that no element registered.
    SourcePosC pos;
    source_path_init(&pos.path);
    pos.path.indices = (int*)malloc(sizeof(int));
    pos.path.indices[0] = 99;
    pos.path.depth = 1;
    pos.offset = 0;
    pos.kind = SOURCE_POS_ELEMENT;
    DomBoundary out{ nullptr, 0 };
    EXPECT_FALSE(dom_boundary_from_source_pos((DomNode*)root, &pos, &out));
    source_pos_free(&pos);
}

// ---------------------------------------------------------------------------
// dom_selection_apply_source_selection parsing layer (Phase R4 §7.4)
// ---------------------------------------------------------------------------
// We only cover the early-out / shape-validation paths here; integration with
// a live DomSelection + render_map is exercised at the editor lambda-test
// layer (test/lambda/editor/source_selection.ls and friends).

TEST(SourcePosBridgeR4, ApplyRejectsNullArgs) {
    Item bogus = {0};
    EXPECT_FALSE(dom_selection_apply_source_selection(nullptr, nullptr, bogus));
    EXPECT_FALSE(dom_selection_apply_source_selection(nullptr,
        (struct DomNode*)0xdead, bogus));
}

TEST(SourcePosBridgeR4, ApplyRejectsMalformedItem) {
    // ds + root passed as opaque non-null sentinels — the function must
    // bail out at MapReader/shape validation before dereferencing them.
    auto ds_sentinel = (struct DomSelection*)0xdead;
    auto root_sentinel = (struct DomNode*)0xbeef;
    Item not_a_map = {0};  // ITEM_NULL — not a map
    EXPECT_FALSE(dom_selection_apply_source_selection(ds_sentinel,
        root_sentinel, not_a_map));
}

TEST(SourcePosBridgeR4, ApplyRejectsUnknownKind) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder mb(input);
    MapBuilder m = mb.map();
    m.put("kind", mb.createSymbolItem("frobnicate"));
    Item bogus_sel = m.final();

    auto ds_sentinel = (struct DomSelection*)0xdead;
    auto root_sentinel = (struct DomNode*)0xbeef;
    EXPECT_FALSE(dom_selection_apply_source_selection(ds_sentinel,
        root_sentinel, bogus_sel));
}

// ---------------------------------------------------------------------------
// render_map retransform (LR11-2)
// ---------------------------------------------------------------------------
// A re-executed template body records its own apply() results, and those
// inserts resize the render map and move Robin Hood buckets. Iterating the map
// while re-executing skipped a dirty entry whenever one was displaced across
// the iterator. The key hashes an interned pointer, so which layout does that
// varies from run to run; the sweep over fresh maps made the old loop miss an
// entry in about one case in ten, and every case must re-run each entry once.

static const char* const retransform_ref = "lr11_2_template";
static int retransform_invocations = 0;
static int retransform_inserts = 0;
static int64_t retransform_next_child = 0;

static Item retransform_recording_body(Context*, Item) {
    retransform_invocations++;
    for (int i = 0; i < retransform_inserts; i++) {
        int64_t child = retransform_next_child++;
        render_map_record(synthetic_bridge_item(child), "lr11_2_child",
            synthetic_bridge_item(-child), Item{0}, -1);
    }
    return Item{0};
}

static bool retransform_records_inner = false;
static Item retransform_shared_body(Context*, Item) {
    Item result = synthetic_bridge_item(20);
    if (retransform_records_inner) {
        render_map_record(synthetic_bridge_item(3), "inner", result, ItemNull, -1);
    }
    return result;
}

TEST(RenderMapRetransform, WrapperOwnershipRequiresANestedCallInTheCurrentBody) {
    TemplateEntry entry = {};
    entry.template_ref = "outer";
    entry.body_func = (fn_ptr)retransform_shared_body;
    TemplateRegistry registry = {};
    registry.first = registry.last = &entry;
    registry.count = 1;
    test_template_registry = &registry;

    for (bool nested : {false, true}) {
        render_map_destroy();
        retransform_records_inner = nested;
        Item result = synthetic_bridge_item(20);
        render_map_record(synthetic_bridge_item(1), "earlier", result, ItemNull, -1);
        render_map_record(synthetic_bridge_item(2), "outer", synthetic_bridge_item(10), ItemNull, -1);
        render_map_mark_dirty(synthetic_bridge_item(2), "outer");
        ASSERT_EQ(render_map_retransform(), 1);

        RenderMapLookup lookup = {};
        ASSERT_TRUE(render_map_reverse_lookup(result, &lookup));
        EXPECT_EQ(lookup.source_item.item, synthetic_bridge_item(nested ? 3 : 2).item);
        EXPECT_EQ(render_map_wrapper_lookup(lookup, &lookup), nested);
        EXPECT_EQ(lookup.source_item.item, synthetic_bridge_item(2).item);
        EXPECT_FALSE(render_map_wrapper_lookup(lookup, &lookup));
    }

    render_map_destroy();
    test_template_registry = nullptr;
}

TEST(RenderMapRetransform, EveryDirtyEntryRunsOnceDespiteInserts) {
    TemplateEntry entry = {};
    entry.template_ref = retransform_ref;
    entry.body_func = (fn_ptr)retransform_recording_body;
    TemplateRegistry registry = {};
    registry.first = registry.last = &entry;
    registry.count = 1;
    test_template_registry = &registry;

    const int inserts_per_run[] = {1, 16};
    for (int inserts : inserts_per_run) {
        for (int dirty = 2; dirty <= 120; dirty++) {
            render_map_destroy();   // a fresh map for every case
            for (int64_t i = 1; i <= dirty; i++) {
                render_map_record(synthetic_bridge_item(i), retransform_ref,
                    synthetic_bridge_item(-i), Item{0}, -1);
                render_map_mark_dirty(synthetic_bridge_item(i), retransform_ref);
            }
            retransform_invocations = 0;
            retransform_inserts = inserts;
            retransform_next_child = 100000;
            ASSERT_EQ(render_map_retransform(), dirty)
                << dirty << " dirty entries, " << inserts << " inserts each";
            ASSERT_EQ(retransform_invocations, dirty);
            ASSERT_FALSE(render_map_has_dirty());
        }
    }

    render_map_reset();
    test_template_registry = nullptr;
}

TEST(RenderMapRetransform, ForgetsRetiredResultsBeforeTheirAddressesAreReused) {
    render_map_destroy();
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item old_leaf = builder.element("span").text("old").final();
    Item old_page = builder.element("section").child(old_leaf).final();
    Item new_page = builder.element("section").text("new").final();
    Item root = builder.element("main").child(new_page).final();
    Item old_source = synthetic_bridge_item(1);
    Item new_source = synthetic_bridge_item(2);
    render_map_record(old_source, "old", old_page, ItemNull, -1);
    render_map_record(new_source, "new", new_page, root, 0);
    render_map_set_doc_root(root);

    size_t forward = 0, reverse = 0;
    render_map_get_counts(nullptr, &forward, &reverse);
    EXPECT_EQ(forward, 0u);
    EXPECT_EQ(reverse, 0u);
    render_map_get_counts(context, &forward, &reverse);
    EXPECT_EQ(forward, 2u);
    EXPECT_GE(reverse, 3u);
    render_map_forget_retired_result(context, old_leaf);
    render_map_forget_retired_result(context, old_page);
    render_map_get_counts(context, &forward, &reverse);
    EXPECT_EQ(forward, 1u);
    EXPECT_EQ(get_type_id(render_map_get_result(old_source, "old")), LMD_TYPE_NULL);
    RenderMapLookup lookup = {};
    EXPECT_FALSE(render_map_reverse_lookup(old_leaf, &lookup));
    ASSERT_TRUE(render_map_reverse_lookup(new_page, &lookup));
    EXPECT_EQ(lookup.source_item.item, new_source.item);

    // Reusing a retired address must install the new owner's nested route.
    Item reused_page = builder.element("article").child(old_leaf).final();
    render_map_record(new_source, "reused", reused_page, ItemNull, -1);
    ASSERT_TRUE(render_map_reverse_lookup(old_leaf, &lookup));
    EXPECT_EQ(lookup.source_item.item, new_source.item);
    render_map_destroy();
}

TEST(RenderMapRetransform, RetiringOldResultPreservesReplacementAndLiveFragments) {
    render_map_destroy();
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item old_leaf = builder.element("span").text("old").final();
    Item leaf = builder.element("span").text("live").final();
    Item fragment = builder.array().append(leaf).final();
    Item empty = builder.array().final();
    Item root = builder.element("main").child(leaf).final();
    Item source = synthetic_bridge_item(1);
    render_map_record(source, "fragment", old_leaf, root, 0);
    render_map_record(source, "fragment", fragment, root, 0);
    render_map_record(source, "empty", empty, ItemNull, -1);
    render_map_bind_fragment_parent(empty, root, 1, 0);
    render_map_forget_retired_result(context, old_leaf);
    EXPECT_EQ(render_map_get_result(source, "fragment").item, fragment.item);
    EXPECT_EQ(render_map_get_result(source, "empty").item, empty.item);
    RenderMapLookup lookup = {};
    EXPECT_FALSE(render_map_reverse_lookup(old_leaf, &lookup));
    EXPECT_TRUE(render_map_reverse_lookup(leaf, &lookup));
    render_map_destroy();
}
