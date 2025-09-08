// Comprehensive Int64 and Array Operations Test Suite
// Tests cover: int64 literals, conversions, arithmetic, arrays, and functions

"===== INT64 COMPREHENSIVE TEST SUITE ====="

"=== Basic Int64 Literals and Type Inference ==="

// Test 1: large int literals promoted to double
9223372036854775807
-9223372036854775808

// Test 2: int64 conversion from int32
int64(42)
int64(-123)

// Test 3: int64 conversion from string
int64('1234567890123456789')
int64('-9876543210987654321')

// Test 4: int64 conversion from float
int64(3.14)
int64(-2.71)

"=== Int64 Arithmetic Operations ==="

// Test 5: Basic arithmetic with int64
int64(100) + int64(200)
int64(500) - int64(300)
int64(10) * int64(20)
int64(1000) / int64(10)

// Test 6: Mixed int32/int64 arithmetic (should promote to int64)
42 + int64(1000000000000)
int64(2000000000000) - 100
50 * int64(3000000000)
int64(8000000000000) / 200

"=== ArrayInt64 Construction and Operations ==="

// Test 7: ArrayInt64 construction
[int64(1), int64(2), int64(3), int64(4), int64(5)]

// Test 8: ArrayInt64 with large values
[int64(1000000000000), int64(2000000000000), int64(3000000000000)]

// Test 9: Mixed construction (should promote to ArrayInt64)
[42, int64(1000000000000), 100]

"=== ArrayInt64 Utility Functions ==="

// Test 10: Length function
let arr64 = [int64(10), int64(20), int64(30), int64(40), int64(50)]
len(arr64)

// Test 11: Sum function
sum(arr64)

// Test 12: Average function  
avg(arr64)

// Test 13: Min function
min(arr64)

// Test 14: Max function
max(arr64)

"=== ArrayInt64 vs ArrayInt Comparison ==="

// Test 15: Regular int array operations
let arr32 = [10, 20, 30, 40, 50]
len(arr32)
sum(arr32)
avg(arr32)
min(arr32)
max(arr32)

"Test 16: Large ArrayInt64 operations"
let large_arr64 = [int64(1000000000000), int64(2000000000000), int64(3000000000000), int64(4000000000000)]
len(large_arr64)
sum(large_arr64)
avg(large_arr64)
min(large_arr64)
max(large_arr64)

"=== ArrayInt64 Arithmetic Operations ==="

// Test 17: ArrayInt64 addition
[int64(100), int64(200)] + [int64(300), int64(400)]

// Test 18: ArrayInt64 subtraction
[int64(500), int64(600)] - [int64(100), int64(200)]

// Test 19: ArrayInt64 multiplication
[int64(10), int64(20)] * [int64(5), int64(3)]

"=== Edge Cases and Type Conversions ==="
// Test 20: Empty ArrayInt64
let empty64 = []
len(empty64)

// Test 21: Single element ArrayInt64
let single64 = [int64(9223372000000000000)]
len(single64)
sum(single64)
min(single64)
max(single64)

// Test 22: Zero values
int64(0)
[int64(0), int64(0), int64(0)]
sum([int64(0), int64(0), int64(0)])

// Test 23: Negative values in arrays
let neg_arr64 = [int64(-100), int64(-200), int64(-300)]
sum(neg_arr64)
min(neg_arr64)
max(neg_arr64)

"=== Complex Mixed Operations ==="

// Test 24: Complex expression with mixed types
(int64(1000) + 500) * int64(2)

// Test 25: Array operations with mixed results
sum([10, 20, 30]) + sum([int64(1000000000000), int64(2000000000000)])

// Test 26: Nested function calls
max([min([int64(100), int64(200)]), max([int64(50), int64(150)])])

"===== END INT64 COMPREHENSIVE TEST ====="
