// Test: Vector Algebra
// Layer: 2 | Category: function | Covers: dot, norm, cumsum, cumprod, argmin, argmax
// math is a built-in module, accessed via math.method() syntax

// ===== dot product =====
math.dot([1, 2, 3], [4, 5, 6])
math.dot([1, 0, 0], [0, 1, 0])
math.dot([2, 3], [4, 5])

// ===== norm =====
math.norm([3, 4])
math.norm([1, 0])
math.norm([0, 0, 0])

// ===== cumsum =====
math.cumsum([1, 2, 3, 4])
math.cumsum([10, 20, 30])
math.cumsum([1])

// ===== cumprod =====
math.cumprod([1, 2, 3, 4])
math.cumprod([2, 3, 5])

// ===== argmin =====
argmin([5, 3, 8, 1, 9])
argmin([1, 2, 3])
argmin([3, 2, 1])

// ===== argmax =====
argmax([5, 3, 8, 1, 9])
argmax([1, 2, 3])
argmax([3, 2, 1])

// ===== Combined =====
let v1 = [1, 2, 3]
let v2 = [4, 5, 6]
math.dot(v1, v2)
math.norm(v1)
math.cumsum(v1)
