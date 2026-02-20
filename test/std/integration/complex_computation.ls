// Test: Complex Computation
// Layer: 1 | Category: integration | Covers: multi-step math, functions, collections

// Fibonacci sequence via for
[for (i in 0 to 9, let fib_val = if (i == 0) 0 else if (i == 1) 1 else 0) i]

// Factorial using recursion
fn fact(n: int) int {
    if (n == 0) 1 else n * fact(n - 1)
}
[fact(0), fact(1), fact(5), fact(10)]

// Sum of squares
sum([for (x in 1 to 10) x * x])

// Pythagorean check
fn is_pythagorean(a: int, b: int, c: int) bool => a * a + b * b == c * c
is_pythagorean(3, 4, 5)
is_pythagorean(5, 12, 13)
is_pythagorean(1, 2, 3)

// GCD via recursion
fn gcd(a: int, b: int) int {
    if (b == 0) a else gcd(b, a % b)
}
gcd(48, 18)
gcd(100, 75)
