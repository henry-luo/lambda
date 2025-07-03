#include <criterion/criterion.h>
#include "../lib/num_stack.h"

// test basic stack creation and destruction
Test(num_stack, create_and_destroy) {
    num_stack_t *stack = num_stack_create(10);
    cr_assert_not_null(stack, "stack creation should succeed");
    cr_assert_eq(num_stack_length(stack), 0, "new stack should be empty");
    cr_assert(num_stack_is_empty(stack), "new stack should be empty");
    
    num_stack_destroy(stack);
}

// test stack creation with zero capacity (should use default)
Test(num_stack, create_with_zero_capacity) {
    num_stack_t *stack = num_stack_create(0);
    cr_assert_not_null(stack, "stack creation with 0 capacity should succeed");
    cr_assert_eq(num_stack_length(stack), 0, "new stack should be empty");
    
    num_stack_destroy(stack);
}

// test pushing long values
Test(num_stack, push_long_values) {
    num_stack_t *stack = num_stack_create(5);
    
    // push some long values
    cr_assert(num_stack_push_long(stack, 42), "pushing long should succeed");
    cr_assert(num_stack_push_long(stack, -100), "pushing negative long should succeed");
    cr_assert(num_stack_push_long(stack, 0), "pushing zero long should succeed");
    
    cr_assert_eq(num_stack_length(stack), 3, "stack should have 3 elements");
    cr_assert(!num_stack_is_empty(stack), "stack should not be empty");
    
    num_stack_destroy(stack);
}

// test pushing double values
Test(num_stack, push_double_values) {
    num_stack_t *stack = num_stack_create(5);
    
    // push some double values
    cr_assert(num_stack_push_double(stack, 3.14), "pushing double should succeed");
    cr_assert(num_stack_push_double(stack, -2.5), "pushing negative double should succeed");
    cr_assert(num_stack_push_double(stack, 0.0), "pushing zero double should succeed");
    
    cr_assert_eq(num_stack_length(stack), 3, "stack should have 3 elements");
    
    num_stack_destroy(stack);
}

// test mixing long and double values
Test(num_stack, push_mixed_values) {
    num_stack_t *stack = num_stack_create(5);
    
    cr_assert(num_stack_push_long(stack, 123), "pushing long should succeed");
    cr_assert(num_stack_push_double(stack, 4.56), "pushing double should succeed");
    cr_assert(num_stack_push_long(stack, -789), "pushing long should succeed");
    
    cr_assert_eq(num_stack_length(stack), 3, "stack should have 3 elements");
    
    num_stack_destroy(stack);
}

// test accessing elements by index
Test(num_stack, get_elements) {
    num_stack_t *stack = num_stack_create(5);
    
    num_stack_push_long(stack, 100);
    num_stack_push_double(stack, 2.5);
    num_stack_push_long(stack, 200);
    
    // test valid indices
    num_value_t *val0 = num_stack_get(stack, 0);
    cr_assert_not_null(val0, "get index 0 should succeed");
    cr_assert_eq(val0->as_long, 100, "first element should be 100");
    
    num_value_t *val1 = num_stack_get(stack, 1);
    cr_assert_not_null(val1, "get index 1 should succeed");
    cr_assert_float_eq(val1->as_double, 2.5, 1e-6, "second element should be 2.5");
    
    num_value_t *val2 = num_stack_get(stack, 2);
    cr_assert_not_null(val2, "get index 2 should succeed");
    cr_assert_eq(val2->as_long, 200, "third element should be 200");
    
    // test invalid indices
    cr_assert_null(num_stack_get(stack, 3), "get index 3 should return null");
    cr_assert_null(num_stack_get(stack, 100), "get large index should return null");
    
    num_stack_destroy(stack);
}

// test peek functionality
Test(num_stack, peek_element) {
    num_stack_t *stack = num_stack_create(5);
    
    // peek empty stack
    cr_assert_null(num_stack_peek(stack), "peek empty stack should return null");
    
    num_stack_push_long(stack, 42);
    num_value_t *peek1 = num_stack_peek(stack);
    cr_assert_not_null(peek1, "peek should succeed");
    cr_assert_eq(peek1->as_long, 42, "peek should return last element");
    cr_assert_eq(num_stack_length(stack), 1, "peek should not change length");
    
    num_stack_push_double(stack, 3.14);
    num_value_t *peek2 = num_stack_peek(stack);
    cr_assert_not_null(peek2, "peek should succeed");
    cr_assert_float_eq(peek2->as_double, 3.14, 1e-6, "peek should return last element");
    cr_assert_eq(num_stack_length(stack), 2, "peek should not change length");
    
    num_stack_destroy(stack);
}

