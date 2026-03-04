// Test closures with typed parameters
// Verifies correct boxing/unboxing for captured variables in closure env
// Covers Phase 3 (dual version generation) and Phase 4 (try_box_scalar for captured vars)

// ============================================
// Section 1: Typed outer param captured by untyped inner
// ============================================

fn make_adder_typed(n: int) {
    fn inner(x) => x + n
    inner
}

"1. Typed outer, untyped inner"
[make_adder_typed(10)(5), make_adder_typed(-3)(8)]

// ============================================
// Section 2: Typed outer AND typed inner
// ============================================

fn make_adder_both(n: int) {
    fn inner(x: int) => x + n
    inner
}

"2. Both typed"
[make_adder_both(10)(5), make_adder_both(-3)(8)]

// ============================================
// Section 3: Int multiplier closure
// ============================================

fn make_scaler(factor: int) {
    fn scale(x: int) => x * factor
    scale
}

"3. Int multiplier closure"
[make_scaler(3)(10), make_scaler(5)(7)]

// ============================================
// Section 4: Bool typed capture
// ============================================

fn make_guard(flag: bool) {
    fn guarded(x) => if (flag) x else null
    guarded
}

"4. Bool typed closure"
[make_guard(true)(42), make_guard(false)(42)]

// ============================================
// Section 5: String typed capture
// ============================================

fn make_greeter(prefix: string) {
    fn greet(name) => prefix ++ " " ++ name
    greet
}

"5. String typed closure"
[make_greeter("Hello")("World"), make_greeter("Hi")("Lambda")]

// ============================================
// Section 6: Multiple typed captures
// ============================================

fn make_linear(slope: int, intercept: int) {
    fn eval(x: int) => slope * x + intercept
    eval
}

"6. Multiple typed captures"
[make_linear(2, 3)(5), make_linear(-1, 10)(4)]

// ============================================
// Section 7: Mixed typed/untyped captures
// ============================================

fn make_mixed(a: int, b) {
    fn apply(x: int) => a * x + b
    apply
}

"7. Mixed typed/untyped captures"
[make_mixed(3, 7)(10), make_mixed(2, 100)(5)]

// ============================================
// Section 8: Nested typed closures
// ============================================

fn outer_typed(a: int) {
    fn inner(b: int) => a * 10 + b
    inner
}

"8. Nested typed closures"
[outer_typed(1)(3), outer_typed(4)(6)]

// ============================================
// Section 9: Typed closure as higher-order function argument
// ============================================

fn apply_fn(f, x) => f(x)
let add5 = make_adder_typed(5)
let times3 = make_scaler(3)

"9. Higher-order with typed closure"
[apply_fn(add5, 10), apply_fn(times3, 7)]

// ============================================
// Section 10: Typed closure returning container
// ============================================

fn make_pair_maker(first: int) {
    fn make_pair(second: int) => [first, second]
    make_pair
}

"10. Typed closure returning container"
[make_pair_maker(1)(2), make_pair_maker(10)(20)]

// ============================================
// Section 11: Let variable captured in typed closure
// ============================================

fn make_offset(base: int) {
    let doubled = base * 2
    fn offset(x: int) => x + doubled
    offset
}

"11. Let variable capture"
[make_offset(5)(10), make_offset(3)(7)]
