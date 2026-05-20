#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/num_stack.h"
#include "../lib/datetime.h"
#include "../lib/log.h"
}

class NumStackTest : public ::testing::Test {
protected:
    num_stack_t* stack = nullptr;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        stack = num_stack_create(10);
        ASSERT_NE(stack, nullptr) << "Failed to create num stack";
    }

    void TearDown() override {
        if (stack) {
            num_stack_destroy(stack);
            stack = nullptr;
        }
    }
};

TEST_F(NumStackTest, BasicStackOperations) {
    // Test initial empty state
    EXPECT_EQ(num_stack_length(stack), 0UL) << "new stack should be empty";
    EXPECT_TRUE(num_stack_is_empty(stack)) << "new stack should report as empty";
}

TEST_F(NumStackTest, PushLongValues) {
    // Test pushing long values
    int64_t* val1 = num_stack_push_long(stack, 42);
    EXPECT_NE(val1, nullptr) << "push should return valid pointer";
    EXPECT_EQ(*val1, 42L) << "pushed value should be correct";
    EXPECT_EQ(num_stack_length(stack), 1UL) << "stack length should be 1";
    EXPECT_FALSE(num_stack_is_empty(stack)) << "stack should not be empty";
    
    int64_t* val2 = num_stack_push_long(stack, -123);
    EXPECT_NE(val2, nullptr) << "second push should return valid pointer";
    EXPECT_EQ(*val2, -123L) << "second pushed value should be correct";
    EXPECT_EQ(num_stack_length(stack), 2UL) << "stack length should be 2";
}

TEST_F(NumStackTest, PushDoubleValues) {
    // Test pushing double values
    double* val1 = num_stack_push_double(stack, 3.14159);
    EXPECT_NE(val1, nullptr) << "push double should return valid pointer";
    EXPECT_DOUBLE_EQ(*val1, 3.14159) << "pushed double value should be correct";
    EXPECT_EQ(num_stack_length(stack), 1UL) << "stack length should be 1";
    
    double* val2 = num_stack_push_double(stack, -2.71828);
    EXPECT_NE(val2, nullptr) << "second push double should return valid pointer";
    EXPECT_DOUBLE_EQ(*val2, -2.71828) << "second pushed double value should be correct";
    EXPECT_EQ(num_stack_length(stack), 2UL) << "stack length should be 2";
}

TEST_F(NumStackTest, PushDateTimeValues) {
    // Test pushing DateTime values
    DateTime dt = {0};
    dt.year_month = 2025 * 16 + 8; // Year 2025, Month 8
    dt.day = 15;
    dt.hour = 10;
    dt.minute = 30;
    
    DateTime* val = num_stack_push_datetime(stack, dt);
    EXPECT_NE(val, nullptr) << "push datetime should return valid pointer";
    EXPECT_EQ(val->day, 15UL) << "pushed datetime day should be correct";
    EXPECT_EQ(val->hour, 10UL) << "pushed datetime hour should be correct";
    EXPECT_EQ(num_stack_length(stack), 1UL) << "stack length should be 1";
}

TEST_F(NumStackTest, PeekOperations) {
    // Push some values first
    num_stack_push_long(stack, 100);
    num_stack_push_long(stack, 200);
    
    // Test peek (should return top element without removing it)
    num_value_t* top = num_stack_peek(stack);
    EXPECT_NE(top, nullptr) << "peek should return valid value";
    EXPECT_EQ(num_stack_length(stack), 2UL) << "peek should not change stack length";
    
    // Peek again should return same value
    num_value_t* top2 = num_stack_peek(stack);
    EXPECT_EQ(top, top2) << "multiple peeks should return same value";
}

TEST_F(NumStackTest, PopOperations) {
    // Test pop on empty stack
    EXPECT_FALSE(num_stack_pop(stack)) << "pop empty stack should return false";
    
    // Push some values
    num_stack_push_long(stack, 100);
    num_stack_push_long(stack, 200);
    EXPECT_EQ(num_stack_length(stack), 2UL) << "should have 2 values";
    
    // Get top value before popping
    num_value_t* pop_val = num_stack_peek(stack);
    EXPECT_NE(pop_val, nullptr) << "should be able to peek before pop";
    
    // Pop the value
    EXPECT_TRUE(num_stack_pop(stack)) << "pop should succeed";
    EXPECT_EQ(num_stack_length(stack), 1UL) << "stack length should decrease";
    
    // Pop again
    EXPECT_TRUE(num_stack_pop(stack)) << "second pop should succeed";
    EXPECT_EQ(num_stack_length(stack), 0UL) << "stack should be empty";
    EXPECT_TRUE(num_stack_is_empty(stack)) << "stack should report as empty";
    
    // Pop empty stack again
    EXPECT_FALSE(num_stack_pop(stack)) << "pop empty stack should return false";
}

