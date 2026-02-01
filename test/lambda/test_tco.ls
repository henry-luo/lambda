// Test Tail Call Optimization (TCO)
// These tail-recursive functions should run without stack overflow

// ==============================================================================
// PART 1: Pure Tail-Recursive Functions (SAFE - no stack check needed)
// ==============================================================================

// Classic factorial with accumulator (tail-recursive)
fn factorial(n: int, acc: int) int => 
    if (n <= 1) acc
    else factorial(n - 1, acc * n)

// Sum from 1 to n with accumulator (tail-recursive)
fn sum_to(n: int, acc: int) int =>
    if (n <= 0) acc
    else sum_to(n - 1, acc + n)

// Count down to zero (simple tail recursion)
fn countdown(n: int) int =>
    if (n <= 0) 0
    else countdown(n - 1)

// Fibonacci with accumulator (tail-recursive)
fn fib_tail(n: int, a: int, b: int) int =>
    if (n <= 0) a
    else if (n == 1) b
    else fib_tail(n - 1, b, a + b)

fn fib(n: int) int => fib_tail(n, 0, 1)

// Tail recursion in both branches of if-expression
fn gcd(a: int, b: int) int =>
    if (b == 0) a
    else gcd(b, a % b)

// ==============================================================================
// PART 2: Non-Tail-Recursive Functions (UNSAFE - need stack check)
// These have operations AFTER the recursive call
// ==============================================================================

// Addition after recursive call - NOT tail recursive
fn sum_non_tail(n: int) int =>
    if (n <= 0) 0
    else n + sum_non_tail(n - 1)

// Multiplication after recursive call - NOT tail recursive  
fn factorial_non_tail(n: int) int =>
    if (n <= 1) 1
    else n * factorial_non_tail(n - 1)

// ==============================================================================
// PART 3: Mixed Recursion (has both tail and non-tail calls)
// These still need stack checks because of the non-tail calls
// ==============================================================================

// Recursive call in condition (not tail position) plus tail call in branch
fn count_evens(n: int) int =>
    if (n <= 0) 0
    else if (n % 2 == 0) 1 + count_evens(n - 1)   // NON-tail: addition after
    else count_evens(n - 1)                        // tail call

// ==============================================================================
// Test Cases
// ==============================================================================

// Test basic correctness
let f10 = factorial(10, 1)
let s1000 = sum_to(1000, 0)
let fib_30 = fib(30)
let gcd_result = gcd(48, 18)

// Test non-tail versions with small values (they work, just can't go deep)
let sum_nt_100 = sum_non_tail(100)
let fact_nt_10 = factorial_non_tail(10)
let count_evens_100 = count_evens(100)

// With TCO, deep recursion completes without stack overflow
// Pure tail-recursive functions can go arbitrarily deep
let deep_count = countdown(100000)
let deep_sum = sum_to(100000, 0)
let deep_gcd = gcd(1000000, 999999)

// Return results for verification
{
    // Basic correctness tests
    factorial_10: f10,          // Expected: 3628800
    sum_1000: s1000,            // Expected: 500500
    fib_30: fib_30,             // Expected: 832040
    gcd_48_18: gcd_result,      // Expected: 6
    
    // Non-tail versions (small depth - should work)
    sum_non_tail_100: sum_nt_100,       // Expected: 5050
    factorial_non_tail_10: fact_nt_10,  // Expected: 3628800
    count_evens_100: count_evens_100,   // Expected: 50 (counts even numbers 2,4,...,100)
    
    // Deep recursion (only works with TCO)
    deep_count: deep_count,     // Expected: 0 (no crash!)
    deep_sum: deep_sum,         // Expected: 705082704 (overflow but no crash!)
    deep_gcd: deep_gcd          // Expected: 1
}
