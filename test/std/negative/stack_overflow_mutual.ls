// Test: Stack Overflow Mutual Recursion
// Layer: 2 | Category: negative | Covers: mutually recursive fns without base case, no crash

// Mutual recursion without base case - should produce error, not crash
fn ping(n: int) => pong(n)
fn pong(n: int) => ping(n)

ping(1)
