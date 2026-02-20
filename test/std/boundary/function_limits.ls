// Test: Function Limits
// Layer: 2 | Category: boundary | Covers: recursion, closures, higher-order

// Deep recursion
fn countdown(n: int) int {
    if (n == 0) 0 else countdown(n - 1)
}
countdown(100)

// Nested closures
fn outer(x: int) => (y: int) => (z: int) => x + y + z
outer(1)(2)(3)

// Function as data in array
let fns = [(x: int) => x + 1, (x: int) => x * 2, (x: int) => x * x]
fns[0](10)
fns[1](10)
fns[2](10)

// Chained function application
fn inc(x: int) int => x + 1
fn dbl(x: int) int => x * 2
dbl(inc(inc(inc(0))))
