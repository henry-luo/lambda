#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/strview.h"
}

class StrViewTest : public ::testing::Test {
protected:
    void SetUp() override {
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