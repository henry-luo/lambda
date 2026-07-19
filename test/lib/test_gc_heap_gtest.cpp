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
#include "../../lambda/lambda.h"
#include "../../lib/gc/gc_heap.h"
#include "../../lib/gc/gc_object_zone.h"
#include "../../lib/gc/gc_data_zone.h"
#include "../../lib/mempool.h"
#include "../../lib/side_stack.h"
}

// Use LMD_TYPE_* enum values from lambda.h directly — no local aliases needed.

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
        void* obj = gc_heap_calloc(gc, 40, LMD_TYPE_ARRAY);
        EXPECT_NE(obj, nullptr);
        uint8_t* p = (uint8_t*)obj;
        *(uint8_t*)(p + 0) = LMD_TYPE_ARRAY;  // type_id

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
        void* obj = gc_heap_alloc(gc, total, LMD_TYPE_STRING);
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
        return ((uint64_t)LMD_TYPE_STRING << 56) | ((uint64_t)(uintptr_t)ptr & 0x00FFFFFFFFFFFFFF);
    }

    // Helper: tag a raw int as an INT Item (inline value in lower 56 bits)
    uint64_t int_item(int val) {
        return ((uint64_t)LMD_TYPE_INT << 56) | (uint64_t)(uint32_t)val;
    }
};

static int external_destroy_calls = 0;
static uint16_t external_destroy_last_tag = 0;
static int weak_clear_calls = 0;
static void* weak_clear_context = nullptr;

static void test_external_destroy(void* data, uint16_t type_tag) {
    external_destroy_calls++;
    external_destroy_last_tag = type_tag;
    *(void**)data = NULL;
}

static void test_weak_clear(uint64_t* slot, void* context) {
    weak_clear_calls++;
    weak_clear_context = context;
    EXPECT_EQ(*slot, 0u);
}

TEST(SideStackRootFrameTest, NestedFramesRestoreExactWatermarks) {
    Context runtime{};
    ASSERT_TRUE(lambda_side_stack_bind(&runtime));
    uint64_t* initial_top = runtime.side_root_top;

    LambdaRootFrame outer{};
    ASSERT_TRUE(lambda_root_frame_begin(&runtime, &outer, 3));
    ASSERT_EQ(runtime.side_root_top, initial_top + 3);
    for (size_t i = 0; i < 3; i++) {
        ASSERT_NE(lambda_root_frame_slot(&outer, i), nullptr);
        EXPECT_EQ(*lambda_root_frame_slot(&outer, i), 0u);
    }
    *lambda_root_frame_take_slot(&outer) = UINT64_C(0x1234);

    LambdaRootFrame inner{};
    ASSERT_TRUE(lambda_root_frame_begin(&runtime, &inner, 2));
    EXPECT_EQ(inner.watermark, initial_top + 3);
    EXPECT_EQ(runtime.side_root_top, initial_top + 5);
    lambda_root_frame_end(&inner);
    EXPECT_EQ(runtime.side_root_top, initial_top + 3);
    EXPECT_EQ(*lambda_root_frame_slot(&outer, 0), UINT64_C(0x1234));

    lambda_root_frame_end(&outer);
    EXPECT_EQ(runtime.side_root_top, initial_top);
    lambda_side_stack_reset(&runtime);
}

TEST(SideStackRootFrameTest, RecoveryCheckpointRestoresBothStacks) {
    Context runtime{};
    ASSERT_TRUE(lambda_side_stack_bind(&runtime));
    LambdaRecoveryCheckpoint checkpoint =
        lambda_recovery_checkpoint_capture(&runtime);
    ASSERT_TRUE(checkpoint.active);

    LambdaRootFrame roots{};
    ASSERT_TRUE(lambda_root_frame_begin(&runtime, &roots, 2));
    ASSERT_NE(lambda_side_number_alloc(&runtime), nullptr);
    EXPECT_NE(runtime.side_root_top, checkpoint.side_stack.root_top);
    EXPECT_NE(runtime.side_number_top, checkpoint.side_stack.number_top);

    lambda_recovery_checkpoint_restore(&checkpoint);
    EXPECT_EQ(runtime.side_root_top, checkpoint.side_stack.root_top);
    EXPECT_EQ(runtime.side_number_top, checkpoint.side_stack.number_top);
    EXPECT_FALSE(checkpoint.active);
    lambda_side_stack_reset(&runtime);
}

TEST(SideStackRootFrameTest, OversizedFrameFailsWithoutAdvancingWatermark) {
    Context runtime{};
    ASSERT_TRUE(lambda_side_stack_bind(&runtime));
    uint64_t* initial_top = runtime.side_root_top;
    LambdaRootFrame roots{};
    size_t reserve_slots =
        LAMBDA_SIDE_ROOT_RESERVE_BYTES / sizeof(uint64_t);

    EXPECT_FALSE(lambda_root_frame_begin(&runtime, &roots,
        reserve_slots + 1));
    EXPECT_FALSE(roots.active);
    EXPECT_EQ(runtime.side_root_top, initial_top);
    lambda_side_stack_reset(&runtime);
}

