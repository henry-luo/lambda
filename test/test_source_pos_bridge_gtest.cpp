// test_source_pos_bridge_gtest.cpp — C-side helpers for the editor
// source-position bridge (Phase R7, Radiant integration step 1).
// The pure-Lambda half is exercised via the auto-discovered test
// `editor_dom_bridge_basic.ls`; this file covers the small C-side
// scaffolding (path lifecycle, equality, stub return values).

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda-data.hpp"
#include "../lambda/render_map.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/input/input.hpp"
#include "../radiant/dom_range.hpp"
#include "../radiant/source_pos_bridge.hpp"

// ---------------------------------------------------------------------------
// Bridge stubs (see test_dom_range_gtest.cpp). dom_range.cpp links against
// these RadiantState accessors but the bridge tests don't exercise the
// selection-resync paths, so trivial stubs satisfy the linker.
// ---------------------------------------------------------------------------
struct Arena;
struct RadiantState;
struct DomRange;
struct DomSelection;

extern "C" Arena* dom_range_state_arena(RadiantState*) { return nullptr; }
extern "C" DomRange** dom_range_state_live_ranges_slot(RadiantState*) {
    static DomRange* slot = nullptr;
    return &slot;
}
extern "C" struct DomSelection* dom_range_state_selection(RadiantState*) {
    return nullptr;
}

// render_map.cpp pulls in g_template_registry (defined in template_registry.cpp)
// and lambda-mem.cpp's GC root registration. Stub them out for unit tests.
struct TemplateRegistry;
extern "C" TemplateRegistry* g_template_registry = nullptr;
extern "C" void heap_register_gc_root(uint64_t*) {}
extern "C" void heap_register_gc_root_range(uint64_t*, int) {}

TEST(SourcePosBridgeC, PathInitFreeIsSafe) {
    SourcePathC p;
    source_path_init(&p);
    EXPECT_EQ(p.indices, nullptr);
    EXPECT_EQ(p.depth, 0);
    source_path_free(&p);  // double-free of init'd state is safe
    source_path_free(&p);
}

TEST(SourcePosBridgeC, PathCloneAndEqual) {
    SourcePathC src;
    source_path_init(&src);
    src.depth = 3;
    src.indices = (int*)malloc(sizeof(int) * 3);
    src.indices[0] = 4;
    src.indices[1] = 0;
    src.indices[2] = 7;

    SourcePathC dup = source_path_clone(&src);
    EXPECT_EQ(dup.depth, 3);
    ASSERT_NE(dup.indices, nullptr);
    EXPECT_NE(dup.indices, src.indices);
    EXPECT_EQ(dup.indices[0], 4);
    EXPECT_EQ(dup.indices[2], 7);
    EXPECT_TRUE(source_path_equal(&src, &dup));

    dup.indices[1] = 1;
    EXPECT_FALSE(source_path_equal(&src, &dup));

    source_path_free(&src);
    source_path_free(&dup);
}

TEST(SourcePosBridgeC, EmptyPathsAreEqual) {
    SourcePathC a, b;
    source_path_init(&a);
    source_path_init(&b);
    EXPECT_TRUE(source_path_equal(&a, &b));
    SourcePathC c = source_path_clone(&a);
    EXPECT_EQ(c.depth, 0);
    EXPECT_EQ(c.indices, nullptr);
    EXPECT_TRUE(source_path_equal(&a, &c));
}

