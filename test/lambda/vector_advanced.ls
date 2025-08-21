// Advanced Vector Operations Test Suite
// Tests complex scenarios, error cases, and integration with existing features

"===== VECTOR ADVANCED OPERATIONS TEST ====="

"=== Broadcasting Rules ==="

// Single element broadcasting to larger vector
// [5] + [1, 2, 3]        // Should become [6, 7, 8]

// Scalar effectively broadcasts to any size
3 + [1, 2, 3, 4, 5]

// Different size vectors (should error without broadcasting)
// [1, 2] + [3, 4, 5]     // Size mismatch error

"=== Complex Expressions ==="

// Nested arithmetic with vectors
([1, 2, 3] + [4, 5, 6]) * [2, 2, 2]

// Mixed scalar and vector operations
[1, 2, 3] * 2 + 5

// Multiple vector operations
[1, 2] + [3, 4] - [1, 1] + 10

// Precedence with vectors
[2, 3] * [4, 5] + [1, 2]

// Complex with parentheses
([1, 2] + [3, 4]) * ([5, 6] - [1, 1])

"=== Integration with Variables ==="

let vec1 = [1, 2, 3]
let vec2 = [4, 5, 6]
let scalar = 10

// Variable vector operations
vec1 + vec2
vec1 * scalar
scalar - vec1

// Complex variable expressions
(vec1 + vec2) * scalar

"=== Integration with Functions ==="

// Vector length
len([1, 2, 3, 4])
len([])
len([42])

// Sum of vector
sum([1, 2, 3, 4])
sum([1.5, 2.5, 3.5])
sum([])

// Average of vector
avg([1, 2, 3, 4])
avg([10, 20, 30])
avg([1.5, 2.5, 3.5])

// Min and max
min([5, 2, 8, 1])
max([5, 2, 8, 1])
min([3.7, 1.2, 5.8])
max([3.7, 1.2, 5.8])

"=== Vector Indexing ==="

// Access vector elements
let numbers = [10, 20, 30, 40, 50]
numbers[0]
numbers[2]
numbers[4]

// Out of bounds (should return null or error)
// numbers[10]
// numbers[-1]

// Index with expressions
numbers[1 + 1]
numbers[sum([1, 1])]

"=== Vectors in Control Flow ==="

// If expressions with vectors
let condition = true
if (condition) [1, 2, 3] else [4, 5, 6]

// For loops with vectors
for (x in [1, 2, 3]) x * 2

// Vector in conditions (truthiness)
if ([1, 2, 3]) "non-empty" else "empty"
if ([]) "non-empty" else "empty"

"=== Mixed Type Vectors ==="

// Heterogeneous vectors
[1, "hello", true, 3.14]

// Operations on mixed vectors (should handle gracefully)
// [1, "hello"] + 5        // Should error or handle strings specially

// Mostly numeric with some non-numeric
// [1, 2, "three", 4] * 2  // Should handle appropriately

"=== Error Cases ==="

// Division by zero in vectors
// [1, 2, 3] / 0           // All elements divided by zero
// [6, 8, 10] / [2, 0, 2]  // One element has division by zero

// Modulo by zero
// [5, 10, 15] % 0
// [10, 20] % [5, 0]

// Invalid operations
// [1, 2, 3] + "hello"     // Type error
// true * [1, 2, 3]        // Type error

"=== Vector Comparisons ==="

// Element-wise comparisons (if supported)
// [1, 2, 3] > [0, 2, 4]   // [true, false, false]
// [1, 2, 3] == [1, 2, 3]  // [true, true, true]
// [1, 2, 3] == 2          // [false, true, false]

// Vector equality as a whole
[1, 2, 3] == [1, 2, 3]
[1, 2, 3] == [1, 2, 4]
[1, 2] == [1, 2, 3]

"=== Performance Tests ==="

// Large vector operations
let large_vec = for (i in 1 to 100) i
large_vec + large_vec
large_vec * 2

// Chained large operations
let big_result = large_vec * 2 + large_vec / 2

"=== Vector Transformations ==="

// Vector to list and back (if different)
let original = [1, 2, 3]
// let as_list = to_list(original)
// let back_to_vector = to_vector(as_list)

"=== Nested Vector Operations ==="

// Vectors containing vectors (2D-like structures)
[[1, 2], [3, 4], [5, 6]]

// Operations on nested structures
// [[1, 2], [3, 4]] + 1    // Should add 1 to each inner vector?

"=== String and Vector Operations ==="

// String repetition with vectors (if supported)
// "abc" * [1, 2, 3]       // ["abc", "abcabc", "abcabcabc"]

// Vector of strings
["hello", "world", "test"]

// String operations on string vectors
// ["a", "b", "c"] + "!"   // ["a!", "b!", "c!"]

"=== Vector with Ranges ==="

// Range to vector conversion
1 to 5
for (x in 1 to 5) x

// Vector operations with ranges
// (1 to 5) + [1, 1, 1, 1, 1]
// [1, 2, 3, 4, 5] * (1 to 5)

"=== Memory and Performance Edge Cases ==="

// Very large vectors
// let huge = for (i in 1 to 10000) i
// huge + huge

// Deeply nested operations
// [1, 2, 3] + [4, 5, 6] + [7, 8, 9] + [10, 11, 12]

// Repeated operations
let base = [1, 2, 3]
base + base + base + base

"=== Type System Integration ==="

// Vector type checking
type([1, 2, 3])
type([1.5, 2.5])
type([])

// Mixed type handling
type([1, "hello", true])

"=== Vector Aggregation Chain ==="

// Complex aggregation operations
sum([1, 2, 3] * [2, 2, 2])
avg([10, 20, 30] + [5, 5, 5])
max([1, 5, 3] + [2, 1, 4])

"=== Conditional Vector Operations ==="

// Vector operations in conditional expressions
let choice = 1
if (choice == 1) [1, 2, 3] + 5 else [4, 5, 6] * 2

// Vectors in boolean contexts
if ([1, 2, 3]) "has elements" else "empty"
if ([]) "has elements" else "empty"

"=== Vector Slicing (Future Feature) ==="

// These would be advanced features for later implementation
// [1, 2, 3, 4, 5][1:3]    // Slice notation
// [1, 2, 3, 4, 5][::2]    // Step notation

"End of Vector Advanced Operations Tests"