// ============================================================================
// 1. Object Allocation and Tracking
// ============================================================================

TEST_F(GCHeapTest, AllocBasic) {
    void* ptr = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(gc->object_count, 1u);

    // verify header
    gc_header_t* header = gc_get_header(ptr);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->type_tag, LMD_TYPE_STRING);
    EXPECT_EQ(header->marked, 0);
    EXPECT_EQ(header->gc_flags, 0);
    EXPECT_EQ(header->alloc_size, 32u);
}

TEST_F(GCHeapTest, CallocZeroed) {
    void* ptr = gc_heap_calloc(gc, 64, LMD_TYPE_ARRAY);
    ASSERT_NE(ptr, nullptr);

    // verify memory is zeroed
    uint8_t* bytes = (uint8_t*)ptr;
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(bytes[i], 0) << "byte " << i << " not zero";
    }
}

TEST_F(GCHeapTest, AllocMultiple) {
    void* a = gc_heap_alloc(gc, 16, LMD_TYPE_INT64);
    void* b = gc_heap_alloc(gc, 48, LMD_TYPE_STRING);
    void* c = gc_heap_alloc(gc, 128, LMD_TYPE_ARRAY);
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
    void* ptr = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(gc_is_managed(gc, ptr));

    // random pointer should NOT be managed
    int dummy;
    EXPECT_FALSE(gc_is_managed(gc, &dummy));
}

TEST_F(GCHeapTest, ObjectInteriorPointerNotManaged) {
    void* ptr = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    ASSERT_NE(ptr, nullptr);

    EXPECT_TRUE(gc_is_managed(gc, ptr));
    EXPECT_FALSE(gc_is_managed(gc, (uint8_t*)ptr + 8));
}

TEST_F(GCHeapTest, ExternalPayloadFinalizerRunsDuringSweep) {
    external_destroy_calls = 0;
    external_destroy_last_tag = 0;
    gc->external_destroy = test_external_destroy;
    void* obj = gc_heap_calloc(gc, sizeof(void*), LMD_TYPE_BINARY);
    ASSERT_NE(obj, nullptr);
    *(void**)obj = obj;

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(external_destroy_calls, 1);
    EXPECT_EQ(external_destroy_last_tag, LMD_TYPE_BINARY);
}

