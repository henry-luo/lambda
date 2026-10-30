#include <gtest/gtest.h>
#include "../lib/priority_queue.h"
#include <stdint.h>

// ============================================================================
// Priority Queue Tests
// ============================================================================

TEST(PriorityQueue, CreateAndDestroy) {
    PriorityQueue* pq = priority_queue_create(10);
    ASSERT_NE(pq, nullptr);
    EXPECT_TRUE(priority_queue_is_empty(pq));
    EXPECT_EQ(priority_queue_size(pq), 0);
    priority_queue_destroy(pq);
}

TEST(PriorityQueue, PushAndPop) {
    PriorityQueue* pq = priority_queue_create(10);

    int data1 = 100, data2 = 200, data3 = 300;

    // Push items with different priorities
    EXPECT_TRUE(priority_queue_push(pq, &data1, 2));  // Normal
    EXPECT_TRUE(priority_queue_push(pq, &data2, 0));  // Critical
    EXPECT_TRUE(priority_queue_push(pq, &data3, 1));  // High

    EXPECT_EQ(priority_queue_size(pq), 3);

    // Should pop in priority order: 0, 1, 2
    EXPECT_EQ(priority_queue_pop(pq), &data2);  // Critical (0)
    EXPECT_EQ(priority_queue_pop(pq), &data3);  // High (1)
    EXPECT_EQ(priority_queue_pop(pq), &data1);  // Normal (2)

    EXPECT_TRUE(priority_queue_is_empty(pq));

    priority_queue_destroy(pq);
}

TEST(PriorityQueue, Peek) {
    PriorityQueue* pq = priority_queue_create(10);

    int data1 = 100, data2 = 200;
    priority_queue_push(pq, &data1, 5);
    priority_queue_push(pq, &data2, 3);

    // Peek should return highest priority without removing
    EXPECT_EQ(priority_queue_peek(pq), &data2);
    EXPECT_EQ(priority_queue_size(pq), 2);

    // Pop should remove it
    EXPECT_EQ(priority_queue_pop(pq), &data2);
    EXPECT_EQ(priority_queue_size(pq), 1);

    priority_queue_destroy(pq);
}

TEST(PriorityQueue, Clear) {
    PriorityQueue* pq = priority_queue_create(10);

    int data = 100;
    priority_queue_push(pq, &data, 1);
    priority_queue_push(pq, &data, 2);
    priority_queue_push(pq, &data, 3);

    EXPECT_EQ(priority_queue_size(pq), 3);

    priority_queue_clear(pq);

    EXPECT_TRUE(priority_queue_is_empty(pq));
    EXPECT_EQ(priority_queue_size(pq), 0);

    priority_queue_destroy(pq);
}

struct IndexedPriority {double score;unsigned id;size_t index;};
static int indexed_priority_compare(const void* left,const void* right,void*) {
    const auto *a=(const IndexedPriority*)left,*b=(const IndexedPriority*)right;
    return a->score!=b->score?(a->score>b->score?1:-1):(a->id>b->id)-(a->id<b->id);
}
static void indexed_priority_relocate(void* item,size_t index,void*) {((IndexedPriority*)item)->index=index;}
TEST(PriorityQueue, CustomOrderingTracksBothKeyDirectionsAndRemoval) {
    auto* queue=priority_queue_create_custom(1,indexed_priority_compare,indexed_priority_relocate,nullptr);ASSERT_NE(queue,nullptr);
    IndexedPriority items[]={{4,4,SIZE_MAX},{3,3,SIZE_MAX},{2,2,SIZE_MAX},{1,1,SIZE_MAX}};
    for(auto& item:items)ASSERT_TRUE(priority_queue_push(queue,&item,0));
    items[0].score=.5;ASSERT_TRUE(priority_queue_update(queue,items[0].index));EXPECT_EQ(priority_queue_peek(queue),&items[0]);
    items[0].score=5;ASSERT_TRUE(priority_queue_update(queue,items[0].index));EXPECT_EQ(priority_queue_pop(queue),&items[3]);
    EXPECT_EQ(items[3].index,SIZE_MAX);
    for(size_t i=0;i<queue->size;i++)EXPECT_EQ(((IndexedPriority*)queue->entries[i].data)->index,i);
    priority_queue_clear(queue);for(auto& item:items)EXPECT_EQ(item.index,SIZE_MAX);
    EXPECT_FALSE(priority_queue_update(queue,0));priority_queue_destroy(queue);
}
TEST(PriorityQueue, CustomOrderingKeepsFractionalPrecisionAndStableTies) {
    auto* queue=priority_queue_create_custom(2,indexed_priority_compare,indexed_priority_relocate,nullptr);ASSERT_NE(queue,nullptr);
    IndexedPriority items[]={{1.000000000000001,0,SIZE_MAX},{1,2,SIZE_MAX},{1,1,SIZE_MAX}};
    for(auto& item:items)ASSERT_TRUE(priority_queue_push(queue,&item,0));
    EXPECT_EQ(priority_queue_pop(queue),&items[2]);EXPECT_EQ(priority_queue_pop(queue),&items[1]);EXPECT_EQ(priority_queue_pop(queue),&items[0]);
    priority_queue_destroy(queue);
}
