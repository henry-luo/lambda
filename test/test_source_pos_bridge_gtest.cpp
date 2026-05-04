// test_source_pos_bridge_gtest.cpp — C-side helpers for the editor
// source-position bridge (Phase R7, Radiant integration step 1).
// The pure-Lambda half is exercised via the auto-discovered test
// `editor_dom_bridge_basic.ls`; this file covers the small C-side
// scaffolding (path lifecycle, equality, stub return values).

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include "../lambda/lambda-data.hpp"
#include "../radiant/source_pos_bridge.hpp"

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