TEST_F(GCHeapTest, ExternalPayloadFinalizerRunsAtHeapTeardown) {
    external_destroy_calls = 0;
    external_destroy_last_tag = 0;
    gc->external_destroy = test_external_destroy;
    void* obj = gc_heap_calloc(gc, sizeof(void*), LMD_TYPE_BINARY);
    ASSERT_NE(obj, nullptr);
    *(void**)obj = obj;

    gc_heap_destroy(gc);
    gc = nullptr;

    EXPECT_EQ(external_destroy_calls, 1);
    EXPECT_EQ(external_destroy_last_tag, LMD_TYPE_BINARY);
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

TEST_F(GCHeapTest, DataZoneReuseAfterPartialResetIsZeroed) {
    // reset must scrub the previously used prefix even when the most recent
    // allocation only touched a smaller prefix of the same block.
    uint8_t* large = (uint8_t*)gc_data_alloc(gc, 1024);
    ASSERT_NE(large, nullptr);
    memset(large, 0xA5, 1024);

    gc_data_zone_reset(gc->data_zone);

    uint8_t* small = (uint8_t*)gc_data_alloc(gc, 128);
    ASSERT_NE(small, nullptr);
    memset(small, 0x5A, 128);

    gc_data_zone_reset(gc->data_zone);

    uint8_t* reused = (uint8_t*)gc_data_alloc(gc, 1024);
    ASSERT_NE(reused, nullptr);
    for (int i = 0; i < 1024; i++) {
        EXPECT_EQ(reused[i], 0) << "byte " << i << " was not zeroed after partial reset";
    }
}

// ============================================================================
// 3. Root Registration
// ============================================================================

TEST_F(GCHeapTest, WeakSlotClearsDeadReferentBeforeSweep) {
    weak_clear_calls = 0;
    weak_clear_context = nullptr;
    uint64_t weak = string_item(make_string("weak"));
    int marker = 42;
    gc_register_weak(gc, &weak, test_weak_clear, &marker);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(weak, 0u);
    EXPECT_EQ(weak_clear_calls, 1);
    EXPECT_EQ(weak_clear_context, &marker);
    EXPECT_EQ(gc->weak_slot_count, 0);
}

TEST_F(GCHeapTest, WeakSlotPreservesStronglyReachableReferent) {
    weak_clear_calls = 0;
    uint64_t weak = string_item(make_string("reachable"));
    uint64_t root = weak;
    gc_register_root(gc, &root);
    gc_register_weak(gc, &weak, test_weak_clear, nullptr);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_NE(weak, 0u);
    EXPECT_EQ(weak_clear_calls, 0);
    gc_unregister_weak(gc, &weak);
    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, UnregisteredWeakSlotIsNotTouched) {
    weak_clear_calls = 0;
    uint64_t weak = string_item(make_string("unregistered"));
    gc_register_weak(gc, &weak, test_weak_clear, nullptr);
    gc_unregister_weak(gc, &weak);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_NE(weak, 0u);
    EXPECT_EQ(weak_clear_calls, 0);
}

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

TEST_F(GCHeapTest, TryRegisterRootReportsCanonicalRegistration) {
    uint64_t slot = 0;
    EXPECT_EQ(gc_try_register_root(gc, &slot), 1);
    EXPECT_EQ(gc_try_register_root(gc, &slot), 1);
    EXPECT_EQ(gc->root_slot_count, 1);
    EXPECT_EQ(gc_try_register_root(nullptr, &slot), 0);
}

#ifndef NDEBUG
TEST_F(GCHeapTest, NoGcScopeRejectsPublicAllocation) {
    EXPECT_DEATH({
        gc_no_gc_scope_begin(gc);
        (void)gc_heap_alloc(gc, 16, LMD_TYPE_STRING);
    }, ".*");
}

TEST_F(GCHeapTest, NoGcScopeSupportsBalancedNesting) {
    gc_no_gc_scope_begin(gc);
    gc_no_gc_scope_begin(gc);
    gc_no_gc_scope_end(gc);
    gc_no_gc_scope_end(gc);
    EXPECT_EQ(gc->no_gc_scope_depth, 0);
}
#endif

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

TEST_F(GCHeapTest, CompactPromotesArrayNumBaseAndRebindsLiveView) {
    // Build the raw ArrayNum ABI used by the C collector: the view is newer and
    // therefore visited before its base in the all_objects list.
    uint8_t* base = (uint8_t*)gc_heap_calloc(gc, 40, LMD_TYPE_ARRAY_NUM);
    ASSERT_NE(base, nullptr);
    base[0] = LMD_TYPE_ARRAY_NUM;
    base[3] = ELEM_INT64;
    int64_t* base_data = (int64_t*)gc_data_alloc(gc, 4 * sizeof(int64_t));
    ASSERT_NE(base_data, nullptr);
    base_data[0] = 10;
    base_data[1] = 20;
    base_data[2] = 30;
    base_data[3] = 40;
    *(void**)(base + 8) = base_data;
    *(int64_t*)(base + 16) = 4;
    *(int64_t*)(base + 32) = 4;

    uint8_t* view = (uint8_t*)gc_heap_calloc(gc, 40, LMD_TYPE_ARRAY_NUM);
    ASSERT_NE(view, nullptr);
    view[0] = LMD_TYPE_ARRAY_NUM;
    view[2] = 0x03;  // is_ndim | is_view
    view[3] = ELEM_INT64;
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * sizeof(int64_t);
    ArrayNumShape* shape = (ArrayNumShape*)gc_data_alloc(gc, shape_bytes);
    ASSERT_NE(shape, nullptr);
    memset(shape, 0, shape_bytes);
    shape->ndim = 1;
    shape->is_c_contig = 1;
    shape->is_f_contig = 1;
    shape->backing_kind = ARRAY_NUM_BACKING_GC_VIEW;
    shape->offset = 1;
    shape->base = base;
    array_num_shape_dims(shape)[0] = 2;
    array_num_shape_strides(shape)[0] = 1;
    *(void**)(view + 8) = base_data + 1;
    *(int64_t*)(view + 16) = 2;
    *(void**)(view + 24) = shape;
    *(int64_t*)(view + 32) = 2;

    uint64_t root = list_item(view);
    gc_register_root(gc, &root);
    gc_collect(gc, NULL, 0, 0, 0);

    int64_t* promoted_base = *(int64_t**)(base + 8);
    int64_t* rebound_view = *(int64_t**)(view + 8);
    EXPECT_FALSE(gc_is_nursery_data(gc, promoted_base));
    EXPECT_NE(promoted_base, base_data);
    EXPECT_EQ(rebound_view, promoted_base + 1);
    EXPECT_EQ(rebound_view[0], 20);
    EXPECT_EQ(rebound_view[1], 30);

    // Nursery reuse must not overwrite the promoted owner or its rebound alias.
    int64_t* reused = (int64_t*)gc_data_alloc(gc, 4 * sizeof(int64_t));
    ASSERT_NE(reused, nullptr);
    for (int i = 0; i < 4; i++) reused[i] = 99;
    EXPECT_EQ(rebound_view[0], 20);
    EXPECT_EQ(rebound_view[1], 30);

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

TEST_F(GCHeapTest, ForcedScheduleCoversEveryPublicAllocationPath) {
    trigger_count = 0;
    gc_set_collect_callback(gc, test_collect_callback);
    gc_set_force_collect_interval(gc, 1);

    int cls = gc_object_zone_class_index(32);
    ASSERT_GE(cls, 0);
    size_t slot_size = sizeof(gc_header_t) + gc_object_zone_class_size(cls);

    ASSERT_NE(gc_heap_alloc(gc, 32, LMD_TYPE_STRING), nullptr);
    ASSERT_NE(gc_heap_calloc(gc, 32, LMD_TYPE_STRING), nullptr);
    ASSERT_NE(gc_heap_calloc_class(gc, 32, LMD_TYPE_STRING, cls), nullptr);
    ASSERT_NE(gc_heap_bump_alloc(gc, slot_size, 32, LMD_TYPE_STRING, cls), nullptr);
    ASSERT_NE(gc_data_alloc(gc, 32), nullptr);

    EXPECT_EQ(trigger_count, 5);
    EXPECT_EQ(gc_get_forced_collection_count(gc), 5u);

    // A collection may allocate internal data while the guard is active; that
    // work must not recursively trigger another forced collection.
    gc->collecting = 1;
    ASSERT_NE(gc_data_alloc(gc, 32), nullptr);
    gc->collecting = 0;
    EXPECT_EQ(trigger_count, 5);
    EXPECT_EQ(gc_get_forced_collection_count(gc), 5u);
}

TEST_F(GCHeapTest, RandomForcedScheduleIsSeedReproducible) {
    trigger_count = 0;
    gc_set_collect_callback(gc, test_collect_callback);
    gc_set_force_collect_random(gc, 0x12345678u, 4);

    for (int i = 0; i < 32; i++) {
        ASSERT_NE(gc_heap_alloc(gc, 16, LMD_TYPE_STRING), nullptr);
    }
    int first_count = trigger_count;
    ASSERT_GT(first_count, 0);
    ASSERT_LT(first_count, 32);

    trigger_count = 0;
    gc_set_force_collect_random(gc, 0x12345678u, 4);
    for (int i = 0; i < 32; i++) {
        ASSERT_NE(gc_heap_alloc(gc, 16, LMD_TYPE_STRING), nullptr);
    }
    EXPECT_EQ(trigger_count, first_count);
    EXPECT_EQ(gc_get_forced_collection_count(gc), (size_t)first_count);
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
    void* ptr = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    EXPECT_EQ(gc->object_count, 1u);

    gc_heap_pool_free(gc, ptr);
    EXPECT_EQ(gc->object_count, 0u);

    // double-free should be safe
    gc_heap_pool_free(gc, ptr);
    EXPECT_EQ(gc->object_count, 0u);
}

TEST_F(GCHeapTest, FreedObjectsCleanedInSweep) {
    void* a = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    void* b = gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    gc_heap_pool_free(gc, a);

    // root b so it survives
    uint64_t root = string_item(b);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // only b should remain
    EXPECT_EQ(gc->object_count, 1u);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, PoisonFreedOverwritesDeadPayloadAndRecycledSlotIsZeroed) {
    gc_set_poison_freed(gc, 1);
    EXPECT_EQ(gc_get_poison_freed(gc), 1);

    uint8_t* dead = (uint8_t*)gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    ASSERT_NE(dead, nullptr);
    memset(dead, 0x5A, 32);

    gc_collect(gc, NULL, 0, 0, 0);

    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(dead[i], GC_FREED_POISON_BYTE);
    }

    uint8_t* recycled = (uint8_t*)gc_heap_alloc(gc, 32, LMD_TYPE_STRING);
    ASSERT_EQ(recycled, dead);
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(recycled[i], 0);
    }
}

