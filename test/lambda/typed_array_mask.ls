// Boolean mask indexing arr[mask] + vectorized ordering comparisons.
// Phase 2 — masks come from element-wise comparisons (a > 0) or bool literals.
// Bare `a > 0` at statement level hits the < > markup ambiguity, so standalone
// comparisons are parenthesised; inside arr[...] they need no parens.

// ============================================================
// VECTORIZED ORDERING COMPARISONS → bool mask
// ============================================================
'=== comparison vs scalar ==='
let a = [1, -2, 3, -4, 5]
(a > 0)                         // [true, false, true, false, true]
(a < 0)                         // [false, true, false, true, false]
(a >= 3)                        // [false, false, true, false, true]
(a <= -2)                       // [false, true, false, true, false]

'=== comparison vs array (broadcast) ==='
let b = [2, 2, 2, 2, 2]
(a > b)                         // [false, false, true, false, true]
(a <= b)                        // [true, true, false, true, false]

'=== 2-D comparison ==='
let m = [[1, 5], [3, 2]]
(m > 2)                         // [[false, true], [true, false]]

'=== float ==='
let f = [1.5, 2.5, 0.5]
(f >= 1.0)                      // [true, true, false]

// ============================================================
// MASK INDEXING — arr[mask]
// ============================================================
'=== canonical idiom a[a > 0] ==='
a[a > 0]                        // [1, 3, 5]
a[a < 0]                        // [-2, -4]
a[a >= 3]                       // [3, 5]

'=== mask with array comparison ==='
a[a > b]                        // [3, 5]

'=== literal bool mask ==='
let v = [10, 20, 30, 40, 50]
v[ [true, false, true, false, true] ]   // [10, 30, 50]
v[ [false, false, false, false, false] ] // []  (empty)

'=== full-shape mask on 2-D → flatten-select ==='
m[m > 2]                        // [5, 3]  (elements where mask true, row-major)
m[ [[true, false], [false, true]] ]      // [1, 2]

'=== 1-D mask on 2-D → row select ==='
m[ [true, false] ]              // [[1, 5]]
m[ [false, true] ]              // [[3, 2]]

// ============================================================
// COMPOSITION — masks reduce/compose
// ============================================================
'=== reductions over masked ==='
sum(a[a > 0])                   // 9   (1+3+5)
len(a[a > 0])                   // 3   (count of positives)
sum(a[a < 0])                   // -6  (-2 + -4)
max(a[a > 0])                   // 5

'=== reusable mask applied to two arrays ==='
let temps = [10, 35, 20, 40, 15]
let hot = [false, true, false, true, false]
temps[hot]                      // [35, 40]

'=== float mask result preserves float ==='
f[f >= 1.0]                     // [1.5, 2.5]

// ============================================================
// == / != stay structural (NOT vectorized) — cross-type semantics
// ============================================================
'=== structural equality preserved ==='
let p = [1, 2, 3]
let q = [1, 2, 3]
p == q                          // true  (structural, array-vs-array)
p != [1, 2, 4]                  // true
