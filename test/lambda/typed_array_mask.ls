// Boolean mask indexing arr[mask] + keyword element-wise comparisons.
// Phase 6 — masks come from explicit comparisons (a gt 0) or bool literals.
// Symbolic `a > 0` is scalar-only; mask-producing array comparisons use keywords.

// ============================================================
// VECTORIZED ORDERING COMPARISONS → bool mask
// ============================================================
'=== comparison vs scalar ==='
let a = [1, -2, 3, -4, 5]
(a gt 0)                        // [true, false, true, false, true]
(a lt 0)                        // [false, true, false, true, false]
(a ge 3)                        // [false, false, true, false, true]
(a le -2)                       // [false, true, false, true, false]

'=== comparison vs array (broadcast) ==='
let b = [2, 2, 2, 2, 2]
(a gt b)                        // [false, false, true, false, true]
(a le b)                        // [true, true, false, true, false]

'=== 2-D comparison ==='
let m = [[1, 5], [3, 2]]
(m gt 2)                        // [[false, true], [true, false]]

'=== float ==='
let f = [1.5, 2.5, 0.5]
(f ge 1.0)                      // [true, true, false]

// ============================================================
// MASK INDEXING — arr[mask]
// ============================================================
'=== canonical idiom a[a gt 0] ==='
a[a gt 0]                       // [1, 3, 5]
a[a lt 0]                       // [-2, -4]
a[a ge 3]                       // [3, 5]

'=== mask with array comparison ==='
a[a gt b]                       // [3, 5]

'=== literal bool mask ==='
let v = [10, 20, 30, 40, 50]
v[ [true, false, true, false, true] ]   // [10, 30, 50]
v[ [false, false, false, false, false] ] // []  (empty)

'=== full-shape mask on 2-D → flatten-select ==='
m[m gt 2]                       // [5, 3]  (elements where mask true, row-major)
m[ [[true, false], [false, true]] ]      // [1, 2]

'=== 1-D mask on 2-D → row select ==='
m[ [true, false] ]              // [[1, 5]]
m[ [false, true] ]              // [[3, 2]]

// ============================================================
// MASK CONSUMPTION — masks count/select; combination helpers are deferred
// ============================================================
'=== direct mask counts ==='
sum(a gt 0)                      // 3 true lanes
sum(m gt 2)                      // 2 true lanes across the full shape

'=== reductions over masked ==='
sum(a[a gt 0])                  // 9   (1+3+5)
len(a[a gt 0])                  // 3   (count of positives)
sum(a[a lt 0])                  // -6  (-2 + -4)
max(a[a gt 0])                  // 5

'=== reusable mask applied to two arrays ==='
let temps = [10, 35, 20, 40, 15]
let hot = [false, true, false, true, false]
temps[hot]                      // [35, 40]

'=== float mask result preserves float ==='
f[f ge 1.0]                     // [1.5, 2.5]

// ============================================================
// == / != stay structural (NOT vectorized) — cross-type semantics
// ============================================================
'=== structural equality preserved ==='
let p = [1, 2, 3]
let q = [1, 2, 3]
p == q                          // true  (structural, array-vs-array)
p != [1, 2, 4]                  // true
