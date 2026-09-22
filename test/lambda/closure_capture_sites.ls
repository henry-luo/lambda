// Test: closure captures reached only through less common node kinds.
// The capture walker used to skip these kinds, so the outer binding was never
// captured and the closure read a missing value.

fn sub(a: int, b: int) => a - b

// Test 1: outer binding used only as a named argument
fn named_arg(n: int) {
    fn inner(x) => sub(b: n, a: x)
    inner(100)
}
let t1 = named_arg(10)
// expect: 90

// Test 2: outer binding used only in a parameter default
fn param_default(n: int) {
    fn inner(x: int = n) => x
    inner()
}
let t2 = param_default(7)
// expect: 7

// Test 3: outer binding used only in a for-where clause
fn where_clause(n: int) {
    fn inner(xs) => for (x in xs where x > n) x
    inner([5, 15, 25])
}
let t3 = where_clause(10)
// expect: [15, 25]

// Test 4: outer binding used only in a raise branch
fn raise_branch(n: int) {
    fn inner(x) => if (x < 0) raise error("negative") else x + n
    inner(5)
}
let t4 = raise_branch(10);
// expect: 15

// Final result: array of all test values
[t1, t2, t3, t4]
