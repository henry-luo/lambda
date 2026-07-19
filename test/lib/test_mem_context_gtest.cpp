#include <gtest/gtest.h>
#include "../../lib/mem_context.h"
#include <string.h>
#include <stdlib.h>
#include <atomic>
#include <thread>
#include <vector>

// ----------------------------------------------------------------------------
// Mock allocator: a struct with reportable stats and a destroy flag.
// ----------------------------------------------------------------------------
struct MockAlloc {
    uint64_t reserved;
    uint64_t in_use;
    uint64_t allocs;
    std::atomic<int>* destroy_counter;  // incremented on destroy (nullable)
};

static bool mock_stat(void* a, MemStatSample* s) {
    MockAlloc* m = (MockAlloc*)a;
    s->bytes_reserved = m->reserved;
    s->bytes_in_use = m->in_use;
    s->alloc_count = m->allocs;
    s->chunk_count = 1;
    return true;
}

static void mock_destroy(void* a) {
    MockAlloc* m = (MockAlloc*)a;
    if (m->destroy_counter) m->destroy_counter->fetch_add(1);
    free(m);
}

static MockAlloc* make_mock(uint64_t reserved, uint64_t in_use,
                            std::atomic<int>* counter) {
    MockAlloc* m = (MockAlloc*)calloc(1, sizeof(MockAlloc));
    m->reserved = reserved;
    m->in_use = in_use;
    m->allocs = 1;
    m->destroy_counter = counter;
    return m;
}

// Reset global registry state between tests for isolation.
class MemContextTest : public ::testing::Test {
protected:
    void SetUp() override { mem_context_shutdown(); }
    void TearDown() override { mem_context_shutdown(); }
};

// ============================================================================
// Context lifecycle
// ============================================================================

TEST_F(MemContextTest, RootExists) {
    MemContext* root = mem_context_root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(mem_context_root(), root);  // stable singleton
    EXPECT_EQ(mem_context_live_count(root), 0u);
}

TEST_F(MemContextTest, CreateSubContextInheritsDoc) {
    MemContext* root = mem_context_root();
    mem_context_set_doc_id(root, 7);
    MemContext* child = mem_context_create(root, MEM_ROLE_INPUT, "child");
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(mem_context_doc_id(child), 7u);
    mem_context_destroy(child);
}

// ============================================================================
// Node registration
// ============================================================================

TEST_F(MemContextTest, RegisterUnregisterCounts) {
    MemContext* root = mem_context_root();
    MockAlloc* m = make_mock(100, 50, nullptr);
    MemNode* n = mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "p", m,
                              nullptr, mock_stat, nullptr);
    ASSERT_NE(n, nullptr);
    EXPECT_GT(mem_node_id(n), 0u);
    EXPECT_EQ(mem_context_live_count(root), 1u);
    mem_unregister(n);
    EXPECT_EQ(mem_context_live_count(root), 0u);
    free(m);
}

TEST_F(MemContextTest, ParentChildEdge) {
    MemContext* root = mem_context_root();
    MockAlloc* mp = make_mock(1000, 0, nullptr);
    MockAlloc* ma = make_mock(500, 0, nullptr);
    MemNode* pool = mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "pool", mp,
                                 nullptr, mock_stat, nullptr);
    MemNode* arena = mem_register(root, MEM_KIND_ARENA, MEM_ROLE_VIEW, "arena", ma,
                                  pool, mock_stat, nullptr);
    EXPECT_EQ(mem_node_child_count(pool), 1u);
    EXPECT_EQ(mem_node_child_count(arena), 0u);
    mem_unregister(arena);
    EXPECT_EQ(mem_node_child_count(pool), 0u);
    mem_unregister(pool);
    free(mp); free(ma);
}

TEST_F(MemContextTest, Reparent) {
    MemContext* root = mem_context_root();
    MockAlloc* m1 = make_mock(1, 0, nullptr);
    MockAlloc* m2 = make_mock(1, 0, nullptr);
    MockAlloc* m3 = make_mock(1, 0, nullptr);
    MemNode* p1 = mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "p1", m1, nullptr, mock_stat, nullptr);
    MemNode* p2 = mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "p2", m2, nullptr, mock_stat, nullptr);
    MemNode* a  = mem_register(root, MEM_KIND_ARENA, MEM_ROLE_VIEW, "a", m3, p1, mock_stat, nullptr);
    EXPECT_EQ(mem_node_child_count(p1), 1u);
    mem_reparent(a, p2);
    EXPECT_EQ(mem_node_child_count(p1), 0u);
    EXPECT_EQ(mem_node_child_count(p2), 1u);
    mem_unregister(a); mem_unregister(p1); mem_unregister(p2);
    free(m1); free(m2); free(m3);
}

