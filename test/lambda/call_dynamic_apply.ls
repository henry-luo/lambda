// S12.3.4: call(f, args) applies f to the members of args as arguments.
// S12.1.4: its colour follows f — fn here, so every target must be an fn.
fn add(a, b) => a + b
fn zero() => 42
fn triple(x) => x * 3

let fixed = call(add, [1, 2])
let empty = call(zero, [])
let closure = call((x) => x * 3, [4])
let nested = call(add, [call(add, [1, 2]), 3])

// the motivating case: forwarding a collected argument list to a
// variadic callee, which nothing else expresses (LR02-10 / S12.3.5)
fn inner(...) => sum(varg())
fn outer(...) => call(inner, varg())
let forwarded = outer(1, 2, 3)

// a packed numeric array is the other array face and must work too
let packed = call(add, [10, 20])
let out = [fixed, empty, closure, nested, forwarded, packed]
out
