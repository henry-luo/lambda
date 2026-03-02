// reduce() function tests

// Test 1: Sum of integers
reduce([1, 2, 3, 4], (a, b) => a + b)

// Test 2: Product of integers
reduce([1, 2, 3, 4, 5], (a, b) => a * b)

// Test 3: Single element returns unchanged
reduce([42], (a, b) => a + b)

// Test 4: Two elements
reduce([10, 20], (a, b) => a + b)

// Test 5: Min via reduce using min function
reduce([3, 7, 2, 9, 1], (a, b) => min(a, b))

// Test 6: Sum of floats
reduce([1.5, 2.5, 3.0], (a, b) => a + b)