TEST_F(MemContextTest, DisabledContextReturnsNull) {
    MemContext* c = mem_context_create(nullptr, MEM_ROLE_TEMP, "off");
    mem_context_set_enabled(c, false);
    MockAlloc* m = make_mock(1, 0, nullptr);
    MemNode* n = mem_register(c, MEM_KIND_POOL, MEM_ROLE_TEMP, "p", m, nullptr, mock_stat, nullptr);
    EXPECT_EQ(n, nullptr);
    free(m);
    mem_context_destroy(c);
}

// ============================================================================
// Cascade teardown
// ============================================================================

TEST_F(MemContextTest, CascadeDestroyInvokesDestroyFn) {
    std::atomic<int> destroyed{0};
    MemContext* doc = mem_context_create(nullptr, MEM_ROLE_INPUT, "doc");
    MemNode* pool = mem_register(doc, MEM_KIND_POOL, MEM_ROLE_INPUT, "pool",
                                 make_mock(1000, 0, &destroyed), nullptr,
                                 mock_stat, mock_destroy);
    mem_register(doc, MEM_KIND_ARENA, MEM_ROLE_VIEW, "arena",
                 make_mock(500, 0, &destroyed), pool, mock_stat, mock_destroy);
    mem_register(doc, MEM_KIND_SCRATCH, MEM_ROLE_LAYOUT, "scratch",
                 make_mock(200, 0, &destroyed), pool, mock_stat, mock_destroy);
    EXPECT_EQ(mem_context_live_count(doc), 3u);
    mem_context_destroy(doc);
    EXPECT_EQ(destroyed.load(), 3);  // all allocators freed
}

TEST_F(MemContextTest, CascadeDestroyChildContextsFirst) {
    std::atomic<int> destroyed{0};
    MemContext* parent = mem_context_create(nullptr, MEM_ROLE_RUNTIME_HEAP, "parent");
    MemContext* child = mem_context_create(parent, MEM_ROLE_INPUT, "child");
    mem_register(parent, MEM_KIND_POOL, MEM_ROLE_RUNTIME_HEAP, "pp",
                 make_mock(1, 0, &destroyed), nullptr, mock_stat, mock_destroy);
    mem_register(child, MEM_KIND_POOL, MEM_ROLE_INPUT, "cp",
                 make_mock(1, 0, &destroyed), nullptr, mock_stat, mock_destroy);
    mem_context_destroy(parent);  // should also destroy child
    EXPECT_EQ(destroyed.load(), 2);
}

// ============================================================================
// Document URL registry
// ============================================================================

TEST_F(MemContextTest, DocRegistry) {
    uint32_t page = mem_doc_register("https://example.com/a.html", 0);
    uint32_t css  = mem_doc_register("a.css", page);
    EXPECT_GE(page, 1u);
    EXPECT_NE(page, css);
    EXPECT_STREQ(mem_doc_url(page), "https://example.com/a.html");
    EXPECT_STREQ(mem_doc_url(css), "a.css");
    EXPECT_EQ(mem_doc_parent(css), page);
    EXPECT_EQ(mem_doc_parent(page), 0u);
    EXPECT_EQ(mem_doc_url(0), nullptr);
    EXPECT_EQ(mem_doc_url(99999), nullptr);
    mem_doc_retire(page);  // entry preserved
    EXPECT_STREQ(mem_doc_url(page), "https://example.com/a.html");
}

// ============================================================================
// Snapshot
// ============================================================================

