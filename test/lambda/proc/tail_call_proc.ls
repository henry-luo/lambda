// Test Tail Call Optimization (TCO) for Procedures (pn)
// Verifies that tail recursion is correctly optimized in procedural functions

// ==============================================================================
// PART 1: Pure Tail-Recursive Procedures (typed)
// ==============================================================================

// Factorial with accumulator (tail-recursive)
pn factorial(n: int, acc: int) int {
    if (n <= 1) { return acc }
    return factorial(n - 1, acc * n)
}

// Sum with accumulator (tail-recursive)
pn sum_to(n: int, acc: int) int {
    if (n <= 0) { return acc }
    return sum_to(n - 1, acc + n)
}

// GCD — tail recursion in both branches
pn gcd(a: int, b: int) int {
    if (b == 0) { return a }
    return gcd(b, a % b)
}

// ==============================================================================
// PART 2: Pure Tail-Recursive Procedures (untyped)
// ==============================================================================

// Untyped countdown (tail-recursive)
pn countdown(n) {
    if (n <= 0) { return 0 }
    return countdown(n - 1)
}

// ==============================================================================
// PART 3: Mixed Recursion (tail + non-tail calls)
// ==============================================================================

// Takeuchi function — final call is tail, inner 3 are non-tail
pn tak(x: int, y: int, z: int) int {
    if (y >= x) { return z }
    let a = tak(x - 1, y, z)
    let b = tak(y - 1, z, x)
    let c = tak(z - 1, x, y)
    return tak(a, b, c)
}

// ==============================================================================
// PART 4: Non-Tail-Recursive (should NOT get TCO)
// ==============================================================================

// Addition after recursive call — NOT tail recursive
pn fib(n: int) int {
    if (n <= 1) { return n }
    return fib(n - 1) + fib(n - 2)
}

// ==============================================================================
// Test Cases
// ==============================================================================

pn main() {
    // Basic correctness
    print(factorial(10, 1))
    print(sum_to(1000, 0))
    print(gcd(48, 18))
    print(countdown(100))
    print(tak(18, 12, 6))
    print(fib(10))

    // Deep recursion — only works with TCO
    print(sum_to(100000, 0))
    print(gcd(1000000, 999999))
}
