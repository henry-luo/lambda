// test/lib/test_lru_cache_gtest.cpp - tests for lib/lru_cache
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

extern "C" {
#include "../../lib/lru_cache.h"
}

namespace {

struct EvictRecord {
    std::string key;
    void* value;
    size_t bytes;
};

void capture_evict(const char* key, void* value, size_t bytes, void* udata) {
    auto* vec = static_cast<std::vector<EvictRecord>*>(udata);
    vec->push_back({key, value, bytes});
}

LruCache* make_cache(size_t max_entries = 0, size_t max_bytes = 0,
                     uint64_t default_ttl_ms = 0,
                     LruEvictFn on_evict = nullptr, void* udata = nullptr) {
    LruCacheConfig cfg{};
    cfg.max_entries = max_entries;
    cfg.max_bytes = max_bytes;
    cfg.default_ttl_ms = default_ttl_ms;
    cfg.on_evict = on_evict;
    cfg.udata = udata;
    return lru_cache_new(&cfg);
}

}  // namespace

TEST(LruCacheTest, CreateDestroy) {
    LruCache* c = make_cache();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(lru_cache_count(c), 0u);
    EXPECT_EQ(lru_cache_bytes(c), 0u);
    lru_cache_free(c);
}

TEST(LruCacheTest, PutGet) {
    LruCache* c = make_cache();
    int a = 1, b = 2;
    EXPECT_TRUE(lru_cache_put(c, "a", &a, sizeof(a)));
    EXPECT_TRUE(lru_cache_put(c, "b", &b, sizeof(b)));
    EXPECT_EQ(lru_cache_count(c), 2u);
    EXPECT_EQ(lru_cache_bytes(c), 2 * sizeof(int));
    EXPECT_EQ(lru_cache_get(c, "a"), &a);
    EXPECT_EQ(lru_cache_get(c, "b"), &b);
    EXPECT_EQ(lru_cache_get(c, "missing"), nullptr);
    lru_cache_free(c);
}

TEST(LruCacheTest, ReplaceCallsEvictForOldValue) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(0, 0, 0, capture_evict, &evicts);
    int a = 1, a2 = 99;
    lru_cache_put(c, "k", &a, sizeof(a));
    lru_cache_put(c, "k", &a2, sizeof(a2));
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "k");
    EXPECT_EQ(evicts[0].value, &a);
    EXPECT_EQ(lru_cache_count(c), 1u);
    EXPECT_EQ(lru_cache_get(c, "k"), &a2);
    lru_cache_free(c);
}

TEST(LruCacheTest, MaxEntriesEvictsLRU) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(/*max_entries=*/2, 0, 0, capture_evict, &evicts);
    int a = 1, b = 2, d = 3;
    lru_cache_put(c, "a", &a, sizeof(a));
    lru_cache_put(c, "b", &b, sizeof(b));
    // touch "a" so "b" becomes LRU
    EXPECT_EQ(lru_cache_get(c, "a"), &a);
    lru_cache_put(c, "c", &d, sizeof(d));  // should evict "b"
    EXPECT_EQ(lru_cache_count(c), 2u);
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "b");
    EXPECT_EQ(lru_cache_get(c, "b"), nullptr);
    EXPECT_NE(lru_cache_get(c, "a"), nullptr);
    EXPECT_NE(lru_cache_get(c, "c"), nullptr);
    lru_cache_free(c);
}

TEST(LruCacheTest, MaxBytesEvicts) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(0, /*max_bytes=*/100, 0, capture_evict, &evicts);
    int v = 0;
    lru_cache_put(c, "a", &v, 40);
    lru_cache_put(c, "b", &v, 40);
    EXPECT_EQ(lru_cache_bytes(c), 80u);
    lru_cache_put(c, "c", &v, 40);  // 120 > 100 → evict "a"
    EXPECT_LE(lru_cache_bytes(c), 100u);
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "a");
    lru_cache_free(c);
}

TEST(LruCacheTest, PeekDoesNotTouch) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(2, 0, 0, capture_evict, &evicts);
    int a = 1, b = 2, d = 3;
    lru_cache_put(c, "a", &a, sizeof(a));
    lru_cache_put(c, "b", &b, sizeof(b));
    // peek a — should NOT touch its position
    EXPECT_EQ(lru_cache_peek(c, "a"), &a);
    lru_cache_put(c, "c", &d, sizeof(d));  // evicts LRU which should still be "a"
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "a");
    lru_cache_free(c);
}

TEST(LruCacheTest, ExplicitDelete) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(0, 0, 0, capture_evict, &evicts);
    int a = 1;
    lru_cache_put(c, "a", &a, sizeof(a));
    EXPECT_TRUE(lru_cache_delete(c, "a"));
    EXPECT_FALSE(lru_cache_delete(c, "a"));
    EXPECT_EQ(lru_cache_count(c), 0u);
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "a");
    lru_cache_free(c);
}