TEST_F(MemContextTest, SnapshotCountsAndTotals) {
    MemContext* root = mem_context_root();
    MockAlloc* m1 = make_mock(1000, 600, nullptr);
    MockAlloc* m2 = make_mock(2000, 1500, nullptr);
    MemNode* p = mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "pool", m1, nullptr, mock_stat, nullptr);
    mem_register(root, MEM_KIND_ARENA, MEM_ROLE_VIEW, "arena", m2, p, mock_stat, nullptr);

    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->count, 2u);
    EXPECT_EQ(snap->total_reserved, 3000u);
    EXPECT_EQ(snap->total_in_use, 2100u);
    EXPECT_EQ(snap->physical_total_reserved, 1000u);
    EXPECT_EQ(snap->physical_total_in_use, 600u);

    // verify parent linkage is present in the flat samples
    bool found_arena_with_parent = false;
    uint32_t pool_id = mem_node_id(p);
    for (uint32_t i = 0; i < snap->count; i++) {
        if (snap->samples[i].kind == MEM_KIND_ARENA) {
            EXPECT_EQ(snap->samples[i].parent_id, pool_id);
            found_arena_with_parent = true;
        }
    }
    EXPECT_TRUE(found_arena_with_parent);
    mem_snapshot_free(snap);
    free(m1); free(m2);
}

TEST_F(MemContextTest, SnapshotIncludesChildContexts) {
    MemContext* root = mem_context_root();
    MemContext* doc = mem_context_create(root, MEM_ROLE_INPUT, "doc");
    mem_register(root, MEM_KIND_POOL, MEM_ROLE_RUNTIME_HEAP, "global",
                 make_mock(100, 0, nullptr), nullptr, mock_stat, mock_destroy);
    mem_register(doc, MEM_KIND_POOL, MEM_ROLE_INPUT, "docpool",
                 make_mock(200, 0, nullptr), nullptr, mock_stat, mock_destroy);
    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->count, 2u);  // global + docpool
    mem_snapshot_free(snap);
    mem_context_destroy(doc);
}

TEST_F(MemContextTest, SnapshotJsonContainsFields) {
    MemContext* root = mem_context_root();
    uint32_t doc = mem_doc_register("https://x.test/p.html", 0);
    mem_context_set_doc_id(root, doc);
    mem_register(root, MEM_KIND_POOL, MEM_ROLE_INPUT, "mypool",
                 make_mock(4096, 1024, nullptr), nullptr, mock_stat, mock_destroy);
    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    char* json = mem_snapshot_to_json(snap);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"label\":\"mypool\""), nullptr);
    EXPECT_NE(strstr(json, "\"kind\":\"pool\""), nullptr);
    EXPECT_NE(strstr(json, "\"role\":\"input\""), nullptr);
    EXPECT_NE(strstr(json, "\"bytes_reserved\":4096"), nullptr);
    EXPECT_NE(strstr(json, "\"physical_total\""), nullptr);
    EXPECT_NE(strstr(json, "\"logical_domains\""), nullptr);
    EXPECT_NE(strstr(json, "https://x.test/p.html"), nullptr);
    free(json);
    mem_snapshot_free(snap);
}

// ============================================================================
// Thread safety — concurrent register/unregister/capture
// ============================================================================

TEST_F(MemContextTest, ConcurrentRegisterUnregister) {
    MemContext* root = mem_context_root();
    const int kThreads = 8;
    const int kIters = 2000;
    std::vector<std::thread> threads;
    std::atomic<int> live{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&, t]() {
            // each thread owns its own sub-context
            MemContext* ctx = mem_context_create(root, MEM_ROLE_TEMP, "worker");
            for (int i = 0; i < kIters; i++) {
                MockAlloc* m = make_mock((uint64_t)(i + 1), 0, nullptr);
                MemNode* n = mem_register(ctx, MEM_KIND_ARENA, MEM_ROLE_TEMP,
                                          "w", m, nullptr, mock_stat, nullptr);
                if (n) {
                    live.fetch_add(1);
                    // occasionally take a snapshot from the root concurrently
                    if ((i & 0x3f) == 0) {
                        MemSnapshot* s = mem_snapshot_capture(root);
                        if (s) mem_snapshot_free(s);
                    }
                    mem_unregister(n);
                    live.fetch_sub(1);
                }
                free(m);
            }
            mem_context_destroy(ctx);
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(live.load(), 0);
    // root has no leftover direct nodes; all worker contexts destroyed
    EXPECT_EQ(mem_context_live_count(root), 0u);
    MemSnapshot* snap = mem_snapshot_capture(root);
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->count, 0u);
    mem_snapshot_free(snap);
}
