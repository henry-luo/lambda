// test/lib/test_sort_gtest.cpp - tests for lib/sort
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

extern "C" {
#include "../../lib/sort.h"
}

namespace {

int cmp_int(const void* a, const void* b, void* udata) {
    (void)udata;
    int va = *(const int*)a;
    int vb = *(const int*)b;
    return (va > vb) - (va < vb);
}

int cmp_int_desc(const void* a, const void* b, void* udata) {
    return -cmp_int(a, b, udata);
}

struct Item { int priority; const char* tag; };

long item_priority_key(const void* item, void* udata) {
    (void)udata;
    return (long)((const Item*)item)->priority;
}

double item_priority_key_d(const void* item, void* udata) {
    (void)udata;
    return (double)((const Item*)item)->priority;
}

}  // namespace

TEST(SortTest, InsertionSortInt) {
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    insertion_sort(arr, 10, sizeof(int), cmp_int, nullptr);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(arr[i], i);
}

TEST(SortTest, InsertionSortDescending) {
    int arr[] = {5, 3, 8, 1, 9};
    insertion_sort(arr, 5, sizeof(int), cmp_int_desc, nullptr);
    EXPECT_EQ(arr[0], 9);
    EXPECT_EQ(arr[1], 8);
    EXPECT_EQ(arr[2], 5);
    EXPECT_EQ(arr[3], 3);
    EXPECT_EQ(arr[4], 1);
}

TEST(SortTest, InsertionSortAlreadySorted) {
    int arr[] = {1, 2, 3, 4, 5};
    int expected[] = {1, 2, 3, 4, 5};
    insertion_sort(arr, 5, sizeof(int), cmp_int, nullptr);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(arr[i], expected[i]);
}