TEST_F(NumStackTest, GetOperations) {
    // Push some values
    num_stack_push_long(stack, 10);
    num_stack_push_long(stack, 20);
    num_stack_push_long(stack, 30);
    
    // Test get by index
    num_value_t* val0 = num_stack_get(stack, 0);
    num_value_t* val1 = num_stack_get(stack, 1);
    num_value_t* val2 = num_stack_get(stack, 2);
    
    EXPECT_NE(val0, nullptr) << "get index 0 should return valid value";
    EXPECT_NE(val1, nullptr) << "get index 1 should return valid value";
    EXPECT_NE(val2, nullptr) << "get index 2 should return valid value";
    
    // Test out of bounds
    num_value_t* invalid = num_stack_get(stack, 10);
    EXPECT_EQ(invalid, nullptr) << "get invalid index should return null";
}

TEST_F(NumStackTest, ResetOperations) {
    // Push some values
    num_stack_push_long(stack, 10);
    num_stack_push_long(stack, 20);
    num_stack_push_long(stack, 30);
    num_stack_push_long(stack, 40);
    EXPECT_EQ(num_stack_length(stack), 4UL) << "should have 4 values";
    
    // Reset to index 2 (keep first 2 elements)
    EXPECT_TRUE(num_stack_reset_to_index(stack, 2)) << "reset should succeed";
    EXPECT_EQ(num_stack_length(stack), 2UL) << "stack length should be 2 after reset";
    
    // Verify remaining values are accessible
    num_value_t* val0 = num_stack_get(stack, 0);
    num_value_t* val1 = num_stack_get(stack, 1);
    EXPECT_NE(val0, nullptr) << "first value should still be accessible";
    EXPECT_NE(val1, nullptr) << "second value should still be accessible";
}

TEST_F(NumStackTest, LargeStackOperations) {
    // Test with many values to trigger chunk allocation
    const size_t num_values = 100;
    
    // Push many values
    for (size_t i = 0; i < num_values; i++) {
        int64_t* val = num_stack_push_long(stack, (int64_t)i);
        EXPECT_NE(val, nullptr) << "push should succeed for value " << i;
        EXPECT_EQ(*val, (int64_t)i) << "value should be correct for index " << i;
    }
    
    EXPECT_EQ(num_stack_length(stack), num_values) << "stack should contain all pushed values";
    
    // Verify all values are accessible
    for (size_t i = 0; i < num_values; i++) {
        num_value_t* val = num_stack_get(stack, i);
        EXPECT_NE(val, nullptr) << "value at index " << i << " should be accessible";
    }
    
    // Pop all values
    for (size_t i = num_values; i > 0; i--) {
        EXPECT_TRUE(num_stack_pop(stack)) << "pop should succeed for remaining " << i << " values";
        EXPECT_EQ(num_stack_length(stack), i - 1) << "stack length should be correct";
    }
    
    EXPECT_TRUE(num_stack_is_empty(stack)) << "stack should be empty after popping all values";
}

// Missing test 1: Create with zero capacity
TEST_F(NumStackTest, CreateWithZeroCapacity) {
    num_stack_t* zero_stack = num_stack_create(0);
    EXPECT_NE(zero_stack, nullptr) << "stack should be created even with zero capacity";
    
    EXPECT_EQ(num_stack_length(zero_stack), 0UL) << "zero capacity stack should start empty";
    EXPECT_TRUE(num_stack_is_empty(zero_stack)) << "zero capacity stack should be empty";
    
    // Should still be able to push values (will allocate as needed)
    int64_t* val = num_stack_push_long(zero_stack, 42);
    EXPECT_NE(val, nullptr) << "push should succeed on zero capacity stack";
    EXPECT_EQ(*val, 42) << "pushed value should be correct";
    
    num_stack_destroy(zero_stack);
}

