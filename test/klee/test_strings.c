/**
 * @file test_strings.c
 * @brief KLEE test harness for string operations in Lambda Script
 * @author Henry Luo
 * 
 * This test harness uses KLEE to discover buffer overflows, null pointer
 * dereferences, and other string-related vulnerabilities.
 */

#include <klee/klee.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

// Mock Lambda String structure for testing
typedef struct TestString {
    uint32_t len;
    uint32_t ref_cnt;
    char chars[128]; // Fixed size for testing
} TestString;

// Mock heap allocation for testing
TestString* test_heap_alloc(size_t size) {
    if (size > sizeof(TestString)) {
        return NULL; // Allocation too large
    }
    return (TestString*)malloc(sizeof(TestString));
}

// Instrumented string concatenation function
TestString* test_strcat(TestString* left, TestString* right) {
    // Null pointer checks - KLEE will explore NULL cases
    if (!left) {
        klee_assert(0); // Null left pointer
        return NULL;
    }
    
    if (!right) {
        klee_assert(0); // Null right pointer  
        return NULL;
    }
    
    uint32_t left_len = left->len;
    uint32_t right_len = right->len;
    
    // Check for length field consistency
    if (left_len > 127) { // Our test buffer is 128 chars
        klee_assert(0); // Invalid left length
        return NULL;
    }
    
    if (right_len > 127) {
        klee_assert(0); // Invalid right length
        return NULL;
    }
    
    // Check for integer overflow in length calculation
    if (left_len > UINT32_MAX - right_len) {
        klee_assert(0); // Length overflow
        return NULL;
    }
    
    uint32_t total_len = left_len + right_len;
    
    // Check if result would fit in our test buffer
    if (total_len >= 127) { // Leave space for null terminator
        klee_assert(0); // Result too large for buffer
        return NULL;
    }
    
    // Allocate result string
    TestString* result = test_heap_alloc(sizeof(TestString));
    if (!result) {
        klee_assert(0); // Allocation failure
        return NULL;
    }
    
    result->len = total_len;
    result->ref_cnt = 1;
    
    // Ensure source strings are null-terminated within their declared length
    // This checks for buffer over-read vulnerabilities
    for (uint32_t i = 0; i < left_len; i++) {
        if (left->chars[i] == '\0' && i < left_len - 1) {
            klee_assert(0); // Premature null terminator in left string
            free(result);
            return NULL;
        }
    }
    
    for (uint32_t i = 0; i < right_len; i++) {
        if (right->chars[i] == '\0' && i < right_len - 1) {
            klee_assert(0); // Premature null terminator in right string
            free(result);
            return NULL;
        }
    }
    
    // Safe copy operations with explicit bounds checking
    memcpy(result->chars, left->chars, left_len);
    memcpy(result->chars + left_len, right->chars, right_len);
    result->chars[total_len] = '\0';
    
    return result;
}

// Test string length function with bounds checking
uint32_t test_strlen_safe(TestString* str) {
    if (!str) {
        klee_assert(0); // Null pointer
        return 0;
    }
    
    // Verify length field matches actual string length
    uint32_t declared_len = str->len;
    if (declared_len > 127) {
        klee_assert(0); // Invalid declared length
        return 0;
    }
    
    // Check that the string is properly null-terminated
    if (str->chars[declared_len] != '\0') {
        klee_assert(0); // Missing null terminator
        return 0;
    }
    
    // Verify no embedded nulls before the end
    for (uint32_t i = 0; i < declared_len; i++) {
        if (str->chars[i] == '\0') {
            klee_assert(0); // Embedded null character
            return 0;
        }
    }
    
    return declared_len;
}

// Test string comparison with buffer over-read protection
int test_strcmp_safe(TestString* str1, TestString* str2) {
    if (!str1 || !str2) {
        klee_assert(0); // Null pointer in comparison
        return -2; // Error indicator
    }
    
    uint32_t len1 = str1->len;
    uint32_t len2 = str2->len;
    
    if (len1 > 127 || len2 > 127) {
        klee_assert(0); // Invalid string length
        return -2;
    }
    
    // Use safe bounded comparison
    uint32_t min_len = (len1 < len2) ? len1 : len2;
    
    for (uint32_t i = 0; i < min_len; i++) {
        if (str1->chars[i] < str2->chars[i]) return -1;
        if (str1->chars[i] > str2->chars[i]) return 1;
    }
    
    // If prefixes are equal, compare lengths
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

// Test string repeat function (potential for massive memory allocation)
TestString* test_str_repeat(TestString* str, uint32_t times) {
    if (!str) {
        klee_assert(0); // Null string pointer
        return NULL;
    }
    
    uint32_t str_len = str->len;
    
    if (str_len > 127) {
        klee_assert(0); // Invalid string length
        return NULL;
    }
    
    // Check for multiplication overflow
    if (times > 0 && str_len > UINT32_MAX / times) {
        klee_assert(0); // Multiplication overflow
        return NULL;
    }
    
    uint32_t total_len = str_len * times;
    
    // Check if result fits in our test buffer
    if (total_len >= 127) {
        klee_assert(0); // Result too large
        return NULL;
    }
    
    TestString* result = test_heap_alloc(sizeof(TestString));
    if (!result) {
        klee_assert(0); // Allocation failure
        return NULL;
    }
    
    result->len = total_len;
    result->ref_cnt = 1;
    
    // Copy the string 'times' times
    for (uint32_t i = 0; i < times; i++) {
        memcpy(result->chars + (i * str_len), str->chars, str_len);
    }
    result->chars[total_len] = '\0';
    
    return result;
}

int main() {
    TestString str1, str2;
    TestString* result;
    uint32_t repeat_count;
    
    // Make string structures symbolic
    klee_make_symbolic(&str1, sizeof(str1), "string1");
    klee_make_symbolic(&str2, sizeof(str2), "string2");
    klee_make_symbolic(&repeat_count, sizeof(repeat_count), "repeat_count");
    
    // Add constraints for reasonable testing
    klee_assume(str1.len <= 127);
    klee_assume(str2.len <= 127);
    klee_assume(str1.ref_cnt > 0 && str1.ref_cnt < 1000);
    klee_assume(str2.ref_cnt > 0 && str2.ref_cnt < 1000);
    klee_assume(repeat_count <= 100);
    
    // Ensure strings are null-terminated at their declared length
    str1.chars[str1.len] = '\0';
    str2.chars[str2.len] = '\0';
    
    // Test string concatenation - KLEE will find overflow conditions
    result = test_strcat(&str1, &str2);
    if (result) {
        free(result);
    }
    
    // Test length calculation - KLEE will find inconsistencies
    uint32_t len1 = test_strlen_safe(&str1);
    uint32_t len2 = test_strlen_safe(&str2);
    
    // Test string comparison - KLEE will find buffer over-read conditions
    int cmp_result = test_strcmp_safe(&str1, &str2);
    
    // Test string repetition - KLEE will find overflow in multiplication
    result = test_str_repeat(&str1, repeat_count);
    if (result) {
        free(result);
    }
    
    return 0;
}
