// Test: Recursive Performance
// Layer: 1 | Category: performance | Covers: recursion depth, memoization

fn fib(n: int) int {
    if (n == 0) 0
    else if (n == 1) 1
    else fib(n - 1) + fib(n - 2)
}
fib(20)

fn ackermann(m: int, n: int) int {
    if (m == 0) n + 1
    else if (n == 0) ackermann(m - 1, 1)
    else ackermann(m - 1, ackermann(m, n - 1))
}
ackermann(3, 4)

fn sum_recursive(n: int) int {
    if (n == 0) 0 else n + sum_recursive(n - 1)
}
sum_recursive(100)
