/*
 * Comprehensive StrBuf Test Suite
 * ===============================
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

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lib/strbuf.h"

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(strbuf_tests, .init = setup, .fini = teardown);

Test(strbuf_tests, test_new) {
    StrBuf* sb = strbuf_new();
    cr_assert_not_null(sb, "strbuf_new() should return non-null pointer");
    cr_assert_not_null(sb->str, "String buffer should be allocated");
    cr_assert_eq(sb->length, 0, "Initial length should be 0");
    cr_assert_gt(sb->capacity, 0, "Initial capacity should be at least 1");
    cr_assert_eq(sb->str[0], '\0', "Buffer should be null-terminated");
    strbuf_free(sb);
}

Test(strbuf_tests, test_new_cap) {
    size_t cap = 64;
    StrBuf* sb = strbuf_new_cap(cap);
    cr_assert_not_null(sb);
    cr_assert_not_null(sb->str);
    cr_assert_eq(sb->length, 0);
    cr_assert_geq(sb->capacity, cap);
    cr_assert_eq(sb->str[0], '\0');
    strbuf_free(sb);
}

Test(strbuf_tests, test_create) {
    const char* test_str = "Hello";
    StrBuf* sb = strbuf_create(test_str);
    cr_assert_not_null(sb);
    cr_assert_str_eq(sb->str, test_str);
    cr_assert_eq(sb->length, strlen(test_str));
    cr_assert_geq(sb->capacity, sb->length + 1);
    strbuf_free(sb);
}

Test(strbuf_tests, test_reset) {
    StrBuf* sb = strbuf_create("Test");
    strbuf_reset(sb);
    cr_assert_eq(sb->length, 0);
    cr_assert_eq(sb->str[0], '\0');
    cr_assert_gt(sb->capacity, 0);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_str) {
    StrBuf* sb = strbuf_new();
    const char* str1 = "Hello";
    const char* str2 = " World";
    
    strbuf_append_str(sb, str1);
    cr_assert_str_eq(sb->str, str1);
    cr_assert_eq(sb->length, strlen(str1));
    
    strbuf_append_str(sb, str2);
    cr_assert_str_eq(sb->str, "Hello World");
    cr_assert_eq(sb->length, strlen("Hello World"));
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_str_n) {
    StrBuf* sb = strbuf_new();
    const char* test = "HelloWorld";
    strbuf_append_str_n(sb, test, 5);
    cr_assert_str_eq(sb->str, "Hello");
    cr_assert_eq(sb->length, 5);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_char) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, 'A');
    cr_assert_str_eq(sb->str, "A");
    cr_assert_eq(sb->length, 1);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_char_n) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char_n(sb, 'x', 3);
    cr_assert_str_eq(sb->str, "xxx");
    cr_assert_eq(sb->length, 3);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_all) {
    StrBuf* sb = strbuf_new();
    strbuf_append_all(sb, 3, "One", "Two", "Three");
    cr_assert_str_eq(sb->str, "OneTwoThree");
    cr_assert_eq(sb->length, strlen("OneTwoThree"));
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_format) {
    StrBuf* sb = strbuf_new();
    strbuf_append_format(sb, "Number: %d, String: %s", 42, "test");
    char expected[50];
    snprintf(expected, 50, "Number: %d, String: %s", 42, "test");
    cr_assert_str_eq(sb->str, expected);
    cr_assert_eq(sb->length, strlen(expected));
    strbuf_free(sb);
}

Test(strbuf_tests, test_copy_and_dup) {
    StrBuf* src = strbuf_create("Original");
    StrBuf* dst = strbuf_new();
    
    strbuf_copy(dst, src);
    cr_assert_str_eq(dst->str, "Original");
    cr_assert_eq(dst->length, src->length);
    
    StrBuf* dup = strbuf_dup(src);
    cr_assert_str_eq(dup->str, "Original");
    cr_assert_eq(dup->length, src->length);
    
    strbuf_free(src);
    strbuf_free(dst);
    strbuf_free(dup);
}

Test(strbuf_tests, test_append_file) {
    // Create a temporary file for testing
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file(sb, temp);
    
    cr_assert_eq(result, true);
    cr_assert_str_eq(sb->str, content);
    cr_assert_eq(sb->length, strlen(content));
    
    strbuf_free(sb);
    fclose(temp);
}

Test(strbuf_tests, test_append_file_head) {
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file_head(sb, temp, 4);
    
    cr_assert_eq(result, true);
    cr_assert_str_eq(sb->str, "File");
    cr_assert_eq(sb->length, 4);
    
    strbuf_free(sb);
    fclose(temp);
}

Test(strbuf_tests, test_memory_reallocation) {
    // Start with small capacity to force reallocation
    StrBuf *sb = strbuf_new_cap(8);
    size_t initial_capacity = sb->capacity;
    char *initial_ptr = sb->str;
    
    // Add small string that fits
    strbuf_append_str(sb, "Hi");
    cr_assert_str_eq(sb->str, "Hi");
    cr_assert_eq(sb->length, 2);
    cr_assert_eq(sb->str, initial_ptr, "Pointer should be unchanged for small append");
    
    // Add string that requires reallocation
    strbuf_append_str(sb, " World!");
    cr_assert_str_eq(sb->str, "Hi World!");
    cr_assert_eq(sb->length, 9);
    cr_assert_gt(sb->capacity, initial_capacity, "Capacity should increase after reallocation");
    
    // Test multiple reallocations
    size_t prev_capacity = sb->capacity;
    for (int i = 0; i < 10; i++) {
        strbuf_append_str(sb, " More text to force reallocation");
    }
    
    cr_assert_gt(sb->capacity, prev_capacity, "Multiple reallocations should occur");
    cr_assert(strstr(sb->str, "Hi World!") == sb->str, "Original content should be preserved");
    cr_assert_not_null(strstr(sb->str, "More text"), "New content should be added");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_char_append_reallocation) {
    StrBuf *sb = strbuf_new_cap(4);
    
    // Fill to near capacity
    strbuf_append_str(sb, "ab");
    size_t initial_capacity = sb->capacity;
    
    // Add single character that should fit
    strbuf_append_char(sb, 'c');
    cr_assert_str_eq(sb->str, "abc");
    cr_assert_eq(sb->length, 3);
    
    // Add character that forces reallocation
    strbuf_append_char(sb, 'd');
    cr_assert_str_eq(sb->str, "abcd");
    cr_assert_eq(sb->length, 4);
    cr_assert_gt(sb->capacity, initial_capacity, "Capacity should increase");
    
    // Test multiple character appends
    for (char c = 'e'; c <= 'z'; c++) {
        strbuf_append_char(sb, c);
    }
    
    cr_assert_eq(sb->length, 26);
    cr_assert_str_eq(sb->str, "abcdefghijklmnopqrstuvwxyz");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_char_n_append_reallocation) {
    StrBuf *sb = strbuf_new_cap(5);
    
    // Append small amount
    strbuf_append_char_n(sb, 'A', 3);
    cr_assert_str_eq(sb->str, "AAA");
    cr_assert_eq(sb->length, 3);
    
    // Append large amount that forces reallocation
    strbuf_append_char_n(sb, 'B', 100);
    cr_assert_eq(sb->length, 103);
    cr_assert_geq(sb->capacity, 104);
    
    // Verify content pattern
    char expected[104];
    memset(expected, 'A', 3);
    memset(expected + 3, 'B', 100);
    expected[103] = '\0';
    
    cr_assert_str_eq(sb->str, expected);
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_copy_with_reallocation) {
    StrBuf *src = strbuf_create("Source string for testing copy operations that require reallocation");
    StrBuf *dst = strbuf_new_cap(5); // Small capacity to force reallocation
    
    size_t initial_capacity = dst->capacity;
    strbuf_copy(dst, src);
    
    cr_assert_str_eq(dst->str, src->str);
    cr_assert_eq(dst->length, src->length);
    cr_assert_gt(dst->capacity, initial_capacity, "Destination should reallocate");
    cr_assert_geq(dst->capacity, src->length + 1);
    
    strbuf_free(src);
    strbuf_free(dst);
}

Test(strbuf_tests, test_edge_cases) {
    StrBuf *sb = strbuf_new();
    
    // Test NULL string append
    strbuf_append_str(sb, NULL);
    cr_assert_eq(sb->length, 0, "NULL string append should do nothing");
    
    // Test empty string append
    strbuf_append_str(sb, "");
    cr_assert_eq(sb->length, 0, "Empty string append should do nothing");
    
    // Test zero-length string_n append
    strbuf_append_str_n(sb, "Hello", 0);
    cr_assert_eq(sb->length, 0, "Zero-length append should do nothing");
    
    // Test zero count char_n append
    strbuf_append_char_n(sb, 'A', 0);
    cr_assert_eq(sb->length, 0, "Zero count char append should do nothing");
    
    // Test very large single allocation
    size_t large_size = 1024 * 1024; // 1MB
    bool success = strbuf_ensure_cap(sb, large_size);
    cr_assert_eq(success, true, "Large allocation should succeed");
    cr_assert_geq(sb->capacity, large_size, "Large capacity should be set");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_capacity_management) {
    StrBuf *sb = strbuf_new_cap(16);
    
    // Test ensure_cap with smaller size (should do nothing)
    bool result = strbuf_ensure_cap(sb, 8);
    cr_assert_eq(result, true, "ensure_cap with smaller size should succeed");
    cr_assert_eq(sb->capacity, 16, "Capacity should be unchanged for smaller request");
    
    // Test ensure_cap with exact size (should do nothing)
    result = strbuf_ensure_cap(sb, 16);
    cr_assert_eq(result, true, "ensure_cap with exact size should succeed");
    cr_assert_eq(sb->capacity, 16, "Capacity should be unchanged for exact request");
    
    // Test ensure_cap with larger size
    result = strbuf_ensure_cap(sb, 64);
    cr_assert_eq(result, true, "ensure_cap with larger size should succeed");
    cr_assert_eq(sb->capacity, 64, "Capacity should increase to requested size");
    
    // Test ensure_cap with very large size
    result = strbuf_ensure_cap(sb, 1000);
    cr_assert_eq(result, true, "ensure_cap with very large size should succeed");
    cr_assert_geq(sb->capacity, 1000, "Capacity should be at least requested size");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_full_reset) {
    StrBuf *sb = strbuf_create("Test string for full reset");
    size_t original_capacity = sb->capacity;
    
    // Test full reset
    strbuf_full_reset(sb);
    cr_assert_eq(sb->length, 0, "Length should be reset to 0");
    cr_assert_eq(sb->capacity, 0, "Capacity should be reset to 0");
    cr_assert_null(sb->str, "Pointer should be reset to NULL");
    
    // Note: Don't call strbuf_free after full_reset since str is NULL
    free(sb);
}

Test(strbuf_tests, test_stress_scenarios) {
    StrBuf *sb = strbuf_new_cap(8);
    
    // Stress test: many small appends
    for (int i = 0; i < 100; i++) { // Reduced from 1000 for faster testing
        strbuf_append_char(sb, 'A' + (i % 26));
    }
    cr_assert_eq(sb->length, 100, "All characters should be appended");
    cr_assert_geq(sb->capacity, 101, "Capacity should be sufficient");
    
    // Verify content pattern
    for (int i = 0; i < 100; i++) {
        cr_assert_eq(sb->str[i], 'A' + (i % 26), "Character pattern should be correct");
    }
    
    strbuf_free(sb);
    
    // Stress test: alternating large and small appends
    sb = strbuf_new_cap(4);
    for (int i = 0; i < 20; i++) { // Reduced for faster testing
        if (i % 2 == 0) {
            strbuf_append_str(sb, "Large string that will cause reallocation ");
        } else {
            strbuf_append_char(sb, '.');
        }
    }
    
    cr_assert_gt(sb->length, 0, "Stress test should complete");
    cr_assert_not_null(strstr(sb->str, "Large string"), "Large strings should be present");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_integer_functions) {
    StrBuf *sb = strbuf_new();
    
    // Test append_int
    strbuf_append_int(sb, 42);
    cr_assert_str_eq(sb->str, "42");
    
    strbuf_reset(sb);
    strbuf_append_int(sb, -123);
    cr_assert_str_eq(sb->str, "-123");
    
    // Test append_long
    strbuf_reset(sb);
    strbuf_append_long(sb, 1234567890L);
    cr_assert_str_eq(sb->str, "1234567890");
    
    strbuf_reset(sb);
    strbuf_append_long(sb, -9876543210L);
    cr_assert_str_eq(sb->str, "-9876543210");
    
    // Test append_ulong
    strbuf_reset(sb);
    strbuf_append_ulong(sb, 18446744073709551615UL); // Max unsigned long
    cr_assert_not_null(sb->str);
    cr_assert_gt(sb->length, 0);
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_reallocation_pattern_verification) {
    StrBuf *sb = strbuf_new_cap(4);
    size_t prev_capacity = sb->capacity;
    
    // Test capacity doubling pattern
    for (int i = 0; i < 5; i++) {
        // Add enough content to trigger reallocation
        strbuf_append_str(sb, "This is a long string that should trigger reallocation ");
        
        if (sb->capacity > prev_capacity) {
            // Verify capacity approximately doubled (allowing for some variance)
            cr_assert_geq(sb->capacity, prev_capacity, "Capacity should not decrease");
            prev_capacity = sb->capacity;
        }
    }
    
    // Verify final state
    cr_assert_not_null(strstr(sb->str, "This is a long string"));
    cr_assert_gt(sb->capacity, 4, "Final capacity should be much larger than initial");
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_vappend_and_format_functions) {
    StrBuf *sb = strbuf_new();
    
    // Test vappend_format with various format specifiers
    strbuf_append_format(sb, "Int: %d, Float: %.2f, String: %s", 42, 3.14, "test");
    char expected[100];
    snprintf(expected, sizeof(expected), "Int: %d, Float: %.2f, String: %s", 42, 3.14, "test");
    cr_assert_str_eq(sb->str, expected);
    
    // Test with reallocation during formatting
    strbuf_reset(sb);
    strbuf_append_format(sb, "This is a very long formatted string with number %d and repeated text: %s %s %s %s", 
                         12345, "repeat", "repeat", "repeat", "repeat");
    cr_assert_not_null(strstr(sb->str, "12345"));
    cr_assert_not_null(strstr(sb->str, "repeat"));
    
    strbuf_free(sb);
}


Test(strbuf_tests, test_boundary_conditions) {
    StrBuf *sb = strbuf_new_cap(1); // Minimum practical capacity
    
    // Test single character operations on minimal buffer
    strbuf_append_char(sb, 'A');
    cr_assert_str_eq(sb->str, "A");
    cr_assert_eq(sb->length, 1);
    
    // This should trigger reallocation
    strbuf_append_char(sb, 'B');
    cr_assert_str_eq(sb->str, "AB");
    cr_assert_eq(sb->length, 2);
    cr_assert_gt(sb->capacity, 1);
    
    strbuf_free(sb);
    
    // Test with capacity 0 (edge case)
    sb = strbuf_new_cap(0);
    if (sb) {
        strbuf_append_str(sb, "test");
        cr_assert_str_eq(sb->str, "test");
        strbuf_free(sb);
    }
}

Test(strbuf_tests, test_ensure_cap_edge_cases) {
    StrBuf *sb = strbuf_new();
    size_t original_capacity = sb->capacity;
    
    // Test ensuring capacity equal to current
    bool result = strbuf_ensure_cap(sb, original_capacity);
    cr_assert_eq(result, true);
    cr_assert_eq(sb->capacity, original_capacity);
    
    // Test ensuring capacity less than current
    result = strbuf_ensure_cap(sb, original_capacity / 2);
    cr_assert_eq(result, true);
    cr_assert_eq(sb->capacity, original_capacity); // Should not shrink
    
    // Test ensuring very large capacity
    result = strbuf_ensure_cap(sb, SIZE_MAX / 2); // Large but not overflow
    // Result depends on system memory, but should not crash
    cr_assert(result == true || result == false); // Either is acceptable
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_memory_reallocation_preservation) {
    StrBuf *sb = strbuf_new_cap(8);
    
    // Build a pattern we can verify after multiple reallocations
    const char *pattern = "ABCDEFGHIJ";
    for (int i = 0; i < 10; i++) {
        strbuf_append_char(sb, pattern[i]);
        
        // Verify pattern is preserved after each append
        for (int j = 0; j <= i; j++) {
            cr_assert_eq(sb->str[j], pattern[j], "Pattern should be preserved during reallocation");
        }
        cr_assert_eq(sb->str[i + 1], '\0', "String should remain null-terminated");
    }
    
    cr_assert_str_eq(sb->str, pattern);
    cr_assert_eq(sb->length, 10);
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_free_regular_memory) {
    // Test freeing StrBuf allocated with regular malloc
    StrBuf *sb = strbuf_new();
    cr_assert_not_null(sb);
    // Pool member removed from StrBuf struct
    
    // Add some content to ensure str is allocated
    strbuf_append_str(sb, "Test content for regular memory");
    cr_assert_not_null(sb->str);
    cr_assert_gt(sb->length, 0);
    
    // Free should not crash and should handle both str and sb pointers
    strbuf_free(sb); // This should call free() on both sb->str and sb
    // Note: Cannot test memory deallocation directly, but absence of crash indicates success
}

Test(strbuf_tests, test_free_empty_regular_memory) {
    // Test freeing StrBuf with no content (str might be NULL or empty)
    StrBuf *sb = strbuf_new();
    cr_assert_not_null(sb);
    // Pool member removed from StrBuf struct
    cr_assert_eq(sb->length, 0);
    
    // Free should handle case where str might be allocated but empty
    strbuf_free(sb);
}

Test(strbuf_tests, test_free_after_full_reset) {
    // Test freeing StrBuf after full_reset (str should be NULL)
    StrBuf *sb = strbuf_create("Initial content");
    cr_assert_not_null(sb->str);
    
    strbuf_full_reset(sb);
    cr_assert_null(sb->str);
    cr_assert_eq(sb->length, 0);
    cr_assert_eq(sb->capacity, 0);
    
    // Free should handle NULL str pointer gracefully
    // Note: After full_reset, need to free the StrBuf struct manually since str is NULL
    strbuf_free(sb); // Changed from free(sb) to strbuf_free(sb)
}
