// Test: Immutable Reassign
// Layer: 2 | Category: negative | Covers: reassign let binding, reassign fn parameter
//
// Compile-failure case, so it carries no `.expected` — the std harness asserts a zero exit
// status, and only registers `.ls` files that have a golden beside them (same convention as
// mutation_in_fn.ls). Both assignments below are outside any `pn`, so E224 fires before the
// immutability rule is ever reached; E211 itself is covered from inside a `pn` elsewhere.
// Asserted by NegativeScriptTest.ProceduralStatementsOutsidePnReportE224WithoutCascade.

// Reassign let binding - assignment is procedural, so this is E224 at module scope
let x = 42
x = 99

// Reassign function parameter
fn bad(a: int) {
    a = 10
    a
}
bad(5)
