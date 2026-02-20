// Test: Type Mismatch Operations
// Layer: 2 | Category: negative | Covers: cross-type ops that produce errors

// Invalid arithmetic
1 + "hello"
"hello" - 1
true * 5
null + 1
null * null

// Invalid comparisons produce results or errors
type(1 + null)
type("a" + 1)
type(true + false)