// test pop functionality
Test(num_stack, pop_element) {
    num_stack_t *stack = num_stack_create(5);
    
    // pop empty stack
    cr_assert(!num_stack_pop(stack), "pop empty stack should fail");
    
    num_stack_push_long(stack, 10);
    num_stack_push_long(stack, 20);
    num_stack_push_long(stack, 30);
    
    cr_assert_eq(num_stack_length(stack), 3, "stack should have 3 elements");
    
    cr_assert(num_stack_pop(stack), "pop should succeed");
    cr_assert_eq(num_stack_length(stack), 2, "stack should have 2 elements after pop");
    
    num_value_t *peek = num_stack_peek(stack);
    cr_assert_eq(peek->as_long, 20, "top element should now be 20");
    
    cr_assert(num_stack_pop(stack), "pop should succeed");
    cr_assert_eq(num_stack_length(stack), 1, "stack should have 1 element after pop");
    
    cr_assert(num_stack_pop(stack), "pop should succeed");
    cr_assert_eq(num_stack_length(stack), 0, "stack should be empty after pop");
    cr_assert(num_stack_is_empty(stack), "stack should be empty");
    
    num_stack_destroy(stack);
}

// test chunk allocation (exceeding initial capacity)
Test(num_stack, chunk_allocation) {
    num_stack_t *stack = num_stack_create(2); // small initial capacity
    
    // push more elements than initial capacity
    for (int i = 0; i < 10; i++) {
        cr_assert(num_stack_push_long(stack, i), "push should succeed");
    }
    
    cr_assert_eq(num_stack_length(stack), 10, "stack should have 10 elements");
    
    // verify all elements are accessible
    for (int i = 0; i < 10; i++) {
        num_value_t *val = num_stack_get(stack, i);
        cr_assert_not_null(val, "get should succeed");
        cr_assert_eq(val->as_long, i, "element value should match");
    }
    
    num_stack_destroy(stack);
}

// test reset to index functionality
Test(num_stack, reset_to_index) {
    num_stack_t *stack = num_stack_create(3);
    
    // push several elements
    for (int i = 0; i < 8; i++) {
        num_stack_push_long(stack, i * 10);
    }
    
    cr_assert_eq(num_stack_length(stack), 8, "stack should have 8 elements");
    
    // reset to index 5
    cr_assert(num_stack_reset_to_index(stack, 5), "reset should succeed");
    cr_assert_eq(num_stack_length(stack), 5, "stack should have 5 elements after reset");
    
    // verify remaining elements
    for (int i = 0; i < 5; i++) {
        num_value_t *val = num_stack_get(stack, i);
        cr_assert_not_null(val, "get should succeed");
        cr_assert_eq(val->as_long, i * 10, "element value should match");
    }
    
    // verify we can continue pushing from this point
    cr_assert(num_stack_push_long(stack, 999), "push after reset should succeed");
    cr_assert_eq(num_stack_length(stack), 6, "stack should have 6 elements");
    
    num_value_t *last = num_stack_peek(stack);
    cr_assert_eq(last->as_long, 999, "last element should be 999");
    
    num_stack_destroy(stack);
}

// test reset to index edge cases
Test(num_stack, reset_to_index_edge_cases) {
    num_stack_t *stack = num_stack_create(5);
    
    // reset empty stack
    cr_assert(num_stack_reset_to_index(stack, 0), "reset empty stack to 0 should succeed");
    
    num_stack_push_long(stack, 1);
    num_stack_push_long(stack, 2);
    num_stack_push_long(stack, 3);
    
    // reset to current length (no-op)
    cr_assert(num_stack_reset_to_index(stack, 3), "reset to current length should succeed");
    cr_assert_eq(num_stack_length(stack), 3, "length should remain 3");
    
    // reset to index 0 (clear all)
    cr_assert(num_stack_reset_to_index(stack, 0), "reset to 0 should succeed");
    cr_assert_eq(num_stack_length(stack), 0, "stack should be empty");
    cr_assert(num_stack_is_empty(stack), "stack should be empty");
    
    // reset beyond current length should fail
    cr_assert(!num_stack_reset_to_index(stack, 5), "reset beyond length should fail");
    
    num_stack_destroy(stack);
}

