// Test: Vector Algebra
// Layer: 2 | Category: function | Covers: dot, norm, cumsum, cumprod, argmin, argmax

// ===== dot product =====
dot([1, 2, 3], [4, 5, 6])
dot([1, 0, 0], [0, 1, 0])
dot([2, 3], [4, 5])

// ===== norm =====
norm([3, 4])
norm([1, 0])
norm([0, 0, 0])

// ===== cumsum =====
cumsum([1, 2, 3, 4])
cumsum([10, 20, 30])
cumsum([1])

// ===== cumprod =====
cumprod([1, 2, 3, 4])
cumprod([2, 3, 5])

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
dot(v1, v2)
norm(v1)
cumsum(v1)
