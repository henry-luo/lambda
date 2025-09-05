// Test: Boolean Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Boolean literals
let t = true
t
let f = false
f
let and1 = t && t
and1
let and2 = t && f
and2
let or1 = t || f
or1
let or2 = f || f
or2
let not_t = !t
not_t
let not_f = !f
not_f
let short_circuit = f && (1/0 == 0)
short_circuit
let eq = t == true
eq
let neq = t != false
neq
let truthy = t == 1
truthy
let falsy = f == 0
falsy