TEST_F(GCHeapTest, PoisonFreedCoversExplicitFreeBeforeSweep) {
    gc_set_poison_freed(gc, 1);
    uint8_t* dead = (uint8_t*)gc_heap_alloc(gc, 24, LMD_TYPE_STRING);
    ASSERT_NE(dead, nullptr);
    memset(dead, 0x5A, 24);

    gc_heap_pool_free(gc, dead);

    for (int i = 0; i < 24; i++) {
        EXPECT_EQ(dead[i], GC_FREED_POISON_BYTE);
    }
    gc_collect(gc, NULL, 0, 0, 0);
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

TEST_F(GCHeapTest, ShadowScanReportsOnlyObjectsOutsidePreciseGraph) {
    void* parent = make_list(1, 1);
    void* precise_child = make_string("precise child");
    void* stack_only = make_string("stack only");
    uint64_t* items = *(uint64_t**)((uint8_t*)parent + 8);
    items[0] = string_item(precise_child);

    uint64_t root = list_item(parent);
    gc_register_root(gc, &root);
    gc_set_root_mode(gc, GC_ROOT_MODE_SHADOW_VERIFY);

    uint64_t fake_stack[16];
    memset(fake_stack, 0, sizeof(fake_stack));
    fake_stack[3] = string_item(precise_child);
    fake_stack[9] = string_item(stack_only);

    gc_collect(gc, NULL, 0,
        (uintptr_t)&fake_stack[16], (uintptr_t)&fake_stack[0]);

    // The precise child is traced before the shadow scan, so only the orphan
    // is a scan-exclusive object even though both words look pointer-like.
    const gc_root_stats_t* stats = gc_get_last_root_stats(gc);
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->precise_root_count, 1u);
    EXPECT_EQ(stats->conservative_candidate_words, 2u);
    EXPECT_EQ(stats->conservative_new_objects, 1u);
    EXPECT_EQ(stats->conservative_scan_bytes, sizeof(fake_stack));
    EXPECT_EQ(gc_get_last_conservative_type_count(gc, LMD_TYPE_STRING), 1u);
    EXPECT_EQ(gc->object_count, 3u);
}

