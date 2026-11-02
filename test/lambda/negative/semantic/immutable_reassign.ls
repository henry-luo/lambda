// Test: Immutable Reassign
// Layer: 2 | Category: negative | Covers: reassign let binding, reassign fn parameter
//
// the module-level assignment remains E224; the fn parameter is separately immutable.
// Asserted by NegativeScriptTest.ReassignAtModuleScopeReportsE224.

// Reassign let binding - assignment is procedural, so this is E224 at module scope
let x = 42
x = 99

// Reassign function parameter
fn bad(a: int) {
    a = 10
    a
}
bad(5)
