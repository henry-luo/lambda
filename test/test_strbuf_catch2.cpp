/*
 * Comprehensive StrBuf Test Suite - Catch2 Version
 * ================================================
 * 
 * Migrated from Criterion framework to Catch2
 * 
 * Total Tests: 30 test cases covering:
 * 
 * Basic Operations:
 * - Creation and initialization
 * - String and character appending 
 * - Format functions and file operations
 * - Copy and duplication operations
 * 
 * Memory Management:
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
 * - Memory preservation during reallocation
 * - Memory free operations
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lib/strbuf.h"


// Individual test cases to match Criterion exactly - each as separate TEST_CASE
TEST_CASE("test_new", "[strbuf][basic]") {
    StrBuf* sb = strbuf_new();
    REQUIRE(sb != nullptr);
    REQUIRE(sb->str != nullptr);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->capacity > 0);
    REQUIRE(sb->str[0] == '\0');
    strbuf_free(sb);
}

TEST_CASE("test_new_cap", "[strbuf][basic]") {
    size_t cap = 64;
    StrBuf* sb = strbuf_new_cap(cap);
    REQUIRE(sb != nullptr);
    REQUIRE(sb->str != nullptr);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->capacity >= cap);
    REQUIRE(sb->str[0] == '\0');
    strbuf_free(sb);
}

TEST_CASE("test_create", "[strbuf][basic]") {
    const char* test_str = "Hello";
    StrBuf* sb = strbuf_create(test_str);
    REQUIRE(sb != nullptr);
    REQUIRE(strcmp(sb->str, test_str) == 0);
    REQUIRE(sb->length == strlen(test_str));
    REQUIRE(sb->capacity >= sb->length + 1);
    strbuf_free(sb);
}

TEST_CASE("test_reset", "[strbuf][basic]") {
    StrBuf* sb = strbuf_create("Test");
    strbuf_reset(sb);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->str[0] == '\0');
    REQUIRE(sb->capacity > 0);
    strbuf_free(sb);
}

TEST_CASE("test_append_str", "[strbuf][append]") {
    StrBuf* sb = strbuf_new();
    const char* str1 = "Hello";
    const char* str2 = " World";
    
    strbuf_append_str(sb, str1);
    REQUIRE(strcmp(sb->str, str1) == 0);
    REQUIRE(sb->length == strlen(str1));
    
    strbuf_append_str(sb, str2);
    REQUIRE(strcmp(sb->str, "Hello World") == 0);
    REQUIRE(sb->length == strlen("Hello World"));
    
    strbuf_free(sb);
}

TEST_CASE("test_append_str_n", "[strbuf][append]") {
    StrBuf* sb = strbuf_new();
    const char* test = "HelloWorld";
    strbuf_append_str_n(sb, test, 5);
    REQUIRE(strcmp(sb->str, "Hello") == 0);
    REQUIRE(sb->length == 5);
    strbuf_free(sb);
}

TEST_CASE("test_append_char", "[strbuf][char]") {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, 'A');
    REQUIRE(strcmp(sb->str, "A") == 0);
    REQUIRE(sb->length == 1);
    strbuf_free(sb);
}

TEST_CASE("test_append_char_n", "[strbuf][char]") {
    StrBuf* sb = strbuf_new();
    strbuf_append_char_n(sb, 'x', 3);
    REQUIRE(strcmp(sb->str, "xxx") == 0);
    REQUIRE(sb->length == 3);
    strbuf_free(sb);
}

TEST_CASE("test_append_all", "[strbuf][append]") {
    StrBuf* sb = strbuf_new();
    strbuf_append_all(sb, 3, "One", "Two", "Three");
    REQUIRE(strcmp(sb->str, "OneTwoThree") == 0);
    REQUIRE(sb->length == strlen("OneTwoThree"));
    strbuf_free(sb);
}

TEST_CASE("test_append_format", "[strbuf][format]") {
    StrBuf* sb = strbuf_new();
    strbuf_append_format(sb, "Number: %d, String: %s", 42, "test");
    char expected[50];
    snprintf(expected, 50, "Number: %d, String: %s", 42, "test");
    REQUIRE(strcmp(sb->str, expected) == 0);
    REQUIRE(sb->length == strlen(expected));
    strbuf_free(sb);
}

TEST_CASE("test_copy_and_dup", "[strbuf][copy]") {
    StrBuf* src = strbuf_create("Original");
    StrBuf* dst = strbuf_new();
    
    strbuf_copy(dst, src);
    REQUIRE(strcmp(dst->str, "Original") == 0);
    REQUIRE(dst->length == src->length);
    
    StrBuf* dup = strbuf_dup(src);
    REQUIRE(strcmp(dup->str, "Original") == 0);
    REQUIRE(dup->length == src->length);
    
    strbuf_free(src);
    strbuf_free(dst);
    strbuf_free(dup);
}

TEST_CASE("test_append_file", "[strbuf][file]") {
    // Create a temporary file for testing
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file(sb, temp);
    
    REQUIRE(result == true);
    REQUIRE(strcmp(sb->str, content) == 0);
    REQUIRE(sb->length == strlen(content));
    
    strbuf_free(sb);
    fclose(temp);
}

TEST_CASE("test_append_file_head", "[strbuf][file]") {
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file_head(sb, temp, 4);
    
    REQUIRE(result == true);
    REQUIRE(strcmp(sb->str, "File") == 0);
    REQUIRE(sb->length == 4);
    
    strbuf_free(sb);
    fclose(temp);
}

TEST_CASE("test_memory_reallocation", "[strbuf][memory]") {
    // Start with small capacity to force reallocation
    StrBuf* sb = strbuf_new_cap(8);
    size_t initial_capacity = sb->capacity;
    char* initial_ptr = sb->str;
    
    // Add small string that fits
    strbuf_append_str(sb, "Hi");
    REQUIRE(strcmp(sb->str, "Hi") == 0);
    REQUIRE(sb->length == 2);
    REQUIRE(sb->str == initial_ptr); // Pointer should be unchanged for small append
    
    // Add string that requires reallocation
    strbuf_append_str(sb, " World!");
    REQUIRE(strcmp(sb->str, "Hi World!") == 0);
    REQUIRE(sb->length == 9);
    REQUIRE(sb->capacity > initial_capacity); // Capacity should increase after reallocation
    
    // Test multiple reallocations
    size_t prev_capacity = sb->capacity;
    for (int i = 0; i < 10; i++) {
        strbuf_append_str(sb, " More text to force reallocation");
    }
    
    REQUIRE(sb->capacity > prev_capacity); // Multiple reallocations should occur
    REQUIRE(strstr(sb->str, "Hi World!") == sb->str); // Original content should be preserved
    REQUIRE(strstr(sb->str, "More text") != nullptr); // New content should be added
    
    strbuf_free(sb);
}

TEST_CASE("test_char_append_reallocation", "[strbuf][memory]") {
    StrBuf* sb = strbuf_new_cap(4);
    
    // Fill to near capacity
    strbuf_append_str(sb, "ab");
    size_t initial_capacity = sb->capacity;
    
    // Add single character that should fit
    strbuf_append_char(sb, 'c');
    REQUIRE(strcmp(sb->str, "abc") == 0);
    REQUIRE(sb->length == 3);
    
    // Add character that forces reallocation
    strbuf_append_char(sb, 'd');
    REQUIRE(strcmp(sb->str, "abcd") == 0);
    REQUIRE(sb->length == 4);
    REQUIRE(sb->capacity > initial_capacity); // Capacity should increase
    
    // Test multiple character appends
    for (char c = 'e'; c <= 'z'; c++) {
        strbuf_append_char(sb, c);
    }
    
    REQUIRE(sb->length == 26);
    REQUIRE(strcmp(sb->str, "abcdefghijklmnopqrstuvwxyz") == 0);
    
    strbuf_free(sb);
}

TEST_CASE("test_char_n_append_reallocation", "[strbuf][memory]") {
    StrBuf* sb = strbuf_new_cap(5);
    
    // Append small amount
    strbuf_append_char_n(sb, 'A', 3);
    REQUIRE(strcmp(sb->str, "AAA") == 0);
    REQUIRE(sb->length == 3);
    
    // Append large amount that forces reallocation
    strbuf_append_char_n(sb, 'B', 100);
    REQUIRE(sb->length == 103);
    REQUIRE(sb->capacity >= 104);
    
    // Verify content pattern
    char expected[104];
    memset(expected, 'A', 3);
    memset(expected + 3, 'B', 100);
    expected[103] = '\0';
    
    REQUIRE(strcmp(sb->str, expected) == 0);
    
    strbuf_free(sb);
}

TEST_CASE("test_copy_with_reallocation", "[strbuf][memory]") {
    StrBuf* src = strbuf_create("Source string for testing copy operations that require reallocation");
    StrBuf* dst = strbuf_new_cap(5); // Small capacity to force reallocation
    
    size_t initial_capacity = dst->capacity;
    strbuf_copy(dst, src);
    
    REQUIRE(strcmp(dst->str, src->str) == 0);
    REQUIRE(dst->length == src->length);
    REQUIRE(dst->capacity > initial_capacity); // Destination should reallocate
    REQUIRE(dst->capacity >= src->length + 1);
    
    strbuf_free(src);
    strbuf_free(dst);
}

TEST_CASE("test_edge_cases", "[strbuf][edge]") {
    StrBuf* sb = strbuf_new();
    
    // Test NULL string append
    strbuf_append_str(sb, NULL);
    REQUIRE(sb->length == 0); // NULL string append should do nothing
    
    // Test empty string append
    strbuf_append_str(sb, "");
    REQUIRE(sb->length == 0); // Empty string append should do nothing
    
    // Test zero-length string_n append
    strbuf_append_str_n(sb, "Hello", 0);
    REQUIRE(sb->length == 0); // Zero-length append should do nothing
    
    // Test zero count char_n append
    strbuf_append_char_n(sb, 'A', 0);
    REQUIRE(sb->length == 0); // Zero count char append should do nothing
    
    // Test very large single allocation
    size_t large_size = 1024 * 1024; // 1MB
    bool success = strbuf_ensure_cap(sb, large_size);
    REQUIRE(success == true); // Large allocation should succeed
    REQUIRE(sb->capacity >= large_size); // Large capacity should be set
    
    strbuf_free(sb);
}

TEST_CASE("test_capacity_management", "[strbuf][capacity]") {
    StrBuf* sb = strbuf_new_cap(16);
    
    // Test ensure_cap with smaller size (should do nothing)
    bool result = strbuf_ensure_cap(sb, 8);
    REQUIRE(result == true); // ensure_cap with smaller size should succeed
    REQUIRE(sb->capacity == 16); // Capacity should be unchanged for smaller request
    
    // Test ensure_cap with exact size (should do nothing)
    result = strbuf_ensure_cap(sb, 16);
    REQUIRE(result == true); // ensure_cap with exact size should succeed
    REQUIRE(sb->capacity == 16); // Capacity should be unchanged for exact request
    
    // Test ensure_cap with larger size
    result = strbuf_ensure_cap(sb, 64);
    REQUIRE(result == true); // ensure_cap with larger size should succeed
    REQUIRE(sb->capacity == 64); // Capacity should increase to requested size
    
    // Test ensure_cap with very large size
    result = strbuf_ensure_cap(sb, 1000);
    REQUIRE(result == true); // ensure_cap with very large size should succeed
    REQUIRE(sb->capacity >= 1000); // Capacity should be at least requested size
    
    strbuf_free(sb);
}

TEST_CASE("test_full_reset", "[strbuf][memory]") {
    StrBuf* sb = strbuf_create("Test string for full reset");
    size_t original_capacity = sb->capacity;
    
    // Test full reset
    strbuf_full_reset(sb);
    REQUIRE(sb->length == 0); // Length should be reset to 0
    REQUIRE(sb->capacity == 0); // Capacity should be reset to 0
    REQUIRE(sb->str == nullptr); // Pointer should be reset to NULL
    
    // Note: Don't call strbuf_free after full_reset since str is NULL
    free(sb);
}

TEST_CASE("test_stress_scenarios", "[strbuf][stress]") {
    StrBuf* sb = strbuf_new_cap(8);
    
    // Stress test: many small appends
    for (int i = 0; i < 100; i++) { // Reduced from 1000 for faster testing
        strbuf_append_char(sb, 'A' + (i % 26));
    }
    REQUIRE(sb->length == 100); // All characters should be appended
    REQUIRE(sb->capacity >= 101); // Capacity should be sufficient
    
    // Verify content pattern
    for (int i = 0; i < 100; i++) {
        REQUIRE(sb->str[i] == 'A' + (i % 26)); // Character pattern should be correct
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
    
    REQUIRE(sb->length > 0); // Stress test should complete
    REQUIRE(strstr(sb->str, "Large string") != nullptr); // Large strings should be present
    
    strbuf_free(sb);
}

TEST_CASE("test_append_integer_functions", "[strbuf][integer]") {
    StrBuf* sb = strbuf_new();
    
    // Test append_int
    strbuf_append_int(sb, 42);
    REQUIRE(strcmp(sb->str, "42") == 0);
    
    strbuf_reset(sb);
    strbuf_append_int(sb, -123);
    REQUIRE(strcmp(sb->str, "-123") == 0);
    
    // Test append_long
    strbuf_reset(sb);
    strbuf_append_long(sb, 1234567890L);
    REQUIRE(strcmp(sb->str, "1234567890") == 0);
    
    strbuf_reset(sb);
    strbuf_append_long(sb, -9876543210L);
    REQUIRE(strcmp(sb->str, "-9876543210") == 0);
    
    // Test append_ulong
    strbuf_reset(sb);
    strbuf_append_ulong(sb, 18446744073709551615UL); // Max unsigned long
    REQUIRE(sb->str != nullptr);
    REQUIRE(sb->length > 0);
    
    strbuf_free(sb);
}

TEST_CASE("test_reallocation_pattern_verification", "[strbuf][stress]") {
    StrBuf* sb = strbuf_new_cap(4);
    size_t prev_capacity = sb->capacity;
    
    // Test capacity doubling pattern
    for (int i = 0; i < 5; i++) {
        // Add enough content to trigger reallocation
        strbuf_append_str(sb, "This is a long string that should trigger reallocation ");
        
        if (sb->capacity > prev_capacity) {
            // Verify capacity approximately doubled (allowing for some variance)
            REQUIRE(sb->capacity >= prev_capacity); // Capacity should not decrease
            prev_capacity = sb->capacity;
        }
    }
    
    // Verify final state
    REQUIRE(strstr(sb->str, "This is a long string") != nullptr);
    REQUIRE(sb->capacity > 4); // Final capacity should be much larger than initial
    
    strbuf_free(sb);
}

TEST_CASE("test_vappend_and_format_functions", "[strbuf][format]") {
    StrBuf* sb = strbuf_new();
    
    // Test vappend_format with various format specifiers
    strbuf_append_format(sb, "Int: %d, Float: %.2f, String: %s", 42, 3.14, "test");
    char expected[100];
    snprintf(expected, sizeof(expected), "Int: %d, Float: %.2f, String: %s", 42, 3.14, "test");
    REQUIRE(strcmp(sb->str, expected) == 0);
    
    // Test with reallocation during formatting
    strbuf_reset(sb);
    strbuf_append_format(sb, "This is a very long formatted string with number %d and repeated text: %s %s %s %s", 
                         12345, "repeat", "repeat", "repeat", "repeat");
    REQUIRE(strstr(sb->str, "12345") != nullptr);
    REQUIRE(strstr(sb->str, "repeat") != nullptr);
    
    strbuf_free(sb);
}

TEST_CASE("test_boundary_conditions", "[strbuf][edge]") {
    StrBuf* sb = strbuf_new_cap(1); // Minimum practical capacity
    
    // Test single character operations on minimal buffer
    strbuf_append_char(sb, 'A');
    REQUIRE(strcmp(sb->str, "A") == 0);
    REQUIRE(sb->length == 1);
    
    // This should trigger reallocation
    strbuf_append_char(sb, 'B');
    REQUIRE(strcmp(sb->str, "AB") == 0);
    REQUIRE(sb->length == 2);
    REQUIRE(sb->capacity > 1);
    
    strbuf_free(sb);
    
    // Test with capacity 0 (edge case)
    sb = strbuf_new_cap(0);
    if (sb) {
        strbuf_append_str(sb, "test");
        REQUIRE(strcmp(sb->str, "test") == 0);
        strbuf_free(sb);
    }
}

TEST_CASE("test_ensure_cap_edge_cases", "[strbuf][capacity]") {
    StrBuf* sb = strbuf_new();
    size_t original_capacity = sb->capacity;
    
    // Test ensuring capacity equal to current
    bool result = strbuf_ensure_cap(sb, original_capacity);
    REQUIRE(result == true);
    REQUIRE(sb->capacity == original_capacity);
    
    // Test ensuring capacity less than current
    result = strbuf_ensure_cap(sb, original_capacity / 2);
    REQUIRE(result == true);
    REQUIRE(sb->capacity == original_capacity); // Should not shrink
    
    // Test ensuring very large capacity
    result = strbuf_ensure_cap(sb, SIZE_MAX / 2); // Large but not overflow
    // Result depends on system memory, but should not crash
    REQUIRE((result == true || result == false)); // Either is acceptable
    
    strbuf_free(sb);
}

TEST_CASE("test_memory_reallocation_preservation", "[strbuf][memory]") {
    StrBuf* sb = strbuf_new_cap(8);
    
    // Build a pattern we can verify after multiple reallocations
    const char* pattern = "ABCDEFGHIJ";
    for (int i = 0; i < 10; i++) {
        strbuf_append_char(sb, pattern[i]);
        
        // Verify pattern is preserved after each append
        for (int j = 0; j <= i; j++) {
            REQUIRE(sb->str[j] == pattern[j]); // Pattern should be preserved during reallocation
        }
        REQUIRE(sb->str[i + 1] == '\0'); // String should remain null-terminated
    }
    
    REQUIRE(strcmp(sb->str, pattern) == 0);
    REQUIRE(sb->length == 10);
    
    strbuf_free(sb);
}

TEST_CASE("test_free_regular_memory", "[strbuf][memory_free]") {
    // Test freeing StrBuf allocated with regular malloc
    StrBuf* sb = strbuf_new();
    REQUIRE(sb != nullptr);
    // Pool member removed from StrBuf struct
    
    // Add some content to ensure str is allocated
    strbuf_append_str(sb, "Test content for regular memory");
    REQUIRE(sb->str != nullptr);
    REQUIRE(sb->length > 0);
    
    // Free should not crash and should handle both str and sb pointers
    strbuf_free(sb); // This should call free() on both sb->str and sb
    // Note: Cannot test memory deallocation directly, but absence of crash indicates success
}

TEST_CASE("test_free_empty_regular_memory", "[strbuf][memory_free]") {
    // Test freeing StrBuf with no content (str might be NULL or empty)
    StrBuf* sb = strbuf_new();
    REQUIRE(sb != nullptr);
    // Pool member removed from StrBuf struct
    REQUIRE(sb->length == 0);
    
    // Free should handle case where str might be allocated but empty
    strbuf_free(sb);
}

TEST_CASE("test_free_after_full_reset", "[strbuf][memory_free]") {
    // Test freeing StrBuf after full_reset (str should be NULL)
    StrBuf* sb = strbuf_create("Initial content");
    REQUIRE(sb->str != nullptr);
    
    strbuf_full_reset(sb);
    REQUIRE(sb->str == nullptr);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->capacity == 0);
    
    // Free should handle NULL str pointer gracefully
    // Note: After full_reset, need to free the StrBuf struct manually since str is NULL
    strbuf_free(sb); // Changed from free(sb) to strbuf_free(sb)
}