TEST(SourcePosBridgeC, SourcePosFreeReleasesPath) {
    SourcePosC p;
    source_path_init(&p.path);
    p.path.depth = 2;
    p.path.indices = (int*)malloc(sizeof(int) * 2);
    p.path.indices[0] = 0;
    p.path.indices[1] = 1;
    p.offset = 5;
    p.kind = SOURCE_POS_TEXT;
    source_pos_free(&p);
    EXPECT_EQ(p.path.indices, nullptr);
    EXPECT_EQ(p.path.depth, 0);
    EXPECT_EQ(p.offset, 0u);
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

TEST(SourcePosBridgePathTable, RecordAndLookupRoundTrip) {
    source_pos_bridge_reset();
    render_map_reset();

    // Two synthetic source items + two synthetic result items.
    Item src_a; src_a.item = 0xA000ULL;
    Item src_b; src_b.item = 0xB000ULL;
    Item res_a; res_a.item = 0xA111ULL;
    Item res_b; res_b.item = 0xB222ULL;
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

    Item src; src.item = 0xCAFE;
    Item res; res.item = 0xBEEF;
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
    Item src; src.item = 0x1234;
    Item res; res.item = 0x5678;
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

TEST(SourcePosBridgePathTable, MissOnUnknownResultItem) {
    source_pos_bridge_reset();
    render_map_reset();
    Item ghost; ghost.item = 0xDEADBEEFULL;
    RenderMapLookup lk;
    SourcePathC out;
    EXPECT_FALSE(render_map_reverse_lookup_with_path(ghost, &lk, &out));
    EXPECT_EQ(out.depth, 0);
    EXPECT_EQ(out.indices, nullptr);
    source_pos_bridge_reset();
    render_map_reset();
}

// ---------------------------------------------------------------------------
// End-to-end round-trip: tiny synthetic DOM + render_map + path table.
//
// Source tree (modeled, not built):  doc [ para [ "hello" ], hr ]
// DOM tree:                          div [ p   [ "hello" ], b  ]
//
// The DomElements get stand-in `native_element` Item values that are
// recorded in render_map alongside their source paths via
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

    // Stand-in Items: we never dereference these as real Elements; only the
    // 64-bit Item bits matter for render_map keying.
    Item root_item{};
    Item para_item{};
    Item hr_item{};
    const char* tref = "rt_template";

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        arena = arena_create_default(pool);
        doc_storage.pool = pool;
        doc_storage.arena = arena;

        root  = make_element();
        para  = make_element();
        hr    = make_element();
        hello = make_text("hello", 5);
        ASSERT_TRUE(root->append_child(para));
        ASSERT_TRUE(root->append_child(hr));
        ASSERT_TRUE(para->append_child(hello));

        // Distinct synthetic Item bits per element, plant in native_element.
        root_item.item = 0xA0001ULL;
        para_item.item = 0xA0002ULL;
        hr_item.item   = 0xA0003ULL;
        root->native_element = (Element*)(uintptr_t)root_item.item;
        para->native_element = (Element*)(uintptr_t)para_item.item;
        hr->native_element   = (Element*)(uintptr_t)hr_item.item;

        // Source items used as render_map keys; values don't matter beyond
        // uniqueness. Use the same bits as the result items.
        source_pos_bridge_reset();
        render_map_reset();
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
        if (pool) pool_destroy(pool);
    }

    DomElement* make_element() {
        DomElement* e = new DomElement();
        e->doc = &doc_storage;
        return e;
    }
    DomText* make_text(const char* s, size_t len) {
        DomText* t = new DomText();
        t->text = s;
        t->length = len;
        return t;
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
// MarkBuilder helpers — produce the Lambda `pos` / `selection` shapes used
// by lambda/package/editor/mod_source_pos.ls.
// ---------------------------------------------------------------------------

#include "../lambda/lambda.h"
#include "../lambda/mark_reader.hpp"

namespace {

SourcePosC make_pos(std::initializer_list<int> indices, uint32_t offset,
                    SourcePosKind kind) {
    SourcePosC p;
    source_path_init(&p.path);
    p.offset = offset;
    p.kind = kind;
    if (indices.size() > 0) {
        p.path.indices = (int*)malloc(sizeof(int) * indices.size());
        int i = 0;
        for (int v : indices) p.path.indices[i++] = v;
        p.path.depth = (int)indices.size();
    }
    return p;
}

} // namespace

TEST(SourcePosBridgeMarkBuilder, PosToItemBuildsMap) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder mb(input);

    SourcePosC p = make_pos({0, 2, 1}, 7, SOURCE_POS_TEXT);
    Item item = source_pos_to_item(mb, &p);

    MapReader m = MapReader::fromItem(item);
    ItemReader path = m.get("path");
    ItemReader off  = m.get("offset");
    EXPECT_TRUE(path.isArray());
    EXPECT_TRUE(off.isInt());
    EXPECT_EQ(off.asInt(), 7);
    ArrayReader arr = path.asArray();
    EXPECT_EQ(arr.length(), 3);

    source_pos_free(&p);
}

TEST(SourcePosBridgeMarkBuilder, PosToItemEmptyPath) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder mb(input);

    SourcePosC p = make_pos({}, 0, SOURCE_POS_ELEMENT);
    Item item = source_pos_to_item(mb, &p);
    MapReader m = MapReader::fromItem(item);
    ItemReader path = m.get("path");
    EXPECT_TRUE(path.isArray());
    EXPECT_EQ(path.asArray().length(), 0);

    source_pos_free(&p);
}

TEST(SourcePosBridgeMarkBuilder, TextSelectionShape) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder mb(input);

    SourcePosC anchor = make_pos({0, 0}, 1, SOURCE_POS_TEXT);
    SourcePosC head   = make_pos({0, 0}, 4, SOURCE_POS_TEXT);
    Item sel = source_text_selection_to_item(mb, &anchor, &head);

    MapReader m = MapReader::fromItem(sel);
    ItemReader kind = m.get("kind");
    EXPECT_TRUE(kind.isSymbol());
    EXPECT_STREQ(kind.asSymbol()->chars, "text");
    EXPECT_TRUE(m.get("anchor").isMap());
    EXPECT_TRUE(m.get("head").isMap());

    source_pos_free(&anchor);
    source_pos_free(&head);
}

TEST(SourcePosBridgeMarkBuilder, NodeSelectionShape) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder mb(input);

    SourcePathC path;
    source_path_init(&path);
    path.indices = (int*)malloc(sizeof(int) * 2);
    path.indices[0] = 0;
    path.indices[1] = 3;
    path.depth = 2;

    Item sel = source_node_selection_to_item(mb, &path);
    MapReader m = MapReader::fromItem(sel);
    ItemReader kind = m.get("kind");
    EXPECT_TRUE(kind.isSymbol());
    EXPECT_STREQ(kind.asSymbol()->chars, "node");
    ItemReader p = m.get("path");
    EXPECT_TRUE(p.isArray());
    EXPECT_EQ(p.asArray().length(), 2);

    source_path_free(&path);
}