TEST_F(GCHeapTest, PreciseOnlyNeverFallsBackToStackScanning) {
    void* stack_only = make_string("not precisely rooted");
    uint64_t fake_stack[8];
    memset(fake_stack, 0, sizeof(fake_stack));
    fake_stack[2] = string_item(stack_only);
    gc_set_root_mode(gc, GC_ROOT_MODE_PRECISE_ONLY);

    gc_collect(gc, NULL, 0,
        (uintptr_t)&fake_stack[8], (uintptr_t)&fake_stack[0]);

    const gc_root_stats_t* stats = gc_get_last_root_stats(gc);
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->conservative_candidate_words, 0u);
    EXPECT_EQ(stats->conservative_new_objects, 0u);
    EXPECT_EQ(stats->conservative_scan_bytes, 0u);
    EXPECT_EQ(gc->object_count, 0u);
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

// ============================================================================
// 14. Closure Environment Tracing
// ============================================================================

// Helper: create a fake Function object with a closure env containing cap_count Items
// Function layout: { type_id(1@0), arity(1@1), closure_field_count(1@2), pad(5),
//                    fn_type*(8@8), ptr*(8@16), closure_env*(8@24), name*(8@32) }
// Total: 40 bytes
static void* make_closure(GCHeapTest* t, gc_heap_t* gc, int cap_count, uint64_t* env_items_out[]) {
    void* fn = gc_heap_calloc(gc, 40, LMD_TYPE_FUNC);
    EXPECT_NE(fn, nullptr);
    uint8_t* p = (uint8_t*)fn;
    *(uint8_t*)(p + 0) = LMD_TYPE_FUNC;        // type_id
    *(uint8_t*)(p + 1) = 0;                     // arity
    *(uint8_t*)(p + 2) = (uint8_t)cap_count;    // closure_field_count

    if (cap_count > 0) {
        void* env = gc_data_alloc(gc, cap_count * sizeof(uint64_t));
        EXPECT_NE(env, nullptr);
        memset(env, 0, cap_count * sizeof(uint64_t));
        *(void**)(p + 24) = env;  // closure_env
        if (env_items_out) *env_items_out = (uint64_t*)env;
    }
    return fn;
}

