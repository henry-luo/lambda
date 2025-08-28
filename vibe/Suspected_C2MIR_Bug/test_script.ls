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
let single64 = [int64(9223372036854775806)]   // INT64_MAX - 1
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
max([min([int64(101), int64(200)]), max([int64(50), int64(151)])])
