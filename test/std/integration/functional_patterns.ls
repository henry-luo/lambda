// Test: Functional Patterns
// Layer: 1 | Category: integration | Covers: closures, higher-order, pipeline

// Closure captures outer variable
fn make_counter(start: int) => (step: int) => start + step
let counter = make_counter(10)
counter(1)
counter(5)

// Currying
fn multiply(a: int) => (b: int) => a * b
let double_fn = multiply(2)
let triple_fn = multiply(3)
double_fn(5)
triple_fn(5)

// Pipeline chain
let evens = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] that (~ % 2 == 0)
let squares = evens | ~ * ~
sum(squares)

// Nested for with let
[for (x in 1 to 5, let sq = x * x where sq > 10) {val: x, square: sq}]

// Function composition via direct calls
fn inc2(x: int) int => x + 1
fn dbl2(x: int) int => x * 2
dbl2(inc2(3))
inc2(dbl2(3))
