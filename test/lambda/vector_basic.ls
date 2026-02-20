// Comprehensive Vector Operations Test Suite - Basic Operations
// Tests fundamental scalar-vector and vector-vector arithmetic

"===== VECTOR BASIC OPERATIONS TEST ====="

"=== Scalar + Vector Operations ==="

// Integer scalar with integer vector
1 + [2, 3, 4]

// Float scalar with integer vector (should promote to float vector)
1.5 + [2, 3, 4]

// Integer scalar with float vector
2 + [1.5, 2.5, 3.5]

// Float scalar with float vector
2.5 + [1.5, 2.5, 3.5]

// Negative scalar
-1 + [1, 2, 3]

// Zero scalar
0 + [5, 10, 15]

"=== Vector + Scalar Operations ==="

// Integer vector with integer scalar
[1, 2, 3] + 10

// Integer vector with float scalar
[1, 2, 3] + 2.5

// Float vector with integer scalar
[1.5, 2.5, 3.5] + 2

// Float vector with float scalar
[1.5, 2.5, 3.5] + 1.5

// Vector with negative scalar
[5, 10, 15] + (-2)

// Vector with zero scalar
[1, 2, 3] + 0

"=== Vector + Vector Operations ==="

// Same size integer vectors
[1, 2, 3] + [4, 5, 6]

// Same size float vectors
[1.5, 2.5, 3.5] + [0.5, 1.5, 2.5]

// Mixed type vectors (should promote to float)
[1, 2, 3] + [1.5, 2.5, 3.5]

// Single element vectors
[5] + [3]

// Empty vectors
[] + []

"=== Subtraction Operations ==="

// Scalar - Vector
10 - [1, 2, 3]

// Vector - Scalar
[10, 20, 30] - 5

// Vector - Vector
[10, 20, 30] - [1, 2, 3]

// With floats
5.5 - [1.5, 2.5]

// Negative results
[1, 2, 3] - 5

"=== Multiplication Operations ==="

// Scalar * Vector
3 * [1, 2, 3]

// Vector * Scalar
[2, 4, 6] * 2

// Vector * Vector (element-wise)
[2, 3, 4] * [1, 2, 3]

// With floats
2.5 * [2, 4]

// Zero multiplication
0 * [1, 2, 3]

// One multiplication
1 * [5, 10, 15]

"=== Division Operations ==="

// Scalar / Vector
12 / [2, 3, 4]

// Vector / Scalar
[10, 20, 30] / 5

// Vector / Vector
[12, 15, 18] / [3, 5, 6]

// Float division
[7.5, 10.0] / 2.5

// Results in floats
[1, 2, 3] / 2

"=== Power Operations ==="

// Scalar ^ Vector
2 ** [1, 2, 3]

// Vector ^ Scalar
[2, 3, 4] ** 2

// Vector ^ Vector
[2, 3, 4] ** [1, 2, 3]

// Float powers
2.5 ** [1, 2]

// Zero and one powers
[5, 10] ** 0
[5, 10] ** 1

"=== Modulo Operations ==="

// Scalar % Vector
10 % [3, 4, 6]

// Vector % Scalar
[10, 15, 20] % 3

// Vector % Vector
[10, 15, 20] % [3, 4, 6]

// Edge cases
[5, 6, 7] % 2

"=== Mixed Vector Sizes ==="

// Note: These might error or use broadcasting rules

// Different sizes - should error or broadcast
// [1, 2] + [3, 4, 5]

// Single element with multi-element (broadcasting candidate)
// [5] + [1, 2, 3]

"=== Edge Cases ==="

// Empty vector operations
// [] + 5
// 5 + []
// [] * []

// Single element vectors
[42] + 8
8 * [3]
[16] / [4]

// Very large numbers
[999999999] + 1
1000000000 * [1, 2]

// Very small numbers
[0.0001, 0.0002] * 10000

"=== Type Preservation Tests ==="

// Integer operations should stay integer when possible
[1, 2, 3] + [4, 5, 6]

// Any float should promote to float
[1, 2] + 3.0
2.5 * [1, 2]

// Mixed vectors
[1, 2.5, 3] + 1

"=== Chained Operations ==="

// Multiple operations in sequence
[1, 2, 3] + [1, 1, 1] * 2

// Parentheses precedence
([1, 2, 3] + [1, 1, 1]) * 2

// Complex chaining
[1, 2, 3] * 2 + [5, 5, 5] / 5

"=== Vector Construction Validation ==="

// Verify vector creation works correctly
[1, 2, 3]
[1.5, 2.5, 3.5]
[1, 2.5, 3]
[]
[42]

"End of Vector Basic Operations Tests"
