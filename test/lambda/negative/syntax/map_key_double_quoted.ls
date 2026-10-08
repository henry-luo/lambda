// Test: a double-quoted map key
// Layer: 2 | Category: negative | Covers: map key spelling, S16.4.1v2 braces
// A map key is a symbol — bare when it is a name, single-quoted otherwise. A
// double-quoted string is not a key form. Because the brace resolver decides
// by interior, `{"k": 1}` used to be read as a BLOCK and fail at the `:` with
// a bare "expected an expression", naming neither the rule nor the repair.

let m = {"data-node-id": "a"}
m
