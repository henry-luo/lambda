// Test: Closures (Immutable Variable Captures)
// Tests closure creation, captured variable access, and nested closures

// Test 1: Basic closure - single captured variable
fn make_adder(n) {
    fn inner(x) => x + n
    inner
}
let add10 = make_adder(10)
let t1 = add10(5)
// expect: 15

// Test 2: Multiple captured variables
fn make_affine(a, b) {
    fn transform(x) => x * a + b
    transform
}
let affine = make_affine(3, 7)
let t2 = affine(10)
// expect: 37

// Test 3: Chained call (direct invocation of returned closure)
let t3 = make_adder(20)(5)
// expect: 25

// Test 4: Multiple closures with independent environments
let add5 = make_adder(5)
let add100 = make_adder(100)
let t4 = add5(10) + add100(10)
// expect: 125 (15 + 110)

// Test 5: Nested closures (closure capturing outer scope variable)
fn make_nested(a) {
    fn middle(b) {
        fn inner(x) => x + a + b
        inner
    }
    middle
}
let step1 = make_nested(10)
let step2 = step1(20)
let t5 = step2(3)
// expect: 33 (3 + 10 + 20)

// Test 6: Closure capturing multiple variables from different scopes
fn outer(x) {
    fn mid(y) {
        fn inner(z) => x * y + z
        inner
    }
    mid
}
let t6 = outer(2)(3)(4)
// expect: 10 (2 * 3 + 4)

// Test 7: Closure capturing parameter values in calculation
fn make_scaled_adder(scale, offset) {
    fn adder(x) => x + scale * offset
    adder
}
let scaled = make_scaled_adder(3, 4)
let t7 = scaled(10)
// expect: 22 (10 + 3*4)

// Test 8: Multiple calls to same closure (verify env persists)
let counter_base = make_adder(100)
let t8 = counter_base(1) + counter_base(2) + counter_base(3)
// expect: 306 (101 + 102 + 103)

// Test 9: Closure passed to higher-order function
fn apply_twice(f, x) => f(f(x))
let add3 = make_adder(3)
let t9 = apply_twice(add3, 10)
// expect: 16 (10 + 3 + 3)

// Test 10: Closure returning closure (currying pattern)
fn curry_add(a) {
    fn add_b(b) {
        fn add_c(c) => a + b + c
        add_c
    }
    add_b
}
let t10 = curry_add(1)(2)(3)
// expect: 6

// Final result: array of all test values
[t1, t2, t3, t4, t5, t6, t7, t8, t9, t10]
