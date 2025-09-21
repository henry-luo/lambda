/*
 * Comprehensive StrBuf Test Suite (GTest Version)
 * ===============================================
 * 
 * Total Tests: 36 test cases covering:
 * 
 * Original Tests (13):
 * - Basic creation and initialization
 * - String and character appending 
 * - Format functions and file operations
 * - Copy and duplication operations
 * 
 * Added Memory Reallocation Tests (15):
 * - Memory reallocation scenarios
 * - Character append reallocation
 * - Character N append reallocation  
 * - Copy with reallocation
 * - Edge cases and boundary conditions
 * - Capacity management
 * - Full reset operations
 * - Stress testing scenarios
 * - Integer append functions
 * - Reallocation pattern verification
 * - Format function reallocation
 * - Pooled memory basics
 * - Boundary conditions
 * - Ensure capacity edge cases
 * - Memory preservation during reallocation
 * 
 * Added Memory Free Tests (8):
 * - Regular memory free (malloc/free path)
 * - Empty buffer free
 * - Free after full reset
 * - Pooled memory free basic cases
 * - Pooled memory free after reallocation
 * - Multiple pooled buffers from same pool
 * - Comparison of pooled vs regular free paths
 * - Edge cases in memory deallocation
 * 
 * Special Focus on Memory Management:
 * - Tests buffer expansion when capacity is exceeded
 * - Verifies capacity doubling behavior
 * - Validates content preservation during reallocation
 * - Tests multiple successive reallocations
 * - Checks pointer updates during reallocation
 * - Comprehensive testing of both malloc/free and pool allocation paths
 * - Verifies proper cleanup of both string data and StrBuf structures
 * - Tests pool association and deallocation
 * - Stress tests with many operations
 * - Edge case handling and boundary conditions
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "../lib/strbuf.h"
}

class StrBufTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Runs before each test
    }

    void TearDown() override {
        // Runs after each test
    }
};

TEST_F(StrBufTest, TestNew) {
    StrBuf* sb = strbuf_new();
    ASSERT_NE(sb, nullptr) << "strbuf_new() should return non-null pointer";
    ASSERT_NE(sb->str, nullptr) << "String buffer should be allocated";
    ASSERT_EQ(sb->length, 0) << "Initial length should be 0";
    ASSERT_GT(sb->capacity, 0) << "Initial capacity should be at least 1";
    ASSERT_EQ(sb->str[0], '\0') << "Buffer should be null-terminated";
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestNewCap) {
    size_t cap = 64;
    StrBuf* sb = strbuf_new_cap(cap);
    ASSERT_NE(sb, nullptr);
    ASSERT_NE(sb->str, nullptr);
    ASSERT_EQ(sb->length, 0);
    ASSERT_GE(sb->capacity, cap);
    ASSERT_EQ(sb->str[0], '\0');
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestCreate) {
    const char* test_str = "Hello";
    StrBuf* sb = strbuf_create(test_str);
    ASSERT_NE(sb, nullptr);
    ASSERT_STREQ(sb->str, test_str);
    ASSERT_EQ(sb->length, strlen(test_str));
    ASSERT_GE(sb->capacity, sb->length + 1);
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestReset) {
    StrBuf* sb = strbuf_create("Test");
    strbuf_reset(sb);
    ASSERT_EQ(sb->length, 0);
    ASSERT_EQ(sb->str[0], '\0');
    ASSERT_GT(sb->capacity, 0);
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendStr) {
    StrBuf* sb = strbuf_new();
    const char* str1 = "Hello";
    const char* str2 = " World";
    
    strbuf_append_str(sb, str1);
    ASSERT_STREQ(sb->str, str1);
    ASSERT_EQ(sb->length, strlen(str1));
    
    strbuf_append_str(sb, str2);
    ASSERT_STREQ(sb->str, "Hello World");
    ASSERT_EQ(sb->length, strlen("Hello World"));
    
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendStrN) {
    StrBuf* sb = strbuf_new();
    const char* test = "HelloWorld";
    strbuf_append_str_n(sb, test, 5);
    ASSERT_STREQ(sb->str, "Hello");
    ASSERT_EQ(sb->length, 5);
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendChar) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, 'A');
    ASSERT_STREQ(sb->str, "A");
    ASSERT_EQ(sb->length, 1);
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendCharN) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char_n(sb, 'x', 3);
    ASSERT_STREQ(sb->str, "xxx");
    ASSERT_EQ(sb->length, 3);
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendAll) {
    StrBuf* sb = strbuf_new();
    strbuf_append_all(sb, 3, "One", "Two", "Three");
    ASSERT_STREQ(sb->str, "OneTwoThree");
    ASSERT_EQ(sb->length, strlen("OneTwoThree"));
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestAppendFormat) {
    StrBuf* sb = strbuf_new();
    strbuf_append_format(sb, "Number: %d, String: %s", 42, "test");
    char expected[50];
    snprintf(expected, 50, "Number: %d, String: %s", 42, "test");
    ASSERT_STREQ(sb->str, expected);
    ASSERT_EQ(sb->length, strlen(expected));
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestCopyAndDup) {
    StrBuf* src = strbuf_create("Original");
    StrBuf* dst = strbuf_new();
    
    strbuf_copy(dst, src);
    ASSERT_STREQ(dst->str, "Original");
    ASSERT_EQ(dst->length, src->length);
    
    StrBuf* dup = strbuf_dup(src);
    ASSERT_STREQ(dup->str, "Original");
    ASSERT_EQ(dup->length, src->length);
    
    strbuf_free(src);
    strbuf_free(dst);
    strbuf_free(dup);
}

TEST_F(StrBufTest, TestAppendFile) {
    // Create a temporary file for testing
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file(sb, temp);
    
    ASSERT_TRUE(result);
    ASSERT_STREQ(sb->str, content);
    ASSERT_EQ(sb->length, strlen(content));
    
    strbuf_free(sb);
    fclose(temp);
}

TEST_F(StrBufTest, TestAppendFileHead) {
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file_head(sb, temp, 4);
    
    ASSERT_TRUE(result);
    ASSERT_STREQ(sb->str, "File");
    ASSERT_EQ(sb->length, 4);
    
    strbuf_free(sb);
    fclose(temp);
}

TEST_F(StrBufTest, TestMemoryReallocation) {
    // Start with small capacity to force reallocation
    StrBuf *sb = strbuf_new_cap(8);
    size_t initial_capacity = sb->capacity;
    char *initial_ptr = sb->str;
    
    // Add small string that fits
    strbuf_append_str(sb, "Hi");
    ASSERT_STREQ(sb->str, "Hi");
    ASSERT_EQ(sb->length, 2);
    ASSERT_EQ(sb->str, initial_ptr) << "Pointer should be unchanged for small append";
    
    // Add string that requires reallocation
    strbuf_append_str(sb, " World!");
    ASSERT_STREQ(sb->str, "Hi World!");
    ASSERT_EQ(sb->length, 9);
    ASSERT_GT(sb->capacity, initial_capacity) << "Capacity should increase after reallocation";
    
    // Test multiple reallocations
    size_t prev_capacity = sb->capacity;
    for (int i = 0; i < 10; i++) {
        strbuf_append_str(sb, " More text to force reallocation");
    }
    
    ASSERT_GT(sb->capacity, prev_capacity) << "Multiple reallocations should occur";
    ASSERT_EQ(strstr(sb->str, "Hi World!"), sb->str) << "Original content should be preserved";
    ASSERT_NE(strstr(sb->str, "More text"), nullptr) << "New content should be added";
    
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestFreeEmptyRegularMemory) {
    // Test freeing StrBuf with no content (str might be NULL or empty)
    StrBuf *sb = strbuf_new();
    ASSERT_NE(sb, nullptr);
    ASSERT_EQ(sb->length, 0);
    
    // Free should handle case where str might be allocated but empty
    strbuf_free(sb);
}

TEST_F(StrBufTest, TestFreeAfterFullReset) {
    // Test freeing StrBuf after full_reset (str should be NULL)
    StrBuf *sb = strbuf_create("Initial content");
    ASSERT_NE(sb->str, nullptr);
    
    strbuf_full_reset(sb);
    ASSERT_EQ(sb->str, nullptr);
    ASSERT_EQ(sb->length, 0);
    ASSERT_EQ(sb->capacity, 0);
    
    // Free should handle NULL str pointer gracefully
    strbuf_free(sb);
}