#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

extern "C" {
#include "../../lib/strview.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
}

class StrViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Runs before each test
    }

    void TearDown() override {
        // Runs after each test
    }
};

TEST_F(StrViewTest, Basic) {
    const char* str = "Hello, World!";
    StrView s = strview_from_str(str);
    
    EXPECT_EQ(s.length, strlen(str));
    EXPECT_EQ(strview_get(&s, 0), 'H');
    EXPECT_EQ(strview_get(&s, s.length), '\0'); // Out of bounds should return '\0'
    EXPECT_EQ(strview_get(&s, s.length - 1), '!'); // Last character
}

TEST_F(StrViewTest, Sub) {
    StrView s = strview_from_str("Hello, World!");
    StrView sub = strview_sub(&s, 7, 12);
    
    EXPECT_EQ(sub.length, 5UL);
    StrView expected = strview_from_str("World");
    EXPECT_TRUE(strview_eq(&sub, &expected));
}

TEST_F(StrViewTest, SubEdgeCases) {
    StrView s = strview_from_str("Hello");
    
    // Valid substring
    StrView sub1 = strview_sub(&s, 1, 4);
    EXPECT_EQ(sub1.length, 3UL);
    EXPECT_TRUE(strview_equal(&sub1, "ell"));
    
    // Invalid range: start > end
    StrView sub2 = strview_sub(&s, 3, 1);
    EXPECT_EQ(sub2.length, 0UL);
    EXPECT_EQ(sub2.str, nullptr);
    
    // Invalid range: end > length
    StrView sub3 = strview_sub(&s, 0, 10);
    EXPECT_EQ(sub3.length, 0UL);
    EXPECT_EQ(sub3.str, nullptr);
    
    // Empty substring
    StrView sub4 = strview_sub(&s, 2, 2);
    EXPECT_EQ(sub4.length, 0UL);
}

TEST_F(StrViewTest, PrefixSuffix) {
    StrView s = strview_from_str("Hello, World!");
    
    EXPECT_TRUE(strview_start_with(&s, "Hello"));
    EXPECT_FALSE(strview_start_with(&s, "World"));
    EXPECT_TRUE(strview_end_with(&s, "World!"));
    EXPECT_FALSE(strview_end_with(&s, "Hello"));
}

TEST_F(StrViewTest, Find) {
    StrView s = strview_from_str("Hello, World!");
    
    EXPECT_EQ(strview_find(&s, "World"), 7);
    EXPECT_EQ(strview_find(&s, "NotFound"), -1);
    EXPECT_EQ(strview_find(&s, ","), 5);
}

TEST_F(StrViewTest, Trim) {
    StrView s = strview_from_str("  Hello, World!  ");
    strview_trim(&s);
    
    StrView expected = strview_from_str("Hello, World!");
    EXPECT_TRUE(strview_eq(&s, &expected));
    EXPECT_EQ(s.length, 13UL);
}

TEST_F(StrViewTest, ToCstr) {
    StrView s = strview_from_str("Hello");
    char* cstr = strview_to_cstr(&s);
    
    ASSERT_NE(cstr, nullptr);
    EXPECT_STREQ(cstr, "Hello");
    free(cstr);
}

TEST_F(StrViewTest, EqualCstr) {
    StrView s = strview_from_str("Hello");
    
    EXPECT_TRUE(strview_equal(&s, "Hello"));
    EXPECT_FALSE(strview_equal(&s, "World"));
    EXPECT_FALSE(strview_equal(&s, "Hello, World!"));
}

TEST_F(StrViewTest, ToInt) {
    StrView s1 = strview_from_str("123");
    StrView s2 = strview_from_str("-456");
    StrView s3 = strview_from_str("0");
    StrView s4 = strview_from_str("abc");
    StrView s5 = strview_from_str("123abc");
    
    EXPECT_EQ(strview_to_int(&s1), 123);
    EXPECT_EQ(strview_to_int(&s2), -456);
    EXPECT_EQ(strview_to_int(&s3), 0);
    EXPECT_EQ(strview_to_int(&s4), 0);
    EXPECT_EQ(strview_to_int(&s5), 123);
}

