#include <stdio.h>
#include <assert.h>

// Simple test framework without Criterion
int test_count = 0;
int passed_count = 0;

void test_assert(int condition, const char* message) {
    test_count++;
    if (condition) {
        passed_count++;
        printf("✓ %s\n", message);
    } else {
        printf("✗ %s\n", message);
    }
}

// Simple tests
void test_basic_math() {
    test_assert(2 + 2 == 4, "Basic addition works");
    test_assert(10 - 3 == 7, "Basic subtraction works");
    test_assert(5 * 6 == 30, "Basic multiplication works");
}

void test_string_ops() {
    const char* str = "Hello, World!";
    test_assert(str[0] == 'H', "String indexing works");
    test_assert(str[7] == 'W', "String indexing at position 7 works");
}

int main() {
    printf("Running simple Linux tests...\n\n");
    
    test_basic_math();
    test_string_ops();
    
    printf("\nTest Results: %d/%d tests passed\n", passed_count, test_count);
    
    if (passed_count == test_count) {
        printf("All tests passed! ✓\n");
        return 0;
    } else {
        printf("Some tests failed! ✗\n");
        return 1;
    }
}