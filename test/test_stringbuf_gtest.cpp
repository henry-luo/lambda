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

// Test for the critical bug fix: vsnprintf buffer calculation
TEST_F(StringBufTest, TestStringbufFormatLongString) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 50);  // Start with small capacity

    // Create a format string that will result in a long output
    // This tests the vsnprintf buffer calculation bug fix
    stringbuf_append_format(sb, "This is a very long formatted string with number %d and string '%s' that should trigger buffer growth", 12345, "test_string_value");

    ASSERT_NE(sb->str, nullptr) << "str should be allocated after format append";
    ASSERT_GT(sb->str->len, 50) << "formatted string should be longer than initial capacity";

    // Verify the content is correct
    const char* expected = "This is a very long formatted string with number 12345 and string 'test_string_value' that should trigger buffer growth";
    ASSERT_STREQ(sb->str->chars, expected) << "formatted string content should be correct";
    ASSERT_EQ(sb->str->len, strlen(expected)) << "length should match expected string length";

    stringbuf_free(sb);
}

// Test multiple format appends to ensure buffer calculations remain correct
TEST_F(StringBufTest, TestStringbufMultipleFormatAppends) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 20);  // Start small

    stringbuf_append_format(sb, "First: %d", 1);
    ASSERT_STREQ(sb->str->chars, "First: 1") << "first format should be correct";

    stringbuf_append_format(sb, ", Second: %s", "hello");
    ASSERT_STREQ(sb->str->chars, "First: 1, Second: hello") << "second format should append correctly";

    stringbuf_append_format(sb, ", Third: %.2f", 3.14);
    ASSERT_EQ(strncmp(sb->str->chars, "First: 1, Second: hello, Third: 3.14", 37), 0) << "third format should append correctly";

    // Add a very long format that forces significant buffer growth
    stringbuf_append_format(sb, ", Fourth: This is a very long string with multiple placeholders %d %s %f %c",
                           42, "world", 2.718, 'X');

    ASSERT_GT(sb->str->len, 100) << "final string should be quite long";
    ASSERT_NE(strstr(sb->str->chars, "Fourth: This is a very long string"), nullptr) << "should contain the long format";
    ASSERT_NE(strstr(sb->str->chars, "42 world"), nullptr) << "should contain formatted values";

    stringbuf_free(sb);
}

// Test stress case: repeated appends with buffer growth
TEST_F(StringBufTest, TestStringbufRepeatedAppends) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 16);  // Very small initial capacity

    // Append many small strings to force multiple buffer growth operations
    for (int i = 0; i < 100; i++) {
        stringbuf_append_format(sb, "Item%d ", i);
    }

    ASSERT_GT(sb->str->len, 500) << "accumulated string should be quite long";
    ASSERT_GT(sb->capacity, 16) << "capacity should have grown significantly";

    // Verify some content
    ASSERT_EQ(strncmp(sb->str->chars, "Item0 Item1 Item2", 17), 0) << "should start with first items";
    ASSERT_NE(strstr(sb->str->chars, "Item99 "), nullptr) << "should contain last item";

    stringbuf_free(sb);
}

// Test edge case: format string that exactly fits in remaining buffer
TEST_F(StringBufTest, TestStringbufFormatExactFit) {
    StringBuf *sb = stringbuf_new_cap(test_pool, sizeof(String) + 20);  // Exact capacity

    // First, partially fill the buffer
    stringbuf_append_str(sb, "Start:");  // 6 chars

    // Now append a format that should exactly fit in remaining space
    stringbuf_append_format(sb, "%d", 1234);  // 4 chars, total would be 10

    ASSERT_STREQ(sb->str->chars, "Start:1234") << "exact fit format should work";
    ASSERT_EQ(sb->str->len, 10) << "length should be exactly 10";

    // Now add something that forces growth
    stringbuf_append_format(sb, " and more %s", "content");
    ASSERT_NE(strstr(sb->str->chars, "Start:1234 and more content"), nullptr) << "should contain all content after growth";

    stringbuf_free(sb);
}

// Test very large format string to ensure no overflow issues
TEST_F(StringBufTest, TestStringbufVeryLargeFormat) {
    StringBuf *sb = stringbuf_new(test_pool);

    // Create a very large string through formatting
    char large_input[1000];
    memset(large_input, 'A', 999);
    large_input[999] = '\0';

    stringbuf_append_format(sb, "Large string: %s", large_input);

    ASSERT_GT(sb->str->len, 1000) << "resulting string should be very large";
    ASSERT_EQ(strncmp(sb->str->chars, "Large string: AAA", 17), 0) << "should start with expected prefix";
    ASSERT_EQ(sb->str->chars[sb->str->len - 1], 'A') << "should end with 'A'";

    stringbuf_free(sb);
}

// Test format with NULL format string (edge case)
TEST_F(StringBufTest, TestStringbufFormatNullFormat) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str(sb, "Before");
    stringbuf_append_format(sb, nullptr);  // Should be handled gracefully
    stringbuf_append_str(sb, "After");

    ASSERT_STREQ(sb->str->chars, "BeforeAfter") << "null format should not affect other appends";

    stringbuf_free(sb);
}

// Test length field overflow protection (22-bit limit = 4,194,303)
TEST_F(StringBufTest, TestStringbufLengthOverflowProtection) {
    StringBuf *sb = stringbuf_new(test_pool);

    // First, get to exactly the limit
    const size_t max_len = 0x3FFFFF; // 22-bit max value: 4,194,303

    // Manually set length to exactly the limit (for testing purposes)
    // In real usage, this would happen through many append operations
    if (stringbuf_ensure_cap(sb, max_len + 100)) {
        sb->length = max_len;
        sb->str->len = max_len;
        sb->str->chars[max_len] = '\0';

        size_t old_length = sb->length;

        // Try to append a character that would exceed the limit
        stringbuf_append_char(sb, 'X');

        // Length should remain unchanged due to overflow protection
        ASSERT_EQ(sb->length, old_length) << "char append should be rejected due to overflow";
        ASSERT_EQ(sb->str->len, old_length) << "str->len should not change when overflow would occur";

        // Try to append a string that would exceed the limit
        stringbuf_append_str(sb, "This should be rejected");
        ASSERT_EQ(sb->length, old_length) << "string append should be rejected due to overflow";

        // Try format append that should also be rejected
        stringbuf_append_format(sb, "Number: %d", 42);
        ASSERT_EQ(sb->length, old_length) << "format append should be rejected due to overflow";
    }

    stringbuf_free(sb);
}
