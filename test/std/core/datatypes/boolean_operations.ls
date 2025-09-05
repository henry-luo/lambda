// Test: Boolean Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Boolean literals
let t = true
let f = false

// Logical operations
let and1 = t && t    // true
let and2 = t && f     // false
let or1 = t || f      // true
let or2 = f || f      // false
let not_t = !t        // false
let not_f = !f        // true

// Short-circuit evaluation
let short_circuit = f && (1/0 == 0)  // false (doesn't evaluate division)

// Comparison operations
let eq = t == true    // true
let neq = t != false  // true

// Type conversion in comparisons
let truthy = t == 1    // true (if 1 is truthy)
let falsy = f == 0     // true (if 0 is falsy)

// Final check
1
