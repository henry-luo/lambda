// Test: Immutable Reassign
// Layer: 2 | Category: negative | Covers: reassign let binding, reassign fn parameter

// Reassign let binding - should produce error E211
let x = 42
x = 99

// Reassign function parameter
fn bad(a: int) {
    a = 10
    a
}
bad(5)
