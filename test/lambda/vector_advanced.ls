// Advanced Vector Operations Test Suite
// Tests complex scenarios, error cases, and integration with existing features

"===== VECTOR ADVANCED OPERATIONS TEST ====="

"=== Broadcasting Rules ==="

// Single element broadcasting to larger vector
[5] + [1, 2, 3]

// Scalar effectively broadcasts to any size
3 + [1, 2, 3, 4, 5]

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

// Product
prod([1, 2, 3, 4])
prod([2, 3, 5])

// Cumulative operations
cumsum([1, 2, 3, 4])
cumprod([1, 2, 3, 4])

// Argmin/Argmax
argmin([5, 2, 8, 1, 9])
argmax([5, 2, 8, 1, 9])

// Dot product and norm
dot([1, 2, 3], [4, 5, 6])
norm([3, 4])

// Fill
fill(5, 3)
fill(3, 0)

"=== Vector Indexing ==="

// Access vector elements
let numbers = [10, 20, 30, 40, 50]
numbers[0]
numbers[2]
numbers[4]

// Index with expressions
numbers[1 + 1]

"=== For loops with vectors ==="

for (x in [1, 2, 3]) x * 2

"=== Mixed Type Vectors ==="

// Heterogeneous vectors with error handling
[1, "hello", true, 3.14]

// Operations on mixed vectors produce per-element errors
[3, "str", 5] + 1

// Mostly numeric with some non-numeric
[1, 2, "three", 4] * 2

"=== Error Cases ==="

// Division by zero in vectors
[1, 2, 3] / 0
[6, 8, 10] / [2, 0, 2]

// Modulo by zero
[5, 10, 15] % 0
[10, 20] % [5, 0]

"=== Vector Comparisons ==="

// Vector equality as a whole
[1, 2, 3] == [1, 2, 3]
[1, 2, 3] == [1, 2, 4]
[1, 2] == [1, 2, 3]

"=== Performance Tests ==="

// Large vector operations
let large_vec = for (i in 1 to 100) i
sum(large_vec)
avg(large_vec)

"=== Nested Vector Operations ==="

// Vectors containing vectors (2D-like structures)
[[1, 2], [3, 4], [5, 6]]

// Vector of strings
["hello", "world", "test"]

"=== Vector with Ranges ==="

// Range operations
1 to 5
for (x in 1 to 5) x

// Range with scalar arithmetic
(1 to 5) * 2
(1 to 5) + 10

"=== Memory and Performance Edge Cases ==="

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

"End of Vector Advanced Operations Tests"
