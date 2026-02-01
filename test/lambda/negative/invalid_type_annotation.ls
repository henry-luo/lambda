// Negative test: invalid type annotations
// These should produce errors, not crashes

// Test 1: Typo in type name for parameter
fn bad_param1(x: iunt) { x }

// Test 2: Another typo variant
fn bad_param2(x: it) { x }

// Test 3: Invalid return type
fn bad_return() strng { "hello" }

// Test 4: Invalid type in let binding
let x: intt = 5

// Test 5: Multiple invalid types
fn multi_bad(a: inta, b: flot) booln { true }
