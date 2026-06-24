// matmul / concat / stack for typed N-D arrays
// Phase 2 — N-D structural ops (Lambda_Typed_Array2.md §1)

// ============================================================
// MATMUL — 2-D · 2-D
// ============================================================
'=== matmul 2x2 ==='
let a = reshape([1, 2, 3, 4], [2, 2])
let b = reshape([5, 6, 7, 8], [2, 2])
matmul(a, b)                   // [[19, 22], [43, 50]]
shape(matmul(a, b))            // [2, 2]

'=== matmul 2x3 . 3x2 ==='
let p = reshape([1, 2, 3, 4, 5, 6], [2, 3])
let q = reshape([7, 8, 9, 10, 11, 12], [3, 2])
matmul(p, q)                   // [[58, 64], [139, 154]]
shape(matmul(p, q))            // [2, 2]

'=== matmul float ==='
let fa = reshape([1.0, 2.0, 3.0, 4.0], [2, 2])
matmul(fa, fa)                 // [[7, 10], [15, 22]]

'=== matmul identity ==='
let id = reshape([1, 0, 0, 1], [2, 2])
matmul(a, id)                  // a unchanged: [[1, 2], [3, 4]]

// ============================================================
// MATMUL — vector cases
// ============================================================
'=== dot (1-D . 1-D) ==='
matmul([1, 2, 3], [4, 5, 6])   // 32

'=== vector . matrix (1-D . 2-D) ==='
matmul([1, 2], reshape([1, 2, 3, 4], [2, 2]))   // [7, 10]

'=== matrix . vector (2-D . 1-D) ==='
matmul(reshape([1, 2, 3, 4], [2, 2]), [1, 1])   // [3, 7]

'=== matmul on transposed operand ==='
matmul(transpose(p), q)        // (3x2)·(3x2) -> dim mismatch -> error
matmul(p, transpose(reshape([7, 8, 9, 10, 11, 12], [2, 3])))   // (2x3)·(3x2) -> [[50,68],[122,167]]

// ============================================================
// CONCAT — along axis 0
// ============================================================
'=== concat 1-D ==='
concat([1, 2, 3], [4, 5])      // [1, 2, 3, 4, 5]

'=== concat 2-D (stack rows) ==='
concat([[1, 2], [3, 4]], [[5, 6]])             // [[1, 2], [3, 4], [5, 6]]
shape(concat([[1, 2], [3, 4]], [[5, 6]]))      // [3, 2]

'=== concat 2-D (multiple rows each) ==='
concat([[1, 2, 3]], [[4, 5, 6], [7, 8, 9]])    // [[1,2,3],[4,5,6],[7,8,9]]

'=== concat with type promotion ==='
concat([1, 2], [3.5, 4.5])     // [1, 2, 3.5, 4.5] (float)

'=== concat dim mismatch -> error ==='
concat([[1, 2]], [[3, 4, 5]])  // trailing dim mismatch

// ============================================================
// STACK — along new leading axis
// ============================================================
'=== stack 1-D -> 2-D ==='
stack([1, 2, 3], [4, 5, 6])    // [[1, 2, 3], [4, 5, 6]]
shape(stack([1, 2, 3], [4, 5, 6]))             // [2, 3]

'=== stack 2-D -> 3-D ==='
stack(reshape([1, 2, 3, 4], [2, 2]), reshape([5, 6, 7, 8], [2, 2]))
shape(stack(reshape([1, 2, 3, 4], [2, 2]), reshape([5, 6, 7, 8], [2, 2])))   // [2, 2, 2]

'=== stack shape mismatch -> error ==='
stack([1, 2], [3, 4, 5])

// ============================================================
// COMPOSITION
// ============================================================
'=== matmul then reduce ==='
sum(matmul(a, b))              // 19+22+43+50 = 134

'=== concat then matmul ==='
let big = concat([[1, 2], [3, 4]], [[5, 6]])   // 3x2
matmul(transpose(big), big)    // (2x3)·(3x2) -> 2x2

'=== stack then transpose ==='
transpose(stack([1, 2, 3], [4, 5, 6]))         // [[1,4],[2,5],[3,6]]

// ============================================================
// ERRORS
// ============================================================
'=== errors ==='
matmul(42, [1, 2])             // not arrays
matmul([1, 2], [1, 2, 3])      // length mismatch (dot)