TEST_F(GCHeapTest, ClosureEnvTracesChildren) {
    // Create a closure that captures two strings
    void* str_a = make_string("captured_a");
    void* str_b = make_string("captured_b");
    void* dead_str = make_string("not captured");

    uint64_t* env_items = nullptr;
    void* fn = make_closure(this, gc, 2, &env_items);
    ASSERT_NE(env_items, nullptr);

    // Store tagged string Items in the closure env
    env_items[0] = string_item(str_a);
    env_items[1] = string_item(str_b);

    // Root only the function — strings should survive via env tracing
    uint64_t root = list_item(fn);  // Function is a container (raw pointer)
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // fn + 2 captured strings survive, dead_str is collected
    EXPECT_EQ(gc->object_count, 3u);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, ClosureEnvCompactsToTenured) {
    // Create closure with env in nursery data zone
    uint64_t* env_items = nullptr;
    void* fn = make_closure(this, gc, 2, &env_items);
    ASSERT_NE(env_items, nullptr);

    // Put known values in env
    env_items[0] = int_item(42);
    env_items[1] = int_item(99);

    // Verify env is in nursery
    EXPECT_TRUE(gc_is_nursery_data(gc, env_items));

    // Root the function and collect
    uint64_t root = list_item(fn);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // After compaction, env should be in tenured (not nursery)
    uint8_t* p = (uint8_t*)fn;
    uint64_t* new_env = (uint64_t*)(*(void**)(p + 24));
    EXPECT_FALSE(gc_is_nursery_data(gc, new_env));
    EXPECT_NE((void*)new_env, (void*)env_items);  // pointer changed

    // Values preserved after compaction
    EXPECT_EQ(new_env[0], int_item(42));
    EXPECT_EQ(new_env[1], int_item(99));

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, ClosureCycleCollected) {
    // Build a cycle: Function -> env -> List -> [Function]
    // When neither is rooted, GC should collect the entire cycle.

    // Create a list (will hold a reference to the function)
    void* list = make_list(1, 2);

    // Create a closure that captures the list
    uint64_t* env_items = nullptr;
    void* fn = make_closure(this, gc, 1, &env_items);
    ASSERT_NE(env_items, nullptr);
    env_items[0] = list_item(list);  // fn's env -> list

    // Make the list reference the function (creating the cycle)
    uint8_t* lp = (uint8_t*)list;
    uint64_t* list_items = (uint64_t*)(*(void**)(lp + 8));
    list_items[0] = list_item(fn);   // list -> fn

    // We have: fn -> env -> list -> fn (cycle!)
    // Total objects: fn + list + list's items data = 2 objects + 2 data allocations
    EXPECT_EQ(gc->object_count, 2u);  // fn and list

    // Collect with NO roots — the entire cycle should be freed
    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 0u);
}

TEST_F(GCHeapTest, ClosureCycleSurvivesWhenRooted) {
    // Same cycle as above, but root one member — all should survive
    void* list = make_list(1, 2);

    uint64_t* env_items = nullptr;
    void* fn = make_closure(this, gc, 1, &env_items);
    ASSERT_NE(env_items, nullptr);
    env_items[0] = list_item(list);

    uint8_t* lp = (uint8_t*)list;
    uint64_t* list_items = (uint64_t*)(*(void**)(lp + 8));
    list_items[0] = list_item(fn);

    // Root the function
    uint64_t root = list_item(fn);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // Both fn and list should survive (reachable through cycle from rooted fn)
    EXPECT_EQ(gc->object_count, 2u);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, ClosureNoEnvSafe) {
    // Function with closure_field_count=0 and closure_env as a boxed Item (bound method pattern)
    void* fn = gc_heap_calloc(gc, 40, LMD_TYPE_FUNC);
    uint8_t* p = (uint8_t*)fn;
    *(uint8_t*)(p + 0) = LMD_TYPE_FUNC;
    *(uint8_t*)(p + 2) = 0;  // closure_field_count = 0
    // Set closure_env to a non-null value (boxed Item, not a real env)
    *(void**)(p + 24) = (void*)(uintptr_t)0xDEADBEEF;

    // Root it and collect — should not crash trying to trace the fake env
    uint64_t root = list_item(fn);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 1u);

    gc_unregister_root(gc, &root);
}

// ============================================================================
// 10. VMap GC Tracing and Finalization
// ============================================================================

// VMap layout: { type_id(2@0), pad(6), data*(8@8), vtable*(8@16) } = 24 bytes
// type_id is Container.id which is uint16_t, matching LMD_TYPE_VMAP = 18

// Test-local state for VMap callback verification
static int s_vmap_trace_calls = 0;
static void* s_vmap_trace_last_data = nullptr;
static gc_heap_t* s_vmap_trace_last_gc = nullptr;
static int s_vmap_destroy_calls = 0;
static void* s_vmap_destroy_last_obj = nullptr;
static void* s_vmap_destroy_last_data = nullptr;
// Track Items marked during trace callback
static uint64_t s_traced_items[64];
static int s_traced_item_count = 0;

static void test_vmap_trace(void* data, gc_heap_t* gc) {
    s_vmap_trace_calls++;
    s_vmap_trace_last_data = data;
    s_vmap_trace_last_gc = gc;
    // Simulate tracing: mark two Items stored at data[0] and data[1]
    // (our test fake data is an array of uint64_t Items)
    uint64_t* items = (uint64_t*)data;
    for (int i = 0; i < 2; i++) {
        if (items[i]) {
            if (s_traced_item_count < 64) {
                s_traced_items[s_traced_item_count++] = items[i];
            }
            gc_mark_item(gc, items[i]);
        }
    }
}

static void test_vmap_destroy(void* obj, void* data) {
    s_vmap_destroy_calls++;
    s_vmap_destroy_last_obj = obj;
    s_vmap_destroy_last_data = data;
    free(data);
}