// Missing test 2: Push mixed values
TEST_F(NumStackTest, PushMixedValues) {
    // Push different types of values
    int64_t* long_val = num_stack_push_long(stack, 123);
    double* double_val = num_stack_push_double(stack, 45.67);
    int64_t* long_val2 = num_stack_push_long(stack, -789);
    
    EXPECT_NE(long_val, nullptr) << "long push should succeed";
    EXPECT_NE(double_val, nullptr) << "double push should succeed";
    EXPECT_NE(long_val2, nullptr) << "second long push should succeed";
    
    EXPECT_EQ(*long_val, 123) << "first long value should be correct";
    EXPECT_DOUBLE_EQ(*double_val, 45.67) << "double value should be correct";
    EXPECT_EQ(*long_val2, -789) << "second long value should be correct";
    
    EXPECT_EQ(num_stack_length(stack), 3UL) << "stack should contain 3 values";
    
    // Verify values can be retrieved
    num_value_t* val0 = num_stack_get(stack, 0);
    num_value_t* val1 = num_stack_get(stack, 1);
    num_value_t* val2 = num_stack_get(stack, 2);
    
    EXPECT_NE(val0, nullptr) << "first value should be retrievable";
    EXPECT_NE(val1, nullptr) << "second value should be retrievable";
    EXPECT_NE(val2, nullptr) << "third value should be retrievable";
}

// Missing test 3: Chunk allocation behavior
TEST_F(NumStackTest, ChunkAllocation) {
    // Test that stack grows properly when exceeding initial capacity
    const size_t initial_capacity = 16; // Assuming this is the chunk size
    
    // Push values beyond initial capacity
    for (size_t i = 0; i < initial_capacity + 5; i++) {
        int64_t* val = num_stack_push_long(stack, (int64_t)i);
        EXPECT_NE(val, nullptr) << "push should succeed for value " << i;
        EXPECT_EQ(*val, (int64_t)i) << "value should be correct";
    }
    
    EXPECT_EQ(num_stack_length(stack), initial_capacity + 5) << "stack should contain all pushed values";
    
    // Verify all values are still accessible after chunk allocation
    for (size_t i = 0; i < initial_capacity + 5; i++) {
        num_value_t* val = num_stack_get(stack, i);
        EXPECT_NE(val, nullptr) << "value at index " << i << " should be accessible after chunk allocation";
    }
}

// Missing test 4: Reset to index edge cases
TEST_F(NumStackTest, ResetToIndexEdgeCases) {
    // Push several values
    for (int i = 0; i < 5; i++) {
        int64_t* val = num_stack_push_long(stack, i);
        EXPECT_NE(val, nullptr);
    }
    
    EXPECT_EQ(num_stack_length(stack), 5UL) << "stack should have 5 values";
    
    // Reset to current length (should be no-op)
    num_stack_reset_to_index(stack, 5);
    EXPECT_EQ(num_stack_length(stack), 5UL) << "reset to current length should not change stack";
    
    // Reset to beyond current length (should be no-op or clamped)
    num_stack_reset_to_index(stack, 10);
    EXPECT_EQ(num_stack_length(stack), 5UL) << "reset beyond length should not extend stack";
    
    // Reset to middle
    num_stack_reset_to_index(stack, 2);
    EXPECT_EQ(num_stack_length(stack), 2UL) << "reset to index 2 should leave 2 elements";
    
    // Verify remaining values are still accessible
    num_value_t* val0 = num_stack_get(stack, 0);
    num_value_t* val1 = num_stack_get(stack, 1);
    EXPECT_NE(val0, nullptr) << "first value should remain after reset";
    EXPECT_NE(val1, nullptr) << "second value should remain after reset";
    
    // Reset to 0 (clear stack)
    num_stack_reset_to_index(stack, 0);
    EXPECT_EQ(num_stack_length(stack), 0UL) << "reset to 0 should clear stack";
    EXPECT_TRUE(num_stack_is_empty(stack)) << "stack should be empty after reset to 0";
}

// Missing test 5: Null pointer handling
TEST_F(NumStackTest, NullPointerHandling) {
    // Test null stack parameter handling
    EXPECT_EQ(num_stack_length(nullptr), 0UL) << "length of null stack should be 0";
    EXPECT_TRUE(num_stack_is_empty(nullptr)) << "null stack should be considered empty";
    
    EXPECT_EQ(num_stack_push_long(nullptr, 123), nullptr) << "push to null stack should return null";
    EXPECT_EQ(num_stack_push_double(nullptr, 45.67), nullptr) << "push double to null stack should return null";
    
    EXPECT_EQ(num_stack_get(nullptr, 0), nullptr) << "get from null stack should return null";
    EXPECT_EQ(num_stack_peek(nullptr), nullptr) << "peek null stack should return null";
    
    EXPECT_FALSE(num_stack_pop(nullptr)) << "pop from null stack should return false";
    
    // Reset and destroy should handle null gracefully
    num_stack_reset_to_index(nullptr, 0); // Should not crash
    num_stack_destroy(nullptr); // Should not crash
    
    SUCCEED() << "null pointer handling completed without crashes";
}