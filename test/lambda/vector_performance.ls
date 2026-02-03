// Vector Performance and Edge Cases Test Suite  
// Tests performance characteristics, memory usage, and edge cases

"===== VECTOR PERFORMANCE AND EDGE CASES TEST ====="

"=== Empty Vector Handling ==="

// Empty vector operations
[]
len([])

// Operations with empty vectors (should handle gracefully)
// [] + 5              // Should return empty or error appropriately
// 5 + []              // Should return empty or error appropriately  
// [] + []             // Should return empty
// [] * []             // Should return empty

// Empty vector with non-empty
// [] + [1, 2, 3]      // Should handle appropriately
// [1, 2, 3] + []      // Should handle appropriately

"=== Single Element Vectors ==="

// Single element vector operations
[42]
len([42])

// Single element arithmetic
[5] + 3
3 * [7]
[16] / [4]
[10] - [3]

// Single element with multi-element
[5] + [1, 2, 3]      // Broadcasting test
[1, 2, 3] * [2]      // Broadcasting test

"=== Large Vector Performance ==="

// Large vector creation
let vec_100 = for (i in 1 to 100) i
let vec_1000 = for (i in 1 to 1000) i

// Large vector operations
vec_100 + vec_100
vec_100 * 2
vec_100 - 50

// Very large vectors (performance test)
// let vec_10000 = for (i in 1 to 10000) i
// vec_10000 + vec_10000    // Should complete in reasonable time

"=== Chained Operations Performance ==="

// Multiple chained operations
let base = [1, 2, 3, 4, 5]
base + base + base + base

// Complex chained expression
(base * 2 + base) / (base + 1)

// Long chain
base + 1 + 2 + 3 + 4 + 5

"=== Memory Edge Cases ==="

// Repeated large allocations
let mem_test1 = [1, 2, 3] * 1000
let mem_test2 = [1, 2, 3] + [4, 5, 6]
let mem_test3 = for (i in 1 to 500) i * 2

"=== Numeric Edge Cases ==="

// Very large numbers
[999999999, 1000000000] + [1, 1]
[9223372036854775806, 9223372036854775807] + [1, 1]

// Very small numbers  
[0.000001, 0.000002] * 1000000
[1e-10, 2e-10] + [1e-10, 1e-10]

// Mixed scales
[1e10, 1e-10] * [1e-10, 1e10]

// Zero operations
[0, 0, 0] + [1, 2, 3]
[1, 2, 3] * [0, 0, 0]
[0] / [1]

// Infinity and NaN (if supported)
// [1, 2, 3] / [0, 1, 0]   // Should handle division by zero
// [inf] + [1, 2, 3]       // If infinity is supported

"=== Type Promotion Edge Cases ==="

// Integer overflow promotion
[2147483647] + [1]       // Should promote to long if needed

// Float precision
[0.1, 0.2] + [0.2, 0.1]  // Floating point arithmetic

// Mixed precision
[1.0000000000000001, 1.0000000000000002] + [1e-16, 1e-16]

"=== Vector Size Mismatches ==="

// Different sizes (error cases or broadcasting)
// [1, 2] + [3, 4, 5]           // 2 vs 3 elements
// [1] + [2, 3, 4, 5]           // 1 vs 4 elements  
// [1, 2, 3, 4, 5] + [6]        // 5 vs 1 elements

"=== Deeply Nested Expressions ==="

// Deep nesting
((([1, 2] + [3, 4]) * [2, 2]) - [1, 1]) / [2, 2]

// Complex precedence
[1, 2] + [3, 4] * [5, 6] - [7, 8] / [2, 2]

"=== Error Recovery ==="

// Operations that should error gracefully  
// [1, 2, 3] + "hello"          // Type error
// [1, 2, 3] / [0, 1, 0]        // Division by zero
// true * [1, 2, 3]             // Invalid operand type

"=== Vector Construction Stress ==="

// Many different vector types
[1, 2, 3]
[1.5, 2.5, 3.5]  
[true, false, true]
["a", "b", "c"]
[1, "mixed", 3.14, true]

// Empty and single element variations
[]
[null]
[0]
[false]
[""]

"=== Integration Stress Tests ==="

// Vector operations within complex expressions
let complex_result = sum([1, 2, 3] * [2, 2, 2]) + avg([10, 20, 30] / [2, 4, 6])

// Vector operations in control flow
for (multiplier in [1, 2, 3, 4, 5]) sum([10, 20, 30] * multiplier)

// Nested vector operations
let nested_calc = ([1, 2, 3] + [4, 5, 6]) * ([7, 8, 9] - [1, 1, 1])

"=== Boundary Value Testing ==="

// Minimum and maximum values
let min_vec = [-2147483648, -1000000000]
let max_vec = [2147483647, 1000000000]
min_vec + max_vec

// Edge of precision
[1.0, 2.0] + [1e-15, 2e-15]

// Near-zero values
[1e-308, 1e-307] * [1e308, 1e307]

"=== Vector Truthiness ==="

// Vector truthiness in boolean contexts
if ([1, 2, 3]) "truthy vector" else "falsy vector"
if ([]) "truthy empty" else "falsy empty"  
if ([0]) "truthy zero" else "falsy zero"
if ([false]) "truthy false" else "falsy false"

"=== Vector Comparison Chains ==="

// Complex comparison scenarios (if supported)
// [1, 2, 3] == [1, 2, 3] and [4, 5, 6] != [1, 2, 3]

"=== Memory Fragmentation Test ==="

// Create and discard many vectors
[for (i in 1 to 50) [i, i*2, i*3] + [1, 2, 3]]

"=== Vector with Range Integration ==="

// Combine vectors with ranges
let range_vec = for (x in 1 to 10) x
[range_vec + [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]]

// Range to vector conversion performance
[for (x in 1 to 100) x]

"=== Function Call Overhead ==="

// Function calls with vectors
let test_vec = [1, 2, 3, 4, 5]
len(test_vec)
sum(test_vec)
avg(test_vec)
min(test_vec)  
max(test_vec)

// Chained function calls
max(sum([1, 2, 3] * [2, 2, 2]))

"=== String Vector Operations ==="

// String vector operations (if supported)
["hello", "world"] + ["!", "!"]     // String concatenation
// "test" * [1, 2, 3]               // String repetition

"=== Vector Serialization ==="

// Vector conversion to string representation
format([1, 2, 3], 'json')
// string([1, 2, 3])                // If string conversion supported

"End of Vector Performance and Edge Cases Tests"
