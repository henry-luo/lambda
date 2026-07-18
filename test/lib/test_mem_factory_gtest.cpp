#include <gtest/gtest.h>
#include "../../lib/mem_factory.h"
#include "../../lib/mem_context.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../../lib/scratch_arena.h"
#include <string.h>

// Exercises the allocator factory with REAL Pool/Arena allocators: snapshot
// reflects real sizes, parent edges link arena -> backing pool, and cascade
// teardown destroys real allocators.
class MemFactoryTest : public ::testing::Test {
protected:
    void SetUp() override { mem_context_shutdown(); }
    void TearDown() override { mem_context_shutdown(); }
};

TEST_F(MemFactoryTest, PoolCreateDestroyRegisters) {
    MemContext* root = mem_context_root();
    Pool* p = mem_pool_create(root, MEM_ROLE_INPUT, "factory.pool");
    ASSERT_NE(p, nullptr);
    EXPECT_NE(pool_get_mem_node(p), nullptr);
    EXPECT_EQ(mem_context_live_count(root), 1u);

    // allocate something so the pool reports non-zero usage
    void* buf = pool_calloc(p, 4096);
    ASSERT_NE(buf, nullptr);

    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->count, 1u);
    EXPECT_EQ(snap->samples[0].kind, MEM_KIND_POOL);
    EXPECT_GE(snap->samples[0].bytes_in_use, 4096u);
    EXPECT_STREQ(snap->samples[0].label, "factory.pool");
    mem_snapshot_free(snap);

    mem_pool_destroy(p);
    EXPECT_EQ(mem_context_live_count(root), 0u);
}

TEST_F(MemFactoryTest, MmapPoolFlagged) {
    MemContext* root = mem_context_root();
    Pool* p = mem_pool_create_mmap(root, MEM_ROLE_TEMP, "factory.mmap");
    ASSERT_NE(p, nullptr);
    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->count, 1u);
    EXPECT_TRUE(snap->samples[0].flags & MEM_FLAG_MMAP);
    EXPECT_GT(snap->samples[0].bytes_reserved, 0u);  // mmap chunk reserved
    mem_snapshot_free(snap);
    mem_pool_destroy(p);
}

TEST_F(MemFactoryTest, ArenaParentEdgeToPool) {
    MemContext* root = mem_context_root();
    Pool* p = mem_pool_create(root, MEM_ROLE_INPUT, "pool");
    Arena* a = mem_arena_create(root, p, MEM_ROLE_VIEW, "arena");
    ASSERT_NE(a, nullptr);

    // pool node should now have one child (the arena)
    MemNode* pool_node = (MemNode*)pool_get_mem_node(p);
    EXPECT_EQ(mem_node_child_count(pool_node), 1u);

    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->count, 2u);
    uint32_t pool_id = mem_node_id(pool_node);
    bool arena_linked = false;
    uint64_t arena_backing = 0;
    uint64_t pool_direct = 0;
    uint64_t pool_live = 0;
    for (uint32_t i = 0; i < snap->count; i++) {
        if (snap->samples[i].kind == MEM_KIND_ARENA) {
            EXPECT_EQ(snap->samples[i].parent_id, pool_id);
            EXPECT_GT(snap->samples[i].backing_bytes, 0u);
            EXPECT_EQ(snap->samples[i].committed_bytes, ARENA_INITIAL_CHUNK_SIZE);
            arena_backing = snap->samples[i].backing_bytes;
            arena_linked = true;
        } else if (snap->samples[i].id == pool_id) {
            pool_direct = snap->samples[i].direct_bytes;
            pool_live = snap->samples[i].bytes_in_use;
        }
    }
    EXPECT_TRUE(arena_linked);
    EXPECT_EQ(pool_live, pool_direct + arena_backing);
    EXPECT_EQ(snap->physical_total_reserved, snap->samples[0].parent_id == 0 &&
              snap->samples[0].id == pool_id ? snap->samples[0].bytes_reserved :
              snap->samples[1].bytes_reserved);
    mem_snapshot_free(snap);

    mem_arena_destroy(a);
    EXPECT_EQ(mem_node_child_count(pool_node), 0u);
    mem_pool_destroy(p);
}

TEST_F(MemFactoryTest, CascadeTeardownDestroysRealAllocators) {
    // A per-document sub-context owning a pool + arena; destroying it should
    // free both in dependency order (arena before its backing pool).
    MemContext* doc = mem_context_create(nullptr, MEM_ROLE_INPUT, "doc");
    Pool* p = mem_pool_create(doc, MEM_ROLE_INPUT, "doc.pool");
    Arena* a = mem_arena_create(doc, p, MEM_ROLE_VIEW, "doc.arena");
    ASSERT_NE(a, nullptr);
    // use the arena
    void* x = arena_alloc(a, 1024);
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(mem_context_live_count(doc), 2u);

    mem_context_destroy(doc);  // must not crash; frees arena then pool
    // root is unaffected and empty
    EXPECT_EQ(mem_context_live_count(mem_context_root()), 0u);
}

TEST_F(MemFactoryTest, UntrackedPoolDestroyIsSafe) {
    // a raw pool (not via factory) carries a NULL mem_node; mem_pool_destroy
    // must handle it gracefully.
    Pool* p = pool_create();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(pool_get_mem_node(p), nullptr);
    mem_pool_destroy(p);  // unregister(NULL) + pool_destroy
}

TEST_F(MemFactoryTest, SizedArena) {
    MemContext* root = mem_context_root();
    Pool* p = mem_pool_create(root, MEM_ROLE_INPUT, "pool");
    Arena* a = mem_arena_create_sized(root, p, 16 * 1024, 64 * 1024,
                                      MEM_ROLE_LAYOUT, "sized.arena");
    ASSERT_NE(a, nullptr);
    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    bool found = false;
    for (uint32_t i = 0; i < snap->count; i++) {
        if (snap->samples[i].kind == MEM_KIND_ARENA) {
            EXPECT_GE(snap->samples[i].bytes_reserved, 16u * 1024u);
            found = true;
        }
    }
    EXPECT_TRUE(found);
    mem_snapshot_free(snap);
    mem_arena_destroy(a);
    mem_pool_destroy(p);
}

// ---- Scratch arena factory ----

TEST_F(MemFactoryTest, ScratchInitRegistersAndReleaseUnregisters) {
    MemContext* root = mem_context_root();
    Pool* p = mem_pool_create(root, MEM_ROLE_LAYOUT, "pool");
    Arena* a = mem_arena_create(root, p, MEM_ROLE_LAYOUT, "arena");
    ScratchArena sa;
    mem_scratch_init(root, &sa, a, MEM_ROLE_LAYOUT, "layout.scratch");
    EXPECT_NE(sa.mem_node, nullptr);

    // scratch node is a child of its backing arena
    MemNode* arena_node = (MemNode*)arena_get_mem_node(a);
    EXPECT_EQ(mem_node_child_count(arena_node), 1u);
    EXPECT_EQ(mem_context_live_count(root), 3u);  // pool + arena + scratch

    void* x = scratch_alloc(&sa, 256);
    ASSERT_NE(x, nullptr);

    scratch_release(&sa);                 // auto-unregisters the scratch node
    EXPECT_EQ(sa.mem_node, nullptr);
    EXPECT_EQ(mem_node_child_count(arena_node), 0u);
    EXPECT_EQ(mem_context_live_count(root), 2u);  // pool + arena remain

    mem_arena_destroy(a);
    mem_pool_destroy(p);
}
