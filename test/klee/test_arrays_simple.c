/**
 * @file test_arrays_simple.c
 * @brief Simplified KLEE test for array operations
 * @author Henry Luo
 * 
 * This test harness uses KLEE symbolic execution to discover
 * array handling issues without depending on Lambda Script headers.
 */

#include <klee/klee.h>
#include <assert.h>
#include <stdint.h>

#define ARRAY_SIZE 8

// Simple array functions to test
int array_sum(int* arr, size_t size) {
    if (!arr || size == 0) return 0;
    
    int sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += arr[i];
    }
    return sum;
}

int array_find(int* arr, size_t size, int target) {
    if (!arr || size == 0) return -1;
    
    for (size_t i = 0; i < size; i++) {
        if (arr[i] == target) {
            return (int)i;
        }
    }
    return -1;
}

void array_reverse(int* arr, size_t size) {
    if (!arr || size <= 1) return;
    
    for (size_t i = 0; i < size / 2; i++) {
        int temp = arr[i];
        arr[i] = arr[size - 1 - i];
        arr[size - 1 - i] = temp;
    }
}

int array_max(int* arr, size_t size) {
    if (!arr || size == 0) return INT32_MIN;
    
    int max = arr[0];
    for (size_t i = 1; i < size; i++) {
        if (arr[i] > max) {
            max = arr[i];
        }
    }
    return max;
}

int main() {
    int array[ARRAY_SIZE];
    size_t array_size;
    int target;
    
    // Make array and size symbolic
    klee_make_symbolic(array, sizeof(array), "array");
    klee_make_symbolic(&array_size, sizeof(array_size), "array_size");
    klee_make_symbolic(&target, sizeof(target), "target");
    
    // Constrain array size to valid range
    klee_assume(array_size <= ARRAY_SIZE);
    
    // Constrain array elements to reasonable range
    for (int i = 0; i < ARRAY_SIZE; i++) {
        klee_assume(array[i] >= -100 && array[i] <= 100);
    }
    
    // Constrain target to reasonable range
    klee_assume(target >= -100 && target <= 100);
    
    // Test array sum
    if (array_size > 0) {
        int sum = array_sum(array, array_size);
        // Verify sum is within expected bounds
        assert(sum >= -100 * (int)array_size);
        assert(sum <= 100 * (int)array_size);
    }
    
    // Test array find
    int found_index = array_find(array, array_size, target);
    if (found_index >= 0) {
        assert(found_index < (int)array_size);
        assert(array[found_index] == target);
    }
    
    // Test array max
    if (array_size > 0) {
        int max = array_max(array, array_size);
        assert(max >= -100 && max <= 100);
        
        // Verify max is actually in the array
        int found = 0;
        for (size_t i = 0; i < array_size; i++) {
            if (array[i] == max) {
                found = 1;
                break;
            }
        }
        assert(found == 1);
    }
    
    // Test array reverse (just ensure no crashes)
    int original[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        original[i] = array[i];
    }
    
    array_reverse(array, array_size);
    
    // Verify reverse worked correctly for non-empty arrays
    if (array_size > 1) {
        assert(array[0] == original[array_size - 1]);
        assert(array[array_size - 1] == original[0]);
    }
    
    return 0;
}
