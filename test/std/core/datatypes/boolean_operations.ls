// Test: Boolean Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Boolean literals
let t = true
t
t
let f = false
f
f
let and1 = t && t
and1
and1
let and2 = t && f
and2
and2
let or1 = t || f
or1
or1
let or2 = f || f
or2
or2
let not_t = !t
not_t
not_t
let not_f = !f
not_f
not_f
let short_circuit = f && (1/0 == 0)
short_circuit
short_circuit
let eq = t == true
eq
eq
let neq = t != false
neq
neq
let truthy = t == 1
truthy
truthy
let falsy = f == 0
falsy
falsy
