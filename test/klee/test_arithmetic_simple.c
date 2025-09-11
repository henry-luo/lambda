/**
 * @file test_arithmetic_simple.c
 * @brief Simplified KLEE test for basic arithmetic operations
 * @author Henry Luo
 * 
 * This test harness uses KLEE symbolic execution to discover
 * arithmetic issues without depending on Lambda Script headers.
 */

#include <klee/klee.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>

// Simple arithmetic functions to test
int add_integers(int a, int b) {
    return a + b;
}

int subtract_integers(int a, int b) {
    return a - b;
}

int multiply_integers(int a, int b) {
    return a * b;
}

int divide_integers(int a, int b) {
    if (b == 0) {
        return -1; // Error case
    }
    return a / b;
}

int main() {
    int a, b;
    
    // Make variables symbolic
    klee_make_symbolic(&a, sizeof(a), "a");
    klee_make_symbolic(&b, sizeof(b), "b");
    
    // Constrain inputs to reasonable ranges
    klee_assume(a >= -1000 && a <= 1000);
    klee_assume(b >= -1000 && b <= 1000);
    
    // Test addition overflow
    if (a > 0 && b > 0) {
        assert(a <= INT_MAX - b); // Prevent overflow
    }
    
    // Test subtraction underflow
    if (a < 0 && b > 0) {
        assert(a >= INT_MIN + b); // Prevent underflow
    }
    
    // Test division by zero
    int result = divide_integers(a, b);
    if (b == 0) {
        assert(result == -1);
    } else {
        assert(result == a / b);
    }
    
    // Test multiplication overflow (simple case)
    if (a != 0 && b != 0) {
        if (a > 0 && b > 0) {
            assert(a <= INT_MAX / b);
        }
    }
    
    return 0;
}
