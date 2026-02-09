// Robustness tests covering issues from Robust_*.md documents
//
// Covers:
// - Robust_Lambda4: NULL field write in elmt_put (8-byte overflow fix)
// - Robust_Lambda2: Mixed-type conditional expression coercion
// - Robust_Numeric: Modulo with equality comparisons

// === Element null field handling ===
// Validates null field write doesn't corrupt adjacent fields in elements
'Element Null Fields:'
let e1 = <item before: 42, null_field: null, after: 123>
e1.before
e1.after

let e2 = <item a: 1, b: null, c: null, d: 99>
e2.a
e2.d

let e3 = <item x: null, name: "hello">
e3.name

// Verify element structure with null fields is intact
<test n: null, v: 55>

// === Mixed-type conditional expressions ===
'Mixed-Type Conditionals:'
(if (false) null else "hello")
(if (true) 42 else null)
// Null comparison checks
(if (false) 42 else null) == null
(if (true) null else "hello") == null
(if (true) null else 42) == null
(if (false) "x" else null) == null

// === Modulo with equality comparisons ===
'Modulo Equality:'
5 % 3 == 2
10 % 4 == 2
7 % 3 == 1
10 % 5 == 0
5 % 3 != 1
(5 % 3) + 1 == 3
