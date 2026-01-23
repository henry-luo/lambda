// Test: First-Class Functions
// Tests function assignment, passing, and dynamic invocation

// Typed function definitions (for direct calls)
fn double(x: int) => x * 2
fn triple(x: int) => x * 3

// Untyped function definitions (for higher-order function use)
fn double_dyn(x) => x * 2
fn triple_dyn(x) => x * 3

// Anonymous functions assigned to variables
let f = (x: int) => x + 1
let g = f
let h = (a: int, b: int) => a * b

// Higher-order function (uses untyped parameters for dynamic dispatch)
fn apply(func, x) => func(x)

// Function for apply test
fn square(n) => n * n

// All test results in a single output array
[
    double(5),            // Test 1: Direct function call -> 10
    f(10),                // Test 2: Anonymous function -> 11
    g(20),                // Test 3: Assigned function variable -> 21
    h(3, 4),              // Test 4: Multiple parameters -> 12
    apply(double_dyn, 7), // Test 5a: HOF with double -> 14
    apply(triple_dyn, 5), // Test 5b: HOF with triple -> 15
    apply(square, 6),     // Test 5c: HOF with square -> 36
    (let op = double, let result = op(8), result), // Test 6: Let chain -> 16
    (let a = (x: int) => x + 1,
     let b = (x: int) => x * 2,
     let c = (x: int) => x - 1,
     a(b(c(10))))         // Test 7: Chained functions -> 19
]
