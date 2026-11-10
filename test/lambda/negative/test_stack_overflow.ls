// Test: Stack overflow protection
// Non-tail recursion must retain stack checks when self tails become loops (D8.2.6).

// addition after the call prevents tail-frame reuse
fn infinite_recurse(n) => infinite_recurse(n + 1) + 1

// Test direct recursion - should catch stack overflow
infinite_recurse(0)
