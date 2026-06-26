// Read-only views over ArrayNum
// Phase 2b §2 of Lambda_Typed_Array2.md
// Covers: subview() creation, indexing, iteration, arithmetic, reductions,
//         vectorized math, view-of-view, mutation rejection, GC-safe lifetime.

// ============================================================
// CREATION & is_view
// ============================================================
'=== creation & is_view ==='
let a = [10, 20, 30, 40, 50]
is_view(a)            // false: base
let v = subview(a, 1, 4)
is_view(v)            // true: view
v
len(v)
a                     // base unchanged

// ============================================================
// INDEXING
// ============================================================
'=== indexing ==='
v[0]
v[1]
v[2]

// ============================================================
// EDGE CASES — negative & out-of-range indices
// ============================================================
'=== negative indices ==='
let b = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
subview(b, -3, -1)    // last 3 ... up to second-last → [8, 9]
subview(b, 0, -1)     // all but last → [1..9]
subview(b, -5, 10)    // last 5 → [6..10]

'=== empty view ==='
let e = subview(b, 4, 4)
e
len(e)
sum(e)                // 0

'=== full-array view ==='
let fa = subview(b, 0, 10)
sum(fa)               // 55
len(fa)

// ============================================================
// FLOAT VIEWS
// ============================================================
'=== float views ==='
let f = [1.5, 2.5, 3.5, 4.5, 5.5]
let fv = subview(f, 1, 4)
fv
sum(fv)
avg(fv)

// ============================================================
// VIEW OF VIEW
// ============================================================
'=== view of view ==='
let outer = [100, 200, 300, 400, 500, 600]
let inner1 = subview(outer, 1, 5)         // [200, 300, 400, 500]
inner1
let inner2 = subview(inner1, 1, 3)        // [300, 400]
inner2
is_view(inner1)
is_view(inner2)

// ============================================================
// ARITHMETIC ON VIEWS
// ============================================================
'=== arithmetic ==='
let arr = [1, 2, 3, 4, 5, 6]
let vw = subview(arr, 1, 5)               // [2, 3, 4, 5]
vw + 1                                     // [3, 4, 5, 6]
vw * 10                                    // [20, 30, 40, 50]
vw + subview(arr, 0, 4)                   // [3, 5, 7, 9]

// ============================================================
// VECTORIZED MATH ON VIEWS
// ============================================================
'=== vectorized math ==='
let sqs = [1.0, 4.0, 9.0, 16.0, 25.0]
math.sqrt(subview(sqs, 1, 4))             // [2, 3, 4]
abs(subview([-1, 2, -3, 4, -5], 1, 4))   // [2, 3, 4]

// ============================================================
// REDUCTIONS
// ============================================================
'=== reductions ==='
let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let mid = subview(nums, 3, 8)             // [4, 5, 6, 7, 8]
sum(mid)
avg(mid)
min(mid)
max(mid)

// ============================================================
// FOR-IN ITERATION
// ============================================================
'=== for-in over view ==='
for (x in subview(nums, 2, 5)) (x * 100)

// ============================================================
// COMPOSITION
// ============================================================
'=== composition ==='
sum(math.sqrt(subview([1.0, 4.0, 9.0, 16.0], 0, 4)))   // 1+2+3+4 = 10
math.cos(subview([0.0, 0.0, 0.0], 0, 3))               // [1, 1, 1]

// ============================================================
// ERROR CASES
// ============================================================
'=== errors ==='
subview(42, 0, 1)              // not an array — error
subview([1,2,3], "a", 2)       // non-int bounds — error
