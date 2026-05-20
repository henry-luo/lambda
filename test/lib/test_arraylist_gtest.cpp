// test/lib/test_arraylist_gtest.cpp - tests for lib/arraylist
// Focused on the accessor API added on top of the upstream library.
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "../../lib/arraylist.h"
}

namespace {

ArrayList* build_list(const std::vector<void*>& items) {
    ArrayList* l = arraylist_new(4);
    for (void* p : items) arraylist_append(l, p);
    return l;
}

}  // namespace

TEST(ArrayListTest, GetSetSize) {
    int a = 1, b = 2, c = 3;
    ArrayList* l = build_list({&a, &b, &c});
    EXPECT_EQ(arraylist_size(l), 3);
    EXPECT_EQ(arraylist_length(l), 3);
    EXPECT_EQ(arraylist_get(l, 0), &a);
    EXPECT_EQ(arraylist_get(l, 1), &b);
    EXPECT_EQ(arraylist_get(l, 2), &c);

    arraylist_set(l, 1, &c);
    EXPECT_EQ(arraylist_get(l, 1), &c);

    arraylist_free(l);
}

TEST(ArrayListTest, FrontBack) {
    int a = 1, b = 2, c = 3;
    ArrayList* l = build_list({&a, &b, &c});
    EXPECT_EQ(arraylist_front(l), &a);
    EXPECT_EQ(arraylist_back(l), &c);
    arraylist_free(l);
}

TEST(ArrayListTest, PopReturnsLastAndShrinks) {
    int a = 1, b = 2, c = 3;
    ArrayList* l = build_list({&a, &b, &c});
    EXPECT_EQ(arraylist_pop(l), &c);
    EXPECT_EQ(arraylist_size(l), 2);
    EXPECT_EQ(arraylist_pop(l), &b);
    EXPECT_EQ(arraylist_pop(l), &a);
    EXPECT_EQ(arraylist_pop(l), nullptr);  // empty
    EXPECT_EQ(arraylist_size(l), 0);
    arraylist_free(l);
}

TEST(ArrayListTest, PopFrontReturnsFirstAndShifts) {
    int a = 1, b = 2, c = 3;
    ArrayList* l = build_list({&a, &b, &c});
    EXPECT_EQ(arraylist_pop_front(l), &a);
    EXPECT_EQ(arraylist_size(l), 2);
    EXPECT_EQ(arraylist_get(l, 0), &b);
    EXPECT_EQ(arraylist_get(l, 1), &c);
    EXPECT_EQ(arraylist_pop_front(l), &b);
    EXPECT_EQ(arraylist_pop_front(l), &c);
    EXPECT_EQ(arraylist_pop_front(l), nullptr);
    arraylist_free(l);
}

TEST(ArrayListTest, ReserveGrowsCapacity) {
    ArrayList* l = arraylist_new(4);
    EXPECT_TRUE(arraylist_reserve(l, 100));
    EXPECT_GE(l->_alloced, 100);
    EXPECT_EQ(arraylist_size(l), 0);
    // shrinking via reserve is a no-op (it never shrinks)
    int prev = l->_alloced;
    EXPECT_TRUE(arraylist_reserve(l, 2));
    EXPECT_EQ(l->_alloced, prev);
    arraylist_free(l);
}

TEST(ArrayListTest, ForeachIteratesInOrder) {
    int a = 1, b = 2, c = 3;
    ArrayList* l = build_list({&a, &b, &c});

    std::vector<int*> seen;
    ARRAYLIST_FOREACH(l, int*, p) {
        seen.push_back(p);
    }
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], &a);
    EXPECT_EQ(seen[1], &b);
    EXPECT_EQ(seen[2], &c);

    arraylist_free(l);
}

TEST(ArrayListTest, ForeachEmptyListIsNoOp) {
    ArrayList* l = arraylist_new(4);
    int count = 0;
    ARRAYLIST_FOREACH(l, void*, p) {
        (void)p; count++;
    }
    EXPECT_EQ(count, 0);
    arraylist_free(l);
}

TEST(ArrayListTest, ReserveNullSafety) {
    EXPECT_FALSE(arraylist_reserve(nullptr, 10));
}

TEST(ArrayListTest, PopNullSafety) {
    EXPECT_EQ(arraylist_pop(nullptr), nullptr);
    EXPECT_EQ(arraylist_pop_front(nullptr), nullptr);
}
