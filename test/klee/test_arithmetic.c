/**
 * @file test_arithmetic.c
 * @brief KLEE test harness for arithmetic operations in Lambda Script
 * @author Henry Luo
 * 
 * This test harness uses KLEE symbolic execution to automatically discover
 * arithmetic issues like division by zero, integer overflow, and underflow.
 */

#include <klee/klee.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>

// Include Lambda Script headers
#include "../../lambda/lambda.h"
#include "../../lambda/lambda-data.hpp"

// Mock implementations for testing
typedef struct {
    int32_t int_val;
    uint8_t type_id;
} TestItem;

#define TEST_TYPE_INT 3

// Instrumented division function for KLEE analysis
TestItem test_divide(TestItem a, TestItem b) {
    TestItem result = {0, TEST_TYPE_INT};
    
    // KLEE will explore the case where b.int_val == 0
    if (b.int_val == 0) {
        // This assertion will fail and KLEE will report division by zero
        klee_assert(0);
        result.int_val = 0;
        return result;
    }
    
    // Check for overflow in division (INT_MIN / -1 causes overflow)
    if (a.int_val == INT_MIN && b.int_val == -1) {
        klee_assert(0); // Integer overflow in division
        result.int_val = 0;
        return result;
    }
    
    result.int_val = a.int_val / b.int_val;
    return result;
}

// Instrumented addition function for overflow detection
TestItem test_add(TestItem a, TestItem b) {
    TestItem result = {0, TEST_TYPE_INT};
    
    // Check for overflow: if a > 0 and b > 0 and a > INT_MAX - b
    if (a.int_val > 0 && b.int_val > 0 && a.int_val > INT_MAX - b.int_val) {
        klee_assert(0); // Positive overflow
        result.int_val = 0;
        return result;
    }
    
    // Check for underflow: if a < 0 and b < 0 and a < INT_MIN - b
    if (a.int_val < 0 && b.int_val < 0 && a.int_val < INT_MIN - b.int_val) {
        klee_assert(0); // Negative underflow
        result.int_val = 0;
        return result;
    }
    
    result.int_val = a.int_val + b.int_val;
    return result;
}

// Instrumented multiplication function
TestItem test_multiply(TestItem a, TestItem b) {
    TestItem result = {0, TEST_TYPE_INT};
    
    // Handle zero cases first
    if (a.int_val == 0 || b.int_val == 0) {
        result.int_val = 0;
        return result;
    }
    
    // Check for overflow in multiplication
    // If a > 0 and b > 0: overflow if a > INT_MAX / b
    if (a.int_val > 0 && b.int_val > 0) {
        if (a.int_val > INT_MAX / b.int_val) {
            klee_assert(0); // Positive overflow
            result.int_val = 0;
            return result;
        }
    }
    // If a < 0 and b < 0: overflow if a < INT_MAX / b (both negative)
    else if (a.int_val < 0 && b.int_val < 0) {
        if (a.int_val < INT_MAX / b.int_val) {
            klee_assert(0); // Overflow from negative * negative
            result.int_val = 0;
            return result;
        }
    }
    // If signs differ: underflow if a < INT_MIN / b or a > INT_MIN / b
    else if ((a.int_val > 0 && b.int_val < 0) || (a.int_val < 0 && b.int_val > 0)) {
        int32_t pos_val = (a.int_val > 0) ? a.int_val : b.int_val;
        int32_t neg_val = (a.int_val < 0) ? a.int_val : b.int_val;
        
        if (pos_val > INT_MIN / neg_val) {
            klee_assert(0); // Underflow from mixed signs
            result.int_val = 0;
            return result;
        }
    }
    
    result.int_val = a.int_val * b.int_val;
    return result;
}

// Instrumented modulo function
TestItem test_modulo(TestItem a, TestItem b) {
    TestItem result = {0, TEST_TYPE_INT};
    
    // Division by zero in modulo
    if (b.int_val == 0) {
        klee_assert(0); // Modulo by zero
        result.int_val = 0;
        return result;
    }
    
    // Special case: INT_MIN % -1 causes undefined behavior
    if (a.int_val == INT_MIN && b.int_val == -1) {
        klee_assert(0); // Undefined behavior in modulo
        result.int_val = 0;
        return result;
    }
    
    result.int_val = a.int_val % b.int_val;
    return result;
}

int main() {
    TestItem a, b, result;
    
    // Make operands symbolic - KLEE will explore all possible values
    klee_make_symbolic(&a.int_val, sizeof(a.int_val), "operand_a");
    klee_make_symbolic(&b.int_val, sizeof(b.int_val), "operand_b");
    
    // Set types
    a.type_id = TEST_TYPE_INT;
    b.type_id = TEST_TYPE_INT;
    
    // Add some reasonable constraints to limit exploration space
    // But still allow problematic values
    klee_assume(a.int_val >= INT_MIN && a.int_val <= INT_MAX);
    klee_assume(b.int_val >= INT_MIN && b.int_val <= INT_MAX);
    
    // Test division - KLEE will find division by zero cases
    result = test_divide(a, b);
    
    // Test addition - KLEE will find overflow cases
    result = test_add(a, b);
    
    // Test multiplication - KLEE will find overflow cases
    result = test_multiply(a, b);
    
    // Test modulo - KLEE will find modulo by zero cases
    result = test_modulo(a, b);
    
    // If we reach here without assertion failures, the operations are safe
    return 0;
}
