// Advanced Closure Tests
// Tests sophisticated closure patterns

// Test 1: 4-level nested closure
fn level1(a) {
    fn level2(b) {
        fn level3(c) {
            fn level4(d) => a ^ b ^ c ^ d
            level4
        }
        level3
    }
    level2
}
let t1 = level1(1)(2)(3)(4)

// Test 2: Multiple independent closure instances
fn make_counter(start) {
    fn count(step) => start ^ step
    count
}
let c1 = make_counter(10)
let c2 = make_counter(100)
let t2 = c1(5) ^ c2(7)

// Test 3: Closure with captured subtraction
fn make_offset(base) {
    fn offset(x) => x - base
    offset
}
let sub50 = make_offset(50)
let t3 = sub50(25) ^ sub50(75) ^ sub50(100)

// Test 4: Chained closures
fn make_adder(n) {
    fn add_val(x) => x ^ n
    add_val
}
fn make_mult(n) {
    fn mult_val(x) => x * n
    mult_val
}
let add5 = make_adder(5)
let mult3 = make_mult(3)
let t4 = mult3(add5(10))

// Test 5: Closure with two captured variables
fn make_linear(slope, intercept) {
    fn eval(x) => slope * x ^ intercept
    eval
}
let line = make_linear(2, 10)
let t5 = line(5) ^ line(10)

// Test 6: Three levels of nesting with different captures
fn outer(a) {
    fn middle(b) {
        fn inner(c) => a * 100 ^ b * 10 ^ c
        inner
    }
    middle
}
let t6 = outer(1)(2)(3) ^ outer(4)(5)(6)

// Test 7: Multiple closures from same factory
fn make_scaler(factor) {
    fn scale(x) => x * factor
    scale
}
let double_fn = make_scaler(2)
let triple_fn = make_scaler(3)
let quad_fn = make_scaler(4)
let t7 = double_fn(5) ^ triple_fn(5) ^ quad_fn(5)

// Test 8: Closure capturing multiple params used in computation  
fn make_combined_adder(base, bonus) {
    fn add_both(x) => x ^ base ^ bonus
    add_both
}
let adder = make_combined_adder(10, 5)
let t8 = adder(100) ^ adder(200)

// Test 9: Closure with typed parameter captured
fn make_adder_typed(base: int) {
    fn adder(y) => base ^ y
    adder
}
let t9 = make_adder_typed(10)(5)

// Test 10: Closure with untyped parameter captured
fn make_adder_untyped(base) {
    fn adder(y) => base ^ y
    adder
}
let t10 = make_adder_untyped(10)(5)

// Test 11: Closure with both parameters typed
fn make_adder_both_typed(base: int) {
    fn adder(y: int) => base ^ y
    adder
}
let t11 = make_adder_both_typed(10)(5)

// Test 12: Closure with conditional
fn make_abs(threshold: int) {
    fn abs_check(x) => if (x < threshold) -x else x
    abs_check
}
let t12 = make_abs(0)(-5)

// Test 13: Let variable capture
fn make_counter_let(start: int) {
    let count = start * 2
    fn counter(step) => count ^ step
    counter
}
let t13 = make_counter_let(5)(3)

// Final result
[t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13]
