// Test Tail Call Optimization (TCO)
// These tail-recursive functions should run without stack overflow

// Classic factorial with accumulator (tail-recursive)
fn factorial(n: int, acc: int): int => 
    if (n <= 1) acc
    else factorial(n - 1, acc * n)

// Sum from 1 to n with accumulator (tail-recursive)
fn sum_to(n: int, acc: int): int =>
    if (n <= 0) acc
    else sum_to(n - 1, acc + n)

// Count down to zero (simple tail recursion)
fn countdown(n: int): int =>
    if (n <= 0) 0
    else countdown(n - 1)

// Fibonacci with accumulator (tail-recursive)
fn fib_tail(n: int, a: int, b: int): int =>
    if (n <= 0) a
    else if (n == 1) b
    else fib_tail(n - 1, b, a + b)

fn fib(n: int): int => fib_tail(n, 0, 1)

// Test with various values
let f10 = factorial(10, 1)
let s1000 = sum_to(1000, 0)
let fib_30 = fib(30)

// With TCO, deep recursion completes without stack overflow
// (Without TCO, these would crash)
let deep_count = countdown(100000)
let deep_sum = sum_to(100000, 0)

// Return results for verification
{
    factorial_10: f10,      // Expected: 3628800
    sum_1000: s1000,        // Expected: 500500
    fib_30: fib_30,         // Expected: 832040
    deep_count: deep_count, // Expected: 0 (no crash!)
    deep_sum: deep_sum      // Expected: 5000050000 (overflow in int, but no crash!)
}
