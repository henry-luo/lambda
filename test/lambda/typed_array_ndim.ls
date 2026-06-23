// N-dimensional typed arrays via reshape (views with shape metadata)
// Phase 2b §1 of Lambda_Typed_Array2.md
// Covers: reshape, shape, ndim, nested format output, reductions on N-D.
//
// Limitation: arithmetic operations on N-D arrays currently return flat 1-D
// (shape is not yet preserved by vec_*). N-D format output, indexing, and
// reductions work correctly. Broadcasting/transpose/axis-reductions are future
// work.

// ============================================================
// SHAPE / NDIM
// ============================================================
'=== shape & ndim ==='
let a = [1, 2, 3, 4, 5, 6]
ndim(a)                                 // 1
shape(a)                                // [6]

let m = reshape(a, [2, 3])
ndim(m)                                 // 2
shape(m)                                // [2, 3]

let t = reshape(a, [1, 2, 3])
ndim(t)                                 // 3
shape(t)                                // [1, 2, 3]

// ============================================================
// NESTED PRINT FORMAT
// ============================================================
'=== 2-D matrices ==='
reshape([1, 2, 3, 4, 5, 6], [2, 3])    // [[1,2,3], [4,5,6]]
reshape([1, 2, 3, 4, 5, 6], [3, 2])    // [[1,2], [3,4], [5,6]]
reshape([1, 2, 3, 4, 5, 6], [6, 1])    // [[1],[2],[3],[4],[5],[6]]
reshape([1, 2, 3, 4, 5, 6], [1, 6])    // [[1,2,3,4,5,6]]

'=== 3-D tensors ==='
reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])

'=== float matrices ==='
reshape([0.5, 1.5, 2.5, 3.5], [2, 2])

// ============================================================
// VIEW SEMANTICS — reshape returns a view
// ============================================================
'=== reshape is a view ==='
let base = [10, 20, 30, 40, 50, 60]
let r = reshape(base, [2, 3])
is_view(r)
is_view(base)

// ============================================================
// REDUCTIONS work over flat data
// ============================================================
'=== sum/min/max ==='
let m2 = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 4])
sum(m2)                                 // 36
min(m2)                                 // 1
max(m2)                                 // 8
avg(m2)                                 // 4.5

'=== float reductions ==='
let fm = reshape([1.5, 2.5, 3.5, 4.5], [2, 2])
sum(fm)                                 // 12.0
avg(fm)                                 // 3.0

// ============================================================
// LEN — NumPy-style: shape[0] for N-D, total length for 1-D
// ============================================================
'=== len ==='
len(reshape([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], [3, 4]))   // 3 (leading axis)
len(reshape([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], [4, 3]))   // 4
len([1, 2, 3, 4, 5, 6])                                          // 6 (1-D unchanged)

// ============================================================
// ROUND-TRIP — reshape to N-D then flatten via reshape([n])
// ============================================================
'=== round-trip ==='
let orig = [1, 2, 3, 4, 5, 6]
let m3 = reshape(orig, [2, 3])
let flat = reshape(m3, [6])
flat
sum(flat) == sum(orig)

// ============================================================
// ERROR CASES
// ============================================================
'=== errors ==='
reshape([1, 2, 3, 4], [3, 3])           // shape mismatch (4 != 9)
reshape([1, 2, 3], [])                  // empty shape — invalid ndim
reshape("hello", [5])                   // not an array