// test large number of elements across multiple chunks
Test(num_stack, large_stack) {
    num_stack_t *stack = num_stack_create(4); // small chunks to force multiple allocations
    
    const int num_elements = 100;
    
    // push many elements
    for (int i = 0; i < num_elements; i++) {
        if (i % 2 == 0) {
            cr_assert(num_stack_push_long(stack, i), "push long should succeed");
        } else {
            cr_assert(num_stack_push_double(stack, i + 0.5), "push double should succeed");
        }
    }
    
    cr_assert_eq(num_stack_length(stack), num_elements, "stack should have all elements");
    
    // verify all elements
    for (int i = 0; i < num_elements; i++) {
        num_value_t *val = num_stack_get(stack, i);
        cr_assert_not_null(val, "get should succeed");
        
        if (i % 2 == 0) {
            cr_assert_eq(val->as_long, i, "long element should match");
        } else {
            cr_assert_float_eq(val->as_double, i + 0.5, 1e-6, "double element should match");
        }
    }
    
    // reset to middle and verify
    cr_assert(num_stack_reset_to_index(stack, 50), "reset should succeed");
    cr_assert_eq(num_stack_length(stack), 50, "stack should have 50 elements");
    
    // verify we can still access all remaining elements
    for (int i = 0; i < 50; i++) {
        num_value_t *val = num_stack_get(stack, i);
        cr_assert_not_null(val, "get should succeed after reset");
    }
    
    num_stack_destroy(stack);
}

// test null pointer handling
Test(num_stack, null_pointer_handling) {
    // test with null stack pointer
    cr_assert(!num_stack_push_long(NULL, 42), "push to null stack should fail");
    cr_assert(!num_stack_push_double(NULL, 3.14), "push to null stack should fail");
    cr_assert_null(num_stack_get(NULL, 0), "get from null stack should return null");
    cr_assert_null(num_stack_peek(NULL), "peek null stack should return null");
    cr_assert(!num_stack_pop(NULL), "pop null stack should fail");
    cr_assert(!num_stack_reset_to_index(NULL, 0), "reset null stack should fail");
    cr_assert_eq(num_stack_length(NULL), 0, "length of null stack should be 0");
    cr_assert(num_stack_is_empty(NULL), "null stack should be considered empty");
    
    // destroy null stack should not crash
    num_stack_destroy(NULL);
}

// test proper destruction and memory cleanup
Test(num_stack, destroy_functionality) {
    // test destroying empty stack
    num_stack_t *empty_stack = num_stack_create(5);
    cr_assert_not_null(empty_stack, "stack creation should succeed");
    num_stack_destroy(empty_stack);
    // if we reach here without crash, destruction of empty stack works
    
    // test destroying stack with single chunk
    num_stack_t *single_chunk_stack = num_stack_create(10);
    num_stack_push_long(single_chunk_stack, 42);
    num_stack_push_double(single_chunk_stack, 3.14);
    num_stack_push_long(single_chunk_stack, 100);
    cr_assert_eq(num_stack_length(single_chunk_stack), 3, "stack should have 3 elements");
    num_stack_destroy(single_chunk_stack);
    // if we reach here without crash, destruction of single chunk stack works
    
    // test destroying stack with multiple chunks
    num_stack_t *multi_chunk_stack = num_stack_create(2); // small capacity to force multiple chunks
    for (int i = 0; i < 20; i++) {
        if (i % 2 == 0) {
            num_stack_push_long(multi_chunk_stack, i);
        } else {
            num_stack_push_double(multi_chunk_stack, i + 0.5);
        }
    }
    cr_assert_eq(num_stack_length(multi_chunk_stack), 20, "stack should have 20 elements");
    num_stack_destroy(multi_chunk_stack);
    // if we reach here without crash, destruction of multi-chunk stack works
    
    // test destroying stack after reset (with freed chunks)
    num_stack_t *reset_stack = num_stack_create(3);
    for (int i = 0; i < 15; i++) {
        num_stack_push_long(reset_stack, i * 10);
    }
    cr_assert_eq(num_stack_length(reset_stack), 15, "stack should have 15 elements");
    
    // reset to smaller size (should free some chunks)
    cr_assert(num_stack_reset_to_index(reset_stack, 5), "reset should succeed");
    cr_assert_eq(num_stack_length(reset_stack), 5, "stack should have 5 elements after reset");
    
    num_stack_destroy(reset_stack);
    // if we reach here without crash, destruction after reset works
    
    // test destroying stack that was completely emptied via reset
    num_stack_t *emptied_stack = num_stack_create(4);
    num_stack_push_long(emptied_stack, 1);
    num_stack_push_long(emptied_stack, 2);
    num_stack_push_long(emptied_stack, 3);
    cr_assert_eq(num_stack_length(emptied_stack), 3, "stack should have 3 elements");
    
    // reset to empty
    cr_assert(num_stack_reset_to_index(emptied_stack, 0), "reset to 0 should succeed");
    cr_assert_eq(num_stack_length(emptied_stack), 0, "stack should be empty after reset");
    cr_assert(num_stack_is_empty(emptied_stack), "stack should be empty");
    
    num_stack_destroy(emptied_stack);
    // if we reach here without crash, destruction of emptied stack works
}