TEST(SortTest, InsertionSortReverse) {
    int arr[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    insertion_sort(arr, 10, sizeof(int), cmp_int, nullptr);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(arr[i], i);
}

TEST(SortTest, InsertionSortEdgeCases) {
    int single[] = {42};
    insertion_sort(single, 1, sizeof(int), cmp_int, nullptr);
    EXPECT_EQ(single[0], 42);

    insertion_sort(nullptr, 0, sizeof(int), cmp_int, nullptr);   // no-op
    insertion_sort(single, 0, sizeof(int), cmp_int, nullptr);    // no-op
    insertion_sort(single, 1, sizeof(int), nullptr, nullptr);    // no cmp
}

TEST(SortTest, InsertionSortStability) {
    // pairs with equal keys: insertion sort is stable, so original order of
    // equal-keyed elements must be preserved.
    struct Pair { int key; int orig_idx; };
    Pair arr[] = {{1, 0}, {2, 1}, {1, 2}, {2, 3}, {1, 4}};
    insertion_sort(arr, 5, sizeof(Pair),
        [](const void* a, const void* b, void*) -> int {
            int ka = ((const Pair*)a)->key;
            int kb = ((const Pair*)b)->key;
            return (ka > kb) - (ka < kb);
        }, nullptr);
    // first three should be the 1s in original order
    EXPECT_EQ(arr[0].orig_idx, 0);
    EXPECT_EQ(arr[1].orig_idx, 2);
    EXPECT_EQ(arr[2].orig_idx, 4);
    EXPECT_EQ(arr[3].orig_idx, 1);
    EXPECT_EQ(arr[4].orig_idx, 3);
}

TEST(SortTest, SortPtrsByIntKey) {
    Item a{3, "a"}, b{1, "b"}, c{2, "c"}, d{0, "d"};
    void* arr[] = {&a, &b, &c, &d};
    sort_ptrs_by_int_key(arr, 4, item_priority_key, nullptr);
    EXPECT_EQ(((Item*)arr[0])->priority, 0);
    EXPECT_EQ(((Item*)arr[1])->priority, 1);
    EXPECT_EQ(((Item*)arr[2])->priority, 2);
    EXPECT_EQ(((Item*)arr[3])->priority, 3);
}

TEST(SortTest, SortPtrsByDoubleKey) {
    Item a{3, "a"}, b{1, "b"}, c{2, "c"};
    void* arr[] = {&a, &b, &c};
    sort_ptrs_by_double_key(arr, 3, item_priority_key_d, nullptr);
    EXPECT_EQ(((Item*)arr[0])->priority, 1);
    EXPECT_EQ(((Item*)arr[1])->priority, 2);
    EXPECT_EQ(((Item*)arr[2])->priority, 3);
}

TEST(SortTest, SortPtrsEmptyAndSingle) {
    void* arr[] = {nullptr};
    sort_ptrs_by_int_key(nullptr, 0, item_priority_key, nullptr);  // no-op
    sort_ptrs_by_int_key(arr, 0, item_priority_key, nullptr);
    sort_ptrs_by_int_key(arr, 1, item_priority_key, nullptr);
}

// ───────── Standard comparator tests ─────────

#include <cstdlib>  // qsort
#include <cstring>

TEST(SortTest, StdCmpIntWithQsort) {
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    qsort(arr, 10, sizeof(int), sort_cmp_int_asc);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(arr[i], i);

    qsort(arr, 10, sizeof(int), sort_cmp_int_desc);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(arr[i], 9 - i);
}

TEST(SortTest, StdCmpInt64WithQsort) {
    int64_t arr[] = {-5, 3, INT64_MAX, INT64_MIN, 0, 42};
    qsort(arr, 6, sizeof(int64_t), sort_cmp_int64_asc);
    EXPECT_EQ(arr[0], INT64_MIN);
    EXPECT_EQ(arr[1], -5);
    EXPECT_EQ(arr[2], 0);
    EXPECT_EQ(arr[3], 3);
    EXPECT_EQ(arr[4], 42);
    EXPECT_EQ(arr[5], INT64_MAX);

    qsort(arr, 6, sizeof(int64_t), sort_cmp_int64_desc);
    EXPECT_EQ(arr[0], INT64_MAX);
    EXPECT_EQ(arr[5], INT64_MIN);
}

TEST(SortTest, StdCmpUint64WithQsort) {
    uint64_t arr[] = {5, 0, UINT64_MAX, 7, 1};
    qsort(arr, 5, sizeof(uint64_t), sort_cmp_uint64_asc);
    EXPECT_EQ(arr[0], 0u);
    EXPECT_EQ(arr[1], 1u);
    EXPECT_EQ(arr[2], 5u);
    EXPECT_EQ(arr[3], 7u);
    EXPECT_EQ(arr[4], UINT64_MAX);
}

TEST(SortTest, StdCmpDoubleWithQsort) {
    double arr[] = {3.5, -1.0, 0.0, 2.7, 1e-9};
    qsort(arr, 5, sizeof(double), sort_cmp_double_asc);
    EXPECT_LT(arr[0], arr[4]);
    EXPECT_DOUBLE_EQ(arr[0], -1.0);
    EXPECT_DOUBLE_EQ(arr[4], 3.5);

    qsort(arr, 5, sizeof(double), sort_cmp_double_desc);
    EXPECT_DOUBLE_EQ(arr[0], 3.5);
    EXPECT_DOUBLE_EQ(arr[4], -1.0);
}

TEST(SortTest, StdCmpFloatWithQsort) {
    float arr[] = {3.5f, -1.0f, 0.0f};
    qsort(arr, 3, sizeof(float), sort_cmp_float_asc);
    EXPECT_FLOAT_EQ(arr[0], -1.0f);
    EXPECT_FLOAT_EQ(arr[2], 3.5f);
}

TEST(SortTest, StdCmpCstrWithQsort) {
    const char* arr[] = {"banana", "apple", "cherry", "Apple"};
    qsort(arr, 4, sizeof(const char*), sort_cmp_cstr_asc);
    // case-sensitive ASCII: 'A' < 'a' so "Apple" first
    EXPECT_STREQ(arr[0], "Apple");
    EXPECT_STREQ(arr[1], "apple");
    EXPECT_STREQ(arr[2], "banana");
    EXPECT_STREQ(arr[3], "cherry");

    // case-insensitive: "apple"/"Apple" tie, others fall in
    qsort(arr, 4, sizeof(const char*), sort_cmp_cstr_ci_asc);
    EXPECT_EQ(0, strcasecmp(arr[0], "apple"));
    EXPECT_EQ(0, strcasecmp(arr[1], "apple"));
    EXPECT_STREQ(arr[2], "banana");
    EXPECT_STREQ(arr[3], "cherry");
}

// SORT_CMP_AS_R adapter — wraps a 2-arg cmp into 3-arg for insertion_sort.
SORT_CMP_AS_R(sort_cmp_int_asc)

TEST(SortTest, AsRAdapterWorksWithInsertionSort) {
    int arr[] = {5, 3, 8, 1, 9};
    insertion_sort(arr, 5, sizeof(int), sort_cmp_int_asc_r, nullptr);
    EXPECT_EQ(arr[0], 1);
    EXPECT_EQ(arr[4], 9);
}

TEST(SortTest, SortQsortRUsesUserData) {
    struct OffsetCmp {
        static int cmp(const void* a, const void* b, void* udata) {
            int offset = *(int*)udata;
            int va = *(const int*)a + offset;
            int vb = *(const int*)b + offset;
            return (va > vb) - (va < vb);
        }
    };

    int arr[] = {9, 1, 4, 3, 7, 0, 2, 8, 5, 6};
    int offset = 10;
    sort_qsort_r(arr, 10, sizeof(int), OffsetCmp::cmp, &offset);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(arr[i], i);
}

TEST(SortTest, IntrosortAliasSorts) {
    int arr[] = {4, 2, 5, 1, 3};
    introsort(arr, 5, sizeof(int), cmp_int, nullptr);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(arr[i], i + 1);
}