static void reset_vmap_test_state() {
    s_vmap_trace_calls = 0;
    s_vmap_trace_last_data = nullptr;
    s_vmap_trace_last_gc = nullptr;
    s_vmap_destroy_calls = 0;
    s_vmap_destroy_last_obj = nullptr;
    s_vmap_destroy_last_data = nullptr;
    s_traced_item_count = 0;
    memset(s_traced_items, 0, sizeof(s_traced_items));
}

// Helper: create a fake VMap with a malloc'd data block
// The data block is an array of 2 uint64_t Items (simulating key/value pairs).
static void* make_vmap(GCHeapTest* /*t*/, gc_heap_t* gc, uint64_t item0, uint64_t item1) {
    void* obj = gc_heap_calloc(gc, 24, LMD_TYPE_VMAP);
    uint8_t* p = (uint8_t*)obj;
    *(uint16_t*)(p + 0) = LMD_TYPE_VMAP;  // Container type_id

    // Allocate fake "data" via malloc (simulates HashMapData*)
    uint64_t* fake_data = (uint64_t*)calloc(2, sizeof(uint64_t));
    fake_data[0] = item0;
    fake_data[1] = item1;

    *(void**)(p + 8) = fake_data;   // data pointer
    *(void**)(p + 16) = nullptr;    // vtable (not needed for GC)
    return obj;
}

TEST_F(GCHeapTest, VMapTraceCallbackInvoked) {
    // VMap trace callback is invoked during mark phase for rooted VMaps
    reset_vmap_test_state();
    gc->vmap_trace = test_vmap_trace;
    gc->vmap_destroy = test_vmap_destroy;

    void* str = make_string("vmap_value");
    void* vm = make_vmap(this, gc, string_item(str), int_item(42));

    uint64_t root = list_item(vm);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // trace callback should have been called once
    EXPECT_EQ(s_vmap_trace_calls, 1);
    // The string referenced by VMap should survive (marked through callback)
    EXPECT_EQ(gc->object_count, 2u);  // vm + str

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, VMapTracedValuesKeepReferencesAlive) {
    // Objects referenced only through VMap data should survive GC
    reset_vmap_test_state();
    gc->vmap_trace = test_vmap_trace;
    gc->vmap_destroy = test_vmap_destroy;

    void* str_alive = make_string("kept_alive");
    void* str_dead = make_string("unreachable");
    void* vm = make_vmap(this, gc, string_item(str_alive), int_item(99));

    EXPECT_EQ(gc->object_count, 3u);  // vm + str_alive + str_dead

    // Root only the VMap — str_alive is reachable through VMap, str_dead is not
    uint64_t root = list_item(vm);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    // str_alive survives (marked through VMap trace), str_dead is collected
    EXPECT_EQ(gc->object_count, 2u);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, VMapDestroyCallbackOnDead) {
    // Dead VMap gets its backing data freed during sweep
    reset_vmap_test_state();
    gc->vmap_trace = test_vmap_trace;
    gc->vmap_destroy = test_vmap_destroy;

    void* vm = make_vmap(this, gc, int_item(1), int_item(2));
    EXPECT_EQ(gc->object_count, 1u);

    // Collect with no roots — VMap is dead
    gc_collect(gc, NULL, 0, 0, 0);

    // VMap should be collected
    EXPECT_EQ(gc->object_count, 0u);
    // destroy callback should have been called once
    EXPECT_EQ(s_vmap_destroy_calls, 1);
    EXPECT_EQ(s_vmap_destroy_last_obj, vm);
}

TEST_F(GCHeapTest, VMapDeadAlsoCollectsUnreferencedChildren) {
    // When a VMap dies, its children (strings) also die if not rooted elsewhere
    reset_vmap_test_state();
    gc->vmap_trace = test_vmap_trace;
    gc->vmap_destroy = test_vmap_destroy;

    void* str = make_string("orphan");
    void* vm = make_vmap(this, gc, string_item(str), int_item(0));
    EXPECT_EQ(gc->object_count, 2u);

    // No roots — both VMap and string are dead
    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 0u);
    EXPECT_EQ(s_vmap_destroy_calls, 1);
    EXPECT_EQ(s_vmap_destroy_last_obj, vm);
}

