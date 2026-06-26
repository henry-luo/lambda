// split() with axis for typed N-D arrays
// Phase 2 §1 — split(arr, n) along axis 0, split(arr, n, axis) along given axis.
// (split() also still works for strings — dispatch is on the first arg's type.)

// ============================================================
// 1-D SPLIT
// ============================================================
'=== 1-D split into n ==='
split([1, 2, 3, 4, 5, 6], 3)          // [[1,2],[3,4],[5,6]]
split([1, 2, 3, 4, 5, 6], 2)          // [[1,2,3],[4,5,6]]
split([1, 2, 3, 4, 5, 6], 6)          // [[1],[2],[3],[4],[5],[6]]
split([10, 20, 30, 40], 1)            // [[10,20,30,40]] (one part)

// ============================================================
// 2-D SPLIT — axis 0 (rows), default
// ============================================================
'=== 2-D split axis 0 ==='
let m = reshape([1, 2, 3, 4, 5, 6], [3, 2])
m                                      // [[1,2],[3,4],[5,6]]
split(m, 3)                            // 3 sub-arrays of shape (1,2)
split(m, 3, 0)                         // same, axis explicit

let m4 = reshape([1, 2, 3, 4, 5, 6, 7, 8], [4, 2])
split(m4, 2)                           // 2 sub-arrays of shape (2,2)

// ============================================================
// 2-D SPLIT — axis 1 (columns)
// ============================================================
'=== 2-D split axis 1 ==='
let m2 = reshape([1, 2, 3, 4, 5, 6], [2, 3])
m2                                     // [[1,2,3],[4,5,6]]
split(m2, 3, 1)                        // columns: [[[1],[4]],[[2],[5]],[[3],[6]]]
split(m2, 1, 1)                        // whole thing as one part

// ============================================================
// 3-D SPLIT
// ============================================================
'=== 3-D split ==='
let t = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
split(t, 2, 0)                         // split along leading axis
split(t, 2, 2)                         // split along last axis

// ============================================================
// SPLIT OF NON-CONTIGUOUS (transpose) — strided gather
// ============================================================
'=== split of transpose ==='
split(transpose(m2), 3, 0)            // transpose is (3,2): [[[1,4]],[[2,5]],[[3,6]]]
split(transpose(m), 2, 0)             // transpose is (2,3): split into 2 rows of (1,3)

// ============================================================
// FLOAT
// ============================================================
'=== float split ==='
split([1.5, 2.5, 3.5, 4.5], 2)        // [[1.5,2.5],[3.5,4.5]]

// ============================================================
// COMPOSITION — split parts are typed; reduce each
// ============================================================
'=== reduce split parts ==='
let parts = split([1, 2, 3, 4, 5, 6], 3)
sum(parts[0])                          // 3
sum(parts[1])                          // 7
sum(parts[2])                          // 11

// ============================================================
// STRING split still works (dispatch on first-arg type)
// ============================================================
'=== string split unaffected ==='
split("a,b,c", ",")                    // ('a', 'b', 'c')

// ============================================================
// ERRORS
// ============================================================
'=== errors ==='
split([1, 2, 3, 4, 5], 2)             // 5 not divisible by 2
split(m2, 2, 5)                        // axis out of range
split([1, 2, 3], 0)                   // n must be positive
