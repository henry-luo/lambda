// ArrayNum algebraic & structural invariants, referenced against NumPy semantics.
// N-D value-equality uses flatten(x) == flatten(y) (a 1-D structural compare that
// ignores layout/dtype the way np.array_equal does); scalar reductions and shape
// lists compare directly.  Every invariant below holds in NumPy and prints `true`.

let a = reshape([1, 2, 3, 4, 5, 6], [2, 3])
let b = reshape([6, 5, 4, 3, 2, 1], [2, 3])
let p = reshape([1, 2, 3, 4], [2, 2])
let q = reshape([5, 6, 7, 8], [2, 2])
let id = reshape([1, 0, 0, 1], [2, 2])

// elementwise arithmetic laws
"add identity:";    (flatten(a + 0) == flatten(a))
"mul identity:";    (flatten(a * 1) == flatten(a))
"sub self = mul 0:"; (flatten(a - a) == flatten(a * 0))
"add commutes:";    (flatten(a + b) == flatten(b + a))
"mul commutes:";    (flatten(a * b) == flatten(b * a))
"distributive:";    (flatten(a * (a + b)) == flatten(a * a + a * b))
"double negate:";   (flatten(0 - (0 - a)) == flatten(a))

// transpose laws
"transpose involution:"; (flatten(transpose(transpose(a))) == flatten(a))
"transpose swaps shape:"; (shape(transpose(a)) == [3, 2])
"(AB)^T == B^T A^T:";     (flatten(transpose(matmul(p, q))) == flatten(matmul(transpose(q), transpose(p))))

// matmul laws
"right identity:"; (flatten(matmul(p, id)) == flatten(p))
"left identity:";  (flatten(matmul(id, p)) == flatten(p))
"associative:";    (flatten(matmul(matmul(p, q), id)) == flatten(matmul(p, matmul(q, id))))
"2x2 value:";      matmul(p, q)                         // [[19, 22], [43, 50]]
"dot product:";    matmul([1, 2, 3], [4, 5, 6])         // 32

// reduction consistency
"sum = sum of col sums:"; (sum(a) == sum(sum(a, 0)))
"sum = sum of row sums:"; (sum(a) == sum(sum(a, 1)))
"sum all:";   sum(a)                                    // 21
"col sums:";  sum(a, 0)                                 // [5, 7, 9]
"row sums:";  sum(a, 1)                                 // [6, 15]
"min <= max:"; (min(a) <= max(a))
"product:";   math.prod([1, 2, 3, 4, 5, 6])             // 720 (prod under the math module)

// reshape / ravel round-trips
"reshape preserves data:";  (flatten(reshape(a, [3, 2])) == flatten(a))
"reshape to 1-D = flatten:"; (reshape(a, [6]) == flatten(a))
"ravel of contiguous:";     (ravel(a) == flatten(a))

// broadcasting (outer ops)
let col = reshape([1, 2, 3], [3, 1])
let row = reshape([10, 20, 30], [1, 3])
"broadcast shape:"; (shape(col + row) == [3, 3])
"broadcast value:"; (col + row)                         // [[11,21,31],[12,22,32],[13,23,33]]
"scalar broadcast shape:"; (shape(a + 100) == [2, 3])

// concat / stack shape arithmetic
"concat axis0 shape:"; (shape(concat(a, a)) == [4, 3])
"stack shape:";        (shape(stack(a, a)) == [2, 2, 3])

// 1-D properties
"sort idempotent:"; (sort(sort([3, 1, 2])) == sort([3, 1, 2]))
"sort ascending:";  sort([3, 1, 2, 5, 4])               // [1, 2, 3, 4, 5]
"all positive:";    all([1, 2, 3] gt [0, 0, 0])
"any over 5:";      any([1, 2, 3] gt [5, 5, 5])         // false (none exceed 5)
"clip range:";      clip([-1, 5, 12], 0, 10)            // [0, 5, 10]
