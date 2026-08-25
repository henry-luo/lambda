// Test: Type-set operator annotation mismatch
// Layer: 2 | Category: negative | Covers: E201 with & / ! contracts (LR02-9)
// The contract must be REJECTED and the diagnostic must name it, not print
// the bare word "type".

let a: int & string = 1