// ───────── extended API ─────────

TEST_F(StrViewTest, FromCstr) {
    StrView s = strview_from_cstr("Hello");
    EXPECT_EQ(s.length, 5u);
    EXPECT_EQ(strview_get(&s, 0), 'H');

    StrView empty = strview_from_cstr(nullptr);
    EXPECT_EQ(empty.length, 0u);
    ASSERT_NE(empty.str, nullptr);  // points to "" sentinel, not NULL
    EXPECT_EQ(empty.str[0], '\0');

    StrView e2 = strview_from_cstr("");
    EXPECT_EQ(e2.length, 0u);
}

TEST_F(StrViewTest, StartsEndsWithAliases) {
    StrView s = strview_from_str("Hello, World!");
    EXPECT_TRUE(strview_starts_with(&s, "Hello"));
    EXPECT_FALSE(strview_starts_with(&s, "World"));
    EXPECT_TRUE(strview_ends_with(&s, "World!"));
    EXPECT_FALSE(strview_ends_with(&s, "Hello"));
}

TEST_F(StrViewTest, Contains) {
    StrView s = strview_from_str("the quick brown fox");
    EXPECT_TRUE(strview_contains(&s, "quick"));
    EXPECT_TRUE(strview_contains(&s, "fox"));
    EXPECT_TRUE(strview_contains(&s, ""));  // empty needle always matches
    EXPECT_FALSE(strview_contains(&s, "missing"));
    EXPECT_FALSE(strview_contains(&s, nullptr));
}

TEST_F(StrViewTest, ToInt64) {
    int64_t v;
    StrView s1 = strview_from_str("9223372036854775807");  // INT64_MAX
    EXPECT_TRUE(strview_to_int64(&s1, &v));
    EXPECT_EQ(v, INT64_MAX);

    StrView s2 = strview_from_str("-123");
    EXPECT_TRUE(strview_to_int64(&s2, &v));
    EXPECT_EQ(v, -123);

    StrView empty = strview_from_str("");
    EXPECT_FALSE(strview_to_int64(&empty, &v));

    StrView garbage = strview_from_str("abc");
    EXPECT_FALSE(strview_to_int64(&garbage, &v));
}

TEST_F(StrViewTest, ToDouble) {
    double v;
    StrView s1 = strview_from_str("3.14");
    EXPECT_TRUE(strview_to_double(&s1, &v));
    EXPECT_DOUBLE_EQ(v, 3.14);

    StrView s2 = strview_from_str("-1e3");
    EXPECT_TRUE(strview_to_double(&s2, &v));
    EXPECT_DOUBLE_EQ(v, -1000.0);

    StrView empty = strview_from_str("");
    EXPECT_FALSE(strview_to_double(&empty, &v));
}

TEST_F(StrViewTest, HashStableAcrossSameContents) {
    StrView a = strview_from_str("hello");
    StrView b = strview_from_str("hello");
    StrView c = strview_from_str("world");
    EXPECT_EQ(strview_hash(&a), strview_hash(&b));
    EXPECT_NE(strview_hash(&a), strview_hash(&c));
    StrView empty = strview_from_str("");
    EXPECT_EQ(strview_hash(&empty), 0u);
    EXPECT_EQ(strview_hash(nullptr), 0u);
}

TEST_F(StrViewTest, DupWithPool) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    StrView s = strview_from_str("pool-allocated");
    char* copy = strview_dup_with_pool(&s, pool);
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, "pool-allocated");

    // pool=NULL falls back to a heap allocation we must NOT free with pool_free
    char* heap_copy = strview_dup_with_pool(&s, nullptr);
    ASSERT_NE(heap_copy, nullptr);
    EXPECT_STREQ(heap_copy, "pool-allocated");
    free(heap_copy);

    // empty view round-trips to ""
    StrView empty = strview_from_str("");
    char* e = strview_dup_with_pool(&empty, pool);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(e, "");

    pool_destroy(pool);
}