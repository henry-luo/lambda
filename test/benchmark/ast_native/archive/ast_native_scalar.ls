// native scalar-body experiment: recursion, branches, helpers and return admission.
fn fib(n) {
    if (n < 2) { return n }
    return fib(n - 1) + fib(n - 2)
}
fn fib_float(n: float) float {
    if (n < 2.0) { return n }
    return fib_float(n - 1.0) + fib_float(n - 2.0)
}
fn add_three(a, b, c) { return (a + b) + c }
fn choose(a, b) { return if (a > b) a else b }
[fib(10) == 55, fib_float(10.0) == 55.0,
 add_three(3, 4, 5) == 12, choose(3, 8) == 8, choose(9, 2) == 9,
 add_three(1.5, 2.5, 3.0) == 7.0]
