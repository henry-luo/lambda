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
// expect: (15, 25) — a list, so it spreads in the final array (S2.5.1v2)

// Test 4: outer binding used only in a raise branch
fn raise_branch(n: int) {
    fn inner(x) => if (x < 0) raise error("negative") else x + n
    inner(5)
}
let t4 = raise_branch(10);
// expect: 15

// Test 5: a closure called as a value resolves an omitted default in its
// public entry, which must see the captured binding
fn make_doubler(n: int) {
    fn inner(x: int = n) => x * 2
    inner
}
let doubler = make_doubler(21);
let t5 = [doubler(), doubler(4)];
// expect: [42, 8]

// Test 6: a default reading both an earlier parameter and a capture
fn make_combiner(n: int) {
    fn inner(a: int, b: int = a + n) => a * 100 + b
    inner
}
let combiner = make_combiner(5);
let t6 = [combiner(3), combiner(3, 1)];
// expect: [308, 301]

// Final result: array of all test values
[t1, t2, t3, t4, t5, t6]
