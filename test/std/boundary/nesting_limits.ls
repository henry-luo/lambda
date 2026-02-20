// Test: Deep Nesting
// Layer: 2 | Category: boundary | Covers: nested expressions, data structures

// Deeply nested arithmetic
((((1 + 2) * 3) + 4) * 5)
(1 + (2 + (3 + (4 + 5))))

// Nested if expressions
(if (true) (if (true) (if (true) 42 else 0) else 0) else 0)

// Nested function calls
abs(abs(abs(-42)))
len(string(len([1, 2, 3])))

// Nested arrays
[[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
[[[1, 2], [3, 4]], [[5, 6], [7, 8]]][0][1][0]
