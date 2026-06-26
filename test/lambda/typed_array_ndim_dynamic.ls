// Dynamic runtime auto-promotion of nested arrays to N-D ArrayNum.
// Phase 2b §1 of Lambda_Typed_Array2.md
// Triggers when static AST detection couldn't tell the structure ahead of time
// (e.g. let-bound rows, function-returned rows). At runtime, array_end inspects
// children and folds uniform numeric arrays into a single N-D tensor.

// ============================================================
// LET-BOUND ROWS — children are ArrayNum identifiers
// ============================================================
'=== let-bound rows ==='
let r1 = [1, 2, 3]
let r2 = [4, 5, 6]
let mat = [r1, r2]
mat
ndim(mat)                          // 2
shape(mat)                         // [2, 3]
sum(mat)                           // 21
mat[0]                             // [1, 2, 3]
mat[1]                             // [4, 5, 6]

// ============================================================
// FUNCTION-RETURNED ROWS — children are generic Arrays at runtime
// ============================================================
'=== function-returned rows ==='
fn mkrow(n) => [n, n*2, n*3]
let m2 = [mkrow(1), mkrow(2), mkrow(3)]
m2
ndim(m2)                           // 2
shape(m2)                          // [3, 3]
sum(m2)                            // 6 + 12 + 18 = 36
m2[0]                              // [1, 2, 3]
m2[2]                              // [3, 6, 9]

// ============================================================
// MIXED INT/FLOAT ROWS — promoted with widening to float
// ============================================================
'=== int + float rows ==='
let ir = [1, 2, 3]
let fr = [4.5, 5.5, 6.5]
let mixed = [ir, fr]
mixed
ndim(mixed)                        // 2
shape(mixed)                       // [2, 3]
sum(mixed)                         // 22.5

// ============================================================
// ARITHMETIC ON DYNAMICALLY-PROMOTED MATRICES
// ============================================================
'=== arithmetic ==='
let a = [r1, r2]
a + 10                             // [[11, 12, 13], [14, 15, 16]]
a * 2                              // [[2, 4, 6], [8, 10, 12]]
a + a                              // [[2, 4, 6], [8, 10, 12]]

// ============================================================
// BROADCASTING WITH DYNAMICALLY-PROMOTED MATRIX
// ============================================================
'=== broadcasting ==='
let col = [[10], [20], [30]]       // (3, 1) via static or dynamic
let row = [r1, r2]                  // (2, 3) via dynamic
ndim(col)                          // 2
shape(col)                         // [3, 1]
shape(row)                         // [2, 3]
col * [100, 200, 300]              // (3,1) * (3,) → (3,3)

// ============================================================
// JAGGED ROWS — NOT promoted
// ============================================================
'=== jagged ==='
let j1 = [1, 2]
let j2 = [3, 4, 5]
let jagged = [j1, j2]
ndim(jagged)                       // 0 — remains generic Array
shape(jagged)                      // error

// ============================================================
// SINGLE-ROW NOT PROMOTED — needs at least 2 rows
// ============================================================
'=== single row ==='
let only = [[1, 2, 3]]              // static detection: 2-D shape [1,3]
ndim(only)                         // 2 (static AST detected)
shape(only)                        // [1, 3]

let runtime_only = [r1]            // dynamic: only 1 row → not promoted
ndim(runtime_only)                 // 0

// ============================================================
// 3-D PROMOTION — rows are themselves 2-D ArrayNums
// ============================================================
'=== 3-D from 2-D rows ==='
let plane1 = [[1, 2], [3, 4]]       // 2-D
let plane2 = [[5, 6], [7, 8]]       // 2-D
let cube = [plane1, plane2]
cube
ndim(cube)                         // 3
shape(cube)                        // [2, 2, 2]
sum(cube)                          // 36
