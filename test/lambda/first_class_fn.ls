// Test: First-Class Functions
// Tests function assignment, passing, and dynamic invocation

// Basic function definition
fn double(x: int) => x * 2
fn triple(x: int) => x * 3

// Test 1: Direct function call (baseline)
double(5)
// expect: 10

// Test 2: Assign anonymous function to variable
let f = (x: int) => x + 1
f(10)
// expect: 11

// Test 3: Assign function variable to another variable
let g = f
g(20)
// expect: 21

// Test 4: Function with multiple parameters
let h = (a: int, b: int) => a * b
h(3, 4)
// expect: 12

// Test 5: Higher-order function - function as parameter
fn apply(func, x: int) => func(x)
apply(double, 7)
// expect: 14

apply(triple, 5)
// expect: 15

// Test 6: Apply with anonymous function
apply((n: int) => n * n, 6)
// expect: 36

// Test 7: Pass function through let chain
(let op = double, let result = op(8), result)
// expect: 16

// Test 8: Multiple function variables chained
(let a = (x: int) => x + 1,
 let b = (x: int) => x * 2,
 let c = (x: int) => x - 1,
 a(b(c(10))))
// expect: 19

// Summary result
[
    double(5),      // 10
    f(10),          // 11
    g(20),          // 21
    h(3, 4),        // 12
    apply(double, 7) // 14
]
