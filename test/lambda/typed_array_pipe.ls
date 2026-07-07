// Typed results from pipe-map, comprehensions, and filter.
// Phase 2 §4 — iteration sites produce typed ArrayNum (via array_end promotion).
// shape()/reshape() are typed-only ops, so they confirm the result is an ArrayNum.

// ============================================================
// PIPE MAP — arr |> ~ * 2
// ============================================================
'=== pipe map (int) ==='
let a = [1, 2, 3, 4, 5]
let m = a |> ~ * 2
m                              // [2, 4, 6, 8, 10]
shape(m)                       // [5] — confirms typed
sum(m)                         // 30

'=== pipe map (to float) ==='
let mf = a |> ~ * 1.5
mf                             // [1.5, 3, 4.5, 6, 7.5]
shape(mf)                      // [5]

'=== pipe map with index ==='
a |> ~ + ~#                     // element + index: [1, 3, 5, 7, 9]

// ============================================================
// BRACKETED COMPREHENSION — [for (x in arr) body]
// ============================================================
'=== comprehension ==='
let c = [for (x in a) x * 10]
c                              // [10, 20, 30, 40, 50]
shape(c)                       // [5]
sum(c)                         // 150

'=== comprehension to float ==='
[for (x in a) x / 2.0]         // [0.5, 1, 1.5, 2, 2.5]
shape([for (x in a) x / 2.0])  // [5]

// ============================================================
// FILTER — [for (x in arr where cond) x]
// ============================================================
'=== filter keeps subset, typed ==='
let f = [for (x in a where x > 2) x]
f                              // [3, 4, 5]
shape(f)                       // [3]
sum(f)                         // 12

'=== filter float array ==='
let g = [for (x in [1.5, 2.5, 3.5, 4.5] where x > 2.0) x]
g                              // [2.5, 3.5, 4.5]
shape(g)                       // [3]

'=== filter all-out (empty) ==='
let e = [for (x in a where x > 100) x]
e                              // []
sum(e)                         // 0

// ============================================================
// CHAINING — typed results compose with typed ops
// ============================================================
'=== chaining ==='
sum([for (x in a) x * x])              // 1+4+9+16+25 = 55
reshape([for (x in a) x * 10], [5])    // [10, 20, 30, 40, 50]
matmul([for (x in [1,2,3]) x], [for (x in [4,5,6]) x])   // dot: 32

'=== map then reduce ==='
sum(a |> ~ * 2)                          // 30
max([for (x in a) x * x])               // 25

// ============================================================
// NON-NUMERIC stays generic (no promotion)
// ============================================================
'=== non-numeric ==='
[for (x in a) "item"]          // strings — generic array
[for (x in a) [x, x]]          // nested arrays — generic
