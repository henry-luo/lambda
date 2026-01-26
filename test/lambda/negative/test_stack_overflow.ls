// Test: Stack overflow protection
// This tests that infinite recursion is caught gracefully

// Direct infinite recursion
fn infinite_recurse(n) => infinite_recurse(n + 1)

// Test direct recursion - should catch stack overflow
infinite_recurse(0)
