// NumPy-style shape broadcasting on N-D typed arrays.
// Phase 2b §1 of Lambda_Typed_Array2.md
// Rules: right-align dims; pad shorter with leading 1s; at each axis dims must
// be equal or one of them must be 1 (the size-1 axis is "stretched").

// ============================================================
// CLASSIC: (3,1) op (1,4) → (3,4) outer product
// ============================================================
'=== (3,1) * (1,4) → (3,4) ==='
let col = reshape([1, 2, 3], [3, 1])
let row = reshape([10, 20, 30, 40], [1, 4])
col * row
shape(col * row)

'=== (3,1) + (1,4) ==='
col + row

// ============================================================
// 2-D op SCALAR — shape preserved
// ============================================================
'=== mat + scalar ==='
let mat = reshape([1, 2, 3, 4, 5, 6], [2, 3])
mat + 10
mat * 2
mat - 1
shape(mat + 100)

// ============================================================
// 2-D op 1-D — 1-D broadcasts across rows
// ============================================================
'=== (2,3) * (3,) ==='
let v = [10, 20, 30]
mat * v
mat + v

// ============================================================
// SAME-SHAPE element-wise (no stretching)
// ============================================================
'=== same shape ==='
mat + mat
mat * mat

// ============================================================
// HIGHER DIMENSIONS
// ============================================================
'=== (2,2,2) + (1,1,2) ==='
let t = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
let bcast = reshape([10, 20], [1, 1, 2])
t + bcast
shape(t + bcast)

'=== (2,2,2) * (2,1,1) ==='
let layer_mul = reshape([10, 100], [2, 1, 1])
t * layer_mul

// ============================================================
// MIXED INT/FLOAT (result promotes to float)
// ============================================================
'=== int * float ==='
let im = reshape([1, 2, 3, 4], [2, 2])
let fm = reshape([0.5, 1.5, 2.5, 3.5], [2, 2])
im * fm

'=== division produces float ==='
let m1 = reshape([10, 20, 30, 40], [2, 2])
let m2 = reshape([2, 4, 5, 8], [2, 2])
m1 / m2

// ============================================================
// 1-D broadcast (no shape metadata) still works as before
// ============================================================
'=== 1-D scalar broadcast still flat ==='
[1, 2, 3] + 10

'=== 1-D same length ==='
[1, 2, 3] + [10, 20, 30]

// ============================================================
// ERRORS — incompatible shapes
// ============================================================
'=== incompatible shapes ==='
reshape([1, 2, 3, 4, 5, 6], [2, 3]) + reshape([1, 2, 3, 4], [2, 2])
reshape([1, 2, 3], [3, 1]) * reshape([1, 2], [2, 1])    // (3,1) vs (2,1) — dim 0 mismatch

// ============================================================
// CHAINING — broadcasting through pipeline
// ============================================================
'=== chained ==='
let a = reshape([1, 2, 3], [3, 1])
let b = reshape([10, 20], [1, 2])
let c = reshape([100, 200, 300], [3, 1])
(a + b) * c
