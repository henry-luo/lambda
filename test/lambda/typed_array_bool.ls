// ELEM_BOOL — typed boolean arrays
// Phase 2a of Lambda_Typed_Array2.md
// Covers: construction, indexing, iteration, any()/all() reductions, edge cases

// ============================================================
// CONSTRUCTION
// ============================================================
'=== construction ==='
[true, false, true]
[false]
[true, true, true, true]
[]    // empty literal — not a bool array per se

// ============================================================
// INDEXING
// ============================================================
'=== indexing ==='
let arr = [true, false, true, false, true]
arr[0]
arr[1]
arr[2]
arr[3]
arr[4]

// ============================================================
// LEN
// ============================================================
'=== len ==='
len([true, false, true])
len([true])
len([false, false, false, false])

// ============================================================
// any()/all() REDUCTIONS
// ============================================================
'=== all ==='
all([true, true, true])
all([true, false, true])
all([false, false, false])
all([])                       // empty: vacuously true

'=== any ==='
any([true, true, true])
any([true, false, true])
any([false, false, false])
any([])                       // empty: false

// ============================================================
// any/all on mixed value sources (truthiness coercion)
// ============================================================
'=== truthiness ==='
all([1, 2, 3])               // all non-zero ints
all([1, 0, 3])               // contains 0
any([0, 0, 1])               // single non-zero
any([0, 0, 0])               // all zeros
all([1.0, 2.5, 3.14])
any([0.0, 0.0, -0.0])

// ============================================================
// ITERATION (for-in)
// ============================================================
'=== for-in over bool array ==='
for (x in [true, false, true, false]) x

// ============================================================
// COMPOSITION
// ============================================================
'=== composition ==='
let big = [true, true, false, true, true]
all(big)
any(big)
len(big)

// nested bool arrays
'=== array of arrays (not promoted) ==='
let mat = [[true, false], [false, true]]
len(mat)
mat[0]
mat[1]
