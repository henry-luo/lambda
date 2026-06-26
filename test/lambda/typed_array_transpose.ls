// transpose / flatten / ravel for typed N-D arrays
// Phase 2 — N-D structural ops (Lambda_Typed_Array2.md §1)

// ============================================================
// TRANSPOSE (zero-copy view, reversed axes)
// ============================================================
'=== transpose 2x3 ==='
let m = reshape([1, 2, 3, 4, 5, 6], [2, 3])
m
transpose(m)
shape(transpose(m))
ndim(transpose(m))
is_view(transpose(m))

'=== transpose element access via strides ==='
let t = transpose(m)
t[0, 0]                        // 1
t[1, 0]                        // 2
t[2, 1]                        // 6
sum(t)                         // 21 (same data)

'=== transpose 3x2 ==='
let m2 = reshape([1, 2, 3, 4, 5, 6], [3, 2])
transpose(m2)
shape(transpose(m2))

'=== transpose of 1-D is identity ==='
transpose([1, 2, 3])
ndim(transpose([1, 2, 3]))     // 1

'=== double transpose round-trips ==='
transpose(transpose(m))
shape(transpose(transpose(m)))   // [2, 3]

'=== 3-D transpose reverses all axes ==='
let c = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
shape(transpose(c))            // [2, 2, 2]
transpose(c)

// ============================================================
// FLATTEN (owned 1-D copy in C-order)
// ============================================================
'=== flatten contiguous ==='
flatten(m)                     // [1, 2, 3, 4, 5, 6]
ndim(flatten(m))               // 1
is_view(flatten(m))            // false (owned copy)

'=== flatten of transpose (gathers C-order) ==='
flatten(transpose(m))          // [1, 4, 2, 5, 3, 6]
sum(flatten(transpose(m)))     // 21

'=== flatten 3-D ==='
flatten(c)                     // [1..8]

'=== flatten float matrix ==='
flatten(reshape([1.5, 2.5, 3.5, 4.5], [2, 2]))

// ============================================================
// RAVEL (view if contiguous, else copy)
// ============================================================
'=== ravel contiguous -> view ==='
ravel(m)                       // [1, 2, 3, 4, 5, 6]
is_view(ravel(m))              // true

'=== ravel of transpose -> copy ==='
ravel(transpose(m))            // [1, 4, 2, 5, 3, 6]
is_view(ravel(transpose(m)))   // false (non-contiguous -> copy)

// ============================================================
// COMPOSITION
// ============================================================
'=== arithmetic on transpose ==='
transpose(m) + 10
sum(transpose(m) * 2)          // 42 (21 * 2)

'=== reshape after flatten ==='
reshape(flatten(transpose(m)), [3, 2])

// ============================================================
// ERRORS
// ============================================================
'=== errors ==='
transpose(42)                  // not an array
flatten("hello")               // not an array
