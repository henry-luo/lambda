#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

extern "C" {
#include "../lib/num_stack.h"
}

TEST_CASE("Num Stack Create and Destroy", "[num_stack][basic]") {
    num_stack_t *stack = num_stack_create(10);
    REQUIRE(stack != nullptr);
    REQUIRE(num_stack_length(stack) == 0);
    REQUIRE(num_stack_is_empty(stack));
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Create with Zero Capacity", "[num_stack][basic]") {
    num_stack_t *stack = num_stack_create(0);
    REQUIRE(stack != nullptr);
    REQUIRE(num_stack_length(stack) == 0);
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Push Long Values", "[num_stack][push]") {
    num_stack_t *stack = num_stack_create(5);
    
    // Push some long values
    REQUIRE(num_stack_push_long(stack, 42));
    REQUIRE(num_stack_push_long(stack, -100));
    REQUIRE(num_stack_push_long(stack, 0));
    
    REQUIRE(num_stack_length(stack) == 3);
    REQUIRE_FALSE(num_stack_is_empty(stack));
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Push Double Values", "[num_stack][push]") {
    num_stack_t *stack = num_stack_create(5);
    
    // Push some double values
    REQUIRE(num_stack_push_double(stack, 3.14));
    REQUIRE(num_stack_push_double(stack, -2.5));
    REQUIRE(num_stack_push_double(stack, 0.0));
    
    REQUIRE(num_stack_length(stack) == 3);
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Push Mixed Values", "[num_stack][push]") {
    num_stack_t *stack = num_stack_create(5);
    
    REQUIRE(num_stack_push_long(stack, 123));
    REQUIRE(num_stack_push_double(stack, 4.56));
    REQUIRE(num_stack_push_long(stack, -789));
    
    REQUIRE(num_stack_length(stack) == 3);
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Get Elements", "[num_stack][access]") {
    num_stack_t *stack = num_stack_create(5);
    
    num_stack_push_long(stack, 100);
    num_stack_push_double(stack, 2.5);
    num_stack_push_long(stack, 200);
    
    SECTION("Valid indices") {
        num_value_t *val0 = num_stack_get(stack, 0);
        REQUIRE(val0 != nullptr);
        REQUIRE(val0->as_long == 100);
        
        num_value_t *val1 = num_stack_get(stack, 1);
        REQUIRE(val1 != nullptr);
        REQUIRE(val1->as_double == Catch::Approx(2.5));
        
        num_value_t *val2 = num_stack_get(stack, 2);
        REQUIRE(val2 != nullptr);
        REQUIRE(val2->as_long == 200);
    }
    
    SECTION("Invalid indices") {
        REQUIRE(num_stack_get(stack, 3) == nullptr);
        REQUIRE(num_stack_get(stack, 100) == nullptr);
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Peek Element", "[num_stack][peek]") {
    num_stack_t *stack = num_stack_create(5);
    
    SECTION("Peek empty stack") {
        REQUIRE(num_stack_peek(stack) == nullptr);
    }
    
    SECTION("Peek with elements") {
        num_stack_push_long(stack, 42);
        num_value_t *peek1 = num_stack_peek(stack);
        REQUIRE(peek1 != nullptr);
        REQUIRE(peek1->as_long == 42);
        REQUIRE(num_stack_length(stack) == 1);
        
        num_stack_push_double(stack, 3.14);
        num_value_t *peek2 = num_stack_peek(stack);
        REQUIRE(peek2 != nullptr);
        REQUIRE(peek2->as_double == Catch::Approx(3.14));
        REQUIRE(num_stack_length(stack) == 2);
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Pop Element", "[num_stack][pop]") {
    num_stack_t *stack = num_stack_create(5);
    
    SECTION("Pop empty stack") {
        REQUIRE_FALSE(num_stack_pop(stack));
    }
    
    SECTION("Pop with elements") {
        num_stack_push_long(stack, 10);
        num_stack_push_long(stack, 20);
        num_stack_push_long(stack, 30);
        
        REQUIRE(num_stack_length(stack) == 3);
        
        REQUIRE(num_stack_pop(stack));
        REQUIRE(num_stack_length(stack) == 2);
        
        num_value_t *peek = num_stack_peek(stack);
        REQUIRE(peek->as_long == 20);
        
        REQUIRE(num_stack_pop(stack));
        REQUIRE(num_stack_length(stack) == 1);
        
        REQUIRE(num_stack_pop(stack));
        REQUIRE(num_stack_length(stack) == 0);
        REQUIRE(num_stack_is_empty(stack));
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Chunk Allocation", "[num_stack][capacity]") {
    num_stack_t *stack = num_stack_create(2); // Small initial capacity
    
    // Push more elements than initial capacity
    for (int i = 0; i < 10; i++) {
        REQUIRE(num_stack_push_long(stack, i));
    }
    
    REQUIRE(num_stack_length(stack) == 10);
    
    // Verify all elements are accessible
    for (int i = 0; i < 10; i++) {
        num_value_t *val = num_stack_get(stack, i);
        REQUIRE(val != nullptr);
        REQUIRE(val->as_long == i);
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Reset to Index", "[num_stack][reset]") {
    num_stack_t *stack = num_stack_create(3);
    
    // Push several elements
    for (int i = 0; i < 8; i++) {
        num_stack_push_long(stack, i * 10);
    }
    
    REQUIRE(num_stack_length(stack) == 8);
    
    // Reset to index 5
    REQUIRE(num_stack_reset_to_index(stack, 5));
    REQUIRE(num_stack_length(stack) == 5);
    
    // Verify remaining elements
    for (int i = 0; i < 5; i++) {
        num_value_t *val = num_stack_get(stack, i);
        REQUIRE(val != nullptr);
        REQUIRE(val->as_long == i * 10);
    }
    
    // Verify we can continue pushing from this point
    REQUIRE(num_stack_push_long(stack, 999));
    REQUIRE(num_stack_length(stack) == 6);
    
    num_value_t *last = num_stack_peek(stack);
    REQUIRE(last->as_long == 999);
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Reset Edge Cases", "[num_stack][reset][edge_cases]") {
    num_stack_t *stack = num_stack_create(5);
    
    SECTION("Reset empty stack") {
        REQUIRE(num_stack_reset_to_index(stack, 0));
    }
    
    SECTION("Reset operations") {
        num_stack_push_long(stack, 1);
        num_stack_push_long(stack, 2);
        num_stack_push_long(stack, 3);
        
        // Reset to current length (no-op)
        REQUIRE(num_stack_reset_to_index(stack, 3));
        REQUIRE(num_stack_length(stack) == 3);
        
        // Reset to index 0 (clear all)
        REQUIRE(num_stack_reset_to_index(stack, 0));
        REQUIRE(num_stack_length(stack) == 0);
        REQUIRE(num_stack_is_empty(stack));
        
        // Reset beyond current length should fail
        REQUIRE_FALSE(num_stack_reset_to_index(stack, 5));
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Large Stack", "[num_stack][stress]") {
    num_stack_t *stack = num_stack_create(4); // Small chunks to force multiple allocations
    
    const int num_elements = 100;
    
    // Push many elements
    for (int i = 0; i < num_elements; i++) {
        if (i % 2 == 0) {
            REQUIRE(num_stack_push_long(stack, i));
        } else {
            REQUIRE(num_stack_push_double(stack, i + 0.5));
        }
    }
    
    REQUIRE(num_stack_length(stack) == num_elements);
    
    // Verify all elements
    for (int i = 0; i < num_elements; i++) {
        num_value_t *val = num_stack_get(stack, i);
        REQUIRE(val != nullptr);
        
        if (i % 2 == 0) {
            REQUIRE(val->as_long == i);
        } else {
            REQUIRE(val->as_double == Catch::Approx(i + 0.5));
        }
    }
    
    // Reset to middle and verify
    REQUIRE(num_stack_reset_to_index(stack, 50));
    REQUIRE(num_stack_length(stack) == 50);
    
    // Verify we can still access all remaining elements
    for (int i = 0; i < 50; i++) {
        num_value_t *val = num_stack_get(stack, i);
        REQUIRE(val != nullptr);
    }
    
    num_stack_destroy(stack);
}

TEST_CASE("Num Stack Null Pointer Handling", "[num_stack][null_safety]") {
    // Test with null stack pointer
    REQUIRE_FALSE(num_stack_push_long(nullptr, 42));
    REQUIRE_FALSE(num_stack_push_double(nullptr, 3.14));
    REQUIRE(num_stack_get(nullptr, 0) == nullptr);
    REQUIRE(num_stack_peek(nullptr) == nullptr);
    REQUIRE_FALSE(num_stack_pop(nullptr));
    REQUIRE_FALSE(num_stack_reset_to_index(nullptr, 0));
    REQUIRE(num_stack_length(nullptr) == 0);
    REQUIRE(num_stack_is_empty(nullptr));
    
    // Destroy null stack should not crash
    num_stack_destroy(nullptr);
}

TEST_CASE("Num Stack Destroy Functionality", "[num_stack][destroy]") {
    SECTION("Destroy empty stack") {
        num_stack_t *empty_stack = num_stack_create(5);
        REQUIRE(empty_stack != nullptr);
        num_stack_destroy(empty_stack);
        // If we reach here without crash, destruction of empty stack works
    }
    
    SECTION("Destroy stack with single chunk") {
        num_stack_t *single_chunk_stack = num_stack_create(10);
        num_stack_push_long(single_chunk_stack, 42);
        num_stack_push_double(single_chunk_stack, 3.14);
        num_stack_push_long(single_chunk_stack, 100);
        REQUIRE(num_stack_length(single_chunk_stack) == 3);
        num_stack_destroy(single_chunk_stack);
        // If we reach here without crash, destruction of single chunk stack works
    }
    
    SECTION("Destroy stack with multiple chunks") {
        num_stack_t *multi_chunk_stack = num_stack_create(2); // Small capacity to force multiple chunks
        for (int i = 0; i < 20; i++) {
            if (i % 2 == 0) {
                num_stack_push_long(multi_chunk_stack, i);
            } else {
                num_stack_push_double(multi_chunk_stack, i + 0.5);
            }
        }
        REQUIRE(num_stack_length(multi_chunk_stack) == 20);
        num_stack_destroy(multi_chunk_stack);
        // If we reach here without crash, destruction of multi-chunk stack works
    }
    
    SECTION("Destroy stack after reset") {
        num_stack_t *reset_stack = num_stack_create(3);
        for (int i = 0; i < 15; i++) {
            num_stack_push_long(reset_stack, i * 10);
        }
        REQUIRE(num_stack_length(reset_stack) == 15);
        
        // Reset to smaller size (should free some chunks)
        REQUIRE(num_stack_reset_to_index(reset_stack, 5));
        REQUIRE(num_stack_length(reset_stack) == 5);
        
        num_stack_destroy(reset_stack);
        // If we reach here without crash, destruction after reset works
    }
    
    SECTION("Destroy completely emptied stack") {
        num_stack_t *emptied_stack = num_stack_create(4);
        num_stack_push_long(emptied_stack, 1);
        num_stack_push_long(emptied_stack, 2);
        num_stack_push_long(emptied_stack, 3);
        REQUIRE(num_stack_length(emptied_stack) == 3);
        
        // Reset to empty
        REQUIRE(num_stack_reset_to_index(emptied_stack, 0));
        REQUIRE(num_stack_length(emptied_stack) == 0);
        REQUIRE(num_stack_is_empty(emptied_stack));
        
        num_stack_destroy(emptied_stack);
        // If we reach here without crash, destruction of emptied stack works
    }
}
