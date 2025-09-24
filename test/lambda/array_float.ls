// Comprehensive ArrayFloat Test Suite
// This file consolidates all ArrayFloat functionality tests
// Tests cover: construction, indexing, arithmetic, statistics, edge cases

// ========================================
"Basic Array Construction Tests:"
// ========================================

// Test 1: Basic float array construction with parentheses syntax
let float_list = (1.5, 2.7, 3.14)
float_list

// Test 2: Basic float array construction with bracket syntax
let float_arr = [1.0, 2.0, 3.0, 4.0, 5.0]
float_arr

// Test 3: Empty array construction
let empty_arr = []
empty_arr

// Test 4: Single element array
let single_arr = [42.0]
single_arr

// Test 5: Mixed precision floats
let mixed_arr = [1.1, 2.2, 3.3, 4.444, 5.55555]
mixed_arr

// ========================================
"Array Indexing Tests:"
// ========================================

// Test 6: Basic indexing with list syntax
let list_index = float_list[1]  // Should be 2.7
list_index

// Test 7: Basic indexing with array syntax
let array_index = float_arr[2]  // Should be 3.0
array_index

// Test 8: First element access
let first_elem = float_arr[0]  // Should be 1.0
first_elem

// Test 9: Last element access
let last_elem = float_arr[4]  // Should be 5.0
last_elem

// ========================================
"Statistical Functions Tests"
// ========================================

'Test 10: Sum function on various arrays'
let sum_float = sum(float_arr)        // Should be 15.0 (1+2+3+4+5)
let sum_mixed = sum(mixed_arr)        // Should be 16.59955
let sum_single = sum(single_arr)      // Should be 42.0
let sum_empty = sum(empty_arr)        // Should be 0
sum_float; sum_mixed; sum_single; sum_empty

"Test 11: Average function on various arrays"
let avg_float = avg(float_arr)        // Should be 3.0 (15/5)
let avg_mixed = avg(mixed_arr)        // Should be 3.31991
let avg_single = avg(single_arr)      // Should be 42.0
avg_float; avg_mixed; avg_single

"Test 12: Minimum function on various arrays"
let min_float = min(float_arr)        // Should be 1.0
let min_mixed = min(mixed_arr)        // Should be 1.1
let min_single = min(single_arr)      // Should be 42.0
min_float; min_mixed; min_single

"Test 13: Maximum function on various arrays"
let max_float = max(float_arr)        // Should be 5.0
let max_mixed = max(mixed_arr)        // Should be 5.55555
let max_single = max(single_arr)      // Should be 42.0
max_float; max_mixed; max_single

// ========================================
"Edge Cases and Error Handling:"
// ========================================

// Test 14: Empty array edge cases
let empty_min = min(empty_arr)        // Should be error or null
let empty_max = max(empty_arr)        // Should be error or null
empty_min; empty_max

// Test 15: Large arrays
let large_arr = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]
let large_sum = sum(large_arr)        // Should be 55.0
let large_avg = avg(large_arr)        // Should be 5.5
let large_min = min(large_arr)        // Should be 1.0
let large_max = max(large_arr)        // Should be 10.0
large_sum; large_avg; large_min; large_max

"Test 16: Negative numbers"
let negative_arr = [-1.0, -2.0, 3.0, -4.0, 5.0]
let neg_sum = sum(negative_arr)       // Should be 1.0
let neg_avg = avg(negative_arr)       // Should be 0.2
let neg_min = min(negative_arr)       // Should be -4.0
let neg_max = max(negative_arr)       // Should be 5.0
neg_sum; neg_avg; neg_min; neg_max

// ========================================
"Advanced Mathematical Operations:"
// ========================================

// Test 19: Decimal precision preservation
let precision_arr = [0.1, 0.2, 0.3]
let precision_sum = sum(precision_arr)  // Test floating point precision
precision_sum

// Test 20: Zero values handling
let zero_arr = [0.0, 1.0, 0.0, 2.0, 0.0]
let zero_sum = sum(zero_arr)           // Should be 3.0
let zero_avg = avg(zero_arr)           // Should be 0.6
let zero_min = min(zero_arr)           // Should be 0.0
let zero_max = max(zero_arr)           // Should be 2.0
zero_sum; zero_avg; zero_min; zero_max

// ========================================
// Test Summary and Results
// ========================================

// Test 21: Final validation arrays
let test_result_arr1 = [1.0, 2.0, 3.0, 4.0, 5.0]
let test_result_arr2 = [10.0, 20.0, 30.0, 40.0, 50.0]

// Expected results:
// arr1: min=1.0, max=5.0, sum=15.0, avg=3.0
// arr2: min=10.0, max=50.0, sum=150.0, avg=30.0
let final_min1 = min(test_result_arr1)
let final_max1 = max(test_result_arr1) 
let final_sum1 = sum(test_result_arr1)
let final_avg1 = avg(test_result_arr1)

let final_min2 = min(test_result_arr2)
let final_max2 = max(test_result_arr2)
let final_sum2 = sum(test_result_arr2)
let final_avg2 = avg(test_result_arr2)

// Return final test results
final_min1; final_max1; final_sum1; final_avg1;
final_min2; final_max2; final_sum2; final_avg2

"For loop with ArrayFloat:"
for (val in [null, 0.7, 0.2, 0.9]) if (val != null) val else 0.0