TEST(LruCacheTest, ClearEvictsAll) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(0, 0, 0, capture_evict, &evicts);
    int v = 0;
    lru_cache_put(c, "a", &v, 1);
    lru_cache_put(c, "b", &v, 1);
    lru_cache_put(c, "c", &v, 1);
    lru_cache_clear(c);
    EXPECT_EQ(lru_cache_count(c), 0u);
    EXPECT_EQ(evicts.size(), 3u);
    lru_cache_free(c);
}

TEST(LruCacheTest, EvictOneReturnsBytes) {
    LruCache* c = make_cache();
    int v = 0;
    lru_cache_put(c, "a", &v, 7);
    lru_cache_put(c, "b", &v, 3);
    // a is LRU
    EXPECT_EQ(lru_cache_evict_one(c), 7u);
    EXPECT_EQ(lru_cache_count(c), 1u);
    EXPECT_EQ(lru_cache_evict_one(c), 3u);
    EXPECT_EQ(lru_cache_evict_one(c), 0u);  // empty
    lru_cache_free(c);
}

TEST(LruCacheTest, TouchMovesToFront) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(2, 0, 0, capture_evict, &evicts);
    int v = 0;
    lru_cache_put(c, "a", &v, 1);
    lru_cache_put(c, "b", &v, 1);
    EXPECT_TRUE(lru_cache_touch(c, "a"));
    lru_cache_put(c, "c", &v, 1);  // evicts b
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "b");
    EXPECT_FALSE(lru_cache_touch(c, "missing"));
    lru_cache_free(c);
}

TEST(LruCacheTest, TtlExpiresOnAccess) {
    std::vector<EvictRecord> evicts;
    LruCache* c = make_cache(0, 0, 0, capture_evict, &evicts);
    int v = 0;
    EXPECT_TRUE(lru_cache_put_ttl(c, "k", &v, sizeof(v), /*ttl_ms=*/50));
    EXPECT_EQ(lru_cache_get(c, "k"), &v);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(lru_cache_get(c, "k"), nullptr);
    // expired entries should be evicted lazily, with on_evict invoked
    EXPECT_EQ(lru_cache_count(c), 0u);
    ASSERT_EQ(evicts.size(), 1u);
    EXPECT_EQ(evicts[0].key, "k");
    lru_cache_free(c);
}

TEST(LruCacheTest, DefaultTtlApplies) {
    LruCache* c = make_cache(0, 0, /*default_ttl_ms=*/30);
    int v = 0;
    lru_cache_put(c, "k", &v, sizeof(v));
    EXPECT_NE(lru_cache_peek(c, "k"), nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(lru_cache_peek(c, "k"), nullptr);
    lru_cache_free(c);
}

static bool count_iter(const char* key, void* value, size_t bytes, void* udata) {
    (void)key; (void)value; (void)bytes;
    auto* counter = static_cast<int*>(udata);
    (*counter)++;
    return true;
}

TEST(LruCacheTest, IterCoversMruToLru) {
    LruCache* c = make_cache();
    int v = 0;
    lru_cache_put(c, "a", &v, 1);
    lru_cache_put(c, "b", &v, 1);
    lru_cache_put(c, "c", &v, 1);

    std::vector<std::string> seen;
    lru_cache_iter(c, [](const char* k, void*, size_t, void* u) -> bool {
        static_cast<std::vector<std::string>*>(u)->push_back(k);
        return true;
    }, &seen);
    // insertion order pushed each to front, so MRU→LRU is c, b, a
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], "c");
    EXPECT_EQ(seen[1], "b");
    EXPECT_EQ(seen[2], "a");
    lru_cache_free(c);
}

TEST(LruCacheTest, IterStopsWhenIteratorReturnsFalse) {
    LruCache* c = make_cache();
    int v = 0;
    lru_cache_put(c, "a", &v, 1);
    lru_cache_put(c, "b", &v, 1);

    int count = 0;
    bool done = lru_cache_iter(c, [](const char*, void*, size_t, void* u) -> bool {
        (*static_cast<int*>(u))++;
        return false;  // stop immediately
    }, &count);
    EXPECT_EQ(count, 1);
    EXPECT_FALSE(done);

    int total = 0;
    EXPECT_TRUE(lru_cache_iter(c, count_iter, &total));
    EXPECT_EQ(total, 2);
    lru_cache_free(c);
}

TEST(LruCacheTest, NullSafety) {
    EXPECT_EQ(lru_cache_count(nullptr), 0u);
    EXPECT_EQ(lru_cache_bytes(nullptr), 0u);
    EXPECT_EQ(lru_cache_get(nullptr, "x"), nullptr);
    EXPECT_FALSE(lru_cache_delete(nullptr, "x"));
    EXPECT_FALSE(lru_cache_touch(nullptr, "x"));
    EXPECT_EQ(lru_cache_evict_one(nullptr), 0u);
    lru_cache_free(nullptr);  // no-op
}