TEST_F(GCHeapTest, VMapNullDataSafe) {
    // VMap with NULL data pointer should not crash during trace or finalize
    reset_vmap_test_state();
    gc->vmap_trace = test_vmap_trace;
    gc->vmap_destroy = test_vmap_destroy;

    void* obj = gc_heap_calloc(gc, 24, LMD_TYPE_VMAP);
    uint8_t* p = (uint8_t*)obj;
    *(uint16_t*)(p + 0) = LMD_TYPE_VMAP;
    *(void**)(p + 8) = nullptr;   // NULL data
    *(void**)(p + 16) = nullptr;

    // Collect with no roots — should not crash
    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 0u);
    // NULL backing data is not traced, but finalization still runs so host
    // VMaps can release native payloads even when the map store was lazy.
    EXPECT_EQ(s_vmap_trace_calls, 0);
    EXPECT_EQ(s_vmap_destroy_calls, 1);
    EXPECT_EQ(s_vmap_destroy_last_obj, obj);
    EXPECT_EQ(s_vmap_destroy_last_data, nullptr);
}

TEST_F(GCHeapTest, VMapNoCallbacksSafe) {
    // VMap trace/destroy with no callbacks registered should not crash
    // (gc->vmap_trace and gc->vmap_destroy stay NULL)
    EXPECT_EQ(gc->vmap_trace, nullptr);
    EXPECT_EQ(gc->vmap_destroy, nullptr);

    void* fake_data = calloc(2, sizeof(uint64_t));
    void* obj = gc_heap_calloc(gc, 24, LMD_TYPE_VMAP);
    uint8_t* p = (uint8_t*)obj;
    *(uint16_t*)(p + 0) = LMD_TYPE_VMAP;
    *(void**)(p + 8) = fake_data;
    *(void**)(p + 16) = nullptr;

    // Root it, then collect — should silently skip tracing
    uint64_t root = list_item(obj);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 1u);  // survives because rooted

    // Now unroot and collect — without destroy callback, data leaks (expected)
    gc_unregister_root(gc, &root);
    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 0u);
    // Manually free since no callback was set
    free(fake_data);
}

// ============================================================================
// 11. Error GC Tracing and Finalization
// ============================================================================

static int s_error_trace_calls = 0;
static void* s_error_trace_last_data = nullptr;
static gc_heap_t* s_error_trace_last_gc = nullptr;
static int s_error_destroy_calls = 0;
static void* s_error_destroy_last_data = nullptr;

static uint64_t error_item(void* ptr) {
    return ((uint64_t)LMD_TYPE_ERROR << 56) |
           ((uint64_t)(uintptr_t)ptr & 0x00FFFFFFFFFFFFFFULL);
}

static void test_error_trace(void* data, gc_heap_t* gc) {
    s_error_trace_calls++;
    s_error_trace_last_data = data;
    s_error_trace_last_gc = gc;
}

static void test_error_destroy(void* data) {
    s_error_destroy_calls++;
    s_error_destroy_last_data = data;
}

static void reset_error_test_state() {
    s_error_trace_calls = 0;
    s_error_trace_last_data = nullptr;
    s_error_trace_last_gc = nullptr;
    s_error_destroy_calls = 0;
    s_error_destroy_last_data = nullptr;
}

TEST_F(GCHeapTest, ErrorTaggedPointerRootKeepsObjectAlive) {
    reset_error_test_state();
    gc->error_trace = test_error_trace;
    gc->error_destroy = test_error_destroy;

    void* error = gc_heap_calloc(gc, 32, LMD_TYPE_ERROR);
    ASSERT_NE(error, nullptr);
    uint64_t root = error_item(error);
    gc_register_root(gc, &root);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 1u);
    EXPECT_EQ(s_error_trace_calls, 1);
    EXPECT_EQ(s_error_trace_last_data, error);
    EXPECT_EQ(s_error_trace_last_gc, gc);
    EXPECT_EQ(s_error_destroy_calls, 0);

    gc_unregister_root(gc, &root);
}

TEST_F(GCHeapTest, ErrorDestroyCallbackOnDead) {
    reset_error_test_state();
    gc->error_trace = test_error_trace;
    gc->error_destroy = test_error_destroy;

    void* error = gc_heap_calloc(gc, 32, LMD_TYPE_ERROR);
    ASSERT_NE(error, nullptr);

    gc_collect(gc, NULL, 0, 0, 0);

    EXPECT_EQ(gc->object_count, 0u);
    EXPECT_EQ(s_error_trace_calls, 0);
    EXPECT_EQ(s_error_destroy_calls, 1);
    EXPECT_EQ(s_error_destroy_last_data, error);
}

TEST_F(GCHeapTest, NativeSeenSetDeduplicatesPointers) {
    gc_native_seen_t seen;
    gc_native_seen_init(&seen);

    int first = 1;
    int second = 2;
    EXPECT_EQ(gc_native_seen_seen_or_add(&seen, &first), 0);
    EXPECT_EQ(gc_native_seen_seen_or_add(&seen, &first), 1);
    EXPECT_EQ(gc_native_seen_seen_or_add(&seen, &second), 0);
    EXPECT_EQ(gc_native_seen_seen_or_add(&seen, NULL), 1);

    gc_native_seen_dispose(&seen);
}
