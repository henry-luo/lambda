#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/stringbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

class StringBufTest : public ::testing::Test {
protected:
    VariableMemPool *test_pool = nullptr;

    void SetUp() override {
        MemPoolError err = pool_variable_init(&test_pool, 1024 * 1024, 10);
        ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (test_pool) {
            pool_variable_destroy(test_pool);
            test_pool = nullptr;
        }
    }
};

TEST_F(StringBufTest, TestStringbufCreation) {
    StringBuf *sb = stringbuf_new(test_pool);
    ASSERT_NE(sb, nullptr) << "stringbuf_new should return non-NULL";
    ASSERT_EQ(sb->pool, test_pool) << "pool should be set correctly";
    ASSERT_EQ(sb->length, 0) << "initial length should be 0";
    ASSERT_TRUE(sb->str == nullptr || sb->capacity > 0) << "str should be NULL or capacity > 0";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCreationWithCapacity) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 100);
    ASSERT_NE(sb, nullptr) << "stringbuf_new_cap should return non-NULL";
    ASSERT_GE(sb->capacity, sizeof(String) + 100) << "capacity should be at least requested + String header";
    ASSERT_EQ(sb->length, 0) << "initial length should be 0";
    ASSERT_NE(sb->str, nullptr) << "str should be allocated";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendStr) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello");
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending 'Hello'";
    ASSERT_STREQ(sb->str->chars, "Hello") << "content should be 'Hello'";
    
    stringbuf_append_str(sb, " World");
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 after appending ' World'";
    ASSERT_STREQ(sb->str->chars, "Hello World") << "content should be 'Hello World'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendChar) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_char(sb, 'A');
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 1) << "length should be 1 after appending 'A'";
    ASSERT_EQ(sb->str->chars[0], 'A') << "first character should be 'A'";
    ASSERT_EQ(sb->str->chars[1], '\0') << "should be null terminated";
    
    stringbuf_append_char(sb, 'B');
    ASSERT_EQ(sb->str->len, 2) << "length should be 2 after appending 'B'";
    ASSERT_STREQ(sb->str->chars, "AB") << "content should be 'AB'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendStrN) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str_n(sb, "Hello World", 5);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending first 5 chars";
    ASSERT_STREQ(sb->str->chars, "Hello") << "content should be 'Hello'";
    
    stringbuf_append_str_n(sb, " World!", 6);
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 after appending ' World'";
    ASSERT_STREQ(sb->str->chars, "Hello World") << "content should be 'Hello World'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendCharN) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_char_n(sb, 'X', 3);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 3) << "length should be 3 after appending 3 'X's";
    ASSERT_STREQ(sb->str->chars, "XXX") << "content should be 'XXX'";
    
    stringbuf_append_char_n(sb, 'Y', 2);
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending 2 'Y's";
    ASSERT_STREQ(sb->str->chars, "XXXYY") << "content should be 'XXXYY'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendFormat) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_format(sb, "Number: %d", 42);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_STREQ(sb->str->chars, "Number: 42") << "content should be 'Number: 42'";
    
    stringbuf_append_format(sb, ", String: %s", "test");
    ASSERT_STREQ(sb->str->chars, "Number: 42, String: test") << "content should include both parts";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendNumbers) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_int(sb, 123);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_STREQ(sb->str->chars, "123") << "content should be '123'";
    
    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%u", 456U);
    ASSERT_STREQ(sb->str->chars, "456") << "content should be '456'";
    
    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%.2f", 3.14159);
    ASSERT_EQ(strncmp(sb->str->chars, "3.14", 4), 0) << "content should start with '3.14'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufReset) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 before reset";
    
    stringbuf_reset(sb);
    ASSERT_EQ(sb->str->len, 0) << "length should be 0 after reset";
    ASSERT_EQ(sb->str->chars[0], '\0') << "should be null terminated after reset";
    
    // Should be able to append after reset
    stringbuf_append_str(sb, "New");
    ASSERT_EQ(sb->str->len, 3) << "length should be 3 after appending to reset buffer";
    ASSERT_STREQ(sb->str->chars, "New") << "content should be 'New'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufFullReset) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    
    stringbuf_full_reset(sb);
    ASSERT_EQ(sb->str, nullptr) << "str should be NULL after full reset";
    ASSERT_EQ(sb->length, 0) << "length should be 0 after full reset";
    ASSERT_EQ(sb->capacity, 0) << "capacity should be 0 after full reset";
    
    // Should be able to append after full reset
    stringbuf_append_str(sb, "New");
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 3) << "length should be 3";
    ASSERT_STREQ(sb->str->chars, "New") << "content should be 'New'";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCopy) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    StringBuf *sb2 = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb1, "Hello World");
    stringbuf_copy(sb2, sb1);
    
    ASSERT_NE(sb2->str, nullptr) << "destination str should be allocated";
    ASSERT_EQ(sb2->str->len, sb1->str->len) << "lengths should match";
    ASSERT_STREQ(sb2->str->chars, sb1->str->chars) << "contents should match";
    ASSERT_NE(sb2->str, sb1->str) << "should be different String objects";
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

