// N-D for-in iteration yields leading-axis slices (NumPy semantics).
// Phase 2b §1 of Lambda_Typed_Array2.md
// Rules:
//   - 1-D for-in yields scalars (unchanged)
//   - N-D for-in yields (ndim-1)-D views ("rows" for 2-D, "slabs" for 3-D)
//   - mat[i] also yields a row view (same semantics as iteration)
//   - len(mat) returns shape[0] (NumPy-compatible)

// ============================================================
// 1-D still yields scalars
// ============================================================
'=== 1-D scalars ==='
for (x in [10, 20, 30]) x
len([10, 20, 30])                           // 3

// ============================================================
// 2-D yields rows
// ============================================================
'=== 2-D rows ==='
let m = reshape([1, 2, 3, 4, 5, 6], [2, 3])
len(m)                                      // 2 (shape[0])
for (row in m) row
for (row in m) sum(row)
for (row in m) len(row)                     // each row has len 3

// ============================================================
// 2-D indexing yields rows too
// ============================================================
'=== 2-D indexing ==='
m[0]
m[1]
sum(m[0])
sum(m[1])
shape(m[0])

// ============================================================
// 3-D yields 2-D slabs
// ============================================================
'=== 3-D slabs ==='
let t = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
len(t)                                      // 2
for (slab in t) slab
for (slab in t) shape(slab)
for (slab in t) sum(slab)

// ============================================================
// Nested iteration: rows-of-rows
// ============================================================
'=== nested iteration ==='
for (row in m) for (x in row) (x * 100)
sum(for (row in m) sum(row))                // 6 + 15 = 21

// ============================================================
// Float matrices
// ============================================================
'=== float matrix iteration ==='
let fm = reshape([1.5, 2.5, 3.5, 4.5, 5.5, 6.5], [3, 2])
len(fm)                                     // 3
for (row in fm) row
for (row in fm) avg(row)

// ============================================================
// Row view properties
// ============================================================
'=== row view properties ==='
let big = reshape([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12], [3, 4])
let r0 = big[0]
let r1 = big[1]
let r2 = big[2]
is_view(r0)                                 // true
shape(r0)                                   // [4]
ndim(r0)                                    // 1
sum(r0) + sum(r1) + sum(r2)                 // 10 + 26 + 42 = 78
