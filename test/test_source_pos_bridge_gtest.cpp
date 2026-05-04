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
    // The DOM glue stubs intentionally return false until render_map is
    // extended with path recording. This locks in the contract.
    SourcePosC pos;
    source_path_init(&pos.path);
    pos.offset = 0;
    pos.kind = SOURCE_POS_TEXT;
    EXPECT_FALSE(source_pos_from_dom_boundary(nullptr, &pos));
    EXPECT_FALSE(source_pos_from_dom_range(nullptr, &pos, &pos));
    EXPECT_FALSE(dom_boundary_from_source_pos(Item{0}, &pos, nullptr));
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