TEST_F(StringBufTest, TestStringbufDup) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    stringbuf_append_str(sb1, "Hello World");
    
    StringBuf *sb2 = stringbuf_dup(sb1);
    ASSERT_NE(sb2, nullptr) << "dup should return non-NULL";
    ASSERT_NE(sb2->str, nullptr) << "dup str should be allocated";
    ASSERT_EQ(sb2->str->len, sb1->str->len) << "lengths should match";
    ASSERT_STREQ(sb2->str->chars, sb1->str->chars) << "contents should match";
    ASSERT_NE(sb2->str, sb1->str) << "should be different String objects";
    ASSERT_EQ(sb2->pool, sb1->pool) << "pools should match";
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

TEST_F(StringBufTest, TestStringbufToString) {
    StringBuf *sb = stringbuf_new(test_pool);
    stringbuf_append_str(sb, "Hello World");
    
    String *str = stringbuf_to_string(sb);
    ASSERT_NE(str, nullptr) << "to_string should return non-NULL";
    ASSERT_EQ(str->len, 11) << "string length should be 11";
    ASSERT_STREQ(str->chars, "Hello World") << "string content should be 'Hello World'";
    
    // Buffer should be reset after to_string (str pointer becomes NULL)
    ASSERT_EQ(sb->str, nullptr) << "buffer str should be NULL after to_string";
    ASSERT_EQ(sb->length, 0) << "buffer length should be 0 after to_string";
    ASSERT_EQ(sb->capacity, 0) << "buffer capacity should be 0 after to_string";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCapacityGrowth) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 10);
    size_t initial_capacity = sb->capacity;
    
    // Append enough data to force growth
    for (int i = 0; i < 100; i++) {
        stringbuf_append_char(sb, 'A');
    }
    
    ASSERT_GT(sb->capacity, initial_capacity) << "capacity should have grown";
    ASSERT_EQ(sb->str->len, 100) << "length should be 100";
    
    // Verify content
    bool all_a = true;
    for (int i = 0; i < 100; i++) {
        if (sb->str->chars[i] != 'A') {
            all_a = false;
            break;
        }
    }
    ASSERT_TRUE(all_a) << "all characters should be 'A'";
    ASSERT_EQ(sb->str->chars[100], '\0') << "should be null terminated";
    
    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufEdgeCases) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    // Test empty string append
    stringbuf_append_str(sb, "");
    ASSERT_EQ(sb->str->len, 0) << "empty string append should not change length";
    
    // Test zero character append
    stringbuf_append_char_n(sb, 'X', 0);
    ASSERT_EQ(sb->str->len, 0) << "zero char append should not change length";
    
    // Test zero length string append
    stringbuf_append_str_n(sb, "Hello", 0);
    ASSERT_EQ(sb->str->len, 0) << "zero length append should not change length";
    
    stringbuf_free(sb);
}