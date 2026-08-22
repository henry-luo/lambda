// Comprehensive Int64 and Array Operations Test Suite
// Tests cover: i64 literals, conversions, arithmetic, arrays, and functions

"===== INT64 COMPREHENSIVE TEST SUITE ====="

'=== Basic Int64 Literals and Type Inference ==='

// Test 1: explicit i64 values
9223372036854775807i64
i64('-9223372036854775808')

// Test 2: i64 conversion from int32
i64(42)
i64(-123)

// Test 3: i64 conversion from string
i64('1234567890123456789')
i64('-9876543210987654321')

// Test 4: i64 conversion from float
i64(3.14)
i64(-2.71)

"=== Int64 Arithmetic Operations ==="

// Test 5: Basic arithmetic with i64
i64(100) + i64(200)
i64(500) - i64(300)
i64(10) * i64(20)
i64(1000) / i64(10)

// Test 6: Mixed int32/i64 arithmetic (should promote to i64)
42 + i64(1000000000000)
i64(2000000000000) - 100
50 * i64(3000000000)
i64(8000000000000) / 200

"=== ArrayInt64 Construction and Operations ===";

// Test 7: ArrayInt64 construction
[i64(1), i64(2), i64(3), i64(4), i64(5)];

// Test 8: ArrayInt64 with large values
[i64(1000000000000), i64(2000000000000), i64(3000000000000)];

// Test 9: Mixed construction (should promote to ArrayInt64)
[42, i64(1000000000000), 100]

"=== ArrayInt64 Utility Functions ==="

// Test 10: Length function
let arr64 = [i64(10), i64(20), i64(30), i64(40), i64(50)]
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
let large_arr64 = [i64(1000000000000), i64(2000000000000), i64(3000000000000), i64(4000000000000)]
len(large_arr64)
sum(large_arr64)
avg(large_arr64)
min(large_arr64)
max(large_arr64)

"=== ArrayInt64 Arithmetic Operations ===";

// Test 17: ArrayInt64 addition
[i64(100), i64(200)] + [i64(300), i64(400)];

// Test 18: ArrayInt64 subtraction
[i64(500), i64(600)] - [i64(100), i64(200)];

// Test 19: ArrayInt64 multiplication
[i64(10), i64(20)] * [i64(5), i64(3)]

"=== Edge Cases and Type Conversions ==="
// Test 20: Empty ArrayInt64
let empty64 = []
len(empty64)

// Test 21: Single element ArrayInt64
let single64 = [9223372000000000000i64]
len(single64)
sum(single64)
min(single64)
max(single64)

// Test 22: Zero values
i64(0);
[i64(0), i64(0), i64(0)]
sum([i64(0), i64(0), i64(0)])

// Test 23: Negative values in arrays
let neg_arr64 = [i64(-100), i64(-200), i64(-300)]
sum(neg_arr64)
min(neg_arr64)
max(neg_arr64)

"=== Complex Mixed Operations ===";

// Test 24: Complex expression with mixed types
(i64(1000) + 500) * i64(2)

// Test 25: Array operations with mixed results
sum([10, 20, 30]) + sum([i64(1000000000000), i64(2000000000000)])

// Test 26: Nested function calls
max([min([i64(100), i64(200)]), max([i64(50), i64(150)])])

"===== END INT64 COMPREHENSIVE TEST ====="
