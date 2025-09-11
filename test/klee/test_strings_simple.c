/**
 * @file test_strings_simple.c
 * @brief Simplified KLEE test for string operations
 * @author Henry Luo
 * 
 * This test harness uses KLEE symbolic execution to discover
 * string handling issues without depending on Lambda Script headers.
 */

#include <klee/klee.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define MAX_STRING_SIZE 32

// Simple string functions to test
int string_length(const char* str) {
    if (!str) return -1;
    return strlen(str);
}

int string_compare(const char* str1, const char* str2) {
    if (!str1 || !str2) return -1;
    return strcmp(str1, str2);
}

char* string_copy(char* dest, const char* src, size_t max_size) {
    if (!dest || !src || max_size == 0) return NULL;
    
    size_t i;
    for (i = 0; i < max_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return dest;
}

int main() {
    char buffer1[MAX_STRING_SIZE];
    char buffer2[MAX_STRING_SIZE];
    char input[16];
    
    // Make input symbolic
    klee_make_symbolic(input, sizeof(input), "input");
    
    // Ensure null termination within bounds
    klee_assume(input[15] == '\0');
    
    // Ensure printable ASCII characters
    for (int i = 0; i < 15; i++) {
        klee_assume(input[i] >= 0 && input[i] <= 127);
        if (input[i] == '\0') break;
    }
    
    // Test string length
    int len = string_length(input);
    assert(len >= 0);
    assert(len <= 15);
    
    // Test string copy with bounds checking
    char* result = string_copy(buffer1, input, MAX_STRING_SIZE);
    assert(result != NULL);
    assert(buffer1[MAX_STRING_SIZE - 1] == '\0');
    
    // Test string comparison
    strcpy(buffer2, "test");
    int cmp_result = string_compare(input, buffer2);
    
    // Verify comparison logic
    if (strcmp(input, buffer2) == 0) {
        assert(cmp_result == 0);
    }
    
    return 0;
}
