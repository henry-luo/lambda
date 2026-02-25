/**
 * GC Heap Test Suite (GTest)
 * ==========================
 *
 * Tests for the dual-zone non-moving mark-and-sweep garbage collector:
 *
 * 1. Object allocation and tracking
 * 2. Data zone allocation
 * 3. Root registration
 * 4. Mark phase (mark items, trace objects)
 * 5. Sweep phase (free dead objects)
 * 6. Data compaction (nursery → tenured)
 * 7. Full collection cycle
 * 8. Collection triggers (threshold-based auto-GC)
 * 9. Conservative stack scanning
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

extern "C" {
#include "../lib/gc_heap.h"
#include "../lib/gc_object_zone.h"
#include "../lib/gc_data_zone.h"
#include "../lib/mempool.h"
}

// ============================================================================
// Type tag constants matching Lambda's EnumTypeId values (from lambda.h)
// ============================================================================
#define TEST_TYPE_RAW_POINTER 0
#define TEST_TYPE_NULL     1
#define TEST_TYPE_BOOL     2
#define TEST_TYPE_INT      3
#define TEST_TYPE_INT64    4
#define TEST_TYPE_FLOAT    5
#define TEST_TYPE_STRING  10
#define TEST_TYPE_LIST    12
#define TEST_TYPE_ARRAY   17
#define TEST_TYPE_MAP     18
#define TEST_TYPE_FUNC    23

// ============================================================================
// Test Fixture
// ============================================================================

class GCHeapTest : public ::testing::Test {
protected:
    gc_heap_t* gc = nullptr;

    void SetUp() override {
        gc = gc_heap_create();
        ASSERT_NE(gc, nullptr);
    }

    void TearDown() override {
        if (gc) {
            gc_heap_destroy(gc);
            gc = nullptr;
        }
    }

    // Helper: create a fake List-like object with items array
    void* make_list(int64_t length, int64_t capacity) {
        // List layout: { type_id(1), flags(1), pad(6), Item* items(8@8),
        //                length(8@16), extra(8@24), capacity(8@32) }
        void* obj = gc_heap_calloc(gc, 40, TEST_TYPE_LIST);
        EXPECT_NE(obj, nullptr);
        uint8_t* p = (uint8_t*)obj;
        *(uint8_t*)(p + 0) = TEST_TYPE_LIST;  // type_id

        // allocate items from data zone
        size_t items_size = capacity * sizeof(uint64_t);
        void* items = gc_data_alloc(gc, items_size);
        EXPECT_NE(items, nullptr);
        memset(items, 0, items_size);

        *(void**)(p + 8) = items;              // items pointer
        *(int64_t*)(p + 16) = length;          // length
        *(int64_t*)(p + 32) = capacity;        // capacity
        return obj;
    }

    // Helper: create a fake String object
    void* make_string(const char* text) {
        size_t len = strlen(text);
        // String: { len(4), chars[len+1] }
        size_t total = sizeof(int32_t) + len + 1;
        void* obj = gc_heap_alloc(gc, total, TEST_TYPE_STRING);
        EXPECT_NE(obj, nullptr);
        uint8_t* p = (uint8_t*)obj;
        *(int32_t*)p = (int32_t)len;
        memcpy(p + sizeof(int32_t), text, len + 1);
        return obj;
    }

    // Helper: tag a pointer as a List Item (container types use raw pointer as Item)
    // On 64-bit platforms, heap pointer high byte is 0, so this is just the pointer.
    uint64_t list_item(void* ptr) {
        return (uint64_t)(uintptr_t)ptr;
    }

    // Helper: tag a pointer as a String Item (tagged pointer with type byte in MSB)
    uint64_t string_item(void* ptr) {
        return ((uint64_t)TEST_TYPE_STRING << 56) | ((uint64_t)(uintptr_t)ptr & 0x00FFFFFFFFFFFFFF);
    }

    // Helper: tag a raw int as an INT Item (inline value in lower 56 bits)
    uint64_t int_item(int val) {
        return ((uint64_t)TEST_TYPE_INT << 56) | (uint64_t)(uint32_t)val;
    }
};

// ============================================================================
// 1. Object Allocation and Tracking
// ============================================================================

TEST_F(GCHeapTest, AllocBasic) {
    void* ptr = gc_heap_alloc(gc, 32, TEST_TYPE_STRING);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(gc->object_count, 1u);

    // verify header
    gc_header_t* header = gc_get_header(ptr);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->type_tag, TEST_TYPE_STRING);
    EXPECT_EQ(header->marked, 0);
    EXPECT_EQ(header->gc_flags, 0);
    EXPECT_EQ(header->alloc_size, 32u);
}

TEST_F(GCHeapTest, CallocZeroed) {
    void* ptr = gc_heap_calloc(gc, 64, TEST_TYPE_ARRAY);
    ASSERT_NE(ptr, nullptr);

    // verify memory is zeroed
    uint8_t* bytes = (uint8_t*)ptr;
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(bytes[i], 0) << "byte " << i << " not zero";
    }
}

TEST_F(GCHeapTest, AllocMultiple) {
    void* a = gc_heap_alloc(gc, 16, TEST_TYPE_INT64);
    void* b = gc_heap_alloc(gc, 48, TEST_TYPE_STRING);
    void* c = gc_heap_alloc(gc, 128, TEST_TYPE_LIST);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(gc->object_count, 3u);

    // verify all_objects linked list
    gc_header_t* h = gc->all_objects;
    int count = 0;
    while (h) { count++; h = h->next; }
    EXPECT_EQ(count, 3);
}

TEST_F(GCHeapTest, OwnershipQuery) {
    void* ptr = gc_heap_alloc(gc, 32, TEST_TYPE_STRING);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(gc_is_managed(gc, ptr));

    // random pointer should NOT be managed
    int dummy;
    EXPECT_FALSE(gc_is_managed(gc, &dummy));
}

// ============================================================================
// 2. Data Zone Allocation
// ============================================================================

TEST_F(GCHeapTest, DataZoneAlloc) {
    void* data = gc_data_alloc(gc, 256);
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(gc_is_nursery_data(gc, data));
    EXPECT_TRUE(gc_is_managed(gc, data));
}

TEST_F(GCHeapTest, DataZoneMultipleAllocs) {
    void* a = gc_data_alloc(gc, 64);
    void* b = gc_data_alloc(gc, 128);
    void* c = gc_data_alloc(gc, 256);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    // all should be nursery data
    EXPECT_TRUE(gc_is_nursery_data(gc, a));
    EXPECT_TRUE(gc_is_nursery_data(gc, b));
    EXPECT_TRUE(gc_is_nursery_data(gc, c));

    // pointers should be different
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
}

// ============================================================================
// 3. Root Registration
// ============================================================================

TEST_F(GCHeapTest, RegisterRoot) {
    uint64_t slot1 = 0;
    uint64_t slot2 = 0;

    gc_register_root(gc, &slot1);
    EXPECT_EQ(gc->root_slot_count, 1);

    gc_register_root(gc, &slot2);
    EXPECT_EQ(gc->root_slot_count, 2);

    // duplicate registration should be idempotent
    gc_register_root(gc, &slot1);
    EXPECT_EQ(gc->root_slot_count, 2);
}

TEST_F(GCHeapTest, UnregisterRoot) {
    uint64_t slot1 = 0, slot2 = 0, slot3 = 0;
    gc_register_root(gc, &slot1);
    gc_register_root(gc, &slot2);
    gc_register_root(gc, &slot3);
    EXPECT_EQ(gc->root_slot_count, 3);

    gc_unregister_root(gc, &slot2);
    EXPECT_EQ(gc->root_slot_count, 2);

    // verify remaining slots are slot1 and slot3
    EXPECT_EQ(gc->root_slots[0], &slot1);
    EXPECT_EQ(gc->root_slots[1], &slot3);
}

// ============================================================================
// 4. Mark Phase
// ============================================================================

TEST_F(GCHeapTest, MarkSingleObject) {
    void* str = make_string("hello");
    gc_header_t* header = gc_get_header(str);

    // object should start unmarked
    EXPECT_EQ(header->marked, 0);

    // mark it via tagged Item
    gc->mark_top = 0;
    gc_mark_item(gc, string_item(str));

    // should be marked and on the mark stack
    EXPECT_EQ(header->marked, 1);
    EXPECT_GE(gc->mark_top, 1);
}

TEST_F(GCHeapTest, MarkInlineValueIgnored) {
    // inline int values should not cause any marking
    gc->mark_top = 0;
    gc_mark_item(gc, int_item(42));
    EXPECT_EQ(gc->mark_top, 0);

    // null item
    gc_mark_item(gc, 0);
    EXPECT_EQ(gc->mark_top, 0);
}

TEST_F(GCHeapTest, MarkListTracesChildren) {
    // create a list with two string children
    void* str_a = make_string("alpha");
    void* str_b = make_string("beta");
    void* list = make_list(2, 4);

    // set items
    uint8_t* p = (uint8_t*)list;
    uint64_t* items = (uint64_t*)(*(void**)(p + 8));
    items[0] = string_item(str_a);
    items[1] = string_item(str_b);

    // use gc_collect to test full mark/trace cycle
    // root the list — children should be kept alive through tracing
    uint64_t root = list_item(list);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // list + 2 strings should survive (dead_str not in this test = all 3 survive)
    // We had 3 objects total (str_a, str_b, list), all reachable
    EXPECT_EQ(gc->object_count, 3u);

    // verify all headers are unmarked (reset after collection)
    gc_header_t* list_h = gc_get_header(list);
    EXPECT_EQ(list_h->marked, 0);  // marks are reset after sweep

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 5. Full Collection — Live Objects Survive, Dead Objects Freed
// ============================================================================

TEST_F(GCHeapTest, CollectFreesUnreachableObjects) {
    // create some objects
    void* alive_str = make_string("keep me");
    void* dead_str1 = make_string("garbage 1");
    void* dead_str2 = make_string("garbage 2");

    size_t initial_count = gc->object_count;
    EXPECT_EQ(initial_count, 3u);

    // register alive_str as a root
    uint64_t root = string_item(alive_str);
    gc_register_root(gc, &root);

    // run collection (no stack scan — pass 0 for stack bounds)
    gc_collect(gc, NULL, 0, 0, 0);

    // alive_str should survive, dead objects should be freed
    EXPECT_EQ(gc->object_count, 1u);
    EXPECT_EQ(gc->collections, 1u);
    EXPECT_GT(gc->bytes_collected, 0u);

    // verify the surviving object
    gc_header_t* h = gc->all_objects;
    ASSERT_NE(h, nullptr);
    EXPECT_EQ((void*)(h + 1), alive_str);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, CollectPreservesAllRooted) {
    void* s1 = make_string("one");
    void* s2 = make_string("two");
    void* s3 = make_string("three");

    uint64_t roots[3] = { string_item(s1), string_item(s2), string_item(s3) };

    gc_collect(gc, roots, 3, 0, 0);

    // all three should survive
    EXPECT_EQ(gc->object_count, 3u);
}

TEST_F(GCHeapTest, CollectEmptyHeap) {
    // should not crash on empty heap
    gc_collect(gc, NULL, 0, 0, 0);
    EXPECT_EQ(gc->collections, 1u);
    EXPECT_EQ(gc->object_count, 0u);
}

// ============================================================================
// 6. Data Compaction
// ============================================================================

TEST_F(GCHeapTest, CompactMovesNurseryData) {
    // create a list with data in nursery
    void* list = make_list(2, 4);
    uint8_t* p = (uint8_t*)list;
    void* old_items = *(void**)(p + 8);

    // items should be in nursery
    EXPECT_TRUE(gc_is_nursery_data(gc, old_items));

    // root the list and collect (compaction should move items to tenured)
    uint64_t root = list_item(list);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // items should now be in tenured, NOT nursery
    void* new_items = *(void**)(p + 8);
    EXPECT_FALSE(gc_is_nursery_data(gc, new_items));
    // items pointer should have changed (moved from nursery to tenured)
    EXPECT_NE(old_items, new_items);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, CompactPreservesListData) {
    // create a list with known Item values
    void* list = make_list(3, 4);
    uint8_t* p = (uint8_t*)list;
    uint64_t* items = (uint64_t*)(*(void**)(p + 8));
    items[0] = int_item(10);
    items[1] = int_item(20);
    items[2] = int_item(30);

    uint64_t root = list_item(list);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // verify items data was preserved after compaction
    uint64_t* new_items = (uint64_t*)(*(void**)(p + 8));
    EXPECT_EQ(new_items[0], int_item(10));
    EXPECT_EQ(new_items[1], int_item(20));
    EXPECT_EQ(new_items[2], int_item(30));

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 7. Reachability Through Object Graph
// ============================================================================

TEST_F(GCHeapTest, CollectTracesListChildren) {
    // create a list that references strings
    void* str_a = make_string("alpha");
    void* str_b = make_string("beta");
    void* dead_str = make_string("dead");
    void* list = make_list(2, 4);

    uint8_t* p = (uint8_t*)list;
    uint64_t* items = (uint64_t*)(*(void**)(p + 8));
    items[0] = string_item(str_a);
    items[1] = string_item(str_b);

    // root only the list — strings should be kept alive through tracing
    uint64_t root = list_item(list);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // list + 2 strings survive, dead_str is collected
    EXPECT_EQ(gc->object_count, 3u);

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 8. Multiple Collection Cycles
// ============================================================================

TEST_F(GCHeapTest, MultipleCollections) {
    uint64_t root = 0;
    gc_register_root(gc, &root);

    // cycle 1: allocate and release
    void* str1 = make_string("cycle1");
    root = string_item(str1);
    gc_collect(gc, NULL, 0, 0, 0);
    EXPECT_EQ(gc->object_count, 1u);
    EXPECT_EQ(gc->collections, 1u);

    // cycle 2: allocate more, release old
    void* str2 = make_string("cycle2");
    root = string_item(str2);
    gc_collect(gc, NULL, 0, 0, 0);
    EXPECT_EQ(gc->object_count, 1u);
    EXPECT_EQ(gc->collections, 2u);

    // cycle 3: no allocations
    gc_collect(gc, NULL, 0, 0, 0);
    EXPECT_EQ(gc->object_count, 1u);
    EXPECT_EQ(gc->collections, 3u);

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 9. Collection Trigger (Threshold-Based)
// ============================================================================

static int trigger_count = 0;
static void test_collect_callback(void) {
    trigger_count++;
}

TEST_F(GCHeapTest, ShouldCollectThreshold) {
    // initially, data zone is empty — should NOT trigger
    EXPECT_FALSE(gc_should_collect(gc));

    // set a very low threshold for testing
    gc->gc_threshold = 128;

    // allocate enough data to exceed threshold
    gc_data_alloc(gc, 64);
    gc_data_alloc(gc, 64);
    EXPECT_TRUE(gc_should_collect(gc));
}

TEST_F(GCHeapTest, AutoTriggerCallback) {
    trigger_count = 0;

    // set a low threshold and a callback
    gc->gc_threshold = 64;
    gc_set_collect_callback(gc, test_collect_callback);

    // first alloc under threshold — no trigger
    gc_data_alloc(gc, 32);
    EXPECT_EQ(trigger_count, 0);

    // fill to just past threshold
    gc_data_alloc(gc, 48);
    // now at 80 bytes used, exceeding 64 threshold — next alloc triggers
    EXPECT_EQ(trigger_count, 0);  // trigger fires BEFORE alloc, so check on next alloc

    // this alloc causes the threshold check → triggers callback
    gc_data_alloc(gc, 16);
    EXPECT_GE(trigger_count, 1);
}

TEST_F(GCHeapTest, ReentrancyGuard) {
    // during collection, gc_data_alloc should NOT re-trigger
    gc->gc_threshold = 64;
    gc->collecting = 1;  // simulate active collection
    gc_set_collect_callback(gc, test_collect_callback);

    trigger_count = 0;
    gc_data_alloc(gc, 128);  // over threshold, but collecting flag blocks
    EXPECT_EQ(trigger_count, 0);

    gc->collecting = 0;
}

// ============================================================================
// 10. Explicit Free (gc_heap_pool_free)
// ============================================================================

TEST_F(GCHeapTest, ExplicitFreeReducesCount) {
    void* ptr = gc_heap_alloc(gc, 32, TEST_TYPE_STRING);
    EXPECT_EQ(gc->object_count, 1u);

    gc_heap_pool_free(gc, ptr);
    EXPECT_EQ(gc->object_count, 0u);

    // double-free should be safe
    gc_heap_pool_free(gc, ptr);
    EXPECT_EQ(gc->object_count, 0u);
}

TEST_F(GCHeapTest, FreedObjectsCleanedInSweep) {
    void* a = gc_heap_alloc(gc, 32, TEST_TYPE_STRING);
    void* b = gc_heap_alloc(gc, 32, TEST_TYPE_STRING);
    gc_heap_pool_free(gc, a);

    // root b so it survives
    uint64_t root = string_item(b);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // only b should remain
    EXPECT_EQ(gc->object_count, 1u);

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 11. Collection Statistics
// ============================================================================

TEST_F(GCHeapTest, CollectionStats) {
    make_string("garbage 1");
    make_string("garbage 2");
    make_string("garbage 3");

    EXPECT_EQ(gc->collections, 0u);
    EXPECT_EQ(gc->bytes_collected, 0u);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->collections, 1u);
    // all 3 objects should have been collected
    EXPECT_GT(gc->bytes_collected, 0u);
    EXPECT_EQ(gc->object_count, 0u);
}

// ============================================================================
// 12. Conservative Stack Scanning
// ============================================================================

TEST_F(GCHeapTest, StackScanKeepsStackReferencedObjects) {
    // This test verifies that conservative stack scanning finds tagged pointers
    // in a memory region and marks the referenced objects as live.
    // We use a controlled buffer rather than the real stack to avoid ASAN issues.
    void* str = make_string("on stack");
    uint64_t tagged = string_item(str);

    // plant the tagged value in a fake "stack" buffer at a known offset
    uint64_t fake_stack[16];
    memset(fake_stack, 0, sizeof(fake_stack));
    fake_stack[4] = tagged;  // GC scanner should find this

    uintptr_t stack_lo = (uintptr_t)&fake_stack[0];
    uintptr_t stack_hi = (uintptr_t)&fake_stack[16];

    // collect with this region as the stack scan range
    gc_collect(gc, NULL, 0, stack_hi, stack_lo);

    // the string should survive because its tagged pointer was in the scan range
    EXPECT_EQ(gc->object_count, 1u);
}

// ============================================================================
// 13. Stress Test — Many Objects
// ============================================================================

TEST_F(GCHeapTest, StressAllocCollect) {
    uint64_t root = 0;
    gc_register_root(gc, &root);

    // allocate many objects, keeping only the last one alive
    for (int i = 0; i < 1000; i++) {
        void* str = make_string("stress test string with some data");
        root = string_item(str);
    }

    EXPECT_EQ(gc->object_count, 1000u);

    gc_collect(gc, NULL, 0, 0, 0);

    // only the last allocated string (referenced by root) should survive
    EXPECT_EQ(gc->object_count, 1u);

    gc_unregister_root(gc, &root);
}
